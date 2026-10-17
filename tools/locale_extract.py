"""Read a localized WarBreeds CD and compare its game with the English (GOG/Steam) one.

Usage:
  python -I tools/locale_extract.py <disc> <code> [--gog <game dir>] [--lang <file>] [--names]

  <disc>   the player's own disc: a raw image (.img/.bin, 2352-byte sectors, MODE1
           or MODE2; a .cue or .ccd next to it is also accepted), a 2048-byte .iso,
           or a folder (a mounted disc or a copy of one)
  <code>   language code: files go to re/<code>/ (git-ignored, never published)
  --gog    the English game folder to compare with (default: testgame)
  --lang   also update this language file from the disc: [game] (the string table
           and the three .data literals), [maps] (map names) and [messages]
           (campaign map chat lines). [patch] is kept as it is. A line marked ~
           (a tight spot) keeps its mark.
  --names  also write re/<code>/names.tsv: re/names.tsv's names moved to this exe's
           addresses (for its own Ghidra project; see re/french-version.md)

Writes re/<code>/:
  disc/        WARBREED.EXE, GLOBSFX.MHK (voices), CAMPAIGN/*.WBM, MAPS/*.WBM, FONTS/*, the readmes
  report.md    exe version, string table language, the font table (face, size and
               style per slot), the fonts on the disc, the text styles compared
               with the English exe, string ids added or missing, the literals
  strings.tsv  id, English, translation (for reading; local only)

Nothing of the game is written anywhere else. See re/french-version.md.
"""
import argparse
import os
import re
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import capstone  # noqa: E402
import pefile  # noqa: E402

import lang_check  # noqa: E402
import wbm  # noqa: E402

sys.stdout.reconfigure(encoding='utf-8')
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASE = 0x400000

# The English exe's font table (0x550890, set up at 0x468fd9) in slot order, and
# what lang.cpp's .data literals are (push site, string) in it.
GOG_SLOTS = ['IntReg10', 'IntBld10', 'IntReg12', 'Darwin18', 'IntBld12', 'WBLg24', 'WBMd14', 'WBMd18', 'WBMd24', 'WBSm12']
GOG_LITERALS = [(0x489ed3, 0x51d6d4), (0x4948bd, 0x51d940), (0x49480d, 0x51d96c)]
# ResolveTextStyle (GOG 0x4698c0): "mov eax,[esp+0x14]; mov ecx,[esp+4]; cmp ecx,N;
# mov word [eax],0; ja ..." then a jump table, one case per style.
STYLE_SIG = re.compile(rb'\x8b\x44\x24\x14\x8b\x4c\x24\x04\x83\xf9(.)\x66\xc7\x00\x00\x00\x0f\x87', re.S)
PROTECTED = lang_check.PROTECTED
# Obvious typos in a release's own text, fixed when --lang writes it: (section, key,
# as shipped, fixed). Each is listed in that language's re/*-version.md.
FIXES = {
    'fr': [
        ('game', '33', "-Pas d'excès d'énergie --", "-- Pas d'excès d'énergie --"),
        ('game', '78', 'CGC kalika', 'CGC kelika'),
        ('game', '278', 'Ecraser le jeu?', 'Ecraser le jeu ?'),
        ('game', '283', 'DirextX', 'DirectX'),
        ('game', '413', 'à cours de', 'à court de'),
        ('maps', 'SPMAG5B.WBM', 'Al’est', 'A l’est'),
        ('messages', '3D9F0FA3', 'Savant Yedda trouve.  Ramenez-le a la base.', 'Savant Yedda trouvé.  Ramenez-le à la base.'),
        ('messages', '64613601', 'trouves les autres', 'trouvez les autres'),
        # Shortened to fit: drawn in the English game's own (wider) font, this line ran
        # into "Votre CGC" on the skirmish setup (the release's narrow font fitted it).
        ('game', '416', 'Brouillard de guerre', 'Brouillard'),
        ('game', '543', 'Technologie génétique', 'Tech. génétique'),  # its button, as the English "Genetic Tech"
    ],
}
CS = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)


# --- the disc -------------------------------------------------------------------

