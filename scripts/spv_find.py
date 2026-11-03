#!/usr/bin/env python3
"""spv_find.py - find the fog among the game's shader modules (docs/re-xr-pipeline.md 137): the layer's
SOMVR_XR_SPV_DUMP=DIR,all writes every module; the fog overlay is a full-screen fragment shader that reads the depth,
exponentiates (the extinction) and raises to the 1.5th power (the Henyey-Greenstein phase of the sun's Mie haze).

  spv_find.py DIR [N]      the N (20) fragment modules scoring highest, with their counts
"""
import os
import struct
import sys


def scan(path):
    d = open(path, "rb").read()
    w = struct.unpack("<%dI" % (len(d) // 4), d)
    if not w or w[0] != 0x07230203:
        return None
    i, frag, ext, consts, img, dots, fdiv, n = 5, 0, {}, set(), 0, 0, 0, 0
    glsl_set = None
    while i < len(w):
        wc, op = w[i] >> 16, w[i] & 0xffff
        a = w[i + 1:i + wc]
        if op == 15 and a and a[0] == 4:  # OpEntryPoint Fragment
            frag = 1
        elif op == 11:  # OpExtInstImport
            glsl_set = a[0]
        elif op == 12 and len(a) >= 4 and a[2] == glsl_set:  # OpExtInst
            ext[a[3]] = ext.get(a[3], 0) + 1
        elif op == 43 and len(a) == 3:  # OpConstant (32-bit)
            consts.add(round(struct.unpack("<f", struct.pack("<I", a[2]))[0], 4))
        elif op in (87, 88, 89, 90, 91, 92, 93, 95):  # OpImageSample* / OpImageFetch...
            img += 1
        elif op == 148:  # OpDot
            dots += 1
        elif op == 136:  # OpFDiv
            fdiv += 1
        n += 1
        i += max(wc, 1)
    return frag, ext, consts, img, dots, fdiv, n, len(d)


def main():
    dr = sys.argv[1]
    top = int(sys.argv[2]) if len(sys.argv) > 2 else 20
    rows = []
    for f in sorted(os.listdir(dr)):
        if not f.endswith(".spv"):
            continue
        r = scan(os.path.join(dr, f))
        if not r or not r[0]:
            continue
        frag, ext, consts, img, dots, fdiv, n, size = r
        exp = ext.get(27, 0) + ext.get(29, 0)  # Exp, Exp2
        pw = ext.get(26, 0)  # Pow
        has15 = 1.5 in consts or -1.5 in consts
        score = 3 * min(exp, 3) + 2 * min(pw, 3) + 4 * has15 + (img <= 4) + 2 * (dots >= 1)
        rows.append((score, f, exp, pw, has15, img, dots, fdiv, n, size))
    rows.sort(reverse=True)
    print("%d fragment modules" % len(rows))
    for r in rows[:top]:
        print("score %2d %s: exp %d pow %d 1.5 %s samples %d dots %d fdiv %d insts %d bytes %d" % r)


main()
