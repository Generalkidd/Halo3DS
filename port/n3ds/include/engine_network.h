#ifndef HALO_N3DS_ENGINE_NETWORK_H
#define HALO_N3DS_ENGINE_NETWORK_H

/* Campaign identity is taken from the installed scenario path, never a peer's
 * display name. All peers must agree on this before creating game objects. */
int n3ds_network_campaign_level(const char *map);
const char *n3ds_network_campaign_label(int level);
int n3ds_network_coop_selected(void);
/* Menu intent survives profile selection and lobby navigation. */
void n3ds_network_set_coop_menu(int cooperative);
int n3ds_network_coop_menu(void);
struct network_game_server;
int n3ds_network_server_coop_current_difficulty(struct network_game_server *server);
int n3ds_network_server_coop_difficulty(struct network_game_server *server, int difficulty);
int n3ds_network_coop_active(void);
int n3ds_network_bot_options(int count_delta,int difficulty_delta,int *count,int *difficulty);
int n3ds_ui_wireless_lobby_active(void);
/* Reserved high bits travel in the existing reliable player-action stream.
 * They are removed before unit control flags reach gameplay. */
#define N3DS_COOP_SKIP 0x40000000u
#define N3DS_COOP_REVERT 0x80000000u
#define N3DS_COOP_RESTART 0x20000000u
#define N3DS_COOP_COMMANDS (N3DS_COOP_SKIP|N3DS_COOP_REVERT|N3DS_COOP_RESTART)
void n3ds_network_coop_reset(void);
void n3ds_network_coop_request(unsigned command);
unsigned n3ds_network_coop_pending(void);
unsigned n3ds_network_coop_consume(unsigned player, unsigned flags, int local);
void n3ds_network_coop_return_to_lobby(int advance);
void n3ds_network_coop_frame(void);
void n3ds_main_coop_before_tick(void);
void n3ds_main_coop_after_tick(void);

#endif
