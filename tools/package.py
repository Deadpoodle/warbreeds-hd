"""Build the shareable WarBreeds HD patch: dist/WarBreedsHD-<version>/ and its .zip.

Usage: package.py [--art DIR]
  --art DIR   folder of widescreen menu PNGs (default: testgame/Menus); each is
              saved as JPEG quality 95 without chroma subsampling (about a
              third of the size; no visible change at the 4:3 seams).

Takes build/wsock32.dll (run build.sh first), the fonts in third_party/fonts/
(Orbitron, and Roboto Condensed for the French menus), release/README.txt and release/LICENSE.txt
(CRLF for Notepad), lang/ as Lang\, and docs/controls.md made into a
standalone Controls.html (the zip's offline copy; GitHub shows the .md).
The version comes from src/version.h."""
import os
import re
import shutil
import sys
import zipfile

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

PAGE_STYLE = """
:root { color-scheme: light dark; --ink: #1d1b22; --muted: #5d5868; --line: #d9d4e2; --surface: #f6f3fa;
        --bg: #ffffff; --accent: #8a3fd1; }
@media (prefers-color-scheme: dark) {
  :root { --ink: #ece8f3; --muted: #a59fb3; --line: #3a3546; --surface: #221f2a; --bg: #16141b; --accent: #c89bff; }
}
body { margin: 0; background: var(--bg); color: var(--ink);
       font: 16px/1.55 system-ui, -apple-system, "Segoe UI", sans-serif; }
main { max-width: 900px; margin: 0 auto; padding: 24px 16px 48px; }
h1 { font-size: 30px; margin: 8px 0 4px; } h2 { margin-top: 36px; border-bottom: 1px solid var(--line); }
a { color: var(--accent); } blockquote { margin: 12px 0; padding: 8px 14px; border-left: 3px solid var(--accent);
  background: var(--surface); color: var(--muted); }
table { border-collapse: collapse; width: 100%; margin: 12px 0; display: block; overflow-x: auto; }
th, td { border: 1px solid var(--line); padding: 6px 10px; text-align: left; vertical-align: top; }
th { background: var(--surface); } td:first-child { white-space: nowrap; }
kbd { display: inline-block; min-width: 1.4em; padding: 0 6px; border: 1px solid var(--line); border-bottom-width: 2px;
      border-radius: 5px; background: var(--surface); font: 600 13px/1.6 ui-monospace, Consolas, monospace;
      text-align: center; }
code { font: 14px ui-monospace, Consolas, monospace; background: var(--surface); padding: 1px 4px; border-radius: 4px; }
hr { border: 0; border-top: 1px solid var(--line); margin: 32px 0 12px; }
@media (max-width: 480px) { td:first-child { white-space: normal; } }
"""


def md_inline(t):
    """The inline Markdown docs/controls.md uses; <kbd> and entities pass through."""
    t = re.sub(r'`([^`]+)`', lambda m: '<code>' + m.group(1).replace('<', '&lt;') + '</code>', t)
    t = re.sub(r'\*\*(.+?)\*\*', r'<strong>\1</strong>', t)
    t = re.sub(r'(?<![*\w])\*(?!\s)(.+?)(?<!\s)\*(?![*\w])', r'<em>\1</em>', t)
    t = re.sub(r'\[([^\]]+)\]\(([^)]+)\)', r'<a href="\2">\1</a>', t)
    return t.replace('\\|', '|')


