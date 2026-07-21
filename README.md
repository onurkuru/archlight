# ARCLIGHT

A cinematic 2.5D cyberpunk flow-platformer for PlayStation Vita.
*Rayman Legends' run, filmed like Replaced.*

**Status: playable on desktop, never run on Vita hardware.** One level (`1-1`, "THE GUTTER")
with all 7 verbs, one enemy type, and real art. It cross-compiles and packs a VPK, but the
question M0 exists to answer — whether the SGX543 holds 60 fps with this post stack — is
still open. No audio yet.

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
| `Down + Z` in the air | stomp |
| `C` | tether (auto-targets the nearest anchor ahead of you) |
| `SPACE` | toggle the post stack (A/B its cost) |
| `R` | restart the level |
| `ESC` / Vita `START` | quit |

| Env var | Effect |
| --- | --- |
| `ARCLIGHT_LEVEL=n` | load level `n` from `src/levels.h` |
| `ARCLIGHT_WARP=tiles` | spawn at that tile x, straight down onto the level |
| `ARCLIGHT_SHOT=out.bmp` | render one frame, write it, exit (`ARCLIGHT_SHOT_FRAME=n` to wait `n` frames first, e.g. to let physics settle before the dump) |
| `ARCLIGHT_NOVSYNC=1` | uncap the frame rate, for comparable fps numbers |

Frame stats print once per second: fps, ms, draw calls, quads.

Levels are generated, not hand-placed yet — `tools/genlevels.py` describes `1-1` as grid
operations (floors, arcs, Volt trails, enemy placements) and validates it against the
authoring rules in the GDD (one spawn, one exit, ≤70 tiles between checkpoints) before
emitting `src/levels.h`. This is a placeholder for the Tiled pipeline in
[TECH_PLAN.md](docs/TECH_PLAN.md) §5, not the long-term tool — it exists because it got a
level running the same day the physics did.

```sh
python3 tools/genlevels.py    # regenerate src/levels.h after editing the level
```

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
