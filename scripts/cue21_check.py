#!/usr/bin/env python3
"""cue21_check.py LOG - build 21's grab cue never lies (docs/re-xr-pipeline.md 85.1): from somvr.log's `VERBS cue` lines,
every cue that came on came after the game's own answer for that orc was "would grab it", and every "would not grab it"
for the orc shown ended the cue within 0.35 s (the grace 0.15 s, a frame, the line). Prints the counts and each
violation; exit 1 if any."""
import re
import sys

ans_re = re.compile(r'^VERBS cue t=([0-9.]+) orc (0x[0-9a-f]+) in reach .*the game (would grab it|would not grab it)')
st_re = re.compile(r'^VERBS cue t=([0-9.]+) (on|off): orc (0x[0-9a-f]+)')
last = {}
on = None  # (orc, t)
pending_no = None  # (orc, t): a "no" while it was on
bad, n_on, n_off, n_yes, n_no, n_no_on = [], 0, 0, 0, 0, 0
for line in open(sys.argv[1], errors='replace'):
    m = ans_re.match(line)
    if m:
        t, c, yes = float(m.group(1)), m.group(2), m.group(3) == 'would grab it'
        last[c] = (yes, t)
        n_yes += yes
        n_no += not yes
        if on and on[0] == c and not yes and pending_no is None:
            pending_no, n_no_on = (c, t), n_no_on + 1
        continue
    m = st_re.match(line)
    if not m:
        continue
    t, kind, c = float(m.group(1)), m.group(2), m.group(3)
    if kind == 'on':
        n_on += 1
        a = last.get(c)
        if not a or not a[0]:
            bad.append('t=%.3f on for %s without the game\'s yes (last answer %s)' % (t, c, a))
        on, pending_no = (c, t), None
    else:
        n_off += 1
        if pending_no and t - pending_no[1] > 0.35:
            bad.append('t=%.3f off %.2f s after the game\'s no at t=%.3f' % (t, t - pending_no[1], pending_no[1]))
        on, pending_no = None, None
print('cue21_check: answers yes %d no %d; cues on %d off %d; a "no" while on %d; violations %d'
      % (n_yes, n_no, n_on, n_off, n_no_on, len(bad)))
for b in bad:
    print('  VIOLATION', b)
sys.exit(1 if bad else 0)
