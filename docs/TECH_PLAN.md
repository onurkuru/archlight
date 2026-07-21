# ARCLIGHT — Technical Plan (PS Vita / vitaGL)

Companion to [GDD.md](GDD.md). Target: PS Vita homebrew VPK + desktop dev build from one
codebase.

---

## 1. Stack

| Layer | Vita | Desktop (macOS/Linux/Win) |
| --- | --- | --- |
| Window / input / audio / threads | **SDL2** (Vita port) | SDL2 |
| Graphics API | **vitaGL** — <https://github.com/rinnegatamante/vitaGL> | OpenGL 3.3 core via GLAD |
| Shading | GLSL ES 1.00 subset, **precompiled to `.gxp` at build time** | same GLSL source, compiled at runtime |
| Language | C11 | C11 |
| Build | CMake + VitaSDK (`~/vitasdk`, already installed) | CMake |
| Level tooling | Tiled `.tmx` → custom binary `.alv` via a Python packer | same |

### Why vitaGL

vitaGL wraps `sceGxm` behind an OpenGL subset and — critically for this game — exposes
**FBOs / render-to-texture, VBOs and a GLSL path**. ARCLIGHT's whole art direction is a
multi-pass post stack (emissive bloom, reflections, LUT grade). That is not practical on
`vita2d`'s fixed sprite pipeline, and writing raw `sceGxm` costs weeks. vitaGL also keeps the
renderer ~95 % shared with the desktop GL build, which is where 90 % of iteration happens.

### Shader distribution decision (important)

vitaGL's runtime GLSL path depends on `libshacccg.suprx`, which end users must extract on
their own console. **We will not ship a dependency on that.** Instead:

- Shaders are authored once in GLSL ES 1.00.
- At build time, a CMake step compiles each shader to a `.gxp` binary with the VitaSDK
  shader compiler and embeds it in the VPK.
- At runtime the Vita build loads binaries via `glShaderBinary`; the desktop build compiles
  the same sources with `glShaderSource`.
- A thin `gfx_shader_load(name)` hides the difference.

**Spike this in Milestone 0** — it is the single highest-risk assumption in the plan. Fallback
if binary loading proves painful: ship `libshacccg.suprx` install instructions in the README
(common practice in Vita homebrew) and compile at boot with a cached result.

---

## 2. Render Pipeline

Internal resolution **480×272**, presented at 960×544 (integer ×2). This is the core
performance decision: the SGX543MP4+ is fill-rate bound, and a 4-pass full-screen stack at
960×544 will not hold 60 fps. At 480×272 the same stack costs a quarter of the fill and the
pixel art stays authentic instead of being downsampled.

```
[0] Scene pass      → FBO_scene   (RGBA8 480×272)
      layers 0..6, sprite batched, alpha blend
      writes emissive mask to alpha of FBO_emissive (RGBA8, same size)

[1] Light pass      → FBO_light   (RGBA8 480×272)
      clear to district ambient colour
      additive radial light quads (soft gradient texture, tinted, ~24 lights max on screen)

[2] Reflection pass → FBO_reflect (RGBA8 240×136)
      mid + entity layers re-drawn flipped about the floor line, half res, then
      UV-distorted by a scrolling noise texture in the composite

[3] Bloom           → FBO_bloomA/B (240×136 then 120×68)
      threshold from emissive mask → downsample → 2× separable gaussian → upsample

[4] Composite       → default framebuffer 960×544
      scene × light + bloom + reflect (floor rows only) + fog planes
      → 3D LUT grade (32³ packed as a 1024×32 strip) → optional CA/scanline → ×2 upscale
```

### Budgets (per frame, 16.6 ms @ 60 fps)

| Item | Budget |
| --- | --- |
| Draw calls | ≤ 120 (aggressive atlas batching; one call per layer per atlas) |
| Sprites | ≤ 1500 |
| Dynamic lights | ≤ 24 on screen |
| Particles | ≤ 600 (rain is a scrolling shader, **not** particles) |
| CPU game update | ≤ 4 ms |
| GPU total | ≤ 11 ms |

### Techniques, cheapest-first

- **Rain**: full-screen scrolling texture in the composite shader, 3 depths. Zero particles.
- **Fog**: one tinted quad per parallax gap.
- **God rays**: additive cone billboards with scrolling noise. No raymarching.
- **Depth of field**: background layers are **pre-blurred offline** in the asset pipeline.
  No runtime DoF anywhere.
- **Shadows**: none dynamic. Levels ship a baked ambient-occlusion multiply layer authored
  in the tileset.
