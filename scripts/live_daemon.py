#!/usr/bin/env python3
"""live_daemon.py - the live loop's watcher (docs/re-xr-pipeline.md 162): a session's bug marks become the coordinator's
inbox notes within seconds, the voice transcribed by a small local model in the CPU lane.

  python3 -I live_daemon.py --run RUN --live DIR [--model base.en] [--threads 2] [--parent PID]

Started by scripts/live_session.sh (som-win-session.sh / headless_win.sh with SOMVR_LIVE=on). Standard library only;
the speech-to-text worker (live_stt.py, the tools venv's faster-whisper) runs in the lane (live_lane.sh exec stt ...)
and keeps its model loaded, so a note costs only its transcription (base.en int8 on one core: ~1.2 s for 10-18 s).

Every POLL_S (0.25 s):
  - RUN/marks/<id>/ with `served` (the session took the mark): wait for its voice to end - voice.txt is written by
    voice_rec.py when the note ends ("12.4 s from wivrn.source (talk ..; ended: 2 s quiet)"), then voice.wav is
    complete; a "no voice: ..." line is final after GRACE_S (3 s: a test driver may still put a recorded note in);
    no voice.txt at all after WAIT_S (45 s): posted without a voice. A voice goes to the worker; its text comes back.
  - each finished mark -> DIR/inbox/<n>.json (n = 1, 2, ... in the order they finish): the mark, its folder, the game
    time t= (state.txt's second line), the wall time, the transcript (and the model, its seconds, the latency from the
    note's end to the note in the inbox), state.txt's key lines, the pictures (eyes.jpg, flat.jpg - made beside it by
    the session; their paths whether or not written yet), the session and the run. Written atomically; the
    transcript also as the mark's voice-live.txt.
  - b37 (162.8), --tts on: each card's spoken line (say/<key>.line, written by the layer when the card arrives) to the
    Kokoro worker in the lane (live_kokoro.py) -> say/<key>.wav at once; at the card's first calm moment (the layer's
    say/<key>.calm, with its nudge) live_tts.py plays it on wivrn.sink (exactly; tests LIVE_TTS_SINK = their own null
    sink) -> say/<key>.spoken. One line at a time, in the order of their calm moments; not ready 30 s after it: dropped.
  - b37 (lane2's numbers, 163.7): each worker at 1 thread (--threads / LIVE_THREADS); the two models load one at a
    time as the session starts - the speech-to-text's, then Kokoro's once it is ready - each load's time logged with
    whether the world was already in. In the lane (live_lane.sh) the workers are the jobs `live-stt` / `live-tts`:
    in VR they run only under the lane's late-frame guard (LANE_VR_GUARDED, the session's SOMVR_PERF_RAW frames).
  - DIR/daemon.json every 2 s: alive, the notes, the worker's state, the lane's line (live_status.sh reads it).
Ends on SIGTERM / SIGINT, when --parent PID is gone, or when DIR/on is removed; then DIR/on is removed and
daemon.json says so. Never plays or records sound; reads RUN, writes only DIR and the marks' voice-live.txt.
"""
import argparse
import json
import os
import re
import signal
import subprocess
import sys
import time

POLL_S = 0.25
GRACE_S = 3.0
WAIT_S = 45.0
HERE = os.path.dirname(os.path.abspath(__file__))
VENV_PY = os.environ.get("LIVE_STT_PY") or os.path.expanduser("~/Projects/flat2vr/shared/tools-venv/bin/python")
TTS_PY = os.environ.get("LIVE_TTS_PY") or os.path.expanduser("~/Projects/flat2vr/shared/tools/tts/venv/bin/python")
STT_LATE_S = 20.0   # b37: a note whose words take longer (the lane paused / frozen) is posted without them, the words
                    # follow as a note of their own ("update_of") when they come
CALM_LATE_S = 30.0
LOAD_WAIT_S = 90.0  # b37: the TTS model's load waits for the STT model's at most this long (one load at a time)  # b37: a card's spoken line not synthesised this long after its calm moment is dropped
VOICE_END = re.compile(r"^\d+(\.\d+)? s from ")
STOP = False


