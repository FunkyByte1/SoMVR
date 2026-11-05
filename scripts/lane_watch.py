#!/usr/bin/env python3
"""lane_watch.py - the VR-safe CPU lane's watcher and closed loop (som-b35-lane, docs/re-xr-pipeline.md 163).

  lane_watch.py watch     the daemon: the user unit somvr-lane-watch (lane_run.sh starts it). Every 0.05 s:
                          - keeps every thread in somvr-lane.slice on LANE_CPUS (affinity; no cpuset without root);
                          - the user in VR (status.json "user": true or somvr-headset active) with LANE_IN_VR=0: the
                            lane frozen (cgroup.freeze) for the whole session - the old rule, enforced;
                          - a session it protects (LANE_IN_VR=1 and the user's session's perf-raw.txt, or a file in
                            $XDG_RUNTIME_DIR/somvr-lane/protect.d/): the closed loop on that session's own frame times
                            (SOMVR_PERF_RAW: every present's interval and render-thread work) - the lane frozen within
                            ~0.5 s when the frames rise above the session's own baseline (measured while the lane was
                            frozen or empty), thawed after a calm stretch; the session's processes pinned to GAME_CPUS
                            while lane jobs run (their old affinity saved, given back after);
                          - every freeze/thaw/pin/unpin logged (logs/lane/lane-YYYYMMDD.log, both clocks).
                          It exits after 120 s with an empty lane, nothing pinned, nothing protected.
  lane_watch.py status    the lane, the watcher, the protections, the pins (read only)
  lane_watch.py unpin     give back every saved affinity (if a watcher died with threads pinned)
  lane_watch.py placement TARGET [S]  where TARGET's busiest threads run (read only; TARGET as a protect file's pin=)
  lane_watch.py test      the closed loop's decisions on synthetic sessions (offline)

A protect file (protect.d/NAME.conf, one KEY=VALUE a line): raw=/ABS/perf-raw.txt (the frames), pin=TARGET (repeatable:
a run folder = every process whose environment names it; unit:NAME = a systemd user unit's cgroup; /sys/fs/cgroup/...),
loop=on|off (off: pins only, never pauses - for measurements of the lane's raw cost).
"""
import glob
import json
import os
import statistics
import subprocess
import sys
import time
from collections import deque

HERE = os.path.dirname(os.path.abspath(__file__))
SOM = "/home/bara/Projects/flat2vr/ShadowOfMordor"
RT = os.environ.get("XDG_RUNTIME_DIR") or "/run/user/%d" % os.getuid()
STATE = os.path.join(RT, "somvr-lane")
PROTECT_D = os.path.join(STATE, "protect.d")
PINS = os.path.join(STATE, "pins.json")
CG_ROOT = "/sys/fs/cgroup"
TICK = 0.05


# ---- configuration -------------------------------------------------------------------------------------------------
def load_conf():
    c = {}
    for f in (os.path.join(HERE, "lane", "lane.conf"), os.path.expanduser("~/.config/somvr-lane/lane.conf")):
        try:
            for ln in open(f):
                ln = ln.strip()
                if ln and not ln.startswith("#") and "=" in ln:
                    k, v = ln.split("=", 1)
                    c[k.strip()] = v.strip().strip('"')   # (shell syntax: a quoted value)
        except OSError:
            pass
    for k in list(c) + ["LANE_CPUS", "GAME_CPUS", "LANE_IN_VR", "LANE_CALM_S", "LANE_LATE_X", "LANE_REBASE_S",
                        "LANE_VR_JOBS", "LANE_VR_QUOTA", "LANE_QUOTA", "LANE_VR_LOOP", "LANE_VR_NEED_FRAMES",
                        "LANE_VR_GUARDED", "LANE_ATTRIB", "LANE_ACTIVE_PCT"]:
        if k in os.environ:
            c[k] = os.environ[k]
    return c


def cpus(spec):
    s = set()
    for p in str(spec).split(","):
        p = p.strip()
        if p:
            a, _, b = p.partition("-")
            s.update(range(int(a), int(b or a) + 1))
    return s


def fmt_cpus(s):
    return ",".join(str(c) for c in sorted(s))


# ---- logging -------------------------------------------------------------------------------------------------------
class Log:
    def __init__(self, path=None):
        self.path = path

    def __call__(self, msg):
        now = time.time()
        line = "%s.%03d mono=%.3f raw=%.3f %s" % (time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(now)),
                                                 int(now * 1000) % 1000, time.clock_gettime(time.CLOCK_MONOTONIC),
                                                 time.clock_gettime(time.CLOCK_MONOTONIC_RAW), msg)
        print(line, flush=True)
        p = self.path or os.path.join(SOM, "logs", "lane", "lane-%s.log" % time.strftime("%Y%m%d"))
        try:
            os.makedirs(os.path.dirname(p), exist_ok=True)
            with open(p, "a") as f:
                f.write(line + "\n")
        except OSError:
            pass


# ---- cgroups -------------------------------------------------------------------------------------------------------
def unit_cgroup(unit):
    try:
        r = subprocess.run(["systemctl", "--user", "show", "-p", "ControlGroup", "--value", unit],
                           capture_output=True, text=True, timeout=5)
        cg = r.stdout.strip()
        return CG_ROOT + cg if cg else None
    except (OSError, subprocess.SubprocessError):
        return None


def lane_cgroup():
    # systemd: somvr-lane.slice sits in its implicit parent somvr.slice
    p = "%s/user.slice/user-%d.slice/user@%d.service/somvr.slice/somvr-lane.slice" % (CG_ROOT, os.getuid(), os.getuid())
    return p if os.path.isdir(p) else (unit_cgroup("somvr-lane.slice") or p)


def cg_ids(cg, what="cgroup.threads"):
    out = []
    if not cg or not os.path.isdir(cg):
        return out
    for d, _, files in os.walk(cg):
        if what in files:
            try:
                out += [int(x) for x in open(os.path.join(d, what)).read().split()]
            except (OSError, ValueError):
                pass
    return out


def cg_frozen(cg):
    try:
        for ln in open(os.path.join(cg, "cgroup.events")):
            if ln.startswith("frozen "):
                return ln.split()[1] == "1"
    except OSError:
        pass
    return False


def cg_freeze(cg, on):
    try:
        with open(os.path.join(cg, "cgroup.freeze"), "w") as f:
            f.write("1" if on else "0")
        return True
    except OSError:
        return False


def cg_want_frozen(cg):
    try:
        return open(os.path.join(cg, "cgroup.freeze")).read().strip() == "1"
    except OSError:
        return False


# ---- the user's VR -------------------------------------------------------------------------------------------------
def vr_jobs(conf):
    """LANE_VR_JOBS: the job names (glob patterns, space separated; `all` = every job) that may run while the user is
    in VR with LANE_IN_VR=1 (b37 lane2: the rest wait, their scopes frozen)"""
    return conf.get("LANE_VR_JOBS", "all").split() or ["all"]


def job_name(scope):
    """lane-NAME-PID-TS.scope (lane_run.sh's unit name) -> NAME"""
    b = os.path.basename(scope)
    if not (b.startswith("lane-") and b.endswith(".scope")):
        return None
    p = b[5:-6].rsplit("-", 2)
    return p[0] if len(p) == 3 else b[5:-6]


