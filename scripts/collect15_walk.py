#!/usr/bin/env python3
"""collect15_walk.py - walk up to a place as a VR player would (the collect drill, handoff/HANDOFF-collect.md): the
left stick forward (no sprint), the test head turned toward the place, Talion's position from `practice status` each
cycle; slower in the last 3 m; stops within --stop metres, at a new prompt (a `PROMPTS show` line, not the fight's or
the wraith's, or `USE ... pick prompt up` since the start: --prompt), at a window (BODYMOMENT `window 0 -> 1`), at the time, or the user in VR (quit).
For a running headless boot (RUN = its logs/boot-check-<ts>/, SOMVR_XR_TEST=RUN/xrtest.txt, SOMVR_PRACTICE on):
  collect15_walk.py RUN X Z [--stop 1.2] [--secs 30] [--prompt] [--yaw0 DEG] [--look Y]
--look Y: there (no prompt yet), the head pitched toward the place's height Y (cm; the eyes 165 cm over Talion's root:
`head ... pitch`), held 2 s, then turned 20 and 40 deg each way at that pitch, until a prompt shows; the head stays.
The head's yaw for a world heading is learnt from a first short step (the test head's yaw + = atan2(dx, dz) +, as
story2_route.py). One line per cycle and a summary line `WALK end: ...` (the last position, the distance, why)."""
import math, os, re, subprocess, sys, time
R = sys.argv[1]
GX, GZ = float(sys.argv[2]), float(sys.argv[3])
args = sys.argv[4:]
def opt(name, dflt):
    return args[args.index(name) + 1] if name in args else dflt
LOOK = float(opt('--look', 'nan')); STOP = float(opt('--stop', 1.2)) * 100; SECS = float(opt('--secs', 30)); PROMPT = '--prompt' in args
LOG = os.path.join(R, 'somvr.log')
seq = [0]
def user_in():
    a = subprocess.run(['systemctl', '--user', 'is-active', 'somvr-headset'], capture_output=True, text=True).stdout.strip()
    try:
        st = open('/run/user/%d/vr-session/status.json' % os.getuid()).read()
    except OSError:
        st = ''
    return a == 'active' or '"user": true' in st
def xt(*lines):
    seq[0] += 1
    tmp = os.path.join(R, 'xrtest.tmp')
    with open(tmp, 'w') as f:
        f.write('# walk %d\n' % seq[0] + ''.join(l + '\n' for l in lines))
    os.rename(tmp, os.path.join(R, 'xrtest.txt'))
def word(w):
    with open(os.path.join(R, 'somvr.trigger'), 'w') as f:
        f.write(w)
def loglines():
    with open(LOG, errors='replace') as f:
        return f.readlines()
PRE = re.compile(r'^PRACTICE t=([\d.]+) status: .*?Talion \S+ at \((-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\)')
def status():
    n0 = len(loglines())
    word('practice status')
    for _ in range(25):
        time.sleep(0.1)
        ls = loglines()
        for l in reversed(ls[n0:]):
            m = PRE.match(l)
            if m:
                return tuple(float(m.group(i)) for i in (2, 3, 4))
    return None
# the fight's and the wraith's prompts (they come and go everywhere): not a place's
IGN = re.compile(r"'(UI_Wraith_|UI_Ranged_|Domination_|Combat_Grab|UI_Combat_|UI_Attract|Combat_Struggle|UI_Prompt_Interrogate)")
def wrap(a):
    return (a + math.pi) % (2 * math.pi) - math.pi
def news(n0):
    ls = loglines()[n0:]
    p = [l for l in ls if (l.startswith('PROMPTS show') and not IGN.search(l)) or re.match(r'^USE t=.* pick prompt up', l)]
    w = [l for l in ls if re.match(r'^BODYMOMENT t=.*window 0 -> 1', l)]
    return p, w
