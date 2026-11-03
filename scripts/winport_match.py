#!/usr/bin/env python3
"""winport_match.py - research tool (docs/re-winport.md): match the Linux (Feral ELF) functions and vtables that
somvr's SITE(...) entries use to the Windows exe (som-win64), from the two Ghidra index exports only, and say for
every SITE whether its Windows counterpart was found automatically.

  python3 -I winport_match.py LINUX_INDEX_DIR WIN_INDEX_DIR WIN_EXE MOD_DIR OUT_DIR

LINUX_INDEX_DIR = reference/index (functions/calls/strings_xrefs/vfuncs .tsv), WIN_INDEX_DIR = reference/index-win64
(SomIndex.java on som-win64: functions/calls/strings_xrefs .tsv), WIN_EXE = reference/bin/ShadowOfMordor.exe (read for
the vtables and the call bytes), MOD_DIR = mod/somvr (the SITE lines). Writes OUT_DIR/pairs.tsv (every matched function
pair: linux, windows, how), OUT_DIR/sites.tsv (one row per SITE: file, name, kind, the Linux addresses, the Windows
answer, status) and prints a summary. Read-only on everything it is given.

How (no Ghidra, no guessing beyond what is written here):
  1. anchors: a string referenced by exactly one function on each side pairs them; a pair needs the mutual best vote.
  2. propagation (repeated): an unmatched Linux function whose matched callers/callees map onto the matched
     callers/callees of exactly one unmatched Windows function (>= 2 shared neighbours, Jaccard >= 0.5, unique best,
     mutual) is paired with it; plus the lone-child rule (a matched pair with exactly one unmatched callee each).
  3. vtables: Windows vtables are runs of function pointers in .rdata whose start a RIP-relative LEA in .text names;
     a Linux vtable is paired with the Windows vtable most of its matched slot functions sit in; its slots are then
     aligned (GCC's two destructor slots vs MSVC's one) and the aligned slot functions paired; then back to 2.
"""
import bisect, collections, glob, os, re, struct, sys


def load_funcs(d):
    entries, size, name = [], {}, {}
    with open(os.path.join(d, 'functions.tsv')) as f:
        next(f)
        for ln in f:
            a, n, s = ln.rstrip('\n').split('\t')[:3]
            a = int(a, 16)
            entries.append(a); size[a] = int(s); name[a] = n
    entries.sort()
    return entries, size, name


def load_calls(d):
    out, inn = collections.defaultdict(set), collections.defaultdict(set)
    with open(os.path.join(d, 'calls.tsv')) as f:
        next(f)
        for ln in f:
            a, b = ln.split('\t')
            a = int(a, 16); b = int(b, 16)
            if a != b:
                out[a].add(b); inn[b].add(a)
    return out, inn


def load_strings(d):
    m = collections.defaultdict(set)
    with open(os.path.join(d, 'strings_xrefs.tsv')) as f:
        next(f)
        for ln in f:
            p = ln.rstrip('\n').split('\t')
            if len(p) < 4 or len(p[2]) < 4:
                continue
            if re.search(r'(^|,)\+\d+ more', p[3]):
                m[p[2]].add(None)       # truncated list: never unique
                continue
            for a, dash in re.findall(r'(?:^|,)(0x[0-9a-f]+):(-(?=,|$))?', p[3]):
                if not dash:            # names may hold commas; '-' = a reference from outside any function
                    m[p[2]].add(int(a, 16))
    return m


class PE:
    def __init__(self, path):
        self.b = open(path, 'rb').read()
        pe = struct.unpack_from('<I', self.b, 0x3c)[0]
        nsec = struct.unpack_from('<H', self.b, pe + 6)[0]
        opt = struct.unpack_from('<H', self.b, pe + 20)[0]
        self.base = struct.unpack_from('<Q', self.b, pe + 24 + 24)[0]
        self.secs = []
        o = pe + 24 + opt
        for i in range(nsec):
            n = self.b[o:o + 8].rstrip(b'\0').decode()
            vs, va, rs, rp = struct.unpack_from('<IIII', self.b, o + 8)
            self.secs.append((n, self.base + va, vs, rp, rs))
            o += 40

    def sec(self, n):
        return next(s for s in self.secs if s[0] == n)

    def off(self, va):
        for n, a, vs, rp, rs in self.secs:
            if a <= va < a + min(vs, rs):
                return rp + va - a
        return None

    def q(self, va):
        o = self.off(va)
        return struct.unpack_from('<Q', self.b, o)[0] if o is not None else None


