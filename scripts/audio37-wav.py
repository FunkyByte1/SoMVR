"""audio37-wav.py - som-b37-audio (docs/re-xr-pipeline.md 173): a Wwise output capture (`sndcap`) read offline.

  python3 -I audio37-wav.py RUN/game.wav [RUN/vr.wav ...] [--win 0.25]

For each file: the format, then per window (default 0.25 s) the left and right level (dBFS RMS of the front pair; a 5.1
or 7.1 capture is folded to stereo the way headphones get it: L + 0.707 C + 0.707 Ls, R + 0.707 C + 0.707 Rs) and the
level difference L-R (dB, + = louder left). The summary: the overall level, the spread of L-R (its standard deviation
and its 5..95 % range) and the largest step of L-R between neighbouring windows - a sound thrown from one ear to the
other as the head turns shows as a wide spread and big steps; the same rain held steady, a narrow one. Reads only."""
import struct, sys, math

def read_wav(p):
    b = open(p, 'rb').read()
    if b[:4] != b'RIFF' or b[8:12] != b'WAVE':
        raise SystemExit('%s: not a WAV' % p)
    o, fmt, data = 12, None, None
    while o + 8 <= len(b):
        cid, ln = b[o:o + 4], struct.unpack_from('<I', b, o + 4)[0]
        body = b[o + 8:o + 8 + ln]
        if cid == b'fmt ':
            fmt = struct.unpack_from('<HHIIHH', body, 0)
            if fmt[0] == 0xfffe and len(body) >= 40:   # WAVE_FORMAT_EXTENSIBLE: the sub-format's first word
                fmt = (struct.unpack_from('<H', body, 24)[0],) + fmt[1:]
        elif cid == b'data':
            data = body if ln else b[o + 8:]   # a capture not closed: the size may be 0 - take the rest
        o += 8 + ln + (ln & 1)
        if ln == 0:
            break
    if not fmt or data is None:
        raise SystemExit('%s: no fmt/data' % p)
    tag, ch, rate, _, align, bits = fmt
    n = len(data) // align
    if tag == 3 and bits == 32:
        s = struct.unpack('<%df' % (n * ch), data[:n * align])
    elif tag == 1 and bits == 16:
        s = [v / 32768.0 for v in struct.unpack('<%dh' % (n * ch), data[:n * align])]
    elif tag == 1 and bits == 24:
        s = []
        for i in range(n * ch):
            v = int.from_bytes(data[3 * i:3 * i + 3], 'little', signed=True)
            s.append(v / 8388608.0)
    else:
        raise SystemExit('%s: format %d, %d bits not read' % (p, tag, bits))
    return ch, rate, s

def stereo(ch, s):
    n = len(s) // ch
    if ch == 1:
        return s, s
    if ch == 2:
        return s[0::2], s[1::2]
    k = 0.7071
    L = [s[i * ch] + k * s[i * ch + 2] + k * s[i * ch + 4] for i in range(n)] if ch >= 6 else s[0::ch]
    R = [s[i * ch + 1] + k * s[i * ch + 2] + k * s[i * ch + 5] for i in range(n)] if ch >= 6 else s[1::ch]
    return L, R

def db(x):
    return 20 * math.log10(x) if x > 1e-9 else -180.0

def main():
    a = sys.argv[1:]
    win = 0.25
    if '--win' in a:
        i = a.index('--win'); win = float(a[i + 1]); del a[i:i + 2]
    for p in a:
        ch, rate, s = read_wav(p)
        L, R = stereo(ch, s)
        w = max(1, int(rate * win))
        rows = []
        for i in range(0, len(L) - w + 1, w):
            l = math.sqrt(sum(v * v for v in L[i:i + w]) / w)
            r = math.sqrt(sum(v * v for v in R[i:i + w]) / w)
            rows.append((i / rate, db(l), db(r)))
        print('== %s: %d channels, %d Hz, %.1f s, %d windows of %.2f s' % (p, ch, rate, len(L) / rate, len(rows), win))
        live = [(t, l, r) for t, l, r in rows if max(l, r) > -90]
        if not live:
            print('   silent')
            continue
        d = [l - r for _, l, r in live]
        m = sum(d) / len(d)
        sd = math.sqrt(sum((x - m) ** 2 for x in d) / len(d))
        q = sorted(d)
        steps = [abs(d[i + 1] - d[i]) for i in range(len(d) - 1)] or [0]
        lv = [max(l, r) for _, l, r in live]
        print('   level %.1f dBFS (mean of the louder ear), L-R mean %+.1f dB, sd %.1f dB, 5..95 %% %+.1f .. %+.1f dB, '
              'largest step %.1f dB, windows with |L-R| > 6 dB: %d of %d' % (sum(lv) / len(lv), m, sd, q[len(q) // 20],
              q[len(q) * 19 // 20], max(steps), sum(1 for x in d if abs(x) > 6), len(d)))
        for t, l, r in rows:
            print('   %6.2f s  L %6.1f  R %6.1f  L-R %+5.1f' % (t, l, r, l - r))

if __name__ == '__main__':
    main()
