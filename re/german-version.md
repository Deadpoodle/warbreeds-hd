# The German release

The 1998 German CD of WarBreeds, compared with the English exe, and how its text
reached WarBreeds HD. It was done with the process in [french-version.md](french-version.md)
("Doing this for another language"). This file only notes what differs from French.

## The disc
- **Format:** a 2048-byte ISO9660 image, volume label `WARBREEDS`, read directly by
  `tools/locale_extract.py`.
- **Files:** every file the tools need is a plain file, with no installer archive:
  - `WARBREED/WARBREED.EXE` (1,360,896 bytes, PE timestamp 0x34e10d72, 11 Feb 1998, one day
    before the French build);
  - `CAMPAIGN/*.WBM`, `WARBREED/DATA/MAPS/*.WBM`;
  - `WARBREED/DATA/GAMEPLAY.DAT`, `GLOBSFX.MHK` and `SFX.MHK`;
  - `FONTS/`;
  - `LIESMICH.TXT`.
- **SFX.MHK** is byte-identical to the English one. **GLOBSFX.MHK** (4,274,276 bytes) has the
  same 137 sounds, with the announcements in German. That makes it a player's
  `Voices\de\GLOBSFX.MHK`.

## Fonts and text styles: the same as French
- **Exe:** a near-twin of the French build. The same section layout, with 1374 bytes of
  `.text` differing in nine small places (text positions, for example `push -0xc8` against
  French `-0xd2` at 0x48a5f5).
- **Font table:** the same as French. BellGothic BT 13-23 for the menu faces, Geometr231 BT
  11/13 for the interface, Sherwood 14 and 18, and the same "Geometr231 Bold BT" face, which
  no font has, so Windows falls back to Arial Bold.
- **Text styles:** the same changes as French (style 2 regular; new 45, 46 and 47).
- **The disc's `FONTS/`:** the same four files.
- **So:** `lang/WarBreedsHD_de.fonts` (from `tools/font_metrics.py de "BellGothic BT=Roboto
  Condensed" "Geometr231 BT=-" "Geometr231 Bold BT=-" "Sherwood=-"`) is identical to the
  French file. Menus use Roboto Condensed fitted to the release's widths; the HUD, labs and
  headings use the game's own fonts.

## Text
- **String table:** language 0x0407, 583 strings, including the same extra credit as
  French (ids 497-499).
- **`[game]`:** 487 lines. Where the release left a line in English, the earlier
  translation is kept: ids 98, 106, 133, 258, 260, 485, 495 and the "Error No Missions!"
  literal.
- **`[maps]`:** 52 map names; Oublinad and Shudruun keep their names.
- **`[messages]`:** 16 map messages.
- **`[names]`:** 39 thing and gizmo names, from the release's own `GAMEPLAY.DAT`.
- **"Pod":** the German release kept "Pod" for the energy pod (its texts say "Einen
  Pod-Typ auswählen"), so it stays. French has "Cocon".
- **Fixes** (`FIXES['de']` in `tools/locale_extract.py`):
  - 94 `konte` → `konnte`
  - 190 `enfällt` → `entfällt`
  - 543 `Gentechniker` ("genetic technician") → `Gentechnik` (the "Genetic Tech" button)
  - map SPMAG2A `Vebrannte Erde` → `Verbrannte Erde`
  - map SPMAG5B `Der ösliche Rand` → `Der östliche Rand`
  - message 1DE8CB31 `Verteidiigung` → `Verteidigung`
  - message 3D9F0FA3 `yedda-Wissenschaftler`, double space → `Yedda-Wissenschaftler`
  - message 4344E552 `yedd-Ruinen` → `Yedda-Ruinen`. In SPTAN6A, one English line has three
    German versions; messages are keyed by the English, so the one kept is spelled right.
  - message BFFD32DF: "in der Nähe **der** Magha-Basis festgehalten**.**"
  - shortened to the 59-byte copy limit: 75 (`zusätzliche` dropped), 619
    ("…, während Sie bereit sind.")
