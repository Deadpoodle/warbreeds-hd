// Skirmish: a map made in the editor keeps its own Global AI settings.
//
// Every computer player carries squad aggression, base, scouting and AI
// influence (AIComputerPlayer +0x4d84..+0x4d90, with a fifth float at +0x4d80),
// saved in the map's AI state and set in the editor's AI tab. The skirmish
// setup (APlayerRegistry::SetupSkirmish 0x43bd30, called by SGame::LoadStage
// at 0x47aec9) applies each slot to its map player (ApplySlotToPlayer
// 0x43c130), which gives every computer player its clan's default aggression
// (ConfigureAI 0x43c2f0, called at 0x43c275): a map's own settings were never
// used in a skirmish. For a map that isn't shipped with the game, the values a
// computer player had when the map loaded are put back after ConfigureAI. The
// shipped maps play as they always have.
#include "skirmish.h"

#include <windows.h>
#include <stdint.h>
#include <string.h>

#include <string>

#include "editor/mapfile.h"
#include "game.h"
#include "skirmish_teams.h"

namespace skirmish {
namespace {

void (*g_log)(const char*, ...);

constexpr uintptr_t kSetupSkirmishCall = 0x47aec9, kSetupSkirmish = 0x43bd30;
constexpr uintptr_t kConfigureAICall = 0x43c275, kConfigureAI = 0x43c2f0;
constexpr uintptr_t kAI = 0x4d80;  // five floats: (unused 0.5), squad, base, scouting, influence

void* g_registry;     // APlayerRegistry being set up
float g_saved[8][5];  // each map player's AI values as the map loaded them
bool g_have[8];       // that player was a computer in the map
bool g_custom;        // the map being set up isn't a shipped one
std::string g_map;

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

char* Player(int i) { return g_registry ? *(char**)((char*)g_registry + 4 + 4 * i) : nullptr; }

bool IsHuman(char* p) { return ((char(__thiscall*)(void*))(*(void***)p)[0x7c / 4])(p) == 1; }

// The skirmish's map: the single-player command model's GameSettings
// (CommandModel +4, g_SinglePlayerModel 0x5511c8), map file name at +0x170.
constexpr uintptr_t kSettingsMapPath = 0x5511c8 + 4 + 0x170;

std::string MapBaseName() {
    const char* raw = (const char*)kSettingsMapPath;
    std::string path(raw, strnlen(raw, 0x80));
    size_t slash = path.find_last_of("\\/");
    std::string base = slash == std::string::npos ? path : path.substr(slash + 1);
    size_t dot = base.find_last_of('.');
    return dot == std::string::npos ? base : base.substr(0, dot);
}

void __fastcall HookSetupSkirmish(void* registry) {
    g_registry = registry;
    g_map = MapBaseName();
    g_custom = !g_map.empty() && !editor::IsShippedMapName(g_map);
    for (int i = 0; i < 8; ++i) {
        char* p = Player(i);
        g_have[i] = p && !IsHuman(p);
        if (g_have[i]) memcpy(g_saved[i], p + kAI, sizeof g_saved[i]);
    }
    g_log("skirmish: map %s (%s)", g_map.c_str(), g_custom ? "custom: keeps its Global AI settings" : "shipped");
    ((void(__thiscall*)(void*))kSetupSkirmish)(registry);
    teams::Apply(registry);  // allied teams chosen on the setup screen
    g_registry = nullptr;
}

// ConfigureAI is a thiscall on the registry with the player on the stack (it
// pops it): taken as fastcall, the second register argument unused.
void __fastcall HookConfigureAI(void* registry, void*, char* ai) {
    ((void(__thiscall*)(void*, char*))kConfigureAI)(registry, ai);
    if (!g_custom) return;
    for (int i = 0; i < 8; ++i) {
        if (Player(i) != ai || !g_have[i]) continue;
        memcpy(ai + kAI, g_saved[i], sizeof g_saved[i]);
        const float* f = g_saved[i];
        g_log("skirmish: player %d keeps the map's AI: squad %.2f, base %.2f, scouting %.2f, influence %.2f", i, f[1],
              f[2], f[3], f[4]);
    }
}

}  // namespace

void Install(void (*log)(const char*, ...)) {
    g_log = log;
    bool ok = PatchCall(kSetupSkirmishCall, kSetupSkirmish, (void*)&HookSetupSkirmish) &&
              PatchCall(kConfigureAICall, kConfigureAI, (void*)&HookConfigureAI);
    log("skirmish: map AI settings hooks %s", ok ? "installed" : "FAILED (unexpected code)");
}

}  // namespace skirmish
