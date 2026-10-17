"""Compare stand-in fonts with a localized release's own, drawn the way the game draws.

Usage: python -I tools/fontsheet.py <out.png> <spec.json>

spec.json: {"fonts": [ttf paths to load privately],
            "text": {"<size>": "sample string", ...},       optional default samples
            "rows": [{"face": "BellGothic BT", "size": 18, "bold": false,
                      "label": "...", "text": "..."}, ...]}
Each row is drawn with GDI (CreateFontIndirect, lfHeight = -size as the game does,
NONANTIALIASED_QUALITY) into a white-on-black strip, labelled with its width in
pixels, so the sheet shows the glyphs and spacing the game would show. Rows whose
text matches a row above also print their width as a ratio of it.
"""
import ctypes
import json
import sys
from ctypes import wintypes

from PIL import Image, ImageDraw, ImageFont

gdi, user = ctypes.windll.gdi32, ctypes.windll.user32
gdi.AddFontResourceExW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, ctypes.c_void_p]
gdi.TextOutW.argtypes = [wintypes.HDC, ctypes.c_int, ctypes.c_int, wintypes.LPCWSTR, ctypes.c_int]
gdi.GetTextExtentPoint32W.argtypes = [wintypes.HDC, wintypes.LPCWSTR, ctypes.c_int, ctypes.POINTER(wintypes.SIZE)]
gdi.CreateFontIndirectW.argtypes = [ctypes.c_void_p]
gdi.CreateFontIndirectW.restype = wintypes.HFONT
gdi.SelectObject.argtypes = [wintypes.HDC, wintypes.HGDIOBJ]
gdi.SelectObject.restype = wintypes.HGDIOBJ
for _f in ("SetTextColor", "SetBkMode", "GetTextFaceW", "DeleteObject", "DeleteDC"):
    getattr(gdi, _f).argtypes = [wintypes.HANDLE] + {"SetTextColor": [wintypes.DWORD], "SetBkMode": [ctypes.c_int],
                                                     "GetTextFaceW": [ctypes.c_int, wintypes.LPWSTR]}.get(_f, [])
gdi.CreateCompatibleDC.restype = wintypes.HDC
gdi.CreateDIBSection.restype = wintypes.HBITMAP
gdi.CreateDIBSection.argtypes = [wintypes.HDC, ctypes.c_void_p, wintypes.UINT, ctypes.POINTER(ctypes.c_void_p),
                                 wintypes.HANDLE, wintypes.DWORD]


class LOGFONTW(ctypes.Structure):
    _fields_ = [('lfHeight', wintypes.LONG), ('lfWidth', wintypes.LONG), ('lfEscapement', wintypes.LONG),
                ('lfOrientation', wintypes.LONG), ('lfWeight', wintypes.LONG), ('lfItalic', wintypes.BYTE),
                ('lfUnderline', wintypes.BYTE), ('lfStrikeOut', wintypes.BYTE), ('lfCharSet', wintypes.BYTE),
                ('lfOutPrecision', wintypes.BYTE), ('lfClipPrecision', wintypes.BYTE),
                ('lfQuality', wintypes.BYTE), ('lfPitchAndFamily', wintypes.BYTE),
                ('lfFaceName', wintypes.WCHAR * 32)]


class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [('biSize', wintypes.DWORD), ('biWidth', wintypes.LONG), ('biHeight', wintypes.LONG),
                ('biPlanes', wintypes.WORD), ('biBitCount', wintypes.WORD), ('biCompression', wintypes.DWORD),
                ('biSizeImage', wintypes.DWORD), ('biXPelsPerMeter', wintypes.LONG),
                ('biYPelsPerMeter', wintypes.LONG), ('biClrUsed', wintypes.DWORD), ('biClrImportant', wintypes.DWORD)]


def draw(face, size, bold, text, w=1200, h=None):
    """(PIL image of the text in white on black, its width)."""
    h = h or size * 2
    dc = gdi.CreateCompatibleDC(None)
    bi = BITMAPINFOHEADER(ctypes.sizeof(BITMAPINFOHEADER), w, -h, 1, 32, 0, 0, 0, 0, 0, 0)
    bits = ctypes.c_void_p()
    bmp = gdi.CreateDIBSection(dc, ctypes.byref(bi), 0, ctypes.byref(bits), None, 0)
    gdi.SelectObject(dc, bmp)
    lf = LOGFONTW()
    lf.lfHeight = -size  # as the game: character height in pixels (0: a bitmap face at its own size)
    lf.lfWeight = 700 if bold else 400
    lf.lfQuality = 3     # NONANTIALIASED_QUALITY: the game's frame is 8-bit
    lf.lfFaceName = face
    font = gdi.CreateFontIndirectW(ctypes.byref(lf))
    gdi.SelectObject(dc, font)
    gdi.SetTextColor(dc, 0xffffff)
    gdi.SetBkMode(dc, 1)
    gdi.TextOutW(dc, 0, size // 3, text, len(text))
    sz = wintypes.SIZE()
    gdi.GetTextExtentPoint32W(dc, text, len(text), ctypes.byref(sz))
    real = ctypes.create_unicode_buffer(64)
    gdi.GetTextFaceW(dc, 64, real)
    buf = ctypes.string_at(bits, w * h * 4)
    img = Image.frombuffer('RGBX', (w, h), buf, 'raw', 'BGRX', 0, 1).convert('RGB')
    gdi.DeleteObject(font)
    gdi.DeleteObject(bmp)
    gdi.DeleteDC(dc)
    return img.crop((0, 0, min(w, sz.cx + 4), h)), sz.cx, real.value


def main():
    out, spec = sys.argv[1], json.load(open(sys.argv[2], encoding='utf-8'))
    for f in spec.get('fonts', []):
        if not gdi.AddFontResourceExW(f, 0x10, None):  # FR_PRIVATE
            print('not loaded:', f)
    label = ImageFont.truetype('arial.ttf', 12)
    strips, first = [], {}
    for r in spec['rows']:
        if 'heading' in r:
            strips.append((None, r['heading']))
            continue
        text = r.get('text') or spec['text'][str(r['size'])]
        img, width, real = draw(r['face'], r['size'], r.get('bold'), text)
        ratio = ''
        if text in first:
            ratio = f'  {width / first[text]:.2f}x'
        else:
            first[text] = width
        note = '' if real.lower() == r['face'].lower() else f'  (GDI gave "{real}")'
        strips.append((img, f'{r.get("label", r["face"])} {r["size"]}{" bold" if r.get("bold") else ""}: '
                            f'{width} px{ratio}{note}'))
    W = 20 + max(max((s[0].width for s in strips if s[0]), default=0), 600)
    H = sum((s[0].height + 18 if s[0] else 26) for s in strips) + 10
    sheet = Image.new('RGB', (W, H), (16, 16, 24))
    d = ImageDraw.Draw(sheet)
    y = 6
    for img, text in strips:
        if img is None:
            d.text((10, y + 8), text, font=ImageFont.truetype('arialbd.ttf', 14), fill=(255, 200, 90))
            y += 26
            continue
        d.text((10, y), text, font=label, fill=(150, 150, 160))
        sheet.paste(img, (10, y + 14))
        y += img.height + 18
    sheet.save(out)
    print(out)


if __name__ == '__main__':
    main()
