/* Bounded, endian-explicit local wireless transport. Reliable byte streams use
 * cumulative ACKs and retransmission; datagrams retain message boundaries.
 * The radio worker owns a core. Tests instantiate independent peers and inject
 * loss/reordering without libctru or game assets. No wire structs are cast. */
#include "engine_wireless.h"
#include <stdlib.h>
#include <string.h>

enum
{
    SYN = 1,
    SYN_ACK,
    DATA,
    ACK,
    CLOSE,
    DATAGRAM,
    HEADER = 32
};
static uint16_t get16(const unsigned char *p)
{
    return p[0] | (uint16_t)p[1] << 8;
}
static uint32_t get32(const unsigned char *p)
{
    return get16(p) | (uint32_t)get16(p + 2) << 16;
}
static void put16(unsigned char *p, uint16_t x)
{
    p[0] = x;
    p[1] = x >> 8;
}
static void put32(unsigned char *p, uint32_t x)
{
    put16(p, x);
    put16(p + 2, x >> 16);
}
static struct wl_endpoint *endpoint(struct wl_core *c, int id)
{
    return id > 0 && id <= WL_ENDPOINTS && c->ep[id - 1].used ? &c->ep[id - 1] : NULL;
}
static void ring_write(unsigned char *p, unsigned head, unsigned size, const void *data, unsigned n)
{
    unsigned start = (head + size) % WL_QUEUE, first = WL_QUEUE - start;
    if (first > n)
        first = n;
    memcpy(p + start, data, first);
    memcpy(p, (const unsigned char *)data + first, n - first);
}
static void ring_read(const unsigned char *p, unsigned head, void *data, unsigned n)
{
    unsigned first = WL_QUEUE - head;
    if (first > n)
        first = n;
    memcpy(data, p + head, first);
    memcpy((unsigned char *)data + first, p, n - first);
}
static int emit(struct wl_core *c, struct wl_endpoint *e, int kind, uint16_t node, uint16_t port,
                uint32_t seq, const void *data, unsigned n)
{
    unsigned char packet[WL_PACKET];
    if (!c->node || !c->session || n > WL_MTU)
        return WL_AGAIN;
    memset(packet, 0, HEADER);
    put32(packet, 0x334c4148);
    packet[4] = 1;
    packet[5] = (unsigned char)kind;
    put16(packet + 6, (uint16_t)n);
    put32(packet + 8, c->session);
    put16(packet + 12, e->port);
    put16(packet + 14, port);
    put32(packet + 16, e->cookie);
    put32(packet + 20, seq);
    put32(packet + 24, e->received);
    if (n)
        memcpy(packet + HEADER, data, n);
    /* Loopback follows exactly the same parser but never enters the radio. */
    if (node == c->node)
    {
        wl_core_receive(c, c->node, packet, HEADER + n);
        return 0;
    }
    int result = c->send ? c->send(c->user, node, packet, HEADER + n) : WL_LOST;
    if (!result)
        ++c->packets_sent;
    return result;
}
void wl_core_init(struct wl_core *c, wl_send_fn send, void *user, uint32_t seed)
{
    memset(c, 0, sizeof(*c));
    c->send = send;
    c->user = user;
    c->next_port = 40000;
    c->next_cookie = seed ? seed : 1;
}
void wl_core_dispose(struct wl_core *c)
{
    for (int i = 1; i <= WL_ENDPOINTS; ++i)
        wl_core_close(c, i);
    c->node = 0;
    c->session = 0;
}
void wl_core_link(struct wl_core *c, uint16_t node, uint32_t session, uint16_t peers)
{
    if (c->session && c->node && (session != c->session || node != c->node))
        for (unsigned i = 0; i < WL_ENDPOINTS; ++i)
            if (c->ep[i].used)
                c->ep[i].dead = 1;
    c->node = node;
    c->session = session;
    c->peers = peers;
    for (unsigned i = 0; i < WL_ENDPOINTS; ++i)
    {
        struct wl_endpoint *e = &c->ep[i];
        if (e->used && e->connected && e->peer && !(peers & (1u << (e->peer - 1))))
            e->dead = 1;
    }
}
int wl_core_open(struct wl_core *c, int stream)
{
    for (unsigned i = 0; i < WL_ENDPOINTS; ++i)
        if (!c->ep[i].used)
        {
            struct wl_endpoint *e = &c->ep[i];
            memset(e, 0, sizeof(*e));
            e->rx = malloc(WL_QUEUE);
            e->tx = stream ? malloc(WL_QUEUE) : NULL;
            if (!e->rx || (stream && !e->tx))
            {
                free(e->rx);
                free(e->tx);
                memset(e, 0, sizeof(*e));
                return WL_FULL;
            }
            e->used = 1;
            e->stream = !!stream;
            e->port = ++c->next_port;
            e->cookie = ++c->next_cookie;
            if (!e->cookie)
                e->cookie = ++c->next_cookie;
            e->last_receive = c->now;
            return (int)i + 1;
        }
    return WL_FULL;
}
int wl_core_bind(struct wl_core *c, int id, uint16_t port)
{
    struct wl_endpoint *e = endpoint(c, id);
    if (!e)
        return WL_BAD;
    if (!port)
        return 0;
    for (unsigned i = 0; i < WL_ENDPOINTS; ++i)
        if (&c->ep[i] != e && c->ep[i].used && c->ep[i].stream == e->stream &&
            c->ep[i].port == port && !c->ep[i].parent)
            return WL_BAD;
    e->port = port;
    return 0;
}
int wl_core_listen(struct wl_core *c, int id)
{
    struct wl_endpoint *e = endpoint(c, id);
    if (!e || !e->stream)
        return WL_BAD;
    e->listening = 1;
    return 0;
}
int wl_core_connect(struct wl_core *c, int id, uint16_t node, uint16_t port)
{
    struct wl_endpoint *e = endpoint(c, id);
    if (!e || !node || node > 16 || !port)
        return WL_BAD;
    e->peer = node;
    e->peer_port = port;
    e->last_receive = c->now;
    if (!e->stream)
    {
        e->connected = 1;
        return 0;
    }
    if (e->connected)
        return 0;
    e->last_send = c->now;
    emit(c, e, SYN, node, port, 0, NULL, 0);
    return e->connected ? 0 : -23;
}
int wl_core_accept(struct wl_core *c, int id)
{
    struct wl_endpoint *e = endpoint(c, id);
    if (!e || !e->listening)
        return WL_BAD;
    for (unsigned i = 0; i < WL_ENDPOINTS; ++i)
        if (c->ep[i].used && c->ep[i].parent == id && c->ep[i].pending)
        {
            c->ep[i].pending = 0;
            return (int)i + 1;
        }
    return WL_AGAIN;
}
void wl_core_close(struct wl_core *c, int id)
{
    struct wl_endpoint *e = endpoint(c, id);
    if (!e)
        return;
    if (e->connected && e->stream && !e->dead)
        emit(c, e, CLOSE, e->peer, e->peer_port, 0, NULL, 0);
    free(e->rx);
    free(e->tx);
    memset(e, 0, sizeof(*e));
    for (unsigned i = 0; i < WL_ENDPOINTS; ++i)
        if (c->ep[i].used && c->ep[i].parent == id && c->ep[i].pending)
            wl_core_close(c, (int)i + 1);
}
int wl_core_readable(struct wl_core *c, int id)
{
    struct wl_endpoint *e = endpoint(c, id);
    if (!e)
        return 0;
    if (e->listening)
    {
        for (unsigned i = 0; i < WL_ENDPOINTS; ++i)
            if (c->ep[i].used && c->ep[i].parent == id && c->ep[i].pending)
                return 1;
        return 0;
    }
    return e->rx_size || e->dead;
}
int wl_core_read(struct wl_core *c, int id, void *buffer, unsigned n, uint16_t *node,
                 uint16_t *port)
{
    struct wl_endpoint *e = endpoint(c, id);
    if (!e || !buffer || !n)
        return WL_BAD;
    if (!e->rx_size)
        return e->dead ? WL_LOST : WL_AGAIN;
    if (e->stream)
    {
        if (n > e->rx_size)
            n = e->rx_size;
        ring_read(e->rx, e->rx_head, buffer, n);
        e->rx_head = (e->rx_head + n) % WL_QUEUE;
        e->rx_size -= n;
        if (node)
            *node = e->peer;
        if (port)
            *port = e->peer_port;
        return (int)n;
    }
    unsigned char header[6];
    ring_read(e->rx, e->rx_head, header, 6);
    unsigned size = get16(header);
    if (n < size)
        return WL_BAD;
    e->rx_head = (e->rx_head + 6) % WL_QUEUE;
    e->rx_size -= 6;
    ring_read(e->rx, e->rx_head, buffer, size);
    e->rx_head = (e->rx_head + size) % WL_QUEUE;
    e->rx_size -= size;
    if (node)
        *node = get16(header + 2);
    if (port)
        *port = get16(header + 4);
    return (int)size;
}
int wl_core_write(struct wl_core *c, int id, const void *buffer, unsigned n, uint16_t node,
                  uint16_t port)
{
    struct wl_endpoint *e = endpoint(c, id);
    if (!e || !buffer || !n)
        return WL_BAD;
    if (e->dead)
        return WL_LOST;
    if (e->stream)
    {
        if (!e->connected)
            return WL_AGAIN;
        /* All-or-nothing enqueue: Halo's caller does not retain partial writes. */
        if (n > WL_QUEUE - e->tx_size)
            return WL_FULL;
        ring_write(e->tx, e->tx_head, e->tx_size, buffer, n);
        e->tx_size += n;
        return (int)n;
    }
    if (n > WL_MTU)
        return WL_BAD;
    if (!node)
    {
        node = e->peer;
        port = e->peer_port;
    }
    if (!node || !port || (node != 0xffff && node > 16))
        return WL_BAD;
    if (node == 0xffff)
    {
        for (unsigned peer = 1; peer <= 16; ++peer)
            if (c->peers & (1u << (peer - 1)))
                emit(c, e, DATAGRAM, peer, port, 0, buffer, n);
        return (int)n;
    }
    int result = emit(c, e, DATAGRAM, node, port, 0, buffer, n);
    return result ? result : (int)n;
}
void wl_core_receive(struct wl_core *c, uint16_t node, const void *buffer, size_t size)
{
    const unsigned char *p = buffer;
    struct wl_endpoint *e = NULL;
    if (!p || size < HEADER || size > WL_PACKET || !node || node > 16 ||
        !(c->peers & (1u << (node - 1))) || get32(p) != 0x334c4148 || p[4] != 1 ||
        get32(p + 8) != c->session || !c->session || get16(p + 6) != size - HEADER ||
        get16(p + 6) > WL_MTU || !get16(p + 12) || !get16(p + 14) || get32(p + 28) != 0 ||
        p[5] < SYN || p[5] > DATAGRAM)
    {
        ++c->rejected;
        return;
    }
    unsigned kind = p[5], n = get16(p + 6);
    uint16_t src = get16(p + 12), dst = get16(p + 14);
    uint32_t cookie = get32(p + 16), seq = get32(p + 20);
    if ((kind != DATA && kind != DATAGRAM && n) || !cookie)
    {
        ++c->rejected;
        return;
    }
    for (unsigned i = 0; i < WL_ENDPOINTS; ++i)
    {
        struct wl_endpoint *candidate = &c->ep[i];
        if (!candidate->used || candidate->dead || candidate->port != dst)
            continue;
        if (kind == DATAGRAM && !candidate->stream &&
            (!candidate->connected || (candidate->peer == node && candidate->peer_port == src)))
        {
            e = candidate;
            break;
        }
        if (kind != DATAGRAM && candidate->stream && !candidate->listening &&
            candidate->peer == node && candidate->peer_port == src && candidate->cookie == cookie)
        {
            e = candidate;
            break;
        }
    }
    if (kind == SYN && !e)
    {
        for (unsigned i = 0; i < WL_ENDPOINTS; ++i)
            if (c->ep[i].used && c->ep[i].listening && c->ep[i].port == dst)
            {
                int id = wl_core_open(c, 1);
                if (id < 0)
                    return;
                e = endpoint(c, id);
                e->parent = (uint16_t)i + 1;
                e->port = dst;
                e->peer = node;
                e->peer_port = src;
                e->cookie = cookie;
                e->connected = 1;
                e->pending = 1;
                break;
            }
    }
    if (!e)
    {
        ++c->rejected;
        return;
    }
    ++c->packets_received;
    e->last_receive = c->now;
    switch (kind)
    {
    case SYN:
        emit(c, e, SYN_ACK, node, src, 0, NULL, 0);
        break;
    case SYN_ACK:
        e->connected = 1;
        break;
    case CLOSE:
        e->dead = 1;
        e->connected = 0;
        break;
    case ACK:
    {
        uint32_t ack = get32(p + 24), delta = ack - e->acknowledged;
        if (delta && delta <= e->tx_size && delta <= e->sent - e->acknowledged)
        {
            e->tx_head = (e->tx_head + delta) % WL_QUEUE;
            e->tx_size -= delta;
            e->acknowledged = ack;
        }
        break;
    }
    case DATA:
        if (!e->connected || !n)
            return;
        if (seq == e->received && n <= WL_QUEUE - e->rx_size)
        {
            ring_write(e->rx, e->rx_head, e->rx_size, p + HEADER, n);
            e->rx_size += n;
            e->received += n;
        }
        emit(c, e, ACK, node, src, 0, NULL, 0);
        break;
    case DATAGRAM:
        if (n && n + 6 <= WL_QUEUE - e->rx_size)
        {
            unsigned char header[6];
            put16(header, n);
            put16(header + 2, node);
            put16(header + 4, src);
            ring_write(e->rx, e->rx_head, e->rx_size, header, 6);
            e->rx_size += 6;
            ring_write(e->rx, e->rx_head, e->rx_size, p + HEADER, n);
            e->rx_size += n;
        }
        break;
    }
}
void wl_core_tick(struct wl_core *c, uint32_t now)
{
    c->now = now;
    for (unsigned i = 0; i < WL_ENDPOINTS; ++i)
    {
        struct wl_endpoint *e = &c->ep[i];
        if (e->used && e->pending && (e->dead || now - e->last_receive > 30000))
        {
            wl_core_close(c, (int)i + 1);
            continue;
        }
        if (!e->used || e->dead || !e->stream || e->listening || !e->peer)
            continue;
        if (now - e->last_receive > 30000)
        {
            e->dead = 1;
            e->connected = 0;
            continue;
        }
        if (!e->connected)
        {
            if (now - e->last_send >= 200)
            {
                emit(c, e, SYN, e->peer, e->peer_port, 0, NULL, 0);
                e->last_send = now;
            }
        }
        else if (e->tx_size)
        {
            int retry = e->sent != e->acknowledged;
            if (retry && now - e->last_send < 100)
                continue;
            unsigned char data[WL_MTU];
            unsigned n = e->tx_size;
            if (n > WL_MTU)
                n = WL_MTU;
            ring_read(e->tx, e->tx_head, data, n);
            /* Commit sent position before synchronous loopback can ACK it. */
            e->sent = e->acknowledged + n;
            e->last_send = now;
            if (retry)
                ++c->retries;
            emit(c, e, DATA, e->peer, e->peer_port, e->acknowledged, data, n);
        }
        else if (now - e->last_send >= 1000)
        {
            emit(c, e, ACK, e->peer, e->peer_port, 0, NULL, 0);
            e->last_send = now;
        }
    }
}
