#!/usr/bin/env python3
"""ltar.py - read Shadow of Mordor's LTAR v3 archives (share/data/*.arch05) read-only (docs/re-xr-pipeline.md 45.13).

  ltar.py list ARCHIVE...                 every entry: archive name offset packed size kind
  ltar.py get ARCHIVE NAME OUT            one entry unpacked into OUT (never into the install)

Format: a 0x30-byte header ('LTAR', 3, the name table's size, folders, files, 1, 0, 0, 16 zero bytes), the name table
(4-aligned names, offsets from 0x30), 32-byte file entries (name offset u32, data offset u64, packed u64, size u64,
kind u32 = 9), 16-byte folder entries. An entry's data = chunks [packed u32][size u32][zlib or raw when packed ==
size], each 4-aligned. Files are bundles: *.bndlxml05 (an XML asset list) and *.embb ('BNDL' 4, the assets; the scripts
are in the *.objres assets: Havok Script / Lua 5.1 chunks '\\x1bLuaQ')."""
import os, struct, sys, zlib


def read_ltar(path):
    with open(path, 'rb') as f:
        magic, ver, nts, nfold, nfile = struct.unpack('<4s4I', f.read(0x30)[:20])
        if magic != b'LTAR' or ver != 3:
            raise ValueError('%s: not an LTAR v3 archive' % path)
        names = f.read(nts)
        nm = lambda o: names[o:names.index(b'\0', o)].decode('latin1')
        files = []
        for _ in range(nfile):
            no, off, cs, ds, kind = struct.unpack('<IQQQI', f.read(32))
            files.append((nm(no), off, cs, ds, kind))
    return files


def extract(path, entry):
    name, off, cs, ds, kind = entry
    out = bytearray()
    with open(path, 'rb') as f:
        f.seek(off)
        while len(out) < ds:
            c, d = struct.unpack('<II', f.read(8))
            blob = f.read(c)
            out += blob if c == d else zlib.decompress(blob)
            if c % 4:
                f.read(4 - c % 4)
    return bytes(out)


if __name__ == '__main__':
    if len(sys.argv) >= 3 and sys.argv[1] == 'list':
        for p in sys.argv[2:]:
            for e in read_ltar(p):
                print(os.path.basename(p), *e)
    elif len(sys.argv) == 5 and sys.argv[1] == 'get':
        es = [e for e in read_ltar(sys.argv[2]) if e[0] == sys.argv[3]]
        if not es:
            sys.exit('no entry %s' % sys.argv[3])
        with open(sys.argv[4], 'wb') as o:
            o.write(extract(sys.argv[2], es[0]))
    else:
        sys.exit(__doc__)
