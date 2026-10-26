"""check_sites.py SOURCE.c BINARY - check somvr_spike1's patch sites against the game binary on disk (read-only).

Reads the SITE(...) lines of every SITES-BEGIN ... SITES-END block in SOURCE.c and, for each, compares the expected bytes
with the binary's file contents (addresses mapped through the PT_LOAD program headers): the context bytes, the call
target (K_CALL) or slot value (K_VSLOT), and the first bytes of the target. Also checks the GNU build-id.
Exit 0 = all sites match.
"""
import re
import struct
import sys

BUILDID = "87ec5d6fd94afd47a24d5da7c9b0c2b74b30c6e9"


def loads(data):
    if data[:4] != b"\x7fELF" or data[4] != 2:
        sys.exit("not an ELF64 file")
    phoff, = struct.unpack_from("<Q", data, 0x20)
    phentsize, phnum = struct.unpack_from("<HH", data, 0x36)
    segs = []
    for i in range(phnum):
        p_type, _flags, off, vaddr, _paddr, filesz, _memsz, _align = struct.unpack_from(
            "<IIQQQQQQ", data, phoff + i * phentsize)
        segs.append((p_type, off, vaddr, filesz))
    return segs


def read(data, segs, addr, n):
    for p_type, off, vaddr, filesz in segs:
        if p_type == 1 and vaddr <= addr and addr + n <= vaddr + filesz:
            return data[off + addr - vaddr: off + addr - vaddr + n]
    return None


def build_id(data, segs):
    for p_type, off, _vaddr, filesz in segs:
        if p_type != 4:
            continue
        p, end = off, off + filesz
        while p + 12 <= end:
            namesz, descsz, ntype = struct.unpack_from("<III", data, p)
            name = data[p + 12: p + 12 + namesz]
            desc_at = p + 12 + ((namesz + 3) & ~3)
            if ntype == 3 and name == b"GNU\0":
                return data[desc_at: desc_at + descsz].hex()
            p = desc_at + ((descsz + 3) & ~3)
    return None


def main():
    src, binpath = sys.argv[1], sys.argv[2]
    text = open(src, encoding="utf-8").read()
    blocks = [b.split("SITES-END", 1)[0] for b in text.split("SITES-BEGIN")[1:]]
    sites = [m for b in blocks for m in re.findall(
        r'SITE\("(\w+)", (K_CALL|K_VSLOT), (0x[0-9a-f]+), (0x[0-9a-f]+), "([0-9a-f ]+)", '
        r'(0x[0-9a-f]+), "([0-9a-f ]+)"\)', b)]
    if len(sites) != 10:
        sys.exit("expected 10 SITE lines (4 camera hooks, 2 log hooks, 4 save-guard slots), found %d" % len(sites))
    data = open(binpath, "rb").read()
    segs = loads(data)
    bid = build_id(data, segs)
    bad = 0
    print("binary %s build-id %s %s" % (binpath, bid, "ok" if bid == BUILDID else "MISMATCH"))
    bad += bid != BUILDID
    for name, kind, addr, ctx, ctx_hex, target, pro_hex in sites:
        addr, ctx, target = int(addr, 16), int(ctx, 16), int(target, 16)
        exp, pro = bytes.fromhex(ctx_hex), bytes.fromhex(pro_hex)
        problems = []
        live = read(data, segs, ctx, len(exp))
        if live != exp:
            problems.append("context bytes differ: %s" % (live.hex(" ") if live else "unmapped"))
        site = read(data, segs, addr, 5 if kind == "K_CALL" else 8)
        if site is None:
            problems.append("site unmapped")
        elif kind == "K_CALL":
            rel, = struct.unpack("<i", site[1:5])
            if site[0] != 0xE8 or addr + 5 + rel != target:
                problems.append("not a call to %#x (op %02x, target %#x)" % (target, site[0], addr + 5 + rel))
        else:
            val, = struct.unpack("<Q", site)
            if val != target or addr & 7:
                problems.append("slot holds %#x, want %#x" % (val, target))
        tp = read(data, segs, target, len(pro))
        if tp != pro:
            problems.append("target prologue differs: %s" % (tp.hex(" ") if tp else "unmapped"))
        print("%s %s %#x -> %#x: %s" % (name, kind, addr, target, "; ".join(problems) if problems else "ok"))
        bad += bool(problems)
    sys.exit(1 if bad else 0)


main()
