"""check_doc_sites.py BINARY FILE... - check every SITE(...) line in docs or sources against the game binary (read-only).

The SITE format is mod/spike1/check_sites.py's:
  SITE("name", K_CALL|K_VSLOT|K_ENTRY, addr, ctxaddr, "context bytes", target, "target prologue")
For each line: the context bytes at ctxaddr, the call target (K_CALL: an E8 rel32 at addr), slot value (K_VSLOT:
the 8-byte value at addr) or function entry (K_ENTRY: a hook on the function's first bytes, addr == target), and the
first bytes of the target. Also the GNU build-id. Exit 0 = every site matches.
"""
import re
import struct
import sys

BUILDID = "87ec5d6fd94afd47a24d5da7c9b0c2b74b30c6e9"
SITE_RE = re.compile(r'SITE\("(\w+)", (K_CALL|K_VSLOT|K_ENTRY), (0x[0-9a-fA-F]+), (0x[0-9a-fA-F]+), "([0-9a-fA-F ]+)", '
                     r'(0x[0-9a-fA-F]+), "([0-9a-fA-F ]+)"\)')


def loads(data):
    if data[:4] != b"\x7fELF" or data[4] != 2:
        sys.exit("not an ELF64 file")
    phoff, = struct.unpack_from("<Q", data, 0x20)
    phentsize, phnum = struct.unpack_from("<HH", data, 0x36)
    return [struct.unpack_from("<IIQQQQQQ", data, phoff + i * phentsize) for i in range(phnum)]


def read(data, segs, addr, n):
    for p_type, _f, off, vaddr, _pa, filesz, _m, _al in segs:
        if p_type == 1 and vaddr <= addr and addr + n <= vaddr + filesz:
            return data[off + addr - vaddr: off + addr - vaddr + n]
    return None


def build_id(data, segs):
    for p_type, _f, off, _v, _pa, filesz, _m, _al in segs:
        if p_type != 4:
            continue
        p, end = off, off + filesz
        while p + 12 <= end:
            namesz, descsz, ntype = struct.unpack_from("<III", data, p)
            desc_at = p + 12 + ((namesz + 3) & ~3)
            if ntype == 3 and data[p + 12: p + 12 + namesz] == b"GNU\0":
                return data[desc_at: desc_at + descsz].hex()
            p = desc_at + ((descsz + 3) & ~3)
    return None


def check(data, segs, kind, addr, ctx, exp, target, pro):
    problems = []
    live = read(data, segs, ctx, len(exp))
    if live != exp:
        problems.append("context bytes differ: %s" % (live.hex(" ") if live else "unmapped"))
    site = read(data, segs, addr, 5 if kind == "K_CALL" else 8)
    if kind == "K_ENTRY":
        if addr != target:
            problems.append("entry hook at %#x but target %#x" % (addr, target))
    elif site is None:
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
    return problems


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    data = open(sys.argv[1], "rb").read()
    segs = loads(data)
    bid = build_id(data, segs)
    bad = int(bid != BUILDID)
    print("binary %s build-id %s %s" % (sys.argv[1], bid, "ok" if bid == BUILDID else "MISMATCH"))
    for path in sys.argv[2:]:
        try:
            sites = SITE_RE.findall(open(path, encoding="utf-8").read())
        except FileNotFoundError:
            print("%s: missing" % path)
            bad += 1
            continue
        print("%s: %d SITE lines" % (path, len(sites)))
        for name, kind, addr, ctx, ctx_hex, target, pro_hex in sites:
            addr, ctx, target = int(addr, 16), int(ctx, 16), int(target, 16)
            problems = check(data, segs, kind, addr, ctx, bytes.fromhex(ctx_hex), target, bytes.fromhex(pro_hex))
            print("  %s %s %#x -> %#x: %s" % (name, kind, addr, target, "; ".join(problems) if problems else "ok"))
            bad += bool(problems)
    sys.exit(1 if bad else 0)


main()
