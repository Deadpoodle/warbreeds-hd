// Map editor, Stage 2: the engine's things, players and map settings, as the
// editor reads and changes them (re/editor-stage2.md).
//
// Things (units, devices, crops, trees, rocks, ruins) live in the thing
// registry ([0x579278], AThingRegistry) and are created, placed and removed
// with the game's own code: AThingRegistry::CreateAt 0x446db0 (the factory
// the map loader, the Gene Lab and the cheat console use), PlaceCreature
// 0x4472d0, CanPlace 0x4517e0, RemoveThing 0x4481e0. Players live in the
// player registry ([0x57927c]); the map header in MapInfo (0x567578). The
// game's saver (SGame::SaveGame) writes all of it.
//
// Engine ids are not stable across undo (a thing that is removed and put back
// gets a new id), so the editor's records describe a thing by value
// (ThingRec) and refer to it through a handle (editor.cpp).
#pragma once
#include <stdint.h>

#include <string>
#include <vector>

namespace editor {
namespace world {

constexpr int kNoThing = 0xc01;  // "no thing" id
constexpr int kNeutral = 9;      // owner index of nature (crops, trees, rocks)
constexpr int kNoGizmo = 100;

// Thing type ranges (re/units.md 1.1).
inline bool IsCreature(int t) { return t >= 0 && t < 0x2c; }
inline bool IsDevice(int t) { return t >= 0x2c && t < 0x3f; }
inline bool IsCrop(int t) { return t >= 0x3f && t <= 0x41; }
inline bool IsObstacle(int t) { return t >= 0x43 && t <= 0x49; }
inline bool IsShaman(int t) { return t == 0 || t == 9 || t == 0x12 || t == 0x1b; }
inline bool IsFlyer(int t) { return t == 4 || t == 0xd || t == 0x1f; }
constexpr int kPod = 0x3f, kSpore = 0x40, kWeed = 0x41, kRuins = 0x43;
constexpr int kRefinery = 0x2c, kGeneLab = 0x2d, kRelay = 0x2e, kPsi = 0x2f, kGeneLabPlus = 0x35,
              kRelayPlus = 0x36, kPsiPlus = 0x38;

// A thing by value: enough to put it back exactly.
struct ThingRec {
    int type = -1;
    int owner = kNeutral;  // player index, 9 = none
    int x = 0, y = 0;      // top-left corner, world px (+0x0a/+0x0c)
    int hp = 0;            // +0x24
    int vet = 0;           // creatures: veteran level 0..3 (+0x7c)
    int gizmo[4] = {kNoGizmo, kNoGizmo, kNoGizmo, kNoGizmo};  // creatures (+0x54)
    int extra = 0;         // crops: growth stage (+0x28); obstacles: rotation (+0x2a)
};

// --- things -------------------------------------------------------------------
uintptr_t Thing(int id);  // the thing, or 0
int ThingCount();         // size of the thing table (ids are 0..count-1)
int TypeOf(uintptr_t t);
int OwnerOf(uintptr_t t);  // player index, 9 = none
int CentreX(uintptr_t t);
int CentreY(uintptr_t t);
bool Capture(int id, ThingRec& out);
// Put a recorded thing back as it was (only the engine's own minimal checks).
// The new id, or kNoThing.
int Restore(const ThingRec& r);
// A new thing centred on world (wx, wy), with the editor's placement rules:
// creatures as the Gene Lab places them, devices as a builder may (power is
// not needed in the editor), crops on pod soil, trees and rocks on their own
// subtype, ruins on open ground. `attrs` (optional) carries hit points,
// veteran rank and gizmos. The id, or kNoThing with the reason in `why`.
int Put(int type, int owner, int wx, int wy, const ThingRec* attrs, std::string& why);
void Remove(int id);  // quietly: no corpse, no remains
// The footprint Put would give a new `type` centred on (wx, wy): world px, x1/y1 exclusive.
void PlacementRect(int type, int wx, int wy, int& x0, int& y0, int& x1, int& y1);
// The thing under world point (wx, wy): the nearest whose radius covers it.
int PickAt(int wx, int wy);
// Debug: thing-grid entries (World +0x10c) whose id has no thing.
int StaleGridEntries();
void Select(int id);  // the game's selection: just this thing (kNoThing: none)
int MaxHp(int id);
void SetHitPoints(int id, int hp);  // in place (vtable SetHP, clamped to the maximum)
std::string Name(int type);    // the game's display name ("Pod", "Crystal Tree", ...)
std::string GizmoName(int g);  // "Blade", ...; "none" for 100
// Gizmo slots of a creature type (the Gene Lab's rules, 0x42c3e0): which of
// the 4 exist, and whether a slot takes arm (0) or head (1) gizmos.
bool SlotUsed(int type, int slot);
int SlotGroup(int type, int slot);
// The gizmos a slot may hold for `owner`'s clan levels (APlayer::HasGizmoTech
// 0x43a420, Gene Lab+ level), in order, starting with 100 (none).
std::vector<int> GizmoChoices(int type, int slot, int owner);
// Pre-placed devices are complete: the cheat placement's BuildStep loop.
void Complete(int id);

// The obstacle rules: a corner terrain query (TRST values, editor/terrain_paint.h)
// for the tile under a world point; -1 = unknown (no rules then).
void SetCornerQuery(int (*corner)(int tileX, int tileY));

// --- terrain -------------------------------------------------------------------
int TilesW();                      // map size in 32 px tiles
int TilesH();
int TerrainType(int tx, int ty);   // 0 ground, 1 blocked, 2 pod soil (TERRAIN.HOO, 0x42ea40)

// --- players ---------------------------------------------------------------------
// The original editor's player types (dialog 135's combo order, MapEditor.exe
// 0x404640), kept in APlayer +0x4a44 ("MPTY" in the file).
enum PlayerType { kComputer = 0, kHuman = 1, kEither = 2, kNone = 3 };
const char* TypeName(int type);
const char* RaceName(int race);  // 0 Tanu, 1 Sen-Soth, 2 Kelika, 3 Magha

struct PlayerRec {
    int type = kNone;   // kNone: no such player
    int race = 0;       // +0x4878
    int colour = 0;     // +0x484e (palette index)
    int cgk[4] = {};    // +0x4850 per clan, 0..5
    int avail[4] = {};  // +0x1c per clan: the clan's creatures can be bred
    int startX = 0, startY = 0;  // +0x4a1c, cells
};

uintptr_t Player(int i);  // APlayer*, or 0
int LocalPlayerIndex();   // -1 if none
PlayerRec CapturePlayer(int i);
// Make player i what `rec` says: create it (type None -> other), delete it
// (-> None; its things must be gone already), convert it (human <-> computer:
// MakeHuman 0x43c710 / MakeComputer 0x43c7d0) and set its fields. `fogMode`
// (MapRec) applies to a new player.
void ApplyPlayer(int i, const PlayerRec& rec, int fogMode);
// What a new player gets (race in turn, the shipped maps' CGK, a start cell).
PlayerRec DefaultPlayer(int i, int type, int supported);
int ColourOf(int i);  // the retail colour of slot i (247 - 8i)

// --- map ----------------------------------------------------------------------
enum FogMode { kFogAndShroud = 0, kFogOnly = 1, kFogOff = 2 };
const char* FogName(int mode);
struct MapRec {
    std::string name;
    int supported = 2, recommended = 2;
    int fog = kFogAndShroud;
};
std::string MapName();             // MapInfo +0xc
void SetMapName(const std::string& name);  // MapInfo::SetName 0x432150
int Supported();
int Recommended();
void SetSupported(int n);          // MapInfo +2 (the console's "supports" also deletes players; this doesn't)
void SetRecommended(int n);        // MapInfo +4
// The fog mode: every player's fog flag (+0x484c) and explored map (+0x2426);
// the local player's flag is kept off while editing (`localFog` receives it).
int FogModeOf(int localFog);
void SetFogMode(int mode, short& localFog);

}  // namespace world
}  // namespace editor
