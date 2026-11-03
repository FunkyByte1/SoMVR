#!/usr/bin/env python3
"""spv_dis.py - a small SPIR-V disassembler (docs/re-xr-pipeline.md 137) from the system's SPIR-V grammar
(/usr/include/spirv/unified1/spirv.core.grammar.json + the GLSL.std.450 extended set): the game's shader modules as the
layer dumps them (SOMVR_XR_SPV_DUMP) in spirv-dis's style, ids named from OpName / OpMemberName where the module has them.

  spv_dis.py MODULE.spv [--names]      (--names: only the OpName / OpMemberName / OpDecorate lines)
"""
import json
import struct
import sys

G = "/usr/include/spirv/unified1/"


def load_grammar():
    core = json.load(open(G + "spirv.core.grammar.json"))
    ops = {i["opcode"]: i for i in core["instructions"]}
    kinds = {k["kind"]: k for k in core["operand_kinds"]}
    glsl = {i["opcode"]: i["opname"] for i in json.load(open(G + "extinst.glsl.std.450.grammar.json"))["instructions"]}
    return ops, kinds, glsl


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


def main():
    ops, kinds, glsl = load_grammar()
    data = open(sys.argv[1], "rb").read()
    words = struct.unpack("<%dI" % (len(data) // 4), data)
    names_only = "--names" in sys.argv
    if words[0] != 0x07230203:
        sys.exit("not SPIR-V")
    names, members = {}, {}
    i = 5
    insts = []
    while i < len(words):
        wc, op = words[i] >> 16, words[i] & 0xffff
        insts.append((op, words[i + 1:i + wc]))
        i += max(wc, 1)
    for op, a in insts:
        if op == 5:  # OpName
            names[a[0]] = lit_string(a, 1)[0]
        elif op == 6:  # OpMemberName
            members[(a[0], a[1])] = lit_string(a, 2)[0]

    def nm(x):
        return "%" + (names[x] if x in names and names[x] else str(x))

    for op, a in insts:
        d = ops.get(op)
        if not d:
            print("op%d %s" % (op, a))
            continue
        on = d["opname"]
        if names_only and on not in ("OpName", "OpMemberName", "OpDecorate", "OpMemberDecorate", "OpEntryPoint",
                                     "OpSource", "OpString"):
            continue
        out, res, k = [], None, 0
        for od in d.get("operands", []):
            kind, q = od["kind"], od.get("quantifier", "")
            reps = 1 if q == "" else (1 if q == "?" else 10 ** 6)
            for _ in range(reps):
                if k >= len(a):
                    break
                if kind == "IdResult":
                    res = a[k]
                    k += 1
                elif kind in ("IdRef", "IdResultType", "IdMemorySemantics", "IdScope"):
                    out.append(nm(a[k]))
                    k += 1
                elif kind == "LiteralString":
                    s, k = lit_string(a, k)
                    out.append('"%s"' % s)
                elif kind == "LiteralExtInstInteger":
                    out.append(glsl.get(a[k], str(a[k])))
                    k += 1
                elif kind == "LiteralContextDependentNumber":
                    rest = a[k:]
                    if len(rest) == 1:
                        f = struct.unpack("<f", struct.pack("<I", rest[0]))[0]
                        out.append("%d(%.6g)" % (rest[0], f))
                    else:
                        out.append(" ".join(str(r) for r in rest))
                    k = len(a)
                elif kind in kinds and kinds[kind]["category"] in ("ValueEnum", "BitEnum"):
                    v = a[k]
                    k += 1
                    en = kinds[kind].get("enumerants", [])
                    if kinds[kind]["category"] == "ValueEnum":
                        out.append(next((e["enumerant"] for e in en if e["value"] == v), str(v)))
                    else:
                        val = lambda e: int(e["value"], 16) if isinstance(e["value"], str) else e["value"]
                        out.append("|".join(e["enumerant"] for e in en if val(e) & v) or "None")
                elif kind == "PairIdRefLiteralInteger" or kind == "PairLiteralIntegerIdRef" or kind == "PairIdRefIdRef":
                    out.append("%s %s" % (a[k], a[k + 1] if k + 1 < len(a) else ""))
                    k += 2
                else:
                    out.append(str(a[k]))
                    k += 1
            if q == "*" and k >= len(a):
                pass
        if on == "OpMemberName":
            out = [nm(a[0]), str(a[1]), '"%s"' % members[(a[0], a[1])]]
        line = ("%s = " % nm(res) if res is not None else "") + on + " " + " ".join(out)
        print(line)


main()
