/* ARCLIGHT - Milestone 0 spike.
 *
 * Question this program exists to answer: can a PS Vita hold 60 fps while
 * running the full post stack the art direction depends on?
 *
 *   [0] scene      480x272   ~1500 sprites, alpha + additive
 *   [1] bright     240x136   saturation-biased threshold
 *   [2] blur H     240x136
 *   [3] blur V     240x136
 *   [4] composite  960x544   scene + bloom, grade, vignette, scanlines, CA
 *
 * Controls: START/ESC quit, X/SPACE toggles the post stack (A/B the cost),
 *           L/R or [ ] change the sprite count.
 * Env:      ARCLIGHT_SPRITES=n   ARCLIGHT_SHOT=path.bmp (dump one frame, exit)
 */

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "gfx.h"

#ifdef __vita__
  #include <psp2/kernel/processmgr.h>
#endif

#define SCREEN_W 960
#define SCREEN_H 544
#define BLOOM_W  (ARC_W / 2)
#define BLOOM_H  (ARC_H / 2)

/* Atlas cells, 32x32 each in a 64x64 texture. */
#define ATLAS   64.0f
#define CELL    32.0f
#define UV(cx, cy) (cx) * CELL / ATLAS, (cy) * CELL / ATLAS, \
                   ((cx) + 1) * CELL / ATLAS, ((cy) + 1) * CELL / ATLAS
enum { CELL_SOLID = 0, CELL_GLOW = 1, CELL_STREAK = 2, CELL_WINDOWS = 3 };

static void cell_uv(int cell, float *u0, float *v0, float *u1, float *v1)
{
    int cx = cell & 1, cy = cell >> 1;
    *u0 = cx * CELL / ATLAS;       *v0 = cy * CELL / ATLAS;
    *u1 = (cx + 1) * CELL / ATLAS; *v1 = (cy + 1) * CELL / ATLAS;
}

static uint32_t rgba(int r, int g, int b, int a)
{
    return (uint32_t)(r & 255) | ((uint32_t)(g & 255) << 8) |
           ((uint32_t)(b & 255) << 16) | ((uint32_t)(a & 255) << 24);
}

/* ------------------------------------------------------------ procedural art
 * The spike deliberately ships no art files: it must be runnable before the
 * asset licence audit is finished. */

static void build_atlas(arc_texture *tex)
{
    static uint8_t px[64 * 64 * 4];
    memset(px, 0, sizeof px);

    for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 64; x++) {
            uint8_t *p = &px[(y * 64 + x) * 4];
            int lx = x & 31, ly = y & 31;
            int cx = x >> 5, cy = y >> 5;
            int cell = (cy << 1) | cx;

            if (cell == CELL_SOLID) {
                p[0] = p[1] = p[2] = p[3] = 255;
            } else if (cell == CELL_GLOW) {
                float dx = (lx - 15.5f) / 15.5f, dy = (ly - 15.5f) / 15.5f;
                float d = sqrtf(dx * dx + dy * dy);
                float a = 1.0f - d;
                a = a < 0 ? 0 : a * a * a;                 /* tight falloff */
                p[0] = p[1] = p[2] = 255;
                p[3] = (uint8_t)(a * 255.0f);
            } else if (cell == CELL_STREAK) {
                float a = 1.0f - fabsf((lx - 15.5f) / 4.0f);
                a = a < 0 ? 0 : a;
                a *= 0.35f + 0.65f * (ly / 31.0f);         /* fades upward */
                p[0] = p[1] = p[2] = 255;
                p[3] = (uint8_t)(a * 255.0f);
            } else {                                        /* windows */
                int on = ((lx / 4) * 7 + (ly / 6) * 13) % 5 < 2;
                int frame = (lx % 4 == 0) || (ly % 6 == 0);
                p[3] = 255;
                if (frame) { p[0] = p[1] = p[2] = 8; }
                else if (on) { p[0] = 255; p[1] = 214; p[2] = 140; }
                else { p[0] = 14; p[1] = 16; p[2] = 26; }
            }
        }
    }
    gfx_texture_from_rgba(tex, px, 64, 64, 0);
}

/* Deterministic layout PRNG - the scene must be identical across runs so the
 * screenshot diff is meaningful. */
static uint32_t rng_state = 0x9E3779B9u;
static uint32_t rnd(void) {
    rng_state ^= rng_state << 13; rng_state ^= rng_state >> 17; rng_state ^= rng_state << 5;
    return rng_state;
}
static float rndf(float lo, float hi) { return lo + (hi - lo) * ((rnd() >> 8) / 16777216.0f); }

typedef struct { float x, y, w, h, speed; uint32_t col; int cell; } prop;

#define MAX_PROPS 4096
static prop props[MAX_PROPS];
static int  prop_count;

