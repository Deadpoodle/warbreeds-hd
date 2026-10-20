// Translations.
//
// Nearly all of the game's text is one Win32 string table in WARBREED.EXE (ids
// 1..652, English). IGlobalData's constructor (0x464d70, a static initialiser,
// before WinMain) copies it into a list (data at [0x55dd54], count at
// [0x55dd58], index = id - 1; each entry an IWinString holding a char* at +0)
// with LoadStringA, and a few screens call LoadStringA themselves (the labs,
// SDialog, SOpenDialog, SReward, MessageBoxRes). The exe's LoadStringA import
// (slot 0x595a3c) is hooked from DllMain, before any of that runs, so a
// translation replaces every use. A language change later rebuilds the cached
// list in place (the game's own allocator, so its exit-time free still works)
// and relabels the game's text buttons on screen; other screens pick the new
// text up when they next open.
//
// Other game text: mission titles (the map header's name, read by 0x489f00 at
// three call sites; translated by map file), chat lines embedded in campaign
// maps (AAction_Message's call to APlayerRegistry::SendMessage at 0x419419;
// matched by a CRC32 of the English, so no game text is stored), and three
// literals in .data (their push operands are pointed at translated copies).
//
// Language files: Lang\WarBreedsHD_<code>.txt, UTF-8:
//   Language = Français           shown on the Settings page
//   [game]     455 = ...          the game's strings by id; 0x51d940 = ... for a .data literal
//   [patch]    Settings = ...     WarBreeds HD's own text by its English ("context|English" also)
//   [maps]     SPTAN1A.WBM = ...  mission titles by map file
//   [messages] 3F2A9C1B = ...     map chat lines by CRC32 of the English
// Lines starting with # are comments; a missing key falls back to English. A
// value starting with ~ is fitted into the room its English took: drawn
// squeezed if it is wider (for tight spots such as buttons and columns).
// The game's bitmap fonts keep their accented letters in Mac Roman order (é is
// 0x8E), so text is converted to Mac Roman when loaded.
#include "lang.h"

#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <map>
#include <unordered_map>

#include "editor/mapfile.h"

