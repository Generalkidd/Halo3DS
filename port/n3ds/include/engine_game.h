#ifndef N3DS_ENGINE_GAME_H
#define N3DS_ENGINE_GAME_H
/* Original game.c options layout, exposed for staged native startup. */
struct game_options {
    unsigned long flags;
    short code_version;
    short difficulty;
    unsigned long random_seed;
    char map_name[256];
};
/* Allocates the original game globals; does not mark the game active or
 * replace any subsystem initialization performed by game_initialize. */
void game_initialize_globals(void);
#endif
