#!/usr/bin/env python3
"""winport_vtfind.py - research tool (docs/re-winport.md 7.4): the Windows vtable of a Linux vtable, by votes.

  python3 -I winport_vtfind.py LINUX_ADDR...        (a vtable slot address, a vtable address point or a function)

For every Linux vtable that holds the address (vfuncs.tsv; a slot address picks its vtable directly), each slot whose
function is paired (pairs.tsv) votes for every Windows vtable (runs of .text pointers in .rdata named by a LEA, as
winport_match.py finds them) that holds the paired function, with the slot delta. Prints the best Windows vtables with
their votes, the delta, and for the asked slot the Windows slot address and its function.
"""
import collections, os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import winport_match as wm  # noqa: E402

SOM = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REF = os.path.join(SOM, 'reference')
PAIRS = os.environ.get('WP_PAIRS', os.path.expanduser('~/Projects/flat2vr/ShadowOfMordor/logs/winport/m3/pairs.tsv'))


def main():
    l2w = {}
    with open(PAIRS) as f:
        next(f)
        for ln in f:
            a, b, _ = ln.split('\t')
            l2w[int(a, 16)] = int(b, 16)
    pe = wm.PE(os.path.join(REF, 'bin/ShadowOfMordor.exe'))
    wset = set()
    with open(os.path.join(REF, 'index-win64/functions.tsv')) as f:
        next(f)
        for ln in f:
            wset.add(int(ln.split('\t')[0], 16))
    wvt, _ = wm.win_vtables(pe, wset)
    lvt, lcls = wm.load_linux_vtables(os.path.join(REF, 'index'))
    where = collections.defaultdict(list)   # windows function -> [(vtable, slot)]
    for v, sl in wvt.items():
        for j, fn in enumerate(sl):
            where[fn].append((v, j))
    for arg in sys.argv[1:]:
        a = int(arg, 16)
        hits = []
        for v, sl in lvt.items():
            if v <= a < v + 8 * len(sl) and (a - v) % 8 == 0:
                hits.append((v, (a - v) // 8))
            elif a in sl:
                hits.extend((v, i) for i, x in enumerate(sl) if x == a)
        if not hits:
            print('%s: no Linux vtable holds it' % arg)
            continue
        for v, slot in hits[:6]:
            sl = lvt[v]
            votes = collections.Counter()
            for i, fn in enumerate(sl):
                w = l2w.get(fn)
                for wv, j in where.get(w, ()) if w else ():
                    votes[(wv, j - i)] += 1
            npair = sum(1 for fn in sl if fn in l2w)
            print('%s: linux %s vtable %#x slot %d (fn %#x) - %d slots, %d paired' % (
                arg, lcls.get(v, '?'), v, slot, sl[slot] if slot < len(sl) else 0, len(sl), npair))
            for (wv, d), n in votes.most_common(4):
                j = slot + d
                ws = wvt[wv]
                fn = ws[j] if 0 <= j < len(ws) else None
                print('   win vtable %#x (%d slots) delta %+d votes %d -> slot %d at %#x = %s%s' % (
                    wv, len(ws), d, n, j, wv + 8 * j, '%#x' % fn if fn else '-',
                    ' (paired with linux %#x)' % next((l for l, w in l2w.items() if w == fn), 0) if fn in l2w.values() else ''))


if __name__ == '__main__':
    main()
