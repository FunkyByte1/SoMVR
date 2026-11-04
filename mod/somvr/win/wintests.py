#!/usr/bin/env python3
"""wintests.py - the platform-neutral unit tests on the Windows build (win/README.md 7, docs/re-winport.md 7.13).

The list is the Linux one: every `gcc ... -o .../tests/NAME SOURCES` in ../build.sh and ../vklayer/build.sh with its run
line (the program and its arguments), so a test added there runs here too. Each is built for x86_64-pc-windows-msvc with
the DLL's flags (win/build.sh's CF, given after `--`) + -Werror, win/compat, the static CRT, and run under Proton's wine
in a throwaway copy of the harness prefix (logs/winport/prefix: never a play prefix, never the user's Heroic one) with
no display, no audio driver (winepulse/winealsa/wineoss disabled, an empty XDG_RUNTIME_DIR) and HOME in the copy. The
verdict is the test's exit code, as on Linux (a canary first proves wine hands the code back). Absolute paths among a
run line's arguments become Z: paths. LINUX_ONLY names the tests that cannot run here and why; any other test the
parser cannot read is a failure, never a silent skip. The copy is deleted at the end; the outputs stay in
build/win/tests/logs/NAME.log.
  wintests.py --xwin X [--jobs N] [--only a,b] [--build-only] -- CLANG_FLAGS...
Environment: WIN_PROTON (Steam's Proton - Experimental), SOMVR_WIN_TESTS_LOGS (the logs/winport folder for the copy),
SOMVR_WIN_TESTS=build (build only: a machine without Proton).
"""
import argparse
import concurrent.futures as cf
import hashlib
import json
import os
import re
import shlex
import shutil
import signal
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))  # win/
MOD = os.path.dirname(HERE)                        # mod/somvr
OUT = os.path.join(MOD, 'build', 'win', 'tests')

# the Linux scripts: (path, the variables their test lines use, extra flags for their sources)
SCRIPTS = [
    (os.path.join(MOD, 'build.sh'), {'D': MOD}, []),
    (os.path.join(MOD, 'vklayer', 'build.sh'),
     {'D': os.path.join(MOD, 'vklayer'), 'B': os.path.join(MOD, 'vklayer', 'build')},
     ['-I' + os.path.join(MOD, 'vklayer'), '-Wno-missing-prototypes']),
]
# tests of those scripts that cannot run on Windows, and why (printed in every run)
LINUX_ONLY = {
    'test_layer': "the Linux layer .so under a fake Vulkan driver: dlopen()s libVkLayer_somvr_openxr.so, mock_xr.so "
                  "(as libvulkan + the OpenXR loader) and the preload's objects (input_obj.so ...), -rdynamic exports; "
                  "the Windows layer DLL (W1) is proven by its boots",
    'hands_lvp': "the VR hands drawn on lavapipe (Mesa's CPU Vulkan ICD, a Linux .so loaded with dlopen)",
}
# the other parts of the Linux checks that are not C unit tests (for the record; printed with --list)
LINUX_OTHER = {
    'selftest': "the hooks against stand-in functions: an ELF -no-pie -rdynamic program whose stand-ins somvr.c finds "
                "by dlsym and patches in place; Windows hooks are MinHook + the address tables (wintables.py --check, "
                "the boots)",
    'inert': "LD_PRELOAD of somvr.so into /bin/true and python3 (no log) and its export list (the DLL's exports and "
             "imports are win/build.sh's own check)",
    'python': "check_sites.py, handmesh_gen.py --check, wintables.py --check, shaders/build_spv.sh --check: host "
              "scripts, the same answer on any platform",
    'vklayer-lib': "the layer .so's manifest, NEEDED and exports; vulkaninfo with the layer (Linux loader)",
}
LIBS = ['libcmt.lib', 'libvcruntime.lib', 'libucrt.lib', 'oldnames.lib', 'kernel32.lib', 'user32.lib', 'advapi32.lib']
STOP = threading.Event()  # SIGTERM/SIGHUP (win/build.sh's runner stops it): no more wine starts
WINE_DLLS = 'winepulse.drv=d;winealsa.drv=d;wineoss.drv=d;mscoree,mshtml=d'


