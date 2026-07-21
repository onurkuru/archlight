/* ARCLIGHT - entry point, main loop, and the post stack.
 *
 *   [0] scene      480x272   world, batched
 *   [1] bright     240x136   saturation-biased threshold
 *   [2] blur H     240x136
 *   [3] blur V     240x136
 *   [4] composite  960x544   scene + bloom, grade, vignette, scanlines, CA
 *
 * Desktop keys: arrows/WASD move, Z jump, X dash, C tether, V melee, DOWN+Z stomp,
 *               SPACE toggles the post stack, R restarts, ESC quits.
 * Env: ARCLIGHT_SHOT=path.bmp (dump one frame and exit), ARCLIGHT_NOVSYNC=1
 */

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfx.h"
#include "game.h"
#include "levels.h"
#include "font.h"
#include "audio.h"

#ifdef __vita__
  #include <psp2/kernel/processmgr.h>
#endif

#define SCREEN_W 960
#define SCREEN_H 544
#define BLOOM_W  (ARC_W / 2)
#define BLOOM_H  (ARC_H / 2)

#define STEP (1.0f / 60.0f)

void game_build_atlas(arc_texture *tex);   /* game.c */

static arc_shader sh_sprite, sh_bright, sh_blur, sh_composite;
static arc_target rt_scene, rt_bloom_a, rt_bloom_b;
static arc_texture atlas, city;
static arc_world world;

/* The opening runs before the first level takes input. -1 means "done": a
   screenshot or a dev warp skips it entirely, because a capture run that
   waits six seconds for story is a capture run nobody uses. */
static int   intro_card = -1;
static float intro_t = 0.0f;

/* ---------------------------------------------------------------- passes */

static void pass_scene(void)
{
    gfx_target_bind(&rt_scene);
    glViewport(0, 0, ARC_W, ARC_H);
    gfx_clear(0.016f, 0.019f, 0.035f, 1.0f);
    world.hide_hud = (intro_card >= 0);
    world_render(&world, &atlas, &city, &sh_sprite);
    if (intro_card >= 0)
        world_render_intro(&world, &atlas, &sh_sprite, intro_card, intro_t);
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

static void pass_composite(int post_on, float t, float rain)
{
    gfx_target_bind(NULL);
    glViewport(0, 0, SCREEN_W, SCREEN_H);

    gfx_shader_use(&sh_composite);
    glUniform1i(sh_composite.u_tex, 0);
    glUniform1i(sh_composite.u_tex2, 1);
    glUniform4f(sh_composite.u_texel, 1.0f / ARC_W, 1.0f / ARC_H, 0, 0);
    glUniform4f(sh_composite.u_param,
                post_on ? 1.15f  : 0.0f,
                post_on ? 0.55f  : 0.0f,
                post_on ? 0.16f  : 0.0f,
                post_on ? 0.010f : 0.0f);
    if (sh_composite.u_dir >= 0)
        glUniform4f(sh_composite.u_dir, t, post_on ? rain : 0.0f, 0, 0);

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

/* ------------------------------------------------------------------ input */

static arc_input gather_input(void)
{
    static arc_input prev;
    arc_input in;
    memset(&in, 0, sizeof in);

#ifdef __vita__
    SDL_JoystickUpdate();
    SDL_Joystick *j = SDL_JoystickOpen(0);
    if (j) {
        /* Vita SDL2 button order: 0 TRIANGLE 1 CIRCLE 2 CROSS 3 SQUARE
           4 LTRIGGER 5 RTRIGGER 6 DOWN 7 LEFT 8 UP 9 RIGHT 10 SELECT 11 START */
        int left  = SDL_JoystickGetButton(j, 7);
        int right = SDL_JoystickGetButton(j, 9);
        Sint16 ax = SDL_JoystickGetAxis(j, 0);
        if (ax < -8000) left = 1;
        if (ax >  8000) right = 1;
        in.mx     = (int8_t)(right - left);
        in.down   = SDL_JoystickGetButton(j, 6) || SDL_JoystickGetAxis(j, 1) > 8000;
        in.jump   = SDL_JoystickGetButton(j, 2);
        in.dash   = SDL_JoystickGetButton(j, 1);
        in.tether = SDL_JoystickGetButton(j, 5);
        in.attack = SDL_JoystickGetButton(j, 3);   /* SQUARE, per GDD §3.1 */
        in.pulse  = SDL_JoystickGetButton(j, 0);   /* TRIANGLE, per GDD §3.1 */
    }
#else
    const Uint8 *k = SDL_GetKeyboardState(NULL);
    in.mx     = (int8_t)((k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_D]) -
                         (k[SDL_SCANCODE_LEFT]  || k[SDL_SCANCODE_A]));
    in.down   = k[SDL_SCANCODE_DOWN] || k[SDL_SCANCODE_S];
    in.jump   = k[SDL_SCANCODE_Z] || k[SDL_SCANCODE_SPACE];
    in.dash   = k[SDL_SCANCODE_X];
    in.tether = k[SDL_SCANCODE_C];
    in.attack = k[SDL_SCANCODE_V];
    in.pulse  = k[SDL_SCANCODE_F];
#endif

    in.jump_edge   = in.jump   && !prev.jump;
    in.dash_edge   = in.dash   && !prev.dash;
    in.tether_edge = in.tether && !prev.tether;
    in.attack_edge = in.attack && !prev.attack;
    in.pulse_edge  = in.pulse  && !prev.pulse;

    /* Dev affordance: fire a verb on a fixed period so a screenshot run can
       catch it without a human at the keyboard. Same spirit as ARCLIGHT_SHOT -
       it exists to make the feel reviewable. */
    {
        static float ta = 0, tp = 0, tj = 0;
        const char *ea = getenv("ARCLIGHT_ATTACK_EVERY");
        const char *ep = getenv("ARCLIGHT_PULSE_EVERY");
        const char *ej = getenv("ARCLIGHT_JUMP_EVERY");
        if (ej) {
            float period = (float)atof(ej);
            if (period > 0.01f) {
                tj += 1.0f / 60.0f;
                if (tj >= period) { tj = 0; in.jump = 1; in.jump_edge = 1; }
            }
        }
        if (ea) {
            float period = (float)atof(ea);
            if (period > 0.01f) {
                ta += 1.0f / 60.0f;
                if (ta >= period) { ta = 0; in.attack = 1; in.attack_edge = 1; }
            }
        }
        if (ep) {
            float period = (float)atof(ep);
            if (period > 0.01f) {
                tp += 1.0f / 60.0f;
                if (tp >= period) { tp = 0; in.pulse = 1; in.pulse_edge = 1; }
            }
        }
    }
    prev = in;
    return in;
}

/* ------------------------------------------------------------------- main */

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    const char *env_shot = getenv("ARCLIGHT_SHOT");
    const char *env_shot_frame = getenv("ARCLIGHT_SHOT_FRAME");
    int shot_frame = env_shot_frame ? atoi(env_shot_frame) : 4;
    const char *env_level = getenv("ARCLIGHT_LEVEL");
    const char *env_warp = getenv("ARCLIGHT_WARP");   /* start x, in tiles */

#ifdef __vita__
    SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_AUDIO);
    vglInitExtended(0, SCREEN_W, SCREEN_H, 0x1800000, SCE_GXM_MULTISAMPLE_NONE);
    vglWaitVblankStart(GL_TRUE);
    SDL_JoystickOpen(0);
