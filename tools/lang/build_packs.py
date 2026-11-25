#!/usr/bin/env python3
"""Builds the language packs: for each language in tr/<code>.py
     <code>.pak     UI strings + baked glyphs (what the launcher/menus/pause/end screens use)           -> shippable
     <code>.script  the game's dialogue in that language (a translation of the original script)        -> local use only, see DEV-NOTES.md
Usage: build_packs.py [outdir] [codes...]"""
import os, sys, struct, json, importlib, unicodedata
from PIL import Image, ImageDraw, ImageFont
HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, HERE)
sys.dont_write_bytecode = True
from ui_en import UI, LANGS
SIZE = 20; ASCENT = 19; LINE = 24
FONTS = {  # per language: (path, ttc index)
    'default': (os.path.join(ROOT, 'tools/fonts/DejaVuSansCondensed-Bold.ttf'), 0),
    'ja': (os.path.join(ROOT, 'tools/fonts-big/NotoSansCJK-Bold.ttc'), 0),
    'zh': (os.path.join(ROOT, 'tools/fonts-big/NotoSansCJK-Bold.ttc'), 2),
}
CJK = {'ja', 'zh'}; RTL = {'ar'}

# ---------------------------------------------------------------------------------------------- Arabic shaping
AR_RIGHT = set('اأإآدذرزوؤةٱ')           # join to the previous letter only
AR_NONJOIN = set('ء')
def is_ar(c): return '؀' <= c <= 'ۿ'
def ar_dual(c): return is_ar(c) and c not in AR_RIGHT and c not in AR_NONJOIN and unicodedata.category(c) == 'Lo'
def ar_joinable(c): return is_ar(c) and c not in AR_NONJOIN and unicodedata.category(c) == 'Lo'
FORMS = {}
for cp in range(0xFE70, 0xFEFD):
    d = unicodedata.decomposition(chr(cp))
    if d.startswith('<'):
        tag, base = d.split(' ', 1)[0], d.split(' ', 1)[1].split()
        if len(base) == 1: FORMS.setdefault(chr(int(base[0], 16)), {})[tag] = chr(cp)
LAMALEF = {'آ': ('ﻵ', 'ﻶ'), 'أ': ('ﻷ', 'ﻸ'), 'إ': ('ﻹ', 'ﻺ'), 'ا': ('ﻻ', 'ﻼ')}    # (isolated, final) ligatures of lam + alef
def shape_ar(text):
    out = []; i = 0; n = len(text)
    while i < n:
        c = text[i]
        if not ar_joinable(c): out.append(c); i += 1; continue
        prev_join = i > 0 and ar_dual(text[i - 1])
        if c == 'ل' and i + 1 < n and text[i + 1] in LAMALEF:
            iso, fin = LAMALEF[text[i + 1]]
            out.append(fin if prev_join else iso); i += 2; continue
        next_join = i + 1 < n and ar_joinable(text[i + 1]) and ar_dual(c)
        if c in AR_RIGHT: next_join = False
        tag = '<medial>' if prev_join and next_join else '<final>' if prev_join else '<initial>' if next_join else '<isolated>'
        out.append(FORMS.get(c, {}).get(tag, c)); i += 1
    return ''.join(out)

# ---------------------------------------------------------------------------------------------- glyph baking
def bake(font, cps):
    glyphs = {}; atlas = Image.new('L', (512, 1024), 0); x = y = rowh = 0
    for cp in sorted(cps):
        ch = chr(cp)
        l, t, r, b = font.getbbox(ch, anchor='ls'); w, h = max(r - l, 0), max(b - t, 0)
        adv = int(round(font.getlength(ch)))
        if w and h:
            if x + w + 1 > 512: x = 0; y += rowh + 1; rowh = 0
            im = Image.new('L', (w, h), 0); ImageDraw.Draw(im).text((-l, -t), ch, font=font, fill=255, anchor='ls')
            atlas.paste(im, (x, y)); glyphs[cp] = (x, y, w, h, l, t, adv); x += w + 1; rowh = max(rowh, h)
        else: glyphs[cp] = (0, 0, 0, 0, 0, 0, adv)
    ah = y + rowh + 1
    return glyphs, atlas.crop((0, 0, 512, ah))

