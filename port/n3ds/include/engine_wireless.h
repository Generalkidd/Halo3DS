#ifndef HALO_ENGINE_WIRELESS_H
#define HALO_ENGINE_WIRELESS_H
#include <stdint.h>
#include <stddef.h>

/* Session-local endpoints, not IP sockets. Return values match Halo transport. */
enum
{
    WL_ENDPOINTS = 12,
    WL_MTU = 1400,
    WL_PACKET = 1432,
    WL_QUEUE = 32768,
    WL_DATAGRAM = 4096,
    WL_AGAIN = -4,
    WL_LOST = -3,
    WL_FULL = -11,
    WL_BAD = -7
};
typedef int (*wl_send_fn)(void *, uint16_t, const void *, size_t);
struct wl_endpoint
{
    uint8_t used, stream, listening, connected, pending, dead;
    uint16_t port, peer, peer_port, parent;
    uint32_t cookie, sent, acknowledged, received, last_send, last_receive;
    unsigned tx_head, tx_size, rx_head, rx_size;
    unsigned char *tx, *rx;
};
struct wl_core
{
    struct wl_endpoint ep[WL_ENDPOINTS];
    uint32_t session, next_cookie, now;
    uint16_t node, next_port, peers;
    wl_send_fn send;
    void *user;
    uint32_t packets_sent, packets_received, rejected, retries;
};
void wl_core_init(struct wl_core *, wl_send_fn, void *, uint32_t);
void wl_core_dispose(struct wl_core *);
void wl_core_link(struct wl_core *, uint16_t, uint32_t, uint16_t);
int wl_core_open(struct wl_core *, int stream);
int wl_core_bind(struct wl_core *, int, uint16_t);
int wl_core_listen(struct wl_core *, int);
int wl_core_connect(struct wl_core *, int, uint16_t, uint16_t);
int wl_core_accept(struct wl_core *, int);
int wl_core_read(struct wl_core *, int, void *, unsigned, uint16_t *, uint16_t *);
int wl_core_write(struct wl_core *, int, const void *, unsigned, uint16_t, uint16_t);
void wl_core_close(struct wl_core *, int);
int wl_core_readable(struct wl_core *, int);
void wl_core_receive(struct wl_core *, uint16_t, const void *, size_t);
void wl_core_tick(struct wl_core *, uint32_t);

/* libctru owner. No engine state is accessed by the radio worker. */
int n3ds_wireless_search(void);
int n3ds_wireless_host(void);
void n3ds_wireless_stop(void);
int n3ds_wireless_active(void);
unsigned n3ds_wireless_node(void);
unsigned n3ds_wireless_identity(void);
unsigned n3ds_wireless_session(void);
int n3ds_wireless_open(int);
int n3ds_wireless_bind(int, unsigned);
int n3ds_wireless_listen(int);
int n3ds_wireless_connect(int, unsigned, unsigned);
int n3ds_wireless_accept(int);
int n3ds_wireless_read(int, void *, unsigned, unsigned *, unsigned *);
int n3ds_wireless_write(int, const void *, unsigned, unsigned, unsigned);
int n3ds_wireless_readable(int);
int n3ds_wireless_connected(int);
int n3ds_wireless_address(int, unsigned *, unsigned *);
void n3ds_wireless_close(int);
#endif