#else
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { SDL_Log("SDL_Init: %s", SDL_GetError()); return 1; }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_Window *win = SDL_CreateWindow("ARCLIGHT",
                                       SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       SCREEN_W, SCREEN_H, SDL_WINDOW_OPENGL);
    if (!win) { SDL_Log("CreateWindow: %s", SDL_GetError()); return 1; }
    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    if (!ctx) { SDL_Log("GL context: %s", SDL_GetError()); return 1; }
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

    game_build_atlas(&atlas);
    if (!gfx_texture_load(&city, "assets/atlas_city.png", 0)) {
        SDL_Log("city atlas load failed - aborting");
        return 1;
    }
    font_init();
    audio_init();

    int level = env_level ? atoi(env_level) : 0;
    world_load(&world, level);
    if ((!env_shot && !env_level && !getenv("ARCLIGHT_WARP")) || getenv("ARCLIGHT_INTRO"))
        intro_card = getenv("ARCLIGHT_INTRO") ? atoi(getenv("ARCLIGHT_INTRO")) : 0;
    if (env_warp) {
        world.check_x = (float)atoi(env_warp) * TILE;
        world.check_y = world.spawn_y - 64.0f;
        world_reset_to_checkpoint(&world);
        world.cam_x = world.p.x - ARC_W * 0.5f;
        world.cam_y = world.p.y - ARC_H * 0.5f;
    }
    SDL_Log("ARCLIGHT %s \"%s\" %dx%d tiles, %d volts, %d echoes",
            world.id, world.title, world.w, world.h,
            world.volts_total, world.echoes_total);

    int running = 1, post_on = 1, frames = 0;
    float accum = 0.0f;
    arc_input latched;
    memset(&latched, 0, sizeof latched);
    double acc_ms = 0.0;
    Uint64 freq = SDL_GetPerformanceFrequency();
    Uint64 prev = SDL_GetPerformanceCounter();
    Uint64 report = prev;

    while (running) {
        Uint64 now = SDL_GetPerformanceCounter();
        float dt = (float)((now - prev) / (double)freq);
        prev = now;
        if (dt > 0.25f) dt = 0.25f;
        /* Capture mode is deterministic: one sim step per rendered frame,
           independent of wall clock, so ARCLIGHT_SHOT_FRAME=N always means
           "N/60 simulated seconds" - reproducible regardless of how fast the
           uncapped loop happens to spin. */
        if (env_shot) dt = STEP;

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_ESCAPE) {
                    /* Escape skips the whole opening, then quits. */
                    if (intro_card >= 0) { intro_card = -1; continue; }
                    running = 0;
                }
                if (intro_card >= 0) {
                    if (++intro_card >= world_intro_cards()) intro_card = -1;
                    intro_t = 0.0f;
                    continue;
                }
                if (e.key.keysym.sym == SDLK_SPACE)  post_on = !post_on;
                if (e.key.keysym.sym == SDLK_r) world_load(&world, level);
                /* The results screen advances the campaign. Wrapping at the
                   end keeps a dev build playable end to end without a menu. */
                if (e.key.keysym.sym == SDLK_RETURN && world.finished)
                    world_load(&world, level = (level + 1) % ARC_LEVEL_COUNT);
            }
            if (e.type == SDL_JOYBUTTONDOWN && e.jbutton.button == 11) running = 0;
        }

        /* Fixed 60 Hz simulation, clamped so a hitch cannot spiral.
         *
         * Input is sampled once per *rendered* frame but consumed by the sim at
         * exactly 60 Hz, and those two rates are not the same number. On a
         * 120 Hz display roughly half of all rendered frames run no sim step at
         * all, so an edge computed on one of those frames would be computed,
         * never consumed, and gone by the next sample - the press simply never
         * happened. The reverse case is just as wrong: when a hitch makes one
         * frame run several steps, an un-cleared edge would fire once per step.
         *
         * So edges are latched here and cleared by the step that consumes them.
         * Held state (mx, and the buttons themselves) always tracks the newest
         * sample, because that is a level, not an event. */
        arc_input sample = gather_input();
        latched.mx     = sample.mx;
        latched.down   = sample.down;
        latched.jump   = sample.jump;
        latched.dash   = sample.dash;
        latched.tether = sample.tether;
        latched.attack = sample.attack;
        latched.pulse  = sample.pulse;
        latched.jump_edge   |= sample.jump_edge;
        latched.dash_edge   |= sample.dash_edge;
        latched.tether_edge |= sample.tether_edge;
        latched.attack_edge |= sample.attack_edge;
        latched.pulse_edge  |= sample.pulse_edge;

        if (intro_card >= 0) {
            intro_t += dt;
            if (intro_t > 3.4f) {            /* auto-advance, unhurried */
                intro_t = 0.0f;
                if (++intro_card >= world_intro_cards()) intro_card = -1;
            }
            accum = 0.0f;
        }

        /* Music follows the place, and the Warden takes it over. Set every
           frame - audio_music() ignores a repeat request, so this is free.
           (This block was lost in an earlier edit, which is why the game went
           silent; kept here, next to the loop it belongs to.) */
        if (!env_shot) {
            char track[16];
            if (intro_card >= 0)                snprintf(track, sizeof track, "intro");
            else if (world.finished)            snprintf(track, sizeof track, "drop");
            else if (world_boss_active(&world)) snprintf(track, sizeof track, "boss");
            else snprintf(track, sizeof track, "d%d", world.district + 1);
            audio_music(track, 0.75f);
        }

        accum += dt;
        int steps = 0;
        while (intro_card < 0 && accum >= STEP && steps < 4) {
            world_update(&world, &latched, STEP);
            latched.jump_edge = latched.dash_edge = latched.tether_edge =
                latched.attack_edge = latched.pulse_edge = 0;
            accum -= STEP;
            steps++;
        }
        if (steps == 4) accum = 0.0f;

        gfx_stats_reset();
        pass_scene();
        if (post_on) {
            pass_bright();
            pass_blur(&rt_bloom_a, &rt_bloom_b, 1.0f, 0.0f);
            pass_blur(&rt_bloom_b, &rt_bloom_a, 0.0f, 1.0f);
            glEnable(GL_BLEND);
        }
        pass_composite(post_on, world.time, world_rain(&world));

        if (env_shot && frames == shot_frame) { dump_frame(env_shot); running = 0; }

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

    font_shutdown();
    gfx_target_destroy(&rt_scene);
    gfx_target_destroy(&rt_bloom_a);
    gfx_target_destroy(&rt_bloom_b);
    gfx_texture_destroy(&atlas);
    gfx_texture_destroy(&city);
    gfx_shutdown();

#ifdef __vita__
    SDL_Quit();
    sceKernelExitProcess(0);
#else
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
#endif
    return 0;
}
