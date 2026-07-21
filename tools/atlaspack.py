#!/usr/bin/env python3
"""Pack selected frames from the ansimuz zips in art-src/ into one runtime
atlas (assets/atlas_city.png) plus a generated C header of pixel rects.

All source packs are CC0 (verified in docs/ASSETS.md), so baking their frames
into a shipped atlas is licence-clean.

    python3 tools/atlaspack.py
"""

import sys
import zipfile
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
ART_SRC = ROOT / "art-src"
OUT_PNG = ROOT / "assets" / "atlas_city.png"
OUT_H = ROOT / "src" / "atlas_city.h"

# Every sprite this build needs, as (zip name, path inside zip). Order matters
# only for pack density. Frame strips list every frame explicitly rather than
# guessing a stride, since ansimuz filenames are not always zero-padded.
WARPED_CITY = "warped city files.zip"
WARPED_CITY_2 = "cyberpunk city 2 files.zip"
STREET_PACK = "cyberpunk-street-files.zip"
SYNTH_PACK = "SynthCitiesGodot.zip"
MIAMI_PACK = "Miami-synth-files.zip"
SCIFI_PACK = "Scifi lab Files.zip"
WARPED_GODOT = "Warped City Godot.zip"

SOURCES = {
    # -- Building facade props (windows, doors, monitors) - irregular sizes,
    # not a collision grid despite the source file's name. Kept for future
    # background dressing; ground/wall tiles stay procedural for now (see
    # game.c draw_tiles) rather than force irregular art into a tile grid.
    "tileset": (WARPED_CITY, "warped city files/Assets/ENVIRONMENT/tileset.png"),

    # -- Player, pre-built horizontal strips (71x67 per frame).
    "player_idle": (WARPED_CITY, "warped city files/Assets/SPRITESHEETS/player/idle.png"),
    "player_run":  (WARPED_CITY, "warped city files/Assets/SPRITESHEETS/player/run.png"),
    "player_jump": (WARPED_CITY, "warped city files/Assets/SPRITESHEETS/player/jump.png"),

    # The rest of NINE, which shipped in the Godot build of the same pack and
    # which this project spent a long time believing did not exist. Same
    # character, same artist, same rig - no generator can match that, and the
    # blade felt weak mostly because Nine never moved when it swung.
    "player_strike": (WARPED_GODOT, "Warped City Godot/Player/Assets/shoot.png"),
    "player_hurt":   (WARPED_GODOT, "Warped City Godot/Player/Assets/hurt.png"),
    "player_crouch": (WARPED_GODOT, "Warped City Godot/Player/Assets/crouch.png"),
    "player_climb":  (WARPED_GODOT, "Warped City Godot/Player/Assets/climb.png"),

    # -- Scrapper enemy: the drone reads as a small hostile flyer at a glance.
    "drone": (WARPED_CITY, "warped city files/Assets/SPRITESHEETS/misc/drone_preview.png"),

    # -- Sentry: bolted down, so the answer is always routing rather than
    # fighting - the one enemy you are meant to get past, not through.
    "turret": (WARPED_GODOT, "Warped City Godot/Enemies/Turret/turret-preview.png"),

    # -- The five district bosses. One sprite each, because a boss that looks
    # like the last boss reads as the same fight however different its numbers.
    "boss_warden":  (WARPED_GODOT, "Warped City Godot/Vehicles/v-police.png"),   # D1
    "boss_bouncer": (WARPED_GODOT, "Warped City Godot/Vehicles/v-red.png"),      # D2
    "boss_coil":    (WARPED_GODOT, "Warped City Godot/Vehicles/v-truck.png"),    # D3
    "boss_tide":    (WARPED_GODOT, "Warped City Godot/Vehicles/v-yellow.png"),   # D4

    # -- The big explosion, for things that are worth exploding.
    "boom": (WARPED_GODOT, "Warped City Godot/VFX/enemy-explosion-preview.png"),

    # -- Parallax, back to front. Five sets, one per district: the whole point
    # of shipping five is that the game changes place and time of day, which
    # no amount of re-tinting one skyline achieves.
    #
    # D1 HALCYON NIGHT - the default cyberpunk night.
    "skyline_a": (WARPED_CITY, "warped city files/Assets/ENVIRONMENT/background/skyline-a.png"),
    "skyline_b": (WARPED_CITY, "warped city files/Assets/ENVIRONMENT/background/skyline-b.png"),
    "near_buildings": (WARPED_CITY, "warped city files/Assets/ENVIRONMENT/background/near-buildings-bg.png"),

    # D2 THE STRIP - street level, signage close and dense.
    "d2_back":  (STREET_PACK, "cyberpunk-street-files/Assets/city skyline/Layers/back.png"),
    "d2_mid":   (STREET_PACK, "cyberpunk-street-files/Assets/city skyline/Layers/buildings.png"),
    "d2_near":  (STREET_PACK, "cyberpunk-street-files/Assets/city skyline/Layers/front.png"),

    # D3 THE STACKS - tall, cold, vertical.
    "d3_back":  (SYNTH_PACK, "SynthCitiesGodot/CityLayers/back.png"),
    "d3_mid":   (SYNTH_PACK, "SynthCitiesGodot/CityLayers/middle.png"),

    # D4 THE SHORELINE - dusk. The one district that is not night, which is
    # what makes the others read as night.
    "d4_back":  (MIAMI_PACK, "Miami-synth-files/Layers/back.png"),
    "d4_mid":   (MIAMI_PACK, "Miami-synth-files/Layers/buildings.png"),
    "d4_near":  (MIAMI_PACK, "Miami-synth-files/Layers/palms.png"),

    # D5 MERIDIAN LAB - interior. No sky at all.
    "d5_back":  (SCIFI_PACK, "Scifi lab Files/layers/back.png"),
    "d5_mid":   (SCIFI_PACK, "Scifi lab Files/layers/middle.png"),
    "d5_near":  (SCIFI_PACK, "Scifi lab Files/layers/front.png"),
}

