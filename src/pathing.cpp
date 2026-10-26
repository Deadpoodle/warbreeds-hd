#include "pathing.h"

#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <vector>

#include "pathing_nav.h"

namespace pathing {
namespace {

void (*g_log)(const char*, ...);
Debug g_dbg;
bool g_pref = true;      // the player's choice
bool g_improved = false;  // latched for the current mission
bool g_testDone = false;

// --- engine addresses (re/pathfinding.md) -----------------------------------

constexpr uintptr_t kCommands = 0x579260;      // CommandModel*
constexpr uintptr_t kMultiPlayerCmd = 0x578d40;  // g_MultiPlayerCmd: [kCommands] in multiplayer
constexpr uintptr_t kTurn = 0x238;             // CommandModel: game turn counter (int)
constexpr uintptr_t kWorld = 0x579278;         // World*: things data +0x10, count +0x14

// CommandModel::GameTurn 0x463080: inc turn; call ProcessDeferredMoves; players,
// effects, things, terrain. The call is where every simulation turn starts.
constexpr uintptr_t kDeferredCall = 0x463086, kProcessDeferred = 0x462550;
// The game clock is reset (map load, players chunk) or set (save load) here.
constexpr uintptr_t kClockResetCalls[] = {0x48066e, 0x481c6f};
constexpr uintptr_t kClockReset = 0x4630e0, kClockSetCall = 0x481d4d, kClockSet = 0x4630f0;
// ACreature::Think 0x49bd50 -> PlanNextLeg 0x49d130 (thiscall, no args): next waypoint -> steps.
constexpr uintptr_t kThinkPlanCall = 0x49c00e, kPlanNextLeg = 0x49d130;

// Per-type tables, filled at run time.
constexpr uintptr_t kRadius = 0x54cf08;     // short[74]
constexpr uintptr_t kFootprint = 0x54e370;  // short[74]
constexpr uintptr_t kStepSize = 0x54d140;   // uint8_t[44]
constexpr uintptr_t kDiagStep = 0x54df10;   // uint8_t[44]
constexpr uintptr_t kTurnFlag = 0x54caf0;   // short[44]

// The synced random generator 0x471320: index byte and counter word. NewRandomSeed
// (Cmd+0xbc, 0x461430) seeds the C runtime's rand (srand 0x4eccb0) from the time.
constexpr uintptr_t kRandIndex = 0x5792b4, kRandCount = 0x5792ac, kSrand = 0x4eccb0;

// AThing / ACreature fields.
constexpr uintptr_t kOwner = 0x20, kType = 0x18, kId = 0x1c, kHp = 0x24, kX = 0x14, kY = 0x16;
constexpr uintptr_t kActivity = 0xc0, kStepCount = 0x98, kWaypoints = 0xb0, kWaypointCount = 0xb4;
constexpr uintptr_t kTopLeftX = 0xa, kTopLeftY = 0xc;  // short: top-left (the occupancy grid's tile)
constexpr uintptr_t kEnterBuilding = 0x148;  // short: building being entered (EnterBuilding 0x49f320), 0xc01 none
constexpr uintptr_t kPlayerIndex = 4;  // APlayer: short
// APlayer fog (64 px cells, 96 per row): +0x2426 byte 1 = never seen, +0x26 byte
// count of own units seeing the cell, +0x484c short fog on (0x4a35e0 / 0x4a3620).
constexpr uintptr_t kFogExplored = 0x2426, kFogVisible = 0x26, kFogOn = 0x484c;

// Terrain (AGrid 0x55dd60): u16 tile id per 64 px cell [96][96]; the terrain-type
// table (TERRAIN.HOO) packs a byte per 32 px quadrant (tl, tr, bl, br), bit 0 =
// blocked. Animated water frames (ids past the table) are water: blocked.
constexpr uintptr_t kGrid = 0x55dd60, kGridCells = 0x55dd68;
constexpr uintptr_t kTerrainMasks = 0x567104, kTerrainMaskCount = 0x567108;
constexpr uintptr_t kMapTilesW = 0x566db4, kMapTilesH = 0x566db5;  // bytes
// Yedda Ruins (type 0x43, AObstacle hit test 0x434df0): circles per variant (+0x28
// byte): [0x53df9c + v*16] -> 12-byte records {dx +4, dy +6 short, r +8 byte}, count at +4.
constexpr uintptr_t kRuinShapes = 0x53df9c, kObstacleVariant = 0x28;
// Thing hit tests (vtable +0x2c, ICircle* probe): one circle of the type's radius
// (0x445ac0); devices with shapes (0x430c80, e.g. the Summoner): circles per type
// at [0x528264 + (type-0x2c)*16], count at +4, records as for the ruins.
constexpr uintptr_t kHitCircle = 0x445ac0, kHitDeviceShape = 0x430c80, kDeviceShapes = 0x528264;
// The Warp (0x457dd0): its own circle, then extra circles: count [0x54c3d0], offsets
// [0x54c3cc] + i*8 (dx +4, dy +6 short), radii [0x54c3dc] + i*2 (short, low byte).
constexpr uintptr_t kHitWarp = 0x457dd0, kWarpCount = 0x54c3d0, kWarpOffsets = 0x54c3cc, kWarpRadii = 0x54c3dc;
// The Gene Lab and Gene Lab+ (AGeneLab, 0x429ac0): their own circle, and while
// +0x1fc is 0 the second circle of their shape (0x528264 record 1). The Collector
// (0x40ba30) calls the plain circle test.
constexpr uintptr_t kHitGeneLab = 0x429ac0, kGeneLabShapeOff = 0x1fc, kHitCollector = 0x40ba30;

// Stock collision, for the oracle test.
constexpr uintptr_t kIsRectBlocked = 0x42eb50;  // AGrid short(ILogicalC* tl, ILogicalC* br, int flyer), ret 0xc
constexpr uintptr_t kFindThingAt = 0x44db90;    // World AThing*(self, x, y, r, halfFoot, layer), ret 0x18
constexpr uintptr_t kCreateAt = 0x446db0;       // World short(owner, type, x, y, skipChecks, activate, arg)

template <typename T>
T& F(void* p, uintptr_t off) {
    return *(T*)((char*)p + off);
}
void* Cmd() { return *(void**)kCommands; }
bool Multiplayer() { return Cmd() == (void*)kMultiPlayerCmd; }

// --- mission state ----------------------------------------------------------

bool g_missionStart = true;  // reset on a clock reset or set; acted on at the next turn
int g_lastTurn = -1;
int g_missionTurn0;
int g_turn;  // turns since the mission start
bool g_tablesLogged;

void LogTables() {
    char line[600];
    int n = snprintf(line, sizeof line, "pathing: radius/footprint");
    for (int t = 0; t < 74; ++t)
        n += snprintf(line + n, sizeof line - n, " %d/%d", ((short*)kRadius)[t], ((short*)kFootprint)[t]);
    g_log("%s", line);
    n = snprintf(line, sizeof line, "pathing: step/diag/turn");
    for (int t = 0; t < 44; ++t)
        n += snprintf(line + n, sizeof line - n, " %d/%d/%d", ((uint8_t*)kStepSize)[t], ((uint8_t*)kDiagStep)[t],
                      ((short*)kTurnFlag)[t]);
    g_log("%s", line);
}

// FNV-1a over every thing in table order (ids, types, owners by player index,
// positions, hp, activity, steps, waypoints), the turn and the random state.
// Compared across runs (single player) and machines (multiplayer).
uint32_t StateHash(int turn) {
    uint32_t h = 2166136261u;
    auto mix = [&h](uint32_t v) {
        for (int i = 0; i < 4; ++i) h = (h ^ ((v >> (i * 8)) & 0xff)) * 16777619u;
    };
    mix(turn);
    mix(*(uint8_t*)kRandIndex);
    mix(*(uint16_t*)kRandCount);
    char* w = *(char**)kWorld;
    if (!w) return h;
    char** things = *(char***)(w + 0x10);
    int count = *(int*)(w + 0x14);
    for (int i = 0; i < count; ++i) {
        char* t = things[i];
        if (!t) continue;
        int type = F<int>(t, kType);
        void* owner = F<void*>(t, kOwner);
        mix(F<unsigned short>(t, kId));
        mix(type);
        mix(owner ? F<short>(owner, kPlayerIndex) : -1);
        mix(F<unsigned short>(t, kX) | F<unsigned short>(t, kY) << 16);
        mix(F<short>(t, kHp));
        if (type >= 0 && type <= 0x2b) {
            mix(F<int>(t, kActivity));
            mix(F<int>(t, kStepCount));
            int wp = F<int>(t, kWaypointCount);
            mix(wp);
            if (wp > 0) mix(F<uint32_t>(F<char*>(t, kWaypoints), (wp - 1) * 8 + 4));
        }
    }
    return h;
}

// --- the world, as the navigation grid sees it --------------------------------

char** Things(int& count) {
    char* w = *(char**)kWorld;
    count = w ? *(int*)(w + 0x14) : 0;
    return w ? *(char***)(w + 0x10) : nullptr;
}
char* ThingById(int id) {
    int count;
    char** things = Things(count);
    for (int i = 0; i < count; ++i)
        if (things[i] && F<unsigned short>(things[i], kId) == id) return things[i];
    return nullptr;
}

bool IsCreature(int type) { return type >= 0 && type <= 0x2b; }
bool IsObstacle(int type) { return type >= 0x43 && type <= 0x49; }
bool IsDynamic(int type) { return type >= 0x2c && type <= 0x42; }  // buildings, pods, corpses
int Radius(int type) { return ((short*)kRadius)[type] & 0xff; }
int PlayerOf(void* thing) {
    void* owner = F<void*>(thing, kOwner);
    return owner ? F<short>(owner, kPlayerIndex) : -1;
}

uint32_t TileMask(int cx, int cy, bool stock) {
    unsigned id = ((uint16_t*)kGridCells)[cy * 96 + cx];
    if (id < *(uint32_t*)kTerrainMaskCount) return (*(uint32_t**)kTerrainMasks)[id];
    return stock ? 0 : 0x01010101;
}

bool TileBlocked(int tx, int ty) {
    return TileMask(tx >> 1, ty >> 1, false) >> (8 * ((tx & 1) + 2 * (ty & 1))) & 1;
}

// The stock IsRectBlocked 0x42eb50, stale mask and all (re/pathfinding.md §6).
bool StockRectBlocked(int x0, int y0, int x1, int y1) {
    int w = *(uint8_t*)kMapTilesW, h = *(uint8_t*)kMapTilesH;
    if (x0 < 0 || x1 >= w || y0 < 0 || y1 >= h) return true;
    uint32_t mask = TileMask(x0 >> 1, y0 >> 1, true);
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            uint32_t bit;
            if (!(y & 1)) {
                if (!(x & 1))
                    mask = TileMask(x >> 1, y >> 1, true), bit = 1;
                else
                    bit = 0x100;
            } else {
                bit = x & 1 ? 0x1000000 : 0x10000;
            }
            if (mask & bit) return true;
        }
    return false;
}

// --- the IsRectBlocked fix (Improved only) -----------------------------------
// The stock function tests a stale metatile mask on odd tile rows and reads
// animated water as open ground. Improved replaces it with the grid's own rule
// (a JMP over its entry, written at each mission start; Original restores the
// stock bytes), so collision, placement and routes agree.

struct Box {
    uint32_t vtbl;
    short x, y;
};

short __fastcall FixedRectBlocked(void*, void*, Box* tl, Box* br, int flyer) {
    int w = *(uint8_t*)kMapTilesW, h = *(uint8_t*)kMapTilesH;
    if (tl->x < 0 || br->x >= w || tl->y < 0 || br->y >= h) return 1;
    if (flyer == 1) return 0;
    for (int y = tl->y; y <= br->y; ++y)
        for (int x = tl->x; x <= br->x; ++x)
            if (TileBlocked(x, y)) return 1;
    return 0;
}

constexpr uint8_t kRectPrologue[] = {0x83, 0xec, 0x0c, 0x8b, 0x44, 0x24, 0x10};  // sub esp, 0xc; mov eax, [esp+0x10]
bool g_rectPatched;

void SetRectFix(bool on) {
    uint8_t* p = (uint8_t*)kIsRectBlocked;
    if (on == g_rectPatched) return;
    if (on && memcmp(p, kRectPrologue, sizeof kRectPrologue) != 0) {
        g_log("pathing: IsRectBlocked fix FAILED (unexpected code)");
        return;
    }
    DWORD old;
    if (!VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old)) return;
    if (on) {
        p[0] = 0xe9;
        *(int32_t*)(p + 1) = (int32_t)((uintptr_t)&FixedRectBlocked - (kIsRectBlocked + 5));
    } else {
        memcpy(p, kRectPrologue, 5);
    }
    VirtualProtect(p, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 5);
    g_rectPatched = on;
    g_log("pathing: IsRectBlocked %s", on ? "fixed" : "stock bytes");
}

uint32_t g_staticHash;  // of the grid as built

// Terrain bits and every rock, tree and ruin: when it changes, the grid is rebuilt.
uint32_t StaticHash() {
    uint32_t h = 2166136261u;
    auto mix = [&h](uint32_t v) { h = (h ^ v) * 16777619u; };
    int w = *(uint8_t*)kMapTilesW, hh = *(uint8_t*)kMapTilesH;
    mix(w), mix(hh);
    for (int cy = 0; cy < (hh + 1) / 2; ++cy)
        for (int cx = 0; cx < (w + 1) / 2; ++cx) mix(TileMask(cx, cy, false) & 0x01010101);
    int count;
    char** things = Things(count);
    for (int i = 0; i < count; ++i)
        if (things[i] && IsObstacle(F<int>(things[i], kType))) {
            mix(F<unsigned short>(things[i], kId));
            mix(F<uint32_t>(things[i], kX));
            mix(F<int>(things[i], kType) << 8 | F<uint8_t>(things[i], kObstacleVariant));
        }
    return h;
}

