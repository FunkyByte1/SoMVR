"""winport_exe_map.py OLD.exe NEW.exe OUT.tsv [--tsv-dir mod/somvr/win] - carry the Windows tables from one exe to
another build of the same game (the Steam-Windows exe -> the GOG exe; docs/re-winport.md 5, 7.1). Read-only.

Both exes are disassembled (llvm-objdump), cut into functions (.pdata starts + direct call targets), each function
normalised (its own branch targets as offsets, every other address as a placeholder) and hashed. Functions whose
hash is unique on both sides are paired; then pairs spread through the aligned references of paired functions (the
n-th reference of a function points to the n-th reference's pair). Data references vote an old->new data map.

Every row of win/sites-*.tsv (live kinds) and every global of win/offsets-*.tsv is translated:
  code RVAs: the same offset in the paired function (paired functions are identical instruction for instruction);
  vtable slots: the nearest mapped data reference below (a vtable's LEA) + the same offset, checked: the new slot must
  hold the new pair of the old slot's function; globals: the data map (exact, else the nearest key within 0x100).
OUT.tsv: name, kind, old rva, new rva, new ctx_rva, new target_rva, status (ok / unpaired / unchecked / bad). The
generator takes it with `wintables.py --exe NEW.exe --map OUT.tsv`.
"""
import argparse, bisect, collections, glob, hashlib, os, re, struct, subprocess, sys

BASE = 0x140000000


