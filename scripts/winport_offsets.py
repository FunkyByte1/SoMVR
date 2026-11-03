#!/usr/bin/env python3
"""winport_offsets.py - the shared offset-diff tool of the Windows port (docs/re-winport.md 2.2 and 7.6).

For matched Linux <-> Windows function pairs it lines the two decompiles up token by token and reads, for every
struct offset, global and vtable byte offset the Linux code uses, the value the Windows code has at the same place
(the 2.2 method: the same code with shifted constants). Read-only on everything it is given; no Ghidra of its own
(it writes the decompile batches, you run them through ghidra_query.sh, which takes the lock and the gate).

  python3 -I winport_offsets.py prep OUT FILE.c... [--fn 0xLIN[=0xWIN]]... [--cache FILE.out]...
      scans the mod files (their #define hex values, the `+ 0x..` literals in code, the SITE lines, the functions
      their comments name: FUN_xxxxxxxx, Class::vf_NN, code addresses) -> OUT/scan.tsv, OUT/plan.tsv (each Linux
      function, its Windows pair, why it is there), OUT/lin.batch and OUT/win.batch (only what no cache has yet)
  bash scripts/ghidra_query.sh som-linux-q2 OUT/lin.out @OUT/lin.batch     (any free som-linux* copy)
  bash scripts/ghidra_query.sh som-win64   OUT/win.out @OUT/win.batch
  python3 -I winport_offsets.py diff OUT [--group NAME] [--cache FILE.out]...
      -> OUT/occ.tsv (every Linux use of a scanned value in a decompiled pair and the aligned Windows token),
         OUT/offsets-NAME.tsv (one row per offset/global in win/README.md 1.2's columns: the Windows value + the
         evidence; a draft for mod/somvr/win/offsets-NAME.tsv: "same" = every aligned use unchanged, "plausible" =
         moved, read it by hand before "confirmed"; inline literals are named inline:+0x.. - give them an OFS name), OUT/newpairs.tsv (callees and globals the alignment pairs that pairs.tsv
         has not: votes, for finding the sites left)
  python3 -I winport_offsets.py pair OUT 0xLIN 0xWIN [--all] [--cache FILE.out]...
      prints every aligned constant of one pair that differs (--all: every aligned offset) - for reading by hand
  python3 -I winport_offsets.py verify OUT [TAG:]0xLIN=0xWIN...
      one line per pair: the aligned share of the Linux tokens, offsets same/moved/unaligned, paired calls aligned
      (side-by-side evidence for a tier-1 pair; a wrong pair aligns little)
  python3 -I winport_offsets.py show 0xADDR...
      prints decompiles from everyone's caches (Linux or Windows by the address)
  python3 -I winport_offsets.py bytes 0xWINVA [N]
      the exe's bytes at a Windows VA (from the file: reference/bin/ShadowOfMordor.exe) and its RVA

The alignment: both decompiles are tokenised; locals, types (long/longlong, undefined4/int, ...), struct offsets
and data symbols are normalised; calls become the Windows address of the pair (pairs.tsv) or "unpaired"; then
difflib lines up the lines and, inside changed line runs, the tokens. A Linux offset token inside an aligned run
reads its Windows twin ("eq"); inside a changed run of the same length it is read by position ("pos", weaker).
Byte offsets are computed from the access (`*(T *)(x + 0x10)` = 0x10, `p[2]` with `long *p` = 0x10); a twin
whose access type cannot be the same field (a vtable call vs a field read, 4 vs 8 bytes) is dropped. Each use is
scoped to the game functions named where the mod uses it (a #define's own comment, else the comments and code of
the mod function using it, with the comment block above it); unscoped values pool all the file's pairs. Status
per value (>= 3/4 of the aligned uses agree): same / moved (one other value) / rebased? (moved by > 0x80: the
Windows code reads it through another base pointer - or a wrong twin; by hand) / same? moved? (only weak
alignments: single generic lines, short token runs) / conflict (the value names several fields - read the uses) /
unaligned (used, never aligned: by hand; `near` lists Windows offsets of a compatible access type close by) /
absent (no decompiled pair uses it: add the function that does with --fn).

Defaults (env SOM = ~/Projects/flat2vr/ShadowOfMordor): pairs = logs/winport/m3/pairs.tsv, overridden by the
confirmed/plausible rows of this checkout's mod/somvr/win/sites-*.tsv (their target_rva pairs the SITE's Linux target), indexes reference/index and reference/index-win64, the exe
reference/bin/ShadowOfMordor.exe (image base 0x140000000; RVAs = VA - base).
"""
import bisect, collections, difflib, glob, os, re, struct, sys

SOM = os.environ.get('SOM', os.path.expanduser('~/Projects/flat2vr/ShadowOfMordor'))
PAIRS = os.path.join(SOM, 'logs/winport/m3/pairs.tsv')
LIDX = os.path.join(SOM, 'reference/index')
WIDX = os.path.join(SOM, 'reference/index-win64')
EXE = os.path.join(SOM, 'reference/bin/ShadowOfMordor.exe')
WINDIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'mod/somvr/win')
WBASE = 0x140000000
GLOBAL_MIN = 0x400000          # Linux values >= this are addresses (globals, code), below it struct offsets


def tsv(path):
    with open(path) as f:
        next(f)
        for ln in f:
            yield ln.rstrip('\n').split('\t')