def logical_lines(path):
    """the script's lines with backslash continuations joined and trailing comments cut"""
    out, cur = [], ''
    for raw in open(path, encoding='utf-8'):
        line = raw.rstrip('\n')
        if line.endswith('\\'):
            cur += line[:-1] + ' '
            continue
        out.append(cur + line)
        cur = ''
    if cur:
        out.append(cur)
    res = []
    for l in out:
        l = l.strip()
        if not l or l.startswith('#'):
            continue
        res.append(re.sub(r'\s+#\s.*$', '', l))  # " # comment" (no '#' inside the test lines' words)
    return res


def subst(word, vs):
    for k, v in vs.items():
        word = word.replace('${%s}' % k, v).replace('$%s' % k, v)
    return word


def parse_tests():
    """[(name, script, sources, run args, extra flags)], [problems]"""
    tests, problems, seen = [], [], set()
    for path, vs, extra in SCRIPTS:
        lines = logical_lines(path)
        for i, l in enumerate(lines):
            if not l.startswith('gcc '):
                continue
            m = re.search(r'-o "?\$\{?([DB])\}?((?:/build)?/tests/)([A-Za-z0-9_]+)"?(\s|$)', l)
            if not m:
                continue  # the library, the selftest, the .so objects
            name = m.group(3)
            if name in LINUX_ONLY:
                continue
            exe = subst('$' + m.group(1) + m.group(2) + name, vs)
            words = [subst(w, vs) for w in shlex.split(l[4:].replace('"${CF[@]}"', ''))]
            srcs = [w for w in words if w.endswith('.c')]
            flags = [w for w in words if re.match(r'-[DIW]', w) and w != '-Werror' and not w.startswith('-Wl,')]
            bad = [w for w in words if '$' in w]
            run = None
            for r in lines[i + 1:]:  # the first line that runs it (`run PROGRAM ARGS`: buildplan.sh), before its next build
                rw = shlex.split(r)
                if rw[:1] == ['run']:
                    r, rw = r.split(None, 1)[1] if len(rw) > 1 else '', rw[1:]
                if r.startswith('gcc ') and re.search(r'-o "?\$\{?[DB]\}?(?:/build)?/tests/%s"?(\s|$)' % name, r):
                    break
                if rw and subst(rw[0], vs) == exe:
                    run = r
                    break
            where = '%s: %s' % (os.path.relpath(path, MOD), name)
            if not srcs or bad:
                problems.append('%s: cannot read its gcc line (%s)' % (where, ' '.join(bad) or 'no .c'))
                continue
            if run is None:
                problems.append('%s: no run line `run "%s" ...` after its gcc line' % (where, exe))
                continue
            if re.search(r'[|&;<>`]|\$\(', run):
                problems.append('%s: its run line is not a plain `PROGRAM ARGS` (%s)' % (where, run))
                continue
            args = [subst(w, vs) for w in shlex.split(run)[1:]]
            if any('$' in a for a in args):
                problems.append('%s: a variable left in its arguments (%s)' % (where, run))
                continue
            if name in seen:
                problems.append('%s: a second test of that name' % where)
                continue
            seen.add(name)
            tests.append((name, os.path.relpath(path, MOD), srcs, args, extra + flags))
    return tests, problems


def objpath(src, extra=()):
    rel = os.path.relpath(os.path.abspath(src), MOD)
    tag = '-' + hashlib.sha256('\0'.join(extra).encode()).hexdigest()[:8] if extra else ''  # another flag set
    return os.path.join(OUT, 'obj', re.sub(r'[/\\.]+', '_', rel[:-2].replace('..', 'up')) + tag + '.obj')


def file_sig(path):
    """[size, mtime, ctime, inode, sha256] of path, or None"""
    try:
        st = os.stat(path)
        with open(path, 'rb') as f:
            h = hashlib.sha256(f.read()).hexdigest()
    except OSError:
        return None
    return [st.st_size, st.st_mtime_ns, st.st_ctime_ns, st.st_ino, h]


