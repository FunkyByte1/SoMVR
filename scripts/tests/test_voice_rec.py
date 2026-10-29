#!/usr/bin/env python3
"""test_voice_rec.py - build 21's voice notes until the player stops talking (scripts/voice_rec.py; boot_check.sh's
mark_serve with SOMVR_MARK_VOICE=talk; docs/re-xr-pipeline.md 82). Nothing here touches PipeWire or a microphone:
a fake `parec` on PATH plays a planned signal.

  python3 -I scripts/tests/test_voice_rec.py        (about 20 s)

1. the stop rule on planned levels: nothing said (5 s), a sentence (2 s after it), pauses under 2 s kept, the 30 s
   limit, a note that starts mid-sentence (its own speech is not the floor), a loud room (-45 dBFS: not speech), a
   room louder than the bar (the note runs on: longer, never cut);
2. the recorder through the fake parec: the WAV (48 kHz mono s16, as long as the rule says), voice.txt; SIGTERM ends
   it at once with a playable WAV;
3. boot_check.sh's mark_serve (its voice block, sourced): two marks 1 s apart - the first note ended by the second
   ("the next mark"), the second by the rule; SOMVR_MARK_VOICE=fixed: build 20's parecord for VOICE_S.
"""
import os
import random
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPTS = os.path.dirname(HERE)
sys.path.insert(0, SCRIPTS)
import voice_rec as vr  # noqa: E402

FAILS = 0
CHECKS = 0


def check(ok, what):
    global FAILS, CHECKS
    CHECKS += 1
    if not ok:
        FAILS += 1
        print("FAIL: " + what)


def run_levels(plan, **kw):
    """plan = [(seconds, dBFS), ...] as 50 ms frames -> (end time, why)"""
    rule = vr.StopRule(**kw)
    for secs, db in plan:
        for _ in range(int(round(secs * 20))):
            why = rule.feed(db + random.uniform(-3, 3))
            if why:
                return rule.t(), why, rule
    return None, None, rule


def test_rule():
    random.seed(7)
    t, why, _ = run_levels([(40, -90)])
    check(t == 5.0 and why.startswith("nothing said"), "nothing said: %s %s" % (t, why))
    t, why, _ = run_levels([(40, -60)])
    check(t == 5.0 and why.startswith("nothing said"), "the room alone (-60): %s %s" % (t, why))
    t, why, r = run_levels([(0.5, -70), (3.0, -20), (40, -70)])
    check(abs(t - 5.5) < 0.11 and why == "2 s quiet" and abs(r.first - 0.55) < 0.06, "a sentence 0.5 .. 3.5 s: %s %s" % (t, why))
    plan = [(0.3, -80)]
    for _ in range(6):
        plan += [(1.5, -22), (1.2, -65)]        # pauses of 1.2 s inside the talk: kept
    plan += [(40, -80)]
    t, why, _ = run_levels(plan)
    talk_end = 0.3 + 6 * 2.7 - 1.2
    check(abs(t - (talk_end + 2)) < 0.11 and why == "2 s quiet", "pauses under 2 s kept: %s %s (talk to %.1f)" % (t, why, talk_end))
    t, why, _ = run_levels([(60, -20)])
    check(t == 30.0 and why == "the 30 s limit", "talk on: the limit %s %s" % (t, why))
    t, why, _ = run_levels([(60, -20)], max_s=12)
    check(t == 12.0, "--max 12: %s" % t)
    # mid-sentence from the first frame, loud and steady (the 20c notes 194312_7, 195039_19): talk seen at once
    t, why, r = run_levels([(8, -18), (40, -88)])
    check(abs(t - 10.0) < 0.11 and r.first is not None and r.first < 0.1, "talk from the start: %s %s first %s" % (t, why, r.first))
    t, why, _ = run_levels([(1, -45), (3, -20), (40, -45)])
    check(abs(t - 6.0) < 0.11 and why == "2 s quiet", "a loud room (-45): %s %s" % (t, why))
    t, why, _ = run_levels([(3, -30), (60, -34)])
    check(t == 30.0, "a room louder than the bar (-34): runs to the limit (longer, never cut): %s %s" % (t, why))
    t, why, _ = run_levels([(0.5, -80), (2, -20), (2.5, -80), (2, -20), (40, -80)], quiet_s=3)
    check(abs(t - 10.0) < 0.11, "--quiet 3 keeps a 2.5 s pause: %s %s" % (t, why))


