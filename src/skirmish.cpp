// Skirmish: a map made in the editor keeps its own Global AI settings, and the
// Esc menu's Objectives lists the enemies to defeat.
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

#include <algorithm>
#include <vector>

#include "editor/mapfile.h"
#include "game.h"
#include "fonts.h"
#include "lang.h"
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

void StartObjectives();

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
    StartObjectives();
}

// --- Objectives -------------------------------------------------------------
// A skirmish has no objectives: the Esc menu's Objectives (SMenu::OnCommand,
// g_Campaign == 0) shows the stock "No Objectives" box, ShowDialog 0x471950(613
// title, 612 text, 612, 0, 0x1f) at 0x4898ce, which loads its strings through
// LoadStringA. In a skirmish it says instead whom to defeat: every player not
// allied with you, by clan, each marked once defeated. Defeated players are
// deleted from the registry, so the enemies are noted when the skirmish starts
// (after the teams are applied) and again whenever the box opens (a loaded game).
constexpr uintptr_t kObjectivesCall = 0x4898ce, kShowDialog = 0x471950;
constexpr uintptr_t kRegistry = 0x57927c, kLocalPlayer = 0x46f8e0, kIsSkirmish = 0x46f960;
constexpr uintptr_t kPlayerId = 4, kAllyMask = 0x18, kClan = 0x4878, kAlive = 0x48f6;
constexpr int kTitleId = 613, kTextId = 612;

struct Foe {
    int id, clan;
};
std::vector<Foe> g_foes;

char* RegistryPlayer(int i) {
    char* reg = *(char**)kRegistry;
    return reg ? *(char**)(reg + 4 + 4 * i) : nullptr;
}

// Not on your side: either of you doesn't count the other an ally.
bool Enemy(char* me, char* p) {
    int a = *(uint16_t*)(me + kPlayerId) & 7, b = *(uint16_t*)(p + kPlayerId) & 7;
    return !((*(uint8_t*)(me + kAllyMask) >> b & 1) && (*(uint8_t*)(p + kAllyMask) >> a & 1));
}

void NoteFoes() {
    char* me = ((char* (*)())kLocalPlayer)();
    if (!me) return;
    for (int i = 0; i < 8; ++i) {
        char* p = RegistryPlayer(i);
        if (!p || p == me || !*(uint16_t*)(p + kAlive)) continue;
        int id = *(uint16_t*)(p + kPlayerId);
        bool known = false;
        for (const Foe& f : g_foes) known |= f.id == id;
        if (!known && Enemy(me, p)) g_foes.push_back({id, *(int*)(p + kClan)});
    }
}

void StartObjectives() {  // a new skirmish: its enemies, once the teams are set
    g_foes.clear();
    NoteFoes();
}

const char* ClanName(int clan) {
    static const char* const kNames[] = {"Tanu", "Sen-Soth", "Kelika", "Magha"};
    return clan >= 0 && clan < 4 ? kNames[clan] : "Yedda";
}

// The message box (SDialog, static 0x5674e8; ShowDialog's text ids are a range,
// first to last, one text line per id: 0x478040) and its widgets.
constexpr uintptr_t kDialog = 0x5674e8, kDialogLines = 0x44, kDialogLineCount = 0x48;
constexpr uintptr_t kAddText = 0x495950, kSetStyle = 0x499fb0;  // Scene::AddText(text); WText::SetStyle(style)
constexpr uintptr_t kVSetPosition = 0x10;                       // Widget (x, y)
constexpr int kFirstLineId = 900;  // spare string ids for the lines