# ---------------------------------------------------------------------------------------------------- indexes
class Index:
    def __init__(self, d):
        self.entries, self.size, self.name, self.byname = [], {}, {}, {}
        for a, n, s, *_ in tsv(os.path.join(d, 'functions.tsv')):
            a = int(a, 16)
            self.entries.append(a); self.size[a] = int(s); self.name[a] = n
            self.byname.setdefault(n, a)
        self.entries.sort()

    def containing(self, a):
        i = bisect.bisect_right(self.entries, a) - 1
        if i >= 0:
            e = self.entries[i]
            if e <= a < e + max(self.size[e], 1):
                return e
        return None


def load_pairs(extra=()):
    l2w = {}
    if os.path.exists(PAIRS):
        for a, b, *_ in tsv(PAIRS):
            l2w[int(a, 16)] = int(b, 16)
    # the helpers' answers (mod/somvr/win/sites-*.tsv, win/README.md 1.1): a confirmed/plausible row's target_rva
    # pairs the Linux SITE's target (from the SITE line in the sources)
    lin_t = {}
    for src in glob.glob(os.path.join(os.path.dirname(WINDIR), 'somvr*.c')):
        for m in SITE.finditer(open(src, errors='replace').read()):
            lin_t[m.group(1)] = int(m.group(5), 16)
    for p in glob.glob(os.path.join(WINDIR, 'sites-*.tsv')):
        try:
            lines = [ln.rstrip('\n').split('\t') for ln in open(p) if ln.strip() and not ln.startswith('#')]
        except OSError:
            continue
        if not lines:
            continue
        col = {h: i for i, h in enumerate(lines[0])}
        if not {'name', 'target_rva', 'confidence'} <= set(col):
            continue
        for r in lines[1:]:
            try:
                if r[col['confidence']] in ('confirmed', 'plausible') and r[col['name']] in lin_t \
                        and r[col['target_rva']].startswith('0x'):
                    l2w[lin_t[r[col['name']]]] = WBASE + int(r[col['target_rva']], 16)
            except (IndexError, ValueError):
                pass
    for l, w in extra:
        l2w[l] = w
    return l2w


# ---------------------------------------------------------------------------------------------------- mod scan
DEFINE = re.compile(r'^\s*#\s*define\s+(\w+)\s+\(?\s*(0x[0-9a-fA-F]+)[uUlL]*\s*\)?\s*(?:/\*(.*?)(?:\*/|$))?')
SITE = re.compile(r'SITE\(\s*"(\w+)"\s*,\s*(K_\w+)\s*,\s*(0x[0-9a-fA-F]+)\s*,\s*(0x[0-9a-fA-F]+)\s*,\s*"[^"]*"\s*,'
                  r'\s*(0x[0-9a-fA-F]+)')
FUNREF = re.compile(r'\b(?:thunk_)?FUN_([0-9a-f]{8})\b')
VFREF = re.compile(r'\b([A-Z]\w+::vf_\d+)\b')
ADDRREF = re.compile(r'\b0x([0-9a-f]{6,7})\b')
INLINE = re.compile(r'(?<![\w.])[+]\s*(0x[0-9a-fA-F]+|\d+)\b(?!\s*[*.])')
USE = re.compile(r'[+]\s*([A-Z][A-Z0-9_]+)\b')


def strip_c(text):
    """comments and string literals blanked (newlines kept, so line numbers stay)"""
    def blank(m):
        return re.sub(r'[^\n]', ' ', m.group(0))
    return re.sub(r'/\*.*?\*/|//[^\n]*|"(?:[^"\\\n]|\\.)*"|\'(?:[^\'\\\n]|\\.)*\'', blank, text, flags=re.S)


def line_refs(ln, lidx):
    out = set()
    for m in FUNREF.finditer(ln):
        e = lidx.containing(int(m.group(1), 16))
        if e:
            out.add(e)
    for m in VFREF.finditer(ln):
        a = lidx.byname.get(m.group(1))
        if a:
            out.add(a)
    for m in ADDRREF.finditer(ln):
        a = int(m.group(1), 16)
        if 0x411000 <= a < 0x3300000:
            e = lidx.containing(a)
            if e:
                out.add(e)
    return out


