// Skirmish: allied teams and player colours on the setup screen.
//
// SMultiSetup (vtable 0x508214) is both the skirmish setup (+0x292e == 0) and
// the multiplayer lobby. Every row already has the lobby's Team and Color
// widgets (WStatic, 0x2c bytes each: team at +0x279c, colour at +0x237c,
// indexed by slot), created and laid out in both modes; only the lobby draws
// and clicks them. Here, in skirmish only:
//   - Draw: after each row (call at 0x48cc3e to 0x48dc80(this, row, slot)) the
//     team and colour icons are drawn too, with the lobby's "Team" and "Color"
//     headers added after Init (vtable slot 5, 0x48c290).
//   - Click (LButtonDown, vtable slot 7, 0x48d2c0): a row's team cycles none,
//     1..4 (slot +0x1e, which the lobby uses too); its colour cycles through the
//     eight player colours, skipping ones another row picked. Right-click
//     (vtable slot 9, unused in the stock game) puts a colour back to
//     automatic. Every row can be changed, the computer players' too.
//   - Colours: the stock rule (0x48d600, at Start: each clan's own colour, the
//     human first, spares for duplicates) still gives every row nobody picked
//     its colour; picks override it, and automatic rows move off a picked
//     colour. The screen previews the result; a row whose clan is Random gets
//     its colour at Start.
//   - Start (OnCommand, vtable slot 27, 0x48ce30): refused with the lobby's
//     "All players are allied." when every row shares one team. The start
//     shuffles (calls at 0x48d1f0 random, 0x48d205 map default) move each row
//     (clan, human, start ordinal) to another slot; teams and colour picks
//     follow their row. With random placement each team then gets neighbouring
//     start points.
//   - In the game (Apply, from skirmish.cpp after SetupSkirmish): when any
//     team is set, every player's ally mask is reset to itself (undoing the
//     stock "all computer players allied") and teammates are allied both ways
//     (APlayer +0x4a30 team, ApplyTeams 0x43c8f0). With no teams set nothing
//     changes.
//   - Defeat (PlayerDefeated 0x43b780): in single player the human's defeat
//     ends the game at once. In a skirmish where an ally still lives, the human
//     watches instead (the multiplayer path: no local player, map revealed),
//     and the result screen shows Victory if that team wins (DeclareWinner's
//     call to Game_EndMission at 0x43bb64).
//   - Result screen: the players' discs show their own colours (PatchRewardColours).
#include "skirmish_teams.h"

#include <windows.h>
#include <stdint.h>
#include <string.h>

#include <algorithm>
#include <string>

