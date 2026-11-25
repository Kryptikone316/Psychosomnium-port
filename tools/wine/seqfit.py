#!/usr/bin/env python3
"""Sequential input fitter. usage: seqfit.py NAME ROOM "spawns" "save" "mask:hold_ms:gap_lo:gap_hi[:first_lo:first_hi],..."
Each input is scanned (start frame, then hold +-1 around round(ms*0.06)+1) AFTER the previous one; unexplained = original picture-runs my engine does not produce."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import scn, seqcmp
name, room, spawns, save, spec = sys.argv[1:6]
NF = int(os.environ.get('NF', '2200'))
h0 = scn.mine_hashes(room, spawns, save, '10:0x400,13:0', 200, name)
_, allh = scn.orig_runs(name, None)
m0 = next((k for k in range(130, 137) if h0[k] in allh), 131)
oruns, _ = scn.orig_runs(name, h0[m0])
a = [h for h, n in oruns]
def score(ev):
    joy = '10:0x400,13:0,' + ','.join('%d:%d' % e for e in sorted(ev))
    mh = scn.mine_hashes(room, spawns, save, joy, NF, name)[m0:]
    return len(a) - scn.lcs(a, [h for h, n in seqcmp.rle(mh)])
base = []; prev_end = 0
for i, part in enumerate(spec.split(',')):
    f = part.split(':'); mask = int(f[0], 0); ms = int(f[1]); lo = int(f[2]); hi = int(f[3])
    if i == 0 and len(f) >= 6: lo, hi = int(f[4]), int(f[5]); prev_end = 0
    h0n = round(ms * 0.06) + 1
    best = None
    rng = range(lo, hi, 3) if i == 0 else range(prev_end + lo, prev_end + hi, 3)
    for t in rng:
        s = score(base + [(t, mask), (t + h0n, 0)])
        if best is None or s < best[0]: best = (s, t)
    t0 = best[1]
    for t in range(t0 - 2, t0 + 3):
        for h in (h0n - 1, h0n, h0n + 1):
            s = score(base + [(t, mask), (t + h, 0)])
            if s < best[0] or (s == best[0] and (t, h) == (t0, h0n)): best = (s, t, h)
    t = best[1]; h = best[2] if len(best) > 2 else h0n
    base += [(t, mask), (t + h, 0)]; prev_end = t + h
    print('input %d (mask %d, %d ms): start %d hold %d -> unexplained %d of %d' % (i, mask, ms, t, h, best[0], len(a)), flush=True)
print('FINAL', score(base), 'of', len(a), 'events', ','.join('%d:%d' % e for e in sorted(base)))