def win_vtables(pe, fset):
    """address point -> [slot functions]: runs of function pointers in .rdata that a RIP-relative LEA names"""
    _, ta, tvs, trp, trs = pe.sec('.text')
    _, ra, rvs, rrp, rrs = pe.sec('.rdata')
    text = pe.b[trp:trp + min(tvs, trs)]
    leas, sites = set(), collections.defaultdict(list)
    for m in re.finditer(rb'[\x48\x4c]\x8d[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]', text):
        i = m.start()
        disp = struct.unpack_from('<i', text, i + 3)[0]
        t = ta + i + 7 + disp
        if ra <= t < ra + rvs:
            leas.add(t); sites[t].append(ta + i)
    isf = {}
    n = min(rvs, rrs) // 8
    vals = struct.unpack_from('<%dQ' % n, pe.b, rrp)
    for k, v in enumerate(vals):
        if v in fset or ta <= v < ta + tvs:   # Ghidra misses many functions only a vtable names: any .text pointer
            isf[ra + 8 * k] = v
    vt = {}
    for t in sorted(leas):
        if t % 8 or t not in isf:
            continue
        slots, a = [], t
        while a in isf and (a == t or a not in leas):
            slots.append(isf[a]); a += 8
        if len(slots) >= 2:
            vt[t] = slots
    return vt, {t: sites[t] for t in vt}


def linux_vtable_refs(elf, vts):
    """Linux vtable address point -> code addresses storing it (non-PIE: `mov qword [..], imm32` / `mov r32, imm32`)"""
    text = elf[0x11000:0x11000 + 0x2f2fb7d]
    want = {struct.pack('<I', v): v for v in vts if v < 1 << 32}
    refs = collections.defaultdict(list)
    for k, v in want.items():
        for m in re.finditer(re.escape(k), text):
            i = m.start()
            pre = text[max(0, i - 7):i]
            if (len(pre) >= 1 and 0xb8 <= pre[-1] <= 0xbf) or 0xc7 in pre[-6:]:
                refs[v].append(0x411000 + i)
    return refs


def load_linux_vtables(d):
    vt = collections.defaultdict(dict)
    cls = {}
    with open(os.path.join(d, 'vfuncs.tsv')) as f:
        next(f)
        for ln in f:
            p = ln.rstrip('\n').split('\t')
            v = int(p[1], 16); sub = int(p[2], 16)
            a = v                      # vtable column = the address point of the (sub)object's vtable
            vt[a][int(p[3])] = int(p[4], 16) if p[4] not in ('', '0x0') else 0
            cls[a] = p[0] + ('' if sub == 0 else '@%#x' % sub)
    return {a: [s[k] for k in range(max(s) + 1)] if s else [] for a, s in vt.items()}, cls


SITE_RE = re.compile(r'SITE\("([^"]+)",\s*(K_[A-Z]+),\s*(0x[0-9a-f]+),\s*(0x[0-9a-f]+),\s*"([^"]*)",\s*(0x[0-9a-f]+),\s*"([^"]*)"\)')