void BuildStatic() {
    DWORD t0 = GetTickCount();
    int w = *(uint8_t*)kMapTilesW, h = *(uint8_t*)kMapTilesH;
    std::vector<uint8_t> blocked(w * h);
    int nBlocked = 0;
    for (int ty = 0; ty < h; ++ty)
        for (int tx = 0; tx < w; ++tx) nBlocked += blocked[ty * w + tx] = TileBlocked(tx, ty);
    nav::BeginStatic(w, h, blocked.data());
    int count, circles = 0, obstacles = 0;
    char** things = Things(count);
    for (int i = 0; i < count; ++i) {
        char* t = things[i];
        if (!t || !IsObstacle(F<int>(t, kType))) continue;
        ++obstacles;
        int type = F<int>(t, kType), x = F<short>(t, kX), y = F<short>(t, kY);
        nav::AddStaticTile(F<short>(t, kTopLeftX) >> 5, F<short>(t, kTopLeftY) >> 5);
        if (type == 0x43) {
            int v = F<uint8_t>(t, kObstacleVariant);
            char* shape = *(char**)(kRuinShapes + v * 16);
            uint32_t n = *(uint32_t*)(kRuinShapes + v * 16 + 4);
            for (uint32_t k = 0; k < n; ++k, ++circles)
                nav::AddStaticCircle(x + *(short*)(shape + k * 12 + 4), y + *(short*)(shape + k * 12 + 6),
                                     *(uint8_t*)(shape + k * 12 + 8));
        } else {
            nav::AddStaticCircle(x, y, Radius(type));
            ++circles;
        }
    }
    nav::FinishStatic();
    g_staticHash = StaticHash();
    g_log("pathing: grid %dx%d tiles (%d blocked), %d obstacles (%d circles), built in %d ms", w, h, nBlocked,
          obstacles, circles, (int)(GetTickCount() - t0));
}

void* g_players[nav::kMaxPlayers];  // APlayer by index, from the things they own

bool CellVisible(void* player, int x, int y) {
    if (x < 0 || y < 0) return false;
    int idx = (y >> 6) * 96 + (x >> 6);
    if (idx >= 96 * 96) return false;
    uint8_t* pl = (uint8_t*)player;
    if (pl[kFogExplored + idx] == 1) return false;
    if (F<short>(player, kFogOn) != 1) return true;
    return pl[kFogVisible + idx] != 0;
}

// Once per turn: buildings, pods and corpses into the grid (new, moved, gone),
// and which players know them: their own, or seen.
void ScanWorld() {
    if (!nav::Ready() || StaticHash() != g_staticHash) BuildStatic();
    for (void*& p : g_players) p = nullptr;
    static std::vector<int> present, before;
    present.clear();
    int count;
    char** things = Things(count);
    for (int i = 0; i < count; ++i) {
        char* t = things[i];
        if (!t) continue;
        int type = F<int>(t, kType), p = PlayerOf(t);
        if (p >= 0 && p < nav::kMaxPlayers) g_players[p] = F<void*>(t, kOwner);
        if (!IsDynamic(type)) continue;
        int id = F<unsigned short>(t, kId), x = F<short>(t, kX), y = F<short>(t, kY);
        nav::Circle circles[16];
        int n = 0;
        uintptr_t hit = (*(uintptr_t**)t)[0x2c / 4];
        if (hit == kHitDeviceShape) {
            char* shape = *(char**)(kDeviceShapes + (type - 0x2c) * 16);
            int m = (int)*(uint32_t*)(kDeviceShapes + (type - 0x2c) * 16 + 4);
            for (; n < m && n < 16; ++n)
                circles[n] = {x + *(short*)(shape + n * 12 + 4), y + *(short*)(shape + n * 12 + 6),
                              *(uint8_t*)(shape + n * 12 + 8)};
        } else if (hit == kHitWarp) {
            circles[n++] = {x, y, Radius(type)};
            char* off = *(char**)kWarpOffsets;
            short* radii = *(short**)kWarpRadii;
            for (uint32_t k = 0; k < *(uint32_t*)kWarpCount && n < 16; ++k)
                circles[n++] = {x + *(short*)(off + k * 8 + 4), y + *(short*)(off + k * 8 + 6), radii[k] & 0xff};
        } else if (hit == kHitGeneLab) {
            circles[n++] = {x, y, Radius(type)};
            char* shape = *(char**)(kDeviceShapes + (type - 0x2c) * 16);
            if (!F<short>(t, kGeneLabShapeOff) && shape)
                circles[n++] = {x + *(short*)(shape + 12 + 4), y + *(short*)(shape + 12 + 6), *(uint8_t*)(shape + 12 + 8)};
        } else {
            static uintptr_t s_warned[8];
            if (hit != kHitCircle && hit != kHitCollector &&
                std::find(std::begin(s_warned), std::end(s_warned), hit) == std::end(s_warned)) {
                for (uintptr_t& w : s_warned)
                    if (!w) {
                        w = hit;
                        g_log("pathing: type %#x has an unknown hit test %#x (treated as a circle)", type, (unsigned)hit);
                        break;
                    }
            }
            circles[n++] = {x, y, Radius(type)};
        }
        nav::UpsertDynamic(id, x, y, F<short>(t, kTopLeftX) >> 5, F<short>(t, kTopLeftY) >> 5, circles, n);
        present.push_back(id);
    }
    std::sort(present.begin(), present.end());
    nav::DynamicIds(before);
    for (int id : before)
        if (!std::binary_search(present.begin(), present.end(), id)) nav::RemoveDynamic(id);
    for (int i = 0; i < count; ++i) {
        char* t = things[i];
        if (!t || !IsDynamic(F<int>(t, kType))) continue;
        int id = F<unsigned short>(t, kId), owner = PlayerOf(t);
        for (int p = 0; p < nav::kMaxPlayers; ++p)
            if (g_players[p] && !nav::IsKnown(id, p) &&
                (p == owner || CellVisible(g_players[p], F<short>(t, kX), F<short>(t, kY)))) {
                nav::SetKnown(id, p);
                if (g_dbg.test && p != owner)
                    g_log("pathing: turn %d player %d sees thing %d (type %#x) at %d,%d", g_turn, p, id, F<int>(t, kType),
                          F<short>(t, kX), F<short>(t, kY));
            }
    }
    nav::RefreshDynamic();
}

// --- the planner (Improved, ground units) -------------------------------------
// Think calls PlanNextLeg whenever a unit with waypoints has no steps. A route
// to the top waypoint is planned once over the lattice and kept per unit; each
// call hands the unit the steps to the farthest route node it can walk straight
// to (up to 16 steps, in the stock geometry: one step size along the major axis).

constexpr uintptr_t kSetPath = 0x404750;  // ACreatureBody (IStack<tPathStep>*), ret 4: copies x, y, dir
constexpr int kTurnBudget = 40000;        // A* expansions per turn (one search may overrun it)
constexpr int kLookahead = 24;            // route nodes considered for the next straight leg
// Steps per leg: 8 (about 100-150 px, inside every unit's sight), so a building
// coming into view is known before the unit commits to walking into it.
constexpr int kMaxSteps = 8;
constexpr int kJamTurns = 20, kGiveUpTurns = 300;  // about 1 s and 15 s (traffic, below)
constexpr uintptr_t kBuildSiteX = 0x16c, kBuildSiteY = 0x16e;  // AShaman short: its Build site (centre)

struct UnitRoute {
    void* thing = nullptr;
    int type = -1;
    nav::Pt goal{};         // the waypoint it was planned for
    int waypoints = 0;      // waypoint count then
    nav::Route route;
    size_t next = 0;        // first route node not yet reached
    size_t pending = 0;     // next + 1 once the unit stands on the node its last steps lead to
    nav::Pt pendingAt{};
    bool stock = false;     // handed to the stock planner (its OnBlocked too)
    int layerVersion = 0;   // the player's building layer when last checked
    // Traffic (OnBlocked): when the current jam began, and progress along the route.
    int blockedSince = -1;
    int retryTurn = 0;      // next turn a reroute may be tried
    int progressTurn = 0;   // last turn it got 16 px nearer its goal than ever before
    int bestDist = 1 << 30; // its nearest distance to the goal so far (px)
    size_t progressNext = 0;
    int plannedTurn = -1;
    bool valid = false;
};
std::vector<UnitRoute> g_routes(65536);
int g_budget;  // expansions left this turn
int g_plans, g_plansDeferred;

bool IsFlyer(int type) { return type == 4 || type == 0xd || type == 0x1f; }

// The stock facing of a straight line (StraightPath 0x49e4d0, table 0x503210): 0 = north, clockwise.
uint8_t Facing(int dx, int dy) {
    static const uint8_t kTable[8] = {3, 2, 1, 0, 4, 5, 6, 7};
    int a = 2 * dx < dy, c = -dx > 2 * dy;
    int d = (a ^ c) ? 2 * dx < -dy : dx > 2 * dy;
    return kTable[a * 4 + c * 2 + d];
}

struct PathStep {  // tPathStep: vtbl, IPixelC {vtbl, x, y}, dir
    uint32_t vtbl, ptVtbl;
    short x, y;
    uint8_t dir, pad[3];
};
struct StepStack {
    uint32_t vtbl;
    PathStep* data;
    uint32_t count, cap;
};

void GiveSteps(void* c, nav::Pt from, const nav::Pt* steps, int n) {
    PathStep buf[kMaxSteps];
    uint8_t dir = Facing(steps[n - 1].x - from.x, steps[n - 1].y - from.y);
    for (int i = 0; i < n; ++i)  // the stack's top (last) is the next step
        buf[n - 1 - i] = {0x5031b4, 0x5031c4, (short)steps[i].x, (short)steps[i].y, dir, {}};
    StepStack st{0, buf, (uint32_t)n, (uint32_t)n};
    ((void(__thiscall*)(void*, StepStack*))kSetPath)(c, &st);
}

constexpr uintptr_t kVStop = 0xb8;  // ACreature virtual: Stop 0x49f600 (idle)
int g_mismatches, g_giveUps, g_reroutes, g_asks;

void StopUnit(char* c) { ((void(__thiscall*)(void*))(*(void***)c)[kVStop / 4])(c); }

std::vector<int> g_deferredAt(65536, -2);
std::vector<nav::Pt> g_retarget(65536);    // last unreachable goal's replacement, per unit (tests)  // turn a unit's plan last waited for the budget

// 16 directions, cos and sin x 1000.
constexpr int kCos16[16] = {1000, 924, 707, 383, 0, -383, -707, -924, -1000, -924, -707, -383, 0, 383, 707, 924};
constexpr int kSin16[16] = {0, 383, 707, 924, 1000, 924, 707, 383, 0, -383, -707, -924, -1000, -924, -707, -383};
struct IdList {
    uint32_t vtbl;
    short* data;
    uint32_t count, cap;
};
struct PixelPt {
    uint32_t vtbl;
    short x, y;
};
struct PtList {
    uint32_t vtbl;
    PixelPt* data;
    uint32_t count, cap;
};

bool WorkSpotFor(char* c, nav::Pt& out);
int ISqrtDbg(int v);

// Plans or continues the unit's route; false hands it to the stock planner.
bool PlanImproved(char* c) {
    int type = F<int>(c, kType);
    int count = F<int>(c, kWaypointCount);
    if (count <= 0) return false;
    char* top = F<char*>(c, kWaypoints) + (count - 1) * 8;
    nav::Pt goal{*(short*)(top + 4), *(short*)(top + 6)};
    nav::Pt pos{F<short>(c, kX), F<short>(c, kY)};
    int id = F<unsigned short>(c, kId), p = PlayerOf(c);
    nav::Pt spot;
    if (WorkSpotFor(c, spot)) goal = spot;  // a builder: its own spot round the site
    int r = Radius(type), hf = ((short*)kFootprint)[type] / 2, step = ((uint8_t*)kStepSize)[type];
    if (step <= 0 || p < 0 || p >= nav::kMaxPlayers) return false;
    bool building = F<int>(c, kActivity) == 0x2e;
    nav::SetQueryUnit(id, building ? F<short>(c, kBuildSiteX) : -1, building ? F<short>(c, kBuildSiteY) : -1);
    UnitRoute& u = g_routes[id];
    // No progress on this order for 10 s: stop (only a route for the current order: a new click starts afresh).
    bool current = u.valid && u.thing == c && u.type == type && u.waypoints == count && u.goal.x == goal.x &&
                   u.goal.y == goal.y;
    if (current && g_turn - u.progressTurn > kGiveUpTurns && F<int>(c, kActivity) == 0x29) {
        ++g_giveUps;
        g_log("pathing: give up id %d at %d,%d (no progress for %d turns, planner)", id, pos.x, pos.y,
              g_turn - u.progressTurn);
        u.valid = false;
        StopUnit(c);
        return true;
    }
    for (int attempt = 0; attempt < 3; ++attempt) {
        bool same = u.valid && u.thing == c && u.type == type && u.waypoints == count && u.goal.x == goal.x &&
                    u.goal.y == goal.y;
        if (same && u.layerVersion != nav::LayerVersion(p)) {  // buildings changed: is the rest of it still open?
            u.layerVersion = nav::LayerVersion(p);
            same = nav::RouteFree(u.route, u.next, r, hf, p);
            if (!same && g_dbg.test) g_log("pathing: route id %d blocked by a building, replanning", id);
        }
        if (!same) {
            if (u.thing == c && u.plannedTurn == g_turn) return true;  // planned this turn already: wait
            // Over the turn's budget: wait one turn (once), so a crowd's searches spread out.
            if (g_budget <= 0 && g_deferredAt[id] != g_turn - 1) {
                g_deferredAt[id] = g_turn;
                ++g_plansDeferred;
                return true;
            }
            UnitRoute old = u;
            u = UnitRoute();
            u.thing = c, u.type = type, u.goal = goal, u.waypoints = count, u.plannedTurn = g_turn;
            // A replan for the same order keeps its jam and progress clocks: same unit,
            // same goal, and planned recently. Anything else is a new order (comparing
            // only the waypoint count let a new click inherit an idle unit's stale
            // clock, and the unit gave up after its first leg).
            bool sameOrder = old.thing == c && old.waypoints == count && old.goal.x == goal.x && old.goal.y == goal.y &&
                             g_turn - old.plannedTurn < kGiveUpTurns;
            u.blockedSince = sameOrder ? old.blockedSince : -1;
            u.progressTurn = sameOrder ? old.progressTurn : g_turn;
            u.bestDist = sameOrder ? old.bestDist : 1 << 30;
            u.retryTurn = sameOrder ? old.retryTurn : 0;
            if (!nav::FindRoute(pos.x, pos.y, goal.x, goal.y, r, hf, p, 1 << 30, u.route)) {
                if (g_dbg.test) g_log("pathing: no route id %d from %d,%d to %d,%d", id, pos.x, pos.y, goal.x, goal.y);
                return false;
            }
            g_budget -= u.route.expansions;
            ++g_plans;
            u.valid = true;
            u.layerVersion = nav::LayerVersion(p);
            // A move to a point it can't reach goes to the nearest point it can, and
            // stops there (the stock arrival check then sets it idle). Not a unit
            // entering a building (Healer, Warp): it walks up to it and the stock
            // planner takes the last steps.
            if (!u.route.exact && F<int>(c, kActivity) == 0x29 && F<short>(c, kEnterBuilding) == (short)0xc01) {
                *(short*)(top + 4) = (short)u.route.end.x, *(short*)(top + 6) = (short)u.route.end.y;
                u.goal = u.route.end;
                u.route.exact = true;
                g_retarget[id] = u.route.end;
                if (g_dbg.test)
                    g_log("pathing: retarget id %d %d,%d -> %d,%d", id, goal.x, goal.y, u.route.end.x, u.route.end.y);
            }
            if (g_dbg.test)
                g_log("pathing: route id %d from %d,%d to %d,%d: %d nodes to %d,%d%s, %d expansions", id, pos.x, pos.y,
                      goal.x, goal.y, (int)u.route.nodes.size(), u.route.end.x, u.route.end.y,
                      u.route.exact ? "" : " (nearest)", u.route.expansions);
        }
        const std::vector<nav::Pt>& nodes = u.route.nodes;
        if (u.pending && pos.x == u.pendingAt.x && pos.y == u.pendingAt.y) u.next = std::max(u.next, u.pending);
        u.pending = 0;
        if (u.next > u.progressNext) u.progressNext = u.next;
        {  // progress: 16 px nearer the goal than it has been (a queue creeping forward counts)
            int dx = pos.x - u.route.end.x, dy = pos.y - u.route.end.y, d = ISqrtDbg(dx * dx + dy * dy);
            if (d + 16 <= u.bestDist || u.bestDist == 1 << 30) u.bestDist = d, u.progressTurn = g_turn;
        }
        if (u.next >= nodes.size()) return u.route.exact;  // at the end (stock handles a goal it can't reach)
        // The farthest node ahead the unit can walk straight to.
        size_t last = std::min(nodes.size(), u.next + kLookahead);
        int best = -1;
        for (size_t k = last; k-- > u.next;)
            if (nav::LineClear(pos, nodes[k], step, r, hf, p)) {
                best = (int)k;
                break;
            }
        if (best < 0) {  // off the route (pushed, or something new in the way): plan again
            u.valid = false;
            continue;
        }
        nav::Pt steps[kMaxSteps];
        bool reached;
        int n = nav::Steps(pos, nodes[best], step, r, hf, p, steps, kMaxSteps, &reached);
        if (reached) {
            if (n == 0) u.next = best + 1;  // already there
            else u.pending = best + 1, u.pendingAt = nodes[best];
        }
        if (n > 0) {
            GiveSteps(c, pos, steps, n);
            if (u.blockedSince >= 0 && g_turn - u.blockedSince > kJamTurns * 2) u.blockedSince = -1;  // moving again
            return true;
        }
    }
    return true;
}