namespace lang {
namespace {

void (*g_log)(const char*, ...);
std::string g_dir;
std::string g_current = "en";

struct Table {
    std::unordered_map<std::string, bool> fit;          // translations marked ~ (fit the English's room)
    std::unordered_map<int, std::string> game;        // by id
    std::unordered_map<uint32_t, std::string> lits;    // .data literals by address
    std::unordered_map<std::string, std::string> patch;
    std::unordered_map<std::string, std::string> maps;  // upper-case file name
    std::unordered_map<uint32_t, std::string> messages;
    std::unordered_map<std::string, std::string> names;  // GAMEPLAY.DAT thing/gizmo names, by the English
};
Table g_t;
std::unordered_map<std::string, std::string> g_english;  // translated text -> its English
std::unordered_map<std::string, std::string> g_drawn;    // the game's English -> translation

std::vector<Language> g_available;
bool g_quiet;  // Parse without logging (the other languages, read for DrawnTranslation)

constexpr uintptr_t kLoadStringSlot = 0x595a3c;
constexpr uintptr_t kStrings = 0x55dd54, kStringCount = 0x55dd58;
constexpr uintptr_t kGameAlloc = 0x4eba50, kGameFree = 0x4eb770;  // cdecl
constexpr uintptr_t kTextButtonVtbl = 0x50925c, kSetButtonText = 0x49a470;
constexpr uintptr_t kScenes = 0x55aea8, kSceneTop = 0x51bee8;
constexpr uintptr_t kMissionTitle = 0x489f00;  // stdcall (AMission* = file name, char out[84])
const uintptr_t kMissionTitleCalls[] = {0x489e71, 0x489e95, 0x489eb9};
constexpr uintptr_t kSendMessageCall = 0x419419, kSendMessage = 0x43bc40;  // thiscall (registry, from, mask, text)
// MapInfo::Name (fastcall: the name at +0xc), where a map's name is shown: the map
// list (SFileDialog_ListFiles), the skirmish setup's Map line (0x48f410) and the
// multiplayer dialogs' map field. Not where it's checked.
constexpr uintptr_t kMapName = 0x415040;
const uintptr_t kMapNameCalls[] = {0x479cad, 0x48f44a, 0x48b722, 0x48bc64};
std::unordered_map<std::string, std::string> g_mapNames;  // a map's English name (its header) -> [maps]
struct Literal {
    uintptr_t push;  // the push imm32 instruction
    uintptr_t text;  // the original string
    char buf[128];
};
Literal g_lits[] = {{0x4948bd, 0x51d940, {}}, {0x49480d, 0x51d96c, {}}, {0x489ed3, 0x51d6d4, {}},
                    {0x478367, 0x51c220, {}},   // the event editor's "No Events" (SEvent::Draw)
                    {0x4785b8, 0x51c200, {}}};  // its "*PERSISTENT*"

// Ids the game uses as keys, not as text: the window class/title and single-instance
// check (82), config and Red Orb Zone keys.
bool Protected(int id) { return (id >= 80 && id <= 91) || id == 590 || id == 591 || id == 606; }

decltype(&LoadStringA) o_LoadStringA;
HMODULE g_exe;

// --- encoding ---------------------------------------------------------------

// Mac Roman 0x80..0xFF as Unicode.
const uint16_t kMacRoman[128] = {
    0x00C4, 0x00C5, 0x00C7, 0x00C9, 0x00D1, 0x00D6, 0x00DC, 0x00E1, 0x00E0, 0x00E2, 0x00E4, 0x00E3, 0x00E5, 0x00E7,
    0x00E9, 0x00E8, 0x00EA, 0x00EB, 0x00ED, 0x00EC, 0x00EE, 0x00EF, 0x00F1, 0x00F3, 0x00F2, 0x00F4, 0x00F6, 0x00F5,
    0x00FA, 0x00F9, 0x00FB, 0x00FC, 0x2020, 0x00B0, 0x00A2, 0x00A3, 0x00A7, 0x2022, 0x00B6, 0x00DF, 0x00AE, 0x00A9,
    0x2122, 0x00B4, 0x00A8, 0x2260, 0x00C6, 0x00D8, 0x221E, 0x00B1, 0x2264, 0x2265, 0x00A5, 0x00B5, 0x2202, 0x2211,
    0x220F, 0x03C0, 0x222B, 0x00AA, 0x00BA, 0x03A9, 0x00E6, 0x00F8, 0x00BF, 0x00A1, 0x00AC, 0x221A, 0x0192, 0x2248,
    0x2206, 0x00AB, 0x00BB, 0x2026, 0x00A0, 0x00C0, 0x00C3, 0x00D5, 0x0152, 0x0153, 0x2013, 0x2014, 0x201C, 0x201D,
    0x2018, 0x2019, 0x00F7, 0x25CA, 0x00FF, 0x0178, 0x2044, 0x20AC, 0x2039, 0x203A, 0xFB01, 0xFB02, 0x2021, 0x00B7,
    0x201A, 0x201E, 0x2030, 0x00C2, 0x00CA, 0x00C1, 0x00CB, 0x00C8, 0x00CD, 0x00CE, 0x00CF, 0x00CC, 0x00D3, 0x00D4,
    0xF8FF, 0x00D2, 0x00DA, 0x00DB, 0x00D9, 0x0131, 0x02C6, 0x02DC, 0x00AF, 0x02D8, 0x02D9, 0x02DA, 0x00B8, 0x02DD,
    0x02DB, 0x02C7};

// UTF-8 to Mac Roman; characters it has no byte for become '?' and are counted.
std::string ToMacRoman(const std::string& utf8, int& missing) {
    std::wstring w;
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), nullptr, 0);
    w.resize(n);
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), &w[0], n);
    std::string out;
    out.reserve(w.size());
    for (wchar_t c : w) {
        if (c < 0x80) {
            out += (char)c;
            continue;
        }
        int b = -1;
        for (int i = 0; i < 128; ++i)
            if (kMacRoman[i] == c) b = 0x80 + i;
        if (b < 0) {
            // Close stand-ins for typographic marks the fonts don't need.
            b = c == 0x00A0 ? 0xCA : '?';
            if (b == '?') ++missing;
        }
        out += (char)b;
    }
    return out;
}

uint32_t Crc32(const char* s) {
    uint32_t c = 0xffffffff;
    for (; *s; ++s) {
        c ^= (uint8_t)*s;
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xedb88320 & (0u - (c & 1)));
    }
    return ~c;
}

std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

// "\n" in a value is a line break (briefings).
std::string Unescape(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == 'n') {
            out += "\r\n";
            ++i;
        } else {
            out += s[i];
        }
    }
    return out;
}

