#include "voices.h"

#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <map>

#include "render/render.h"

// The sound player (g_SoundPlayer 0x55aeb8) opens SFX.MHK and GLOBSFX.MHK by bare
// name from DATA\ (FUN_00463bd0, from App_StartRunning at start-up and after every
// alt-tab) and closes them on focus loss (FUN_00463e50). The Mohawk library opens
// files through CreateFileA (IAT slot 0x5957f0, call at 0x4d59ad). GLOBSFX.MHK's
// sounds are found by type and id in every open archive (FUN_004e6aa6), and a
// localized copy has the same 137 tWAV ids (250-386; ids 250-356 are the spoken
// announcements, longer in French), so it replaces the English one whole.

namespace voices {
namespace {

void (*g_log)(const char*, ...);
std::string g_dir;
std::string g_current = "en";
std::vector<std::string> g_available;
std::map<std::string, bool> g_valid;  // code|size:date -> its file checked and good

constexpr uintptr_t kCreateFileSlot = 0x5957f0;
constexpr uintptr_t kSoundPlayer = 0x55aeb8, kOpenSounds = 0x463bd0, kCloseSounds = 0x463e50;
constexpr uintptr_t kAppRunning = 0x5792c8;  // set by App_StartRunning 0x471eb0, cleared by App_StopRunning

decltype(&CreateFileA) o_CreateFileA;

// Announcements wait in a queue (IList<tGlobalSoundInfo> at 0x54c570: up to 12, count
// at 0x54c578), each due 20 game turns after it was queued (FUN_004641d0), played from
// SGame::Idle (FUN_004643b0). Only closing the sounds (quitting, alt-tab) emptied it,
// so what a game left pending played in the next: in the map editor, whose clock
// stands still, every building placed queued its "construction complete", and they
// all played in the next skirmish (a player's report). Each new world (all five calls
// of Game_CreateRegistries 0x471c40: a mission or a load starting, the editor opening
// a map) now starts with the queue empty.
constexpr uintptr_t kQueueCount = 0x54c578, kCreateRegistries = 0x471c40;
const uintptr_t kCreateRegistriesCalls[] = {0x47ac09, 0x480673, 0x487af6, 0x48f072, 0x493694};

unsigned __cdecl HookCreateRegistries() {
    if (int& n = *(int*)kQueueCount) {
        g_log("voices: %d queued announcement%s dropped (a new game)", n, n == 1 ? "" : "s");
        n = 0;
    }
    return ((unsigned(__cdecl*)())kCreateRegistries)();
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

std::string FileOf(const std::string& code) { return g_dir + "Voices\\" + code + "\\GLOBSFX.MHK"; }

uint32_t BE32(const uint8_t* p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
uint16_t BE16(const uint8_t* p) { return (uint16_t)(p[0] << 8 | p[1]); }

// A Mohawk archive whose tWAV resources include every announcement id (250-356).
bool Valid(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    std::vector<uint8_t> d;
    uint8_t buf[65536];
    size_t n;
    // The directory is near the end; the whole file is a few MB.
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) d.insert(d.end(), buf, buf + n);
    fclose(f);
    if (d.size() < 28 || memcmp(&d[0], "MHWK", 4) != 0 || memcmp(&d[8], "RSRC", 4) != 0) return false;
    uint32_t dir = BE32(&d[20]);  // RSRC header: version, compaction, size, then the directory's offset
    if (dir + 4 > d.size()) return false;
    int types = BE16(&d[dir + 2]);
    for (int t = 0; t < types; ++t) {
        size_t e = dir + 4 + 8 * t;
        if (e + 8 > d.size()) return false;
        if (memcmp(&d[e], "tWAV", 4) != 0) continue;
        size_t table = dir + BE16(&d[e + 4]);
        if (table + 2 > d.size()) return false;
        int count = BE16(&d[table]);
        bool have[107] = {};
        int found = 0;
        for (int k = 0; k < count && table + 6 + 4 * k <= d.size(); ++k) {
            int id = BE16(&d[table + 2 + 4 * k]);
            if (id >= 250 && id <= 356 && !have[id - 250]) have[id - 250] = true, ++found;
        }
        return found == 107;
    }
    return false;
}

// Checked once per file version (size and date): the file the game has open can't be
// read again, so a rescan while it is in use keeps the earlier verdict.
std::string Stamp(const std::string& path) {
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &a)) return "";
    char s[64];
    snprintf(s, sizeof s, "%lu:%lu:%lu", a.nFileSizeLow, a.ftLastWriteTime.dwLowDateTime, a.ftLastWriteTime.dwHighDateTime);
    return s;
}

bool Good(const std::string& code) {
    std::string key = code + "|" + Stamp(FileOf(code));
    auto it = g_valid.find(key);
    if (it != g_valid.end()) return it->second;
    bool ok = Valid(FileOf(code));
    if (!ok && GetFileAttributesA(FileOf(code).c_str()) != INVALID_FILE_ATTRIBUTES)
        g_log("voices: %s is not a WarBreeds GLOBSFX.MHK (no announcements 250-356); ignored", FileOf(code).c_str());
    return g_valid[key] = ok;
}

HANDLE WINAPI h_CreateFileA(LPCSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disposition,
                            DWORD flags, HANDLE templ) {
    if (name && g_current != "en") {
        const char* base = strrchr(name, '\\');
        base = base ? base + 1 : name;
        if (_stricmp(base, "GLOBSFX.MHK") == 0) {
            std::string own = FileOf(g_current);
            HANDLE h = o_CreateFileA(own.c_str(), access, share, sa, disposition, flags, templ);
            if (h != INVALID_HANDLE_VALUE) {
                g_log("voices: %s -> %s", name, own.c_str());
                return h;
            }
            g_log("voices: %s could not be opened (%lu); English voices", own.c_str(), GetLastError());
        }
    }
    return o_CreateFileA(name, access, share, sa, disposition, flags, templ);
}

}  // namespace