static void build_scene(int sprite_budget)
{
    rng_state = 0x9E3779B9u;
    prop_count = 0;

    /* Three parallax bands of buildings, far to near. Nearer bands are lighter
       and warmer because the city's own light falls off - the cheapest depth
       cue there is, and it costs nothing but a vertex colour. */
    const float band_speed[3] = { 0.15f, 0.4f, 0.75f };
    const int   band_y[3]     = { 130, 165, 205 };
    for (int b = 0; b < 3 && prop_count < sprite_budget; b++) {
        for (float x = -60; x < ARC_W + 120 && prop_count < sprite_budget; ) {
            float w = rndf(24, 60), h = rndf(60, 170);
            int s = b * 26;
            props[prop_count++] = (prop){ x, (float)band_y[b] - h + rndf(0, 20), w, h,
                                          band_speed[b],
                                          rgba(38 + s, 44 + s, 78 + s, 255),
                                          CELL_WINDOWS };
            x += w + rndf(4, 18);
        }
    }

    /* Street plane: everything below the horizon is where reflections will go
       once that pass exists. */
    if (prop_count < sprite_budget)
        props[prop_count++] = (prop){ -80, 205, ARC_W + 240, ARC_H - 205, 0.0f,
                                      rgba(16, 14, 26, 255), CELL_SOLID };

    /* Neon: bright saturated quads plus a glow sprite behind each. These are
       what the bright pass must catch and the concrete must not trigger.
       A real street has tens of signs, not hundreds - the sprite budget goes
       to rain instead, which is the honest load. */
    for (int i = 0; i < 55 && prop_count + 1 < sprite_budget; i++) {
        float x = rndf(-40, ARC_W + 40), y = rndf(30, 190);
        float w = rndf(8, 38), h = rndf(3, 11);
        uint32_t col = (rnd() & 1) ? rgba(255, 40, 190, 255) : rgba(40, 240, 255, 255);
        props[prop_count++] = (prop){ x, y, w, h, 0.75f, col, CELL_SOLID };
        props[prop_count++] = (prop){ x - w, y - h * 2.0f, w * 3, h * 5, 0.75f,
                                      (col & 0x00FFFFFFu) | 0x60000000u, CELL_GLOW };
    }

    /* Rain fills whatever budget is left - in the real game this is a shader,
       here it is sprites precisely because we want to overload the batcher. */
    while (prop_count < sprite_budget) {
        props[prop_count++] = (prop){ rndf(-20, ARC_W + 20), rndf(-40, ARC_H),
                                      rndf(1, 2), rndf(6, 16), rndf(1.2f, 2.4f),
                                      rgba(150, 190, 255, 70), CELL_STREAK };
    }
}

/* ---------------------------------------------------------------- passes */

static arc_shader sh_sprite, sh_bright, sh_blur, sh_composite;
static arc_target rt_scene, rt_bloom_a, rt_bloom_b;
static arc_texture atlas;

static void pass_scene(float t, float cam)
{
    gfx_target_bind(&rt_scene);
    glViewport(0, 0, ARC_W, ARC_H);
    gfx_clear(0.016f, 0.019f, 0.035f, 1.0f);

    /* Two ordered passes rather than one pass that flips blend state per sprite.
       Blend changes break the batch, so a naive single loop costs ~110 draw
       calls here instead of 2 - the single most expensive mistake available in
       this renderer. Layer order, not sprite order, decides draw calls. */
    for (int additive = 0; additive <= 1; additive++) {
        glBlendFunc(GL_SRC_ALPHA, additive ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
        gfx_batch_begin(&sh_sprite, &atlas, ARC_W, ARC_H);

        for (int i = 0; i < prop_count; i++) {
            prop *p = &props[i];
            int is_add = (p->cell == CELL_GLOW || p->cell == CELL_STREAK);
            if (is_add != additive) continue;

            float x = p->x - cam * p->speed;
            float y = p->y;
            if (p->cell == CELL_STREAK) {
                y += t * 260.0f * p->speed;
                y = fmodf(y, (float)ARC_H + 40.0f) - 20.0f;
            }
            /* wrap horizontally so the scene scrolls forever */
            x = fmodf(x, (float)ARC_W + 160.0f);
            if (x < -80.0f) x += (float)ARC_W + 160.0f;

            float u0, v0, u1, v1;
            cell_uv(p->cell, &u0, &v0, &u1, &v1);
            gfx_batch_quad(x, y, p->w, p->h, u0, v0, u1, v1, p->col);
        }
        gfx_batch_end();
    }

    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    gfx_batch_begin(&sh_sprite, &atlas, ARC_W, ARC_H);

    /* The "player": a 16x32 chassis with a lamp, so there is something whose
       motion tells us the frame is actually alive. */
    float px = ARC_W * 0.5f, py = 150.0f + sinf(t * 3.0f) * 26.0f;
    float u0, v0, u1, v1;
    cell_uv(CELL_GLOW, &u0, &v0, &u1, &v1);
    gfx_batch_quad(px - 40, py - 40, 96, 96, u0, v0, u1, v1, rgba(120, 220, 255, 90));
    gfx_batch_end();

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    gfx_batch_begin(&sh_sprite, &atlas, ARC_W, ARC_H);
    cell_uv(CELL_SOLID, &u0, &v0, &u1, &v1);
    gfx_batch_quad(px - 8, py, 16, 32, u0, v0, u1, v1, rgba(232, 240, 255, 255));
    gfx_batch_end();
}

static void pass_bright(void)
{
    gfx_target_bind(&rt_bloom_a);
    glViewport(0, 0, BLOOM_W, BLOOM_H);
    glDisable(GL_BLEND);

    gfx_shader_use(&sh_bright);
    glUniform1i(sh_bright.u_tex, 0);
    glUniform4f(sh_bright.u_param, 0.72f, 0.35f, 0.30f, 0.0f);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, rt_scene.tex);
    gfx_fullscreen();
}

