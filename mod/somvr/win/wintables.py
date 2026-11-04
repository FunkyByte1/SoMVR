"""wintables.py - the Windows DLL's site and offset tables from win/sites-*.tsv and win/offsets-*.tsv (win/README.md).

  python3 -I win/wintables.py --check          validate every row against the exe file and the sources
  python3 -I win/wintables.py --out DIR        + write DIR/somvr_wintables.inc and DIR/somvr_winofs.h

Read-only on the exe and the sources. The exe must be the analysed Steam-Windows build (sha256 below) unless --any-exe.
"""
import argparse, glob, hashlib, os, re, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.dirname(HERE)                                    # mod/somvr
EXE = os.path.join(SRC, '..', '..', 'reference', 'bin', 'ShadowOfMordor.exe')
EXE_SHA = 'f5d72578f7e5fcf05e6d15d8bd231f763078620c8545f8464bb186e842279455'
BASE = 0x140000000
SITE_COLS = ['name', 'kind', 'rva', 'ctx_rva', 'ctx', 'target_rva', 'prologue', 'confidence', 'how', 'notes']
OFS_COLS = ['name', 'kind', 'linux', 'windows', 'confidence', 'evidence', 'notes']
KINDS = {'call': 'K_CALL', 'jmp': 'K_JMP', 'vslot': 'K_VSLOT', 'entry': 'K_ENTRY', 'com': None, 'none': None}
KNUM = {'K_CALL': 1, 'K_VSLOT': 2, 'K_JMP': 3, 'K_ENTRY': 4}
LIVE = ('confirmed', 'plausible')
SITE_CONF = LIVE + ('redesign', 'notfound')
OFS_KINDS = ('off', 'vt', 'size', 'global', 'const')
OFS_CONF = ('confirmed', 'plausible', 'same', 'unknown')
COM_IFACES = ('IUnknown', 'IDXGIObject', 'IDXGIDeviceSubObject', 'IDXGISwapChain', 'IDXGISwapChain1', 'ID3D11Device',
              'ID3D11DeviceChild', 'ID3D11DeviceContext', 'ID3D11Resource', 'ID3D11Texture2D', 'ID3D11Buffer')


