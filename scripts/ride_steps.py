#!/usr/bin/env python3
"""ride_steps.py RUN [-n N] [-j CM] - som-b42-mountview (docs/re-xr-pipeline.md 188): the view's steps on a beast, frame by
frame, from SOMVR_RIDE_TRACE's `RIDE view` lines (somvr_ride_game.c mt_after_base). Each consecutive seated frame's
(not held or blinked: the body moments' black/hold shows no motion)
horizontal view move (the base) is split into what moved it:
  root   the beast's own root (its object position) - the beast's motion, the player's to ask for
  swing  the seat's offset from the root turning with the beast's heading (the eyes sit ~60 cm ahead of the root)
  anchor the root anchor's filter / extrapolation (the base minus the seat)
and its jolt = the change of the view's velocity from the frame before (m/s; a smooth ride is a small one; a step the
player did not make is a big one). The bow's enters/exits and the beast's attacks are marked. Prints the worst N frames
with their causes and the counts over the jolt threshold J (m/s) by cause; the frame time; ok/worst per phase."""
import math, re, sys

V = re.compile(r'^RIDE view f=(\d+) t=([\d.]+) (\S+) beast ([-\d.]+),([-\d.]+),([-\d.]+) hdg ([-\d.]+) head (?:\(none\) )?'
               r'([-\d.]+),([-\d.]+),([-\d.]+) seat ([-\d.]+),([-\d.]+),([-\d.]+) base ([-\d.]+),([-\d.]+),([-\d.]+) '
               r'yaw ([-\d.]+) step')
EV = re.compile(r'^(BOW t=([\d.]+) frame=\d+ (enter|exit)|RIDE t=([\d.]+) (press 186|the ridden caragor: \S+)|'
                r'TRIGGER cmd=ride status t=([\d.]+)|RIDE t=([\d.]+) (seated on|dismounted|mounted|off))')