def md_to_html(md):
    """Headings, paragraphs, quotes, numbered lists, tables and rules: enough for
    docs/controls.md. Headings get GitHub's anchors so the contents links work."""
    out, lines, i = [], md.split('\n'), 0
    while i < len(lines):
        line = lines[i]
        if not line.strip():
            i += 1
        elif line.startswith('#'):
            level = len(line) - len(line.lstrip('#'))
            text = line[level:].strip()
            anchor = re.sub(r'[^a-z0-9 -]', '', text.lower()).replace(' ', '-')
            out.append(f'<h{level} id="{anchor}">{md_inline(text)}</h{level}>')
            i += 1
        elif line.startswith('|'):
            rows = []
            while i < len(lines) and lines[i].startswith('|'):
                rows.append([c.strip() for c in re.split(r'(?<!\\)\|', lines[i].strip())[1:-1]])
                i += 1
            head, body = rows[0], rows[2:]
            out.append('<table><thead><tr>' + ''.join(f'<th>{md_inline(c)}</th>' for c in head) + '</tr></thead><tbody>')
            out += ['<tr>' + ''.join(f'<td>{md_inline(c)}</td>' for c in r) + '</tr>' for r in body]
            out.append('</tbody></table>')
        elif line.startswith('> '):
            out.append(f'<blockquote>{md_inline(line[2:])}</blockquote>')
            i += 1
        elif re.match(r'\d+\. ', line):
            out.append('<ol>')
            while i < len(lines) and re.match(r'\d+\. ', lines[i]):
                out.append('<li>' + md_inline(lines[i].split('. ', 1)[1]) + '</li>')
                i += 1
            out.append('</ol>')
        elif line.strip() == '---':
            out.append('<hr>')
            i += 1
        else:
            out.append(f'<p>{md_inline(line)}</p>')
            i += 1
    return '\n'.join(out)


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
    # Fonts (SIL OFL): each font file, with its licence as OFL-<Family>.txt.
    os.makedirs(os.path.join(out, "Fonts"))
    for family in sorted(os.listdir(os.path.join(ROOT, "third_party", "fonts"))):
        src = os.path.join(ROOT, "third_party", "fonts", family)
        for f in sorted(os.listdir(src)):
            if f.lower().endswith(".ttf"):
                shutil.copy(os.path.join(src, f), os.path.join(out, "Fonts"))
                licence = "OFL-" + re.split(r"[\[\-.]", f)[0] + ".txt"
        shutil.copy(os.path.join(src, "OFL.txt"), os.path.join(out, "Fonts", licence))
    # Languages: the shipped files (UTF-8 as written; .fonts: a language's release
    # fonts) and the how-to (CRLF for Notepad).
    os.makedirs(os.path.join(out, "Lang"))
    for f in sorted(os.listdir(os.path.join(ROOT, "lang"))):
        if f.startswith("WarBreedsHD_") and f.endswith((".txt", ".fonts")) and "_template" not in f and f != "WarBreedsHD_en.txt":
            shutil.copy(os.path.join(ROOT, "lang", f), os.path.join(out, "Lang"))
    text = open(os.path.join(ROOT, "lang", "README.txt"), encoding="utf-8").read().replace("\r\n", "\n")
    open(os.path.join(out, "Lang", "README.txt"), "w", encoding="utf-8", newline="").write(text.replace("\n", "\r\n"))

    for f in sorted(os.listdir(art)):
        if f.lower().endswith(".png"):
            Image.open(os.path.join(art, f)).convert("RGB").save(
                os.path.join(out, "Menus", f[:-4] + ".jpg"), quality=95, subsampling=0, optimize=True)

    for f in ("README.txt", "LICENSE.txt"):
        text = open(os.path.join(ROOT, "release", f), encoding="utf-8").read().replace("\r\n", "\n")
        open(os.path.join(out, f), "w", encoding="utf-8", newline="").write(text.replace("\n", "\r\n"))

    # The controls reference: docs/controls.md (what GitHub shows) as a page that opens offline.
    md = open(os.path.join(ROOT, "docs", "controls.md"), encoding="utf-8").read()
    page = ('<!doctype html>\n<html lang="en">\n<head>\n<meta charset="utf-8">\n'
            '<meta name="viewport" content="width=device-width, initial-scale=1">\n'
            '<title>WarBreeds Controls</title>\n<style>' + PAGE_STYLE + '</style>\n</head>\n<body>\n<main>\n'
            + md_to_html(md) + '\n</main>\n</body>\n</html>\n')
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
