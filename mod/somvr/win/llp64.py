#!/usr/bin/env python3
"""llp64.py - the LLP64 guard of the Windows build (docs/re-xr-pipeline.md 151; som-b34-analyze).

Linux is LP64 (long = 64 bits), Windows LLP64 (long = 32 bits). The mod's code was written for Linux: an explicit cast of
a 64-bit value (a pointer, uintptr_t, uint64_t, size_t) to `long` / `unsigned long` is lossless there and truncates on
Windows - silently: an explicit cast is no compiler warning, and `%lx` matches `(unsigned long)` on both systems. Build
win2's logs printed the game's code addresses (0x140dd50a1) as 0x40dd50a1 and every heap address over 4 GB cut.

  llp64.py --check [--cache DIR] CLANG-ARGS... -- FILE...
      clang's AST of each FILE (Windows target: the CLANG-ARGS of win/build.sh) -> every explicit cast to the 32-bit
      long/unsigned long whose operand is 64 bits, as file:line:col; exit 1 if any (win/build.sh --check runs it).
      The files' ASTs SOMVR_JOBS at a time (default: the CPU count); --cache DIR: a file's answer is kept and reused
      while its clang command and the content of every file its translation unit read (clang's depfile) are unchanged
      (docs/re-xr-pipeline.md 161)
      A narrowing meant: through an exact 32-bit type, (long)(int32_t)x - it cuts the same on both systems
  llp64.py --fix FILE...
      the mechanical fix of the printf-family calls (every function declared format(printf ...) in the tree and the C
      library's): in a call that casts to (unsigned long)/(long), those casts become (unsigned long long)/(long long)
      and its format strings' %l conversions %ll - the same output on Linux (both 64 bits), the full value on Windows.
      Idempotent; a merge that brings new such lines: rerun it on the merged files, then build.sh --check (gcc's
      -Wformat -Werror catches a %ll left with a long argument) and win/build.sh --check.
"""
import concurrent.futures as cf
import hashlib
import json
import os
import re
import shutil
import subprocess
import threading
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
MOD = os.path.dirname(HERE)  # mod/somvr

# ---- --check: clang's AST ---------------------------------------------------------------------------------------
LOC = re.compile(r'(?:(?<![\w/.])(/[^\s<>,:]+):(\d+):(\d+)|line:(\d+):(\d+)|col:(\d+))')
QUOTED = re.compile(r"'[^']*'")  # a type's text may hold a path ('struct (unnamed at /x.c:7:1)'): not a location
CAST = re.compile(r"CStyleCastExpr 0x[0-9a-f]+ <[^>]*> '([^']*)'(?::'([^']*)')? <(\w+)>")
OPERAND = re.compile(r"^[|` ]*[-`]-\w+ 0x[0-9a-f]+ <[^>]*> '([^']*)'(?::'([^']*)')?")
WIDE = ('unsigned long long', 'long long')


def ast_casts(clang_args, path, depfile=None):
    """the truncating casts in path's translation unit (only those in mod/somvr's own files): [(file, line, col, to, from)]"""
    dep = ['-MD', '-MF', depfile] if depfile else []
    p = subprocess.run(['clang'] + clang_args + dep + ['-fsyntax-only', '-w', '-Xclang', '-ast-dump', path],
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, errors='replace')
    if p.returncode != 0:
        sys.stderr.write(p.stderr[-4000:])
        raise SystemExit('llp64: clang failed on %s' % path)
    out, f, ln, col, pend = [], None, 0, 0, None
    for raw in p.stdout.split('\n'):
        first = None
        for m in LOC.finditer(QUOTED.sub("''", raw)):
            if m.group(1):
                f, ln, col = m.group(1), int(m.group(2)), int(m.group(3))
            elif m.group(4):
                ln, col = int(m.group(4)), int(m.group(5))
            else:
                col = int(m.group(6))
            if first is None:
                first = (f, ln, col)
        if pend:
            m = OPERAND.match(raw)
            ty = (m.group(2) or m.group(1)) if m else ''
            if ty in WIDE or '*' in ty or ty.endswith(']'):
                out.append(pend + (m.group(1),))
            pend = None
        m = CAST.search(raw)
        if m and (m.group(2) or m.group(1)) in ('long', 'unsigned long') and first and first[0] and \
                os.path.realpath(first[0]).startswith(MOD + os.sep):
            pend = (first[0], first[1], first[2], m.group(1))
    return out


