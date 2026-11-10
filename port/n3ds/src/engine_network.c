/* Halo transport API over session-local UDS endpoints. Xbox addresses/keys are
 * identifiers within this adapter, never credentials for Xbox networking. */
#include "cseries.h"
#include <xtl.h>
#include "bungie_net/network/transport.h"
#include "bungie_net/network/transport_endpoint_winsock.h"
#include "engine_wireless.h"
#include "engine_network.h"
#include "engine_cache.h"
#include "main/main.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "interface/ui_widget.h"
#include "networking/network_game_globals.h"
#include "networking/network_game_manager.h"
#include "networking/network_server_manager.h"
#include <stdio.h>

/* Kevin Boggs: n3ds_campaign_network_blocked - keep optional objects out of co-op state. */
int n3ds_campaign_network_blocked(void)
{
 return n3ds_network_coop_menu() || n3ds_network_coop_selected() ||
  game_connection()==_game_connection_network_client || game_connection()==_game_connection_network_server;
}
static unsigned coop_pending, coop_previous[16];
static int coop_lobby_request;
static int coop_menu;
void n3ds_network_set_coop_menu(int cooperative) { coop_menu=!!cooperative; }
int n3ds_network_coop_menu(void) { return coop_menu; }
void n3ds_network_coop_reset(void)
{
    coop_pending=0;
    memset(coop_previous,0,sizeof(coop_previous));
    coop_lobby_request=0;
}
void n3ds_network_coop_request(unsigned command)
{
    if(n3ds_network_coop_active()) coop_pending |= command & N3DS_COOP_COMMANDS;
}
unsigned n3ds_network_coop_pending(void) { return coop_pending; }
unsigned n3ds_network_coop_consume(unsigned player,unsigned flags,int local)
{
    unsigned commands=flags & N3DS_COOP_COMMANDS;
    if(player<16 && n3ds_network_coop_active())
    {
        unsigned fresh=commands & ~coop_previous[player];
        coop_previous[player]=commands;
        if(local) coop_pending &= ~commands;
        if(fresh & N3DS_COOP_RESTART) n3ds_network_coop_return_to_lobby(0);
        else if(fresh & N3DS_COOP_REVERT) main_revert_map();
        else if(fresh & N3DS_COOP_SKIP) main_skip_cinematic();
    }
    return flags & ~N3DS_COOP_COMMANDS;
}
void n3ds_network_coop_return_to_lobby(int advance)
{
    coop_lobby_request=advance ? 2 : 1;
}
void n3ds_network_coop_frame(void)
{
    if(coop_lobby_request)
    {
        struct network_game_server *server=global_network_game_server_get();
        int advance=coop_lobby_request==2;
        coop_lobby_request=0;
        if(server)
        {
            int level=n3ds_network_campaign_level(main_get_multiplayer_map_name());
            network_game_server_switch_to_postgame(server);
            if(!network_game_server_reset_to_pregame(server)) network_game_abort();
            else if(advance && level>=0 && level<9)
            {
                const char *next=main_get_solo_level_name(level+1);
                if(n3ds_engine_map_installed(next))
                {
                    main_set_multiplayer_map_name(next);
                    network_game_server_change_map_name(server,next);
                }
            }
        }
    }
    /* A co-op checkpoint contains both players. End a broken session instead
     * of continuing with a disconnected player who could reappear on revert. */
    if(n3ds_network_coop_active())
    {
        extern int n3ds_network_coop_player_count(void);
        if(n3ds_network_coop_player_count()!=2) network_game_abort();
    }
}

int n3ds_network_campaign_level(const char *map)
{
    static const char *const levels[] = {"a10", "a30", "a50", "b30", "b40",
                                         "c10", "c20", "c40", "d20", "d40"};
    if (!map) return -1;
    for (int i = 0; i < 10; ++i)
    {
        char path[32];
        snprintf(path, sizeof(path), "levels\\%s\\%s", levels[i], levels[i]);
        if (!strcmp(map, path)) return i;
    }
    return -1;
}

int n3ds_network_coop_active(void)
{
    return player_spawn_count == 2 && !main_menu_is_active() && !game_engine_running() &&
           (game_connection() == _game_connection_network_client ||
            game_connection() == _game_connection_network_server);
}

