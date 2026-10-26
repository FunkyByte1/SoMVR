#!/usr/bin/env python3
"""Tests for tools/review_stereo.py on synthetic stereo pairs (no game, no files outside a temp folder).

Run: python3 -I tools/tests/test_review_stereo.py [-v]

The scene: a textured far wall at disparity d_bg and textured rectangles in front of it, each with its own
disparity d. Eye R sees every element moved left by its d (a near object sits further left in eye R) and down by dy.
"""
import contextlib
import importlib.util
import io
import json
import math
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

import numpy as np
from PIL import Image

sys.dont_write_bytecode = True
TOOL = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "review_stereo.py")
_spec = importlib.util.spec_from_file_location("review_stereo", TOOL)
rs = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(rs)

W, H, M = 640, 360, 200
OBJECTS = ((120, 60, 140, 100, 12), (380, 180, 160, 120, 25))  # x, y, w, h, disparity d (px)


def texture(rng, h, w, lo=40, hi=200):
    small = rng.integers(lo, hi, size=(h // 4 + 2, w // 4 + 2, 3), dtype=np.uint8)
    im = Image.fromarray(small).resize((small.shape[1] * 4, small.shape[0] * 4), Image.Resampling.BICUBIC)
    return np.asarray(im)[:h, :w].copy()


def scene(d_bg=2, objects=OBJECTS, dy=0, seed=1, lo=40, hi=200):
    rng = np.random.default_rng(seed)
    bg = texture(rng, H + 2 * M, W + 2 * M, lo, hi)
    L = bg[M: M + H, M: M + W].copy()
    R = bg[M - dy: M - dy + H, M + d_bg: M + d_bg + W].copy()
    for x, y, w, h, d in objects:
        t = texture(rng, h, w, lo, hi)
        L[y: y + h, x: x + w] = t
        R[y + dy: y + dy + h, x - d: x - d + w] = t
    return L, R


def add(img, delta):
    """add a per-column luma offset (array of W values) to every channel"""
    return np.clip(img.astype(np.int16) + np.asarray(delta, np.int16)[None, :, None], 0, 255).astype(np.uint8)


class ReviewStereo(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix="review_stereo_test.")
        cls.n = 0

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def run_tool(self, L, R, *opts, images=False):
        type(self).n += 1
        p = os.path.join(self.tmp, "f%d.ppm" % self.n)
        Image.fromarray(np.concatenate([L, R], axis=1)).save(p)
        out = os.path.join(self.tmp, "out%d" % self.n)
        with contextlib.redirect_stdout(io.StringIO()):
            rc = rs.main(["-o", out] + ([] if images else ["--no-images"]) + list(opts) + [p])
        with open(os.path.join(out, "report.json")) as f:
            fr = json.load(f)["frames"][0]
        return rc, fr, {f["code"] for f in fr["flags"]}

    # ---- disparity ----
    def test_good_pair_no_flags(self):
        rc, fr, flags = self.run_tool(*scene())
        self.assertEqual(flags, set(), fr["flags"])
        self.assertEqual(rc, 0)
        dp = fr["disparity"]
        self.assertGreaterEqual(dp["matched"], 100)
        self.assertAlmostEqual(dp["d_median"], 2, delta=0.3)
        self.assertAlmostEqual(dp["d_max"], 25, delta=0.5)
        self.assertEqual(dp["beyond"], 0)
        self.assertLess(abs(dp["dy_median"]), 0.2)
        ds = sorted({round(b[2]) for b in dp["blocks"]})
        self.assertEqual(ds, [2, 12, 25])  # the three depths, nothing else
        self.assertIsNone(fr["seam"]["L"]["kind"])
        self.assertIsNone(fr["seam"]["R"]["kind"])

    def test_swapped_pair(self):
        L, R = scene()
        _rc, fr, flags = self.run_tool(R, L)
        self.assertEqual(flags, {"EYES_SWAPPED"}, fr["flags"])
        self.assertAlmostEqual(fr["disparity"]["d_median"], -2, delta=0.3)

    def test_flat_no_parallax(self):
        L, R = scene(d_bg=0, objects=())  # R == L: give eye R its own noise so the eyes are not identical
        R = np.clip(R.astype(np.int16) + np.random.default_rng(3).integers(-4, 5, R.shape), 0, 255).astype(np.uint8)
        _rc, fr, flags = self.run_tool(L, R)
        self.assertEqual(flags, {"FLAT"}, fr["flags"])

    def test_vertical_offset(self):
        _rc, fr, flags = self.run_tool(*scene(dy=4))
        self.assertEqual(flags, {"VERTICAL"}, fr["flags"])
        self.assertAlmostEqual(fr["disparity"]["dy_median"], 4, delta=0.3)
        self.assertIn("lower", fr["flags"][0]["msg"])

    def test_rolled_eye(self):
        L, R = scene()
        R = np.asarray(Image.fromarray(R).rotate(0.8, resample=Image.Resampling.BICUBIC))  # ~4.5 px at the edges
        _rc, fr, flags = self.run_tool(L, R)
        self.assertIn("VERTICAL", flags, fr["flags"])
        self.assertIn("rolled", [f for f in fr["flags"] if f["code"] == "VERTICAL"][0]["msg"])
        self.assertGreater(abs(fr["disparity"]["dy_roll_edge_px"]), 2)

    def test_depth_from_ipd_fov(self):
        # fov 90: 320 px per tan unit at W 640; ipd 6.4 cm; d 20 px = 102.4 cm
        _rc, fr, flags = self.run_tool(*scene(d_bg=0, objects=((300, 60, 200, 260, 20),)), "--ipd-cm", "6.4",
                                       "--fov-deg", "90")
        self.assertEqual(flags, set(), fr["flags"])
        self.assertAlmostEqual(fr["geometry"]["px_per_tan"], 320, delta=0.1)
        self.assertAlmostEqual(fr["disparity"]["depth_near_cm"], 102.4, delta=3)
        self.assertAlmostEqual(fr["geometry"]["expected_px"]["100cm"], 20.48, delta=0.05)

    def test_too_near(self):
        # d 150 px with ipd 6.4 cm at 320 px per tan unit = 13.7 cm < 15
        _rc, fr, flags = self.run_tool(*scene(d_bg=0, objects=((300, 60, 200, 260, 150),)), "--ipd-cm", "6.4",
                                       "--fov-deg", "90")
        self.assertEqual(flags, {"TOO_NEAR"}, fr["flags"])
        self.assertAlmostEqual(fr["disparity"]["depth_near_cm"], 13.65, delta=0.5)

    def test_asymmetric_fov(self):
        # eye L angles -50..40 deg, eye R mirrored: infinity sits d_inf = s*(tan 50 - tan 40) px apart
        s = W / (math.tan(math.radians(40)) + math.tan(math.radians(50)))
        d_inf = s * (math.tan(math.radians(50)) - math.tan(math.radians(40)))
        di = round(d_inf)  # 111
        opts = ("--ipd-cm", "6.4", "--fov-deg=-50,40")
        pair = scene(d_bg=di, objects=((300, 60, 200, 260, di + 20),))
        _rc, fr, flags = self.run_tool(*pair, *opts)
        self.assertEqual(flags, set(), fr["flags"])
        self.assertAlmostEqual(fr["geometry"]["d_inf_px"], d_inf, delta=0.01)
        self.assertAlmostEqual(fr["disparity"]["depth_near_cm"], s * 6.4 / (20 + di - d_inf), delta=3)
        self.assertIn("overlap", fr["luma"])
        _rc, fr, flags = self.run_tool(*reversed(pair), *opts)
        self.assertIn("EYES_SWAPPED", flags, fr["flags"])
        _rc, fr, flags = self.run_tool(*scene(d_bg=di, objects=()), *opts)
        self.assertEqual(flags, {"FLAT"}, fr["flags"])

    # ---- identical eyes ----
    def test_identical_eyes(self):
        L, _R = scene()
        _rc, fr, flags = self.run_tool(L, L.copy())
        self.assertEqual(flags, {"IDENTICAL_EYES"}, fr["flags"])
        self.assertTrue(fr["eyes_diff"]["identical"])

    def test_near_identical_eyes(self):
        L, _R = scene()
        rng = np.random.default_rng(7)
        R = np.clip(L.astype(np.int16) + rng.choice([-1, 0, 0, 0, 1], L.shape), 0, 255).astype(np.uint8)
        _rc, fr, flags = self.run_tool(L, R)
        self.assertIn("NEAR_IDENTICAL", flags, fr["flags"])
        self.assertNotIn("IDENTICAL_EYES", flags)

    # ---- brightness ----
    def test_brightness_split(self):
        L, R = scene()
        R = (R.astype(np.float32) * 0.9).astype(np.uint8)
        _rc, fr, flags = self.run_tool(L, R)
        self.assertEqual(flags, {"LUMA_SPLIT"}, fr["flags"])
        self.assertAlmostEqual(fr["luma"]["ratio"], 1 / 0.9, delta=0.02)

    def test_small_brightness_difference_passes(self):
        L, R = scene()
        R = add(R, np.full(W, -2))  # ~1.7 %: under the 3 % limit
        _rc, fr, flags = self.run_tool(L, R)
        self.assertEqual(flags, set(), fr["flags"])

    def test_run2_blown_and_dark(self):
        L, R = scene()
        L = np.clip(L.astype(np.int16) * 3, 0, 255).astype(np.uint8)
        R = R // 30
        _rc, fr, flags = self.run_tool(L, R)
        self.assertTrue({"BLOWN_OUT_L", "DARK_R", "LUMA_SPLIT", "CLIP_SPLIT"} <= flags, fr["flags"])
        self.assertNotIn("BLOWN_OUT_R", flags)
        self.assertNotIn("DARK_L", flags)

    # ---- seams ----
    def test_hard_seam(self):
        L, R = scene()
        L = add(L, np.where(np.arange(W) >= 400, 30, 0))
        _rc, fr, flags = self.run_tool(L, R)
        self.assertIn("SEAM_HARD_L", flags, fr["flags"])
        self.assertNotIn("SEAM_HARD_R", flags)
        self.assertNotIn("SEAM_SOFT_L", flags)
        self.assertEqual(fr["seam"]["L"]["hard_x"], 400)

    def test_soft_band(self):
        L, R = scene(lo=30, hi=140)
        x = np.arange(W)
        L = add(L, np.clip((x - 300) * 1.0, 0, 100))  # a 100 px ramp of +100 levels
        _rc, fr, flags = self.run_tool(L, R)
        self.assertIn("SEAM_SOFT_L", flags, fr["flags"])
        self.assertNotIn("SEAM_HARD_L", flags)
        self.assertNotIn("SEAM_SOFT_R", flags)
        sm = fr["seam"]["L"]
        self.assertTrue(300 <= sm["band_x"] <= 400 - rs.SEAM_BAND_K, sm)
        self.assertTrue(70 <= sm["band_width_px"] <= 130, sm)

    def test_scene_edge_is_not_a_seam(self):
        # a dark full-height pillar 10 px nearer than the wall: its edges are in both eyes, shifted by d
        L, R = scene(objects=())
        L[:, 300:340] = 20
        R[:, 290:330] = 20
        _rc, fr, flags = self.run_tool(L, R)
        self.assertEqual(flags, set(), fr["flags"])
        self.assertEqual(fr["seam"]["L"]["kind"], "edge")
        self.assertEqual(fr["seam"]["L"]["twin_d"], 10)
        self.assertTrue(any("scene edge" in n for n in fr["notes"]), fr["notes"])
        # the same pillar in eye L only: a seam
        L2, R2 = scene(objects=())
        L2[:, 300:340] = 20
        _rc, fr, flags = self.run_tool(L2, R2)
        self.assertIn("SEAM_HARD_L", flags, fr["flags"])

    # ---- the command line: files, mono, split PNG, the layer log, html ----
    def test_cli_mono_split_log_html(self):
        d = tempfile.mkdtemp(dir=self.tmp)
        L, R = scene()
        dump = os.path.join(d, "xr-layer.log.stereo60.ppm")
        Image.fromarray(np.concatenate([R, L], axis=1)).save(dump)  # swapped: a flag for the html
        with open(os.path.join(d, "xr-layer.log"), "w") as f:
            f.write("xr: something else\nxr: stereo frame 812 (tag 3, applied 0x3, flags 0x0, display-time error "
                    "+0.120 ms): views -0.0320,+1.6000,+0.0000 / +0.0320,+1.6000,+0.0000 fov -0.7854..0.7854 / "
                    "-0.7854..0.7854 rects 0+640 / 640+640 (stereo 3, reuse 0, mono 0)\n")
        mono = os.path.join(d, "shot.png")
        Image.fromarray(np.dstack([L, np.full((H, W), 255, np.uint8)])).save(mono)  # RGBA like gamescope
        sbs = os.path.join(d, "sbs.png")
        Image.fromarray(np.concatenate([L, R], axis=1)).save(sbs)
        odd = os.path.join(d, "odd.png")
        Image.fromarray(L[:, :W - 1]).save(odd)
        out = os.path.join(d, "out")
        cmd = [sys.executable, "-I", "-B", TOOL, "-o", out]
        p = subprocess.run(cmd + [dump, mono], capture_output=True, text=True)
        self.assertEqual(p.returncode, 1, p.stderr)
        with open(os.path.join(out, "report.json")) as f:
            frames = json.load(f)["frames"]
        st, mo = frames
        self.assertEqual(st["name"], "xr-layer.log.stereo60")
        self.assertEqual(st["kind"], "stereo")
        self.assertIn("EYES_SWAPPED", {f["code"] for f in st["flags"]})
        self.assertAlmostEqual(st["geometry"]["ipd_cm"], 6.4, delta=0.001)
        self.assertEqual(st["geometry"]["fov_deg"], [-45.0, 45.0, -45.0, 45.0])
        self.assertTrue(any("stereo frame 812" in n for n in st["notes"]), st["notes"])
        for im in ("left.png", "right.png", "preview.png", "cross.png", "anaglyph.png", "blocks.png", "report.json"):
            self.assertTrue(os.path.isfile(os.path.join(out, st["name"], im)), im)
        with Image.open(os.path.join(out, st["name"], "preview.png")) as im:
            self.assertEqual(im.size, (W, H // 2))
        with Image.open(os.path.join(out, st["name"], "anaglyph.png")) as im:
            self.assertEqual(im.size, (W, H))
        self.assertEqual(mo["kind"], "mono")
        self.assertEqual(mo["flags"], [])
        self.assertEqual(set(mo["luma"]), {"view"})
        self.assertNotIn("disparity", mo)
        self.assertTrue(os.path.isfile(os.path.join(out, "shot", "view.png")))
        self.assertIn("xr-layer.log.stereo60: FLAGS", p.stdout)
        with open(os.path.join(out, "index.html")) as f:
            page = f.read()
        self.assertIn("<div class='flag'>EYES_SWAPPED", page)
        self.assertIn("src='xr-layer.log.stereo60/preview.png'", page)
        self.assertNotIn(d, page.replace(os.path.abspath(dump), "").replace(os.path.abspath(mono), ""))
        # --split for a PNG; an odd width is an input error (exit 2) and the other input is still reviewed
        out2 = os.path.join(d, "out2")
        p = subprocess.run(cmd[:-1] + [out2, "--split", sbs, odd], capture_output=True, text=True)
        self.assertEqual(p.returncode, 2, p.stderr)
        with open(os.path.join(out2, "report.json")) as f:
            a, b = json.load(f)["frames"]
        self.assertEqual((a["kind"], a["flags"]), ("stereo", []))
        self.assertIn("odd width", b["error"])
        # --mono for a dump
        out3 = os.path.join(d, "out3")
        p = subprocess.run(cmd[:-1] + [out3, "--mono", "--no-images", dump], capture_output=True, text=True)
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertIn("mono %dx%d" % (2 * W, H), p.stdout)


if __name__ == "__main__":
    unittest.main()
