#!/usr/bin/env python3
"""inrec.py - b35 (docs/re-xr-pipeline.md 160): SOMVR_INPUT_REC's recordings (mod/somvr/somvr_inrec.h's format).

  inrec.py info FILE                         the header, the records, the span, the marks, the hits
  inrec.py marks FILE                        the MARK records: number, t, folder
  inrec.py cut FILE T0 T1 -o OUT             the records with t in [T0, T1] (the game clock) after a header and the
                                             combat config: a replay's source (SOMVR_INPUT_REPLAY)
  inrec.py state FILE T                      the last STATE at or before T as shell lines (ROOT, YAW, ORC_n ...)
  inrec.py compare SRC REPLAYED [-o TSV]     a replay against its source, frame by frame: the replayed frames' head and
                                             grips (what the logic got) against the source at the same source time
                                             through the same remap; the head's and the hands' place to Talion's base;
                                             the hits on both sides
  inrec.py from-swordrec SWORDREC LOG T0 T1 -o OUT   a SOMVR_SWORD_REC file's right grip (the arms' space) as FRAME
                                             records (the game clock from LOG's H4 lines): an older session's swing for
                                             tests/replay_input's `grip`
Reads only; writes only OUT.
"""
import argparse
import bisect
import math
import os
import re
import struct
import sys

HDR = struct.Struct('<8sIIdqiIII16s')
RH = struct.Struct('<HH')
FRAME_HEAD = struct.Struct('<HHIdqq6f8f3ff3fffI2HI')
HAND = struct.Struct('<7f6f7f4fI8B8B')
STATE_HEAD = struct.Struct('<HHIdq3ffffII')
ORC = struct.Struct('<3ffffII')
BLADE = struct.Struct('<HHId15fI')
HIT = struct.Struct('<HHIdi5fII')
MARK = struct.Struct('<HHId32s')
NOTE = struct.Struct('<HHId')
CFG = struct.Struct('<HHIII')
REPLAY = struct.Struct('<HHIqqff3f3fII')
assert HDR.size == 64 and FRAME_HEAD.size == 136 and HAND.size == 116 and STATE_HEAD.size == 56 and ORC.size == 32
assert BLADE.size == 80 and HIT.size == 48 and MARK.size == 48 and REPLAY.size == 64
FRAME_SIZE = 136 + 2 * 116
T_FRAME, T_STATE, T_BLADE, T_HIT, T_MARK, T_CFG, T_NOTE, T_REPLAY = 1, 2, 3, 4, 5, 6, 7, 8
TIERS = ['none', 'light', 'heavy', 'stab']


def load(path):
    b = open(path, 'rb').read()
    h = HDR.unpack_from(b, 0)
    if h[0] != b'SOMVRIN1' or h[1] < 1 or h[7] != FRAME_SIZE:
        sys.exit('%s: not a SOMVR_INPUT_REC recording of this format (%r, frame size %d)' % (path, h[0], h[7]))
    return b, h


def records(b, h):
    o = h[2]
    while o + 4 <= len(b):
        t, sz = RH.unpack_from(b, o)
        if sz < 4 or sz % 8 or o + sz > len(b):
            break
        yield t, o, sz
        o += sz


def rec_t(b, t, o):
    """the record's game clock (None: it has none)"""
    if t in (T_FRAME, T_STATE, T_BLADE, T_HIT, T_MARK, T_NOTE):
        return struct.unpack_from('<d', b, o + 8)[0]
    return None


class Frame:
    __slots__ = ('frame', 't', 'xr', 'snap', 'eye_p', 'eye_q', 'p0', 'yaw0', 'base', 'psi', 'scale', 'flags', 'hands')

    def __init__(self, b, o):
        v = FRAME_HEAD.unpack_from(b, o)
        self.frame, self.t, self.xr, self.snap = v[2], v[3], v[4], v[5]
        self.eye_p = [list(v[6:9]), list(v[9:12])]
        self.eye_q = [list(v[12:16]), list(v[16:20])]
        self.p0, self.yaw0, self.base, self.psi, self.scale = list(v[20:23]), v[23], list(v[24:27]), v[27], v[28]
        self.flags = v[29]
        self.hands = []
        for k in range(2):
            hv = HAND.unpack_from(b, o + 136 + 116 * k)
            self.hands.append({'p': list(hv[0:3]), 'q': list(hv[3:7]), 'lin': list(hv[7:10]), 'ang': list(hv[10:13]),
                               'trigger': hv[20], 'squeeze': hv[21], 'buttons': hv[24], 'valid': hv[25],
                               'active': hv[32], 'presses': hv[33:41]})


