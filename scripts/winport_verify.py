#!/usr/bin/env python3
"""winport_verify.py - research tool (docs/re-winport.md): the second tier after winport_match.py for a sample of SITEs.

  python3 -I winport_verify.py prep SAMPLE PAIRS LINUX_INDEX WIN_INDEX OUT_DIR   -> OUT_DIR/{lin,win}.batch, cand.tsv
  (run the two batches with ghidra_query.sh: som-linux* -> OUT_DIR/lin.out, som-win64 -> OUT_DIR/win.out)
  python3 -I winport_verify.py score OUT_DIR                                      -> OUT_DIR/score.tsv + a summary

SAMPLE = winport_match.py's sites.tsv rows (the sample's lines, same columns, with a leading index column).
For every Linux function a sampled SITE needs (K_ENTRY/K_VSLOT: the target; K_CALL/K_JMP: the caller and the target),
the Windows candidates are the matched function (if any) or the unmatched Windows functions next to its matched
neighbours in the call graph (callees of its callers' images, callers of its callees' images; the 6 with most votes).
`score` compares each candidate's decompile with the Linux one: the small hex constants (field offsets, < 0x4000),
the floats and the strings both use (Jaccard), so a verdict is evidence, not a name.
"""
import collections, os, re, sys


def tsv(path):
    with open(path) as f:
        next(f)
        for ln in f:
            yield ln.rstrip('\n').split('\t')


def prep(sample, pairs, li, wi, out):
    os.makedirs(out, exist_ok=True)
    l2w = {int(a, 16): int(b, 16) for a, b, _ in tsv(pairs)}
    w2l = {b: a for a, b in l2w.items()}
    calls = {}
    for side, d in (('l', li), ('w', wi)):
        o, i = collections.defaultdict(set), collections.defaultdict(set)
        for a, b in tsv(os.path.join(d, 'calls.tsv')):
            a, b = int(a, 16), int(b, 16)
            o[a].add(b); i[b].add(a)
        calls[side] = (o, i)
    lo, lin = calls['l']; wo, win = calls['w']
    need = []
    for ln in open(sample):
        p = ln.split()
        idx, fn, nm, k, addr, tgt = p[:6]
        rest = p[6:]
        need.append((idx, nm, 'target', int(tgt, 16)))
        if k in ('K_CALL', 'K_JMP') and rest and rest[0].startswith('0x'):
            need.append((idx, nm, 'caller', int(rest[0], 16)))
    lins, wins = set(), set()
    with open(os.path.join(out, 'cand.tsv'), 'w') as f:
        f.write('idx\tsite\trole\tlinux\tmatched\tcandidates\n')
        for idx, nm, role, l in need:
            lins.add(l)
            if l in l2w:
                c = [l2w[l]]; m = 'yes'
            else:
                v = collections.Counter()
                for x in lin.get(l, ()):
                    if x in l2w:
                        for y in wo.get(l2w[x], ()):
                            if y not in w2l:
                                v[y] += 1
                for x in lo.get(l, ()):
                    if x in l2w and len(win.get(l2w[x], ())) <= 400:
                        for y in win.get(l2w[x], ()):
                            if y not in w2l:
                                v[y] += 1
                c = [y for y, _ in v.most_common(6)]; m = 'no'
            wins.update(c)
            f.write('%s\t%s\t%s\t%#x\t%s\t%s\n' % (idx, nm, role, l, m, ','.join('%#x' % y for y in c)))
    for name, s in (('lin', lins), ('win', wins)):
        with open(os.path.join(out, name + '.batch'), 'w') as f:
            for a in sorted(s):
                f.write('decompile %#x 30\n' % a)
    print('linux functions %d, windows candidates %d' % (len(lins), len(wins)))


def parse(path):
    """'### decompile 0xADDR ...' sections -> {addr: text}"""
    d, cur, buf = {}, None, []
    for ln in open(path, errors='replace'):
        m = re.match(r'### decompile (0x[0-9a-f]+)', ln)
        if m:
            if cur is not None:
                d[cur] = ''.join(buf)
            cur, buf = int(m.group(1), 16), []
        elif cur is not None:
            buf.append(ln)
    if cur is not None:
        d[cur] = ''.join(buf)
    return d


def feats(t):
    t = re.sub(r'(FUN|DAT|LAB|PTR|thunk_FUN|switchD|caseD)_[0-9a-f]+', 'X', t)
    hx = {int(h, 16) for h in re.findall(r'0x([0-9a-f]+)', t)}
    hx = {h for h in hx if 4 <= h < 0x4000}
    fl = set(re.findall(r'\b\d+\.\d+\b', t))
    st = set(re.findall(r'"([^"]{3,})"', t))
    return hx, fl, st


def jac(a, b):
    return len(a & b) / len(a | b) if a | b else 0.0


def score(out):
    L, W = parse(os.path.join(out, 'lin.out')), parse(os.path.join(out, 'win.out'))
    rows = []
    for idx, nm, role, l, m, cs in tsv(os.path.join(out, 'cand.tsv')):
        l = int(l, 16)
        lf = feats(L.get(l, ''))
        res = []
        for c in [int(x, 16) for x in cs.split(',') if x]:
            wf = feats(W.get(c, ''))
            s = 0.6 * jac(lf[0], wf[0]) + 0.2 * jac(lf[1], wf[1]) + 0.2 * jac(lf[2], wf[2])
            res.append((s, c, len(lf[0] & wf[0]), len(lf[0]), len(wf[0])))
        res.sort(reverse=True)
        b = res[0] if res else (0, 0, 0, len(lf[0]), 0)
        s2 = res[1][0] if len(res) > 1 else 0
        rows.append((idx, nm, role, l, m, b[1], b[0], s2, b[2], b[3], b[4], len(res)))
    with open(os.path.join(out, 'score.tsv'), 'w') as f:
        f.write('idx\tsite\trole\tlinux\tmatched\tbest\tscore\tsecond\tshared_offs\tlin_offs\twin_offs\tncand\n')
        for r in rows:
            f.write('%s\t%s\t%s\t%#x\t%s\t%#x\t%.2f\t%.2f\t%d\t%d\t%d\t%d\n' % r)
    for r in rows:
        print('%-4s %-20s %-6s %#9x m=%-3s best=%#x s=%.2f 2nd=%.2f offs %d/%d/%d n=%d' % r)


if __name__ == '__main__':
    if sys.argv[1] == 'prep':
        prep(*sys.argv[2:7])
    else:
        score(sys.argv[2])
