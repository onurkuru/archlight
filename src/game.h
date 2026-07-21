/* ARCLIGHT - world state, player physics, camera. */
#ifndef ARC_GAME_H
#define ARC_GAME_H

#include <stdint.h>
#include "gfx.h"

#define TILE 16

/* Input is collected once and passed down, so replay/ghost recording later
 * only has to capture this struct. */
typedef struct {
    int8_t mx;                  /* -1 / 0 / +1 */
    uint8_t jump, jump_edge;
    uint8_t dash, dash_edge;
    uint8_t down;
    uint8_t tether, tether_edge;
} arc_input;

typedef enum {
    PS_GROUND, PS_AIR, PS_WALL, PS_DASH, PS_STOMP, PS_TETHER, PS_DEAD
} arc_pstate;

typedef struct {
    float x, y, vx, vy;
    float w, h;
    int   facing;               /* -1 left, +1 right */
    arc_pstate state;

    float coyote, jump_buffer;  /* seconds remaining */
    float dash_time, dash_cd;
    int   air_dash;             /* dashes left in this airtime */
    int   wall_dir;             /* which way the wall is, when PS_WALL */
    float wall_stick;

    float charge;               /* 0..100, the GDD's flow economy */
    int   plates;               /* health */
    float invuln, dead_time;

    float tether_x, tether_y, tether_len;
    int   tethered;

    float squash;               /* landing squash, drives the only juice we have */
    float apex;                 /* seconds spent near apex, for the hang window */
} arc_player;

typedef enum { E_NONE, E_VOLT, E_ECHO, E_CHECKPOINT, E_EXIT, E_ANCHOR, E_HINT } arc_ent_kind;

typedef struct {
    arc_ent_kind kind;
    float x, y;
    int   taken;
    float bob;
} arc_ent;

#define MAX_ENTS 1024

/* Enemies are obstacles with intent, not health bars (GDD §5) - most die in
 * one hit. Only one archetype exists yet: the Scrapper, a ground drone that
 * charges in a straight line and is the game's designated bounce target. */
typedef enum { EN_SCRAPPER } arc_enemy_kind;

typedef struct {
    arc_enemy_kind kind;
    float x, y, vx;
    float home_x, range;        /* patrol bounds before it has a target */
    int   alive;
    float hit_flash;
    float squash;
} arc_enemy;

#define MAX_ENEMIES 128

typedef struct {
    const char *id, *title;
    int w, h;
    const char *const *rows;

    arc_ent ents[MAX_ENTS];
    int     ent_count;

    arc_enemy enemies[MAX_ENEMIES];
    int       enemy_count;

    float spawn_x, spawn_y;
    float check_x, check_y;

    arc_player p;
    float cam_x, cam_y;

    int   volts, volts_total;
    int   echoes, echoes_total;
    int   deaths;
    float time;
    int   finished;

    float flow;                 /* seconds of unbroken high-speed movement */
    float shake;
    float hitstop;
} arc_world;

void  world_load(arc_world *w, int index);
void  world_reset_to_checkpoint(arc_world *w);
void  world_update(arc_world *w, const arc_input *in, float dt);
void  world_render(arc_world *w, const arc_texture *atlas, const arc_shader *sprite);

int   world_tile_solid(const arc_world *w, int tx, int ty);

#endif
