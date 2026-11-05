#!/usr/bin/env python3
"""live_kokoro.py - the live loop's text-to-speech worker (docs/re-xr-pipeline.md 162.8): Kokoro-82M (ONNX, CPU) with
misaki's English phonemes, loaded once from shared/tools/tts/models (nothing downloaded), then one job per stdin line.
Never plays anything: it only writes WAV files (the daemon plays them, live_tts.py).

  shared/tools/tts/venv/bin/python -I live_kokoro.py [--voice af_heart] [--threads 1] [--speed 1.0]
      stdin:  OUT.wav<TAB>TEXT           one job per line (TEXT: one line, at most ~600 characters)
      stdout: "ready kokoro VOICE load S" once, then per job "part OUT I SYNTH_S AUDIO_S" per sentence as it is
              written (b37 live3: the line streamed - piece 0 = OUT.wav, piece I = OUT.I.wav; live_tts_common.py) and
              "done OUT SYNTH_S AUDIO_S N" (or "fail OUT why")
  ... --bench TEXT... [--bench-out DIR]   each text once, streamed: the first piece's time and the whole line's

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

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))  # (run with -I: our own folder only)
import live_tts_common as C  # noqa: E402

TTS = "/home/bara/Projects/flat2vr/shared/tools/tts/models"
MODEL = os.path.join(TTS, "kokoro-82m-v1.0-onnx-timestamped")
VOICE_DIRS = [os.path.join(TTS, "kokoro-82m-v1.0-voices-female", "voices"), os.path.join(MODEL, "voices")]
RATE = 24000
MAX_IDS = 500          # the model takes 510 tokens; a longer text goes in pieces (by sentences, then by commas)


def voice_path(name):
    for d in VOICE_DIRS:
        p = os.path.join(d, name + ".bin")
        if os.path.exists(p):
            return p
    raise FileNotFoundError(f"no Kokoro voice {name} in {VOICE_DIRS}")


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
        self.speed, self.rate = speed, RATE

    def ids(self, text):
        ps, _ = self.g2p(text)
        return [self.vocab[c] for c in ps if c in self.vocab]

    def synth(self, text):
        """one piece (a sentence) -> float audio; a sentence past MAX_IDS phonemes in parts (commas, then words)"""
        np = self.np
        idss = []
        ids = self.ids(text)
        if len(ids) <= MAX_IDS:
            idss = [ids]
        else:
            cur = ""
            for part in re.split(r"(?<=[,;:])\s+|\s+", text):
                trial = (cur + " " + part).strip()
                if cur and len(self.ids(trial)) > MAX_IDS:
                    idss.append(self.ids(cur)[:MAX_IDS])
                    cur = part
                else:
                    cur = trial
            if cur:
                idss.append(self.ids(cur)[:MAX_IDS])
        out = []
        for ids in (i for i in idss if i):
            style = self.voice[min(len(ids), len(self.voice) - 1)]
            feed = {"input_ids": np.array([[0] + ids + [0]], dtype=np.int64), "style": style.astype(np.float32),
                    "speed": np.array([self.speed], dtype=np.float32)}
            out.append(np.asarray(self.s.run(None, feed)[0], dtype=np.float32).reshape(-1))
        if not out:
            raise ValueError("nothing to say")
        return np.concatenate(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--voice", default=os.environ.get("SOMVR_LIVE_TTS_VOICE", "af_heart"))
    ap.add_argument("--threads", type=int, default=int(os.environ.get("LANE_THREADS", "1") or 1))
    ap.add_argument("--speed", type=float, default=1.0)
    ap.add_argument("--bench", nargs="*")
    ap.add_argument("--bench-out", default=os.environ.get("TMPDIR", "/tmp") + "/live-tts-bench")
    a = ap.parse_args()
    t0 = time.monotonic()
    try:
        k = Kokoro(a.voice, a.threads, a.speed)
        k.synth("Ready.")  # the first run's set-up (onnxruntime's allocations) before the first real line
    except Exception as e:
        print(f"fail - Kokoro did not load ({type(e).__name__}: {e})", flush=True)
        return 2
    if a.bench is not None:
        print(f"ready kokoro {a.voice} load {time.monotonic() - t0:.2f}", flush=True)
        return C.bench(k, k.np, "kokoro", a.bench, a.bench_out)
    return C.serve(k, k.np, "kokoro", a.voice, t0)


if __name__ == "__main__":
    sys.exit(main())
