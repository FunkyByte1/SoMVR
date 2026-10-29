#!/usr/bin/env python3
"""story2_route.py - walk a timed objective as a VR player would (the story drill's round 2, handoff/HANDOFF-story.md):
the left stick held forward, the sprint latched by its click (our locomotion's 640 cm/s, SOMVR_LOCO), the head turned
toward the next point of a route (or the game's objective marker on the flat picture), Talion's position from
`practice status` every cycle, a flat shot every few seconds. For a running headless boot (RUN = its
logs/boot-check-<ts>/, SOMVR_XR_TEST=RUN/xrtest.txt and SOMVR_PRACTICE on):
  story2_route.py RUN [--secs 150] [--tag rt] [--nosprint] [--waypoints X,Z;X,Z;...] [--yaw0 DEG] [--dumps S]
--waypoints: world points (cm) walked through in turn (each counts as reached within 5 m; the last is the goal, walked
on toward until the run ends); A when SOMVR_CLIMB_DESCENT's edge cue comes (--noedge: never); the head's yaw for a world
heading is learnt from the walk itself (the test head's yaw + = atan2(dx, dz) +, run 20261026-094108); without
waypoints (or after the last) the marker: the gold coin on the flat picture, its bearing.
Ends at a window (a failed screen, a cinematic: BODYMOMENT `window 0 -> 1`), a video, the time, or the user in VR (quit).
One line per cycle (t since the start, position, speed, the head's yaw, why) and a summary line."""
import math, os, re, subprocess, sys, time

R = sys.argv[1]
args = sys.argv[2:]
def opt(name, dflt):
    return args[args.index(name) + 1] if name in args else dflt
SECS = float(opt('--secs', 150)); TAG = opt('--tag', 'rt'); SPRINT = '--nosprint' not in args
DUMPS = float(opt('--dumps', 0))  # every this many seconds the eyes too (a dump: xr_compose.py; 0 = none)
WPS = [tuple(float(v) for v in p.split(',')) for p in opt('--waypoints', '').split(';') if p]
LOG = os.path.join(R, 'somvr.log')
seq = [0]

def user_in():
    a = subprocess.run(['systemctl', '--user', 'is-active', 'somvr-headset'], capture_output=True, text=True).stdout.strip()
    try:
        st = open('/run/user/%d/vr-session/status.json' % os.getuid()).read()
    except OSError:
        st = ''
    return a == 'active' or '"user": true' in st

def bail(why):
    print('%s STOP: %s' % (time.strftime('%T'), why), flush=True)
    with open(os.path.join(R, 'cmd'), 'a') as f:
        f.write('quit\n')
    sys.exit(1)

def xt(*lines):
    seq[0] += 1
    tmp = os.path.join(R, 'xrtest.tmp')
    with open(tmp, 'w') as f:
        f.write('# route %d\n' % seq[0] + ''.join(l + '\n' for l in lines))
    os.rename(tmp, os.path.join(R, 'xrtest.txt'))

def word(w):
    with open(os.path.join(R, 'somvr.trigger'), 'w') as f:
        f.write(w)

def loglines():
    with open(LOG, errors='replace') as f:
        return f.readlines()

PRE = re.compile(r'^PRACTICE t=([\d.]+) status: .*?Talion \S+ at \((-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\)')
def status(n0):
    word('practice status')
    for _ in range(20):
        time.sleep(0.1)
        ls = loglines()
        for l in reversed(ls[n0:]):
            m = PRE.match(l)
            if m:
                return len(ls), tuple(float(m.group(i)) for i in (2, 3, 4))
    return len(loglines()), None

def shot(name, wait=True):
    p = os.path.join(R, 'shots', name + '.png')
    with open(os.path.join(R, 'cmd'), 'a') as f:
        f.write('shot %s\n' % name)
    if not wait:
        return p
    for _ in range(40):
        time.sleep(0.1)
        if os.path.exists(p) and os.path.getsize(p) > 0:
            time.sleep(0.15)
            return p
    return None