def stale(obj, stamp_text):
    """the object must be compiled again: its command changed, or any file its last compile read (the .d file) is not
    the content it read - by the stat (ctime too) or else the sha256 (docs/re-xr-pipeline.md 161: an mtime compare kept
    a broken object after a source restored with its old mtime, cp -p)"""
    try:
        if open(obj + '.flags').read() != stamp_text:
            return True
        deps = json.load(open(obj + '.deps.json'))
        os.stat(obj)
    except (OSError, ValueError):
        return True
    for path, rec in deps:
        try:
            st = os.stat(path)
        except OSError:
            return True
        if [st.st_size, st.st_mtime_ns, st.st_ctime_ns, st.st_ino] != rec[:4]:
            s = file_sig(path)
            if s is None or s[4] != rec[4]:
                return True
    return False


def read_deps(dfile):
    try:
        text = open(dfile).read().replace('\\\n', ' ')
    except OSError:
        return None
    return [w.replace('\\ ', ' ') for w in text.split(':', 1)[1].split()] if ':' in text else []


def compile_one(src, flags, extra):
    obj = objpath(src, extra)
    cmd = ['clang'] + flags + list(extra) + ['-Werror', '-MD', '-MF', obj + '.d', '-c', '-o', obj, src]
    stamp_text = hashlib.sha256('\0'.join(cmd).encode()).hexdigest()
    if not stale(obj, stamp_text):
        return obj, None
    for x in (obj + '.flags', obj + '.deps.json'):
        try:
            os.unlink(x)
        except OSError:
            pass
    p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if p.returncode:
        return obj, p.stdout.strip() or 'clang exit %d' % p.returncode
    deps = read_deps(obj + '.d')
    sigs = [(d, file_sig(d)) for d in deps or []]
    if deps and all(sg is not None for d, sg in sigs):
        with open(obj + '.deps.json', 'w') as f:
            json.dump(sigs, f)
        with open(obj + '.flags', 'w') as f:
            f.write(stamp_text)
    return obj, None


def link_one(exe, objs, xwin):
    if os.path.exists(exe) and all(os.path.getmtime(o) <= os.path.getmtime(exe) for o in objs):
        return None
    cmd = ['lld-link', '/nologo', '/out:' + exe, '/subsystem:console', '/machine:x64', '/ignore:4099',
           '/libpath:%s/crt/lib/x86_64' % xwin, '/libpath:%s/sdk/lib/um/x86_64' % xwin,
           '/libpath:%s/sdk/lib/ucrt/x86_64' % xwin] + objs + LIBS
    p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if p.returncode:
        try:
            os.unlink(exe)
        except OSError:
            pass
        return p.stdout.strip() or 'lld-link exit %d' % p.returncode
    return None


def user_in_vr():
    try:
        return bool(json.load(open('/run/user/%d/vr-session/status.json' % os.getuid())).get('user'))
    except (OSError, ValueError):
        return False


def logs_dir():
    if os.environ.get('SOMVR_WIN_TESTS_LOGS'):
        return os.environ['SOMVR_WIN_TESTS_LOGS']
    own = os.path.join(MOD, '..', '..', 'logs')
    if not os.path.isdir(own):  # a worktree: the main checkout's logs/
        try:
            common = subprocess.run(['git', '-C', MOD, 'rev-parse', '--path-format=absolute', '--git-common-dir'],
                                    stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True).stdout.strip()
            own = os.path.join(os.path.dirname(common), 'ShadowOfMordor', 'logs')
        except OSError:
            pass
    return os.path.join(os.path.abspath(own), 'winport')


def winarg(a):
    return 'Z:' + os.path.abspath(a) if a.startswith('/') and os.path.exists(a) else a


def last_line(text):
    lines = [l for l in text.replace('\r', '').split('\n') if l.strip() and not l.startswith('ntsync:')]
    return lines[-1].strip() if lines else '(no output)'


