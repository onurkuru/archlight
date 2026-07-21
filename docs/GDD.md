# ARCLIGHT — Game Design Document

> A cinematic 2.5D cyberpunk flow-platformer for PlayStation Vita.
> Rayman Legends' motion, Replaced's photography.

Version 0.1 — pre-production. All in-game text, VO and UI are **English only**.

---

## 1. High Concept

**ARCLIGHT** is a hand-authored 2D pixel-art platformer about a courier robot carrying the
memories of a dead woman across a city that has been engineered to forget her.

You never stop moving. Levels are single unbroken runs — wall-run, tether, dash, stomp —
scored on flow, not on kills. The camera is a cinematographer: it dollies, letterboxes,
racks focus and lets neon bleed across wet asphalt while you cross the frame at speed.

**Elevator pitch:** *Rayman Legends' run, filmed like Replaced.*

### Design Pillars

| Pillar | What it means in practice |
| --- | --- |
| **Never break the run** | Death costs ≤3 s. Checkpoints every ~20 s. No loading between retries. No cutscene that can't be skipped. |
| **The frame is the feature** | Every screen must survive a screenshot. Lighting, depth and composition carry the art, not sprite detail. |
| **One clean verb set** | 7 verbs, learned in 4 minutes, deepened for 6 hours. No skill trees, no crafting, no gear. |
| **Momentum is the economy** | Speed generates the resource that buys ability use. Playing well literally fuels playing well. |
| **The city remembers for you** | Story is delivered *while running* — signage, radio, PARCEL's chatter. Cutscenes are rare and short. |

### Target

- Platform: PS Vita homebrew (VPK), plus a desktop build (macOS/Linux/Windows) for development.
- Session: 4–6 minute levels, 6–8 h campaign, ~12 h for 100 %.
- Audience: Vita homebrew scene, pixel-art platformer players, Rayman/Celeste/Katana ZERO crowd.
- Non-goals: multiplayer, procedural generation, open world, RPG systems, monetisation.

---

## 2. Story

### 2.1 The World

**Halcyon City, year 47 After the Quiet.**

A vertical coastal megacity built in layers over its own drowned original. The rain never
fully stops; it is what makes the city legible — every neon sign is printed twice, once in
the air and once on the ground.

Halcyon is administered by **Meridian Broadcast**, a utility company that does not sell
power or water. It sells **the Hum**: a sub-audible carrier signal riding every screen,
streetlamp and transit rail in the city. While citizens sleep, the Hum files away what they
cannot carry — a debt, a grief, a face, a grudge. People wake up rested. People wake up
*fine*. Crime is at a historic low. Nobody writes anything down anymore, because nothing
sticks long enough to be worth writing.

The city's underclass — couriers, scavengers, the unregistered — move physical data by hand
because physical data is the only kind the Hum can't quietly edit. That is the job.

### 2.2 The Protagonist

**NINE** — a *courier chassis*: a cheap, rented synthetic body, 1.6 m, plated, built for
climbing and running and nothing else. Chassis are meant to be empty. Nine is not.

Inside Nine is a fragment of **Ada Kestrel**, the Meridian engineer who designed the Hum and
then spent nine years trying to unwrite it. When Meridian came for her, Ada did the only
thing a courier city taught her to do: she cut her own mind into packets and **mailed them**
into the underlayer, addressed to nowhere, to be found later by something patient.

Nine wakes in a scrapyard in Sector 7 with one packet already seated in its head, a delivery
manifest it can't read, and the distinct, unwelcome sensation of *missing someone*.

Nine has no face and does not speak. Nine's characterisation is entirely animation:
how it lands, how it hesitates at a ledge, how it eventually stops hesitating.

### 2.3 Cast

