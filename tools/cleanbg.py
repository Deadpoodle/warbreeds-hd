"""Clean (widget-free) menu background: frame 0 of a screen's .MHK bitmap set,
with colours mapped to exactly what the game shows (the game remaps art to its
shared screen palette), learned from a screen capture of the same screen.

Usage: cleanbg.py <file.MHK> <capture.bmp> <out.png>"""
import sys
from collections import Counter, defaultdict

import numpy as np
from PIL import Image

sys.path.insert(0, __file__.rsplit("\\", 1)[0].rsplit("/", 1)[0])
import mhkimg  # noqa: E402

mhk, cap, out = sys.argv[1:4]
res = mhkimg.parse(mhk)
w, h, fmt, px = next(mhkimg.images(res["tBMH"][0][1]))
px = np.frombuffer(px, dtype=np.uint8).reshape(h, w)
ref = np.asarray(Image.open(cap).convert("RGB"))
votes = defaultdict(Counter)
for idx, col in zip(px.reshape(-1), map(tuple, ref.reshape(-1, 3))):
    votes[idx][col] += 1
lut = np.zeros((256, 3), np.uint8)
for idx, c in votes.items():
    lut[idx] = c.most_common(1)[0][0]
clean = lut[px]
print(f"{mhk}: {w}x{h}, {(np.abs(clean.astype(int) - ref.astype(int)).max(axis=2) == 0).mean() * 100:.1f}% identical to the capture")
Image.fromarray(clean).save(out)
