# ARCLIGHT

A cinematic 2.5D cyberpunk flow-platformer for PlayStation Vita.
*Rayman Legends' run, filmed like Replaced.*

**Status: Milestone 0 — renderer spike.** Not a game yet.

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

## The M0 spike

`src/main.c` runs the exact post stack the art direction depends on, over a
synthetic scene with no art files (so it works before the asset licence audit
is finished):

```
scene 480x272  ->  bright 240x136  ->  blur H  ->  blur V  ->  composite 960x544
```

| Input | Effect |
| --- | --- |
| `ESC` / `START` | quit |
| `SPACE` / `✕` | toggle the whole post stack (A/B its cost) |
| `[` `]` | ±250 sprites |

| Env var | Effect |
| --- | --- |
| `ARCLIGHT_SPRITES=n` | sprite budget (default 1500) |
| `ARCLIGHT_SHOT=out.bmp` | render one frame, write it, exit |

Frame stats print once per second: fps, ms, draw calls, quads.

### Shaders on Vita

Shaders are compiled from GLSL at boot by vitaGL, which requires
**`libshacccg.suprx`** to be installed on the console — standard on jailbroken
Vitas and installable from the Vita Homebrew Browser.

If a `shaders/<name>.<stage>.gxp` binary is ever placed next to the GLSL source,
it is loaded instead and the compiler is skipped.

## Licence

Code: MIT. Art: see [docs/ASSETS.md](docs/ASSETS.md) — **not yet audited, do not
redistribute art from this repo until that table is filled in.**
