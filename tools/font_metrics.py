"""Write a language's font file (lang/WarBreedsHD_<code>.fonts) from its release.

Usage: python -I tools/font_metrics.py <code> "<release face>=<stand-in face>" ... [--style-rooms]
  e.g. font_metrics.py fr "BellGothic BT=Roboto Condensed" "Geometr231 BT=-" \
                          "Geometr231 Bold BT=-" "Sherwood=-"
  ("-": keep the English game's own face for it)

Reads re/<code>/disc/ (made by tools/locale_extract.py): the localized exe's font
table and text styles, and the release's fonts in FONTS/. Stand-ins are loaded from
third_party/fonts/. Measures with GDI, as the game draws.

Each line of the file is one of the English exe's faces:
  WBMd14 = BellGothic BT, 14, 400 | Roboto Condensed, 400, 16 | 4 5 4 ...
  - the face Windows drew the release's text in (the face it asked for, or Windows'
    fallback when no font has that name), its size (character height, the game's
    -lfHeight) and weight: used as they are when the player has them all installed;
  - the stand-in, its weight and cell height (the release face's, so line spacing
    and layout stay the release's); "-" keeps the English game's own bitmap face
    (French: the interface and heading faces, which read best at their small sizes);
  - the room: advance widths of Windows-1252 0x20..0xFF in the release's face. A
    string the stand-in draws wider than the release face did is narrowed to that
    width, so text fits wherever the release's did. With --style-rooms, where the
    release moved a text style off a face onto a narrower one of the same table
    (French: style 2, bold to regular), the narrower face's widths are the room.
    French doesn't use it: the release kept bold at most of style 2's places (the
    build list's labels among them), and the regular room squeezed those too hard.
The widths are measurements of the fonts (no glyphs), so the file can be published.
"""
import ctypes
import glob
import os
import struct
import sys
from ctypes import wintypes

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fontsheet as fs  # noqa: E402
import locale_extract as le  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
gdi = fs.gdi
gdi.CreateCompatibleDC.argtypes = [wintypes.HDC]
gdi.GetTextMetricsW.argtypes = [wintypes.HDC, ctypes.c_void_p]
gdi.GetCharWidth32W.argtypes = [wintypes.HDC, wintypes.UINT, wintypes.UINT, ctypes.POINTER(ctypes.c_int)]


class TM(ctypes.Structure):
    _fields_ = [(n, ctypes.c_long) for n in ('height', 'ascent', 'descent', 'il', 'el', 'ave', 'max', 'weight')] + \
               [('rest', ctypes.c_byte * 40)]


DC = None

# The .rdata rects (left, top, right, bottom) of text boxes the French and German
# releases resized for their fonts: found by diffing the exes (re/french-version.md).
LAYOUT_RECTS = [
    (0x5063f0, 'Device Lab: a long name\'s first line (SDeviceLab::CreateWidgets)'),
    (0x5063f8, 'Device Lab: a long name\'s second line'),
    (0x506400, 'Device Lab: the description page'),
    (0x5075d8, 'Gene Lab: a text box (0x485d5a, 0x4863c5)'),
    (0x5075e0, 'Gene Lab: a text box (0x485d6c, 0x4864aa)'),
    (0x507cc8, 'Briefing: the text page (0x489c1b)'),
    (0x507cd0, 'Briefing: a button label (0x489cdd)'),
    (0x507cd8, 'Briefing: a button label (0x489d23)'),
]


def measure(face, height, weight):
    """(tmHeight, widths of cp1252 0x20..0xFF, the face GDI used)"""
    global DC
    DC = DC or gdi.CreateCompatibleDC(None)
    lf = fs.LOGFONTW()
    lf.lfHeight, lf.lfWeight, lf.lfQuality, lf.lfFaceName = height, weight, 3, face
    f = gdi.CreateFontIndirectW(ctypes.byref(lf))
    old = gdi.SelectObject(DC, f)
    tm = TM()
    gdi.GetTextMetricsW(DC, ctypes.byref(tm))
    widths = []
    for b in range(0x20, 0x100):
        ch = bytes([b]).decode('cp1252', 'replace')
        w = ctypes.c_int()
        gdi.GetCharWidth32W(DC, ord(ch), ord(ch), ctypes.byref(w))
        widths.append(w.value)
    real = ctypes.create_unicode_buffer(64)
    gdi.GetTextFaceW(DC, 64, real)
    gdi.SelectObject(DC, old)
    gdi.DeleteObject(f)
    return tm.height, widths, real.value