def file_sig(path):
    try:
        st = os.stat(path)
        with open(path, 'rb') as f:
            h = hashlib.sha256(f.read()).hexdigest()
    except OSError:
        return None
    return [st.st_size, st.st_mtime_ns, st.st_ctime_ns, st.st_ino, h]


def same_file(path, rec):
    try:
        st = os.stat(path)
    except OSError:
        return False
    if [st.st_size, st.st_mtime_ns, st.st_ctime_ns, st.st_ino] == rec[:4]:
        return True
    s = file_sig(path)
    return s is not None and s[4] == rec[4]


def deps_of(depfile):
    try:
        text = open(depfile, encoding='utf-8', errors='surrogateescape').read().replace('\\\n', ' ')
    except OSError:
        return None
    words = text.split(':', 1)[1].split() if ':' in text else []
    return sorted({os.path.normpath(w) for w in words})


def cached_casts(args, path, cache):
    """ast_casts, its answer kept in cache (a JSON per file) for an unchanged command and translation unit"""
    if not cache:
        return ast_casts(args, path)
    clang = shutil.which('clang') or 'clang'
    st = os.stat(os.path.realpath(clang))
    key = hashlib.sha256(json.dumps([os.path.realpath(clang), st.st_size, st.st_mtime_ns, os.getcwd(), args,
                                     path]).encode()).hexdigest()
    entry = os.path.join(cache, '%s-%s.json' % (os.path.basename(path), hashlib.sha256(path.encode()).hexdigest()[:8]))
    try:
        e = json.load(open(entry))
        if e.get('key') == key and all(same_file(p, r) for p, r in e['deps']):
            return [tuple(c) for c in e['casts']]
    except (OSError, ValueError, KeyError, TypeError):
        pass
    depfile = '%s.d.%d' % (entry, os.getpid())
    casts = ast_casts(args, path, depfile)
    deps = deps_of(depfile)
    try:
        os.unlink(depfile)
    except OSError:
        pass
    sigs = [(p, file_sig(p)) for p in deps or []]
    if deps and all(s is not None for p, s in sigs):
        tmp = '%s.tmp.%d.%d' % (entry, os.getpid(), threading.get_ident())  # per thread: two threads caching the same file raced on one tmp (11-05)
        with open(tmp, 'w') as f:
            json.dump({'key': key, 'deps': sigs, 'casts': casts}, f)
        os.replace(tmp, entry)
    return casts


def check(argv):
    cache = None
    if argv[:1] == ['--cache'] and len(argv) > 1:
        cache, argv = argv[1], argv[2:]
        os.makedirs(cache, exist_ok=True)
    if '--' not in argv:
        raise SystemExit('usage: llp64.py --check [--cache DIR] CLANG-ARGS... -- FILE...')
    k = argv.index('--')
    args, files = argv[:k], argv[k + 1:]
    seen = {}
    jobs = int(os.environ.get('SOMVR_JOBS') or 0) or os.cpu_count() or 1
    with cf.ThreadPoolExecutor(jobs) as ex:
        found = list(ex.map(lambda p: cached_casts(args, p, cache), files))
    for casts in found:  # the files' order, as one after another
        for f, ln, col, to, frm in casts:
            seen.setdefault((os.path.realpath(f), ln, col), (to, frm))
    for (f, ln, col), (to, frm) in sorted(seen.items()):
        print('%s:%d:%d: (%s) of a 64-bit %s: cut to 32 bits on Windows only (LLP64) - in a log (unsigned) long long '
              'and %%ll (llp64.py --fix); a value known to fit: say so, (%s)(int32_t)/(uint32_t), the same on both' %
              (os.path.relpath(f, MOD), ln, col, to, frm, to))
    print('llp64: %d explicit casts of a 64-bit value to the 32-bit long in %d translation units' % (len(seen), len(files)))
    return 1 if seen else 0


