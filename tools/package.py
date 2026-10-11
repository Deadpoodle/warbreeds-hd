"""Build the shareable WarBreeds HD patch: dist/WarBreedsHD-<version>/ and its .zip.

Usage: package.py [--art DIR]
  --art DIR   folder of widescreen menu PNGs (default: testgame/Menus); each is
              saved as JPEG quality 95 without chroma subsampling (about a
              third of the size; no visible change at the 4:3 seams).

Takes build/wsock32.dll (run build.sh first), the Orbitron font from
third_party/fonts/orbitron, release/README.txt and release/LICENSE.txt
(CRLF for Notepad), and docs/controls.html wrapped as a standalone page.
The version comes from src/version.h."""
import os
import re
import shutil
import sys
import zipfile

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    args = sys.argv[1:]
    art = args[args.index("--art") + 1] if "--art" in args else os.path.join(ROOT, "testgame", "Menus")
    version = re.search(r'WBHD_VERSION "([^"]+)"', open(os.path.join(ROOT, "src", "version.h")).read()).group(1)
    name = "WarBreedsHD-" + version
    dist = os.path.join(ROOT, "dist")
    out = os.path.join(dist, name)
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(os.path.join(out, "Menus"))

    shutil.copy(os.path.join(ROOT, "build", "wsock32.dll"), out)
    shutil.copytree(os.path.join(ROOT, "third_party", "fonts", "orbitron"), os.path.join(out, "Fonts"))

    for f in sorted(os.listdir(art)):
        if f.lower().endswith(".png"):
            Image.open(os.path.join(art, f)).convert("RGB").save(
                os.path.join(out, "Menus", f[:-4] + ".jpg"), quality=95, subsampling=0, optimize=True)

    for f in ("README.txt", "LICENSE.txt"):
        text = open(os.path.join(ROOT, "release", f), encoding="utf-8").read().replace("\r\n", "\n")
        open(os.path.join(out, f), "w", encoding="utf-8", newline="").write(text.replace("\n", "\r\n"))

    # The controls page is written for the artifact viewer (no <html>/<head>);
    # give it its own document skeleton.
    html = open(os.path.join(ROOT, "docs", "controls.html"), encoding="utf-8").read()
    cut = html.index("</style>") + len("</style>")
    page = ('<!doctype html>\n<html lang="en">\n<head>\n<meta charset="utf-8">\n'
            '<meta name="viewport" content="width=device-width, initial-scale=1">\n'
            + html[:cut] + "\n<style>body{margin:0}</style>\n</head>\n<body>\n" + html[cut:] + "\n</body>\n</html>\n")
    open(os.path.join(out, "Controls.html"), "w", encoding="utf-8").write(page)

    z = os.path.join(dist, name + ".zip")
    if os.path.exists(z):
        os.remove(z)
    with zipfile.ZipFile(z, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for dp, _, files in os.walk(out):
            for f in sorted(files):
                p = os.path.join(dp, f)
                zf.write(p, os.path.relpath(p, dist))
    print(f"wrote {z} ({os.path.getsize(z) // 1024} KB)")


if __name__ == "__main__":
    main()
