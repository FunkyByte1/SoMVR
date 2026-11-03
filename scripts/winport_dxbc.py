"""winport_dxbc.py - research tool (docs/re-winport.md 7.5): the game's DXBC shaders, read-only on everything it is given.

  python3 -I winport_dxbc.py scan ARCHIVE.arch05 OUTDIR   the pixel shaders with the fog's marks (l(6.371e8) / l(1e7)) out
                                                          of an LTAR archive's zlib streams (global.arch05: the opaque fog
                                                          overlay shaders, opaque 8168 + OIT 9048 bytes; ~17 s, nice it)
  python3 -I winport_dxbc.py dis FILE [FROM [N]]          a blob's SM4/5 program, one instruction per line
  python3 -I winport_dxbc.py sums FILE                    every DXBC blob in FILE (an exe): its checksum recomputed
The checksum is DXBC's MD5 variant (somvr_fogdxbc.h's C twin); both verify all 927 blobs the exe embeds."""
import struct, sys, re, os, hashlib

def _md5_transform(state, block):
    import math
    K = [int(abs(math.sin(i + 1)) * 2**32) & 0xffffffff for i in range(64)]
    S = [7, 12, 17, 22] * 4 + [5, 9, 14, 20] * 4 + [4, 11, 16, 23] * 4 + [6, 10, 15, 21] * 4
    M = struct.unpack('<16I', block)
    a, b, c, d = state
    for i in range(64):
        if i < 16: f = (b & c) | (~b & d); g = i
        elif i < 32: f = (d & b) | (~d & c); g = (5 * i + 1) % 16
        elif i < 48: f = b ^ c ^ d; g = (3 * i + 5) % 16
        else: f = c ^ (b | ~d); g = (7 * i) % 16
        f = (f + a + K[i] + M[g]) & 0xffffffff
        a, d, c = d, c, b
        b = (b + ((f << S[i]) | (f >> (32 - S[i])))) & 0xffffffff
    return [(x + y) & 0xffffffff for x, y in zip(state, (a, b, c, d))]

def checksum(blob):
    """the DXBC header checksum of blob (whole container; bytes 20.. are hashed)"""
    data = blob[20:]
    n = len(data); bits = (n * 8) & 0xffffffff
    st = [0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476]
    full = n - n % 64
    for i in range(0, full, 64):
        st = _md5_transform(st, data[i:i + 64])
    last = data[full:]
    pad = b'\x80' + b'\0' * 63
    if len(last) >= 56:
        st = _md5_transform(st, (last + pad)[:64])
        blk = struct.pack('<I', bits) + b'\0' * 56 + struct.pack('<I', (bits >> 2) | 1)
        st = _md5_transform(st, blk)
    else:
        blk = struct.pack('<I', bits) + last + pad[:60 - 4 - len(last)] + struct.pack('<I', (bits >> 2) | 1)
        st = _md5_transform(st, blk)
    return struct.pack('<4I', *st)

def blobs(b):
    for m in re.finditer(b'DXBC', b):
        o = m.start()
        if o + 32 > len(b): continue
        ver, size, nch = struct.unpack_from('<III', b, o + 20)
        if ver != 1 or not (32 < size < 4 << 20) or not (0 < nch < 32) or o + size > len(b): continue
        offs = struct.unpack_from('<%dI' % nch, b, o + 32)
        if any(x >= size for x in offs): continue
        yield o, b[o:o + size]

def chunks(blob):
    nch = struct.unpack_from('<I', blob, 28)[0]
    offs = struct.unpack_from('<%dI' % nch, blob, 32)
    out = []
    for x in offs:
        tag = blob[x:x + 4].decode('latin1'); ln = struct.unpack_from('<I', blob, x + 4)[0]
        out.append((tag, x, ln))
    return out

def program(blob):
    for tag, x, ln in chunks(blob):
        if tag in ('SHEX', 'SHDR'):
            ver, nd = struct.unpack_from('<II', blob, x + 8)
            return tag, x, ver, nd
    return None


OPN = ('add and break breakc call callc case continue continuec cut default deriv_rtx deriv_rty discard div dp2 dp3 dp4 '
       'else emit emitthencut endif endloop endswitch eq exp frc ftoi ftou ge iadd if ieq ige ilt imad imax imin imul ine '
       'ineg ishl ishr itof label ld ld_ms log loop lt mad min max customdata mov movc mul ne nop not or resinfo ret retc '
       'round_ne round_ni round_pi round_z rsq sample sample_c sample_c_lz sample_l sample_d sample_b sqrt switch sincos '
       'udiv ult uge umul umad umax umin ushr utof xor dcl_resource dcl_constantbuffer dcl_sampler dcl_index_range '
       'dcl_gs_out_topology dcl_gs_in_prim dcl_max_out_vtx dcl_input dcl_input_sgv dcl_input_siv dcl_input_ps '
       'dcl_input_ps_sgv dcl_input_ps_siv dcl_output dcl_output_sgv dcl_output_siv dcl_temps dcl_indexable_temp '
       'dcl_global_flags').split()
REG = {0: 'r', 1: 'v', 2: 'o', 3: 'x', 4: 'l', 5: 'd', 6: 's', 7: 't', 8: 'cb', 9: 'icb'}

