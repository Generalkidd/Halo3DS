/* UDS service owner. Only packet queues cross the worker boundary; game/menu
 * objects stay on the main thread. Scans never hold the queue lock. */
#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine_wireless.h"
#include "engine_network.h"

#define WLAN_ID 0x48414c33u
#define CHANNEL 1
static const char passphrase[] = "Halo3DS local wireless v1";
static struct wl_core core;
static LightLock lock;
static Thread worker;
static udsBindContext binding;
static aptHookCookie app_hook;
static int initialized, hosting, bound, running, healthy;
static uint32_t last_scan, identity;
/* Fixed before starting the worker; different menus discover different rooms. */
static unsigned char network_kind;
void n3ds_log(const char *);
static uint32_t milliseconds(void)
{
    uint64_t ticks = svcGetSystemTick();
    return (uint32_t)(ticks / SYSCLOCK_ARM11 * 1000 +
                      (ticks % SYSCLOCK_ARM11) * 1000 / SYSCLOCK_ARM11);
}
static void app_state(APT_HookType event, void *unused)
{
    (void)unused;
    /* Sleep/HOME cannot preserve the lockstep session. Let the main thread
     * return through
     * Halo's normal disconnect flow after waking. */
    if (event == APTHOOK_ONSUSPEND || event == APTHOOK_ONSLEEP || event == APTHOOK_ONEXIT)
    {
        LightLock_Lock(&lock);
        healthy = 0;
        running = 0;
        LightLock_Unlock(&lock);
    }
}
static void report(const char *operation, Result result)
{
    char s[128];
    snprintf(s, sizeof(s), "WIRELESS: %s result=%08lx", operation, (unsigned long)result);
    n3ds_log(s);
}
static int send_packet(void *user, uint16_t node, const void *data, size_t size)
{
    (void)user;
    if (!bound)
        return WL_AGAIN;
    Result r = udsSendTo(node, CHANNEL, UDS_SENDFLAG_Default, data, size);
    if (UDS_CHECK_SENDTO_FATALERROR(r))
    {
        healthy = 0;
        return WL_LOST;
    }
    return R_FAILED(r) ? WL_AGAIN : 0;
}
static void update_status(void)
{
    udsConnectionStatus status;
    Result r = udsGetConnectionStatus(&status);
    if (R_FAILED(r) || !status.cur_NetworkNodeID || !status.node_bitmask)
    {
        healthy = 0;
        return;
    }
    wl_core_link(&core, status.cur_NetworkNodeID, core.session, status.node_bitmask);
}
static void search_network(void)
{
    void *scan = malloc(0x4000);
    udsNetworkScanInfo *networks = NULL;
    size_t count = 0;
    if (!scan)
        return;
    Result r = udsScanBeacons(scan, 0x4000, &networks, &count, WLAN_ID, network_kind, NULL, false);
    if (R_SUCCEEDED(r))
        for (size_t i = 0; i < count; ++i)
        {
            LightLock_Lock(&lock);
            int keep = running && healthy;
            LightLock_Unlock(&lock);
            if (!keep)
                break;
            if (networks[i].network.total_nodes >= networks[i].network.max_nodes)
                continue;
            uint32_t app[3];
            size_t size = 0;
            r = udsGetNetworkStructApplicationData(&networks[i].network, app, sizeof(app), &size);
            if (R_FAILED(r) || size != sizeof(app) || app[0] != 0x314c4148 ||
                app[1] != HALO_N3DS_CHECKPOINT_BUILD || !app[2])
                continue;
            r = udsConnectNetwork(&networks[i].network, passphrase, sizeof(passphrase), &binding,
                                  UDS_BROADCAST_NETWORKNODEID, UDSCONTYPE_Client, CHANNEL, 0x10000);
            if (R_SUCCEEDED(r))
            {
                LightLock_Lock(&lock);
                bound = 1;
                core.session = app[2];
                update_status();
                LightLock_Unlock(&lock);
                report("connected to host", r);
                break;
            }
            report("join failed", r);
        }
    free(networks);
    free(scan);
}
static void radio_worker(void *unused)
{
    (void)unused;
    while (1)
    {
        LightLock_Lock(&lock);
        int keep = running, scan = healthy && !hosting && !bound;
        LightLock_Unlock(&lock);
        if (!keep)
            break;
        uint32_t now = milliseconds();
        if (scan && now - last_scan >= 2000)
        {
            last_scan = now;
            search_network();
        }
        LightLock_Lock(&lock);
        if (bound && healthy)
        {
            if (udsWaitConnectionStatusEvent(false, false))
                update_status();
            unsigned char data[UDS_DATAFRAME_MAXSIZE];
            for (unsigned i = 0; i < 32 && healthy; ++i)
            {
                size_t size = 0;
                u16 node = 0;
                Result r = udsPullPacket(&binding, data, sizeof(data), &size, &node);
                if (R_FAILED(r))
                {
                    healthy = 0;
                    report("receive failed", r);
                    break;
                }
                if (!size)
                    break;
                wl_core_receive(&core, node, data, size);
            }
            wl_core_tick(&core, now);
        }
        if (!healthy)
            for (unsigned i = 0; i < WL_ENDPOINTS; ++i)
                if (core.ep[i].used)
                    core.ep[i].dead = 1;
        LightLock_Unlock(&lock);
        svcSleepThread(5000000);
    }
}
void n3ds_wireless_stop(void)
{
    if (!initialized)
        return;
    LightLock_Lock(&lock);
    running = 0;
    LightLock_Unlock(&lock);
    if (worker)
    {
        threadJoin(worker, U64_MAX);
        threadFree(worker);
        worker = NULL;
    }
    aptUnhook(&app_hook);
    wl_core_dispose(&core);
    if (bound)
    {
        if (hosting)
            udsDestroyNetwork();
        else
            udsDisconnectNetwork();
        udsUnbind(&binding);
    }
    udsExit();
    initialized = hosting = bound = running = healthy = 0;
    n3ds_log("WIRELESS: session closed; buffers released");
}
static int begin(int host)
{
    unsigned char kind=n3ds_network_coop_menu()?2:1;
    if (initialized && healthy && hosting == host && network_kind==kind)
        return 1;
    n3ds_wireless_stop();
    network_kind=kind;
    LightLock_Init(&lock);
    Result r = udsInit(0x30000, "Halo3DS");
    if (R_FAILED(r))
    {
        report("local wireless unavailable (check wireless switch)", r);
        return 0;
    }
    initialized = healthy = 1;
    hosting = host;
    aptHook(&app_hook, app_state, NULL);
    uint64_t ticks = svcGetSystemTick();
    uint32_t seed = (uint32_t)ticks ^ (uint32_t)(ticks >> 32);
    identity = seed;
    wl_core_init(&core, send_packet, NULL, seed);
    core.now = milliseconds();
    last_scan = core.now - 2000;
    if (host)
    {
        udsNetworkStruct network;
        udsGenerateDefaultNetworkStruct(&network, WLAN_ID, network_kind, kind==2?2:4);
        r = udsCreateNetwork(&network, passphrase, sizeof(passphrase), &binding, CHANNEL, 0x10000);
        if (R_FAILED(r))
        {
            report("host creation failed", r);
            n3ds_wireless_stop();
            return 0;
        }
        bound = 1;
        core.session = seed ? seed : 1;
        uint32_t app[3] = {0x314c4148, HALO_N3DS_CHECKPOINT_BUILD, core.session};
        r = udsSetApplicationData(app, sizeof(app));
        if (R_FAILED(r))
        {
            report("host advertisement failed", r);
            n3ds_wireless_stop();
            return 0;
        }
        update_status();
    }
    running = 1;
    /* A bounded radio poll must still run when rendering saturates Core 0,
     * especially on Original models. It sleeps after each receive batch. */
    worker = threadCreate(radio_worker, NULL, 16 * 1024, 0x2f, -2, false);
    if (!worker || !healthy)
    {
        n3ds_wireless_stop();
        return 0;
    }
    n3ds_log(host ? (kind==2?"WIRELESS: hosting campaign co-op (two consoles)":"WIRELESS: hosting local System Link (up to four consoles)")
                  : (kind==2?"WIRELESS: looking for a nearby co-op host with the same build":"WIRELESS: looking for a nearby System Link host with the same build"));
    return 1;
}
int n3ds_wireless_search(void)
{
    return begin(0);
}
int n3ds_wireless_host(void)
{
    return begin(1);
}
int n3ds_wireless_active(void)
{
    if (!initialized)
        return 0;
    LightLock_Lock(&lock);
    int r = healthy;
    LightLock_Unlock(&lock);
    return r;
}
unsigned n3ds_wireless_node(void)
{
    if (!initialized)
        return 0;
    LightLock_Lock(&lock);
    unsigned n = core.node;
    LightLock_Unlock(&lock);
    return n;
}
unsigned n3ds_wireless_identity(void)
{
    return identity;
}
unsigned n3ds_wireless_session(void)
{
    if (!initialized)
        return 0;
    LightLock_Lock(&lock);
    unsigned n = core.session;
    LightLock_Unlock(&lock);
    return n;
}
#define LOCKED(expr)                                                                               \
    do                                                                                             \
    {                                                                                              \
        if (!initialized)                                                                          \
            return WL_LOST;                                                                        \
        LightLock_Lock(&lock);                                                                     \
        int r = (expr);                                                                            \
        LightLock_Unlock(&lock);                                                                   \
        return r;                                                                                  \
    } while (0)
