#pragma once
// Pathfinding: Improved / Original (see re/pathfinding.md for the stock planner
// and its faults). Original runs the stock code untouched. Improved replaces how
// ground units find their way: whole-map A* over a 32 px tile grid that knows
// terrain, rocks and the buildings each player has seen, with unreachable orders
// sent to the nearest reachable spot, traffic handling and group moves spread
// around the click point. Unit speeds and stepping stay stock.
//
// Everything that changes the simulation runs inside the game turn (on every
// machine in multiplayer), in integer maths, so lockstep stays in sync. The mode
// is latched at each mission start; multiplayer stays Original until the host's
// choice can be shared.

namespace pathing {

struct Debug {
    bool hash = false;  // [Debug] PathHash=1: log a state hash every 50 turns
    int test = 0;       // [Debug] PathTest=n: run test scenario n (autopilot mode 26)
};

void Install(void (*log)(const char*, ...), const Debug& d);
// The player's choice ([Gameplay] Pathfinding); takes effect at the next mission start.
void SetPreference(bool improved);
// The mode the current mission runs in.
bool Improved();
// Autopilot mode 26: the test scenario has finished (and logged its PASS/FAIL lines).
bool TestDone();

}  // namespace pathing
