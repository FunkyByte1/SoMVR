#!/usr/bin/env python3
"""winport_xref.py - research tool (docs/re-winport.md 7.2): Linux <-> Windows side-by-side checks without Ghidra.

Reads the two index exports (reference/index, reference/index-win64), the two binaries (reference/bin) and
winport_match.py's pairs.tsv; disassembles with llvm-objdump (Intel syntax). Read-only on everything.

  python3 -I winport_xref.py cmp LINUX_FN WIN_FN     field offsets / vcall slots / calls both functions use
  python3 -I winport_xref.py cands LINUX_FN          Windows candidates by matched call-graph neighbours (+ cmp each)
  python3 -I winport_xref.py dis l|w ADDR [N]        disassembly (N bytes, default the function's size)
  python3 -I winport_xref.py vt WIN_VTABLE           a Windows vtable's slots (any run of .text pointers in .rdata that
                                                     a RIP-relative LEA or a relocated pointer names)
  python3 -I winport_xref.py vtsize LO HI            Windows vtables with LO..HI slots (static objects included)
  python3 -I winport_xref.py lvt LINUX_VTABLE        a Linux vtable's slots (vfuncs.tsv)

Env: WINPORT_PAIRS (default ../logs/winport/m3/pairs.tsv of the main checkout), WINPORT_CACHE (a pickle of the
indexes, default $TMPDIR/winport_xref.pkl). pairs.tsv's `lone` (and some `str`) pairs can be wrong: cmp them.
"""
import bisect, collections, os, pickle, re, struct, subprocess, sys
import numpy as np
SOM = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(SOM, 'scripts'))
import winport_match as M
R = SOM + '/reference'
EXE = R + '/bin/ShadowOfMordor.exe'
ELF = R + '/bin/linux/ShadowOfMordor'
CACHE = os.environ.get('WINPORT_CACHE', os.path.join(os.environ.get('TMPDIR', '/tmp'), 'winport_xref.pkl'))
PAIRS = os.environ.get('WINPORT_PAIRS', '/home/bara/Projects/flat2vr/ShadowOfMordor/logs/winport/m3/pairs.tsv')

def _build():
    lE, lS, lN = M.load_funcs(R + '/index')
    wE, wS, wN = M.load_funcs(R + '/index-win64')
    lo, li = M.load_calls(R + '/index')
    wo, wi = M.load_calls(R + '/index-win64')
    pe = M.PE(EXE)
    wvt, wvref = M.win_vtables(pe, set(wE))
    lvt, lcls = M.load_linux_vtables(R + '/index')
    l2w = {}
    for ln in open(PAIRS).read().split('\n')[1:]:
        if ln:
            a, b, h = ln.split('\t'); l2w[int(a, 16)] = (int(b, 16), h)
    d = dict(lE=lE, lS=lS, lN=lN, wE=wE, wS=wS, wN=wN, lo=dict(lo), li=dict(li), wo=dict(wo), wi=dict(wi),
             wvt=wvt, wvref=wvref, lvt=lvt, lcls=lcls, l2w=l2w)
    pickle.dump(d, open(CACHE, 'wb'))
    return d

D = pickle.load(open(CACHE, 'rb')) if os.path.exists(CACHE) else _build()
globals().update(D)
w2l = {w: l for l, (w, h) in l2w.items()}
PEo = M.PE(EXE)
ELFb = open(ELF, 'rb').read()
BASE = 0x140000000

def cont(E, S, a):
    i = bisect.bisect_right(E, a) - 1
    return E[i] if i >= 0 and a < E[i] + max(S[E[i]], 1) else None
def wf(a): return cont(wE, wS, a)
def lf(a): return cont(lE, lS, a)
def wb(va, n): o = PEo.off(va); return PEo.b[o:o + n]
def loff(va):
    return va - 0x44dcd70 + 0x40dbd70 if va >= 0x44dcd70 else va - 0x400000