class PE:
    def __init__(self, path):
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
        rva, size = struct.unpack_from('<II', self.b, pe + 24 + 112 + 5 * 8)  # the base relocation directory
        self.dir64 = set()
        end = rva + size
        while rva and rva + 8 <= end:
            page, bs = struct.unpack('<II', self.read(rva, 8))
            if bs < 8:
                break
            for i in range((bs - 8) // 2):
                e = struct.unpack('<H', self.read(rva + 8 + 2 * i, 2))[0]
                if e >> 12 == 10:                                          # IMAGE_REL_BASED_DIR64
                    self.dir64.add(page + (e & 0xfff))
            rva += bs

    def relocs(self, rva, n):
        """bit i: a DIR64 relocation starts at rva + i (the loader adds base - preferred base there)"""
        return sum(1 << i for i in range(n) if rva + i in self.dir64)

    def sec_of(self, rva):
        for s in self.secs:
            if s[1] <= rva < s[1] + max(s[2], s[4]):
                return s[0]
        return None

    def read(self, rva, n):
        for _, va, vs, rp, rs in self.secs:
            if va <= rva and rva + n <= va + min(vs, rs):
                return self.b[rp + rva - va:rp + rva - va + n]
        return None


def hexb(b):
    return ' '.join('%02x' % x for x in b)


def num(s):
    return int(s, 16) if s.lower().startswith('0x') else int(s)


def scan_sources(src):
    """SITE names -> (Linux kind, file); OFS/GADDR names -> set of (macro, Linux value, file); tables without SITE_TABLE"""
    sites, uses, bare = {}, {}, []
    files = [f for f in glob.glob(os.path.join(src, 'somvr*.[ch]'))] + glob.glob(os.path.join(src, 'win', '*.[ch]'))
    for f in sorted(files):
        base = os.path.relpath(f, src)
        txt = open(f, encoding='utf-8', errors='replace').read()
        for m in re.finditer(r'\bSITE\(\s*"([A-Za-z0-9_]+)"\s*,\s*(K_[A-Z]+)', txt):
            sites.setdefault(m.group(1), (m.group(2), base))
        for m in re.finditer(r'\b(OFS|GADDR)\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*,\s*(0x[0-9a-fA-F]+|[0-9]+)\s*\)', txt):
            uses.setdefault(m.group(2), set()).add((m.group(1), num(m.group(3)), base))
        for m in re.finditer(r'^\s*static\s+const\s+struct\s+site_spec\s+(\w+)\s*\[', txt, re.M):
            bare.append('%s:%s' % (base, m.group(1)))
    return sites, uses, bare


def read_tsv(path, cols, errs):
    rows = []
    with open(path, encoding='utf-8') as fh:
        lines = [(i + 1, l.rstrip('\n')) for i, l in enumerate(fh)]
    lines = [(i, l) for i, l in lines if l.strip() and not l.lstrip().startswith('#')]
    if not lines or lines[0][1].split('\t') != cols:
        errs.append('%s: the header must be: %s' % (path, '\\t'.join(cols)))
        return rows
    for i, l in lines[1:]:
        c = l.split('\t')
        if len(c) != len(cols):
            errs.append('%s:%d: %d columns, want %d' % (path, i, len(c), len(cols)))
            continue
        rows.append((i, dict(zip(cols, [x.strip() for x in c]))))
    return rows


def take_bytes(pe, rva, spec, what, where, errs):
    """spec = a count (take them from the exe) or hex (check them); -> bytes or None"""
    if re.fullmatch(r'[0-9]+', spec):
        n = int(spec)
        if not 1 <= n <= 64:
            errs.append('%s: %s count %d not 1..64' % (where, what, n))
            return None
        b = pe.read(rva, n)
        if b is None:
            errs.append('%s: %s rva %#x+%d outside the file' % (where, what, rva, n))
        return b
    try:
        want = bytes.fromhex(spec)
    except ValueError:
        errs.append('%s: %s is neither a count nor hex bytes' % (where, what))
        return None
    b = pe.read(rva, len(want))
    if b != want:
        errs.append('%s: %s at rva %#x differ: exe %s' % (where, what, rva, hexb(b) if b else 'outside the file'))
        return None
    return b


def check_site(pe, r, where, lsites, errs):
    """-> ('live', tuple) | ('com', tuple) | ('none', name) | None (error)"""
    name, kind, conf = r['name'], r['kind'], r['confidence']
    if kind not in KINDS:
        errs.append('%s: kind %r not one of %s' % (where, kind, ', '.join(KINDS)))
        return None
    if conf not in SITE_CONF:
        errs.append('%s: confidence %r not one of %s' % (where, conf, ', '.join(SITE_CONF)))
        return None
    if name not in lsites:
        errs.append('%s: %s is no SITE("%s", ...) in the sources' % (where, name, name))
        return None
    lk = lsites[name][0]
    if kind == 'none' or conf not in LIVE:
        return ('none', name)
    if kind == 'com':
        m = re.fullmatch(r'(\w+):([0-9]+)', r['rva'])
        if not m or m.group(1) not in COM_IFACES:
            errs.append('%s: a com row needs rva Interface:slot (%s)' % (where, ', '.join(COM_IFACES)))
            return None
        if lk != 'K_VSLOT':
            errs.append('%s: %s is %s on Linux; com rows replace K_VSLOT sites' % (where, name, lk))
            return None
        return ('com', (name, m.group(1), int(m.group(2))))
    if KINDS[kind] != lk:
        errs.append('%s: %s is %s on Linux but %s here: another shape is a redesign row' % (where, name, lk, kind))
        return None
    try:
        rva = num(r['rva'])
        ctx = rva if r['ctx_rva'] == '-' else num(r['ctx_rva'])
        tgt = num(r['target_rva']) if r['target_rva'] != '-' else None
    except ValueError:
        errs.append('%s: rva, ctx_rva or target_rva not a number' % where)
        return None
    if rva >= BASE or ctx >= BASE or (tgt is not None and tgt >= BASE):
        errs.append('%s: an absolute address; RVAs only (VA - %#x)' % (where, BASE))
        return None
    ctxb = take_bytes(pe, ctx, r['ctx'], 'ctx', where, errs)
    if ctxb is None:
        return None
    need = 5 if kind in ('call', 'jmp') else 8 if kind == 'vslot' else 1
    if not ctx <= rva or rva + need > ctx + len(ctxb):
        errs.append('%s: ctx %#x+%d does not hold rva %#x..+%d' % (where, ctx, len(ctxb), rva, need))
        return None
    if kind in ('call', 'jmp'):
        op = pe.read(rva, 5)
        if op[0] != (0xe8 if kind == 'call' else 0xe9):
            errs.append('%s: byte at %#x is %02x, not %s' % (where, rva, op[0], 'E8' if kind == 'call' else 'E9'))
            return None
        real = (rva + 5 + struct.unpack('<i', op[1:])[0]) & 0xffffffff
    elif kind == 'vslot':
        if rva & 7 or pe.sec_of(rva) not in ('.rdata', '.data'):
            errs.append('%s: slot %#x unaligned or not in .rdata/.data' % (where, rva))
            return None
        real = struct.unpack('<Q', pe.read(rva, 8))[0] - BASE
    else:
        real = rva
    if tgt is None:
        tgt = real
    if real != tgt:
        errs.append('%s: %s at %#x leads to %#x, target_rva says %#x' % (where, kind, rva, real, tgt))
        return None
    if pe.sec_of(tgt) not in ('.text', '.interpr', 'text'):
        errs.append('%s: target %#x is not in code' % (where, tgt))
        return None
    prob = take_bytes(pe, tgt, r['prologue'] if r['prologue'] != '-' else '15', 'prologue', where, errs)
    if prob is None:
        return None
    if len(prob) > 32:  # check_site's pro[32] (somvr.c): a longer prologue is a "bad site spec" at run time
        errs.append('%s: prologue %d bytes > 32 (the DLL checks at most 32)' % (where, len(prob)))
        return None
    rc, rp = pe.relocs(ctx, len(ctxb)), pe.relocs(tgt, len(prob))
    for what, at, nb in (('context', ctx, len(ctxb)), ('prologue', tgt, len(prob))):  # (b34 review)
        part = [x for x in range(at - 7, at + nb) if x in pe.dir64 and not (at <= x and x + 8 <= at + nb)]
        if part:  # somvr_win_sites.c moves only whole relocated qwords: an ASLR load (native Windows) would mismatch
            errs.append('%s: a relocation at %#x straddles the %s bytes at %#x' % (where, part[0], what, at))
            return None
    if kind in ('call', 'jmp') and any(rva + i in pe.dir64 for i in range(-7, 5)):
        errs.append('%s: a relocation overlaps the patched bytes at %#x' % (where, rva))
        return None
    return ('live', (name, KNUM[KINDS[kind]], rva, ctx, hexb(ctxb), tgt, hexb(prob), conf, rc, rp))


def carry(tag, path, mpath, live, gnames, ga0, quiet):
    """the live rows on another exe through winport_exe_map.py's map: every row re-checked on that exe's bytes"""
    if not os.path.exists(path) or not os.path.exists(mpath):
        print('wintables: %s: no %s or %s' % (tag, path, mpath))
        return None
    raw = open(path, 'rb').read()
    sha = hashlib.sha256(raw).hexdigest()
    pe = PE(path)
    m = {}
    for l in open(mpath):
        c = l.rstrip('\n').split('\t')
        if c[0] == 'name' or len(c) < 7:
            continue
        m[(c[0], c[1] == 'global')] = c
    num_ = lambda v: int(v, 16) if v not in ('-', '') else None
    rows, drop = [], []
    for n, k, rva, ctx, ch, tgt, ph, conf, rc, rp in live:
        c = m.get((n, False))
        if not c or c[6] != 'ok':
            drop.append('%s (%s)' % (n, c[6] if c else 'not in the map'))
            continue
        nr, nc, nt = num_(c[3]), num_(c[4]), num_(c[5])
        if nc is None:
            nc = nr
        if nt is None and k == KNUM['K_ENTRY']:
            nt = nr
        nb, npb = len(ch.split()), len(ph.split())
        cb, pb = pe.read(nc, nb) if nc is not None else None, pe.read(nt, npb) if nt is not None else None
        ok = cb is not None and pb is not None and nc <= nr
        if ok and k in (KNUM['K_CALL'], KNUM['K_JMP']):
            op = pe.read(nr, 5)
            ok = op[0] == (0xe8 if k == KNUM['K_CALL'] else 0xe9) and \
                (nr + 5 + struct.unpack('<i', op[1:])[0]) & 0xffffffff == nt
        elif ok and k == KNUM['K_VSLOT']:
            ok = struct.unpack('<Q', pe.read(nr, 8))[0] - BASE == nt
        elif ok:
            ok = nr == nt
        if not ok:
            drop.append('%s (bytes or target do not hold on %s)' % (n, tag))
            continue
        rows.append((n, k, nr, nc, hexb(cb), nt, hexb(pb), conf, pe.relocs(nc, nb), pe.relocs(nt, npb)))
    ga = {}
    for n in gnames:
        c = m.get((n, True))
        ga[n] = num_(c[3]) if c and c[6] in ('ok', 'unchecked') else None
    if not quiet or drop:
        print('wintables: %s (sha256 %.16s): %d of %d live rows carried, %d globals; dropped: %s' %
              (tag, sha, len(rows), len(live), sum(v is not None for v in ga.values()), ', '.join(drop) or 'none'))
    return (tag, sha, rows, ga)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--out')
    ap.add_argument('--exe', default=EXE)
    ap.add_argument('--any-exe', action='store_true', help='skip the sha256 check (tests)')
    ap.add_argument('--src', default=SRC)
    ap.add_argument('--quiet', action='store_true')
    ap.add_argument('--extra-exe', action='append', default=[], metavar='TAG=EXE:MAP',
                    help='another build: its rows carried by scripts/winport_exe_map.py (MAP)')
    ap.add_argument('--tsv-dir', help='the folder of sites-*.tsv / offsets-*.tsv (default: win/)')
    a = ap.parse_args()
    errs, warns = [], []
    if not os.path.exists(a.exe):
        print('wintables: no exe at %s (ShadowOfMordor/reference is a symlink to the main checkout\'s)' % a.exe)
        return 2
    raw = open(a.exe, 'rb').read()
    sha = hashlib.sha256(raw).hexdigest()
    if sha != EXE_SHA and not a.any_exe:
        print('wintables: %s is not the analysed Steam-Windows exe (sha256 %s)' % (a.exe, sha[:16]))
        return 2
    pe = PE(a.exe)
    if pe.base != BASE:
        errs.append('exe image base %#x, want %#x' % (pe.base, BASE))
    lsites, uses, bare = scan_sources(a.src)
    for b in bare:
        warns.append('site table %s is "static const": use SITE_TABLE or it keeps Linux addresses on Windows' % b)
    live, com, none, seen, groups = [], [], [], {}, {}
    ofs, gaddr = {}, {}
    windir = a.tsv_dir or os.path.join(a.src, 'win')
    for path in sorted(glob.glob(os.path.join(windir, 'sites-*.tsv'))):
        g = os.path.basename(path)[6:-4]
        cnt = groups.setdefault(g, {'live': 0, 'com': 0, 'none': 0, 'ofs': 0, 'ofs_known': 0})
        for i, r in read_tsv(path, SITE_COLS, errs):
            where = '%s:%d' % (os.path.relpath(path, a.src), i)
            key = tuple(r[c] for c in ('kind', 'rva', 'ctx_rva', 'ctx', 'target_rva', 'prologue', 'confidence'))
            if r['name'] in seen:
                if seen[r['name']][1] == key:   # the same row in two groups' files (the COM slots): once
                    warns.append('%s: %s also in %s (the same row: counted once)' % (where, r['name'], seen[r['name']][0]))
                else:
                    errs.append('%s: %s already in %s with other values' % (where, r['name'], seen[r['name']][0]))
                continue
            seen[r['name']] = (where, key)
            res = check_site(pe, r, where, lsites, errs)
            if res is None:
                continue
            {'live': live, 'com': com, 'none': none}[res[0]].append(res[1])
            cnt[res[0]] += 1
    oseen = {}
    for path in sorted(glob.glob(os.path.join(windir, 'offsets-*.tsv'))):
        g = os.path.basename(path)[8:-4]
        cnt = groups.setdefault(g, {'live': 0, 'com': 0, 'none': 0, 'ofs': 0, 'ofs_known': 0})
        for i, r in read_tsv(path, OFS_COLS, errs):
            where = '%s:%d' % (os.path.relpath(path, a.src), i)
            n = r['name']
            if n in oseen:
                errs.append('%s: %s already in %s' % (where, n, oseen[n]))
                continue
            oseen[n] = where
            if r['kind'] not in OFS_KINDS or r['confidence'] not in OFS_CONF:
                errs.append('%s: kind %r / confidence %r not known' % (where, r['kind'], r['confidence']))
                continue
            try:
                lv = num(r['linux'])
                wv = None if r['windows'] in ('?', '-') or r['confidence'] == 'unknown' else num(r['windows'])
            except ValueError:
                errs.append('%s: linux/windows not numbers' % where)
                continue
            if n not in uses:
                errs.append('%s: %s is used in no OFS(%s, ...) / GADDR(%s, ...) in the sources' % (where, n, n, n))
                continue
            for macro, v, f in uses[n]:
                if v != lv:
                    errs.append('%s: %s says linux %#x but %s has %s(%s, %#x)' % (where, n, lv, f, macro, n, v))
                if (macro == 'GADDR') != (r['kind'] == 'global'):
                    errs.append('%s: %s is a %s in %s but kind %s' % (where, n, macro, f, r['kind']))
            if r['kind'] == 'global' and wv is not None and wv >= BASE:
                errs.append('%s: a global is an RVA (VA - %#x)' % (where, BASE))
                continue
            (gaddr if r['kind'] == 'global' else ofs)[n] = (wv, r['confidence'])
            cnt['ofs'] += 1
            cnt['ofs_known'] += wv is not None
    unchecked = []
    for n, us in sorted(uses.items()):
        macros = {m for m, _, _ in us}
        if len(macros) > 1 or len({v for _, v, _ in us}) > 1:
            errs.append('%s is used as %s with values %s: one name, one value' %
                        (n, '/'.join(sorted(macros)), ', '.join('%#x' % v for v in sorted({v for _, v, _ in us}))))
        if n not in ofs and n not in gaddr:
            unchecked.append(n)
    if not a.quiet or errs:
        for w in warns:
            print('wintables: warning: ' + w)
        for e in errs:
            print('wintables: ERROR ' + e)
    tot_live, tot_com, tot_none = len(live), len(com), len(none)
    nofs = sum(1 for n in uses if n not in gaddr)
    print('wintables: %d live sites, %d COM slots, %d none of %d Linux SITEs; offsets %d used (%d unchecked), '
          'globals %d used (%d unknown); %d errors' %
          (tot_live, tot_com, tot_none, len(lsites), nofs, sum(1 for n in unchecked if n not in gaddr and
           not any(m == 'GADDR' for m, _, _ in uses[n])),
           len(uses) - nofs, sum(1 for n, us in uses.items() if any(m == 'GADDR' for m, _, _ in us) and
                                 (n not in gaddr or gaddr[n][0] is None)), len(errs)))
    if not a.quiet:
        for g, c in sorted(groups.items()):
            print('  %-8s live %3d  com %2d  none %3d  offsets %3d (%d known)' %
                  (g, c['live'], c['com'], c['none'], c['ofs'], c['ofs_known']))
    if errs:
        return 1
    live.sort(key=lambda t: t[0].encode())
    gnames = sorted(n for n, us in uses.items() if any(m == 'GADDR' for m, _, _ in us))
    exes = [('steamwin', EXE_SHA, live, {n: gaddr.get(n, (None, ''))[0] for n in gnames})]
    for spec in a.extra_exe:   # TAG=EXE:MAP (scripts/winport_exe_map.py's output for that exe)
        tag, rest = spec.split('=', 1)
        path, mpath = rest.rsplit(':', 1)
        res = carry(tag, path, mpath, live, gnames, exes[0][3], a.quiet)
        if res is None:
            return 1
        exes.append(res)
    if a.out:
        os.makedirs(a.out, exist_ok=True)
        with open(os.path.join(a.out, 'somvr_wintables.inc'), 'w') as o:
            o.write('/* generated by win/wintables.py from win/sites-*.tsv and offsets-*.tsv - do not edit */\n')
            o.write('/* name, kind, rva, ctx_rva, ctx, target_rva, prologue, confirmed, the DIR64 relocations in ctx and\n'
                    ' * prologue (bit i = at byte i: the loader adds base - 0x140000000 there) */\n')
            for tag, sha, rows, ga in exes:
                o.write('static const struct somvr_win_site g_win_sites_%s[] = { /* sorted by name (strcmp) */\n' % tag)
                for n, k, rva, ctx, ch, tgt, ph, conf, rc, rp in rows:
                    o.write('\t{"%s", %d, %#x, %#x, "%s", %#x, "%s", %d, %#xull, %#xull},\n' %
                            (n, k, rva, ctx, ch, tgt, ph, conf == 'confirmed', rc, rp))
                o.write('\t{0}\n};\n')
                o.write('static const uint32_t g_win_gaddr_%s[%d] = { /* WGI_* order */\n' % (tag, len(gnames) + 1))
                for n in gnames:
                    o.write('\t%s, /* %s */\n' % ('%#x' % ga[n] if ga.get(n) is not None else '0', n))
                o.write('\t0\n};\n')
            o.write('static const struct somvr_win_exe g_win_exes[] = {\n')
            for tag, sha, rows, ga in exes:
                o.write('\t{"%s", "%s", g_win_sites_%s, %d, g_win_gaddr_%s},\n' % (tag, sha, tag, len(rows), tag))
            o.write('};\n#define SOMVR_WIN_NEXES %d\n' % len(exes))
            o.write('static const struct somvr_win_com g_win_com[] = {\n')
            for n, iface, slot in sorted(com):
                o.write('\t{"%s", "%s", %d},\n' % (n, iface, slot))
            o.write('\t{0}\n};\n#define SOMVR_WIN_NCOM %d\n' % len(com))
        with open(os.path.join(a.out, 'somvr_winofs.h'), 'w') as o:
            o.write('/* generated by win/wintables.py from win/offsets-*.tsv - do not edit */\n#pragma once\n')
            o.write('#define SOMVR_WIN_NGADDR %d\n' % len(gnames))
            for n in sorted(uses):
                (macro, lv, f) = sorted(uses[n])[0]
                if macro == 'GADDR':
                    o.write('#define WGI_%s %d /* the index into the running exe\'s global RVAs */\n' % (n, gnames.index(n)))
                else:
                    wv, conf = ofs.get(n, (None, ''))
                    o.write('#define WOFS_%s %#x%s\n' % (n, wv if wv is not None else lv,
                                                       '' if wv is not None else ' /* unchecked: the Linux value */'))
        if not a.quiet:
            print('wintables: wrote %s/somvr_wintables.inc (%s) and somvr_winofs.h' %
                  (a.out, ', '.join('%s %d sites' % (t, len(r)) for t, _, r, _ in exes)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