def marker(path):
    """the objective's gold coin on the flat picture: (x, y, n) of the biggest cluster outside the objective lines (the
    banner's coin, the top-left line's) and the minimap (bottom left), or None"""
    from PIL import Image
    try:
        im = Image.open(path).convert('RGB')
    except Exception:
        return None
    w, h = im.size
    px = im.load()
    cl = {}
    for y in range(0, h - 40, 2):
        for x in range(0, w, 2):
            if (x < 700 and y > 700) or (720 <= x <= 820 and 200 <= y <= 300) or (x < 700 and y < 120):
                continue
            r, g, b = px[x, y]
            if r > 170 and g > 130 and b < 90 and r - b > 110:
                k = (x // 40, y // 40)
                c = cl.setdefault(k, [0, 0, 0])
                c[0] += 1; c[1] += x; c[2] += y
    if not cl:
        return None
    k, c = max(cl.items(), key=lambda kv: kv[1][0])
    if c[0] < 6:
        return None
    return c[1] / c[0], c[2] / c[0], c[0]

CX, HW, TAN = 959.5, 490.5, 1.107  # the eye image in the 1920 x 1080 shot (469..1450), its half-width tangent (+10 %)
def bearing(x):
    return math.degrees(math.atan((x - CX) / HW * TAN))

def windows(ls):
    return sum(1 for l in ls if re.match(r'^BODYMOMENT t=.*window 0 -> 1', l))

def video(ls):
    for l in reversed(ls):
        if l.startswith('SESSION ui '):
            return 'video=1' in l
    return False

def wrap(a):
    return (a + math.pi) % (2 * math.pi) - math.pi

def edge_cues(ls):
    return sum(1 for l in ls if re.match(r'^CLIMB t=.* edge cue: ', l))

n0 = len(loglines()); w0 = windows(loglines()); e0 = edge_cues(loglines()); n_a = 0
yaw = float(opt('--yaw0', 0)); t0 = time.time()
xt('stick L 0 1', 'head 0 0 0 yaw %.1f s 0.2' % yaw)
time.sleep(0.4)
if SPRINT:
    xt('btn L stick 1'); time.sleep(0.35); xt('btn L stick 0')
print('%s route start (sprint %s, yaw %.1f, waypoints %d)' % (time.strftime('%T'), 'latched' if SPRINT else 'off', yaw,
      len(WPS)), flush=True)
prev = None; path = []; stuck = 0; side = 1; cyc = 0; wi = 0; miss = 0; base = None; lastshot = -9; lastdump = 0
yaw_prev = yaw; end = 'time'; reached = []; detour_until = 0; detour_yaw = 0; yaw_goal = 0; spot = None; nstuck = 0
while time.time() - t0 < SECS:
    if user_in():
        xt('stick L 0 0'); bail('the user is in VR')
    cyc += 1
    t = time.time() - t0
    n0, pos = status(n0)
    ls = loglines()
    if windows(ls) > w0:
        end = 'a window'; break
    if video(ls):
        end = 'a video'; break
    # the edge cue (SOMVR_CLIMB_DESCENT: the walk stopped at an edge over a 4 m drop, a double pulse on the right
    # hand): a player answers it with A, the blink down
    ec = edge_cues(ls)
    if ec > e0 and '--noedge' not in args:
        e0 = ec; n_a += 1
        # build 17+: A held shows the landing (SOMVR_CLIMB_DESCENT_MARK), let go goes, the move stick cancels: a player
        # lets the stick go, holds A a moment, lets go, then walks on (the sprint latched again)
        xt('stick L 0 0', 'head 0 0 0 yaw %.1f s 0.2' % yaw); time.sleep(0.25)
        xt('stick L 0 0', 'btn R a 1', 'head 0 0 0 yaw %.1f s 0.2' % yaw); time.sleep(0.5)
        xt('stick L 0 0', 'btn R a 0', 'head 0 0 0 yaw %.1f s 0.2' % yaw)
        print('%5.1f s  the edge cue: the stick let go, A held 0.5 s (the drop mark), let go (the blink down)' % (time.time() - t0), flush=True)
        time.sleep(1.8)
        xt('stick L 0 1', 'head 0 0 0 yaw %.1f s 0.2' % yaw); time.sleep(0.3)
        if SPRINT:
            xt('stick L 0 1', 'btn L stick 1', 'head 0 0 0 yaw %.1f s 0.2' % yaw); time.sleep(0.35)
            xt('stick L 0 1', 'btn L stick 0', 'head 0 0 0 yaw %.1f s 0.2' % yaw)
        prev = None
        continue
    sp = None
    if pos and prev:
        dx, dz = pos[0] - prev[1][0], pos[2] - prev[1][2]
        sp = math.hypot(dx, dz) / 100.0 / max(t - prev[0], 1e-3)
        # the world heading of the head's yaw 0: from a straight fast stretch (the yaw unchanged since the last cycle)
        if sp > 1.5 and abs(yaw - yaw_prev) < 1:
            b = wrap(math.atan2(dx, dz) - math.radians(yaw))
            base = b if base is None else base + 0.3 * wrap(b - base)
    if pos:
        path.append((round(t, 1), pos))
    turn = None; how = ''
    yaw_prev = yaw
    if wi < len(WPS) and pos:
        wx, wz = WPS[wi]
        dist = math.hypot(wx - pos[0], wz - pos[2]) / 100.0
        if dist < 5 and wi < len(WPS) - 1:  # (the last one is the goal: walked on toward it)
            wi += 1; reached.append(round(t, 1)); how = 'waypoint %d reached at %.1f s; ' % (wi, t)
            if wi < len(WPS):
                wx, wz = WPS[wi]; dist = math.hypot(wx - pos[0], wz - pos[2]) / 100.0
        if wi < len(WPS) and base is not None:
            want = math.atan2(wx - pos[0], wz - pos[2])
            turn = math.degrees(wrap(want - base - math.radians(yaw)))
            how += 'waypoint %d %.0f m' % (wi + 1, dist)
        elif wi < len(WPS):
            how += 'waypoint %d %.0f m (the heading not learnt yet)' % (wi + 1, dist)
    if turn is None and (wi >= len(WPS) or (base is None and not WPS)):
        p = shot('%s-%d' % (TAG, cyc))
        lastshot = t
        mk = marker(p) if p else None
        if mk:
            turn = 0.8 * bearing(mk[0]); how += 'marker x=%.0f y=%.0f n=%d' % mk; miss = 0
        else:
            how += 'no marker'; miss += 1
            if miss >= 2:
                turn = 60; how += ': a look to the right'; miss = 0
    elif t - lastshot >= 3:
        shot('%s-%d' % (TAG, cyc), wait=False); lastshot = t
    if DUMPS and t - lastdump >= DUMPS:
        xt('dump'); lastdump = t
    if sp is not None and sp < 1.0 and t > 4:
        stuck += 1
    else:
        stuck = 0
    # stuck (a rock, a wall, a slope up): a detour square to the goal, longer each time at the same spot, the sides in
    # turn (a player walks along the obstacle and tries again)
    if t < detour_until:
        turn = detour_yaw - yaw; how += ' (detour %+.0f)' % (detour_yaw - yaw_goal)
    elif stuck >= 2 and pos:
        if spot and math.hypot(pos[0] - spot[0], pos[2] - spot[2]) < 1500:
            nstuck += 1
        else:
            spot, nstuck = pos, 1
        dside = -1 if nstuck % 2 else 1
        yaw_goal = yaw + (turn or 0)
        detour_yaw = yaw_goal + 90 * dside
        detour_until = t + 1.5 + 1.2 * nstuck
        turn = detour_yaw - yaw; how += ' STUCK %d: a detour %s for %.1f s' % (nstuck, 'left' if dside < 0 else 'right',
                                                                           detour_until - t)
        stuck = 0
    if turn is not None and abs(turn) > 2:
        yaw += max(-120.0, min(120.0, turn))
        xt('head 0 0 0 yaw %.1f s 0.3' % yaw)
    print('%5.1f s  pos %s  %s  yaw %+6.1f  %s' % (t, '(%.0f %.0f %.0f)' % pos if pos else '-',
          '%.1f m/s' % sp if sp is not None else '-', yaw, how), flush=True)
    prev = (t, pos) if pos else prev
    time.sleep(0.4 if wi < len(WPS) else 0.2)
xt('stick L 0 0')
tot = sum(math.hypot(b[1][0] - a[1][0], b[1][2] - a[1][2]) for a, b in zip(path, path[1:])) / 100.0
print('%s route end (%s) after %.1f s: %.0f m walked, %d cycles, waypoints reached at %s, A at edges %d; path %s' % (
      time.strftime('%T'), end, time.time() - t0, tot, cyc, reached, n_a, ';'.join('%.0f,%.0f' % (p[1][0], p[1][2]) for p in path[::3])), flush=True)
