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
    """1-1 THE GUTTER - grey-box. Teaches the verb set in GDD order:
    run -> jump -> one-way -> wall jump -> dash -> stomp -> tether."""
    W, H = 210, 24
    g = Grid(W, H)
    GY = 18                      # ground row

    # --- teach: run. Flat, safe, a Volt trail that rewards holding top speed.
    g.floor(0, 26, GY)
    g.put(4, GY - 2, 'S')
    g.volts(8, GY - 2, 24, GY - 2, 9)
    g.put(14, GY - 4, 'T')       # tutorial hint marker

    # --- teach: jump. Three gaps, widening. First is free, third needs commit.
    g.floor(30, 40, GY)
    g.arc(27, GY - 2, 30, GY - 2, 3, 5)
    g.enemy(34, GY - 1)               # first Scrapper, plenty of runway to see it coming
    g.floor(45, 56, GY)
    g.arc(41, GY - 2, 45, GY - 2, 4, 6)
    g.floor(62, 74, GY)
    g.arc(57, GY - 2, 62, GY - 2, 5, 7)
    g.put(70, GY - 1, 'C')       # checkpoint 1

    # --- teach: one-way platforms, and height as a resource.
    g.plat(78, GY - 4, 5)
    g.plat(86, GY - 7, 5)
    g.plat(94, GY - 10, 5)
    g.floor(78, 100, GY)
    g.arc(75, GY - 2, 80, GY - 5, 3, 5)
    g.volts(88, GY - 9, 96, GY - 12, 5)
    g.put(96, GY - 12, 'E')      # echo 1, on the high line

    # --- teach: wall jump. A shaft with no floor route out.
    g.floor(100, 104, GY)
    g.block(105, GY - 13, 2, 13)      # left wall of the shaft
    g.block(112, GY - 16, 2, 16)      # right wall, taller
    g.floor(107, 111, GY)             # shaft floor (a pit you must climb out of)
    for i in range(5):
        g.put(107 + (i % 2) * 4, GY - 3 - i * 2, 'v')
    g.plat(107, GY - 14, 5)
    g.put(109, GY - 15, 'C')          # checkpoint 2, at the top

    # --- teach: dash. A gap too wide to jump, with a landing you can see.
    g.floor(114, 122, GY - 14)
    g.floor(133, 146, GY - 14)
    g.volts(124, GY - 15, 131, GY - 15, 5)
    g.put(140, GY - 16, 'E')          # echo 2

    # --- teach: stomp. A one-way roof over a pit of Volts; the only way in is down.
    g.floor(150, 168, GY)
    g.plat(152, GY - 6, 14)
    g.put(153, GY - 7, 'C')           # checkpoint 3, on the roof
    g.volts(154, GY - 3, 164, GY - 3, 7)
    g.put(159, GY - 2, 'E')           # echo 3, under the roof
    g.enemy(161, GY - 1)              # a second Scrapper, now under a roof - stomp or dash it
    g.arc(147, GY - 15, 152, GY - 7, 2, 6)

    # --- teach: tether. Anchors over a long gap, no floor at all.
    g.put(174, GY - 12, 'A')
    g.put(183, GY - 13, 'A')
    g.put(192, GY - 11, 'A')
    g.arc(170, GY - 4, 196, GY - 4, 9, 14)

    # --- landing pad: the GDD's end-of-level sprint. Downhill, no hazards.
    g.floor(196, W - 1, GY)
    g.volts(198, GY - 2, 206, GY - 2, 6)
    g.put(206, GY - 3, 'X')

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
