#pragma once
// In-game map editor: a map loaded into the mission scene (SGame) with the
// simulation paused; terrain, things, units, players and map settings. See
// re/editor-shell.md (Stage 1) and re/editor-stage2.md. Tabs: tabs.h.
#include <windows.h>

#include <string>
#include <vector>

#include "mapfile.h"

namespace editor {

struct Hooks {
    void (*log)(const char*, ...);
    std::string dir;     // game folder, with trailing backslash
    bool debug = false;  // [Debug] Autopilot: write each save's terrain diff for the tests
};

// After controls::Install (the mouse hooks chain onto whatever is in SGame's vtable).
void Install(const Hooks& h);

// From the main menu: load a map into the mission scene for editing.
void OpenMap(const MapEntry& map);
// From the main menu: write a blank map and open it.
void NewMap(const NewMapSpec& spec);

// Every frame tick.
void Tick();
bool Active();  // a map is open in the editor
// Window messages while editing (keys, typed text). True = handled; the game
// must not see it.
bool OnMessage(UINT msg, WPARAM wp, LPARAM lp);
// The main menu asks once, after the editor exits, whether to show the map list again.
bool TakeReturnToMapList();

// For the tests (autopilot).
std::string Status();            // the panel's status line
bool Idle();                     // editing, nothing in progress (no name entry, processing or saving)
int SaveCount();                 // completed saves since the editor opened
bool CellCentre(int cx, int cy, POINT& frame);  // frame pixel of a cell's centre, if on screen
bool WorldPoint(int wx, int wy, POINT& frame);  // frame pixel of a world point, if on screen
int TestStage();                 // Test Map: 0 none, 1.. in progress
int LateMoves();                 // panel items moved at draw time (zoom resized the frame)
int TypeTile(const char* name);  // the tile a palette entry stamps, -1 if none
std::string TypeTiles(const char* name);  // every tile it may paint, "1,2,3" (tests)
// Would a stroke paint there without a refusal? (tests choose legal terrains; the map is untouched)
bool StrokeWorks(const char* name, int size, int x0, int y0, int x1, int y1, std::string* why = nullptr);
std::string TypeName();          // the chosen palette entry
// A panel item shown with exactly this text: its centre in frame pixels.
bool FindItem(const char* text, long& x, long& y);
int CurrentTab();                // tabs.h Tab
// The tool outline drawn last frame: "kind x0 y0 x1 y1" (world px; kind 0 none, 1 brush,
// 2 fill, 3 a thing's footprint).
std::string GhostInfo();
bool Collapsed();                // the panel is folded to its strip (Tab)
// A frame point the panel or the minimap covers on screen (a click there doesn't reach the map).
bool Hidden(POINT frame);
std::vector<std::string> Findings();  // the last map check
// An on-screen cell near (nearX, nearY) whose four tiles suit `what`, with no thing near.
enum CellKind { kCellOpen, kCellPodSoil, kCellRoots, kCellCrushed, kCellHardenedTar };
bool FindCell(int what, int nearX, int nearY, int& cx, int& cy, int clearPx = 128);
int CountThings(int type, int owner);          // -1 = any
bool ThingPoint(int type, int owner, POINT& frame);  // the newest such thing's centre, if on screen
std::string Selected();          // "id type owner x y hp vet g0,g1,g2,g3" or "none"
int UndoDepth();
// The index-th clickable item on a page row (tabs.h rows): its centre in frame pixels.
bool FindItemOnRow(int pageRow, int index, long& x, long& y);
std::string ThingName(int type);  // the game's display name
// "type race colour cgk0,cgk1,cgk2,cgk3 avail0,..,avail3 startX startY" (type 3 = None).
std::string PlayerInfo(int i);
std::string MapInfoText();       // "supported recommended fog name"
// Where the newest thing of `type` (-2: any Shaman) owned by `owner` (-1: anyone) is, world px.
bool ThingWorld(int type, int owner, int& wx, int& wy);
void LookAt(int wx, int wy);     // centre the camera there

}  // namespace editor
