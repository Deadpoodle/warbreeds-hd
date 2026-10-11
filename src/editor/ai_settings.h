#pragma once
// In-game map editor, Stage 3: the Global AI settings of the 1998 editor's
// Tools > Global AI Editor (dialog 132): per computer player, squad
// aggression, base defence, scouting and AI influence, five steps each. Shown
// as a panel on the event editor's scene (events.h). See re/editor-events.md.
#include <windows.h>

#include <string>

namespace editor {
namespace ai {

enum Setting { kSquad, kBase, kScouting, kInfluence, kSettingCount };

bool Open();    // queued, like events::Open
void Close();
bool Active();  // the panel is open (or opening)
void Tick();    // every frame while the editor is open
bool Dirty();   // differs from the map as opened or last saved

// For the tests (autopilot).
int ComputerCount();                         // computer players in the map
int PlayerSlot(int computer);                // its player slot, -1 if none
int Level(int slot, Setting s);              // 0..4, -1 if not a computer player
std::string Describe();                      // "slot squad base scouting influence" per computer player
// Frame pixels (centre) of a step (0..4) of a setting, of the player buttons
// (prev = 0, next = 1) and of Close.
bool StepCentre(Setting s, int level, POINT& frame);
bool PlayerButtonCentre(int next, POINT& frame);
bool CloseCentre(POINT& frame);
int ShownSlot();                             // the player the panel shows

}  // namespace ai
}  // namespace editor