def lb(va, n): o = loff(va); return ELFb[o:o + n]
def hx(b): return ' '.join('%02x' % c for c in b)
def wq(va): return PEo.q(va)
def lq(va): return struct.unpack_from('<Q', ELFb, loff(va))[0]
_wslot = None
def wslots(f):
    global _wslot
    if _wslot is None:
        _wslot = collections.defaultdict(list)
        for v, sl in wvt.items():
            for k, x in enumerate(sl): _wslot[x].append((v, k))
    return _wslot.get(f, [])
_lslot = None
def lslots(f):
    global _lslot
    if _lslot is None:
        _lslot = collections.defaultdict(list)
        for v, sl in lvt.items():
            for k, x in enumerate(sl):
                if x: _lslot[x].append((v, k))
    return _lslot.get(f, [])
def wcalls_in(w, target):
    o = PEo.off(w); body = PEo.b[o:o + wS.get(w, 0)]; hits = []
    for i in range(len(body) - 4):
        if body[i] in (0xe8, 0xe9) and w + i + 5 + struct.unpack_from('<i', body, i + 1)[0] == target: hits.append(w + i)
    return hits
def lcalls_in(l, target):
    body = lb(l, lS.get(l, 0)); hits = []
    for i in range(len(body) - 4):
        if body[i] in (0xe8, 0xe9) and l + i + 5 + struct.unpack_from('<i', body, i + 1)[0] == target: hits.append(l + i)
    return hits
def W(l):
    r = l2w.get(l); return r[0] if r else None
def wdis(a, b=None, n=0x40):
    b = b or a + n
    out = subprocess.run(['llvm-objdump', '-d', '--no-show-raw-insn', '-M', 'intel', '--start-address=%#x' % a, '--stop-address=%#x' % b, EXE],
                         capture_output=True, text=True).stdout
    return '\n'.join(l for l in out.split('\n') if l.strip().startswith('14'))
def ldis(a, b=None, n=0x40):
    b = b or a + n
    out = subprocess.run(['llvm-objdump', '-d', '--no-show-raw-insn', '-M', 'intel', '--start-address=%#x' % a, '--stop-address=%#x' % b, ELF],
                         capture_output=True, text=True).stdout
    return '\n'.join(l for l in out.split('\n') if ':' in l and l.strip()[:1] in '0123456789abcdef' and not l.endswith(':'))
def rva(va): return va - BASE

# ---- side-by-side fingerprints
DISP = re.compile(r'\[(?:r|e)[a-z0-9]+ [+-] (0x[0-9a-f]+)\]|\[(?:r|e)[a-z0-9]+ \+ (?:r|e)[a-z0-9]+(?:\*\d)? [+-] (0x[0-9a-f]+)\]')
IMM = re.compile(r', (0x[0-9a-f]+)$')
CALL = re.compile(r'(?:call|jmp)\s+0x([0-9a-f]+)')
RIP = re.compile(r'# 0x([0-9a-f]+)')
def _dis(side, a, n):
    return (wdis if side == 'w' else ldis)(a, a + n)
def fp(side, a, n=None):
    n = n or (wS if side == 'w' else lS).get(a) or 0x100
    txt = _dis(side, a, n)
    offs, imms, calls, rips, vcalls = collections.Counter(), collections.Counter(), [], [], collections.Counter()
    for ln in txt.split('\n'):
        ins = ln.split('\t', 1)[-1] if '\t' in ln else ln
        if 'rsp' in ins or 'rbp' in ins:
            stack = True
        else:
            stack = False
        for m in DISP.finditer(ins):
            v = int(m.group(1) or m.group(2), 16)
            if not stack and v < 0x10000: offs[v] += 1
        m = IMM.search(ins)
        if m and 'rsp' not in ins:
            v = int(m.group(1), 16)
            if v < 0x100000000: imms[v] += 1
        m = CALL.search(ins)
        if m:
            calls.append(int(m.group(1), 16))
        m = re.search(r'call\s+qword ptr \[(?:r|e)[a-z0-9]+ \+ (0x[0-9a-f]+)\]', ins)
        if m: vcalls[int(m.group(1), 16)] += 1
        m = RIP.search(ins)
        if m: rips.append((int(m.group(1), 16), ins.split()[0]))
    return dict(offs=offs, imms=imms, calls=calls, rips=rips, vcalls=vcalls, txt=txt)
