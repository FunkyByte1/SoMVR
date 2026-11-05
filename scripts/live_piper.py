#!/usr/bin/env python3
"""live_piper.py - the live loop's fast text-to-speech worker (docs/re-xr-pipeline.md 162.9, som-b37-live3): Piper
(a VITS voice, ONNX, onnxruntime CPU), loaded once from shared/tools/tts/models/piper-voices (nothing downloaded), then
one job per stdin line - the same protocol as live_kokoro.py (live_tts_common.py: the line streamed sentence by sentence).
Never plays anything: it only writes WAV files (the daemon plays them, live_tts.py).

  shared/tools/tts/piper-venv/bin/python -I live_piper.py [--voice en_US-lessac-medium] [--threads 1]
  ... --bench TEXT... [--bench-out DIR]

Run only inside the lane (scripts/live_lane.sh exec tts ...), one thread, never the GPU. The voice: lessac (medium) -
a clear, even US English voice; the WAV at 22.05 kHz, the same level rules as Kokoro's.
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))  # (run with -I: our own folder only)
import live_tts_common as C  # noqa: E402

VOICES = "/home/bara/Projects/flat2vr/shared/tools/tts/models/piper-voices"


class Piper:
    def __init__(self, voice, threads, speed):
        import numpy as np
        import onnxruntime as ort
        from piper import PiperVoice, SynthesisConfig
        self.np = np
        path = os.path.join(VOICES, voice + ".onnx")
        v = PiperVoice.load(path)
        so = ort.SessionOptions()   # the session again with one thread (PiperVoice.load takes onnxruntime's default)
        so.intra_op_num_threads, so.inter_op_num_threads = max(1, threads), 1
        so.execution_mode = ort.ExecutionMode.ORT_SEQUENTIAL
        v.session = ort.InferenceSession(path, so, providers=["CPUExecutionProvider"])
        self.v, self.rate = v, v.config.sample_rate
        self.cfg = SynthesisConfig(length_scale=1.0 / max(0.5, speed), normalize_audio=False)

    def synth(self, text):
        np = self.np
        out = [np.asarray(c.audio_float_array, dtype=np.float32).reshape(-1) for c in self.v.synthesize(text, self.cfg)]
        if not out:
            raise ValueError("nothing to say")
        return np.concatenate(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--voice", default=os.environ.get("SOMVR_LIVE_PIPER_VOICE", "en_US-lessac-medium"))
    ap.add_argument("--threads", type=int, default=int(os.environ.get("LANE_THREADS", "1") or 1))
    ap.add_argument("--speed", type=float, default=1.0)
    ap.add_argument("--bench", nargs="*")
    ap.add_argument("--bench-out", default=os.environ.get("TMPDIR", "/tmp") + "/live-tts-bench")
    a = ap.parse_args()
    t0 = time.monotonic()
    try:
        p = Piper(a.voice, a.threads, a.speed)
        p.synth("Ready.")   # the first run's set-up before the first real line
    except Exception as e:
        print(f"fail - Piper did not load ({type(e).__name__}: {e})", flush=True)
        return 2
    if a.bench is not None:
        print(f"ready piper {a.voice} load {time.monotonic() - t0:.2f}", flush=True)
        return C.bench(p, p.np, "piper-" + a.voice.split("-")[1], a.bench, a.bench_out)
    return C.serve(p, p.np, "piper", a.voice, t0)


if __name__ == "__main__":
    sys.exit(main())