- **Sprite lighting**: sprites are multiplied by the light buffer + a cheap rim term from a
  per-sprite direction constant. No normal maps — the free asset packs don't ship them and
  generating them looks worse than not having them.

---

## 3. Memory

Vita gives homebrew roughly 350 MB main + 128 MB CDRAM/VRAM (via `vitaGL`'s heap config, set
with `vglInit`/`vglInitExtended` — tune the pool sizes at startup).

| Budget | Target |
| --- | --- |
| Texture atlases resident | ≤ 48 MB (one district at a time, 1024×1024 RGBA8 atlases) |
| FBOs | ~6 MB total at the resolutions above |
| Audio (streamed OGG + SFX bank) | ≤ 24 MB |
| Level + game state | ≤ 8 MB |

**Optimisation held in reserve:** pixel art uses few colours — an indexed-8 texture format
with the palette applied in the fragment shader cuts atlas memory ~4× and enables free
per-district palette swaps. Adds asset-pipeline complexity; only do it if atlas budget bites.
ETC1 is available in vitaGL but compresses pixel art badly (blocky artefacts on hard edges) —
avoid for sprites, acceptable for pre-blurred background layers.

---

## 4. Code Architecture

```
arclight/
  src/
    main.c            entry, SDL init, vitaGL/GL init, main loop, fixed timestep
    platform/         plat_vita.c / plat_desktop.c  (paths, input mapping, shader load)
    gfx/              gfx.c (batcher), fbo.c, shader.c, post.c, light.c, camera.c
    game/             player.c physics.c level.c entity.c enemy.c boss.c collect.c
    ui/               hud.c menu.c results.c story.c font.c
    audio/            audio.c (SDL_audio callback mixer), music.c (stems + beat clock)
    core/             math, arena allocator, fixed-point-free f32 helpers, save.c
  shaders/            *.vert / *.frag (GLSL ES 1.00)  → build step → *.gxp
  assets/             atlases, levels (.alv), audio, luts
  tools/              tmx2alv.py, atlaspack.py, emissive_mask.py, blur_bg.py, shadergen.py
  sce_sys/            LiveArea (icon0, bg, startup, template.xml)
  CMakeLists.txt      -DBUILD_VITA=ON switches toolchain + renderer backend
```

**Loop:** fixed 60 Hz simulation, decoupled render, accumulator clamped at 4 steps.
Physics in floats, deterministic enough for ghost replays (Ghost Runs record input, not
position — 60 inputs/s × 4 min ≈ 14 KB per ghost).

**Threads:** main (sim+render), audio callback, one streaming/decode thread.

---

## 5. Asset Pipeline

1. **Tiled** (`.tmx`) authors collision, entity placement, light placement, camera hints,
   checkpoint zones, Volt trails.
2. `tools/tmx2alv.py` → binary level: tile layers RLE'd, entity table, light table, spline
   data for Volt trails. Validates every level (reachability of all 6 Echoes, no >20 s
   checkpoint gap, no blind drops) — **the validator is a design tool, not just a build step.**
3. `tools/atlaspack.py` → 1024×1024 atlases + JSON frame table → generated C header.
4. `tools/emissive_mask.py` → per-sprite emissive channel by thresholding saturated bright
   pixels; hand-editable override PNGs where the heuristic is wrong.
5. `tools/blur_bg.py` → pre-blurred parallax variants (the fake DoF).
6. Shaders → `.gxp` via VitaSDK compiler, embedded into the VPK.

Reuse from Skyrift where possible: the CMake dual-target setup, LiveArea packaging, the
save-path abstraction (`SDL_GetPrefPath` / `ux0:data`), the screenshot-dump debug hook
(`ARCLIGHT_SHOT=/path.png` → render one frame, write, exit) which is how the visuals get
self-verified without hardware in the loop.

---

## 6. Debug & Verification

- `ARCLIGHT_LEVEL=3-2` jump to any level.
- `ARCLIGHT_SHOT=out.png` one-frame dump and exit (visual regression).
- `ARCLIGHT_TEST=1` headless physics/validator smoke tests.
- On-screen frame graph (F3 / rear-touch double-tap on Vita): CPU ms, GPU ms, draw calls,
  sprite count, atlas residency.
- Every district gets a **capture level** — a static scene used only for screenshot diffs
  when the post stack changes.

---

## 7. Milestones

