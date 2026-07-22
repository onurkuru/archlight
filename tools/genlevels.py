#!/usr/bin/env python3
"""Generate src/levels.h from a compact level description.

This is the embryo of the M4 Tiled pipeline: levels are described as ops on a
grid rather than typed out as ASCII, and the generator validates the result
against the authoring rules in docs/GDD.md before it will emit anything.

    python3 tools/genlevels.py
"""

import sys

TILE = 16
CAM_TILES_X = 30          # 480 / 16

EMPTY, SOLID, ONEWAY, STREET = '.', '#', '=', 'G'


class Grid:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.g = [[EMPTY] * w for _ in range(h)]

    def put(self, x, y, ch):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.g[y][x] = ch

    def floor(self, x0, x1, y):
        """Ground from x0 to x1 inclusive, filled down to the bottom."""
        for x in range(x0, x1 + 1):
            for y2 in range(y, self.h):
                self.put(x, y2, SOLID)

    def street(self, x0, x1, y):
        """Asphalt from x0 to x1 inclusive, filled down to the bottom. Same
        collision as floor(); the renderer draws it as road, which is the
        answer to 'what is Nine actually standing on'."""
        for x in range(x0, x1 + 1):
            for y2 in range(y, self.h):
                self.put(x, y2, STREET)

    def block(self, x, y, w, h):
        for j in range(h):
            for i in range(w):
                self.put(x + i, y + j, SOLID)

    def plat(self, x, y, w):
        """One-way platform - can be jumped through from below."""
        for i in range(w):
            self.put(x + i, y, ONEWAY)

    def soft_put(self, x, y, ch):
        """Place only into empty space. Decoration must never eat geometry:
        a Volt written over a platform tile both vanishes from view and puts
        a hole in the platform, which is a bug that looks like a design
        mistake and cost a real debugging pass to find."""
        if 0 <= x < self.w and 0 <= y < self.h and self.g[y][x] == EMPTY:
            self.g[y][x] = ch

    def volts(self, x0, y0, x1, y1, n):
        """A Volt trail. Per GDD: this is the level telling the player where the
        fun is, so trails are always drawn along a line the player can hold
        speed through - never into a corner."""
        for i in range(n):
            t = i / max(n - 1, 1)
            self.soft_put(round(x0 + (x1 - x0) * t), round(y0 + (y1 - y0) * t), 'v')

    def arc(self, x0, y0, x1, y1, peak, n):
        """Volt trail along a jump arc, which is what most trails actually are."""
        for i in range(n):
            t = i / max(n - 1, 1)
            x = x0 + (x1 - x0) * t
            y = y0 + (y1 - y0) * t - peak * 4 * t * (1 - t)
            self.soft_put(round(x), round(y), 'v')

    def _actor(self, x, y, ch):
        """Place an actor without eating geometry.

        Same rule as soft_put, and for the same reason: a cop written onto a
        stair ledge deleted that tile and split the platform in two, which the
        width check then reported as a 1-wide platform. Actors settle under
        gravity anyway, so nudging one up a row is free - and if there is no
        room at all, no actor is better than a hole in a platform."""
        for dy in range(0, 4):
            if 0 <= y - dy < self.h and 0 <= x < self.w and self.g[y - dy][x] == EMPTY:
                self.g[y - dy][x] = ch
                return

    def enemy(self, x, y):
        self._actor(x, y, 'z')

    def cop(self, x, y):
        """Enforcer: ground patrol, turns at ledges. Bleeds red."""
        self._actor(x, y, 'p')

    def crate(self, x, y, h):
        """A cover crate: solid, drawn as an industrial container so it reads
        as an object to hide behind rather than as raised floor."""
        for j in range(h):
            self.put(x, y + j, 'K')

    def door(self, x, y0, y1):
        """Hack-gate shutter: solid until the terminal puzzle opens it."""
        for y in range(y0, y1 + 1):
            self.put(x, y, 'D')

    def rows(self):
        return [''.join(r) for r in self.g]


def stair(g, x, top_row, steps, width=8, overlap=6):
    """A climb built as overlapping one-way ledges, two rows apart.

    Each ledge sits two rows above the last and overlaps it by `overlap`, so
    the whole climb is "jump straight up" N times onto a landing you cannot
    miss. This is the only climb primitive in the game on purpose: it makes
    easy platforming the path of least resistance for whoever builds the next
    level, including me."""
    for i in range(steps):
        g.plat(x + i * (width - overlap), top_row + (steps - 1 - i) * 2, width)


def gate(g, GY, x, style='climb'):
    """The hack gate, in two shapes so it is not the same tower-ladder in
    every level (which it was, and which read as the same climb everywhere):

    - 'climb': the original walled shaft - nodes up a ladder (D1, D3)
    - 'run':   a low hop-line of nodes straight at the shutter - hack on the
               approach, collect at speed, no ladder at all (D2, D4, D5)"""
    if style == 'run':
        g.put(x, GY - 2, 'H')
        g.put(x + 3, GY - 2, 'N')
        g.put(x + 5, GY - 3, 'N')
        g.put(x + 7, GY - 2, 'N')
        g.door(x + 10, GY - 15, GY - 1)
        return
    g.block(x, GY - 10, 2, 8)
    g.block(x + 5, GY - 10, 2, 8)
    g.plat(x, GY - 2, 7)
    g.plat(x + 2, GY - 4, 3)
    g.plat(x + 2, GY - 6, 3)
    g.plat(x + 2, GY - 8, 3)
    g.put(x - 4, GY - 2, 'H')
    g.put(x + 3, GY - 3, 'N')
    g.put(x + 3, GY - 5, 'N')
    g.put(x + 3, GY - 7, 'N')
    g.put(x + 1, GY - 11, 'E')
    g.door(x + 10, GY - 15, GY - 1)