def now_iso():
    return time.strftime("%Y-%m-%dT%H:%M:%S", time.localtime())


def write_json(path, obj):
    tmp = path + ".tmp"
    with open(tmp, "w") as f:
        json.dump(obj, f, indent=1)
        f.write("\n")
    os.replace(tmp, path)


def read(path, n=65536):
    try:
        with open(path, errors="replace") as f:
            return f.read(n)
    except OSError:
        return None


def state_lines(mdir, cap=12):
    """state.txt's head: the lines up to the first blank one (the mark, the build and t=, the context, the frame rate,
    Talion, the head, the hands, the practice area), each cut to 200 characters"""
    s = read(os.path.join(mdir, "state.txt"), 8192)
    if not s:
        return []
    out = []
    for ln in s.split("\n"):
        if not ln.strip():
            break
        out.append(ln[:200])
        if len(out) >= cap:
            break
    return out


def game_time(lines):
    for ln in lines[:3]:
        m = re.search(r"\bt=([\d.]+)", ln)
        if m:
            return float(m.group(1))
    return None


class Worker:
    """the speech-to-text worker in the lane: one model load, then a job per line"""

    def __init__(self, model, threads, log, kind="stt"):
        self.model, self.threads, self.log, self.kind = model, threads, log, kind
        self.p, self.ready, self.fails, self.why, self.busy = None, False, 0, "", {}
        self.buf = b""

    def start(self):
        py = VENV_PY if self.kind == "stt" else TTS_PY
        if not os.access(py, os.X_OK):
            self.why = f"no {'tools' if self.kind == 'stt' else 'TTS'} venv python at {py}"
            return False
        if self.kind == "stt":
            args = [os.path.join(HERE, "live_stt.py"), "--model", self.model, "--threads", str(self.threads)]
        else:  # b37: the spoken answer's Kokoro (live_kokoro.py: its threads = the lane's, LANE_THREADS)
            args = [os.path.join(HERE, "live_kokoro.py"), "--threads", str(self.threads)]
        cmd = [os.path.join(HERE, "live_lane.sh"), "exec", self.kind, py, "-I"] + args
        self.p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                                  start_new_session=True)
        os.set_blocking(self.p.stdout.fileno(), False)
        self.ready, self.buf = False, b""
        self.log(f"{self.kind}: worker started in the lane (pid {self.p.pid}, "
                 f"{self.model if self.kind == 'stt' else 'Kokoro af_heart'}, threads {self.threads})")
        return True

    def alive(self):
        return self.p is not None and self.p.poll() is None

    def submit(self, wav, out, key):
        """stt: WAV -> OUT.json; tts: OUT.wav <- TEXT (the line's order: tts's job is OUT then TEXT)"""
        if not self.alive():
            return False
        try:
            self.p.stdin.write((f"{wav}\t{out}\n" if self.kind == "stt" else f"{out}\t{wav}\n").encode())
            self.p.stdin.flush()
        except OSError:
            return False
        self.busy[out] = (key, time.monotonic())
        return True

    def lines(self):
        """the worker's finished lines since the last call"""
        if self.p is None:
            return []
        try:
            chunk = self.p.stdout.read()
        except (OSError, ValueError):
            chunk = None
        if chunk:
            self.buf += chunk
        out = []
        while b"\n" in self.buf:
            ln, self.buf = self.buf.split(b"\n", 1)
            out.append(ln.decode(errors="replace"))
        return out

    def stop(self):
        if self.alive():
            try:
                self.p.stdin.close()
            except OSError:
                pass
            try:
                self.p.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.p.terminate()  # our own child only
                try:
                    self.p.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    self.p.kill()