def scan_file(path, lidx):
    text = open(path, errors='replace').read()
    code = strip_c(text)
    base = os.path.basename(path)
    items = []      # (kind, name, value, line, uses, note)
    funcs = []      # (linux fn, why)
    defs = {}
    lines = text.split('\n')
    for n, ln in enumerate(lines, 1):
        m = DEFINE.match(ln)
        if m:
            v = int(m.group(2), 16)
            defs[m.group(1)] = v
            items.append(['global' if v >= GLOBAL_MIN else 'offset', m.group(1), v, n, [], (m.group(3) or '').strip()])
        for m in SITE.finditer(ln):
            nm, k, a, _c, t = m.group(1), m.group(2), int(m.group(3), 16), m.group(4), int(m.group(5), 16)
            e = lidx.containing(t)
            funcs.append((e or t, 'site:%s:target' % nm))
            if k in ('K_CALL', 'K_JMP'):
                c = lidx.containing(a)
                if c:
                    funcs.append((c, 'site:%s:caller' % nm))
    # the functions the comments name
    for n, ln in enumerate(lines, 1):
        if 'SITE(' not in ln:
            for e in line_refs(ln, lidx):
                funcs.append((e, 'comment:%d' % n))
    # the mod's own C functions (with the comment block above each): the game functions they name scope their uses
    seg_of, refs_of = {}, []
    start = None
    for n, ln in enumerate(lines, 1):
        if start is None and re.match(r'^[A-Za-z_][\w \*]*\b\w+\s*\(', ln) and not ln.rstrip().endswith(';') \
                and not ln.startswith(('SITE', 'static const', 'typedef')):
            start = n
            while start > 1 and lines[start - 2].strip().startswith(('/*', '*', '//')):
                start -= 1
        elif start is not None and ln.startswith('}'):
            refs = set()
            for k in range(start, n + 1):
                refs |= line_refs(lines[k - 1], lidx)
            refs_of.append(refs)
            for k in range(start, n + 1):
                seg_of[k] = len(refs_of) - 1
            start = None

    def scope(ns):
        out = set()
        for k in ns:
            if k in seg_of:
                out |= refs_of[seg_of[k]]
        return out
    # uses: `+ NAME` of the defines, `+ 0x..` literals in code
    inline = collections.OrderedDict()
    for n, ln in enumerate(code.split('\n'), 1):
        if 'SITE(' in lines[n - 1] or DEFINE.match(lines[n - 1]):
            continue
        for m in USE.finditer(ln):
            if m.group(1) in defs:
                for it in items:
                    if it[1] == m.group(1):
                        it[4].append(n)
        for m in INLINE.finditer(ln):
            v = int(m.group(1), 0)
            if v < 4 or v >= GLOBAL_MIN:
                continue
            inline.setdefault(v, []).append(n)
    for v, ns in inline.items():
        items.append(['offset', '+%#x' % v, v, ns[0], ns, 'inline'])
    for it in items:            # the scope: a define's own comment's functions, else those of the mod code using it
        own = line_refs(it[5], lidx) if it[5] != 'inline' else set()
        it.append(own or scope(it[4]))
    # comment-named functions per define line (a define's evidence prefers them)
    return base, items, funcs


# ---------------------------------------------------------------------------------------------------- decompiles
def parse_out(paths):
    """'### decompile' sections of ghidra_query.sh outputs -> {entry: (name, text)}"""
    d = {}
    for p in paths:
        if not os.path.exists(p):
            continue
        cur, buf = None, []

        def flush():
            if cur is not None and buf:
                m = re.match(r'// (.*?) @ ([0-9a-f]+)\s*$', buf[0])
                if m:
                    d.setdefault(int(m.group(2), 16), (m.group(1), ''.join(buf[1:])))
        for ln in open(p, errors='replace'):
            if ln.startswith('### '):
                flush()
                cur, buf = (ln if ln.startswith('### decompile') else None), []
            elif cur is not None:
                if buf or ln.startswith('// '):
                    buf.append(ln)
        flush()
    return d


TOK = re.compile(r'"(?:[^"\\]|\\.)*"|\'(?:[^\'\\]|\\.)*\'|0x[0-9a-fA-F]+|\d+\.\d+(?:[eE][-+]?\d+)?|\d+'
                 r'|[A-Za-z_~][\w]*(?:::[~\w]+)*|->|==|!=|<=|>=|&&|\|\||<<|>>|\+\+|--|\S')
TYPES = {}
for _t in 'char byte uchar bool undefined undefined1 sbyte'.split():
    TYPES[_t] = 'T1'
for _t in 'short ushort undefined2 wchar16'.split():
    TYPES[_t] = 'T2'
for _t in 'int uint undefined4 dword'.split():
    TYPES[_t] = 'T4'
for _t in 'long ulong longlong ulonglong undefined8 qword'.split():
    TYPES[_t] = 'T8'
TYPES.update(float='F4', double='F8', code='code', void='void', undefined3='T3', undefined5='T5',
             undefined6='T6', undefined7='T7', float10='F10', wchar_t='TW', undefined16='T16')
SIZES = dict(T1=1, T2=2, T3=3, T4=4, T5=5, T6=6, T7=7, T8=8, F4=4, F8=8, F10=10, T16=16)
KEYWORDS = set('if else while do for return break continue goto switch case default sizeof true false NULL'.split())
OPEN = set('( [ , = ! ~ & | ^ + - * / % < > ? : ; { } return == != <= >= && || <<'.split())
DATA = re.compile(r'^(?:_*(?:DAT|PTR|PTR_DAT|PTR_FUN|PTR_LAB|PTR_PTR|s|u|ds|BYTE|WORD|DWORD|QWORD|float|double|'
                  r'pointer|uRam|bRam|iRam|cRam|lRam|fRam|dRam|sRam|pcRam|plRam|puRam|ppcRam)_.*?|.*::(?:vtable|vftable|'
                  r'typeinfo).*)$')
SYMADDR = re.compile(r'(?:^|_)([0-9a-f]{7,16})$')


