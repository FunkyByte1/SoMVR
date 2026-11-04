#!/usr/bin/env python3
"""buildplan.py - the build checks' runner (docs/re-xr-pipeline.md 161).

build.sh, vklayer/build.sh and win/build.sh do not run their commands one after another any more: buildplan.sh's
functions (gcc, clang, run, say, built) write them as records of a plan file, and this program runs the plan with
SOMVR_JOBS processes at a time and prints every record's output in the plan's order - the log reads as the serial
script's did. The first record in that order that fails ends the run with its exit code (as set -e did; 128+N for a
signal), the commands still running are stopped, and nothing after it is printed.

  gcc/clang ARGS...   a compiler line as written. With -c: one object, cached. Without (compile and link in one, the
                      tests' lines): each source compiled on its own with the line's arguments (the other sources and
                      -o OUT left out, -c -o OBJ added; OBJ under OUT's folder in .obj/<flags hash>/, so a source
                      compiled with the same arguments for several programs is compiled once), then the link with the
                      line's arguments, each source replaced by its object in its place - the same objects and the same
                      output as the one command (somvr.so: byte-identical, docs/re-xr-pipeline.md 161).
                      -fsyntax-only, -E, -S: run as written.
  run [-k] [-d PATH]... [-o PATH]... [--] ARGV...
                      a command. It waits for the records that make its program, its -d paths and any argument that
                      names (or holds, as in LD_PRELOAD=PATH or /out:PATH) a file an earlier record makes. -o: the
                      files it makes. -k: skipped when its command and the content of its inputs (-d and the made files
                      it names) are those of its last successful run and its -o files are unchanged (the links).
  echo TEXT           TEXT, in its place.
  built PATH [SUFFIX] "built PATH (its sha256's first 16)SUFFIX", once PATH is made.

The object cache: OBJ is rebuilt when its command, the compiler (path, size, time), the working directory, an
environment variable that changes the output (SOURCE_DATE_EPOCH: __DATE__/__TIME__, the compiler's search paths), or
the content of any file its last compile read (the compiler's -MD depfile: the source and every header, system headers
included) changed; a stat shortcut (size, mtime, ctime, inode) skips hashing a file nothing touched. Every cache file is
under the output's own build folder (a worktree's own); the plan file is a mktemp in TMPDIR.

  buildplan.py PLAN --state DIR
Environment: SOMVR_JOBS=N (default: the CPU count; 1 while the user is in VR - /run/user/UID/vr-session/status.json),
SOMVR_PLAN_DIRECT=1 (no object cache, every compiler line run as written: tests/sanitize.sh's gcc wrapper),
SOMVR_PLAN_TIMES=1 (each executed command's time on stderr at the end). DIR keeps .plan-times.json (the last time of
each command: the longest chains start first).
"""
import fcntl
import hashlib
import heapq
import json
import os
import shutil
import signal
import subprocess
import sys
import threading
import time

TAKES_VALUE = {'-o', '-I', '-isystem', '-include', '-imacros', '-iquote', '-idirafter', '-MF', '-MT', '-MQ', '-x', '-L',
               '-l', '-Xlinker', '-Xclang', '-target', '-D', '-U', '-u', '-z', '-T', '-aux-info', '-Xpreprocessor',
               '-Xassembler', '-arch'}
INPUT_EXT = ('.o', '.obj', '.a', '.so', '.lib', '.res')
KEY_ENV = ('SOURCE_DATE_EPOCH', 'CPATH', 'C_INCLUDE_PATH', 'GCC_EXEC_PREFIX', 'COMPILER_PATH', 'LIBRARY_PATH',
           'LD_RUN_PATH', 'SDKROOT')
TERMINAL = ('done', 'failed', 'skipped')
DIRECT = os.environ.get('SOMVR_PLAN_DIRECT') == '1'


def sha_bytes(b):
    return hashlib.sha256(b).hexdigest()


def sha_text(*parts):
    return sha_bytes(json.dumps(parts, separators=(',', ':')).encode('utf-8', 'surrogateescape'))


# ---- files: content signatures with a stat shortcut ------------------------------------------------------------
_sig_lock = threading.Lock()
_sig_cache = {}


def stat_key(path):
    try:
        st = os.stat(path)
    except OSError:
        return None
    return [st.st_size, st.st_mtime_ns, st.st_ctime_ns, st.st_ino]