def vr_allowed(name, pats):
    import fnmatch
    return "all" in pats or any(fnmatch.fnmatchcase(name, q) for q in pats)


def job_scopes(lane):
    try:
        return [os.path.join(lane, d) for d in os.listdir(lane) if d.startswith("lane-") and d.endswith(".scope")]
    except OSError:
        return []


SOLO = "/tmp/somvr-slots-solo.lock"   # scripts/game_slot.sh's solo request (the launcher takes it as user-session)


def user_session_req():
    """b37: the slots' solo request taken as `user-session` - the user's launcher is starting or running a session"""
    try:
        with open(os.path.join(SOLO, "owner")) as f:
            return f.readline().startswith("user-session")
    except OSError:
        return False


def user_in_vr(fake_ok=True):
    """the user in VR: status.json "user": true, a user-session request, or somvr-headset active. fake_ok=False: the
    real signs only (LANE_FAKE_VR - a test's - ignored)"""
    fake = os.environ.get("LANE_FAKE_VR")   # (tests only: this file's existence = the user is in VR)
    if fake and fake_ok:
        return os.path.exists(fake)
    if user_session_req():
        return True
    try:
        st = json.load(open(os.path.join(RT, "vr-session", "status.json")))
        if st.get("user"):
            return True
    except (OSError, ValueError):
        pass
    r = subprocess.run(["systemctl", "--user", "is-active", "-q", "somvr-headset"], capture_output=True)
    return r.returncode == 0


# ---- frames (SOMVR_PERF_RAW) ---------------------------------------------------------------------------------------
class Raw:
    """Tails one perf-raw.txt: frames (t, interval ms, work ms or None), `# perf NAME` marks, its clock."""

    NOFRAMES_S = 3.0   # b37 live3: no frame read this long = no frames (the session's file stopped or became unreadable)

    def __init__(self, path, tail=0):
        self.path, self.off, self.part, self.clock = path, 0, b"", time.CLOCK_MONOTONIC
        self.marks = []
        self.t_frame, self.stale, self.truncated = time.monotonic(), False, 0   # b37 live3
        try:
            with open(path, "rb") as f:
                if b"monotonic_raw" in f.readline():   # the header: the game's clock (Wine: MONOTONIC_RAW)
                    self.clock = time.CLOCK_MONOTONIC_RAW
                self.off = max(0, os.path.getsize(path) - tail) if tail else 0
        except OSError:
            pass

    def read(self):
        out = []
        try:
            with open(self.path, "rb") as f:
                size = f.seek(0, 2)
                if size < self.off:   # b37 live3: the file began again (build 37's hot reload reopened it with O_TRUNC:
                    self.off, self.part = 0, b""   # this reader stayed past its new end and saw no frames)
                    self.truncated += 1
                f.seek(self.off)
                b = f.read()
        except OSError:
            return out
        self.off += len(b)
        b = self.part + b
        lines = b.split(b"\n")
        self.part = lines.pop()
        for ln in lines:
            s = ln.decode("ascii", "replace").split()
            if not s:
                continue
            if s[0] == "#":
                if "monotonic_raw" in ln.decode("ascii", "replace"):
                    self.clock = time.CLOCK_MONOTONIC_RAW
                if len(s) >= 4 and s[1] == "perf":
                    self.marks.append((float(s[-1]), " ".join(s[2:-1])))
                continue
            try:
                t = float(s[0])
                iv = float(s[1]) if s[1] != "-" else None
                wk = float(s[2]) if len(s) > 2 and s[2] != "-" else None
            except (ValueError, IndexError):
                continue
            out.append((t, iv, wk))
        return out

    def now(self):
        return time.clock_gettime(self.clock)


def pct(sorted_v, q):
    if not sorted_v:
        return None
    k = min(len(sorted_v) - 1, max(0, int(round(q / 100.0 * (len(sorted_v) - 1)))))
    return sorted_v[k]


def binom_tail(n, k, p):
    """P(X >= k) for X ~ Binomial(n, p)"""
    if k <= 0:
        return 1.0
    from math import comb
    return sum(comb(n, i) * p ** i * (1 - p) ** (n - i) for i in range(k, n + 1))