static void pass_blur(arc_target *src, arc_target *dst, float dx, float dy)
{
    gfx_target_bind(dst);
    glViewport(0, 0, dst->w, dst->h);

    gfx_shader_use(&sh_blur);
    glUniform1i(sh_blur.u_tex, 0);
    glUniform4f(sh_blur.u_texel, 1.0f / src->w, 1.0f / src->h, 0, 0);
    glUniform4f(sh_blur.u_dir, dx, dy, 0, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, src->tex);
    gfx_fullscreen();
}

static void pass_composite(int post_on)
{
    gfx_target_bind(NULL);
    glViewport(0, 0, SCREEN_W, SCREEN_H);

    gfx_shader_use(&sh_composite);
    glUniform1i(sh_composite.u_tex, 0);
    glUniform1i(sh_composite.u_tex2, 1);
    glUniform4f(sh_composite.u_texel, 1.0f / ARC_W, 1.0f / ARC_H, 0, 0);
    glUniform4f(sh_composite.u_param,
                post_on ? 1.15f : 0.0f,   /* bloom  */
                post_on ? 0.55f : 0.0f,   /* vignette */
                post_on ? 0.16f : 0.0f,   /* scanline */
                post_on ? 0.010f : 0.0f); /* aberration */

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, rt_scene.tex);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, rt_bloom_a.tex);
    glActiveTexture(GL_TEXTURE0);
    gfx_fullscreen();

    glEnable(GL_BLEND);
}

/* ------------------------------------------------------------------- shot */

static void dump_frame(const char *path)
{
    uint8_t *px = malloc(SCREEN_W * SCREEN_H * 4);
    if (!px) return;
    glReadPixels(0, 0, SCREEN_W, SCREEN_H, GL_RGBA, GL_UNSIGNED_BYTE, px);

    /* 24-bit BGR: the one BMP flavour every image tool actually reads.
       glReadPixels is bottom-up; SDL surfaces are top-down. */
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, SCREEN_W, SCREEN_H, 24,
                                                    SDL_PIXELFORMAT_BGR24);
    if (s) {
        for (int y = 0; y < SCREEN_H; y++) {
            uint8_t *dst = (uint8_t *)s->pixels + y * s->pitch;
            const uint8_t *src = px + (SCREEN_H - 1 - y) * SCREEN_W * 4;
            for (int x = 0; x < SCREEN_W; x++) {
                dst[x * 3 + 0] = src[x * 4 + 2];
                dst[x * 3 + 1] = src[x * 4 + 1];
                dst[x * 3 + 2] = src[x * 4 + 0];
            }
        }
        SDL_SaveBMP(s, path);
        SDL_FreeSurface(s);
        SDL_Log("wrote %s", path);
    }
    free(px);
}

/* ------------------------------------------------------------------- main */

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    const char *env_n   = getenv("ARCLIGHT_SPRITES");
    const char *env_shot = getenv("ARCLIGHT_SHOT");
    int sprite_budget = env_n ? atoi(env_n) : 1500;
    if (sprite_budget < 16) sprite_budget = 16;
    if (sprite_budget > MAX_PROPS) sprite_budget = MAX_PROPS;

#ifdef __vita__
    SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_AUDIO);
    /* vitaGL owns the display; SDL is here for input, audio and timing only. */
    vglInitExtended(0, SCREEN_W, SCREEN_H, 0x1800000, SCE_GXM_MULTISAMPLE_NONE);
    vglWaitVblankStart(GL_TRUE);
    SDL_Joystick *pad = SDL_JoystickOpen(0);