// --- traffic: OnBlocked (ACreature vtable +0x90) -------------------------------
// ACreatureBody::Step calls it when the next step collides (+0x2c = 1, blocker
// id +0x2e, 0xc01 = terrain). Stock lowers the unit's patience and grows its
// arrival tolerance, which is how its orders "finish" when stuck. For a unit on
// an Improved route:
//   - a building, rock, pod or corpse: now known to its player; replan round it;
//   - terrain: the grid and the game disagree (MISMATCH; never expected);
//   - an own idle unit: ask it to step aside (its OnBumped), wait a moment;
//   - any other unit: wait up to about a second, then reroute round it if that
//     is not much longer, otherwise keep waiting; two units blocking each other
//     head-on: the lower id goes round;
//   - no progress along the route for 10 s: stop (idle).

constexpr uintptr_t kSlotOnBlocked[] = {0x5093a4, 0x504abc};  // ACreature, AShaman vtables
constexpr uintptr_t kOnBlocked = 0x49bab0, kCheckWaypoint = 0x49c490, kClearSteps = 0x4041e0;
constexpr uintptr_t kBlocked = 0x2c, kBlocker = 0x2e;  // short
constexpr uintptr_t kWaitTicks = 0x140;               // short, counted down by Think
constexpr uintptr_t kStance = 0x44;                   // int: 4 = immobile


std::vector<int> g_askedAt(65536, -100);  // turn a unit last asked another to step aside
std::vector<int> g_blockedBy(65536, -1);   // per unit: the creature that last blocked it
std::vector<int> g_askedOf(65536, -1), g_asks2(65536, 0);  // the idle friend it last asked, and how often
std::vector<int> g_blockedAt(65536, -100); // and when

bool Idle(char* t) {
    return F<int>(t, kActivity) == 0x28 && F<int>(t, kWaypointCount) == 0 && F<int>(t, kStepCount) == 0;
}

// Reroute round the creature in the way: taken if not much longer than the rest of the current route.
// `any`: take the detour however long (a head-on block, or a long wait: waiting would never end).
bool Reroute(char* c, UnitRoute& u, char* blocker, bool any) {
    int type = F<int>(c, kType), p = PlayerOf(c);
    int r = Radius(type), hf = ((short*)kFootprint)[type] / 2;
    nav::Pt pos{F<short>(c, kX), F<short>(c, kY)};
    nav::Circle around{F<short>(blocker, kX), F<short>(blocker, kY), Radius(F<int>(blocker, kType))};
    nav::SetTemporary(&around, 1);
    nav::SetQueryUnit(F<unsigned short>(c, kId));
    nav::Route alt;
    bool ok = nav::FindRoute(pos.x, pos.y, u.goal.x, u.goal.y, r, hf, p, 1 << 30, alt);
    nav::SetTemporary(nullptr, 0);
    if (!ok) return false;
    g_budget -= alt.expansions;
    int now = nav::RouteLength(u.route, u.next, pos), detour = nav::RouteLength(alt, 0, pos);
    if ((!alt.exact && u.route.exact) || (!any && detour > now * 3 / 2 + 96) || (any && detour > now * 2 + 400)) {
        if (g_dbg.test)
            g_log("pathing: id %d keeps waiting (detour %d px against %d)", F<unsigned short>(c, kId), detour, now);
        return false;
    }
    u.route = alt;
    u.next = 0;
    u.layerVersion = nav::LayerVersion(p);
    ++g_reroutes;
    if (g_dbg.test)
        g_log("pathing: id %d reroutes round %d (%d px against %d)", F<unsigned short>(c, kId),
              F<unsigned short>(blocker, kId), detour, now);
    return true;
}

void MoveUnit(char* c, int x, int y);
int ISqrtDbg(int v);

// The mover's path ahead: its position and the next route nodes.
std::vector<nav::Pt> PathAhead(char* c, const UnitRoute& u) {
    std::vector<nav::Pt> path{{F<short>(c, kX), F<short>(c, kY)}};
    for (size_t k = u.next; k < u.route.nodes.size() && path.size() < 14; ++k) path.push_back(u.route.nodes[k]);
    if (path.size() == 1) path.push_back(u.route.end);
    return path;
}

int64_t SegDist2(nav::Pt p, nav::Pt a, nav::Pt b) {
    int64_t vx = b.x - a.x, vy = b.y - a.y, wx = p.x - a.x, wy = p.y - a.y, len = vx * vx + vy * vy;
    int64_t t = len ? std::clamp<int64_t>((wx * vx + wy * vy) * 1024 / len, 0, 1024) : 0;
    int64_t dx = wx - vx * t / 1024, dy = wy - vy * t / 1024;
    return dx * dx + dy * dy;
}

// The nearest spot to unit t (in its region, passable for it) at least `clear`
// px from every segment of `path`: where it can stand out of the way. Rings of
// 16 directions, 24 px apart, out to 384 px.
bool OutOfTheWay(char* t, const std::vector<nav::Pt>& path, int clear, nav::Pt& out) {
    int type = F<int>(t, kType), r = Radius(type), hf = ((short*)kFootprint)[type] / 2, p = PlayerOf(t);
    int x = F<short>(t, kX), y = F<short>(t, kY), region = nav::Region(x, y, r);
    nav::SetQueryUnit(F<unsigned short>(t, kId));
    for (int d = 24; d <= 384; d += 24)
        for (int k = 0; k < 16; ++k) {
            nav::Pt q{x + kCos16[k] * d / 1000, y + kSin16[k] * d / 1000};
            bool off = true;
            for (size_t i = 0; i + 1 < path.size() && off; ++i) off = SegDist2(q, path[i], path[i + 1]) >= (int64_t)clear * clear;
            // (Region 0: it stands where the lattice is blocked, beside a building, say; any spot then.)
            if (off && nav::Passable(q.x, q.y, r, hf, p) && (region == 0 || nav::Region(q.x, q.y, r) == region)) {
                out = q;
                return true;
            }
        }
    return false;
}

// An own idle unit in the way gets a real move (the stock OnBumped make-way
// leaves it idle with steps it never walks): to the nearest spot off the mover's
// path (out of a corridor, if that is the only way).
void StepAside(char* b, char* c, const UnitRoute& u) {
    nav::Pt to;
    int clear = Radius(F<int>(b, kType)) + Radius(F<int>(c, kType)) + 8;
    if (OutOfTheWay(b, PathAhead(c, u), clear, to)) MoveUnit(b, to.x, to.y);
    nav::SetQueryUnit(F<unsigned short>(c, kId));
}

// Two units head-on with no way round: the one giving way backs off to a spot off
// the other's path, as a waypoint pushed on top of its order (it carries on after).
bool BackOff(char* c, char* other) {
    const UnitRoute& ou = g_routes[F<unsigned short>(other, kId)];
    if (!ou.valid || ou.thing != other) return false;
    nav::Pt to;
    int clear = Radius(F<int>(c, kType)) + Radius(F<int>(other, kType)) + 8;
    bool found = OutOfTheWay(c, PathAhead(other, ou), clear, to);
    nav::SetQueryUnit(F<unsigned short>(c, kId));
    if (!found) return false;
    PixelPt pt{0x5031c4, (short)to.x, (short)to.y};
    char* stack = c + 0xac;  // IStack<IPixelC> waypoints: vtable slot 0 pushes on top
    ((void(__thiscall*)(void*, void*))(*(void***)stack)[0])(stack, &pt);
    if (g_dbg.test) g_log("pathing: id %d backs off to %d,%d for %d", F<unsigned short>(c, kId), to.x, to.y,
                          F<unsigned short>(other, kId));
    return true;
}

void __fastcall HookOnBlocked(void* creature, void*) {
    char* c = (char*)creature;
    int id = F<unsigned short>(c, kId);
    UnitRoute& u = g_routes[id];
    if (!g_improved || !nav::Ready() || IsFlyer(F<int>(c, kType)) || (u.thing == c && u.stock)) {
        ((void(__thiscall*)(void*))kOnBlocked)(c);
        return;
    }
    if (!u.valid || u.thing != c) {  // between routes: replan, without the stock patience and tolerance changes
        ((void(__thiscall*)(void*))kCheckWaypoint)(c);
        ((void(__thiscall*)(void*))kClearSteps)(c);
        F<short>(c, kWaitTicks) = 4;
        return;
    }
    ((void(__thiscall*)(void*))kCheckWaypoint)(c);  // as stock: pop waypoints already reached
    int blockerId = F<unsigned short>(c, kBlocker), p = PlayerOf(c);
    nav::Pt stepAt{-1, -1};
    if (F<int>(c, kStepCount) > 0) {
        char* top = F<char*>(c, 0x94) + (F<int>(c, kStepCount) - 1) * 16;
        stepAt = {*(short*)(top + 8), *(short*)(top + 0xa)};
    }
    ((void(__thiscall*)(void*))kClearSteps)(c);
    if (F<int>(c, kWaypointCount) == 0) return;
    if (g_turn - u.progressTurn > kGiveUpTurns && F<int>(c, kActivity) == 0x29) {
        ++g_giveUps;
        g_log("pathing: give up id %d at %d,%d (no progress for %d turns)", id, F<short>(c, kX), F<short>(c, kY),
              g_turn - u.progressTurn);
        u.valid = false;
        StopUnit(c);
        return;
    }
    if (blockerId == 0xc01 && stepAt.x < 0) {  // not a step (the follow branch): wait
        F<short>(c, kWaitTicks) = 4;
        return;
    }
    if (blockerId == 0xc01) {
        if (g_mismatches++ < 20) {
            int r = Radius(F<int>(c, kType));
            g_log("pathing: MISMATCH terrain blocks id %d at %d,%d stepping to %d,%d r %d (the grid said %s)", id,
                  F<short>(c, kX), F<short>(c, kY), stepAt.x, stepAt.y, r,
                  nav::TerrainBoxBlocked(stepAt.x - r, stepAt.y - r, stepAt.x + r, stepAt.y + r) ? "blocked" : "free");
            for (int cy = (stepAt.y - r) >> 6; cy <= (stepAt.y + r) >> 6 && stepAt.x >= 0; ++cy)
                for (int cx = (stepAt.x - r) >> 6; cx <= (stepAt.x + r) >> 6; ++cx)
                    g_log("pathing:   cell %d,%d id %#x mask %08x (table %u)", cx, cy, ((uint16_t*)kGridCells)[cy * 96 + cx],
                          TileMask(cx, cy, false), *(uint32_t*)kTerrainMaskCount);
        }
        u.valid = false;
        return;
    }
    char* b = ThingById(blockerId);
    if (!b) return;
    if (!IsCreature(F<int>(b, kType))) {  // a building or rock: now it is known
        nav::SetKnown(blockerId, p);
        nav::RefreshDynamic();
        return;  // the route re-check replans round it
    }
    g_blockedBy[id] = blockerId, g_blockedAt[id] = g_turn;
    // Its last stop is taken by a friend standing there: stop beside it.
    if (PlayerOf(b) == p && F<int>(c, kWaypointCount) == 1 && F<int>(c, kActivity) == 0x29) {
        char* last = F<char*>(c, kWaypoints);
        int gx = *(short*)(last + 4) - F<short>(c, kX), gy = *(short*)(last + 6) - F<short>(c, kY);
        int besideR = 2 * (Radius(F<int>(c, kType)) + Radius(F<int>(b, kType))) + 16;
        if (gx * gx + gy * gy <= besideR * besideR && (Idle(b) || g_turn - u.progressTurn > kJamTurns)) {
            if (g_dbg.test) g_log("pathing: id %d stops beside %d, its spot taken", id, blockerId);
            u.valid = false;
            StopUnit(c);
            return;
        }
    }
    // An idle friend that was asked twice and is still in the way: go round it.
    if (PlayerOf(b) == p && Idle(b) && F<int>(b, kStance) != 4 && g_asks2[id] >= 2 && g_askedOf[id] == blockerId) {
        g_asks2[id] = 0;
        if (Reroute(c, u, b, false)) return;
    }
    if (PlayerOf(b) == p && Idle(b) && F<int>(b, kStance) != 4) {
        if (g_askedOf[id] != blockerId) g_askedOf[id] = blockerId, g_asks2[id] = 0;
        if (g_turn - g_askedAt[id] >= 20) {
            ++g_asks2[id];
            g_askedAt[id] = g_turn;
            ++g_asks;
            StepAside(b, c, u);
            if (g_dbg.test) g_log("pathing: bump id %d asks %d to step aside", id, blockerId);
        }
        F<short>(c, kWaitTicks) = 6;
        return;
    }
    // Following a friend going the same way (a queue): just wait for it.
    const UnitRoute& bu = g_routes[blockerId];
    if (PlayerOf(b) == p && bu.valid && bu.thing == b && !Idle(b)) {
        nav::Pt mine = u.next < u.route.nodes.size() ? u.route.nodes[u.next] : u.route.end;
        nav::Pt theirs = bu.next < bu.route.nodes.size() ? bu.route.nodes[bu.next] : bu.route.end;
        int ax = mine.x - F<short>(c, kX), ay = mine.y - F<short>(c, kY);
        int bx = theirs.x - F<short>(b, kX), by = theirs.y - F<short>(b, kY);
        if (ax * bx + ay * by > 0 && g_blockedBy[blockerId] != id) {
            u.blockedSince = -1;
            F<short>(c, kWaitTicks) = 4;
            return;
        }
    }
    if (u.blockedSince < 0) u.blockedSince = g_turn;
    // Head-on (each blocks the other): the lower id gives way at once, round the
    // other if it can, else backing off out of its path.
    bool headOn = g_blockedBy[blockerId] == id && g_turn - g_blockedAt[blockerId] <= 8;
    bool yield = headOn && id < blockerId;
    if (yield || (g_turn - u.blockedSince >= kJamTurns && g_turn >= u.retryTurn)) {
        // A short way round first; giving way head-on, then backing off; a long way round last.
        if (Reroute(c, u, b, false)) {
            u.blockedSince = -1;
            return;
        }
        if (yield && BackOff(c, b)) {
            u.blockedSince = -1;
            u.valid = false;  // its top waypoint changed: plan to it
            return;
        }
        if (yield && Reroute(c, u, b, true)) {
            u.blockedSince = -1;
            return;
        }
        u.retryTurn = g_turn + kJamTurns / 2;  // try again in half a second
    }
    if (g_dbg.test && g_turn % 10 == 0)
        g_log("pathing: id %d blocked by %d (head-on %d, jammed %d turns)", id, blockerId, headOn, g_turn - u.blockedSince);
    F<short>(c, kWaitTicks) = 4;
}

