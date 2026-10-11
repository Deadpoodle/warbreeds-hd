"""Find where a known image region sits inside an AI-generated image that was
made from a canvas containing it, and resample the generated image onto that
canvas's coordinates.

Usage as a module: fit(known, known_box, gen, canvas_size) -> fitted image
  known      PIL image of the content we supplied
  known_box  (x, y, w, h) where it sat on the uploaded canvas
  gen        the generated image
  canvas_size (W, H) of the uploaded canvas"""
import numpy as np
from PIL import Image


def fit(known, known_box, gen, canvas_size, search=0.06):
    W, H = canvas_size
    kx, ky, kw, kh = known_box
    # Work at canvas height 360.
    f = 360.0 / H
    Ga = np.asarray(gen.resize((round(gen.width * 360 / gen.height), 360), Image.BILINEAR).convert("L"),
                    dtype=np.float32)
    best = None
    for s in np.arange(1 - search, 1 + search + 1e-9, 0.005):
        # Canvas (at 360) -> generated (at 360): g = c * s * (gen aspect correction) + offset
        sx = s * (Ga.shape[1] / (W * f))
        sy = s
        w = round(kw * f * sx)
        h = round(kh * f * sy)
        K = np.asarray(known.resize((w, h), Image.BILINEAR).convert("L"), dtype=np.float32)
        base_x = kx * f * sx
        base_y = ky * f * sy
        for dy in range(-14, 15):
            for dx in range(-14, 15, 2):
                x = round(base_x + dx)
                y = round(base_y + dy)
                gy0, gx0 = max(0, y), max(0, x)
                gy1, gx1 = min(Ga.shape[0], y + h), min(Ga.shape[1], x + w)
                if gy1 - gy0 < 150 or gx1 - gx0 < 150:
                    continue
                err = float(np.mean(np.abs(Ga[gy0:gy1, gx0:gx1] - K[gy0 - y:gy1 - y, gx0 - x:gx1 - x])))
                if best is None or err < best[0]:
                    best = (err, sx, sy, x - base_x + 0.0, y - base_y + 0.0)
    err, sx, sy, ox, oy = best
    # canvas pixel C -> gen360 = C * f * s + o, gen pixel = gen360 * k
    k = gen.height / 360.0
    affine = (f * sx * k, 0, ox * k, 0, f * sy * k, oy * k)
    print(f"fit: error {err:.1f}, scale {sx / (Ga.shape[1] / (W * f)):.3f}, offset {ox:.1f},{oy:.1f} (360p)")
    out = np.asarray(gen.transform((W, H), Image.AFFINE, affine, resample=Image.BICUBIC), dtype=np.uint8).copy()
    # Pixels the generated image doesn't reach (a few rows/columns at the
    # edges after alignment): copy the nearest covered row/column.
    cover = np.asarray(Image.new("L", gen.size, 255).transform((W, H), Image.AFFINE, affine,
                                                               resample=Image.NEAREST)) > 0
    rows = np.where(cover.any(axis=1))[0]
    cols = np.where(cover.any(axis=0))[0]
    if len(rows) and len(cols):
        r0, r1, c0, c1 = rows[0] + 1, rows[-1] - 1, cols[0] + 1, cols[-1] - 1
        out[:r0] = out[r0]
        out[r1 + 1:] = out[r1]
        out[:, :c0] = out[:, c0:c0 + 1]
        out[:, c1 + 1:] = out[:, c1:c1 + 1]
    return Image.fromarray(out)
