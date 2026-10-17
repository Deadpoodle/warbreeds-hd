#pragma once
// The fonts the game's text is drawn in. Its own bitmap faces (WBLg24, IntBld12,
// ...) are swapped, when they are selected into a DC, for:
//   - a language's release fonts, when Lang\WarBreedsHD_<code>.fonts exists: the
//     faces that language's 1998 release drew in if the player has them installed,
//     else free stand-ins (Fonts\) narrowed where needed to the release's widths,
//     or the game's own faces where the file says so (French: the HUD's);
//   - in English only, Orbitron (SIL OFL), an option.
// Everything changes live.
#include <string>

namespace fonts {

// dir: the game folder (fonts are loaded from <dir>Fonts\, release font files from
// <dir>Lang\). Install after render::Install (its SelectObject hook is chained).
void Install(const char* dir, void (*log)(const char*, ...));
void SetTextTrace(bool on);  // [Debug] TextTrace: log each distinct drawn string
// The fonts for this language: its release's (if it has a .fonts file), else the
// game's own, or Orbitron if `orbitron` and the language is English.
void SetFor(const std::string& language, bool orbitron);
// Width in pixels of text in one of the game's faces (e.g. "WBLg24"), in the
// font currently shown for it.
int TextWidth(const char* face, const char* text);

}  // namespace fonts
