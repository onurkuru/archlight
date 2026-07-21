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

EMPTY, SOLID, ONEWAY = '.', '#', '='


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

    def block(self, x, y, w, h):
        for j in range(h):
            for i in range(w):
                self.put(x + i, y + j, SOLID)

    def plat(self, x, y, w):
        """One-way platform - can be jumped through from below."""
        for i in range(w):
            self.put(x + i, y, ONEWAY)

    def volts(self, x0, y0, x1, y1, n):
        """A Volt trail. Per GDD: this is the level telling the player where the
        fun is, so trails are always drawn along a line the player can hold
        speed through - never into a corner."""
        for i in range(n):
            t = i / max(n - 1, 1)
            self.put(round(x0 + (x1 - x0) * t), round(y0 + (y1 - y0) * t), 'v')

    def arc(self, x0, y0, x1, y1, peak, n):
        """Volt trail along a jump arc, which is what most trails actually are."""
        for i in range(n):
            t = i / max(n - 1, 1)
            x = x0 + (x1 - x0) * t
            y = y0 + (y1 - y0) * t - peak * 4 * t * (1 - t)
            self.put(round(x), round(y), 'v')

    def enemy(self, x, y):
        self.put(x, y, 'z')

    def rows(self):
        return [''.join(r) for r in self.g]


def build_1_1():
    """1-1 THE GUTTER. Teaches the verb set in GDD order, tuned for the 2x
    camera (visible window is 15x8.5 tiles): shorter sight-lines, a drone
    every couple of screens, volts always marking the line."""
    W, H = 200, 24
    g = Grid(W, H)
    GY = 18                      # ground row

    # --- teach: run. Short, safe, volts immediately.
    g.floor(0, 20, GY)
    g.put(3, GY - 2, 'S')
    g.volts(6, GY - 2, 18, GY - 2, 7)
    g.put(11, GY - 3, 'T')       # tutorial hint marker

    # --- teach: jump + first drone at head height, stompable from a hop.
    g.floor(24, 33, GY)
    g.arc(21, GY - 2, 24, GY - 2, 3, 5)
    g.enemy(28, GY - 2)
    g.floor(37, 46, GY)
    g.arc(34, GY - 2, 37, GY - 2, 4, 6)
    g.enemy(42, GY - 2)
    g.put(45, GY - 1, 'C')       # checkpoint 1

    # --- teach: one-way ladder up, drone guarding the echo line.
    g.floor(50, 72, GY)
    g.arc(47, GY - 2, 51, GY - 2, 4, 5)
    g.plat(53, GY - 4, 4)
    g.plat(59, GY - 7, 4)
    g.plat(65, GY - 10, 4)
    g.enemy(62, GY - 8)
    g.volts(55, GY - 6, 66, GY - 12, 6)
    g.put(67, GY - 12, 'E')      # echo 1, top of the ladder

    # --- teach: wall jump. Compact shaft; volts zigzag shows the bounces.
    g.floor(73, 76, GY)
    # Walls use floor(), not block(): block() would leave the rows beneath it
    # empty, i.e. a bottomless pit right where the player runs into the shaft.
    g.floor(77, 78, GY - 12)
    g.floor(82, 83, GY - 13)      # top flush with the shaft exit, not above it
    g.floor(79, 81, GY)
    # 3 tiles wide: each wall jump climbs ~2 tiles, so a narrower shaft keeps
    # the bounces close enough together to read as a rhythm.
    for i in range(5):
        g.put(79 + (i % 2) * 2, GY - 3 - i * 2, 'v')
    g.plat(79, GY - 13, 3)
    g.put(80, GY - 14, 'C')      # checkpoint 2, top of the shaft

    # --- teach: dash across the roofline, drones patrolling the gaps.
    g.floor(84, 92, GY - 13)
    g.floor(99, 108, GY - 13)
    g.volts(93, GY - 14, 98, GY - 14, 4)
    g.enemy(95, GY - 15)
    g.floor(113, 124, GY - 13)
    g.volts(109, GY - 14, 112, GY - 14, 3)
    g.enemy(118, GY - 15)
    g.put(121, GY - 16, 'E')     # echo 2 on the roofline
    g.put(122, GY - 14, 'C')     # checkpoint 3

    # --- teach: stomp. Drop off the roof through one-ways into a volt pit.
    g.floor(128, 148, GY)
    g.plat(130, GY - 6, 12)
    g.volts(132, GY - 3, 142, GY - 3, 6)
    g.put(137, GY - 2, 'E')      # echo 3, under the roof
    g.enemy(139, GY - 5)
    g.enemy(133, GY - 2)

    # --- teach: tether. Anchors over the void; a drone hovers on the line.
    g.put(153, GY - 11, 'A')
    g.put(161, GY - 12, 'A')
    g.put(169, GY - 10, 'A')
    g.arc(149, GY - 4, 174, GY - 4, 8, 12)
    g.enemy(163, GY - 6)

    # --- landing pad sprint: flat, fast, lined with volts, one last drone
    #     placed to be dashed through at full speed.
    g.floor(174, W - 1, GY)
    g.put(175, GY - 1, 'C')      # checkpoint 4, start of the pad
    g.volts(177, GY - 2, 194, GY - 2, 10)
    g.enemy(186, GY - 2)
    g.put(196, GY - 3, 'X')

    return '1-1', 'THE GUTTER', g


def validate(name, g):
    """The validator is a design tool, not just a build step (TECH_PLAN §5)."""
    rows = g.rows()
    problems = []

    flat = ''.join(rows)
    if flat.count('S') != 1:
        problems.append(f"{name}: needs exactly one spawn, found {flat.count('S')}")
    if flat.count('X') != 1:
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
    exit_x = next(x for y in range(g.h) for x in range(g.w) if g.g[y][x] == 'X')
    marks = [spawn] + cps + [exit_x]
    for a, b in zip(marks, marks[1:]):
        if b - a > 70:
            problems.append(f"{name}: {b - a} tiles between checkpoints at x={a} "
                            f"and x={b} (max 70)")

    # Bottomless columns. Designed gaps are bottomless too, so this is a note
    # rather than an error - but an unintended one (a wall built with block()
    # leaving empty rows under it) shows up here as a run of 1-2 columns in
    # the middle of otherwise solid ground, which is easy to spot.
    holes = [x for x in range(g.w)
             if not any(g.g[y][x] == SOLID for y in range(g.h))]
    runs = []
    for x in holes:
        if runs and runs[-1][1] == x - 1:
            runs[-1][1] = x
        else:
            runs.append([x, x])
    if runs:
        print(f"  note: {name} bottomless columns: "
              + ', '.join(f"{a}-{b}" if a != b else str(a) for a, b in runs))

    return problems


def emit(levels, path):
    out = ['/* GENERATED by tools/genlevels.py - do not edit. */',
           '#ifndef ARC_LEVELS_H', '#define ARC_LEVELS_H', '']
    out.append('typedef struct {')
    out.append('    const char *id, *title;')
    out.append('    int w, h;')
    out.append('    const char *const *rows;')
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
        out.append(f'    {{ "{lid}", "{title}", {g.w}, {g.h}, LVL_{sym}_ROWS }},')
    out.append('};')
    out.append('#define ARC_LEVEL_COUNT (int)(sizeof(ARC_LEVELS) / sizeof(ARC_LEVELS[0]))')
    out.append('')
    out.append('#endif')

    with open(path, 'w') as f:
        f.write('\n'.join(out) + '\n')


def main():
    levels = [build_1_1()]

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
