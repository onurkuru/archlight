#include "game.h"
#include "font.h"
#include "levels.h"
#include "atlas_city.h"
#include "audio.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* ---------------------------------------------------------------- tuning
 * Units are internal pixels and seconds (see GDD §3.1). These numbers are the
 * game; everything else is presentation. Change them here, nowhere else. */
#define RUN_MAX        180.0f
#define RUN_ACCEL     1400.0f
#define RUN_FRICTION  1800.0f
#define AIR_ACCEL      900.0f
#define AIR_DRAG       260.0f

#define GRAV          1300.0f
#define GRAV_FALL     1700.0f
#define FALL_MAX       520.0f
#define JUMP_V         330.0f
#define APEX_BAND       60.0f     /* |vy| under this counts as apex hang */
#define APEX_GRAV_MUL    0.55f

#define COYOTE           0.10f
#define JUMP_BUFFER      0.13f

#define WALL_SLIDE       90.0f
#define WALL_JUMP_X     205.0f
#define WALL_JUMP_Y    -300.0f
#define WALL_STICK       0.12f    /* grace before you peel off a wall */

#define DASH_SPEED     420.0f
#define DASH_TIME        0.14f
#define DASH_CD          0.22f

#define STOMP_V        540.0f
#define STOMP_LAUNCH   240.0f     /* height converted to forward speed */

#define TETHER_RANGE   150.0f
#define TETHER_BOOST     1.25f

#define CHARGE_MAX     100.0f
#define CHARGE_FLOOR    20.0f
#define COST_DASH       20.0f
#define COST_TETHER     15.0f
#define COST_PULSE      30.0f

/* Pulse: the access verb. Short range on purpose - reaching a target should
 * mean closing on it, so hacking costs the same commitment a hit would and a
 * blocked route becomes a movement problem instead of a menu. It disables; it
 * does not destroy. Nothing Nine hacks ever dies of it. */
#define PULSE_RANGE     60.0f
#define PULSE_TIME       3.0f     /* seconds a drone stays down before reboot */
#define PULSE_CD         0.7f
#define PULSE_KNOCK     70.0f     /* shove the wavefront gives as it lands */

/* The hack gate. 6 seconds is generous on purpose: the puzzle is the route
 * through the nodes, not the clock - the clock only exists so a failed run
 * resets instead of leaving half-lit nodes around forever. */
#define HACK_WINDOW      6.0f

/* The camera zoom: world is rendered 2x into the 480x272 internal buffer, so
 * the visible window is 240x136 world px (15x8.5 tiles) and every source
 * pixel lands on exactly 2 internal / 4 screen pixels - crisp, and close
 * enough that Nine reads as a character instead of a speck. */
#define ZOOM   2.0f
#define VIEW_W (ARC_W / 2)
#define VIEW_H (ARC_H / 2)

/* ------------------------------------------------------------------ atlas */

enum { CELL_SOLID = 0, CELL_GLOW = 1, CELL_STREAK = 2, CELL_TILE = 3 };
#define ATLAS_PX 64.0f
#define CELL_PX  32.0f

static void cell_uv(int cell, float *u0, float *v0, float *u1, float *v1)
{
    int cx = cell & 1, cy = cell >> 1;
    /* Inset by half a texel: at nearest filtering with a 2x upscale this is
       the difference between clean edges and bleeding from the next cell. */
    const float pad = 0.5f / ATLAS_PX;
    *u0 = cx * CELL_PX / ATLAS_PX + pad;
    *v0 = cy * CELL_PX / ATLAS_PX + pad;
    *u1 = (cx + 1) * CELL_PX / ATLAS_PX - pad;
    *v1 = (cy + 1) * CELL_PX / ATLAS_PX - pad;
}

void game_build_atlas(arc_texture *tex)
{
    static uint8_t px[64 * 64 * 4];
    memset(px, 0, sizeof px);

    for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 64; x++) {
            uint8_t *p = &px[(y * 64 + x) * 4];
            int lx = x & 31, ly = y & 31;
            int cell = ((y >> 5) << 1) | (x >> 5);

            if (cell == CELL_SOLID) {
                p[0] = p[1] = p[2] = p[3] = 255;
            } else if (cell == CELL_GLOW) {
                float dx = (lx - 15.5f) / 15.5f, dy = (ly - 15.5f) / 15.5f;
                float a = 1.0f - sqrtf(dx * dx + dy * dy);
                a = a < 0 ? 0 : a * a * a;
                p[0] = p[1] = p[2] = 255;
                p[3] = (uint8_t)(a * 255.0f);
            } else if (cell == CELL_STREAK) {
                float a = 1.0f - fabsf((lx - 15.5f) / 4.0f);
                a = (a < 0 ? 0 : a) * (0.35f + 0.65f * (ly / 31.0f));
                p[0] = p[1] = p[2] = 255;
                p[3] = (uint8_t)(a * 255.0f);
            } else {
                /* Grey-box tile: a lit top edge and a darker body, which is all
                   the readability a collision test actually needs. */
                int top = ly < 2;
                int seam = (lx % 16 == 0) || (ly % 16 == 0);
                p[3] = 255;
                if (top)       { p[0] = 96;  p[1] = 116; p[2] = 168; }
                else if (seam) { p[0] = 30;  p[1] = 34;  p[2] = 54;  }
                else           { p[0] = 44;  p[1] = 50;  p[2] = 78;  }
            }
        }
    }
    gfx_texture_from_rgba(tex, px, 64, 64, 0);
}

/* ------------------------------------------------------------------ tiles */

static char tile_at(const arc_world *w, int tx, int ty)
{
    if (tx < 0 || ty < 0 || tx >= w->w || ty >= w->h) return '.';
    return w->rows[ty][tx];
}

int world_tile_solid(const arc_world *w, int tx, int ty)
{
    char c = tile_at(w, tx, ty);
    /* '#' is building, 'G' is street. Same collision, different material -
       the split exists so the renderer can answer "what am I standing on".
       'D' is the hack gate: a wall until the terminal puzzle opens it. */
    return c == '#' || c == 'G' || c == 'K' || (c == 'D' && !w->door_open);
}

static int tile_oneway(const arc_world *w, int tx, int ty)
{
    return tile_at(w, tx, ty) == '=';
}

/* ------------------------------------------------------------------ level */

static uint32_t rgba(int r, int g, int b, int a)
{
    return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | ((uint32_t)a << 24);
}

static void add_ent(arc_world *w, arc_ent_kind k, int tx, int ty)
{
    if (w->ent_count >= MAX_ENTS) return;
    arc_ent *e = &w->ents[w->ent_count++];
    e->kind = k;
    e->x = tx * TILE + TILE * 0.5f;
    e->y = ty * TILE + TILE * 0.5f;
    e->taken = 0;
    e->bob = (float)((tx * 37 + ty * 17) % 100) * 0.06f;
}

void world_load(arc_world *w, int index)
{
    const arc_level_def *d = &ARC_LEVELS[index % ARC_LEVEL_COUNT];

    memset(w, 0, sizeof *w);
    w->id = d->id; w->title = d->title;
    w->district = d->district;
    w->index = index % ARC_LEVEL_COUNT;
    w->level_count = ARC_LEVEL_COUNT;
    w->w = d->w;   w->h = d->h;
    w->rows = d->rows;

    for (int ty = 0; ty < w->h; ty++) {
        for (int tx = 0; tx < w->w; tx++) {
            switch (d->rows[ty][tx]) {
                case 'S': w->spawn_x = tx * TILE + 3.0f; w->spawn_y = ty * TILE; break;
                case 'v': add_ent(w, E_VOLT,       tx, ty); w->volts_total++;  break;
                case 'E': add_ent(w, E_ECHO,       tx, ty); w->echoes_total++; break;
                case 'C': add_ent(w, E_CHECKPOINT, tx, ty); break;
                case 'X': add_ent(w, E_EXIT,       tx, ty); break;
                case 'A': add_ent(w, E_ANCHOR,     tx, ty); break;
                case 'T': add_ent(w, E_HINT,       tx, ty); break;
                case 'H': add_ent(w, E_TERMINAL,   tx, ty); break;
                case 'm':                 /* horizontal mover */
                case 'n':                 /* vertical mover */
                    if (w->mover_count < MAX_MOVERS) {
                        arc_mover *m = &w->movers[w->mover_count++];
                        memset(m, 0, sizeof *m);
                        m->x0 = m->x = tx * TILE;
                        m->y0 = m->y = ty * TILE;
                        int vertical = d->rows[ty][tx] == 'n';
                        m->dx = vertical ? 0.0f : 6.0f * TILE;
                        m->dy = vertical ? -5.0f * TILE : 0.0f;
                        m->tiles = 4;
                        /* Staggered so a row of them reads as a rhythm rather
                           than as one wide platform cut into pieces. */
                        m->t = (float)((tx * 7 + ty * 3) % 100) / 100.0f;
                        m->speed = vertical ? 0.34f : 0.28f;
                    }
                    break;
                case 'L':
                    if (w->laser_count < MAX_LASERS) {
                        arc_laser *l = &w->lasers[w->laser_count++];
                        memset(l, 0, sizeof *l);
                        l->x = tx * TILE + TILE * 0.5f;
                        l->y = ty * TILE + TILE;
                        /* Reach down to the first solid, so an emitter always
                           spans its gap exactly. */
                        int by = ty + 1;
                        while (by < w->h && !world_tile_solid(w, tx, by)) by++;
                        l->len = (float)(by * TILE) - l->y;
                        l->period = 2.2f;
                        l->phase = (float)((tx * 13) % 100) / 100.0f * l->period;
                    }
                    break;
                case 'N': add_ent(w, E_NODE,       tx, ty); break;
                case 'D':
                    /* Door tiles stay in the row data; only the centre is
                       tracked, for the unlock impact ring. */
                    w->door_x += tx * TILE + TILE * 0.5f;
                    w->door_y += ty * TILE + TILE * 0.5f;
                    w->door_open++;     /* abused as a counter, fixed below */
                    break;
                case 'z':
                case 'p':
                case 'u':
                case 'W':
                    if (w->enemy_count < MAX_ENEMIES) {
                        char c = d->rows[ty][tx];
                        arc_enemy *en = &w->enemies[w->enemy_count++];
                        memset(en, 0, sizeof *en);
                        en->kind = c == 'p' ? EN_COP
                                 : c == 'u' ? EN_TURRET
                                 : c == 'W' ? EN_WARDEN : EN_SCRAPPER;
                        en->x = tx * TILE; en->y = ty * TILE;
                        en->home_x = en->x;
                        en->home_y = en->y;
                        en->range = 3.0f * TILE;
                        /* Cops walk a beat, drones fly one, sentries are
                           bolted down, and the Warden owns the whole street. */
                        en->vx = en->kind == EN_COP    ?  42.0f
                               : en->kind == EN_TURRET ?   0.0f
                               : en->kind == EN_WARDEN ? -150.0f : 60.0f;
                        if (en->kind == EN_WARDEN) {
                            en->variant = d->district % 5;
                            /* Later bosses take more, but the curve is gentle:
                               the fights differ in shape, not in bullet-sponge. */
                            static const int BOSS_HP[5] = { 6, 7, 8, 8, 10 };
                            en->hp = en->hp_max = BOSS_HP[en->variant];
                        } else {
                            en->hp = en->hp_max = 1;
                        }
                        en->alive = 1;
                    }
                    break;
                default: break;
            }
        }
    }

    if (w->door_open) {         /* finish the door-centre average */
        w->door_x /= w->door_open;
        w->door_y /= w->door_open;
        w->door_open = 0;
    }

    w->check_x = w->spawn_x;
    w->check_y = w->spawn_y;
    w->base_enemy_count = w->enemy_count;   /* everything above this is summoned */
    world_reset_to_checkpoint(w);
    w->cam_x = w->p.x - ARC_W * 0.5f;
    w->cam_y = w->p.y - ARC_H * 0.6f;
}

void world_reset_to_checkpoint(arc_world *w)
{
    arc_player *p = &w->p;
    float charge = p->charge;

    memset(p, 0, sizeof *p);
    p->x = w->check_x; p->y = w->check_y;
    p->w = 10.0f; p->h = 20.0f;
    p->facing = 1;
    p->state = PS_AIR;
    p->plates = 3;
    p->air_dash = 1;
    /* Charge survives death: dying is a routing mistake, not a resource loss,
       and taking the fuel away would punish the retry the game wants to be free. */
    p->charge = charge > CHARGE_FLOOR ? charge : CHARGE_MAX * 0.5f;

    /* Boss-summoned drones live above base_enemy_count; drop them so they do
       not accumulate as permanent level enemies across deaths. */
    if (w->base_enemy_count > 0) w->enemy_count = w->base_enemy_count;

    /* Popcorn enemies respawn with the player - a flow game's obstacles are
       part of the route, and a cleared route retried is a different level. */
    for (int i = 0; i < w->enemy_count; i++) {
        arc_enemy *en = &w->enemies[i];
        en->alive = 1;
        en->x = en->home_x;
        en->y = en->home_y;
        en->death_t = 0;
        en->hit_flash = 0;
        en->hacked = 0;
        en->glitch = 0;
        en->shoot_cd = 0;
        en->telegraph = 0;
        en->summon_cd = 0;
        en->hp = en->hp_max;        /* a chipped boss comes back whole */
        /* Restore the patrol speed the archetype was created with; the old
           `vx>0?60:-60` slid turrets (vx 0 -> -60) off their lane and forced
           cops from 42 to 60. */
        en->vx = en->kind == EN_TURRET ?   0.0f
               : en->kind == EN_COP    ?  42.0f
               : en->kind == EN_WARDEN ? -150.0f
                                       :  60.0f;
    }

    /* Debris and rounds in flight from the death that caused this respawn
       must not survive it. */
    memset(w->parts, 0, sizeof w->parts);
    memset(w->shots, 0, sizeof w->shots);
    w->ring_t = 0;
    w->ride = -1;               /* nobody is riding a mover at spawn */

    /* A hack interrupted by death resets; a door already opened stays open.
       Re-running the node climb after every death would punish the retry. */
    w->hack_t = 0;
    if (!w->door_open)
        for (int i = 0; i < w->ent_count; i++)
            if (w->ents[i].kind == E_NODE) w->ents[i].taken = 0;
}

/* -------------------------------------------------------------- collision */

static int box_hits_solid(const arc_world *w, float x, float y, float bw, float bh)
{
    int tx0 = (int)floorf(x / TILE), tx1 = (int)floorf((x + bw - 0.001f) / TILE);
    int ty0 = (int)floorf(y / TILE), ty1 = (int)floorf((y + bh - 0.001f) / TILE);
    for (int ty = ty0; ty <= ty1; ty++)
        for (int tx = tx0; tx <= tx1; tx++)
            if (world_tile_solid(w, tx, ty)) return 1;
    return 0;
}

static void move_x(arc_world *w, arc_player *p, float dx)
{
    p->x += dx;
    if (!box_hits_solid(w, p->x, p->y, p->w, p->h)) return;

    /* Step back to the tile boundary rather than integrating out - at dash
       speed a per-pixel walk would cost 7 iterations a frame for nothing. */
    if (dx > 0) p->x = floorf((p->x + p->w) / TILE) * TILE - p->w - 0.001f;
    else        p->x = floorf(p->x / TILE) * TILE + TILE + 0.001f;
    p->vx = 0;
}

static int move_y(arc_world *w, arc_player *p, float dy)
{
    float prev_bottom = p->y + p->h;
    p->y += dy;

    int landed = 0;
    if (box_hits_solid(w, p->x, p->y, p->w, p->h)) {
        if (dy > 0) { p->y = floorf((p->y + p->h) / TILE) * TILE - p->h - 0.001f; landed = 1; }
        else        { p->y = floorf(p->y / TILE) * TILE + TILE + 0.001f; }
        p->vy = 0;
    } else if (dy > 0 && p->state != PS_STOMP) {
        /* One-way platforms: only solid when falling onto them from above,
           and never during a Stomp - smashing down through a grating is the
           whole reason the verb exists (GDD §3.1). */
        int ty = (int)floorf((p->y + p->h) / TILE);
        int tx0 = (int)floorf(p->x / TILE), tx1 = (int)floorf((p->x + p->w - 0.001f) / TILE);
        for (int tx = tx0; tx <= tx1; tx++) {
            if (!tile_oneway(w, tx, ty)) continue;
            float top = ty * TILE;
            if (prev_bottom <= top + 1.0f) {
                p->y = top - p->h - 0.001f;
                p->vy = 0;
                landed = 1;
                break;
            }
        }

        /* Movers, same one-way rule. Checked after the tiles so a mover
           parked against geometry never steals a landing from it. */
        if (!landed) {
            for (int i = 0; i < w->mover_count; i++) {
                const arc_mover *m = &w->movers[i];
                float mw = m->tiles * (float)TILE;
                if (p->x + p->w < m->x || p->x > m->x + mw) continue;
                /* A generous top band: the platform is moving too, so an exact
                   crossing test drops you through it on the frames it rises. */
                if (prev_bottom <= m->y + 6.0f && p->y + p->h >= m->y - 1.0f) {
                    p->y = m->y - p->h - 0.001f;
                    p->vy = 0;
                    landed = 1;
                    w->ride = i;
                    break;
                }
            }
        }
    }
    return landed;
}