class Fn:
    """a decompile tokenised: raw tokens per line, the declarations, the side's call mapping"""

    def __init__(self, text, side, names, l2w, w2l):
        body = re.sub(r'/\*.*?\*/', ' ', text, flags=re.S)
        self.decl = {}
        for m in re.finditer(r'([A-Za-z_]\w*)\s*(\*+)\s*([A-Za-z_]\w*)\s*(?=[;,)\[])', body):
            t, st, v = m.group(1), m.group(2), m.group(3)
            if t not in KEYWORDS:
                self.decl.setdefault(v, (TYPES.get(t, t), len(st)))
        for m in re.finditer(r'^\s*([A-Za-z_]\w*)\s+([A-Za-z_]\w*)\s*;', body, re.M):
            self.decl.setdefault(m.group(2), (TYPES.get(m.group(1), m.group(1)), 0))
        sig = re.search(r'^[^\n(]*\(([^)]*)\)\s*$', body, re.M)
        if sig:
            for p in sig.group(1).split(','):
                p = p.strip().split()
                if len(p) == 2:
                    self.decl.setdefault(p[1], (TYPES.get(p[0], p[0]), 0))
        self.lines = []          # [(raw tokens, norm tokens)]
        self.flat = []           # raw tokens of the whole function (for context)
        for ln in body.split('\n'):
            raw = TOK.findall(ln)
            if not raw:
                continue
            self.lines.append(raw)
        self.flat = [t for r in self.lines for t in r]
        self.side, self.names, self.l2w, self.w2l = side, names, l2w, w2l
        self.norm = [self.normalise(i) for i in range(len(self.flat))]

    def callee(self, t):
        m = re.match(r'^(?:thunk_)?FUN_([0-9a-f]+)$', t)
        a = int(m.group(1), 16) if m else self.names.get(t)
        return a

    def is_offpos(self, i):
        R = self.flat
        if i >= 1 and R[i - 1] == '+' and i >= 2 and (re.match(r'^[A-Za-z_]\w*$', R[i - 2]) or R[i - 2] in ')]'):
            return True
        return i >= 1 and R[i - 1] == '[' and i + 1 < len(R) and R[i + 1] == ']'

    def normalise(self, i):
        t = self.flat[i]
        c = t[0]
        if c.isdigit():
            return 'H' if '.' not in t and self.is_offpos(i) else t
        if c in '"\'':
            return t
        if not (c.isalpha() or c in '_~'):
            return t
        if t in KEYWORDS:
            return t
        if t in TYPES:
            return TYPES[t]
        a = self.callee(t)
        if a is not None and (i + 1 < len(self.flat) and self.flat[i + 1] == '(' or t.startswith(('FUN_', 'thunk_FUN_'))):
            if self.side == 'l':
                w = self.l2w.get(a)
                return 'F:%x' % w if w else 'F?'
            return 'F:%x' % a if a in self.w2l else 'F?'
        if DATA.match(t):
            return 'D'
        if re.match(r'^[a-z]{1,6}Var\d+$', t):
            return 'v'
        if re.match(r'^(?:local_|a\w*Stack_|\w*Stack_|in_stack_)', t):
            return 'loc'
        if re.match(r'^(?:in_|extraout_|unaff_|register0x|UNRECOVERED_JUMPTABLE)', t):
            return 'reg'
        if t.startswith('param_') or t.startswith(('LAB_', 'switchD', 'caseD')):
            return t if t.startswith('param_') else 'lab'
        return 'N'

    # ---- reading one constant
    def deref_count(self, j):
        n = 0
        while j >= 1 and self.flat[j] == '*' and (j == 0 or self.flat[j - 1] in OPEN):
            n += 1; j -= 1
        return n

    def elsize(self, var, deref=0):
        if var not in self.decl:
            return 1                    # not declared = a global (DAT_x + n): byte arithmetic
        b, d = self.decl[var]
        d -= deref
        if d >= 2:
            return 8
        if d == 1:
            return SIZES.get(b)
        return 1

    def byteval(self, i):
        """(byte value, form) of the integer token i"""
        R = self.flat
        t = R[i]
        v = int(t, 0) if not (len(t) > 1 and t[0] == '0' and t[1].isdigit()) else int(t)
        if i >= 2 and R[i - 1] == '[' and i + 1 < len(R) and R[i + 1] == ']':
            s = self.elsize(R[i - 2]) if re.match(r'^[A-Za-z_]\w*$', R[i - 2]) else None
            return (v * s if s else None), 'idx'
        if i >= 2 and R[i - 1] == '+':
            if re.match(r'^[A-Za-z_]\w*$', R[i - 2]):
                if i >= 3 and R[i - 3] == ')':          # (cast)var + n
                    q = i - 3; depth = 0
                    while q >= 0:
                        depth += R[q] == ')'; depth -= R[q] == '('
                        if depth == 0:
                            break
                        q -= 1
                    inner = R[q + 1:i - 3]
                    if inner and inner[-1] == '*' and len(inner) >= 2:
                        st = 0
                        while inner[-1 - st] == '*' and st < len(inner) - 1:
                            st += 1
                        s = 8 if st >= 2 else SIZES.get(TYPES.get(inner[0], inner[0]))
                        return (v * s if s else None), 'ptr'
                    return v, 'byte'
                s = self.elsize(R[i - 2], self.deref_count(i - 3))
                return (v * s if s else None), ('byte' if s == 1 else 'ptr')
            return v, 'byte'
        return v, 'const'

    def access(self, i):
        """the type a constant's expression is read as: `*(float *)(x + c)` -> F4, `p[c]` -> p's type"""
        R = self.flat
        if i >= 2 and R[i - 1] == '[':
            d = self.decl.get(R[i - 2])
            return ('%s%s' % (d[0], '*' * (d[1] - 1))) if d else ''
        q, depth = i - 1, 0
        while q >= 0:
            if R[q] == ')':
                depth += 1
            elif R[q] == '(':
                if depth == 0:
                    break
                depth -= 1
            q -= 1
        if q >= 1 and R[q - 1] == ')':
            p, depth = q - 1, 0
            while p >= 0:
                depth += R[p] == ')'; depth -= R[p] == '('
                if depth == 0:
                    break
                p -= 1
            inner = R[p + 1:q - 1]
            if p >= 1 and R[p - 1] == '*' and inner and inner[-1] == '*':
                ty = [TYPES.get(x, x) for x in inner[:-1]]
                acc = ''.join(ty).replace('*', '')
                acc += '*' * sum(1 for x in inner[:-1] if x == '*')
                if acc.startswith('code') and len(inner) >= 3:
                    return 'vcall'
                return acc
        return ''

    def ctx(self, i, before=9, after=3):
        return ' '.join(self.flat[max(0, i - before):i + after + 1])