class Loop:
    """The closed loop's decisions for one session (pure: times are the frames' own clock)."""
    WIN_S, WIN_MIN, LATE_S, BASE_N, BASE_MIN, SETTLE_S = 0.5, 12, 1.0, 1200, 100, 0.3
    # b37 live3 (163.8): whose late frames - the seconds with a burst (>= 2 late frames) while the lane is frozen or
    # idle (the last ATT_CALM_N such seconds) against the last ATT_BUSY_N seconds the lane worked (within ATT_BUSY_S)
    ATT_CALM_N, ATT_BUSY_N, ATT_BUSY_S, ATT_P = 300, 4, 30.0, 0.02

    def __init__(self, calm_s=3.0, late_x=1.5, rebase_s=60.0, mode="work", attrib=True):
        # mode (b37): "work" = pause on the render thread's work rising (the median over the baseline's p90, 3 of 4
        # frames over its p99) or on late frames; "late" = on late frames only (what the headset shows: a compositor
        # slot without a new frame) - the user's session's mode (LANE_VR_LOOP), where the builds' small work rise is free
        # attrib (b37 live3, LANE_ATTRIB=1): late frames pause the lane only when the lane plausibly made them - a
        # session late on its own (GPU-bound at 120 Hz: as often with the lane frozen) never holds it (plausible())
        self.mode, self.attrib = mode, attrib
        self.bins = {}               # second -> [calm frames, busy frames, late frames] (the seconds not yet closed)
        self.hist_calm = deque(maxlen=self.ATT_CALM_N)   # a burst (bool) per closed second, the lane frozen or idle
        self.hist_busy = deque(maxlen=60)                # (second, burst) per closed second, the lane working
        self.note_t = None
        self.n_ignored = 0
        self.calm0, self.calm_s, self.late_x, self.rebase_s = calm_s, calm_s, late_x, rebase_s
        self.base = deque()          # (t, x, iv) calm frames
        self.recent = deque()        # (t, x, iv) the last 3 s
        self.calm_from = None        # the lane frozen or empty since (frames' clock)
        self.last_calm = None        # the last closed calm interval (a, b)
        self.paused_at = None        # our pause (frames' clock); None = not paused by us
        self.pause_calm = calm_s     # this pause's calm stretch
        self.reason = ""
        self.resumed_at = None
        self.run_from = None         # lane work since (for the re-measure)
        self.stats = None
        self.stats_n = -1

    def set_calm(self, calm, t):
        if calm and self.calm_from is None:
            self.calm_from = t
        elif not calm and self.calm_from is not None:
            self.last_calm = (self.calm_from, t)
            self.calm_from = None

    def _is_calm(self, t):
        if self.calm_from is not None and t >= self.calm_from + self.SETTLE_S:
            return True
        a = self.last_calm
        return a is not None and a[0] + self.SETTLE_S <= t <= a[1]

    def _in_calm(self, t):   # inside a calm interval at all (the settle included)
        if self.calm_from is not None and t >= self.calm_from:
            return True
        a = self.last_calm
        return a is not None and a[0] <= t <= a[1]

    def feed(self, frames):
        iv50 = self.stats["iv50"] if self.stats else None
        for t, iv, wk in frames:
            x = wk if wk is not None else iv
            if x is None:
                continue
            self.recent.append((t, x, iv))
            calm = self._is_calm(t)
            if calm:
                self.base.append((t, x, iv))
            if iv50 and iv is not None:   # b37 live3: the second's frames by the lane's state (the settle in neither)
                b = self.bins.setdefault(int(t), [0, 0, 0])
                if calm:
                    b[0] += 1
                elif not self._in_calm(t):
                    b[1] += 1
                b[2] += iv > self.late_x * iv50
        if self.bins and self.recent:
            now_s = int(self.recent[-1][0])
            for s in sorted(k for k in self.bins if k < now_s):
                nc, nb, late = self.bins.pop(s)
                if nc and not nb:
                    self.hist_calm.append(late >= 2)
                elif nb and not nc:
                    self.hist_busy.append((s, late >= 2))
        if self.recent:
            tl = self.recent[-1][0]
            while self.recent and self.recent[0][0] < tl - 3.0:
                self.recent.popleft()
            while len(self.base) > self.BASE_N:   # the last 1200 calm frames (13 s at 90 Hz, 60 s at 20 Hz)
                self.base.popleft()

    def baseline(self):
        if len(self.base) == self.stats_n:
            return self.stats
        self.stats_n = len(self.base)
        if len(self.base) < self.BASE_MIN:
            self.stats = None
            return None
        xs = sorted(b[1] for b in self.base)
        ivs = sorted(b[2] for b in self.base if b[2] is not None)
        mi = pct(ivs, 50) if ivs else None
        late = sum(1 for v in ivs if mi and v > self.late_x * mi)
        self.stats = {"n": len(xs), "p50": pct(xs, 50), "p75": pct(xs, 75), "p90": pct(xs, 90), "p95": pct(xs, 95),
                      "p99": pct(xs, 99), "iv50": mi, "late_rate": late / len(ivs) if ivs else 0.0}
        return self.stats

    def decide(self, lane_busy, t):
        """-> ('pause'|'probe'|'resume'|None, reason). lane_busy: jobs in the lane and the lane not frozen."""
        b = self.baseline()
        if self.paused_at is not None:   # ours: the lane frozen for this session
            calm_n = sum(1 for f in self.base if f[0] >= self.paused_at + self.SETTLE_S)
            if t - self.paused_at >= self.pause_calm and calm_n >= 20 and b is not None:
                self.resumed_at, self.paused_at, self.run_from = t, None, t
                return "resume", "calm %.1f s, %d frames measured; the baseline p50 %.2f p90 %.2f p99 %.2f ms" % (
                    self.pause_calm, calm_n, b["p50"], b["p90"], b["p99"])
            return None, ""
        if not lane_busy:
            self.run_from = None
            if self.resumed_at is not None and t - self.resumed_at > 60:
                self.calm_s = self.calm0
            return None, ""
        if self.run_from is None:
            self.run_from = t
        if b is None:
            return self._pause(t, "probe", "no baseline yet (%d calm frames): the lane frozen to measure the session"
                               % len(self.base), 2.0)
        w = [f for f in self.recent if f[0] > t - self.WIN_S]
        if len(w) < self.WIN_MIN:
            w = list(self.recent)[-self.WIN_MIN:]
        if len(w) >= self.WIN_MIN and self.mode == "work":
            med = statistics.median(f[1] for f in w)
            if med > b["p90"] and med > b["p50"] + 0.2:
                return self._pause(t, "pause", "the last %.1f s's median %.2f ms > the baseline's p90 %.2f (p50 %.2f, "
                                   "p99 %.2f; %d frames)" % (self.WIN_S, med, b["p90"], b["p50"], b["p99"], len(w)))
            last4 = list(self.recent)[-4:]
            hi = sum(1 for f in last4 if f[1] > b["p99"])
            if hi >= 3:
                return self._pause(t, "pause", "%d of the last 4 frames over the baseline's p99 %.2f ms (%s)" % (
                    hi, b["p99"], " ".join("%.2f" % f[1] for f in last4)))
        if b["iv50"]:
            lw = [f for f in self.recent if f[0] > t - self.LATE_S and f[2] is not None]
            late = sum(1 for f in lw if f[2] > self.late_x * b["iv50"])
            want = max(2, int(2 * b["late_rate"] * len(lw)) + 1)
            if late >= want:
                ok, att = self.plausible(t)
                if not ok:   # b37 live3: late as often without the lane: not its doing - the lane goes on
                    self.n_ignored += 1
                    if self.note_t is None or t - self.note_t >= 30:
                        self.note_t = t
                        return "note", "%d late frames in %.1f s, not the lane's (%s; %d such bursts let through)" % (
                            late, self.LATE_S, att, self.n_ignored)
                    return None, ""
                return self._pause(t, "pause", "%d late frames in %.1f s (an interval over %.1f x %.2f ms; the "
                                   "baseline's rate %.2f %%%s)" % (late, self.LATE_S, self.late_x, b["iv50"],
                                                                   100 * b["late_rate"], "; " + att if att else ""))
        if self.rebase_s and t - self.run_from >= self.rebase_s:
            return self._pause(t, "probe", "%.0f s of lane work: the baseline measured again" % self.rebase_s, 2.0)
        return None, ""

    def plausible(self, t):
        """(b37 live3) a burst of late frames now, the lane working: could the lane have made it? The seconds with a
        burst while the lane was frozen or idle give the session's own rate f (smoothed: (bursts + 0.1) / (seconds +
        20) - 0.5 % with no history, so a clean session pauses at once); this burst and the last ATT_BUSY_N seconds the
        lane worked (within ATT_BUSY_S) are k bursts in n seconds. Plausible when k or more in n is unlikely at f
        (P < ATT_P): a GPU-bound session (bursts in 5-7 % of its seconds, frozen or not) needs 3 of 5 working seconds,
        a clean one 1 of 1 or 2 of 5. -> (bool, the numbers)"""
        if not self.attrib:
            return True, ""
        nc, bc = len(self.hist_calm), sum(self.hist_calm)
        f = (bc + 0.1) / (nc + 20)
        rb = [x for s, x in self.hist_busy if s >= t - self.ATT_BUSY_S][-self.ATT_BUSY_N:]
        n, k = 1 + len(rb), 1 + sum(rb)
        p = binom_tail(n, k, f)
        return p < self.ATT_P, "bursts in %d of the lane's last %d working seconds; %d of %d s frozen or idle = %.1f %%: " \
            "P %.3f" % (k, n, bc, nc, 100 * f, p)

    def _pause(self, t, kind, why, calm=None):
        if kind == "pause":
            if self.resumed_at is not None and t - self.resumed_at < 10 and \
                    (not self.attrib or len(self.hist_calm) >= 60):   # (b37 live3: only once the session's own rate is known)
                self.calm_s = min(60.0, self.calm_s * 2)   # it came back at once: a longer calm next time
            calm = self.calm_s
        self.paused_at, self.pause_calm, self.reason, self.run_from = t, calm, why, None
        return kind, why + " - calm %.1f s before the lane goes on" % calm


# ---- pins ----------------------------------------------------------------------------------------------------------
def proc_tids(pid):
    try:
        return [int(t) for t in os.listdir("/proc/%d/task" % pid)]
    except OSError:
        return []