static int on_ground(const arc_world *w, const arc_player *p)
{
    float y = p->y + p->h + 1.0f;
    if (box_hits_solid(w, p->x, p->y + 1.0f, p->w, p->h)) return 1;

    for (int i = 0; i < w->mover_count; i++) {
        const arc_mover *m = &w->movers[i];
        float mw = m->tiles * (float)TILE;
        if (p->x + p->w < m->x || p->x > m->x + mw) continue;
        if (y >= m->y - 1.0f && y <= m->y + 7.0f) return 1;
    }

    int ty = (int)floorf(y / TILE);
    int tx0 = (int)floorf(p->x / TILE), tx1 = (int)floorf((p->x + p->w - 0.001f) / TILE);
    for (int tx = tx0; tx <= tx1; tx++) {
        if (world_tile_solid(w, tx, ty)) return 1;
        if (tile_oneway(w, tx, ty) && (p->y + p->h) <= ty * TILE + 1.0f) return 1;
    }
    return 0;
}

static int wall_side(const arc_world *w, const arc_player *p)
{
    if (box_hits_solid(w, p->x - 1.5f, p->y + 2.0f, p->w, p->h - 4.0f)) return -1;
    if (box_hits_solid(w, p->x + 1.5f, p->y + 2.0f, p->w, p->h - 4.0f)) return +1;
    return 0;
}

/* ---------------------------------------------------------------- charge */

static int spend(arc_player *p, float cost)
{
    if (p->charge < cost) return 0;
    p->charge -= cost;
    return 1;
}

static void gain(arc_player *p, float amount)
{
    p->charge += amount;
    if (p->charge > CHARGE_MAX) p->charge = CHARGE_MAX;
}

/* ---------------------------------------------------------------- player */

static void player_jump(arc_world *w, arc_player *p)
{
    (void)w;
    p->vy = -JUMP_V;
    p->state = PS_AIR;
    p->coyote = 0;
    p->jump_buffer = 0;
    p->squash = -0.35f;
}

static void try_tether(arc_world *w, arc_player *p)
{
    /* Auto-target the best anchor in a forward cone. GDD open question 2 said
       manual aiming at speed would be miserable on a Vita stick; it is. */
    float best = TETHER_RANGE * TETHER_RANGE;
    arc_ent *pick = NULL;
    float cx = p->x + p->w * 0.5f, cy = p->y + p->h * 0.5f;

    for (int i = 0; i < w->ent_count; i++) {
        arc_ent *e = &w->ents[i];
        if (e->kind != E_ANCHOR) continue;
        float dx = e->x - cx, dy = e->y - cy;
        if (dy > 8.0f) continue;                       /* anchors are overhead */
        if (dx * p->facing < -24.0f) continue;         /* and ahead of you */
        float d2 = dx * dx + dy * dy;
        if (d2 < best) { best = d2; pick = e; }
    }
    if (!pick || !spend(p, COST_TETHER)) return;

    p->tethered = 1;
    p->tether_x = pick->x;
    p->tether_y = pick->y;
    p->tether_len = sqrtf(best);
    p->state = PS_TETHER;
}

static void release_tether(arc_player *p)
{
    if (!p->tethered) return;
    p->tethered = 0;
    p->state = PS_AIR;
    p->vx *= TETHER_BOOST;
    p->vy *= TETHER_BOOST;
}

/* Defined with the rest of the particle code, below the player - the player is
 * the only thing above it that needs to throw sparks. */
static void spawn_parts(arc_world *w, float x, float y, int n, float speed,
                        float spread_x, uint32_t col, int glow, float grav);
static void spawn_shot(arc_world *w, float x, float y, float ax, float ay, int friendly, int target);

/* Sprite is bigger than the collision box on purpose - a forgiving hitbox
   read against a character that fills the frame the way Rayman's does. */
#define ENEMY_W 22.0f
#define ENEMY_H 20.0f
#define PLAYER_VISUAL_SCALE 0.5f

/* Muzzle positions, measured off the sprites rather than guessed. Every one of
 * these is the rightmost opaque pixel at weapon height in the source frame, so
 * a round leaves the barrel that is drawn instead of the middle of the actor.
 * Guessed offsets are why shots used to appear out of Nine's chest. */
#define NINE_MUZZLE_X   54.0f    /* in the 71x67 shoot frame */
#define NINE_MUZZLE_Y   25.0f
#define COP_MUZZLE_X    40.0f    /* in the 61x64 cop frame */
#define COP_MUZZLE_Y    34.0f
#define TURRET_MUZZLE_X 19.0f    /* in the 25x23 turret frame */
#define TURRET_MUZZLE_Y  2.0f
/* Per-variant boss sprite rects and frame sizes, indexed by en->variant. */
static arc_rect boss_rect(int v)
{
    switch (v) {
        case 1: return RECT_BOSS_BOUNCER;
        case 2: return RECT_BOSS_COIL;
        case 3: return RECT_BOSS_TIDE;
        case 4: return (arc_rect){ RECT_BOSS_CHORUS.x, RECT_BOSS_CHORUS.y,
                                   CHORUS_FRAME_W, CHORUS_FRAME_H };
        default: return RECT_BOSS_WARDEN;
    }
}

#define WARDEN_MUZZLE_X 160.0f   /* in the 163x60 rig */
#define WARDEN_MUZZLE_Y  25.0f

/* Turn a muzzle measured in sprite pixels into a world point, given where the
 * sprite is drawn and which way it faces. `sx, sy` is the sprite's top-left in
 * world space, `sw, sh` its drawn size, `fw, fh` its source frame size. */
static void muzzle_world(float sx, float sy, float sw, float sh,
                         float fw, float fh, float mx, float my,
                         int facing, float *ox, float *oy)
{
    float u = mx / fw;
    if (facing < 0) u = 1.0f - u;            /* the sprite is mirrored */
    *ox = sx + u * sw;
    *oy = sy + (my / fh) * sh;
}

/* Melee timing, in seconds. The windup is deliberately tiny: this is a courier
 * throwing an elbow at a running pace, not a fighting-game startup. The whole
 * swing is shorter than a jump arc so it can never cost you a landing.
 * `atk_time` counts DOWN, so a zeroed player struct is a player not swinging. */
#define ATK_WINDUP       0.045f
#define ATK_ACTIVE       0.10f
#define ATK_TOTAL        0.22f
#define ATK_CHAIN_WINDOW 0.26f
#define ATK_REACH        23.0f
#define ATK_HEIGHT       20.0f

static void player_update(arc_world *w, arc_player *p, const arc_input *in, float dt)
{
    if (p->state == PS_DEAD) {
        p->dead_time += dt;
        if (p->dead_time > 0.45f) {
            w->deaths++;
            world_reset_to_checkpoint(w);
        }
        return;
    }

    if (p->invuln > 0) p->invuln -= dt;
    if (p->dash_cd > 0) p->dash_cd -= dt;
    if (p->squash < 0) p->squash += dt * 3.5f; else p->squash = 0;

    int grounded = on_ground(w, p);
    if (grounded) { p->coyote = COYOTE; p->air_dash = 1; }
    else if (p->coyote > 0) p->coyote -= dt;

    if (in->jump_edge) p->jump_buffer = JUMP_BUFFER;
    else if (p->jump_buffer > 0) p->jump_buffer -= dt;

    if (in->mx) p->facing = in->mx;

    /* ---- melee: an overlay on the state machine, not a state ----
       You keep run, jump and fall control through the whole swing. An attack
       that froze you in place would be the single fastest way to break the
       "never break the run" pillar, so it is not allowed to. */
    if (p->atk_time > 0) p->atk_time -= dt;
    if (p->atk_chain_win > 0) {
        p->atk_chain_win -= dt;
        if (p->atk_chain_win <= 0) p->atk_chain = 0;
    }

    if (in->attack_edge && p->atk_time <= 0) {
        p->atk_chain = p->atk_chain_win > 0 ? (p->atk_chain + 1) % 3 : 0;
        p->atk_time  = ATK_TOTAL;
        p->atk_chain_win = ATK_TOTAL + ATK_CHAIN_WINDOW;
        p->atk_hit = 0;

        /* Firing kicks you back a little rather than pulling you forward:
           Nine is shooting, not charging, and recoil is what tells you the
           gun has weight. It is small enough never to cost you a landing. */
        p->vx -= p->facing * 26.0f;

        /* Firing in the air floats you slightly - it buys the airtime to
           actually aim, and makes an air shot land instead of dropping. */
        if (!grounded && p->vy > 0) p->vy *= 0.62f;

        /* Same transform draw_player uses, so the round starts at the barrel
           that is actually on screen rather than at a guessed offset. */
        {
            float dw = PLAYER_FRAME_W * PLAYER_VISUAL_SCALE;
            float dh = PLAYER_FRAME_H * PLAYER_VISUAL_SCALE;
            float sx = p->x + p->w * 0.5f - dw * 0.5f;
            float sy = p->y + p->h - dh;
            float mx, my;
            muzzle_world(sx, sy, dw, dh, PLAYER_FRAME_W, PLAYER_FRAME_H,
                         NINE_MUZZLE_X, NINE_MUZZLE_Y, p->facing, &mx, &my);

            /* Aim. Holding up or down fires diagonally, like the dash - but the
               round also locks softly onto the nearest enemy ahead within a
               cone, so a flying drone above you can be shot without pixel-
               perfect aiming. That auto-lock is the fix for "robots don't get
               hit": a purely horizontal round flew under everything airborne. */
            float ax = (float)p->facing;
            float ay = in->down ? 0.8f : (in->jump && !grounded ? -0.8f : 0.0f);

            float best2 = 280.0f * 280.0f; int lock = -1;
            for (int j = 0; j < w->enemy_count; j++) {
                arc_enemy *en = &w->enemies[j];
                if (!en->alive) continue;
                float ex = en->x + ENEMY_W * 0.5f - mx;
                float ey = en->y + ENEMY_H * 0.5f - my;
                if (ex * p->facing < 0) continue;          /* only ahead */
                float d2 = ex * ex + ey * ey;
                if (d2 < best2) { best2 = d2; ax = ex; ay = ey; lock = j; }
            }
            spawn_shot(w, mx, my, ax, ay, 1, lock);
        }
    }

    /* ---- pulse: hijack the nearest drone ---- */
    if (p->pulse_cd > 0) p->pulse_cd -= dt;

    if (in->pulse_edge && p->pulse_cd <= 0) {
        float pcx = p->x + p->w * 0.5f, pcy = p->y + p->h * 0.5f;

        /* Terminals first: they are the progression, and hacking one is free -
           a door that could soft-lock a broke player would be a design bug,
           not a challenge. (Ada left herself backdoors; drones cost Charge
           because they are Meridian's, the terminals were always hers.) */
        /* A Pulse also drops any beam in reach - GDD verb 7 says it kills
           lasers, and this is where that stops being a document. Dropping a
           beam arms the cooldown, so it cannot be spammed to hold a beam down
           for free forever; the beam cycles off on its own anyway. */
        for (int i = 0; i < w->laser_count; i++) {
            arc_laser *l = &w->lasers[i];
            float dx2 = l->x - pcx, dy2 = (l->y + l->len * 0.5f) - pcy;
            if (dx2 * dx2 + dy2 * dy2 < (PULSE_RANGE * 1.6f) * (PULSE_RANGE * 1.6f)) {
                l->down = 4.0f;
                p->pulse_cd = PULSE_CD;
                w->emp_t = 0.30f; w->emp_x = pcx; w->emp_y = pcy;
            }
        }

        int hacked_terminal = 0;
        if (!w->door_open && w->hack_t <= 0) {
            for (int i = 0; i < w->ent_count; i++) {
                arc_ent *e = &w->ents[i];
                if (e->kind != E_TERMINAL) continue;
                float ex = e->x - pcx, ey = e->y - pcy;
                if (ex * ex + ey * ey > PULSE_RANGE * PULSE_RANGE) continue;

                w->hack_t = HACK_WINDOW;
                for (int j = 0; j < w->ent_count; j++)
                    if (w->ents[j].kind == E_NODE) w->ents[j].taken = 0;

                p->pulse_cd = PULSE_CD;
                w->emp_t = 0.30f; w->emp_x = pcx; w->emp_y = pcy;
                spawn_parts(w, e->x, e->y, 8, 110.0f, 1.0f,
                            rgba(120, 240, 255, 230), 1, 40.0f);
                hacked_terminal = 1;
                break;
            }
        }

        int best = -1;
        float best_d2 = PULSE_RANGE * PULSE_RANGE;
        for (int i = 0; !hacked_terminal && i < w->enemy_count; i++) {
            arc_enemy *en = &w->enemies[i];
            if (!en->alive || en->hacked > 0) continue;
            float ex = en->x + 11.0f - pcx, ey = en->y + 10.0f - pcy;
            float d2 = ex * ex + ey * ey;
            if (d2 < best_d2) { best_d2 = d2; best = i; }
        }

        /* Charge is only spent when something is actually taken. A Pulse into
           empty air costing 30 would teach the player not to press the button. */
        if (best >= 0 && spend(p, COST_PULSE)) {
            arc_enemy *en = &w->enemies[best];
            en->hacked = PULSE_TIME;
            en->glitch = 0.35f;
            en->vx = (en->x + 11.0f < pcx ? -1.0f : 1.0f) * PULSE_KNOCK;

            p->pulse_cd = PULSE_CD;
            w->emp_t = 0.30f; w->emp_x = pcx; w->emp_y = pcy;
            w->hitstop = w->hitstop > 0.04f ? w->hitstop : 0.04f;
            spawn_parts(w, en->x + 11.0f, en->y + 10.0f, 8, 110.0f, 1.0f,
                        rgba(120, 240, 255, 230), 1, 40.0f);
            audio_play(SFX_PULSE, 0.7f, 1.0f);
        }
    }

    /* ---- dash: overrides everything while it lasts ---- */
    if (p->state == PS_DASH) {
        p->dash_time -= dt;
        p->invuln = p->invuln > 0.02f ? p->invuln : 0.02f;
        if (p->dash_time <= 0) {
            p->state = grounded ? PS_GROUND : PS_AIR;
            p->vx *= 0.55f;                      /* bleed, don't stop dead */
            p->vy *= 0.35f;
        }
    } else if (in->dash_edge && p->dash_cd <= 0 &&
               (grounded || p->air_dash) && spend(p, COST_DASH)) {
        float dx = (float)in->mx, dy = 0.0f;
        if (in->down) dy = 1.0f;
        else if (in->jump && !grounded) dy = -1.0f;
        if (dx == 0 && dy == 0) dx = (float)p->facing;
        float len = sqrtf(dx * dx + dy * dy);
        p->vx = dx / len * DASH_SPEED;
        p->vy = dy / len * DASH_SPEED;
        p->state = PS_DASH;
        audio_play(SFX_DASH, 0.5f, 0.95f + 0.1f * (p->facing > 0));
        p->dash_time = DASH_TIME;
        p->dash_cd = DASH_CD;
        if (!grounded) p->air_dash = 0;
        p->jump_buffer = 0;
    }

    /* ---- tether ---- */
    if (in->tether_edge && !p->tethered && p->state != PS_DASH) try_tether(w, p);
    if (p->tethered && (!in->tether || p->jump_buffer > 0)) release_tether(p);

    if (p->tethered) {
        p->vy += GRAV * dt;
        /* Swing input adds tangential push, which is the whole feel of it. */
        p->vx += in->mx * AIR_ACCEL * 0.5f * dt;

        p->x += p->vx * dt;
        p->y += p->vy * dt;

        float cx = p->x + p->w * 0.5f, cy = p->y + p->h * 0.5f;
        float dx = cx - p->tether_x, dy = cy - p->tether_y;
        float d = sqrtf(dx * dx + dy * dy);
        if (d > p->tether_len && d > 0.001f) {
            float nx = dx / d, ny = dy / d;
            p->x -= nx * (d - p->tether_len);
            p->y -= ny * (d - p->tether_len);
            float radial = p->vx * nx + p->vy * ny;      /* kill outward speed */
            p->vx -= nx * radial;
            p->vy -= ny * radial;
        }
        if (box_hits_solid(w, p->x, p->y, p->w, p->h)) release_tether(p);
        gain(p, 6.0f * dt);
        p->anim_cat = ANIM_AIR;
        return;
    }

    /* ---- stomp ----
       Also startable while standing on a one-way, so "down + jump" drops you
       through a grating instead of doing nothing. */
    int on_oneway = 0;
    {
        int ty = (int)floorf((p->y + p->h + 1.0f) / TILE);
        int tx0 = (int)floorf(p->x / TILE), tx1 = (int)floorf((p->x + p->w - 0.001f) / TILE);
        for (int tx = tx0; tx <= tx1 && !on_oneway; tx++)
            if (tile_oneway(w, tx, ty)) on_oneway = 1;
    }
    if (p->state != PS_DASH && (!grounded || on_oneway) && in->down && in->jump_edge) {
        p->state = PS_STOMP;
        p->vy = STOMP_V;
        p->vx = 0;
        p->jump_buffer = 0;
    }

    /* ---- wall ---- */
    int wall = (!grounded && p->state != PS_DASH && p->state != PS_STOMP)
             ? wall_side(w, p) : 0;
    if (wall && p->vy > -10.0f && in->mx == wall) {
        p->state = PS_WALL;
        p->wall_dir = wall;
        p->wall_stick = WALL_STICK;
    } else if (p->state == PS_WALL) {
        p->wall_stick -= dt;
        if (p->wall_stick <= 0 || !wall) p->state = PS_AIR;
    }

    if (p->state == PS_WALL) {
        if (p->vy > WALL_SLIDE) p->vy = WALL_SLIDE;
        if (p->jump_buffer > 0) {
            p->vx = -p->wall_dir * WALL_JUMP_X;
            p->vy = WALL_JUMP_Y;
            p->facing = -p->wall_dir;
            p->state = PS_AIR;
            p->jump_buffer = 0;
            p->wall_stick = 0;
            gain(p, 5.0f);
        }
    }

    /* ---- ordinary jump ---- */
    if (p->state != PS_DASH && p->state != PS_STOMP && p->state != PS_WALL &&
        p->jump_buffer > 0 && p->coyote > 0) {
        player_jump(w, p);
    }
    /* Variable height: releasing jump on the way up cuts the arc. */
    if (!in->jump && p->vy < 0 && p->state == PS_AIR) p->vy += GRAV * 1.6f * dt;

    /* ---- horizontal ---- */
    if (p->state != PS_DASH && p->state != PS_STOMP) {
        float accel = grounded ? RUN_ACCEL : AIR_ACCEL;
        if (in->mx) {
            p->vx += in->mx * accel * dt;
            if (p->vx > RUN_MAX && in->mx > 0 && grounded) p->vx = RUN_MAX;
            if (p->vx < -RUN_MAX && in->mx < 0 && grounded) p->vx = -RUN_MAX;
        } else {
            float drag = (grounded ? RUN_FRICTION : AIR_DRAG) * dt;
            if (fabsf(p->vx) <= drag) p->vx = 0;
            else p->vx -= drag * (p->vx > 0 ? 1.0f : -1.0f);
        }
    }

    /* ---- gravity ---- */
    if (p->state != PS_DASH) {
        float g = p->vy < 0 ? GRAV : GRAV_FALL;
        if (fabsf(p->vy) < APEX_BAND && p->state == PS_AIR) {
            g *= APEX_GRAV_MUL;                  /* the apex hang */
            p->apex += dt;
        } else p->apex = 0;
        if (p->state == PS_STOMP) g = 0;         /* stomp falls at a fixed rate */
        p->vy += g * dt;
        if (p->vy > FALL_MAX && p->state != PS_STOMP) p->vy = FALL_MAX;
    }

    /* ---- integrate ---- */
    move_x(w, p, p->vx * dt);
    float fall_vy = p->vy;
    int landed = move_y(w, p, p->vy * dt);
    /* Boots on wet asphalt, scaled by how far you fell. A landing you barely
       feel should barely be heard. */
    if (landed && fall_vy > 120.0f) {
        float k = fall_vy / FALL_MAX;
        if (k > 1.0f) k = 1.0f;
        audio_play(SFX_LAND, 0.20f + 0.35f * k, 1.15f - 0.25f * k);
    }

    if (landed) {
        if (p->state == PS_STOMP) {
            /* Height becomes forward speed - the reason to stomp is to go
               faster afterwards, not to hit something. */
            p->vx = p->facing * STOMP_LAUNCH;
            p->squash = -0.7f;
            w->shake = 0.12f;
            gain(p, 10.0f);
        } else if (p->apex > 0.0f) {
            gain(p, 12.0f);                      /* perfect landing */
            p->squash = -0.3f;
        } else {
            p->squash = -0.2f;
        }
        p->state = PS_GROUND;
        p->apex = 0;
    } else if (p->state == PS_GROUND && !on_ground(w, p)) {
        p->state = PS_AIR;
    }

    /* ---- flow economy ---- */
    float speed = fabsf(p->vx);
    if (speed >= RUN_MAX * 0.8f) {
        gain(p, 8.0f * dt);
        w->flow += dt;
    } else if (speed < 8.0f && grounded) {
        p->charge -= 4.0f * dt;
        if (p->charge < CHARGE_FLOOR) p->charge = CHARGE_FLOOR;
        w->flow = 0;
    }

    /* ---- out of the world ---- */
    if (p->y > w->h * TILE + 48.0f) {
        p->state = PS_DEAD;
        p->dead_time = 0;
    }

    /* ---- animation category ---- */
    {
        arc_anim_cat cat = ANIM_IDLE;
        if (p->state == PS_AIR || p->state == PS_STOMP || p->state == PS_WALL || p->state == PS_DASH)
            cat = ANIM_AIR;
        else if (fabsf(p->vx) > 10.0f)
            cat = ANIM_RUN;

        if (cat != p->anim_cat) { p->anim_cat = cat; p->anim_t = 0; }
        else p->anim_t += dt;
    }
}

