#pragma once
// The Improved pathfinding's navigation data and searches (no game addresses;
// pathing.cpp fills it from game memory). Integer maths and fixed orders only,
// so every machine computes the same routes in multiplayer.
//
// Geometry (re/pathfinding.md, TestMoveTo 0x4020b0): a ground unit of radius r
// at pixel (x, y) collides with
//   - terrain when any 32 px tile in [(x-r)>>5, (x+r)>>5] x [(y-r)>>5, (y+r)>>5]
//     is blocked (inclusive box; off the map is blocked);
//   - a thing of radius R when dx*dx + dy*dy < (R + r)^2 (circle test 0x445ac0;
//     Yedda Ruins are several circles, 0x434df0);
//   - a thing whose top-left tile is the tile of (x - hf, y - hf), hf = half the
//     unit's footprint (the first test in FindThingAt 0x44db90).
// Routes run over a lattice of nodes every 16 px. Each node stores its
// clearance: the largest radius a unit centred there may have. For edges to be
// safe the circle test gets a margin of 128 px^2 (half a lattice diagonal,
// squared); diagonal moves also need both orthogonal neighbours free. The
// steps a unit is given are checked exactly.

#include <stdint.h>

#include <vector>

namespace nav {

constexpr int kMaxR = 40;        // clearance cap (ground creature radii are 18..31)
constexpr int kMaxPlayers = 8;
constexpr int kLattice = 16;     // px between nodes
constexpr int kAll = 100;        // as a player: every thing counts as known (tests)

struct Pt {
    int x, y;
};
struct Circle {
    int x, y, r;  // centre (absolute px) and radius
};

// --- building the grid ------------------------------------------------------

// A new map: w x h tiles; blocked[ty * w + tx] != 0 for blocked tiles.
void BeginStatic(int w, int h, const uint8_t* blocked);
// Rocks, trees and ruins (fixed for the mission): one circle each (ruins: several).
void AddStaticCircle(int x, int y, int r);
void AddStaticTile(int tx, int ty);  // a static thing's top-left tile
void FinishStatic();                 // computes the static clearance; drops regions
bool Ready();
int TilesW();
int TilesH();

// Buildings, pods and corpses: by thing id, each known to a mask of players.
// A thing comes with its centre, top-left tile and collision circles (most have
// one; some buildings several); moves and changes are upserts.
void UpsertDynamic(int id, int x, int y, int tlx, int tly, const Circle* circles, int n);
void RemoveDynamic(int id);
void ClearDynamic();
// Player p now knows thing id (owned, seen or bumped into).
void SetKnown(int id, int p);
bool IsKnown(int id, int p);
// Thing ids present (in id order) - for the per-turn diff.
void DynamicIds(std::vector<int>& out);
// Recomputes the per-player clearance where things appeared, moved, vanished or
// became known. Players get a layer on their first route.
void RefreshDynamic();

// --- queries ----------------------------------------------------------------

// Exact: may a unit of radius r (half footprint hf) of player p stand at (x, y)?
// Creatures are not considered.
bool Passable(int x, int y, int r, int hf, int p);
// The things part only: circles and top-left tiles (p = -1: static things only).
bool ThingsBlocked(int x, int y, int r, int hf, int p);
// The terrain part only (fixed IsRectBlocked semantics).
bool TerrainBoxBlocked(int x0, int y0, int x1, int y1);
// The static region of node-space point (x, y) for radius r (0 = blocked).
int Region(int x, int y, int r);

// Building sites waiting for their builder: circles of radius `half` round (x, y)
// (one of the building's collision circles) that player p's units keep clear of
// (a unit's centre stays more than half + its radius away; `fixed`: more than
// half), all but the builder itself. (siteX, siteY): the building's centre.
struct Site {
    int x, y, half, player, builder, siteX, siteY;
    bool fixed;
};
void SetSites(const std::vector<Site>& sites);
// The unit the following queries are for: its own site doesn't block it, nor
// does the site at (siteX, siteY) it is building too (-1: none).
void SetQueryUnit(int id, int siteX = -1, int siteY = -1);
// Is (x, y) inside one of player p's sites for a unit of radius r (not its builder)?
bool OnSite(int x, int y, int r, int p);

// Extra circles (creatures in the way) that count as blocked for the queries and
// routes that follow, until cleared with n = 0.
void SetTemporary(const Circle* circles, int n);

// --- routes -----------------------------------------------------------------

struct Route {
    std::vector<Pt> nodes;  // lattice points from near the start to the end point
    Pt end{};               // the reachable point the route ends at
    bool exact = false;     // end is the requested goal
    int expansions = 0;
};

// A route for a unit of radius r, half footprint hf, player p from (sx, sy) to
// (gx, gy). If the goal can't be reached, the route ends at the nearest point the
// unit can reach (Route::exact false). Returns false when the unit has nowhere to go.
bool FindRoute(int sx, int sy, int gx, int gy, int r, int hf, int p, int maxExpansions, Route& out);
// The simulation turn (regions with buildings are refilled at most every 60 turns).
void SetClock(int turn);
// How many region fills (with buildings) since the last call.
int TakeFills();
double TakeFillMs();  // and their time
// Bumped whenever player p's building layer changes (things appeared, moved, went
// or became known), so routes can be re-checked.
int LayerVersion(int p);
// Are a route's nodes from index `from` on still free for this unit?
bool RouteFree(const Route& route, size_t from, int r, int hf, int p);
// The nearest point to (gx, gy) a unit of radius r of player p starting at
// (sx, sy) can reach (the goal itself when it can); false if none.
bool NearestReachable(int sx, int sy, int gx, int gy, int r, int hf, int p, Pt& out);

// A route's length in px from node index `from` (starting at `pos`).
int RouteLength(const Route& route, size_t from, Pt pos);
// Can a unit walk the straight line from a to b in steps of `step` px (major
// axis), every step checked exactly?
bool LineClear(Pt a, Pt b, int step, int r, int hf, int p);
// The steps from a toward b (at most max; positions only), each one step size or
// less along the major axis, stopping before the first blocked one. Returns how
// many were written; *reached = the last one is b.
int Steps(Pt a, Pt b, int step, int r, int hf, int p, Pt* out, int max, bool* reached);

// --- debug ------------------------------------------------------------------

// Static clearance of the node at lattice coordinates (i, j).
int StaticClearance(int i, int j);
int PlayerClearance(int i, int j, int p);
int LatticeW();
int LatticeH();

}  // namespace nav