def main():
    # stopped by win/build.sh's runner (an earlier record failed: SIGTERM) - still delete the run folder and stop its
    # wineserver (the finally below; a plain SIGTERM would skip it and leave the 400 MB copy, docs/re-xr-pipeline.md 161)
    def stop(n, f):  # the tests not started yet are skipped, then the finally (wineserver -k, the folder) at once
        STOP.set()
        sys.exit(128 + n)
    for sg in (signal.SIGTERM, signal.SIGHUP):
        signal.signal(sg, stop)
    ap = argparse.ArgumentParser()
    ap.add_argument('--xwin', required=True)
    ap.add_argument('--jobs', type=int, default=0)
    ap.add_argument('--only', default='')
    ap.add_argument('--build-only', action='store_true')
    ap.add_argument('--list', action='store_true')
    ap.add_argument('--timeout', type=int, default=300)
    ap.add_argument('flags', nargs=argparse.REMAINDER)
    a = ap.parse_args()
    flags = a.flags[1:] if a.flags[:1] == ['--'] else a.flags
    t0 = time.time()
    tests, problems = parse_tests()
    if a.only:
        want = set(a.only.split(','))
        tests = [t for t in tests if t[0] in want]
    if a.list:
        for name, script, srcs, args, extra in tests:
            print('%-18s %-18s %s%s' % (name, script, ' '.join(os.path.relpath(s, MOD) for s in srcs),
                                        ('  ARGS ' + ' '.join(args)) if args else ''))
        for k, v in list(LINUX_ONLY.items()) + list(LINUX_OTHER.items()):
            print('linux-only %-11s %s' % (k, v))
        return 1 if problems else 0
    for p in problems:
        print('FAIL: wintests: ' + p)
    jobs = a.jobs or int(os.environ.get('SOMVR_JOBS') or 0) or (1 if user_in_vr() else min(8, os.cpu_count() or 1))
    os.makedirs(os.path.join(OUT, 'obj'), exist_ok=True)
    os.makedirs(os.path.join(OUT, 'logs'), exist_ok=True)

    # ---- build: every source once (the test's own flags), then one exe per test
    canary = os.path.join(HERE, 'tests', 'exitcode.c')
    units = {(canary, ())}
    units.add((os.path.join(HERE, 'somvr_win_compat.c'), ()))
    for name, script, srcs, args, extra in tests:
        for s in srcs:
            units.add((s, tuple(extra)))
    errs = {}
    with cf.ThreadPoolExecutor(jobs) as ex:
        futs = {ex.submit(compile_one, s, flags, e): s for s, e in sorted(units)}
        for f in cf.as_completed(futs):
            obj, err = f.result()
            if err:
                errs[futs[f]] = err
    for s, e in sorted(errs.items()):
        print('FAIL: compile %s:\n%s' % (os.path.relpath(s, MOD), '\n'.join(e.splitlines()[:30])))
    compat = objpath(os.path.join(HERE, 'somvr_win_compat.c'))
    plan = [('exitcode', [objpath(canary)], ['ignored'])]
    for name, script, srcs, args, extra in tests:
        if any(s in errs for s in srcs):
            continue
        plan.append((name, [objpath(s, tuple(extra)) for s in srcs] + [compat], args))
    if compat in [objpath(s) for s in errs] or canary in errs:
        return 1
    linkerr = {}
    with cf.ThreadPoolExecutor(jobs) as ex:
        futs = {ex.submit(link_one, os.path.join(OUT, n + '.exe'), objs, a.xwin): n for n, objs, args in plan}
        for f in cf.as_completed(futs):
            if f.result():
                linkerr[futs[f]] = f.result()
    for n, e in sorted(linkerr.items()):
        print('FAIL: link %s:\n%s' % (n, '\n'.join(e.splitlines()[:30])))
    plan = [p for p in plan if p[0] not in linkerr]
    tb = time.time() - t0
    nbuilt = len(plan) - 1
    if a.build_only or os.environ.get('SOMVR_WIN_TESTS') == 'build':
        print('win tests: %d built in %.0f s (not run: %s)' % (
            nbuilt, tb, '--build-only' if a.build_only else 'SOMVR_WIN_TESTS=build'))
        return 1 if (problems or errs or linkerr) else 0

    # ---- run: Proton's wine, a throwaway copy of the harness prefix, no display, no audio
    proton = os.environ.get('WIN_PROTON', os.path.expanduser(
        '~/.local/share/Steam/steamapps/common/Proton - Experimental/proton'))
    files = os.path.join(os.path.dirname(proton), 'files')
    wine, wsrv = os.path.join(files, 'bin', 'wine'), os.path.join(files, 'bin', 'wineserver')
    if not os.access(wine, os.X_OK):
        print('FAIL: no Proton wine at %s (WIN_PROTON; SOMVR_WIN_TESTS=build to build only)' % wine)
        return 1
    L = logs_dir()
    src_pfx = os.path.join(L, 'prefix', 'pfx')
    R = os.path.join(L, 'tests-%s-%d' % (time.strftime('%Y%m%d-%H%M%S'), os.getpid()))
    if not R.startswith('/') or not os.path.basename(R).startswith('tests-') or os.path.exists(R):
        print('FAIL: wintests: bad run folder %s' % R)
        return 1
    for k in ('pfx', 'home', 'run', 'cwd'):
        os.makedirs(os.path.join(R, k), mode=0o700, exist_ok=True)
    os.rmdir(os.path.join(R, 'pfx'))
    how = 'a copy of %s' % os.path.relpath(src_pfx, L)
    if not os.path.isdir(os.path.join(src_pfx, 'drive_c')):
        how = 'a new prefix (no %s yet: wine makes one, ~1 min)' % src_pfx
    env = {'HOME': os.path.join(R, 'home'), 'PATH': '/usr/bin:/bin', 'USER': os.environ.get('USER', 'user'),
           'LANG': 'C.UTF-8', 'XDG_RUNTIME_DIR': os.path.join(R, 'run'), 'WINEPREFIX': os.path.join(R, 'pfx'),
           'WINEDEBUG': '-all', 'WINEDLLOVERRIDES': WINE_DLLS}
    results = {}

    def run_one(item):
        n, objs, args = item
        if STOP.is_set():
            return n, 'stopped', 0.0, ''
        log = os.path.join(OUT, 'logs', n + '.log')
        t = time.time()
        with open(log, 'w') as f:
            try:
                rc = subprocess.run([wine, os.path.join(OUT, n + '.exe')] + [winarg(x) for x in args], env=env,
                                    cwd=os.path.join(R, 'cwd'), stdin=subprocess.DEVNULL, stdout=f,
                                    stderr=subprocess.STDOUT, timeout=a.timeout).returncode
            except subprocess.TimeoutExpired:
                rc = 'timeout %d s' % a.timeout
        return n, rc, time.time() - t, open(log, errors='replace').read()

    rc_all = 1 if (problems or errs or linkerr) else 0
    try:
        if how.startswith('a copy') and subprocess.run(['cp', '-a', '--reflink=auto', src_pfx,
                                                        os.path.join(R, 'pfx')]).returncode:
            print('FAIL: wintests: the prefix copy %s -> %s failed' % (src_pfx, R))
            return 1
        n, rc, dt, text = run_one(plan[0])  # the canary (also starts the wineserver, the copy's own)
        if rc != 7 or 'exitcode: 7' not in text:
            print('FAIL: the canary exited %s under wine (want 7): no test verdict would be seen\n%s' % (rc, text[-2000:]))
            return 1
        tr = time.time()
        with cf.ThreadPoolExecutor(jobs) as ex:
            for n, rc, dt, text in ex.map(run_one, plan[1:]):
                results[n] = (rc, dt, text)
        for n, objs, args in plan[1:]:
            rc, dt, text = results[n]
            ok = rc == 0
            print('win %-17s %s %5.1f s  %s' % (n, 'ok  ' if ok else 'FAIL', dt, last_line(text)))
            if not ok:
                rc_all = 1
                print('  exit %s; the last lines (build/win/tests/logs/%s.log):' % (rc, n))
                for l in text.replace('\r', '').rstrip().split('\n')[-25:]:
                    print('  | ' + l)
        nfail = sum(1 for r in results.values() if r[0] != 0)
        print('win tests: %d of %d passed under wine (%s), %d failed, %d not built; Linux-only: %s; '
              'build %.0f s, run %.0f s, total %.0f s' % (
                  len(results) - nfail, len(tests), how, nfail, len(tests) - len(results),
                  ', '.join(sorted(LINUX_ONLY) + ['selftest']), tb, time.time() - tr, time.time() - t0))
    finally:
        subprocess.run([wsrv, '-k'], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            subprocess.run([wsrv, '-w'], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=60)
        except subprocess.TimeoutExpired:
            print('wintests: the copy\'s wineserver did not stop in 60 s')
        if R.startswith(L + '/tests-'):
            shutil.rmtree(R, ignore_errors=True)
    return rc_all


if __name__ == '__main__':
    sys.exit(main())