class Disc:
    """ISO9660 files of a disc image or a folder, by upper-case path ("WARBREED/WARBREED.EXE")."""

    def __init__(self, path):
        self.folder = path if os.path.isdir(path) else None
        self.files = {}
        if self.folder:
            for base, _, names in os.walk(path):
                for n in names:
                    full = os.path.join(base, n)
                    self.files[os.path.relpath(full, path).replace('\\', '/').upper()] = full
            return
        path = self.image_of(path)
        self.f = open(path, 'rb')
        size = os.path.getsize(path)
        self.sector, self.offset = 2048, 0
        if size % 2352 == 0:
            self.f.seek(16 * 2352)
            raw = self.f.read(2352)
            if raw[:12] == b'\x00' + b'\xff' * 10 + b'\x00':
                self.sector, self.offset = 2352, {1: 16, 2: 24}[raw[15]]
        pvd = self.read_sector(16)
        if pvd[1:6] != b'CD001':
            raise SystemExit(f'{path}: no ISO9660 volume (sector {self.sector}, offset {self.offset})')
        self.label = pvd[40:72].decode('latin-1').strip()
        root = pvd[156:190]
        self.walk(struct.unpack_from('<I', root, 2)[0], struct.unpack_from('<I', root, 10)[0], '')

    @staticmethod
    def image_of(path):
        low = path.lower()
        if low.endswith('.cue'):
            m = re.search(r'FILE\s+"([^"]+)"', open(path, encoding='latin-1').read())
            return os.path.join(os.path.dirname(path), m.group(1))
        if low.endswith('.ccd'):
            return path[:-4] + '.img'
        return path

    def read_sector(self, lba):
        self.f.seek(lba * self.sector + self.offset)
        return self.f.read(2048)

    def walk(self, lba, size, prefix):
        data = b''.join(self.read_sector(lba + i) for i in range((size + 2047) // 2048))
        p = 0
        while p < len(data):
            n = data[p]
            if n == 0:
                p = (p // 2048 + 1) * 2048
                continue
            rec = data[p:p + n]
            p += n
            name = rec[33:33 + rec[32]]
            if name in (b'\0', b'\1'):
                continue
            name = name.decode('latin-1').split(';')[0].upper()
            elba, esize = struct.unpack_from('<I', rec, 2)[0], struct.unpack_from('<I', rec, 10)[0]
            if rec[25] & 2:
                self.walk(elba, esize, prefix + name + '/')
            else:
                self.files[prefix + name] = (elba, esize)

    def read(self, name):
        where = self.files[name]
        if self.folder:
            return open(where, 'rb').read()
        lba, size = where
        return b''.join(self.read_sector(lba + i) for i in range((size + 2047) // 2048))[:size]

    def find(self, pattern):
        return sorted(n for n in self.files if re.fullmatch(pattern, n))


# --- an exe -----------------------------------------------------------------------

class Exe:
    def __init__(self, data):
        self.d = data
        self.pe = pefile.PE(data=data, fast_load=False)
        self.secs = [(s.Name.rstrip(b'\0').decode(), s.VirtualAddress, max(s.Misc_VirtualSize, s.SizeOfRawData),
                      s.PointerToRawData, s.SizeOfRawData) for s in self.pe.sections]
        text = next(s for s in self.secs if s[0] == '.text')
        self.text = (BASE + text[1], data[text[3]:text[3] + text[4]])

    def off(self, va):
        for _, rva, _vs, raw, rs in self.secs:
            if rva <= va - BASE < rva + rs:
                return va - BASE - rva + raw
        return None

    def va(self, off):
        for _, rva, _vs, raw, rs in self.secs:
            if raw <= off < raw + rs:
                return BASE + rva + off - raw
        return None

    def cstr(self, va):
        o = self.off(va)
        if o is None:
            return None
        e = self.d.find(b'\0', o)
        return self.d[o:e] if 0 <= e - o < 512 else None

    def code(self, va, n):
        o = self.off(va)
        return self.d[o:o + n]

    def strings(self):
        out = {}
        langs = set()
        for t in self.pe.DIRECTORY_ENTRY_RESOURCE.entries:
            if t.id != 6:
                continue
            for e in t.directory.entries:
                for lang in e.directory.entries:
                    langs.add(lang.id)
                    d = self.pe.get_data(lang.data.struct.OffsetToData, lang.data.struct.Size)
                    p = 0
                    for i in range(16):
                        n = struct.unpack_from('<H', d, p)[0]
                        p += 2
                        if n:
                            out[(e.id - 1) * 16 + i] = d[p:p + 2 * n].decode('utf-16le')
                        p += 2 * n
        return out, sorted(langs)

    def font_table(self):
        """The LoadFonts calls: [(slot address, face, size, style)], found as the run of
        'push style; push size; push face; call F' to one F with string faces."""
        start, text = self.text
        hits = {}
        for m in re.finditer(rb'(?=([\x50-\x57]|\x6a.)([\x50-\x57]|\x6a.)\x68(....)(?:\xa3....)?\xe8(....))', text, re.S):
            face = self.cstr(struct.unpack('<I', m.group(3))[0])
            if not face or not re.fullmatch(rb'[ -~]{3,40}', face):
                continue
            end = m.end(4)  # just past the call
            target = start + end + struct.unpack('<i', m.group(4))[0]
            hits.setdefault(target, []).append((start + end, face.decode('latin-1'), arg(m.group(2)), arg(m.group(1))))
        calls = max(hits.values(), key=len)
        if len(calls) < 8:
            return None, []
        out = []
        for after, face, size, style in calls:
            slot = None
            for ins in CS.disasm(self.code(after, 32), after):
                mm = re.fullmatch(r'dword ptr \[(0x[0-9a-f]+)\], eax', ins.op_str)
                if ins.mnemonic == 'mov' and mm:
                    slot = int(mm.group(1), 16)
                    break
            out.append((slot, face, size, style))
        loader = next(t for t, v in hits.items() if v is calls)
        return loader, sorted(out, key=lambda x: x[0] or 0)

    def text_styles(self):
        """ResolveTextStyle's cases: (function address, {style: [values it stores]}).
        The first value is the font slot."""
        start, text = self.text
        m = STYLE_SIG.search(text)
        if not m:
            return None, {}
        fva = start + m.start()
        tbl = None
        for ins in CS.disasm(self.code(fva, 160), fva):
            if ins.mnemonic == 'jmp' and '*4' in ins.op_str:
                tbl = int(re.search(r'0x[0-9a-f]+', ins.op_str).group(0), 16)
                break
        res = {}
        for c in range(m.group(1)[0] + 1):
            tgt = struct.unpack('<I', self.code(tbl + 4 * c, 4))[0]
            vals = []
            for ins in CS.disasm(self.code(tgt, 64), tgt):
                if ins.mnemonic in ('ret', 'jmp'):
                    break
                if ins.mnemonic == 'mov' and ins.op_str.startswith(('dword ptr [e', 'word ptr [e')):
                    v = ins.op_str.split(', ')[-1]
                    vals.append(int(v, 0) if re.fullmatch(r'(0x)?[0-9a-f]+', v) else v)
            res[c] = vals
        return fva, res


def arg(b):
    if b[0] == 0x6a:
        return b[1]
    return ['eax', 'ecx', 'edx', 'ebx', 'esp', 'ebp', 'esi', 'edi'][b[0] - 0x50]


def pattern_at(exe, va, before, after):
    """A byte pattern of exe's code around va with addresses and call/jump targets
    wildcarded (they move between builds)."""
    lo = va - before
    data = exe.code(lo, before + after)
    wild = [False] * len(data)
    for i in range(len(data) - 3):
        v = struct.unpack_from('<I', data, i)[0]
        if 0x401000 <= v < 0x600000:
            for k in range(4):
                wild[i + k] = True
        if data[i] in (0xe8, 0xe9) and i + 5 <= len(data):
            for k in range(1, 5):
                wild[i + k] = True
    return re.compile(b''.join(b'.' if w else re.escape(bytes([c])) for c, w in zip(data, wild)), re.S), before


def counterpart(gog, other, va):
    """The address in `other` of the instruction at `va` in `gog` (None if not unique).
    Tries the code around va, then only the code after it (a function entry whose
    neighbour changed), growing until one match is left."""
    start, text = other.text
    for shape in ((24, 24), (40, 40), (64, 64), (96, 96), (0, 32), (0, 48), (0, 80), (0, 128)):
        pat, before = pattern_at(gog, va, *shape)
        found = [m.start() for m in pat.finditer(text)]
        if len(found) == 1:
            return start + found[0] + before
    return None


def carry_names(gog, other, names_tsv, xrefs_tsv, out_path):
    """re/names.tsv for another build: code addresses by counterpart(); globals through
    the operand of a code reference to them (from the English export's xrefs)."""
    gstart, gtext = gog.text
    refs = {}
    if os.path.exists(xrefs_tsv):
        for line in open(xrefs_tsv, encoding='utf-8'):
            p = line.split('\t')
            if len(p) > 2 and p[2] in ('DATA', 'READ', 'WRITE', 'PARAM') and re.fullmatch(r'[0-9a-f]{8}', p[0]) \
                    and re.fullmatch(r'[0-9a-f]{8}', p[1]):
                refs.setdefault(int(p[1], 16), []).append(int(p[0], 16))
    CS.detail = True
    done = lost = 0
    with open(out_path, 'w', encoding='utf-8') as out:
        out.write('# Names carried over from re/names.tsv (English build) by tools/locale_extract.py --names.\n')
        for line in open(names_tsv, encoding='utf-8'):
            if line.startswith('#') or not line.strip():
                continue
            p = line.rstrip('\n').split('\t')
            addr = int(p[0], 16)
            there = None
            if gstart <= addr < gstart + len(gtext):
                there = counterpart(gog, other, addr)
            else:
                for frm in refs.get(addr, [])[:6]:
                    if not gstart <= frm < gstart + len(gtext):
                        continue
                    at = counterpart(gog, other, frm)
                    if not at:
                        continue
                    gi = next(CS.disasm(gog.code(frm, 16), frm), None)
                    oi = next(CS.disasm(other.code(at, 16), at), None)
                    if not gi or not oi or gi.mnemonic != oi.mnemonic:
                        continue
                    gv = [o.imm if o.type == capstone.x86.X86_OP_IMM else o.mem.disp for o in gi.operands]
                    ov = [o.imm if o.type == capstone.x86.X86_OP_IMM else o.mem.disp for o in oi.operands]
                    for g, o in zip(gv, ov):
                        if 0 <= addr - (g & 0xffffffff) < 0x1000:
                            there = (o & 0xffffffff) + addr - (g & 0xffffffff)
                            break
                    if there:
                        break
            if there:
                out.write('\t'.join([f'0x{there:x}'] + p[1:]) + '\n')
                done += 1
            else:
                lost += 1
    CS.detail = False
    print(f'names: {done} carried over, {lost} not found -> {out_path}')


# --- the maps ---------------------------------------------------------------------

def strings_between(exe, a, b):
    """How many NUL-terminated strings lie from a to b in exe's data; None if b is
    not within 8 strings after a."""
    p = a
    for k in range(8):
        if p == b:
            return k
        p = step_strings(exe, p, 1)
    return None


def step_strings(exe, va, k):
    """The address k strings after (k < 0: before) the string at va. MSVC pads
    each string with NULs to 4 bytes."""
    o = exe.off(va)
    for _ in range(abs(k)):
        if k > 0:
            o = exe.d.index(b'\0', o)
            while exe.d[o] == 0:
                o += 1
        else:
            o -= 1
            while exe.d[o - 1] == 0:
                o -= 1
            while exe.d[o - 1] != 0:
                o -= 1
    return exe.va(o)


def cp1252(raw):
    return raw.split(b'\0')[0].decode('cp1252', 'replace')


def map_texts(data):
    """(name, [Message action texts in event order]) of a .WBM; texts as raw bytes."""
    m = wbm.parse(data)
    h = m['header']
    name = h['name'].encode('latin-1') if 'name' in h else h['name_raw'].split(b'\0')[0]
    msgs = []
    for e in m['events']['events']:
        for a in e['actions']:
            if a['name'] == 'Message':
                st = a['ST']
                msgs.append(bytes.fromhex(st) if isinstance(st, str) else bytes(st))
    return name, msgs


# --- the language file ------------------------------------------------------------

def esc(s):
    return s.replace('\r\n', '\n').replace('\n', '\\n')


def read_lang(path):
    """(header lines, Language value, {section: [(key, value, fit)]}, comments per section)"""
    sections = {}
    head, language, section = [], None, ''
    for raw in open(path, encoding='utf-8-sig'):
        line = raw.rstrip('\r\n')
        s = line.strip()
        if s.startswith('['):
            section = s[1:s.index(']')]
            sections.setdefault(section, [])
            continue
        if not section:
            if s.startswith('Language') and '=' in s:
                language = s.split('=', 1)[1].strip()
            else:
                head.append(line)
            continue
        if not s or s.startswith('#'):
            sections[section].append((None, line, False))
            continue
        k, v = (x.strip() for x in s.split('=', 1))
        fit = v.startswith('~')
        sections[section].append((k, v[1:].strip() if fit else v, fit))
    return head, language, sections


def write_lang(path, head, language, sections):
    out = list(head)
    while out and not out[-1].strip():
        out.pop()
    out += ['', f'Language = {language}', '']
    for name, entries in sections.items():
        out.append(f'[{name}]')
        for k, v, fit in entries:
            out.append(v if k is None else f'{k} = {"~" if fit else ""}{v}')
        while out and not out[-1].strip():
            out.pop()
        out.append('')
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(out))


# --- main -------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('disc')
    ap.add_argument('code')
    ap.add_argument('--gog', default=os.path.join(ROOT, 'testgame'))
    ap.add_argument('--lang')
    ap.add_argument('--names', action='store_true')
    a = ap.parse_args()
    out = os.path.join(ROOT, 're', a.code)
    disc_dir = os.path.join(out, 'disc')

    disc = Disc(a.disc)
    exe_name = next(iter(disc.find(r'(.*/)?WARBREED\.EXE')), None)
    if not exe_name:
        cabs = disc.find(r'.*\.(CAB|Z|EX_|INS)')
        raise SystemExit('no plain WARBREED.EXE on the disc; the installer packs it: ' + ', '.join(cabs[:8]) +
                         '\n(extract it with unshield / i5comp, or install the game and pass its folder)')
    wanted = {exe_name: 'WARBREED.EXE'}
    for n in disc.find(r'(.*/)?CAMPAIGN/[^/]+\.WBM'):
        wanted[n] = 'CAMPAIGN/' + n.split('/')[-1]
    for n in disc.find(r'(.*/)?MAPS/[^/]+\.WBM'):
        wanted[n] = 'MAPS/' + n.split('/')[-1]
    for n in disc.find(r'(.*/)?FONTS?/[^/]+\.(TTF|FON|OTF)') + disc.find(r'(.*/)?[^/]+\.FON'):
        wanted[n] = 'FONTS/' + n.split('/')[-1]
    for n in disc.find(r'[^/]+\.(TXT|DOC)') + disc.find(r'SETUP/[^/]+\.TXT'):
        wanted[n] = n.split('/')[-1]
    # The spoken announcements (a player's Voices\<code>\GLOBSFX.MHK; SFX.MHK is the same everywhere)
    for n in disc.find(r'(.*/)?DATA/GLOBSFX\.MHK'):
        wanted[n] = 'GLOBSFX.MHK'
    for n, dest in wanted.items():
        path = os.path.join(disc_dir, dest)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, 'wb') as f:
            f.write(disc.read(n))
    print(f'{len(wanted)} files -> {disc_dir}')

    loc = Exe(disc.read(exe_name))
    gog = Exe(open(os.path.join(a.gog, 'WARBREED.EXE'), 'rb').read())
    rep = [f'# {a.code}: the localized WarBreeds compared with the English exe', '',
           f'Disc: `{getattr(disc, "label", a.disc)}`; exe `{exe_name}`, {len(loc.d)} bytes, '
           f'PE timestamp 0x{loc.pe.FILE_HEADER.TimeDateStamp:08x} '
           f'(English 0x{gog.pe.FILE_HEADER.TimeDateStamp:08x}).', '']

    # Fonts
    gl, gfonts = gog.font_table()
    ll, lfonts = loc.font_table()
    rep += ['## Font table', '',
            f'LoadFonts(face, size, style): English 0x{gl:x}, this exe 0x{ll:x}. Size 0 is the bitmap face\'s own size.', '',
            '| slot | English | this exe: face | size | style |', '|---|---|---|---|---|']
    gbase, lbase = gfonts[0][0], lfonts[0][0]
    lnames = {}
    for slot, face, size, style in lfonts:
        i = (slot - lbase) // 4
        gface = GOG_SLOTS[i] if i < len(GOG_SLOTS) else '(none)'
        lnames[i] = f'{face} {size}'
        rep.append(f'| {i} (0x{slot:x}) | {gface} | {face} | {size} | {style} |')
    rep.append('')
    ttf = [n for n in os.listdir(os.path.join(disc_dir, 'FONTS'))] if os.path.isdir(os.path.join(disc_dir, 'FONTS')) else []
    if ttf:
        rep += ['Fonts on the disc:', '']
        for n in sorted(ttf):
            rep.append(f'- `{n}`: {font_names(os.path.join(disc_dir, "FONTS", n))}')
        rep.append('')

    # Text styles
    gf, gst = gog.text_styles()
    lf, lst = loc.text_styles()
    rep += ['## Text styles (ResolveTextStyle)', '',
            f'English 0x{gf:x} ({len(gst)} styles), this exe 0x{lf:x} ({len(lst)} styles). '
            'The first value each case stores is the font slot; the others are colours.', '',
            '| style | English | this exe | |', '|---|---|---|---|']
    for c in sorted(set(gst) | set(lst)):
        g, l = gst.get(c), lst.get(c)
        gs = f'{GOG_SLOTS[g[0]]} {g[1:]}' if g else '-'
        ls = f'{lnames.get(l[0], l[0])} {l[1:]}' if l else '-'
        same = g and l and g == l
        rep.append(f'| {c} | {gs} | {ls} | {"" if same else "**differs**"} |')
    rep.append('')

    # Strings
    gstr, _ = gog.strings()
    lstr, langs = loc.strings()
    added = sorted(set(lstr) - set(gstr))
    missing = sorted(set(gstr) - set(lstr))
    same = [i for i in lstr if i in gstr and lstr[i] == gstr[i] and i not in PROTECTED]
    rep += ['## Strings', '',
            f'String table language(s): {", ".join(f"0x{x:04x}" for x in langs)}; {len(lstr)} strings, '
            f'English {len(gstr)}.', '',
            f'- only in this exe: {added or "none"}' + ''.join(f'\n  - {i}: {lstr[i]!r}' for i in added),
            f'- missing from this exe: {missing or "none"}',
            f'- the same as the English: {len(same)}' + ''.join(f'\n  - {i}: {lstr[i]!r}' for i in same), '']
    with open(os.path.join(out, 'strings.tsv'), 'w', encoding='utf-8') as f:
        f.write('id\tenglish\ttranslation\n')
        for i in sorted(set(gstr) | set(lstr)):
            f.write(f'{i}\t{esc(gstr.get(i, ""))}\t{esc(lstr.get(i, ""))}\n')

    # .data literals: by the code that pushes them, else by their neighbour in .data
    # (the strings keep their order; a localized build may move the code around them)
    lits, found = {}, {}
    for site, text in GOG_LITERALS:
        there = counterpart(gog, loc, site)
        if there and loc.code(there, 1) == b'\x68':
            found[text] = (struct.unpack('<I', loc.code(there + 1, 4))[0], f'push at 0x{there:x}')
    for site, text in GOG_LITERALS:
        for near, (addr, _) in list(found.items()):
            if text in found:
                break
            k = strings_between(gog, text, near)
            if k is not None:
                found[text] = (step_strings(loc, addr, -k), f'{k} string(s) before the one above')
    rep += ['## .data literals', '', '| English push | English | this exe | text |', '|---|---|---|---|']
    for site, text in GOG_LITERALS:
        addr, how = found.get(text, (None, 'not found'))
        raw = loc.cstr(addr) if addr else None
        s = cp1252(raw) if raw is not None else None
        if s is not None:
            lits[text] = s
        rep.append(f'| 0x{site:x} | {cp1252(gog.cstr(text))!r} | {how} | {s!r} |')
    rep.append('')

    # Maps
    gmaps = {os.path.basename(p).upper(): p for p in wbm.shipped_maps()} if a.gog == wbm.GAME else {}
    if not gmaps:
        import glob
        gmaps = {os.path.basename(p).upper(): p for p in glob.glob(os.path.join(a.gog, 'DATA', 'MAPS', '*.WBM')) +
                 glob.glob(os.path.join(a.gog, 'CAMPAIGN', '*.WBM'))}
    names, messages, notes = {}, {}, []
    for n, dest in sorted(wanted.items(), key=lambda x: x[1]):
        if not dest.endswith('.WBM'):
            continue
        key = dest.split('/')[-1]
        try:
            lname, lmsgs = map_texts(disc.read(n))
        except Exception as e:  # a map this tool can't read
            notes.append(f'{key}: not read ({e})')
            continue
        if key not in gmaps:
            notes.append(f'{key}: not in the English game')
            continue
        gname, gmsgs = map_texts(open(gmaps[key], 'rb').read())
        if lname != gname:
            names[key] = cp1252(lname)
        if len(lmsgs) != len(gmsgs):
            notes.append(f'{key}: {len(lmsgs)} messages, English {len(gmsgs)}: not paired')
            continue
        for g, l in zip(gmsgs, lmsgs):
            g0 = g.split(b'\0')[0]
            if g0 and g0 != l.split(b'\0')[0]:
                messages[f'{zlib.crc32(g0) & 0xffffffff:08X}'] = cp1252(l)
    rep += ['## Maps', '', f'{len(names)} map names and {len(messages)} map messages differ from the English.', '']
    rep += [f'- {x}' for x in notes]
    with open(os.path.join(out, 'report.md'), 'w', encoding='utf-8') as f:
        f.write('\n'.join(rep) + '\n')
    print(f'report -> {os.path.join(out, "report.md")}')

    if a.lang:
        update_lang(a.lang, a.code, gstr, lstr, lits, {t: cp1252(gog.cstr(t)) for _, t in GOG_LITERALS}, names, messages)
    if a.names:
        carry_names(gog, loc, os.path.join(ROOT, 're', 'names.tsv'),
                    os.path.join(ROOT, 're', 'ghidra-export-v2', 'xrefs.tsv'), os.path.join(out, 'names.tsv'))
        with open(os.path.join(out, 'names.tsv'), 'a', encoding='utf-8') as f:  # changed in a localized build
            f.write(f'0x{lf:x}\tResolveTextStyle\tH: text style id -> font slot, colours (found by signature)\n')


def font_names(path):
    d = open(path, 'rb').read()
    if d[:2] == b'MZ':
        return 'bitmap font (.FON)'
    n = struct.unpack_from('>H', d, 4)[0]
    for i in range(n):
        tag, _cs, off, _ln = struct.unpack_from('>4sIII', d, 12 + 16 * i)
        if tag != b'name':
            continue
        _fmt, cnt, so = struct.unpack_from('>HHH', d, off)
        got = {}
        for j in range(cnt):
            pid, _eid, _lid, nid, ln, o = struct.unpack_from('>HHHHHH', d, off + 6 + 12 * j)
            s = d[off + so + o:off + so + o + ln]
            s = s.decode('utf-16be', 'replace') if pid in (0, 3) else s.decode('latin-1')
            got.setdefault(nid, s)
        return f'"{got.get(1, "?")}" {got.get(2, "")}; {got.get(0, "")}'
    return '?'


def update_lang(path, code, gstr, lstr, lits, english_lits, names, messages):
    head, language, sections = read_lang(path)
    old = {s: {k: (v, fit) for k, v, fit in e if k} for s, e in sections.items()}
    kept, game = [], []
    for i in sorted(gstr):
        if i in PROTECTED:
            continue
        mine = old.get('game', {}).get(str(i))
        fit = bool(mine and mine[1])
        if i in lstr and lstr[i].strip() != gstr[i].strip():
            game.append((str(i), esc(lstr[i].strip()), fit))
        elif mine:  # the localized release left it in English (or never had it): keep ours
            game.append((str(i), mine[0], fit))
            kept.append(i)
    for text, s in sorted(lits.items()):
        mine = old.get('game', {}).get(f'0x{text:x}')
        if s.strip() != english_lits[text].strip():
            game.append((f'0x{text:x}', esc(s.strip()), bool(mine and mine[1])))
        elif mine:
            game.append((f'0x{text:x}', mine[0], mine[1]))
            kept.append(f'0x{text:x}')
    sections['game'] = game
    maps = dict(old.get('maps', {}))
    for k, s in names.items():
        maps[k] = (s.strip(), maps.get(k, ('', False))[1])
    sections['maps'] = [(k, v, f) for k, (v, f) in sorted(maps.items())]
    msgs = dict(old.get('messages', {}))
    for k, s in messages.items():
        msgs[k] = (esc(s.strip()), msgs.get(k, ('', False))[1])
    sections['messages'] = [(k, v, f) for k, (v, f) in sorted(msgs.items())]
    for section, key, was, fixed in FIXES.get(code, []):
        entries = sections.get(section, [])
        hit = [i for i, (k, v, _f) in enumerate(entries) if k == key and was in v]
        if not hit:
            print(f'fix not applied (text not found): [{section}] {key}: {was!r}')
        for i in hit:
            k, v, f = entries[i]
            entries[i] = (k, v.replace(was, fixed), f)
    write_lang(path, head, language, sections)
    print(f'{path}: {len(game)} game lines ({len(kept)} kept from the file: {kept}), '
          f'{len(names)} map names, {len(messages)} messages from the disc')


if __name__ == '__main__':
    main()