def main():
    a = sys.argv[1:]
    run = a[0]
    n = int(a[a.index('-n') + 1]) if '-n' in a else 15
    jt = float(a[a.index('-j') + 1]) if '-j' in a else 2.0
    fr, ev = [], []
    with open(run.rstrip('/') + '/somvr.log', errors='replace') as f:
        for l in f:
            if l.startswith('RIDE view f='):
                m = V.match(l)
                if m:
                    g = m.groups()
                    tail = l.rstrip().rsplit(' roll ', 1)[-1]
                    st = g[2] if 'held' not in tail and 'blink' not in tail else g[2] + '-held'  # (held/blinked: not shown)
                    fr.append((int(g[0]), float(g[1]), st, [float(x) for x in g[3:6]], float(g[6]),
                               [float(x) for x in g[10:13]], [float(x) for x in g[13:16]], float(g[16])))
            elif l[:4] in ('BOW ', 'RIDE', 'TRIG'):
                m = EV.match(l)
                if m:
                    t = next(float(x) for x in (m.group(2), m.group(4), m.group(6), m.group(7)) if x)
                    ev.append((t, (m.group(3) and 'bow ' + m.group(3)) or m.group(5) or (m.group(6) and 'stamp') or m.group(8)))
    if len(fr) < 3:
        print('ride_steps: %d RIDE view lines (SOMVR_RIDE_TRACE=N)' % len(fr))
        return
    rows, prev, pv = [], None, None
    dts = []
    for f in fr:
        if prev and f[0] == prev[0] + 1 and f[2] == 'seated' and prev[2] == 'seated':
            dt = f[1] - prev[1]
            if dt <= 0:
                prev = f
                continue
            dts.append(dt)
            d = lambda p, q: (p[0] - q[0], p[2] - q[2])
            db, ds, dp = d(f[3], prev[3]), d(f[5], prev[5]), d(f[6], prev[6])
            o1, o0 = d(f[5], f[3]), d(prev[5], prev[3])
            sw = (o1[0] - o0[0], o1[1] - o0[1])
            an = (dp[0] - ds[0], dp[1] - ds[1])
            v = (dp[0] / dt / 100, dp[1] / dt / 100)
            jolt = math.hypot(v[0] - pv[0], v[1] - pv[1]) if pv else 0.0
            vr = (db[0] / dt / 100, db[1] / dt / 100)
            rows.append(dict(f=f[0], t=f[1], dt=dt, step=math.hypot(*dp), root=math.hypot(*db), swing=math.hypot(*sw),
                             anchor=math.hypot(*an), dh=(f[4] - prev[4] + 180) % 360 - 180,
                             dy=(f[7] - prev[7] + 180) % 360 - 180, v=math.hypot(*v), jolt=jolt, vr=vr,
                             sw=sw, an=an, dt_prev=rows[-1]['dt'] if rows and rows[-1]['f'] == f[0] - 1 else dt))
            pv = v
        else:
            pv = None
        prev = f
    if not rows:
        print('ride_steps: no consecutive seated frames')
        return
    dts.sort()
    med = dts[len(dts) // 2]
    # the root's own jolt: its velocity change frame to frame
    for i, r in enumerate(rows):
        p = rows[i - 1] if i and rows[i - 1]['f'] == r['f'] - 1 else None
        r['rjolt'] = math.hypot(r['vr'][0] - p['vr'][0], r['vr'][1] - p['vr'][1]) if p else 0.0
        r['sjolt'] = math.hypot(r['sw'][0] / r['dt'] - p['sw'][0] / p['dt'], r['sw'][1] / r['dt'] - p['sw'][1] / p['dt']) / 100 if p else 0.0
        r['ajolt'] = math.hypot(r['an'][0] / r['dt'] - p['an'][0] / p['dt'], r['an'][1] / r['dt'] - p['an'][1] / p['dt']) / 100 if p else 0.0

    def cause(r):
        c = []
        if r['dt'] > 1.6 * med or r['dt_prev'] > 1.6 * med:
            c.append('hitch %.0f ms' % (r['dt'] * 1000))
        if r['sjolt'] >= 0.5 * r['jolt'] and r['sjolt'] > 0.5:
            c.append('swing (hdg %+.1f deg)' % r['dh'])
        if r['rjolt'] >= 0.5 * r['jolt'] and r['rjolt'] > 0.5:
            c.append('root')
        if r['ajolt'] >= 0.5 * r['jolt'] and r['ajolt'] > 0.5:
            c.append('anchor')
        return ', '.join(c) or '-'

    def near(t, w=1.5):
        return '; '.join('%s %+.2f s' % (e, et - t) for et, e in ev if abs(et - t) <= w and e != 'stamp')[:150]
    print('ride_steps %s: %d seated frame pairs (%d RIDE view lines), frame time median %.1f ms (%.0f Hz), max %.1f ms' %
          (run, len(rows), len(fr), med * 1000, 1 / med, dts[-1] * 1000))
    print('  the view step: max %.1f cm; its speed max %.1f m/s; the root step max %.1f cm; the swing max %.1f cm; '
          'the anchor max %.1f cm' % (max(r['step'] for r in rows), max(r['v'] for r in rows),
                                      max(r['root'] for r in rows), max(r['swing'] for r in rows),
                                      max(r['anchor'] for r in rows)))
    over = [r for r in rows if r['jolt'] > jt]
    by = {}
    for r in over:
        for c in (cause(r).split(', ') or ['-']):
            k = c.split(' ')[0]
            by[k] = by.get(k, 0) + 1
    print('  jolts over %.1f m/s (the view velocity change in one frame): %d of %d frames; by cause: %s' %
          (jt, len(over), len(rows), ', '.join('%s %d' % kv for kv in sorted(by.items(), key=lambda x: -x[1])) or 'none'))
    print('  swing jolts over %.1f m/s: %d; root jolts over %.1f: %d; anchor jolts over %.1f: %d' %
          (jt, sum(r['sjolt'] > jt for r in rows), jt, sum(r['rjolt'] > jt for r in rows), jt, sum(r['ajolt'] > jt for r in rows)))
    print('  the worst %d frames (jolt m/s = the view velocity change; root/swing/anchor = their own share):' % n)
    for r in sorted(rows, key=lambda r: -r['jolt'])[:n]:
        print('   f=%d t=%.3f dt %.0f ms: step %.1f cm (%.1f m/s), jolt %.1f (root %.1f swing %.1f anchor %.1f); root %.1f cm, '
              'swing %.1f cm, hdg %+.2f view yaw %+.2f deg: %s | %s' %
              (r['f'], r['t'], r['dt'] * 1000, r['step'], r['v'], r['jolt'], r['rjolt'], r['sjolt'], r['ajolt'], r['root'],
               r['swing'], r['dh'], r['dy'], cause(r), near(r['t'])))
    # around each bow enter / exit: the worst jolt within 1 s after it
    bows = [(t, e) for t, e in ev if e.startswith('bow ')]
    if bows:
        print('  the bow (%d enters/exits): the worst jolt within 1 s after each:' % len(bows))
        for t, e in bows:
            w = [r for r in rows if t - 0.05 <= r['t'] <= t + 1.0]
            if w:
                r = max(w, key=lambda r: r['jolt'])
                print('   %s t=%.3f: %d frames, worst jolt %.2f m/s at %+.2f s (step %.1f cm, swing %.1f, hdg %+.2f; %s); '
                      'the view speed %.1f-%.1f m/s' % (e, t, len(w), r['jolt'], r['t'] - t, r['step'], r['swing'], r['dh'],
                                                         cause(r), min(x['v'] for x in w), max(x['v'] for x in w)))
            else:
                print('   %s t=%.3f: no traced seated frames' % (e, t))


if __name__ == '__main__':
    main()
