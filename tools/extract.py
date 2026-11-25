#!/usr/bin/env python3
"""Extract a Game Maker 7.0 exe into plain assets + JSON/GML.

usage: python3 -I extract.py psycho.exe outdir
Format reference: LateralGM GmFileReader (GM 7.0 / "701" flavour, exe-embedded variant).
"""
import sys, os, io, json, struct, zlib
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gm7 import Stream, make_decode_table
SCRIPT_TABLE = make_decode_table(12345)
from PIL import Image

MAIN_EVENTS = ['create', 'destroy', 'alarm', 'step', 'collision', 'keyboard',
               'mouse', 'other', 'draw', 'keypress', 'keyrelease']
SOUND_KINDS = ['normal', 'background', '3d', 'multimedia']


def expect(got, *want, what=''):
    if got not in want:
        raise ValueError('%s: version %r not in %r' % (what, got, want))
    return got


def locate(d):
    """Return (magic_offset). The data header is the last 1234321 magic followed by 700."""
    m = struct.pack('<II', 1234321, 700)
    return d.rfind(m)


def bmp_to_png(data, transparent, path):
    im = Image.open(io.BytesIO(data)).convert('RGBA')
    if transparent:
        # GM treats the bottom-left pixel colour as the transparent key
        key = im.getpixel((0, im.height - 1))
        px = im.load()
        for y in range(im.height):
            for x in range(im.width):
                if px[x, y] == key:
                    px[x, y] = (key[0], key[1], key[2], 0)
    im.save(path)
    return im.size


def bgra_to_png(data, w, h, transparent, path, flip=False):
    """The pixel data's 4th byte is the alpha (0 = transparent); there is no colour key."""
    im = Image.frombuffer('RGBA', (w, h), data, 'raw', 'BGRA', 0, 1)
    if flip: im = im.transpose(Image.FLIP_TOP_BOTTOM)
    im.save(path)


def read_actions(s):
    expect(s.i4(), 400, what='actions')
    acts = []
    for _ in range(s.i4()):
        a = {}
        s.skip(4)
        a['lib'] = s.i4(); a['id'] = s.i4()
        a['kind'] = s.i4(); a['allow_relative'] = s.bool(); a['question'] = s.bool()
        a['can_apply_to'] = s.bool(); a['exec_type'] = s.i4()
        a['func'] = s.str(); a['code'] = s.str()
        nargs = s.i4()
        kinds = [s.i4() for _ in range(s.i4())]
        a['arg_kinds'] = kinds
        a['applies_to'] = s.i4()
        a['relative'] = s.bool()
        n = s.i4()
        a['args'] = [s.str() for _ in range(n)]
        a['not'] = s.bool()
        acts.append(a)
    return acts


INFO = {'rooms': {}}


def read_rooms(s, nrooms, lock_inst, lock_tile):
    rooms = {}
    for i in range(nrooms):
        if not s.bool(): continue
        name = s.str()
        expect(s.i4(), 541, what='room')
        rp = INFO['rooms'].setdefault(i, {})
        r = dict(name=name, caption=s.str(), w=s.i4(), h=s.i4(), speed=s.i4(), persistent=s.bool(),
                 bgcolor=s.i4(), draw_bg_color=s.bool())
        rp['code_pos'] = s.pos
        r['creation_code'] = s.str()
        nb = s.i4()
        r['backgrounds'] = [dict(visible=s.bool(), foreground=s.bool(), bg=s.i4(), x=s.i4(), y=s.i4(),
                                 tile_h=s.bool(), tile_v=s.bool(), hspeed=s.i4(), vspeed=s.i4(),
                                 stretch=s.bool()) for _ in range(nb)]
        r['views_enabled'] = s.bool()
        nv = s.i4()
        r['views'] = [dict(visible=s.bool(), vx=s.i4(), vy=s.i4(), vw=s.i4(), vh=s.i4(), px=s.i4(),
                           py=s.i4(), pw=s.i4(), ph=s.i4(), hborder=s.i4(), vborder=s.i4(),
                           hspeed=s.i4(), vspeed=s.i4(), follow=s.i4()) for _ in range(nv)]
        rp['ninst_pos'] = s.pos
        ni = s.i4()
        insts = []
        for _ in range(ni):
            e = dict(x=s.i4(), y=s.i4(), obj=s.i4(), id=s.i4(), code=s.str())
            if lock_inst: e['locked'] = s.bool()
            insts.append(e)
        r['instances'] = insts
        rp['inst_end'] = s.pos
        nt = s.i4()
        tiles = []
        for _ in range(nt):
            e = dict(x=s.i4(), y=s.i4(), bg=s.i4(), bx=s.i4(), by=s.i4(), w=s.i4(), h=s.i4(),
                     depth=s.i4(), id=s.i4())
            if lock_tile: e['locked'] = s.bool()
            tiles.append(e)
        r['tiles'] = tiles
        rooms[i] = r
    return rooms