FAKE_PAREC = r'''#!/usr/bin/env python3
# a fake parec: plays FAKE_PLAN ("secs:dB,secs:dB,...") as 48 kHz mono s16le on stdout at FAKE_PACE x real time
import array, os, random, signal, sys, time
signal.signal(signal.SIGINT, lambda *a: sys.exit(0))
random.seed(3)
pace = float(os.environ.get("FAKE_PACE", "1"))
out = sys.stdout.buffer
for seg in os.environ["FAKE_PLAN"].split(","):
    secs, db = seg.split(":")
    amp = 32768 * 10 ** (float(db) / 20)
    for _ in range(int(float(secs) * 50)):           # 20 ms chunks
        a = array.array("h", (int(amp if random.random() < 0.5 else -amp) for _ in range(960)))
        try:
            out.write(a.tobytes())
            out.flush()
        except BrokenPipeError:
            sys.exit(0)
        time.sleep(0.02 / pace)
'''


FAKE_PARECORD = r'''#!/usr/bin/env python3
# a fake parecord: the fake parec's stream into the WAV (its last argument) until SIGINT
import os, signal, subprocess, sys, wave
w = wave.open(sys.argv[-1], "wb")
w.setnchannels(1), w.setsampwidth(2), w.setframerate(48000)
signal.signal(signal.SIGINT, lambda *a: (w.close(), os._exit(0)))
s = subprocess.Popen(["parec"], stdout=subprocess.PIPE)
while True:
    d = s.stdout.read(9600)
    if not d:
        break
    w.writeframes(d)
w.close()
'''


def wav_secs(path):
    with wave.open(path) as w:
        return w.getnframes() / w.getframerate(), w.getframerate(), w.getnchannels(), w.getsampwidth()


def test_recorder(tmp, env):
    out, txt = os.path.join(tmp, "a.wav"), os.path.join(tmp, "a.txt")
    e = dict(env, FAKE_PLAN="0.5:-80,2:-20,10:-80", FAKE_PACE="4")
    p = subprocess.run([sys.executable, "-I", os.path.join(SCRIPTS, "voice_rec.py"), "--src", "wivrn.source", "--out", out,
                        "--txt", txt], env=e, capture_output=True, text=True, timeout=30)
    secs, rate, ch, sw = wav_secs(out)
    line = open(txt).read().strip()
    check(p.returncode == 0 and abs(secs - 4.5) < 0.11 and (rate, ch, sw) == (48000, 1, 2) and
          line.startswith("4.5 s from wivrn.source (talk 0.5 .. 2.5 s; ended: 2 s quiet)"),
          "the recorder: %.2f s %s %s %s; '%s' %s" % (secs, rate, ch, sw, line, p.stderr[-300:]))
    # SIGTERM (the next mark): at once, a playable WAV
    out2, txt2 = os.path.join(tmp, "b.wav"), os.path.join(tmp, "b.txt")
    e = dict(env, FAKE_PLAN="60:-20", FAKE_PACE="1")
    p = subprocess.Popen([sys.executable, "-I", os.path.join(SCRIPTS, "voice_rec.py"), "--src", "wivrn.source", "--out", out2,
                          "--txt", txt2], env=e, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1.5)
    t0 = time.monotonic()
    p.send_signal(signal.SIGTERM)
    rc = p.wait(timeout=10)
    dt = time.monotonic() - t0
    secs, _, _, _ = wav_secs(out2)
    line = open(txt2).read().strip()
    check(rc == 0 and dt < 1.0 and 0.8 < secs < 1.7 and "ended: the next mark" in line,
          "SIGTERM: ended in %.2f s, %.2f s recorded, '%s'" % (dt, secs, line))