namespace teams {
namespace {

void (*g_log)(const char*, ...);

// SMultiSetup
constexpr uintptr_t kVtbl = 0x508214;
constexpr uintptr_t kSlotInit = kVtbl + 5 * 4, kInit = 0x48c290;
constexpr uintptr_t kSlotLDown = kVtbl + 7 * 4, kLDown = 0x48d2c0;
constexpr uintptr_t kSlotRDown = kVtbl + 9 * 4, kRDownStock = 0x435a70;  // shared "return 0"
constexpr uintptr_t kSlotCommand = kVtbl + 27 * 4, kCommand = 0x48ce30;
constexpr uintptr_t kRowDrawCall = 0x48cc3e, kRowDraw = 0x48dc80;
constexpr uintptr_t kRandomStartsCall = 0x48d1f0, kRandomStarts = 0x48f530;
constexpr uintptr_t kDefaultStartsCall = 0x48d205, kDefaultStarts = 0x48f880;
constexpr uintptr_t kColoursCall = 0x48d53e, kColours = 0x48d600;
constexpr uintptr_t kVisibleRows = 0x48cd60;  // fastcall(this): rows shown in skirmish (AI count + 1)
constexpr uintptr_t kUsedSlot = 0x48cde0;     // thiscall(this, slot) -> slot info or 0
constexpr uintptr_t kClansByRow = 0x48f940, kOrdinals = 0x48f8b0, kLayout = 0x48c9c0;  // fastcall(this)
constexpr uintptr_t kClickSound = 0x46fa70;
constexpr uintptr_t kCanShowDialog = 0x4982e0;  // thiscall(scene) -> 1
constexpr uintptr_t kShowDialog = 0x471950;     // cdecl (title, text, text, 0, 0x1f)
constexpr uintptr_t kAddText = 0x495950, kCentre = 0x49a8b0, kTextStyle = 0x499fb0;
constexpr uintptr_t kStrings = 0x55dd54;
constexpr int kStrTeam = 0x1c6, kStrColor = 0x1c7;
constexpr int kStrGameOptions = 0x218, kStrAllAllied = 0x21e;

constexpr int kTeamWidgets = 0x279c, kColourWidgets = 0x237c, kWidgetSize = 0x2c;
constexpr int kPerm = 0x2968, kRandomClan = 0x2958, kStartButton = 0x28fc;
constexpr int kTeamFrame = 0x24, kColourFrame = 6;

// The skirmish's slots: g_SinglePlayerModel (0x5511c8) +4, 8 x 0x28 bytes.
constexpr uintptr_t kSlots = 0x5511cc;
constexpr int kSlotSize = 0x28, kSlotClan = 0x1c, kSlotColour = 0x1d, kSlotTeam = 0x1e;
constexpr int kTeams = 5;  // 0 = none, 1..4

// Players
constexpr uintptr_t kRegistry = 0x57927c, kLocalPlayer = 0x579288, kCommandModel = 0x579260;
constexpr uintptr_t kPlayerBits = 0x53e0e8;
constexpr uintptr_t kApplyTeams = 0x43c8f0;  // fastcall(registry)
constexpr uintptr_t kIsSkirmish = 0x567f7a;  // SGame +0x82
constexpr int kAllyMask = 0x18, kAlive = 0x48f6, kTeam = 0x4a30, kStartX = 0x4a1c, kStartY = 0x4a1e;
constexpr uintptr_t kDefeatSite = 0x43b7c0;  // GetMode() == 0x32 test in PlayerDefeated
constexpr uintptr_t kEndMissionCall = 0x43bb64, kEndMission = 0x470820;

// The eight player colours, in the order the colour column cycles them.
const uint8_t kPalette[8] = {0xbf, 0xc7, 0xcf, 0xd7, 0xdf, 0xe7, 0xef, 0xf7};
// The order the stock rule hands out spares in.
const uint8_t kSpares[8] = {0xf7, 0xdf, 0xcf, 0xc7, 0xe7, 0xd7, 0xef, 0xbf};

int g_pick[8] = {-1, -1, -1, -1, -1, -1, -1, -1};  // colour picked for each slot, -1 = automatic
int g_lastRow = 99;     // rows drawn this pass (a smaller row starts a new pass)
int g_spectateId = -1;  // the human, watching after defeat with an ally alive

char* Slot(int i) { return (char*)(kSlots + i * kSlotSize); }
bool Skirmish(void* s) { return *(short*)((char*)s + 0x292e) == 0; }
bool MapLoaded(void* s) { return *(short*)((char*)s + 0x292c) != 0; }
int Perm(void* s, int r) { return *(uint8_t*)((char*)s + kPerm + r * 2); }
void* Widget(void* s, int base, int w) { return (char*)s + base + w * kWidgetSize; }
bool Used(void* s, int slot) { return ((char*(__thiscall*)(void*, int))kUsedSlot)(s, slot) != nullptr; }
int VisibleRows(void* s) { return ((short(__fastcall*)(void*))kVisibleRows)(s); }
bool ValidColour(int c) { return c >= 0xbf && c <= 0xf7 && (c - 0xbf) % 8 == 0; }
bool RandomClan(void* s, int w) {
    return Slot(w)[kSlotClan] == 4 || *(short*)((char*)s + kRandomClan + w * 2) == 1;
}
int Team(int w) {
    int t = (uint8_t)Slot(w)[kSlotTeam];
    return t < kTeams ? t : 0;
}

// The visible rows, top to bottom, as slot indices (as the row drawing picks them).
int VisibleSlots(void* s, int out[8]) {
    int n = 0, max = VisibleRows(s);
    for (int r = 0; r < 8 && n < max; ++r) {
        int w = Perm(s, r);
        if (Used(s, w)) out[n++] = w;
    }
    return n;
}

bool Hit(void* widget, int x, int y) {
    return ((short(__thiscall*)(void*, int, int))(*(uintptr_t**)widget)[0xc / 4])(widget, x, y) == 1;
}

void DrawIcon(void* scene, void* widget, int frame) {
    void* sheet = *(void**)((char*)scene + 0x18);
    ((void(__thiscall*)(void*, void*, int))(*(uintptr_t**)widget)[0x24 / 4])(widget, sheet, frame);
    ((void(__thiscall*)(void*))(*(uintptr_t**)widget)[4 / 4])(widget);
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

bool PatchSlot(uintptr_t slot, uintptr_t expect, void* hook) {
    if (*(uintptr_t*)slot != expect) return false;
    DWORD old;
    if (!VirtualProtect((void*)slot, 4, PAGE_READWRITE, &old)) return false;
    *(void**)slot = hook;
    VirtualProtect((void*)slot, 4, old, &old);
    return true;
}

// --- colours ------------------------------------------------------------------

bool ColourTaken(int c, int except) {
    for (int i = 0; i < 8; ++i)
        if (i != except && g_pick[i] == c) return true;
    return false;
}

// After the stock rule has coloured every slot: picks win, and an automatic
// slot on a picked colour (or one another slot already has) moves to a spare.
void ApplyPicks(void* scene) {
    bool used[8] = {};
    bool any = false;
    for (int i = 0; i < 8; ++i)
        if (Used(scene, i) && g_pick[i] >= 0) {
            Slot(i)[kSlotColour] = (char)g_pick[i];
            used[(g_pick[i] - 0xbf) / 8] = true;
            any = true;
        }
    if (!any) return;
    for (int i = 0; i < 8; ++i) {
        if (!Used(scene, i) || g_pick[i] >= 0) continue;
        int c = (uint8_t)Slot(i)[kSlotColour];
        if (ValidColour(c) && !used[(c - 0xbf) / 8]) {
            used[(c - 0xbf) / 8] = true;
            continue;
        }
        for (uint8_t s : kSpares)
            if (!used[(s - 0xbf) / 8]) {
                Slot(i)[kSlotColour] = (char)s;
                used[(s - 0xbf) / 8] = true;
                break;
            }
    }
}

void __fastcall HookColours(void* scene) {
    ((void(__fastcall*)(void*))kColours)(scene);
    ApplyPicks(scene);
    for (int i = 0; i < 8; ++i)
        if (Used(scene, i))
            g_log("teams: slot %d colour %#x (%s), team %d", i, (uint8_t)Slot(i)[kSlotColour],
                  g_pick[i] >= 0 ? "picked" : "automatic", Team(i));
}

// --- setup screen -------------------------------------------------------------

void __fastcall HookRowDraw(void* scene, void*, int row, char* slot) {
    ((void(__thiscall*)(void*, int, char*))kRowDraw)(scene, row, slot);
    if (!Skirmish(scene)) return;
    row &= 0xff;
    if (row <= g_lastRow) {  // a new pass: preview the colours everyone will start with
        ((void(__fastcall*)(void*))kColours)(scene);
        ApplyPicks(scene);
    }
    g_lastRow = row;
    int w = Perm(scene, row);
    DrawIcon(scene, Widget(scene, kTeamWidgets, w), kTeamFrame + Team(w));
    int c = (uint8_t)Slot(w)[kSlotColour];
    if (ValidColour(c) && (g_pick[w] >= 0 || !RandomClan(scene, w)))
        DrawIcon(scene, Widget(scene, kColourWidgets, w), kColourFrame + (c - 0xbf) / 8);
}

void AddHeader(void* scene, int str, int dx) {
    const char* text = *(const char**)((*(char***)kStrings)[str]);
    void* t = ((void*(__thiscall*)(void*, const char*))kAddText)(scene, text);
    if (!t) return;
    ((void(__thiscall*)(void*))kCentre)(t);
    ((void(__thiscall*)(void*, int))kTextStyle)(t, 1);
    ((void(__thiscall*)(void*, int, int))(*(uintptr_t**)t)[0x14 / 4])(t, dx, -0xba);
}

short __thiscall HookInit(void* scene) {
    short r = ((short(__thiscall*)(void*))kInit)(scene);
    if (Skirmish(scene)) {
        AddHeader(scene, kStrTeam, -0xff);
        AddHeader(scene, kStrColor, -0xd6);
    }
    g_lastRow = 99;
    return r;
}

void CycleColour(int w) {
    int c = g_pick[w] >= 0 ? g_pick[w] : (uint8_t)Slot(w)[kSlotColour];
    int at = ValidColour(c) ? (c - 0xbf) / 8 : 7;
    for (int k = 1; k <= 8; ++k) {
        int next = kPalette[(at + k) % 8];
        if (!ColourTaken(next, w)) {
            g_pick[w] = next;
            return;
        }
    }
}

short __thiscall HookLDown(void* scene, int x, int y) {
    if (Skirmish(scene) && MapLoaded(scene)) {
        int rows[8], n = VisibleSlots(scene, rows);
        for (int i = 0; i < n; ++i) {
            int w = rows[i];
            if (Hit(Widget(scene, kTeamWidgets, w), x, y)) {
                ((void(*)())kClickSound)();
                Slot(w)[kSlotTeam] = (char)((Team(w) + 1) % kTeams);
                g_log("teams: row %d (slot %d) team %d", i, w, Team(w));
                return 1;
            }
            if (Hit(Widget(scene, kColourWidgets, w), x, y)) {
                ((void(*)())kClickSound)();
                CycleColour(w);
                g_log("teams: row %d (slot %d) colour %#x picked", i, w, g_pick[w]);
                return 1;
            }
        }
    }
    return ((short(__thiscall*)(void*, int, int))kLDown)(scene, x, y);
}

short __thiscall HookRDown(void* scene, int x, int y) {
    if (Skirmish(scene) && MapLoaded(scene)) {
        int rows[8], n = VisibleSlots(scene, rows);
        for (int i = 0; i < n; ++i) {
            int w = rows[i];
            if (Hit(Widget(scene, kColourWidgets, w), x, y)) {
                ((void(*)())kClickSound)();
                g_pick[w] = -1;
                g_log("teams: row %d (slot %d) colour automatic", i, w);
                return 1;
            }
        }
    }
    return ((short(__thiscall*)(void*, int, int))kRDownStock)(scene, x, y);
}

int __thiscall HookCommand(void* scene, void* button) {
    if (Skirmish(scene) && MapLoaded(scene) && button && button == *(void**)((char*)scene + kStartButton)) {
        int rows[8], n = VisibleSlots(scene, rows);
        bool same = n > 1 && Team(rows[0]) != 0;
        for (int i = 1; i < n && same; ++i) same = Team(rows[i]) == Team(rows[0]);
        if (same) {
            ((void(*)())kClickSound)();
            g_log("teams: Start refused: every player is on team %d", Team(rows[0]));
            if (((short(__thiscall*)(void*))kCanShowDialog)(scene) == 1)
                ((void(__cdecl*)(int, int, int, int, int))kShowDialog)(kStrGameOptions, kStrAllAllied, kStrAllAllied,
                                                                       0, 0x1f);
            return 1;
        }
    }
    return ((int(__thiscall*)(void*, void*))kCommand)(scene, button);
}

// --- start positions ----------------------------------------------------------

bool StartPoint(int slot, int& x, int& y) {
    char* reg = *(char**)kRegistry;
    char* p = reg ? *(char**)(reg + 4 + 4 * slot) : nullptr;
    if (!p) return false;
    x = *(short*)(p + kStartX);
    y = *(short*)(p + kStartY);
    return true;
}

double Dist2(int ax, int ay, int bx, int by) { return double(ax - bx) * (ax - bx) + double(ay - by) * (ay - by); }

// After the random shuffle: the visible rows keep the start points the
// shuffle gave them as a set, but each team's rows take neighbouring ones.
// A new team starts on the free point farthest from everyone placed so far;
// its next members take the free point nearest the team's centre.
void GroupTeams(void* scene, const int rowTeam[8]) {
    int n = 0, max = VisibleRows(scene);
    while (n < max && n < 8 && Used(scene, Perm(scene, n))) ++n;
    int starts[8], sx[8], sy[8];
    bool any = false;
    for (int r = 0; r < n; ++r) {
        starts[r] = Perm(scene, r);
        if (!StartPoint(starts[r], sx[r], sy[r])) return;
        any |= rowTeam[r] != 0;
    }
    if (!any || n < 2) return;
    int assigned[8];
    bool taken[8] = {}, placed[8] = {};
    auto place = [&](int r, int k) {
        assigned[r] = k;
        taken[k] = placed[r] = true;
    };
    for (int r = 0; r < n; ++r) {
        if (placed[r] || rowTeam[r] == 0) continue;
        int best = -1;
        double bestD = -1;
        for (int k = 0; k < n; ++k) {  // seed: farthest from every placed row
            if (taken[k]) continue;
            double d = 1e18;
            for (int q = 0; q < n; ++q)
                if (placed[q]) d = std::min(d, Dist2(sx[k], sy[k], sx[assigned[q]], sy[assigned[q]]));
            if (best < 0 || d > bestD) best = k, bestD = d;
        }
        place(r, best);
        for (int m = r + 1; m < n; ++m) {
            if (rowTeam[m] != rowTeam[r]) continue;
            double cx = 0, cy = 0;
            int c = 0;
            for (int q = 0; q < n; ++q)
                if (placed[q] && rowTeam[q] == rowTeam[r]) cx += sx[assigned[q]], cy += sy[assigned[q]], ++c;
            cx /= c, cy /= c;
            int nearest = -1;
            double nearD = 0;
            for (int k = 0; k < n; ++k) {
                if (taken[k]) continue;
                double d = (sx[k] - cx) * (sx[k] - cx) + (sy[k] - cy) * (sy[k] - cy);
                if (nearest < 0 || d < nearD) nearest = k, nearD = d;
            }
            place(m, nearest);
        }
    }
    for (int r = 0; r < n; ++r)
        if (!placed[r])
            for (int k = 0; k < n; ++k)
                if (!taken[k]) {
                    place(r, k);
                    break;
                }
    for (int r = 0; r < n; ++r) *(short*)((char*)scene + kPerm + r * 2) = (short)starts[assigned[r]];
    ((void(__fastcall*)(void*))kClansByRow)(scene);
    ((void(__fastcall*)(void*))kOrdinals)(scene);
    ((void(__fastcall*)(void*))kLayout)(scene);
    for (int r = 0; r < n; ++r)
        g_log("teams: row %d team %d starts at map start %d (%d,%d)", r, rowTeam[r], starts[assigned[r]],
              sx[assigned[r]], sy[assigned[r]]);
}

// A start shuffle moves each row to another slot: its team and colour pick
// move with it.
void Shuffle(void* scene, uintptr_t fn, bool random) {
    int rowTeam[8], rowPick[8];
    for (int r = 0; r < 8; ++r) {
        rowTeam[r] = Team(Perm(scene, r));
        rowPick[r] = g_pick[Perm(scene, r)];
    }
    ((void(__fastcall*)(void*))fn)(scene);
    if (random) GroupTeams(scene, rowTeam);
    int pick[8];
    for (int r = 0; r < 8; ++r) {
        int w = Perm(scene, r);
        Slot(w)[kSlotTeam] = (char)rowTeam[r];
        pick[w] = rowPick[r];
    }
    memcpy(g_pick, pick, sizeof pick);
}

void __fastcall HookRandomStarts(void* scene) { Shuffle(scene, kRandomStarts, true); }
void __fastcall HookDefaultStarts(void* scene) { Shuffle(scene, kDefaultStarts, false); }

// --- in the game --------------------------------------------------------------

char* Player(int i) {
    char* reg = *(char**)kRegistry;
    return reg ? *(char**)(reg + 4 + 4 * i) : nullptr;
}
bool IsHuman(char* p) { return ((char(__thiscall*)(void*))(*(void***)p)[0x7c / 4])(p) == 1; }
bool Allied(char* a, char* b, int ia, int ib) {
    return (*(uint8_t*)(a + kAllyMask) & (1 << ib)) && (*(uint8_t*)(b + kAllyMask) & (1 << ia));
}
// A living player allied (both ways) with player `id`.
bool LivingAlly(int id) {
    char* me = Player(id);
    if (!me) return false;
    for (int j = 0; j < 8; ++j) {
        char* p = Player(j);
        if (j != id && p && *(short*)(p + kAlive) == 1 && Allied(me, p, id, j)) return true;
    }
    return false;
}

// Replaces "g_Cmd->GetMode()" in PlayerDefeated's local-player branch: not
// single player (0x32) means "no defeat screen; watch".
int __cdecl DefeatMode() {
    void* cmd = *(void**)kCommandModel;
    int mode = ((int(__thiscall*)(void*))(*(uintptr_t**)cmd)[0x18 / 4])(cmd);
    char* me = *(char**)kLocalPlayer;
    if (mode != 0x32 || !me || *(short*)kIsSkirmish == 0) return mode;
    int id = *(short*)(me + 4);
    if (!LivingAlly(id)) {
        g_log("teams: player %d defeated with no ally left: defeat", id);
        return mode;
    }
    g_spectateId = id;
    g_log("teams: player %d defeated; an ally still fights: watching", id);
    return 0;
}

void __cdecl HookEndMission(int result) {
    if (g_spectateId >= 0 && result == 0 && !*(char**)kLocalPlayer && LivingAlly(g_spectateId)) {
        g_log("teams: player %d's team won: victory", g_spectateId);
        result = 1;
    }
    ((void(__cdecl*)(int))kEndMission)(result);
}

bool PatchDefeat() {
    const uint8_t expect[14] = {0x8b, 0x0d, 0x60, 0x92, 0x57, 0x00, 0x8b, 0x11, 0xff, 0x52, 0x18, 0x83, 0xf8, 0x32};
    uint8_t* p = (uint8_t*)kDefeatSite;
    if (memcmp(p, expect, sizeof expect) != 0) return false;
    DWORD old;
    if (!VirtualProtect(p, sizeof expect, PAGE_EXECUTE_READWRITE, &old)) return false;
    p[0] = 0xe8;  // call DefeatMode
    *(int32_t*)(p + 1) = (int32_t)((uintptr_t)&DefeatMode - (kDefeatSite + 5));
    p[5] = 0x83, p[6] = 0xf8, p[7] = 0x32;  // cmp eax, 0x32
    memset(p + 8, 0x90, 6);
    VirtualProtect(p, sizeof expect, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, sizeof expect);
    return true;
}

}  // namespace

int Watching() { return g_spectateId; }
void SetWatching(int id) { g_spectateId = id; }

void Apply(void* registry) {
    g_spectateId = -1;
    bool any = false;
    for (int i = 0; i < 8; ++i) {
        char* p = *(char**)((char*)registry + 4 + 4 * i);
        if (p && Team(i)) any = true;
    }
    if (any) {
        for (int i = 0; i < 8; ++i) {
            char* p = *(char**)((char*)registry + 4 + 4 * i);
            if (!p) continue;
            *(uint8_t*)(p + kTeam) = (uint8_t)Team(i);
            *(uint8_t*)(p + kAllyMask) = *(uint8_t*)(kPlayerBits + i);
        }
        ((void(__fastcall*)(void*))kApplyTeams)(registry);
    }
    for (int i = 0; i < 8; ++i) {
        char* p = *(char**)((char*)registry + 4 + 4 * i);
        if (p)
            g_log("teams: player %d (%s, clan %d) team %d allies %#x colour %#x start %d,%d", i, IsHuman(p) ? "human" : "AI", *(int*)(p + 0x4878),
                  any ? Team(i) : 0, *(uint8_t*)(p + kAllyMask), *(uint8_t*)(p + 0x484e), *(short*)(p + kStartX),
                  *(short*)(p + kStartY));
    }
    if (!any) g_log("teams: no teams set: the stock alliances");
}

bool HitPoint(void* scene, int row, int kind, long& x, long& y) {
    int rows[8], n = VisibleSlots(scene, rows);
    if (row >= n) return false;
    void* w = Widget(scene, kind == 0 ? kTeamWidgets : kColourWidgets, rows[row]);
    int x0 = *(short*)((char*)w + 8), y0 = *(short*)((char*)w + 10);
    for (int dy = 0; dy < 40; ++dy)
        for (int dx = 0; dx < 40; ++dx)
            if (Hit(w, x0 + dx, y0 + dy)) {
                bool inner = Hit(w, x0 + dx + 3, y0 + dy + 3);
                x = x0 + dx + (inner ? 3 : 0);
                y = y0 + dy + (inner ? 3 : 0);
                return true;
            }
    return false;
}

// The result screen (SReward::Draw 0x493180) draws each player's clan icon on a
// disc of the player's colour (+0x484e), except in a skirmish (model 0x32), where
// it took each clan's stock colour (Tanu 0xe7, Sen-Soth 0xef, Kelika 0xd7, Magha
// 0xbf): right before colours could be picked, wrong after. The skirmish test's
// jne becomes a jmp, so every mode uses the player's own colour.
constexpr uintptr_t kRewardColourBranch = 0x4931df;

bool PatchRewardColours() {
    uint8_t* p = (uint8_t*)kRewardColourBranch;
    if (p[-3] != 0x83 || p[-2] != 0xf9 || p[-1] != 0x32 || (p[0] != 0x75 && p[0] != 0xeb) || p[1] != 0x2e) return false;
    DWORD old;
    if (!VirtualProtect(p, 1, PAGE_EXECUTE_READWRITE, &old)) return false;
    p[0] = 0xeb;
    VirtualProtect(p, 1, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 1);
    return true;
}

void Install(void (*log)(const char*, ...)) {
    g_log = log;
    bool ok = PatchSlot(kSlotInit, kInit, (void*)&HookInit) && PatchSlot(kSlotLDown, kLDown, (void*)&HookLDown) &&
              PatchSlot(kSlotRDown, kRDownStock, (void*)&HookRDown) &&
              PatchSlot(kSlotCommand, kCommand, (void*)&HookCommand) &&
              PatchCall(kRowDrawCall, kRowDraw, (void*)&HookRowDraw) &&
              PatchCall(kRandomStartsCall, kRandomStarts, (void*)&HookRandomStarts) &&
              PatchCall(kDefaultStartsCall, kDefaultStarts, (void*)&HookDefaultStarts) &&
              PatchCall(kColoursCall, kColours, (void*)&HookColours) && PatchRewardColours();
    bool defeat = PatchDefeat() && PatchCall(kEndMissionCall, kEndMission, (void*)&HookEndMission);
    log("teams: skirmish teams and colours %s; watching after defeat %s", ok ? "installed" : "FAILED (unexpected code)",
        defeat ? "installed" : "FAILED (unexpected code)");
}

}  // namespace teams
