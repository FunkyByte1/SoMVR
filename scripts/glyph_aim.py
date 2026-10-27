#!/usr/bin/env python3
"""glyph_aim.py - the collect drill's round 2 (handoff/HANDOFF-collect.md "Round 2"): turn the game's own camera (not
our view: the HMD owns that) at a place, to test whether a script-side check uses the game's camera. For a running
headless boot with SOMVR_INSPECT (it tracks the game's view) and SOMVR_CROUCH (its probe word `crouch try CMD MS`):
  glyph_aim.py RUN X Y Z [--tries 14] [--cmds YP,YN,PP,PN] [--stick]
Each step: `collect status` -> `COLLECT view: the game's camera at (x y z) yaw Y pitch P deg`; the yaw error -> a
press of YawPositive 10 or YawNegative 11 (`crouch try CMD MS`), the pitch error -> PitchPositive 8 / PitchNegative 9;
the rate is learnt from the first presses. --stick: the pad's right stick instead (`collect camstick X Y MS`,
SOMVR_GLYPH=log). Run 20261027-084303's YawAxis 13 had no binding record (`rec=0`) and turned nothing, 085024's
YawAccel 23 was not sent, 091807's right stick reached only CombatSecondaryAnalogX 134. Ends within 6 deg (yaw) / 8 deg (pitch), or after --tries steps; prints `AIM end: ...`."""
import math, os, re, sys, time
R = sys.argv[1]
GX, GY, GZ = (float(v) for v in sys.argv[2:5])
TRIES = int(sys.argv[sys.argv.index('--tries') + 1]) if '--tries' in sys.argv else 14
CMDS = [int(v) for v in (sys.argv[sys.argv.index('--cmds') + 1] if '--cmds' in sys.argv else '10,11,8,9').split(',')]
STICK = '--stick' in sys.argv  # the test word `collect camstick X Y MS` (the pad's right stick); else command presses
LOG = os.path.join(R, 'somvr.log')
VRE = re.compile(r'^COLLECT view: the game.s camera at \((-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\) yaw (-?[\d.]+) pitch (-?[\d.]+)')


def word(w):
    with open(os.path.join(R, 'somvr.trigger'), 'w') as f:
        f.write(w)


def view():
    n0 = sum(1 for _ in open(LOG, errors='replace'))
    word('collect status')
    for _ in range(30):
        time.sleep(0.15)
        ls = open(LOG, errors='replace').readlines()[n0:]
        for ln in ls:
            m = VRE.match(ln)
            if m:
                return [float(m.group(i)) for i in range(1, 6)]
    return None


def wrap(a):
    return (a + 180.0) % 360.0 - 180.0


rate = {0: None, 1: None}  # deg per ms at stick +1 (axis 0 = yaw by RX, 1 = pitch by RY)
v = view()
for step in range(TRIES):
    if not v:
        print('AIM end: no view line')
        sys.exit(1)
    x, y, z, yaw, pitch = v
    dx, dy, dz = GX - x, GY - y, GZ - z
    want_yaw = math.degrees(math.atan2(dx, dz))
    want_pitch = math.degrees(math.atan2(dy, math.hypot(dx, dz)))
    ey, ep = wrap(want_yaw - yaw), want_pitch - pitch
    print('%s step %d: yaw %+.1f (want %+.1f, off %+.1f) pitch %+.1f (want %+.1f, off %+.1f)' % (
        time.strftime('%T'), step, yaw, want_yaw, ey, pitch, want_pitch, ep), flush=True)
    if abs(ey) <= 6 and abs(ep) <= 8:
        print('AIM end: on it (yaw off %+.1f, pitch off %+.1f)' % (ey, ep))
        sys.exit(0)
    ax, err = (0, ey) if abs(ey) > 6 else (1, ep)
    r = rate[ax]
    if r is None:
        sgn, ms = 1.0, 250
    else:
        sgn = 1.0 if err / r > 0 else -1.0
        ms = int(max(60, min(2000, abs(err / r) * 0.8)))
    if STICK:
        word('collect camstick %.2f %.2f %d' % ((sgn if ax == 0 else 0.0), (sgn if ax == 1 else 0.0), ms))
    else:  # YawPositive/YawNegative, PitchPositive/PitchNegative: the sign picks the command
        word('crouch try %d %d' % (CMDS[(0 if ax == 0 else 2) + (0 if sgn > 0 else 1)], ms))
    time.sleep(ms / 1000.0 + 0.5)
    v2 = view()
    if v2:
        d = wrap(v2[3] - yaw) if ax == 0 else v2[4] - pitch
        if abs(d) > 0.5:
            rate[ax] = d / ms / sgn
        print('  %s %+.0f %d ms: %+.1f deg (%s deg/ms)' % ('yaw' if ax == 0 else 'pitch', sgn, ms, d,
                                                          '%.3f' % rate[ax] if rate[ax] else '?'), flush=True)
    v = v2
print('AIM end: not within the tolerance after %d steps' % TRIES)