def frames_of(b, h):
    return [Frame(b, o) for t, o, sz in records(b, h) if t == T_FRAME]


# ---- the math of somvr_inrec.c (the sample, the remap) and somvr_hands.c (the rig) --------------------------------------
def lerp3(a, b, u):
    return [a[i] + u * (b[i] - a[i]) for i in range(3)]


def nlerp(a, b, u):
    d = sum(a[i] * b[i] for i in range(4))
    s = -1.0 if d < 0 else 1.0
    r = [a[i] + u * (s * b[i] - a[i]) for i in range(4)]
    n = math.sqrt(sum(x * x for x in r))
    return [x / n for x in r] if n > 1e-6 else list(a)


def qmul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return [aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz]


def yrot(a, v):
    """sv_qyaw(a) applied: R(0,0,1) = (sin a, 0, cos a)"""
    c, s = math.cos(a), math.sin(a)
    return [c * v[0] + s * v[2], v[1], -s * v[0] + c * v[2]]


def to_game(p):
    return [100 * p[0], 100 * p[1], -100 * p[2]]


def qgame(q):
    return [-q[0], -q[1], q[2], q[3]]


def map_pose(rot, p0s, p0l, p, q):
    g = to_game(p)
    d = yrot(rot, [g[i] - p0s[i] for i in range(3)])
    gp = [p0l[i] + d[i] for i in range(3)]
    r = [0.0, math.sin(rot / 2), 0.0, math.cos(rot / 2)]
    gq = qmul(r, qgame(q))
    n = math.sqrt(sum(x * x for x in gq))
    gq = [x / n for x in gq]
    return [gp[0] / 100, gp[1] / 100, -gp[2] / 100], qgame(gq)


def sample(fr, xs, t):
    """the source at XrTime t as somvr_ir_sample (positions, the head's eyes, the grips)"""
    i = bisect.bisect_right(xs, t) - 1
    i = max(0, i)
    a = fr[i]
    if i + 1 >= len(fr) or t <= a.xr:
        return a.eye_p, a.eye_q, [(hh['p'], hh['q'], hh['valid']) for hh in a.hands], a
    bb = fr[i + 1]
    u = min(1.0, max(0.0, (t - a.xr) / float(bb.xr - a.xr)))
    eye_p = [lerp3(a.eye_p[e], bb.eye_p[e], u) for e in range(2)]
    eye_q = [nlerp(a.eye_q[e], bb.eye_q[e], u) for e in range(2)]
    hands = []
    for k in range(2):
        ha, hb = a.hands[k], bb.hands[k]
        if ha['valid'] and hb['valid']:
            hands.append((lerp3(ha['p'], hb['p'], u), nlerp(ha['q'], hb['q'], u), 1))
        else:
            hands.append((ha['p'], ha['q'], ha['valid']))
    return eye_p, eye_q, hands, a


def world_rel(f_rig, p_xr):
    """the rig's map of an XR point, relative to its base (cm): s R(psi - yaw0) (m(p) - P0)"""
    g = to_game(p_xr)
    d = yrot(f_rig.psi - f_rig.yaw0, [g[i] - f_rig.p0[i] for i in range(3)])
    s = f_rig.scale if 0.5 <= f_rig.scale <= 2 else 1.0
    return [s * x for x in d]


def dist(a, b):
    return math.sqrt(sum((a[i] - b[i]) ** 2 for i in range(3)))


def qdeg(a, b):
    d = abs(sum(a[i] * b[i] for i in range(4)))
    return 2 * math.degrees(math.acos(min(1.0, d)))


def hits_of(b, h):
    out = []
    for t, o, sz in records(b, h):
        if t == T_HIT:
            v = HIT.unpack_from(b, o)
            out.append({'orc': v[2], 't': v[3], 'tier': v[4], 'along': v[5], 'j': v[7], 'mps': v[8],
                        'kind': 'contact' if v[11] == 1 else 'hit'})
    return out