def align(L, W):
    """{linux token index: (windows token index, 'eq'|'pos')}"""
    def keys(F):
        out, k = [], 0
        for raw in F.lines:
            out.append((k, k + len(raw)))
            k += len(raw)
        return out
    lk, wk = keys(L), keys(W)
    ln = [' '.join(L.norm[a:b]) for a, b in lk]
    wn = [' '.join(W.norm[a:b]) for a, b in wk]
    m = {}

    def anchors(norms):
        return sum(1 for t in norms if t not in WILD and (t[0].isalnum() or t[0] in '"\'_'))
    sm = difflib.SequenceMatcher(None, ln, wn, autojunk=False)
    for op, i1, i2, j1, j2 in sm.get_opcodes():
        if op == 'equal':
            strong = i2 - i1 >= 3 or anchors(L.norm[lk[i1][0]:lk[i2 - 1][1]]) >= 1
            for x, y in zip(range(i1, i2), range(j1, j2)):
                for p, q in zip(range(*lk[x]), range(*wk[y])):
                    m[p] = (q, 'eq' if strong else 'weak')
        elif op == 'replace':
            a0, a1 = lk[i1][0], lk[i2 - 1][1]
            b0, b1 = wk[j1][0], wk[j2 - 1][1]
            if (a1 - a0) * (b1 - b0) > 4_000_000:
                continue
            ta, tb = L.norm[a0:a1], W.norm[b0:b1]
            s2 = difflib.SequenceMatcher(None, ta, tb, autojunk=False)
            ops = s2.get_opcodes()
            good = [op2 == 'equal' and (p2 - p1 >= 8 and anchors(ta[p1:p2]) >= 2) for op2, p1, p2, q1, q2 in ops]
            for k, (op2, p1, p2, q1, q2) in enumerate(ops):
                if op2 == 'equal':
                    for p, q in zip(range(p1, p2), range(q1, q2)):
                        m[a0 + p] = (b0 + q, 'tok' if good[k] else 'weak')
                elif op2 == 'replace' and p2 - p1 == q2 - q1 and p2 - p1 <= 3 and 0 < k < len(ops) - 1 \
                        and good[k - 1] and good[k + 1]:
                    for p, q in zip(range(p1, p2), range(q1, q2)):
                        m[a0 + p] = (b0 + q, 'pos')
    return m


WILD = {'v', 'loc', 'reg', 'H', 'N', 'D', 'lab', 'F?'} | set(SIZES) | {'code', 'void'}


def acc_class(a):
    if not a:
        return ''
    if a == 'vcall':
        return 'vcall'
    if a.endswith('*'):
        return 'ptr'
    return a[0] if a[0] in 'TF' else a


def compatible(a, b):
    """two access types that can be the same field (Ghidra types floats undefined4 at times; pointers T8)"""
    ca, cb = acc_class(a), acc_class(b)
    if not ca or not cb or ca == cb:
        return True
    if 'vcall' in (ca, cb):
        return False
    sa, sb = SIZES.get(a.rstrip('*')), SIZES.get(b.rstrip('*'))
    if ca == 'ptr' or cb == 'ptr':
        return (sa if ca != 'ptr' else 8) == (sb if cb != 'ptr' else 8)
    return sa == sb


def occurrences(L, W, lfn, wfn):
    """every Linux struct offset / global / callee and its aligned Windows twin"""
    m = align(L, W)
    occ, calls = [], []
    for i, t in enumerate(L.flat):
        n = L.norm[i]
        if n == 'H' and L.is_offpos(i):
            lv, lf = L.byteval(i)
            j, conf = m.get(i, (None, ''))
            wv = wf = None
            if j is not None and W.norm[j] == 'H' and compatible(L.access(i), W.access(j)):
                wv, wf = W.byteval(j)
            occ.append(('offset', lfn, wfn, lv, wv, conf if wv is not None else '', L.access(i),
                        W.access(j) if j is not None else '', L.ctx(i), W.ctx(j) if j is not None else '', lf))
        elif n == 'D':
            a = SYMADDR.search(t)
            if not a:
                continue
            j, conf = m.get(i, (None, ''))
            wv = None
            if j is not None and W.norm[j] == 'D':
                b = SYMADDR.search(W.flat[j])
                wv = int(b.group(1), 16) if b else None
            occ.append(('global', lfn, wfn, int(a.group(1), 16), wv, conf if wv is not None else '', '', '',
                        L.ctx(i), W.ctx(j) if j is not None else '', 'sym'))
        elif n == 'F?':
            j, conf = m.get(i, (None, ''))
            if j is not None and W.norm[j] == 'F?' and conf in ('eq', 'tok'):
                la, wa = L.callee(t), W.callee(W.flat[j])
                if la and wa:
                    calls.append((la, wa))
    return occ, calls


