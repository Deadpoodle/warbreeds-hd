"""Make "-glow" variants of a widescreen menu background for a hover highlight
that touches the edge of the 4:3 frame (main menu: the top-right hotspot).

The colour change from normal to lit is fitted (3x3 matrix + offset) on the
highlighted region of the original, then applied to the widescreen side next
to it, on the rows the highlight reaches the frame edge, fading out sideways.

Usage: menuglow.py <original.png> <lit-capture.bmp> <x0> <y0> <x1> <y1> <side> <background.png>...
  x0..y1: the highlight's rectangle in 640x480 frame pixels; side: left|right.
  Writes <background>-glow.png next to each background (16:9 or 21:9; the
  original is assumed centred and filling the height)."""
import os
import sys

import numpy as np
from PIL import Image


def main():
    orig_p, lit_p = sys.argv[1], sys.argv[2]
    x0, y0, x1, y1 = (int(v) for v in sys.argv[3:7])
    side = sys.argv[7]
    base = np.asarray(Image.open(orig_p).convert("RGB"), dtype=np.float64)
    lit = np.asarray(Image.open(lit_p).convert("RGB"), dtype=np.float64)
    a = base[y0:y1, x0:x1].reshape(-1, 3)
    b = lit[y0:y1, x0:x1].reshape(-1, 3)
    A = np.hstack([a, np.ones((len(a), 1))])
    M, *_ = np.linalg.lstsq(A, b, rcond=None)  # 4x3
    err = np.abs(A @ M - b).mean()
    print(f"colour fit error {err:.1f} (0-255)")
    # Rows where the highlight reaches the frame edge (strength 0..1).
    cols = slice(632, 640) if side == "right" else slice(0, 8)
    d = np.abs(lit[:, cols] - base[:, cols]).max(axis=2).mean(axis=1)
    edge = np.clip(d / max(1.0, np.percentile(d[y0:y1], 75)), 0, 1)
    edge = np.convolve(edge, np.ones(9) / 9, mode="same")
    for bg_p in sys.argv[8:]:
        bg = np.asarray(Image.open(bg_p).convert("RGB"), dtype=np.float64)
        H, W = bg.shape[:2]
        s = H / 480.0
        fw = 640 * s
        fx0 = (W - fw) / 2
        out = bg.copy()
        span = 260 * s  # fade distance (frame px * scale)
        ys = np.arange(H)
        e = np.interp(ys / s, np.arange(480), edge)
        if side == "right":
            xs = np.arange(int(fx0 + fw), W)
            t = (xs - (fx0 + fw)) / span
        else:
            xs = np.arange(0, int(fx0))
            t = (fx0 - 1 - xs) / span
        fade = np.clip(1 - t, 0, 1) ** 1.5
        region = bg[:, xs]
        # The lit art is a soft haze over the scene: blend towards the lit
        # frame's own edge colour on each row (smoothed), strongest at the
        # edge and fading outwards, plus the fitted tint underneath.
        cols_px = lit[:, cols].mean(axis=1)
        k = 15
        cols_px = np.stack([np.convolve(cols_px[:, c], np.ones(k) / k, mode="same") for c in range(3)], axis=1)
        haze = np.stack([np.interp(ys / s, np.arange(480), cols_px[:, c]) for c in range(3)], axis=1)
        tinted = (np.hstack([region.reshape(-1, 3), np.ones((region.size // 3, 1))]) @ M).reshape(region.shape)
        w = (e[:, None] * fade[None, :])[..., None]
        a_h = w
        mixed = tinted * (1 - a_h) + haze[:, None, :] * a_h
        out[:, xs] = region * (1 - w) + mixed * w
        root, ext = os.path.splitext(bg_p)
        dst = root + "-glow" + ext
        Image.fromarray(np.clip(out, 0, 255).astype(np.uint8)).save(dst)
        print("wrote", dst)


if __name__ == "__main__":
    main()