def cmp(l, w, ln=None, wn=None, show=True):
    a, b = fp('l', l, ln), fp('w', w, wn)
    so, sb = set(a['offs']), set(b['offs'])
    lc = set(W(c) for c in a['calls'] if W(c)); wc = set(b['calls'])
    j = len(so & sb) / max(1, len(so | sb))
    r = dict(shared_offs=sorted(so & sb), lin_only=sorted(so - sb), win_only=sorted(sb - so), j=j,
             calls_mapped=len(lc & wc), lin_calls_mapped=len(lc), win_calls=len(set(b['calls'])),
             lvc=sorted(a['vcalls']), wvc=sorted(b['vcalls']))
    if show:
        print('L %#x (%s b) W %#x (%s b): offsets shared %d/%d (J %.2f) calls mapped %d/%d (win %d)' % (
            l, lS.get(l), w, wS.get(w), len(so & sb), len(so | sb), j, r['calls_mapped'], r['lin_calls_mapped'], r['win_calls']))
        print('  shared', ' '.join(hex(x) for x in r['shared_offs'][:60]))
        print('  lin-only', ' '.join(hex(x) for x in r['lin_only'][:40]))
        print('  win-only', ' '.join(hex(x) for x in r['win_only'][:40]))
        print('  vcalls L', [hex(x) for x in r['lvc']], 'W', [hex(x) for x in r['wvc']])
    return r