def main():
    li, wi, exe, mod, out = sys.argv[1:6]
    os.makedirs(out, exist_ok=True)
    lE, lS, lN = load_funcs(li)
    wE, wS, wN = load_funcs(wi)
    lset, wset = set(lE), set(wE)
    lo, lin = load_calls(li)
    wo, win = load_calls(wi)
    ls_, ws_ = load_strings(li), load_strings(wi)
    pe = PE(exe)
    wvt, wvref = win_vtables(pe, wset)
    lvt, lcls = load_linux_vtables(li)
    elf = open(os.path.join(os.path.dirname(exe), 'linux', 'ShadowOfMordor'), 'rb').read()
    lvref = linux_vtable_refs(elf, lvt)

    def containing(E, S, a):
        i = bisect.bisect_right(E, a) - 1
        if i < 0:
            return None, False
        return E[i], a < E[i] + max(S[E[i]], 1)

    lvf = {v: {f for f, ok in (containing(lE, lS, a) for a in xs) if ok} for v, xs in lvref.items()}
    wvf = {v: {f for f, ok in (containing(wE, wS, a) for a in xs) if ok} for v, xs in wvref.items()}
    wfv = collections.defaultdict(set)
    for v, fs in wvf.items():
        for f in fs:
            wfv[f].add(v)
    l2w, w2l, how = {}, {}, {}

    def pair(l, w, h):
        if l in l2w or w in w2l:
            return False
        l2w[l] = w; w2l[w] = l; how[l] = h
        return True

    # 1. string anchors
    votes, wvotes = collections.defaultdict(collections.Counter), collections.defaultdict(collections.Counter)
    for t, L in ls_.items():
        W = ws_.get(t)
        if not W or None in L or None in W:
            continue
        if len(L) == 1 and len(W) == 1:
            l, w = next(iter(L)), next(iter(W))
            votes[l][w] += 1; wvotes[w][l] += 1
    for l, c in votes.items():
        (w, n), *rest = c.most_common(2) + [(None, 0)]
        if rest[0][1] == n:
            continue
        (l2, n2), *rest2 = wvotes[w].most_common(2) + [(None, 0)]
        if l2 == l and rest2[0][1] < n2:
            pair(l, w, 'str')
    n_str = len(l2w)

    # windows function -> [(vtable, slot)]
    wslot = collections.defaultdict(list)
    for v, sl in wvt.items():
        for k, f in enumerate(sl):
            wslot[f].append((v, k))
    lslot = collections.defaultdict(list)
    for v, sl in lvt.items():
        for k, f in enumerate(sl):
            if f:
                lslot[f].append((v, k))
    vmap = {}

    def propagate():
        new = 0
        while True:
            added = 0
            # lone child / lone parent
            for l, w in list(l2w.items()):
                for A, B in ((lo, wo), (lin, win)):
                    cl = [x for x in A.get(l, ()) if x not in l2w]
                    cw = [x for x in B.get(w, ()) if x not in w2l]
                    if len(cl) == 1 and len(cw) == 1:
                        added += pair(cl[0], cw[0], 'lone')
            # neighbourhood votes
            cand = collections.defaultdict(collections.Counter)
            for l, w in list(l2w.items()):
                # unmatched callers of l <-> unmatched callers of w (they share the callee)
                cw = [y for y in win.get(w, ()) if y not in w2l]
                if 0 < len(cw) <= 300:
                    for x in lin.get(l, ()):
                        if x not in l2w:
                            for y in cw:
                                cand[x][y] += 1
                cw = [y for y in wo.get(w, ()) if y not in w2l]
                if 0 < len(cw) <= 300:
                    for x in lo.get(l, ()):
                        if x not in l2w:
                            for y in cw:
                                cand[x][y] += 1
            best = {}
            for x, c in cand.items():
                top = c.most_common(2)
                y, n = top[0]
                if n < 2 or (len(top) > 1 and top[1][1] == n):
                    continue
                nx = sum(1 for z in lo.get(x, ()) if z in l2w) + sum(1 for z in lin.get(x, ()) if z in l2w)
                ny = sum(1 for z in wo.get(y, ()) if z in w2l) + sum(1 for z in win.get(y, ()) if z in w2l)
                if n / max(1, nx + ny - n) < 0.5:
                    continue
                best[x] = (y, n)
            rev = collections.defaultdict(list)
            for x, (y, n) in best.items():
                rev[y].append((n, x))
            for y, xs in rev.items():
                xs.sort(reverse=True)
                if len(xs) == 1 or xs[0][0] > xs[1][0]:
                    added += pair(xs[0][1], y, 'nbr')
            # vtables
            vv = collections.defaultdict(collections.Counter)
            for v, sl in lvt.items():
                for k, f in enumerate(sl):
                    w = l2w.get(f)
                    if w and len(lslot[f]) <= 3 and len(wslot.get(w, ())) <= 3:
                        for (wv, wk) in wslot.get(w, ()):
                            if abs(wk - k) <= 2:
                                vv[v][wv] += 1
            for v, fs in lvf.items():
                for f in fs:
                    w = l2w.get(f)
                    if w and len(wfv.get(w, ())) <= 6:
                        for wv in wfv.get(w, ()):
                            vv[v][wv] += 1
            for v, c in vv.items():
                top = c.most_common(2)
                wv, n = top[0]
                if n < 2 or (len(top) > 1 and top[1][1] == n):
                    continue
                vmap[v] = wv
                cl = [f for f in lvf.get(v, ()) if f not in l2w]
                cw = [f for f in wvf.get(wv, ()) if f not in w2l]
                if len(cl) == 1 and len(cw) == 1:
                    added += pair(cl[0], cw[0], 'ctor')
                sl, ws = lvt[v], wvt[wv]
                # slot alignment by the matched slots: delta per Linux slot from the nearest matched one
                deltas = {}
                for k, f in enumerate(sl):
                    w = l2w.get(f)
                    if w and w in ws:
                        deltas[k] = ws.index(w) - k
                if not deltas:
                    continue
                for k, f in enumerate(sl):
                    if not f or f in l2w:
                        continue
                    kk = min(deltas, key=lambda z: abs(z - k))
                    j = k + deltas[kk]
                    if 0 <= j < len(ws) and len(lslot[f]) <= 2 and len(wslot.get(ws[j], ())) <= 2:
                        added += pair(f, ws[j], 'vslot')
            new += added
            if not added:
                return new

    propagate()

    def win_calls_in(w, target):
        """addresses of E8/E9 rel32 to target inside w's body (contiguous body assumed)"""
        o = pe.off(w)
        if o is None:
            return []
        n = wS.get(w, 0)
        body = pe.b[o:o + n]
        hits = []
        for i in range(len(body) - 4):
            if body[i] in (0xe8, 0xe9):
                d = struct.unpack_from('<i', body, i + 1)[0]
                if w + i + 5 + d == target:
                    hits.append(w + i)
        return hits

    def lin_calls_in(l, target, elf):
        o = l - 0x411000 + 0x11000
        n = lS.get(l, 0)
        body = elf[o:o + n]
        hits = []
        for i in range(len(body) - 4):
            if body[i] in (0xe8, 0xe9):
                d = struct.unpack_from('<i', body, i + 1)[0]
                if l + i + 5 + d == target:
                    hits.append(l + i)
        return hits

    rows = []
    for path in sorted(glob.glob(os.path.join(mod, '*.c'))):
        fn = os.path.basename(path)
        if fn == 'selftest.c':
            continue
        for m in SITE_RE.finditer(open(path).read()):
            nm, k, addr, ctx, _, tgt, _ = m.groups()
            addr, tgt = int(addr, 16), int(tgt, 16)
            wt = l2w.get(tgt)
            r = dict(file=fn, name=nm, kind=k, addr=addr, target=tgt, win='', status='', how=how.get(tgt, ''))
            if k == 'K_ENTRY':
                r['win'] = '%#x' % wt if wt else ''
                r['status'] = 'found' if wt else 'missing'
            elif k in ('K_CALL', 'K_JMP'):
                c, inside = containing(lE, lS, addr)
                wc = l2w.get(c)
                r['caller'] = c
                if wc and wt:
                    hits = win_calls_in(wc, wt)
                    lh = lin_calls_in(c, tgt, elf)
                    if len(hits) == 1:
                        r['win'] = '%#x' % hits[0]; r['status'] = 'found'
                    elif hits and len(hits) == len(lh) and addr in lh:
                        r['win'] = '%#x' % hits[lh.index(addr)]; r['status'] = 'found-nth'
                    elif hits:
                        r['status'] = 'ambiguous(%d calls, linux %d)' % (len(hits), len(lh))
                    else:
                        r['status'] = 'inlined-or-indirect'
                else:
                    r['status'] = 'missing-' + ('both' if not wc and not wt else 'caller' if not wc else 'target')
            elif k == 'K_VSLOT':
                v = max((a for a in lvt if a <= addr and addr < a + 8 * len(lvt[a])), default=None)
                slot = (addr - v) // 8 if v is not None else None
                wv = vmap.get(v)
                r['vtable'] = v; r['slot'] = slot
                if wv is not None:
                    ws = wvt[wv]
                    j = ws.index(wt) if wt in ws else None
                    if j is not None:
                        r['win'] = '%#x' % (wv + 8 * j); r['status'] = 'found'
                    else:
                        r['status'] = 'vtable-only'
                else:
                    r['status'] = 'missing-vtable' if v is not None else 'not-a-linux-vtable'
            rows.append(r)

    with open(os.path.join(out, 'pairs.tsv'), 'w') as f:
        f.write('linux\twindows\thow\n')
        for l, w in sorted(l2w.items()):
            f.write('%#x\t%#x\t%s\n' % (l, w, how[l]))
    with open(os.path.join(out, 'sites.tsv'), 'w') as f:
        f.write('file\tname\tkind\taddr\ttarget\tcaller_or_vtable\tslot\twin\tstatus\ttarget_how\n')
        for r in rows:
            f.write('%s\t%s\t%s\t%#x\t%#x\t%s\t%s\t%s\t%s\t%s\n' % (
                r['file'], r['name'], r['kind'], r['addr'], r['target'],
                '%#x' % r['caller'] if r.get('caller') else ('%#x' % r['vtable'] if r.get('vtable') else ''),
                r.get('slot', '') if r.get('slot') is not None else '', r['win'], r['status'], r['how']))
    hc = collections.Counter(how.values())
    print('functions: linux %d windows %d; matched %d (%s); string anchors %d' % (len(lE), len(wE), len(l2w),
          ', '.join('%s %d' % kv for kv in hc.most_common()), n_str))
    print('vtables: linux %d windows %d matched %d' % (len(lvt), len(wvt), len(vmap)))
    sc = collections.Counter(r['status'].split('(')[0] for r in rows)
    print('sites %d: %s' % (len(rows), ', '.join('%s %d' % kv for kv in sc.most_common())))
    for k in ('K_ENTRY', 'K_CALL', 'K_JMP', 'K_VSLOT'):
        rs = [r for r in rows if r['kind'] == k]
        print('  %-8s %3d found %3d' % (k, len(rs), sum(1 for r in rs if r['status'].startswith('found'))))


if __name__ == '__main__':
    main()