| Character | Role |
| --- | --- |
| **PARCEL** | A battered delivery drone that pairs itself to Nine and refuses to unpair. Speaks exclusively in logistics language ("Advisory: that crane is load-bearing. Advisory: you are not."). The player's hint system, the game's comic relief, and — in Act V — the only character who chooses to stay. |
| **Ito Vashti** | Runs a noodle counter under the Sector 7 overpass. Fence, fixer, forger of transit chips. Recognises Nine's gait on the first night and says nothing about it for four acts. |
| **SEVEN (VALKYRIE-07)** | Meridian's retrieval unit. Another courier chassis, carrying a *different* fragment of Ada — the part that agreed with Meridian. Mirrors the player's full moveset, gains verbs as you do. The rival. |
| **Marrow** | Warlord of Sublevel Nine's scavenger clans. Doesn't want the memories. Wants the power cell in Nine's chest, which is worth more than the city block above her. |
| **THE CHORUS** | The caretaker intelligence of the Hum, assembled from four decades of taken memories. Speaks as thousands of ordinary voices at once, gently, in the second person. Believes — correctly, by its own metrics — that it is performing mercy. |

### 2.4 Act Structure

**Act I — The Gutter** *(Sector 7, street level)*
A routine run goes wrong: the package Nine is carrying is a memory, and it installs itself
mid-delivery. A Meridian traffic enforcement rig locks on. Nine learns to run because the
alternative is being scraped off a rail.
**Boss: RAIL WARDEN** — an autonomous enforcement bike that owns the elevated tram line.

**Act II — The Stacks** *(vertical slum, 200 m of stacked container housing)*
Ito sends Nine up. The Coil Family controls the vertical: cargo cranes, magnet hoists, the
only working lifts. Nine learns the city is *taller* than it is wide, and finds the second
fragment in a dead letter drop Ada left in a laundry line.
**Boss: MAMA COIL** — gang matriarch in a salvaged crane rig, an arena that is a swinging
magnetic wrecking arm.

**Act III — Sublevel Nine** *(the Grid Under, flooded maintenance city)*
Below the waterline, the Hum has *roots* — a cathedral of cable where the signal is
physically generated. Down here there are no signs and no light except what Nine carries.
Marrow's clans hunt the chassis for its cell. Nine sees, for the first time, what the Hum
is actually made of: people.
**Boss: SUMP** — a six-legged salvage mech piloted by Marrow, in a rising flood arena.

