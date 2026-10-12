#pragma once
// Optional modern font: the game's bitmap faces (WBLg24, IntBld12, ...) are
// swapped for a bundled TrueType font (Orbitron, SIL OFL) when they are
// selected into a DC, so the option can change live.

namespace fonts {

// dir: the game folder (the font is loaded from <dir>Fonts\). Install after
// render::Install (its SelectObject hook is chained).
void Install(const char* dir, void (*log)(const char*, ...));
void SetTextTrace(bool on);  // [Debug] TextTrace: log each distinct drawn string
void SetModern(bool on);  // false = the original fonts
// Width in pixels of text in one of the game's faces (e.g. "WBLg24"), in the
// font currently shown for it.
int TextWidth(const char* face, const char* text);

}  // namespace fonts