def roof(g, GY, x, height, width):
    """A rooftop with its own stair. `height` is in 2-row steps, so the stair
    always starts at the street and the roof is always exactly reachable -
    the two mistakes that made the first pass of these levels impassable."""
    steps = height
    stair(g, x, GY - 2 * steps, steps)
    rx = x + (steps - 1) * 2 + 8
    g.block(rx, GY - 2 * steps - 2, width, 2 * steps - 4 if steps > 2 else 2)
    return rx, GY - 2 * steps - 3     # roof x, standing row


# ---------------------------------------------------------------- sections
#
# A level is a sequence drawn from this vocabulary, not one fixed layout with
# the enemy count turned up. The first version of this generator emitted the
# same map fifteen times - same two roofs, same gate, same checkpoints - and it
# read exactly like what it was. Each section below returns the width it used,
# so levels differ in shape and in length, not only in roster.

def spread(g, x, n, count, place, margin=5):
    """Place `count` things evenly inside [x+margin, x+n-margin].

    Sections own a fixed width and must stay inside it: a roster that walked
    past its own end used to overwrite the next section's geometry, which
    showed up as 1-tile platform fragments and cost a debugging pass."""
    if count <= 0:
        return
    span = max(n - 2 * margin, 1)
    for i in range(count):
        place(x + margin + (span * i) // max(count, 1))


def sec_run(g, GY, x, d):
    """Open street. Room to hit top speed, a patrol strung along it."""
    n = 26
    spread(g, x, n, min(1 + d // 3, 5), lambda px: g.cop(px, GY - 2))
    if d >= 2:
        g.enemy(x + 14, GY - 5)
    if d >= 7:
        g.enemy(x + 20, GY - 4)
    g.volts(x + 4, GY - 2, x + n - 4, GY - 2, 7)
    return n


def sec_roof(g, GY, x, d):
    """Climb to a rooftop and run it. The high line, with the Echo on it."""
    h = 2 + (d % 3)                       # 2..4 steps: roofs differ in height
    steps = h
    stair(g, x, GY - 2 * steps, steps)
    rx = x + (steps - 1) * 2 + 8
    rw = 12 + (d % 4) * 3
    ry = GY - 2 * steps - 2
    g.block(rx, ry, rw, 2)
    g.put(rx + rw // 2, ry - 1, 'E')
    spread(g, rx, rw, min(1 + d // 3, 4), lambda px: g.enemy(px, ry - 4), margin=3)
    if d >= 3:
        g.cop(x + 6, GY - 2)              # someone waiting under the stair
    g.volts(x + 2, GY - 3, rx + 2, ry - 1, 6)
    return (rx - x) + rw + 6


def sec_shaft(g, GY, x, d):
    """A wall-jump shaft between two service towers. Vertical breathing room
    in a game that otherwise runs flat."""
    h = 8 + (d % 3) * 2
    g.block(x + 2, GY - h, 2, h - 2)
    g.block(x + 7, GY - h, 2, h - 2)
    g.plat(x + 2, GY - 2, 7)
    for i in range(1, (h - 2) // 2):
        g.plat(x + 4, GY - 2 * i - 2, 3)
        g.put(x + 5, GY - 2 * i - 3, 'v')
    g.put(x + 5, GY - h - 1, 'E' if d % 2 else 'v')
    if d >= 4:
        g.enemy(x + 12, GY - 5)
    return 18


def sec_tether(g, GY, x, d):
    """Anchors over a plaza. High line swings it, ground line fights through."""
    n = 30
    g.put(x + 6, GY - 11, 'A')
    g.put(x + 15, GY - 12, 'A')
    g.put(x + 24, GY - 10, 'A')
    g.arc(x + 2, GY - 6, x + n - 2, GY - 6, 6, 12)
    spread(g, x, n, min(2 + d // 3, 6), lambda px: g.cop(px, GY - 2))
    if d >= 9:
        g.put(x + n // 2, GY - 2, 'u')
    return n


def sec_sentry(g, GY, x, d):
    """A covered lane. Sentries hold it; the cover is how you cross it."""
    n = 24
    g.put(x + 6, GY - 2, 'u')
    g.put(x + 17, GY - 2, 'u')
    g.block(x + 11, GY - 2, 3, 2)          # waist-high cover, one step up
    if d >= 5:
        g.enemy(x + 12, GY - 6)
    g.volts(x + 3, GY - 2, x + n - 3, GY - 2, 6)
    return n


def sec_awning(g, GY, x, d):
    """A one-way awning over a street group: stomp through it, or run under."""
    n = 22
    g.plat(x + 4, GY - 5, 12)
    spread(g, x, n, min(2 + d // 2, 6), lambda px: g.cop(px, GY - 2), margin=4)
    g.enemy(x + 10, GY - 8)
    if d >= 9:
        g.enemy(x + 15, GY - 9)
    g.volts(x + 4, GY - 6, x + 15, GY - 6, 5)
    return n


def sec_gate(g, GY, x, d):
    district = d // 3 + 1                  # d is the campaign step (0,3,6,9,12)
    gate(g, GY, x + 4, 'climb' if district in (1, 3) else 'run')
    return 26


def sec_underpass(g, GY, x, d):
    """A road that runs under a slab. Low ceiling, no high line, everything
    happens at street level - the opposite read from a rooftop section."""
    n = 28
    g.block(x + 2, GY - 7, n - 4, 2)
    spread(g, x, n, min(2 + d // 4, 4), lambda px: g.cop(px, GY - 2), margin=4)
    g.enemy(x + n // 2, GY - 5)
    g.volts(x + 4, GY - 4, x + n - 4, GY - 4, 7)
    return n


def sec_movers(g, GY, x, d):
    """A gap crossed on moving platforms - the section that is about timing
    instead of aim. A wide pit with a static ledge on the far side, and two or
    three platforms shuttling across it. Falling short lands on the street, so
    it is never a death, only a lost tempo."""
    n = 34
    # a shallow pit: the street is still under it, but the fast line is the
    # platforms. `m` walks horizontally, `n_` (vertical) lifts you between rows.
    # Movers shuttle over the section; the street runs clean underneath, so
    # they are always the fast line and never a wall. The Echo sits on a low
    # platform the movers reach - reachable on foot too, just slower.
    g.put(x + 8, GY - 3, 'm')
    g.put(x + 18, GY - 4, 'm')
    g.put(x + 26, GY - 3, 'm')
    # A two-step stair to the Echo: overlapping ledges, jump straight up.
    stair(g, x + 18, GY - 4, 2)
    g.put(x + 20, GY - 5, 'E')
    if d >= 3:
        g.enemy(x + 14, GY - 5)
    g.volts(x + 6, GY - 4, x + 28, GY - 4, 7)
    return n


def sec_cover(g, GY, x, d):
    """A firefight across a lane. Waist-high crates on both sides, Enforcers
    posted behind theirs, a no-man's-land between. Shots stop on the crates
    (solid geometry already blocks fire), so advancing means moving crate to
    crate in the gaps between their volleys - the one section that is about
    trading fire rather than running past it.

    Enemies sit ON their cover so they clear it to shoot and you can hit them
    on the beat; you take the low line and pop up. Cover is 3 tiles tall so it
    stops a chest-height round."""
    n = 32
    # player-side crates to advance behind
    # Crates are 2 tiles tall: high enough to stop a chest-height round, low
    # enough to vault (a 2-tile hop is inside the jump budget), so cover is
    # something you move over and around, never a wall that seals the lane.
    for cx in (x + 4, x + 12, x + 22, x + 27):
        for i in range(3):
            g.crate(cx + i, GY - 2, 2)
    g.cop(x + 20, GY - 2)
    g.cop(x + 30, GY - 2)
    if d >= 2:
        g.put(x + 25, GY - 2, 'u')      # a sentry pinning the middle
    if d >= 5:
        g.enemy(x + 24, GY - 7)         # and a drone over the top
    if d >= 8:
        g.cop(x + 16, GY - 2); g.cop(x + 18, GY - 2)  # a heavier line
    g.volts(x + 5, GY - 4, x + 18, GY - 4, 6)
    return n


def sec_beams(g, GY, x, d):
    """A corridor of containment beams. The hazard section: run the gaps in
    the cycle, or Pulse a beam to walk through it. Emitters hang from a low
    ceiling so the beams reach the floor."""
    n = 30
    # Emitters hang on short stubs rather than a continuous ceiling: a ceiling
    # spanning the corridor read to the reachability fill as a lid and sealed
    # the level. Each beam still reaches the floor; the gaps between them are
    # open sky.
    beams = 2 + d // 4
    for i in range(beams):
        bx = x + 6 + i * 7
        g.block(bx - 1, GY - 9, 3, 1)         # a small housing to hang from
        g.put(bx, GY - 9, 'L')
    spread(g, x, n, min(1 + d // 5, 3), lambda px: g.cop(px, GY - 2), margin=5)
    g.volts(x + 4, GY - 3, x + n - 4, GY - 3, 8)
    return n


def sec_steps(g, GY, x, d):
    """A rising terrace: three short roofs climbing away from the street, each
    one a landing. Height without a shaft."""
    n = 34
    for i in range(3):
        g.block(x + 4 + i * 9, GY - 2 - i * 2, 8, 2 + i * 2)
        g.plat(x + i * 9, GY - 2 - i * 2, 5)
    g.put(x + 26, GY - 7, 'E')
    spread(g, x, n, min(1 + d // 5, 3), lambda px: g.enemy(px, GY - 9), margin=6)
    if d >= 4:
        g.cop(x + 6, GY - 2)
    g.volts(x + 2, GY - 3, x + 26, GY - 7, 7)
    return n


SECTIONS = [sec_run, sec_roof, sec_shaft, sec_tether, sec_sentry, sec_awning,
            sec_underpass, sec_steps, sec_movers, sec_beams, sec_cover]

# Each district leans on two of them, so a place has a shape as well as a sky:
# the Stacks are vertical, the Lab is corridors, the Shoreline is open road.
# Each district draws ONLY from its own pool, so its level is built from its
# own moves - no shared rotation quietly putting the same stair-climb in all
# five. Verticality itself changes flavour: D1 climbs rooftops, D2 rides
# movers and swings, D3 wall-jumps shafts, D4 is a flat coastal road with no
# climb at all, D5 is low corridors.
DISTRICT_POOLS = {
    1: [sec_roof, sec_cover, sec_awning, sec_run],       # streets & rooftops
    2: [sec_tether, sec_movers, sec_awning, sec_sentry], # swing & shuttle
    3: [sec_shaft, sec_movers, sec_steps, sec_underpass],# vertical: shafts, hoists
    4: [sec_beams, sec_tether, sec_run, sec_sentry],     # flat dusk highway
    5: [sec_cover, sec_beams, sec_underpass, sec_sentry],# lab corridors
}


def build_district(d, n, title, drones=0, cops=0, has_gate=True):
    """Compose a level out of sections. `step` picks a different rotation of
    the vocabulary per level, so no two share a layout."""
    step = (d - 1) * 3 + (n - 1)
    diff = step

    # An opening straight, then the district's own pool, gate slotted in.
    order = [sec_run] + DISTRICT_POOLS[d]
    if has_gate:
        order.insert(2 + (d % 2), sec_gate)

    # Lay them out end to end, dropping a checkpoint every second section.
    g = Grid(400, 26)                      # oversized; trimmed once we know W
    GY = 21
    g.street(0, 399, GY)
    x = 14
    last_cp = 3
    for sec in order:
        # Checkpoints go by distance, not by section count: sections differ in
        # width now, so "every other one" drifts past the 70-tile rule.
        if x - last_cp > 30:
            g.put(x - 2, GY - 1, 'C')
            last_cp = x - 2
        x += sec(g, GY, x, diff) + 4

    # Landing pad: flat, fast, and the drop at the end of it. It gets its own
    # checkpoint so the last stretch is never the longest one.
    g.put(x, GY - 1, 'C')
    g.volts(x + 2, GY - 2, x + 16, GY - 2, 8)
    x += 22
    W = x + 6

    trimmed = Grid(W, 26)
    for y in range(26):
        trimmed.g[y] = g.g[y][:W]
    trimmed.put(3, GY - 2, 'S')
    if d == 1 and n == 1:
        trimmed.put(11, GY - 3, 'T')
    trimmed.volts(5, GY - 2, 12, GY - 2, 4)

    # Every level carries three Echoes. The sections place them where they make
    # sense; if a level's mix happened not to, top it up on the reachable
    # rooftops rather than shipping a level with nothing to find in it.
    want = 3
    have = sum(r.count('E') for r in trimmed.rows())
    if have < want:
        # Only somewhere the player can actually stand: an Echo tucked into
        # unreachable geometry is worse than no Echo, because it reads as a
        # collectible you failed to work out how to get.
        spots = sorted(p for p in reachable(trimmed, doors_open=True)
                       if 12 < p[0] < W - 14 and p[1] < GY - 1
                       and trimmed.g[p[1]][p[0]] == EMPTY)
        if spots:
            stride = max(len(spots) // (want - have + 1), 1)
            for k in range(want - have):
                sx, sy = spots[min((k + 1) * stride, len(spots) - 1)]
                trimmed.put(sx, sy, 'E')

    # The drop is the end of the level: a full-height wall stands right behind
    # it so you cannot run off into the void past the door - you deliver here.
    trimmed.put(W - 4, GY - 1, 'X')
    trimmed.block(W - 2, 0, 2, GY)
    return f'{d}-{n}', title, trimmed


def build_boss(d, title):
    """A district's boss: one locked screen, nothing else.

    The arena is exactly one camera window (15 tiles) walled on both sides -
    no run, no checkpoints, no exit door. You and the rig in a box; the level
    completes itself when the rig goes down (game.c auto-finish). Two low
    ledges and the walls give the height the openings demand (the stomp line,
    getting behind COIL), and that is all the geometry a duel needs."""
    W = 15                        # == VIEW_W: the camera cannot scroll
    g = Grid(W, 26)
    GY = 21
    g.street(0, W - 1, GY)
    g.block(0, 0, 1, GY)          # full-height walls: nobody leaves the box,
    g.block(W - 1, 0, 1, GY)      # and they double as wall-jump surfaces
    g.plat(1, GY - 2, 4)          # side ledges, one jump up: the stomp line
    g.plat(W - 5, GY - 2, 4)
    g.put(2, GY - 3, 'S')         # spawn ON the left ledge (placed after it,
                                  # or the plat overwrites the marker)
    g.put(7, GY - 5, 'W')         # the rig, centre stage
    return f'{d}-2', title, g


# Short and boss-gated: one flow level per district, then the boss you fight to
# unlock the next place. Ten levels, five environments, five distinct bosses -
# tight enough to finish in a sitting, every level pulling its weight.
DISTRICTS = [
    (1, "HALCYON NIGHT", ["THE GUTTER"],   "RAIL WARDEN"),
    (2, "THE STRIP",     ["NEON MILE"],    "THE BOUNCER"),
    (3, "THE STACKS",    ["THE HOIST"],    "MAMA COIL"),
    (4, "THE SHORELINE", ["THE CAUSEWAY"], "TIDE BREAKER"),
    (5, "MERIDIAN LAB",  ["DEEP INDEX"],   "THE CHORUS"),
]


def build_campaign():
    """Fifteen levels, five districts, difficulty rising monotonically in
    enemy count - and only in enemy count."""
    levels = []
    for di, (d, _dname, titles, boss) in enumerate(DISTRICTS):
        for n, title in enumerate(titles, start=1):
            levels.append(build_district(d, n, title, has_gate=True))
        levels.append(build_boss(d, boss))
    return levels


def build_1_2():
    """1-2 NIGHT SHIFT. Same easy geometry as 1-1, more of the city awake.

    The difficulty step from 1-1 is entirely enemy count and composition: two
    Enforcers where 1-1 had one, drones layered above them, and the first
    group that has to be fought rather than run past. Nothing here asks for a
    harder jump than 1-1 did."""
    W, H = 200, 26
    g = Grid(W, H)
    GY = 21
    g.street(0, W - 1, GY)

    g.put(3, GY - 2, 'S')
    g.volts(6, GY - 2, 20, GY - 2, 8)

    # First patrol: a pair, spaced so you meet them one at a time.
    g.cop(26, GY - 2)
    g.enemy(30, GY - 4)
    g.cop(34, GY - 2)
    g.put(40, GY - 1, 'C')

    # Rooftop line over a crossfire: two Enforcers below, drones above.
    stair(g, 46, GY - 6, 3)      # tops out standing GY-7 at x=50..57
    g.block(58, GY - 8, 14, 6)   # roof starts where the stair ends
    g.put(64, GY - 9, 'E')
    g.enemy(61, GY - 11)
    g.enemy(68, GY - 11)
    g.cop(60, GY - 2)
    g.cop(66, GY - 2)
    g.volts(48, GY - 3, 57, GY - 7, 5)
    g.put(76, GY - 1, 'C')

    # The gauntlet: four on the street, one drone, no roof to skip it.
    g.cop(92, GY - 2)
    g.cop(98, GY - 2)
    g.enemy(102, GY - 4)
    g.cop(106, GY - 2)
    g.volts(90, GY - 2, 110, GY - 2, 10)
    g.put(114, GY - 1, 'C')

    # Hack gate, same shape as 1-1 so the verb reads as learned, not new.
    g.block(122, GY - 10, 2, 8)
    g.block(127, GY - 10, 2, 8)
    g.plat(122, GY - 2, 7)
    g.plat(124, GY - 4, 3)
    g.plat(124, GY - 6, 3)
    g.plat(124, GY - 8, 3)
    g.put(119, GY - 2, 'H')
    g.put(125, GY - 3, 'N')
    g.put(125, GY - 5, 'N')
    g.put(125, GY - 7, 'N')
    g.put(123, GY - 11, 'E')
    g.door(132, GY - 15, GY - 1)

    g.cop(140, GY - 2)
    g.enemy(145, GY - 4)
    g.cop(150, GY - 2)
    g.put(158, GY - 1, 'C')

    g.block(166, GY - 2, 5, 2)
    g.put(168, GY - 3, 'E')
    g.volts(174, GY - 2, 190, GY - 2, 9)
    g.enemy(180, GY - 3)
    g.put(196, GY - 3, 'X')
    return '1-2', 'NIGHT SHIFT', g


def build_1_3():
    """1-3 CROSSFIRE. The Enforcer level.

    Every group here has a shooter in it, and the geometry is built to teach
    the counters: a wall to break line of fire, a roof to drop from, a shaft
    where their rounds cannot follow. Still no jump harder than 1-1's."""
    W, H = 200, 26
    g = Grid(W, H)
    GY = 21
    g.street(0, W - 1, GY)

    g.put(3, GY - 2, 'S')
    g.volts(6, GY - 2, 16, GY - 2, 6)

    # Opening: a shooter with cover between you and it - the lesson is that
    # geometry stops rounds.
    g.cop(24, GY - 2)
    g.block(18, GY - 2, 3, 2)    # waist-high cover, one step up
    g.cop(32, GY - 2)
    g.put(38, GY - 1, 'C')

    # Firing line under a roof you can drop from.
    stair(g, 44, GY - 4, 2)      # tops out standing GY-5 at x=46..53
    g.block(54, GY - 6, 16, 4)   # roof begins at the stair's edge
    g.cop(58, GY - 2)
    g.cop(64, GY - 2)
    g.enemy(61, GY - 8)
    g.put(62, GY - 7, 'E')
    g.volts(46, GY - 3, 53, GY - 5, 4)

    # Crossfire: shooters facing each other, drone between. Charge through or
    # take the roof.
    g.cop(88, GY - 2)
    g.cop(96, GY - 2)
    g.enemy(92, GY - 5)
    # Four steps, because this roof is 8 rows up: a stair has to start at the
    # street, not halfway to the target.
    stair(g, 74, GY - 8, 4)
    g.block(86, GY - 8, 14, 4)
    g.put(92, GY - 9, 'E')
    g.put(104, GY - 1, 'C')

    # Gate, with a shooter posted on the approach.
    g.block(122, GY - 10, 2, 8)
    g.block(127, GY - 10, 2, 8)
    g.plat(122, GY - 2, 7)
    g.plat(124, GY - 4, 3)
    g.plat(124, GY - 6, 3)
    g.plat(124, GY - 8, 3)
    g.put(116, GY - 2, 'H')
    g.cop(112, GY - 2)
    g.put(125, GY - 3, 'N')
    g.put(125, GY - 5, 'N')
    g.put(125, GY - 7, 'N')
    g.put(123, GY - 11, 'E')
    g.door(132, GY - 15, GY - 1)

    # Final run: the densest group in the district.
    g.cop(142, GY - 2)
    g.enemy(146, GY - 4)
    g.cop(150, GY - 2)
    g.cop(158, GY - 2)
    g.enemy(162, GY - 4)
    g.put(168, GY - 1, 'C')
    g.volts(140, GY - 2, 166, GY - 2, 12)

    g.cop(178, GY - 2)
    g.enemy(184, GY - 4)
    g.volts(174, GY - 2, 190, GY - 2, 8)
    g.put(196, GY - 3, 'X')
    return '1-3', 'CROSSFIRE', g


def build_1_1():
    """1-1 THE GUTTER, rebuilt on the ground.

    The premise of the redesign: the street never stops. One continuous run of
    asphalt under the whole level, buildings standing ON it as climbable
    masses. There is no bottomless pit anywhere - falling off a roof costs you
    the fast line, never the run (the pillar: never break the run). Two lines
    everywhere: fast = rooftops, safe = street. Enemies come in groups on
    surfaces, because a lone drone floating over a void was neither a fight
    nor an obstacle."""
    W, H = 220, 26
    g = Grid(W, H)
    GY = 21                      # street surface row

    # --- the street: unbroken, end to end.
    g.street(0, W - 1, GY)

    # --- teach: run. Safe straight, volts immediately.
    g.put(3, GY - 2, 'S')
    g.volts(6, GY - 2, 18, GY - 2, 7)
    g.put(11, GY - 3, 'T')       # tutorial hint marker

    # --- teach: jump. A step onto the first low roof and back down.
    #     Every climb in this level is a chain of <=2-tile steps, because a
    #     plain jump tops out at 41.9 px (2.6 tiles). The reachability check
    #     at the bottom of this file enforces it - the first draft of this
    #     level had a 3-tile first step and sealed off everything past x=57.
    g.plat(19, GY - 2, 5)        # step: standing row GY-3, 2 up from street
    g.block(24, GY - 4, 8, 4)    # building A, roof standing row GY-5
    g.arc(18, GY - 2, 25, GY - 5, 2, 5)
    g.volts(26, GY - 5, 30, GY - 5, 3)

    # --- first group: a drone overhead, an Enforcer walking a beat below it.
    g.enemy(37, GY - 2)
    g.cop(41, GY - 2)
    g.volts(34, GY - 2, 44, GY - 2, 6)
    g.put(46, GY - 1, 'C')       # checkpoint 1

    # --- fire escape up to building B: three ledges, two tiles apart each.
    #
    #     Buildings stop two rows short of the street so the road runs clean
    #     underneath them. That is what actually keeps the promise that falling
    #     off a roof costs the fast line and not the run - a building grounded
    #     into the street is a wall, and a wall means backtracking to the last
    #     ladder, which is the worst thing this game can ask of a player.
    #     The ledges are 8 wide and each overlaps the one below by 6, so the
    #     climb is "jump straight up" four times with a huge landing area.
    #     Platforming is not where this game's difficulty lives - the enemies
    #     are. A 3-wide ledge with a 2-tile horizontal gap, which is what this
    #     was, is a precision test nobody asked for.
    g.plat(50, GY - 2, 8)        # standing GY-3
    g.plat(52, GY - 4, 8)        # standing GY-5
    g.plat(54, GY - 6, 8)        # standing GY-7
    g.block(62, GY - 8, 10, 6)   # building B, roof standing GY-9
    g.arc(51, GY - 3, 63, GY - 9, 3, 7)
    g.put(66, GY - 9, 'E')       # echo 1, on the roof
    g.enemy(68, GY - 11)

    # --- roofline: B -> C -> D. Gaps stay inside the 4-tile jump budget, and
    #     falling short lands on the street below, never in a pit.
    g.block(75, GY - 8, 9, 6)    # building C, same height, 4-tile gap
    g.enemy(73, GY - 11)
    g.arc(72, GY - 10, 76, GY - 10, 3, 5)
    g.block(89, GY - 6, 9, 4)    # building D, lower - a drop, not a climb
    g.arc(84, GY - 10, 90, GY - 8, 3, 5)
    g.put(93, GY - 7, 'C')       # checkpoint 2, on D's roof

    # --- stomp drop: off D's edge, through an awning, into a mixed patrol -
    #     two Enforcers walking the street under a drone. The layered group is
    #     what makes the verbs compose: stomp the drone, blade the cops.
    g.plat(100, GY - 5, 6)
    g.cop(103, GY - 2)
    g.enemy(107, GY - 3)
    g.cop(111, GY - 2)
    g.volts(100, GY - 2, 114, GY - 2, 8)

    # --- the hack gate, and the shaft that is the puzzle.
    #
    #     Two service towers with a 3-wide shaft between them. The fast line is
    #     a wall-jump chain straight up; the safe line is the three ledges,
    #     spaced two tiles so a plain jump clears them. Both exist because
    #     requiring a wall-jump to progress on level one would gate the game on
    #     the hardest verb it has just taught.
    g.block(118, GY - 10, 2, 8)  # towers clear the street like the buildings
    g.block(123, GY - 10, 2, 8)
    g.plat(118, GY - 2, 7)       # entry step, wide - the towers start above it
    g.plat(120, GY - 4, 3)       # standing GY-5
    g.plat(120, GY - 6, 3)       # standing GY-7
    g.plat(120, GY - 8, 3)       # standing GY-9
    for i in range(4):
        g.put(120 + (i % 2) * 2, GY - 3 - i * 2, 'v')

    #     Hack on the approach, collect on the climb, leave through the gate -
    #     no doubling back. The shutter is the one thing here that does reach
    #     the street, and it runs higher than the towers, so it can be neither
    #     walked under nor jumped over: the only way through is the hack.
    g.put(116, GY - 2, 'H')      # terminal, on the approach
    g.put(121, GY - 3, 'N')      # relay nodes, one per ledge
    g.put(121, GY - 5, 'N')
    g.put(121, GY - 7, 'N')
    g.put(119, GY - 11, 'E')     # echo 2, on top of the left tower
    g.door(128, GY - 15, GY - 1)

    # --- teach: tether. Anchors strung over a plaza; below them a street
    #     patrol. High line swings over, ground line fights through.
    g.put(132, GY - 11, 'A')
    g.put(140, GY - 12, 'A')
    g.put(148, GY - 10, 'A')
    g.arc(129, GY - 6, 152, GY - 6, 6, 12)
    g.cop(136, GY - 2)
    g.enemy(141, GY - 2)
    g.cop(146, GY - 2)
    g.put(155, GY - 1, 'C')      # checkpoint 3

    # --- a kiosk with the last echo on its roof.
    g.block(162, GY - 2, 4, 2)   # roof standing GY-3, one step from the street
    g.arc(158, GY - 2, 163, GY - 3, 2, 4)
    g.put(163, GY - 3, 'E')      # echo 3

    # --- landing pad sprint: flat asphalt, volts, two drones to dash through.
    g.volts(170, GY - 2, 205, GY - 2, 12)
    g.enemy(185, GY - 2)
    g.enemy(196, GY - 2)
    g.put(214, GY - 3, 'X')

    return '1-1', 'THE GUTTER', g


# Movement envelope, derived from the tuning block at the top of src/game.c.
# Kept conservative on purpose: the checker should only ever complain about a
# gap the player genuinely cannot clear, never about one they can only clear
# with a perfect input. A false "unreachable" that sends you rebuilding a good
# section is worse than a missed one.
#   apex   = JUMP_V^2 / (2*GRAV)      = 330^2 / 2600      = 41.9 px = 2.6 tiles
#   air    = JUMP_V/GRAV + sqrt(2*apex/GRAV_FALL)         = 0.48 s
#   reach  = RUN_MAX * air            = 180 * 0.48        = 86 px  = 5.4 tiles
JUMP_UP_TILES = 2          # 2.6 rounded down
JUMP_ACROSS = 4            # 5.4 rounded well down
FALL_DRIFT = 6             # falling gives more airtime, so more horizontal room

# Difficulty in ARCLIGHT comes from enemies, never from precision platforming.
# These two rules keep that honest: a landing has to be wide enough to hit
# without aiming, and a climb has to be a step, not a leap.
MIN_PLAT_W = 4
MAX_STEP_UP = 2


def standable(g, x, y, doors_open):
    """A tile the player can stand in: feet supported, head clear."""
    blocking = (SOLID, STREET, 'K') if doors_open else (SOLID, STREET, 'K', 'D')
    support = (SOLID, STREET, ONEWAY, 'K') if doors_open else (SOLID, STREET, ONEWAY, 'K', 'D')
    if not (0 <= x < g.w and 0 <= y < g.h - 1):
        return False
    if g.g[y][x] in blocking:
        return False                        # inside geometry
    if y - 1 >= 0 and g.g[y - 1][x] in blocking:
        return False                        # no headroom
    return g.g[y + 1][x] in support


def reachable(g, doors_open):
    """Flood fill the standing positions the player can actually get to.

    Run twice per level: with the hack gate shut (which is how the player
    meets the terminal and the relay nodes) and with it open (which is how
    they reach everything past it). One pass would either mark the whole back
    half unreachable or quietly excuse a gate that can never be opened."""
    starts = [(x, y) for y in range(g.h) for x in range(g.w)
              if g.g[y][x] == 'S']
    if not starts:
        return set()
    sx, sy = starts[0]
    # The spawn marker sits in the air; drop it onto whatever is below.
    while sy + 1 < g.h and not standable(g, sx, sy, doors_open):
        sy += 1
    seen = {(sx, sy)}
    stack = [(sx, sy)]

    blocking = (SOLID, STREET, 'K') if doors_open else (SOLID, STREET, 'K', 'D')

    def clear(x, y):
        return 0 <= x < g.w and 0 <= y < g.h and g.g[y][x] not in blocking

    while stack:
        x, y = stack.pop()
        cands = []

        # Walk / step up or down one, along the ground.
        for dx in (-1, 1):
            for dy in (-1, 0, 1):
                cands.append((x + dx, y + dy))

        # Jump: anywhere inside the envelope, provided the column is open.
        for dx in range(-JUMP_ACROSS, JUMP_ACROSS + 1):
            for dy in range(-JUMP_UP_TILES, 1):
                cands.append((x + dx, y + dy))

        # Fall: straight down or drifting, to any landing below.
        for dx in range(-FALL_DRIFT, FALL_DRIFT + 1):
            for dy in range(1, g.h):
                cands.append((x + dx, y + dy))

        for nx, ny in cands:
            if (nx, ny) in seen or not standable(g, nx, ny, doors_open):
                continue
            # The destination and the tile above it must be open, so we do not
            # teleport through a floor slab.
            if not clear(nx, ny):
                continue
            seen.add((nx, ny))
            stack.append((nx, ny))

    return seen


def validate(name, g):
    """The validator is a design tool, not just a build step (TECH_PLAN §5)."""
    rows = g.rows()
    problems = []

    flat = ''.join(rows)
    if flat.count('S') != 1:
        problems.append(f"{name}: needs exactly one spawn, found {flat.count('S')}")
    # A boss arena carries no exit: the level completes when the boss dies.
    if flat.count('X') != 1 and flat.count('W') == 0:
        problems.append(f"{name}: needs exactly one exit, found {flat.count('X')}")

    echoes = flat.count('E')
    if echoes != 3:
        # The shipping target is 6 per level; the grey-box carries 3 on purpose.
        print(f"  note: {name} has {echoes} echoes (shipping levels want 6)")

    # 20-second rule: at ~180 px/s top speed a checkpoint every ~20 s means one
    # roughly every 225 tiles. Being far stricter here because grey-box deaths
    # are frequent and the whole pillar is that death is cheap.
    cps = sorted(x for y in range(g.h) for x in range(g.w) if g.g[y][x] == 'C')
    spawn = next(x for y in range(g.h) for x in range(g.w) if g.g[y][x] == 'S')
    exit_xs = [x for y in range(g.h) for x in range(g.w) if g.g[y][x] == 'X']
    marks = [spawn] + cps + exit_xs
    for a, b in zip(marks, marks[1:]):
        if b - a > 70:
            problems.append(f"{name}: {b - a} tiles between checkpoints at x={a} "
                            f"and x={b} (max 70)")

    # Bottomless columns. Designed gaps are bottomless too, so this is a note
    # rather than an error - but an unintended one (a wall built with block()
    # leaving empty rows under it) shows up here as a run of 1-2 columns in
    # the middle of otherwise solid ground, which is easy to spot.
    holes = [x for x in range(g.w)
             if not any(g.g[y][x] in (SOLID, STREET) for y in range(g.h))]
    runs = []
    for x in holes:
        if runs and runs[-1][1] == x - 1:
            runs[-1][1] = x
        else:
            runs.append([x, x])
    if runs:
        print(f"  note: {name} bottomless columns: "
              + ', '.join(f"{a}-{b}" if a != b else str(a) for a, b in runs))

    # Reachability, in the two states the level actually has. Shut, the player
    # must be able to reach the terminal and every relay node; open, they must
    # be able to reach everything else.
    shut = reachable(g, doors_open=False)
    open_ = reachable(g, doors_open=True)

    def near(reach, mx, my, radius=2):
        return any((mx + dx, my + dy) in reach
                   for dx in range(-radius, radius + 1)
                   for dy in range(-radius, radius + 1))

    # Movers and beams are decoration to the reachability fill: a mover is a
    # bonus line the player can always skip by staying on the street, and a
    # beam is passable (it cycles off). The fill already treats them as empty
    # because they are not SOLID, so no special case is needed here.
    for ch, label, reach in (('H', 'terminal', shut), ('N', 'relay node', shut),
                             ('X', 'exit', open_), ('C', 'checkpoint', open_),
                             ('E', 'echo', open_)):
        for y in range(g.h):
            for x in range(g.w):
                if g.g[y][x] == ch and not near(reach, x, y):
                    problems.append(f"{name}: {label} at ({x},{y}) is unreachable")

    # Every one-way platform must be wide enough to land on without aiming.
    for y in range(g.h):
        x = 0
        while x < g.w:
            if g.g[y][x] == ONEWAY:
                n = 0
                while x + n < g.w and g.g[y][x + n] == ONEWAY:
                    n += 1
                # A ledge that spans a shaft wall-to-wall cannot be missed,
                # however narrow it is, so it is exempt from the width rule.
                walled = (x > 0 and g.g[y][x - 1] in (SOLID, STREET) and
                          x + n < g.w and g.g[y][x + n] in (SOLID, STREET))
                if n < MIN_PLAT_W and not walled:
                    problems.append(f"{name}: {n}-wide platform at ({x},{y}) "
                                    f"- minimum is {MIN_PLAT_W}, difficulty "
                                    f"belongs to the enemies")
                x += n
            else:
                x += 1

    # Any stretch of the level the player simply cannot get to reads to a
    # player as "the section is impassable", so report the gaps in coverage.
    reach_x = {x for x, _ in open_}
    missing = [x for x in range(g.w) if x not in reach_x]
    gaps = []
    for x in missing:
        if gaps and gaps[-1][1] == x - 1:
            gaps[-1][1] = x
        else:
            gaps.append([x, x])
    gaps = [(a, b) for a, b in gaps if b - a >= 2]
    if gaps:
        problems.append(f"{name}: unreachable columns: "
                        + ', '.join(f"{a}-{b}" for a, b in gaps))

    return problems


def emit(levels, path):
    out = ['/* GENERATED by tools/genlevels.py - do not edit. */',
           '#ifndef ARC_LEVELS_H', '#define ARC_LEVELS_H', '']
    out.append('typedef struct {')
    out.append('    const char *id, *title;')
    out.append('    int w, h;')
    out.append('    const char *const *rows;')
    out.append('    int district;          /* selects the parallax set and grade */')
    out.append('} arc_level_def;')
    out.append('')

    for lid, title, g in levels:
        sym = lid.replace('-', '_')
        out.append(f'static const char *const LVL_{sym}_ROWS[] = {{')
        for r in g.rows():
            out.append(f'    "{r}",')
        out.append('};')
        out.append('')

    out.append('static const arc_level_def ARC_LEVELS[] = {')
    for lid, title, g in levels:
        sym = lid.replace('-', '_')
        district = int(lid.split('-')[0]) - 1
        out.append(f'    {{ "{lid}", "{title}", {g.w}, {g.h}, LVL_{sym}_ROWS, {district} }},')
    out.append('};')
    out.append('#define ARC_LEVEL_COUNT (int)(sizeof(ARC_LEVELS) / sizeof(ARC_LEVELS[0]))')
    out.append('')
    out.append('#endif')

    with open(path, 'w') as f:
        f.write('\n'.join(out) + '\n')


def main():
    levels = build_campaign()

    problems = []
    for lid, title, g in levels:
        problems += validate(lid, g)
    if problems:
        print('level validation failed:')
        for p in problems:
            print('  ' + p)
        return 1

    emit(levels, 'src/levels.h')
    for lid, title, g in levels:
        print(f'  {lid} "{title}" {g.w}x{g.h} tiles '
              f'({g.w * TILE}x{g.h * TILE} px, {g.w / CAM_TILES_X:.1f} screens)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
