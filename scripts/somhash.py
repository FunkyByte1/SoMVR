"""somhash.py NAME... - the Firebird node/socket/record name hash (CSimpleModel::vf_62 0x23530c0), docs/re-animation.md.

h = h*0x397 + T[c] (32-bit), where T is the 256-byte table at 0x38d7480 in the game binary; it is case-insensitive
because T maps upper and lower case to the same value. The table is read from the binary (SOM_BIN, default
reference/bin/linux/ShadowOfMordor), so the hash cannot drift from the game's. Example: R_Hand 0xc7485ad3.
"""
import os
import struct
import sys

TABLE = 0x38d7480
BIN = os.environ.get("SOM_BIN", os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                              "../reference/bin/linux/ShadowOfMordor"))


def table(path=BIN):
    data = open(path, "rb").read()
    phoff, = struct.unpack_from("<Q", data, 0x20)
    phentsize, phnum = struct.unpack_from("<HH", data, 0x36)
    for i in range(phnum):
        p_type, _f, off, vaddr, _pa, filesz, _m, _al = struct.unpack_from("<IIQQQQQQ", data, phoff + i * phentsize)
        if p_type == 1 and vaddr <= TABLE and TABLE + 256 <= vaddr + filesz:
            return data[off + TABLE - vaddr: off + TABLE - vaddr + 256]
    sys.exit("table 0x%x not mapped in %s" % (TABLE, path))


def somhash(name, t):
    h = 0
    for b in name.encode():
        h = (h * 0x397 + t[b]) & 0xffffffff
    return h


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    t = table()
    assert somhash("R_Hand", t) == 0xc7485ad3 and somhash("SocketHead", t) == 0x7b92d623, "table mismatch"
    for n in sys.argv[1:]:
        print("%-24s 0x%08x" % (n, somhash(n, t)))
