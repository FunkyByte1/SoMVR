#!/usr/bin/env python3
"""winport_pairdiff.py - side-by-side check of Linux <-> Windows function pairs from the disassembly (no Ghidra).

  python3 -I winport_pairdiff.py PAIRS_IN [OUT_PREFIX]       (PAIRS_IN = ALL: every pair of pairs.tsv)
  WP_DIS_L / WP_DIS_W = whole-text dumps (llvm-objdump -d --no-show-raw-insn | sed 's/<[^>]*>//g; s/[[:space:]]+/ /g') read
  in place of one llvm-objdump run per function (~20x faster for ALL)

PAIRS_IN: lines "linux_addr windows_addr [label]" (hex). For every pair both functions are disassembled with
llvm-objdump (sizes from reference/index*/functions.tsv), the instruction shapes (mnemonic + operand kinds; registers
ignored: SysV vs Win64) are aligned with difflib, and the aligned instructions give
  - field offsets: (linux disp, windows disp) of every non-stack memory operand  -> OUT.disp.tsv
  - globals:       (linux addr, windows addr) of RIP-relative operands (+ the 4/8-byte value when it is .rodata)
  - calls:         (linux target, windows target) checked against pairs.tsv (agree / conflict / new)  -> OUT.calls.tsv
and a verdict line per pair (OUT.tsv): shape ratio, disp agreement, call agreement, float constants shared.
The 'new' call pairs are how an unmatched callee is found from its matched caller.
"""
import bisect, collections, difflib, os, re, struct, subprocess, sys

SOM = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REF = os.path.join(SOM, 'reference')
LBIN = os.path.join(REF, 'bin/linux/ShadowOfMordor')
WBIN = os.path.join(REF, 'bin/ShadowOfMordor.exe')
PAIRS = os.environ.get('WP_PAIRS', os.path.expanduser('~/Projects/flat2vr/ShadowOfMordor/logs/winport/m3/pairs.tsv'))


def tsv(path):
    with open(path) as f:
        next(f)
        for ln in f:
            yield ln.rstrip('\n').split('\t')


def funcs(path):
    starts, sizes = [], {}
    for r in tsv(path):
        a = int(r[0], 16)
        starts.append(a); sizes[a] = int(r[2])
    starts.sort()
    return starts, sizes


class Image:
    def __init__(self, path):
        self.data = open(path, 'rb').read()
        self.segs = []  # (vaddr, size, fileoff, name)
        d = self.data
        if d[:4] == b'\x7fELF':
            phoff, = struct.unpack_from('<Q', d, 0x20)
            phentsize, phnum = struct.unpack_from('<HH', d, 0x36)
            for i in range(phnum):
                p_type, flags, off, va, pa, fsz, msz = struct.unpack_from('<IIQQQQQ', d, phoff + i * phentsize)
                if p_type == 1:
                    self.segs.append((va, fsz, off, 'x' if flags & 1 else 'r' if not flags & 2 else 'w'))
        else:
            pe, = struct.unpack_from('<I', d, 0x3c)
            nsec, = struct.unpack_from('<H', d, pe + 6)
            optsz, = struct.unpack_from('<H', d, pe + 20)
            base, = struct.unpack_from('<Q', d, pe + 24 + 24)
            sh = pe + 24 + optsz
            for i in range(nsec):
                name = d[sh + 40 * i: sh + 40 * i + 8].rstrip(b'\0').decode('latin1')
                vsz, va, rsz, roff = struct.unpack_from('<IIII', d, sh + 40 * i + 8)
                self.segs.append((base + va, rsz, roff, name))

    def read(self, va, n):
        for s, sz, off, nm in self.segs:
            if s <= va and va + n <= s + sz:
                return self.data[off + va - s: off + va - s + n]
        return None


LINE = re.compile(r'^\s*([0-9a-f]+):\s+(\S+)\s*(.*)$')
MEM = re.compile(r'^(-?0x[0-9a-f]+|-?\d+)?\((%\w+)?(?:,(%\w+))?(?:,(\d))?\)$')


def split_ops(s):
    out, depth, cur = [], 0, ''
    for ch in s:
        if ch == '(':
            depth += 1
        elif ch == ')':
            depth -= 1
        if ch == ',' and depth == 0:
            out.append(cur.strip()); cur = ''
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


