#!/usr/bin/env python3
"""lane_loads.py KIND --seconds S [--threads N] - the lane's test loads (som-b35-lane, docs/re-xr-pipeline.md 163),
repeated until S seconds have passed; one line per round (its time) and a summary. Run them through scripts/lane_run.sh
(the lane) or plainly (the old way: nice 19 and every CPU) for the comparison. Nothing is downloaded: the models come
from the local caches, the speech never goes to an audio output (the TTS line's samples are only computed).
  whisper   faster-whisper small.en (int8, CPU, the local Hugging Face cache) transcribing a 15 s wav (--wav; default
            a 15 s cut of a voice note under /tmp/somvr-lane35/)
  tts       a TTS line: Kokoro-82M's ONNX model on onnxruntime (CPU) speaking a 12-word sentence; piper is not
            installed here - Kokoro is the heavier model (82 M parameters against piper medium's ~20 M), an upper
            bound for it
  build     ShadowOfMordor/mod/somvr/build.sh --check in a copy of the tree (--tree, made by `prepare`), never the
            worktree a game runs from
  hog       the deliberate overload: N threads streaming 256 MB arrays (memory bandwidth and L3), scripts/lane/lane_hog.c
  voice     (b37) the live loop's speech jobs back to back, each model loaded once: faster-whisper base.en and small.en
            (int8) each transcribing a 15 s note, then a Kokoro line - the STT of every note and the spoken answer
            (--models base.en: base.en only; --every S: one round every S seconds, the rest idle - a note every S s)
  builds    (b37) the workers' builds back to back, each after a one-line change of a *_game.c (a real content change,
            so the object cache cannot skip it): the logic DLL (win/build.sh --logic of t3/som-hot-reload's tree, the
            hot reload's ~11 s build: somvr.c split for 8 llc at once), then the full check pair (build.sh --check and
            win/build.sh --check) of the build copy; SOMVR_JOBS from lane_run.sh (the lane's CPUs)
  prepare   the 15 s wav, the build copy, the logic copy (git archive of --logic-ref, default t3/som-hot-reload), each
            built once (warm), the hog (once, before a boot)
The threads: --threads N, else $LANE_THREADS (lane_run.sh sets it to the lane's CPU count), else 4.
Python: whisper and tts need /home/bara/Projects/flat2vr/shared/tools-venv/bin/python (this script re-runs itself there).
"""
import argparse
import os
import shutil
import subprocess
import sys
import time

VENV = "/home/bara/Projects/flat2vr/shared/tools-venv/bin/python"
WORK = "/tmp/somvr-lane35"
HERE = os.path.dirname(os.path.abspath(__file__))
SOM = os.path.dirname(HERE)
KOKORO = "/home/bara/Projects/flat2vr/shared/tools/tts/models/kokoro-82m-v1.0-onnx-timestamped"
WHISPER = os.path.expanduser("~/.cache/huggingface/hub/models--Systran--faster-whisper-small.en")
WHISPER_BASE = os.path.expanduser("~/.cache/huggingface/hub/models--Systran--faster-whisper-base.en")
CHANGE = "somvr_practice_game.c"   # (b37 builds: the file each round changes by one comment line, in the copies only)


def say(*a):
    print("lane_loads:", *a, flush=True)


def rounds(seconds, fn, label):
    t0 = time.monotonic()
    n, times = 0, []
    while time.monotonic() - t0 < seconds:
        r0 = time.monotonic()
        info = fn()
        times.append(time.monotonic() - r0)
        n += 1
        say("%s round %d: %.2f s%s" % (label, n, times[-1], (" (" + info + ")") if info else ""))
    say("%s: %d rounds in %.1f s, a round %.2f s mean" % (label, n, time.monotonic() - t0,
                                                          sum(times) / len(times) if times else 0))


