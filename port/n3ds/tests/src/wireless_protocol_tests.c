/* Host-native transport tests: no SDK, radio, maps or Xbox headers required. */
#include "engine_wireless.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct packet
{
    unsigned char data[WL_PACKET];
    size_t size;
    uint16_t from, to;
};
struct bus
{
    struct packet packets[512];
    unsigned count, drop_kind, drops;
    struct wl_core *nodes[5];
};
struct sender
{
    struct bus *bus;
    uint16_t node;
};
static int send_packet(void *user, uint16_t to, const void *data, size_t n)
{
    struct sender *s = user;
    struct bus *b = s->bus;
    if (((const unsigned char *)data)[5] == b->drop_kind && b->drops)
    {
        --b->drops;
        return 0;
    }
    assert(b->count < 512);
    struct packet *p = &b->packets[b->count++];
    memcpy(p->data, data, n);
    p->size = n;
    p->from = s->node;
    p->to = to;
    return 0;
}
static void deliver(struct bus *b)
{
    while (b->count)
    {
        /* Reverse delivery deliberately exercises reordering. */
        struct packet p = b->packets[--b->count];
        assert(p.to < 5 && b->nodes[p.to]);
        wl_core_receive(b->nodes[p.to], p.from, p.data, p.size);
        if (p.data[5] == 3)
            wl_core_receive(b->nodes[p.to], p.from, p.data, p.size); /* duplicate DATA */
    }
}
static void tick(struct bus *b, uint32_t time)
{
    for (unsigned i = 1; i < 5; ++i)
        if (b->nodes[i])
            wl_core_tick(b->nodes[i], time);
    deliver(b);
}
static void four_console_session(void)
{
    struct wl_core peers[4];
    struct sender senders[4];
    static struct bus bus;
    int outgoing[4], incoming[4];
    unsigned char data[2048], received[2048];
    for (unsigned i = 0; i < 4; ++i)
    {
        senders[i] = (struct sender){&bus, (uint16_t)(i + 1)};
        bus.nodes[i + 1] = &peers[i];
        wl_core_init(&peers[i], send_packet, &senders[i], 90 + i);
        wl_core_link(&peers[i], i + 1, 1234, 15);
    }
    int listener = wl_core_open(&peers[0], 1);
    assert(!wl_core_bind(&peers[0], listener, 5150));
    assert(!wl_core_listen(&peers[0], listener));
    for (unsigned i = 0; i < 4; ++i)
    {
        outgoing[i] = wl_core_open(&peers[i], 1);
        assert(wl_core_connect(&peers[i], outgoing[i], 1, 5150) == (i ? -23 : 0));
        deliver(&bus);
        incoming[i] = wl_core_accept(&peers[0], listener);
        assert(incoming[i] > 0);
        memset(data, (int)i, sizeof(data));
        assert(wl_core_write(&peers[i], outgoing[i], data, sizeof(data), 0, 0) == sizeof(data));
    }
    for (unsigned t = 10; t < 100; t += 10)
        tick(&bus, t);
    for (unsigned i = 0; i < 4; ++i)
    {
        memset(data, (int)i, sizeof(data));
        assert(wl_core_read(&peers[0], incoming[i], received, sizeof(received), NULL, NULL) ==
               sizeof(received));
        assert(!memcmp(data, received, sizeof(data)));
    }
    /* A datagram with a stream's valid tuple/cookie must not corrupt its bytes. */
    assert(wl_core_write(&peers[1], outgoing[1], "test", 4, 0, 0) == 4);
    wl_core_tick(&peers[1], 100);
    assert(bus.count == 1);
    struct packet bad = bus.packets[0];
    bad.data[5] = 6;
    wl_core_receive(&peers[0], 2, bad.data, bad.size);
    assert(wl_core_read(&peers[0], incoming[1], received, sizeof(received), NULL, NULL) ==
           WL_AGAIN);
    deliver(&bus);
    assert(wl_core_read(&peers[0], incoming[1], received, sizeof(received), NULL, NULL) == 4);
    for (unsigned i = 0; i < 4; ++i)
        wl_core_dispose(&peers[i]);
}
int main(void)
{
    struct wl_core host, client;
    struct bus bus = {0};
    struct sender senders[2] = {{&bus, 1}, {&bus, 2}};
    bus.nodes[1] = &host;
    bus.nodes[2] = &client;
    wl_core_init(&host, send_packet, &senders[0], 42);
    wl_core_init(&client, send_packet, &senders[1], 123);
    /* The menu opens endpoints before an asynchronous beacon scan completes. */
    int discovery = wl_core_open(&client, 0);
    assert(discovery > 0);
    client.session = 77;
    wl_core_link(&host, 1, 77, 3);
    wl_core_link(&client, 2, 77, 3);
    assert(!client.ep[discovery - 1].dead);
    wl_core_close(&client, discovery);
    int listener = wl_core_open(&host, 1), out = wl_core_open(&client, 1);
    assert(listener > 0 && out > 0);
    assert(!wl_core_bind(&host, listener, 5150));
    assert(!wl_core_listen(&host, listener));
    bus.drop_kind = 1;
    bus.drops = 1;
    assert(wl_core_connect(&client, out, 1, 5150) == -23);
    tick(&bus, 250);
    assert(client.ep[out - 1].connected);
    int in = wl_core_accept(&host, listener);
    assert(in > 0 && wl_core_accept(&host, listener) == WL_AGAIN);
    unsigned char original[WL_QUEUE], received[WL_QUEUE];
    for (unsigned i = 0; i < sizeof(original); ++i)
        original[i] = (unsigned char)(i * 71 + i / 13);
    assert(wl_core_write(&client, out, original, sizeof(original), 0, 0) == sizeof(original));
    assert(wl_core_write(&client, out, original, 1, 0, 0) == WL_FULL);
    bus.drop_kind = 3;
    bus.drops = 2;
    unsigned total = 0;
    for (unsigned t = 260; t < 5000 && total < sizeof(original); t += 20)
    {
        if (t == 560)
        {
            bus.drop_kind = 4;
            bus.drops = 2;
        }
        tick(&bus, t);
        int n = wl_core_read(&host, in, received + total, sizeof(received) - total, NULL, NULL);
        if (n > 0)
            total += n;
        else
            assert(n == WL_AGAIN);
    }
    assert(total == sizeof(original) && !memcmp(original, received, total));
    assert(client.retries > 0);
    assert(wl_core_read(&host, in, received, 1, NULL, NULL) == WL_AGAIN);
    /* Reuse wrapped queues and split reads; reliable fragments preserve bytes. */
    tick(&bus, 5100);
    assert(wl_core_write(&client, out, original, 2048, 0, 0) == 2048);
    for (unsigned t = 5120; t < 5400; t += 20)
        tick(&bus, t);
    assert(wl_core_read(&host, in, received, 7, NULL, NULL) == 7);
    assert(wl_core_read(&host, in, received + 7, 2041, NULL, NULL) == 2041);
    assert(!memcmp(original, received, 2048));
    int hu = wl_core_open(&host, 0), cu = wl_core_open(&client, 0);
    assert(!wl_core_bind(&host, hu, 5150) && !wl_core_bind(&client, cu, 5151));
    assert(wl_core_write(&client, cu, original, 400, 0xffff, 5150) == 400);
    deliver(&bus);
    uint16_t from = 0, port = 0;
    assert(wl_core_read(&host, hu, received, 400, &from, &port) == 400);
    assert(from == 2 && port == 5151 && !memcmp(original, received, 400));
    assert(wl_core_write(&client, cu, original, WL_MTU + 1, 1, 5150) == WL_BAD);
    /* Wrong session, corrupt length and truncation are discarded. */
    assert(wl_core_write(&client, cu, original, 400, 1, 5150) == 400);
    assert(bus.count == 1);
    struct packet bad = bus.packets[0];
    bad.data[8] ^= 1;
    wl_core_receive(&host, 2, bad.data, bad.size);
    bad = bus.packets[0];
    bad.data[6] ^= 1;
    wl_core_receive(&host, 2, bad.data, bad.size);
    wl_core_receive(&host, 2, bad.data, 4);
    assert(wl_core_read(&host, hu, received, 400, NULL, NULL) == WL_AGAIN);
    assert(host.rejected >= 3);
    deliver(&bus);
    assert(wl_core_read(&host, hu, received, 400, NULL, NULL) == 400);
    /* Local host-client passes through identical endpoint and parser logic. */
    int loop = wl_core_open(&host, 1);
    assert(!wl_core_connect(&host, loop, 1, 5150));
    int accepted = wl_core_accept(&host, listener);
    assert(accepted > 0);
    assert(wl_core_write(&host, loop, "abc", 3, 0, 0) == 3);
    wl_core_tick(&host, 5500);
    assert(wl_core_read(&host, accepted, received, 3, NULL, NULL) == 3 &&
           !memcmp(received, "abc", 3));
    assert(host.ep[loop - 1].tx_size == 0);
    wl_core_close(&host, accepted);
    assert(wl_core_read(&host, loop, received, 3, NULL, NULL) == WL_LOST);
    wl_core_link(&host, 1, 77, 1);
    assert(wl_core_read(&host, in, received, 1, NULL, NULL) == WL_LOST);
    wl_core_close(&client, out);
    deliver(&bus);
    wl_core_link(&host, 1, 77, 3);
    out = wl_core_open(&client, 1);
    assert(out > 0);
    assert(wl_core_connect(&client, out, 1, 5150) == -23);
    deliver(&bus);
    in = wl_core_accept(&host, listener);
    assert(in > 0);
    assert(wl_core_write(&client, out, "reconnect", 9, 0, 0) == 9);
    tick(&bus, 5600);
    assert(wl_core_read(&host, in, received, 9, NULL, NULL) == 9 &&
           !memcmp(received, "reconnect", 9));
    /* A silent peer eventually fails rather than keeping the game in a loop. */
    wl_core_tick(&client, 40000);
    assert(client.ep[out - 1].dead);
    wl_core_dispose(&host);
    wl_core_dispose(&client);
    for (int i = 0; i < WL_ENDPOINTS; ++i)
        assert(!host.ep[i].used && !client.ep[i].used);
    four_console_session();
    puts("PASS: loss/retry, reordering, fragmentation, queue wrap, datagrams, malformed packets, "
         "loopback, reconnect, disconnect and four-console streams");
    return 0;
}