#else
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { SDL_Log("SDL_Init: %s", SDL_GetError()); return 1; }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_Window *win = SDL_CreateWindow("ARCLIGHT - M0 spike",
                                       SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       SCREEN_W, SCREEN_H, SDL_WINDOW_OPENGL);
    if (!win) { SDL_Log("CreateWindow: %s", SDL_GetError()); return 1; }
    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    if (!ctx) { SDL_Log("GL context: %s", SDL_GetError()); return 1; }
    /* Vsync off makes the desktop numbers comparable to the Vita's. */
    SDL_GL_SetSwapInterval((env_shot || getenv("ARCLIGHT_NOVSYNC")) ? 0 : 1);
#endif

    if (!gfx_init()) return 1;

    if (!gfx_shader_load(&sh_sprite,    "sprite")    ||
        !gfx_shader_load(&sh_bright,    "bright")    ||
        !gfx_shader_load(&sh_blur,      "blur")      ||
        !gfx_shader_load(&sh_composite, "composite")) {
        SDL_Log("shader load failed - aborting");
        return 1;
    }

    if (!gfx_target_create(&rt_scene,   ARC_W,   ARC_H)   ||
        !gfx_target_create(&rt_bloom_a, BLOOM_W, BLOOM_H) ||
        !gfx_target_create(&rt_bloom_b, BLOOM_W, BLOOM_H)) {
        SDL_Log("render target creation failed - aborting");
        return 1;
    }

    build_atlas(&atlas);
    build_scene(sprite_budget);
    SDL_Log("ARCLIGHT M0: %d sprites, scene %dx%d, bloom %dx%d, out %dx%d",
            prop_count, ARC_W, ARC_H, BLOOM_W, BLOOM_H, SCREEN_W, SCREEN_H);

    int running = 1, post_on = 1, frames = 0;
    float t = 0.0f, cam = 0.0f;
    double acc_ms = 0.0;
    Uint64 freq = SDL_GetPerformanceFrequency();
    Uint64 prev = SDL_GetPerformanceCounter();
    Uint64 report = prev;

    while (running) {
        Uint64 now = SDL_GetPerformanceCounter();
        float dt = (float)((now - prev) / (double)freq);
        prev = now;
        if (dt > 0.1f) dt = 0.1f;
        t += dt;
        cam += dt * 60.0f;

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_ESCAPE) running = 0;
                if (e.key.keysym.sym == SDLK_SPACE)  post_on = !post_on;
                if (e.key.keysym.sym == SDLK_LEFTBRACKET || e.key.keysym.sym == SDLK_RIGHTBRACKET) {
                    sprite_budget += (e.key.keysym.sym == SDLK_RIGHTBRACKET) ? 250 : -250;
                    if (sprite_budget < 16) sprite_budget = 16;
                    if (sprite_budget > MAX_PROPS) sprite_budget = MAX_PROPS;
                    build_scene(sprite_budget);
                    SDL_Log("sprites -> %d", prop_count);
                }
            }
            if (e.type == SDL_JOYBUTTONDOWN) {
                if (e.jbutton.button == 11) running = 0;      /* START */
                if (e.jbutton.button == 2)  post_on = !post_on; /* CROSS */
            }
        }

        gfx_stats_reset();
        pass_scene(t, cam);
        if (post_on) {
            pass_bright();
            pass_blur(&rt_bloom_a, &rt_bloom_b, 1.0f, 0.0f);
            pass_blur(&rt_bloom_b, &rt_bloom_a, 0.0f, 1.0f);
            glEnable(GL_BLEND);
        }
        pass_composite(post_on);

        if (env_shot && frames == 4) {          /* let the scene settle first */
            dump_frame(env_shot);
            running = 0;
        }

#ifdef __vita__
        vglSwapBuffers(GL_FALSE);
#else
        SDL_GL_SwapWindow(win);
#endif

        frames++;
        acc_ms += dt * 1000.0;
        if ((now - report) > freq) {
            SDL_Log("%5.1f fps | %5.2f ms | %3d draws | %4d quads | post %s",
                    frames / (acc_ms / 1000.0), acc_ms / frames,
                    gfx_stats.draw_calls, gfx_stats.quads, post_on ? "ON" : "off");
            frames = 0; acc_ms = 0.0; report = now;
        }
    }

    gfx_target_destroy(&rt_scene);
    gfx_target_destroy(&rt_bloom_a);
    gfx_target_destroy(&rt_bloom_b);
    gfx_texture_destroy(&atlas);
    gfx_shutdown();

#ifdef __vita__
    if (pad) SDL_JoystickClose(pad);
    SDL_Quit();
    sceKernelExitProcess(0);
#else
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
#endif
    return 0;
}