// Leading and trailing spaces matter in some of the game's strings (" (Human)",
// "Neutral " are joined to other text), and language files trim them: a
// translation gets its English original's edges back.
std::string WithEdges(const std::string& english, const std::string& text) {
    size_t lead = english.find_first_not_of(' '), tail = english.find_last_not_of(' ');
    if (lead == std::string::npos) return text;
    std::string core = Trim(text);
    return english.substr(0, lead) + core + english.substr(tail + 1);
}

std::string FilePath(const std::string& code) { return g_dir + "Lang\\WarBreedsHD_" + code + ".txt"; }

// Reads a language file. `nameOnly`: stop at the first section (for the list).
bool Parse(const std::string& path, Table* t, std::string* name) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    std::string section;
    char line[4096];
    int count = 0, missing = 0, bad = 0;
    bool first = true;
    while (fgets(line, sizeof line, f)) {
        std::string s = line;
        if (first && s.compare(0, 3, "\xEF\xBB\xBF") == 0) s = s.substr(3);  // UTF-8 BOM
        first = false;
        s = Trim(s);
        if (s.empty() || s[0] == '#') continue;
        if (s[0] == '[') {
            if (!t) break;
            section = s.substr(1, s.find(']') - 1);
            continue;
        }
        size_t eq = s.find('=');
        if (eq == std::string::npos) {
            ++bad;
            continue;
        }
        std::string key = Trim(s.substr(0, eq)), value = Unescape(Trim(s.substr(eq + 1)));
        bool fit = !value.empty() && value[0] == '~';
        if (fit) value = Trim(value.substr(1));
        if (value.empty()) continue;  // not translated yet
        std::string text = ToMacRoman(value, missing);
        if (fit && t) t->fit[text] = true;
        if (section.empty()) {
            if (key == "Language" && name) *name = text;
            continue;
        }
        if (!t) continue;
        ++count;
        if (section == "game") {
            if (key.compare(0, 2, "0x") == 0)
                t->lits[(uint32_t)strtoul(key.c_str(), nullptr, 16)] = text;
            else
                t->game[atoi(key.c_str())] = text;
        } else if (section == "patch") {
            t->patch[ToMacRoman(key, missing)] = text;
        } else if (section == "maps") {
            std::transform(key.begin(), key.end(), key.begin(), ::toupper);
            t->maps[key] = text;
        } else if (section == "messages") {
            t->messages[(uint32_t)strtoul(key.c_str(), nullptr, 16)] = text;
        } else if (section == "names") {
            t->names[ToMacRoman(key, missing)] = text;
        } else {
            --count;
            ++bad;
        }
    }
    fclose(f);
    if (t && !g_quiet)
        g_log("lang: %s: %d entries (%zu game, %zu patch, %zu maps, %zu messages)%s", path.c_str(), count,
              t->game.size(), t->patch.size(), t->maps.size(), t->messages.size(),
              missing || bad ? " - with problems below" : "");
    if (t && !g_quiet && missing) g_log("lang:   %d characters have no Mac Roman byte (shown as ?)", missing);
    if (t && !g_quiet && bad) g_log("lang:   %d lines not understood", bad);
    return true;
}

// --- hooks --------------------------------------------------------------------

int WINAPI h_LoadStringA(HINSTANCE inst, UINT id, LPSTR buf, int max) {
    if ((inst == nullptr || inst == g_exe) && buf && max > 0 && !Protected((int)id)) {
        auto it = g_t.game.find((int)id);
        if (it != g_t.game.end()) {
            int n = std::min((int)it->second.size(), max - 1);
            memcpy(buf, it->second.data(), n);
            buf[n] = 0;
            return n;
        }
    }
    return o_LoadStringA(inst, id, buf, max);
}

void __stdcall h_MissionTitle(const char* mission, char* out) {
    ((void(__stdcall*)(const char*, char*))kMissionTitle)(mission, out);
    if (!mission || g_t.maps.empty()) return;
    std::string file = mission;
    size_t slash = file.find_last_of("\\/");
    if (slash != std::string::npos) file = file.substr(slash + 1);
    std::transform(file.begin(), file.end(), file.begin(), ::toupper);
    auto it = g_t.maps.find(file);
    if (it == g_t.maps.end() && file.find('.') == std::string::npos) it = g_t.maps.find(file + ".WBM");
    if (it == g_t.maps.end()) return;
    strncpy(out, it->second.c_str(), 59);
    out[59] = 0;
}