/* --------------------------------------------------------------- particles
 * Debris and sparks. Deterministic pseudo-random: a hit in the same place
 * twice should not look copy-pasted, but it must not need a RNG either. */

static float prand(int seed)
{
    float s = sinf(seed * 12.9898f) * 43758.5453f;
    return s - floorf(s);
}

static void spawn_parts(arc_world *w, float x, float y, int n, float speed,
                        float spread_x, uint32_t col, int glow, float grav)
{
    for (int i = 0; i < n; i++) {
        arc_particle *q = &w->parts[w->part_head];
        w->part_head = (w->part_head + 1) % MAX_PARTICLES;

        int seed = w->part_head * 7 + i * 31 + (int)(w->time * 97.0f);
        float a  = prand(seed) * 6.28318f;
        float sp = speed * (0.35f + 0.65f * prand(seed + 5));

        q->x = x; q->y = y;
        q->vx = cosf(a) * sp * spread_x;
        q->vy = sinf(a) * sp - sp * 0.35f;      /* biased upward, so it arcs */
        q->life0 = q->life = 0.22f + prand(seed + 11) * 0.38f;
        q->size = glow ? 5.0f + prand(seed + 17) * 5.0f
                       : 1.0f + prand(seed + 17) * 2.0f;
        q->grav = grav;
        q->col = col;
        q->glow = glow;
    }
}

/* A directional cone rather than a burst: what the blade throws off follows
 * the blade. `sx, sy` is the sweep direction, `spread` how far off it strays. */
static void spawn_spray(arc_world *w, float x, float y, float sx, float sy,
                        int n, float speed, float spread, uint32_t col, int stick)
{
    for (int i = 0; i < n; i++) {
        arc_particle *q = &w->parts[w->part_head];
        w->part_head = (w->part_head + 1) % MAX_PARTICLES;

        int seed = w->part_head * 13 + i * 53 + (int)(w->time * 131.0f);
        float a  = atan2f(sy, sx) + (prand(seed) - 0.5f) * spread;
        float sp = speed * (0.30f + 0.70f * prand(seed + 3));

        q->x = x; q->y = y;
        q->vx = cosf(a) * sp;
        q->vy = sinf(a) * sp;
        q->life0 = q->life = 0.35f + prand(seed + 9) * 0.45f;
        q->size = 1.0f + prand(seed + 21) * 2.5f;
        q->grav = 780.0f;
        q->col = col;
        q->glow = 0;
        q->stick = stick;
    }
}

static void parts_update(arc_world *w, float dt)
{
    for (int i = 0; i < MAX_PARTICLES; i++) {
        arc_particle *q = &w->parts[i];
        if (q->life <= 0) continue;
        q->life -= dt;

        if (q->stick == 2) continue;            /* landed: a decal, not a body */

        q->vy += q->grav * dt;
        q->vx -= q->vx * 3.0f * dt;             /* air drag, so sparks settle */
        q->x  += q->vx * dt;
        q->y  += q->vy * dt;

        /* Spray that finds a surface stays on it and dries out slowly. */
        if (q->stick == 1 &&
            world_tile_solid(w, (int)floorf(q->x / TILE), (int)floorf(q->y / TILE))) {
            q->stick = 2;
            q->vx = q->vy = q->grav = 0;
            q->size *= 1.5f;
            q->life0 = q->life = 1.7f;
        }
    }
    if (w->ring_t > 0) w->ring_t -= dt;
    if (w->emp_t  > 0) w->emp_t  -= dt;
}

uint32_t enemy_fluid(arc_enemy_kind k)
{
    switch (k) {
        case EN_SCRAPPER: return rgba(255, 140, 55, 255);   /* hot coolant */
        case EN_COP:      return rgba(205, 30, 45, 255);    /* blood */
        default:          return rgba(205, 30, 45, 255);
    }
}

/* ----------------------------------------------------------------- enemies
 * Most of the roster is a bounce target, not a fight - see GDD §5. A
 * Scrapper is a hovering drone: it patrols its range in the air, turns at
 * walls, and dies to Dash, Stomp or a falling bounce; touching it any other
 * way costs a plate. */

/* Hitbox roughly matches the drawn drone (27x26 world px). A hitbox much
 * smaller than the sprite reads as the game cheating. */

/* The blade's sweep, in screen-space radians (+y is down). Each hit of the
 * chain enters from a different side, so three swings read as three different
 * cuts instead of the same one three times. Shared by the renderer and by the
 * spray, which is what makes the fluid actually follow the blade. */
static void slash_sweep(int chain, int facing, float *a0, float *a1)
{
    switch (chain) {
        case 0:  *a0 = -1.15f; *a1 =  0.50f; break;   /* overhead, cutting down */
        case 1:  *a0 =  1.10f; *a1 = -0.45f; break;   /* rising backhand */
        default: *a0 = -0.40f; *a1 =  0.40f; break;   /* flat cleave */
    }
    if (facing < 0) {
        const float PI = 3.14159265f;
        *a0 = PI - *a0;
        *a1 = PI - *a1;
    }
}

/* One kill path for every verb that can kill, so a Scrapper dropped by a dash
 * dies exactly as hard as one dropped by the third hit of a melee chain.
 * `weight` scales the whole impact - hitstop, shake, debris - and is the only
 * knob that separates a light hit from a finisher.
 *
 * Everything here exists to make the kill *felt*: the freeze reads as force,
 * the debris reads as consequence, and the Charge refund reads as permission
 * to keep moving. Enemies are flow nodes, not walls (GDD §3.1: "most enemies
 * are solved by routing past them" - this is routing *through* them). */
/* Each boss has a different opening, so the five fights are five puzzles
 * rather than one rig at five sizes. Returns whether the boss can be damaged
 * right now, given where the hit is coming from (hit_x). WARDEN is always
 * open; the rest demand a specific verb or position. */
static int boss_vulnerable(const arc_world *w, const arc_enemy *en, float hit_x)
{
    const arc_player *p = &w->p;
    switch (en->variant) {
        case 1:  /* BOUNCER: too fast to trade with on the ground - come down on
                    it from above (a stomp, or any airborne descent onto it) */
            return p->state == PS_STOMP ||
                   (p->vy > 0.0f && p->y + p->h * 0.5f < en->y + 10.0f);
        case 2: { /* COIL: front-plated - hit it from behind, or stomp it */
            float bw = 128.0f, facing = en->vx >= 0 ? 1.0f : -1.0f;
            int from_behind = (hit_x - (en->x + bw * 0.5f)) * facing < 0.0f;
            return p->state == PS_STOMP || from_behind;
        }
        case 3: { /* TIDE: shielded mid-sweep, exposed at the ends of its arc.
                     Position-based, not instantaneous speed, so the opening is
                     a readable ~1 s window rather than a single frame. */
            float c = 0.5f - 0.5f * cosf(en->state_t * 0.7f);   /* 0..1 sweep */
            return c < 0.18f || c > 0.82f;
        }
        case 4: { /* CHORUS: invulnerable while any of its drones still live */
            for (int i = 0; i < w->enemy_count; i++)
                if (w->enemies[i].kind == EN_SCRAPPER && w->enemies[i].alive)
                    return 0;
            return 1;
        }
        default: return 1;   /* WARDEN */
    }
}

/* A hit that lands on a boss's armour: spark and a metal clang, no damage, so
 * the player reads "wrong opening" instead of "my attack did nothing". */
static void boss_clang(arc_world *w, float x, float y)
{
    w->emp_t = 0.0f;
    spawn_parts(w, x, y, 5, 130.0f, 1.0f, rgba(210, 220, 235, 255), 0, 300.0f);
    audio_play(SFX_BOSS_HIT, 0.5f, 1.7f);
    if (0.10f > w->shake) w->shake = 0.10f;
}

static void enemy_kill(arc_world *w, arc_enemy *en, float dir, float weight)
{
    arc_player *p = &w->p;

    /* Anything with hp left takes the hit and stays up. The feedback is the
       same either way - what changes is whether the thing falls over. */
    if (--en->hp > 0) {
        en->hit_flash = 0.16f;
        en->squash = -0.5f;
        float cx0 = en->x + ENEMY_W * 0.5f, cy0 = en->y + ENEMY_H * 0.5f;
        float stop = 0.05f + 0.05f * weight;
        if (stop > w->hitstop) w->hitstop = stop;
        if (0.14f > w->shake) w->shake = 0.14f;
        spawn_parts(w, cx0, cy0, 6, 150.0f, 1.0f, rgba(255, 205, 150, 255), 0, 620.0f);
        audio_play(SFX_BOSS_HIT, 0.6f, 0.95f + 0.1f * (en->hp & 1));
        spawn_spray(w, cx0, cy0, dir, -0.3f, 6, 170.0f, 1.0f,
                    enemy_fluid(en->kind), 1);
        gain(p, 6.0f);
        /* Popcorn hits refund an air-dash to reward routing through a crowd;
           bosses do not, or the fight becomes infinite air mobility (an
           exploit the review flagged). */
        if (en->kind != EN_WARDEN) p->air_dash = 1;
        return;
    }

    en->alive  = 0;
    en->death_t = 0.34f;
    en->dvx    = dir * (120.0f + 130.0f * weight);
    en->dvy    = -90.0f - 60.0f * weight;
    en->hit_flash = 0.12f;

    float cx = en->x + ENEMY_W * 0.5f, cy = en->y + ENEMY_H * 0.5f;

    /* Hitstop is the single biggest contributor to weight. It is capped hard:
       past ~0.13 s the game stops feeling responsive and starts feeling laggy. */
    float stop = 0.045f + 0.075f * weight;
    if (stop > 0.13f) stop = 0.13f;
    if (stop > w->hitstop) w->hitstop = stop;
    if (0.10f + 0.16f * weight > w->shake) w->shake = 0.10f + 0.16f * weight;

    w->ring_t = 0.26f; w->ring_x = cx; w->ring_y = cy;
    /* Heavier kills read lower. Pitch is the cheapest way to make one sample
       cover a light hit and a finisher without shipping two. */
    audio_play(en->kind == EN_WARDEN ? SFX_BOSS_DIE
             : en->kind == EN_COP     ? SFX_KILL_MAN
                                      : SFX_KILL_BOT,
               0.55f + 0.25f * weight, 1.12f - 0.22f * weight);

    spawn_parts(w, cx, cy, 5 + (int)(weight * 5.0f), 150.0f + 120.0f * weight,
                1.0f, rgba(255, 205, 150, 255), 0, 620.0f);
    spawn_parts(w, cx, cy, 3, 90.0f, 1.0f, rgba(255, 150, 90, 190), 1, 90.0f);

    /* What the cut throws off follows the cut. A blade kill sprays along the
       blade's exit angle; a dash or stomp has no blade, so it just bursts
       away from the impact. */
    float sx, sy;
    if (p->atk_time > 0) {
        float a0, a1;
        slash_sweep(p->atk_chain, p->facing, &a0, &a1);
        sx = cosf(a1); sy = sinf(a1);
    } else {
        sx = dir; sy = -0.45f;
    }

    uint32_t fluid = enemy_fluid(en->kind);
    spawn_spray(w, cx, cy, sx, sy, 10 + (int)(weight * 10.0f),
                190.0f + 150.0f * weight, 1.15f, fluid, 1);
    /* A brighter core to the spray, so it glows on the way out and the decal
       it leaves behind is the dimmer, cooled version of it. */
    spawn_spray(w, cx, cy, sx, sy, 3, 150.0f, 0.7f,
                (fluid & 0x00FFFFFFu) | 0xC0000000u, 1);

    /* The reward loop: a kill hands back the resources that keep you moving. */
    gain(p, 8.0f + 6.0f * weight);
    p->air_dash = 1;
}

/* Enforcer fire. The numbers are all readability: slow rounds you can watch
 * cross the street, a windup long enough to react to, and a cooldown long
 * enough that a group is a rhythm rather than a wall of lead. */
#define SHOT_SPEED     185.0f
#define SHOT_RANGE     150.0f
#define SHOT_BAND       26.0f     /* vertical slice the cop can actually hit */
#define SHOT_TELEGRAPH   0.30f
#define SHOT_COOLDOWN    1.7f