def wrap_lines(font, text, width, cjk):
    lines = 0; cur = ''
    toks = list(text) if cjk else text.replace('#', ' ').split(' ')
    for tkn in toks:
        trial = cur + tkn if cjk else (cur + ' ' + tkn if cur else tkn)
        if font.getlength(trial) <= width: cur = trial
        else: lines += 1; cur = tkn
    return lines + 1

def build(code, outdir):
    tr = importlib.import_module('tr.' + code)
    try: pr = importlib.import_module('private.' + code)       # the dialogue translation, if this machine has it
    except ImportError: pr = None
    fpath, fidx = FONTS.get(code, FONTS['default'])
    font = ImageFont.truetype(fpath, SIZE, index=fidx)
    shape = shape_ar if code == 'ar' else (lambda s: s)
    ui = []
    for k, en in UI:
        s = tr.UI.get(k)
        if s is None: raise SystemExit('%s: missing UI string %s' % (code, k))
        if k in ('V_FRAMES', 'V_MS') and s.count('%d') != 1: raise SystemExit('%s: %s needs exactly one %%d' % (code, k))
        s = shape(s); ui.append(s)
        if k.startswith('D_') and wrap_lines(font, s, 520, code in CJK) > 3: print('  warn %s: %s wraps to > 3 lines' % (code, k))
        if k.startswith('ROW_') and font.getlength(s) > 250: print('  warn %s: %s is wide (%d px)' % (code, k, font.getlength(s)))
        if k.startswith('V_') and font.getlength(s) > 190: print('  warn %s: %s is wide (%d px)' % (code, k, font.getlength(s)))
    script = []
    keys_file = os.path.join(HERE, 'private', 'en_script.json')
    for row in (json.load(open(keys_file, encoding='utf-8')) if pr and os.path.exists(keys_file) else []):
        key = row['key']; s = pr.SCRIPT.get(key)
        if s is None: raise SystemExit('%s: missing dialogue %s' % (code, key))
        o, e, sub, k = (int(v) for v in key.split('/'))
        script.append((o, e, sub, k, shape(s)))
        if wrap_lines(font, s, 592, code in CJK) > 4: print('  warn %s: dialogue %s wraps to > 4 lines' % (code, key))
        if key in pr.ALT: script.append((o, e, sub, k | 0x80, shape(pr.ALT[key])))
    cps = set(range(32, 127))
    for s in ui: cps |= set(ord(c) for c in s)
    for *_, s in script: cps |= set(ord(c) for c in s.replace('#', ' '))       # (dialogue glyphs only when the private translation is present)
    cps.discard(10)
    glyphs, atlas = bake(font, cps)
    flags = (1 if code in RTL else 0) | (2 if code in CJK else 0)
    b = bytearray(struct.pack('<4sIIHH', b'PSYL', 1, flags, ASCENT, LINE))
    b += struct.pack('<I', len(ui))
    for s in ui: e = s.encode('utf-8'); b += struct.pack('<H', len(e)) + e
    b += struct.pack('<I', len(glyphs))
    for cp in sorted(glyphs):
        x, y, w, h, bx, ty, adv = glyphs[cp]
        b += struct.pack('<IHHBBbbBB', cp, x, y, w, h, max(-128, min(127, bx)), max(-128, min(127, ty)), min(255, adv), 0)
    b += struct.pack('<HH', atlas.width, atlas.height) + atlas.tobytes()
    open(os.path.join(outdir, code + '.pak'), 'wb').write(b)
    if not script:
        print('%s: %d glyphs, pak %d bytes (no dialogue translation on this machine)' % (code, len(glyphs), len(b)))
        return
    s = bytearray(struct.pack('<4sII', b'PSYS', 1, len(script)))
    for o, e, sub, k, text in script:
        t = text.replace('#', ' ').encode('utf-8'); s += struct.pack('<BBBBH', o, e, sub, k, len(t)) + t
    open(os.path.join(outdir, code + '.script'), 'wb').write(s)
    print('%s: %d glyphs, atlas %dx%d, pak %d bytes, script %d bytes' % (code, len(glyphs), atlas.width, atlas.height, len(b), len(s)))

if __name__ == '__main__':
    outdir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'build-lang')
    os.makedirs(outdir, exist_ok=True)
    codes = sys.argv[2:] or [c for c, _ in LANGS if c != 'en' and os.path.exists(os.path.join(HERE, 'tr', c + '.py'))]
    for c in codes: build(c, outdir)
