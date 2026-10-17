# The French release: fonts, text, and how to do this for another language

The 1998 French CD of WarBreeds (`WARBREED.EXE` PE timestamp 0x34e22285, Feb 1998; the English GOG/Steam exe
is 0x34c3efae, Jan 1998) compared with the English exe, and the process that took its text and fonts into
WarBreeds HD. Addresses are the French exe's unless marked GOG. Everything here can be redone from the disc
with the tools below; nothing of the game is in the repo except the French text in `lang/WarBreedsHD_fr.txt`
(used with the original developers' blessing).

## How the French release draws its text

**No bitmap fonts.** The English game draws everything in its own bitmap `.FON` faces (WBLg24, IntReg10, ...).
The French exe never mentions them: it names four TrueType faces, shipped in the disc's `FONTS\` and installed
into Windows by setup:

| file | GDI family | maker |
|---|---|---|
| `TT0359M_.TTF` | BellGothic BT | Bitstream |
| `TT1127M_.TTF` | Geometr231 BT (Geometric 231, a Futura design) | Bitstream |
| `TT1128M_.TTF` | Geometr231 BT, Bold | Bitstream |
| `SHERWOOD.TTF` | Sherwood (an uncial display face) | Fontographer-made |

The Bitstream fonts are Bitstream's to license, not the game's, so the patch can't ship them.

**The font table** is the same ten slots (one more in French), set up by the same code: `LoadFonts(face, size,
style)` (`0x4d8259`; GOG `0x4d82d9`) called from `0x468ef9` (GOG `0x468fd9`) into the table at `0x550870`
(GOG `0x550890`). The size is the character height in pixels (the font record's `+0x12`; the port's font
creation, GOG `0x4dec76`, sets `lfHeight = -size`), style bit 0 is bold (`lfWeight` 700). GOG passes size 0
(a bitmap face's own size).

| slot | English face | French face, size |
|---|---|---|
| 0 | IntReg10 | Geometr231 BT 11 |
| 1 | IntBld10 | Geometr231 Bold BT 11, bold |
| 2 | IntReg12 | Geometr231 BT 13 |
| 3 | Darwin18 | Sherwood 14 |
| 4 | IntBld12 | Geometr231 Bold BT 13, bold |
| 5 | WBLg24 | BellGothic BT 23 |
| 6 | WBMd14 | BellGothic BT 14 |
| 7 | WBMd18 | BellGothic BT 18 |
| 8 | WBMd24 | BellGothic BT 23 |
| 9 | WBSm12 | BellGothic BT 13 |
| 10 | (none) | Sherwood 18 |

**A 1998 bug:** slots 1 and 4 ask for the face "Geometr231 Bold BT", but no font has that name (the bold
file's family is "Geometr231 BT", style Bold). GDI falls back, to Arial on Windows today, so the French game's
bold interface text has most likely always been Arial Bold. The patch measures against today's fallback
(Arial Bold); what Windows 9x picked doesn't matter here.

**Text styles** (`ResolveTextStyle`, `0x469800`; GOG `0x4698c0`: style id to font slot and colours). Styles
0-44 are the same as the English, apart from:
- style 2: IntBld10 (slot 1, bold) became slot 0 (regular, narrower). The French release then pointed most of
  style 2's users at a new style 47 (slot 1 again, bold). It kept regular only in the three texts set up at
  `0x40bd40` (GOG `0x40be00`) and `SAllies::BuildRows` (`0x475220`; GOG `0x475200`). Bold stays in `0x401810`,
  the 14 texts of `0x4547c0` (GOG `0x4547b0`) and `SAllies::Init`.
- styles 45 and 46: Sherwood 18 (the new slot 10), with style 31's and style 0's colours. They're used once each:
  `SMultiDialog::vf14` (`0x48a580`) and `SMultiSetup::Init` (`0x48c2f0`). Everywhere else the
  Darwin18 slot became Sherwood 14.

**Why French overflowed in WarBreeds HD.** The English bitmap faces are much wider than the French release's
faces. Measured on the official French strings (GDI, as the game draws): WBMd24/18/14 are 1.25-1.38x
Bell Gothic, WBSm12 1.20x, IntReg10/12 1.50x Geometric 231, IntBld10/12 1.47-1.69x. The French text was written
for the narrow fonts.

**Other differences found on the way:**
- Three French-only strings, ids 497-499: a credits entry for the map editor's programming (Carl Bevil,
  Malcolm Blanchard). The English exe has no such ids, so they aren't used.
- Some dialogs moved their text: the TCP/IP join dialog's text offsets changed (e.g. a `push 0x48` x offset is
  `push -0x48` in French, near `0x4948b4`). The code around the `.data` literals is not byte-identical either.
- 81 strings stay English in the French release (credits names, "Options", "tanu cgk" ids 408-411, ...).

## What WarBreeds HD does with it (v0.16.0)

- **Text:** `lang/WarBreedsHD_fr.txt` `[game]`, `[maps]`, `[messages]` and the three `.data` literals are the
  French release's own text. Where the release left a line in English, our earlier translation is kept
  (id 98 "Démo WarBreeds", the "Error No Missions!" literal). `[patch]` (WarBreeds HD's own text) is still
  machine-drafted. Capitals keep the 1998 spelling ("Energie").
- **Typos fixed** (in `tools/locale_extract.py` `FIXES`, so a re-run keeps them):
  - 33 `-Pas d'excès d'énergie --` → `-- Pas d'excès d'énergie --`
  - 78 `CGC kalika` → `CGC kelika`
  - 278 `Ecraser le jeu?` → `Ecraser le jeu ?`
  - 283 `DirextX` → `DirectX`
  - 413 `à cours de` → `à court de`
  - map SPMAG5B `Al’est` → `A l’est`
  - message 3D9F0FA3 `Savant Yedda trouve.  Ramenez-le a la base.` → `trouvé`, `à la base`
  - message 64613601 `trouves les autres` → `trouvez les autres`
- **Fit changes** (drawn in the English game's wider fonts, checked in the French screenshot tour):
  - shortened in `FIXES`: 416 `Brouillard de guerre` → `Brouillard` (it ran into "Votre CGC" on the
    skirmish setup), 543 `Technologie génétique` → `Tech. génétique` (its button; the English is "Genetic Tech");
  - marked `~` (a pixel tighter, or one face smaller, when wider than its English's room): 17 `Construire`,
    455 `Equipe`, 456 `Couleur`, 474 `Charger un jeu`, 475 `Enregistrer le jeu`, 543.
  - The map editor's map list shows each map's own (English) name from its file, as in English.
- **Fonts follow the language** (`src/fonts.cpp`, `fonts::SetFor`). A language with
  `Lang\WarBreedsHD_<code>.fonts` is always drawn in its release's fonts; the Font setting (Orbitron) is for
  English only and shows "By language" otherwise.
  - If every face the release drew in is installed (Bell Gothic, Geometric 231, Sherwood, and Arial for the
    bold slots), they are used at the release's sizes: French looks as it did in 1998.
  - Otherwise Roboto Condensed for Bell Gothic (the menu faces; 0.87-0.97x its width, picked from a
    GDI-drawn comparison sheet, `tools/fontsheet.py`), and the English game's own bitmap faces for the
    interface faces and Darwin18 (`-` in the `.fonts` file).
  - **Readability pass (checked in game at 1080p):** the first stand-ins for Geometric 231 (League Spartan) and
    Sherwood (Uncial Antiqua) were unreadable in the build panel, and Uncial drew "Tanu" as "Tonu". Measured
    from screenshots: Geometric 231 at 11 px averages 4 px a letter; no readable font fits its widths. But the
    buttons have room. The build panel's art is 57/59/99/60 px wide (Build/Plant/Upgrade/Repair). In the
    English IntReg10, "planter" (42), "améliorer" (54) and "réparer" (45) fit as they are, and "Construire"
    (63) fits 1 px tighter (53). So the interface and headings keep the English bitmap faces, and the tight
    spots are marked `~` in the language file (the existing squeeze: 1 px tighter, then the next smaller face).

    Roboto Condensed is drawn at the release face's cell height (the same line spacing). Any string it would draw wider
    than the release face did is narrowed (`lfWidth`) to that width, down to 0.6x. The width comes from the
    `.fonts` file's room: the release face's advance widths for Windows-1252 0x20-0xFF (measurements, no
    glyphs). Anything still too wide logs `STILL TOO WIDE`. A first try gave IntBld10 the regular face's
    room (as style 2 became regular), but that squeezed the build list's bold labels ("Raffinerie") badly,
    and the release kept those bold. The room is now the face the release drew in, Arial Bold
    (`tools/font_metrics.py --style-rooms` restores the stricter rule).
  - Not reproduced: the per-site style changes (style 2 regular at two places; Sherwood 18 at two). The room
    already keeps those texts within the release's widths, and those sites only lose the bold or the larger
    size.

## Doing this for another language (German next)

Everything runs from the repo root with the repo's `.venv` (pefile, capstone, Pillow). The disc is the
player's own; extracted files go to `re/<code>/`, which is git-ignored.

1. **Extract and compare:**
   `python -I tools/locale_extract.py <disc .cue/.ccd/.img/.iso or folder> de --names`
   - copies `WARBREED.EXE`, the campaign and multiplayer maps, `FONTS\` and the readmes to `re/de/disc/`;
   - writes `re/de/report.md`: PE timestamp, string table language, the font table (face, size, style per slot),
     the fonts on the disc, the text styles against the English exe (differences marked), string ids
     added/missing/still English, the three `.data` literals;
   - `--names` writes `re/de/names.tsv`: `re/names.tsv` moved to the localized exe's addresses (functions by
     their code with addresses wildcarded; globals through a code reference), 2069 of 2120 for French.

   If the disc has no plain `WARBREED.EXE` (an InstallShield `.cab`/`_INST32I.EX_` instead), the tool says
   so: unpack it (unshield / i5comp) or install the game, and pass that folder.
2. **Read the report.** Bitmap `.FON` faces in the font table (as English) mean the release used the game's
   own fonts, perhaps redrawn: compare them with the English `.FON` files. TrueType faces mean a `.fonts` file
   (step 5). Also look at the style differences, and at any face name no font on the disc has (as French bold).
3. **Optional Ghidra project** for questions the report can't answer:
   ```
   GHIDRA/support/analyzeHeadless re/ghidra-project-de WarBreedsDE -import re/de/disc/WARBREED.EXE \
     -scriptPath tools/ghidra -preScript MarkVisualStudio.java -postScript RunRttiAnalyzerScript.java \
     -postScript ApplyNames.java re/de/names.tsv re/types.tsv -postScript ExportCaches.java re/ghidra-export-de
   ```
   (about 5 minutes). For French: callers of the new styles are `grep -n "SetStyle([^,]*,0x2[def])"
   re/ghidra-export-fr/decompiled.c`.
4. **Text:** `python -I tools/locale_extract.py <disc> de --lang lang/WarBreedsHD_de.txt` replaces `[game]`,
   `[maps]`, `[messages]` and the literals with the release's own, keeping our lines where the release left
   English and keeping `[patch]`. Read the result for typos, add them to `FIXES['de']` in the tool and run it
   again; list them here. Then `python tools/lang_check.py testgame lang/WarBreedsHD_de.txt`.
5. **Fonts**, if the release used its own TrueType faces:
   - download OFL candidates for each face;
   - draw them against the real ones with `tools/fontsheet.py` (a JSON spec of rows: face, size, bold, text);
   - pick one per face;
   - put them in `third_party/fonts/<name>/` with `OFL.txt`;
   - run `python -I tools/font_metrics.py de "<release face>=<stand-in>" ...` to write
     `lang/WarBreedsHD_de.fonts`.

   Packaging (`tools/package.py`) ships every font in `third_party/fonts/` and every `.fonts` file.
6. **Check on screen:** `sh tools/langtest.sh de original "0 1 2 3"` (hidden; screenshots in
   `build/langshots/de-original/`, the fonts log lines in `log.txt`, including any `STILL TOO WIDE`).