class DisFile:
    """a whole-text llvm-objdump dump (`addr: mnem ops # comment` per line, <symbols> stripped), indexed by address"""
    def __init__(self, path):
        import array
        self.data = open(path, 'rb').read()
        self.addr, self.off = array.array('Q'), array.array('Q')
        for m in re.finditer(rb'^ *([0-9a-f]+):', self.data, re.M):
            self.addr.append(int(m.group(1), 16)); self.off.append(m.start())

    def lines(self, start, end):
        i, j = bisect.bisect_left(self.addr, start), bisect.bisect_left(self.addr, end)
        if i >= j:
            return []
        e = self.off[j] if j < len(self.off) else len(self.data)
        return self.data[self.off[i]:e].decode('latin1').splitlines()


DIS = {}   # binary path -> DisFile (WP_DIS_L / WP_DIS_W: the dumps; else llvm-objdump per function)


def disasm(binary, start, size):
    if binary in DIS:
        out = DIS[binary].lines(start, start + size)
    else:
        r = subprocess.run(['llvm-objdump', '-d', '--no-show-raw-insn', '--start-address=%#x' % start,
                            '--stop-address=%#x' % (start + size), binary], capture_output=True, text=True)
        out = r.stdout.splitlines()
    ins = []
    for ln in out:
        m = LINE.match(ln)
        if not m:
            continue
        addr, mn, rest = int(m.group(1), 16), m.group(2), m.group(3)
        com = None
        if '#' in rest:
            rest, c = rest.split('#', 1)
            mm = re.search(r'0x[0-9a-f]+', c)
            com = int(mm.group(0), 16) if mm and 'imm' not in c else None
        rest = re.sub(r'<[^>]*>', '', rest).strip()
        ops = split_ops(rest) if rest else []
        kinds, vals = [], []
        for o in ops:
            o2 = o.lstrip('*')
            if o2.startswith('$'):
                kinds.append('I'); vals.append(('imm', int(o2[1:], 0)))
            elif o2.startswith('%') and '(' not in o2:
                kinds.append('R'); vals.append(None)
            else:
                mm = MEM.match(o2.split(':')[-1])
                if mm:
                    disp = int(mm.group(1), 0) if mm.group(1) else 0
                    base = mm.group(2)
                    if base == '%rip':
                        kinds.append('G'); vals.append(('glob', com))
                    elif base in ('%rsp',) or (base is None and mm.group(3) is None):
                        kinds.append('S'); vals.append(('stack', disp))
                    else:
                        kinds.append('M'); vals.append(('disp', disp, mm.group(3) is not None))
                elif re.match(r'^0x[0-9a-f]+$', o2):
                    kinds.append('T'); vals.append(('tgt', int(o2, 16)))
                else:
                    kinds.append('?'); vals.append(None)
        mnb = re.sub(r'(q|l|w|b)$', '', mn) if mn not in ('movsbl', 'movzbl', 'movswl', 'movzwl') else mn
        ins.append((addr, mn, mnb + ' ' + ','.join(kinds), vals))
    return ins