def file_sig(path):
    """[size, mtime_ns, ctime_ns, ino, sha256] of path now, or None"""
    sk = stat_key(path)
    if sk is None:
        return None
    k = (path, tuple(sk))
    with _sig_lock:
        h = _sig_cache.get(k)
    if h is None:
        try:
            with open(path, 'rb') as f:
                d = hashlib.sha256()
                for chunk in iter(lambda: f.read(1 << 20), b''):
                    d.update(chunk)
                h = d.hexdigest()
        except OSError:
            return None
        with _sig_lock:
            _sig_cache[k] = h
    return sk + [h]


def same_file(path, rec):
    """path's content is the one rec ([size, mtime, ctime, ino, sha]) recorded"""
    sk = stat_key(path)
    if sk is None:
        return False
    if sk == rec[:4]:
        return True
    s = file_sig(path)
    return s is not None and s[4] == rec[4]


def write_json(path, obj):
    tmp = '%s.tmp.%d.%d' % (path, os.getpid(), threading.get_ident())
    with open(tmp, 'w') as f:
        json.dump(obj, f, separators=(',', ':'))
    os.replace(tmp, path)


def read_json(path):
    try:
        with open(path) as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def unlink(path):
    try:
        os.unlink(path)
    except OSError:
        pass


def parse_depfile(path):
    try:
        text = open(path, encoding='utf-8', errors='surrogateescape').read()
    except OSError:
        return None
    text = text.replace('\\\n', ' ')
    deps = []
    for line in text.split('\n'):
        if ':' not in line:
            continue
        rhs = line.split(':', 1)[1] if not line.startswith(' ') else line
        words, cur, esc = [], '', False
        for ch in rhs:
            if esc:
                cur += ch
                esc = False
            elif ch == '\\':
                esc = True
            elif ch in ' \t':
                if cur:
                    words.append(cur)
                cur = ''
            else:
                cur += ch
        if cur:
            words.append(cur)
        deps.extend(words)
    seen, out = set(), []
    for d in deps:
        if d not in seen:
            seen.add(d)
            out.append(d)
    return out


# ---- tools ------------------------------------------------------------------------------------------------------
_tool_ids = {}


def tool_id(name):
    if name not in _tool_ids:
        p = name if '/' in name else shutil.which(name)
        if p:
            rp = os.path.realpath(p)
            sk = stat_key(rp) or [0, 0]
            _tool_ids[name] = '%s:%d:%d' % (rp, sk[0], sk[1])
        else:
            _tool_ids[name] = name + ':missing'
    return _tool_ids[name]


def key_env():
    return [(k, os.environ[k]) for k in KEY_ENV if k in os.environ]


# ---- the plan ---------------------------------------------------------------------------------------------------
class Unit:
    __slots__ = ('kind', 'argv', 'cwd', 'deps', 'dependents', 'outs', 'ins', 'stamp', 'depfile', 'depcmd', 'label',
                 'est', 'rank', 'minrec', 'state', 'rc', 'out', 'dur', 'pending', 'proc', 'cached', 'seq')

    def __init__(self, kind, argv, cwd, minrec):
        self.kind, self.argv, self.cwd, self.minrec = kind, argv, cwd, minrec
        self.deps, self.dependents, self.outs, self.ins = [], [], [], []
        self.stamp = self.depfile = self.depcmd = None
        self.label = '%s:%s' % (kind, ' '.join(argv)[:400])
        self.est, self.rank, self.state, self.rc, self.out, self.dur = 1.0, 0.0, 'wait', 0, b'', 0.0
        self.pending, self.proc, self.cached, self.seq = 0, None, False, 0


class Rec:
    __slots__ = ('kind', 'units', 'show', 'text', 'path')

    def __init__(self, kind):
        self.kind, self.units, self.show, self.text, self.path = kind, [], [], '', None