// --- group moves: each unit its own spot ---------------------------------------
// CommandModel::ExecuteMove 0x461b00 (IdList*, PtList*, short tol) gives every
// unit of a group the same final point (and a tolerance grown with the group's
// size). Improved hooks its callers (single player, multiplayer, the computer
// players; not FormationMove) and, for two or more units, gives each its own
// spot: a lattice spiralling out from the final point, spaced for the group's
// largest unit; each spot passable for the unit, in its region, clear of the
// spots already taken, and taken by the nearest unassigned unit (ties by id).
// Flyers get spots spaced by radius only. Each unit then gets its own stock
// ExecuteMove of one unit, its spot replacing the final point, tolerance 0.

constexpr uintptr_t kExecuteMove = 0x461b00;
constexpr uintptr_t kExecuteMoveCalls[] = {0x46045a, 0x45ad24, 0x4b014b, 0x4bb21f};


typedef void(__thiscall* ExecuteMove_t)(void*, IdList*, PtList*, int);

struct Member {
    char* c;
    int id, r, hf, p, region;
    bool flyer;
    nav::Pt spot;
    bool placed;
};

// Spots round (gx, gy) for the members; deterministic for the same group and world.
void AssignSpots(std::vector<Member>& m, int gx, int gy) {
    for (int air = 0; air <= 1; ++air) {
        int rmax = 0, left = 0;
        for (const Member& u : m)
            if (u.flyer == (air == 1)) rmax = std::max(rmax, u.r), ++left;
        if (!left) continue;
        int sp = 2 * rmax + 10;
        std::vector<nav::Pt> taken;
        std::vector<int> takenR;
        int mapW = *(uint8_t*)kMapTilesW * 32, mapH = *(uint8_t*)kMapTilesH * 32;
        for (int k = 0; left > 0 && k <= 24; ++k)
            for (int t = 0; left > 0 && t < (k ? 8 * k : 1); ++t) {
                // Ring k, clockwise from the top-left corner.
                int side = t / (2 * k + (k == 0)), off = t % (2 * k + (k == 0));
                int dx = 0, dy = 0;
                if (k) {
                    if (side == 0) dx = -k + off, dy = -k;
                    if (side == 1) dx = k, dy = -k + off;
                    if (side == 2) dx = k - off, dy = k;
                    if (side == 3) dx = -k, dy = k - off;
                }
                nav::Pt spot{gx + dx * sp, gy + dy * sp};
                if (spot.x < 40 || spot.y < 40 || spot.x > mapW - 40 || spot.y > mapH - 40) continue;
                int best = -1, bestD = 0;
                for (size_t i = 0; i < m.size(); ++i) {
                    Member& u = m[i];
                    if (u.placed || u.flyer != (air == 1)) continue;
                    nav::SetQueryUnit(u.id);
                    if (!u.flyer && (!nav::Passable(spot.x, spot.y, u.r, u.hf, u.p) ||
                                     nav::Region(spot.x, spot.y, u.r) != u.region))
                        continue;
                    bool clear = true;
                    for (size_t j = 0; j < taken.size() && clear; ++j) {
                        int ex = spot.x - taken[j].x, ey = spot.y - taken[j].y, need = u.r + takenR[j] + 8;
                        clear = ex * ex + ey * ey >= need * need;
                    }
                    if (!clear) continue;
                    int ux = F<short>(u.c, kX) - spot.x, uy = F<short>(u.c, kY) - spot.y, d = ux * ux + uy * uy;
                    if (best < 0 || d < bestD || (d == bestD && u.id < m[best].id)) best = (int)i, bestD = d;
                }
                if (best < 0) continue;
                m[best].spot = spot, m[best].placed = true;
                taken.push_back(spot);
                takenR.push_back(m[best].r);
                --left;
            }
    }
}

int g_spreads;

void __fastcall HookExecuteMove(void* cmd, void*, IdList* ids, PtList* pts, int tol) {
    auto stock = (ExecuteMove_t)kExecuteMove;
    if (!g_improved || !nav::Ready() || !ids || !pts || ids->count < 2 || pts->count < 1) {
        stock(cmd, ids, pts, tol);
        return;
    }
    PixelPt goal = pts->data[pts->count - 1];
    std::vector<Member> m;
    for (uint32_t i = 0; i < ids->count; ++i) {
        char* c = ThingById((unsigned short)ids->data[i]);
        if (!c || !IsCreature(F<int>(c, kType))) continue;
        int type = F<int>(c, kType);
        Member u{c, (unsigned short)ids->data[i], Radius(type), ((short*)kFootprint)[type] / 2, PlayerOf(c), 0,
                 IsFlyer(type), {goal.x, goal.y}, false};
        u.region = nav::Region(F<short>(c, kX), F<short>(c, kY), u.r);
        m.push_back(u);
    }
    if (m.size() < 2) {
        stock(cmd, ids, pts, tol);
        return;
    }
    AssignSpots(m, goal.x, goal.y);
    nav::SetQueryUnit(-1);
    ++g_spreads;
    if (g_dbg.test) g_log("pathing: spread %d units round %d,%d", (int)m.size(), goal.x, goal.y);
    std::vector<PixelPt> copy(pts->data, pts->data + pts->count);
    for (Member& u : m) {
        copy.back() = {goal.vtbl, (short)u.spot.x, (short)u.spot.y};
        short one = (short)u.id;
        IdList oneId{ids->vtbl, &one, 1, 1};
        PtList list{pts->vtbl, copy.data(), (uint32_t)copy.size(), (uint32_t)copy.size()};
        stock(cmd, &oneId, &list, 0);
    }
}

// --- building sites ---------------------------------------------------------
// A building can only be created on free ground: CreateAt collides it with every
// object but its builder, so one unit standing on the site (a helper of a group
// build, say) makes the builder fail, step aside and try again. Every turn the
// sites of Shamans walking to build (activity 0x2e, nothing created yet) or
// plant (0x2d) are reserved: the builder's player's other units route round
// them, aren't given spots or goals on them, and an idle one standing on one
// is walked off it, to the nearest side.

constexpr uintptr_t kWorkType = 0x184;                   // AShaman int: building or pod type being worked on
constexpr uintptr_t kSiteX = 0x16c, kSiteY = 0x16e;     // AShaman short: the Build site (centre)
constexpr uintptr_t kWorkTarget = 0x18e;                 // short: the building once created (0xc01 before)
constexpr uintptr_t kPlantPoints = 0x1a0, kPlantCount = 0x1a4;  // AShaman IList<IPixelC>: pods to plant
constexpr uintptr_t kVFollowPath = 0xb4;                 // ACreature: OrderMove (PtList*, short tol)

// Building together (Improved: a group build gives every builder the same Build
// order, so they all walk to the site, stand at their own work spots round it and
// play the build animation). The work counter (+0x180, short) counts down once per
// work tick from S = 0x443d10() (0x54c670[type]); the building is created when
// S - counter == 0x40 (work tick 0x4436b0, 0x443bae). Each turn the builders of
// one site pool their work: the most advanced counts one extra tick per other
// builder on site, and only it may create the building (the rest are held a tick
// short). Once it stands, the others are switched to Repair (which builds it on).
constexpr uintptr_t kWorkCounter = 0x180, kWorkStart = 0x443d10;
constexpr uintptr_t kRepairExec = 0x4621b0;  // stdcall (builderId, targetId): Repair, as the command runs it
int g_helpTicks, g_joins;

struct Builder {
    char* c;
    int id, x, y, type, p, reach;  // reach: how far the building will collide
};

// Work spots: each builder of a site gets its own spot on a circle round it, at
// its radius + 60 px from the centre (the work reach is radius + 64, IsAtWorkSite
// 0x4411d0) and clear of the building's reach; spaced two radii + 8 px apart, the
// nearest free one to the builder, kept once given. A builder routes to its spot
// instead of the centre, so a group fans out round the building rather than
// queueing on one side.
struct WorkSpot {
    int siteX = -1, siteY = -1;
    nav::Pt at{};
};
std::vector<WorkSpot> g_workSpot(65536);

void AssignWorkSpots(const std::vector<const Builder*>& group, int reach) {
    std::vector<nav::Pt> taken;
    for (const Builder* b : group) {  // keep the spots already given for this site
        WorkSpot& w = g_workSpot[b->id];
        if (w.siteX == b->x && w.siteY == b->y) taken.push_back(w.at);
    }
    for (const Builder* b : group) {
        WorkSpot& w = g_workSpot[b->id];
        if (w.siteX == b->x && w.siteY == b->y) continue;
        int r = Radius(F<int>(b->c, kType)), hf = ((short*)kFootprint)[F<int>(b->c, kType)] / 2;
        int d = std::max(r + 60, reach + r + 4), gap = 2 * r + 8;
        int bx = F<short>(b->c, kX), by = F<short>(b->c, kY), best = -1, bestD = 0;
        nav::Pt pick{};
        nav::SetQueryUnit(b->id, b->x, b->y);
        for (int k = 0; k < 16; ++k) {
            nav::Pt q{b->x + kCos16[k] * d / 1000, b->y + kSin16[k] * d / 1000};
            bool clear = nav::Passable(q.x, q.y, r, hf, b->p);
            for (const nav::Pt& t : taken)
                clear = clear && (q.x - t.x) * (q.x - t.x) + (q.y - t.y) * (q.y - t.y) >= gap * gap;
            if (!clear) continue;
            int dd = (q.x - bx) * (q.x - bx) + (q.y - by) * (q.y - by);
            if (best < 0 || dd < bestD) best = k, bestD = dd, pick = q;
        }
        nav::SetQueryUnit(-1);
        if (best < 0) continue;  // no room: it heads for the centre as before
        w.siteX = b->x, w.siteY = b->y, w.at = pick;
        taken.push_back(pick);
    }
}

// The spot a Shaman building at its site should walk to, if it has one.
bool WorkSpotFor(char* c, nav::Pt& out) {
    if (F<int>(c, kActivity) != 0x2e) return false;
    const WorkSpot& w = g_workSpot[F<unsigned short>(c, kId)];
    if (w.siteX != F<short>(c, kSiteX) || w.siteY != F<short>(c, kSiteY)) return false;
    out = w.at;
    return true;
}

void BuildTogether(std::vector<Builder>& builds, char** things, int count) {
    // A building of the type now standing on the site (the lead made it): the
    // others at that site repair it, which builds it on.
    for (Builder& b : builds) {
        for (int i = 0; i < count && b.id >= 0; ++i) {
            char* t = things[i];
            if (!t || F<int>(t, kType) != b.type || PlayerOf(t) != b.p) continue;
            if (abs(F<short>(t, kX) - b.x) > 2 || abs(F<short>(t, kY) - b.y) > 2) continue;
            int made = F<unsigned short>(t, kId);
            ((void(__stdcall*)(int, int))kRepairExec)(b.id, made);
            ++g_joins;
            if (g_dbg.test) g_log("pathing: builder %d joins building %d", b.id, made);
            b.id = -1;  // not pending any more
        }
    }
    builds.erase(std::remove_if(builds.begin(), builds.end(), [](const Builder& b) { return b.id < 0; }), builds.end());
    // Pool the start of each site's group.
    for (size_t i = 0; i < builds.size(); ++i) {
        const Builder& lead0 = builds[i];
        bool first = true;
        for (size_t j = 0; j < i; ++j)
            first &= !(builds[j].p == lead0.p && builds[j].x == lead0.x && builds[j].y == lead0.y && builds[j].type == lead0.type);
        if (!first) continue;
        std::vector<const Builder*> group;
        for (const Builder& b : builds)
            if (b.p == lead0.p && b.x == lead0.x && b.y == lead0.y && b.type == lead0.type) group.push_back(&b);
        AssignWorkSpots(group, lead0.reach);
        if (group.size() < 2) continue;
        short start = ((short(__thiscall*)(void*))kWorkStart)(group[0]->c);
        const Builder* lead = nullptr;
        int leadDone = 0, onSite = 0;
        for (const Builder* b : group) {
            int done = start - F<short>(b->c, kWorkCounter);
            if (done <= 0 || done > 0x40) continue;  // not working on site yet
            ++onSite;
            if (!lead || done > leadDone) lead = b, leadDone = done;
        }
        if (!lead) continue;
        int next = std::min(leadDone + onSite - 1, 0x3f);
        g_helpTicks += next - leadDone;
        F<short>(lead->c, kWorkCounter) = (short)(start - next);
        for (const Builder* b : group) {
            if (b == lead) continue;
            int done = start - F<short>(b->c, kWorkCounter);
            if (done <= 0 || done > 0x40) continue;
            F<short>(b->c, kWorkCounter) = (short)(start - std::min(next, 0x3e));  // a tick short: the lead creates it
        }
    }
}

