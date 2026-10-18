"""Check a WarBreeds HD language file against your own copy of the game.

Usage: python tools/lang_check.py <game dir> <lang file> [--all]

Reads the game's string table from <game dir>\\WARBREED.EXE and glyph widths from
its .FON files (nothing of the game is stored by this tool), then checks every
entry of the language file:
  - syntax, and keys that exist (game ids; [patch] keys must be a string literal
    somewhere in src/; protected ids are refused);
  - placeholders: {1}..{9} and printf specifiers (%d, %s, ...) as in the English;
  - characters: every one must have a Mac Roman byte (the game's fonts), and
    no guillemets (Orbitron has none: use plain quotes);
  - [names] keys: thing and gizmo names in the game's GAMEPLAY.DAT;
  - hard byte limits: 1023 for any game string; 59 where the English fits a
    text button (59 or less); the 'Squad' word 8; unit names with the info
    panel's prefix 39; briefings within the briefing's 7 pages;
  - width against the English in the game's font (WBMd14). Only a value
    marked ~ (a tight spot) is squeezed into the English's room when drawn:
    one past what the squeeze can do (about 1.35x) is TOO WIDE and must be
    reworded. Unmarked values wider than their English are listed with --all,
    to check on screen. A language with a .fonts file beside it (its release's
    fonts, e.g. French) is drawn in those, every line fitted to the width it had
    in that release, so this check is skipped for it; the game logs any line
    that still can't fit ("STILL TOO WIDE").
Exit status 1 if anything is an error.
"""
import os
import re
import struct
import sys

import pefile

sys.stdout.reconfigure(encoding='utf-8')

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROTECTED = set(range(80, 92)) | {590, 591, 606}
BRIEFINGS = set(range(301, 349)) | set(range(397, 404))
FIT, SQUEEZE_MAX = 1.10, 1.35


def string_table(exe):
    pe = pefile.PE(exe)
    out = {}
    for t in pe.DIRECTORY_ENTRY_RESOURCE.entries:
        if t.id != 6:
            continue
        for e in t.directory.entries:
            for lang in e.directory.entries:
                d = pe.get_data(lang.data.struct.OffsetToData, lang.data.struct.Size)
                p = 0
                for i in range(16):
                    n = struct.unpack_from('<H', d, p)[0]
                    p += 2
                    if n:
                        out[(e.id - 1) * 16 + i] = d[p:p + 2 * n].decode('utf-16le')
                    p += 2 * n
    return out


def fon_widths(path):
    """Glyph advance widths (byte -> px) of the first FNT in a .FON."""
    d = open(path, 'rb').read()
    ne = struct.unpack_from('<I', d, 0x3c)[0]
    rtab = ne + struct.unpack_from('<H', d, ne + 0x24)[0]
    shift = struct.unpack_from('<H', d, rtab)[0]
    p = rtab + 2
    while True:
        typ, count = struct.unpack_from('<HH', d, p)
        if typ == 0:
            raise ValueError('no font in ' + path)
        p += 8
        for _ in range(count):
            off = struct.unpack_from('<H', d, p)[0] << shift
            p += 12
            if typ == 0x8008:
                version = struct.unpack_from('<H', d, off)[0]
                first, last = d[off + 0x5f], d[off + 0x60]
                table, step = (off + 0x76, 4) if version == 0x200 else (off + 0x94, 6)
                return {first + i: struct.unpack_from('<H', d, table + i * step)[0] for i in range(last - first + 1)}


def gameplay_names(path):
    """The thing and gizmo names in GAMEPLAY.DAT ([names] keys)."""
    d = open(path, 'rb').read()
    out = set()
    for off, count in ((0x3c2, 74), (0x2e8f, 25)):
        for i in range(count):
            n = d[off + 20 * i:off + 20 * i + 20].split(b'\0')[0].decode('cp1252', 'replace')
            if n:
                out.add(n)
    return out


def width(widths, text):
    return sum(widths.get(b, 0) for b in text.encode('mac_roman', 'replace'))


def placeholders(s):
    return sorted(re.findall(r'\{\d\}', s)), re.findall(r'%[-+ #0]*\d*(?:\.\d+)?l?[dsuxXcf%]', s)


def source_literals():
    lits = set()
    for base, _, files in os.walk(os.path.join(ROOT, 'src')):
        for f in files:
            if f.endswith(('.cpp', '.h')):
                text = open(os.path.join(base, f), encoding='utf-8', errors='replace').read()
                text = re.sub(r'"\s*\n\s*"', '', text)  # adjacent literals are one string
                for m in re.finditer(r'"((?:[^"\\\n]|\\.)*)"', text):
                    lits.add(m.group(1).replace('\\"', '"').replace('\\\\', '\\'))
    return lits