class Plan:
    def __init__(self):
        self.units, self.recs = [], []
        self.producer = {}  # path -> the unit that makes it
        self.by_obj = {}    # object path -> its compile unit
        self.gcc_same = {}  # (cwd, argv) of a compiler line -> its record's units (a line planned twice)

    def add_unit(self, u, deps):
        for d in deps:
            if d is not None and d is not u and d not in u.deps:
                u.deps.append(d)
        u.seq = len(self.units)
        self.units.append(u)
        return u

    def made_by(self, path):
        return self.producer.get(os.path.normpath(path)) if path else None

    def refs(self, words):
        """the units making a file named by (or inside) one of words"""
        found = []
        for w in words:
            if '/' not in w:
                continue
            cands = [w]
            for sep in ('=', ':', ','):
                if sep in w:
                    cands.extend(p for p in w.split(sep) if '/' in p)
            for c in cands:
                u = self.made_by(c)
                if u is not None and u not in found:
                    found.append(u)
        return found

    def make(self, path, u):
        p = os.path.normpath(path)
        old = self.producer.get(p)
        if old is not None and old is not u:  # two commands writing one file at once: never
            raise SystemExit('buildplan: two records make %s:\n  %s\n  %s' % (p, old.label[:200], u.label[:200]))
        self.producer[p] = u

    # -- records
    def echo(self, text):
        r = Rec('echo')
        r.text = text
        self.recs.append(r)

    def built(self, path, suffix):
        r = Rec('built')
        r.path, r.text = path, suffix
        u = self.made_by(path)
        if u is not None:
            r.units.append(u)
        self.recs.append(r)

    def run(self, cwd, fields):
        keep, deps, outs, i = False, [], [], 0
        while i < len(fields):
            f = fields[i]
            if f == '-k':
                keep = True
            elif f == '-d':
                deps.append(os.path.join(cwd, fields[i + 1]))
                i += 1
            elif f == '-o':
                outs.append(os.path.join(cwd, fields[i + 1]))
                i += 1
            elif f == '--':
                i += 1
                break
            else:
                break
            i += 1
        argv = fields[i:]
        if not argv:
            raise SystemExit('buildplan: a run record without a command')
        k = len(self.recs)
        u = Unit('run', argv, cwd, k)
        prods = [self.made_by(d) for d in deps] + self.refs(argv)
        self.add_unit(u, prods)
        if keep and outs:
            u.kind = 'keep'
            u.outs = outs
            u.ins = sorted(set(deps) | {p for d in self.refs(argv) for p in d.outs})
            u.stamp = os.path.join(os.path.dirname(outs[0]), '.obj', 'run', os.path.basename(outs[0]) + '.stamp')
        for o in outs:
            self.make(o, u)
        r = Rec('run')
        r.units.append(u)
        r.show.append(u)
        self.recs.append(r)

    def gcc(self, cwd, argv):
        k = len(self.recs)
        r = Rec('gcc')
        same = self.gcc_same.get((cwd, tuple(argv)))
        if same is not None:  # the same line again (vklayer/build.sh's library, built and then checked): made once
            r.units = list(same)
            self.recs.append(r)
            return
        i, out, srcs, files, flags = 1, None, [], [], set()
        while i < len(argv):
            a = argv[i]
            if a == '-o' and i + 1 < len(argv):
                out = i + 1
                i += 2
                continue
            if a in TAKES_VALUE:
                i += 2
                continue
            if a.startswith('-'):
                flags.add(a)
            elif a.endswith('.c'):
                srcs.append(i)
            elif a.endswith(INPUT_EXT):
                files.append(i)
            i += 1
        outp = os.path.join(cwd, argv[out]) if out else None
        plain = DIRECT or flags & {'-fsyntax-only', '-E', '-S', '-M', '-MM'} or not outp or \
            ('-c' in flags and len(srcs) != 1)
        if plain:
            u = self.add_unit(Unit('run', argv, cwd, k), self.refs(argv[1:]))
            if outp:
                u.outs = [outp]
                self.make(outp, u)
            r.units, r.show = [u], [u]
        elif '-c' in flags:
            u = self.compile_unit(cwd, argv, outp, k, os.path.join(cwd, argv[srcs[0]]))
            r.units, r.show = [u], ([u] if u.minrec == k else [])
        else:
            drop = set(srcs) | set(files) | {out, out - 1}
            objs, cus = {}, []
            for si in srcs:
                src = os.path.join(cwd, argv[si])
                norm = [a for j, a in enumerate(argv) if j not in drop or j == si]
                tag = sha_text(tool_id(argv[0]), cwd, key_env(), [('<SRC>' if a == argv[si] else a) for a in norm])[:12]
                name = os.path.basename(src)[:-2] + '-' + sha_text(os.path.normpath(src))[:8]
                obj = os.path.join(os.path.dirname(outp), '.obj', tag, name + '.o')
                cargv = norm + ['-c', '-o', obj]
                cu = self.by_obj.get(obj) or self.compile_unit(cwd, cargv, obj, k, src)
                objs[si] = obj
                cus.append(cu)
            largv = [objs.get(j, a) for j, a in enumerate(argv)]
            lu = Unit('keep', largv, cwd, k)
            lu.label = 'link:' + outp
            lu.outs = [outp]
            lu.ins = [objs[si] for si in srcs] + [os.path.join(cwd, argv[j]) for j in files]
            lu.stamp = os.path.join(os.path.dirname(outp), '.obj', 'link', os.path.basename(outp) + '.stamp')
            lu.est = 0.3
            self.add_unit(lu, cus + self.refs([argv[j] for j in files] + argv[1:]))
            self.make(outp, lu)
            r.units = cus + [lu]
            r.show = [cu for cu in cus if cu.minrec == k] + [lu]
        self.gcc_same[(cwd, tuple(argv))] = r.units
        self.recs.append(r)

    def compile_unit(self, cwd, cargv, obj, k, src):
        """cargv: the compile as written (-c -o obj). Its depfile: gcc's -MD on the compile (gcc's output is the same
        with it); clang's from a second, preprocessor-only run after it (-M): clang -gcodeview writes its command line
        into the object (LF_BUILDINFO), so a -MD there would change the object and the DLL's PDB GUID"""
        depcmd = None
        if '-MF' in cargv:  # the line names its own depfile
            dep = cargv[cargv.index('-MF') + 1]
        elif 'clang' in os.path.basename(cargv[0]):
            dep = obj + '.d'
            j = cargv.index('-o')
            depcmd = [a for i, a in enumerate(cargv) if a != '-c' and i not in (j, j + 1)] + ['-M', '-MF', dep, '-MT', obj]
        else:
            dep = obj + '.d'
            n = len(cargv) - 3 if cargv[-3:-1] == ['-c', '-o'] else len(cargv)
            cargv = cargv[:n] + ['-MD', '-MF', dep] + cargv[n:]
        u = Unit('cc', cargv, cwd, k)
        u.label = 'cc:' + obj
        u.outs = [obj]
        u.stamp = obj + '.stamp'
        u.depfile, u.depcmd = dep, depcmd
        try:
            u.est = 0.15 + os.path.getsize(src) / 40000.0
        except OSError:
            u.est = 0.5
        self.add_unit(u, self.refs(cargv[1:]))
        self.by_obj[obj] = u
        self.make(obj, u)
        return u