std::vector<nav::Site> g_siteList;
std::vector<int> g_clearedAt(65536, -100);  // turn a unit was last walked off a site
int g_siteClears;

void MoveUnit(char* c, int x, int y) {  // the unit's own move order (simulation side, every machine)
    PixelPt pt{0x5031c4, (short)x, (short)y};
    PtList list{0x5033d4, &pt, 1, 1};
    ((void(__thiscall*)(void*, PtList*, int))(*(void***)c)[kVFollowPath / 4])(c, &list, 0);
}

void UpdateSites() {
    g_siteList.clear();
    static std::vector<Builder> builds;
    builds.clear();
    int count;
    char** things = Things(count);
    for (int i = 0; i < count; ++i) {
        char* t = things[i];
        if (!t || !IsCreature(F<int>(t, kType)) || IsFlyer(F<int>(t, kType))) continue;
        int act = F<int>(t, kActivity), type = F<int>(t, kWorkType), p = PlayerOf(t), id = F<unsigned short>(t, kId);
        if (p < 0 || type < 0x2c || type > 0x41) continue;
        // How far the building will collide (CreateAt): its shape circles (0x528264), which reach past
        // its footprint, the footprint's corners (the object grid) and its radius.
        int half = ((short*)kFootprint)[type] / 2 * 1415 / 1000;
        half = std::max(half, Radius(type));
        if (type <= 0x3e) {
            char* shape = *(char**)(kDeviceShapes + (type - 0x2c) * 16);
            int n = (int)*(uint32_t*)(kDeviceShapes + (type - 0x2c) * 16 + 4);
            for (int k = 0; k < n && shape; ++k) {
                int dx = *(short*)(shape + k * 12 + 4), dy = *(short*)(shape + k * 12 + 6);
                half = std::max(half, ISqrtDbg(dx * dx + dy * dy) + 1 + *(uint8_t*)(shape + k * 12 + 8));
            }
        }
        half += 2;
        if (act == 0x2e && F<short>(t, kWorkTarget) == (short)0xc01) {
            g_siteList.push_back({F<short>(t, kSiteX), F<short>(t, kSiteY), half, p, id});
            if (type <= 0x3e) builds.push_back({t, id, F<short>(t, kSiteX), F<short>(t, kSiteY), type, p, half});
        }
        if (act == 0x2d) {
            char* pts = F<char*>(t, kPlantPoints);
            int n = std::min(F<int>(t, kPlantCount), 8);
            for (int k = 0; pts && k < n; ++k)
                g_siteList.push_back({*(short*)(pts + k * 8 + 4), *(short*)(pts + k * 8 + 6), half, p, id});
        }
    }
    BuildTogether(builds, things, count);
    nav::SetSites(g_siteList);
    if (g_siteList.empty()) return;
    // Idle units of the builder's player standing on a site: off it, to the nearest side.
    for (int i = 0; i < count; ++i) {
        char* t = things[i];
        if (!t || !IsCreature(F<int>(t, kType)) || IsFlyer(F<int>(t, kType)) || !Idle(t)) continue;
        int id = F<unsigned short>(t, kId), p = PlayerOf(t), r = Radius(F<int>(t, kType));
        int hf = ((short*)kFootprint)[F<int>(t, kType)] / 2, x = F<short>(t, kX), y = F<short>(t, kY);
        if (F<int>(t, kStance) == 4 || g_turn - g_clearedAt[id] < 40) continue;
        nav::SetQueryUnit(id);
        for (const nav::Site& site : g_siteList) {
            if (site.player != p || site.builder == id || !nav::OnSite(x, y, r, p)) continue;
            int out = site.half + r + 12, dx = x - site.x, dy = y - site.y, len = ISqrtDbg(dx * dx + dy * dy);
            if (len == 0) dx = 1, dy = 0, len = 1;
            nav::Pt cand[4] = {{site.x + dx * out / len, site.y + dy * out / len},
                               {site.x - dy * out / len, site.y + dx * out / len},
                               {site.x + dy * out / len, site.y - dx * out / len},
                               {site.x - dx * out / len, site.y - dy * out / len}};
            nav::Pt to = cand[0];
            for (const nav::Pt& q : cand)
                if (nav::Passable(q.x, q.y, r, hf, p)) {
                    to = q;
                    break;
                }
            g_clearedAt[id] = g_turn;
            ++g_siteClears;
            if (g_dbg.test) g_log("pathing: id %d steps off the site at %d,%d to %d,%d", id, site.x, site.y, to.x, to.y);
            MoveUnit(t, to.x, to.y);
            break;
        }
        nav::SetQueryUnit(-1);
    }
}

// --- test scenarios ([Debug] PathTest, autopilot mode 26) ------------------
// Each runs from the start of the game turn, keyed by turns since the mission
// start, so orders land on the same turn in every run; the random generators are
// pinned at the start. Results are PASS/FAIL lines for tools/path_check.py.


void DumpUnit(int test, int id) {
    char* c = ThingById(id);
    if (!c) return;
    const UnitRoute& u = g_routes[id];
    int n = F<int>(c, kWaypointCount);
    char* top = n ? F<char*>(c, kWaypoints) + (n - 1) * 8 : nullptr;
    g_log("pathing: test %d   unit %d at %d,%d activity %#x waypoints %d top %d,%d steps %d wait %d tol %d; route %s "
          "next %d/%d end %d,%d exact %d, progress %d turns ago",
          test, id, F<short>(c, kX), F<short>(c, kY), F<int>(c, kActivity), n, top ? *(short*)(top + 4) : 0,
          top ? *(short*)(top + 6) : 0, F<int>(c, kStepCount), F<short>(c, 0x140), F<int>(c, 0xc4),
          u.valid && u.thing == c ? "valid" : "none", (int)u.next, (int)u.route.nodes.size(), u.route.end.x,
          u.route.end.y, u.route.exact, g_turn - u.progressTurn);
}

// The test's own side: the local player's first creature (tests only: the
// computer players would give their own units orders of their own).
void* FirstCreature() {
    void* me = ((void* (*)())0x46f8e0)();
    int count;
    char** things = Things(count);
    for (int i = 0; i < count; ++i)
        if (things[i] && IsCreature(F<int>(things[i], kType)) && F<void*>(things[i], kOwner) == me) return things[i];
    return nullptr;
}

// Buildings (unfinished: they collide all the same) and rocks round the first
// creature, so the oracle meets every kind of thing.
bool FindOpenArea(int x, int y, int hw, int hh, nav::Pt& out);

void SpawnTestBuildings() {
    char* c = (char*)FirstCreature();
    if (!c) return;
    short owner = (short)PlayerOf(c);
    // On open ground: with the placement checks skipped, a thing put on another's
    // top-left tile would take its place in the game's occupancy grid (one id per tile).
    nav::Pt open;
    if (!FindOpenArea(F<short>(c, kX), F<short>(c, kY), 352, 352, open)) open = {F<short>(c, kX), F<short>(c, kY)};
    int x = open.x, y = open.y;
    static const int kTypes[] = {0x2c, 0x2e, 0x2f, 0x30, 0x31, 0x33, 0x39, 0x3a, 0x45, 0x47, 0x43};
    static const int kCos[] = {100, 84, 42, -14, -65, -96, -96, -65, -14, 42, 84};
    static const int kSin[] = {0, 54, 91, 99, 76, 28, -28, -76, -99, -91, -54};
    int made = 0;
    for (int i = 0; i < 11; ++i) {
        int dist = 160 + (i % 3) * 64, px = x + kCos[i] * dist / 100, py = y + kSin[i] * dist / 100;
        int before, after;
        Things(before);
        ((short(__thiscall*)(void*, short, int, short, short, short, short, short))kCreateAt)(
            *(void**)kWorld, owner, kTypes[i], (short)px, (short)py, 1, 1, (short)0xc01);
        Things(after);
        made += after > before;
        g_log("pathing: test spawn type %#x at %d,%d: %s", kTypes[i], px, py, after > before ? "made" : "failed");
    }
    g_log("pathing: test spawned %d things", made);
}

// Every 8 px of the map, for two unit sizes: the fixed terrain box against the
// stock function (and its emulation, which must agree exactly), things against
// FindThingAt, and every lattice node the grid calls free against the exact test.
void Oracle() {
    ScanWorld();
    int w = *(uint8_t*)kMapTilesW * 32, h = *(uint8_t*)kMapTilesH * 32;
    static const int kSizes[][2] = {{18, 16}, {29, 22}};
    int emuMismatch = 0, fixedDiff = 0, thingMismatch = 0, latticeMismatch = 0, points = 0, skipped = 0, conservative = 0;
    for (const auto& sz : kSizes) {
        int r = sz[0], hf = sz[1];
        for (int y = 0; y < h; y += 8)
            for (int x = 0; x < w; x += 8) {
                ++points;
                Box tl{0x5030ec, (short)(x - r < 0 ? -1 : (x - r) >> 5), (short)(y - r < 0 ? -1 : (y - r) >> 5)};
                Box br{0x5030ec, (short)((x + r) >> 5), (short)((y + r) >> 5)};
                bool game = ((short(__thiscall*)(void*, Box*, Box*, int))kIsRectBlocked)((void*)kGrid, &tl, &br, 0) != 0;
                bool emu = StockRectBlocked(tl.x, tl.y, br.x, br.y);
                bool fixed = nav::TerrainBoxBlocked(x - r, y - r, x + r, y + r);
                bool expect = g_rectPatched ? fixed : emu;  // what the game's function should say
                if (expect != game && emuMismatch++ < 5)
                    g_log("pathing: MISMATCH terrain at %d,%d r %d: game %d expected %d (%s)", x, y, r, game, expect,
                          g_rectPatched ? "fixed" : "stock emulation");
                fixedDiff += fixed != emu;
                char* hit = ((char*(__thiscall*)(void*, int, int, int, int, int, int))kFindThingAt)(
                    *(void**)kWorld, 0xc01, x, y, r, hf, 0);
                if (hit && IsCreature(F<int>(hit, kType))) {
                    ++skipped;
                    continue;
                }
                bool ours = nav::ThingsBlocked(x, y, r, hf, nav::kAll);
                // FindThingAt only scans things whose top-left tile is in a box round the point,
                // so a building whose shape reaches past it is missed on that side: the grid is
                // stricter there (routes stay clear of it), which is safe.
                if (ours && !hit) {
                    ++conservative;
                    continue;
                }
                if (ours != (hit != nullptr) && thingMismatch++ < 8)
                    g_log("pathing: MISMATCH things at %d,%d r %d hf %d: game %s (vtbl %#x hit test %#x type %#x id %d at "
                          "%d,%d tl %d,%d) ours %d",
                          x, y, r, hf, hit ? "hit" : "free", hit ? F<uint32_t>(hit, 0) : 0,
                          hit ? (*(uint32_t**)hit)[0x2c / 4] : 0, hit ? F<int>(hit, kType) : -1,
                          hit ? F<unsigned short>(hit, kId) : -1, hit ? F<short>(hit, kX) : 0, hit ? F<short>(hit, kY) : 0,
                          hit ? F<short>(hit, kTopLeftX) : 0, hit ? F<short>(hit, kTopLeftY) : 0, ours);
            }
        for (int j = 0; j < nav::LatticeH(); ++j)
            for (int i = 0; i < nav::LatticeW(); ++i) {
                if (nav::StaticClearance(i, j) <= r) continue;
                int x = i * nav::kLattice, y = j * nav::kLattice;
                if ((nav::TerrainBoxBlocked(x - r, y - r, x + r, y + r) || nav::ThingsBlocked(x, y, r, hf, -1)) &&
                    latticeMismatch++ < 5)
                    g_log("pathing: MISMATCH lattice node %d,%d free for r %d but blocked", x, y, r);
            }
    }
    g_log("pathing: oracle %d points (%d on creatures): terrain mismatches %d, fixed differs from stock at %d, "
          "things mismatches %d (stricter than the game at %d), lattice mismatches %d",
          points, skipped, emuMismatch, fixedDiff, thingMismatch, conservative, latticeMismatch);
    bool ok = emuMismatch == 0 && thingMismatch == 0 && latticeMismatch == 0;
    g_log("pathing: test 2 %s (grid agrees with the game's collision tests)", ok ? "PASS" : "FAIL");
}

constexpr uintptr_t kPlaceCreature = 0x4472d0;  // World ushort(player, type, x, y, 4 gene values)
constexpr uintptr_t kCmdMove = 0x34;            // Cmd (IdList*, PtList*, short tol)
constexpr uintptr_t kTolerance = 0xc4;          // ACreature: arrival tolerance (px^2)

int SpawnUnit(int player, int type, int x, int y) {
    return ((unsigned short(__thiscall*)(void*, int, int, int, int, int, int, int, int))kPlaceCreature)(
        *(void**)kWorld, player, type, x, y, 100, 100, 100, 100);
}

void SpawnObject(int player, int type, int x, int y) {
    ((short(__thiscall*)(void*, short, int, short, short, short, short, short))kCreateAt)(
        *(void**)kWorld, (short)player, type, (short)x, (short)y, 1, 1, (short)0xc01);
}