void __cdecl HookObjectives(int title, int text, int text2, int a4, int a5) {
    typedef void(__cdecl * ShowDialog_t)(int, int, int, int, int);
    if (!((short (*)())kIsSkirmish)()) {
        ((ShowDialog_t)kShowDialog)(title, text, text2, a4, a5);
        return;
    }
    NoteFoes();
    std::vector<std::string> lines;
    std::vector<bool> done;
    char* me = ((char* (*)())kLocalPlayer)();
    for (size_t k = 0; k < g_foes.size(); ++k) {
        const Foe& f = g_foes[k];
        // Two enemies of one clan: numbered.
        int same = 0, nth = 0;
        for (size_t j = 0; j < g_foes.size(); ++j)
            if (g_foes[j].clan == f.clan) ++same, nth += j <= k;
        std::string name = ClanName(f.clan);
        if (same > 1) name += " " + std::to_string(nth);
        char* p = nullptr;
        for (int i = 0; i < 8; ++i)
            if (RegistryPlayer(i) && *(uint16_t*)(RegistryPlayer(i) + kPlayerId) == f.id) p = RegistryPlayer(i);
        bool alive = p && *(uint16_t*)(p + kAlive);
        if (alive && me && !Enemy(me, p)) continue;  // now an ally
        lines.push_back(lang::trf("Defeat the {1}", {name}));
        done.push_back(!alive);
    }
    if (lines.empty()) lines.push_back(lang::tr("No enemies are left.")), done.push_back(false);
    lang::OverrideString(kTitleId, lang::tr("Objectives"));
    for (size_t i = 0; i < lines.size(); ++i) lang::OverrideString(kFirstLineId + (int)i, lines[i]);
    ((ShowDialog_t)kShowDialog)(title, kFirstLineId, kFirstLineId + (int)lines.size() - 1, a4, a5);
    lang::ClearOverrides();
    // A green check mark at the right of each defeated enemy's line.
    char** widgets = *(char***)(kDialog + kDialogLines);
    uint32_t count = *(uint32_t*)(kDialog + kDialogLineCount);
    for (size_t i = 0; i < lines.size() && i < count && widgets; ++i) {
        if (!done[i]) continue;
        short* r = (short*)(widgets[i] + 0xc);  // left, top, right, bottom
        char* tick = ((char*(__thiscall*)(void*, const char*))kAddText)((void*)kDialog, "\xC3");  // Mac Roman: a check (square root)
        if (!tick) continue;
        ((void(__thiscall*)(void*, int))kSetStyle)(tick, fonts::kStyleGreen);
        ((void(__thiscall*)(void*, int, int))(*(void***)tick)[kVSetPosition / 4])(tick, r[0] + 2 * 0x9c - 14, r[1]);
    }
    g_log("skirmish: objectives: %d enemies, %d defeated", (int)lines.size(), (int)std::count(done.begin(), done.end(), true));
}

// --- "The X are defeated" --------------------------------------------------
// The game says nothing when a player is defeated (APlayerRegistry::PlayerDefeated
// 0x43b780: an active player, +0x48f6 == 1, is deactivated). Each of its five
// callers (a player's tick, a thing removed, DeletePlayer, the recheck, a removed
// machine) is hooked: once a player goes from active to defeated, the local player
// gets a message in the game's message line (APlayer::PostMessage 0x439600, shown
// for 300 turns; sent as from the local player, as a defeated player may be
// deleted at once and a message from no one isn't drawn), and the log says so.
// Not while a game is set up (empty slots are removed), in its first 20 turns (an
// empty slot's player is defeated at its first tick) or shut down (every player).
constexpr uintptr_t kPlayerDefeated = 0x43b780, kPostMessage = 0x439600;
constexpr uintptr_t kGameTurn = 0x4708b0, kAppRunning = 0x5792c8;
constexpr uintptr_t kDefeatedCalls[] = {0x435fe9, 0x43774c, 0x43abfe, 0x43bb87, 0x463047};

void __fastcall HookPlayerDefeated(char* registry, void*, int id, int recheck) {
    char* p = *(char**)(registry + 4 + 4 * (id & 7));
    bool wasActive = p && *(uint16_t*)(p + kAlive) == 1;
    int clan = p ? *(int*)(p + kClan) : -1;
    ((void(__thiscall*)(void*, int, int))kPlayerDefeated)(registry, id, recheck);
    if (!wasActive || g_registry || ((int (*)())kGameTurn)() < 20 || !*(int*)kAppRunning) return;
    char* me = ((char* (*)())kLocalPlayer)();
    // The clan's name, numbered if another player shares the clan.
    std::string name = ClanName(clan);
    for (int i = 0; i < 8; ++i) {
        char* o = RegistryPlayer(i);
        if (o && o != p && *(int*)(o + kClan) == clan) {
            name += " " + std::to_string((id & 7) + 1);
            break;
        }
    }
    g_log("skirmish: player %d (%s) defeated", id & 7, name.c_str());
    if (!me) return;  // watching or gone: no message line
    std::string text = lang::trf("The {1} are defeated", {name});
    if (text.size() > 80) text.resize(80);  // the message holds 82 bytes
    ((void(__thiscall*)(void*, short, int, const char*))kPostMessage)(me, *(short*)(me + kPlayerId), 0xff, text.c_str());
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
    bool objectives = PatchCall(kObjectivesCall, kShowDialog, (void*)&HookObjectives);
    log("skirmish: objectives %s", objectives ? "installed" : "FAILED (unexpected code)");
    bool defeated = true;
    for (uintptr_t site : kDefeatedCalls) defeated &= PatchCall(site, kPlayerDefeated, (void*)&HookPlayerDefeated);
    log("skirmish: defeat messages %s", defeated ? "installed" : "FAILED (unexpected code)");
}

}  // namespace skirmish