def read_plan(path):
    data = open(path, 'rb').read().split(b'\0')
    plan, i = Plan(), 0
    while i < len(data) - 1:
        if data[i] != b'R' or i + 3 >= len(data):
            raise SystemExit('buildplan: %s: a bad record at field %d' % (path, i))
        kind, cwd, n = data[i + 1].decode(), data[i + 2].decode('utf-8', 'surrogateescape'), int(data[i + 3])
        f = [x.decode('utf-8', 'surrogateescape') for x in data[i + 4:i + 4 + n]]
        i += 4 + n
        if kind == 'gcc':
            plan.gcc(cwd, f)
        elif kind == 'run':
            plan.run(cwd, f)
        elif kind == 'echo':
            plan.echo(' '.join(f))
        elif kind == 'built':
            plan.built(os.path.join(cwd, f[0]), ''.join(f[1:]))
        else:
            raise SystemExit('buildplan: unknown record %r' % kind)
    for u in plan.units:
        for d in u.deps:
            d.dependents.append(u)
    return plan


# ---- caching ------------------------------------------------------------------------------------------------------
def unit_key(u):
    return sha_text(u.kind, tool_id(u.argv[0]), u.cwd, key_env(), u.argv)


def cached(u):
    """the unit's stamp says its outputs are current"""
    st = read_json(u.stamp)
    if not st or st.get('key') != unit_key(u):
        return False
    for path, rec in st.get('outs', []):
        if not same_file(path, rec):
            return False
    for path, rec in st.get('ins', []):
        if not same_file(path, rec):
            return False
    return True