void OrderMove(int id, int x, int y) {  // as a click would: through the command model
    struct {
        uint32_t vtbl;
        short x, y;
    } pt{0x5031c4, (short)x, (short)y};
    struct {
        uint32_t vtbl;
        void* data;
        uint32_t count, cap;
    } pts{0x5033d4, &pt, 1, 1};
    short sid = (short)id;
    struct {
        uint32_t vtbl;
        short* data;
        uint32_t count, cap;
    } ids{0x5030dc, &sid, 1, 1};
    void* cmd = Cmd();
    ((void(__thiscall*)(void*, void*, void*, int))(*(void***)cmd)[kCmdMove / 4])(cmd, &ids, &pts, 0);
}

// An open area (every 16 px passable for the largest unit) of the given half size,
// spiralling out from (x, y) in 64 px steps.
bool FindOpenArea(int x, int y, int hw, int hh, nav::Pt& out) {
    for (int ring = 0; ring < 48; ++ring)
        for (int dy = -ring; dy <= ring; ++dy)
            for (int dx = -ring; dx <= ring; ++dx) {
                if (std::max(abs(dx), abs(dy)) != ring) continue;
                int cx = x + dx * 64, cy = y + dy * 64;
                bool ok = true;
                for (int py = cy - hh; py <= cy + hh && ok; py += 16)
                    for (int px = cx - hw; px <= cx + hw && ok; px += 16)
                        ok = nav::Passable(px, py, 31, 22, nav::kAll);
                if (ok) {
                    out = {cx, cy};
                    return true;
                }
            }
    return false;
}

int ISqrtDbg(int v) {
    int r = 0;
    while ((r + 1) * (r + 1) <= v) ++r;
    return r;
}

// One unit's move, tracked to the end: arrived (idle within 32 px), or a FAIL at the limit.
struct Tracked {
    int id = -1;
    nav::Pt goal{};
    int ordered = 0, limit = 0;
    bool done = false, pass = false;
};

void Track(int test, Tracked& tr, int t, const char* what) {
    if (tr.done || tr.id < 0) return;
    char* c = ThingById(tr.id);
    if (!c) {
        tr.done = true;
        g_log("pathing: test %d FAIL (%s: unit %d gone)", test, what, tr.id);
        return;
    }
    int dx = F<short>(c, kX) - tr.goal.x, dy = F<short>(c, kY) - tr.goal.y, d2 = dx * dx + dy * dy;
    bool idle = F<int>(c, kActivity) == 0x28 && F<int>(c, kWaypointCount) == 0 && F<int>(c, kStepCount) == 0;
    if (!idle && t < tr.ordered + tr.limit) return;
    tr.done = true;
    tr.pass = idle && d2 <= 32 * 32;
    g_log("pathing: test %d %s (%s: unit %d %s after %d turns at %d,%d, %d px from %d,%d, tolerance %d)", test,
          tr.pass ? "PASS" : "FAIL", what, tr.id, idle ? "idle" : "still moving", t - tr.ordered, F<short>(c, kX),
          F<short>(c, kY), ISqrtDbg(d2), tr.goal.x, tr.goal.y, F<int>(c, kTolerance));
}

nav::Pt g_area;
Tracked g_t1;

// A player other than p that owns a creature (the enemy side of a test), or -1.
int OtherPlayer(int p) {
    int count;
    char** things = Things(count);
    for (int i = 0; i < count; ++i)
        if (things[i] && IsCreature(F<int>(things[i], kType)) && PlayerOf(things[i]) >= 0 && PlayerOf(things[i]) != p)
            return PlayerOf(things[i]);
    return -1;
}

// Tests 4 and 5: a north-south wall of Relays across the unit's way east.
// 4: the player's own (known from the start); 5: an enemy's, out of sight until
// the unit walks up to it.
void WallTest(int test, int t) {
    char* me = (char*)FirstCreature();
    if (t == 5) {
        if (!me || !FindOpenArea(F<short>(me, kX), F<short>(me, kY), 560, 400, g_area)) {
            g_log("pathing: test %d FAIL (no open area)", test);
            g_testDone = true;
            return;
        }
        int p = PlayerOf(me), owner = test == 4 ? p : OtherPlayer(p), ox = g_area.x, oy = g_area.y;
        if (owner < 0) {
            g_log("pathing: test %d FAIL (no other player)", test);
            g_testDone = true;
            return;
        }
        for (int y = oy - 320; y <= oy + 320; y += 32) SpawnObject(owner, 0x2e, ox, y);
        int start = test == 4 ? ox - 160 : ox - 520;
        g_t1.id = SpawnUnit(p, 1, start, oy);
        g_t1.goal = {ox + 200, oy};
        g_log("pathing: test %d wall of player %d at x %d, unit %d (player %d) from %d,%d", test, owner, ox, g_t1.id, p,
              start, oy);
    }
    if (t == 20) {
        OrderMove(g_t1.id, g_t1.goal.x, g_t1.goal.y);
        g_t1.ordered = t, g_t1.limit = 600;
    }
    if (t > 20) Track(test, g_t1, t, test == 4 ? "round an own building wall" : "round an unseen enemy wall");
    if (g_t1.done) g_testDone = true;
}

// Test 7 (T3): one unit ordered in turn into a cluster of rocks, into blocked
// terrain and onto an own building. Each time it must end idle at the nearest
// reachable point (logged by the retarget), close to where it was sent, with the
// order's tolerance (no creep).
struct Unreachable {
    const char* what;
    nav::Pt goal;
};
std::vector<Unreachable> g_t3;
size_t g_t3Step;
int g_t3Pass;

bool FindBlockedTerrain(int x, int y, nav::Pt& out) {  // the middle of a 3x3-tile blocked patch
    int w = *(uint8_t*)kMapTilesW, h = *(uint8_t*)kMapTilesH, best = -1;
    for (int ty = 1; ty < h - 1; ++ty)
        for (int tx = 1; tx < w - 1; ++tx) {
            bool all = true;
            for (int j = -1; j <= 1 && all; ++j)
                for (int i = -1; i <= 1 && all; ++i) all = TileBlocked(tx + i, ty + j);
            int dx = tx * 32 + 16 - x, dy = ty * 32 + 16 - y, d2 = dx * dx + dy * dy;
            if (all && (best < 0 || d2 < best)) best = d2, out = {tx * 32 + 16, ty * 32 + 16};
        }
    return best >= 0 && best < 1200 * 1200;
}

void UnreachableTest(int t) {
    char* me = (char*)FirstCreature();
    if (t == 5) {
        if (!me || !FindOpenArea(F<short>(me, kX), F<short>(me, kY), 400, 300, g_area)) {
            g_log("pathing: test 7 FAIL (no open area)");
            g_testDone = true;
            return;
        }
        int p = PlayerOf(me), ox = g_area.x, oy = g_area.y;
        for (int j = -1; j <= 1; ++j)
            for (int i = -1; i <= 1; ++i) SpawnObject(p, 0x45, ox + 200 + i * 40, oy + j * 40);
        SpawnObject(p, 0x2c, ox - 200, oy + 150);  // a Refinery
        g_t1.id = SpawnUnit(p, 1, ox - 300, oy - 200);
        g_t3 = {{"into a cluster of rocks", {ox + 200, oy}}, {"onto an own building", {ox - 200, oy + 150}}};
        nav::Pt water{};
        if (FindBlockedTerrain(ox, oy, water)) g_t3.push_back({"into blocked terrain", water});
        g_t3Step = 0;
        g_t1.ordered = -1;
    }
    if (t < 20 || g_t3Step >= g_t3.size()) return;
    if (g_t1.ordered < 0) {
        g_t1.goal = g_t3[g_t3Step].goal;
        g_retarget[g_t1.id] = {-1, -1};
        OrderMove(g_t1.id, g_t1.goal.x, g_t1.goal.y);
        g_t1.ordered = t, g_t1.limit = 500, g_t1.done = false;
        return;
    }
    char* c = ThingById(g_t1.id);
    if (!c) return;
    bool idle = F<int>(c, kActivity) == 0x28 && F<int>(c, kWaypointCount) == 0 && F<int>(c, kStepCount) == 0;
    if (!idle && t < g_t1.ordered + g_t1.limit) return;
    nav::Pt rt = g_retarget[g_t1.id], at{F<short>(c, kX), F<short>(c, kY)};
    int dx = at.x - rt.x, dy = at.y - rt.y, gx = at.x - g_t1.goal.x, gy = at.y - g_t1.goal.y;
    bool ok = idle && rt.x >= 0 && dx * dx + dy * dy <= 32 * 32 && gx * gx + gy * gy <= 160 * 160 &&
              F<int>(c, kTolerance) == 0;
    g_t3Pass += ok;
    g_log("pathing: test 7 %s %s: %s after %d turns at %d,%d (retarget %d,%d, %d px from the goal, tolerance %d)",
          ok ? "ok" : "BAD", g_t3[g_t3Step].what, idle ? "idle" : "still moving", t - g_t1.ordered, at.x, at.y, rt.x, rt.y,
          ISqrtDbg(gx * gx + gy * gy), F<int>(c, kTolerance));
    g_t1.ordered = -1;
    if (++g_t3Step == g_t3.size()) {
        g_log("pathing: test 7 %s (unreachable clicks: %d of %d ended at the nearest reachable point)",
              g_t3Pass == (int)g_t3.size() ? "PASS" : "FAIL", g_t3Pass, (int)g_t3.size());
        g_testDone = true;
    }
}

void OrderMoveGroup(const std::vector<short>& idList, int x, int y, int tol) {
    struct {
        uint32_t vtbl;
        short x, y;
    } pt{0x5031c4, (short)x, (short)y};
    struct {
        uint32_t vtbl;
        void* data;
        uint32_t count, cap;
    } pts{0x5033d4, &pt, 1, 1};
    std::vector<short> copy = idList;
    struct {
        uint32_t vtbl;
        short* data;
        uint32_t count, cap;
    } ids{0x5030dc, copy.data(), (uint32_t)copy.size(), (uint32_t)copy.size()};
    void* cmd = Cmd();
    ((void(__thiscall*)(void*, void*, void*, int))(*(void***)cmd)[kCmdMove / 4])(cmd, &ids, &pts, tol);
}

// The stock group tolerance (World_GroupArrivalTolerance 0x44ceb0), for a group of n units of radius r.
int GroupTolerance(int n, int r) {
    int sum = n * (r + 1);
    return n < 2 ? 0 : sum >= 255 ? 0xffff : (sum + 25) * (sum + 25) + 200;
}

// Several units' moves, tracked until every one is idle (or the limit).
struct Crowd {
    std::vector<short> ids;
    std::vector<nav::Pt> goals;  // per unit
    int ordered = 0, limit = 0;
    bool done = false;
};
Crowd g_crowd;

// Logs the result once all are idle or time is up; `ok(i, c)` judges each unit.
template <typename Judge>
void TrackCrowd(int test, int t, const char* what, Judge ok) {
    Crowd& cr = g_crowd;
    if (cr.done) return;
    bool allIdle = true;
    for (short id : cr.ids) {
        char* c = ThingById(id);
        allIdle &= c && Idle(c);
    }
    if ((!allIdle || t < cr.ordered + 10) && t < cr.ordered + cr.limit) return;
    cr.done = true;
    int good = 0;
    for (size_t i = 0; i < cr.ids.size(); ++i) {
        char* c = ThingById(cr.ids[i]);
        bool g = c && Idle(c) && ok(i, c);
        good += g;
        if (!g) DumpUnit(test, cr.ids[i]);
    }
    bool pass = good == (int)cr.ids.size() && g_mismatches == 0 && g_giveUps == 0;
    g_log("pathing: test %d %s (%s: %d of %d after %d turns; %d reroutes, %d asks, %d give-ups, %d mismatches)", test,
          pass ? "PASS" : "FAIL", what, good, (int)cr.ids.size(), t - cr.ordered, g_reroutes, g_asks, g_giveUps,
          g_mismatches);
    g_testDone = true;
}

// Rock walls for the traffic tests: a north-south wall at x with one gap at gapY
// (rock centres `gap` apart), and an east-west corridor (two rows `width` apart).
void RockWall(int p, int x, int y0, int y1, int gapY, int gap) {
    for (int y = y0; y <= y1; y += 40)
        if (abs(y - gapY) * 2 >= gap) SpawnObject(p, 0x45, x, y);
}
void Corridor(int p, int x0, int x1, int y, int width) {
    for (int x = x0; x <= x1; x += 40) {
        SpawnObject(p, 0x45, x, y - width / 2);
        SpawnObject(p, 0x45, x, y + width / 2);
    }
}

void TrafficTest(int test, int t) {
    char* me = (char*)FirstCreature();
    Crowd& cr = g_crowd;
    if (t == 5) {
        if (!me || !FindOpenArea(F<short>(me, kX), F<short>(me, kY), 480, 400, g_area)) {
            g_log("pathing: test %d FAIL (no open area)", test);
            g_testDone = true;
            return;
        }
        int p = PlayerOf(me), ox = g_area.x, oy = g_area.y;
        cr = Crowd();
        if (test == 8) {  // 12 units through one gap (rock centres 120 apart: one unit at a time)
            RockWall(p, ox, oy - 400, oy + 400, oy, 120);
            for (int i = 0; i < 12; ++i) cr.ids.push_back((short)SpawnUnit(p, 1, ox - 300 + (i % 4) * 48, oy - 72 + (i / 4) * 72));
            cr.goals.assign(12, {ox + 300, oy});
        } else {  // a corridor 120 wide (single file), 400 long
            Corridor(p, ox - 200, ox + 200, oy, 120);
            if (test == 9) {  // an own idle unit in the middle; the mover goes through
                cr.ids.push_back((short)SpawnUnit(p, 1, ox - 360, oy));
                cr.goals.push_back({ox + 360, oy});
                g_t1.id = SpawnUnit(p, 1, ox, oy);  // the idle one
            } else {  // two units swap ends
                cr.ids.push_back((short)SpawnUnit(p, 1, ox - 260, oy));
                cr.goals.push_back({ox + 260, oy});
                cr.ids.push_back((short)SpawnUnit(p, 1, ox + 260, oy));
                cr.goals.push_back({ox - 260, oy});
            }
        }
        g_log("pathing: test %d set up at %d,%d with %d units", test, ox, oy, (int)cr.ids.size());
    }
    if (t == 20) {
        cr.ordered = t, cr.limit = 900;
        if (test == 8)
            OrderMoveGroup(cr.ids, cr.goals[0].x, cr.goals[0].y, GroupTolerance(12, Radius(1)));
        else
            for (size_t i = 0; i < cr.ids.size(); ++i) OrderMove(cr.ids[i], cr.goals[i].x, cr.goals[i].y);
    }
    if (t <= 20) return;
    if (test == 8)
        TrackCrowd(test, t, "12 units through a one-unit gap",
                   [&](size_t, char* c) { return F<short>(c, kX) > g_area.x + 30; });
    else
        TrackCrowd(test, t, test == 9 ? "past an own idle unit in a corridor" : "two units swapping ends of a corridor",
                   [&](size_t i, char* c) {
                       int dx = F<short>(c, kX) - cr.goals[i].x, dy = F<short>(c, kY) - cr.goals[i].y;
                       return dx * dx + dy * dy <= 32 * 32;
                   });
}

