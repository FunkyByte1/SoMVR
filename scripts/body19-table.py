#!/usr/bin/env python3
"""body19-table.py RUN_DIR... - build 19b's orc bodies (docs/re-xr-pipeline.md 74): per hold mode (the `CONFIG fight body
CM` lines; the env's at the start, then the words) and per move, from the `FIGHT body near` lines (moves whose head,
neck, chest or shoulders came within 70 cm of the eyes): how many, how many under 50 cm, the nearest, the nearest part."""
import re
import sys
from collections import defaultdict

CFG = re.compile(r"^CONFIG fight body (\d+) cm")
NEAR = re.compile(r"^FIGHT body near .*? move '([^']+)'( \(a sync\))?: its ([a-z ]+?) (\d+) cm from the eyes")

for run in sys.argv[1:]:
    mode, rows = None, defaultdict(lambda: defaultdict(list))
    with open(run.rstrip("/") + "/somvr.log", errors="replace") as f:
        for line in f:
            m = CFG.match(line)
            if m:
                mode = int(m.group(1))
                continue
            m = NEAR.match(line)
            if m and mode is not None:
                rows[mode][m.group(1)].append((int(m.group(4)), m.group(3)))
    print("==", run)
    for md in sorted(rows):
        allv = [v for mv in rows[md].values() for v, _ in mv]
        print(f"  hold {md or 'off'}: {len(allv)} moves under 70 cm, {sum(v < 50 for v in allv)} under 50, "
              f"nearest {min(allv) if allv else '-'} cm")
        for mv, vs in sorted(rows[md].items(), key=lambda kv: min(v for v, _ in kv[1])):
            v0, p0 = min(vs)
            print(f"    {mv:36s} {len(vs):3d} under 70, {sum(v < 50 for v, _ in vs):3d} under 50, nearest {v0:3d} cm ({p0})")