// A map's name where it's shown, from [maps] by its English name (the maps' own
// headers are English; each [maps] entry is keyed by file).
const char* __fastcall h_MapName(void* info) {
    const char* name = ((const char*(__fastcall*)(void*))kMapName)(info);
    if (!name || g_mapNames.empty()) return name;
    auto it = g_mapNames.find(name);
    return it == g_mapNames.end() ? name : it->second.c_str();
}

void BuildMapNames() {
    g_mapNames.clear();
    for (auto& [file, text] : g_t.maps) {
        editor::MapEntry e;
        for (const char* dir : {"DATA\\MAPS\\", "CAMPAIGN\\"})
            if (editor::ReadHeader(g_dir + dir + file, e) && !e.name.empty()) {
                g_mapNames.emplace(e.name, text);
                break;
            }
    }
}

void __fastcall h_SendMessage(void* registry, void*, int from, int mask, const char* text) {
    if (text && !g_t.messages.empty()) {
        auto it = g_t.messages.find(Crc32(text));
        if (it != g_t.messages.end()) text = it->second.c_str();
    }
    ((void(__thiscall*)(void*, int, int, const char*))kSendMessage)(registry, from, mask, text);
}

bool PatchCall(uintptr_t site, uintptr_t expect, void* hook) {
    uint8_t* p = (uint8_t*)site;
    if (p[0] != 0xe8 || site + 5 + *(int32_t*)(p + 1) != expect) return false;
    DWORD old;
    if (!VirtualProtect(p + 1, 4, PAGE_EXECUTE_READWRITE, &old)) return false;
    *(int32_t*)(p + 1) = (int32_t)((uintptr_t)hook - (site + 5));
    VirtualProtect(p + 1, 4, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 5);
    return true;
}

// Points each literal's push at its translation (or back at the original).
void ApplyLiterals() {
    for (Literal& l : g_lits) {
        uint8_t* p = (uint8_t*)l.push;
        uint32_t cur = *(uint32_t*)(p + 1);
        if (p[0] != 0x68 || (cur != l.text && cur != (uint32_t)(uintptr_t)l.buf)) continue;
        auto it = g_t.lits.find((uint32_t)l.text);
        uint32_t want = (uint32_t)l.text;
        if (it != g_t.lits.end()) {
            strncpy(l.buf, it->second.c_str(), sizeof l.buf - 1);
            want = (uint32_t)(uintptr_t)l.buf;
        }
        if (cur == want) continue;
        DWORD old;
        if (VirtualProtect(p + 1, 4, PAGE_EXECUTE_READWRITE, &old)) {
            *(uint32_t*)(p + 1) = want;
            VirtualProtect(p + 1, 4, old, &old);
            FlushInstructionCache(GetCurrentProcess(), p, 5);
        }
    }
}

// The game's text buttons in every scene on the stack whose text is `from`.
void RelabelButtons(const std::map<std::string, std::string>& change) {
    short top = *(short*)kSceneTop;
    if (top < 0 || top > 16) return;
    int n = 0;
    for (int s = 0; s <= top; ++s) {
        char* scene = ((char**)kScenes)[s];
        if (!scene) continue;
        void** list = *(void***)(scene + 0xc);
        int count = *(int*)(scene + 0x10);
        for (int i = 0; list && i < count && i < 256; ++i) {
            char* b = (char*)list[i];
            if (!b || *(uintptr_t*)b != kTextButtonVtbl) continue;
            auto it = change.find(std::string(b + 0x2c, strnlen(b + 0x2c, 60)));
            if (it == change.end()) continue;
            ((void(__thiscall*)(void*, const char*))kSetButtonText)(b, it->second.c_str());
            *(short*)(b + 0x1a) = 1;
            ++n;
        }
    }
    if (n) g_log("lang: relabelled %d of the game's buttons", n);
}