// Test 11 (T8): 10 ground units and 2 flyers ordered as one group onto open
// ground beside a few rocks: 12 distinct spots, none in a rock, none closer than
// the two units' radii, all near the click.
void SpreadTest(int t) {
    char* me = (char*)FirstCreature();
    Crowd& cr = g_crowd;
    if (t == 5) {
        if (!me || !FindOpenArea(F<short>(me, kX), F<short>(me, kY), 480, 400, g_area)) {
            g_log("pathing: test 11 FAIL (no open area)");
            g_testDone = true;
            return;
        }
        int p = PlayerOf(me), ox = g_area.x, oy = g_area.y;
        cr = Crowd();
        for (int j = -1; j <= 1; ++j) SpawnObject(p, 0x45, ox + 260 + 60, oy + j * 40);  // rocks right by the click
        for (int i = 0; i < 12; ++i)
            cr.ids.push_back((short)SpawnUnit(p, i < 10 ? 1 : 4, ox - 300 + (i % 4) * 48, oy - 72 + (i / 4) * 72));
        cr.goals.assign(12, {ox + 260, oy});
    }
    if (t == 20) {
        cr.ordered = t, cr.limit = 600;
        OrderMoveGroup(cr.ids, cr.goals[0].x, cr.goals[0].y, GroupTolerance(12, Radius(1)));
    }
    if (t <= 20 || cr.done) return;
    bool allIdle = true;
    for (short id : cr.ids) {
        char* c = ThingById(id);
        allIdle &= c && Idle(c);
    }
    if ((!allIdle || t < cr.ordered + 10) && t < cr.ordered + cr.limit) return;
    cr.done = true;
    std::vector<nav::Pt> at;
    std::vector<int> radii, air;
    bool inRock = false;
    int farthest = 0;
    for (short id : cr.ids) {
        char* c = ThingById(id);
        if (!c) continue;
        nav::Pt q{F<short>(c, kX), F<short>(c, kY)};
        at.push_back(q);
        radii.push_back(Radius(F<int>(c, kType)));
        air.push_back(IsFlyer(F<int>(c, kType)));
        if (!IsFlyer(F<int>(c, kType)) && nav::ThingsBlocked(q.x, q.y, Radius(F<int>(c, kType)), 0, nav::kAll))
            inRock = true;
        int dx = q.x - cr.goals[0].x, dy = q.y - cr.goals[0].y;
        farthest = std::max(farthest, ISqrtDbg(dx * dx + dy * dy));
    }
    int closest = 1 << 30, distinct = 0;
    for (size_t i = 0; i < at.size(); ++i) {
        bool unique = true;
        for (size_t j = 0; j < at.size(); ++j) {
            if (i == j) continue;
            int dx = at[i].x - at[j].x, dy = at[i].y - at[j].y, d = ISqrtDbg(dx * dx + dy * dy);
            if (j > i && air[i] == air[j]) closest = std::min(closest, d - radii[i] - radii[j]);  // flyers pass over
            unique &= d > 0;
        }
        distinct += unique;
    }
    for (short id : cr.ids) {
        char* c = ThingById(id);
        if (c && !Idle(c)) DumpUnit(11, id);
    }
    bool pass = allIdle && (int)at.size() == 12 && distinct == 12 && closest >= -8 && !inRock && farthest <= 400 &&
                g_spreads > 0;
    g_log("pathing: test 11 %s (group spread: %d idle units, %d distinct spots, closest pair %d px beyond their radii, "
          "farthest %d px from the click%s, after %d turns)",
          pass ? "PASS" : "FAIL", (int)at.size(), distinct, closest, farthest, inRock ? ", one in a rock" : "", t - cr.ordered);
    g_testDone = true;
}

// Test 12: three idle Shamans stand on a valid building spot; a fourth is ordered
// to build there (the stock order clears the site of them), then one of them is
// sent back right onto the site. It must stop beside it: the building goes up at
// the first attempt (no failure counted, +0x170).
constexpr uintptr_t kCanPlace = 0x4517e0;  // World int(player, x, y, type, ignoreUnits, skipSite, 0), ret 0x1c
constexpr uintptr_t kFailures = 0x170;     // AShaman u8: failed build attempts
// Plants a pod (World 0x448100(player, type, x, y), as the plant work does) on the
// pod soil nearest (px, py), then finds the nearest spot beside it where player p
// may place a Refinery (CanPlace 0); `avoid`: keep away from another test site.
bool BuildSpot(int p, int px, int py, nav::Pt& out, int& type, nav::Pt avoid = {-9999, -9999}) {
    int best = -1, w = *(uint8_t*)kMapTilesW, h = *(uint8_t*)kMapTilesH, ox = px, oy = py;
    for (int ty = 0; ty < h; ++ty)
        for (int tx = 0; tx < w; ++tx) {
            uint8_t q = TileMask(tx >> 1, ty >> 1, false) >> (8 * ((tx & 1) + 2 * (ty & 1)));
            int dx = tx * 32 + 16 - ox, dy = ty * 32 + 16 - oy, d2 = dx * dx + dy * dy;
            if (q == 2 && abs(tx * 32 - avoid.x) > 300 && (best < 0 || d2 < best))
                best = d2, px = tx * 32 + 16, py = ty * 32 + 16;
        }
    int before, after;
    Things(before);
    ((void(__thiscall*)(void*, int, int, int, int))0x448100)(*(void**)kWorld, p, 0x3f, px, py);
    Things(after);
    g_log("pathing: test pod for player %d at %d,%d: %s", p, px, py, after > before ? "planted" : "failed");
    for (int ring = 2; ring < 12; ++ring)
        for (int k = 0; k < 8 * ring; ++k) {
            int side = k / (2 * ring), off = k % (2 * ring);
            int dx = side == 0 ? -ring + off : side == 1 ? ring : side == 2 ? ring - off : -ring;
            int dy = side == 0 ? -ring : side == 1 ? -ring + off : side == 2 ? ring : ring - off;
            int x = px + dx * 32, y = py + dy * 32;
            if (abs(x - avoid.x) < 200 && abs(y - avoid.y) < 200) continue;
            if (((int(__thiscall*)(void*, int, int, int, int, int, int, int))kCanPlace)(*(void**)kWorld, p, x, y, 0x2c, 1,
                                                                                    0, 0) == 0) {
                out = {x, y}, type = 0x2c;
                return true;
            }
        }
    return false;
}

struct BuildTest {
    int founder = -1, type = 0, x = 0, y = 0, ordered = 0;
    std::vector<short> helpers;
} g_bt;

void BuildSiteTest(int t) {
    char* me = (char*)FirstCreature();
    if (t == 5) {
        if (!me) return;
        int p = PlayerOf(me), mx = F<short>(me, kX), my = F<short>(me, kY);
        nav::Pt spot;
        bool open = FindOpenArea(mx, my, 320, 320, g_area);
        g_log("pathing: test 12 player %d creature at %d,%d, open area %d at %d,%d", p, mx, my, open, g_area.x, g_area.y);
        int type = 0;
        bool found = open && BuildSpot(p, g_area.x + 160, g_area.y, spot, type);
        if (found) g_bt.type = type, g_bt.x = spot.x, g_bt.y = spot.y;
        if (!found) {
            g_log("pathing: test 12 FAIL (no valid building spot)");
            g_testDone = true;
            return;
        }
        static const int kOn[3][2] = {{-20, -20}, {20, -20}, {0, 22}};  // on the footprint, 40 px apart
        for (const auto& o : kOn) g_bt.helpers.push_back((short)SpawnUnit(p, 0, g_bt.x + o[0], g_bt.y + o[1]));
        g_bt.founder = SpawnUnit(p, 0, g_bt.x - 260, g_bt.y);

        g_log("pathing: test 12 type %#x at %d,%d, founder %d, %d Shamans on the site", g_bt.type, g_bt.x, g_bt.y,
              g_bt.founder, (int)g_bt.helpers.size());
    }
    if (t == 80) {
        for (short id : g_bt.helpers)
            if (char* h = ThingById(id))
                g_log("pathing: test 12 Shaman %d stands at %d,%d (site %d,%d)", id, F<short>(h, kX), F<short>(h, kY), g_bt.x,
                      g_bt.y);
        void* cmd = Cmd();
        ((void(__thiscall*)(void*, int, int, int, int))(*(void***)cmd)[0x5c / 4])(cmd, g_bt.founder, g_bt.type, g_bt.x,
                                                                                    g_bt.y);
        g_bt.ordered = t;
    }
    if (t <= 80) return;
    if (t == 84)  // after the order: another Shaman sent right onto the site (as a group's spot could)
        if (char* h = ThingById(g_bt.helpers[0])) MoveUnit(h, g_bt.x, g_bt.y);
    char* f = ThingById(g_bt.founder);
    if (!f) return;
    if (t % 50 == 0) {
        DumpUnit(12, g_bt.founder);
        for (short id : g_bt.helpers) DumpUnit(12, id);
        g_log("pathing: test 12   founder work type %#x site %d,%d target %d counter %d; %d sites", F<int>(f, kWorkType),
              F<short>(f, kSiteX), F<short>(f, kSiteY), F<short>(f, kWorkTarget), F<short>(f, 0x180), (int)g_siteList.size());
    }
    short made = F<short>(f, kWorkTarget);
    int failures = F<uint8_t>(f, kFailures);
    if ((made == (short)0xc01 || made == 0) && failures == 0 && t < g_bt.ordered + 900) return;
    bool pass = made != (short)0xc01 && made != 0 && failures == 0;
    g_log("pathing: test 12 %s (build site kept clear: building %d after %d turns, %d failed attempts, %d units walked "
          "off the site)",
          pass ? "PASS" : "FAIL", made, t - g_bt.ordered, failures, g_siteClears);
    g_testDone = true;
}

// Test 13: two Builds of the same type side by side: one Shaman alone, one with
// three idle Shamans waiting beside its site. The helped one must stand up first,
// in at most half the alone one's start time.
struct StartRace {
    int founder[2] = {-1, -1}, made[2] = {-1, -1}, x[2], y[2], type = 0, ordered = 0, helpers[3] = {-1, -1, -1};
} g_sr;

void StartTogetherTest(int t) {
    char* me = (char*)FirstCreature();
    if (t == 5) {
        if (!me) return;
        int p = PlayerOf(me), mx = F<short>(me, kX), my = F<short>(me, kY);
        nav::Pt s0, s1;
        int t0 = 0, t1 = 0;
        bool found = FindOpenArea(mx, my, 480, 360, g_area) && BuildSpot(p, g_area.x - 260, g_area.y, s0, t0) &&
                     BuildSpot(p, g_area.x + 260, g_area.y, s1, t1, s0) && t0 == t1;
        if (found) g_sr.type = t0, g_sr.x[0] = s0.x, g_sr.x[1] = s1.x, g_sr.y[0] = s0.y, g_sr.y[1] = s1.y;
        if (!found) {
            g_log("pathing: test 13 FAIL (no two valid building spots)");
            g_testDone = true;
            return;
        }
        int half = ((short*)kFootprint)[g_sr.type] / 2;
        for (int k = 0; k < 2; ++k) g_sr.founder[k] = SpawnUnit(p, 0, g_sr.x[k], g_sr.y[k] - half - 120);
        for (int i = 0; i < 3; ++i) g_sr.helpers[i] = SpawnUnit(p, 0, g_sr.x[1] - 48 + i * 48, g_sr.y[1] + half + 140);
        g_log("pathing: test 13 type %#x at %d,%d (alone) and %d,%d (3 helpers)", g_sr.type, g_sr.x[0], g_sr.y[0],
              g_sr.x[1], g_sr.y[1]);
    }
    if (t == 20) {
        void* cmd = Cmd();
        for (int k = 0; k < 2; ++k)
            ((void(__thiscall*)(void*, int, int, int, int))(*(void***)cmd)[0x5c / 4])(cmd, g_sr.founder[k], g_sr.type,
                                                                                        g_sr.x[k], g_sr.y[k]);
        for (int h : g_sr.helpers)  // as a group build does: the same Build for every builder
            ((void(__thiscall*)(void*, int, int, int, int))(*(void***)cmd)[0x5c / 4])(cmd, h, g_sr.type, g_sr.x[1],
                                                                                        g_sr.y[1]);
        g_sr.ordered = t;
    }
    if (t <= 20) return;
    if (t % 4 == 0) {  // the work pose: animation fields of the helped founder and a waiting helper
        char* f = ThingById(g_sr.founder[1]);
        char* h = ThingById(g_sr.helpers[0]);
        for (char* u : {f, h})
            if (u)
                g_log("pathing: test 13 pose id %d activity %#x anim %d frame %d flag74 %d facing %d at %d,%d counter %d",
                      F<unsigned short>(u, kId), F<int>(u, kActivity), F<int>(u, 0x88), F<uint8_t>(u, 0x8c),
                      F<short>(u, 0x74), F<uint8_t>(u, 0x48), F<short>(u, kX), F<short>(u, kY), F<short>(u, 0x180));
    }
    for (int k = 0; k < 2; ++k) {
        char* f = ThingById(g_sr.founder[k]);
        short made = f ? F<short>(f, kWorkTarget) : (short)0xc01;
        if (g_sr.made[k] < 0 && made != (short)0xc01 && made != 0) g_sr.made[k] = t - g_sr.ordered;
    }
    if ((g_sr.made[0] < 0 || g_sr.made[1] < 0 || t < g_sr.ordered + std::max(g_sr.made[0], g_sr.made[1]) + 10) &&
        t < g_sr.ordered + 900)
        return;
    // Both walked the same distance; compare the time from arrival (walk ~ the same for both).
    int repairing = 0;
    for (int h : g_sr.helpers)
        if (char* c = ThingById(h)) repairing += F<int>(c, kActivity) == 0x39 || F<short>(c, kWorkTarget) == g_sr.made[1];
    char* f1 = ThingById(g_sr.founder[1]);
    int failures = f1 ? F<uint8_t>(f1, kFailures) : -1;
    bool pass = g_sr.made[0] > 0 && g_sr.made[1] > 0 && g_sr.made[1] < g_sr.made[0] && failures == 0 && g_joins == 3;
    g_log("pathing: test 13 %s (building together: alone %d turns, with 3 more builders %d turns; %d pooled work ticks, "
          "%d builders joined, %d failed attempts)",
          pass ? "PASS" : "FAIL", g_sr.made[0], g_sr.made[1], g_helpTicks, g_joins, failures);
    g_testDone = true;
}