static void spawn_shot(arc_world *w, float x, float y, float ax, float ay, int friendly, int target)
{
    /* Normalise the aim so a diagonal round is not faster than a straight one. */
    float m = sqrtf(ax * ax + ay * ay);
    if (m < 0.001f) { ax = 1.0f; ay = 0.0f; m = 1.0f; }
    ax /= m; ay /= m;

    for (int i = 0; i < MAX_SHOTS; i++) {
        arc_shot *s = &w->shots[i];
        if (s->alive) continue;
        s->alive = 1;
        s->x = x; s->y = y;
        float sp = SHOT_SPEED * (friendly ? 1.9f : 1.0f);
        s->vx = ax * sp;
        s->vy = ay * sp;
        s->life = 2.5f;
        s->friendly = friendly;
        s->target = target;
        audio_play(SFX_SHOT, 0.5f, 0.95f + 0.1f * (i & 1));
        return;
    }
}

static void shots_update(arc_world *w, float dt)
{
    arc_player *p = &w->p;
    float pcx = p->x + p->w * 0.5f, pcy = p->y + p->h * 0.5f;

    for (int i = 0; i < MAX_SHOTS; i++) {
        arc_shot *s = &w->shots[i];
        if (!s->alive) continue;

        s->life -= dt;

        /* A locked friendly round curves toward its target, so a drone weaving
           overhead still gets hit - this is what makes "shoot the robot"
           actually kill the robot instead of firing past where it was. The
           turn rate is gentle enough that a missed lock still reads as a miss,
           not as a heat-seeker. */
        if (s->friendly && s->target >= 0 && s->target < w->enemy_count &&
            w->enemies[s->target].alive) {
            arc_enemy *t = &w->enemies[s->target];
            float tx = t->x + ENEMY_W * 0.5f - s->x;
            float ty = t->y + ENEMY_H * 0.5f - s->y;
            float tm = sqrtf(tx * tx + ty * ty);
            if (tm > 0.001f) {
                float sp = sqrtf(s->vx * s->vx + s->vy * s->vy);
                float wx = tx / tm * sp, wy = ty / tm * sp;
                float k = 7.0f * dt;               /* steer strength */
                s->vx += (wx - s->vx) * k;
                s->vy += (wy - s->vy) * k;
            }
        }

        s->x += s->vx * dt;
        s->y += s->vy * dt;

        /* Geometry stops it: putting a wall between you and a muzzle is a
           real answer, which is what makes the street layout matter. */
        if (s->life <= 0 ||
            world_tile_solid(w, (int)floorf(s->x / TILE), (int)floorf(s->y / TILE))) {
            s->alive = 0;
            spawn_parts(w, s->x, s->y, 3, 70.0f, 1.0f, rgba(255, 170, 120, 220), 1, 120.0f);
            continue;
        }

        if (s->friendly) {
            /* Nine's rounds. One archetype at a time, and they stop on
               contact - a piercing round would make the gun trivialise the
               groups the levels are built around. */
            for (int j = 0; j < w->enemy_count; j++) {
                arc_enemy *en = &w->enemies[j];
                if (!en->alive) continue;
                float ew = en->kind == EN_WARDEN ? 80.0f : ENEMY_W;
                float eh = en->kind == EN_WARDEN ? 40.0f : ENEMY_H;
                if (s->x > en->x && s->x < en->x + ew &&
                    s->y > en->y && s->y < en->y + eh) {
                    /* A round on a boss's armour clangs off - the gun cannot
                       shortcut the opening the boss demands. */
                    if (en->kind == EN_WARDEN && !boss_vulnerable(w, en, s->x))
                        boss_clang(w, s->x, s->y);
                    else
                        enemy_kill(w, en, s->vx > 0 ? 1.0f : -1.0f, 0.35f);
                    s->alive = 0;
                    break;
                }
            }
            continue;
        }

        if (p->state == PS_DEAD) continue;

        if (fabsf(s->x - pcx) < (p->w * 0.5f + 4.0f) &&
            fabsf(s->y - pcy) < (p->h * 0.5f + 3.0f)) {
            /* Dashing is i-frames (GDD §3.1): the verb is the dodge. */
            if (p->state == PS_DASH) continue;
            s->alive = 0;
            spawn_parts(w, s->x, s->y, 5, 90.0f, 1.0f, rgba(255, 190, 140, 230), 1, 100.0f);

            if (p->invuln > 0) continue;
            p->plates--;
            p->invuln = 1.0f;
            p->vx = (s->vx > 0 ? 1.0f : -1.0f) * 130.0f;
            p->vy = -140.0f;
            w->shake = 0.14f;
            w->hitstop = 0.05f;
            spawn_parts(w, pcx, pcy, 6, 130.0f, 1.0f, rgba(190, 225, 255, 255), 0, 640.0f);
            audio_play(SFX_HURT, 0.8f, 1.05f);

            if (p->plates <= 0) {
                p->state = PS_DEAD;
                p->dead_time = 0;
                w->hitstop = 0.30f;
                w->shake   = 0.45f;
                w->ring_t = 0.34f; w->ring_x = pcx; w->ring_y = pcy;
                spawn_parts(w, pcx, pcy, 22, 240.0f, 1.0f, rgba(210, 235, 255, 255), 0, 700.0f);
                spawn_parts(w, pcx, pcy, 6, 120.0f, 1.0f, rgba(120, 200, 255, 200), 1, 60.0f);
            }
        }
    }
}

/* Movers walk their path on a cosine, so they ease at each end instead of
 * snapping - a platform that reverses instantly throws you off it. */
static void movers_update(arc_world *w, float dt)
{
    for (int i = 0; i < w->mover_count; i++) {
        arc_mover *m = &w->movers[i];
        m->t += m->speed * dt;
        if (m->t > 1.0f) m->t -= 1.0f;
        float k = 0.5f - 0.5f * cosf(m->t * 6.28318f);
        float nx = m->x0 + m->dx * k, ny = m->y0 + m->dy * k;

        /* Whoever is riding this one moves with it. Without the carry you
           slide off the back of every platform, which feels like a bug even
           when the platform is doing exactly what it should. */
        if (w->ride == i) {
            w->p.x += nx - m->x;
            w->p.y += ny - m->y;
        }
        m->x = nx; m->y = ny;
    }
}

/* Beams cycle, and a Pulse drops one for long enough to walk through it. */
static void lasers_update(arc_world *w, float dt)
{
    arc_player *p = &w->p;
    float pcx = p->x + p->w * 0.5f, pcy = p->y + p->h * 0.5f;

    for (int i = 0; i < w->laser_count; i++) {
        arc_laser *l = &w->lasers[i];
        if (l->down > 0) { l->down -= dt; continue; }
        l->phase += dt;
        if (l->phase > l->period) l->phase -= l->period;

        /* On for the first 60% of the cycle. The gap is the whole design: a
           beam that is always on is a wall, and this game does not want
           walls - it wants you to arrive at the right moment. */
        if (l->phase > l->period * 0.6f) continue;

        if (p->state == PS_DEAD || p->invuln > 0 || p->state == PS_DASH) continue;
        if (fabsf(pcx - l->x) > p->w * 0.5f + 3.0f) continue;
        if (pcy < l->y - p->h * 0.5f || pcy > l->y + l->len) continue;

        p->plates--;
        p->invuln = 1.0f;
        p->vy = -190.0f;
        p->vx = (p->facing > 0 ? -1.0f : 1.0f) * 120.0f;
        w->shake = 0.16f;
        w->hitstop = 0.05f;
        audio_play(SFX_HURT, 0.8f, 1.1f);
        spawn_parts(w, pcx, pcy, 6, 130.0f, 1.0f, rgba(255, 160, 190, 255), 0, 640.0f);
        if (p->plates <= 0) { p->state = PS_DEAD; p->dead_time = 0; w->hitstop = 0.30f; }
    }
}

static void enemies_update(arc_world *w, float dt)
{
    arc_player *p = &w->p;
    float pcx = p->x + p->w * 0.5f, pcy = p->y + p->h * 0.5f;

    for (int i = 0; i < w->enemy_count; i++) {
        arc_enemy *en = &w->enemies[i];
        if (en->hit_flash > 0) en->hit_flash -= dt;
        if (en->squash < 0) en->squash += dt * 4.0f; else en->squash = 0;

        /* Dying drones keep simulating: they are knocked off their hover and
           fall, which is what sells the hit as an impact rather than a delete. */
        if (!en->alive) {
            if (en->death_t > 0) {
                en->death_t -= dt;
                en->dvy += 900.0f * dt;
                en->x += en->dvx * dt;
                en->y += en->dvy * dt;
                en->dvx -= en->dvx * 1.6f * dt;
                if (en->death_t <= 0) {
                    /* The pop: it goes out as scrap, not as a fade. */
                    spawn_parts(w, en->x + ENEMY_W * 0.5f, en->y + ENEMY_H * 0.5f,
                                9, 190.0f, 1.0f, rgba(255, 235, 200, 255), 0, 700.0f);
                    spawn_parts(w, en->x + ENEMY_W * 0.5f, en->y + ENEMY_H * 0.5f,
                                2, 40.0f, 1.0f, rgba(255, 170, 110, 160), 1, 20.0f);
                }
            }
            continue;
        }

        if (en->glitch > 0) en->glitch -= dt;

        /* Stunned by a Pulse. Hacking is access, not a weapon: the drone drops
           out of the fight, sags on dying rotors, and cannot touch you - but it
           reboots and goes back to work. Nine opens doors, Nine does not
           execute (GDD §3.1: "you are a courier, not a fighter"). */
        if (en->hacked > 0) {
            en->hacked -= dt;

            en->vx -= en->vx * 4.0f * dt;              /* thrust dies off */
            en->x += en->vx * dt;
            /* A drone sags on dying rotors; a cop just staggers where they
               stand - their visor is fried, not their legs. */
            if (en->kind == EN_SCRAPPER) en->y += 26.0f * dt;

            /* Trailing sparks, thinning as the reboot completes. */
            if (en->hacked < 0.7f && ((int)(w->time * 30.0f) & 7) == 0)
                spawn_parts(w, en->x + ENEMY_W * 0.5f, en->y + ENEMY_H * 0.5f, 1,
                            50.0f, 1.0f, rgba(120, 235, 255, 200), 1, 120.0f);

            if (en->hacked <= 0) {
                /* Back online. Only a flyer climbs back to altitude. */
                if (en->kind == EN_SCRAPPER) en->y = en->home_y;
                en->vx = (en->vx >= 0 ? 1.0f : -1.0f) *
                         (en->kind == EN_COP ? 42.0f : 60.0f);
                en->glitch = 0.2f;
            }
            continue;
        }

        /* Sentries are bolted down. They cover a lane and nothing else, so the
           answer is always a route: go over, go under, or break line of fire.
           A turret you have to fight would just be a cop that cannot walk. */
        if (en->kind == EN_TURRET) {
            if (en->shoot_cd > 0) en->shoot_cd -= dt;
            float ecx = en->x + ENEMY_W * 0.5f, ecy = en->y + ENEMY_H * 0.5f;
            float dxp = pcx - ecx, dyp = pcy - ecy;

            if (en->telegraph > 0) {
                en->telegraph -= dt;
                if (en->telegraph <= 0) {
                    float d = dxp > 0 ? 1.0f : -1.0f;
                    {
                        float dw = TURRET_FRAME_W * 0.5f, dh = TURRET_FRAME_H * 0.5f;
                        float sx = en->x + (ENEMY_W - dw) * 0.5f;
                        float sy = en->y + ENEMY_H - dh;
                        float mx, my;
                        muzzle_world(sx, sy, dw, dh, TURRET_FRAME_W, TURRET_FRAME_H,
                                     TURRET_MUZZLE_X, TURRET_MUZZLE_Y,
                                     d > 0 ? 1 : -1, &mx, &my);
                        spawn_shot(w, mx, my, d, 0.0f, 0, -1);
                    }
                    en->shoot_cd = SHOT_COOLDOWN * 0.8f;
                }
            } else if (fabsf(dxp) < SHOT_RANGE + 40.0f && fabsf(dyp) < SHOT_BAND &&
                       en->shoot_cd <= 0 && p->state != PS_DEAD) {
                en->telegraph = SHOT_TELEGRAPH;
            }
            /* Fall through to the contact check so it can still be cut down. */
        }

        /* RAIL WARDEN. Three beats, and the phase changes what it does rather
           than only how fast: charge, then stand off and shell you, then both
           at once. It is a boss because it survives hits, and it survives hits
           because a single-hit boss is a set piece with no arc. */
        else if (en->kind == EN_WARDEN) {
            en->state_t += dt;
            float frac = en->hp / (float)en->hp_max;
            en->phase = frac > 0.66f ? 0 : (frac > 0.33f ? 1 : 2);

            /* Five bosses, five fights. The variant sets the shape; the phase
               still escalates within it. This is what stops "boss" meaning
               "the same rig with more hp" five times over.
                 0 WARDEN  - the reference: charge, then charge-and-shell
                 1 BOUNCER - fast, low, all charge, almost no shooting
                 2 COIL    - slow and huge, hangs back and shells constantly
                 3 TIDE    - bobs in a wide arc, fires in bursts
                 4 CHORUS  - stationary turret that summons drones */
            int V = en->variant;

            float bw = V == 2 ? 128.0f : (V == 4 ? 22.0f : 48.0f);
            float base = V == 1 ? 260.0f : (V == 2 ? 90.0f : (V == 3 ? 150.0f : 190.0f));
            float speed = base * (en->phase == 0 ? 1.0f : (en->phase == 1 ? 0.8f : 1.3f));

            int shoots = (V == 2) ? 1                       /* COIL always shells */
                       : (V == 4) ? 1                       /* CHORUS always fires */
                       : (V == 1) ? (en->phase >= 2)        /* BOUNCER barely */
                                  : (en->phase >= 1);
            float fire_cd = V == 2 ? 0.9f : (V == 4 ? 0.7f : (en->phase == 2 ? 0.8f : 1.3f));

            /* Movement archetypes. */
            if (V == 4) {
                /* CHORUS is bolted to the arena centre and does not move. */
                en->y = en->home_y + sinf(en->state_t * 2.0f) * 4.0f;
            } else if (V == 2) {
                /* COIL hangs back at half the player's side of the arena and
                   keeps its distance, so the fight is about closing on it. */
                float want = pcx + (pcx > en->x ? -150.0f : 150.0f);
                en->vx = (want > en->x ? 1.0f : -1.0f) * speed;
                en->x += en->vx * dt;
                /* Clamp to the arena like WARDEN/BOUNCER, or the player can
                   herd the 128px rig into a wall and strand it out of reach. */
                float clo = 2.0f * TILE, chi = (w->w - 12) * (float)TILE - bw;
                if (en->x < clo) en->x = clo;
                if (en->x > chi) en->x = chi;
                en->y = en->home_y + sinf(en->state_t * 0.9f) * 6.0f;
            } else if (V == 3) {
                /* TIDE sweeps the whole arena in a wide sine, high then low. */
                float span = (w->w - 20) * (float)TILE;
                float nx = 8.0f * TILE + (0.5f - 0.5f * cosf(en->state_t * 0.7f)) * span;
                en->vx = nx - en->x;    /* keep vx signed by travel so the sprite faces its motion */
                en->x = nx;
                en->y = en->home_y + sinf(en->state_t * 1.8f) * 34.0f;
            } else {
                /* WARDEN and BOUNCER hunt: close, barrel through, re-acquire. */
                float dxp = pcx - (en->x + bw * 0.5f);
                if (fabsf(dxp) > 110.0f)
                    en->vx = (dxp > 0 ? 1.0f : -1.0f) * speed;
                en->x += en->vx * dt;
                float lo = 2.0f * TILE, hi = (w->w - 12) * (float)TILE;
                if (en->x < lo) { en->x = lo; en->vx = speed; }
                if (en->x > hi) { en->x = hi; en->vx = -speed; }
                en->y = en->home_y + sinf(en->state_t * (V == 1 ? 2.2f : 1.3f))
                        * (V == 1 ? 6.0f : 10.0f);
            }

            /* CHORUS summons: a dedicated countdown drops a drone every ~2.6 s,
               up to a live cap, so the caretaker fields the city's own machines
               against you. (The old gate keyed on shoot_cd going below -1.5,
               which it never did - the summon was dead code.) */
            if (V == 4 && en->phase >= 1) {
                en->summon_cd -= dt;
                if (en->summon_cd <= 0.0f) {
                    en->summon_cd = 3.4f;   /* slow enough that 2 drones are always clearable */
                    int live = 0;
                    for (int j = 0; j < w->enemy_count; j++)
                        if (w->enemies[j].kind == EN_SCRAPPER && w->enemies[j].alive) live++;
                    if (live < 2 && w->enemy_count < MAX_ENEMIES) {
                        arc_enemy *nd = &w->enemies[w->enemy_count++];
                        memset(nd, 0, sizeof *nd);
                        nd->kind = EN_SCRAPPER;
                        nd->x = en->x; nd->y = en->y + 20.0f;
                        nd->home_x = nd->x; nd->home_y = nd->y;
                        nd->range = 6.0f * TILE; nd->vx = 70.0f;
                        nd->hp = nd->hp_max = 1; nd->alive = 1;
                        spawn_parts(w, nd->x + 11, nd->y + 10, 6, 90.0f, 1.0f,
                                    rgba(200, 120, 255, 220), 1, 40.0f);
                    }
                }
            }

            if (shoots) {
                if (en->shoot_cd > 0) en->shoot_cd -= dt;
                if (en->telegraph > 0) {
                    en->telegraph -= dt;
                    if (en->telegraph <= 0) {
                        float d = pcx > en->x ? 1.0f : -1.0f;
                        float sx = en->x + bw * 0.5f + d * (bw * 0.4f);
                        float sy = en->y + 14.0f;
                        float aimy = (pcy - sy) / 200.0f;   /* lead toward Nine */

                        /* Each boss fires a different shape, so the fight is a
                           different fight and not the same volley at five sizes:
                             WARDEN  - one aimed round
                             BOUNCER - a fast pair while it barrels in
                             COIL    - a 3-round spread, shotgun from range
                             TIDE    - a 3-round rising fan
                             CHORUS  - a 5-way radial burst */
                        switch (V) {
                        case 2:                             /* COIL spread */
                            spawn_shot(w, sx, sy, d,  0.30f, 0, -1);
                            spawn_shot(w, sx, sy, d,  0.00f, 0, -1);
                            spawn_shot(w, sx, sy, d, -0.30f, 0, -1);
                            break;
                        case 3:                             /* TIDE fan */
                            spawn_shot(w, sx, sy, d, aimy - 0.25f, 0, -1);
                            spawn_shot(w, sx, sy, d, aimy,         0, -1);
                            spawn_shot(w, sx, sy, d, aimy + 0.25f, 0, -1);
                            break;
                        case 4:                             /* CHORUS radial */
                            for (int a = -2; a <= 2; a++)
                                spawn_shot(w, sx, sy, d, a * 0.45f, 0, -1);
                            break;
                        case 1:                             /* BOUNCER pair */
                            spawn_shot(w, sx, sy, d, aimy, 0, -1);
                            spawn_shot(w, sx + d * 8.0f, sy, d, aimy, 0, -1);
                            break;
                        default:                            /* WARDEN aimed */
                            spawn_shot(w, sx, sy, d, aimy, 0, -1);
                        }
                        en->shoot_cd = fire_cd;
                    }
                } else if (en->shoot_cd <= 0 && p->state != PS_DEAD) {
                    en->telegraph = SHOT_TELEGRAPH * 1.3f;
                }
            }

            /* Contact with the rig hurts, but it is never a one-hit kill: it
               shoves you off the rail line instead. */
            float dxb = (en->x + bw * 0.5f) - pcx, dyb = (en->y + 14.0f) - pcy;
            if (fabsf(dxb) < bw * 0.5f + 4.0f && fabsf(dyb) < 22.0f) {
                int wins = p->state == PS_DASH || p->state == PS_STOMP;
                /* Gate on the boss's own hit-flash, not p->invuln: a dash keeps
                   p->invuln refreshed every frame, so without this a single
                   dash chipped the boss once per overlapping frame and killed
                   it in one pass. enemy_kill sets hit_flash ~0.16 s, so this
                   caps it at one chip per dash. */
                if (wins && en->hit_flash <= 0.0f) {
                    if (boss_vulnerable(w, en, pcx)) {
                        enemy_kill(w, en, pcx < en->x ? 1.0f : -1.0f, 1.0f);
                    } else {
                        boss_clang(w, pcx, pcy);
                        en->hit_flash = 0.12f;   /* debounce the clang too */
                    }
                    if (p->state == PS_STOMP) { p->vy = -STOMP_V * 0.6f; p->state = PS_AIR; }
                    p->invuln = 0.5f;
                } else if (!wins && p->invuln <= 0 && p->state != PS_DEAD) {
                    p->plates--;
                    p->invuln = 1.0f;
                    p->vx = (dxb > 0 ? -1.0f : 1.0f) * 220.0f;
                    p->vy = -200.0f;
                    w->shake = 0.24f;
                    w->hitstop = 0.07f;
                    spawn_parts(w, pcx, pcy, 8, 150.0f, 1.0f, rgba(190, 225, 255, 255), 0, 640.0f);
                    if (p->plates <= 0) { p->state = PS_DEAD; p->dead_time = 0; w->hitstop = 0.30f; }
                }
            }
            continue;
        }

        /* Enforcers shoot. They stop to aim, which is both the tell and the
           opening: a cop winding up is a cop not walking, and a cop mid-shot
           is a cop you can close on. */
        int cop_rooted = 0;    /* a cop aiming holds still but stays hittable */
        if (en->kind == EN_COP) {
            if (en->shoot_cd > 0) en->shoot_cd -= dt;

            float ecx = en->x + ENEMY_W * 0.5f, ecy = en->y + ENEMY_H * 0.5f;
            float dxp = pcx - ecx, dyp = pcy - ecy;
            /* An Enforcer that only fires the way it happens to be walking
               spends most of a fight with its back to you, which reads as
               broken rather than as an opening. In range and at its height,
               it turns first - the turn is part of the tell. */
            int in_arc = fabsf(dxp) < SHOT_RANGE && fabsf(dyp) < SHOT_BAND;
            if (in_arc && en->telegraph <= 0 && en->shoot_cd <= 0)
                en->vx = (dxp > 0 ? 1.0f : -1.0f) * fabsf(en->vx);

            if (en->telegraph > 0) {
                en->telegraph -= dt;
                if (en->telegraph <= 0) {
                    {
                        float dw = COP_FRAME_W * 0.5f, dh = COP_FRAME_H * 0.5f;
                        float sx = en->x + (ENEMY_W - dw) * 0.5f;
                        float sy = en->y + ENEMY_H - dh;
                        float mx, my;
                        muzzle_world(sx, sy, dw, dh, COP_FRAME_W, COP_FRAME_H,
                                     COP_MUZZLE_X, COP_MUZZLE_Y,
                                     en->vx > 0 ? 1 : -1, &mx, &my);
                        spawn_shot(w, mx, my, en->vx > 0 ? 1.0f : -1.0f, 0.0f, 0, -1);
                    }
                    en->shoot_cd = SHOT_COOLDOWN;
                }
                cop_rooted = 1;        /* held in place, but still killable below */
            } else if (in_arc && en->shoot_cd <= 0 && p->state != PS_DEAD) {
                en->telegraph = SHOT_TELEGRAPH;
                cop_rooted = 1;
            }
        }

        if (en->kind == EN_TURRET) {
            /* Bolted down, but it still has to find the floor to be bolted
               to: settle it, then never move it again. */
            if (!box_hits_solid(w, en->x, en->y + ENEMY_H, ENEMY_W, 1.0f))
                en->y += 200.0f * dt;
        } else
        /* Cops are ground units: gravity, and they turn at a ledge instead of
           marching off it. Drones only care about walls and their leash. */
        if (en->kind == EN_COP) {
            if (!box_hits_solid(w, en->x, en->y + ENEMY_H, ENEMY_W, 1.0f)) {
                en->y += 200.0f * dt;                  /* settle onto the street */
            } else {
                float ahead = en->vx > 0 ? en->x + ENEMY_W + 2.0f : en->x - 2.0f;
                if (!box_hits_solid(w, ahead, en->y + ENEMY_H, 1.0f, 4.0f))
                    en->vx = -en->vx;
            }
        }

        if (en->kind != EN_TURRET &&
            (box_hits_solid(w, en->x + (en->vx > 0 ? ENEMY_W : -1.0f), en->y + 2.0f, 1.0f, ENEMY_H - 4.0f) ||
             fabsf(en->x - en->home_x) > en->range)) {
            en->vx = -en->vx;
        }
        if (!cop_rooted) en->x += en->vx * dt;

        float dx = (en->x + ENEMY_W * 0.5f) - pcx, dy = (en->y + ENEMY_H * 0.5f) - pcy;
        int overlap = fabsf(dx) < (ENEMY_W + p->w) * 0.42f &&
                     fabsf(dy) < (ENEMY_H + p->h) * 0.42f;
        if (!overlap) continue;

        int player_wins = p->state == PS_DASH || p->state == PS_STOMP ||
                          (p->state == PS_AIR && p->vy > 40.0f && pcy < en->y);
        if (player_wins) {
            /* Stomp lands heavier than a dash-through: it is the verb that
               commits, so it gets the bigger freeze. */
            float weight = p->state == PS_STOMP ? 0.85f : 0.45f;
            enemy_kill(w, en, pcx < en->x + ENEMY_W * 0.5f ? 1.0f : -1.0f, weight);
            if (p->state == PS_STOMP) { p->vy = -STOMP_V * 0.55f; p->state = PS_AIR; }
        } else if (p->invuln <= 0 && p->state != PS_DEAD) {
            p->plates--;
            p->invuln = 1.0f;
            p->vx = (pcx < en->x + ENEMY_W * 0.5f ? -1.0f : 1.0f) * 160.0f;
            p->vy = -180.0f;
            w->shake = 0.15f;
            w->hitstop = 0.05f;

            audio_play(SFX_HURT, 0.8f, 1.05f);
            /* A lost plate is physical: it comes off the chassis as scrap. */
            spawn_parts(w, pcx, pcy, 6, 130.0f, 1.0f, rgba(190, 225, 255, 255), 0, 640.0f);

            if (p->plates <= 0) {
                p->state = PS_DEAD;
                p->dead_time = 0;
                /* Death gets the longest freeze in the game, then the chassis
                   comes apart. GDD §3.5 budgets 0.35 s of hit-stop for it. */
                w->hitstop = 0.30f;
                w->shake   = 0.45f;
                w->ring_t = 0.34f; w->ring_x = pcx; w->ring_y = pcy;
                spawn_parts(w, pcx, pcy, 22, 240.0f, 1.0f, rgba(210, 235, 255, 255), 0, 700.0f);
                spawn_parts(w, pcx, pcy, 6, 120.0f, 1.0f, rgba(120, 200, 255, 200), 1, 60.0f);
            }
        }
    }
}