def pids_of(target):
    """target: a run folder (every process whose environment names it), unit:NAME, or a cgroup path"""
    if target.startswith("unit:"):
        cg = unit_cgroup(target[5:])
        return cg_ids(cg, "cgroup.procs") if cg else []
    if target.startswith(CG_ROOT):
        return cg_ids(target, "cgroup.procs")
    key = target.rstrip("/").encode() + b"/"
    out = []
    me = os.getpid()
    for d in os.listdir("/proc"):
        if not d.isdigit() or int(d) == me:
            continue
        try:
            env = open("/proc/%s/environ" % d, "rb").read()
        except OSError:
            continue
        if key in env:
            out.append(int(d))
    return out


class Pins:
    def __init__(self, log, game):
        self.log, self.game = log, game
        self.saved = {}  # tid -> [cpus], pid of it
        self.load()

    def load(self):
        try:
            d = json.load(open(PINS))
            self.saved = {int(k): v for k, v in d.get("saved", {}).items()}
        except (OSError, ValueError):
            self.saved = {}

    def store(self):
        os.makedirs(STATE, exist_ok=True)
        tmp = PINS + ".tmp"
        with open(tmp, "w") as f:
            json.dump({"saved": self.saved}, f)
        os.replace(tmp, PINS)

    def pin(self, pids, why):
        n_new = 0
        for pid in pids:
            for tid in proc_tids(pid):
                try:
                    cur = os.sched_getaffinity(tid)
                except OSError:
                    continue
                if cur == self.game:
                    continue
                if tid not in self.saved:
                    self.saved[tid] = {"cpus": sorted(cur), "pid": pid}
                try:
                    os.sched_setaffinity(tid, self.game)
                    n_new += 1
                except OSError:
                    pass
        if n_new:
            self.store()
            self.log("PIN %d threads of %d processes -> CPUs %s (%s; %d saved in all)" % (
                n_new, len(pids), fmt_cpus(self.game), why, len(self.saved)))

    def unpin(self, why):
        if not self.saved:
            return
        n = gone = 0
        for tid, s in sorted(self.saved.items()):
            try:
                os.sched_setaffinity(tid, set(s["cpus"]))
                n += 1
            except OSError:
                gone += 1
        # threads born while pinned whose start we never saw: their process's saved affinity
        for pid in {s["pid"] for s in self.saved.values()}:
            main = self.saved.get(pid)
            for tid in proc_tids(pid):
                if tid in self.saved or not main:
                    continue
                try:
                    if os.sched_getaffinity(tid) == self.game:
                        os.sched_setaffinity(tid, set(main["cpus"]))
                        n += 1
                except OSError:
                    pass
        self.log("UNPIN %d threads given their affinity back (%d gone) (%s)" % (n, gone, why))
        self.saved = {}
        try:
            os.unlink(PINS)
        except OSError:
            pass


# ---- the daemon ----------------------------------------------------------------------------------------------------
def read_protects():
    out = {}
    for f in sorted(glob.glob(os.path.join(PROTECT_D, "*.conf"))):
        d = {"pin": [], "loop": "on", "raw": None}
        try:
            for ln in open(f):
                ln = ln.strip()
                if "=" not in ln or ln.startswith("#"):
                    continue
                k, v = ln.split("=", 1)
                if k == "pin":
                    d["pin"].append(v)
                else:
                    d[k] = v
        except OSError:
            continue
        out[os.path.basename(f)[:-5]] = d
    return out


def headset_run():
    """b37: the user's session's own run folder - SOM_BOOTCHECK_RUN in the environment of a process of the somvr-headset
    unit (som-win-session.sh gives the game it: logs/win-session-*; the Linux launcher's boot_check.sh: its own run),
    else None"""
    cg = unit_cgroup("somvr-headset")
    if not cg:
        return None
    for pid in cg_ids(cg, "cgroup.procs"):
        try:
            with open("/proc/%d/environ" % pid, "rb") as f:
                env = f.read().split(b"\0")
        except OSError:
            continue
        for kv in env:
            if kv.startswith(b"SOM_BOOTCHECK_RUN="):
                return kv.split(b"=", 1)[1].decode(errors="replace")
    return None


def user_raw(som=SOM, run_of=headset_run, now=None):
    """the user's running session's perf-raw.txt (fresh: written in the last 3 s) - b37: only the user's own run: the
    somvr-headset unit's (headset_run), else the freshest logs/win-session-*; a helper's headless boot
    (logs/boot-check-*, every one writes perf-raw.txt with SOMVR_PERF_RAW in the env) never stands in for it"""
    now = time.time() if now is None else now

    def fresh(p):
        try:
            return now - os.stat(p).st_mtime < 3.0
        except OSError:
            return False
    best = os.environ.get("LANE_USER_RAW")   # (tests only: this file stands for the user's session)
    if best:
        return best if fresh(best) else None
    r = run_of() if run_of else None
    if r:   # the unit's own run: that one only (not fresh yet - a load - is no frames, never another run's)
        p = os.path.join(r, "perf-raw.txt")
        return p if fresh(p) else None
    best, bt = None, 0.0
    for p in glob.glob(os.path.join(som, "logs", "win-session-*", "perf-raw.txt")):
        try:
            m = os.stat(p).st_mtime
        except OSError:
            continue
        if m > bt:
            best, bt = p, m
    return best if best and fresh(best) else None


def protects_for(protects, real_vr):
    """b37: while the real user is in VR, a protect file whose frames are a helper's headless boot (logs/boot-check-*)
    is not followed - those are the lane's test mode only (a LANE_FAKE_VR test, or no user session)"""
    if not real_vr:
        return protects
    return {n: d for n, d in protects.items() if "/logs/boot-check-" not in (d.get("raw") or "")}


