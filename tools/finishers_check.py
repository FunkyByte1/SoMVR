#!/usr/bin/env python3
"""finishers_check.py - check mod/somvr/assets/finishers.txt (docs/re-xr-pipeline.md 149): every block's fields, its
motions' tokens and windows; with --anix DIR (the .anix sets, out of the bundles by tools/anix.py get) the animations'
names and lengths and each motion's contact against the set's own keys; with --attacks TSV (attacks-full.tsv) the
records and the sync pairing.

  finishers_check.py [FILE] [--anix DIR] [--attacks TSV]      prints one line per finisher, exit 1 on any fault"""
import os, re, sys

FIELDS = ('source', 'target', 'sync', 'talion', 'orc', 'ready', 'orc_root', 'orc_keys', 'slowmo', 'kill', 'sever')
KINDS, NODES = ('stab', 'in', 'cut'), ('Pelvis', 'Torso', 'Neck', 'Head')
HITS = ('-', 'hit', 'kill', 'sever', 'sever,kill')


def load(path):
    """[(id, {field: value}, [motion dict])] in the file's order"""
    out, cur = [], None
    for n, line in enumerate(open(path), 1):
        line = line.strip()
        if not line or line.startswith('#'):
            continue
        m = re.match(r'^\[(\w+)\]$', line)
        if m:
            cur = (m.group(1), {}, [])
            out.append(cur)
            continue
        k, sep, v = line.partition('=')
        if not sep or cur is None:
            raise ValueError('%s:%d: not a field: %s' % (path, n, line))
        k, v = k.strip(), v.strip()
        if k == 'motion':
            cur[2].append(dict(t.split('=', 1) if '=' in t else (t, '') for t in v.split()))
        else:
            cur[1][k] = v
    return out


def check(fins, anix_dir=None, attacks=None):
    faults = []
    sets = {}
    recs = {}
    if attacks:
        for line in open(attacks):
            c = line.rstrip('\n').split('\t')
            if len(c) > 3:
                recs[c[1]] = c[3]
    if anix_dir:
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import anix
    for fid, f, ms in fins:
        bad = lambda s: faults.append('[%s] %s' % (fid, s))
        for k in FIELDS:
            if k not in f:
                bad('no ' + k)
        if not 1 <= len(ms) <= 3:
            bad('%d motions (1-3)' % len(ms))
        tal = f.get('talion', '').split()
        tlen = int(tal[1]) if len(tal) == 3 and tal[1].isdigit() else 0
        if not tlen:
            bad('talion = ANIM MS SET')
        last = -1
        for i, m in enumerate(ms, 1):
            if m.get('kind') not in KINDS or m.get('node') not in NODES or m.get('hit') not in HITS:
                bad('motion %d: kind/node/hit %s' % (i, m))
            at = int(m.get('at', '-1'))
            r = re.match(r'^(\d+)-(\d+)$', m.get('rec', ''))
            if not r or not int(r.group(1)) <= at <= int(r.group(2)) or int(r.group(2)) > tlen:
                bad('motion %d: at %d outside rec %s (or past %d ms)' % (i, at, m.get('rec'), tlen))
            if at <= last:
                bad('motion %d: at %d not after the last' % (i, at))
            last = at
            p = m.get('path', None)
            if p is None:
                bad('motion %d: no path token' % i)
            elif p == 'line':
                if m.get('kind') == 'cut' or 'start_cm' not in m or 'depth_cm' not in m:
                    bad('motion %d: a line needs a stab and start_cm/depth_cm' % i)
            elif p:
                pts = p.split(';')
                if len(pts) < 2 or any(len(q.split(',')) != 3 for q in pts):
                    bad('motion %d: path not x,y,z;...' % i)
        if ms and 'kill' not in ms[-1].get('hit', ''):
            bad('the last motion does not kill')
        if f.get('sever') and not any('sever' in m.get('hit', '') for m in ms):
            bad('sever keys but no sever motion')
        if recs:
            for k in ('source', 'target'):
                if f.get(k) not in recs:
                    bad('%s %s not in attacks' % (k, f.get(k)))
                elif recs[f[k]] != f.get('sync'):
                    bad('%s pairs %s, not %s' % (k, recs[f[k]], f.get('sync')))
        if anix_dir:
            for side in ('talion', 'orc'):
                v = f.get(side, '').split()
                if len(v) != 3:
                    continue
                if v[2] not in sets:
                    p = os.path.join(anix_dir, v[2])
                    sets[v[2]] = {a[0]: a for a in anix.parse(open(p, 'rb').read())} if os.path.exists(p) else {}
                a = sets[v[2]].get(v[0])
                if not a:
                    bad('%s anim %s not in %s' % (side, v[0], v[2]))
                    continue
                if (a[1] - 1) * 1000 // 30 != int(v[1]):
                    bad('%s anim %s is %d ms, not %s' % (side, v[0], (a[1] - 1) * 1000 // 30, v[1]))
                if side == 'talion':
                    keyms = {k[1] for k in a[2] if k[2]}
                    for i, m in enumerate(ms, 1):
                        if int(m.get('at', '-1')) not in keyms:
                            bad('motion %d: no key at %s ms' % (i, m.get('at')))
                    for fld, pat in (('kill', r'(^|;)\s*(APPLYDAMAGE|AD)\s*(;|$)'), ('sever', r'(^|;)\s*SEVER\s*(;|$)')):
                        got = ','.join(str(k[1]) for k in a[2] if re.search(pat, k[2]))
                        if got != f.get(fld, ''):
                            bad('%s = %s, the set says %s' % (fld, f.get(fld), got))
        print('%-9s %d motion(s) %-44s %s' % (fid, len(ms), f.get('target', '?'),
                                               ' '.join('%s@%s:%s' % (m.get('kind'), m.get('at'), 'rec' if not m.get('path') else m.get('path')[:4]) for m in ms)))
    return faults


def main(argv):
    a = argv[1:]

    def opt(k):
        if k not in a:
            return None
        i = a.index(k)
        v = a[i + 1]
        del a[i:i + 2]
        return v
    anix_dir, attacks = opt('--anix'), opt('--attacks')
    path = a[0] if a else os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'mod', 'somvr', 'assets', 'finishers.txt')
    faults = check(load(path), anix_dir, attacks)
    for x in faults:
        print('FAULT ' + x)
    print('%d fault(s)' % len(faults))
    return 1 if faults else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