def instrs(blob):
    """-> [(word_offset_in_blob, opcode, length, [words])] of the SHEX/SHDR program"""
    tag, x, ver, nd = program(blob)
    w = struct.unpack_from('<%dI' % nd, blob, x + 8)
    out, i = [], 2
    while i < nd:
        op = w[i] & 0x7ff
        ln = (w[i] >> 24) & 0x7f
        if op == 53:
            ln = w[i + 1]
        if ln == 0: raise ValueError('zero length at %d' % i)
        out.append(((x + 8) // 4 + i, op, ln, list(w[i:i + ln])))
        i += ln
    return out

def operands(ws, op):
    """decode the operands after the opcode token (+ extended opcode tokens) -> [(text, start, end)]"""
    i = 1
    t = ws[0]
    while t & 0x80000000 and i < len(ws):
        t = ws[i]; i += 1
    res = []
    while i < len(ws):
        s = i; tok = ws[i]; i += 1
        ext = tok & 0x80000000
        mod = ''
        while ext:
            e = ws[i]; i += 1; ext = e & 0x80000000
            if e & 0x3f == 1: mod = {1: '-', 2: '|', 3: '-|'}.get((e >> 6) & 0xff, '')
        nc = tok & 3; sel = (tok >> 2) & 3; ty = (tok >> 12) & 0xff; dim = (tok >> 20) & 3
        if ty in (4, 5):
            n = {0: 0, 1: 1, 2: 4}.get(nc, 0)
            vals = ws[i:i + n]; i += n
            txt = 'l(%s)' % ','.join('%g' % struct.unpack('<f', struct.pack('<I', v))[0] for v in vals)
            res.append((txt, s, i)); continue
        idx = []
        for k in range(dim):
            rep = (tok >> (22 + 3 * k)) & 7
            if rep == 0: idx.append(str(ws[i])); i += 1
            elif rep == 1: idx.append(str(ws[i] | ws[i + 1] << 32)); i += 2
            else:
                idx.append('?rel%d' % rep); i += 1 + (rep == 3)
                # relative: a nested operand follows
                sub = operands([0] + ws[i:], op)
                if sub: i += sub[0][2] - 1
        comp = ''
        if nc == 2:
            m = (tok >> 4) & 0xff
            if sel == 0: comp = '.' + ''.join('xyzw'[c] for c in range(4) if m & (1 << c))
            elif sel == 1: comp = '.' + ''.join('xyzw'[(m >> (2 * c)) & 3] for c in range(4))
            else: comp = '.' + 'xyzw'[m & 3]
        name = REG.get(ty, 'T%d' % ty)
        if ty == 8 and len(idx) == 2: txt = 'cb%s[%s]' % (idx[0], idx[1])
        else: txt = name + (idx[0] if idx else '') + ''.join('[%s]' % x for x in idx[1:])
        res.append((mod + txt + comp + (mod[-1] if mod.endswith('|') else ''), s, i))
    return res

def dis(blob, n=None, start=0):
    out = []
    for k, (wo, op, ln, ws) in enumerate(instrs(blob)):
        if k < start: continue
        if n is not None and len(out) >= n: break
        nm = OPN[op] if op < len(OPN) else 'op%d' % op
        if op >= 88 or op == 53:
            out.append('%5d %s %s' % (k, nm, ' '.join('%x' % x for x in ws[1:6])))
        else:
            sat = '_sat' if ws[0] & (1 << 13) else ''
            out.append('%5d %s%s %s' % (k, nm, sat, ', '.join(t for t, a, b in operands(ws, op))))
    return out


def scan(arch, out):
    """the archives store files as consecutive zlib streams of 64 KB each: a blob can cross two of them, so the outputs
    are scanned in a rolling window of the last four; after a good stream the search goes on where it ended"""
    import zlib, mmap
    os.makedirs(out, exist_ok=True)
    f = open(arch, 'rb'); m = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
    F1, F2 = struct.pack('<f', 6.371e8), struct.pack('<f', 1e7)
    hdr = re.compile(rb'\x78[\x01\x5e\x9c\xda]')
    n_s = n_b = 0; seen = set(); win = []; o = 0; size = len(m)
    while True:
        hm = hdr.search(m, o)
        if not hm:
            break
        o = hm.start()
        d = zlib.decompressobj()
        try:
            data = d.decompress(m[o:o + (1 << 20)], 16 << 20)
        except zlib.error:
            o += 1
            continue
        if not d.eof or len(data) < 64:
            o += 1
            continue
        used = min(1 << 20, size - o) - len(d.unused_data)
        n_s += 1
        win = (win + [data])[-4:]
        if b'DXBC' in data or (len(win) > 1 and b'DXBC' in win[-2][-4:] + data[:4]):
            buf = b''.join(win)
            for bo, bl in blobs(buf):
                k = bl[4:20]
                if k in seen:
                    continue
                seen.add(k); n_b += 1
                p = program(bl)
                if p and p[2] >> 16 == 0 and (F1 in bl or F2 in bl):
                    open(os.path.join(out, '%s.%x.%d.dxbc' % (k.hex()[:16], o, len(bl))), 'wb').write(bl)
                    print('stream %#x: pixel shader %d bytes %s  6.371e8 %s  1e7 %s' % (o, len(bl), k.hex(), F1 in bl, F2 in bl), flush=True)
        o += max(used, 1)
    print('zlib streams %d, unique DXBC blobs %d' % (n_s, n_b))

if __name__ == '__main__':
    a = sys.argv[1:]
    if a[0] == 'scan':
        scan(a[1], a[2])
    elif a[0] == 'dis':
        b = open(a[1], 'rb').read()
        print('\n'.join(dis(b, int(a[3]) if len(a) > 3 else None, int(a[2]) if len(a) > 2 else 0)))
    elif a[0] == 'sums':
        b = open(a[1], 'rb').read(); n = ok = 0
        for o, bl in blobs(b):
            n += 1; ok += checksum(bl) == bl[4:20]
        print('blobs %d, checksum right %d' % (n, ok))
