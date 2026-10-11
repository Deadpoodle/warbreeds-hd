"""Turn one Gemini outpainting into centred widescreen menu backgrounds
(21:9 and 16:9, original 4:3 screen in the middle).

  menuwide.py canvas <folder>             -> gemini/centre-canvas.png: a 16:9 canvas with the
                                             original scaled to fill the middle of a 21:9 band,
                                             grey all round (upload this to Gemini)
  menuwide.py final  <folder> <gemini>    -> gemini/<Scene>-21x9.png (3440x1440) and
                                             gemini/<Scene>.png (2560x1440, the middle of it)
  menuwide.py seams  <folder>             -> gemini/seamfix-canvas.png: the 16:9 middle with grey
                                             bands just outside both edges of the original (upload
                                             to Gemini if the joins show)
  menuwide.py fixseams <folder> <gemini>  -> repaints those bands into both outputs
  menuwide.py patchcanvas <folder> <x0> <x1>       -> gemini/patch-canvas.png: the 16:9 window with a
                                                      grey band over 21:9 columns x0..x1 (a leftover line)
  menuwide.py patch <folder> <gemini> <x0> <x1>    -> paints that band back in, colour-matched on both sides

<folder> is art/menus/<nn-screen>; the scene name is read from its README.

Geometry: the 21:9 result is 3440x1440 with the original as 1920x1440 at x=760.
On the 2560x1440 canvas that whole 21:9 band is scaled by 2560/3440, so the
original is 1429x1072 at (566, 184) and the band is 2560x1072 at y=184."""
import os
import re
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fitpart import fit  # noqa: E402

CW, CH = 2560, 1440            # canvas (Gemini's 16:9 output)
FW, FH = 3440, 1440            # final 21:9
K = CW / FW                    # canvas px per final px
OW, OH = round(1920 * K), round(1440 * K)
OX, OY = (CW - OW) // 2, (CH - OH) // 2
BAND = (0, OY, CW, OY + OH)    # the 21:9 band on the canvas
SEAMW = 160                    # width of the bands repainted over the joins
# Original strip the new sides are colour-graded against (px at 1920 wide);
# narrow where UI panels sit near the edge, wider where the edge column is unrepresentative.
GRADE_STRIP = {"SMultiSetup": 240}


def scene_of(folder):
    m = re.search(r"\(`([\w~]+)`\)", open(os.path.join(folder, "README.md"), encoding="utf-8").read())
    return m.group(1)


def grade_sides(F, big, x0=760, strip=32, strength=0.75):
    """Match the colour and contrast of each new side to the original strip
    beside it (per channel mean/std), so the sides read as the same painting."""
    for side in ("left", "right"):
        if side == "left":
            o, n, region = big[:, :strip], F[:, x0 - strip:x0], slice(0, x0)
        else:
            o, n, region = big[:, -strip:], F[:, x0 + 1920:x0 + 1920 + strip], slice(x0 + 1920, F.shape[1])
        om, osd = o.reshape(-1, 3).mean(0), o.reshape(-1, 3).std(0) + 1e-3
        nm, nsd = n.reshape(-1, 3).mean(0), n.reshape(-1, 3).std(0) + 1e-3
        graded = (F[:, region] - nm) / nsd * osd + om
        F[:, region] = F[:, region] * (1 - strength) + graded * strength


def ease_edges(F, big, x0=760, span=200, probe=12):
    """Shift the new art's colours so they meet the original's edge exactly,
    fading the correction out over `span` px (per row, smoothed vertically)."""
    for seam, d in ((x0 + 1920, +1), (x0, -1)):
        o = big[:, -probe:] if d > 0 else big[:, :probe]
        gs = F[:, seam:seam + probe] if d > 0 else F[:, seam - probe:seam]
        diff = (o - gs).mean(axis=1)
        diff = np.stack([np.convolve(diff[:, c], np.ones(41) / 41, mode="same") for c in range(3)], axis=1)
        for i in range(span):
            col = seam + i if d > 0 else seam - 1 - i
            F[:, col] += diff * (1 - i / span) ** 2