def whisper(a, nt):
    os.environ.setdefault("HF_HUB_OFFLINE", "1")
    from faster_whisper import WhisperModel
    snap = sorted(os.listdir(os.path.join(WHISPER, "snapshots")))[-1]
    m = WhisperModel(os.path.join(WHISPER, "snapshots", snap), device="cpu", compute_type="int8", cpu_threads=nt,
                     num_workers=1)
    import wave
    import numpy as np
    w = wave.open(a.wav or os.path.join(WORK, "voice15.wav"))
    x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(np.float32) / 32768.0
    if w.getnchannels() > 1:
        x = x.reshape(-1, w.getnchannels()).mean(axis=1)
    r = w.getframerate() // 16000   # (48 kHz: the mean of each 3 samples = 16 kHz; this av can't decode for it)
    audio = x[: len(x) // r * r].reshape(-1, r).mean(axis=1) if r > 1 else x

    def one():
        segs, info = m.transcribe(audio, beam_size=1, language="en", vad_filter=False)
        txt = " ".join(s.text.strip() for s in segs)
        return "%.1f s of audio, %d words" % (info.duration, len(txt.split()))
    rounds(a.seconds, one, "whisper small.en int8 x%d" % nt)


def kokoro(nt):
    """Kokoro-82M loaded (CPU, nt threads): a function speaking the line once, returning its seconds of speech"""
    import json
    import numpy as np
    import onnxruntime as ort
    so = ort.SessionOptions()
    so.intra_op_num_threads, so.inter_op_num_threads = nt, 1
    so.execution_mode = ort.ExecutionMode.ORT_SEQUENTIAL
    s = ort.InferenceSession(os.path.join(KOKORO, "onnx", "model.onnx"), so, providers=["CPUExecutionProvider"])
    vocab = json.load(open(os.path.join(KOKORO, "tokenizer.json")))["model"]["vocab"]
    ph = "ðɪs ɪz ə ʃɔːɹt laɪn fɹʌm ðə hɛdsɛt, spoʊkən wɛn ðə faɪt ɪz ˈoʊvɚ ænd ðə wɝːld ɪz kwaɪət."
    ids = [0] + [vocab[c] for c in ph if c in vocab] + [0]
    voice = np.fromfile(os.path.join(KOKORO, "voices", "af_heart.bin")
                        if os.path.exists(os.path.join(KOKORO, "voices", "af_heart.bin"))
                        else os.path.join(KOKORO, "voices", sorted(os.listdir(os.path.join(KOKORO, "voices")))[0]),
                        dtype=np.float32).reshape(-1, 1, 256)
    style = voice[min(len(ids) - 2, len(voice) - 1)]
    feed = {"input_ids": np.array([ids], dtype=np.int64), "style": style.astype(np.float32),
            "speed": np.array([1.0], dtype=np.float32)}

    return lambda: s.run(None, feed)[0].shape[-1] / 24000.0


def tts(a, nt):
    line = kokoro(nt)
    rounds(a.seconds, lambda: "%.1f s of speech" % line(), "tts kokoro-82m onnx x%d" % nt)


def voice(a, nt):
    """base.en + small.en on the 15 s note, then a Kokoro line; the three loaded once (the live loop's daemons)"""
    os.environ.setdefault("HF_HUB_OFFLINE", "1")
    from faster_whisper import WhisperModel
    import wave
    import numpy as np
    ms = {}
    want = a.models.split(",")
    for name, repo in (("base.en", WHISPER_BASE), ("small.en", WHISPER)):
        if name not in want:
            continue
        snap = sorted(os.listdir(os.path.join(repo, "snapshots")))[-1]
        ms[name] = WhisperModel(os.path.join(repo, "snapshots", snap), device="cpu", compute_type="int8",
                                cpu_threads=nt, num_workers=1)
    w = wave.open(a.wav or os.path.join(WORK, "voice15.wav"))
    x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(np.float32) / 32768.0
    if w.getnchannels() > 1:
        x = x.reshape(-1, w.getnchannels()).mean(axis=1)
    r = w.getframerate() // 16000
    audio = x[: len(x) // r * r].reshape(-1, r).mean(axis=1) if r > 1 else x
    say_line = kokoro(nt)

    def one():
        out = []
        r0 = time.monotonic()
        for name in [m for m in ("base.en", "small.en") if m in ms]:
            t0 = time.monotonic()
            segs, info = ms[name].transcribe(audio, beam_size=1, language="en", vad_filter=False)
            n = len(" ".join(s.text.strip() for s in segs).split())
            out.append("%s %.2f s (%d words)" % (name, time.monotonic() - t0, n))
        t0 = time.monotonic()
        sp = say_line()
        out.append("kokoro %.2f s (%.1f s of speech)" % (time.monotonic() - t0, sp))
        if a.every > 0:   # (bursty: one note's work every EVERY s, as the live loop's notes come)
            time.sleep(max(0.0, a.every - (time.monotonic() - r0)))
        return ", ".join(out)
    rounds(a.seconds, one, "voice x%d" % nt)


def builds(a, nt):
    """the logic DLL, then the check pair, each after a real one-line change"""
    tree = a.tree or os.path.join(WORK, "tree")
    d = os.path.join(tree, "ShadowOfMordor", "mod", "somvr")
    h = os.path.join(WORK, "logic", "ShadowOfMordor", "mod", "somvr")
    for x in (d, h):
        if not os.path.exists(os.path.join(x, "build.sh")):
            say("STOP: no copy %s (lane_loads.py prepare)" % x)
            return 1
    n = [0]

    def change(folder):
        n[0] += 1
        with open(os.path.join(folder, CHANGE), "a") as f:
            f.write("/* lane_loads %d %d */\n" % (os.getpid(), n[0]))

    def run(cmd, cwd):
        t0 = time.monotonic()
        r = subprocess.run(cmd, cwd=cwd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return "%s %.1f s exit %d" % (" ".join(cmd), time.monotonic() - t0, r.returncode)

    def one():
        change(h)
        out = [run(["win/build.sh", "--logic"], h)]
        change(d)
        out.append(run(["./build.sh", "--check"], d))
        out.append(run(["win/build.sh", "--check"], d))
        return "; ".join(out)
    rounds(a.seconds, one, "builds (logic + check pair) SOMVR_JOBS=%s" % os.environ.get("SOMVR_JOBS", "-"))


def build(a, nt):
    tree = a.tree or os.path.join(WORK, "tree")
    d = os.path.join(tree, "ShadowOfMordor", "mod", "somvr")
    if not os.path.exists(os.path.join(d, "build.sh")):
        say("STOP: no build copy in %s (lane_loads.py prepare)" % tree)
        return 1

    def one():
        r = subprocess.run(["./build.sh", "--check"], cwd=d, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return "exit %d" % r.returncode
    rounds(a.seconds, one, "build.sh --check")


def hog(a, nt):
    exe = os.path.join(WORK, "lane_hog")
    src = os.path.join(HERE, "lane", "lane_hog.c")
    if not os.path.exists(exe) or os.path.getmtime(exe) < os.path.getmtime(src):
        os.makedirs(WORK, exist_ok=True)
        subprocess.run(["cc", "-O2", "-pthread", "-o", exe, src], check=True)
    os.execv(exe, [exe, str(nt), str(int(a.seconds))])


def prepare(a, nt):
    import wave
    os.makedirs(WORK, exist_ok=True)
    out = os.path.join(WORK, "voice15.wav")
    if not os.path.exists(out):
        src = a.wav
        if not src:   # a voice note of 15 s or more from a past session (only read; the cut stays in /tmp)
            import glob
            for f in sorted(glob.glob("/home/bara/Projects/flat2vr/ShadowOfMordor/logs/*/marks/*/voice.wav")):
                w = wave.open(f)
                if w.getnframes() >= 15 * w.getframerate():
                    src = f
                    break
        w = wave.open(src)
        fr = w.getframerate()
        data = w.readframes(15 * fr)
        o = wave.open(out, "wb")
        o.setnchannels(w.getnchannels()), o.setsampwidth(w.getsampwidth()), o.setframerate(fr)
        o.writeframes(data)
        o.close()
        say("wrote %s (15 s of %s)" % (out, src))
    tree = a.tree or os.path.join(WORK, "tree")
    d = os.path.join(tree, "ShadowOfMordor")
    if not os.path.exists(os.path.join(d, "mod", "somvr", "build.sh")):
        os.makedirs(os.path.join(d, "mod"), exist_ok=True)
        shutil.copytree(os.path.join(SOM, "mod", "somvr"), os.path.join(d, "mod", "somvr"), symlinks=True,
                        ignore=shutil.ignore_patterns("build", "*.o", "*.obj"))
        for n in ("third_party", "reference"):
            os.symlink("/home/bara/Projects/flat2vr/ShadowOfMordor/" + n, os.path.join(d, n))
        shutil.copytree(os.path.join(SOM, "scripts"), os.path.join(d, "scripts"), symlinks=True,
                        ignore=shutil.ignore_patterns("*.ppm", "*.pgm"))
        say("made the build copy %s" % d)
    lg = os.path.join(WORK, "logic")
    if not os.path.exists(os.path.join(lg, "ShadowOfMordor", "mod", "somvr", "win", "build.sh")):
        os.makedirs(lg, exist_ok=True)
        arc = subprocess.run(["git", "-C", os.path.dirname(SOM), "archive", a.logic_ref, "ShadowOfMordor/mod/somvr", "ShadowOfMordor/scripts"],
                             stdout=subprocess.PIPE, check=True).stdout
        subprocess.run(["tar", "-x", "-C", lg], input=arc, check=True)
        for n in ("third_party", "reference"):
            os.symlink("/home/bara/Projects/flat2vr/ShadowOfMordor/" + n, os.path.join(lg, "ShadowOfMordor", n))
        say("made the logic copy %s (git archive %s)" % (lg, a.logic_ref))
    for top in (tree, lg):   # (win/build.sh finds the xwin SDK in shared/toolchain above its tree)
        if not os.path.exists(os.path.join(top, "shared")):
            os.symlink("/home/bara/Projects/flat2vr/shared", os.path.join(top, "shared"))
    for cwd, cmd in ((os.path.join(lg, "ShadowOfMordor", "mod", "somvr"), ["./build.sh"]),
                     (os.path.join(lg, "ShadowOfMordor", "mod", "somvr"), ["win/build.sh"]),
                     (os.path.join(d, "mod", "somvr"), ["./build.sh", "--check"]),
                     (os.path.join(d, "mod", "somvr"), ["win/build.sh", "--check"])):
        t0 = time.monotonic()
        r = subprocess.run(cmd, cwd=cwd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        say("warm: %s in %s: exit %d, %.0f s" % (" ".join(cmd), cwd, r.returncode, time.monotonic() - t0))
        if r.returncode:
            return 1
    for name, src in (("lane_hog", "lane_hog.c"), ("lane_wivrn", "lane_wivrn.c")):
        exe = os.path.join(WORK, name)
        subprocess.run(["cc", "-O2", "-pthread", "-o", exe, os.path.join(HERE, "lane", src)], check=True)
        say("built %s" % exe)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("kind", choices=["whisper", "tts", "build", "hog", "voice", "builds", "prepare"])
    p.add_argument("--seconds", type=float, default=60)
    p.add_argument("--threads", type=int, default=0)
    p.add_argument("--wav")
    p.add_argument("--tree")
    p.add_argument("--logic-ref", default="t3/som-hot-reload")
    p.add_argument("--models", default="base.en,small.en", help="voice: the STT models of a round")
    p.add_argument("--every", type=float, default=0, help="voice: a round every EVERY s (0 = back to back)")
    a = p.parse_args()
    nt = a.threads or int(os.environ.get("LANE_THREADS", "0") or 0) or 4
    if a.kind in ("whisper", "tts", "voice") and os.path.realpath(sys.executable) != os.path.realpath(VENV):
        os.execv(VENV, [VENV, "-I", os.path.abspath(__file__)] + sys.argv[1:])
    say("%s for %.0f s, %d threads, pid %d, CPUs %s" % (a.kind, a.seconds, nt, os.getpid(),
                                                       sorted(os.sched_getaffinity(0))))
    return {"whisper": whisper, "tts": tts, "build": build, "hog": hog, "voice": voice, "builds": builds,
            "prepare": prepare}[a.kind](a, nt) or 0


if __name__ == "__main__":
    sys.exit(main())