def watch(argv):
    conf = load_conf()
    log = Log(os.environ.get("LANE_LOG"))
    lane_set, game_set = cpus(conf.get("LANE_CPUS", "0,2,8,10")), cpus(conf.get("GAME_CPUS", "1,3-7,9,11-15"))
    in_vr_ok = conf.get("LANE_IN_VR", "0") == "1"
    pats = vr_jobs(conf)
    # b37: LANE_VR_GUARDED = jobs that may run in VR only while the closed loop watches the session's frames (a fresh
    # SOMVR_PERF_RAW): the speech models, not free by the rule but with no more late frames (docs 163.6)
    guarded = conf.get("LANE_VR_GUARDED", "").split()
    # b37: LANE_VR_QUOTA (e.g. 100%: one CPU's time) = the lane's CPU limit while the user is in VR, written into its
    # cgroup's cpu.max (and again every second: systemd writes the slice's own when it starts a job's scope); the
    # slice's own (LANE_QUOTA) back when the session ends. Unset = the slice's limit in VR too.
    vr_quota = conf.get("LANE_VR_QUOTA", "").rstrip("%")
    vr_quota = int(vr_quota) if vr_quota.isdigit() and int(vr_quota) > 0 else None
    base_quota = conf.get("LANE_QUOTA", "200%").rstrip("%")
    base_quota = int(base_quota) if base_quota.isdigit() else 200
    quota_set = [False]

    def set_quota(pct, why):
        try:
            per = int(open(os.path.join(lane, "cpu.max")).read().split()[1])
            want = "%d %d" % (per * pct // 100, per)
            if open(os.path.join(lane, "cpu.max")).read().strip() != want:
                with open(os.path.join(lane, "cpu.max"), "w") as f:
                    f.write(want)
                log("QUOTA the lane's cpu.max %s (%d%%: %s)" % (want, pct, why))
            return True
        except (OSError, ValueError, IndexError):
            return False
    calm_s, late_x = float(conf.get("LANE_CALM_S", 3)), float(conf.get("LANE_LATE_X", 1.5))
    rebase_s = float(conf.get("LANE_REBASE_S", 60))
    # b37 live3 (163.8): LANE_ATTRIB=1 (the default) - late frames hold the lane only when it plausibly made them
    # (Loop.plausible); LANE_ACTIVE_PCT (5) - the lane counts as working only while its cgroup used at least this much
    # of one CPU over the last 0.5 s (an idle speech worker waiting for its next line is not a load: its frames are calm)
    attrib = conf.get("LANE_ATTRIB", "1") != "0"
    active_pct = float(conf.get("LANE_ACTIVE_PCT", 5) or 5)
    use = deque()   # (monotonic, the lane cgroup's usage_usec)
    vr_loop = conf.get("LANE_VR_LOOP", "late")   # b37: the user's session's closed loop: late frames only
    # b37: LANE_VR_NEED_FRAMES=1 = no lane job in VR without the session's frames (SOMVR_PERF_RAW); 0 = the jobs of
    # LANE_VR_JOBS (measured free) run without them, under the closed loop whenever the session writes them
    need_frames = conf.get("LANE_VR_NEED_FRAMES", "0") == "1"
    os.makedirs(PROTECT_D, exist_ok=True)
    pins = Pins(log, game_set)
    if pins.saved:
        log("START: %d saved affinities from a watcher before: kept until the lane is idle" % len(pins.saved))
    log("START lane_watch (pid %d): lane CPUs %s, game CPUs %s, in VR %s, calm %.1f s, late x%.1f, re-measure %.0f s, "
        "late frames %s, working at %.0f %% of a CPU" % (
            os.getpid(), fmt_cpus(lane_set), fmt_cpus(game_set),
            ("under the closed loop, jobs %s" % " ".join(pats)) if in_vr_ok else "frozen",
            calm_s, late_x, rebase_s, "only when the lane's (LANE_ATTRIB=1)" if attrib else "always (LANE_ATTRIB=0)",
            active_pct))
    loops, raws = {}, {}
    lane = lane_cgroup()
    frozen, why_frozen = cg_want_frozen(lane), "found frozen at the start"

    def stop(sig, frm):   # systemctl stop / uninstall: every pinned thread back; the lane thawed unless the user is in VR
        pins.unpin("the watcher stopped (signal %d)" % sig)
        if quota_set[0]:
            set_quota(base_quota, "the watcher stopped")
        if not user_in_vr():
            for sc in job_scopes(lane):
                if cg_want_frozen(sc):
                    cg_freeze(sc, False)
                    log("THAW job %s (the watcher stopped)" % job_name(sc))
        if cg_want_frozen(lane) and not (user_in_vr() and not in_vr_ok):
            cg_freeze(lane, False)
            log("THAW the lane (the watcher stopped)")
        log("EXIT on signal %d" % sig)
        sys.exit(0)
    import signal
    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    if frozen:
        log("the lane was frozen at the start (a watcher before): held until this one decides")
    t_slow = t_busy = t_pin_need = 0.0
    t_busy = time.monotonic()
    vr, protects, uraw, aff_fixed, had_jobs = False, {}, None, set(), False
    dropped = set()   # b37: protect files not followed while the user is in VR (logged once each)
    while True:
        tick0 = time.monotonic()
        slow = tick0 - t_slow >= 1.0
        if slow:   # the slow checks: once a second
            t_slow = tick0
            lane = lane_cgroup()
            vr = user_in_vr()
            allp = read_protects()
            protects = protects_for(allp, user_in_vr(fake_ok=False))
            for n in sorted(set(allp) - set(protects)):
                if n not in dropped:
                    dropped.add(n)
                    log("PROTECT %s not followed: the user is in VR and its frames are a helper's boot (%s)" % (n, allp[n].get("raw")))
            dropped &= set(allp) - set(protects)
            uraw = user_raw() if vr and in_vr_ok else None
            for tid in cg_ids(lane):   # every lane thread on the lane's CPUs
                try:
                    if not os.sched_getaffinity(tid) <= lane_set:
                        os.sched_setaffinity(tid, lane_set)
                        if tid not in aff_fixed:
                            aff_fixed.add(tid)
                            log("AFFINITY lane thread %d had other CPUs: set to %s" % (tid, fmt_cpus(lane_set)))
                except OSError:
                    pass
        jobs = cg_ids(lane, "cgroup.procs")
        try:   # b37 live3: is the lane working (its CPU time over the last 0.5 s)?
            with open(os.path.join(lane, "cpu.stat")) as f:
                u = int(f.readline().split()[1])
        except (OSError, ValueError, IndexError):
            u = None
        if u is not None:
            use.append((tick0, u))
        while len(use) > 1 and use[1][0] <= tick0 - 0.5:
            use.popleft()
        active = u is None or len(use) < 2 or \
            (use[-1][1] - use[0][1]) / max(1e-3, use[-1][0] - use[0][0]) / 1e4 >= active_pct
        # the sessions protected this tick: name -> (raw path, closed loop on, pin targets)
        src = {n: (d["raw"], d.get("loop", "on") != "off", d["pin"]) for n, d in protects.items() if d.get("raw")}
        modes = {n: ("late" if d.get("loop") == "late" else "work") for n, d in protects.items()}
        if uraw:
            src["user"] = (uraw, True, ["unit:somvr-headset", "unit:wivrn"])
            modes["user"] = "late" if vr_loop == "late" else "work"
        for n in list(loops):
            if n not in src:
                log("PROTECT END %s" % n)
                del loops[n], raws[n]
        for n, (p, lp, tg) in src.items():
            if n not in loops or raws[n].path != p:
                raws[n], loops[n] = Raw(p, tail=65536), Loop(calm_s, late_x, rebase_s, modes.get(n, "work"), attrib)
                log("PROTECT %s: frames from %s, closed loop %s, pins %s" % (
                    n, p, ("on (%s)" % loops[n].mode) if lp else "off", tg))
            loops[n].mode = modes.get(n, "work")
        # the decision: frozen while the VR gate holds or any closed loop holds
        gate = ""
        if vr and not in_vr_ok:
            gate = "the user is in VR (LANE_IN_VR=0: no lane work during a session)"
        elif vr and not uraw and need_frames:
            gate = "the user is in VR and no frame signal (no fresh perf-raw.txt, LANE_VR_NEED_FRAMES=1): the lane waits"
        events = []
        for n, (p, lp, _) in src.items():
            L, R = loops[n], raws[n]
            tr0 = R.truncated
            fr = R.read()
            t = R.now()
            if R.truncated != tr0:
                log("FRAMES %s: %s began again (truncated): read from its start" % (n, p))
            # b37 live3: no frame for NOFRAMES_S = no frames - never "probe forever": this loop holds nothing (the
            # guarded jobs follow LANE_VR_NEED_FRAMES) until frames come again
            if fr:
                R.t_frame = tick0
            stale = tick0 - R.t_frame > R.NOFRAMES_S
            if stale != R.stale:
                R.stale = stale
                log("FRAMES %s: %s" % (n, ("none for %.0f s from %s (the session's frames stopped or unreadable): no "
                                           "frames - its loop holds nothing; the guarded jobs follow LANE_VR_NEED_FRAMES"
                                           % (R.NOFRAMES_S, p)) if stale else "again from %s" % p))
            L.set_calm(frozen or not jobs or not active, t)
            L.feed(fr)
            if stale:
                L.paused_at = None
            elif lp:
                a, w = L.decide(bool(jobs) and not frozen and active, t)
                if a:
                    events.append("%s %s: %s" % (a, n, w))
        held = [n for n in loops if src[n][1] and loops[n].paused_at is not None and not raws[n].stale]
        if "user" in raws and raws["user"].stale:
            uraw = None   # (the user's frames stopped: the same as none)
            if vr and in_vr_ok and need_frames and not gate:
                gate = "the user is in VR and the session's frames stopped (LANE_VR_NEED_FRAMES=1): the lane waits"
        want = bool(jobs) and bool(gate or held)
        if not jobs:
            want = False
        why = gate or ("; ".join(events) if events else ("held by " + ",".join(held) if held else ""))
        if want != frozen or (events and not want):
            if want != frozen:
                if not os.path.isdir(lane):
                    frozen = False
                elif cg_freeze(lane, want):
                    t0 = time.monotonic()
                    while want and not cg_frozen(lane) and time.monotonic() - t0 < 0.5:
                        time.sleep(0.002)
                    log("%s the lane (%d jobs; %s)%s" % (
                        "FREEZE" if want else "THAW", len(jobs), why or ("the lane empty" if not jobs else "no hold"),
                        " - frozen after %.1f ms" % ((time.monotonic() - t0) * 1e3) if want else ""))
                    frozen, why_frozen = want, why
                    for n, L in loops.items():   # the calm intervals in the frames' clock
                        L.set_calm(frozen or not jobs or not active, raws[n].now())
                else:
                    log("WARN could not write %s/cgroup.freeze" % lane)
            else:
                log("NOTE %s" % why)
        # b37: the smaller lane in VR (LANE_VR_QUOTA), the slice's own after
        if slow and vr_quota:
            if vr and in_vr_ok:
                quota_set[0] = set_quota(vr_quota, "the user is in VR") or quota_set[0]
            elif quota_set[0]:
                set_quota(base_quota, "the session ended")
                quota_set[0] = False
        # b37: in VR with LANE_IN_VR=1 only LANE_VR_JOBS run - every other job's scope frozen by itself until VR ends
        if slow or vr:
            for sc in job_scopes(lane):
                nm = job_name(sc) or "?"
                free = vr_allowed(nm, pats)
                # b37 live3: no session frames to watch (none, or stopped) -> LANE_VR_NEED_FRAMES decides (0: they
                # run, as the free jobs; 1: they wait) - build 37 froze them, and a hot reload's lost frames held the
                # words of the player's notes 20 s and more (11-04 22:12)
                guard_ok = bool(guarded) and vr_allowed(nm, guarded) and (uraw is not None or not need_frames)
                hold_it = vr and in_vr_ok and not (free or guard_ok)
                if cg_want_frozen(sc) != hold_it and cg_freeze(sc, hold_it):
                    log("%s job %s (%s)" % (
                        "FREEZE" if hold_it else "THAW", nm,
                        ("in LANE_VR_GUARDED but no session frames to watch" if guarded and vr_allowed(nm, guarded)
                         else "not in LANE_VR_JOBS") + " while the user is in VR" if hold_it else
                        "under the closed loop (LANE_VR_GUARDED)" if guard_ok and not free else "allowed"))
        # pins: the protected sessions' processes on GAME_CPUS while lane jobs run beside them
        now = time.monotonic()
        if jobs and any(tg for _, _, tg in src.values()):
            t_pin_need = now
            if slow or not had_jobs:   # (a new job: at once, not at the next slow tick)
                for n, (_, _, targets) in src.items():
                    ps = []
                    for tg in targets:
                        ps += pids_of(tg)
                    if ps:
                        pins.pin(ps, "beside %s" % n)
        if pins.saved and now - t_pin_need > 10:
            pins.unpin("no lane job beside a session to pin for 10 s")
        if jobs or src or pins.saved or frozen:
            t_busy = now
        if now - t_busy > 120:
            log("EXIT: the lane empty for 120 s, nothing pinned, protected or frozen")
            return 0
        if not jobs:
            aff_fixed.clear()
        had_jobs = bool(jobs)
        time.sleep(max(0.0, TICK - (time.monotonic() - tick0)))


def status(argv):
    conf = load_conf()
    lane = lane_cgroup()
    if "--line" in argv:   # (b37: lane.sh status, live_lane.sh's one line)
        inst = os.path.exists(os.path.expanduser("~/.config/systemd/user/somvr-lane.slice"))
        jobs = cg_ids(lane, "cgroup.procs")
        vr, ok = user_in_vr(), conf.get("LANE_IN_VR", "0") == "1"
        now = ("not installed (scripts/lane_install.sh install)" if not inst else
               "a job runs now" if not vr else
               "a job runs now under the closed loop (LANE_IN_VR=1)" if ok else
               "a job waits: the user is in VR (LANE_IN_VR=0)")
        print("lane: CPUs %s, quota %s, memory high %s; user in VR %s; %s, %d jobs; %s" % (
            conf.get("LANE_CPUS"), conf.get("LANE_QUOTA"), conf.get("LANE_MEMORY_HIGH"), "yes" if vr else "no",
            "frozen" if cg_frozen(lane) else "running", len(jobs), now))
        return 0
    print("lane cgroup %s: %s, jobs %s" % (lane, "frozen" if cg_frozen(lane) else "running",
                                          " ".join(map(str, cg_ids(lane, "cgroup.procs"))) or "none"))
    print("LANE_CPUS %s GAME_CPUS %s LANE_IN_VR %s" % (conf.get("LANE_CPUS"), conf.get("GAME_CPUS"),
                                                      conf.get("LANE_IN_VR")))
    r = subprocess.run(["systemctl", "--user", "is-active", "somvr-lane-watch"], capture_output=True, text=True)
    print("watcher:", r.stdout.strip())
    for n, d in read_protects().items():
        print("protect %s: %s" % (n, d))
    try:
        print("pins: %d saved affinities" % len(json.load(open(PINS)).get("saved", {})))
    except (OSError, ValueError):
        print("pins: none")
    print("user in VR:", user_in_vr())
    return 0


def placement(argv):
    """placement TARGET [SECONDS]: where the busiest threads of TARGET's processes run (each 0.05 s, their last CPU)"""
    tg, secs = argv[0], float(argv[1]) if len(argv) > 1 else 5.0

    def ticks(pid, tid):
        try:
            f = open("/proc/%d/task/%d/stat" % (pid, tid)).read().rsplit(")", 1)[1].split()
            return int(f[11]) + int(f[12]), int(f[36]), open("/proc/%d/task/%d/comm" % (pid, tid)).read().strip()
        except (OSError, IndexError, ValueError):
            return None
    ps = pids_of(tg)
    t0 = {(p, t): ticks(p, t) for p in ps for t in proc_tids(p)}
    hist, n = {}, 0
    end = time.monotonic() + secs
    while time.monotonic() < end:
        for (p, t) in t0:
            r = ticks(p, t)
            if r:
                hist.setdefault((p, t, r[2]), {}).setdefault(r[1], 0)
                hist[(p, t, r[2])][r[1]] += 1
        n += 1
        time.sleep(0.05)
    busy = []
    for (p, t), a in t0.items():
        b = ticks(p, t)
        if a and b:
            busy.append((b[0] - a[0], p, t, b[2]))
    busy.sort(reverse=True)
    print("placement of %s: %d processes, %d threads, %d samples in %.0f s; the 10 busiest threads (CPU ticks, the CPUs "
          "they were on):" % (tg, len(ps), len(t0), n, secs))
    for c, p, t, nm in busy[:10]:
        h = hist.get((p, t, nm), {})
        print("  %5d ticks  pid %d tid %d %-16s %s" % (c, p, t, nm, " ".join("cpu%d:%d" % (k, v) for k, v in
                                                                            sorted(h.items(), key=lambda x: -x[1]))))
    return 0


def unpin(argv):
    conf = load_conf()
    Pins(Log(os.environ.get("LANE_LOG")), cpus(conf.get("GAME_CPUS", "1,3-7,9,11-15"))).unpin("lane_watch.py unpin")
    return 0


# ---- the offline test ----------------------------------------------------------------------------------------------
def selftest(argv):
    import random
    rnd = random.Random(7)
    fails = []
    if "--no-loop" in argv:   # the old way (no closed loop): the test must fail
        Loop.decide = lambda self, lane_busy, t: (None, "")

    attrib = "--no-attrib" not in argv   # (b37 live3: the rule before - test 7 must fail with it)

    def run(hz, seconds, work, lane_jobs_at=0.0, rebase=60.0, mode="work", working=None):
        """a session at hz frames a second, work(t) -> ms; the lane busy from lane_jobs_at (b37 live3: and only while
        working(t), the lane's jobs using CPU - the speech workers wait for lines most of the time); returns the events"""
        L = Loop(3.0, 1.5, rebase, mode, attrib)
        frozen, ev, t, dt = False, [], 0.0, 1.0 / hz
        nxt = TICK
        buf = []

        def idle(t):
            return t < lane_jobs_at or (working is not None and not working(t))
        while t < seconds:
            t += dt
            # frozen = the lane's effect stops at once (the freezer is immediate)
            wk = work(t, frozen or idle(t))
            buf.append((t, dt * 1e3 * (2 if wk > 1000 * dt * 1.6 else 1), wk))
            if t >= nxt:
                nxt += TICK
                L.set_calm(frozen or idle(t), t)
                L.feed(buf)
                buf = []
                a, w = L.decide(not idle(t) and not frozen, t)
                if a in ("pause", "probe"):
                    frozen = True
                    ev.append((t, a, w))
                    L.set_calm(True, t)
                elif a == "resume":
                    frozen = False
                    ev.append((t, a, w))
                    L.set_calm(idle(t), t)
        return ev

    def base(t):
        return 6.0 + rnd.gauss(0, 0.35) + (2.5 if rnd.random() < 0.01 else 0.0)

    # 1. noise only, the lane busy from 5 s (a light job with no effect): one probe, then never a pause in 10 minutes
    for hz in (20, 90):
        ev = run(hz, 600, lambda t, calm: base(t), lane_jobs_at=5.0, rebase=0)
        pauses = [e for e in ev if e[1] == "pause"]
        if len(pauses) > 1:
            fails.append("noise at %d Hz: %d false pauses (%s)" % (hz, len(pauses), pauses[:3]))
        print("test noise %d Hz: %d events (%d pauses)" % (hz, len(ev), len(pauses)))
    # 2. a step overload at 30 s (+40 %): paused within 1 s; the rolling median never past the baseline's p99
    for hz in (20, 90):
        rec = []

        def over(t, calm):
            v = base(t) * (1.4 if t >= 30 and not calm else 1.0)
            rec.append((t, v))
            return v
        ev = run(hz, 60, over, lane_jobs_at=5.0, rebase=0)
        p = [e for e in ev if e[1] == "pause" and e[0] >= 30]
        lat = p[0][0] - 30 if p else None
        if lat is None or lat > 1.0:
            fails.append("step at %d Hz: paused after %s s" % (hz, lat))
        b = sorted(v for t, v in rec if 5.3 < t < 30)
        p99 = pct(b, 99)
        roll = [statistics.median(v for tt, v in rec if t - 0.5 < tt <= t) for t, _ in rec if 30 <= t <= 30 + (lat or 1)]
        print("test step %d Hz: paused %.2f s after the overload (%s); rolling median max %.2f vs p99 %.2f" % (
            hz, lat or -1, p[0][2][:60] if p else "-", max(roll), p99))
        if max(roll) > p99:
            fails.append("step at %d Hz: the rolling median %.2f passed p99 %.2f before the pause" % (hz, max(roll), p99))
        # it comes back after the calm stretch and pauses again (the load is still there), the calm doubling
        rs = [e for e in ev if e[1] == "resume" and e[0] > 30]
        if not rs:
            fails.append("step at %d Hz: never resumed" % hz)
    # 3. the scene gets heavier at 30 s (not the lane: frozen or not): a pause, then the baseline follows it and the
    #    lane runs again within 30 s
    for hz in (90,):
        ev = run(hz, 120, lambda t, calm: base(t) * (1.5 if t >= 30 else 1.0), lane_jobs_at=5.0, rebase=0)
        late_runs = [e for e in ev if e[0] > 60]
        pz = [e for e in ev if e[0] >= 30]
        print("test scene %d Hz: %d events after the change, last %s" % (hz, len(pz), pz[-1][:2] if pz else "-"))
        if pz and pz[-1][1] != "resume" and any(e[1] == "pause" for e in late_runs[-3:]):
            fails.append("scene at %d Hz: the lane stays paused (%s)" % (hz, pz[-4:]))
    # 4. late frames (an interval twice the period) at 3 a second from 30 s: paused
    ev = run(90, 60, lambda t, calm: 20.0 if (t >= 30 and not calm and int(t * 90) % 30 == 0) else base(t),
             lane_jobs_at=5.0, rebase=0)
    p = [e for e in ev if e[1] == "pause" and e[0] >= 30]
    print("test late 90 Hz: %s" % (("paused %.2f s after: %s" % (p[0][0] - 30, p[0][2][:70])) if p else "no pause"))
    if not p or p[0][0] - 30 > 1.5:
        fails.append("late frames: no pause within 1.5 s")
    # 5. (b37) the late mode (the user's session): a work rise with no late frame is no pause (the builds' +0.6 ms);
    #    late frames are, within 1.5 s
    ev = run(90, 120, lambda t, calm: base(t) * (1.4 if t >= 30 and not calm else 1.0), lane_jobs_at=5.0, rebase=0,
             mode="late")
    p = [e for e in ev if e[1] == "pause"]
    print("test late mode, +40 %% work: %d pauses" % len(p))
    if p:
        fails.append("late mode: paused on a work rise without late frames (%s)" % p[0][2][:60])
    ev = run(90, 60, lambda t, calm: 20.0 if (t >= 30 and not calm and int(t * 90) % 30 == 0) else base(t),
             lane_jobs_at=5.0, rebase=0, mode="late")
    p = [e for e in ev if e[1] == "pause" and e[0] >= 30]
    print("test late mode, late frames: %s" % (("paused %.2f s after" % (p[0][0] - 30)) if p else "no pause"))
    if not p or p[0][0] - 30 > 1.5:
        fails.append("late mode: late frames not paused within 1.5 s")
    # 7. (b37 live3, 163.8) a GPU-bound 120 Hz session (the user's 11-04 19:50: bursts of 2-4 late frames in 6.5 % of
    #    its seconds, the lane frozen or not): the lane is held at most 10 % of 10 minutes and never 12 s at once (the
    #    rule before held it 60 s at a time); the lane's own late frames on top of it (3 a second while it works, from
    #    300 s) are still paused, within 4 s
    for extra, job in ((False, "speech"), (False, "build"), (True, "speech")):
        bad = {}

        def gpu(t, calm):
            s_ = int(t)
            if s_ not in bad:
                r = random.Random(s_ * 7919 + 13)
                bad[s_] = set(r.sample(range(120), r.randint(2, 4))) if r.random() < 0.065 else set()
            i = int((t - s_) * 120)
            if i in bad[s_] or (extra and t >= 300 and not calm and i % 40 == 0):
                return 20.0
            return 5.5 + rnd.gauss(0, 0.4)
        ev = run(120, 600, gpu, lane_jobs_at=5.0, rebase=60.0, mode="late",
                 working=(lambda t: t % 20 < 6) if job == "speech" else None)   # (a line synthesised every 20 s)
        held, longest, t_p = 0.0, 0.0, None
        for e in ev:
            if e[1] in ("pause", "probe"):
                t_p = e[0]
            elif e[1] == "resume" and t_p is not None:
                held += e[0] - t_p
                longest = max(longest, e[0] - t_p)
                t_p = None
        pz = [e for e in ev if e[1] == "pause"]
        if not extra:
            print("test GPU-bound 120 Hz, the lane's %s: the lane held %.1f s of 600 (%.1f %%), the longest %.1f s, "
                  "%d pauses" % ("speech workers (6 s of work in 20)" if job == "speech" else "build (working all along)",
                                 held, held / 6, longest, len(pz)))
            if held > (60 if job == "speech" else 120) or longest >= 12:
                fails.append("GPU-bound 120 Hz: the lane held %.1f s, the longest %.1f s (%s)" % (
                    held, longest, pz[-1][2][:90] if pz else "-"))
        else:
            p = [e for e in pz if e[0] >= 300]
            print("test GPU-bound 120 Hz + the lane's own late frames from 300 s: %s" % (
                ("paused %.2f s after: %s" % (p[0][0] - 300, p[0][2][:110])) if p else "no pause"))
            if not p or p[0][0] - 300 > 4.0:
                fails.append("GPU-bound + the lane's late frames: no pause within 4 s")
    # 8. (b37 live3) a frame file that begins again (a hot reload's module reopened it with O_TRUNC): the reader goes back
    #    to its start - build 37's stayed past the new end and read nothing (the lane held 20 s+ of a session's notes)
    import tempfile as _tf
    with _tf.TemporaryDirectory() as d8:
        pth = os.path.join(d8, "perf-raw.txt")
        with open(pth, "w") as f:
            f.write("# somvr perf raw 1: T INTERVAL_MS WORK_MS (clock monotonic_raw, the render thread)\n")
            for i in range(200):
                f.write("%.6f 11.111 5.000\n" % (100 + i / 90))
        R8 = Raw(pth)
        n1 = len(R8.read())
        with open(pth, "w") as f:   # truncated, a new module's lines
            f.write("# somvr perf raw 1: T INTERVAL_MS WORK_MS (clock monotonic_raw, the render thread)\n")
            for i in range(30):
                f.write("%.6f 11.111 5.000\n" % (103 + i / 90))
        n2 = len(R8.read())
        print("test truncated frame file: %d frames, then %d after it began again (truncated %d)" % (n1, n2, R8.truncated))
        if n1 != 200 or n2 != 30 or R8.truncated != 1:
            fails.append("truncated frame file: %d / %d frames, truncated %d" % (n1, n2, R8.truncated))
    # 6. (b37) whose frames, while the user is in VR: only the user's own run - a helper's headless boot (it writes a
    #    perf-raw.txt too, SOMVR_PERF_RAW in the env) never stands in for the session's
    import tempfile
    saved = os.environ.pop("LANE_USER_RAW", None)
    with tempfile.TemporaryDirectory() as d:
        bc, ws, ws2 = (os.path.join(d, "logs", n) for n in ("boot-check-20261104-120000", "win-session-20261104-110000",
                                                             "win-session-20261104-100000"))

        def touch(r, age):
            os.makedirs(r, exist_ok=True)
            p = os.path.join(r, "perf-raw.txt")
            with open(p, "w") as f:
                f.write("# perf test\n")
            t = time.time() - age
            os.utime(p, (t, t))
            return p
        pb, pw, pw2 = touch(bc, 0.2), touch(ws, 1.0), touch(ws2, 0.5)
        old = max(glob.glob(os.path.join(d, "logs", "boot-check-*", "perf-raw.txt")) +
                  glob.glob(os.path.join(d, "logs", "win-session-*", "perf-raw.txt")), key=os.path.getmtime)
        cases = [("the freshest win-session's, not the newer helper's boot", user_raw(d, lambda: None), pw2),
                 ("the somvr-headset unit's own run", user_raw(d, lambda: ws), pw)]
        touch(ws, 10), touch(ws2, 10)
        cases += [("only a helper's boot fresh: no frames", user_raw(d, lambda: None), None),
                  ("the unit's run not fresh: no frames (never another's)", user_raw(d, lambda: ws), None)]
        pr = {"helper": {"raw": pb, "pin": []}, "session": {"raw": pw, "pin": []}}
        cases += [("a protect file on a helper's boot, the user in VR: dropped", sorted(protects_for(pr, True)), ["session"]),
                  ("... the lane's test mode (no real user): kept", sorted(protects_for(pr, False)), ["helper", "session"])]
        print("test whose frames: the old rule (the newest perf-raw.txt of boot-check-* and win-session-*) took %s" %
              ("the helper's boot" if old == pb else old))
        for what, got, want in cases:
            ok = got == want
            print("test whose frames: %s - %s" % (what, "ok" if ok else "got %s" % got))
            if not ok:
                fails.append("whose frames: " + what)
    if saved is not None:
        os.environ["LANE_USER_RAW"] = saved
    for f in fails:
        print("FAIL", f)
    print("lane_watch test: %s" % ("PASS" if not fails else "FAIL (%d)" % len(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else "status"
    fn = {"watch": watch, "status": status, "unpin": unpin, "test": selftest, "placement": placement}.get(cmd)
    if not fn:
        print(__doc__)
        sys.exit(2)
    sys.exit(fn(sys.argv[2:]))
