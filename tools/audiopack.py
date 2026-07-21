#!/usr/bin/env python3
"""Bake the music and SFX the game plays into one runtime format.

Everything becomes 22050 Hz mono signed-16 WAV, which is what src/audio.c
mixes. That choice is the whole design: raw PCM needs no decoder, so the game
links nothing beyond SDL2 and the same code runs on the Vita, where an Ogg
decoder would have been another dependency to cross-compile.

The SFX set is *derived*, not sampled twelve times over. There are only three
real one-shots in the packs (a beam, an explosion, a hurt), and playing the
same explosion for a drone, a person and a boss is what made the audio feel
wrong. So each event gets its own filtered and re-pitched cut of a real
sample: a drone dies bright and metallic, a body dies low and dull, a pickup
is the beam pitched up to a blip. Same source material, different object.
"""

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MUSIC_SRC = Path.home() / "Desktop" / "music"
SFX_SRC = ROOT / "art-src" / "Warped City Phaser" / "assets" / "sounds"
OUT = ROOT / "assets" / "audio"

RATE = 22050

# Tracks assigned by measured character, not by filename. Loudness, spectral
# brightness and onset density were sampled from 30 s of each; the numbers are
# in the comments so the next reassignment can argue with the last one.
MUSIC = {
    #  name       file                 why
    "intro": "void.ogg",            # 181 Hz, 0.2 onsets/s - nearly still, ambient
    "d1":    "Tension plane.ogg",   # 292 Hz, 2.1 - dark but moving: Halcyon night
    "d2":    "ArcadeKind.ogg",      # 339 Hz, 1.6, loudest mids - the neon Strip
    "d3":    "Ancient Robot.ogg",   # 373 Hz, 2.5 - mechanical, busy: the Stacks
    "d4":    "RetroFuture.wav",     # 316 Hz, 2.8 - the driving one: the Shoreline
    "d5":    "glitchbass.ogg",      # 1456 Hz, 0.8 - brightest, broken: the Lab
    "boss":  "AgresiveBass.ogg",    # 421 Hz, 1.1, bass-forward - the Wardens
    "drop":  "InterRegret.ogg",     # 493 Hz, 0.6 - slow and warm: the results screen
}

# name -> (source, ffmpeg filter chain, seconds, gain)
#
# `asetrate` re-pitches by resampling, then `aresample` puts it back on the
# runtime rate - the cheap classic way to get a family of sounds out of one.
SFX = {
    # A machine coming apart: bright, short, metallic.
    "kill_bot":  ("explosion.ogg",
                  f"asetrate={RATE}*1.45,aresample={RATE},highpass=f=700", 0.35, 0.75),
    # A body hitting the street: low, dull, no ring.
    "kill_man":  ("explosion.ogg",
                  f"asetrate={RATE}*0.72,aresample={RATE},lowpass=f=900", 0.45, 0.85),
    # Armour taking a hit that does not kill: a clang, not a boom.
    "boss_hit":  ("explosion.ogg",
                  f"asetrate={RATE}*1.9,aresample={RATE},highpass=f=1200", 0.18, 0.6),
    # The rig going up.
    "boss_die":  ("explosion.ogg",
                  f"asetrate={RATE}*0.6,aresample={RATE}", 0.72, 1.0),
    # Nine losing a plate.
    "hurt":      ("hurt.wav", "lowpass=f=4000", 0.6, 0.85),
    # A round leaving a muzzle.
    "shot":      ("beam.ogg", f"asetrate={RATE}*1.1,aresample={RATE}", 0.3, 0.4),
    # The Pulse wavefront: the beam dropped an octave and a half.
    "pulse":     ("beam.ogg",
                  f"asetrate={RATE}*0.45,aresample={RATE},lowpass=f=2200", 0.5, 0.7),
    # A Volt: the beam pitched into a blip.
    "volt":      ("beam.ogg",
                  f"asetrate={RATE}*2.6,aresample={RATE},highpass=f=1500", 0.09, 0.3),
    # An Echo: the same blip, lower and longer, so it reads as worth more.
    "echo":      ("beam.ogg",
                  f"asetrate={RATE}*1.6,aresample={RATE}", 0.35, 0.55),
    # A relay node lighting up.
    "node":      ("beam.ogg",
                  f"asetrate={RATE}*2.0,aresample={RATE}", 0.16, 0.45),
    # The shutter unbolting: the explosion stretched into machinery.
    "gate":      ("explosion.ogg",
                  f"asetrate={RATE}*0.5,aresample={RATE},lowpass=f=1400", 0.9, 0.9),
    # Boots on wet asphalt.
    "land":      ("explosion.ogg",
                  f"asetrate={RATE}*1.7,aresample={RATE},lowpass=f=1100", 0.12, 0.3),
    # The dash: beam, brightened and clipped to a whoosh.
    "dash":      ("beam.ogg",
                  f"asetrate={RATE}*1.35,aresample={RATE},highpass=f=900", 0.22, 0.35),
}


def run(cmd):
    subprocess.run(cmd, check=True)


def main():
    if not MUSIC_SRC.exists():
        print(f"error: {MUSIC_SRC} does not exist")
        return 1
    OUT.mkdir(parents=True, exist_ok=True)
    total = 0

    for name, fn in MUSIC.items():
        src = MUSIC_SRC / fn
        if not src.exists():
            print(f"  skip mus_{name}: {fn} not found")
            continue
        dst = OUT / f"mus_{name}.wav"
        # Capped at two minutes: they loop anyway, and the tail of a long track
        # is memory a handheld does not need to be holding.
        run(["ffmpeg", "-v", "error", "-y", "-i", str(src), "-t", "120",
             "-ac", "1", "-ar", str(RATE), "-filter:a", "volume=0.55",
             "-acodec", "pcm_s16le", str(dst)])
        total += dst.stat().st_size
        print(f"  mus_{name:6s} {dst.stat().st_size // 1024:5d} KB  <- {fn}")

    for name, (fn, filt, secs, gain) in SFX.items():
        src = SFX_SRC / fn
        if not src.exists():
            print(f"  skip sfx_{name}: {fn} not found")
            continue
        dst = OUT / f"sfx_{name}.wav"
        run(["ffmpeg", "-v", "error", "-y", "-i", str(src), "-t", str(secs),
             "-ac", "1", "-ar", str(RATE),
             "-filter:a", f"{filt},volume={gain}",
             "-acodec", "pcm_s16le", str(dst)])
        total += dst.stat().st_size
        print(f"  sfx_{name:9s} {dst.stat().st_size // 1024:4d} KB  <- {fn}")

    print(f"  total {total // 1024} KB in {OUT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
