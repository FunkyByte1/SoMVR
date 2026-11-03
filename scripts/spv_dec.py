#!/usr/bin/env python3
"""spv_dec.py - the game's translated shaders (IndirectX DXBC -> GLSL -> SPIR-V) read back as one line per store
(docs/re-xr-pipeline.md 137): every OpStore printed as `target = expression`, the expression built from the values it
uses (loads of the DXBC registers r0.., the constant buffers cbN[i], the inputs vN, constants), so the shader reads like
its DXBC. Branches print their labels. Not a general decompiler: enough for Feral's straight-line translations.

  spv_dec.py MODULE.spv [--func main]
"""
import struct
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])


def lit_string(words, i):
    b = b""
    while i < len(words):
        w = struct.pack("<I", words[i])
        i += 1
        if b"\0" in w:
            b += w[:w.index(b"\0")]
            break
        b += w
    return b.decode("utf-8", "replace"), i


BIN = {129: "+", 131: "-", 133: "*", 136: "/", 128: "+", 130: "-", 132: "*", 134: "/", 135: "/", 141: "%",
       142: "*", 143: "*", 144: "*", 145: "*", 146: "*", 166: "||", 167: "&&", 196: ">>", 194: ">>", 195: "<<",
       197: "|", 198: "^", 199: "&",
       180: "==", 181: "!=", 184: "<", 185: "<", 186: ">", 187: ">", 182: ">=", 183: ">=", 188: "<=", 189: "<=",
       170: "==", 171: "!=", 172: ">", 173: ">", 174: ">=", 175: ">=", 176: "<", 177: "<", 178: "<=", 179: "<=",
       190: "==", 191: "==", 192: "!=", 193: "!="}
GLSL = {1: "round", 2: "roundEven", 3: "trunc", 4: "abs", 6: "sign", 8: "floor", 9: "ceil", 10: "fract", 13: "sin",
        14: "cos", 15: "tan", 26: "pow", 27: "exp", 28: "log", 29: "exp2", 30: "log2", 31: "sqrt", 32: "inversesqrt",
        37: "min", 38: "umin", 39: "smin", 40: "max", 41: "umax", 42: "smax", 43: "clamp", 46: "mix", 48: "step",
        49: "smoothstep", 50: "fma", 66: "length", 67: "distance", 68: "cross", 69: "normalize", 79: "nmin", 80: "nmax",
        81: "nclamp", 16: "asin", 17: "acos", 18: "atan", 25: "atan2"}


