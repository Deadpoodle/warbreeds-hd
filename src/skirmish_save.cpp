// Skirmish: Load Game and Save Game on the Esc menu, as in a campaign.
//
// The game saves a skirmish as well as a mission: a .wbs is the map stream
// (SGame::SerializeAll 0x4812c0, stages 2..8), and only the Esc menu's
// skirmish layout leaves the two buttons out (SMenu::Init, 0x4895a8: in a
// skirmish, the short layout). What the file doesn't hold is that it was a
// skirmish (SGame +0x82, which plays by different rules: genes, the Yedda,
// player colours, where the result screen leads), who the human was (a load
// takes the first player), where the view was (kept only for a local player)
// and the skirmish setup (the result screen goes back to it). Those go in a
// small text file beside the save, <name>.wbs.hd, and
// are put back once the load's last stage has run (SGame::LoadStage stage 8:
// with the flag down it picks a local player and leaves the world as saved;
// with it up it would set the skirmish up afresh over the loaded one). A save
// without one loads as before. See re/save-load.md.
#include "skirmish_save.h"

#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <string>

#include "skirmish_teams.h"

namespace skirmish_save {
namespace {

void (*g_log)(const char*, ...);

constexpr uintptr_t kSGame = 0x567ef8;
constexpr int kIsSkirmish = 0x82, kSavePath = 0x88, kLoadPath = 0x1fe;
constexpr uintptr_t kRegistry = 0x57927c, kLocalPlayer = 0x579288, kCampaign = 0x57928c, kCommandModel = 0x579260;
constexpr int kCameraX = 0x38, kCameraY = 0x3a, kViewW = 0x4c, kViewH = 0x4e;  // SGame, world pixels
// The skirmish setup: the single-player command model's GameSettings (CSinglePlayer
// 0x5511c8 +4): the eight slots, the map path (+0x170) and the options (+0x214..+0x224).
constexpr uintptr_t kSettings = 0x5511cc;
constexpr int kSettingsSize = 0x230;

constexpr uintptr_t kMenuLayoutJump = 0x4895b0;  // jne 0x48968c: the skirmish layout
constexpr uintptr_t kSerializeAllCall = 0x480dcb, kSerializeAll = 0x4812c0;
constexpr uintptr_t kLoadInMissionCall = 0x47cf7f, kLoadFromMenuCall = 0x487bd3, kGameStart = 0x470780;
constexpr uintptr_t kLoadStageCall = 0x492716, kLoadStage = 0x46f930;
constexpr uintptr_t kCentreOn = 0x480340;  // thiscall(SGame, short x, short y): world pixels

// The skirmish a load is bringing back (read from the save's .hd before the stages run).
struct Pending {
    bool active;
    int local;     // the human's player index
    int watching;  // the human's index while watching an ally after defeat, else -1
    int viewX, viewY;  // the view's centre (world pixels); the game keeps it only for a
                       // local player (none while watching)
    uint8_t settings[kSettingsSize];
} g_pending;

short& Flag() { return *(short*)(kSGame + kIsSkirmish); }

bool SinglePlayer() {
    void* cmd = *(void**)kCommandModel;
    return cmd && ((int(__thiscall*)(void*))(*(uintptr_t**)cmd)[0x18 / 4])(cmd) == 0x32;
}

char* Player(int i) {
    char* reg = *(char**)kRegistry;
    return reg && i >= 0 && i < 8 ? *(char**)(reg + 4 + 4 * i) : nullptr;
}

std::string Sidecar(const char* save) {
    char full[MAX_PATH];
    DWORD n = GetFullPathNameA(save, sizeof full, full, nullptr);
    return std::string(n && n < sizeof full ? full : save) + ".hd";
}

bool IsSave(const char* path) {
    size_t n = strlen(path);
    return n > 4 && _stricmp(path + n - 4, ".wbs") == 0;
}

// After the stream is written: the skirmish's own state beside it (or, for any
// other save, no stale one left from an earlier skirmish of that name).
void __fastcall HookSerializeAll(void* sg, void*, void* stream) {
    ((void(__thiscall*)(void*, void*))kSerializeAll)(sg, stream);
    const char* path = (const char*)sg + kSavePath;
    if (!IsSave(path)) return;  // the map editor's .wbm
    std::string side = Sidecar(path);
    if (!Flag() || !SinglePlayer()) {
        DeleteFileA(side.c_str());
        return;
    }
    char* me = *(char**)kLocalPlayer;
    int local = me ? *(short*)(me + 4) : -1;
    int watching = teams::Watching();
    if (local < 0) local = watching;
    FILE* f = fopen(side.c_str(), "w");
    if (!f) {
        g_log("skirmish save: can't write %s", side.c_str());
        return;
    }
    char* g = (char*)sg;
    fprintf(f, "# WarBreeds HD: the save beside this file is a skirmish. The .wbs is the game's own\n"
               "# format; this holds what it doesn't: the human player and the skirmish setup.\n"
               "[Skirmish]\nLocalPlayer=%d\nWatching=%d\nView=%d,%d\nSettings=",
            local, watching, *(short*)(g + kCameraX) + *(short*)(g + kViewW) / 2,
            *(short*)(g + kCameraY) + *(short*)(g + kViewH) / 2);
    for (int i = 0; i < kSettingsSize; ++i) fprintf(f, "%02x", ((uint8_t*)kSettings)[i]);
    fprintf(f, "\n");
    fclose(f);
    g_log("skirmish save: %s (player %d%s)", side.c_str(), local, watching >= 0 ? ", watching" : "");
}

bool ReadSidecar(const char* save, Pending& p) {
    FILE* f = fopen(Sidecar(save).c_str(), "r");
    if (!f) return false;
    p = {};
    p.watching = p.viewX = p.viewY = -1;
    int got = 0;
    char line[2 * kSettingsSize + 64];
    while (fgets(line, sizeof line, f)) {
        if (sscanf(line, "LocalPlayer=%d", &p.local) == 1) got |= 1;
        sscanf(line, "Watching=%d", &p.watching);
        sscanf(line, "View=%d,%d", &p.viewX, &p.viewY);
        if (strncmp(line, "Settings=", 9) == 0) {
            int i = 0;
            for (unsigned b; i < kSettingsSize && sscanf(line + 9 + 2 * i, "%2x", &b) == 1; ++i) p.settings[i] = (uint8_t)b;
            if (i == kSettingsSize) got |= 2;
        }
    }
    fclose(f);
    p.active = got == 3 && p.local >= 0 && p.local < 8;
    return p.active;
}

// Game_Start(0x32) for a load (from the Esc menu or the main menu): every load
// starts with the skirmish flag down, so the last stage keeps the world as saved
// (a campaign save loaded from inside a skirmish used to be set up as a skirmish).
void __cdecl HookStartLoad(int mode) {
    const char* path = (const char*)kSGame + kLoadPath;
    Flag() = 0;
    teams::SetWatching(-1);
    if (ReadSidecar(path, g_pending)) {
        *(void**)kCampaign = nullptr;  // no campaign carried into a skirmish (it would advance on a win)
        g_log("skirmish save: loading %s as a skirmish (player %d)", path, g_pending.local);
    }
    ((void(__cdecl*)(int))kGameStart)(mode);
}

void __cdecl HookLoadStage(int stage) {
    ((void(__cdecl*)(int))kLoadStage)(stage);
    if ((uint8_t)stage != 8 || !g_pending.active) return;
    g_pending.active = false;
    memcpy((void*)kSettings, g_pending.settings, kSettingsSize);
    Flag() = 1;
    char* me = Player(g_pending.local);
    if (g_pending.watching >= 0) {
        *(char**)kLocalPlayer = nullptr;
        teams::SetWatching(g_pending.watching);
    } else if (me) {
        *(char**)kLocalPlayer = me;
    }
    // Where the player was looking (camera::InstallKeepCentre keeps it when the
    // mission's frame replaces the loading screen's).
    if (g_pending.viewX >= 0 && g_pending.viewY >= 0)
        ((void(__thiscall*)(void*, int, int))kCentreOn)((void*)kSGame, g_pending.viewX, g_pending.viewY);
    g_log("skirmish save: loaded as a skirmish, player %d%s; view %d,%d", g_pending.local,
          g_pending.watching >= 0 ? " (watching)" : me ? "" : " (NOT FOUND)", g_pending.viewX, g_pending.viewY);
}

// SMenu::OnCommand (vtable slot 0x5079a0): Save Game while watching an ally after
// defeat is refused, as the game refuses Alliances then ("You are no longer in
// the game."): the defeated player's last things would load without an owner.
constexpr uintptr_t kMenuCommandSlot = 0x5079a0, kMenuCommand = 0x489830;
constexpr int kMenuSaveButton = 0x60;
constexpr uintptr_t kCanShowDialog = 0x4982e0;  // thiscall(scene) -> 1
constexpr uintptr_t kShowDialog = 0x471950;     // cdecl (title, text, text, 0, 0x1f): string ids
constexpr int kStrSaveGame = 475, kStrNotInGame = 622;

void __fastcall HookMenuCommand(char* menu, void*, void* widget) {
    if (widget && widget == *(void**)(menu + kMenuSaveButton) && Flag() && SinglePlayer() &&
        !*(char**)kLocalPlayer) {
        if (((short(__thiscall*)(void*))kCanShowDialog)(menu) == 1)
            ((void(__cdecl*)(int, int, int, int, int))kShowDialog)(kStrSaveGame, kStrNotInGame, kStrNotInGame, 0, 0x1f);
        return;
    }
    ((void(__thiscall*)(void*, void*))kMenuCommand)(menu, widget);
}

bool PatchSlot(uintptr_t slot, uintptr_t expect, void* hook) {
    if (*(uintptr_t*)slot != expect) return false;
    DWORD old;
    if (!VirtualProtect((void*)slot, 4, PAGE_READWRITE, &old)) return false;
    *(void**)slot = hook;
    VirtualProtect((void*)slot, 4, old, &old);
    return true;
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

// The Esc menu: a skirmish takes the campaign layout (with Load Game and Save Game).
bool PatchMenu() {
    const uint8_t expect[6] = {0x0f, 0x85, 0xd6, 0x00, 0x00, 0x00};
    uint8_t* p = (uint8_t*)kMenuLayoutJump;
    if (memcmp(p, expect, sizeof expect) != 0) return false;
    DWORD old;
    if (!VirtualProtect(p, sizeof expect, PAGE_EXECUTE_READWRITE, &old)) return false;
    memset(p, 0x90, sizeof expect);
    VirtualProtect(p, sizeof expect, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, sizeof expect);
    return true;
}

}  // namespace

void Install(void (*log)(const char*, ...)) {
    g_log = log;
    bool ok = PatchCall(kSerializeAllCall, kSerializeAll, (void*)&HookSerializeAll) &&
              PatchCall(kLoadInMissionCall, kGameStart, (void*)&HookStartLoad) &&
              PatchCall(kLoadFromMenuCall, kGameStart, (void*)&HookStartLoad) &&
              PatchCall(kLoadStageCall, kLoadStage, (void*)&HookLoadStage) &&
              PatchSlot(kMenuCommandSlot, kMenuCommand, (void*)&HookMenuCommand) && PatchMenu();
    log("skirmish save: %s", ok ? "installed" : "FAILED (unexpected code)");
}

}  // namespace skirmish_save