# ---------------------------------------------------------------------------------------------------- commands
def opt_list(argv, flag):
    out, rest, i = [], [], 0
    while i < len(argv):
        if argv[i] == flag and i + 1 < len(argv):
            out.append(argv[i + 1]); i += 2
        else:
            rest.append(argv[i]); i += 1
    return out, rest


def opt_one(argv, flag, default=None):
    v, rest = opt_list(argv, flag)
    return (v[-1] if v else default), rest


def cmd_prep(argv):
    caches, argv = opt_list(argv, '--cache')
    fns, argv = opt_list(argv, '--fn')
    out, files = argv[0], argv[1:]
    os.makedirs(out, exist_ok=True)
    lidx = Index(LIDX)
    extra = []
    for f in fns:
        a, _, b = f.partition('=')
        extra.append((int(a, 16), int(b, 16) if b else None))
    l2w = load_pairs([(a, b) for a, b in extra if b])
    have = parse_out(caches + [os.path.join(out, 'lin.out'), os.path.join(out, 'win.out')]
                     + default_caches('lin') + default_caches('win'))
    plan = collections.OrderedDict()
    with open(os.path.join(out, 'scan.tsv'), 'w') as f:
        f.write('file\tkind\tname\tlinux\tline\tuses\tnote\tscope\n')
        for path in files:
            base, items, funcs = scan_file(path, lidx)
            for k, nm, v, n, uses, note, sc in items:
                f.write('%s\t%s\t%s\t%#x\t%d\t%s\t%s\t%s\n' % (base, k, nm, v, n, ','.join(map(str, uses)),
                                                               note.replace('\t', ' '), ','.join('%#x' % x for x in sorted(sc))))
            for a, why in funcs:
                plan.setdefault(a, [base, []])[1].append(why)
    for a, _ in extra:
        e = lidx.containing(a) or a
        plan.setdefault(e, ['--fn', []])[1].append('fn')
    lb, wb, nopair = [], [], 0
    with open(os.path.join(out, 'plan.tsv'), 'w') as f:
        f.write('linux\tname\twindows\tfile\twhy\n')
        for a, (base, why) in plan.items():
            w = l2w.get(a)
            nopair += w is None
            f.write('%#x\t%s\t%s\t%s\t%s\n' % (a, lidx.name.get(a, ''), '%#x' % w if w else '', base,
                                               ','.join(sorted(set(why)))))
            if a not in have:
                lb.append(a)
            if w and w not in have:
                wb.append(w)
    for name, s in (('lin', lb), ('win', wb)):
        with open(os.path.join(out, name + '.batch'), 'w') as f:
            for a in sorted(set(s)):
                f.write('decompile %#x 60\n' % a)
    print('%d files: %d Linux functions (%d without a Windows pair); to decompile: %d Linux, %d Windows -> %s/{lin,win}.batch'
          % (len(files), len(plan), nopair, len(set(lb)), len(set(wb)), out))


def default_caches(side):
    """every helper's decompiles under logs/winport/ (OUT/lin*.out, OUT/win*.out): no function is decompiled twice"""
    return sorted(glob.glob(os.path.join(SOM, 'logs/winport/*/%s*.out' % side)))


def load_pair_ctx(out, caches):
    lidx, widx = Index(LIDX), Index(WIDX)
    l2w = load_pairs()
    w2l = {b: a for a, b in l2w.items()}
    L = parse_out([os.path.join(out, 'lin.out')] + sorted(glob.glob(os.path.join(out, 'lin*.out'))) + caches
                  + default_caches('lin'))
    Wd = parse_out([os.path.join(out, 'win.out')] + sorted(glob.glob(os.path.join(out, 'win*.out'))) + caches
                   + default_caches('win'))
    lnames = dict(lidx.byname)
    for a, (n, _) in L.items():
        lnames.setdefault(n, a)
    wnames = dict(widx.byname)
    for a, (n, _) in Wd.items():
        wnames.setdefault(n, a)
    return lidx, widx, l2w, w2l, L, Wd, lnames, wnames


def cmd_pair(argv):
    caches, argv = opt_list(argv, '--cache')
    show_all = '--all' in argv
    argv = [a for a in argv if a != '--all']
    out, la, wa = argv[0], int(argv[1], 16), int(argv[2], 16)
    lidx, widx, l2w, w2l, L, Wd, ln, wn = load_pair_ctx(out, caches)
    la, wa = lidx.containing(la) or la, widx.containing(wa) or wa
    if la not in L or wa not in Wd:
        sys.exit('not decompiled: %s%s' % ('' if la in L else 'linux %#x ' % la, '' if wa in Wd else 'windows %#x' % wa))
    A, B = Fn(L[la][1], 'l', ln, l2w, w2l), Fn(Wd[wa][1], 'w', wn, l2w, w2l)
    occ, calls = occurrences(A, B, la, wa)
    for o in occ:
        k, _, _, lv, wv, conf, lacc, wacc, lc, wc, form = o
        if show_all or lv != wv:
            print('%-6s %8s -> %-10s %-3s %-6s|%-6s  L: %s\n%38s W: %s' % (
                k, '%#x' % lv if lv is not None else '?', '%#x' % wv if wv is not None else '-', conf, lacc, wacc,
                lc, '', wc))
    for a, b in collections.Counter(calls):
        print('callee  L %#x (%s) <-> W %#x' % (a, lidx.name.get(a, ''), b))