def save_stamp(u):
    ins = []
    if u.kind == 'cc':
        deps = parse_depfile(u.depfile) if u.depfile else None
        if deps is None:
            return
        paths = [os.path.normpath(os.path.join(u.cwd, d)) for d in deps]
    else:
        paths = u.ins
    for p in paths:
        s = file_sig(p)
        if s is None:
            return  # a file it read is gone already: no stamp, the next run rebuilds
        ins.append([p, s])
    outs = []
    for p in u.outs:
        s = file_sig(p)
        if s is None:
            return
        outs.append([p, s])
    os.makedirs(os.path.dirname(u.stamp), exist_ok=True)
    write_json(u.stamp, {'v': 1, 'key': unit_key(u), 'ins': ins, 'outs': outs})


# ---- running ------------------------------------------------------------------------------------------------------
class Runner:
    def __init__(self, plan, jobs, env):
        self.plan, self.jobs, self.env = plan, jobs, env
        self.cv = threading.Condition()
        self.heap, self.stop_after, self.quit = [], None, False
        self.running = set()

    def push(self, u):
        u.state = 'ready'
        heapq.heappush(self.heap, (-u.rank, u.seq, u))

    def start(self):
        for u in self.plan.units:
            u.pending = len(u.deps)
            if u.pending == 0:
                self.push(u)
        self.threads = [threading.Thread(target=self.worker, daemon=True) for _ in range(self.jobs)]
        for t in self.threads:
            t.start()

    def skip(self, u):
        stack = [u]
        while stack:
            x = stack.pop()
            if x.state in TERMINAL:
                continue
            x.state = 'skipped'
            stack.extend(x.dependents)

    def worker(self):
        while True:
            with self.cv:
                while not self.heap and not self.quit:
                    self.cv.wait()
                if self.quit:
                    return
                _, _, u = heapq.heappop(self.heap)
                if u.state != 'ready':
                    continue
                if self.stop_after is not None and u.minrec > self.stop_after:
                    self.skip(u)
                    self.cv.notify_all()
                    continue
                u.state = 'running'
            try:
                rc, out = self.execute(u)
            except Exception as e:  # never a unit left running (the printer would wait for it forever)
                rc, out = 1, ('buildplan: %s: %s: %s\n' % (u.label[:200], type(e).__name__, e)).encode()
            with self.cv:
                u.rc, u.out = rc, out
                if rc == 0:
                    u.state = 'done'
                    for d in u.dependents:
                        d.pending -= 1
                        if d.pending == 0 and d.state == 'wait':
                            self.push(d)
                else:
                    u.state = 'failed'
                    for d in u.dependents:
                        self.skip(d)
                    if self.stop_after is None or u.minrec < self.stop_after:
                        self.stop_after = u.minrec
                self.cv.notify_all()

    def execute(self, u):
        t0 = time.time()
        if u.stamp and cached(u):
            u.cached = True
            return 0, b''
        if u.stamp:
            unlink(u.stamp)
        for o in u.outs:
            if os.path.dirname(o):
                os.makedirs(os.path.dirname(o), exist_ok=True)
        try:
            p = subprocess.Popen(u.argv, cwd=u.cwd, env=self.env, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                 stderr=subprocess.STDOUT, start_new_session=True)
        except OSError as e:
            return 127, ('buildplan: %s: %s\n' % (u.argv[0], e)).encode()
        with self.cv:
            if self.quit:
                p.kill()
            u.proc = p
            self.running.add(u)
        out, _ = p.communicate()
        with self.cv:
            self.running.discard(u)
            u.proc = None
        rc = p.returncode
        if rc < 0:
            rc = 128 - rc
        if rc == 0 and u.depcmd:  # clang's dependencies, a preprocessor-only run (no stamp if it fails: compiled again)
            unlink(u.depfile)
            d = subprocess.run(u.depcmd, cwd=u.cwd, env=self.env, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL)
            if d.returncode:
                unlink(u.depfile)
        u.dur = time.time() - t0
        if rc == 0 and u.stamp:
            save_stamp(u)
        return rc, out

    def kill_all(self):
        with self.cv:
            self.quit = True
            procs = [u.proc for u in self.running if u.proc is not None]
            self.cv.notify_all()
        for p in procs:
            try:
                os.killpg(p.pid, signal.SIGTERM)
            except OSError:
                pass
        t = time.time() + 10  # their own cleanup (wintests: the wine run folder, its wineserver)
        for p in procs:
            try:
                p.wait(max(0.1, t - time.time()))
            except subprocess.TimeoutExpired:
                try:
                    os.killpg(p.pid, signal.SIGKILL)
                except OSError:
                    pass


class Stopped(Exception):
    pass


