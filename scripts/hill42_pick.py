#!/usr/bin/env python3
"""hill42_pick.py - som-b42-edgestop (build 42): open 10-25 deg downhill lines from a `climb heights STEP N` survey.

Reads the CLIMB heights lines of one survey (a file of them) and prints the best lines, one a line:
    HILLSPOT x y z heading H deg slope MEAN (MIN..MAX) len L cm
y is the root's height to arrive at (the floor + the root's 102 cm + 10). A line: 5 samples one grid step apart along one
of 8 headings, each step falling 11-24 deg, the steps' spread under 8 deg, and the floor one step to either side of
every sample there and within 30 deg of it (an open slope, not a ridge, a gully or a ramp's edge).
"""
import math
import re
import sys

HEAD = re.compile(r'grid (\d+) x \d+, (\d+) cm apart, round the root ([-0-9.]+),([-0-9.]+),([-0-9.]+) .*over the feet ([-0-9.]+)\)')
ROW = re.compile(r'row (\d+) z ([-0-9.]+) x ([-0-9.]+)\.\.: (.*)$')


def main(path, top=3):
    n = st = None
    rows = {}
    feet = 0.0
    for line in open(path, errors='replace'):
        m = HEAD.search(line)
        if m:
            n, st = int(m.group(1)), int(m.group(2))
            feet = float(m.group(6))
            rows = {}
            continue
        m = ROW.search(line)
        if m and n:
            vals = [None if v == '-' else float(v) for v in m.group(4).split()]
            rows[int(m.group(1))] = (float(m.group(2)), float(m.group(3)), vals)
    if not n or len(rows) < n:
        print('HILLSPOT none (no complete survey in %s)' % path)
        return

    def h(i, j):
        if 0 <= j < n and 0 <= i < n and j in rows and i < len(rows[j][2]):
            return rows[j][2][i]
        return None

    cands = []
    for j in range(n):
        for i in range(n):
            for di, dj in ((1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (1, -1), (-1, 1), (-1, -1)):
                step = st * math.hypot(di, dj)
                pts = [(i + k * di, j + k * dj) for k in range(5)]
                hs = [h(a, b) for a, b in pts]
                if any(v is None for v in hs):
                    continue
                angs = [math.degrees(math.atan2(hs[k] - hs[k + 1], step)) for k in range(4)]
                if min(angs) < 11 or max(angs) > 24 or max(angs) - min(angs) > 8:
                    continue
                ok = True
                for a, b in pts:  # the floor one step to either side: there, and no steeper than 30 deg from it
                    for sa, sb in ((-dj, di), (dj, -di)):
                        v = h(a + sa, b + sb)
                        if v is None or abs(v - h(a, b)) > step * math.tan(math.radians(30)) + 5:
                            ok = False
                if not ok:
                    continue
                mean = sum(angs) / 4
                x = rows[j][1] + i * st
                z = rows[j][0]
                hd = math.degrees(math.atan2(di, dj))
                cands.append((abs(mean - 17), x, feet + hs[0] + 102 + 10, z, hd, mean, min(angs), max(angs), 4 * step))
    cands.sort()
    picked = []
    for c in cands:  # spots at least 3 m apart
        if all(math.hypot(c[1] - p[1], c[3] - p[3]) >= 300 for p in picked):
            picked.append(c)
        if len(picked) >= top:
            break
    if not picked:
        print('HILLSPOT none (%d lines judged in %s)' % (len(cands), path))
    for c in picked:
        print('HILLSPOT %.0f %.0f %.0f heading %.0f deg slope %.1f (%.1f..%.1f) len %.0f cm' % c[1:])


if __name__ == '__main__':
    main(sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 3)