# ---- the commands ----------------------------------------------------------------------------------------------------
def cmd_info(a):
    b, h = load(a.file)
    n = {}
    t_lo, t_hi = None, None
    for t, o, sz in records(b, h):
        n[t] = n.get(t, 0) + 1
        if t == T_FRAME:
            tt = rec_t(b, t, o)
            t_lo = tt if t_lo is None else t_lo
            t_hi = tt
    names = {1: 'frames', 2: 'states', 3: 'blades', 4: 'hits', 5: 'marks', 6: 'cfgs', 7: 'notes', 8: 'replayed'}
    print('%s: v%d, built %s, pid %d, t0 %.3f, %.1f MB%s' % (a.file, h[1], h[9].rstrip(b'\0').decode(errors='replace'),
          h[5], h[3], len(b) / 1e6, ' (a mark\'s copy)' if h[6] & 1 else ' (a cut)' if h[6] & 2 else ''))
    print('  ' + ', '.join('%s %d' % (names.get(k, str(k)), v) for k, v in sorted(n.items())))
    if t_lo is not None:
        print('  frames t=%.3f..%.3f (%.1f s, %.1f a second)' % (t_lo, t_hi, t_hi - t_lo,
                                                               n.get(1, 0) / max(1e-3, t_hi - t_lo)))
    cmd_marks(a, b, h)
    for x in hits_of(b, h):
        print('  %s t=%.3f orc %08x %s %.1f J %.1f m/s along %.2f' % (x['kind'], x['t'], x['orc'], TIERS[x['tier'] % 4],
                                                                     x['j'], x['mps'], x['along']))


def cmd_marks(a, b=None, h=None):
    if b is None:
        b, h = load(a.file)
    for t, o, sz in records(b, h):
        if t == T_MARK:
            v = MARK.unpack_from(b, o)
            print('  mark #%d t=%.3f %s' % (v[2], v[3], v[4].rstrip(b'\0').decode(errors='replace')))


def cmd_cut(a):
    b, h = load(a.file)
    out = bytearray(HDR.pack(b'SOMVRIN1', h[1], 64, a.t0, h[4], h[5], h[6] | 2, h[7], 0, h[9]))
    cfg = None
    cur = None
    n = 0
    for t, o, sz in records(b, h):
        if t == T_CFG:
            cfg = b[o:o + sz]
            continue
        tt = rec_t(b, t, o)
        if tt is not None:
            cur = tt
        if cur is None or not (a.t0 <= cur <= a.t1):
            continue
        if cfg is not None:
            out += cfg
            cfg = None
        out += b[o:o + sz]
        n += t == T_FRAME
    if not n:
        sys.exit('no frame in t=%.3f..%.3f' % (a.t0, a.t1))
    open(a.out, 'wb').write(out)
    print('cut %s t=%.3f..%.3f -> %s: %d frames, %.2f MB' % (a.file, a.t0, a.t1, a.out, n, len(out) / 1e6))


def cmd_state(a):
    b, h = load(a.file)
    best = None
    first_frame = None
    for t, o, sz in records(b, h):
        if t == T_STATE and rec_t(b, t, o) <= a.t:
            best = o
        if t == T_FRAME and first_frame is None and rec_t(b, t, o) >= a.t:
            first_frame = Frame(b, o)
    if best is None:
        sys.exit('no STATE at or before t=%.3f' % a.t)
    v = STATE_HEAD.unpack_from(b, best)
    print('STATE_T=%.3f' % v[3])
    print('ROOT="%.1f %.1f %.1f"' % v[5:8])
    print('YAW=%.1f' % math.degrees(v[8]))
    print('HEALTH=%.1f' % v[9])
    print('FLAGS=%d' % v[11])
    norc = v[2]
    k = 0
    for i in range(min(norc, 6)):
        ov = ORC.unpack_from(b, best + 56 + 32 * i)
        if ov[7] & 1:  # dead
            continue
        print('ORC_%d="%.1f %.1f %.1f %.1f %.0f %d"' % (k, ov[0], ov[1], ov[2], math.degrees(ov[3]), ov[4], ov[7]))
        k += 1
    print('NORCS=%d' % k)
    if first_frame:
        print('PSI=%.2f' % math.degrees(first_frame.psi))


