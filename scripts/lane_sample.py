#!/usr/bin/env python3
"""lane_sample.py RUN SECONDS - the lane measurement's pressure log (som-b37-lane2, docs/re-xr-pipeline.md 163.8): once a
second into RUN/lane-sample.txt, beside the game's SOMVR_PERF_RAW frames:
  S T_RAW T_MONO cpu_some cpu_full mem_some mem_full io_some io_full lane_cpu_us lane_thr_us lane_mem lane_high lane_oom
    swap_kb pswpin pswpout game_run_ns game_wait_ns game_threads
(PSI: /proc/pressure/*'s total stall microseconds; the lane's cgroup: cpu.stat usage/throttled, memory.current, its
memory.events high/oom_kill; the swap in use and /proc/vmstat's swap-ins/outs; the game process's threads' schedstat sums:
time on a CPU and time waiting for one), and every 5 s one `TH T_RAW TID COMM RUN_NS WAIT_NS` line per game thread that
ran 1 ms or more since the last (its busiest threads - the render thread, the game thread - waiting for a CPU is the
contention a frame feels). T_RAW is CLOCK_MONOTONIC_RAW (the Windows build's perf-raw clock under Wine). Reads only.
The game = the process whose command line names ShadowOfMordor.exe with RUN in its environment (the most threads).
"""
import os
import sys
import time

CG = "/sys/fs/cgroup/user.slice/user-%d.slice/user@%d.service/somvr.slice/somvr-lane.slice" % (os.getuid(), os.getuid())


def psi(kind):
    out = []
    try:
        for ln in open("/proc/pressure/" + kind):
            out.append(int(ln.split("total=")[1]))
    except (OSError, IndexError, ValueError):
        pass
    return (out + [0, 0])[:2]


def kv(path):
    d = {}
    try:
        for ln in open(path):
            p = ln.split()
            if len(p) == 2:
                d[p[0]] = int(p[1])
    except (OSError, ValueError):
        pass
    return d


def one(path):
    try:
        return int(open(path).read().split()[0])
    except (OSError, ValueError, IndexError):
        return 0


def game_pid(run):
    key = run.rstrip("/").encode() + b"/"
    best, bn = None, -1
    for d in os.listdir("/proc"):
        if not d.isdigit():
            continue
        try:
            if b"ShadowOfMordor.exe" not in open("/proc/%s/cmdline" % d, "rb").read():
                continue
            if key not in open("/proc/%s/environ" % d, "rb").read():
                continue
            n = len(os.listdir("/proc/%s/task" % d))
        except OSError:
            continue
        if n > bn:
            best, bn = int(d), n
    return best


def threads(pid):
    out = {}
    try:
        tids = os.listdir("/proc/%d/task" % pid)
    except OSError:
        return out
    for t in tids:
        try:
            r, w, _ = open("/proc/%d/task/%s/schedstat" % (pid, t)).read().split()
            c = open("/proc/%d/task/%s/comm" % (pid, t)).read().strip().replace(" ", "_") or "-"
            out[int(t)] = (c, int(r), int(w))
        except (OSError, ValueError):
            pass
    return out


def main():
    run, secs = sys.argv[1], float(sys.argv[2])
    o = open(os.path.join(run, "lane-sample.txt"), "a", buffering=1)
    o.write("# lane_sample 1: S T_RAW T_MONO cpu_some cpu_full mem_some mem_full io_some io_full lane_cpu_us lane_thr_us "
            "lane_mem lane_high lane_oom swap_kb pswpin pswpout game_run_ns game_wait_ns game_threads | "
            "TH T_RAW TID COMM RUN_NS WAIT_NS\n")
    t0 = time.monotonic()
    pid, last_th, k = None, {}, 0
    while time.monotonic() - t0 < secs:
        tick = time.monotonic()
        if pid is None or not os.path.exists("/proc/%d" % pid):
            pid = game_pid(run)
        tr, tm = time.clock_gettime(time.CLOCK_MONOTONIC_RAW), time.clock_gettime(time.CLOCK_MONOTONIC)
        c, m, i = psi("cpu"), psi("memory"), psi("io")
        cs, me = kv(os.path.join(CG, "cpu.stat")), kv(os.path.join(CG, "memory.events"))
        vm, mi = kv("/proc/vmstat"), {}
        try:
            for ln in open("/proc/meminfo"):
                p = ln.split()
                mi[p[0].rstrip(":")] = int(p[1])
        except (OSError, ValueError, IndexError):
            pass
        swap = mi.get("SwapTotal", 0) - mi.get("SwapFree", 0)
        th = threads(pid) if pid else {}
        o.write("S %.6f %.6f %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d\n" % (
            tr, tm, c[0], c[1], m[0], m[1], i[0], i[1], cs.get("usage_usec", 0), cs.get("throttled_usec", 0),
            one(os.path.join(CG, "memory.current")), me.get("high", 0), me.get("oom_kill", 0), swap,
            vm.get("pswpin", 0), vm.get("pswpout", 0), sum(v[1] for v in th.values()), sum(v[2] for v in th.values()),
            len(th)))
        k += 1
        if k % 5 == 0:
            for t, (cm, r, w) in sorted(th.items()):
                if r - last_th.get(t, (cm, 0, 0))[1] >= 1000000:
                    o.write("TH %.6f %d %s %d %d\n" % (tr, t, cm, r, w))
            last_th = th
        time.sleep(max(0.0, 1.0 - (time.monotonic() - tick)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