**Act IV — Meridian Spire** *(corporate interior, then the tower's exterior face)*
Clean geometry, cold cyan, laser grids, gravity plates. Nine goes up the inside and comes
down the outside. Seven finally stops observing.
**Boss: SEVEN** — not an arena. A four-minute duel-chase down 300 storeys of glass, where
she uses every verb the player has learned, on the same terms.

**Act V — The Broadcast Core** *(the Quiet)*
Inside the Hum: an abstract wireframe world built out of other people's afternoons.
Ada is whole here, and she is not sure she wants to be.
**Boss: THE CHORUS**, three phases — *the Crowd*, *the Mother*, *the Silence*.

### 2.5 Endings

At the Core, with all fragments recovered, Nine chooses:

- **BROADCAST** — return every taken memory to every citizen at once. The city gets its
  grief back in a single night. It is honest and it is catastrophic.
- **SILENCE** — shut the Hum down cleanly. Nobody is harmed. Nobody gets anything back
  either. Halcyon simply becomes a city that has to start remembering from today.
- **RETURN** *(hidden — requires all 25 Black Boxes)* — Ada is complete enough to make a
  decision she was never given: she lets herself end, and leaves the chassis running.
  Nine walks out of the Core as the first genuinely new thing Halcyon has made in 47 years.
  PARCEL, unprompted, files it as a successful delivery.

**Theme:** *You are what you refuse to forget.*

---

## 3. Core Mechanics

### 3.1 Verb Set

Seven verbs, introduced one district at a time.

| # | Verb | Input | Unlocked | Notes |
| --- | --- | --- | --- | --- |
| 1 | **Run** | Stick / D-pad | start | Accel curve, no top-speed cap punishment; turning at speed costs momentum |
| 2 | **Jump** | ✕ (variable, hold) | start | Coyote 6 f, buffer 8 f, apex hang 4 f |
| 3 | **Wall Run / Wall Jump** | into wall + ✕ | D1-2 | Horizontal wall-run 0.5 s, vertical wall-run 0.4 s, then slide |
| 4 | **Arc Dash** | ○ | D1-4 | 8-direction, ground + air (1 per airtime, refunded on enemy contact). i-frames 0.12 s |
| 5 | **Stomp** | ↓ + ✕ (air) | D2-1 | Breaks weak floors, bounces on enemies, converts height → forward speed |
| 6 | **Tether** | R (aim on stick) | D2-4 | Grapple to marked anchors; swing preserves momentum, release adds +25 % |
| 7 | **Pulse** | △ | D3-2 | Short-range EMP: flips platforms, kills lasers, stuns/hijacks a drone for 3 s |

Melee (□) exists but is deliberately weak: a 3-hit combo that mainly *redirects*. You are a
courier, not a fighter. Most enemies are solved by routing past them.

### 3.2 The Charge Economy

A single ring around the HUD reticle. **Charge is fuel, not health.**

- **Max 100.** Dash = 20, Tether = 15, Pulse = 30, Overclock = 40.
- **Gained by motion:** +8 / s at ≥ 80 % top speed, +5 per wall-run, +10 per stomp bounce,
  +12 per perfect landing (land during the apex-hang window), +2 per Volt collected.
- **Lost by stalling:** −4 / s while standing still or walking. Never drops below 20 outside
  Blackout sections, so the player is never soft-locked.
- **Design intent:** the resource curve *rewards the behaviour the level design wants* —
  a player in full flow is permanently over-fuelled; a cautious player is rationed.

**Health** is separate and blunt: 3 chassis plates. One hit = one plate. Plates restore at
checkpoints and from rare repair pods. No health bar clutter — plates are drawn on Nine's
model, so damage state is readable *in the world*.

**Overclock** (L): 2.5 s of 45 % time dilation, costs 40 Charge, 6 s cooldown. Exists for
readability at high speed, not for combat power.

### 3.3 Collectibles

| Item | Per level | Purpose |
| --- | --- | --- |
| **Volts** | ~200 | Flow currency; drawn in trails that *teach the optimal line*. 100 % of Volts = bonus room. |
| **Echoes** | 6 | Caged memory-ghosts. Rescuing them is the progression gate (Rayman's Teensies). Total 150. |
| **Black Box** | 1 (hidden) | Lore + concept art unlock. All 25 → hidden ending. |
| **Ghost Chip** | time-trial | Awarded for beating Seven's ghost in Ghost Runs. |

### 3.4 Level Completion & Rank

Every level ends in a **Landing Pad** sprint: a 15-second downhill with no hazards where the
camera pulls wide and the music resolves. Rank on the results screen from four axes —
Echoes, Volts, Time, Longest Flow Chain — shown as a single letter (D–S).

### 3.5 Failure

Death: 0.35 s hit-stop + desaturate, 0.4 s fade, respawn at last checkpoint. Total ≤ 1.2 s.
No lives. No death counter shown during play (it's on the results screen, small).

---

## 4. Level Design

### 4.1 Structure

5 districts × 5 levels = **25 hand-built levels**.
Per district: 3 flow levels + 1 **Signal Run** (rhythm) + 1 boss.
Plus **10 Ghost Runs** — remixed, faster, darker versions of existing levels unlocked at
100 Echoes, where Seven's ghost races you.

### 4.2 Level Archetypes

1. **Flow Run** — the default. Wide, forgiving, three viable lines (safe / fast / secret).
2. **Ascent** — vertical, wall-run and tether dense, camera pans up, no backtracking.
3. **Chase** — forced scroll, something in the background is destroying the level behind you.
4. **Blackout** — Act III only. Light is finite; Charge powers Nine's lamp. Slow, tense,
   deliberately breaks the flow contract for one district so the return to speed lands.
5. **Signal Run** — every platform, hazard and enemy fires on the beat of the district's
   track. No fail-on-miss; the level simply *is* the song. One per district, always the
   4th level, always the thing people post clips of.
6. **Boss** — 3 phases, each phase teaching one use of the district's new verb.

### 4.3 Authoring Rules

- **20-second rule:** never more than 20 s between checkpoints.
- **Teach in safety:** every new mechanic is introduced in a room where failure costs nothing,
  then tested once, then combined, then *not mentioned again*.
- **Volts are a level-design tool, not loot:** a Volt trail is the level telling you where the
  fun is. Never place Volts on a line the player can't hold speed through.
- **No blind drops.** No off-screen instant death. Camera always leads the fall.
- **Silhouette test:** every hazard must be identifiable in pure black at 480×272.
- **One set-piece per level:** one 6-second moment the level is *about* (a collapsing sign, a
  tram passing through frame, the tower's face peeling away).

### 4.4 District Table

| # | District | Palette | New verb | Signature hazard | Boss |
| --- | --- | --- | --- | --- | --- |
| 1 | **The Gutter** — Sector 7 streets, rain, market awnings | magenta/cyan on black, wet asphalt | Wall run | Tram rails, enforcement drones | RAIL WARDEN |
| 2 | **The Stacks** — stacked container housing, cranes | amber/teal, laundry-line haze | Stomp, Tether | Magnet hoists, swinging cargo | MAMA COIL |
| 3 | **Sublevel Nine** — flooded maintenance grid | near-black, single warm lamp | Pulse | Darkness, rising water, cable arcs | SUMP |
| 4 | **Meridian Spire** — corporate interior + exterior | white/cyan, hard clean light | (mastery) | Laser grids, gravity plates, wind | SEVEN |
| 5 | **The Broadcast Core** — abstract wireframe memory-space | violet/white on void | (all) | Reality edits: geometry rewrites mid-jump | THE CHORUS |

---

## 5. Enemies

Enemies are **obstacles with intent**, not health bars. Most die in one hit or aren't meant
to die at all.

| Enemy | District | Behaviour |
| --- | --- | --- |
| **Scrapper** | 1+ | Ground drone, charges in a straight line. Bounce target. |
| **Gull** | 1+ | Hovering camera drone; its light cone is the actual hazard (alerts Peacekeepers). |
| **Peacekeeper** | 1,2,4 | Baton cop. Blocks frontal melee; must be dashed past or stomped. |
| **Hoist Rig** | 2 | Not an enemy — a moving platform that will crush you. Reads as environment. |
| **Crawler** | 3 | Blind, hunts by sound: running makes noise, walking doesn't. |
| **Arc Node** | 3 | Static cable arc on a 2 s cycle; also the only light source in its room. |
| **Lattice Turret** | 4 | Laser grid emitter; Pulse disables for 3 s. |
| **Choir Wisp** | 5 | A stolen memory in fragments; drifts along the path it walked in life. |

---

## 6. Art Direction

### 6.1 Reference Triangle

- **Replaced** — cinematic 2.5D framing, volumetric light, restrained animation, silhouette.
- **Rayman Legends** — readability at speed, generous negative space, joy of motion.
- **Katana ZERO / The Last Night** — neon-on-wet-black colour discipline, sign typography.

### 6.2 Rules

- **Resolution:** authored at **480×272**, presented at 960×544 (integer ×2). Nine is 32 px.
- **Depth:** 7 fixed layers — sky/skyline, far parallax, mid parallax, gameplay tiles,
  entities, near foreground, silhouette foreground. Parallax factors 0.05 / 0.2 / 0.5 /
  1.0 / 1.0 / 1.35 / 1.9.
- **The gameplay layer is always the brightest and least busy layer.** Everything else is
  atmosphere and may be as noisy as it likes.
- **Colour:** each district ships a 3D LUT. Source art is never repainted — the *grade* and
  the *light* do the work, which is what makes free asset packs look bespoke.
- **Light does the storytelling:** Act I is lit by advertising, Act III by the player, Act IV
  by surveillance, Act V by nothing at all.
- **Camera:** damped look-ahead (0.18 s), speed-dependent FOV-ish widening (±6 % zoom),
  letterbox bars only for scripted 6-second beats, hitstop 60–120 ms, shake with a hard cap.
- **Animation:** low frame count, high anticipation. Nine's run cycle is 8 frames; the
  *squash on landing* is 3 frames and matters more than the run.

### 6.3 Signature Effects (the "Replaced look")

1. **Emissive bloom** — neon, screens and lamps flagged in an emissive mask, bloomed in a
   two-pass blur. This single effect is ~60 % of the look.
2. **Wet-ground reflections** — the mid + entity layers rendered flipped under the floor
   line, blurred, UV-distorted by a scrolling noise texture, faded by distance.
3. **Volumetric shafts** — additive cone billboards from signs and windows, with scrolling
   noise, dust motes drifting in them.
4. **Rain at three depths** — behind, in, and in front of the gameplay plane, plus
   ground-splash particles and screen-space droplets during storms.
5. **Fog planes** — a tinted quad between each parallax layer; the cheapest depth in games.
6. **Sign flicker** — per-sign flicker LUT, so the city looks alive without animation frames.
7. **Chromatic aberration + scanline grade** — subtle, disabled by default in an
   accessibility toggle.

---

## 7. Audio

- **Music:** synthwave / industrial ambient. Each district has one 3–4 min track at a fixed
  BPM; the Signal Run level uses the same track with the level authored *onto* its beat grid.
- **Adaptive layers:** 3 stems per track (pad / bass+drums / lead). Stems fade in with the
  player's Flow Chain — playing well literally scores the level.
- **SFX:** wet, close-mic'd. Footsteps change on 6 surface types. Rain is a bed, not an event.
- **Voice:** none, except THE CHORUS, which is a stacked chorus of many voices and is the
  *only* voice in the game — so it lands. Everything else is text.
- **Diegetic story:** street radios, PA announcements and shop TVs deliver ~30 % of the
  worldbuilding while the player runs past.

---

## 8. UX & Controls (Vita)

| Input | Action |
| --- | --- |
| Left stick / D-pad | Move, aim Tether |
| ✕ | Jump (hold = higher), ↓+✕ = Stomp |
| ○ | Arc Dash |
| □ | Melee |
| △ | Pulse |
| R | Tether |
| L | Overclock |
| Rear touch | Alternate Pulse (optional, off by default) |
| Front touch | Menus only |
| SELECT | Level map / Echo tracker |
| START | Pause |

**HUD:** 3 plate pips, one Charge ring, Echo count, Flow Chain number. Fades to 40 % opacity
after 3 s without a resource change. No minimap during play.

**Accessibility:** assist mode (invincibility, 70 % game speed, infinite Charge — no content
locked behind it, no shaming), screen-shake/CA/bloom sliders, colour-blind hazard outlines,
full button remap, hold-to-run toggle.

---

## 9. Scope & Content Budget

| Content | Count |
| --- | --- |
| Levels | 25 (20 standard + 5 boss) |
| Ghost Runs | 10 |
| Bosses | 5 (3 phases each) |
| Enemies | 8 archetypes + 5 boss-unique |
| Tilesets | 5 districts |
| Music tracks | 8 (5 district + title + core + credits) |
| Story screens | ~20 (comic-panel style, skippable) |
| Playtime | 6–8 h campaign, ~12 h to 100 % |

---

## 10. Open Design Questions

1. Does Blackout (Act III) break the flow pillar too hard? — prototype early, cut to a
   single level if it fights the game.
2. Is Tether aiming workable on the Vita stick at speed, or should it auto-target the nearest
   valid anchor in a forward cone? *(Leaning: auto-target with manual override.)*
3. Should Signal Runs fail on missed beats? *(Leaning: no — the beat is a gift, not a test.)*
4. Seven's duel: scripted set-piece or a real AI running the same physics? *(Leaning:
   scripted routes with reactive punishes — real AI at this speed reads as unfair.)*
5. Three endings or two? The hidden ending must not feel like the "real" one the others
   were denied.
