#!/usr/bin/env python3
"""collect15_land.py - a safe landing near a place for the collect drill's trip (handoff/HANDOFF-collect.md): the
floors under the place and around it by the test word `practice status ground X Y Z` (a ray down from 30 m over to
60 m under, every surface), then the nearest spot 2.5-6 m from the place whose top surface is open sky (the first hit
from 30 m up) and within -2.5..+1.5 m of the place's own floor (the highest surface at or under the place + 1 m): the
trip lands on the ground (Teleport snaps down; the trip's arrival wants him within 3 m under its target), no roof, no
ledge. For a running headless boot with SOMVR_PRACTICE on:
  collect15_land.py RUN X Y Z [--near 2.5] [--far 6]
Prints one line per probe and `LAND x y z` (y = the surface + 40 cm: the `practice go` target) or `NOLAND why`."""
import math, os, re, sys, time
R = sys.argv[1]
X, Y, Z = (float(v) for v in sys.argv[2:5])
args = sys.argv[5:]
def opt(name, dflt):
    return args[args.index(name) + 1] if name in args else dflt
NEAR = float(opt('--near', 2.5)) * 100; FAR = float(opt('--far', 6)) * 100
LOG = os.path.join(R, 'somvr.log')
GRE = re.compile(r'^PRACTICE t=[\d.]+ ground at \((-?\d+) (-?\d+) (-?\d+)\): (\d+) surface')
def loglines():
    with open(LOG, errors='replace') as f:
        return f.readlines()
def probe(x, y, z):
    n0 = len(loglines())
    with open(os.path.join(R, 'somvr.trigger'), 'w') as f:
        f.write('practice status ground %.0f %.0f %.0f' % (x, y, z))
    for _ in range(30):
        time.sleep(0.1)
        for l in loglines()[n0:]:
            m = GRE.match(l)
            if m and abs(float(m.group(1)) - x) < 2 and abs(float(m.group(3)) - z) < 2:
                return [float(v) for v in re.findall(r'(?:: |, )(-?\d+) \([+-]', l)], l.strip()
    return None, 'no ground line'
hs, line = probe(X, Y, Z)
print(line[:300], flush=True)
if hs is None:
    print('NOLAND the probe did not answer'); sys.exit(1)
under = [h for h in hs if h <= Y + 100]
if not under:
    print('NOLAND no surface under the place (%d surfaces: the world there not loaded?)' % len(hs)); sys.exit(1)
floor = max(under)
print('the place (%.0f %.0f %.0f): its floor %.0f (%+.1f m)' % (X, Y, Z, floor, (floor - Y) / 100), flush=True)
best = None
for r in (NEAR, (NEAR + FAR) / 2, FAR):
    for k in range(8):
        a = k * math.pi / 4
        x, z = X + r * math.sin(a), Z + r * math.cos(a)
        hs2, line2 = probe(x, floor + 150, z)
        top = hs2[0] if hs2 else None
        ok = top is not None and -250 <= top - floor <= 150 and hs2[0] == max(hs2)
        # (open sky: the first hit from 30 m up is this floor itself, not a roof or an arch over it)
        print('  %4.1f m %3d deg: %s%s' % (r / 100, k * 45, 'top %.0f (%+.1f m)' % (top, (top - floor) / 100) if top is not None
              else 'no surface', ' OK' if ok else ''), flush=True)
        if ok:
            best = (x, top, z, r, k * 45)
            break
    if best:
        break
if not best:
    print('NOLAND no open spot 2.5-6 m around'); sys.exit(1)
print('LAND %.0f %.0f %.0f  (%.1f m at %d deg from the place, the floor %+.1f m)' % (best[0], best[1] + 40, best[2], best[3] / 100,
      best[4], (best[1] - floor) / 100))