int n3ds_wireless_open(int stream)
{
    LOCKED(wl_core_open(&core, stream));
}
int n3ds_wireless_bind(int id, unsigned port)
{
    LOCKED(wl_core_bind(&core, id, port));
}
int n3ds_wireless_listen(int id)
{
    LOCKED(wl_core_listen(&core, id));
}
int n3ds_wireless_connect(int id, unsigned node, unsigned port)
{
    LOCKED(wl_core_connect(&core, id, node, port));
}
int n3ds_wireless_accept(int id)
{
    LOCKED(wl_core_accept(&core, id));
}
int n3ds_wireless_readable(int id)
{
    if (!initialized)
        return 0;
    LOCKED(wl_core_readable(&core, id));
}
int n3ds_wireless_connected(int id)
{
    LOCKED(id > 0 && id <= WL_ENDPOINTS && core.ep[id - 1].used && core.ep[id - 1].connected &&
           !core.ep[id - 1].dead);
}
int n3ds_wireless_address(int id, unsigned *node, unsigned *port)
{
    if (!initialized || id <= 0 || id > WL_ENDPOINTS)
        return WL_BAD;
    LightLock_Lock(&lock);
    struct wl_endpoint *e = &core.ep[id - 1];
    int r = e->used ? 0 : WL_BAD;
    if (!r)
    {
        *node = e->peer;
        *port = e->peer_port;
    }
    LightLock_Unlock(&lock);
    return r;
}
int n3ds_wireless_read(int id, void *data, unsigned size, unsigned *node, unsigned *port)
{
    if (!initialized)
        return WL_LOST;
    uint16_t from = 0, p = 0;
    LightLock_Lock(&lock);
    int r = wl_core_read(&core, id, data, size, &from, &p);
    LightLock_Unlock(&lock);
    if (node)
        *node = from;
    if (port)
        *port = p;
    return r;
}
int n3ds_wireless_write(int id, const void *data, unsigned size, unsigned node, unsigned port)
{
    LOCKED(wl_core_write(&core, id, data, size, node, port));
}
void n3ds_wireless_close(int id)
{
    if (initialized)
    {
        LightLock_Lock(&lock);
        wl_core_close(&core, id);
        LightLock_Unlock(&lock);
    }
}
