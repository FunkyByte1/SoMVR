import re, sys, math
# EYEH f=.. t=.. root x,y,z eye E lag L filtered|exact fsm F ground G (eye a root b view c above it) head node H view V x dx,dz
pat = re.compile(r'^EYEH f=(\d+) t=([\d.]+) root ([-\d.]+),([-\d.]+),([-\d.]+) eye ([-\d.]+) lag ([-+\d.]+) (\w+) fsm (-?\d+) '
                 r'ground ([-\w.]+) \(eye ([-\w.]+) root ([-\w.]+) view ([-\w.]+) above it\) head node ([-\d.]+) view ([-\d.]+) '
                 r'x ([-+\d.]+),([-+\d.]+)')
rows = []
for ln in open(sys.argv[1], errors='replace'):
    m = pat.match(ln)
    if m:
        g = m.groups()
        rows.append(dict(f=int(g[0]), t=float(g[1]), rx=float(g[2]), ry=float(g[3]), rz=float(g[4]), eye=float(g[5]),
                         lag=float(g[6]), mode=g[7], fsm=int(g[8]), gy=float(g[9]) if g[9] != 'nan' else float('nan'),
                         eg=float(g[10]) if g[10] != 'nan' else float('nan'), rg=float(g[11]) if g[11] != 'nan' else float('nan'),
                         head=float(g[13]), view=float(g[14]), xx=float(g[15]), xz=float(g[16])))
print("EYEH rows", len(rows))
if not rows: sys.exit()
# segments of consecutive frames
segs = []
cur = [rows[0]]
for a, b in zip(rows, rows[1:]):
    if b['f'] == a['f'] + 1 and b['t'] - a['t'] < 0.3: cur.append(b)
    else: segs.append(cur); cur = [b]
segs.append(cur)
def stats(xs):
    xs = [x for x in xs if not math.isnan(x)]
    if not xs: return "n/a"
    m = sum(xs) / len(xs); sd = math.sqrt(sum((x - m) ** 2 for x in xs) / len(xs))
    return "mean %.2f sd %.2f min %.2f max %.2f" % (m, sd, min(xs), max(xs))
for si, sg in enumerate(segs):
    if len(sg) < 10: continue
    T = sg[-1]['t'] - sg[0]['t']
    hd = sum(math.hypot(b['rx'] - a['rx'], b['rz'] - a['rz']) for a, b in zip(sg, sg[1:]))
    print("seg %d: frames %d..%d (%d) %.2f s, horizontal %.1f cm (%.0f cm/s), fsm %s, modes %s" % (
        si, sg[0]['f'], sg[-1]['f'], len(sg), T, hd, hd / T if T else 0, sorted(set(r['fsm'] for r in sg)),
        sorted(set(r['mode'] for r in sg))))
    print("   root-ground", stats([r['rg'] for r in sg]))
    print("   eye-ground ", stats([r['eg'] for r in sg]))
    print("   ground y   ", stats([r['gy'] for r in sg]), " root y", stats([r['ry'] for r in sg]))
    # vertical velocities / accelerations per frame (cm/s, cm/s^2)
    for key in ('ry', 'eye', 'gy'):
        v = []; a = []
        for p, q in zip(sg, sg[1:]):
            dt = q['t'] - p['t']
            if dt > 0 and not math.isnan(q[key]) and not math.isnan(p[key]): v.append((q[key] - p[key]) / dt)
        for p, q in zip(v, v[1:]): a.append(q - p)
        dabs = [abs(q[key] - p[key]) for p, q in zip(sg, sg[1:]) if not math.isnan(q[key]) and not math.isnan(p[key])]
        print("   %-4s per-frame |dy| %s; vy %s; frame-to-frame dvy %s" % (key, stats(dabs), stats(v), stats([abs(x) for x in a])))
    # extrapolation
    print("   extrapolation x", stats([math.hypot(r['xx'], r['xz']) for r in sg]))
    # high-pass: residual of root y from a 0.5 s moving average (bumps) and of eye y
    def resid(key, w=0.25):
        out = []
        for i, r in enumerate(sg):
            win = [s[key] for s in sg if abs(s['t'] - r['t']) <= w]
            out.append(r[key] - sum(win) / len(win))
        return out
    print("   bumps (y minus its 0.5 s mean): root", stats([abs(x) for x in resid('ry')]), "| eye", stats([abs(x) for x in resid('eye')]))
