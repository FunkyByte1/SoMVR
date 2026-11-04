#!/usr/bin/env python3
"""Tests for tools/anix.py and tools/finishers_check.py on a synthetic ANIX asset (no game files).

Run: python3 -I tools/tests/test_anix.py [-v]

The asset: the 0x20-byte header, filler for the tracks and records, the string pool (the path, 'bind', two
animation names, 'Null', three key strings) and the two key tables in the records' order - the layout
docs/re-xr-pipeline.md 149.1 describes and the game's sets have (pl_syncaction_chordkill_stand.anix: 23 tables).
"""
import importlib.util
import os
import struct
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))


def _load(name):
    spec = importlib.util.spec_from_file_location(name, os.path.join(HERE, '..', name + '.py'))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


anix = _load('anix')
fcheck = _load('finishers_check')


def table(name_off, frames, keys):
    """a key table: box, the name's offset, 16 zero bytes, frames, K, the frames, the packed (ms | string << 24)"""
    b = struct.pack('<3fI', 40.0, 104.0, 40.0, name_off) + bytes(16) + struct.pack('<2I', frames, len(keys))
    b += struct.pack('<%dI' % len(keys), *[f for f, ms, s in keys])
    b += struct.pack('<%dI' % len(keys), *[(ms & 0xffffff) | ((0xff if s is None else s) << 24) for f, ms, s in keys])
    return b


def asset():
    path = b'animation\\player\\elfmisc\\pl_test.anix'
    strs = [path, b'bind', b'PL_Bip_Sync_Test_A', b'PL_Bip_Sync_Test_B', b'Null',
            b'BCAM;', b'APPLYDAMAGE; SEVER; MeleeFX BloodSA_01_WeaponMid_Trail;', b'AEE;']
    pool = b'\0'.join(strs) + b'\0'
    off = lambda i: sum(len(s) + 1 for s in strs[:i])
    body = bytes(0x40) + b'\x01\x02\x03' + bytes(5)            # the tracks' and records' stand-in
    tabs = table(off(1), 2, [(0, 0, None), (1, 33, None)])     # bind
    tabs += table(off(2), 61, [(0, 0, None), (3, 100, 0), (28, 933, 1), (60, 2000, 2)])
    tabs += table(off(3), 31, [(0, 0, None), (30, 1000, 2)])
    data = body + pool + bytes((-len(pool)) % 4) + tabs
    size = 0x20 + len(data)
    hdr = b'ANIX' + struct.pack('<7I', 42, size, 0x20, 0x20, 0x20, len(data), 0)
    return hdr + data


class TestAnix(unittest.TestCase):
    def test_parse(self):
        a = anix.parse(asset())
        self.assertEqual([x[0] for x in a], ['bind', 'PL_Bip_Sync_Test_A', 'PL_Bip_Sync_Test_B'])
        name, frames, keys, box = a[1]
        self.assertEqual(frames, 61)
        self.assertEqual(keys[2], (28, 933, 'APPLYDAMAGE; SEVER; MeleeFX BloodSA_01_WeaponMid_Trail;'))
        self.assertEqual(keys[0], (0, 0, ''))           # 0xff = no string
        self.assertEqual(a[2][2][1], (30, 1000, 'AEE;'))

    def test_find_in_bundle(self):
        blob = b'BNDL' + bytes(60) + asset() + bytes(16)
        found = anix.find_anix(blob)
        self.assertEqual(len(found), 1)
        self.assertEqual(found[0][0], 64)
        self.assertEqual(found[0][2], 'animation\\player\\elfmisc\\pl_test.anix')

    def test_not_anix(self):
        with self.assertRaises(ValueError):
            anix.parse(b'TEXR' + bytes(60))


FIN = """
[t1]
source = PC_A
target = AI_A
sync = PC_A
talion = PL_Bip_Sync_Test_A 2000 pl_test.anix
orc = PL_Bip_Sync_Test_B 1000 pl_test.anix
ready = fighting front execution
orc_root = 10 cm, turn 0 deg
orc_keys = ragdoll 500 600
slowmo =
kill = 933
sever = 933
motion = kind=cut node=Neck at=933 rec=700-1100 hit=sever,kill blood=BloodSA_01_WeaponMid_Trail path=
"""


class TestFinishersCheck(unittest.TestCase):
    def run_check(self, text):
        d = tempfile.mkdtemp()
        p = os.path.join(d, 'f.txt')
        open(p, 'w').write(text)
        open(os.path.join(d, 'pl_test.anix'), 'wb').write(asset())
        att = os.path.join(d, 'att.tsv')
        open(att, 'w').write('index\tname\tanimation\tsync\n1\tPC_A\t\tPC_A\n2\tAI_A\t\tPC_A\n')
        import contextlib, io
        with contextlib.redirect_stdout(io.StringIO()):
            return fcheck.check(fcheck.load(p), d, att)

    def test_good(self):
        self.assertEqual(self.run_check(FIN), [])

    def test_faults(self):
        bad = FIN.replace('kill = 933', 'kill = 900').replace('at=933', 'at=950').replace('2000 pl_test', '1900 pl_test')
        f = self.run_check(bad)
        self.assertTrue(any('kill = 900' in x for x in f), f)
        self.assertTrue(any('no key at 950' in x for x in f), f)
        self.assertTrue(any('is 2000 ms, not 1900' in x for x in f), f)

    def test_last_must_kill(self):
        f = self.run_check(FIN.replace('hit=sever,kill', 'hit=-'))
        self.assertTrue(any('does not kill' in x for x in f), f)


if __name__ == '__main__':
    unittest.main()