def cmd_compare(a):
    bs, hs = load(a.src)
    br, hr = load(a.replayed)
    src = frames_of(bs, hs)
    xs = [f.xr for f in src]
    # the replayed frames and their REPLAY records (the source time each showed, the remap)
    rep = {}
    frames = []
    pend = None
    for t, o, sz in records(br, hr):
        if t == T_REPLAY:
            v = REPLAY.unpack_from(br, o)
            pend = {'frame': v[2], 'src': v[3], 'src0': v[4], 'rot': v[5], 'kappa': v[6], 'p0s': list(v[7:10]),
                    'p0l': list(v[10:13]), 'flags': v[13]}
            rep[v[2]] = pend
        elif t == T_FRAME:
            f = Frame(br, o)
            if f.flags & 8 and f.frame in rep:
                frames.append((f, rep[f.frame]))
    if not frames:
        sys.exit('%s: no replayed frame (SOMVR_INPUT_REPLAY with SOMVR_INPUT_REC=on)' % a.replayed)
    names = ['head', 'left grip', 'right grip']
    err = {k: [] for k in names}
    errq = {k: [] for k in names}
    rel = {k: [] for k in names}
    drift = []
    rows = []
    for f, r in frames:
        eye_p, eye_q, hands, sf = sample(src, xs, r['src'])
        exp = []
        for e in range(1):
            exp.append(map_pose(r['rot'], r['p0s'], r['p0l'], eye_p[e], eye_q[e]))
        got = [(f.eye_p[0], f.eye_q[0])]
        srcp = [eye_p[0]]
        for k in range(2):
            p, q, valid = hands[k]
            exp.append(map_pose(r['rot'], r['p0s'], r['p0l'], p, q) if valid else None)
            got.append((f.hands[k]['p'], f.hands[k]['q']) if f.hands[k]['valid'] else None)
            srcp.append(p if valid else None)
        row = [f.t, (r['src'] - r['src0']) * 1e-9]
        # the rig's heading drift: (psi_live - yaw0_live + rot) - (psi_src - yaw0_src)
        dpsi = math.degrees(math.remainder((f.psi - f.yaw0 + r['rot']) - (sf.psi - sf.yaw0), 2 * math.pi))
        drift.append(abs(dpsi))
        for i, k in enumerate(names):
            if exp[i] is None or got[i] is None:
                row += ['', '']
                continue
            d = dist(exp[i][0], got[i][0]) * 1000.0
            dq = qdeg(exp[i][1], got[i][1])
            err[k].append(d)
            errq[k].append(dq)
            # the place to Talion's base (the rig's world, relative to its base), heading drift removed
            ws = world_rel(sf, srcp[i])
            wl = world_rel(f, got[i][0])
            wl = yrot(-math.radians(dpsi), wl)
            rel[k].append(dist(ws, wl))
            row += ['%.3f' % d, '%.3f' % dq]
        rows.append(row)

    def stat(v):
        if not v:
            return 'none'
        v = sorted(v)
        return 'mean %.3f p50 %.3f max %.3f' % (sum(v) / len(v), v[len(v) // 2], v[-1])
    print('compare %s <- %s: %d replayed frames (the source %d frames, %.1f s)' % (
        a.replayed, a.src, len(frames), len(src), (src[-1].xr - src[0].xr) * 1e-9 if src else 0))
    for k in names:
        print('  %-10s the logic\'s pose vs the source through the remap: position %s mm; orientation %s deg' % (
            k, stat(err[k]), stat(errq[k])))
        print('  %-10s its place to Talion\'s base (the rig\'s world) vs the source\'s: %s cm' % (k, stat(rel[k])))
    print('  the base heading drift (live vs source since the start): %s deg' % stat(drift))
    hs_src = hits_of(bs, hs)
    hs_rep = hits_of(br, hr)
    t0s = frames[0][1]['src0']
    s0 = None
    for f in src:
        if f.xr >= t0s:
            s0 = f.t
            break
    r0 = frames[0][0].t
    print('  hits: the source %d, the replay %d' % (len(hs_src), len(hs_rep)))
    for x in hs_src:
        print('    source  +%.3f s %s %s %.1f J (orc %08x)' % (x['t'] - (s0 or 0), x['kind'], TIERS[x['tier'] % 4], x['j'], x['orc']))
    for x in hs_rep:
        print('    replay  +%.3f s %s %s %.1f J (orc %08x)' % (x['t'] - r0, x['kind'], TIERS[x['tier'] % 4], x['j'], x['orc']))
    if a.out:
        with open(a.out, 'w') as o:
            o.write('t\tsrc_s\thead_mm\thead_deg\tL_mm\tL_deg\tR_mm\tR_deg\n')
            for row in rows:
                o.write('\t'.join(x if isinstance(x, str) else '%.4f' % x for x in row) + '\n')


def cmd_from_swordrec(a):
    off = None
    for line in open(a.log, errors='replace'):
        m = re.match(r'H4 n=\d+ t=([0-9.]+) .* time=(\d+) ', line)
        if m:
            off = float(m.group(1)) - int(m.group(2)) * 1e-9
            if float(m.group(1)) > a.t0:
                break
    if off is None:
        sys.exit('%s: no H4 line with t= and time= (the game clock vs XrTime)' % a.log)
    out = bytearray(HDR.pack(b'SOMVRIN1', 1, 64, a.t0, 0, 0, 2, FRAME_SIZE, 0, b'swordrec'))
    n = 0
    for line in open(a.swordrec):
        if line.startswith('#'):
            continue
        v = line.split()
        if len(v) < 12:
            continue
        xr = float(v[0])
        t = xr + off
        if not (a.t0 <= t <= a.t1):
            continue
        q = [float(x) for x in v[1:5]]
        p = [float(x) for x in v[9:12]]  # cm, the arms' space (the game's convention)
        head = FRAME_HEAD.pack(T_FRAME, FRAME_SIZE, n, t, int(xr * 1e9), 0, *([0.0] * 6), *([0, 0, 0, 1.0] * 2),
                               0, 0, 0, 0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0)
        empty = HAND.pack(*([0.0] * 6), 1.0, *([0.0] * 6), *([0.0] * 6), 1.0, 0, 0, 0, 0, 0, *([0] * 8), *([0] * 8))
        # the game convention back to OpenXR (the reader's somvr_hand_game gives the record's own values again)
        right = HAND.pack(p[0] / 100, p[1] / 100, -p[2] / 100, -q[0], -q[1], q[2], q[3], *([0.0] * 6),
                          *([0.0] * 6), 1.0, 0, 0, 0, 0, 0, 1, 1, 0, 1, 0, 0, 0, 1, *([0] * 8))
        out += head + empty + right
        n += 1
    if not n:
        sys.exit('no SWORDREC line in t=%.3f..%.3f (the offset t - XrTime %.3f s)' % (a.t0, a.t1, off))
    open(a.out, 'wb').write(out)
    print('from-swordrec %s t=%.3f..%.3f (t - XrTime = %.3f s) -> %s: %d frames' % (a.swordrec, a.t0, a.t1, off, a.out, n))


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    s = p.add_subparsers(dest='cmd', required=True)
    x = s.add_parser('info'); x.add_argument('file'); x.set_defaults(fn=cmd_info)
    x = s.add_parser('marks'); x.add_argument('file'); x.set_defaults(fn=cmd_marks)
    x = s.add_parser('cut'); x.add_argument('file'); x.add_argument('t0', type=float); x.add_argument('t1', type=float)
    x.add_argument('-o', '--out', required=True); x.set_defaults(fn=cmd_cut)
    x = s.add_parser('state'); x.add_argument('file'); x.add_argument('t', type=float); x.set_defaults(fn=cmd_state)
    x = s.add_parser('compare'); x.add_argument('src'); x.add_argument('replayed'); x.add_argument('-o', '--out')
    x.set_defaults(fn=cmd_compare)
    x = s.add_parser('from-swordrec'); x.add_argument('swordrec'); x.add_argument('log'); x.add_argument('t0', type=float)
    x.add_argument('t1', type=float); x.add_argument('-o', '--out', required=True); x.set_defaults(fn=cmd_from_swordrec)
    a = p.parse_args()
    a.fn(a)


if __name__ == '__main__':
    main()