def main():
    pin = sys.argv[1]
    outp = sys.argv[2] if len(sys.argv) > 2 else os.path.splitext(pin)[0]
    for k, b in (('WP_DIS_L', LBIN), ('WP_DIS_W', WBIN)):
        if os.environ.get(k):
            DIS[b] = DisFile(os.environ[k])
    lS, lZ = funcs(os.path.join(REF, 'index/functions.tsv'))
    wS, wZ = funcs(os.path.join(REF, 'index-win64/functions.tsv'))
    l2w = {int(a, 16): int(b, 16) for a, b, _ in tsv(PAIRS)}
    w2l = {b: a for a, b in l2w.items()}
    LI, WI = Image(LBIN), Image(WBIN)

    def fsize(S, Z, a):
        if a in Z:
            return Z[a]
        i = bisect.bisect_right(S, a) - 1
        rest = Z[S[i]] - (a - S[i]) if i >= 0 else 0
        return rest if rest > 0 else -0x800   # not a Ghidra function (a vtable-only target): cut at the int3 pad

    def fl(img, va):
        b = img.read(va, 4) if va else None
        if b is None:
            return None
        v = struct.unpack('<f', b)[0]
        return round(v, 5) if v == v and 1e-6 < abs(v) < 1e7 else None

    fo = open(outp + '.tsv', 'w'); fd = open(outp + '.disp.tsv', 'w'); fc = open(outp + '.calls.tsv', 'w')
    fo.write('label\tlinux\twindows\tnL\tnW\tshape\tdisp_same\tdisp_diff\tcalls_agree\tcalls_conflict\tcalls_new\tfloats_shared\tverdict\n')
    fd.write('label\tlinux\twindows\tlin_ins\twin_ins\tlin_disp\twin_disp\tmnem\n')
    fc.write('label\tlinux\twindows\tlin_ins\twin_ins\tlin_tgt\twin_tgt\tstate\n')
    src = open(pin) if pin != 'ALL' else ('%s %s pair' % (a, b) for a, b, _ in tsv(PAIRS))
    for ln in src:
        p = ln.split()
        if not p or p[0].startswith('#'):
            continue
        la, wa = int(p[0], 16), int(p[1], 16)
        lab = p[2] if len(p) > 2 else ''
        L = disasm(LBIN, la, min(fsize(lS, lZ, la), 0x6000))
        Wd = disasm(WBIN, wa, min(abs(fsize(wS, wZ, wa)), 0x6000))
        if fsize(wS, wZ, wa) < 0:
            cut = next((i for i, x in enumerate(Wd) if x[1] == 'int3'), len(Wd))
            Wd = Wd[:cut]
        sm = difflib.SequenceMatcher(None, [x[2] for x in L], [x[2] for x in Wd], autojunk=False)
        pairs = []
        for tag, i1, i2, j1, j2 in sm.get_opcodes():
            if tag == 'equal' or (tag == 'replace' and i2 - i1 == j2 - j1):
                for k in range(i2 - i1):
                    if L[i1 + k][2].split()[0] == Wd[j1 + k][2].split()[0]:
                        pairs.append((L[i1 + k], Wd[j1 + k]))
        dsame = ddiff = ca = cc = cn = 0
        for a, b in pairs:
            for va, vb in zip(a[3], b[3]):
                if not va or not vb or va[0] != vb[0]:
                    continue
                if va[0] == 'disp' and abs(va[1]) >= 8:
                    if va[1] == vb[1]:
                        dsame += 1
                    else:
                        ddiff += 1
                    fd.write('%s\t%#x\t%#x\t%#x\t%#x\t%#x\t%#x\t%s\n' % (lab, la, wa, a[0], b[0], va[1], vb[1], a[1]))
                elif va[0] == 'tgt' and a[1].startswith('call'):
                    t = l2w.get(va[1])
                    st = 'agree' if t == vb[1] else 'conflict' if t is not None else (
                        'new' if vb[1] not in w2l else 'conflict-w')
                    ca += st == 'agree'; cc += st.startswith('conflict'); cn += st == 'new'
                    fc.write('%s\t%#x\t%#x\t%#x\t%#x\t%#x\t%#x\t%s\n' % (lab, la, wa, a[0], b[0], va[1], vb[1], st))
                elif va[0] == 'glob':
                    fd.write('%s\t%#x\t%#x\t%#x\t%#x\tG%s\tG%s\t%s\n' % (lab, la, wa, a[0], b[0],
                             '%#x' % va[1] if va[1] else '?', '%#x' % vb[1] if vb[1] else '?', a[1]))
        lf = collections.Counter(f for f in (fl(LI, v[1]) for x in L for v in x[3] if v and v[0] == 'glob') if f)
        wf = collections.Counter(f for f in (fl(WI, v[1]) for x in Wd for v in x[3] if v and v[0] == 'glob') if f)
        fsh = sum((lf & wf).values())
        shape = sm.ratio()
        dtot = dsame + ddiff
        v = 'same' if shape >= 0.6 and (dtot == 0 or dsame / dtot >= 0.7) and cc == 0 else \
            'likely' if shape >= 0.4 and cc <= 1 else 'weak'
        fo.write('%s\t%#x\t%#x\t%d\t%d\t%.2f\t%d\t%d\t%d\t%d\t%d\t%d\t%s\n' % (
            lab, la, wa, len(L), len(Wd), shape, dsame, ddiff, ca, cc, cn, fsh, v))
    fo.close(); fd.close(); fc.close()


if __name__ == '__main__':
    main()