def main():
    path = sys.argv[1]
    want = sys.argv[sys.argv.index("--func") + 1] if "--func" in sys.argv else "main"
    d = open(path, "rb").read()
    w = struct.unpack("<%dI" % (len(d) // 4), d)
    i = 5
    insts = []
    while i < len(w):
        wc, op = w[i] >> 16, w[i] & 0xffff
        insts.append((op, list(w[i + 1:i + wc])))
        i += max(wc, 1)
    names, consts, types, expr = {}, {}, {}, {}
    for op, a in insts:
        if op == 5:
            names[a[0]] = lit_string(a, 1)[0]
        elif op == 22:  # OpTypeFloat
            types[a[0]] = "f"
        elif op == 21:
            types[a[0]] = "i"
        elif op == 23:  # vector
            types[a[0]] = ("v", a[1], a[2])
        elif op == 43:  # OpConstant
            t = types.get(a[0])
            if t == "f":
                consts[a[1]] = "%.6g" % struct.unpack("<f", struct.pack("<I", a[2]))[0]
            else:
                consts[a[1]] = str(a[2] if a[2] < 0x80000000 else a[2] - (1 << 32))
        elif op == 44:  # OpConstantComposite
            consts[a[1]] = "(" + ", ".join(consts.get(x, "?") for x in a[2:]) + ")"
        elif op in (41, 42):
            consts[a[1]] = "true" if op == 41 else "false"

    def E(x):
        if x in consts:
            return consts[x]
        if x in expr:
            return expr[x]
        return names.get(x) or "%%%d" % x

    fn, on = None, False
    for op, a in insts:
        if op == 54:  # OpFunction
            fn = a[1]
            on = names.get(fn, "").startswith(want)
            if on:
                print("== function", names.get(fn))
            continue
        if not on:
            continue
        if op == 56:
            on = False
            continue
        r = a[1] if len(a) > 1 else None
        if op == 59:  # OpVariable
            continue
        if op == 61:  # OpLoad
            expr[r] = E(a[2])
        elif op == 65 or op == 66:  # OpAccessChain
            base = E(a[2])
            idx = a[3:]
            s = base
            for k in idx:
                ks = E(k)
                s += ".xyzw"[int(ks) + 1] if (ks.lstrip("-").isdigit() and len(idx) and k == idx[-1] and
                                              s.split("[")[0] in names.values() and s.startswith("r")) else "[%s]" % ks
            expr[r] = s
        elif op == 62:  # OpStore
            print("  %s = %s" % (E(a[0]), E(a[1])))
        elif op == 81:  # OpCompositeExtract
            idx = a[3:]
            expr[r] = "%s.%s" % (E(a[2]), "".join("xyzw"[k] if k < 4 else "[%d]" % k for k in idx))
        elif op == 80:  # OpCompositeConstruct
            expr[r] = "vec(%s)" % ", ".join(E(x) for x in a[2:])
        elif op == 79:  # OpVectorShuffle
            v1, v2 = E(a[2]), E(a[3])
            comps = a[4:]
            n1 = None
            # the first vector's size from its type is not tracked: name components by index (<4 = v1, else v2)
            if all(c < 4 for c in comps):
                expr[r] = "%s.%s" % (v1, "".join("xyzw"[c] for c in comps))
            else:
                expr[r] = "shuffle(%s, %s; %s)" % (v1, v2, ",".join(str(c) for c in comps))
        elif op in BIN:
            expr[r] = "(%s %s %s)" % (E(a[2]), BIN[op], E(a[3]))
        elif op == 127:  # OpFNegate
            expr[r] = "-%s" % E(a[2])
        elif op == 126:
            expr[r] = "-%s" % E(a[2])
        elif op == 148:  # OpDot
            expr[r] = "dot(%s, %s)" % (E(a[2]), E(a[3]))
        elif op == 142:  # OpVectorTimesScalar
            expr[r] = "(%s * %s)" % (E(a[2]), E(a[3]))
        elif op == 12:  # OpExtInst
            expr[r] = "%s(%s)" % (GLSL.get(a[3], "ext%d" % a[3]), ", ".join(E(x) for x in a[4:]))
        elif op == 124:  # OpBitcast
            expr[r] = "bits(%s)" % E(a[2])
        elif op in (109, 110, 111, 112, 113, 114, 115):
            expr[r] = "cvt(%s)" % E(a[2])
        elif op == 169:  # OpSelect
            expr[r] = "(%s ? %s : %s)" % (E(a[2]), E(a[3]), E(a[4]))
        elif op == 86:  # OpSampledImage
            expr[r] = "%s+%s" % (E(a[2]), E(a[3]))
        elif op in (87, 88, 89, 90, 91, 92, 93, 95):
            expr[r] = "sample%d(%s, %s)" % (op, E(a[2]), E(a[3]))
        elif op == 100:  # OpImage
            expr[r] = E(a[2])
        elif op == 57:  # OpFunctionCall
            expr[r] = "%s(%s)" % (names.get(a[2], "fn%d" % a[2]).split("(")[0], ", ".join(E(x) for x in a[3:]))
            print("  call %s = %s" % (names.get(r, r), expr[r]))
        elif op == 248:  # OpLabel
            print("L%d:" % a[0])
        elif op == 249:  # OpBranch
            print("  goto L%d" % a[0])
        elif op == 250:  # OpBranchConditional
            print("  if %s goto L%d else L%d" % (E(a[0]), a[1], a[2]))
        elif op == 245:  # OpPhi
            expr[r] = "phi(%s)" % ", ".join(E(x) for x in a[2::2])
        elif op in (246, 247):
            pass
        elif op in (253, 252):
            print("  return %s" % (E(a[0]) if a else ""))
        elif op == 252:
            print("  kill")
        else:
            expr[r] = "op%d(%s)" % (op, ", ".join(E(x) for x in a[2:]))


main()
