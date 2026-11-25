#!/usr/bin/env python3
"""Compare the SEQUENCE of distinct frames of the original (recorded PNG frames) with the port's (PSY_HASHFILE).
Stalls in the Wine run only repeat frames, so both are run-length encoded: the picture order must be identical and each original run
must be >= the port's run.  usage: seqcmp.py <orig frames dir> <first> <last> <port hashfile> [first port frame]"""
import sys, glob, numpy as np
from PIL import Image
N = 640 * 480
W = (np.arange(N, dtype=np.uint32) * np.uint32(2654435761) + np.uint32(12345)).astype(np.uint64)
def hash_png(p, mask=None):
    a = np.array(Image.open(p).convert('RGB'))
    v = ((a[..., 0].astype(np.uint16) >> 3) << 11) | ((a[..., 1].astype(np.uint16) >> 2) << 5) | (a[..., 2].astype(np.uint16) >> 3)
    return int((v.reshape(-1).astype(np.uint64) * W).sum())
def rle(seq):
    out = []
    for h in seq:
        if out and out[-1][0] == h: out[-1][1] += 1
        else: out.append([h, 1])
    return out
def load_port(path, first=0):
    rows = [tuple(map(int, l.split())) for l in open(path)]
    return [h for f, h in rows if f >= first]
def compare(orig, port, label=''):
    ro, rp = rle(orig), rle(port)
    # greedy ordered match of port runs inside orig runs (orig may contain extra leading/trailing runs)
    best = None
    for start in range(0, max(1, len(ro) - len(rp) + 1)):
        mism = 0; stalls = 0; short = 0
        for k, (h, n) in enumerate(rp):
            if start + k >= len(ro): mism += len(rp) - k; break
            if ro[start + k][0] != h: mism += 1
            else:
                if ro[start + k][1] < n: short += 1
                stalls += ro[start + k][1] - n
        if best is None or mism < best[0]: best = (mism, start, stalls, short)
        if mism == 0: break
    print('%s: port runs %d, original runs %d: picture-order mismatches %d (at original run offset %d), runs shorter than the port\'s %d, extra repeated frames (stalls) %d'
          % (label, len(rp), len(ro), best[0], best[1], best[3], best[2]))
    return best
if __name__ == '__main__':
    d, a, b, hp = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
    first = int(sys.argv[5]) if len(sys.argv) > 5 else 0
    orig = [hash_png('%s/f%04d.png' % (d, n)) for n in range(a, b + 1)]
    compare(orig, load_port(hp, first))
