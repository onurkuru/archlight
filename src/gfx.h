/* ARCLIGHT - renderer: shaders, render targets, sprite batcher. */
#ifndef ARC_GFX_H
#define ARC_GFX_H

#include <stdint.h>
#include "gl_compat.h"

/* Internal authoring resolution. Presented at 960x544 (integer x2) on Vita.
 * This is a performance decision, not an aesthetic one - see docs/TECH_PLAN.md. */
#define ARC_W 480
#define ARC_H 272

typedef struct {
    GLuint fbo, tex;
    int w, h;
} arc_target;

typedef struct {
    GLuint prog;
    /* cached uniform locations; -1 when absent */
    GLint u_proj, u_tex, u_tex2, u_texel, u_dir, u_param;
} arc_shader;

typedef struct {
    GLuint id;
    int w, h;
} arc_texture;

/* --- lifecycle --- */
int  gfx_init(void);
void gfx_shutdown(void);

/* --- shaders --- */
/* Loads shaders/<name>.vert + shaders/<name>.frag. On Vita a precompiled
 * shaders/<name>.gxp is preferred when present (see gfx_shader_dump). */
int  gfx_shader_load(arc_shader *s, const char *name);
void gfx_shader_use(const arc_shader *s);

/* --- render targets --- */
int  gfx_target_create(arc_target *t, int w, int h);
void gfx_target_destroy(arc_target *t);
void gfx_target_bind(const arc_target *t);   /* NULL = default framebuffer */

/* --- textures --- */
int  gfx_texture_from_rgba(arc_texture *t, const uint8_t *px, int w, int h, int smooth);
int  gfx_texture_load(arc_texture *t, const char *path, int smooth);
void gfx_texture_destroy(arc_texture *t);

/* --- sprite batching ---
 * begin() sets the target's projection; quads accumulate until end()/flush. */
void gfx_batch_begin(const arc_shader *s, const arc_texture *tex, int target_w, int target_h);
void gfx_batch_quad(float x, float y, float w, float h,
                    float u0, float v0, float u1, float v1, uint32_t rgba);
void gfx_batch_end(void);

/* Draws a screen-filling triangle pair with the currently bound shader. */
void gfx_fullscreen(void);

/* --- misc --- */
void gfx_clear(float r, float g, float b, float a);
const char *gfx_asset_path(const char *rel);   /* app0:/ on Vita, ./ on desktop */

/* Per-frame counters, reset by gfx_stats_reset(). */
typedef struct { int draw_calls, quads; } arc_stats;
extern arc_stats gfx_stats;
void gfx_stats_reset(void);

#endif