| M | Name | Duration | Exit criteria |
| --- | --- | --- | --- |
| **M0** | **vitaGL spike** | 1 week | *(in progress — see §10)* Full post stack at 480×272 with 1500 sprites, running on real hardware at ≥60 fps, shaders loaded from precompiled `.gxp`. **Go/no-go for the whole render plan.** |
| **M1** | Engine core | 3 weeks | Sprite batcher, atlases, camera, tilemap, fixed-timestep loop, input, desktop+Vita parity |
| **M2** | Movement vertical slice | 3 weeks | All 7 verbs, tuned, in one grey-box level. **Playtest gate: is running around a bare room already fun?** If no, stop and re-tune. |
| **M3** | The look | 3 weeks | Full post stack: light, bloom, reflections, LUT, rain, fog. One district's first 30 s at final visual quality. |
| **M4** | Tools | 2 weeks | Tiled pipeline + validator + atlas packer end-to-end |
| **M5** | Systems | 3 weeks | Enemies, collectibles, Charge, checkpoints, HUD, results screen, save |
| **M6** | Content | 10 weeks | 25 levels, 5 bosses (the long pole; ~2 weeks per district) |
| **M7** | Audio & Signal Runs | 3 weeks | Beat clock, adaptive stems, 5 rhythm levels |
| **M8** | Story & polish | 3 weeks | Story screens, endings, accessibility, LiveArea, translations of nothing (English only) |
| **M9** | Release | 2 weeks | Hardware soak test, GitHub release, VitaDB submission |

~33 weeks solo at a steady pace. M6 is where projects die; the mitigation is that M4's
tooling must be genuinely fast before M6 starts.

---

## 8. Risk Register

| Risk | Severity | Mitigation |
| --- | --- | --- |
| Precompiled shader path doesn't work as expected | **High** | M0 spike is exactly this. Fallback: `libshacccg.suprx` + boot-time compile with cache. |
| Fill rate: 4-pass stack too slow even at 480×272 | High | Passes are individually switchable; drop order = reflections → god rays → CA → bloom half-res. Ship a "performance mode" toggle. |
| Content scope (25 levels solo) | High | Tools first (M4). Cut to 4 districts / 20 levels if M6 slips past week 6. |
| Asset licensing on the paid ansimuz packs | Medium | See [ASSETS.md](ASSETS.md); verify each pack's licence text *before* it enters the repo. |
| vitaGL API gaps vs desktop GL | Medium | Restrict to a hand-written "GL subset we allow" header; CI-ish grep to catch calls outside it. |
| Boss AI (Seven duel) complexity | Medium | Scripted routes, not real AI. Prototype in M2 with the grey-box. |
| Beat-accurate audio sync on Vita | Medium | Derive the beat clock from bytes fed to the audio callback, never from wall time. |

---

## 9. Immediate Next Steps

1. Run `arclight.vpk` on hardware and read the frame stats — the actual M0 verdict.
2. Collect the shader `.gxp` dumps from `ux0:data/arclight/shaders/`, commit them,
   rebuild, and confirm a clean console (no `libshacccg.suprx`) runs the VPK.
3. Buy/collect the asset packs, fill in the licence audit in [ASSETS.md](ASSETS.md).
4. Grey-box `1-1` in Tiled.

---

## 10. M0 Progress Log

**Done (2026-07-21):**

- Dual-target CMake builds clean: desktop (SDL2 + GL 3.3 core, macOS) and
  `-DBUILD_VITA=ON` → `arclight.vpk` (~1 MB, shaders packed inside).
- Vita link set resolved: `SDL2 vitaGL vitashark SceShaccCgExt mathneon stdc++` plus the
  usual stubs. `mathneon` and `SceAudioIn_stub` are the two that are easy to miss —
  vitaGL's matrix code and SDL2's Vita audio backend need them respectively.
- Full 5-pass stack implemented and verified on desktop: scene 480×272 → saturation-biased
  bright pass 240×136 → separable gaussian ×2 → composite at 960×544 with grade, vignette,
  scanlines and radial chromatic aberration.
- One GLSL source tree serves both platforms via a preamble that maps
  `attribute`/`varying`/`TEX2D`/`FRAGCOLOR` onto GLSL ES 1.00 or GLSL 3.30.
- Shader binary bootstrap implemented: `.gxp` beside the source is preferred and loaded
  with `glShaderBinary`; otherwise the GLSL is compiled at runtime and dumped to
  `ux0:data/arclight/shaders/`. **Code path exists but is unverified on hardware.**
- Desktop: 1500 sprites, **8 draw calls, ~0.5 ms/frame** (M-series Mac, vsync off).

**Lesson worth keeping:** the first implementation flipped blend mode per sprite and cost
**115 draw calls**; ordering the scene into an alpha pass and an additive pass took it to
**8**. Draw calls are decided by layer order, not by sprite count — the batcher cannot
save a renderer that changes state mid-list.

**Still unknown:** everything that matters. Nothing has run on a Vita yet, so the fill-rate
question the spike exists to answer is still open.