/* --------------------------------------------------------------- entities */

static void collect(arc_world *w, arc_player *p, float dt)
{
    float cx = p->x + p->w * 0.5f, cy = p->y + p->h * 0.5f;

    for (int i = 0; i < w->ent_count; i++) {
        arc_ent *e = &w->ents[i];
        e->bob += dt * 3.0f;
        if (e->taken) continue;

        float dx = e->x - cx, dy = e->y - cy;
        float d2 = dx * dx + dy * dy;

        switch (e->kind) {
            case E_VOLT:
                /* Magnet radius: collecting should feel like the level is
                   coming to you, not like a pixel-perfect pickup test. */
                if (d2 < 26.0f * 26.0f) {
                    e->x -= dx * fminf(dt * 9.0f, 1.0f);
                    e->y -= dy * fminf(dt * 9.0f, 1.0f);
                }
                if (d2 < 10.0f * 10.0f) {
                    e->taken = 1; w->volts++; gain(p, 2.0f);
                    /* Rising pitch across a trail: collecting a line of Volts
                       turns into a run of notes instead of one tick repeated. */
                    audio_play(SFX_VOLT, 0.5f, 0.9f + 0.02f * (w->volts % 12));
                }
                break;
            case E_ECHO:
                if (d2 < 14.0f * 14.0f) {
                    e->taken = 1; w->echoes++; gain(p, 20.0f);
                    audio_play(SFX_ECHO, 0.75f, 1.0f);
                }
                break;
            case E_CHECKPOINT:
                if (d2 < 18.0f * 18.0f && (w->check_x != e->x || w->check_y != e->y - 8.0f)) {
                    e->taken = 1;                /* drawn lit from now on */
                    w->check_x = e->x - 5.0f;
                    w->check_y = e->y - 8.0f;
                }
                break;
            case E_EXIT: {
                /* The drop will not accept the package while the district's
                   rig is still on the rail. A boss you can simply run past is
                   scenery. */
                int boss_up = 0;
                for (int j = 0; j < w->enemy_count; j++)
                    if (w->enemies[j].kind == EN_WARDEN && w->enemies[j].alive)
                        boss_up = 1;
                if (d2 < 16.0f * 16.0f && !boss_up) w->finished = 1;
                break;
            }
            case E_NODE:
                /* Only live while the window is open; radius is generous
                   because you hit these mid-wall-jump. */
                if (w->hack_t > 0 && d2 < 16.0f * 16.0f) {
                    e->taken = 1;
                    gain(p, 6.0f);
                    spawn_parts(w, e->x, e->y, 6, 90.0f, 1.0f,
                                rgba(120, 240, 255, 220), 1, 60.0f);
                    audio_play(SFX_NODE, 0.7f, 1.0f + 0.12f * (3 - 1));

                    int left = 0;
                    for (int j = 0; j < w->ent_count; j++)
                        if (w->ents[j].kind == E_NODE && !w->ents[j].taken) left++;
                    if (left == 0) {
                        w->door_open = 1;
                        w->hack_t = 0;
                        gain(p, 15.0f);
                        /* The unlock is an event at the DOOR, not at the
                           player: the ring pulls your eye to the thing that
                           just changed. */
                        w->ring_t = 0.34f;
                        w->ring_x = w->door_x; w->ring_y = w->door_y;
                        w->shake = 0.18f;
                        audio_play(SFX_GATE, 0.85f, 1.0f);
                        spawn_parts(w, w->door_x, w->door_y, 14, 170.0f, 1.0f,
                                    rgba(140, 255, 200, 230), 1, 300.0f);
                    }
                }
                break;
            default: break;
        }
    }

    /* The window closes: un-taken nodes reset, terminal re-arms. */
    if (w->hack_t > 0) {
        w->hack_t -= dt;
        if (w->hack_t <= 0 && !w->door_open)
            for (int i = 0; i < w->ent_count; i++)
                if (w->ents[i].kind == E_NODE) w->ents[i].taken = 0;
    }
}

/* ----------------------------------------------------------------- camera */

static void camera_update(arc_world *w, float dt)
{
    arc_player *p = &w->p;

    /* Look-ahead in the direction of travel, scaled by speed: the camera shows
       you where you are going, not where you are. */
    float lead = (p->vx / RUN_MAX) * 36.0f;
    float tx = p->x + p->w * 0.5f + lead - VIEW_W * 0.5f;
    float ty = p->y + p->h * 0.5f - VIEW_H * 0.55f;

    float k = 1.0f - expf(-dt / 0.18f);
    w->cam_x += (tx - w->cam_x) * k;
    w->cam_y += (ty - w->cam_y) * k * 1.4f;

    float max_x = (float)(w->w * TILE - VIEW_W);
    float max_y = (float)(w->h * TILE - VIEW_H);
    if (w->cam_x < 0) w->cam_x = 0;
    if (w->cam_y < 0) w->cam_y = 0;
    if (w->cam_x > max_x) w->cam_x = max_x;
    if (w->cam_y > max_y) w->cam_y = max_y;
}

/* ----------------------------------------------------------------- update */

void world_update(arc_world *w, const arc_input *in, float dt)
{
    if (w->hitstop > 0) { w->hitstop -= dt; return; }
    if (w->finished) return;

    w->time += dt;
    if (w->shake > 0) w->shake -= dt;

    movers_update(w, dt);
    w->ride = -1;                 /* re-established by this frame's landing */
    player_update(w, &w->p, in, dt);
    lasers_update(w, dt);
    enemies_update(w, dt);
    shots_update(w, dt);
    parts_update(w, dt);
    collect(w, &w->p, dt);
    camera_update(w, dt);
}

/* ----------------------------------------------------------------- render */

static void quad(float x, float y, float w, float h, int cell, uint32_t col)
{
    float u0, v0, u1, v1;
    cell_uv(cell, &u0, &v0, &u1, &v1);
    gfx_batch_quad(x, y, w, h, u0, v0, u1, v1, col);
}

/* ------------------------------------------------------------- city atlas
 * Real CC0 art (ansimuz "Warped City" / "Warped City 2") packed by
 * tools/atlaspack.py into assets/atlas_city.png - see docs/ASSETS.md. */

#define FLIP_H 1
#define FLIP_V 2

static void city_uv(arc_rect r, float *u0, float *v0, float *u1, float *v1)
{
    const float px = 0.5f / ATLAS_CITY_W, py = 0.5f / ATLAS_CITY_H;
    *u0 = r.x / (float)ATLAS_CITY_W + px;
    *v0 = r.y / (float)ATLAS_CITY_H + py;
    *u1 = (r.x + r.w) / (float)ATLAS_CITY_W - px;
    *v1 = (r.y + r.h) / (float)ATLAS_CITY_H - py;
}

static void city_quad(float x, float y, float w, float h, arc_rect r, int flags, uint32_t col)
{
    float u0, v0, u1, v1;
    city_uv(r, &u0, &v0, &u1, &v1);
    if (flags & FLIP_H) { float t = u0; u0 = u1; u1 = t; }
    if (flags & FLIP_V) { float t = v0; v0 = v1; v1 = t; }
    gfx_batch_quad(x, y, w, h, u0, v0, u1, v1, col);
}

/* Multiply two packed colours, then shade by which run of the district this
 * is. A district is a place; the levels inside it are hours of one night, so
 * 1-1 sits warmer and brighter than 1-3. That gives four distinct looks per
 * district instead of one repeated four times. */
static uint32_t shade(uint32_t a, uint32_t b, float k)
{
    uint32_t o = 0;
    for (int i = 0; i < 3; i++) {
        int ca = (a >> (i * 8)) & 0xFF, cb = (b >> (i * 8)) & 0xFF;
        int v = (int)(ca * cb / 255.0f * k);
        o |= (uint32_t)(v > 255 ? 255 : v) << (i * 8);
    }
    return o | (a & 0xFF000000u);
}

