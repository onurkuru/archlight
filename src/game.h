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
    uint8_t attack, attack_edge;
    uint8_t pulse, pulse_edge;
} arc_input;

typedef enum {
    PS_GROUND, PS_AIR, PS_WALL, PS_DASH, PS_STOMP, PS_TETHER, PS_DEAD
} arc_pstate;

typedef enum { ANIM_IDLE, ANIM_RUN, ANIM_AIR } arc_anim_cat;

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

    /* Melee is an overlay on the state machine, not a state of its own: you
       keep full control of run, jump and fall while swinging. That is the
       whole point - an attack that locks you in place would break the
       "never break the run" pillar (GDD design pillars). */
    float atk_time;             /* seconds into the current swing, <0 = idle */
    float atk_chain_win;        /* time left to chain into the next hit */
    int   atk_chain;            /* 0,1,2 - which hit of the 3-hit chain */
    int   atk_hit;              /* this swing has already connected */

    float pulse_cd;             /* Pulse cooldown */

    float squash;               /* landing squash, drives the only juice we have */
    float apex;                 /* seconds spent near apex, for the hang window */

    arc_anim_cat anim_cat;
    float anim_t;
} arc_player;

typedef enum { E_NONE, E_VOLT, E_ECHO, E_CHECKPOINT, E_EXIT, E_ANCHOR, E_HINT,
               E_TERMINAL, E_NODE } arc_ent_kind;

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
typedef enum { EN_SCRAPPER, EN_COP, EN_TURRET, EN_WARDEN } arc_enemy_kind;

/* What comes out when you cut one open. Keyed on the archetype rather than
 * stored per enemy, so adding an organic enemy is one line here and nothing
 * else: Meridian machines bleed incandescent coolant, people bleed red. */
uint32_t enemy_fluid(arc_enemy_kind k);

typedef struct {
    arc_enemy_kind kind;
    float x, y, vx;
    float home_x, home_y, range; /* patrol bounds before it has a target */
    int   alive;
    float hit_flash;
    float squash;

    /* Death is played out, not instant: the drone is knocked off its hover,
       tumbles, and pops. Killing something has to be worth watching. */
    float death_t;              /* >0 while the death animation runs */
    float dvx, dvy;             /* knock-off velocity during the tumble */

    /* Downed by Pulse (GDD §3.1 verb 7). Hacking is access, not a kill: the
       drone drops out of the fight, and then it reboots. */
    float hacked;               /* seconds until it comes back online */
    float glitch;               /* visual corruption, loudest at the moment of entry */

    float shoot_cd;             /* seconds until this one can fire again */
    float telegraph;            /* >0 while winding up a shot - the tell */

    /* Popcorn dies in one hit (GDD §5) and carries hp 1. Only a boss carries
       more, which is the entire difference between the two - no elite tier,
       no health-bar mooks. */
    int   hp, hp_max;
    int   phase;                /* boss phase, 0..2 */
    int   variant;              /* which district boss: 0..4 */
    float state_t;              /* seconds in the current boss beat */
} arc_enemy;

/* Enemy fire. Slow, visible, and blocked by geometry, because the counter to
 * a bullet in this game is movement: you are meant to dash it, drop under it
 * or put a wall in the way, never to trade damage with it. */
typedef struct {
    float x, y, vx, vy;
    float life;
    int   alive;
    int   friendly;    /* Nine's rounds pass through her and kill theirs */
    int   target;      /* enemy index a friendly round homes onto, -1 = dumb */
} arc_shot;

#define MAX_SHOTS 64

#define MAX_ENEMIES 128

/* Impact debris and sparks. A flat ring buffer - particles are pure garnish,
 * so the oldest is always the right one to recycle. */
typedef struct {
    float x, y, vx, vy;
    float life, life0;
    float size;
    float grav;
    uint32_t col;
    int   glow;                 /* drawn in the additive pass */
    /* 1 = looking for a surface to land on, 2 = landed and fading as a decal.
       Spray that sticks is what makes the city feel like it keeps a record of
       the fight - and on wet asphalt under bloom it costs almost nothing. */
    int   stick;
} arc_particle;

#define MAX_PARTICLES 256

/* A platform that moves. The one thing the levels had none of: every surface
 * in the game was nailed down, which is most of why twenty maps read as one.
 * Collision is one-way like a static ledge - you land on top and you get
 * carried - because side-blocking a moving solid is a whole physics problem
 * for a feature whose point is rhythm, not geometry. */
typedef struct {
    float x, y;             /* current position, world px */
    float x0, y0;           /* one end of the path */
    float dx, dy;           /* travel to the other end */
    float t, speed;         /* phase 0..1 and how fast it walks it */
    int   tiles;            /* width, in tiles */
} arc_mover;

#define MAX_MOVERS 48

/* A Meridian containment beam. The first actual hazard: it hurts to touch, it
 * cycles on and off so it is a rhythm rather than a wall, and Pulse drops it
 * for a few seconds - which is the GDD's own description of verb 7 ("kills
 * lasers") finally doing something. */
typedef struct {
    float x, y;             /* emitter, world px */
    float len;              /* beam length downward, to the first solid */
    float period, phase;    /* on/off cycle */
    float down;             /* seconds of Pulse downtime left */
} arc_laser;

#define MAX_LASERS 32

typedef struct {
    const char *id, *title;
    int w, h;
    const char *const *rows;
    int district;          /* selects the parallax set and grade */
    int index, level_count; /* where this run sits in the campaign */

    arc_ent ents[MAX_ENTS];
    int     ent_count;

    arc_enemy enemies[MAX_ENEMIES];
    int       enemy_count;

    arc_shot  shots[MAX_SHOTS];

    arc_mover movers[MAX_MOVERS];
    int       mover_count;
    int       ride;             /* index of the mover Nine is standing on, -1 */

    arc_laser lasers[MAX_LASERS];
    int       laser_count;

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

    arc_particle parts[MAX_PARTICLES];
    int          part_head;

    /* Expanding impact ring, world space. One is enough - hits that land close
       enough together to overlap read better as a single bigger ring. */
    float ring_t, ring_x, ring_y;

    /* The Pulse wavefront: its own ring, in its own colour, because an EMP
       going out has to read as a different event from something dying. */
    float emp_t, emp_x, emp_y;

    /* The opening owns the screen: the HUD would otherwise show through the
       story cards and give away numbers before the run has started. */
    int   hide_hud;

    /* The hack gate: Pulse a terminal, then run the lit nodes before the
       window closes and the door unbolts. The puzzle input is movement itself
       - a hack that froze the game in a minigame would break the run pillar,
       so the minigame IS the run. */
    float hack_t;               /* seconds left in the open window, 0 = idle */
    int   door_open;
    float door_x, door_y;       /* centre of the door, for the unlock impact */
} arc_world;

void  world_load(arc_world *w, int index);
void  world_reset_to_checkpoint(arc_world *w);
void  world_update(arc_world *w, const arc_input *in, float dt);
void  world_render(arc_world *w, const arc_texture *atlas, const arc_texture *city,
                   const arc_shader *sprite);

int   world_tile_solid(const arc_world *w, int tx, int ty);
float world_rain(const arc_world *w);   /* per-district rain intensity */

int   world_boss_active(const arc_world *w);
int   world_intro_cards(void);
void  world_render_intro(arc_world *w, const arc_texture *atlas,
                         const arc_shader *sprite, int card, float t);

#endif
