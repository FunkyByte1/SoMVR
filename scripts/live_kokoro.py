#!/usr/bin/env python3
"""live_kokoro.py - the live loop's text-to-speech worker (docs/re-xr-pipeline.md 162.8): Kokoro-82M (ONNX, CPU) with
misaki's English phonemes, loaded once from shared/tools/tts/models (nothing downloaded), then one job per stdin line.
Never plays anything: it only writes WAV files (the daemon plays them, live_tts.py).

  shared/tools/tts/venv/bin/python -I live_kokoro.py [--voice af_heart] [--threads 2] [--speed 1.0]
      stdin:  OUT.wav<TAB>TEXT           one job per line (TEXT: one line, at most ~600 characters)
      stdout: "ready kokoro VOICE load S" once, then "done OUT SYNTH_S AUDIO_S" (or "fail OUT why") per job
  ... --bench TEXT...                      each text once: its time (the model's cost, offline)

Run only inside the lane (scripts/live_lane.sh exec tts ...: its CPUs, SCHED_IDLE, nice 19) - never on the GPU
(onnxruntime's CPU provider only). The WAV: 24 kHz mono s16le, the speech's RMS at -20 dBFS (about a game's dialogue
line), its peak at most -1 dBFS, 0.15 s of silence before and after; written atomically (OUT.tmp, then renamed).
The voice: af_heart (Kokoro's best-rated English voice, and unlike every voice in the game - the player knows at once
it is the mod talking, not a character).
"""
import argparse
import json
import os
import re
import sys
import time
import wave

TTS = "/home/bara/Projects/flat2vr/shared/tools/tts/models"
MODEL = os.path.join(TTS, "kokoro-82m-v1.0-onnx-timestamped")
VOICE_DIRS = [os.path.join(TTS, "kokoro-82m-v1.0-voices-female", "voices"), os.path.join(MODEL, "voices")]
RATE = 24000
MAX_IDS = 500          # the model takes 510 tokens; a longer text goes in pieces (by sentences, then by commas)
TARGET_RMS = 0.1       # -20 dBFS
PEAK = 0.89            # -1 dBFS


def voice_path(name):
    for d in VOICE_DIRS:
        p = os.path.join(d, name + ".bin")
        if os.path.exists(p):
            return p
    raise FileNotFoundError(f"no Kokoro voice {name} in {VOICE_DIRS}")


def speakable(text):
    """the card's text as one spoken line: the menu path's '>' and the list marks read as pauses, the ASCII dots kept"""
    t = text.replace(">", ", ").replace("|", ", ").replace("_", " ").replace("...", ".")
    t = re.sub(r"\s+", " ", t).strip()
    return t


class Kokoro:
    def __init__(self, voice, threads, speed):
        import numpy as np
        import onnxruntime as ort
        from misaki import en, espeak
        self.np = np
        so = ort.SessionOptions()
        so.intra_op_num_threads, so.inter_op_num_threads = max(1, threads), 1
        so.execution_mode = ort.ExecutionMode.ORT_SEQUENTIAL
        self.s = ort.InferenceSession(os.path.join(MODEL, "onnx", "model.onnx"), so, providers=["CPUExecutionProvider"])
        self.vocab = json.load(open(os.path.join(MODEL, "tokenizer.json")))["model"]["vocab"]
        self.voice = np.fromfile(voice_path(voice), dtype=np.float32).reshape(-1, 1, 256)
        try:
            fb = espeak.EspeakFallback(british=False)
        except Exception:  # (espeak-ng's library missing: misaki's own lexicon only)
            fb = None
        self.g2p = en.G2P(trf=False, british=False, fallback=fb)
        self.speed = speed

    def ids(self, text):
        ps, _ = self.g2p(text)
        return [self.vocab[c] for c in ps if c in self.vocab]

    def pieces(self, text):
        """the text cut where the phonemes would pass MAX_IDS: sentences, then commas, then words"""
        out = []
        for sent in re.split(r"(?<=[.!?])\s+", text):
            if not sent:
                continue
            ids = self.ids(sent)
            if len(ids) <= MAX_IDS:
                out.append(ids)
                continue
            cur = ""
            for part in re.split(r"(?<=[,;:])\s+|\s+", sent):
                trial = (cur + " " + part).strip()
                if cur and len(self.ids(trial)) > MAX_IDS:
                    out.append(self.ids(cur)[:MAX_IDS])
                    cur = part
                else:
                    cur = trial
            if cur:
                out.append(self.ids(cur)[:MAX_IDS])
        return [i for i in out if i]

    def synth(self, text):
        np = self.np
        chunks = []
        for ids in self.pieces(speakable(text)):
            style = self.voice[min(len(ids), len(self.voice) - 1)]
            feed = {"input_ids": np.array([[0] + ids + [0]], dtype=np.int64), "style": style.astype(np.float32),
                    "speed": np.array([self.speed], dtype=np.float32)}
            w = np.asarray(self.s.run(None, feed)[0], dtype=np.float32).reshape(-1)
            chunks += [w, np.zeros(int(RATE * 0.12), dtype=np.float32)]
        if not chunks:
            raise ValueError("nothing to say")
        pad = np.zeros(int(RATE * 0.15), dtype=np.float32)
        a = np.concatenate([pad] + chunks[:-1] + [pad])
        rms = float(np.sqrt(np.mean(a[np.abs(a) > 1e-4] ** 2))) if np.any(np.abs(a) > 1e-4) else 0.0
        if rms > 0:
            a *= TARGET_RMS / rms
        pk = float(np.max(np.abs(a))) if len(a) else 0.0
        if pk > PEAK:
            a *= PEAK / pk
        return (np.clip(a, -1, 1) * 32767).astype(np.int16), len(a) / RATE


def write_wav(path, pcm):
    tmp = path + ".tmp"
    with wave.open(tmp, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(pcm.tobytes())
    os.replace(tmp, path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--voice", default=os.environ.get("SOMVR_LIVE_TTS_VOICE", "af_heart"))
    ap.add_argument("--threads", type=int, default=int(os.environ.get("LANE_THREADS", "2") or 2))
    ap.add_argument("--speed", type=float, default=1.0)
    ap.add_argument("--bench", nargs="*")
    a = ap.parse_args()
    t0 = time.monotonic()
    try:
        k = Kokoro(a.voice, a.threads, a.speed)
        k.synth("Ready.")  # the first run's set-up (onnxruntime's allocations) before the first real line
    except Exception as e:
        print(f"fail - Kokoro did not load ({type(e).__name__}: {e})", flush=True)
        return 2
    print(f"ready kokoro {a.voice} load {time.monotonic() - t0:.2f}", flush=True)
    if a.bench is not None:
        for text in a.bench:
            t1 = time.monotonic()
            _, dur = k.synth(text)
            print(f"bench {len(text)} chars -> {dur:.2f} s of speech in {time.monotonic() - t1:.2f} s", flush=True)
        return 0
    for line in sys.stdin:
        line = line.rstrip("\n")
        if not line:
            continue
        out, _, text = line.partition("\t")
        try:
            t1 = time.monotonic()
            pcm, dur = k.synth(text)
            write_wav(out, pcm)
            print(f"done {out} {time.monotonic() - t1:.2f} {dur:.2f}", flush=True)
        except Exception as e:
            print(f"fail {out} {type(e).__name__}: {e}", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