/* Districts. Each one is a different place at a different hour, which is what
 * five parallax sets buy that re-tinting one skyline never could. The grade
 * lives in the fog quad and the layer tints rather than in the composite
 * shader, so a district costs nothing at runtime and can be changed here. */
typedef struct {
    arc_rect far_r, near_r;
    float    far_factor, near_factor, far_dy;
    uint32_t far_tint, near_tint, fog;
    float    key_x, key_y;      /* key light position, screen space */
    uint32_t key_col;
    uint8_t  key_a;             /* 0 = no key light at all (interiors) */
    float    rain;              /* 0 = indoors. It does not rain in the lab. */

    /* The playfield's own palette. Without this every district shared one
       street and one set of platforms, so only the far background ever
       changed - which is exactly what "every level looks the same" means when
       the thing you are staring at is the ground under your feet. */
    uint32_t road, tile, plat;
} arc_district;

static const arc_district DISTRICTS[] = {
    /* D1 HALCYON NIGHT - moonlit, cold, the reference look. */
    { RECT_SKYLINE_A, RECT_NEAR_BUILDINGS, 0.04f, 0.18f, -32.0f,
      0xFFCDA096u, 0xFFAF8278u, 0x78381814u,
      360.0f, 60.0f, 0xFFEBA078u, 230, 1.00f,
      0xFFFFFFFFu, 0xFFFAF0DCu, 0xFFFFFFFFu },
    /* D2 THE STRIP - street level, signage close, warmer haze. */
    { RECT_D2_BACK, RECT_D2_NEAR, 0.05f, 0.22f, 0.0f,
      0xFFC8B4A0u, 0xFFB49682u, 0x6E4A2820u,
      140.0f, 48.0f, 0xFFC8B4FFu, 150, 0.85f,
      0xFF8CA0D2u, 0xFFA0BEE6u, 0xFF96AAD2u },
    /* D3 THE STACKS - vertical, colder, higher up so the sky is bigger. */
    { RECT_D3_BACK, RECT_D3_MID, 0.03f, 0.16f, 0.0f,
      0xFFDCB496u, 0xFFB4907Au, 0x82503C1Eu,
      620.0f, 44.0f, 0xFFFFD2A0u, 200, 1.15f,
      0xFFD2C8A0u, 0xFFE6D2B4u, 0xFFC8BE9Bu },
    /* D4 THE SHORELINE - dusk. Warm, low sun, the one district that is not
       night; the others only read as night because this one exists. */
    { RECT_D4_BACK, RECT_D4_MID, 0.04f, 0.20f, 0.0f,
      0xFFB4C8FFu, 0xFF96A0E6u, 0x5A2864A0u,
      300.0f, 150.0f, 0xFF64C8FFu, 245, 0.25f,
      0xFF6EA0E6u, 0xFF82B4FFu, 0xFF78AAF0u },
    /* D5 MERIDIAN LAB - interior. No sky, no key light, flat cold fill. */
    { RECT_D5_BACK, RECT_D5_MID, 0.06f, 0.24f, 0.0f,
      0xFFE6D2B4u, 0xFFC8B49Au, 0x6E4A3C28u,
      0.0f, 0.0f, 0u, 0, 0.0f,
      0xFFC8DCB4u, 0xFFDCF0C8u, 0xFFBED2AAu },
};
#define ARC_DISTRICT_COUNT (int)(sizeof(DISTRICTS) / sizeof(DISTRICTS[0]))

/* Parallax draws in screen space (unzoomed projection) so the source art
 * keeps its authored pixel density regardless of the world zoom. */
static void draw_parallax_layer(arc_world *w, arc_rect r, float factor, float y, uint32_t col)
{
    float u0, v0, u1, v1;
    city_uv(r, &u0, &v0, &u1, &v1);

    float start = fmodf(-w->cam_x * factor, (float)r.w);
    if (start > 0) start -= (float)r.w;

    for (float x = start; x < ARC_W; x += (float)r.w)
        gfx_batch_quad(x, y, (float)r.w, (float)r.h, u0, v0, u1, v1, col);
}

/* The hour of this run inside its district: 1.0 at the district's first level,
 * dimming and cooling through to its boss. */
static float world_hour(const arc_world *w)
{
    return 1.06f - 0.09f * (float)(w->index % 4);
}

/* The street and the hack gate, both from packed art: the roadway is the flat
 * banded course of Warped City 2's block, the roadbed the plain wall below it,
 * and the gate is Warped City's roller shutter. Drawn in the city batch. */
static void draw_streets(arc_world *w)
{
    int tx0 = (int)(w->cam_x / TILE) - 1, tx1 = tx0 + VIEW_W / TILE + 3;
    int ty0 = (int)(w->cam_y / TILE) - 1, ty1 = ty0 + VIEW_H / TILE + 3;

    for (int ty = ty0; ty <= ty1; ty++) {
        for (int tx = tx0; tx <= tx1; tx++) {
            char c = tile_at(w, tx, ty);
            float x = tx * TILE - w->cam_x, y = ty * TILE - w->cam_y;

            if (c == 'D') {
                /* The shutter is 48x64; each tile samples a 16x16 window of
                   it, so a gate of any height stays one continuous shutter.
                   Open, it retracts upward - only the top rows still show. */
                char above = tile_at(w, tx, ty - 1);
                int row = above == 'D' ? 1 : 0;
                arc_rect r = { RECT_SHUTTER.x + 16, RECT_SHUTTER.y + (row ? 24 : 4), 16, 16 };
                if (w->door_open) {
                    if (row) continue;             /* rolled up out of sight */
                    r.y = RECT_SHUTTER.y;          /* just the housing */
                    city_quad(x, y, TILE, TILE * 0.5f, r, 0, rgba(255, 255, 255, 255));
                } else {
                    city_quad(x, y, TILE, TILE, r, 0, rgba(210, 220, 245, 255));
                }
                continue;
            }

            if (c == 'K') {
                /* Cover crate: a capped top on a ribbed body, tinted with the
                   district palette but with its own art so cover never reads
                   as raised ground. */
                char above = tile_at(w, tx, ty - 1);
                arc_rect r = (above == 'K') ? RECT_CRATE_MID : RECT_CRATE_TOP;
                city_quad(x, y, TILE, TILE, r, 0,
                          shade(rgba(255, 255, 255, 255),
                                DISTRICTS[w->district % ARC_DISTRICT_COUNT].tile,
                                world_hour(w)));
                continue;
            }

            if (c != 'G') continue;
            char above = tile_at(w, tx, ty - 1);
            int top = above != 'G' && above != '#';
            const arc_district *D = &DISTRICTS[w->district % ARC_DISTRICT_COUNT];
            float k = world_hour(w);
            city_quad(x, y, TILE, TILE, top ? RECT_ROAD_TOP : RECT_ROAD_FILL, 0,
                      shade(top ? rgba(255, 255, 255, 255) : rgba(150, 150, 175, 255),
                            D->road, k));
        }
    }
}

/* Entity art, city-atlas batch. Everything with a real sprite lives here;
 * draw_ents keeps only the additive glows and the few markers that are still
 * pure light (checkpoint, exit, anchor). */
/* Movers use the same one-way platform art as the static ledges, plus a strip
 * of the teal band underneath so a moving platform reads as a machined thing
 * that could move, not a slab that happens to. Drawn in the city batch. */
static void draw_movers(arc_world *w)
{
    for (int i = 0; i < w->mover_count; i++) {
        const arc_mover *m = &w->movers[i];
        float x = m->x - w->cam_x, y = m->y - w->cam_y;
        if (x < -80 || x > VIEW_W + 80) continue;
        for (int t = 0; t < m->tiles; t++) {
            int end = (t == 0) || (t == m->tiles - 1);
            arc_rect r = end ? RECT_PLAT_END : RECT_PLAT_MID;
            int flags = (t == 0) ? FLIP_H : 0;
            city_quad(x + t * TILE, y, TILE, 14.0f, r, flags, rgba(210, 230, 255, 255));
        }
        /* Underglow, so it is legible over dark geometry as it travels. */
        city_quad(x, y + 14.0f - 3, m->tiles * TILE, 3, RECT_ROAD_TOP, 0,
                  rgba(120, 220, 255, 120));
    }
}

/* Beams, in the additive pass. On is a bright column with a hot core; the
 * emitter housing is always drawn so the threat has an obvious source even
 * while the beam is down. */
static void draw_lasers(arc_world *w, int additive)
{
    for (int i = 0; i < w->laser_count; i++) {
        const arc_laser *l = &w->lasers[i];
        float x = l->x - w->cam_x, y = l->y - w->cam_y;
        if (x < -30 || x > VIEW_W + 30) continue;

        int on = l->down <= 0 && l->phase <= l->period * 0.6f;
        float warn = 0.0f;
        if (l->down <= 0 && l->phase > l->period * 0.6f)
            warn = (l->phase - l->period * 0.6f) / (l->period * 0.4f);  /* 0->1 before firing */

        if (additive) {
            if (on) {
                float f = 0.85f + 0.15f * sinf(w->time * 40.0f);
                quad(x - 4, y, 8, l->len, CELL_GLOW, rgba(255, 90, 120, (uint8_t)(150 * f)));
                quad(x - 1.5f, y, 3, l->len, CELL_SOLID, rgba(255, 230, 240, (uint8_t)(220 * f)));
            } else if (warn > 0.55f) {
                /* A thin targeting line in the last moment before it fires. */
                quad(x - 0.5f, y, 1, l->len, CELL_SOLID, rgba(255, 120, 140, 90));
            }
            /* Emitter glow, always. */
            quad(x - 6, y - 8, 12, 10, CELL_GLOW,
                 l->down > 0 ? rgba(120, 200, 255, 120) : rgba(255, 90, 120, 160));
        } else {
            /* Emitter housing. */
            quad(x - 5, y - 8, 10, 8, CELL_SOLID, rgba(60, 40, 52, 255));
            quad(x - 4, y - 3, 8, 3, CELL_SOLID,
                 l->down > 0 ? rgba(120, 200, 255, 255) : rgba(255, 80, 110, 255));
        }
    }
}

static void draw_ents_art(arc_world *w)
{
    for (int i = 0; i < w->ent_count; i++) {
        arc_ent *e = &w->ents[i];
        float x = e->x - w->cam_x, y = e->y - w->cam_y;
        if (x < -40 || x > VIEW_W + 40 || y < -40 || y > VIEW_H + 40) continue;
        float bob = sinf(e->bob) * 1.5f;

        switch (e->kind) {
            case E_VOLT: {
                if (e->taken) break;
                int fr = ((int)(e->bob * 3.0f)) % VOLT_FRAMES;
                arc_rect r = { RECT_VOLT.x + fr * VOLT_FRAME_W, RECT_VOLT.y,
                               VOLT_FRAME_W, VOLT_FRAME_H };
                city_quad(x - VOLT_FRAME_W * 0.25f,
                          y - VOLT_FRAME_H * 0.25f + sinf(e->bob) * 3.0f,
                          VOLT_FRAME_W * 0.5f, VOLT_FRAME_H * 0.5f, r, 0,
                          rgba(255, 255, 255, 255));
                break;
            }
            case E_ECHO: {
                if (e->taken) break;
                int fr = ((int)(w->time * 5.0f) + i) % ECHO_FRAMES;
                arc_rect r = { RECT_ECHO.x + fr * ECHO_FRAME_W, RECT_ECHO.y,
                               ECHO_FRAME_W, ECHO_FRAME_H };
                city_quad(x - ECHO_FRAME_W * 0.35f, y - ECHO_FRAME_H * 0.35f + bob,
                          ECHO_FRAME_W * 0.7f, ECHO_FRAME_H * 0.7f, r, 0,
                          rgba(255, 210, 245, 255));
                break;
            }
            case E_EXIT: {
                /* The drop: a door with the light on behind it, standing on
                   the street. Anchored to the bottom of its tile so the
                   threshold meets the pavement instead of hovering over it. */
                city_quad(x - 12, y + 8.0f - 34.0f, 24, 34, RECT_DROPDOOR, 0,
                          rgba(255, 255, 255, 255));
                break;
            }
            case E_TERMINAL: {
                uint32_t tint = w->door_open   ? rgba(120, 130, 155, 255)
                              : w->hack_t > 0  ? rgba(190, 255, 255, 255)
                                               : rgba(255, 200, 200, 255);
                city_quad(x - 8, y - 8, 16, 15, RECT_TERMINAL, 0, tint);
                break;
            }
            case E_NODE: {
                int live = w->hack_t > 0 && !e->taken;
                int fr = ((int)(w->time * 8.0f) + i) % NODE_FRAMES;
                arc_rect r = { RECT_NODE.x + fr * NODE_FRAME_W, RECT_NODE.y,
                               NODE_FRAME_W, NODE_FRAME_H };
                uint32_t tint = live     ? rgba(255, 255, 255, 255)
                              : e->taken ? rgba(120, 220, 180, 200)
                                         : rgba(80, 90, 115, 255);
                city_quad(x - NODE_FRAME_W * 0.2f, y - NODE_FRAME_H * 0.2f,
                          NODE_FRAME_W * 0.4f, NODE_FRAME_H * 0.4f, r, 0, tint);
                break;
            }
            default: break;
        }
    }
}

/* Real tiles from Warped City 2: lilac top surface, teal pipe band beneath
 * it, purple fill below that - the same silhouette the grey-box had, now in
 * the pack's own palette. Drawn in the city-atlas batch. */
static void draw_tiles(arc_world *w)
{
    int tx0 = (int)(w->cam_x / TILE) - 1, tx1 = tx0 + VIEW_W / TILE + 3;
    int ty0 = (int)(w->cam_y / TILE) - 1, ty1 = ty0 + VIEW_H / TILE + 3;

    for (int ty = ty0; ty <= ty1; ty++) {
        for (int tx = tx0; tx <= tx1; tx++) {
            char c = tile_at(w, tx, ty);
            float x = tx * TILE - w->cam_x, y = ty * TILE - w->cam_y;
            if (c == '#') {
                int top  = !world_tile_solid(w, tx, ty - 1);
                int band = !top && !world_tile_solid(w, tx, ty - 2);
                arc_rect r = top ? RECT_TILE_TOP : (band ? RECT_TILE_BAND : RECT_TILE_FILL);
                /* Cool the lilac top toward the moonlight, darken the fill so
                   big walls recede instead of reading flat. */
                uint32_t col = top  ? rgba(215, 215, 250, 255)
                             : band ? rgba(235, 235, 255, 255)
                                    : rgba(160, 160, 190, 255);
                col = shade(col, DISTRICTS[w->district % ARC_DISTRICT_COUNT].tile,
                            world_hour(w));
                city_quad(x, y, TILE, TILE, r, 0, col);
            }
        }
    }
}

/* One-way platforms, built from Warped City 2's platform block: an end cap at
 * each end (the left one mirrored) and the repeating middle between them. A
 * platform therefore has real ends instead of a slab that stops mid-material,
 * which is most of what makes it read as a built object. Drawn in the city
 * batch, 14 px tall hanging off the tile's top edge - which is exactly where
 * the one-way collision surface is. */
#define PLAT_H 14.0f

static void draw_oneways(arc_world *w)
{
    int tx0 = (int)(w->cam_x / TILE) - 1, tx1 = tx0 + VIEW_W / TILE + 3;
    int ty0 = (int)(w->cam_y / TILE) - 1, ty1 = ty0 + VIEW_H / TILE + 3;

    for (int ty = ty0; ty <= ty1; ty++) {
        for (int tx = tx0; tx <= tx1; tx++) {
            if (tile_at(w, tx, ty) != '=') continue;

            int left  = tile_at(w, tx - 1, ty) == '=';
            int right = tile_at(w, tx + 1, ty) == '=';

            arc_rect r = (left && right) ? RECT_PLAT_MID : RECT_PLAT_END;
            /* The end cap is drawn facing right; mirror it for a left end. A
               single-tile platform gets a right cap and reads fine. */
            int flags = (!left && right) ? FLIP_H : 0;

            city_quad(tx * TILE - w->cam_x, ty * TILE - w->cam_y,
                      TILE, PLAT_H, r, flags,
                      shade(rgba(255, 255, 255, 255),
                            DISTRICTS[w->district % ARC_DISTRICT_COUNT].plat,
                            world_hour(w)));
        }
    }
}


