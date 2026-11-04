#!/usr/bin/env python3
"""deaths_check.py - build 36 (som-b36-deaths, docs/re-xr-pipeline.md 164): every orc death in a run's somvr.log against
the blow that dealt it. Read-only.

  deaths_check.py RUN/somvr.log [--since T] [--until T]

Per killing blow (SOMVR_KILLS' `KILLS start` / its death at the blow / a test kill): the row, how long from the blow to
the death (`KILLS kill`, the game clock's seconds and the log's), why, and - for a half that played past 0.15 s - what its
trace showed at +0.15 s (the pose moved, the head) - i.e. that it was visibly dying from the start. Then every orc death
in the log (SOMVR_KILLS, the stealth kill, the ground finisher, an ordinary hit's) with the latest blow on that orc before
it: a death with no blow in the 1 s before is listed as BY ITSELF. Exit 1 when a death came more than 0.15 s after its blow
without its half seen moving by then, or a death has no blow in the 1 s before (with --strict)."""
import re
import sys

T_RE = re.compile(r'\bt=([0-9]+\.[0-9]+)')
ORC_RE = re.compile(r'orc[= ](0x[0-9a-f]+)')


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    path, since, until, strict = argv[1], -1e18, 1e18, '--strict' in argv
    if '--since' in argv:
        since = float(argv[argv.index('--since') + 1])
    if '--until' in argv:
        until = float(argv[argv.index('--until') + 1])
    clock = 0.0
    starts = {}          # orc -> (t, row, who, state)
    blows = {}           # orc -> [t...]
    kills = []           # (t_blow, t_death, el, row, who, why, orc, trace015)
    deaths = []          # (t, orc, how)
    trace = {}           # orc -> {mark: (moved, head, head0)}
    bad = 0
    for line in open(path, errors='replace'):
        m = T_RE.search(line)
        if m:
            clock = float(m.group(1))
        if not (since <= clock <= until):
            continue
        o = ORC_RE.search(line)
        orc = o.group(1) if o else None
        if line.startswith('KILLS start t='):
            row = re.search(r"-> '([^']+)'", line)
            who = re.search(r't=[0-9.]+ (.+?) on orc', line)
            st = re.search(r"state '([^']*)', attack '([^']*)'", line)
            starts[orc] = (clock, row.group(1) if row else '?', who.group(1) if who else '?',
                           st.group(2) if st else '?')
            blows.setdefault(orc, []).append(clock)
            trace[orc] = {}
        elif line.startswith('COMBAT hit ') and orc:
            blows.setdefault(orc, []).append(clock)
        elif line.startswith('KILLS trace t=') and orc:
            mk = re.search(r"\+([0-9.]+) s \(its own", line)
            mv = re.search(r'the pose moved ([0-9.]+) cm, the head ([-+0-9a-z]+) cm over its ground \(([-+0-9a-z]+) at the blow',
                           line)
            if mk and mv:
                trace.setdefault(orc, {})[round(float(mk.group(1)), 2)] = (float(mv.group(1)), mv.group(2), mv.group(3))
        elif line.startswith('KILLS kill t=') and orc:
            el = re.search(r"\): '([^']*)' ([0-9.]+) s in \((.*?)\) -> ", line)
            dead = re.search(r'dead ([01])->([01])', line)
            who = re.search(r'\(([^)]*)\): ', line)
            if not el:
                continue
            t_blow, row, w, state = starts.pop(orc, (clock, el.group(1), who.group(1) if who else '?', '-'))
            tr = trace.get(orc, {})
            k015 = min(tr, key=lambda k: abs(k - 0.15)) if tr else None
            kills.append((t_blow, clock, float(el.group(2)), row, w, el.group(3), orc, state,
                          tr[k015] if k015 is not None and abs(k015 - 0.15) < 0.03 else None))
            if dead and dead.group(1) == '0' and dead.group(2) == '1':
                deaths.append((clock, orc, 'SOMVR_KILLS (%s)' % w))
            if el.group(1).startswith('(') and 'at the blow' in el.group(1):
                blows.setdefault(orc, []).append(clock)
            trace.pop(orc, None)
        elif line.startswith('STEALTH kill t=') and 'dead=0->1' in line:
            blows.setdefault(orc, []).append(clock)
            deaths.append((clock, orc, 'the stealth kill'))
        elif line.startswith('ABIL t=') and 'the ground finisher' in line and ', dead' in line:
            blows.setdefault(orc, []).append(clock)
            deaths.append((clock, orc, "SOMVR_ABIL's ground finisher"))
        elif line.startswith('COMBAT hit ') and 'dead=0->1' in line:
            deaths.append((clock, orc, 'an ordinary hit'))
    print('%-34s %-18s %-26s %8s %8s  %s' % ('the row (who)', 'its attack', 'orc', 'blow->dead', 'its own', 'why / at +0.15 s'))
    for (tb, td, el, row, who, why, orc, state, tr) in kills:
        late = el > 0.155 and not (tr and tr[0] >= 3.0)
        bad += late
        r = row.replace('AI_Bip_PC_', '')
        seen = (' | +0.15: moved %.1f cm, head %s (at the blow %s)' % tr) if tr else ''
        print('%-34s %-18s %-26s %7.2fs %7.2fs  %s%s%s' % ((r + ' (' + who + ')')[:34], state[:18], orc, td - tb, el,
                                                          why[:90], seen, '   <-- LATE, not seen dying' if late else ''))
    print()
    alone = 0
    for (td, orc, how) in deaths:
        prev = [b for b in blows.get(orc, []) if b <= td + 1e-3]
        last = max(prev) if prev else None
        ok = last is not None and td - last <= 1.0
        alone += not ok
        print('death t=%.3f orc %s by %s: the last blow %s%s' % (td, orc, how, '%.2f s before' % (td - last) if last is not None
                                                                  else 'none', '' if ok else '   <-- BY ITSELF'))
    n_inst = sum(1 for k in kills if k[2] <= 0.005)
    n_fast = sum(1 for k in kills if k[2] <= 0.155)
    print('\n%d killing blows: %d dead at the blow, %d within 0.15 s, %d played on seen dying, %d late; %d deaths, %d with no '
          'blow in the 1 s before' % (len(kills), n_inst, n_fast, len(kills) - n_fast - bad, bad, len(deaths), alone))
    return 1 if bad or (strict and alone) else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
