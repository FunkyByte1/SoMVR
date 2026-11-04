#!/usr/bin/env python3
"""anix.py - read Shadow of Mordor's animation sets (*.anix assets in the *.embb bundles) read-only: each animation's
length and its key strings with their times - the same keys the game's IEngineModel KeyLookup / GetAnimData read at
run time (docs/re-combat-vr.md 3.3; docs/re-xr-pipeline.md 149).

  anix.py list BUNDLE.embb                  every ANIX asset in an unpacked bundle: offset size name
  anix.py get BUNDLE.embb NAME OUT          one asset (by its path's last part, e.g. pl_syncaction_chordkill_stand.anix)
  anix.py keys FILE.anix [SUBSTR...]        each animation: frames, length, the keys (frame, ms, string)
  anix.py tsv FILE.anix... > OUT.tsv        every key of every animation: set anim frames frame ms key

Bundles come out of the archives with tools/ltar.py (e.g. `ltar.py get .../share/data/corecharacter.arch05
charactercommon.embb /tmp/x/cc.embb`; never into the install).

The format (ANIX version 42, decoded 2026-11-03 for this tool, the parts the tool needs): a 0x20-byte header ('ANIX',
42, the asset's size, 0x20, two section offsets, the tail's size), the compressed tracks, then the records (one per
animation: a 0x30-byte name, the track formats SQ03/AQ03/STRI/UATR/SV01, the track-to-bone map, the key strings
inline), then the string pool: the asset's own path, 'bind', the animation names, 'Null', the key strings (a key's
string index counts from the one after 'Null'), and last one key table per animation in the records' order: 12 bytes
(3 floats: a box), u32 the name's offset in the pool, 16 zero bytes, u32 frames (30 fps), u32 K, u32[K] key frames,
u32[K] packed (ms & 0xffffff, string index >> 24, 0xff = none) - the in-memory AnimData +0x10/+0x14/+0x20/+0x30."""
import struct, sys


def find_anix(d):
    """(offset, size, name) of every ANIX asset in a bundle's bytes"""
    out, o = [], 0
    tag = b'ANIX\x2a\x00\x00\x00'
    while True:
        o = d.find(tag, o)
        if o < 0:
            return out
        size = struct.unpack_from('<I', d, o + 8)[0]
        a = d[o:o + size]
        name = '?'
        for pre in (b'animation\\', b'models\\'):
            i = a.find(pre)
            while i >= 0:
                j = a.find(b'.anix\x00', i)
                if j > 0 and j - i < 200 and all(32 < c < 127 for c in a[i:j]):
                    name = a[i:j + 5].decode()
                    break
                i = a.find(pre, i + 1)
            if name != '?':
                break
        out.append((o, size, name))
        o += 8


def parse(a):
    """[(name, frames, [(frame, ms, key string)], box)] of an ANIX asset"""
    if a[:4] != b'ANIX':
        raise ValueError('not an ANIX asset')
    base = -1
    for pre in (b'animation\\', b'models\\'):
        i = a.find(pre)
        while i >= 0:
            j = a.find(b'.anix\x00', i)
            if j > 0 and j - i < 200 and a[i - 1] == 0:
                base = i
                break
            i = a.find(pre, i + 1)
        if base >= 0:
            break
    if base < 0:
        raise ValueError('no string pool')
    parts = a[base:].split(b'\0')
    try:
        null = parts.index(b'Null')
    except ValueError:
        null = -1
    keys = [p.decode('latin1') for p in parts[null + 1:]] if null >= 0 else []

    def s_at(off):
        e = a.index(b'\0', base + off)
        return a[base + off:e].decode('latin1')

    # the first key table: its name offset is the pool's second string ('bind' or the first animation)
    first = struct.pack('<I', len(parts[0]) + 1)
    o, i = None, a.find(first, base)
    while i >= 0:
        cand = i - 12
        if cand > base and a[cand + 16:cand + 32] == bytes(16):
            F, K = struct.unpack_from('<2I', a, cand + 0x20)
            if 1 <= F <= 20000 and 1 <= K <= 255:
                o = cand
                break
        i = a.find(first, i + 1)
    anims = []
    while o is not None and o + 0x28 <= len(a):
        noff = struct.unpack_from('<I', a, o + 12)[0]
        F, K = struct.unpack_from('<2I', a, o + 0x20)
        if K > 255 or F > 20000 or o + 0x28 + 8 * K > len(a):
            break
        fr = struct.unpack_from('<%dI' % K, a, o + 0x28)
        pk = struct.unpack_from('<%dI' % K, a, o + 0x28 + 4 * K)
        ks = []
        for f, p in zip(fr, pk):
            si = p >> 24
            ks.append((f, p & 0xffffff, keys[si] if si != 0xff and si < len(keys) else ''))
        anims.append((s_at(noff), F, ks, struct.unpack_from('<3f', a, o)))
        o += 0x28 + 8 * K
    return anims


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    cmd = argv[1]
    if cmd == 'list':
        d = open(argv[2], 'rb').read()
        for o, size, name in find_anix(d):
            print('%#x\t%d\t%s' % (o, size, name))
    elif cmd == 'get':
        d = open(argv[2], 'rb').read()
        for o, size, name in find_anix(d):
            if name.split('\\')[-1].lower() == argv[3].lower() or name.lower() == argv[3].lower():
                open(argv[4], 'wb').write(d[o:o + size])
                print('%s: %d bytes -> %s' % (name, size, argv[4]))
                return 0
        print('no such asset: ' + argv[3])
        return 1
    elif cmd == 'keys':
        want = argv[3:]
        for name, F, ks, box in parse(open(argv[2], 'rb').read()):
            if want and not any(w.lower() in name.lower() for w in want):
                continue
            print('== %s  frames %d (%.2f s)' % (name, F, (F - 1) / 30.0))
            for f, ms, s in ks:
                if s:
                    print('   f%-4d %5d ms  %s' % (f, ms, s))
    elif cmd == 'tsv':
        print('set\tanim\tframes\tframe\tms\tkey')
        for path in argv[2:]:
            st = path.replace('\\', '/').split('/')[-1]
            for name, F, ks, box in parse(open(path, 'rb').read()):
                for f, ms, s in ks:
                    if s:
                        print('%s\t%s\t%d\t%d\t%d\t%s' % (st, name, F, f, ms, s))
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