static void draw_player(arc_world *w)
{
    arc_player *p = &w->p;
    if (p->state == PS_DEAD) return;

    arc_rect strip;
    int frames;
    /* Pose beats loop. These four override the run/jump/idle cycle for as long
       as they last, because what Nine's body is doing is most of what a verb
       feels like - the blade read as weak largely because the sprite never
       moved when it swung. Priority is by how much the state matters: hurt,
       then the swing, then the wall, then the stomp. */
    int pose = 0;
    if (p->invuln > 0.72f) {                    /* the first ~0.3 s of a hit */
        strip = RECT_PLAYER_HURT; frames = 1; pose = 1;
    } else if (p->atk_time > 0) {
        strip = RECT_PLAYER_STRIKE; frames = 1; pose = 1;
    } else if (p->state == PS_WALL) {
        strip = RECT_PLAYER_CLIMB; frames = PLAYER_CLIMB_FRAMES;
    } else if (p->state == PS_STOMP) {
        strip = RECT_PLAYER_CROUCH; frames = 1; pose = 1;
    }
    else if (p->anim_cat == ANIM_AIR) { strip = RECT_PLAYER_JUMP; frames = PLAYER_JUMP_FRAMES; }
    else if (p->anim_cat == ANIM_RUN) { strip = RECT_PLAYER_RUN;  frames = PLAYER_RUN_FRAMES;  }
    else                              { strip = RECT_PLAYER_IDLE; frames = PLAYER_IDLE_FRAMES; }
    (void)pose;

    int frame;
    if (p->anim_cat == ANIM_AIR) {
        /* Frame follows vertical velocity, not time: rising, apex and falling
           are distinct poses in the source strip, so this reads correctly
           whether the jump is a hop or a full-height leap. */
        float t = (p->vy + JUMP_V) / (JUMP_V + FALL_MAX);
        t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        frame = (int)(t * (frames - 1) + 0.5f);
    } else {
        float dur = p->anim_cat == ANIM_RUN ? 0.06f : 0.15f;
        frame = ((int)(p->anim_t / dur)) % frames;
    }

    arc_rect r = { strip.x + frame * PLAYER_FRAME_W, strip.y, PLAYER_FRAME_W, PLAYER_FRAME_H };

    float sq = p->squash;
    float dw = PLAYER_FRAME_W * PLAYER_VISUAL_SCALE * (1.0f - sq * 0.35f);
    float dh = PLAYER_FRAME_H * PLAYER_VISUAL_SCALE * (1.0f + sq * 0.5f);
    float cx = p->x + p->w * 0.5f - w->cam_x;
    float feet = p->y + p->h - w->cam_y;
    float x = cx - dw * 0.5f;
    float y = feet - dh;

    uint32_t col = rgba(255, 255, 255, 255);
    if (p->state == PS_DASH) col = rgba(190, 255, 255, 255);
    if (p->invuln > 0 && fmodf(w->time * 20.0f, 2.0f) < 1.0f) col = rgba(255, 130, 130, 255);

    city_quad(x, y, dw, dh, r, p->facing < 0 ? FLIP_H : 0, col);

    /* Wet-street reflection: mirror the sprite about the surface underneath.
       One extra quad per actor buys most of the "rain-slick city" look. */
    float feet_wy = p->y + p->h;
    int ty = (int)(feet_wy / TILE);
    for (int k = 0; k < 4; k++, ty++) {
        if (!world_tile_solid(w, (int)((p->x + p->w * 0.5f) / TILE), ty)) continue;
        float surf = ty * TILE - w->cam_y;
        if (surf < y + dh - 1.0f) break;              /* inside the ground */
        city_quad(x, 2.0f * surf - (y + dh), dw, dh, r,
                  (p->facing < 0 ? FLIP_H : 0) | FLIP_V,
                  rgba(140, 170, 220, 60));
        break;
    }
}

static void draw_enemies(arc_world *w, int additive)
{
    for (int i = 0; i < w->enemy_count; i++) {
        arc_enemy *en = &w->enemies[i];
        if (!en->alive && en->death_t <= 0) continue;

        float x = en->x - w->cam_x, y = en->y - w->cam_y;
        if (x < -40 || x > ARC_W + 40) continue;

        float h = ENEMY_H * (1.0f + en->squash * 0.7f);
        float bw = ENEMY_W * (1.0f - en->squash * 0.5f);
        y += ENEMY_H - h;

        float bob = en->kind == EN_COP ? 0.0f
                  : sinf(w->time * 2.4f + i * 1.7f) * 2.5f;

        /* A dying drone stops hovering, tumbles, and blows out to white just
           before it pops - the flash is what the eye keeps as "it died". */
        float death_k = 0.0f;
        if (!en->alive) {
            death_k = en->death_t / 0.34f;          /* 1 -> 0 */
            bob = 0.0f;
            h  *= 0.55f + 0.45f * death_k;
            bw *= 0.70f + 0.60f * (1.0f - death_k); /* squashes wide as it goes */
        }

        if (additive) {
            /* Every drone carries its own little search-light glow; a dying one
               burns it off in a flare. */
            if (!en->alive) {
                float g = 14.0f + 26.0f * (1.0f - death_k);
                quad(x + bw * 0.5f - g, y + h * 0.5f - g, g * 2, g * 2, CELL_GLOW,
                     rgba(255, 190, 130, (uint8_t)(210 * death_k)));
            } else if (en->hacked > 0) {
                /* Downed: the searchlight turns your colour, and beats faster
                   as the reboot approaches - a timer you read without a number
                   on screen, and a warning that it is about to be hostile. */
                float urgency = 1.0f - en->hacked / PULSE_TIME;
                float beat = 0.6f + 0.4f * sinf(w->time * (10.0f + urgency * 26.0f));
                float g = 11.0f + 5.0f * urgency;
                quad(x + bw * 0.5f - g, y + bob + h * 0.5f - g, g * 2, g * 2, CELL_GLOW,
                     rgba(110, 235, 255, (uint8_t)(150 * beat)));
            } else if (en->kind == EN_WARDEN) {
                /* Beacons that strobe faster as the phase drops, scaled to the
                   boss's own width so a small boss is not haloed like the rig. */
                arc_rect br = boss_rect(en->variant);
                float bw = br.w * 0.5f;
                float rate = 5.0f + en->phase * 5.0f;
                float beat = sinf(w->time * rate) > 0 ? 1.0f : 0.25f;
                quad(x, y - 10, 18, 18, CELL_GLOW,
                     rgba(90, 150, 255, (uint8_t)(150 * beat)));
                quad(x + bw - 18, y - 10, 18, 18, CELL_GLOW,
                     rgba(255, 70, 90, (uint8_t)(150 * (1.25f - beat))));
                if (en->telegraph > 0)
                    quad(x + bw * 0.5f - 16, y, 32, 32, CELL_GLOW, rgba(255, 220, 170, 200));

                /* Read the opening: a cyan aura when the boss can be hit right
                   now, a dim red shield-shimmer when its armour is up. This is
                   what turns "why isn't my attack working" into a puzzle you
                   can see the answer to. */
                if (en->variant != 0) {
                    if (boss_vulnerable(w, en, w->p.x + w->p.w * 0.5f)) {
                        float pulse = 0.6f + 0.4f * sinf(w->time * 9.0f);
                        quad(x - 6, y - 6, bw * 2 + 12, h + 12, CELL_GLOW,
                             rgba(120, 245, 255, (uint8_t)(70 * pulse)));
                    } else {
                        quad(x, y, bw * 2, h, CELL_GLOW, rgba(220, 60, 80, 40));
                    }
                }
            } else if (en->kind == EN_TURRET) {
                quad(x + bw * 0.5f - 7, y + h - 16, 14, 14, CELL_GLOW,
                     en->telegraph > 0 ? rgba(255, 225, 170, 220)
                                       : rgba(255, 190, 60, 70));
            } else if (en->kind == EN_SCRAPPER) {
                quad(x + bw * 0.5f - 10, y + bob + h * 0.5f - 10, 20, 20, CELL_GLOW,
                     en->hit_flash > 0 ? rgba(255, 200, 120, 200) : rgba(255, 90, 90, 70));
            } else {
                /* The Enforcer's visor: a thin hostile red at rest, flaring
                   white-hot through the windup. That flare is the only
                   warning a shot is coming, so it has to be unmissable. */
                if (en->telegraph > 0) {
                    float k = 1.0f - en->telegraph / SHOT_TELEGRAPH;
                    float g = 7.0f + 9.0f * k;
                    quad(x + bw * 0.5f - g, y - g + 2, g * 2, g * 2, CELL_GLOW,
                         rgba(255, 220, 170, (uint8_t)(90 + 165 * k)));
                } else {
                    quad(x + bw * 0.5f - 6, y - 6, 12, 12, CELL_GLOW,
                         en->hit_flash > 0 ? rgba(255, 200, 120, 190) : rgba(255, 70, 70, 60));
                }
            }
        } else {
            int frame;
            arc_rect r;
            if (en->kind == EN_WARDEN) {
                /* Each boss its own sprite. The hull reddens as the phase drops,
                   so damage state reads without the health bar too. CHORUS is a
                   4-frame open/fire cycle; the rest are single frames carried by
                   motion. */
                r = boss_rect(en->variant);
                if (en->variant == 4) {
                    int f = ((int)(w->time * 8.0f)) % CHORUS_FRAMES;
                    r.x = RECT_BOSS_CHORUS.x + f * CHORUS_FRAME_W;
                }
                uint32_t hull = en->phase == 0 ? rgba(255, 255, 255, 255)
                              : en->phase == 1 ? rgba(255, 220, 210, 255)
                                               : rgba(255, 180, 170, 255);
                if (en->hit_flash > 0) hull = rgba(255, 245, 230, 255);
                float dw = r.w * 0.5f, dh = r.h * 0.5f;
                city_quad(x - 4, y + 28.0f - dh, dw, dh, r,
                          en->vx < 0 ? FLIP_H : 0, hull);
                continue;
            }
            if (en->kind == EN_TURRET) {
                /* The head tracks: pick the frame from which way you are. */
                int face = (w->p.x + w->p.w * 0.5f) > en->x ? 1 : 0;
                frame = face ? (TURRET_FRAMES - 1) : 0;
                if (en->telegraph > 0) frame = face ? TURRET_FRAMES - 2 : 1;
                r = (arc_rect){ RECT_TURRET.x + frame * TURRET_FRAME_W,
                                RECT_TURRET.y, TURRET_FRAME_W, TURRET_FRAME_H };
            } else if (en->kind == EN_COP) {
                /* Run strip while walking; idle strip is kept for a future
                   alert state. Frame rate tracks walk speed. */
                frame = ((int)(w->time * 12.0f) + i * 3) % COP_RUN_FRAMES;
                r = (arc_rect){ RECT_COP_RUN.x + frame * COP_FRAME_W,
                                RECT_COP_RUN.y, COP_FRAME_W, COP_FRAME_H };
            } else {
                frame = ((int)(w->time * 10.0f) + i * 3) % DRONE_FRAMES;
                r = (arc_rect){ RECT_DRONE.x + frame * DRONE_FRAME_W,
                                RECT_DRONE.y, DRONE_FRAME_W, DRONE_FRAME_H };
            }
            uint32_t col = en->hit_flash > 0 ? rgba(255, 220, 190, 255) : rgba(255, 255, 255, 255);
            if (en->alive && en->hacked > 0) {
                /* Tinted to the player's cyan so a hijacked drone can never be
                   confused for a hostile one at speed. */
                col = rgba(140, 240, 255, 255);
                /* Entry glitch: the sprite tears sideways for a moment. */
                if (en->glitch > 0) x += sinf(w->time * 90.0f) * en->glitch * 9.0f;
            }
            if (!en->alive) {
                /* Blow toward white as it dies, and fade the last third. */
                uint8_t a = death_k > 0.35f ? 255 : (uint8_t)(255 * (death_k / 0.35f));
                col = rgba(255, 255, 255, a);
            }
            float dw = r.w * 0.5f, dh = r.h * 0.5f;
            /* Drones centre on their box; a cop's boots and a turret's base
               are flush with the bottom of their frames (measured bbox), so
               their sprite bottoms align with the box floor and they stand on
               the street rather than hovering over it. */
            float dy = (en->kind == EN_COP || en->kind == EN_TURRET)
                       ? y + h - dh
                       : y + bob + (h - dh) * 0.5f;
            city_quad(x + (bw - dw) * 0.5f, dy, dw, dh,
                     r, en->vx < 0 ? FLIP_H : 0, col);
        }
    }
}

static void draw_ents(arc_world *w, int additive)
{
    for (int i = 0; i < w->ent_count; i++) {
        arc_ent *e = &w->ents[i];
        float x = e->x - w->cam_x, y = e->y - w->cam_y;
        if (x < -40 || x > ARC_W + 40 || y < -40 || y > ARC_H + 40) continue;

        float bob = sinf(e->bob) * 1.5f;

        switch (e->kind) {
            case E_VOLT: {
                /* Warped City's energy bolt, bobbing. Motion is what reads as
                   "pickup" at 480x272 - a static bright rect reads as a window. */
                if (e->taken) break;
                bob = sinf(e->bob) * 3.0f;
                if (additive)
                    quad(x - 5, y - 5 + bob, 10, 10, CELL_GLOW, rgba(120, 230, 255, 120));
                break;
            }
            case E_ECHO: {
                /* A face behind glass - what an Echo literally is. */
                if (e->taken) break;
                if (additive)
                    quad(x - 20, y - 20 + bob, 40, 40, CELL_GLOW, rgba(255, 120, 220, 150));
                break;
            }
            case E_CHECKPOINT: {
                uint32_t c = e->taken ? rgba(90, 255, 170, 255) : rgba(90, 100, 130, 255);
                if (additive) {
                    if (e->taken) quad(x - 16, y - 24, 32, 48, CELL_GLOW, rgba(90, 255, 170, 110));
                } else {
                    quad(x - 1, y - 12, 2, 24, CELL_SOLID, c);
                    quad(x - 5, y - 12, 10, 5, CELL_SOLID, c);
                }
                break;
            }
            case E_EXIT:
                /* Warm spill from the doorway - the one friendly light in the
                   district, and the thing you steer toward from a screen away. */
                if (additive) {
                    float th = sinf(w->time * 1.6f) * 0.08f + 0.92f;
                    quad(x - 32, y - 40, 64, 64, CELL_GLOW,
                         rgba(255, 214, 140, (uint8_t)(150 * th)));
                }
                break;
            case E_ANCHOR:
                if (additive) quad(x - 12, y - 12, 24, 24, CELL_GLOW, rgba(255, 170, 90, 140));
                else          quad(x - 3, y - 3, 6, 6, CELL_SOLID, rgba(255, 200, 120, 255));
                break;
            case E_TERMINAL: {
                /* Warped City's wall control box. Its glow states carry the
                   whole read: Meridian red while locked, your cyan while the
                   window runs, dark once the gate is open. */
                int live = w->hack_t > 0;
                if (additive) {
                    if (!w->door_open) {
                        float th = live ? sinf(w->time * 14.0f) * 0.3f + 0.7f : 0.5f;
                        quad(x - 11, y - 11, 22, 22, CELL_GLOW,
                             live ? rgba(120, 240, 255, (uint8_t)(180 * th))
                                  : rgba(255, 90, 90, (uint8_t)(120 * th)));
                    }
                }
                break;
            }
            case E_NODE: {
                /* Cyberpunk City 2's LED panel: already cyan, already a
                   4-frame pulse. Dark until the terminal wakes it. */
                int live = w->hack_t > 0 && !e->taken;
                if (additive) {
                    if (live) {
                        float th = sinf(w->time * 10.0f + i) * 0.25f + 0.75f;
                        quad(x - 16, y - 18, 32, 36, CELL_GLOW,
                             rgba(120, 240, 255, (uint8_t)(170 * th)));
                    } else if (e->taken) {
                        quad(x - 10, y - 12, 20, 24, CELL_GLOW, rgba(140, 255, 200, 80));
                    }
                }
                break;
            }
            default: break;
        }
    }
}

/* The district's rig, if it is still on the rail. */
static arc_enemy *live_boss(arc_world *w)
{
    for (int i = 0; i < w->enemy_count; i++)
        if (w->enemies[i].kind == EN_WARDEN && w->enemies[i].alive)
            return &w->enemies[i];
    return NULL;
}

/* Boss bar, split across the two batches it needs. Only drawn when there is
   actually a boss: a permanent empty bar is UI for a thing that is not there. */
static void draw_boss_bar(arc_world *w)
{
    arc_enemy *b = live_boss(w);
    if (!b) return;
    float bw = 200.0f, bx = (ARC_W - bw) * 0.5f, by = ARC_H - 18.0f;
    quad(bx - 1, by - 1, bw + 2, 8, CELL_SOLID, rgba(30, 16, 22, 220));
    quad(bx, by, bw * b->hp / (float)b->hp_max, 6, CELL_SOLID,
         rgba(255, 80, 90, 245));
}

static void draw_boss_name(arc_world *w)
{
    if (!live_boss(w)) return;
    static const char *NAMES[5] = {
        "RAIL WARDEN", "THE BOUNCER", "MAMA COIL", "TIDE BREAKER", "THE CHORUS"
    };
    const char *nm = NAMES[live_boss(w)->variant];
    font_text((ARC_W - font_width(1, nm)) * 0.5f, ARC_H - 30, 1,
              rgba(255, 190, 190, 235), nm);
}