def test_mark_serve(tmp, env):
    bc = open(os.path.join(SCRIPTS, "boot_check.sh")).read()
    a = bc.index("\nVOICE_S=$(genv SOMVR_MARK_VOICE_S)")
    b = bc.index("\n# ---- the game's end, watched")
    run = os.path.join(tmp, "run")
    os.makedirs(os.path.join(run, "marks"))
    harness = os.path.join(tmp, "harness.sh")
    with open(harness, "w") as f:
        f.write("set -u\nRUN=%s\nBCDIR=%s\nGEXTRA=(\"$@\")\nMARKS_N=0\n" % (run, SCRIPTS))
        f.write("genv() { local kv v=\"\" st=1; for kv in \"${GEXTRA[@]}\"; do [ \"${kv%%=*}\" = \"$1\" ] && { v=${kv#*=}; st=0; }; done; printf '%s' \"$v\"; return $st; }\n")
        f.write("log() { echo \"LOG $*\"; }\nshot_to() { return 1; }\n")
        f.write(bc[a:b] + "\n")
        f.write("mic_source() { echo wivrn.source; }\n")
        f.write("mk() { mkdir -p \"$RUN/marks/$1\"; echo state > \"$RUN/marks/$1/state.txt\"; : > \"$RUN/marks/$1/eyes.ppm\"; "
                ": > \"$RUN/marks/$1/want\"; mark_serve \"$RUN/marks/$1\"; }\n")
        f.write("mk 100000_1; sleep 1; mk 100001_2; wait; echo VOICE_MODE=$VOICE_MODE\n")
    e = dict(env, FAKE_PLAN="0.3:-80,30:-20", FAKE_PACE="1")
    p = subprocess.run(["bash", harness, "SOMVR_MARK_VOICE_MAX_S=8"], env=e, capture_output=True, text=True, timeout=60)
    m = os.path.join(run, "marks")
    t1 = open(os.path.join(m, "100000_1", "voice.txt")).read() if os.path.exists(os.path.join(m, "100000_1", "voice.txt")) else ""
    t2 = open(os.path.join(m, "100001_2", "voice.txt")).read() if os.path.exists(os.path.join(m, "100001_2", "voice.txt")) else ""
    s1 = wav_secs(os.path.join(m, "100000_1", "voice.wav"))[0] if os.path.exists(os.path.join(m, "100000_1", "voice.wav")) else 0
    check("VOICE_MODE=talk" in p.stdout and "until the talk ends (2 s quiet, at most 8 s)" in p.stdout and
          "ended: the next mark" in t1 and 0.6 < s1 < 1.6 and "ended: the 8 s limit" in t2 and
          not os.path.exists(os.path.join(m, "100000_1", "voice.err")),
          "mark_serve: the first note ended by the second (%.2f s: %s), the second by the rule (%s); %s %s" %
          (s1, t1.strip(), t2.strip(), p.stdout[-400:], p.stderr[-400:]))
    # fixed (build 20): parecord for VOICE_S
    shutil.rmtree(run)
    os.makedirs(os.path.join(run, "marks"))
    with open(harness) as f:
        h = f.read().replace("mk 100000_1; sleep 1; mk 100001_2;", "mk 100002_3;")
    with open(harness, "w") as f:
        f.write(h)
    p = subprocess.run(["bash", harness, "SOMVR_MARK_VOICE=fixed", "SOMVR_MARK_VOICE_S=2"], env=e, capture_output=True,
                       text=True, timeout=60)
    t3 = open(os.path.join(run, "marks", "100002_3", "voice.txt")).read()
    check("VOICE_MODE=fixed" in p.stdout and "2 s from wivrn.source" in p.stdout and "s from wivrn.source" in t3,
          "fixed: build 20's parecord (%s) %s" % (t3.strip(), p.stdout[-300:]))


def main():
    test_rule()
    tmp = tempfile.mkdtemp(prefix="voice_rec-")
    try:
        fake = os.path.join(tmp, "bin")
        os.makedirs(fake)
        for name, body in (("parec", FAKE_PAREC),
                           ("parecord", FAKE_PARECORD),  # build 20's fixed mode
                           ("magick", "#!/bin/sh\nexit 0\n")):
            path = os.path.join(fake, name)
            with open(path, "w") as f:
                f.write(body)
            os.chmod(path, 0o755)
        env = dict(os.environ, PATH=fake + os.pathsep + os.environ.get("PATH", "/usr/bin:/bin"))
        test_recorder(tmp, env)
        test_mark_serve(tmp, env)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("test_voice_rec: %d checks, %d failures" % (CHECKS, FAILS))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