def main():
    code = sys.argv[1]
    style_rooms = '--style-rooms' in sys.argv
    subs = dict(a.split('=', 1) for a in sys.argv[2:] if not a.startswith('--'))
    disc = os.path.join(ROOT, 're', code, 'disc')
    for p in glob.glob(os.path.join(disc, 'FONTS', '*')) + glob.glob(os.path.join(ROOT, 'third_party', 'fonts', '*', '*.ttf')):
        gdi.AddFontResourceExW(os.path.abspath(p), 0x10, None)
    loc = le.Exe(open(os.path.join(disc, 'WARBREED.EXE'), 'rb').read())
    gog = le.Exe(open(os.path.join(ROOT, 'testgame', 'WARBREED.EXE'), 'rb').read())
    _, table = loc.font_table()
    base = table[0][0]
    slots = {(s - base) // 4: (face, 0 if isinstance(size, str) else size, 700 if style == 1 else 400)
             for s, face, size, style in table}
    _, gst = gog.text_styles()
    _, lst = loc.text_styles()
    lines = [f'# WarBreeds HD fonts for {code}: made by tools/font_metrics.py from the release on the disc.',
             '# game face = release face, size, weight | stand-in, weight, cell height | room: widths of cp1252 0x20..0xFF',
             '# (see the tool for what each part does)']
    for i, gface in enumerate(le.GOG_SLOTS):
        face, size, weight = slots[i]
        if face not in subs:
            raise SystemExit(f'no stand-in given for "{face}"')
        cell, widths, got = measure(face, -size, weight)
        resolved = ''
        drawn = got
        if got.lower() != face.lower():
            # The release asked for a face no font file names (French: "Geometr231 Bold BT";
            # the bold file's family is "Geometr231 BT"), and today's Windows falls back to Arial.
            # The 1998 game drew it in that family (a player's screenshot of the French build
            # list: "Cocon", "Spore" in Geometric 231), so with the release's fonts it's the family
            # in bold. The room stays the fallback's (wider: the stand-ins only get more room).
            family = face.replace(' Bold', '')
            if family != face and measure(family, -size, weight)[2].lower() == family.lower():
                drawn = family
                resolved = f'; no font is named "{face}": drawn in {family} bold (room: {got}, Windows\' fallback today)'
            else:
                resolved = f'; no font is named "{face}": Windows draws it in {got}'
        # The room: this face, or a narrower face of the table the release moved one of its styles to.
        room, why = widths, ''
        for st, g in (gst.items() if style_rooms else ()):
            l = lst.get(st)
            if g and l and g[0] == i and l[0] != i and isinstance(l[0], int) and l[0] < len(le.GOG_SLOTS):
                f2, s2, w2 = slots[l[0]]
                if s2 == size:
                    _, w, _ = measure(f2, -s2, w2)
                    if sum(w) < sum(room):
                        room, why = w, f' (room: {f2} {s2} {w2}, as style {st})'
        sub = subs[face]
        if sub != '-':  # '-': keep the English game's own face
            _, _, sgot = measure(sub, cell, weight)
            if sgot.lower() != sub.lower():
                raise SystemExit(f'stand-in "{sub}" not loaded (GDI gave "{sgot}")')
        lines.append(f'# {gface}: {face} {size} px{" bold" if weight == 700 else ""}, cell {cell}{resolved}{why}')
        lines.append(f'{gface} = {drawn}, {size}, {weight} | {sub}, {weight}, {cell} | ' + ' '.join(map(str, room)))
    # Boxes the release resized for its fonts (scenes copy them from .rdata each time they open).
    lines.append('# Layout: a box the release sized for its fonts = the release\'s left top right bottom | the English exe\'s;')
    lines.append('# used only with the release\'s own fonts (the stand-ins keep the English line heights)')
    for va, what in LAYOUT_RECTS:
        mine, eng = loc.code(va, 8), gog.code(va, 8)
        if mine != eng:
            fmt = lambda b: ' '.join(map(str, struct.unpack('<4h', b)))
            lines.append(f'# {what}')
            lines.append(f'Layout {va:#x} = {fmt(mine)} | {fmt(eng)}')
    out = os.path.join(ROOT, 'lang', f'WarBreedsHD_{code}.fonts')
    with open(out, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')
    print(out)


if __name__ == '__main__':
    main()