static void draw_hud(arc_world *w)
{
    char buf[64];
    arc_player *p = &w->p;

    snprintf(buf, sizeof buf, "VOLTS %d OF %d", w->volts, w->volts_total);
    font_text(8, 8, 1, rgba(190, 210, 235, 220), buf);

    snprintf(buf, sizeof buf, "ECHOES %d OF %d", w->echoes, w->echoes_total);
    font_text(8, 16, 1, rgba(255, 150, 220, 220), buf);

    snprintf(buf, sizeof buf, "CHARGE %d", (int)p->charge);
    font_text(8, 24, 1, p->charge > 40.0f ? rgba(120, 230, 255, 220)
                                          : rgba(255, 140, 120, 230), buf);

    /* Where you are in the campaign, top centre. A game with twenty levels has
       to answer "which one is this" without the player counting. */
    snprintf(buf, sizeof buf, "%s  %s   %d OF %d",
             w->id, w->title, w->index + 1, w->level_count);
    font_text((ARC_W - font_width(1, buf)) * 0.5f, 8, 1,
              rgba(175, 200, 230, 210), buf);

    snprintf(buf, sizeof buf, "TIME %d", (int)w->time);
    font_text(ARC_W - font_width(1, buf) - 8, 8, 1, rgba(190, 210, 235, 200), buf);

    if (w->flow > 1.0f) {
        snprintf(buf, sizeof buf, "FLOW %d", (int)w->flow);
        font_text(ARC_W - font_width(1, buf) - 8, 16, 1, rgba(255, 220, 130, 230), buf);
    }

    if (w->finished) {
        /* The manifest line. A courier game's results screen should say who
           was waiting, not just how fast you ran. */
        static const char *DROPS[] = {
            "ITO VASHTI - NOODLE COUNTER, SECTOR 7 UNDERPASS",
            "DEAD LETTER DROP - LAUNDRY LINE, THE STRIP",
            "COIL FAMILY HOIST - STACK 9, LEVEL 200",
            "A HOUSE ON THE CAUSEWAY - THE LIGHT WAS ON",
            "ADA KESTREL - ADDRESSED TO NOWHERE",
        };
        const char *t = "DELIVERED";
        font_text((ARC_W - font_width(3, t)) * 0.5f, ARC_H * 0.4f - 16, 3,
                  rgba(255, 245, 220, 255), t);

        const char *drop = DROPS[w->district % ARC_DISTRICT_COUNT];
        font_text((ARC_W - font_width(1, drop)) * 0.5f, ARC_H * 0.4f + 8, 1,
                  rgba(255, 214, 140, 235), drop);
        snprintf(buf, sizeof buf, "TIME %d DEATHS %d VOLTS %d OF %d ECHOES %d OF %d",
                 (int)w->time, w->deaths, w->volts, w->volts_total,
                 w->echoes, w->echoes_total);
        font_text((ARC_W - font_width(1, buf)) * 0.5f, ARC_H * 0.4f + 24, 1,
                  rgba(200, 220, 240, 230), buf);

        const char *again = "ENTER  NEXT DELIVERY        R  RUN IT AGAIN";
        font_text((ARC_W - font_width(1, again)) * 0.5f, ARC_H * 0.4f + 40, 1,
                  rgba(150, 175, 205, 200), again);
    }
}

static void draw_plates(arc_world *w)
{
    /* Three ticks, not a bar - damage state should be legible at a glance and
       never need a number (GDD §3.2). Screen space, drawn in the atlas batch. */
    for (int i = 0; i < 3; i++) {
        uint32_t col = i < w->p.plates ? rgba(230, 240, 255, 230) : rgba(60, 66, 84, 150);
        quad((float)(ARC_W - 14 - i * 8), 24.0f, 6.0f, 6.0f, CELL_SOLID, col);
    }
}

static void draw_hints(arc_world *w)
{
    /* In-world tutorial text, per the GDD's "teach in safety" rule: it lives
       where the mechanic is first free to try, and is never repeated. */
    static const char *HINTS[] = { "Z JUMP   X DASH   C TETHER   V BLADE   F HACK" };

    for (int i = 0; i < w->ent_count; i++) {
        arc_ent *e = &w->ents[i];
        if (e->kind != E_HINT) continue;
        /* World-anchored text in a screen-space batch: scale by the zoom. */
        float x = (e->x - w->cam_x) * ZOOM, y = (e->y - w->cam_y) * ZOOM;
        if (x < -200 || x > ARC_W + 200) continue;
        font_text(x - font_width(1, HINTS[0]) * 0.5f, y, 1,
                  rgba(150, 175, 205, 200), HINTS[0]);
    }
}

/* Debris, in the procedural-atlas batch. Sparks are drawn in the additive
 * pass so they bloom; scrap is drawn solid so it reads as matter. */
static void draw_parts(arc_world *w, int additive)
{
    for (int i = 0; i < MAX_PARTICLES; i++) {
        arc_particle *q = &w->parts[i];
        if (q->life <= 0 || q->glow != additive) continue;

        float x = q->x - w->cam_x, y = q->y - w->cam_y;
        if (x < -20 || x > VIEW_W + 20) continue;

        float k = q->life / q->life0;                  /* 1 -> 0 */

        /* A decal has already landed: it dries out, it does not shrink. Only
           the last third of its life is spent fading, so the splatter reads as
           a mark on the street rather than a particle that forgot to die. */
        float s = q->size;
        float a = k;
        if (q->stick == 2) a = k > 0.35f ? 1.0f : k / 0.35f;
        else               s *= additive ? k : 0.45f + 0.55f * k;

        uint32_t c = (q->col & 0x00FFFFFFu) |
                     ((uint32_t)((q->col >> 24) * a) << 24);
        quad(x - s * 0.5f, y - s * 0.5f, s, s, additive ? CELL_GLOW : CELL_SOLID, c);
    }
}

/* The muzzle. Nine's sprite carries the firing pose, so this only has to sell
 * the moment the round leaves: a hot flash at the barrel that dies inside two
 * frames, plus a short recoil streak behind it. A long effect here would
 * outlast the shot it belongs to and read as a beam. */
static void draw_muzzle(arc_world *w)
{
    arc_player *p = &w->p;
    if (p->atk_time <= 0 || p->state == PS_DEAD) return;

    float elapsed = ATK_TOTAL - p->atk_time;
    if (elapsed > 0.12f) return;
    float k = 1.0f - elapsed / 0.12f;          /* 1 -> 0 */

    /* The flash sits on the same measured barrel the round leaves from, so
       the two never disagree about where the gun is. */
    float dw = PLAYER_FRAME_W * PLAYER_VISUAL_SCALE;
    float dh = PLAYER_FRAME_H * PLAYER_VISUAL_SCALE;
    float mx, my;
    muzzle_world(p->x + p->w * 0.5f - dw * 0.5f, p->y + p->h - dh, dw, dh,
                 PLAYER_FRAME_W, PLAYER_FRAME_H,
                 NINE_MUZZLE_X, NINE_MUZZLE_Y, p->facing, &mx, &my);
    float bx = mx - w->cam_x;
    float cy = my - w->cam_y;

    float g = 9.0f + 7.0f * k;
    quad(bx - g * 0.5f, cy - g * 0.5f, g, g, CELL_GLOW,
         rgba(255, 226, 150, (uint8_t)(235 * k)));
    quad(bx - 2.0f, cy - 2.0f, 4.0f, 4.0f, CELL_GLOW,
         rgba(255, 255, 245, (uint8_t)(255 * k)));

    /* Recoil streak, behind the barrel and fading faster than the flash. */
    float sl = 12.0f * k;
    quad(bx - (p->facing > 0 ? sl : 0.0f), cy - 1.5f, sl, 3.0f, CELL_GLOW,
         rgba(255, 190, 110, (uint8_t)(150 * k * k)));
}

/* Enemy rounds. Same bolt sprite as a Volt, tinted hot orange - cyan is
 * yours to collect, orange is theirs to dodge, and the colour split is the
 * whole read at 480x272. Drawn in the city batch (art) plus a glow pass. */
static void draw_shots_art(arc_world *w)
{
    for (int i = 0; i < MAX_SHOTS; i++) {
        arc_shot *s = &w->shots[i];
        if (!s->alive) continue;
        float x = s->x - w->cam_x, y = s->y - w->cam_y;
        if (x < -20 || x > VIEW_W + 20) continue;

        int fr = ((int)(w->time * 18.0f) + i) % VOLT_FRAMES;
        arc_rect r = { RECT_VOLT.x + fr * VOLT_FRAME_W, RECT_VOLT.y,
                       VOLT_FRAME_W, VOLT_FRAME_H };
        /* Cyan is Nine's, orange is theirs - the same split the Volts and the
           hostile rounds already use, so one glance sorts the screen. */
        city_quad(x - VOLT_FRAME_W * 0.3f, y - VOLT_FRAME_H * 0.3f,
                  VOLT_FRAME_W * 0.6f, VOLT_FRAME_H * 0.6f, r,
                  s->vx < 0 ? FLIP_H : 0,
                  s->friendly ? rgba(150, 240, 255, 255) : rgba(255, 150, 90, 255));
    }
}

static void draw_shots_glow(arc_world *w)
{
    for (int i = 0; i < MAX_SHOTS; i++) {
        arc_shot *s = &w->shots[i];
        if (!s->alive) continue;
        float x = s->x - w->cam_x, y = s->y - w->cam_y;
        if (x < -20 || x > VIEW_W + 20) continue;
        quad(x - 7, y - 7, 14, 14, CELL_GLOW,
             s->friendly ? rgba(120, 230, 255, 150) : rgba(255, 140, 70, 150));
    }
}

/* The impact ring: one expanding, thinning circle at the last kill. Cheap,
 * and it is what makes a hit land in the world rather than on the sprite. */
static void draw_impact(arc_world *w)
{
    if (w->ring_t <= 0) return;
    float k = w->ring_t / 0.34f;                       /* 1 -> 0 */
    float r = 10.0f + 46.0f * (1.0f - k);
    quad(w->ring_x - w->cam_x - r, w->ring_y - w->cam_y - r, r * 2, r * 2,
         CELL_GLOW, rgba(255, 225, 190, (uint8_t)(170 * k * k)));
}

/* The Pulse wavefront. It expands to exactly PULSE_RANGE and stops, so the
 * player learns the verb's reach by watching it rather than by missing. */
static void draw_emp(arc_world *w)
{
    if (w->emp_t <= 0) return;
    float k = w->emp_t / 0.30f;                        /* 1 -> 0 */
    float r = PULSE_RANGE * (1.0f - k * k);
    quad(w->emp_x - w->cam_x - r, w->emp_y - w->cam_y - r, r * 2, r * 2,
         CELL_GLOW, rgba(120, 235, 255, (uint8_t)(150 * k)));
}


/* The opening. Six cards over the first street, the world running behind them
 * so the city is the backdrop rather than a black screen. Every card is
 * skippable and the whole thing is skippable, because the pillar is that no
 * cutscene exists that cannot be cut (GDD design pillars). */
static const char *const INTRO[][2] = {
    { "HALCYON CITY.",            "YEAR 47 AFTER THE QUIET." },
    { "MERIDIAN BROADCAST SELLS THE HUM:",
      "THE SIGNAL THAT FILES AWAY WHAT YOU CANNOT CARRY." },
    { "A DEBT. A GRIEF. A FACE.",
      "PEOPLE WAKE UP FINE. NOBODY WRITES ANYTHING DOWN." },
    { "COURIERS MOVE WHAT IS PHYSICAL,",
      "BECAUSE THE HUM CANNOT QUIETLY EDIT IT." },
    { "YOU ARE NINE. A RENTED CHASSIS.",
      "CHASSIS ARE MEANT TO BE EMPTY. YOURS IS NOT." },
    { "THE PACKAGE IS A DEAD WOMAN'S MEMORY.",
      "DELIVER IT." },
};
#define ARC_INTRO_CARDS (int)(sizeof(INTRO) / sizeof(INTRO[0]))

int world_intro_cards(void) { return ARC_INTRO_CARDS; }

void world_render_intro(arc_world *w, const arc_texture *atlas,
                        const arc_shader *sprite, int card, float t)
{
    if (card < 0 || card >= ARC_INTRO_CARDS) return;

    /* Fade each card in fast and hold; the last one holds longest because it
       is the one the player is meant to leave the screen carrying. */
    float a = t < 0.4f ? t / 0.4f : 1.0f;
    if (a > 1.0f) a = 1.0f;

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    gfx_batch_begin(sprite, atlas, ARC_W, ARC_H);
    quad(0, 0, ARC_W, ARC_H, CELL_SOLID, rgba(6, 7, 16, (uint8_t)(205 * a)));
    gfx_batch_end();

    gfx_batch_begin(sprite, font_texture(), ARC_W, ARC_H);
    uint8_t ta = (uint8_t)(245 * a);
    const char *l0 = INTRO[card][0], *l1 = INTRO[card][1];
    font_text((ARC_W - font_width(1, l0)) * 0.5f, ARC_H * 0.44f, 1,
              rgba(235, 240, 255, ta), l0);
    font_text((ARC_W - font_width(1, l1)) * 0.5f, ARC_H * 0.44f + 14, 1,
              rgba(170, 195, 230, ta), l1);

    const char *skip = "ANY KEY";
    font_text(ARC_W - font_width(1, skip) - 10, ARC_H - 16, 1,
              rgba(120, 140, 175, (uint8_t)(150 * a)), skip);
    gfx_batch_end();
}

int world_boss_active(const arc_world *w)
{
    for (int i = 0; i < w->enemy_count; i++)
        if (w->enemies[i].kind == EN_WARDEN && w->enemies[i].alive) return 1;
    return 0;
}

float world_rain(const arc_world *w)
{
    return DISTRICTS[w->district % ARC_DISTRICT_COUNT].rain;
}

void world_render(arc_world *w, const arc_texture *atlas, const arc_texture *city,
                  const arc_shader *sprite)
{
    const arc_district *D = &DISTRICTS[w->district % ARC_DISTRICT_COUNT];

    /* Sky and far skyline, screen-space so the art keeps its pixel density. */
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    gfx_batch_begin(sprite, city, ARC_W, ARC_H);
    float hour = world_hour(w);
    draw_parallax_layer(w, D->far_r, D->far_factor,
                        (float)ARC_H - D->far_r.h + D->far_dy,
                        shade(D->far_tint, 0xFFFFFFFFu, hour));
    gfx_batch_end();

    /* Near buildings, dimmed and cooled. GDD §6.2: the gameplay layer must be
       the brightest and least busy thing on screen, and these facades are the
       busiest art in the game. */
    gfx_batch_begin(sprite, city, ARC_W, ARC_H);
    draw_parallax_layer(w, D->near_r, D->near_factor,
                        (float)ARC_H - D->near_r.h,
                        shade(D->near_tint, 0xFFFFFFFFu, hour));
    gfx_batch_end();

    /* Fog plane between the background and the gameplay plane - the cheapest
       depth in games, and what actually separates the two reads. Its colour is
       the district's whole time-of-day in one quad. */
    gfx_batch_begin(sprite, atlas, ARC_W, ARC_H);
    quad(0, 0, ARC_W, ARC_H, CELL_SOLID, D->fog);
    gfx_batch_end();

    /* The key light, drawn after the fog so it reads as glowing through the
       haze: a moon over the night districts, a low sun over the shoreline,
       and nothing at all inside the lab. */
    if (D->key_a) {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        gfx_batch_begin(sprite, atlas, ARC_W, ARC_H);
        quad(D->key_x - 110, D->key_y - 110, 220, 220, CELL_GLOW,
             (D->key_col & 0x00FFFFFFu) | ((uint32_t)(D->key_a * 0.39f) << 24));
        quad(D->key_x - 55, D->key_y - 55, 110, 110, CELL_GLOW,
             (D->key_col & 0x00FFFFFFu) | ((uint32_t)(D->key_a * 0.65f) << 24));
        quad(D->key_x - 23, D->key_y - 23, 46, 46, CELL_GLOW,
             rgba(255, 255, 255, D->key_a));
        gfx_batch_end();
    }

    /* World: streets first (procedural atlas), then the building tiles and
       actors (city atlas). The reflection quads live inside draw_player, so
       actors draw after tiles. */
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    gfx_batch_begin(sprite, city, VIEW_W, VIEW_H);
    draw_streets(w);
    draw_tiles(w);
    draw_oneways(w);
    draw_movers(w);
    draw_player(w);
    draw_enemies(w, 0);
    draw_ents_art(w);
    draw_shots_art(w);
    draw_lasers(w, 0);
    gfx_batch_end();

    /* Procedural gameplay markers: pickups, checkpoints. */
    gfx_batch_begin(sprite, atlas, VIEW_W, VIEW_H);
    draw_ents(w, 0);
    draw_parts(w, 0);
    gfx_batch_end();

    /* Additive glow layer, world space. */
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    gfx_batch_begin(sprite, atlas, VIEW_W, VIEW_H);
    draw_ents(w, 1);
    draw_enemies(w, 1);
    draw_muzzle(w);
    draw_impact(w);
    draw_emp(w);
    draw_lasers(w, 1);
    draw_shots_glow(w);
    draw_parts(w, 1);
    if (w->p.state != PS_DEAD) {
        arc_player *p = &w->p;
        float g = 24.0f + p->charge * 0.25f;
        quad(p->x + p->w * 0.5f - g - w->cam_x, p->y + p->h * 0.5f - g - w->cam_y,
             g * 2, g * 2, CELL_GLOW, rgba(90, 190, 255, 60));
    }
    gfx_batch_end();

    /* HUD and text, screen space. */
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (!w->hide_hud) {
        gfx_batch_begin(sprite, atlas, ARC_W, ARC_H);
        draw_plates(w);
        draw_boss_bar(w);
        gfx_batch_end();
    }

    if (!w->hide_hud) {
        gfx_batch_begin(sprite, font_texture(), ARC_W, ARC_H);
        draw_hints(w);
        draw_hud(w);
        draw_boss_name(w);
        gfx_batch_end();
    }
}