n0 = len(loglines())
yaw = float(opt('--yaw0', 0)); t0 = time.time(); end = 'time'
xt('head 0 0 0 yaw %.1f s 0.2' % yaw); time.sleep(0.4)
p0 = status()
xt('stick L 0 1', 'head 0 0 0 yaw %.1f s 0.2' % yaw); time.sleep(0.9); xt('stick L 0 0', 'head 0 0 0 yaw %.1f s 0.2' % yaw)
time.sleep(0.4)
p1 = status()
base = None
if p0 and p1 and math.hypot(p1[0] - p0[0], p1[2] - p0[2]) > 30:
    base = wrap(math.atan2(p1[0] - p0[0], p1[2] - p0[2]) - math.radians(yaw))
print('%s walk to (%.0f %.0f): from %s, the first step %s, yaw 0 heads %s' % (time.strftime('%T'), GX, GZ,
      '(%.0f %.0f %.0f)' % p0 if p0 else '-', '%.0f cm' % math.hypot(p1[0] - p0[0], p1[2] - p0[2]) if p0 and p1 else '-',
      '%.0f deg' % math.degrees(base) if base is not None else 'unknown'), flush=True)
pos = p1; best = None; stuck = 0; side = 1
while time.time() - t0 < SECS:
    if user_in():
        xt('stick L 0 0')
        with open(os.path.join(R, 'cmd'), 'a') as f:
            f.write('quit\n')
        print('STOP: the user is in VR', flush=True); sys.exit(1)
    pr, wn = news(n0)
    if PROMPT and pr:
        end = 'a prompt: ' + pr[0].strip()[:160]; break
    if wn:
        end = 'a window: ' + wn[0].strip()[:120]; break
    pos = status() or pos
    if not pos:
        end = 'no position'; break
    dx, dz = GX - pos[0], GZ - pos[2]; d = math.hypot(dx, dz)
    if d <= STOP:
        end = 'there'; break
    if best is not None and d > best - 15:
        stuck += 1
    else:
        stuck = 0
    best = d if best is None else min(best, d)
    if base is None:
        end = 'no heading learnt'; break
    yaw = math.degrees(wrap(math.atan2(dx, dz) - base))
    how = ''
    if stuck >= 4:
        yaw += 70 * side; how = ' (stuck: aside %+d)' % (70 * side); side = -side; stuck = 0
    f = 1.0 if d > 300 else 0.55
    dur = min(1.0, max(0.25, (d - STOP * 0.7) / (230 if f == 1 else 120)))
    xt('head 0 0 0 yaw %.1f s 0.2' % yaw); time.sleep(0.25)
    xt('stick L 0 %.2f' % f, 'head 0 0 0 yaw %.1f s 0.1' % yaw); time.sleep(dur); xt('stick L 0 0', 'head 0 0 0 yaw %.1f s 0.1' % yaw)
    print('%5.1f s  pos (%.0f %.0f %.0f)  %.1f m  yaw %+6.1f  step %.2f s x %.2f%s' % (time.time() - t0, pos[0], pos[1], pos[2],
          d / 100, yaw, dur, f, how), flush=True)
    time.sleep(0.3)
xt('stick L 0 0', 'head 0 0 0 yaw %.1f s 0.2' % yaw)
pos = status() or pos
pitch = 0.0
if LOOK == LOOK and pos and end in ('there', 'time'):
    d = math.hypot(GX - pos[0], GZ - pos[2])
    pitch = max(-75.0, min(75.0, math.degrees(math.atan2(LOOK - (pos[1] + 165), max(d, 1.0)))))
    for dy in (0, -20, 20, -40, 40, 0):
        xt('head 0 0 0 yaw %.1f pitch %.1f s 0.4' % (yaw + dy, pitch)); time.sleep(2.0 if dy == 0 else 1.2)
        pr, wn = news(n0)
        print('  look: yaw %+.1f pitch %+.1f%s' % (yaw + dy, pitch, ' - a prompt' if pr else ''), flush=True)
        if pr:
            end = 'there; looked (yaw %+.0f): a prompt: %s' % (dy, pr[0].strip()[:150]); yaw += dy; break
d = math.hypot(GX - pos[0], GZ - pos[2]) if pos else -100
print('WALK end: %s; at %s, %.1f m from (%.0f %.0f), yaw %.1f pitch %.1f, %.1f s' % (end, '(%.0f %.0f %.0f)' % pos if pos
      else '-', d / 100, GX, GZ, yaw, pitch, time.time() - t0), flush=True)
