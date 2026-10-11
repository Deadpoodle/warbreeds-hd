"""Fit an AI-outpainted widescreen menu background to the game: find where the
original 4:3 picture sits in the generated image (scale and offset), resample
the generated image onto the target canvas so the original lines up exactly,
put the untouched original back, and ease the generated colours towards the
original's edge so the seam where the game's frame ends is invisible.

Usage: fitwide.py <original 640x480> <generated> <out.png> <width> [left|centre]
       (height 1440; the original covers 1920x1440 at the left or centre)"""
import sys

import numpy as np
from PIL import Image


def locate(orig, gen):
    """Scale s and offset (x, y) of the original inside gen, in units of the
    original's own height (gen resized to height 360)."""
    G = gen.resize((round(gen.width * 360 / gen.height), 360), Image.BILINEAR)
    Ga = np.asarray(G.convert("L"), dtype=np.float32)
    best = None
    for s in np.arange(0.85, 1.151, 0.005):
        h = round(360 * s)
        w = round(h * 4 / 3)
        O = np.asarray(orig.resize((w, h), Image.BILINEAR).convert("L"), dtype=np.float32)
        for y in range(-16, 17, 1):
            for x in range(-16, Ga.shape[1] - w + 17, 2):
                gy0, gx0 = max(0, y), max(0, x)
                gy1, gx1 = min(Ga.shape[0], y + h), min(Ga.shape[1], x + w)
                if gy1 - gy0 < 250 or gx1 - gx0 < 250:
                    continue
                err = float(np.mean(np.abs(Ga[gy0:gy1, gx0:gx1] - O[gy0 - y:gy1 - y, gx0 - x:gx1 - x])))
                if best is None or err < best[0]:
                    best = (err, s, x, y)
    return best


def main():
    orig_path, gen_path, out_path, width = sys.argv[1:5]
    side = sys.argv[5] if len(sys.argv) > 5 else "left"
    W, H = int(width), 1440
    ox = 0 if side == "left" else (W - 1920) // 2  # where the 1920x1440 original goes
    orig = Image.open(orig_path).convert("RGB")
    gen = Image.open(gen_path).convert("RGB")
    err, s, x, y = locate(orig, gen)
    print(f"original found in generated image: scale {s:.3f}, offset {x},{y} (at 360p), error {err:.1f}")
    # canvas pixel (X, Y) -> generated pixel: the original's 360p frame maps to
    # 1920x1440 at ox, so X360 = (X - ox) / 4, then gen360 = X360 * s + x.
    k = gen.height / 360.0
    a = s / 4.0 * k
    affine = (a, 0, (x - ox * s / 4.0) * k, 0, a, y * k)
    fitted = gen.transform((W, H), Image.AFFINE, affine, resample=Image.BICUBIC)
    # Pixels the generated image doesn't cover (edges): repeat its last column/row.
    F = np.asarray(fitted, dtype=np.float32)
    covered_right = int(min(W, (gen.width - (x - ox * s / 4.0) * k) / a)) - 1
    if covered_right < W - 1:
        F[:, covered_right:] = F[:, covered_right - 1:covered_right]
    big = np.asarray(orig.resize((1920, H), Image.LANCZOS), dtype=np.float32)
    # Ease generated colours towards the original along each seam.
    def ease(seam_x, direction):
        band = 48  # compare a strip just inside the original
        if direction > 0:
            o_strip, g_strip = big[:, 1920 - band:1920], F[:, seam_x - band:seam_x]
        else:
            o_strip, g_strip = big[:, :band], F[:, seam_x:seam_x + band]
        diff = (o_strip - g_strip).mean(axis=1)  # per row
        k1 = 31
        diff = np.stack([np.convolve(diff[:, c], np.ones(k1) / k1, mode="same") for c in range(3)], axis=1)
        span = 160
        for i in range(span):
            t = 1 - i / span
            col = seam_x + i if direction > 0 else seam_x - 1 - i
            if 0 <= col < W:
                F[:, col] += diff * t
    if side == "left":
        ease(1920, +1)
    else:
        ease(ox + 1920, +1)
        ease(ox, -1)
    F[:, ox:ox + 1920] = big
    Image.fromarray(np.clip(F, 0, 255).astype(np.uint8)).save(out_path)
    print("wrote", out_path, (W, H))


if __name__ == "__main__":
    main()
