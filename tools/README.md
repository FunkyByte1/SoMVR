# ShadowOfMordor/tools

Offline helpers. They never launch the game or a VR app. Run them with `python3 -I`; they need numpy and Pillow.

## review_stereo.py: both eyes "as a player"

This is the review step before a build reaches the user. It reads the vklayer's `SOMVR_XR_DUMP` pictures
(`<SOMVR_XR_LOG>.stereoN.ppm`: P6, both eyes side by side, sRGB bytes as submitted). It also reads side-by-side PNGs
(`--split`) and single desktop views (`--mono`, or any non-PPM by default).

```sh
python3 -I tools/review_stereo.py -o $RUN/review $RUN/xr-layer.log.stereo{60,300,700}.ppm
python3 -I tools/review_stereo.py -o $TMPDIR/shots --mono logs/boot-check-20261019-211128/shots/*.png
python3 -I tools/review_stereo.py -o $TMPDIR/sbs --split --ipd-cm 6.3 --fov-deg=-52,44 shot.png
```

Open `OUT/index.html`. It is self-contained and uses relative links. For each frame it shows:
- the flags in red;
- the parallel (L|R) and cross-eye (R|L) pairs at half size;
- a red/cyan anaglyph;
- eye L with the matched disparity blocks;
- links to the full-size eyes;
- the numbers.

`OUT/report.json` holds every number. `OUT/report.txt` holds one human line per frame, the same lines the tool
prints. Exit status: 0 = no flags, 1 = flags, 2 = an unreadable input.

For a dump, the IPD and the per-eye fov come from the layer log next to it: the last `xr: stereo frame` line.
`--ipd-cm` / `--fov-deg` override them. Without a fov, infinity is taken to sit at disparity 0.

| Flag | Means |
|---|---|
| `LUMA_SPLIT` | mean luma L/R more than 3 % apart (re-stereo.md 9.5 test 1); measured over the overlap when the fov is known |
| `BLOWN_OUT_L/R`, `DARK_L/R`, `CLIP_SPLIT` | an eye > 25 % white or > 50 % black, or the eyes' clipping differs (run 2's 250/27) |
| `SEAM_HARD_L/R`, `SEAM_SOFT_L/R` | a vertical step or gradient band shared by >= 90 % of the row bands (run 2's ~100 px band); a step with a twin in the other eye at a real disparity counts as a scene edge (a note, no flag) |
| `IDENTICAL_EYES`, `NEAR_IDENTICAL` | the stereo did not happen |
| `EYES_SWAPPED`, `DIVERGENT` | most blocks with depth (or > 15 % of all blocks) lie beyond infinity; a near object must sit further left in eye R |
| `FLAT` | no parallax (eye offset 0, or nothing near) |
| `TOO_NEAR` | with IPD and fov: the nearest blocks imply less than 15 cm (`--min-depth-cm`) |
| `VERTICAL` | vertical disparity: an eye shifted (median > 1 px), rolled, or scaled vertically |

The script's docstring has every threshold and the block-matching method. Tests (synthetic pairs, one per flag, plus
the command line): `python3 -I tools/tests/test_review_stereo.py`.
