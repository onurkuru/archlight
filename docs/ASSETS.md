# ARCLIGHT — Asset Plan & Licence Audit

All art is sourced from **ansimuz** packs. The design deliberately does *not* repaint source
art — the cinematic look comes from lighting, grading and composition (see GDD §6), so
off-the-shelf pixel art can carry a bespoke-looking game.

---

## 1. Licence audit — DO THIS FIRST

**Rule: no asset enters `assets/` until its licence text is read, saved to
`assets/licences/<pack>.txt`, and confirmed to permit commercial/redistributed use in a
compiled game.**

ansimuz's packs are not uniformly licensed — some are CC0 (as Sunny Land was for Skyrift),
others carry a custom licence that permits use in games but **forbids redistributing the raw
art files**. That distinction matters here because a VPK ships the atlases inside it. Packed,
recoloured atlases are normally fine; a raw pack dump is not. Verify per pack, and if a
licence is unclear, email ansimuz before shipping — this is a 10-minute email that removes a
project-ending risk.

Record the audit in this table as it happens:

| Pack | Price | Licence | Verified | Notes |
| --- | --- | --- | --- | --- |
| Cyberpunk Street Environment | paid | ? | ☐ | primary tileset, district 1 |
| Warped City | free | ? | ☐ | |
| Warped City 2 | free | ? | ☐ | |
| Synth Cities Environment | free | ? | ☐ | |
| Sewers Action Pack | free | ? | ☐ | |
| Warped Caves | free | ? | ☐ | |
| Mega Bot | free | ? | ☐ | player candidate |
| Streets of Fight | free | ? | ☐ | humanoid NPCs/enemies |
| Space Backgrounds | free | ? | ☐ | skyline/void |
| Warped Highway | $5 | ? | ☐ | optional |
| Warped City Addon | $9 | ? | ☐ | optional |
| Warped Synth Cities Backgrounds ADDON | $5 | ? | ☐ | optional |
| Sci-Fi Icon and Items | $5 | ? | ☐ | UI/collectible icons |

*(Prices as listed on the itch.io "Warped Packs" collection, July 2026 — re-check before purchase.)*

---

## 2. District → Pack Mapping

| District | Primary | Secondary | Notes |
| --- | --- | --- | --- |
| 1 — **The Gutter** | Cyberpunk Street Environment | Warped City | Street level, rain, signage. The pack's wet-street look *is* the game's first impression — spend the money here. |
| 2 — **The Stacks** | Warped City 2 | Cyberpunk Street Env. props, Warped City Addon | Container/vertical geometry assembled from modular city pieces. |
| 3 — **Sublevel Nine** | Sewers Action Pack | Warped Caves | Near-black grading; the pack's detail barely shows, which is the point. |
| 4 — **Meridian Spire** | Synth Cities Environment | Space Backgrounds | Clean geometry, hard cyan light. Grade to white/cyan LUT. |
| 5 — **Broadcast Core** | *procedural / hand-drawn* | Space Backgrounds | Wireframe abstraction — mostly generated geometry + glow, minimal sprite work. Cheapest district to build. |

**Skyline / parallax:** Space Backgrounds packs, recoloured per district via LUT, pre-blurred
by `tools/blur_bg.py`.

---

## 3. Characters

| Role | Source | Work needed |
| --- | --- | --- |
| **NINE** (player) | Mega Bot (free) or the Cyberpunk Street Env. character | Needs custom frames regardless: wall-run, tether swing, stomp, dash, apex hang. Budget ~60 frames of original animation — this is the one place hand-drawing is unavoidable, because the movement *is* the game. |
| **SEVEN** | Recolour + silhouette edit of NINE | Deliberately the same rig — she mirrors your moveset, so sharing the rig is a design statement, not a shortcut. |
| **PARCEL** | Sci-Fi Icons / Mega Bot drone parts | ~12 frames, mostly a hover loop + emote poses. |
| **Peacekeeper / thugs** | Streets of Fight (free) | Recolour, add emissive visor. |
| **Drones / turrets** | Warped City, Mega Bot enemies | Minor edits. |
| **Bosses ×5** | Composited from pack machinery + hand work | The biggest original-art cost after NINE. Budget 2–3 days each. |

---

## 4. Processing Pipeline (applied to every source asset)

1. **Trim + repack** into 1024×1024 atlases (`tools/atlaspack.py`).
2. **Emissive mask** generated per sprite (`tools/emissive_mask.py`) — this is what makes
   ansimuz's neon actually bloom. Hand-fix the misses.
3. **Pre-blur** background layers at 2 blur levels (fake DoF).
4. **Palette check** — flag any asset whose palette fights the district LUT; recolour those
   at the source rather than fighting it in the shader.
5. Store **only processed atlases** in git; keep raw packs in a local `art-src/` that is
   `.gitignore`d (also neatly sidesteps the raw-redistribution licence question).

---

## 5. Audio

Art packs don't cover audio. Options, in preference order:

1. Original synthwave written in a tracker/DAW (best fit — the Signal Run levels need exact
   BPM control and stems, which licensed tracks rarely provide).
2. CC0 / CC-BY synthwave from OpenGameArt or Free Music Archive, with the stem requirement
   compromised (Signal Runs then use a single stem).
3. Procedural chiptune fallback (as Skyrift did) — cheap, but wrong for this game's tone.

SFX: sfxr/Chiptone for UI, freesound CC0 for rain/city/metal, all recorded into one bank.
