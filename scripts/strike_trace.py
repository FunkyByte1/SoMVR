#!/usr/bin/env python3
"""strike_trace.py RUN/somvr.log [T0 [T1]] - the wraith dash's per-frame trace (SOMVR_BOW_STRIKE=dash|blink, docs/re-bow.md 14):
per STRIKE f= line within 1.2 s from game time T0: the frame interval, the eye's step, the view's step, the eye's speed,
the profile's share and speed, the tunnel, the blink and the base yaw (the dash must not turn it); with SOMVR_HP_AWAY's
fields (round 2) also what the eyes hid of Talion (the body, the weapons hidden / kept in our hands) and the arms' states.
T1 (optional, default T0 + 1.2) ends the window."""
import re
import sys
pat = re.compile(r"^STRIKE f=(\d+) t=([\d.]+) (\w+) root (\S+) target (\S+) view (\S+) eye (\S+) step ([\d.]+) \(view ([\d.]+)\) cm yaw (\S+) \(\S+\) base yaw (\S+) f ([\d.]+) speed ([\d.]+) tunnel ([\d.]+) blink ([\d.]+)")
hpa = re.compile(r"talion (\w+) away (\d) body (\d) pose (\d) nodes (\d+) meshes (\d+) weapons hid (\d+) kept (\d+) \(mesh skips "
                 r"(\d+)\); arms L (\d) (\S+) R (\d) (\S+) cm; bm (\d)")
rows = []
for l in open(sys.argv[1], errors="replace"):
    m = pat.match(l)
    if m:
        a = hpa.search(l)
        rows.append(m.groups() + ((a.groups(),) if a else (None,)))
t0 = float(sys.argv[2]) if len(sys.argv) > 2 else 0
t1 = float(sys.argv[3]) if len(sys.argv) > 3 else t0 + 1.2
last = None
for r in rows:
    f, t, ph = int(r[0]), float(r[1]), r[2]
    if t < t0 or t > t1:
        last = r; continue
    dt = t - float(last[1]) if last else 0
    es, vs = float(r[7]), float(r[8])
    print(f"f={f} {ph:5s} dt {dt*1e3:5.1f} ms eye step {es:6.1f} cm view step {vs:6.1f} cm -> {es/100/dt if dt else 0:5.1f} m/s"
          f"  f {r[11]} speed {r[12]} tunnel {r[13]} blink {r[14]} base yaw {r[10]}" +
          (f" | talion {a[0]} away {a[1]} nodes {a[4]} meshes {a[5]} weapons hid {a[6]} kept {a[7]} mesh skips {a[8]}"
           f" arms L {a[9]} {a[10]} R {a[11]} {a[12]} cm bm {a[13]}" if (a := r[15]) else ""))
    last = r