def on_signal(sig, frame):
    raise Stopped(sig)


def user_in_vr():
    try:
        return bool(json.load(open('/run/user/%d/vr-session/status.json' % os.getuid())).get('user'))
    except (OSError, ValueError):
        return False


def ranks(plan, times):
    for u in plan.units:
        if u.label in times:
            u.est = float(times[u.label])
    for u in reversed(plan.units):
        u.rank = u.est + max((d.rank for d in u.dependents), default=0.0)


def main():
    args = sys.argv[1:]
    if len(args) != 3 or args[1] != '--state':
        raise SystemExit('usage: buildplan.py PLAN --state DIR')
    plan = read_plan(args[0])
    state = args[2]
    os.makedirs(state, exist_ok=True)
    tpath = os.path.join(state, '.plan-times.json')
    times = read_json(tpath) or {}
    ranks(plan, times)
    jobs = int(os.environ.get('SOMVR_JOBS') or 0) or (1 if user_in_vr() else (os.cpu_count() or 1))
    env = dict(os.environ)
    env.pop('SOMVR_PLAN', None)
    env['SOMVR_JOBS'] = str(jobs)  # the nested runners (test_layer's scenarios, llp64, wintests) take it
    lock = open(os.path.join(state, '.plan.lock'), 'a')
    try:  # one check at a time in a worktree's build folder (two would write the same objects)
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except OSError:
        sys.stdout.write('buildplan: another check is running on %s: waiting for it\n' % state)
        sys.stdout.flush()
        fcntl.flock(lock, fcntl.LOCK_EX)
    for sg in (signal.SIGTERM, signal.SIGHUP):
        signal.signal(sg, on_signal)
    r = Runner(plan, jobs, env)
    t0 = time.time()
    r.start()
    rc = 0
    outb = sys.stdout.buffer
    try:
        for k, rec in enumerate(plan.recs):
            with r.cv:  # all of the record done - or one of it failed (its other compiles are not waited for)
                while any(u.state not in TERMINAL for u in rec.units) and not any(u.state == 'failed' for u in rec.units):
                    r.cv.wait()
                failed = next((u for u in rec.units if u.state == 'failed'), None) or \
                    next((u for u in rec.units if u.state != 'done'), None)
            for u in rec.show:
                if u.out and u.state in TERMINAL:
                    outb.write(u.out)
            if failed is None:
                if rec.kind == 'echo':
                    outb.write((rec.text + '\n').encode('utf-8', 'surrogateescape'))
                elif rec.kind == 'built':
                    s = file_sig(rec.path)
                    outb.write(('built %s (%s)%s\n' % (rec.path, s[4][:16] if s else 'missing', rec.text)).encode(
                        'utf-8', 'surrogateescape'))
            outb.flush()
            if failed is not None:
                if failed.state == 'failed':
                    rc = failed.rc or 1
                    if failed not in rec.show and failed.out:
                        outb.write(failed.out)  # a shared object's compile, first needed here
                else:
                    rc = next((d.rc for d in failed.deps if d.state == 'failed'), 1) or 1
                why = ' '.join(failed.argv)
                outb.write(('FAIL: exit %d: %s\n' % (rc, why if len(why) < 300 else why[:297] + '...')).encode(
                    'utf-8', 'surrogateescape'))  # (set -e said nothing: the log ended after the last good record)
                outb.flush()
                break
    except KeyboardInterrupt:
        rc = 130
    except Stopped as e:
        rc = 128 + e.args[0]
    finally:
        r.kill_all()
        for u in plan.units:
            if u.state == 'done' and not u.cached and u.dur > 0:
                times[u.label] = round(u.dur, 3)
        try:
            write_json(tpath, times)
        except OSError:
            pass
        if os.environ.get('SOMVR_PLAN_TIMES') == '1':
            ran = sorted((u for u in plan.units if u.dur > 0 and not u.cached), key=lambda u: -u.dur)
            sys.stderr.write('buildplan: %.1f s wall, %d jobs, %d commands run (%.1f s), %d cached\n' % (
                time.time() - t0, jobs, len(ran), sum(u.dur for u in ran), sum(1 for u in plan.units if u.cached)))
            for u in ran[:25]:
                sys.stderr.write('  %6.2f %s\n' % (u.dur, u.label[:150]))
    return rc


if __name__ == '__main__':
    sys.exit(main())