def main():
    cmd, folder = sys.argv[1], sys.argv[2]
    g = os.path.join(folder, "gemini")
    os.makedirs(g, exist_ok=True)
    scene = scene_of(folder)
    orig = Image.open(os.path.join(folder, "original-640x480.png")).convert("RGB")
    if cmd == "canvas":
        c = Image.new("RGB", (CW, CH), (128, 128, 128))
        c.paste(orig.resize((OW, OH), Image.LANCZOS), (OX, OY))
        c.save(os.path.join(g, "centre-canvas.png"))
    elif cmd == "final":
        gen = Image.open(sys.argv[3]).convert("RGB")
        fitted = fit(orig.resize((OW, OH), Image.LANCZOS), (OX, OY, OW, OH), gen, (CW, CH))
        band = fitted.crop(BAND).resize((FW, FH), Image.LANCZOS)
        F = np.asarray(band, dtype=np.float32)
        big = np.asarray(orig.resize((1920, 1440), Image.LANCZOS), dtype=np.float32)
        x0 = 760
        grade_sides(F, np.asarray(orig.resize((1920, 1440), Image.LANCZOS), dtype=np.float32),
                    strip=GRADE_STRIP.get(scene, 32))
        # Ease the generated colours towards the original along both seams.
        for seam, d in ((x0 + 1920, +1), (x0, -1)):
            o = big[:, -48:] if d > 0 else big[:, :48]
            gs = F[:, seam - 48:seam] if d > 0 else F[:, seam:seam + 48]
            diff = (o - gs).mean(axis=1)
            diff = np.stack([np.convolve(diff[:, c], np.ones(31) / 31, mode="same") for c in range(3)], axis=1)
            for i in range(160):
                col = seam + i if d > 0 else seam - 1 - i
                F[:, col] += diff * (1 - i / 160)
        F[:, x0:x0 + 1920] = big
        out = Image.fromarray(np.clip(F, 0, 255).astype(np.uint8))
        out.save(os.path.join(g, f"{scene}-21x9.png"))
        out.crop(((FW - 2560) // 2, 0, (FW + 2560) // 2, FH)).save(os.path.join(g, f"{scene}.png"))
    elif cmd in ("seams", "fixseams"):
        full = Image.open(os.path.join(g, f"{scene}-21x9.png")).convert("RGB")
        x16 = (FW - 2560) // 2  # the 16:9 window inside the 21:9 image
        bands = [(760 - SEAMW, 760), (760 + 1920, 760 + 1920 + SEAMW)]
        if cmd == "seams":
            win = full.crop((x16, 0, x16 + 2560, FH))
            for a, b in bands:
                win.paste(Image.new("RGB", (b - a, FH), (128, 128, 128)), (a - x16, 0))
            win.save(os.path.join(g, "seamfix-canvas.png"))
        else:
            win = full.crop((x16, 0, x16 + 2560, FH))
            known = win.crop((760 - x16, 0, 760 - x16 + 1920, FH))
            fitted = np.asarray(fit(known, (760 - x16, 0, 1920, FH), Image.open(sys.argv[3]).convert("RGB"),
                                    (2560, FH)), dtype=np.float32)
            out = np.asarray(full, dtype=np.float32).copy()
            big = np.asarray(orig.resize((1920, 1440), Image.LANCZOS), dtype=np.float32)
            K = 41  # vertical smoothing of the per-row correction (less = streaks)

            def smooth(v):
                return np.stack([np.convolve(v[:, c], np.ones(K) / K, mode="same") for c in range(3)], axis=1)

            # Gemini's repair image is consistent across its whole 16:9 window,
            # so use it for both sides inside that window: corrected per row to
            # meet the original exactly at the frame edges (correction fading
            # out over 240 px), and crossfaded into the outer 21:9 sides over
            # 96 px at the window's ends.
            fade, xf = 240, 96
            for side in ("left", "right"):
                if side == "left":
                    lo, hi = x16, 760
                    edge_o, edge_g = big[:, 0:4].mean(axis=1), fitted[:, hi - 4 - x16:hi - x16].mean(axis=1)
                else:
                    lo, hi = 760 + 1920, x16 + 2560
                    edge_o, edge_g = big[:, -4:].mean(axis=1), fitted[:, lo - x16:lo + 4 - x16].mean(axis=1)
                d = smooth(edge_o - edge_g)
                for x in range(lo, hi):
                    dist = hi - 1 - x if side == "left" else x - lo  # px from the frame edge
                    w_corr = max(0.0, 1 - dist / fade) ** 2
                    px = fitted[:, x - x16] + d * w_corr
                    to_end = x - lo if side == "left" else hi - 1 - x  # px from the window's outer end
                    w = min(1.0, to_end / xf)
                    out[:, x] = out[:, x] * (1 - w) + px * w
            res = Image.fromarray(np.clip(out, 0, 255).astype(np.uint8))
            res.save(os.path.join(g, f"{scene}-21x9.png"))
            res.crop((x16, 0, x16 + 2560, FH)).save(os.path.join(g, f"{scene}.png"))
    elif cmd in ("patchcanvas", "patch"):
        full = Image.open(os.path.join(g, f"{scene}-21x9.png")).convert("RGB")
        x16 = (FW - 2560) // 2
        x0, x1 = int(sys.argv[-2]), int(sys.argv[-1])
        win = full.crop((x16, 0, x16 + 2560, FH))
        if cmd == "patchcanvas":
            win.paste(Image.new("RGB", (x1 - x0, FH), (128, 128, 128)), (x0 - x16, 0))
            win.save(os.path.join(g, "patch-canvas.png"))
        else:
            known = win.crop((760 - x16, 0, 760 - x16 + 1920, FH))
            fitted = np.asarray(fit(known, (760 - x16, 0, 1920, FH), Image.open(sys.argv[3]).convert("RGB"),
                                    (2560, FH)), dtype=np.float32)
            out = np.asarray(full, dtype=np.float32).copy()
            K = 41

            def smooth(v):
                return np.stack([np.convolve(v[:, c], np.ones(K) / K, mode="same") for c in range(3)], axis=1)

            band = fitted[:, x0 - x16:x1 - x16].copy()
            d_l = smooth(out[:, x0 - 4:x0].mean(axis=1) - band[:, :4].mean(axis=1))
            d_r = smooth(out[:, x1:x1 + 4].mean(axis=1) - band[:, -4:].mean(axis=1))
            t = np.linspace(0, 1, x1 - x0)[None, :, None]
            out[:, x0:x1] = band + d_l[:, None, :] * (1 - t) + d_r[:, None, :] * t
            res = Image.fromarray(np.clip(out, 0, 255).astype(np.uint8))
            res.save(os.path.join(g, f"{scene}-21x9.png"))
            res.crop((x16, 0, x16 + 2560, FH)).save(os.path.join(g, f"{scene}.png"))
    print(cmd, "done for", scene)


if __name__ == "__main__":
    main()
