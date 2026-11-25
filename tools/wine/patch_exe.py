#!/usr/bin/env python3
"""Make a test copy of psycho.exe that starts in a chosen room with chosen characters already placed.
usage: patch_exe.py psycho.exe out.exe ROOM [--spawn OBJ,X,Y ...] [--save ROOM,TYPE]
- the chosen room is moved to position 1 of the room order (right after the splash screen)
- --save sets global.saveroom/global.type in the splash room creation code (so they exist before any Create event); --code is extra creation code of the chosen room
- each --spawn appends an instance (id 200000+n) to that room
- --save sets global.saveroom / global.type in the room's creation code (needed for death/restart in rooms without a pump)
Format: the game data is zlib( scrambled stream ); scrambled byte = enc[(plain + absolute offset) & 255].
"""
import sys, os, struct, zlib, importlib.util
here = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('ex', os.path.join(here, '..', 'extract.py'))
ex = importlib.util.module_from_spec(spec); spec.loader.exec_module(ex)

def enc_table(seed):
    a = 6 + (seed % 250); b = seed // 250
    e = list(range(256))
    for i in range(1, 10001):
        j = 1 + ((i * a + b) % 254); e[j], e[j + 1] = e[j + 1], e[j]
    return e

def main():
    args = sys.argv[1:]
    src, dst, room = args[0], args[1], int(args[2])
    spawns = []; save = None; extra = ''
    i = 3
    while i < len(args):
        if args[i] == '--spawn': spawns.append(tuple(int(v) for v in args[i + 1].split(','))); i += 2
        elif args[i] == '--code': extra = args[i + 1]; i += 2
        elif args[i] == '--save': save = tuple(int(v) for v in args[i + 1].split(',')); i += 2
        else: raise SystemExit('bad arg ' + args[i])
    tmp = os.path.join(os.environ.get('TMPDIR', '/tmp'), 'patch_exe_out'); os.makedirs(tmp, exist_ok=True)
    ex.main(src, tmp)
    I = ex.INFO; B = bytearray(I['B']); exe = I['exe']
    enc = enc_table(I['seed']); dec = [0] * 256
    for k in range(1, 256): dec[enc[k]] = k
    st = I['start']
    plain = bytearray((dec[B[o]] - o) & 255 for o in range(st, len(B)))      # position st is the first scrambled byte
    # self test: re-encrypting the untouched plain stream must give back B
    again = bytes(enc[(plain[o - st] + o) & 255] for o in range(st, len(B)))
    assert again == bytes(B[st:]), 'encrypt/decrypt round trip failed'
    R = I['rooms'][room]
    def at(p): return p - st
    ops = []                                                      # (offset, kind, data), applied from the end of the stream backwards
    order = [22, room] + [r for r in ex_order(I) if r not in (22, room)]
    op = at(I['order_pos']) + 4                                      # skip the count field
    ops.append((op, 'set', struct.pack('<%di' % len(order), *order)))
    ni = struct.unpack_from('<i', plain, at(R['ninst_pos']))[0]
    if spawns:
        recs = b''.join(struct.pack('<4i', x, y, o, 200000 + n) + struct.pack('<i', 0) for n, (o, x, y) in enumerate(spawns))
        ops.append((at(R['inst_end']), 'ins', recs))
        ops.append((at(R['ninst_pos']), 'set', struct.pack('<i', ni + len(spawns))))
    def code_op(rn, code):
        rr = I['rooms'][rn]
        old_len = struct.unpack_from('<i', plain, at(rr['code_pos']))[0]
        old_txt = bytes(plain[at(rr['code_pos']) + 4: at(rr['code_pos']) + 4 + old_len])
        c = (old_txt + b' ' + code) if old_txt else code
        ops.append((at(rr['code_pos']), 'rep', (4 + old_len, struct.pack('<i', len(c)) + c)))
    if save:      # globals are set in the splash room (22) so they exist before any room's Create events read them
        code_op(22, ('global.saveroom=%d global.type=%d' % save).encode())
    if extra:     # extra GML runs as the chosen room's creation code (after its instances are created)
        code_op(room, extra.encode())
    for off, kind, data in sorted(ops, key=lambda t: -t[0]):
        if kind == 'set': plain[off:off + len(data)] = data
        elif kind == 'ins': plain[off:off] = data
        elif kind == 'rep': plain[off:off + data[0]] = data[1]
    newB = bytes(B[:st]) + bytes(enc[(plain[o - st] + o) & 255] for o in range(st, st + len(plain)))
    z = zlib.compress(newB, 9)
    out = bytes(exe[:I['b_len_off']]) + struct.pack('<i', len(z)) + z
    open(dst, 'wb').write(out)
    print('wrote', dst, len(out), 'bytes; order', order[:4], '...')

def ex_order(I):
    import json
    return json.load(open(os.path.join(os.environ.get('TMPDIR', '/tmp'), 'patch_exe_out', 'room_order.json')))

main()