# Frame sequences shipped as loose files: name -> (zip, [paths]). Frames are
# hstacked into a strip at pack time so the runtime only ever sees strips -
# same contract as the pre-built player sheets.

STRIPS = {
    # -- Volt pickup: Warped City's energy bolt. A loose mote of the Hum's
    # carrier signal, which is what a Volt is in fiction.
    "volt": (WARPED_GODOT, ["Warped City Godot/VFX/shot-preview.png"]),

    # -- Pickup burst, reused for any "you got it" flash.
    "collect_fx": (WARPED_GODOT, ["Warped City Godot/VFX/shot-hit-preview.png"]),

    # -- Echo: a face trapped behind glass. Literally what an Echo is - a
    # memory-ghost the Hum filed away - so the monitor face is the sprite.
    "echo": (WARPED_GODOT, [
        f"Warped City Godot/World/props/monitorface/monitor-face-{i}.png"
        for i in (1, 2, 3, 4)]),

    # -- Hack terminal: a real wall-mounted control box.
    "terminal": (WARPED_GODOT, ["Warped City Godot/World/props/control-box-1.png"]),

    # -- Relay node: cyberpunk city 2's LED panel, already cyan and already
    # animated, so the pulse costs nothing.
    "node": (WARPED_CITY_2, [
        f"cyberpunk city 2 files/Environmet/props/lights/lights{i}.png"
        for i in (1, 2, 3, 4)]),

    # -- The Enforcer: cyberpunk city 2's riot cop. The game's first organic
    # enemy - armour over a person, which is what makes the blade's red spray
    # (enemy_fluid) mean something.
    "cop_idle": (WARPED_CITY_2, [
        f"cyberpunk city 2 files/Sprites/cop/idle/cop{i}.png" for i in (1, 2, 3)]),
    "cop_run": (WARPED_CITY_2, [
        f"cyberpunk city 2 files/Sprites/cop/run/cop{i}.png" for i in range(1, 11)]),

    # D5 THE CHORUS: the caretaker intelligence, an egg-turret that opens to fire.
    "boss_chorus": (WARPED_CITY_2, [
        f"cyberpunk city 2 files/Sprites/egg turret/Shoot/shoot{i}.png" for i in (1, 2, 3, 4)]),

}

