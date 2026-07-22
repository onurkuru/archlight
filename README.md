# ARCLIGHT

A cinematic 2.5D cyberpunk flow-platformer for PlayStation Vita.
*Rayman Legends' run, filmed like Replaced.*

**Status: playable on desktop, never run on Vita hardware.** Ten levels across five
districts — one flow run and one boss duel each — four enemy archetypes plus five
distinct bosses, a hack gate, hazards, audio, and an opening. It cross-compiles and
packs a VPK, but the question M0 exists to answer — whether the SGX543 holds 60 fps
with this post stack — is still open.

- [Game Design Document](docs/GDD.md) — story, mechanics, level design, art direction
- [Technical Plan](docs/TECH_PLAN.md) — vitaGL pipeline, budgets, milestones, risks
- [Asset Plan](docs/ASSETS.md) — pack mapping and the licence audit

## Building

### Desktop (macOS / Linux)

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/arclight
```

Needs SDL2 and OpenGL 3.3.

### PS Vita

```sh
export VITASDK=$HOME/vitasdk
export PATH=$VITASDK/bin:$PATH
cmake -S . -B build-vita -DBUILD_VITA=ON
cmake --build build-vita          # -> build-vita/arclight.vpk
```

## Playing it

`1-1` loads on launch. Controls:

| Input | Action |
| --- | --- |
| Arrows / WASD | run |
| `Z` / Space | jump (hold for height, coyote time + input buffer) |
| `X` | arc dash (8-way, ground or air) |
| `Up` / `W` | aim up — hold while firing for up / up-diagonal shots (Metal Slug style) |
| `Down + Z` in the air | stomp |
| `C` | tether (auto-targets the nearest anchor ahead of you) |
| `V` | fire — Nine's sidearm, 8-way aimed by the stick (hold Up / Down while firing). Cyan is yours, orange is theirs. Never interrupts your run |
| `F` | pulse — EMP the nearest drone (30 Charge): it drops for 3 s, then reboots. Never kills. At a terminal it opens the hack window instead, free — light the relay nodes by running through them before it closes and the gate unbolts |
| `SPACE` | toggle the post stack (A/B its cost) |
| `R` | restart the level |
| `ESC` / Vita `START` | quit |

| Env var | Effect |
| --- | --- |
| `ARCLIGHT_LEVEL=n` | load level `n` from `src/levels.h` |
| `ARCLIGHT_WARP=tiles` | spawn at that tile x, straight down onto the level |
| `ARCLIGHT_SHOT=out.bmp` | render one frame, write it, exit (`ARCLIGHT_SHOT_FRAME=n` to wait `n` frames first, e.g. to let physics settle before the dump) |
| `ARCLIGHT_NOVSYNC=1` | uncap the frame rate, for comparable fps numbers |
| `ARCLIGHT_ATTACK_EVERY=s` | fire the melee input every `s` seconds, so a `ARCLIGHT_SHOT` run can catch a swing without a human at the keyboard |
| `ARCLIGHT_PULSE_EVERY=s` | the same, for the pulse |
| `ARCLIGHT_JUMP_EVERY=s` | the same, for the jump — mashing at a fixed rate is how you reproduce a movement bug the same way twice |

Enemies: **Scrappers** are flying drones — bounce targets. **Enforcers** walk the
street, turn to face you, and shoot after a 0.3 s visor flare. **Sentries** are
bolted down and hold a lane — routed past, not fought.

Bosses are single-screen duels: one locked camera frame, walled on both sides, no
checkpoints and no exit — the level completes itself when the boss falls. All five
are Meridian machines (a combat drone or an egg-turret, district-tinted), and each
moves and fires differently: WARDEN charges side to side, BOUNCER hops in arcs and
slams down, COIL hangs back and lobs spread shots, TIDE sweeps high and rains fire,
CHORUS holds centre and summons drones with radial bursts. Each demands a different
opening (cyan aura = open, red shimmer + clang = armoured): WARDEN always, BOUNCER
from above, COIL from behind or above, TIDE at the ends of its sweep, CHORUS only
once its drones are dead. A chipped boss guards for 0.7 s and dying resets it to
full health.

Hazards: **moving platforms** shuttle across gaps (land on top, get carried — the
street runs underneath, so a mover is always the fast line and never a wall) and
**containment beams** cycle on and off from ceiling emitters. A beam hurts to
touch, but it is a rhythm, not a wall — run the gap in its cycle, or `F` Pulse it
to drop it for four seconds (GDD verb 7, "kills lasers").

Frame stats print once per second: fps, ms, draw calls, quads.

Levels are generated, not hand-placed — `tools/genlevels.py` composes each district's
flow level from that district's own section pool (D1 rooftops and cover, D2 tethers
and movers, D3 wall-jump shafts, D4 a flat coastal highway, D5 lab corridors), so no
two districts are built from the same moves, and refuses to emit anything that fails
validation. Beyond the GDD's authoring rules (one spawn, one exit, ≤70 tiles
between checkpoints) it runs a **reachability flood fill** derived from the physics
constants in `src/game.c`: it walks the level the way the player can, with the gate shut
and again with it open, and fails the build if the exit, a checkpoint, a terminal, a relay
node or an Echo cannot be stood next to. It also enforces a 4-tile minimum platform width.

Both rules exist because the first pass of these levels was 70% unreachable — a fire escape
whose first step was three tiles above a 2.6-tile jump — and no amount of reading the ASCII
grid found it. Difficulty in this game is enemies; the geometry is meant to be easy, and
the validator is what keeps it that way.

```sh
python3 tools/genlevels.py    # regenerate src/levels.h
python3 tools/atlaspack.py    # regenerate the art atlas + src/atlas_city.h
python3 tools/audiopack.py    # regenerate assets/audio (needs ffmpeg)
```

## Audio

Music and SFX are baked to 22050 Hz mono s16 WAV by `tools/audiopack.py` and mixed by
`src/audio.c` against raw SDL2 — no decoder, no mixer library, so nothing extra has to
cross-compile for the Vita. Eight one-shot voices and one looping track; the track follows
the district and a Warden takes it over.

## The render pipeline

Every frame runs the post stack the art direction depends on (see
[TECH_PLAN.md](docs/TECH_PLAN.md) §2):

```
scene 480x272  ->  bright 240x136  ->  blur H  ->  blur V  ->  composite 960x544
```

The world is drawn into a 240x136 window inside that 480x272 buffer, so one source pixel
covers exactly four screen pixels. The bright pass biases its threshold by saturation, not
just luminance — neon is saturated *and* bright, wet concrete is only bright, which is what
keeps signs blooming and walls not. Rain, the moonlight grade, vignette and chromatic
aberration all land in the composite pass; reflections are a second mirrored quad per actor.

### Shaders on Vita

Shaders are compiled from GLSL at boot by vitaGL, which requires
**`libshacccg.suprx`** to be installed on the console — standard on jailbroken
Vitas and installable from the Vita Homebrew Browser.

If a `shaders/<name>.<stage>.gxp` binary is ever placed next to the GLSL source,
it is loaded instead and the compiler is skipped.

## Licence

Code: MIT.

Art: **CC0**. `assets/atlas_city.png` is baked from [ansimuz](https://ansimuz.itch.io)'s
*Warped City* and *Warped City 2* packs by `tools/atlaspack.py`. Every pack used ships a
`public-license.pdf` releasing it under Creative Commons Zero — no attribution required,
commercial use and redistribution explicitly permitted. The per-pack audit is in
[docs/ASSETS.md](docs/ASSETS.md). Credit is given here because it is deserved, not because
the licence demands it.

Raw asset packs are not redistributed here — only the packed atlas. To regenerate it,
download the packs into `art-src/` and run:

```sh
python3 tools/inspect_art.py    # list what's inside each zip
python3 tools/atlaspack.py      # -> assets/atlas_city.png + src/atlas_city.h
```