def pair_summary(A, B, la, wa):
    """how alike two decompiles are: aligned share of the Linux tokens, offsets same/moved, paired callees"""
    m = align(A, B)
    n = len(A.flat) or 1
    strong = sum(1 for c in m.values() if c[1] in ('eq', 'tok'))
    occ, calls = occurrences(A, B, la, wa)
    offs = [o for o in occ if o[0] == 'offset']
    same = sum(1 for o in offs if o[4] is not None and o[4] == o[3])
    moved = sum(1 for o in offs if o[4] is not None and o[4] != o[3])
    lc = [i for i, t in enumerate(A.norm) if t.startswith('F:')]
    lcm = sum(1 for i in lc if i in m and B.norm[m[i][0]] == A.norm[i])
    return '%3d%% of %d tokens aligned (W %d); offsets %d same %d moved %d unaligned; paired calls %d/%d aligned' % (
        100 * strong // n, len(A.flat), len(B.flat), same, moved, len(offs) - same - moved, lcm, len(lc))


def cmd_verify(argv):
    caches, argv = opt_list(argv, '--cache')
    out = argv[0]
    lidx, widx, l2w, w2l, L, Wd, ln, wn = load_pair_ctx(out, caches)
    for a in argv[1:]:
        tag, _, a = a.rpartition(':')
        x, _, y = a.partition('=')
        la, wa = int(x, 16), int(y, 16)
        la, wa = lidx.containing(la) or la, widx.containing(wa) or wa
        if la not in L or wa not in Wd:
            print('%-14s L %#x W %#x: not decompiled (%s)' % (tag, la, wa, 'L' if la not in L else 'W'))
            continue
        A, B = Fn(L[la][1], 'l', ln, l2w, w2l), Fn(Wd[wa][1], 'w', wn, l2w, w2l)
        print('%-14s L %#x W %#x: %s' % (tag, la, wa, pair_summary(A, B, la, wa)))


def cmd_diff(argv):
    caches, argv = opt_list(argv, '--cache')
    group, argv = opt_one(argv, '--group', 'x')
    out = argv[0]
    lidx, widx, l2w, w2l, L, Wd, ln, wn = load_pair_ctx(out, caches)
    plan = [(int(r[0], 16), int(r[2], 16) if r[2] else None, r[3], r[4]) for r in tsv(os.path.join(out, 'plan.tsv'))]
    occ, calls, missing = [], collections.Counter(), []
    cache_fn = {}
    for la, wa, base, why in plan:
        if not wa:
            continue
        if la not in L or wa not in Wd:
            missing.append((la, wa)); continue
        A, B = Fn(L[la][1], 'l', ln, l2w, w2l), Fn(Wd[wa][1], 'w', wn, l2w, w2l)
        cache_fn[(la, wa)] = (A, B)
        o, c = occurrences(A, B, la, wa)
        occ += [x + (base,) for x in o]
        calls.update(c)
    with open(os.path.join(out, 'occ.tsv'), 'w') as f:
        f.write('kind\tlinux_fn\twindows_fn\tlinux\twindows\tconf\tlinux_access\twindows_access\tform\tfile\tlinux_ctx\twindows_ctx\n')
        for k, lf, wf, lv, wv, conf, la_, wa_, lc, wc, form, base in occ:
            f.write('%s\t%#x\t%#x\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' % (
                k, lf, wf, '%#x' % lv if lv is not None else '', '%#x' % wv if wv is not None else '', conf, la_, wa_,
                form, base, lc, wc))
    by = collections.defaultdict(list)
    for o in occ:
        if o[3] is not None:
            by[(o[0], o[3])].append(o)
    # Windows offsets per pair and access type (for the `near` hint)
    wofs = collections.defaultdict(set)
    lofs = collections.defaultdict(set)
    for (la, wa), (A, B) in cache_fn.items():
        for i, n in enumerate(B.norm):
            if n == 'H' and B.is_offpos(i):
                v, _ = B.byteval(i)
                if v is not None:
                    wofs[(la, wa)].add((v, B.access(i)))
        for i, n in enumerate(A.norm):
            if n == 'H' and A.is_offpos(i):
                v, _ = A.byteval(i)
                if v is not None:
                    lofs[(la, wa)].add(v)
    rows = []
    for r in tsv(os.path.join(out, 'scan.tsv')):
        base, kind, name, lv, line, uses, note, sc = (r + [''] * 8)[:8]
        lv = int(lv, 16)
        named = {int(x, 16) for x in sc.split(',') if x}
        os_ = [o for o in by.get((kind, lv), ()) if o[11] == base]
        pool = [o for o in os_ if o[1] in named] or os_
        scoped = bool(named) and any(o[1] in named for o in os_)
        strong = [o for o in pool if o[5] in ('eq', 'tok')]
        weak = [o for o in pool if o[5] in ('weak', 'pos')]
        al = strong or weak
        cnt = collections.Counter(o[4] for o in al)
        wv = ''
        if not pool:
            st = 'absent'
        elif not al:
            st = 'unaligned'
        else:
            wv, c = cnt.most_common(1)[0]
            if c * 4 >= len(al) * 3:
                st = ('same' if wv == lv else 'moved') + ('' if strong else '?')
                if kind == 'offset' and wv != lv and abs(wv - lv) > 0x80:
                    st = 'rebased?'     # read through another base pointer on Windows (or a wrong twin): by hand
            else:
                st = 'conflict'
        if kind == 'global' and isinstance(wv, int):
            wvs = '%#x' % (wv - WBASE)
        else:
            wvs = '%#x' % wv if isinstance(wv, int) else ''
        ev = ''
        if al:
            best = next(o for o in al if o[4] == wv)
            ev = 'L %#x `%s` <-> W %#x `%s`; %s; %d aligned (%s) of %d uses in %d pairs%s' % (
                best[1], best[8], best[2], best[9],
                ' '.join('%#x:%d' % (v if kind == 'offset' else v - WBASE, c) for v, c in cnt.most_common(4)),
                len(al), 'strong' if strong else 'weak', len(pool), len({(o[1], o[2]) for o in pool}),
                '' if scoped else ' (unscoped: all the file\'s pairs)')
            ty = collections.Counter((o[6], o[7]) for o in al if o[4] == wv)
            acc = [(a, b) for (a, b), _ in ty.most_common(2) if a != b and a and b]
            if acc:
                ev += '; access %s' % ','.join('%s/%s' % ab for ab in acc)
        elif pool:
            o = pool[0]
            near = sorted(v for v, a in wofs.get((o[1], o[2]), ()) if abs(v - lv) <= 0x40 and compatible(a, o[6])
                          and v not in lofs.get((o[1], o[2]), ()))
            ev = 'L %#x `%s` (%d uses in %d pairs, none aligned)%s' % (
                o[1], o[8], len(pool), len({(x[1], x[2]) for x in pool}),
                '; near in W %#x: %s' % (o[2], ','.join('%#x' % v for v in near)) if near else '')
        elif named:
            ev = 'scope %s: %s' % (','.join('%#x' % x for x in sorted(named)[:6]),
                                   'none of them paired+decompiled' if not any((x in {p[0] for p in cache_fn}) for x in named)
                                   else 'the paired ones do not use it')
        vt = sum(1 for o in (al or pool) if o[6] == 'vcall') * 2 > len(al or pool) > 0
        rows.append((name if note != 'inline' else 'inline:' + name, 'global' if kind == 'global' else 'vt' if vt else 'off',
                     '%#x' % lv, wvs or '?', {'same': 'same', 'moved': 'plausible'}.get(st, 'unknown'), ev or '-',
                     '%s; %s:%s%s' % (st, base, uses or line, '; ' + note if note and note != 'inline' else ''), st))
    path = os.path.join(out, 'offsets-%s.tsv' % group)
    with open(path, 'w') as f:      # win/README.md 1.2's columns (a draft: confirm by reading, name the inline values)
        f.write('name\tkind\tlinux\twindows\tconfidence\tevidence\tnotes\n')
        for r in rows:
            f.write('\t'.join(x.replace('\t', ' ') for x in r[:7]) + '\n')
    with open(os.path.join(out, 'newpairs.tsv'), 'w') as f:
        f.write('linux\twindows\tvotes\tlinux_name\n')
        for (a, b), c in calls.most_common():
            if a not in l2w:
                f.write('%#x\t%#x\t%d\t%s\n' % (a, b, c, lidx.name.get(a, '')))
    st = collections.Counter(r[7] for r in rows)
    print('%d pairs aligned (%d not decompiled yet), %d uses read; %s -> %s'
          % (len(cache_fn), len(missing), len(occ), ', '.join('%s %d' % kv for kv in sorted(st.items())), path))


def cmd_show(argv):
    """print decompiles from the caches (Linux addresses < 0x140000000, Windows above)"""
    for a in argv:
        a = int(a, 16)
        side = 'win' if a >= WBASE else 'lin'
        d = parse_out(default_caches(side))
        idx = Index(WIDX if side == 'win' else LIDX)
        e = idx.containing(a) or a
        if e in d:
            print('// %s @ %#x\n%s' % (d[e][0], e, re.sub(r'\n\s*\n', '\n', d[e][1])))
        else:
            print('// %#x: not decompiled in logs/winport/*/%s*.out' % (e, side))


def cmd_bytes(argv):
    va = int(argv[0], 16)
    n = int(argv[1]) if len(argv) > 1 else 24
    b = open(EXE, 'rb').read()
    pe = struct.unpack_from('<I', b, 0x3c)[0]
    nsec = struct.unpack_from('<H', b, pe + 6)[0]
    opt = struct.unpack_from('<H', b, pe + 20)[0]
    base = struct.unpack_from('<Q', b, pe + 24 + 24)[0]
    o = pe + 24 + opt
    for _ in range(nsec):
        vs, rva, rs, rp = struct.unpack_from('<IIII', b, o + 8)
        if base + rva <= va < base + rva + min(vs, rs):
            off = rp + va - base - rva
            print('rva %#x  %s' % (va - base, ' '.join('%02x' % x for x in b[off:off + n])))
            return
        o += 40
    sys.exit('not in the file: %#x' % va)


if __name__ == '__main__':
    if len(sys.argv) < 3 and not (len(sys.argv) == 2 and sys.argv[1] in ('-h', '--help')):
        sys.exit(__doc__)
    {'prep': cmd_prep, 'diff': cmd_diff, 'pair': cmd_pair, 'bytes': cmd_bytes, 'verify': cmd_verify, 'show': cmd_show}.get(
        sys.argv[1], lambda a: sys.exit(__doc__))(sys.argv[2:])
