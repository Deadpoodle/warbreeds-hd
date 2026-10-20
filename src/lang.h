#pragma once
// Translations: language files in Lang\ (WarBreedsHD_<code>.txt, UTF-8), picked on
// the Settings page. English is built in. Text is handed to the game as Mac Roman
// bytes, the order the game's bitmap fonts hold their accented letters in.
#include <initializer_list>
#include <string>
#include <vector>

namespace lang {

struct Language {
    std::string code;  // "en", "fr", ... (from the file name)
    std::string name;  // as shown on the Settings page (Mac Roman)
};

// In DllMain, before the game loads its strings: hooks LoadStringA and loads `code`.
void Install(const std::string& dir, const std::string& code, void (*log)(const char*, ...));
// English plus every Lang\WarBreedsHD_*.txt (rescanned on request).
const std::vector<Language>& Available(bool rescan);
// Switch language now: reloads the game's cached string table and relabels the
// game's text buttons on screen. Returns false if the file couldn't be read.
bool Set(const std::string& code);
const std::string& Current();

// WarBreeds HD's own text: the translation of `english` (Mac Roman), or `english`.
const char* tr(const char* english);  // also a [names] name
// A thing or gizmo name from GAMEPLAY.DAT ("Pod", "Crystal Tree", "Hammer"): its
// [names] translation, or the English.
const char* Name(const char* english);
std::string Name(const std::string& english);
// A map's name ("4 Corners", from its header): its [maps] translation, or the English.
std::string MapName(const std::string& english);
const char* tr(const char* context, const char* english);  // "context|English" keys first
// A translated pattern with {1}, {2}, ... filled in (the arguments are used as they are).
std::string trf(const char* english, std::initializer_list<std::string> args);

// For a translation marked ~ in its file (a tight spot), the English it stands for:
// the room the English took is what it is fitted into. nullptr otherwise.
const char* EnglishOf(const char* text, int n);

// A string the game is about to draw that is exactly one of its translated
// strings' English (copies made outside the string table, e.g. the build list's
// device names): its translation, or nullptr.
const char* DrawnTranslation(const char* text, int n);

// Mac Roman text to UTF-16 (for TrueType drawing), and whether it needs it.
std::wstring Wide(const char* text, int n);
bool HasHigh(const char* text, int n);

// Lang\WarBreedsHD_template.txt from this copy of the game (written at every start).
void WriteTemplate();

}  // namespace lang
