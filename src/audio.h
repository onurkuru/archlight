/* ARCLIGHT - audio. Raw SDL2, no decoder, no mixer library.
 *
 * Everything is 22050 Hz mono s16 PCM baked by tools/audiopack.py, so this
 * links nothing SDL2 does not already provide and the same code compiles for
 * the Vita. A mixer library would have been one more thing to cross-compile
 * for a game that needs eight voices and one looping track. */
#ifndef ARC_AUDIO_H
#define ARC_AUDIO_H

/* One event, one sound. The set is derived from three real samples by
 * tools/audiopack.py rather than reusing one explosion for everything, which
 * is what made a katana through a person sound like a grenade. */
typedef enum {
    SFX_KILL_BOT,  /* a machine comes apart - bright, metallic */
    SFX_KILL_MAN,  /* a body hits the street - low, dull */
    SFX_BOSS_HIT,  /* armour taking a hit it survives */
    SFX_BOSS_DIE,
    SFX_HURT,      /* a plate comes off Nine */
    SFX_SHOT,      /* a round leaves a muzzle */
    SFX_PULSE,
    SFX_VOLT,
    SFX_ECHO,
    SFX_NODE,
    SFX_GATE,
    SFX_LAND,
    SFX_DASH,
    SFX_COUNT
} arc_sfx;

int  audio_init(void);
void audio_shutdown(void);

/* `gain` 0..1, `pitch` around 1.0. Varying pitch per call is most of what
 * stops a repeated one-shot turning into a machine-gun rattle. */
void audio_play(arc_sfx s, float gain, float pitch);

/* Loads and loops a track, or stops it with name = NULL. Re-requesting the
 * track already playing is a no-op, so callers can set it every frame. */
void audio_music(const char *name, float gain);

#endif