def main():
    global STOP
    ap = argparse.ArgumentParser()
    ap.add_argument("--run", required=True)
    ap.add_argument("--live", required=True)
    ap.add_argument("--model", default=os.environ.get("SOMVR_LIVE_STT_MODEL", "base.en"))
    ap.add_argument("--threads", type=int, default=int(os.environ.get("LIVE_THREADS") or 1),
                    help="each speech worker's threads (b37: 1 - lane2's numbers, 163: the narrowest frame-time spread)")
    ap.add_argument("--parent", type=int, default=0)
    ap.add_argument("--stt", default=os.environ.get("SOMVR_LIVE_STT", "on"), help="on | off (no transcription)")
    ap.add_argument("--tts", default=os.environ.get("SOMVR_LIVE_TTS", "off"),
                    help="on: every card is also spoken in the headset at its calm moment (Kokoro in the lane, "
                         "live_tts.py on wivrn.sink; a message with say/<n>.quiet is not); off (the code default)")
    a = ap.parse_args()
    run, live = os.path.abspath(a.run), os.path.abspath(a.live)
    for d in ("inbox", "stt", "say", "patches"):
        os.makedirs(os.path.join(live, d), exist_ok=True)
    logf = open(os.path.join(live, "live.log"), "a", buffering=1)

    def log(msg):
        logf.write(f"{now_iso()} {msg}\n")

    def on_sig(*_):
        global STOP
        STOP = True

    signal.signal(signal.SIGTERM, on_sig)
    signal.signal(signal.SIGINT, on_sig)
    with open(os.path.join(live, "on"), "w") as f:
        f.write(f"{os.getpid()}\n")
    session = os.path.basename(live)
    write_json(os.path.join(live, "session.json"), {"session": session, "run": run, "started": now_iso(),
                                                     "daemon_pid": os.getpid(), "model": a.model, "stt": a.stt})
    log(f"live: session {session} on (run {run}, the daemon pid {os.getpid()}, stt {a.stt} {a.model}, tts {a.tts})")
    lane = subprocess.run([os.path.join(HERE, "live_lane.sh"), "status"], capture_output=True, text=True).stdout.strip()
    log(f"live: the lane: {lane}")
    t_daemon = time.monotonic()
    mlog = os.path.join(run, "somvr.log")

    def world_in():
        """the game's world loaded (somvr.log's SESSION line with ctx=world) - read only at a model's load end"""
        try:
            with open(mlog, "rb") as f:
                while True:
                    b = f.read(1 << 20)
                    if not b:
                        return False
                    if b"ctx=world" in b:
                        return True
        except OSError:
            return False

    def loaded(kind, ln, t0):
        t_now = time.monotonic()
        log(f"{kind}: {ln} - the load took {t_now - t0:.1f} s in the lane ({a.threads} thread{'s' if a.threads != 1 else ''}), "
            f"done {t_now - t_daemon:.1f} s after the session's start; the world "
            f"{'ALREADY loaded (a load during play)' if world_in() else 'not loaded yet (the game still starting)'}")

    # b37 (lane2's numbers, 163): the speech models load ONE AT A TIME, while the game is still starting (three loads at
    # once made 2 s of hitches): the speech-to-text first, the Kokoro voice when it is ready (or gone, or 90 s)
    w = Worker(a.model, a.threads, log)
    t_stt0 = time.monotonic()
    if a.stt == "on" and not w.start():
        log(f"stt: off - {w.why}")
    tw = Worker("kokoro", a.threads, log, kind="tts")  # b37: the spoken answer (162.8)
    tts_pending, t_tts0 = a.tts == "on", 0.0
    if a.tts == "on":
        log(f"tts: the lines play on {os.environ.get('LIVE_TTS_SINK') or 'wivrn.sink'} (exactly that sink; never another)")
    sp = {}  # b37: say/<key> -> {"state": synth|ready|failed|calm|played, "t": ..., "calm_t": ...}
    tts_restarts = 0
    marks = {}  # id -> {"state": wait|stt|posted, "t_end": mtime of voice.txt, ...}
    n_posted = len([x for x in os.listdir(os.path.join(live, "inbox")) if x.endswith(".json")])
    last_beat, restarts = 0.0, 0
    mroot = os.path.join(run, "marks")
    tts_p, tts_k = None, ""  # the spoken line playing (one at a time)

    def post(mid, m, stt, update_of=None):
        nonlocal n_posted
        mdir = os.path.join(mroot, mid)
        lines = state_lines(mdir)
        n_posted += 1
        t_now = time.time()
        note = {"n": n_posted, "mark": mid, "dir": mdir, "session": session, "run": run, "wall": now_iso(),
                "game_t": game_time(lines), "voice": m.get("voice_line", ""), "voice_wav": m.get("wav"),
                "transcript": (stt or {}).get("text", ""), "stt_model": (stt or {}).get("model"),
                "stt_s": (stt or {}).get("secs"), "audio_s": (stt or {}).get("audio_s"),
                "latency_s": round(t_now - m["t_end"], 2) if m.get("t_end") else None,
                "why": m.get("why", ""), "state": lines,
                "pictures": {k: os.path.join(mdir, k) for k in ("eyes.jpg", "flat.jpg")},
                "pictures_now": [k for k in ("eyes.jpg", "flat.jpg") if os.path.exists(os.path.join(mdir, k))]}
        if update_of:
            note["update_of"] = update_of  # b37: the words of note update_of, late
        m["n"] = n_posted
        write_json(os.path.join(live, "inbox", f"{n_posted}.json"), note)
        if stt and stt.get("text"):
            try:
                with open(os.path.join(mdir, "voice-live.txt"), "w") as f:
                    f.write(stt["text"] + "\n")
            except OSError:
                pass
        m["state"] = "posted"
        log(f"note {n_posted}: mark {mid}"
            + (f", {note['audio_s']} s of voice -> {note['stt_s']} s in the lane, {note['latency_s']} s after the note "
               f"ended: {note['transcript'][:120]!r}" if stt else f" ({m.get('why') or 'no voice'})"))

    while not STOP:
        t = time.monotonic()
        if a.parent and not os.path.exists(f"/proc/{a.parent}"):
            log(f"live: the session (pid {a.parent}) is gone: the daemon ends")
            break
        if not os.path.exists(os.path.join(live, "on")):
            log("live: DIR/on removed: the daemon ends")
            break
        if tts_pending and (a.stt != "on" or w.ready or not w.alive() or t - t_stt0 > LOAD_WAIT_S):
            tts_pending, t_tts0 = False, time.monotonic()
            log(f"tts: its model loads now (the speech-to-text's: "
                f"{'ready' if w.ready else 'off' if a.stt != 'on' else 'not running' if not w.alive() else 'still loading after 90 s'})")
            if not tw.start():
                log(f"tts: off - {tw.why}")
        try:
            ids = sorted(os.listdir(mroot))
        except OSError:
            ids = []
        for mid in ids:
            mdir = os.path.join(mroot, mid)
            m = marks.get(mid)
            if m is None:
                if not os.path.exists(os.path.join(mdir, "served")):
                    continue
                m = marks[mid] = {"state": "wait", "seen": time.time()}
                log(f"mark {mid}: served; waiting for its voice to end")
            if m["state"] != "wait":
                continue
            vt = os.path.join(mdir, "voice.txt")
            line = (read(vt, 400) or "").strip().split("\n")[0]
            wav = os.path.join(mdir, "voice.wav")
            if line and VOICE_END.match(line) and os.path.exists(wav):
                m["voice_line"], m["wav"], m["t_end"] = line, wav, os.stat(vt).st_mtime
                if a.stt == "on" and w.alive():
                    out = os.path.join(live, "stt", f"{mid}.json")
                    if w.submit(wav, out, mid):
                        m["state"], m["out"] = "stt", out
                        continue
                m["why"] = "no transcription (the worker is not running)" if a.stt == "on" else "stt off"
                post(mid, m, None)
            elif line.startswith("no voice"):
                age = time.time() - os.stat(vt).st_mtime
                if age >= GRACE_S:
                    m["voice_line"], m["why"], m["t_end"] = line, line, os.stat(vt).st_mtime
                    post(mid, m, None)
            elif time.time() - m["seen"] > WAIT_S:
                m["why"] = f"no voice.txt {WAIT_S:.0f} s after the mark"
                post(mid, m, None)
        for ln in w.lines():
            if ln.startswith("ready"):
                w.ready = True
                loaded("stt", ln, t_stt0)
            elif ln.startswith("done ") or ln.startswith("fail "):
                parts = ln.split(" ", 2)
                out = parts[1] if len(parts) > 1 else ""
                key = w.busy.pop(out, (None, 0))[0]
                if ln.startswith("fail ") and not key:
                    log(f"stt: {ln}")
                    continue
                stt = None
                if ln.startswith("done "):
                    try:
                        with open(out) as f:
                            stt = json.load(f)
                    except (OSError, ValueError) as e:
                        log(f"stt: {out}: {e}")
                else:
                    log(f"stt: {ln}")
                if key and key in marks:
                    if marks[key]["state"] == "posted":  # b37: posted late without its words: they follow
                        if stt and stt.get("text"):
                            marks[key]["why"] = f"the words of note {marks[key].get('n')}, late"
                            post(key, marks[key], stt, update_of=marks[key].get("n"))
                        continue
                    if stt is None:
                        marks[key]["why"] = "the transcription failed"
                    post(key, marks[key], stt)
        for out, (key, t_sub) in list(w.busy.items()):  # b37: the words late (the lane paused or frozen)
            m = marks.get(key)
            if m and m["state"] == "stt" and time.monotonic() - t_sub > STT_LATE_S:
                m["why"] = (f"the transcription waits ({STT_LATE_S:.0f} s: the lane paused or frozen while the user is in "
                            f"VR); the words follow as a note of their own")
                post(key, m, None)
        if a.stt == "on" and w.p is not None and not w.alive():
            for out, (key, _) in list(w.busy.items()):  # its jobs posted without a transcript
                w.busy.pop(out, None)
                if key in marks:
                    marks[key]["why"] = "the transcription worker ended"
                    post(key, marks[key], None)
            if restarts < 3:
                restarts += 1
                log(f"stt: the worker ended (exit {w.p.returncode}); restart {restarts} of 3")
                t_stt0 = time.monotonic()
                w.start()
            else:
                w.p = None
                log("stt: the worker ended 4 times: no more transcription this session")
        if a.tts == "on":  # b37 (162.8): each card's line synthesised when it arrives, played at its calm moment
            sd = os.path.join(live, "say")
            try:
                names = os.listdir(sd)
            except OSError:
                names = []
            for f in sorted(names):
                if tts_pending:  # (its model not loading yet: the lines wait for it, not dropped)
                    break
                if not f.endswith(".line") or f[:-5] in sp:
                    continue
                k = f[:-5]
                n = k[4:] if k.startswith("say-") else ""
                if n and os.path.exists(os.path.join(sd, f"{n}.quiet")):
                    sp[k] = {"state": "quiet", "t": t}
                    log(f"tts: {k}: quiet (say/{n}.quiet)")
                    continue
                text = " ".join((read(os.path.join(sd, f), 2000) or "").split())
                if text and tw.submit(text, os.path.join(sd, f"{k}.wav"), k):
                    sp[k] = {"state": "synth", "t": t}
                else:
                    sp[k] = {"state": "failed", "t": t}
                    log(f"tts: {k}: not synthesised ({'no text' if not text else 'the worker is not running'})")
            for ln in tw.lines():
                if ln.startswith("ready"):
                    tw.ready = True
                    loaded("tts", ln, t_tts0)
                elif ln.startswith("done ") or ln.startswith("fail "):
                    parts = ln.split(" ")
                    out = parts[1] if len(parts) > 1 else ""
                    k = tw.busy.pop(out, (None, 0))[0]
                    if k in sp:
                        ok = ln.startswith("done ")
                        sp[k]["state"] = "ready" if ok else "failed"
                        log(f"tts: {k}: " + (f"{parts[3]} s of speech synthesised in {parts[2]} s in the lane "
                                             f"({t - sp[k]['t']:.1f} s after the card arrived)" if ok and len(parts) > 3
                                             else ln))
                    else:
                        log(f"tts: {ln}")
            if tts_p is not None and tts_p.poll() is not None:
                log(f"tts: {tts_k}: {(read(os.path.join(sd, f'{tts_k}.spoken'), 300) or '?').strip()}")
                if tts_k in sp:
                    sp[tts_k]["state"] = "played"
                tts_p, tts_k = None, ""
            for f in sorted((x for x in names if x.endswith(".calm")),
                            key=lambda x: os.stat(os.path.join(sd, x)).st_mtime if os.path.exists(os.path.join(sd, x)) else 0):
                k = f[:-5]
                e = sp.get(k)
                if e is None or e["state"] in ("played", "dropped", "quiet") or os.path.exists(os.path.join(sd, f"{k}.spoken")):
                    continue
                e.setdefault("calm_t", t)
                if e["state"] == "ready" and tts_p is None:
                    tts_k = k
                    tts_p = subprocess.Popen(["python3", "-I", os.path.join(HERE, "live_tts.py"), "--wav",
                                              os.path.join(sd, f"{k}.wav"), "--out", os.path.join(sd, f"{k}.spoken")],
                                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                    e["state"] = "playing"
                    log(f"tts: {k}: its calm moment - playing ({t - e['calm_t']:.1f} s after it)")
                elif e["state"] == "failed" or (e["state"] == "synth" and t - e["calm_t"] > CALM_LATE_S):
                    why = "not synthesised" if e["state"] == "failed" else \
                        f"not synthesised {CALM_LATE_S:.0f} s after its calm moment"
                    e["state"] = "dropped"
                    with open(os.path.join(sd, f"{k}.spoken"), "w") as fo:
                        fo.write(f"{time.strftime('%Y-%m-%d %H:%M:%S')} not spoken: {why}\n")
                    log(f"tts: {k}: dropped ({why})")
            if tw.p is not None and not tw.alive():
                for out, (k, _) in list(tw.busy.items()):
                    tw.busy.pop(out, None)
                    if k in sp:
                        sp[k]["state"] = "failed"
                if tts_restarts < 3:
                    tts_restarts += 1
                    log(f"tts: the worker ended (exit {tw.p.returncode}); restart {tts_restarts} of 3")
                    t_tts0 = time.monotonic()
                    tw.start()
                else:
                    tw.p = None
                    log("tts: the worker ended 4 times: no more spoken lines this session")
        if t - last_beat >= 2.0:
            last_beat = t
            write_json(os.path.join(live, "daemon.json"), {
                "alive": True, "pid": os.getpid(), "updated": now_iso(), "session": session, "run": run,
                "notes": n_posted, "marks_seen": len(marks),
                "waiting": [k for k, v in marks.items() if v["state"] in ("wait", "stt")],
                "stt": ("ready" if w.ready else "loading") if w.alive() else ("off" if a.stt != "on" else "down"),
                "model": a.model, "lane": lane,
                "tts": (("ready" if tw.ready else "loading") if tw.alive() else ("off" if a.tts != "on" else "down")),
                "spoken": len([1 for e in sp.values() if e["state"] == "played"])})
        time.sleep(POLL_S)
    w.stop()
    tw.stop()
    if tts_p is not None and tts_p.poll() is None:
        try:
            tts_p.wait(timeout=15)  # a line playing: let it end (its own finally puts the game's volume back)
        except subprocess.TimeoutExpired:
            tts_p.terminate()
    try:
        os.unlink(os.path.join(live, "on"))
    except OSError:
        pass
    write_json(os.path.join(live, "daemon.json"), {"alive": False, "pid": os.getpid(), "updated": now_iso(),
                                                   "session": session, "run": run, "notes": n_posted,
                                                   "marks_seen": len(marks), "model": a.model, "lane": lane})
    log(f"live: session {session} off ({n_posted} notes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
