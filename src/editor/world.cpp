// Map editor, Stage 2: engine access for things, players and map settings.
// See world.h and re/editor-stage2.md.
#include "world.h"

#include <windows.h>
#include <string.h>

#include <algorithm>

#include "../game.h"
#include "../lang.h"
#include "mapfile.h"

namespace editor {
namespace world {
namespace {

// Globals.
constexpr uintptr_t kWorldPtr = 0x579278;    // AThingRegistry*
constexpr uintptr_t kPlayersPtr = 0x57927c;  // APlayerRegistry*: +4 APlayer*[8], +0x24 u8 count
constexpr uintptr_t kLocalPlayer = 0x579288; // APlayer*
constexpr uintptr_t kMapInfo = 0x567578;     // +2 supported, +4 recommended, +0xc char* name
constexpr uintptr_t kGrid = 0x55dd60;        // AGrid
constexpr uintptr_t kVtblLogicalC = 0x5030ec;  // Pt in 32 px tiles

// Gameplay tables (DATA\GAMEPLAY.DAT, re/units.md 2.2).
constexpr uintptr_t kFootprint = 0x54e370;    // short[74] px
constexpr uintptr_t kRadius = 0x54cf08;       // short[74] px
constexpr uintptr_t kDisplayName = 0x550178;  // char[74][20]
constexpr uintptr_t kGizmoName = 0x54fcc0;    // char[25][20]
constexpr uintptr_t kGizmoGroup = 0x54eaf0;   // short[25]: 0 arm, 1 head
constexpr uintptr_t kHasArmSlots = 0x54c590;  // short[44]
constexpr uintptr_t kHasHeadSlot = 0x54ce48;  // short[44]

// Functions.
constexpr uintptr_t kCreateAt = 0x446db0;     // AThingRegistry (player, type, x, y, skipChecks, activate, id) -> id
constexpr uintptr_t kPlaceCreature = 0x4472d0;  // AThingRegistry (player, type, x, y, 4 gizmos) -> id
constexpr uintptr_t kActivate = 0x4473a0;     // stdcall (AThing*): tell the owner (ret 4)
constexpr uintptr_t kRemoveThing = 0x4481e0;  // AThingRegistry (id, quiet)
constexpr uintptr_t kCanPlace = 0x4517e0;     // AThingRegistry (player, cx, cy, type, ignoreUnits, skipSite, a6)
constexpr uintptr_t kSelect = 0x44b5d0;       // AThingRegistry (AThing*)
constexpr uintptr_t kClearSelection = 0x44b680;  // AThingRegistry (short saveToPrevious)
constexpr uintptr_t kBuildStep = 0x40c8a0;    // ADevice short(short)
constexpr uintptr_t kSetGizmo = 0x405230;     // ACreatureBody (slot, gizmo)
constexpr uintptr_t kSetVeteran = 0x406e20;   // ACreatureBody (level)
constexpr uintptr_t kHasGizmoTech = 0x43a420;  // APlayer short(gizmo, short plus)
constexpr uintptr_t kTerrainTypeAt = 0x42ea40;  // AGrid uint(ILogicalC*)
constexpr uintptr_t kSetName = 0x432150;      // MapInfo (char*)
constexpr uintptr_t kCreatePlayer = 0x43b850;  // APlayerRegistry (clan, colour, index, isAI)
constexpr uintptr_t kMakeHuman = 0x43c710;    // APlayerRegistry (index)
constexpr uintptr_t kMakeComputer = 0x43c7d0;  // APlayerRegistry (index)
constexpr uintptr_t kPlayerDtor = 0x43a5e0;   // fastcall APlayer::Destroy
constexpr uintptr_t kAiDtor = 0x4b5d20;       // thiscall AIComputerPlayer::Destroy
constexpr uintptr_t kFree = 0x4eb770;         // cdecl operator delete

// Thing fields (re/units.md 1.6).
constexpr uintptr_t kX = 0x0a, kY = 0x0c, kCX = 0x14, kCY = 0x16, kType = 0x18, kId = 0x1c, kOwner = 0x20,
                    kHp = 0x24, kGizmos = 0x54, kVet = 0x7c, kComplete = 0x2c, kGrowth = 0x28, kRotation = 0x2a;
constexpr uintptr_t kVMaxHp = 0x50, kVSetHp = 0x5c;  // thing vtable
// Player fields (re/players-ai.md 1.3, re/wbm-format.md 3.5).
constexpr uintptr_t kPIndex = 0x4, kPAvail = 0x1c, kPExplored = 0x2426, kPFog = 0x484c, kPColour = 0x484e,
                    kPCgk = 0x4850, kPRace = 0x4878, kPAlive = 0x48f6, kPStartX = 0x4a1c, kPStartY = 0x4a1e,
                    kPType = 0x4a44;
constexpr uintptr_t kVIsHuman = 0x7c;  // player vtable
// Player registry fields.
constexpr uintptr_t kRCount = 0x24, kRReveal = 0x2a, kRFogOn = 0x2c;

int (*g_corner)(int, int);

template <typename T>
T& F(uintptr_t p, uintptr_t off) {
    return *(T*)(p + off);
}

uintptr_t World() { return *(uintptr_t*)kWorldPtr; }
uintptr_t Players() { return *(uintptr_t*)kPlayersPtr; }

int Footprint(int type) { return type >= 0 && type < 74 ? ((short*)kFootprint)[type] : 32; }
int Radius(int type) { return type >= 0 && type < 74 ? ((short*)kRadius)[type] : 16; }

unsigned short CreateAt(int owner, int type, int x, int y, int skip, int activate) {
    return ((unsigned short(__thiscall*)(uintptr_t, int, int, int, int, int, int, int))kCreateAt)(
        World(), owner, type, x, y, skip, activate, kNoThing);
}

void SetHp(uintptr_t t, int hp) {
    uintptr_t vt = *(uintptr_t*)t;
    ((void(__thiscall*)(uintptr_t, int))(*(uintptr_t*)(vt + kVSetHp)))(t, (short)hp);
}

// Gizmos, rank and hit points of a creature that was just created.
void ApplyCreature(uintptr_t t, const ThingRec& r, bool gizmos) {
    if (gizmos)
        for (int s = 0; s < 4; ++s)
            if (r.gizmo[s] != kNoGizmo && r.gizmo[s] != 0x15)  // 0x15: the Screamer's second slot
                ((void(__thiscall*)(uintptr_t, int, int))kSetGizmo)(t, s, r.gizmo[s]);
    if (r.vet > 0) ((void(__thiscall*)(uintptr_t, int))kSetVeteran)(t, r.vet);
    if (r.hp > 0) SetHp(t, r.hp);
}

bool InMap(int x0, int y0, int x1, int y1) {
    return x0 >= 0 && y0 >= 0 && x1 <= TilesW() * 32 && y1 <= TilesH() * 32;
}

// Trees and rocks go on their own subtype, ruins on open ground (the 1998
// editor's manual; re/editor-original.md 5.2): every tile under the footprint.
bool ObstacleSiteOk(int type, int x, int y, std::string& why) {
    int fp = std::max(32, Footprint(type));
    int need = -1;  // corner terrain (TRST)
    const char* where = "";
    switch (type) {
    case 0x44: case 0x45: need = 1; where = "crushed crystal"; break;
    case 0x46: case 0x47: need = 7; where = "roots"; break;
    case 0x48: case 0x49: need = 4; where = "hardened tar"; break;
    default: break;
    }
    for (int ty = y >> 5; ty <= (y + fp - 1) >> 5; ++ty)
        for (int tx = x >> 5; tx <= (x + fp - 1) >> 5; ++tx) {
            if (type == kRuins) {
                if (TerrainType(tx, ty) != 0) {
                    why = lang::tr("Ruins go on open ground");
                    return false;
                }
            } else if (need >= 0 && g_corner) {
                int c = g_corner(tx, ty);
                if (c >= 0 && c != need) {
                    why = lang::trf("{1} only goes on {2}", {lang::Name(Name(type)), lang::tr(where)});
                    return false;
                }
            }
        }
    return true;
}

const char* const kRaces[] = {"Tanu", "Sen-Soth", "Kelika", "Magha"};

}  // namespace

// --- things ---------------------------------------------------------------------------

uintptr_t Thing(int id) {
    uintptr_t w = World();
    if (!w || id < 0 || id >= F<int>(w, 0x14)) return 0;
    return (*(uintptr_t**)(w + 0x10))[id];
}

int ThingCount() { return World() ? F<int>(World(), 0x14) : 0; }
int TypeOf(uintptr_t t) { return F<int>(t, kType); }
int OwnerOf(uintptr_t t) {
    uintptr_t p = F<uintptr_t>(t, kOwner);
    return p ? F<uint16_t>(p, kPIndex) : kNeutral;
}
int CentreX(uintptr_t t) { return F<short>(t, kCX); }
int CentreY(uintptr_t t) { return F<short>(t, kCY); }

bool Capture(int id, ThingRec& r) {
    uintptr_t t = Thing(id);
    if (!t) return false;
    r = ThingRec();
    r.type = TypeOf(t);
    r.owner = OwnerOf(t);
    r.x = F<short>(t, kX);
    r.y = F<short>(t, kY);
    r.hp = F<short>(t, kHp);
    if (IsCreature(r.type)) {
        r.vet = F<int>(t, kVet);
        for (int s = 0; s < 4; ++s) r.gizmo[s] = F<int>(t, kGizmos + 4 * s);
    } else if (IsCrop(r.type)) {
        r.extra = F<int>(t, kGrowth);
    } else if (IsObstacle(r.type)) {
        r.extra = F<short>(t, kRotation);
    }
    return true;
}

int Restore(const ThingRec& r) {
    if (!World() || r.type < 0) return kNoThing;
    if ((IsCreature(r.type) || IsDevice(r.type)) && !Player(r.owner)) return kNoThing;
    unsigned short id = CreateAt(r.owner, r.type, r.x, r.y, 1, IsCreature(r.type) ? 0 : 1);
    uintptr_t t = id == kNoThing ? 0 : Thing(id);
    if (!t) return kNoThing;
    if (IsCreature(r.type)) {
        ApplyCreature(t, r, true);
        ((void(__stdcall*)(uintptr_t))kActivate)(t);
    } else if (IsDevice(r.type)) {
        Complete(id);
        if (r.hp > 0) SetHp(t, r.hp);
    } else if (IsCrop(r.type)) {
        if (r.extra > 0) F<int>(t, kGrowth) = r.extra;
    } else if (IsObstacle(r.type)) {
        F<short>(t, kRotation) = (short)r.extra;
    }
    return id;
}

void PlacementRect(int type, int wx, int wy, int& x0, int& y0, int& x1, int& y1) {
    int fp = Footprint(type);
    x0 = wx - fp / 2;
    y0 = wy - fp / 2;
    if (IsDevice(type) || IsObstacle(type)) {  // on the 32 px tile grid, as a builder places them
        x0 = (x0 + 16) & ~31;
        y0 = (y0 + 16) & ~31;
    } else if (IsCrop(type)) {  // CreateAt puts crops on their tile
        x0 = (x0 + 16) & ~31;
        y0 = (y0 + 16) & ~31;
    }
    x1 = x0 + std::max(fp, 8);
    y1 = y0 + std::max(fp, 8);
}

int Put(int type, int owner, int wx, int wy, const ThingRec* attrs, std::string& why) {
    why.clear();
    if (!World()) return kNoThing;
    int fp = Footprint(type);
    int x, y, x1, y1;
    PlacementRect(type, wx, wy, x, y, x1, y1);
    if (IsCrop(type)) x = wx - fp / 2, y = wy - fp / 2;  // CreateAt snaps them itself
    if (!InMap(x, y, x + fp, y + fp)) {
        why = lang::tr("Too close to the edge of the map");
        return kNoThing;
    }
    unsigned short id = kNoThing;
    if (IsCrop(type)) {
        id = CreateAt(kNeutral, type, x, y, 0, 1);
        if (id == kNoThing)
            why = lang::tr(TerrainType((x + 16) >> 5, (y + 16) >> 5) != 2 ? "Crops only grow on cropland"
                                                                            : "Something is already there");
    } else if (IsObstacle(type)) {
        if (!ObstacleSiteOk(type, x, y, why)) return kNoThing;
        id = CreateAt(kNeutral, type, x, y, 0, 1);
        if (id == kNoThing) why = lang::tr("Something is in the way");
    } else if (IsDevice(type)) {
        if (!Player(owner)) {
            why = lang::trf("Player {1} doesn't exist (Players tab: set a type)", {std::to_string(owner + 1)});
            return kNoThing;
        }
        // CanPlace: 0 fits, 1 blocked, 2 fits but has no power (or a Refinery without
        // crops near): the editor places it anyway, as the 1998 editor did.
        int fit = ((int(__thiscall*)(uintptr_t, int, int, int, int, int, int, int))kCanPlace)(
            World(), owner, x + fp / 2, y + fp / 2, type, 0, 0, 0);
        if (fit == 1) {
            why = lang::trf("No room for a {1} there", {lang::Name(Name(type))});
            return kNoThing;
        }
        id = CreateAt(owner, type, x, y, 1, 1);
        if (id == kNoThing) {
            why = lang::tr("Something is in the way");
        } else {
            Complete(id);
            if (attrs && attrs->hp > 0) SetHp(Thing(id), attrs->hp);
            if (fit == 2) why = lang::tr(type == kRefinery ? "no crops in reach" : "no power here");  // a note, not a refusal
        }
    } else if (IsCreature(type)) {
        if (!Player(owner)) {
            why = lang::trf("Player {1} doesn't exist (Players tab: set a type)", {std::to_string(owner + 1)});
            return kNoThing;
        }
        int g[4] = {kNoGizmo, kNoGizmo, kNoGizmo, kNoGizmo};
        if (attrs)
            for (int s = 0; s < 4; ++s) g[s] = attrs->gizmo[s];
        id = ((unsigned short(__thiscall*)(uintptr_t, int, int, int, int, int, int, int, int))kPlaceCreature)(
            World(), owner, type, x, y, g[0], g[1], g[2], g[3]);
        if (id == kNoThing)
            why = lang::trf("No room for a {1} there", {lang::Name(Name(type))});
        else if (attrs)
            ApplyCreature(Thing(id), *attrs, false);
    } else {
        why = lang::tr("Can't place that");
    }
    return id == kNoThing ? (int)kNoThing : id;
}

void Remove(int id) {
    if (!Thing(id)) return;
    ((void(__thiscall*)(uintptr_t, int, int))kRemoveThing)(World(), id, 1);
}

int PickAt(int wx, int wy) {
    int best = kNoThing;
    long bestD = 1L << 30;
    for (int id = 0; id < ThingCount(); ++id) {
        uintptr_t t = Thing(id);
        if (!t || TypeOf(t) == 0x42) continue;  // corpses
        int type = TypeOf(t);
        int r = std::max({Radius(type), Footprint(type) / 2, 16}) + 4;
        long dx = CentreX(t) - wx, dy = CentreY(t) - wy, d = dx * dx + dy * dy;
        if (d <= (long)r * r && d < bestD) {
            bestD = d;
            best = id;
        }
    }
    return best;
}

int StaleGridEntries() {
    uintptr_t w = World();
    if (!w) return 0;
    const uint16_t* grid = (const uint16_t*)(w + 0x10c);
    int n = 0;
    for (int i = 0; i < 2 * 192 * 192; ++i)
        if (grid[i] != kNoThing && !Thing(grid[i])) ++n;
    return n;
}

void Select(int id) {
    if (!World()) return;
    ((void(__thiscall*)(uintptr_t, int))kClearSelection)(World(), 0);
    if (uintptr_t t = Thing(id)) ((void(__thiscall*)(uintptr_t, uintptr_t))kSelect)(World(), t);
}

int MaxHp(int id) {
    uintptr_t t = Thing(id);
    if (!t) return 0;
    uintptr_t vt = *(uintptr_t*)t;
    return ((short(__thiscall*)(uintptr_t))(*(uintptr_t*)(vt + kVMaxHp)))(t);
}

void SetHitPoints(int id, int hp) {
    if (uintptr_t t = Thing(id)) SetHp(t, hp);
}

std::string Name(int type) {
    if (type < 0 || type >= 74) return "?";
    const char* p = (const char*)kDisplayName + 20 * type;
    std::string s(p, strnlen(p, 20));
    return s.empty() ? "Type " + std::to_string(type) : s;
}

std::string GizmoName(int g) {
    if (g == kNoGizmo) return "none";
    if (g == 0x15) return "(Screamer)";
    if (g < 0 || g >= 25) return "?";
    const char* p = (const char*)kGizmoName + 20 * g;
    return std::string(p, strnlen(p, 20));
}

bool SlotUsed(int type, int slot) {
    if (!IsCreature(type) || slot < 0 || slot > 3) return false;
    switch (type) {  // AGeneLabInterface::ClearUnusedSlots 0x42c3e0
    case 0: case 4: case 9: case 0x12: case 0x13: case 0x1b: case 0x1d:
        return false;
    case 1: case 0xb: case 0xd: case 0x1c: case 0x1e:
        return slot == 0 || slot == 2;
    case 2: case 3: case 0x14: case 0x15:
        return slot != 3;
    case 10: case 0x1f:
        return slot == 1;
    case 0xc: case 0x16:
        return true;
    default:  // creatures the Gene Lab doesn't breed: the gameplay tables
        if (slot == 3) return false;
        return slot == 1 ? ((short*)kHasHeadSlot)[type] == 1 : ((short*)kHasArmSlots)[type] == 1;
    }
}

int SlotGroup(int type, int slot) {
    // Slot 1 is the head; the Leviathan carries arm gizmos in all four (the cheat
    // placement's choice, 0x47dcb0(4, 0)).
    return slot == 1 && type != 0x16 ? 1 : 0;
}

std::vector<int> GizmoChoices(int type, int slot, int owner) {
    std::vector<int> out{kNoGizmo};
    if (!SlotUsed(type, slot)) return out;
    uintptr_t p = Player(owner);
    for (int g = 0; g < 23; ++g) {
        if (g == 0x15 || ((short*)kGizmoGroup)[g] != SlotGroup(type, slot)) continue;
        if (g == 0x13 && (slot == 1 || slot == 3 || !SlotUsed(type, slot ^ 2))) continue;  // the Screamer takes a pair
        if (p && ((short(__thiscall*)(uintptr_t, int, int))kHasGizmoTech)(p, g, 1) != 1) continue;
        out.push_back(g);
    }
    return out;
}

void Complete(int id) {
    uintptr_t t = Thing(id);
    if (!t || !IsDevice(TypeOf(t))) return;
    for (int i = 0; i < 20000 && F<short>(t, kComplete) == 0; ++i)
        ((short(__thiscall*)(uintptr_t, int))kBuildStep)(t, 0);
}

void SetCornerQuery(int (*corner)(int, int)) { g_corner = corner; }

// --- terrain ------------------------------------------------------------------------

int TilesW() { return game::at<uint8_t>(game::MapTilesW); }
int TilesH() { return game::at<uint8_t>(game::MapTilesH); }

int TerrainType(int tx, int ty) {
    if (tx < 0 || ty < 0 || tx >= TilesW() || ty >= TilesH()) return 1;
    struct {
        uintptr_t vtbl;
        short x, y;
    } pt{kVtblLogicalC, (short)tx, (short)ty};
    return (int)((unsigned(__thiscall*)(uintptr_t, void*))kTerrainTypeAt)(kGrid, &pt);
}

// --- players ----------------------------------------------------------------------------

const char* TypeName(int type) {
    static const char* const kNames[] = {"Computer", "Human", "Either", "None"};
    return type >= 0 && type < 4 ? kNames[type] : "?";
}

const char* RaceName(int race) { return race >= 0 && race < 4 ? kRaces[race] : "?"; }

uintptr_t Player(int i) {
    uintptr_t reg = Players();
    if (!reg || i < 0 || i > 7) return 0;
    return F<uintptr_t>(reg, 4 + 4 * i);
}

int LocalPlayerIndex() {
    uintptr_t me = *(uintptr_t*)kLocalPlayer;
    return me ? F<uint16_t>(me, kPIndex) : -1;
}

bool IsHuman(uintptr_t p) {
    uintptr_t vt = *(uintptr_t*)p;
    return (((uint8_t(__thiscall*)(uintptr_t))(*(uintptr_t*)(vt + kVIsHuman)))(p) & 0xff) == 1;
}

PlayerRec CapturePlayer(int i) {
    PlayerRec r;
    uintptr_t p = Player(i);
    if (!p) return r;
    int type = F<int>(p, kPType);
    bool human = IsHuman(p);
    // The file's type, unless it disagrees with the player's class (old maps, or
    // a value the 1998 editor never wrote).
    if (type < 0 || type > 2 || (type == kHuman) != human) type = human ? kHuman : kComputer;
    r.type = type;
    r.race = F<int>(p, kPRace);
    if (r.race < 0 || r.race > 3) r.race = 0;
    r.colour = F<uint8_t>(p, kPColour);
    for (int c = 0; c < 4; ++c) {
        r.cgk[c] = F<int>(p, kPCgk + 4 * c);
        r.avail[c] = F<uint16_t>(p, kPAvail + 2 * c);
    }
    r.startX = F<short>(p, kPStartX);
    r.startY = F<short>(p, kPStartY);
    return r;
}

void ApplyPlayer(int i, const PlayerRec& rec, int fogMode) {
    uintptr_t reg = Players();
    if (!reg || i < 0 || i > 7) return;
    uintptr_t p = Player(i);
    if (rec.type == kNone) {
        if (!p) return;
        // As APlayerRegistry::DeletePlayer 0x43abf0, without PlayerDefeated: that
        // would end the "game" when one player is left.
        bool alive = F<uint16_t>(p, kPAlive) == 1;
        if (IsHuman(p))
            ((void(__fastcall*)(uintptr_t))kPlayerDtor)(p);
        else
            ((void(__thiscall*)(uintptr_t))kAiDtor)(p);
        ((void(__cdecl*)(uintptr_t))kFree)(p);
        F<uintptr_t>(reg, 4 + 4 * i) = 0;
        if (alive && F<uint8_t>(reg, kRCount) > 0) --F<uint8_t>(reg, kRCount);
        return;
    }
    bool human = rec.type == kHuman;
    if (!p) {
        ((int(__thiscall*)(uintptr_t, int, int, int, int))kCreatePlayer)(reg, rec.race,
                                                                         rec.colour ? rec.colour : ColourOf(i), i,
                                                                         human ? 0 : 1);
        p = Player(i);
        if (!p) return;
        // The constructor leaves fog on and the map unexplored (mode 0).
        if (fogMode == kFogOff) F<uint16_t>(p, kPFog) = 0;
        if (fogMode != kFogAndShroud) memset((void*)(p + kPExplored), 0, 96 * 96);
    } else if (IsHuman(p) != human) {
        ((void(__thiscall*)(uintptr_t, int))(human ? kMakeHuman : kMakeComputer))(reg, i);
        p = Player(i);
        if (!p) return;
    }
    F<int>(p, kPType) = rec.type;
    F<int>(p, kPRace) = rec.race;
    for (int c = 0; c < 4; ++c) {
        F<int>(p, kPCgk + 4 * c) = rec.cgk[c];
        F<uint16_t>(p, kPAvail + 2 * c) = (uint16_t)rec.avail[c];
    }
    F<short>(p, kPStartX) = (short)rec.startX;
    F<short>(p, kPStartY) = (short)rec.startY;
}

PlayerRec DefaultPlayer(int i, int type, int supported) {
    static const int kRaceOrder[] = {0, 2, 1, 3};  // Tanu, Kelika, Sen-Soth, Magha, as New Map
    PlayerRec r;
    r.type = type;
    r.race = kRaceOrder[i % 4];
    r.colour = ColourOf(i);
    for (int c = 0; c < 4; ++c) {
        r.cgk[c] = 2;  // the 1998 editor's defaults (0x403c90)
        r.avail[c] = 1;
    }
    StartCell(i, std::max(supported, i + 1), TilesW() / 2, TilesH() / 2, r.startX, r.startY);
    return r;
}

int ColourOf(int i) { return 247 - 8 * i; }

// --- map ---------------------------------------------------------------------------------

const char* FogName(int mode) {
    static const char* const kNames[] = {"Fog and shroud", "Fog, no shroud", "No fog"};
    return mode >= 0 && mode < 3 ? kNames[mode] : "?";
}

std::string MapName() {
    const char* s = F<const char*>(kMapInfo, 0xc);
    return s && !IsBadStringPtrA(s, 256) ? std::string(s) : std::string();
}

void SetMapName(const std::string& name) { ((void(__thiscall*)(uintptr_t, const char*))kSetName)(kMapInfo, name.c_str()); }
int Supported() { return F<short>(kMapInfo, 2); }
int Recommended() { return F<short>(kMapInfo, 4); }
void SetSupported(int n) { F<short>(kMapInfo, 2) = (short)n; }
void SetRecommended(int n) { F<short>(kMapInfo, 4) = (short)n; }

int FogModeOf(int localFog) {
    int me = LocalPlayerIndex();
    uintptr_t p = Player(me >= 0 ? me : 0);
    if (!p) return kFogAndShroud;
    if ((me >= 0 ? localFog : F<uint16_t>(p, kPFog)) == 0) return kFogOff;
    int w = TilesW() / 2, h = TilesH() / 2;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (F<uint8_t>(p, kPExplored + x + y * 96)) return kFogAndShroud;
    return kFogOnly;
}

void SetFogMode(int mode, short& localFog) {
    // As APlayerRegistry::SetFogMode 0x43ce00, both ways: fog on or off, and the
    // map explored (no shroud) or not.
    int me = LocalPlayerIndex();
    for (int i = 0; i < 8; ++i) {
        uintptr_t p = Player(i);
        if (!p) continue;
        short fog = mode == kFogOff ? 0 : 1;
        if (i == me)
            localFog = fog;  // the editor keeps the local player's fog off until it saves
        else
            F<uint16_t>(p, kPFog) = fog;
        memset((void*)(p + kPExplored), mode == kFogAndShroud ? 1 : 0, 96 * 96);
    }
    if (uintptr_t reg = Players()) {
        F<uint16_t>(reg, kRReveal) = mode == kFogAndShroud ? 1 : 0;
        F<uint16_t>(reg, kRFogOn) = mode == kFogOff ? 0 : 1;
    }
}

}  // namespace world
}  // namespace editor
