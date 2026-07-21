#include "game.h"
#include "font.h"
#include "levels.h"
#include "atlas_city.h"

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
    return tile_at(w, tx, ty) == '#';
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
                case 'z':
                    if (w->enemy_count < MAX_ENEMIES) {
                        arc_enemy *en = &w->enemies[w->enemy_count++];
                        memset(en, 0, sizeof *en);
                        en->kind = EN_SCRAPPER;
                        en->x = tx * TILE; en->y = ty * TILE;
                        en->home_x = en->x;
                        en->range = 3.0f * TILE;
                        en->vx = 60.0f;
                        en->alive = 1;
                    }
                    break;
                default: break;
            }
        }
    }

    w->check_x = w->spawn_x;
    w->check_y = w->spawn_y;
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

    /* Popcorn enemies respawn with the player - a flow game's obstacles are
       part of the route, and a cleared route retried is a different level. */
    for (int i = 0; i < w->enemy_count; i++) {
        w->enemies[i].alive = 1;
        w->enemies[i].x = w->enemies[i].home_x;
    }
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
    }
    return landed;
}

static int on_ground(const arc_world *w, const arc_player *p)
{
    float y = p->y + p->h + 1.0f;
    if (box_hits_solid(w, p->x, p->y + 1.0f, p->w, p->h)) return 1;

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
    int landed = move_y(w, p, p->vy * dt);

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

/* ----------------------------------------------------------------- enemies
 * Most of the roster is a bounce target, not a fight - see GDD §5. A
 * Scrapper is a hovering drone: it patrols its range in the air, turns at
 * walls, and dies to Dash, Stomp or a falling bounce; touching it any other
 * way costs a plate. */

/* Hitbox roughly matches the drawn drone (27x26 world px). A hitbox much
 * smaller than the sprite reads as the game cheating. */
#define ENEMY_W 22.0f
#define ENEMY_H 20.0f

static void enemies_update(arc_world *w, float dt)
{
    arc_player *p = &w->p;
    float pcx = p->x + p->w * 0.5f, pcy = p->y + p->h * 0.5f;

    for (int i = 0; i < w->enemy_count; i++) {
        arc_enemy *en = &w->enemies[i];
        if (en->hit_flash > 0) en->hit_flash -= dt;
        if (en->squash < 0) en->squash += dt * 4.0f; else en->squash = 0;
        if (!en->alive) continue;

        if (box_hits_solid(w, en->x + (en->vx > 0 ? ENEMY_W : -1.0f), en->y + 2.0f, 1.0f, ENEMY_H - 4.0f) ||
            fabsf(en->x - en->home_x) > en->range) {
            en->vx = -en->vx;
        }
        en->x += en->vx * dt;

        float dx = (en->x + ENEMY_W * 0.5f) - pcx, dy = (en->y + ENEMY_H * 0.5f) - pcy;
        int overlap = fabsf(dx) < (ENEMY_W + p->w) * 0.42f &&
                     fabsf(dy) < (ENEMY_H + p->h) * 0.42f;
        if (!overlap) continue;

        int player_wins = p->state == PS_DASH || p->state == PS_STOMP ||
                          (p->state == PS_AIR && p->vy > 40.0f && pcy < en->y);
        if (player_wins) {
            en->alive = 0;
            en->squash = -0.6f;
            gain(p, 8.0f);
            if (p->state == PS_STOMP) { p->vy = -STOMP_V * 0.55f; p->state = PS_AIR; }
        } else if (p->invuln <= 0 && p->state != PS_DEAD) {
            p->plates--;
            p->invuln = 1.0f;
            p->vx = (pcx < en->x + ENEMY_W * 0.5f ? -1.0f : 1.0f) * 160.0f;
            p->vy = -180.0f;
            w->shake = 0.15f;
            w->hitstop = 0.05f;
            if (p->plates <= 0) { p->state = PS_DEAD; p->dead_time = 0; }
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
                if (d2 < 10.0f * 10.0f) { e->taken = 1; w->volts++; gain(p, 2.0f); }
                break;
            case E_ECHO:
                if (d2 < 14.0f * 14.0f) { e->taken = 1; w->echoes++; gain(p, 20.0f); }
                break;
            case E_CHECKPOINT:
                if (d2 < 18.0f * 18.0f && (w->check_x != e->x || w->check_y != e->y - 8.0f)) {
                    e->taken = 1;                /* drawn lit from now on */
                    w->check_x = e->x - 5.0f;
                    w->check_y = e->y - 8.0f;
                }
                break;
            case E_EXIT:
                if (d2 < 16.0f * 16.0f) w->finished = 1;
                break;
            default: break;
        }
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

    player_update(w, &w->p, in, dt);
    enemies_update(w, dt);
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
                city_quad(x, y, TILE, TILE, r, 0, col);
            }
        }
    }
}