# 16x16 cells cut out of larger sheets: name -> (zip, path, (x, y, w, h)).
# Warped City 2's tileset is drawn as loose platform blocks, not a grid, but
# these three cells tile cleanly and give the ground real art: the lilac top
# surface, the teal pipe band under it, and the purple wall fill.
CROPS = {
    "tile_top":  (WARPED_CITY_2, "cyberpunk city 2 files/Environmet/tileset.png", (528, 16, 16, 16)),
    "tile_band": (WARPED_CITY_2, "cyberpunk city 2 files/Environmet/tileset.png", (528, 40, 16, 16)),
    "tile_fill": (WARPED_CITY_2, "cyberpunk city 2 files/Environmet/tileset.png", (576, 96, 16, 16)),

    # Street, cut from the flat face of the same block so it tiles: the dark
    # banded course reads as roadway against the lilac building tops, and the
    # plain wall below it as roadbed. Nothing here is drawn by hand.
    "road_top":  (WARPED_CITY_2, "cyberpunk city 2 files/Environmet/tileset.png", (487, 48, 16, 16)),
    "road_fill": (WARPED_CITY_2, "cyberpunk city 2 files/Environmet/tileset.png", (471, 88, 16, 16)),

    # The hack gate, from Warped City's environment sheet: a real roller
    # shutter, slats and all.
    "shutter":   (WARPED_CITY, "warped city files/Assets/ENVIRONMENT/tileset.png", (224, 16, 48, 64)),

    # One-way platforms, cut from Warped City 2's standalone platform block -
    # a lit top slab over its dark underside. Sliced into a repeating middle
    # and an end cap (mirrored for the left end), so a platform of any width
    # is built from real art instead of a stretched sliver.
    "plat_mid":  (WARPED_CITY_2, "cyberpunk city 2 files/Environmet/tileset.png", (200, 26, 16, 14)),
    "plat_end":  (WARPED_CITY_2, "cyberpunk city 2 files/Environmet/tileset.png", (224, 26, 16, 14)),

    # Cover crate: the teal industrial band, deliberately not the lilac ground
    # tile - cover has to read as an object you hide behind, not as raised
    # floor. Two cells: a capped top and a ribbed body.
    "crate_top": (WARPED_CITY_2, "cyberpunk city 2 files/Environmet/tileset.png", (487, 64, 16, 16)),
    "crate_mid": (WARPED_CITY_2, "cyberpunk city 2 files/Environmet/tileset.png", (487, 80, 16, 16)),

    # The drop. A doorway with the light on behind it - the whole job is
    # carrying something to a door somebody is waiting behind, so the level
    # should end at a door and not at an abstract glowing marker.
    "dropdoor":  (WARPED_CITY, "warped city files/Assets/ENVIRONMENT/tileset.png", (48, 16, 48, 64)),
}

PLAYER_FRAME_W = 71
PLAYER_FRAME_H = 67
DRONE_FRAME_W = 55
DRONE_FRAME_H = 52


def load(zip_name, inner_path):
    with zipfile.ZipFile(ART_SRC / zip_name) as z:
        with z.open(inner_path) as f:
            return Image.open(f).convert("RGBA").copy()


class Shelf:
    """Dumb left-to-right, wrap-at-width shelf packer. Good enough for a
    couple dozen images; a real game would reach for a proper bin packer once
    content count justifies it."""

    def __init__(self, width):
        self.width = width
        self.x = 0
        self.y = 0
        self.shelf_h = 0
        self.height = 0

    def place(self, w, h):
        if self.x + w > self.width:
            self.x = 0
            self.y += self.shelf_h
            self.shelf_h = 0
        rect = (self.x, self.y, w, h)
        self.x += w
        self.shelf_h = max(self.shelf_h, h)
        self.height = max(self.height, self.y + self.shelf_h)
        return rect