const char *n3ds_network_campaign_label(int level)
{
    static const char *const names[] = {
        "The Pillar of Autumn", "Halo", "The Truth and Reconciliation",
        "The Silent Cartographer", "Assault on the Control Room",
        "343 Guilty Spark", "The Library", "Two Betrayals", "Keyes", "The Maw"
    };
    return level >= 0 && level < 10 ? names[level] : "Campaign";
}

int n3ds_network_coop_selected(void)
{
    extern struct network_game_server *global_network_game_server_get(void);
    return global_network_game_server_get() &&
           n3ds_network_campaign_level(main_get_multiplayer_map_name()) >= 0;
}

boolean transport_initialized;
static unsigned int nonce[2];
struct transport_endpoint_set
{
    struct transport_endpoint *items[WL_ENDPOINTS];
    short count, cursor, capacity;
};
int n3ds_socket_set_nonblocking(unsigned int, unsigned int);
int n3ds_socket_bad_argument(int);
void n3ds_log(const char *);
static unsigned address_node(unsigned long address)
{
    if (address == 0x7f000001)
        return n3ds_wireless_node();
    if (address == 0xffffffff)
        return 0xffff;
    return (address & 0xffff0000) == 0x0a330000 ? address & 0xffff : 0;
}
static unsigned node_address(unsigned node)
{
    return node == n3ds_wireless_node() ? 0x7f000001 : 0x0a330000 | node;
}
static void address_set(struct transport_address *a, unsigned node, unsigned port)
{
    memset(a, 0, sizeof(*a));
    a->address.ipv4_address = node_address(node);
    a->address_length = 4;
    a->port = port;
}
short transport_initialize(void)
{
    if (!n3ds_wireless_active())
        return _transport_error_not_initialized;
    nonce[0] = n3ds_wireless_identity();
    nonce[1] = HALO_N3DS_CHECKPOINT_BUILD;
    transport_initialized = TRUE;
    return 0;
}
short transport_dispose(void)
{
    boolean was = transport_initialized;
    n3ds_wireless_stop();
    transport_initialized = FALSE;
    return was ? 0 : _transport_error_not_initialized;
}
boolean transport_network_available(void)
{
    return n3ds_wireless_active();
}
void net_startup_debug(void)
{
}
void transport_get_nonce(void *p, long n)
{
    if (p && n == 8)
        memcpy(p, nonce, 8);
}
boolean transport_nonce_is_equal(const void *a, const void *b)
{
    return a && b && !memcmp(a, b, 8);
}
boolean transport_is_nonce(const void *p, long n)
{
    return n == 8 && transport_nonce_is_equal(p, nonce);
}
short transport_server_initialize(void)
{
    return transport_initialized ? 0 : _transport_error_not_initialized;
}
short transport_server_terminate(void)
{
    return 0;
}
void transport_client_stop(void)
{
}
XNADDR *transport_get_xnaddr(XNADDR *a)
{
    memset(a, 0, sizeof(*a));
    a->bSizeOfStruct = sizeof(*a);
    a->ina.s_addr = 0x0a330000 | n3ds_wireless_node();
    return a;
}
XNKID transport_get_key_id(void)
{
    XNKID id = {{0}};
    unsigned s = n3ds_wireless_session();
    memcpy(id.ab, "H3DS", 4);
    memcpy(id.ab + 4, &s, 4);
    return id;
}
XNKEY transport_get_key(void)
{
    XNKEY key = {{0}};
    unsigned b = HALO_N3DS_CHECKPOINT_BUILD;
    memcpy(key.ab, &b, 4);
    return key;
}
INT WSAAPI XNetRegisterKey(const XNKID *id, const XNKEY *key)
{
    XNKID expected = transport_get_key_id();
    XNKEY version = transport_get_key();
    return transport_initialized && id && key && !memcmp(id, &expected, sizeof(*id)) &&
                   !memcmp(key, &version, sizeof(*key))
               ? 0
               : WSANOTINITIALISED;
}
INT WSAAPI XNetUnregisterKey(const XNKID *id)
{
    XNKID expected = transport_get_key_id();
    return transport_initialized && id && !memcmp(id, &expected, sizeof(*id)) ? 0
                                                                              : WSANOTINITIALISED;
}
DWORD WSAAPI XNetGetEthernetLinkStatus(void)
{
    return n3ds_wireless_active() ? XNET_ETHERNET_LINK_ACTIVE : 0;
}
INT WSAAPI XNetXnAddrToInAddr(const XNADDR *a, const XNKID *id, IN_ADDR *out)
{
    XNKID expected = transport_get_key_id();
    if (!transport_initialized)
        return WSANOTINITIALISED;
    if (!a || !id || !out || memcmp(id, &expected, sizeof(*id)) ||
        (a->ina.s_addr & 0xffff0000) != 0x0a330000 || (a->ina.s_addr & 0xffff) < 1 ||
        (a->ina.s_addr & 0xffff) > 4)
        return WSAEINVAL;
    out->s_addr = a->ina.s_addr;
    return 0;
}
void transport_client_start(const XNADDR *a, const XNKEY *key, const XNKID *id, word port,
                            struct transport_address *out)
{
    IN_ADDR result;
    memset(out, 0, sizeof(*out));
    if (!XNetRegisterKey(id, key) && !XNetXnAddrToInAddr(a, id, &result))
        address_set(out, result.s_addr & 0xffff, port);
}
struct transport_endpoint *create_transport_endpoint(long type)
{
    if (!transport_initialized || (type != 0x11 && type != 0x12))
        return NULL;
    int id = n3ds_wireless_open(type == 0x12);
    if (id < 0)
        return NULL;
    struct transport_endpoint *e = calloc(1, sizeof(*e));
    if (!e)
    {
        n3ds_wireless_close(id);
        return NULL;
    }
    e->socket = id;
    e->type = type;
    return e;
}
void disconnect_endpoint(struct transport_endpoint *e)
{
    if (e)
    {
        n3ds_wireless_close(e->socket);
        e->socket = -1;
        e->flags = 0;
    }
}
void delete_transport_endpoint(struct transport_endpoint *e)
{
    if (e)
    {
        disconnect_endpoint(e);
        free(e);
    }
}
short bind_endpoint(struct transport_endpoint *e, struct transport_address *a)
{
    return e && a && a->address_length == 4 ? n3ds_wireless_bind(e->socket, a->port) : WL_BAD;
}
short listen_endpoint(struct transport_endpoint *e)
{
    if (!e)
        return WL_BAD;
    int r = n3ds_wireless_listen(e->socket);
    if (!r)
        e->flags |= 2;
    return r;
}
short connect_endpoint(struct transport_endpoint *e, const struct transport_address *a)
{
    if (!e || !a || a->address_length != 4)
        return WL_BAD;
    if (e->socket < 0)
    {
        e->socket = n3ds_wireless_open(e->type == 0x12);
        if (e->socket < 0)
            return e->socket;
    }
    int r = n3ds_wireless_connect(e->socket, address_node(a->address.ipv4_address), a->port);
    /* Completion remains visible through endpoint_connected(); never spin. */
    return r == -23 ? 0 : r;
}
short connect_endpoint_async(struct transport_endpoint *e, const struct transport_address *a,
                             transport_connect_process_ref *p)
{
    if (p)
        *p = NULL;
    return connect_endpoint(e, a);
}
void cancel_connect_process(transport_connect_process_ref p)
{
    (void)p;
}
long endpoint_connected(const struct transport_endpoint *e)
{
    return e && n3ds_wireless_connected(e->socket) > 0;
}
long endpoint_listening(const struct transport_endpoint *e)
{
    return e && (e->flags & 2);
}
boolean endpoint_blocking(const struct transport_endpoint *e)
{
    return e && !(e->flags & 16);
}
short set_endpoint_blocking(struct transport_endpoint *e, long blocking)
{
    if (!e)
        return WL_BAD;
    if (blocking)
        e->flags &= ~16;
    else
        e->flags |= 16;
    return 0;
}
struct transport_endpoint *accept_endpoint(struct transport_endpoint *listener)
{
    if (!listener)
        return NULL;
    int id = n3ds_wireless_accept(listener->socket);
    if (id < 0)
        return NULL;
    struct transport_endpoint *e = calloc(1, sizeof(*e));
    if (!e)
    {
        n3ds_wireless_close(id);
        return NULL;
    }
    e->socket = id;
    e->type = 0x12;
    e->flags = 1 | 16;
    return e;
}
short reject_endpoint(struct transport_endpoint *e)
{
    struct transport_endpoint *a = accept_endpoint(e);
    if (!a)
        return WL_AGAIN;
    delete_transport_endpoint(a);
    return 0;
}
long read_from_endpoint(struct transport_endpoint *e, void *b, long n, struct transport_address *a)
{
    if (!e || n <= 0)
        return WL_BAD;
    unsigned node = 0, port = 0;
    int r = n3ds_wireless_read(e->socket, b, n, &node, &port);
    if (r > 0 && a)
        address_set(a, node, port);
    return r;
}
long read_endpoint(struct transport_endpoint *e, void *b, long n)
{
    return read_from_endpoint(e, b, n, NULL);
}
long write_to_endpoint(struct transport_endpoint *e, const void *b, long n,
                       const struct transport_address *a)
{
    if (!e || n <= 0 || (a && a->address_length != 4))
        return WL_BAD;
    return n3ds_wireless_write(e->socket, b, n, a ? address_node(a->address.ipv4_address) : 0,
                               a ? a->port : 0);
}
long write_endpoint(struct transport_endpoint *e, const void *b, long n)
{
    return write_to_endpoint(e, b, n, NULL);
}
boolean endpoint_readable(struct transport_endpoint *e, word timeout)
{
    (void)timeout;
    return e && n3ds_wireless_readable(e->socket) > 0;
}
short get_endpoint_address(struct transport_endpoint *e, struct transport_address *a)
{
    if (!e || !a)
        return WL_BAD;
    unsigned node, port;
    int r = n3ds_wireless_address(e->socket, &node, &port);
    if (!r)
        address_set(a, node, port);
    return r;
}
struct transport_endpoint_set *create_endpoint_set(short count)
{
    if (count <= 0 || count > WL_ENDPOINTS)
        return NULL;
    struct transport_endpoint_set *s = calloc(1, sizeof(*s));
    if (s)
        s->capacity = count;
    return s;
}
short delete_endpoint_set(struct transport_endpoint_set *s)
{
    free(s);
    return 0;
}
short add_endpoint_to_set(struct transport_endpoint *e, struct transport_endpoint_set *s)
{
    if (!e || !s)
        return WL_BAD;
    for (int i = 0; i < s->count; ++i)
        if (s->items[i] == e)
            return WL_BAD;
    if (s->count >= s->capacity)
        return _transport_error_endpoint_set_full;
    s->items[s->count++] = e;
    return 0;
}
short remove_endpoint_from_set(struct transport_endpoint *e, struct transport_endpoint_set *s)
{
    if (!s)
        return WL_BAD;
    for (int i = 0; i < s->count; ++i)
        if (s->items[i] == e)
        {
            memmove(s->items + i, s->items + i + 1, (s->count - i - 1) * sizeof(e));
            --s->count;
            if (s->cursor > i)
                --s->cursor;
            return 0;
        }
    return _transport_error_endpoint_not_in_set;
}
void rewind_endpoint_set(struct transport_endpoint_set *s)
{
    if (s)
        s->cursor = 0;
}
struct transport_endpoint *get_next_endpoint_from_set(struct transport_endpoint_set *s)
{
    return s && s->cursor < s->count ? s->items[s->cursor++] : NULL;
}
long count_endpoints_in_set(struct transport_endpoint_set *s)
{
    return s ? s->count : 0;
}
short poll_endpoint_set(struct transport_endpoint_set *s, word timeout)
{
    if (!s)
        return WL_BAD;
    for (int i = 0; i < s->count; ++i)
        if (endpoint_readable(s->items[i], timeout))
            return 0;
    return _transport_result_poll_timeout;
}
char const *winsock_error_to_string(long error)
{
    (void)error;
    return "local wireless transport failure";
}

/* Winsock fd_set is a count plus an array, not libctru's bitset. */
int PASCAL __WSAFDIsSet(SOCKET socket, fd_set *set)
{
    unsigned int i;
    if (!set || set->fd_count > FD_SETSIZE)
        return 0;
    for (i = 0; i < set->fd_count; ++i)
        if (set->fd_array[i] == socket)
            return 1;
    return 0;
}
int WSAAPI ioctlsocket(SOCKET socket, long command, u_long *argument)
{
    if (!argument)
        return n3ds_socket_bad_argument(1);
    if ((unsigned long)command != (unsigned long)FIONBIO)
        return n3ds_socket_bad_argument(0);
    return n3ds_socket_set_nonblocking((unsigned int)socket, *argument != 0);
}