def parse(path):
    entries, errors = [], []
    section = ''
    for no, raw in enumerate(open(path, encoding='utf-8-sig'), 1):
        line = raw.strip()
        if not line or line.startswith('#'):
            continue
        if line.startswith('['):
            section = line[1:line.index(']')]
            if section not in ('game', 'patch', 'maps', 'messages', 'names'):
                errors.append(f'line {no}: unknown section [{section}]')
            continue
        if '=' not in line:
            errors.append(f'line {no}: no "=": {line}')
            continue
        key, value = (x.strip() for x in line.split('=', 1))
        fit = value.startswith('~')
        if fit:
            value = value[1:].strip()
        entries.append((no, section, key, value.replace('\\n', '\r\n'), fit))
    return entries, errors


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    game, path = sys.argv[1], sys.argv[2]
    verbose = '--all' in sys.argv
    table = string_table(os.path.join(game, 'WARBREED.EXE'))
    game_names = gameplay_names(os.path.join(game, 'DATA', 'GAMEPLAY.DAT'))
    font = fon_widths(os.path.join(game, 'WBMD14.FON'))
    lits = source_literals()
    entries, errors = parse(path)
    release_fonts = os.path.exists(os.path.splitext(path)[0] + '.fonts')
    warnings, squeezed = [], []
    language = None
    done = 0
    for no, section, key, value, fit in entries:
        if not section:
            if key == 'Language':
                language = value
            continue
        if not value:
            continue
        done += 1
        where = f'line {no} [{section}] {key}'
        try:
            value.encode('mac_roman')
        except UnicodeEncodeError as e:
            errors.append(f'{where}: "{value[e.start:e.end]}" has no Mac Roman byte (the game fonts can\'t draw it)')
        if '«' in value or '»' in value:
            errors.append(f'{where}: guillemets: Orbitron has none, use plain quotes')
        english = None
        if section == 'game':
            if key.startswith('0x'):
                english = '(a .data literal)'
            elif not key.isdigit() or int(key) not in table:
                errors.append(f'{where}: no such string id')
                continue
            elif int(key) in PROTECTED:
                errors.append(f'{where}: protected id (the game uses it as a key, not as text)')
                continue
            else:
                english = table[int(key)]
        elif section == 'names':
            if key not in game_names:
                errors.append(f'{where}: not a thing or gizmo name in GAMEPLAY.DAT')
        elif section == 'patch':
            english = key.split('|', 1)[-1]
            if english not in lits:
                warnings.append(f'{where}: key not found as a string in src/ (typo, or text since changed?)')
        if english and not english.startswith('(a '):
            if placeholders(english) != placeholders(value):
                errors.append(f'{where}: placeholders differ from the English {placeholders(english)} vs {placeholders(value)}')
            nbytes = len(value.encode('mac_roman', 'replace'))
            if nbytes > 1023:
                errors.append(f'{where}: {nbytes} bytes (the game copies at most 1023)')
            # Buttons are one line; the patch's own long texts wrap over several rows.
            if section == 'game' and len(english) <= 59 < nbytes and '\n' not in english:
                errors.append(f'{where}: {nbytes} bytes; a text button holds 59')
            if section == 'game' and key == '51' and nbytes > 8:
                errors.append(f'{where}: the Squad word must be 8 bytes or less')
            if section == 'game' and key.isdigit() and int(key) in BRIEFINGS:
                lines = sum(max(1, -(-width(font, part) // 357)) for part in value.split('\r\n'))
                if lines > 7 * 16:
                    errors.append(f'{where}: about {lines} lines; the briefing shows 7 pages of 16')
            if '\r\n' not in english and len(english) <= 80 and not release_fonts:
                we, wv = width(font, english), width(font, value)
                if fit and we and wv > we * SQUEEZE_MAX:
                    errors.append(f'{where}: TOO WIDE for its spot: {wv} px vs English {we} px ({wv / we:.2f}x); reword')
                elif we and wv > we * FIT:
                    squeezed.append(f'{where}: {wv} px vs English {we} px ({wv / we:.2f}x)'
                                    + (': squeezed to fit' if fit else ': wider; check on screen'))
        if section == 'game' and key.isdigit() and 132 <= int(key) <= 189:
            prefix = max(len(table.get(i, '')) for i in (48, 49, 50))
            if prefix + len(value.encode('mac_roman', 'replace')) > 39:
                warnings.append(f'{where}: with the info panel\'s prefix this may exceed 39 bytes')
    if not language:
        errors.append('no "Language = ..." line at the top')
    translatable = [i for i in table if i not in PROTECTED]
    have = {int(k) for _, s, k, v, _ in entries if s == 'game' and k.isdigit() and v}
    print(f'{os.path.basename(path)}: {language}: {done} entries; game strings {len(have)} of {len(translatable)}')
    for w in warnings:
        print('warning:', w)
    if release_fonts:
        print('widths: drawn in the release fonts (' + os.path.basename(os.path.splitext(path)[0]) + '.fonts), not checked here')
    if verbose:
        for q in squeezed:
            print('fit:', q)
    else:
        print(f'{len(squeezed)} entries are wider than their English (--all lists them)')
    for e in errors:
        print('ERROR:', e)
    print('OK' if not errors else f'{len(errors)} errors')
    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main())
