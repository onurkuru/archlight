#include "audio.h"

#include <SDL.h>
#include <stdio.h>
#include <string.h>

#define RATE       22050
#define VOICES     8
#define MIX_CHUNK  512

/* One-shots are held whole in memory; they are tens of KB each. Music is held
 * whole too - a two-minute track is ~5 MB, which is cheaper than the streaming
 * machinery would be to write and to get right. */
typedef struct {
    int16_t *pcm;
    int      len;          /* samples */
} arc_clip;

typedef struct {
    const arc_clip *clip;
    float pos;             /* fractional read cursor, for pitch */
    float step, gain;
    int   active;
} arc_voice;

static SDL_AudioDeviceID dev;
static arc_clip   sfx[SFX_COUNT];
static arc_voice  voices[VOICES];

static arc_clip   music;
static char       music_name[32];
static float      music_gain = 0.0f;
static double     music_pos = 0.0;

/* The callback runs on SDL's audio thread; the game thread only ever writes
 * voice slots and the music pointer. Guarding both with one lock is enough at
 * this scale, and SDL gives us the lock for free. */

static int load_clip(arc_clip *c, const char *path)
{
    SDL_AudioSpec spec;
    Uint8 *buf = NULL;
    Uint32 len = 0;
    if (!SDL_LoadWAV(path, &spec, &buf, &len)) {
        SDL_Log("audio: %s: %s", path, SDL_GetError());
        return 0;
    }
    if (spec.freq != RATE || spec.channels != 1 || spec.format != AUDIO_S16SYS) {
        SDL_Log("audio: %s is %d Hz %d ch fmt %04x, expected %d Hz mono s16 "
                "- re-run tools/audiopack.py", path, spec.freq, spec.channels,
                spec.format, RATE);
        SDL_FreeWAV(buf);
        return 0;
    }
    c->pcm = (int16_t *)buf;
    c->len = (int)(len / sizeof(int16_t));
    return 1;
}

static void mix(void *ud, Uint8 *stream, int bytes)
{
    (void)ud;
    int16_t *out = (int16_t *)stream;
    int n = bytes / (int)sizeof(int16_t);

    for (int i = 0; i < n; i++) {
        float acc = 0.0f;

        if (music.pcm && music_gain > 0.0f) {
            acc += music.pcm[(int)music_pos] * music_gain;
            music_pos += 1.0;
            if (music_pos >= music.len) music_pos = 0.0;   /* loop */
        }

        for (int v = 0; v < VOICES; v++) {
            arc_voice *vo = &voices[v];
            if (!vo->active) continue;
            int idx = (int)vo->pos;
            if (idx >= vo->clip->len) { vo->active = 0; continue; }
            acc += vo->clip->pcm[idx] * vo->gain;
            vo->pos += vo->step;
        }

        /* Soft clip rather than wrap: a wrapped sample is a click, and a click
           is louder than anything it was mixed from. */
        if (acc >  32000.0f) acc =  32000.0f;
        if (acc < -32000.0f) acc = -32000.0f;
        out[i] = (int16_t)acc;
    }
}

int audio_init(void)
{
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        SDL_Log("audio: no audio subsystem: %s", SDL_GetError());
        return 0;
    }

    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = MIX_CHUNK;
    want.callback = mix;

    dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!dev) {
        SDL_Log("audio: no device: %s", SDL_GetError());
        return 0;
    }
    (void)have;

    static const char *paths[SFX_COUNT] = {
        "assets/audio/sfx_kill_bot.wav",
        "assets/audio/sfx_kill_man.wav",
        "assets/audio/sfx_boss_hit.wav",
        "assets/audio/sfx_boss_die.wav",
        "assets/audio/sfx_hurt.wav",
        "assets/audio/sfx_shot.wav",
        "assets/audio/sfx_pulse.wav",
        "assets/audio/sfx_volt.wav",
        "assets/audio/sfx_echo.wav",
        "assets/audio/sfx_node.wav",
        "assets/audio/sfx_gate.wav",
        "assets/audio/sfx_land.wav",
        "assets/audio/sfx_dash.wav",
    };
    for (int i = 0; i < SFX_COUNT; i++) load_clip(&sfx[i], paths[i]);

    SDL_PauseAudioDevice(dev, 0);
    return 1;
}

void audio_shutdown(void)
{
    if (!dev) return;
    SDL_CloseAudioDevice(dev);
    dev = 0;
}

void audio_play(arc_sfx s, float gain, float pitch)
{
    if (!dev || s < 0 || s >= SFX_COUNT || !sfx[s].pcm) return;

    SDL_LockAudioDevice(dev);
    /* Steal the voice that is furthest through its clip: if every voice is
       busy, the one closest to finishing is the one nobody will miss. */
    int best = 0;
    float best_left = 1e9f;
    for (int v = 0; v < VOICES; v++) {
        if (!voices[v].active) { best = v; break; }
        float left = voices[v].clip->len - voices[v].pos;
        if (left < best_left) { best_left = left; best = v; }
    }
    voices[best].clip = &sfx[s];
    voices[best].pos = 0.0f;
    voices[best].step = pitch < 0.25f ? 0.25f : pitch;
    voices[best].gain = gain;
    voices[best].active = 1;
    SDL_UnlockAudioDevice(dev);
}

void audio_music(const char *name, float gain)
{
    if (!dev) return;

    if (!name) {
        SDL_LockAudioDevice(dev);
        music_gain = 0.0f;
        SDL_UnlockAudioDevice(dev);
        music_name[0] = '\0';
        return;
    }
    if (strcmp(music_name, name) == 0) return;    /* already playing */

    char path[128];
    snprintf(path, sizeof path, "assets/audio/mus_%s.wav", name);

    arc_clip next;
    memset(&next, 0, sizeof next);
    if (!load_clip(&next, path)) return;

    SDL_LockAudioDevice(dev);
    arc_clip old = music;
    music = next;
    music_pos = 0.0;
    music_gain = gain;
    SDL_UnlockAudioDevice(dev);

    if (old.pcm) SDL_FreeWAV((Uint8 *)old.pcm);
    snprintf(music_name, sizeof music_name, "%s", name);
}
