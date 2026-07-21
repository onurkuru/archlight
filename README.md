# ARCLIGHT

A cinematic 2.5D cyberpunk flow-platformer for PlayStation Vita.
*Rayman Legends' run, filmed like Replaced.*

**Status: M0–M2 done on desktop, unverified on Vita hardware.** One playable grey-box level
(`1-1`, "THE GUTTER") with all 7 verbs and one enemy type. Zero real art, zero audio.

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

### Shaders on Vita

Shaders are compiled from GLSL at boot by vitaGL, which requires
**`libshacccg.suprx`** to be installed on the console — standard on jailbroken
Vitas and installable from the Vita Homebrew Browser.

If a `shaders/<name>.<stage>.gxp` binary is ever placed next to the GLSL source,
it is loaded instead and the compiler is skipped.

## Licence

Code: MIT. Art: see [docs/ASSETS.md](docs/ASSETS.md) — **not yet audited, do not
redistribute art from this repo until that table is filled in.**