def main():
    if not ART_SRC.exists():
        print(f"error: {ART_SRC} does not exist")
        return 1

    images = {name: load(*src) for name, src in SOURCES.items()}
    for name, (zip_name, path, (x, y, w, h)) in CROPS.items():
        images[name] = load(zip_name, path).crop((x, y, x + w, y + h))

    for name, (zip_name, paths) in STRIPS.items():
        frames = [load(zip_name, p) for p in paths]
        fw, fh = frames[0].size
        for i, f in enumerate(frames):
            if f.size != (fw, fh):
                print(f"error: {name} frame {i} is {f.size}, expected {(fw, fh)}")
                return 1
        strip = Image.new("RGBA", (fw * len(frames), fh), (0, 0, 0, 0))
        for i, f in enumerate(frames):
            strip.paste(f, (i * fw, 0))
        images[name] = strip

    shelf = Shelf(1024)
    rects = {}
    # Tallest first so the shelf packer doesn't waste rows on short strips.
    for name in sorted(images, key=lambda n: -images[n].height):
        img = images[name]
        rects[name] = shelf.place(img.width, img.height)

    atlas = Image.new("RGBA", (1024, shelf.height), (0, 0, 0, 0))
    for name, (x, y, w, h) in rects.items():
        atlas.paste(images[name], (x, y))

    OUT_PNG.parent.mkdir(parents=True, exist_ok=True)
    atlas.save(OUT_PNG)

    aw, ah = atlas.size
    lines = [
        "/* GENERATED by tools/atlaspack.py - do not edit. */",
        "#ifndef ARC_ATLAS_CITY_H",
        "#define ARC_ATLAS_CITY_H",
        "",
        f"#define ATLAS_CITY_W {aw}",
        f"#define ATLAS_CITY_H {ah}",
        "",
        "/* x, y, w, h in atlas pixels. */",
        "typedef struct { int x, y, w, h; } arc_rect;",
        "",
    ]

    for name, (x, y, w, h) in sorted(rects.items()):
        lines.append(f"#define RECT_{name.upper()} ((arc_rect){{{x}, {y}, {w}, {h}}})")

    lines += [
        "",
        f"#define PLAYER_FRAME_W {PLAYER_FRAME_W}",
        f"#define PLAYER_FRAME_H {PLAYER_FRAME_H}",
        f"#define PLAYER_IDLE_FRAMES {images['player_idle'].width // PLAYER_FRAME_W}",
        f"#define PLAYER_RUN_FRAMES {images['player_run'].width // PLAYER_FRAME_W}",
        f"#define PLAYER_JUMP_FRAMES {images['player_jump'].width // PLAYER_FRAME_W}",
        f"#define DRONE_FRAME_W {DRONE_FRAME_W}",
        f"#define DRONE_FRAME_H {DRONE_FRAME_H}",
        f"#define DRONE_FRAMES {images['drone'].width // DRONE_FRAME_W}",
        f"#define COP_FRAME_W 61",
        f"#define COP_FRAME_H 64",
        f"#define COP_IDLE_FRAMES 3",
        f"#define COP_RUN_FRAMES 10",
        f"#define ECHO_FRAME_W 21",
        f"#define ECHO_FRAME_H 18",
        f"#define ECHO_FRAMES 4",
        f"#define VOLT_FRAME_W 15",
        f"#define VOLT_FRAME_H 11",
        f"#define VOLT_FRAMES 3",
        f"#define CFX_FRAME_W 15",
        f"#define CFX_FRAME_H 11",
        f"#define CFX_FRAMES 4",
        f"#define PLAYER_CLIMB_FRAMES {images['player_climb'].width // PLAYER_FRAME_W}",
        f"#define WARDEN_W 163",
        f"#define WARDEN_H 60",
        f"#define BOUNCER_W 96",
        f"#define BOUNCER_H 61",
        f"#define COIL_W 257",
        f"#define COIL_H 104",
        f"#define TIDE_W 93",
        f"#define TIDE_H 60",
        f"#define CHORUS_FRAME_W 44",
        f"#define CHORUS_FRAME_H 62",
        f"#define CHORUS_FRAMES 4",
        f"#define TURRET_FRAME_W 25",
        f"#define TURRET_FRAME_H 23",
        f"#define TURRET_FRAMES 6",
        f"#define BOOM_FRAME_W 55",
        f"#define BOOM_FRAME_H 52",
        f"#define BOOM_FRAMES 6",
        f"#define NODE_FRAME_W 55",
        f"#define NODE_FRAME_H 68",
        f"#define NODE_FRAMES 4",
        "",
        "#endif",
    ]

    OUT_H.parent.mkdir(parents=True, exist_ok=True)
    OUT_H.write_text("\n".join(lines) + "\n")

    print(f"wrote {OUT_PNG} ({aw}x{ah}) and {OUT_H}")
    for name, r in sorted(rects.items()):
        print(f"  {name:16s} {r}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