// Rebuilds the game's cached string table for the current language.
void RebuildCache() {
    char** list = *(char***)kStrings;
    int count = *(int*)kStringCount;
    if (!list || count <= 0 || count > 4096) return;
    std::map<std::string, std::string> change;
    static char buf[0x1800];
    int replaced = 0;
    for (int id = 1; id <= count; ++id) {
        char* entry = list[id - 1];  // IWinString: char* at +0
        if (!entry || Protected(id)) continue;
        char*& text = *(char**)entry;
        int n = h_LoadStringA(g_exe, id, buf, sizeof buf);
        if (n <= 0 || (text && strcmp(text, buf) == 0)) continue;
        char* copy = ((char*(__cdecl*)(size_t))kGameAlloc)(n + 1);
        if (!copy) continue;
        memcpy(copy, buf, n + 1);
        if (text) {
            change[text] = buf;
            ((void(__cdecl*)(void*))kGameFree)(text);
        }
        text = copy;
        ++replaced;
    }
    g_log("lang: %d of the game's cached strings replaced", replaced);
    RelabelButtons(change);
}

}  // namespace

void Install(const std::string& dir, const std::string& code, void (*log)(const char*, ...)) {
    g_log = log;
    g_dir = dir;
    g_exe = GetModuleHandleA(nullptr);
    uintptr_t* slot = (uintptr_t*)kLoadStringSlot;
    o_LoadStringA = (decltype(&LoadStringA))*slot;
    DWORD old;
    if (VirtualProtect(slot, 4, PAGE_READWRITE, &old)) {
        *slot = (uintptr_t)&h_LoadStringA;
        VirtualProtect(slot, 4, old, &old);
    }
    bool ok = true;
    for (uintptr_t site : kMissionTitleCalls) ok &= PatchCall(site, kMissionTitle, (void*)&h_MissionTitle);
    ok &= PatchCall(kSendMessageCall, kSendMessage, (void*)&h_SendMessage);
    for (uintptr_t site : kMapNameCalls) ok &= PatchCall(site, kMapName, (void*)&h_MapName);
    log("lang: string table hooked; mission titles, map names and map messages %s",
        ok ? "hooked" : "FAILED (unexpected code)");
    if (code != "en" && !code.empty()) Set(code);
}