# ---- --fix: the printf-family calls -----------------------------------------------------------------------------
STD = {'printf', 'fprintf', 'sprintf', 'snprintf', 'dprintf'}
DECL1 = re.compile(r'format\s*\(\s*printf\s*,\s*\d+\s*,\s*\d+\s*\)\s*\)\s*\)\s*(?:static\s+|inline\s+|extern\s+)*'
                   r'[A-Za-z_][\w \t*]*?\b([A-Za-z_]\w*)\s*\(')
DECL2 = re.compile(r'\b([A-Za-z_]\w*)\s*\([^;{}()]*\)\s*__attribute__\s*\(\(\s*format\s*\(\s*printf')
CASTS = [(re.compile(r'\(\s*unsigned\s+long\s*\)'), '(unsigned long long)'), (re.compile(r'\(\s*long\s*\)'), '(long long)')]
CONV = re.compile(r"%%|(%[-+ #0'*.0-9]*)l([diouxX])")


def printf_names(root):
    names = set(STD)
    for d, _, fs in os.walk(root):
        if os.sep + 'build' in d[len(root):]:
            continue
        for n in fs:
            if n.endswith(('.c', '.h')):
                t = open(os.path.join(d, n), errors='replace').read()
                names.update(DECL1.findall(t))
                names.update(DECL2.findall(t))
    return names


def spans(text):
    """[(start, end, kind)]: the text cut into code / 'str' (a "..." literal) / 'skip' (comments, char literals)"""
    out, i, n, s0 = [], 0, len(text), 0
    while i < n:
        c = text[i]
        if c == '/' and text.startswith('/*', i):
            j = text.find('*/', i + 2)
            j = n if j < 0 else j + 2
        elif c == '/' and text.startswith('//', i):
            j = text.find('\n', i)
            j = n if j < 0 else j
        elif c in '"\'':
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == '\\' else 1
            j += 1
        else:
            i += 1
            continue
        if s0 < i:
            out.append((s0, i, 'code'))
        out.append((i, j, 'str' if c == '"' else 'skip'))
        i = s0 = j
    if s0 < n:
        out.append((s0, n, 'code'))
    return out


def fix_text(text, names):
    sp = spans(text)
    code = ''.join(text[a:b] if k == 'code' else ' ' * (b - a) for a, b, k in sp)  # strings/comments blanked
    calls = []
    for m in re.finditer(r'\b([A-Za-z_]\w*)\s*\(', code):
        if m.group(1) not in names:
            continue
        depth, j = 0, m.end() - 1
        while j < len(code):
            if code[j] == '(':
                depth += 1
            elif code[j] == ')':
                depth -= 1
                if depth == 0:
                    break
            j += 1
        calls.append((m.start(), j + 1))
    edits = []  # (start, end, new)
    done = 0
    for a, b in calls:
        if not any(r.search(code, a, b) for r, _ in CASTS):
            continue
        if any(a < e0 < b for e0, _, _ in edits):  # a call inside a call already done
            continue
        done += 1
        for r, new in CASTS:
            for m in r.finditer(code, a, b):
                edits.append((m.start(), m.end(), new))
        for s, e, k in sp:
            if k == 'str' and s >= a and e <= b:
                lit = text[s:e]
                nl = CONV.sub(lambda m: m.group(0) if m.group(0) == '%%' else m.group(1) + 'll' + m.group(2), lit)
                if nl != lit:
                    edits.append((s, e, nl))
    for s, e, new in sorted(edits, reverse=True):
        text = text[:s] + new + text[e:]
    return text, done


def fix(files):
    names = printf_names(MOD)
    total = 0
    for f in files:
        t = open(f).read()
        nt, n = fix_text(t, names)
        if nt != t:
            open(f, 'w').write(nt)
            print('llp64: %s: %d calls' % (os.path.relpath(f, MOD), n))
            total += n
    print('llp64: %d printf-family calls fixed (%d functions known)' % (total, len(names)))
    return 0


if __name__ == '__main__':
    if len(sys.argv) > 1 and sys.argv[1] == '--check':
        sys.exit(check(sys.argv[2:]))
    if len(sys.argv) > 1 and sys.argv[1] == '--fix':
        sys.exit(fix(sys.argv[2:]))
    sys.exit(__doc__)
