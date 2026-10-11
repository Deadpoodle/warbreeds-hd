#pragma once
// In-game map editor, Stage 1: terrain painting on a map loaded into the
// mission scene (SGame) with the simulation paused. See re/editor-shell.md.
#include <windows.h>

#include <string>

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
bool PanelContains(int x, int y);  // a frame pixel on the tools panel (overlay scenes leave it to the editor)
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
int TestStage();                 // Test Map: 0 none, 1.. in progress
int LateMoves();                 // panel items moved at draw time (zoom resized the frame)
int TypeTile(const char* name);  // the tile a palette entry stamps, -1 if none
std::string TypeTiles(const char* name);  // every tile it may paint, "1,2,3" (tests)
// Would a stroke paint there without a refusal? (tests choose legal terrains; the map is untouched)
bool StrokeWorks(const char* name, int size, int x0, int y0, int x1, int y1, std::string* why = nullptr);
std::string TypeName();          // the chosen palette entry

}  // namespace editor