def main(exe, out):
    d = open(exe, 'rb').read()
    mo = locate(d)
    assert mo > 0, 'GM7 data header not found'
    os.makedirs(out, exist_ok=True)

    # --- plain header: settings -------------------------------------------
    s = Stream(d, mo)
    assert s.i4() == 1234321 and s.i4() == 700
    s.i4()
    expect(s.i4(), 702, what='settings')
    names = ('fullscreen interpolate noborder cursor scaling resize ontop color '
             'setres depth res freq nobuttons sync f4 f1 esc f5 f9 close prio freeze '
             'loadbar showload').split()
    settings = {n: s.i4() for n in names}
    img = s.zlib()
    open(os.path.join(out, 'loading.bmp'), 'wb').write(img)
    settings['load_partial_transp'] = s.i4(); settings['load_alpha'] = s.i4()
    settings['scale_bar'] = s.i4()
    settings['display_errors'] = s.i4(); settings['write_log'] = s.i4()
    settings['abort_on_error'] = s.i4(); settings['error_flags'] = s.i4()
    nconst = s.i4()
    consts = {}
    for _ in range(nconst):
        k = s.str(); consts[k] = s.str()
    settings['constants'] = consts
    dllname = s.str()
    dll = s.zlib()
    bl_off = s.p
    # --- resource block (zlib, then swap-table scrambled) -------------------
    B = zlib.decompress(d[s.p + 4:s.p + 4 + struct.unpack_from('<i', d, s.p)[0]])
    INFO['B'] = B; INFO['b_len_off'] = bl_off; INFO['exe'] = d
    s = Stream(B)
    s1 = s.i4(); s2 = s.i4(); s.skip(s1 * 4); seed = s.i4(); s.skip(s2 * 4)
    b1 = s.raw(1)[0]
    INFO['seed'] = seed; INFO['start'] = s.pos
    s.set_seed(seed)
    settings['game_id'] = b1 | (int.from_bytes(s.raw(3), 'little') << 8)
    s.skip(16)  # GUID
    s.i4()      # unknown, always junk
    expect(s.i4(), 700, what='packages'); assert s.i4() == 0
    json.dump(settings, open(os.path.join(out, 'settings.json'), 'w'), indent=1)

    def mk(sub):
        p = os.path.join(out, sub); os.makedirs(p, exist_ok=True); return p

    # --- sounds ---------------------------------------------------------------
    expect(s.i4(), 400, what='sounds')
    sounds = {}
    n = s.i4()
    for i in range(n):
        if not s.bool(): continue
        name = s.str()
        expect(s.i4(), 600, what='sound')
        kind = SOUND_KINDS[s.i4()]
        ftype = s.str(); fname = s.str()
        data = None
        if s.bool():
            data = s.zlib()
        effects = s.i4(); vol = s.dbl(); pan = s.dbl(); preload = s.bool()
        ext = ftype if ftype.startswith('.') else '.bin'
        if data is not None:
            open(os.path.join(mk('sounds'), name + ext), 'wb').write(data)
        sounds[i] = dict(name=name, kind=kind, type=ftype, file=fname, effects=effects,
                         volume=vol, pan=pan, preload=preload, has_data=data is not None)
    json.dump(sounds, open(os.path.join(out, 'sounds.json'), 'w'), indent=1)

    # --- sprites ----------------------------------------------------------------
    expect(s.i4(), 400, what='sprites')
    sprites = {}
    for i in range(s.i4()):
        if not s.bool(): continue
        name = s.str()
        expect(s.i4(), 542, what='sprite')
        w = s.i4(); h = s.i4()
        bb = [s.i4() for _ in range(4)]  # left right bottom top
        transp = s.bool(); smooth = s.bool(); preload = s.bool()
        bbmode = s.i4(); precise = s.bool()
        ox = s.i4(); oy = s.i4()
        frames = []
        for j in range(s.i4()):
            expect(s.i4(), 540, what='subimage')
            if not s.bool(): continue
            fw = s.i4(); fh = s.i4()
            data = zlib.decompress(s.raw(s.i4()))
            assert len(data) == fw * fh * 4, (name, fw, fh, len(data))
            fn = 'sprites/%s_%02d.png' % (name, j)
            mk('sprites')
            bgra_to_png(data, fw, fh, transp, os.path.join(out, fn))
            frames.append(fn)
        sprites[i] = dict(name=name, w=w, h=h, bbox=bb, transparent=transp, smooth=smooth,
                          bbmode=bbmode, precise=precise, origin=[ox, oy], frames=frames)
    json.dump(sprites, open(os.path.join(out, 'sprites.json'), 'w'), indent=1)

    # --- backgrounds --------------------------------------------------------------
    expect(s.i4(), 400, what='backgrounds')
    backs = {}
    for i in range(s.i4()):
        if not s.bool(): continue
        name = s.str()
        expect(s.i4(), 543, what='background')
        w = s.i4(); h = s.i4(); transp = s.bool(); smooth = s.bool(); preload = s.bool()
        tileset = s.bool()
        tw, th, ho, vo, hs, vs = [s.i4() for _ in range(6)]
        fn = None
        if s.bool():
            if s.i4() != -1:
                data = zlib.decompress(s.raw(s.i4()))
                mk('backgrounds')
                fn = 'backgrounds/%s.png' % name
                bmp_to_png(data, transp, os.path.join(out, fn))
        backs[i] = dict(name=name, w=w, h=h, transparent=transp, smooth=smooth, tileset=tileset,
                        tile=[tw, th], offset=[ho, vo], sep=[hs, vs], file=fn)
    json.dump(backs, open(os.path.join(out, 'backgrounds.json'), 'w'), indent=1)

    # --- paths ----------------------------------------------------------------------
    expect(s.i4(), 420, what='paths')
    paths = {}
    for i in range(s.i4()):
        if not s.bool(): continue
        name = s.str()
        expect(s.i4(), 530, what='path')
        smooth = s.bool(); closed = s.bool(); prec = s.i4(); room = s.i4(); sx = s.i4(); sy = s.i4()
        pts = [[s.dbl(), s.dbl(), s.dbl()] for _ in range(s.i4())]
        paths[i] = dict(name=name, smooth=smooth, closed=closed, precision=prec, points=pts)
    json.dump(paths, open(os.path.join(out, 'paths.json'), 'w'), indent=1)

    # --- scripts ----------------------------------------------------------------------
    expect(s.i4(), 400, what='scripts')
    scripts = {}
    for i in range(s.i4()):
        if not s.bool(): continue
        name = s.str()
        expect(s.i4(), 400, what='script')
        code = s.raw(s.i4())
        try: code = zlib.decompress(code)
        except zlib.error: pass
        if len(code) >= 4 and name:       # exe scripts: 4-byte header, then a fixed-seed substitution
            code = bytes(SCRIPT_TABLE[b] for b in code[4:])
        code = code.decode('latin-1')
        scripts[i] = name
        open(os.path.join(mk('scripts'), name + '.gml'), 'w', encoding='latin-1').write(code)
    json.dump(scripts, open(os.path.join(out, 'scripts.json'), 'w'), indent=1)

    # --- fonts (version int first) -------------------------------------------------------
    rver = s.i4()
    expect(rver, 540, what='fonts')
    fonts = {}
    for i in range(s.i4()):
        if not s.bool(): continue
        name = s.str()
        expect(s.i4(), 540, what='font')
        fam = s.str(); size = s.i4(); bold = s.bool(); italic = s.bool()
        rmin = struct.unpack('<H', s.raw(2))[0]
        charset = s.raw(1)[0]; aa = s.raw(1)[0]; rmax = s.i4()
        glyphs = [[s.i4() for _ in range(6)] for _ in range(256)]   # x y w h shift offset
        aw = s.i4(); ah = s.i4()
        alpha = zlib.decompress(s.raw(s.i4()))
        assert len(alpha) == aw * ah, (name, aw, ah, len(alpha))
        mk('fonts')
        atlas = 'fonts/%s.png' % name
        Image.frombuffer('L', (aw, ah), alpha, 'raw', 'L', 0, 1).save(os.path.join(out, atlas))
        fonts[i] = dict(name=name, family=fam, size=size, bold=bold, italic=italic,
                        range=[rmin, rmax], charset=charset, antialias=aa, atlas=atlas,
                        atlas_size=[aw, ah], glyphs={c: g for c, g in enumerate(glyphs) if any(g)})
    json.dump(fonts, open(os.path.join(out, 'fonts.json'), 'w'), indent=1)

    # --- timelines --------------------------------------------------------------------------
    expect(s.i4(), 500, what='timelines')
    timelines = {}
    for i in range(s.i4()):
        if not s.bool(): continue
        name = s.str()
        expect(s.i4(), 500, what='timeline')
        moms = []
        for _ in range(s.i4()):
            step = s.i4(); moms.append(dict(step=step, actions=read_actions(s)))
        timelines[i] = dict(name=name, moments=moms)
    json.dump(timelines, open(os.path.join(out, 'timelines.json'), 'w'), indent=1)

    # --- objects ------------------------------------------------------------------------------
    expect(s.i4(), 400, what='objects')
    objects = {}
    for i in range(s.i4()):
        if not s.bool(): continue
        name = s.str()
        expect(s.i4(), 430, what='object')
        o = dict(name=name, sprite=s.i4(), solid=s.bool(), visible=s.bool(), depth=s.i4(),
                 persistent=s.bool(), parent=s.i4(), mask=s.i4(), events=[])
        for j in range(s.i4() + 1):
            while True:
                first = s.i4()
                if first == -1: break
                o['events'].append(dict(main=MAIN_EVENTS[j] if j < len(MAIN_EVENTS) else j,
                                        sub=first, actions=read_actions(s)))
        objects[i] = o
    json.dump(objects, open(os.path.join(out, 'objects.json'), 'w'), indent=1)

    # --- rooms ----------------------------------------------------------------------------------
    # The exe omits editor-only room fields (snap grid, isometric, editor tail) and the instances' "locked"
    # flag (verified: last instance/tile ids land on 101410 / 10000000). This game has no tiles.
    expect(s.i4(), 420, what='rooms')
    rooms = read_rooms(s, s.i4(), False, False)
    assert not any(r['tiles'] for r in rooms.values()), 'tiles not supported'
    last_inst, last_tile = s.i4(), s.i4()
    assert (last_inst, last_tile) == (101410, 10000000), (last_inst, last_tile)
    json.dump(rooms, open(os.path.join(out, 'rooms.json'), 'w'), indent=1)
    print('sounds', len(sounds), 'sprites', len(sprites), 'backgrounds', len(backs), 'paths', len(paths),
          'scripts', len(scripts), 'fonts', len(fonts), 'timelines', len(timelines),
          'objects', len(objects), 'rooms', len(rooms))
    print('last ids', last_inst, last_tile, 'stream pos', s.pos, 'of', len(B))

    # --- included files (the music modules) -------------------------------------------------------
    ver = expect(s.i4(), 430, 600, 620, what='includes')
    incs = []
    for _ in range(s.i4()):
        expect(s.i4(), 620, what='include')
        e = dict(filename=s.str(), filepath=s.str(), original=s.bool(), size=s.i4(), stored=s.bool())
        if e['stored']:
            data = zlib.decompress(s.raw(s.i4()))
            assert len(data) == e['size'], (e['filename'], len(data), e['size'])
            open(os.path.join(mk('included'), e['filename']), 'wb').write(data)
        e.update(export=s.i4(), export_folder=s.str(), overwrite=s.bool(), free_memory=s.bool(),
                 remove_at_end=s.bool())
        incs.append(e)
    json.dump(incs, open(os.path.join(out, 'included.json'), 'w'), indent=1)
    # --- game information (help text, RTF) ------------------------------------------------------------
    expect(s.i4(), 430, 600, 620, what='gameinfo')
    bc = s.i4(); embed = s.bool(); cap = s.str()
    s.skip(4 * 4); s.skip(4 * 4)   # window rect, 4 flags
    text = s.raw(s.i4())
    try: text = zlib.decompress(text)
    except zlib.error: pass
    text = text.decode('latin-1')
    open(os.path.join(out, 'gameinfo.rtf'), 'w', encoding='latin-1').write(text)
    # --- library creation code (skipped), room execution order -------------------------------------
    expect(s.i4(), 500, what='library creation')
    for _ in range(s.i4()): s.skip(s.i4())
    expect(s.i4(), 500, 540, 700, what='room order')
    INFO['order_pos'] = s.pos
    order = [s.i4() for _ in range(s.i4())]
    json.dump(order, open(os.path.join(out, 'room_order.json'), 'w'))
    print('room order', order)
    print('stream pos', s.pos, 'of', len(B), '; includes', len(incs), '; gameinfo bytes', len(text))
    open(os.path.join(out, dllname), 'wb').write(dll)


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
