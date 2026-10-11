"""Render the smart terrain painter's test grids (*.tpg) to PNG, before and
after side by side, with the game's own tile art (TERRAIN.MHK, via wbm.py).

Usage: terrain_paint_render.py <dir> [--scale S]
  Writes <dir>/<name>.png for every <dir>/<name>.tpg.

.tpg (written by tests/terrain_paint/test_main.cpp): "TPG1", u16 w, u16 h,
w*h u16 tile ids before, w*h u16 after (row-major), then w*h flag bytes:
1 = changed, 2 = seam after (corners disagree), 4 = crystal/plateau difference
only. In the "after" image a seam cell is outlined red and a crystal/plateau
cell cyan; changed cells get a thin yellow tick in their top-left corner.
"""
import glob
import os
import struct
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wbm  # noqa: E402


def load(path):
    d = open(path, "rb").read()
    assert d[:4] == b"TPG1", path
    w, h = struct.unpack_from("<HH", d, 4)
    n = w * h
    before = struct.unpack_from("<%dH" % n, d, 8)
    after = struct.unpack_from("<%dH" % n, d, 8 + 2 * n)
    flags = d[8 + 4 * n:8 + 5 * n]
    return w, h, before, after, flags


def render(w, h, cells, tiles, scale):
    cs = int(64 * scale)
    im = Image.new("RGB", (w * cs, h * cs))
    for y in range(h):
        for x in range(w):
            im.paste(tiles(cells[y * w + x], cs), (x * cs, y * cs))
    return im


def main(argv):
    if not argv:
        print(__doc__)
        return 1
    out = argv[0]
    scale = float(argv[argv.index("--scale") + 1]) if "--scale" in argv else None
    frames, pal = wbm.terrain_art()
    flat = [c for rgb in pal for c in rgb]
    cache = {}

    def tile(v, cs):
        key = (v, cs)
        if key not in cache:
            if v < len(frames) and frames[v][0] == 64 and frames[v][1] == 64:
                t = Image.frombytes("P", (64, 64), frames[v][3])
                t.putpalette(flat)
                t = t.convert("RGB")
            else:
                t = Image.new("RGB", (64, 64), (255, 0, 255))
            cache[key] = t.resize((cs, cs), Image.LANCZOS) if cs != 64 else t
        return cache[key]

    for path in sorted(glob.glob(os.path.join(out, "*.tpg"))):
        w, h, before, after, flags = load(path)
        s = scale or (0.5 if max(w, h) <= 48 else 0.25)
        cs = int(64 * s)
        a = render(w, h, before, tile, s)
        b = render(w, h, after, tile, s)
        dr = ImageDraw.Draw(b)
        for y in range(h):
            for x in range(w):
                f = flags[y * w + x]
                x0, y0 = x * cs, y * cs
                if f & 1:
                    dr.line((x0, y0, x0 + max(2, cs // 6), y0), fill=(255, 230, 0))
                if f & 2:
                    dr.rectangle((x0, y0, x0 + cs - 1, y0 + cs - 1), outline=(255, 0, 0), width=2)
                elif f & 4:
                    dr.rectangle((x0, y0, x0 + cs - 1, y0 + cs - 1), outline=(0, 220, 255), width=1)
        gap = 16
        im = Image.new("RGB", (a.width * 2 + gap, a.height + 24), (40, 40, 40))
        im.paste(a, (0, 24))
        im.paste(b, (a.width + gap, 24))
        dr = ImageDraw.Draw(im)
        name = os.path.splitext(os.path.basename(path))[0]
        dr.text((4, 6), name + ": before", fill=(255, 255, 255))
        dr.text((a.width + gap + 4, 6), "after", fill=(255, 255, 255))
        png = os.path.splitext(path)[0] + ".png"
        im.save(png)
        print("wrote", png)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