class PE:
    def __init__(self, path):
        self.path = path
        self.b = open(path, 'rb').read()
        pe = struct.unpack_from('<I', self.b, 0x3c)[0]
        nsec = struct.unpack_from('<H', self.b, pe + 6)[0]
        opt = struct.unpack_from('<H', self.b, pe + 20)[0]
        self.base = struct.unpack_from('<Q', self.b, pe + 24 + 24)[0]
        self.secs = []
        o = pe + 24 + opt
        for _ in range(nsec):
            n = self.b[o:o + 8].rstrip(b'\0').decode(errors='replace')
            vs, va, rs, rp = struct.unpack_from('<IIII', self.b, o + 8)
            self.secs.append((n, va, vs, rp, rs))
            o += 40
        rrva, rsz = struct.unpack_from('<II', self.b, pe + 24 + 112 + 5 * 8)  # the base relocations: DIR64 sites
        self.dir64 = []
        end = rrva + rsz
        while rrva and rrva + 8 <= end:
            page, bs = struct.unpack('<II', self.read(rrva, 8))
            if bs < 8:
                break
            for i in range((bs - 8) // 2):
                e = struct.unpack('<H', self.read(rrva + 8 + 2 * i, 2))[0]
                if e >> 12 == 10:
                    self.dir64.append(page + (e & 0xfff))
            rrva += bs
        prva, psz = struct.unpack_from('<II', self.b, pe + 24 + 112 + 3 * 8)  # the exception directory
        self.pdata = []
        for i in range(psz // 12):
            beg, end, _ = struct.unpack('<III', self.read(prva + 12 * i, 12))
            if beg and end > beg:
                self.pdata.append((beg, end))

    def read(self, rva, n):
        for _, va, vs, rp, rs in self.secs:
            if va <= rva and rva + n <= va + min(vs, rs):
                return self.b[rp + rva - va:rp + rva - va + n]
        return None

    def sec_of(self, rva):
        for s in self.secs:
            if s[1] <= rva < s[1] + max(s[2], s[4]):
                return s[0]
        return None

    def q(self, rva):
        b = self.read(rva, 8)
        return struct.unpack('<Q', b)[0] if b else None


LINE = re.compile(r'^\s*([0-9a-f]+):\s+(\S+)\s*(.*)$')
RIP = re.compile(r'-?0x[0-9a-f]+\(%rip\)')
ABS = re.compile(r'0x(1[0-9a-f]{8})\b(?: <[^>]*>)?')


def disasm(pe):
    """[(rva, mnemonic, operands, ref_rva or None)] for .text, in address order"""
    out = []
    cmd = ['llvm-objdump', '-d', '--no-show-raw-insn', '--section=.text', pe.path]
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, text=True, bufsize=1 << 20)
    for line in p.stdout:
        m = LINE.match(line)
        if not m:
            continue
        a = int(m.group(1), 16) - pe.base
        mn, ops = m.group(2), m.group(3)
        ref = None
        if '#' in ops:
            ops, com = ops.split('#', 1)
            mm = re.search(r'0x([0-9a-f]+)', com)
            if mm and '(%rip)' in ops:
                ref = int(mm.group(1), 16) - pe.base
        mm = ABS.search(ops)
        if mm and ref is None:
            ref = int(mm.group(1), 16) - pe.base
        out.append((a, mn, ops.strip(), ref))
    p.wait()
    return out


def functions(pe, ins):
    starts = {b for b, _ in pe.pdata}
    ends = dict(pe.pdata)
    for a, mn, ops, ref in ins:  # direct call targets
        if ref is not None and ((mn.startswith('call') and not ops.startswith('*')) or mn.startswith('lea')):
            starts.add(ref)   # direct call targets; code addresses taken by LEA (callbacks, thunks)
    for r in pe.dir64:   # code pointers in the data (vtables, tables): function starts too
        if pe.sec_of(r) in ('.rdata', '.data'):
            v = pe.q(r)
            if v and pe.sec_of(v - pe.base) == '.text':
                starts.add(v - pe.base)
    addr_set = {i[0]: i[1] for i in ins}
    for b, e in pe.pdata:   # code after a pdata range that no other start covers (stubs, tail chunks): its own piece
        if e not in starts and addr_set.get(e, 'int3') != 'int3':
            starts.add(e)
    starts = sorted(s for s in starts if pe.sec_of(s) == '.text')
    addrs = [i[0] for i in ins]
    funcs = []
    for k, s in enumerate(starts):
        e = min(ends.get(s, 1 << 40), starts[k + 1] if k + 1 < len(starts) else 1 << 40)
        i0, i1 = bisect.bisect_left(addrs, s), bisect.bisect_left(addrs, e)
        funcs.append((s, e, i0, i1))
    return funcs


def norm(ins, f):
    s, e, i0, i1 = f
    parts, refs = [], []
    for a, mn, ops, ref in ins[i0:i1]:
        if mn == 'int3':
            continue
        o = RIP.sub('R', ops)
        if ref is not None:
            if s <= ref < e:
                o = ABS.sub('L%x' % (ref - s), o)
            else:
                o = ABS.sub('A', o)
                refs.append((a - s, ref, mn.startswith(('call', 'jmp')) and '(%rip)' not in ops))
        parts.append(mn + ' ' + o)
    return hashlib.sha1('\n'.join(parts).encode()).hexdigest(), refs


def build(pe):
    ins = disasm(pe)
    funcs = functions(pe, ins)
    info = {}
    for f in funcs:
        h, refs = norm(ins, f)
        info[f[0]] = (f[1], h, refs)
    return info


def pair_all(old, new, po, pn):
    by_h = [collections.defaultdict(list), collections.defaultdict(list)]
    for i, d in enumerate((old, new)):
        for s, (_, h, _) in d.items():
            by_h[i][h].append(s)
    pairs = {}
    for h, l in by_h[0].items():   # unique on both sides; else the same count on both: in address order
        r = by_h[1].get(h)
        if r and len(l) == len(r) and len(l) <= 64:
            for x, y in zip(sorted(l), sorted(r)):
                pairs[x] = y
    data = collections.defaultdict(collections.Counter)
    done_vt = set()
    todo = list(pairs)
    seen = set()
    while todo:
        s = todo.pop()
        if s in seen:
            continue
        seen.add(s)
        t = pairs[s]
        ra, rb = old[s][2], new[t][2]
        if old[s][1] != new[t][1] or len(ra) != len(rb):
            continue
        for (_, x, code), (_, y, _) in zip(ra, rb):
            if code:
                if x in old and y in new and x not in pairs and old[x][1] == new[y][1]:
                    pairs[x] = y
                    todo.append(x)
                continue
            data[x][y] += 1
            if (x, y) in done_vt or po.sec_of(x) != '.rdata':
                continue
            done_vt.add((x, y))   # a vtable (or a table of code pointers): its slots pair their functions
            for k in range(512):
                fx, fy = po.q(x + 8 * k), pn.q(y + 8 * k)
                if fx is None or fy is None or not (BASE < fx < BASE + 0x2000000 and BASE < fy < BASE + 0x2000000):
                    break
                fx, fy = fx - BASE, fy - BASE
                if fx in old and fy in new and fx not in pairs and old[fx][1] == new[fy][1]:
                    pairs[fx] = fy
                    todo.append(fx)
    dmap = {x: c.most_common(1)[0][0] for x, c in data.items()}
    # the duplicates (identical code, e.g. 'lea rcx, X; jmp Y' thunks): told apart by what they reference, mapped
    rev = set(pairs.values())
    for rnd in range(3):
        new_by_sig = {}
        for h, r in by_h[1].items():
            for y in r:
                if y not in rev:
                    sig = (h, tuple(t for _, t, _ in new[y][2]))
                    new_by_sig[sig] = None if sig in new_by_sig else y
        added = 0
        for h, l in by_h[0].items():
            for x in l:
                if x in pairs or not old[x][2]:
                    continue
                m = tuple(pairs.get(t, dmap.get(t)) for _, t, _ in old[x][2])
                if None in m:
                    continue
                y = new_by_sig.get((h, m))
                if y is not None and y not in rev:
                    pairs[x] = y
                    rev.add(y)
                    added += 1
        if not added:
            break
    return pairs, dmap


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('old')
    ap.add_argument('new')
    ap.add_argument('out')
    ap.add_argument('--tsv-dir', default=os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'mod', 'somvr',
                                                       'win'))
    a = ap.parse_args()
    po, pn = PE(a.old), PE(a.new)
    old, new = build(po), build(pn)
    pairs, dmap = pair_all(old, new, po, pn)
    print('winport_exe_map: %d / %d functions, %d paired, %d data references mapped' %
          (len(old), len(new), len(pairs), len(dmap)))
    ostarts = sorted(old)
    dkeys = sorted(dmap)

    def code(x):
        k = bisect.bisect_right(ostarts, x) - 1
        if k < 0:
            return None
        s = ostarts[k]
        if x >= old[s][0] or s not in pairs:
            return None
        return pairs[s] + (x - s)

    def data(x, near=0x100):
        if x in dmap:
            return dmap[x], 'ok'
        k = bisect.bisect_right(dkeys, x) - 1
        if k >= 0 and x - dkeys[k] < near:
            return dmap[dkeys[k]] + (x - dkeys[k]), 'unchecked'
        return None, 'unpaired'

    rows, st = [], collections.Counter()
    for path in sorted(glob.glob(os.path.join(a.tsv_dir, 'sites-*.tsv'))):
        lines = [l.rstrip('\n').split('\t') for l in open(path) if l.strip() and not l.startswith('#')]
        hdr = lines[0]
        for c in lines[1:]:
            r = dict(zip(hdr, c))
            if r['kind'] not in ('call', 'jmp', 'vslot', 'entry') or r['confidence'] not in ('confirmed', 'plausible'):
                continue
            rva = int(r['rva'], 16)
            ctx = rva if r['ctx_rva'] == '-' else int(r['ctx_rva'], 16)
            if r['kind'] == 'vslot':
                k = bisect.bisect_right(dkeys, rva) - 1
                nrv, status = (None, 'unpaired')
                if k >= 0 and rva - dkeys[k] < 0x800:
                    nrv = dmap[dkeys[k]] + (rva - dkeys[k])
                    of, nf = (po.q(rva) or 0) - BASE, (pn.q(nrv) or 0) - BASE
                    same = of in old and nf in new and old[of][1] == new[nf][1]   # the same code (a shared stub)
                    status = 'ok' if pairs.get(of) == nf or (of not in pairs and same) else \
                        'unchecked' if of not in pairs else 'bad'
                nt = (pn.q(nrv) - BASE) if nrv is not None and status == 'ok' else None
                nctx = nrv - (rva - ctx) if nrv is not None else None
            else:
                nrv = code(rva)
                nctx = code(ctx) if nrv is not None else None
                t = int(r['target_rva'], 16) if r['target_rva'] != '-' else None
                nt = code(t) if t is not None else None
                status = 'ok' if nrv is not None and nctx is not None and (t is None or nt is not None) else 'unpaired'
            st[status] += 1
            rows.append((r['name'], r['kind'], r['rva'], nrv, nctx, nt, status))
    for path in sorted(glob.glob(os.path.join(a.tsv_dir, 'offsets-*.tsv'))):
        lines = [l.rstrip('\n').split('\t') for l in open(path) if l.strip() and not l.startswith('#')]
        hdr = lines[0]
        for c in lines[1:]:
            r = dict(zip(hdr, c))
            if r['kind'] != 'global' or r['windows'] in ('?', '-'):
                continue
            nv, status = data(int(r['windows'], 16))
            if nv is None:
                nv, status = (code(int(r['windows'], 16)), 'ok') if code(int(r['windows'], 16)) is not None else (None,
                                                                                                            status)
            st['global ' + status] += 1
            rows.append((r['name'], 'global', r['windows'], nv, None, None, status))
    with open(a.out, 'w') as o:
        o.write('name\tkind\told_rva\trva\tctx_rva\ttarget_rva\tstatus\n')
        for n, k, orv, nrv, nctx, nt, s in rows:
            f = lambda v: '%#x' % v if v is not None else '-'
            o.write('%s\t%s\t%s\t%s\t%s\t%s\t%s\n' % (n, k, orv, f(nrv), f(nctx), f(nt), s))
    print('winport_exe_map: rows %s -> %s' % (', '.join('%s %d' % kv for kv in sorted(st.items())), a.out))
    return 0 if not st['bad'] else 1


if __name__ == '__main__':
    sys.exit(main())
