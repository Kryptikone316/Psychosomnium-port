#!/usr/bin/env python3
"""Scenario runner: patched exe in the original (Wine) vs the port, compared by SEQUENCE of distinct pictures (stall tolerant).
  scn.py record <name> <room> "<spawns obj,x,y;...>" <save room,type> <script-file>   -> work/wine/frames_<name>/ (host Wine + ffmpeg)
  scn.py fit    <name> <room> "<spawns>" <save> "<initial joy: frame:mask,...>"        -> searches input frames that reproduce the recording
"""
import os, sys, subprocess, glob, re, itertools
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import seqcmp
ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
WORK = os.path.join(ROOT, 'work', 'wine'); BIN = os.path.join(ROOT, 'build-host', 'psychosomnium')
PY = os.path.expanduser('~/venvs/pydev/bin/python')
TMPD = os.path.join(ROOT, 'work', 'scn_tmp'); os.makedirs(TMPD, exist_ok=True)

def host(cmd): return subprocess.run(['flatpak-spawn', '--host', 'sh', '-c', cmd], capture_output=True, text=True)

def record(name, room, spawns, save, script):
    exe = '%s/orig/%s.exe' % (WORK, name)
    args = [PY, '-I', os.path.join(ROOT, 'tools/wine/patch_exe.py'), os.path.join(WORK, 'orig/psycho.exe'), exe, str(room)]
    for sp in filter(None, spawns.split(';')): args += ['--spawn', sp]
    if save: args += ['--save', save]
    if os.environ.get('SCN_CODE'): args += ['--code', os.environ['SCN_CODE']]
    print(subprocess.run(args, capture_output=True, text=True, env=dict(os.environ, TMPDIR=TMPD)).stdout.strip().splitlines()[-1])
    print(host('timeout 200 env %s/rec.sh %s %s %s.exe' % (WORK, name, os.path.abspath(script), name)).stdout.strip())
    fd = '%s/frames_%s' % (WORK, name); os.makedirs(fd, exist_ok=True)
    host('cd %s && ffmpeg -loglevel error -y -i shots/%s.mkv -compression_level 1 frames_%s/f%%04d.png' % (WORK, name, name))
    print('frames:', len(glob.glob(fd + '/f*.png')))

def mine_hashes(room, spawns, save, joy, nframes, tag):
    hf = os.path.join(TMPD, 'h_%s.txt' % tag)
    env = dict(os.environ, PSY_GAMEDATA=os.path.join(ROOT, 'work', 't1', 'gamedata'), PSY_HEADLESS='1', PSY_TESTROOM=str(room), PSY_JOY=joy, PSY_HASHFILE=hf, PSY_MAXFRAMES=str(nframes))
    if spawns: env['PSY_SPAWNS'] = spawns
    if save: env['PSY_SAVE'] = save
    subprocess.run([BIN], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return [int(l.split()[1]) for l in open(hf)]

def lcs(a, b):
    prev = [0] * (len(b) + 1)
    for x in a:
        cur = [0]
        for j, y in enumerate(b): cur.append(prev[j] + 1 if x == y else max(prev[j + 1], cur[j]))
        prev = cur
    return prev[-1]

def orig_runs(name, hroom0=None):
    fs = sorted(glob.glob('%s/frames_%s/f*.png' % (WORK, name)))
    cache = os.path.join(TMPD, 'oh_%s.txt' % name)
    if os.path.exists(cache): hs = [int(x) for x in open(cache).read().split()]
    else:
        hs = [seqcmp.hash_png(f) for f in fs]; open(cache, 'w').write(' '.join(map(str, hs)))
    if hroom0 is not None and hroom0 in hs: hs = hs[hs.index(hroom0):]
    return seqcmp.rle(hs), hs

def score(room, spawns, save, joy, name, n=1200):
    h = mine_hashes(room, spawns, save, joy, n, name)
    # room start in mine: first frame after the title: the hash that first repeats after START (frame ~131+)
    return h

if __name__ == '__main__':
    if sys.argv[1] == 'record': record(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5], sys.argv[6])

def fit(name, room, spawns, save, events, nframes=1100, ranges=None, verbose=True):
    """events: list of [frame, mask]; searched one after another, coarse to fine. Returns (best score, events)."""
    base = 'START'
    def joy_of(ev): return '10:0x400,13:0,' + ','.join('%d:%d' % (f, m) for f, m in sorted(ev))
    h0 = mine_hashes(room, spawns, save, joy_of([]), 200, name)
    # room start: the first of my frames 130..136 (the room's first pictures) that the original also shows
    _, allh = orig_runs(name, None)
    m0 = next((k for k in range(130, 137) if h0[k] in allh), 131)
    oruns, ohs = orig_runs(name, h0[m0])
    cache = {}
    def sc(ev):
        key = tuple(sorted(map(tuple, ev)))
        if key in cache: return cache[key]
        mh = mine_hashes(room, spawns, save, joy_of(ev), nframes, name)[m0:]
        mr = seqcmp.rle(mh)
        l = lcs([h for h, n in oruns], [h for h, n in mr])
        cache[key] = (len(oruns) - l, len(mr)); return cache[key]
    ev = [list(e) for e in events]
    best = sc(ev)
    if verbose: print('start', best, ev, flush=True)
    for step in (32, 8, 2, 1):
        for i in range(len(ev)):
            improved = True
            while improved:
                improved = False
                for d in (-step, step):
                    cand = [list(e) for e in ev]; cand[i][0] += d
                    if cand[i][0] < 0: continue
                    s = sc(cand)
                    if s < best: best, ev, improved = s, cand, True
                if improved and verbose: print('step', step, 'event', i, '->', best, ev, flush=True)
    return best, ev, len(oruns)

if __name__ == '__main__' and sys.argv[1] == 'fit':
    name, room, spawns, save = sys.argv[2:6]
    events = [[int(a), int(b, 0)] for a, b in (x.split(':') for x in sys.argv[6].split(','))]
    best, ev, n = fit(name, room, spawns, save, events)
    print('RESULT: unexplained original picture-runs %d of %d (mine has %d runs); inputs %s' % (best[0], n, best[1], ev))
