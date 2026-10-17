#pragma once
// Voice language: the spoken announcements ("construction complete", "unit lost",
// ...) from another language's release. They are all in DATA\GLOBSFX.MHK (ids
// 250-356); SFX.MHK is the same in every release. A player puts their own
// release's file at Voices\<code>\GLOBSFX.MHK (the game's own English one in
// DATA\ is never touched), and the game's open of GLOBSFX.MHK is pointed there.
#include <string>
#include <vector>

namespace voices {

// In DllMain, before the game opens its sound files: hooks the exe's CreateFileA.
void Install(const std::string& dir, void (*log)(const char*, ...));
// Codes with a valid Voices\<code>\GLOBSFX.MHK (rescanned on request).
const std::vector<std::string>& Available(bool rescan);
// [Sound] Voices: "auto" (the text language's voices if installed, else English),
// "en", or a code; `language` is the text language. A change reopens the game's
// sound files at once (as the game does after alt-tab).
void Set(const std::string& setting, const std::string& language);
// The voices in use: "en" or a code.
const std::string& Current();

}  // namespace voices