# ---- Windows vtables incl. static objects (relocations)
def relocs():
    pe = PEo; b = pe.b
    peo = struct.unpack_from('<I', b, 0x3c)[0]
    opt = peo + 24
    # data directories at opt+112 (PE32+); reloc = index 5
    rva, size = struct.unpack_from('<II', b, opt + 112 + 5 * 8)
    o = pe.off(BASE + rva); end = o + size; out = []
    while o < end:
        page, bs = struct.unpack_from('<II', b, o)
        if bs == 0: break
        for i in range((bs - 8) // 2):
            e = struct.unpack_from('<H', b, o + 8 + 2 * i)[0]
            if e >> 12 == 10: out.append(BASE + page + (e & 0xfff))
        o += bs
    return out
_, ta, tvs, trp, trs = PEo.sec('.text')
_, ra, rvs, rrp, rrs = PEo.sec('.rdata')
def istext(v): return ta <= v < ta + tvs
R = relocs()
ptrs = {a: PEo.q(a) for a in R if PEo.off(a) is not None}
rdtext = {a: v for a, v in ptrs.items() if ra <= a < ra + rvs and istext(v)}
# referenced rdata addresses (from .data/.rdata pointers)
refd = collections.defaultdict(list)
for a, v in ptrs.items():
    if ra <= v < ra + rvs: refd[v].append(a)
def run(t, stop=None):
    sl = []; a = t
    while a in rdtext and (a == t or a not in refd):
        sl.append(rdtext[a]); a += 8
    return sl

# ---- RIP-relative references
_ripcache = {}
def _arr(side):
    if side in _ripcache: return _ripcache[side]
    if side == 'w':
        _, ta, tvs, trp, trs = PEo.sec('.text'); buf = PEo.b[trp:trp+min(tvs,trs)]; va0 = ta
    else:
        buf = ELFb[0x11000:0x11000+0x2f2fb7d]; va0 = 0x411000
    a = np.frombuffer(buf, dtype=np.uint8)
    n = len(a) - 4
    d = (a[0:n].astype(np.int64) | (a[1:n+1].astype(np.int64) << 8) | (a[2:n+2].astype(np.int64) << 16) | (a[3:n+3].astype(np.int64) << 24))
    d = np.where(d >= 1 << 31, d - (1 << 32), d)
    tgt = d + np.arange(n, dtype=np.int64) + va0 + 4
    _ripcache[side] = (tgt, va0, d)
    return _ripcache[side]
def riprefs(side, v, extras=(0, 1, 4), lo=0, hi=0):
    """positions (disp32 VA) whose RIP-relative target (+extra for a trailing imm) lands in [v+lo, v+hi]"""
    tgt, va0, d = _arr(side)
    out = []
    for e in extras:
        idx = np.nonzero((tgt + e >= v + lo) & (tgt + e <= v + hi))[0]
        out += [(int(i) + va0, int(tgt[i] + e)) for i in idx]
    return sorted(set(out))
def imm32refs(v):
    tgt, va0, d = _arr('l')
    idx = np.nonzero(d == v)[0]
    return [int(i) + va0 for i in idx]

# ---- call-graph candidates
def cands(l, top=6, show=True):
    v = collections.Counter()
    for x in li.get(l, ()):            # callers of l, mapped -> their callees
        w = W(x)
        if w:
            for y in wo.get(w, ()):
                if y not in w2l: v[y] += 1
    for x in lo.get(l, ()):            # callees of l, mapped -> their callers
        w = W(x)
        if w and len(wi.get(w, ())) <= 400:
            for y in wi.get(w, ()):
                if y not in w2l: v[y] += 1
    nl = len(li.get(l, ())), len(lo.get(l, ()))
    mc = sum(1 for x in li.get(l, ()) if W(x)), sum(1 for x in lo.get(l, ()) if W(x))
    if show:
        print('L %#x %s size %s callers %d (matched %d) callees %d (matched %d)' % (l, lN.get(l), lS.get(l), nl[0], mc[0], nl[1], mc[1]))
        for y, n in v.most_common(top):
            print('   cand %#x votes %d size %s callers %d callees %d' % (y, n, wS.get(y), len(wi.get(y, ())), len(wo.get(y, ()))))
    return v.most_common(top)
def neigh(l):
    print('L %#x callers:' % l, [(hex(x), hex(W(x)) if W(x) else '-') for x in sorted(li.get(l, ()))][:12])
    print('   callees:', [(hex(x), hex(W(x)) if W(x) else '-') for x in sorted(lo.get(l, ()))][:30])

_allv = None
def allv():
    """every Windows vtable candidate: LEA-named (win_vtables) or named by a relocated pointer (static objects)"""
    global _allv
    if _allv is None:
        _allv = {}
        for t in sorted(set(list(refd) + list(wvt))):
            if t in rdtext:
                sl = run(t)
                if len(sl) >= 2:
                    _allv[t] = sl
    return _allv


def main():
    a = sys.argv[1:]
    h = lambda s: int(s, 16)
    if not a:
        print(__doc__); return
    if a[0] == 'cmp':
        cmp(h(a[1]), h(a[2]))
    elif a[0] == 'cands':
        l = h(a[1]); neigh(l)
        for y, n in cands(l):
            cmp(l, y)
    elif a[0] == 'dis':
        f = h(a[2]); n = int(a[3], 0) if len(a) > 3 else ((lS if a[1] == 'l' else wS).get(f) or 0x80)
        print((ldis if a[1] == 'l' else wdis)(f, f + n))
    elif a[0] == 'vt':
        for k, f in enumerate(allv().get(h(a[1]), run(h(a[1])))):
            print(k, hex(f), wS.get(f), 'pair L %#x' % w2l[f] if f in w2l else '', hx(wb(f, 16)))
    elif a[0] == 'vtsize':
        for t, sl in sorted(allv().items()):
            if int(a[1]) <= len(sl) <= int(a[2]):
                print(hex(t), len(sl), 'lea' if t in wvt else 'reloc', 'matched %d' % sum(1 for f in sl if f in w2l))
    elif a[0] == 'lvt':
        v = h(a[1])
        for k, f in enumerate(lvt.get(v, [])):
            print(k, hex(f), lS.get(f), lN.get(f), 'pair W %#x' % W(f) if W(f) else '')
    else:
        print(__doc__)


if __name__ == '__main__':
    main()