void TestTurn(int t) {
    switch (g_dbg.test) {
    case 1:  // baseline: nothing ordered; the hash lines compare modes and builds
        if (t == 400) {
            g_log("pathing: test 1 PASS (idle baseline)");
            g_testDone = true;
        }
        break;
    case 2:  // the oracle: the grid against the game's own collision tests
        if (t == 20) SpawnTestBuildings();
        if (t == 60) {
            Oracle();
            g_testDone = true;
        }
        break;
    case 3: {  // T1: a C of rocks opening west; the unit in its pocket, the goal behind its back
        char* me = (char*)FirstCreature();
        if (t == 5) {
            if (!me || !FindOpenArea(F<short>(me, kX), F<short>(me, kY), 480, 320, g_area)) {
                g_log("pathing: test 3 FAIL (no open area)");
                g_testDone = true;
                break;
            }
            int p = PlayerOf(me), ox = g_area.x, oy = g_area.y;
            for (int y = oy - 192; y <= oy + 192; y += 40) SpawnObject(p, 0x45, ox + 96, y);  // the back
            for (int x = ox - 128; x < ox + 96; x += 40) {                                   // the arms
                SpawnObject(p, 0x45, x, oy - 192);
                SpawnObject(p, 0x45, x, oy + 192);
            }
            g_t1.id = SpawnUnit(p, 1, ox - 64, oy);
            g_t1.goal = {ox + 320, oy};
            g_log("pathing: test 3 C at %d,%d, unit %d", ox, oy, g_t1.id);
        }
        if (t == 20) {
            OrderMove(g_t1.id, g_t1.goal.x, g_t1.goal.y);
            g_t1.ordered = t, g_t1.limit = 600;
        }
        if (t > 20) Track(3, g_t1, t, "out of a C-shaped pocket");
        if (g_t1.done) g_testDone = true;
        break;
    }
    case 4:
    case 5:
        WallTest(g_dbg.test, t);
        break;
    case 7:
        UnreachableTest(t);
        break;
    case 14: {  // a Shaman idle for 13 s (past the 10 s give-up), then sent 600 px: it must get there
        char* me = (char*)FirstCreature();
        if (t == 5) {
            if (!me || !FindOpenArea(F<short>(me, kX), F<short>(me, kY), 400, 240, g_area)) {
                g_log("pathing: test 14 FAIL (no open area)");
                g_testDone = true;
                break;
            }
            g_t1.id = SpawnUnit(PlayerOf(me), 0, g_area.x - 300, g_area.y);
            g_t1.goal = {g_area.x + 300, g_area.y};
        }
        if (t == 30) OrderMove(g_t1.id, g_area.x - 260, g_area.y);  // a short first move, then idle
        if (t == 300) {
            OrderMove(g_t1.id, g_t1.goal.x, g_t1.goal.y);
            g_t1.ordered = t, g_t1.limit = 400;
        }
        if (t > 300) Track(14, g_t1, t, "a long move after standing idle");
        if (g_t1.done) g_testDone = true;
        break;
    }
    case 8:
    case 9:
    case 10:
        TrafficTest(g_dbg.test, t);
        break;
    case 11:
        SpreadTest(t);
        break;
    case 12:
        BuildSiteTest(t);
        break;
    case 13:
        StartTogetherTest(t);
        break;
    case 6: {  // speed: the same open-ground move must take as many turns as in Original
        char* me = (char*)FirstCreature();
        if (t == 5) {
            if (!me || !FindOpenArea(F<short>(me, kX), F<short>(me, kY), 400, 240, g_area)) {
                g_log("pathing: test 6 FAIL (no open area)");
                g_testDone = true;
                break;
            }
            g_t1.id = SpawnUnit(PlayerOf(me), 1, g_area.x - 300, g_area.y - 100);
            g_t1.goal = {g_area.x + 300, g_area.y + 100};
        }
        if (t == 20) {
            OrderMove(g_t1.id, g_t1.goal.x, g_t1.goal.y);
            g_t1.ordered = t, g_t1.limit = 300;
        }
        if (t > 20) Track(6, g_t1, t, "straight across open ground");
        if (g_t1.done) g_testDone = true;
        break;
    }
    default:
        if (t == 1) {
            g_log("pathing: test %d FAIL (no such test)", g_dbg.test);
            g_testDone = true;
        }
    }
}

// --- multiplayer: everyone runs the host's choice ------------------------------
// Every machine starts a multiplayer mission in Original and sends one hello in
// the lockstep command stream: SetStance (Cmd+0xa0, opcode 0x48) for thing 0xffff,
// which its executor 0x462e20 ignores (no such thing), so a copy without
// WarBreeds HD ignores it too. The stance word carries 0x5700 | version << 4 |
// slot << 1 | preference. Commands run in the same order on every machine, so
// once every human slot has said hello, all switch together at the next turn to
// the first human slot's (the host's) preference; a missing or different hello
// by turn 250 keeps the game Original.

constexpr uintptr_t kVSetStance = 0xa0, kVLocalSlot = 0xd0;  // Cmd virtuals
constexpr uintptr_t kStanceExecCall = 0x45bcb7, kStanceExec = 0x462e20;  // in CommandList::Execute (MP)
constexpr uintptr_t kSlots = 4, kSlotSize = 0x28, kSlotUsed = 0, kSlotHuman = 0x24;  // cmd+4: tPlayerSlot[8]
constexpr int kHelloVersion = 1, kHelloTimeout = 250, kHelloId = 0xffff;

struct MpState {
    bool active = false;    // a multiplayer mission waiting for hellos
    int hello[8];           // per slot: -1 none, else the hello word
    int switchTurn = -1;    // the turn the agreed mode takes effect
    bool agreed = false;
} g_mp;

bool SlotHuman(int i) {
    char* slot = (char*)Cmd() + kSlots + i * kSlotSize;
    return slot[kSlotUsed] && *(short*)(slot + kSlotHuman);
}

void SendHello() {
    void* cmd = Cmd();
    int slot = ((int(__thiscall*)(void*))(*(void***)cmd)[kVLocalSlot / 4])(cmd) & 7;
    int word = 0x5700 | kHelloVersion << 4 | slot << 1 | (g_pref ? 1 : 0);
    ((void(__thiscall*)(void*, int, int))(*(void***)cmd)[kVSetStance / 4])(cmd, kHelloId, word);
    g_log("pathing: multiplayer hello sent (slot %d, %s)", slot, g_pref ? "improved" : "original");
}

void __stdcall HookStanceExec(int id, int stance) {
    if ((id & 0xffff) == kHelloId && (stance & 0xff00) == 0x5700) {
        int slot = stance >> 1 & 7;
        if (g_mp.active && !g_mp.agreed) {
            g_mp.hello[slot] = stance & 0xff;
            g_log("pathing: hello from slot %d: version %d, %s", slot, stance >> 4 & 0xf,
                  stance & 1 ? "improved" : "original");
        }
        return;
    }
    ((void(__stdcall*)(int, int))kStanceExec)(id, stance);
}

void SetMode(bool improved);

// Once per multiplayer turn: are all hellos in?
void MultiplayerTurn() {
    if (!g_mp.active) return;
    if (g_mp.agreed) {
        if (g_turn == g_mp.switchTurn) {
            g_mp.active = false;
            SetMode(true);
        }
        return;
    }
    int host = -1;
    bool all = true, same = true;
    for (int i = 0; i < 8; ++i) {
        if (!SlotHuman(i)) continue;
        if (host < 0) host = i;
        if (g_mp.hello[i] < 0) all = false;
        else if ((g_mp.hello[i] >> 4 & 0xf) != kHelloVersion) same = false;
    }
    if (all && same && host >= 0) {
        g_mp.agreed = true;
        bool improved = g_mp.hello[host] & 1;
        g_log("pathing: every player has WarBreeds HD; mode %s (host slot %d) from turn %d", improved ? "improved" : "original",
              host, g_turn + 1);
        if (improved) g_mp.switchTurn = g_turn + 1;
        else g_mp.active = false;
    } else if (!same || g_turn > kHelloTimeout) {
        g_mp.active = false;
        g_log("pathing: mode original (%s)", !same ? "a player has another WarBreeds HD version" :
                                                    "a player has no WarBreeds HD");
    }
}

void SetMode(bool improved) {
    g_improved = improved;
    SetRectFix(g_improved);
    if (g_improved) {
        nav::ClearDynamic();
        g_staticHash = 0;
        BuildStatic();
        g_log("pathing: mode improved from turn %d", g_turn);
    }
}

void OnMissionStart(int turn) {
    g_improved = g_pref && !Multiplayer();
    g_mp = MpState();
    for (int& h : g_mp.hello) h = -1;
    g_missionTurn0 = turn;
    nav::ClearDynamic();
    for (UnitRoute& u : g_routes) u.valid = false, u.thing = nullptr;
    std::fill(g_deferredAt.begin(), g_deferredAt.end(), -2);
    std::fill(g_askedAt.begin(), g_askedAt.end(), -100);
    std::fill(g_blockedBy.begin(), g_blockedBy.end(), -1);
    std::fill(g_askedOf.begin(), g_askedOf.end(), -1);
    std::fill(g_asks2.begin(), g_asks2.end(), 0);
    std::fill(g_blockedAt.begin(), g_blockedAt.end(), -100);
    std::fill(g_clearedAt.begin(), g_clearedAt.end(), -100);
    g_siteClears = g_helpTicks = g_joins = 0;
    g_mismatches = g_giveUps = g_reroutes = g_asks = g_spreads = 0;
    g_staticHash = 0;
    SetRectFix(g_improved);
    if (g_improved || g_dbg.test) BuildStatic();
    g_log("pathing: mission start at turn %d, mode %s%s", turn, g_improved ? "improved" : "original",
          Multiplayer() ? " (multiplayer: until every player's choice is in)" : "");
    if (Multiplayer()) {
        g_mp.active = true;
        SendHello();
    }
    if (g_dbg.test) {
        *(uint8_t*)kRandIndex = 0;
        *(uint16_t*)kRandCount = 0;
        ((void(__cdecl*)(unsigned))kSrand)(1);
    }
    if (!g_tablesLogged) {
        g_tablesLogged = true;
        LogTables();
    }
}

// --- hooks ------------------------------------------------------------------

// Start of every simulation turn (inside GameTurn, after the turn counter moved).
void __fastcall HookDeferred(void* cmd, void*) {
    int turn = F<int>(cmd, kTurn);
    if (g_missionStart || turn != g_lastTurn + 1) {
        g_missionStart = false;
        OnMissionStart(turn);
    }
    g_lastTurn = turn;
    if (g_dbg.hash && (turn - g_missionTurn0) % 50 == 0)
        g_log("pathing: hash t=%d %08x", turn - g_missionTurn0, StateHash(turn - g_missionTurn0));
    g_turn = turn - g_missionTurn0;
    g_budget = kTurnBudget;
    MultiplayerTurn();
    if (g_improved || g_dbg.test) ScanWorld();
    if (g_improved) UpdateSites();
    if (g_dbg.test && !g_testDone) TestTurn(turn - g_missionTurn0);
    ((void(__thiscall*)(void*))kProcessDeferred)(cmd);
}

void __fastcall HookClockReset(void* cmd, void*) {
    g_missionStart = true;
    ((void(__thiscall*)(void*))kClockReset)(cmd);
}

void __fastcall HookClockSet(void* cmd, void*, int turn) {
    g_missionStart = true;
    ((void(__thiscall*)(void*, int))kClockSet)(cmd, turn);
}

void __fastcall HookPlan(void* creature, void*) {
    if (g_improved && nav::Ready() && !IsFlyer(F<int>(creature, kType))) {
        UnitRoute& u = g_routes[F<unsigned short>(creature, kId)];
        bool mine = PlanImproved((char*)creature);
        if (u.thing == creature) u.stock = !mine;
        if (mine) return;
    }
    ((void(__thiscall*)(void*))kPlanNextLeg)(creature);
}

bool PatchSlot(uintptr_t slot, uintptr_t expect, void* hook) {
    if (*(uintptr_t*)slot != expect) return false;
    DWORD old;
    if (!VirtualProtect((void*)slot, 4, PAGE_READWRITE, &old)) return false;
    *(void**)slot = hook;
    VirtualProtect((void*)slot, 4, old, &old);
    return true;
}

// Point a `call rel32` at `site` (which must call `expect`) to `hook`, a
// function with the same convention.
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

}  // namespace

void Install(void (*log)(const char*, ...), const Debug& d) {
    g_log = log;
    g_dbg = d;
    bool ok = PatchCall(kDeferredCall, kProcessDeferred, (void*)&HookDeferred) &&
              PatchCall(kClockSetCall, kClockSet, (void*)&HookClockSet) &&
              PatchCall(kThinkPlanCall, kPlanNextLeg, (void*)&HookPlan);
    for (uintptr_t site : kClockResetCalls) ok = ok && PatchCall(site, kClockReset, (void*)&HookClockReset);
    for (uintptr_t slot : kSlotOnBlocked) ok = ok && PatchSlot(slot, kOnBlocked, (void*)&HookOnBlocked);
    for (uintptr_t site : kExecuteMoveCalls) ok = ok && PatchCall(site, kExecuteMove, (void*)&HookExecuteMove);
    ok = ok && PatchCall(kStanceExecCall, kStanceExec, (void*)&HookStanceExec);
    log("pathing: %s", ok ? "installed" : "FAILED (unexpected code)");
}

void SetPreference(bool improved) {
    if (improved != g_pref && g_log) g_log("pathing: preference %s (from the next mission)", improved ? "improved" : "original");
    g_pref = improved;
}

bool Improved() { return g_improved; }
bool TestDone() { return g_testDone; }

}  // namespace pathing