/* One-way platforms: a slice of the teal pipe band, so they read as part of
 * the same construction kit as the ground. Drawn in the city batch. */
static void draw_oneways(arc_world *w)
{
    int tx0 = (int)(w->cam_x / TILE) - 1, tx1 = tx0 + VIEW_W / TILE + 3;
    int ty0 = (int)(w->cam_y / TILE) - 1, ty1 = ty0 + VIEW_H / TILE + 3;

    arc_rect r = { RECT_TILE_BAND.x, RECT_TILE_BAND.y + 4, 16, 6 };
    for (int ty = ty0; ty <= ty1; ty++)
        for (int tx = tx0; tx <= tx1; tx++)
            if (tile_at(w, tx, ty) == '=')
                city_quad(tx * TILE - w->cam_x, ty * TILE - w->cam_y,
                          TILE, 6, r, 0, rgba(255, 255, 255, 255));
}

/* Sprite is bigger than the collision box on purpose - a forgiving hitbox
   read against a character that fills the frame the way Rayman's does. */
#define PLAYER_VISUAL_SCALE 0.5f

static void draw_player(arc_world *w)
{
    arc_player *p = &w->p;
    if (p->state == PS_DEAD) return;

    arc_rect strip;
    int frames;
    if (p->anim_cat == ANIM_AIR)      { strip = RECT_PLAYER_JUMP; frames = PLAYER_JUMP_FRAMES; }
    else if (p->anim_cat == ANIM_RUN) { strip = RECT_PLAYER_RUN;  frames = PLAYER_RUN_FRAMES;  }
    else                              { strip = RECT_PLAYER_IDLE; frames = PLAYER_IDLE_FRAMES; }

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
        if (!en->alive && en->squash == 0) continue;

        float x = en->x - w->cam_x, y = en->y - w->cam_y;
        if (x < -40 || x > ARC_W + 40) continue;

        float h = ENEMY_H * (1.0f + en->squash * 0.7f);
        float bw = ENEMY_W * (1.0f - en->squash * 0.5f);
        y += ENEMY_H - h;

        float bob = sinf(w->time * 2.4f + i * 1.7f) * 2.5f;

        if (additive) {
            /* Every drone carries its own little search-light glow. */
            quad(x + bw * 0.5f - 10, y + bob + h * 0.5f - 10, 20, 20, CELL_GLOW,
                 en->hit_flash > 0 ? rgba(255, 200, 120, 200) : rgba(255, 90, 90, 70));
        } else {
            int frame = ((int)(w->time * 10.0f) + i * 3) % DRONE_FRAMES;
            arc_rect r = { RECT_DRONE.x + frame * DRONE_FRAME_W, RECT_DRONE.y,
                          DRONE_FRAME_W, DRONE_FRAME_H };
            uint32_t col = en->hit_flash > 0 ? rgba(255, 220, 190, 255) : rgba(255, 255, 255, 255);
            float dw = DRONE_FRAME_W * 0.5f, dh = DRONE_FRAME_H * 0.5f;
            city_quad(x + (bw - dw) * 0.5f, y + bob + (h - dh) * 0.5f, dw, dh,
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
            case E_VOLT:
                /* Small core, small glow, big bob: reads as a pickup through
                   motion, not brightness - bright static rects read as
                   background windows. */
                if (e->taken) break;
                bob = sinf(e->bob) * 3.0f;
                if (additive) quad(x - 4, y - 4 + bob, 8, 8, CELL_GLOW, rgba(120, 230, 255, 110));
                else          quad(x - 1.0f, y - 2.0f + bob, 2, 4, CELL_SOLID, rgba(210, 250, 255, 255));
                break;
            case E_ECHO:
                if (e->taken) break;
                if (additive) quad(x - 20, y - 20 + bob, 40, 40, CELL_GLOW, rgba(255, 120, 220, 170));
                else          quad(x - 4, y - 5 + bob, 8, 10, CELL_SOLID, rgba(255, 190, 240, 255));
                break;
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
                if (additive) quad(x - 26, y - 34, 52, 68, CELL_GLOW, rgba(255, 220, 130, 130));
                else          quad(x - 7, y - 20, 14, 28, CELL_SOLID, rgba(255, 230, 160, 255));
                break;
            case E_ANCHOR:
                if (additive) quad(x - 12, y - 12, 24, 24, CELL_GLOW, rgba(255, 170, 90, 140));
                else          quad(x - 3, y - 3, 6, 6, CELL_SOLID, rgba(255, 200, 120, 255));
                break;
            default: break;
        }
    }
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

    snprintf(buf, sizeof buf, "TIME %d", (int)w->time);
    font_text(ARC_W - font_width(1, buf) - 8, 8, 1, rgba(190, 210, 235, 200), buf);

    if (w->flow > 1.0f) {
        snprintf(buf, sizeof buf, "FLOW %d", (int)w->flow);
        font_text(ARC_W - font_width(1, buf) - 8, 16, 1, rgba(255, 220, 130, 230), buf);
    }

    if (w->finished) {
        const char *t = "DELIVERED";
        font_text((ARC_W - font_width(3, t)) * 0.5f, ARC_H * 0.4f, 3,
                  rgba(255, 245, 220, 255), t);
        snprintf(buf, sizeof buf, "TIME %d DEATHS %d VOLTS %d OF %d ECHOES %d OF %d",
                 (int)w->time, w->deaths, w->volts, w->volts_total,
                 w->echoes, w->echoes_total);
        font_text((ARC_W - font_width(1, buf)) * 0.5f, ARC_H * 0.4f + 24, 1,
                  rgba(200, 220, 240, 230), buf);

        const char *again = "PRESS R TO RUN IT AGAIN";
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
    static const char *HINTS[] = { "ARROWS RUN   Z JUMP   X DASH   C TETHER" };

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

void world_render(arc_world *w, const arc_texture *atlas, const arc_texture *city,
                  const arc_shader *sprite)
{
    /* Sky and far skyline, screen-space so the art keeps its pixel density. */
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    gfx_batch_begin(sprite, city, ARC_W, ARC_H);
    draw_parallax_layer(w, RECT_SKYLINE_A, 0.04f, 0.0f, rgba(150, 160, 205, 255));
    gfx_batch_end();

    /* Near buildings, dimmed and cooled. GDD §6.2: the gameplay layer must be
       the brightest and least busy thing on screen, and these facades are the
       busiest art in the game. */
    gfx_batch_begin(sprite, city, ARC_W, ARC_H);
    draw_parallax_layer(w, RECT_NEAR_BUILDINGS, 0.18f,
                        (float)ARC_H - RECT_NEAR_BUILDINGS.h, rgba(120, 130, 175, 255));
    gfx_batch_end();

    /* Fog plane between the background and the gameplay plane - the cheapest
       depth in games, and what actually separates the two reads. */
    gfx_batch_begin(sprite, atlas, ARC_W, ARC_H);
    quad(0, 0, ARC_W, ARC_H, CELL_SOLID, rgba(20, 24, 56, 120));
    gfx_batch_end();

    /* The moon, drawn after the fog: it is the scene's light source, so it
       reads as glowing through the haze rather than sitting behind it. */
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    gfx_batch_begin(sprite, atlas, ARC_W, ARC_H);
    quad(250, -50, 220, 220, CELL_GLOW, rgba(120, 160, 235, 90));
    quad(305, 5, 110, 110, CELL_GLOW, rgba(190, 215, 255, 150));
    quad(337, 37, 46, 46, CELL_GLOW, rgba(255, 255, 255, 230));
    gfx_batch_end();

    /* World: real tiles + actors, in the zoomed projection. The reflection
       quads live inside draw_player, so actors draw after tiles. */
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    gfx_batch_begin(sprite, city, VIEW_W, VIEW_H);
    draw_tiles(w);
    draw_oneways(w);
    draw_player(w);
    draw_enemies(w, 0);
    gfx_batch_end();

    /* Procedural gameplay markers: pickups, checkpoints. */
    gfx_batch_begin(sprite, atlas, VIEW_W, VIEW_H);
    draw_ents(w, 0);
    gfx_batch_end();

    /* Additive glow layer, world space. */
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    gfx_batch_begin(sprite, atlas, VIEW_W, VIEW_H);
    draw_ents(w, 1);
    draw_enemies(w, 1);
    if (w->p.state != PS_DEAD) {
        arc_player *p = &w->p;
        float g = 24.0f + p->charge * 0.25f;
        quad(p->x + p->w * 0.5f - g - w->cam_x, p->y + p->h * 0.5f - g - w->cam_y,
             g * 2, g * 2, CELL_GLOW, rgba(90, 190, 255, 60));
    }
    gfx_batch_end();

    /* HUD and text, screen space. */
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    gfx_batch_begin(sprite, atlas, ARC_W, ARC_H);
    draw_plates(w);
    gfx_batch_end();

    gfx_batch_begin(sprite, font_texture(), ARC_W, ARC_H);
    draw_hints(w);
    draw_hud(w);
    gfx_batch_end();
}