const std::vector<Language>& Available(bool rescan) {
    if (!rescan && !g_available.empty()) return g_available;
    g_available.clear();
    g_available.push_back({"en", "English"});
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((g_dir + "Lang\\WarBreedsHD_*.txt").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            std::string file = fd.cFileName;
            size_t us = file.find('_'), dot = file.rfind('.');
            if (us == std::string::npos || dot == std::string::npos || dot <= us + 1) continue;
            std::string code = file.substr(us + 1, dot - us - 1);
            std::transform(code.begin(), code.end(), code.begin(), ::tolower);
            if (code == "en" || code == "template") continue;
            std::string name;
            if (!Parse(g_dir + "Lang\\" + file, nullptr, &name)) continue;
            g_available.push_back({code, name.empty() ? code : name});
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    std::sort(g_available.begin() + 1, g_available.end(),
              [](const Language& a, const Language& b) { return a.name < b.name; });
    return g_available;
}

bool Set(const std::string& code) {
    Table t;
    bool ok = true;
    if (code != "en" && !code.empty()) ok = Parse(FilePath(code), &t, nullptr);
    if (!ok) {
        g_log("lang: %s not found; keeping %s", FilePath(code).c_str(), g_current.c_str());
        return false;
    }
    if (o_LoadStringA) {  // the game strings' edge spaces, from the English
        static char en[0x1800];
        for (auto& [id, text] : t.game) {
            if (o_LoadStringA(g_exe, id, en, sizeof en) <= 0) continue;
            std::string edged = WithEdges(en, text);
            if (edged == text) continue;
            if (t.fit.count(text)) t.fit[edged] = true;
            text = edged;
        }
    }
    for (auto& [english, text] : t.patch) {
        size_t bar = english.find('|');
        std::string edged = WithEdges(bar == std::string::npos ? english : english.substr(bar + 1), text);
        if (edged == text) continue;
        if (t.fit.count(text)) t.fit[edged] = true;
        text = edged;
    }
    g_t = std::move(t);
    g_current = ok && !code.empty() ? code : "en";
    BuildMapNames();
    // Text the game draws from copies it made outside its string table: the
    // build list's names (in English), and ~40 labels GameInit copies once at
    // start-up (the Shaman panel's buttons, stances, Gene Lab labels, the stats
    // rows) in whatever language was current then. Each is recognised by its
    // English or any installed language's text for the same id, and drawn in the
    // current language; in English too. Where two ids share a text, the lower wins.
    g_drawn.clear();
    if (o_LoadStringA) {
        std::vector<Table> others;
        g_quiet = true;
        for (const Language& l : Available(false)) {
            if (l.code == "en" || l.code == g_current) continue;
            others.emplace_back();
            if (!Parse(FilePath(l.code), &others.back(), nullptr)) others.pop_back();
        }
        g_quiet = false;
        static char en[0x1800];
        for (int id = 1; id < 0x400; ++id) {
            if (Protected(id) || o_LoadStringA(g_exe, id, en, sizeof en) <= 2) continue;
            auto own = g_t.game.find(id);
            const std::string& want = own != g_t.game.end() ? own->second : std::string(en);
            auto from = [&](const std::string& text) {
                if (text.size() > 2 && text != want && !g_drawn.count(text)) g_drawn[text] = want;
            };
            from(en);
            for (const Table& o : others) {
                auto it = o.game.find(id);
                if (it != o.game.end()) from(WithEdges(en, it->second));
            }
        }
    }
    g_english.clear();  // only for the translations marked to fit
    for (auto& [english, text] : g_t.patch) {
        size_t bar = english.find('|');
        if (g_t.fit.count(text)) g_english[text] = bar == std::string::npos ? english : english.substr(bar + 1);
    }
    if (o_LoadStringA) {
        static char buf[0x1800];
        for (auto& [id, text] : g_t.game)
            if (g_t.fit.count(text) && o_LoadStringA(g_exe, id, buf, sizeof buf) > 0) g_english[text] = buf;
    }
    g_log("lang: language %s", g_current.c_str());
    ApplyLiterals();
    RebuildCache();
    return true;
}

const std::string& Current() { return g_current; }

const char* DrawnTranslation(const char* text, int n) {
    if (!text || (g_drawn.empty() && g_t.names.empty())) return nullptr;
    if (n < 0) n = (int)strlen(text);
    if (n < 3) return nullptr;
    std::string s(text, n);
    auto it = g_drawn.find(s);
    if (it != g_drawn.end()) return it->second.c_str();
    auto nm = g_t.names.find(s);  // a name copied from GAMEPLAY.DAT (build and pod lists, labs)
    return nm == g_t.names.end() ? nullptr : nm->second.c_str();
}

const char* EnglishOf(const char* text, int n) {
    if (!text || g_english.empty()) return nullptr;
    auto it = g_english.find(n < 0 ? std::string(text) : std::string(text, n));
    return it == g_english.end() ? nullptr : it->second.c_str();
}

const char* tr(const char* english) {
    if (!english) return english;
    auto it = g_t.patch.find(english);
    return it != g_t.patch.end() ? it->second.c_str() : Name(english);
}

const char* Name(const char* english) {
    if (!english || g_t.names.empty()) return english;
    auto it = g_t.names.find(english);
    return it == g_t.names.end() ? english : it->second.c_str();
}

std::string Name(const std::string& english) { return Name(english.c_str()); }

std::string MapName(const std::string& english) {
    auto it = g_mapNames.find(english);
    return it == g_mapNames.end() ? english : it->second;
}

const char* tr(const char* context, const char* english) {
    if (!english || g_t.patch.empty()) return english;
    auto it = g_t.patch.find(std::string(context) + "|" + english);
    return it == g_t.patch.end() ? tr(english) : it->second.c_str();
}

std::string trf(const char* english, std::initializer_list<std::string> args) {
    std::string pattern = tr(english), out;
    for (size_t i = 0; i < pattern.size(); ++i) {
        if (pattern[i] == '{' && i + 2 < pattern.size() && pattern[i + 1] >= '1' && pattern[i + 1] <= '9' &&
            pattern[i + 2] == '}') {
            size_t k = pattern[i + 1] - '1';
            if (k < args.size()) out += *(args.begin() + k);
            i += 2;
        } else {
            out += pattern[i];
        }
    }
    return out;
}

std::wstring Wide(const char* text, int n) {
    if (n < 0) n = (int)strlen(text);
    std::wstring w(n, 0);
    for (int i = 0; i < n; ++i) {
        uint8_t c = (uint8_t)text[i];
        w[i] = c < 0x80 ? c : kMacRoman[c - 0x80];
    }
    return w;
}

bool HasHigh(const char* text, int n) {
    if (!text) return false;
    if (n < 0) n = (int)strlen(text);
    for (int i = 0; i < n; ++i)
        if ((uint8_t)text[i] >= 0x80) return true;
    return false;
}

// The template: every key a language file can have, with the English beside it,
// written from the player's own copy of the game (so nothing of the game's text
// is shipped with the patch). The game's strings come from WARBREED.EXE; the
// mission titles, map messages and WarBreeds HD's own keys from a shipped
// language file, with its text as a reference.
void WriteTemplate() {
    std::string path = g_dir + "Lang\\WarBreedsHD_template.txt";
    CreateDirectoryA((g_dir + "Lang").c_str(), nullptr);
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return;
    fputs("\xEF\xBB\xBF# WarBreeds HD language template, made from your own copy of the game.\r\n"
          "# It is rewritten every time the game starts: COPY it before editing, as\r\n"
          "# WarBreedsHD_<code>.txt (for example WarBreedsHD_es.txt), then fill in the lines\r\n"
          "# after each comment. Lines left empty stay English. README.txt explains the rest.\r\n\r\n"
          "Language = \r\n\r\n[game]\r\n",
          f);
    static char buf[0x1800];
    for (int id = 1; id <= 652; ++id) {
        if (Protected(id)) continue;
        int n = o_LoadStringA(g_exe, id, buf, sizeof buf);
        if (n <= 0) continue;
        std::string en;
        for (int i = 0; i < n; ++i) {
            if (buf[i] == '\r') continue;
            en += buf[i] == '\n' ? std::string("\\n") : std::string(1, buf[i]);
        }
        fprintf(f, "# %s\r\n%d = \r\n", en.c_str(), id);
    }
    for (Literal& l : g_lits) fprintf(f, "# %s\r\n0x%x = \r\n", (const char*)l.text, (unsigned)l.text);

    // [names]: every thing and gizmo name in this copy's GAMEPLAY.DAT (char[20] tables at
    // 0x3c2, 74 thing types, and 0x2e8f, 25 gizmos; read from the file: the game hasn't
    // loaded it yet).
    if (FILE* gp = fopen((g_dir + "DATA\\GAMEPLAY.DAT").c_str(), "rb")) {
        fputs("\r\n[names]\r\n# Names of things and gizmos (build and pod lists, labs, map editor)\r\n", f);
        std::map<std::string, bool> seen;
        for (auto [off, count] : {std::pair<long, int>{0x3c2, 74}, {0x2e8f, 25}}) {
            for (int i = 0; i < count; ++i) {
                char name[21] = "";
                fseek(gp, off + 20 * i, SEEK_SET);
                if (fread(name, 1, 20, gp) != 20 || !name[0] || !seen.emplace(name, true).second) continue;
                fprintf(f, "%s = \r\n", name);
            }
        }
        fclose(gp);
    }

    // The other sections' keys, from a shipped language (French if it's there).
    std::string reference;
    std::string refName;
    for (const char* code : {"fr", "de"}) {
        FILE* r = fopen(FilePath(code).c_str(), "rb");
        if (!r) continue;
        fclose(r);
        reference = FilePath(code);
        break;
    }
    if (reference.empty()) {
        for (const Language& l : Available(true))
            if (l.code != "en") {
                reference = FilePath(l.code);
                break;
            }
    }
    if (FILE* r = reference.empty() ? nullptr : fopen(reference.c_str(), "rb")) {
        char line[4096];
        std::string section;
        while (fgets(line, sizeof line, r)) {
            std::string t = line;
            if (t.compare(0, 3, "\xEF\xBB\xBF") == 0) t = t.substr(3);
            t = Trim(t);
            if (t.empty() || t[0] == '#') continue;
            if (t[0] == '[') {
                section = t.substr(1, t.find(']') - 1);
                if (section != "game" && section != "names") fprintf(f, "\r\n[%s]\r\n", section.c_str());
                continue;
            }
            size_t eq = t.find('=');
            if (eq == std::string::npos) continue;
            std::string key = Trim(t.substr(0, eq)), value = Trim(t.substr(eq + 1));
            if (section.empty()) {
                if (key == "Language") refName = value;
                continue;
            }
            if (section == "game" || section == "names") continue;  // written above
            fprintf(f, "# %s: %s\r\n%s = \r\n", refName.c_str(), value.c_str(), key.c_str());
        }
        fclose(r);
    }
    fclose(f);
    g_log("lang: wrote %s", path.c_str());
}

}  // namespace lang
