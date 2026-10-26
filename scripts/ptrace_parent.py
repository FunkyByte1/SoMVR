#!/usr/bin/env python3
"""ptrace_parent.py LOGFILE -- CMD [ARGS...]

Start CMD as our child and stay its parent. The Feral build calls ptrace(PTRACE_TRACEME) (seen 2026-10-17 23:55: its
main thread showed TracerPid = its parent, the boot_check.sh bash), which makes the parent its tracer; under a
parent that is not ptrace-aware (bash, a reaper) the next signal (here SIGCHLD of a FeralLinuxMessage alert) leaves it
frozen in "tracing stop" for good. This parent passes every signal stop straight through (PTRACE_CONT with the same
signal; job-control stop signals are suppressed), so the game behaves as if nobody traced it. Untraced children are
simply waited for. SIGTERM / SIGINT / SIGHUP sent to us go to the child. Exit status = the child's (128+N if killed).
Test harness only (no game code, nothing injected).
"""
import ctypes
import os
import signal
import sys
import time

PTRACE_CONT = 7
WALL = 0x40000000  # __WALL: also report the child's non-leader threads (TRACEME traces only the calling thread)
JOBCTL = {signal.SIGSTOP, signal.SIGTSTP, signal.SIGTTIN, signal.SIGTTOU}


def main(argv):
    if len(argv) < 4 or argv[2] != "--":
        print(__doc__, file=sys.stderr)
        return 2
    log = open(argv[1], "a", buffering=1)
    cmd = argv[3:]
    libc = ctypes.CDLL(None, use_errno=True)
    libc.ptrace.argtypes = [ctypes.c_long, ctypes.c_long, ctypes.c_void_p, ctypes.c_void_p]
    libc.ptrace.restype = ctypes.c_long

    pid = os.fork()
    if pid == 0:
        try:
            os.execv(cmd[0], cmd)
        finally:
            os._exit(127)

    def fwd(sig, _frame):
        try:
            os.kill(pid, sig)
        except OSError:
            pass

    for s in (signal.SIGTERM, signal.SIGINT, signal.SIGHUP):
        signal.signal(s, fwd)
    log.write("%s ptrace_parent: child %d: %s\n" % (time.strftime("%T"), pid, " ".join(cmd[:3]) + " ..."))
    counts = {}
    code = 1
    while True:
        try:
            w, st = os.waitpid(-1, WALL)
        except ChildProcessError:
            break
        if os.WIFSTOPPED(st):
            sig = os.WSTOPSIG(st)
            n = counts[sig] = counts.get(sig, 0) + 1
            if n == 1 or n % 500 == 0:
                log.write("%s traced stop of %d: signal %d (%s), #%d - passed through\n"
                          % (time.strftime("%T"), w, sig, signal.Signals(sig).name if sig in signal.Signals._value2member_map_ else "?", n))
            deliver = 0 if sig in JOBCTL else sig
            if libc.ptrace(PTRACE_CONT, w, None, ctypes.c_void_p(deliver)) != 0:
                log.write("%s PTRACE_CONT %d failed: errno %d\n" % (time.strftime("%T"), w, ctypes.get_errno()))
            continue
        if w == pid and (os.WIFEXITED(st) or os.WIFSIGNALED(st)):
            code = os.WEXITSTATUS(st) if os.WIFEXITED(st) else 128 + os.WTERMSIG(st)
            log.write("%s child %d ended: %s %d; stops seen %s\n" % (
                time.strftime("%T"), pid, "exit" if os.WIFEXITED(st) else "signal",
                os.WEXITSTATUS(st) if os.WIFEXITED(st) else os.WTERMSIG(st), counts))
            break
    return code


if __name__ == "__main__":
    sys.exit(main(sys.argv))