void Install(const std::string& dir, void (*log)(const char*, ...)) {
    g_dir = dir;
    g_log = log;
    if (!render::HookImport(kCreateFileSlot, &h_CreateFileA, &o_CreateFileA)) log("voices: CreateFileA hook FAILED");
    bool ok = true;
    for (uintptr_t site : kCreateRegistriesCalls) ok &= PatchCall(site, kCreateRegistries, (void*)&HookCreateRegistries);
    log("voices: announcement queue emptied for each new game: %s", ok ? "hooked" : "FAILED (unexpected code)");
}

const std::vector<std::string>& Available(bool rescan) {
    if (rescan || g_available.empty()) {
        g_available.clear();
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA((g_dir + "Voices\\*").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == '.') continue;
                std::string code = fd.cFileName;
                for (char& c : code) c = (char)tolower((unsigned char)c);
                if (code != "en" && Good(code)) g_available.push_back(code);
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
    }
    return g_available;
}

void Set(const std::string& setting, const std::string& language) {
    std::string want = setting.empty() || _stricmp(setting.c_str(), "auto") == 0 ? language : setting;
    for (char& c : want) c = (char)tolower((unsigned char)c);
    if (want != "en" && !Good(want)) want = "en";
    if (want == g_current) return;
    g_current = want;
    g_log("voices: %s", want == "en" ? "English (the game's own)" : FileOf(want).c_str());
    // The sound files are open while the game runs (g_AppRunning): reopen them now,
    // as after alt-tab. Before that, the game's first open picks the choice up.
    if (*(short*)kAppRunning) {
        ((void(__fastcall*)(void*))kCloseSounds)((void*)kSoundPlayer);
        ((short(__fastcall*)(void*))kOpenSounds)((void*)kSoundPlayer);
    }
}

const std::string& Current() { return g_current; }

}  // namespace voices
