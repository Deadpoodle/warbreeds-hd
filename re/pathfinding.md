# Pathfinding and unit movement

> Research report (static analysis, nothing run). Confidence: **H** read from the code, **M** mechanism clear but purpose inferred, **L** a guess. Addresses are authoritative; where a name here differs from `re/names.tsv`, the TSV is the current one (and the export `re/ghidra-export-v2/` uses it). Index: `re/subsystems.md`.


Confidence: **H** = read from the instructions (decompile checked against `re/text.asm` for the key
constants), **M** = mechanism clear, purpose inferred, **L** = guess.
Many ACreature virtuals (e.g. 0x49f0c0, 0x49bab0, 0x4a2240) are missing from
`re/ghidra-export/functions.tsv`; they were read from the linear disassembly.

## Verdict on the C-shape hypothesis: confirmed (H)

Both planners are A* in form but greedy in behaviour, and both have step caps. When a cap is hit
or the goal is unreachable, both return a path to the node closest to the goal. The movement code
accepts such a partial local path as success, so the global planner never runs while the local
planner can still reduce the distance. Five things combine:

1. **Local planner (0x4a37a0)**: f = g + h, with g in pixels and **h = squared pixel distance,
   clamped to 0xFFFF**. Within about 256 px of the goal, h dwarfs g, so the search is greedy best-first.
   Beyond 256 px, h saturates and the search cannot tell which node is closer (H).
   - **Hard caps:** 600 nodes (pool), 1000 expansions (`cmp eax,0x3e7` at 0x4a396f), and a
     193x193-cell window (±96 cells) around the start point.
   - **Stall cap:** stop after `stall` expansions with no strict improvement in the best h.
     `stall` = `Path_ScaleBudget(creature+0x11c)`; in single player that is +0x11c itself, usually
     144/172/64, but only 48, 28 or 14 after failures (below).
   - **On any stop:** it returns the parent chain of the node with the **smallest h** (H).
2. **The movement code accepts partial paths.** `ACreature::PlanNextLeg` (0x49d130) accepts any
   local path with at least 1 step (sometimes 2) after the start node is popped (H). The unit
   therefore walks into the bottom of the C. From there, the best reachable node is the unit's own
   position, so the next search returns 0 steps.
3. **The global planner only runs after the straight-line and local planners both fail**
   (0x49df30, H). It needs a guide point within 96 px of the unit (`Guide_FindNear` 0x4a22f0:
   own tile or ±32 px, dist² <= 0x2400). Guide points exist only at **convex** corners of static
   obstacles (below), so the inside of a C usually has none, and the global planner returns
   without a route.
4. **Global A\* (0x4a6d40)** is also greedy: its h is an unclamped squared distance, against edge
   costs in plain pixels. It stops on 1000 expansions, or after 68 expansions (45 in multiplayer)
   with no improvement in the best f. It then returns the route to the node with the lowest f,
   which is roughly the closest one (H).
5. **Fallbacks:** a sidestep waypoint (up to 4 tries, +0xbc), then "restart the order with a
   ±4 px jitter" (0x49d980), plus a slowly growing arrival tolerance. A unit therefore oscillates
   in the pocket until the tolerance swallows the goal or the order is cancelled (M).

## Overview: data flow of a move order

```
World::IssueMove 0x44ccd0 --CmdMove (vtbl+0x34)--> CSinglePlayer::Move 0x460430
  -> CommandModel::ExecuteMove 0x461b00
       1-2 units: thing->vtbl+0xb4 (ACreature::OrderMove 0x49f0c0) at once
       3+ units : CommandModel::QueueDeferredMove 0x462750, due turn = turn+1+(i % 3 SP / % 7 MP)
                  -> CommandModel::GameTurn 0x463080 -> ProcessDeferredMoves 0x462550 -> ExecuteMove
ACreature::OrderMove: waypoint stack +0xac (reversed so data[0] = final goal, top = next waypoint),
                      copy in +0xc8, activity +0xc0 = 0x29, arrival tolerance +0xc4 = order flag
per tick: thing vtbl+0x08 -> ACreature::MoveTick 0x49b6b0 (vtbl+0x88)
   if no steps: ACreature::Think 0x49bd50 (vtbl+0x8c) -> CheckWaypointReached 0x49c490 -> PlanNextLeg 0x49d130
        PlanNextLeg -> steps into ACreatureBody step stack +0x90 via ACreatureBody::SetPath 0x404750
   -> ACreatureBody::MoveTick 0x403800 -> ACreatureBody::Step 0x404200 (one step per tick, collision test)
        blocked -> ACreature::OnBlocked 0x49bab0 (vtbl+0x90): wait 9-12 ticks / replan
```

## 1. Order entry (H unless marked)

- **`World::IssueMove` 0x44ccd0** (cdecl: selection, PtList*, short exact) handles selection kinds 1-3.
  - **Tolerance:** `tol = World_GroupArrivalTolerance 0x44ceb0` (0 for one unit; otherwise
    `(Σ(radius[type]+1) + 25)² + 200` px², or 0xFFFF when the sum is ≥ 255).
  - **Points:** the order points are clamped to [40, mapPx-40] (0x44a570 / 0x44a620).
  - **Command:** it then calls `CmdMove(ids, pts, tol)`.
  - **Groups:** there is **no formation**. Every unit gets the same point list and the same
    (group-size-scaled) tolerance.
- **`CommandModel::ExecuteMove` 0x461b00** (thiscall, IdList*, PtList*, short tol).
  - It skips things whose type is not a creature (0..0x2b).
  - **Three or more units:** the move is deferred and staggered per unit to spread the
    pathfinding load: due turn = `GetGameTurn()+1+(i % 3)` in single player, `% 7` when
    `CommandModel::GetType()==0x33` (multiplayer).
  - **Queue:** `CommandModel+0x240` (IList of DeferredMove*, data +0x244, count +0x248).
    `QueueDeferredMove` replaces an older entry for the same id unless its first point is the
    same.
  - **Processing:** `ProcessDeferredMoves 0x462550` runs at the start of `CommandModel::GameTurn
    0x463080`. The turn counter is +0x238 (`GetGameTurn 0x4708b0`).
- **`ACreature::OrderMove` 0x49f0c0** (vtbl+0xb4, thiscall(PtList*, short tol), ret 8).
  - **Ignored:** the order is dropped if `0x407170()` (an order lock, L) is set, or if the new
    final point equals the current final goal.
  - **Steps:**
    1. It calls vtbl+0xc8 = `ACreature::ResetOrder 0x49ac10`. That clears the path state, sets
       +0x11c (stall limit) to its maximum and +0xbc (detour tries) to `[0x509300]` = 4.
    2. It copies the points into the waypoint stack +0xac and reverses them, so `data[0]` is the
       final goal and the top `data[count-1]` is the next waypoint.
    3. It copies them unchanged into +0xc8 (order copy, used for restarts and patrols).
    4. It sets activity +0xc0 = 0x29 (move) and tolerance +0xc4 = tol.
  - AShaman uses the same slot.

## 2. Per-tick movement (H mechanism, names M)

- **`ACreature::MoveTick` 0x49b6b0** (vtbl+0x88):
  - It handles follow and attack bookkeeping and the bump-retry counter +0x142.
  - When `vtbl+0x80` (`ACreature::IsWalking 0x4a2240`: ready && (steps || follow target) && no
    attack target) is false, it calls `vtbl+0x8c` (`ACreature::Think 0x49bd50`).
  - It always ends with `ACreatureBody::MoveTick 0x403800`.
- **`ACreatureBody::MoveTick` 0x403800** calls `Step 0x404200` when walking. With +0x76 non-zero,
  it steps only every other tick (half speed). It also handles animation state +0x88.
- **`ACreatureBody::Step` 0x404200**:
  - **Empty stack:** with a follow target (+0x4a), it makes one step toward that thing (range +0x4c).
  - **Turning:** it peeks at the top `tPathStep`. If the facing (+0x48) differs by more than one
    octant and `turnFlag[type]` (0x54caf0) is set, it turns first (0x404910).
  - **Leash:** a leash test (+0x34 anchor, +0x3c radius²; when exceeded it calls vtbl+0x9c).
  - **Collision:** `ACreatureBody::TestMoveTo 0x4020b0(step.pos, &hit)`.
    - **Clear:** it pops the step, places the unit at step.pos (centre +0x14/+0x16, top-left
      +0xa/+0xc via footprint/2) and calls vtbl+0x94 when the stack is now empty.
    - **Blocked:** it sets +0x2c = 1, +0x2e = blocker id (0xC01 = terrain) and calls vtbl+0x90.
  - **Speed:** units move one step of `stepSize[type]` px (0x54d140) per tick. Line-scan steps
    are measured on the major axis, so diagonal movement is about 1.41x faster (M).
- **`ACreatureBody::TestMoveTo` 0x4020b0**:
  1. `World::FindThingAt 0x44db90(selfId, x, y, radius[type] 0x54cf08, footprint[type]/2,
     flyer)` checks other things.
  2. Then `Terrain::IsRectBlocked 0x42eb50` over the tiles of pos ± radius.
  - Flyers are types 4, 0xd and 0x1f (M). They use occupancy layer 1, and terrain never blocks them.
- **`ACreature::OnBlocked` 0x49bab0** (vtbl+0x90):
  - It pops reached waypoints first.
  - **While jittering (+0x10c):** it asks a blocking friendly to move aside (blocker vtbl+0xc4 =
    `ACreature::OnBumped 0x49ff70`).
  - **Normal move:** it sets the stall limit +0x11c to 28 (types 0/9/0x12/0x1b) or 14 (others).
    - If stepSize < 9, it waits `+0x140 = 9 + (id & 3)` ticks (+1 in multiplayer) and clears the
      steps.
    - Otherwise it just clears the steps (a replan follows).
- **`ACreature::Think` 0x49bd50** (vtbl+0x8c):
  - **Overflow:** more than 64 waypoints aborts the order (`0x49fea0`).
  - **Regrowth:** while +0x11c is below its maximum, each call adds 10 (18 for types
    0/9/0x12/0x1b) to +0x11c and **25 to the arrival tolerance +0xc4**.
  - **Waiting:** while the wait counter +0x140 is non-zero, it just counts down.
  - **Otherwise:** `CheckWaypointReached 0x49c490`, then `PlanNextLeg 0x49d130` if waypoints remain.
- **`ACreature::CheckWaypointReached` 0x49c490**:
  - **Arrival radius** (d² = distance² to the waypoint):
    - last waypoint: d² ≤ tol;
    - intermediate: d² ≤ tol/4 + 6·step²;
    - guide-route waypoint (route active, +0x13c): d² ≤ tol/4 + 24·step².
  - **Arrived:** it pops the waypoint (`PopWaypoint 0x49de60`). At the last one it sets activity
    0x28 (idle) and notifies the squad (+0x28, `AISquad` 0x4baf10, L).

## 3. Planning cascade: `ACreature::PlanNextLeg` 0x49d130 (H)

Input: the top waypoint `W = +0xb0[+0xb4-1]`. Output: `ACreatureBody::SetPath 0x404750(this, &steps)`,
which clears and copies into the +0x90 step stack. `need` = 1 step, or 2 when `(turn+id) & 4`.

- **Flyers (types 4/0xd/0x1f):**
  1. `StraightPath(W, 16)`.
  2. Otherwise `LocalPathToWaypoint`.
  3. Otherwise, if the stall limit +0x11c is at its maximum, `ArriveOrGiveUp`.
- **Ground units:**
  1. **Straight line** `ACreature::StraightPath 0x49e4d0(out, W, 16)`. It uses
     `ILineScanConversion`, probes every 4 steps, then emits steps with `IsBlockedAt` checks and
     fails if any probe or step is blocked. It returns at most 16 steps.
  2. **Guide-route shortcut** (only while a guide route is active and budget > 2): `0x49ec70`
     tries a straight line, ignoring creatures, to the waypoint below the route. On success it
     drops the route.
  3. **Local A\*** `ACreature::LocalPathToWaypoint 0x49e430`. It first clears blocker id +0x146.
     - If W itself is blocked, it **sets the stall limit +0x11c = 48 permanently** (until Think
       regrows it).
     - It then runs `AILocalPathFinding::FindPath(self → W)` and pops the start node.
     - **A partial (closest-node) path is accepted here.**
  4. **Global plan** `ACreature::GlobalPlan 0x49df30(out, 0)`, described below.
  5. **Detour:** only when `GetBehaviour 0x4a22b0()==0` or activity 0x35 (L: passive stance).
     - While the detour tries counter +0xbc > 0, it picks a sidestep point (`0x49cf40` →
       `0x49cba0`), pushes it as a new waypoint (`0x49ed70`), clears the old waypoints and recurses.
     - When +0xbc == 0, `RestartOrderJitter 0x49d980` restores the original order list from
       +0xc8, steps to a random ±4 px offset and sets +0xbc = 3.
  6. **Multiplayer only:** the local A* is retried with a doubled stall limit when W is free.
  7. **Human players only** (`APlayer vtbl+0x7c` == 1, budget > 2): `GlobalPlan(out, 1)`, which
     searches rings of up to 10 tiles (7 in multiplayer) for any guide point closer to the target
     (`0x4a23f0`).
  8. **Give-up logic**, depending on the stall limit +0x11c:
     - Below its maximum: `WaitOrCancel 0x49d820` (wait 4/6/8/12 ticks, or **cancel all
       waypoints** for passive units), or `RestartOrderJitter`.
     - At its maximum: `ArriveOrGiveUp 0x49daa0`. It loosens the tolerance +0xc4 by
       1764 px² (= 42², `0x4a2bf0`), detours, shoves friendlies (`World::ShoveFriendlies
       0x447810`) or pops/skips waypoints.
     - Every branch then sets +0x11c to 28 or 14.
- **Global abort flag** `g_PathAbort` [0x57abc0]: `IsBlockedAt` sets it when it switches the
  unit to "follow the friendly unit in front of me" (`ACreatureBody::Follow 0x404880`). Every stage
  checks it (0x4a1be0) and bails out; PlanNextLeg clears it (0x4a1bf0).

### `ACreature::GlobalPlan` 0x49df30(out, mode)
1. Returns at once while a guide route is already active (+0x13c).
2. **Mode 0:**
   1. Straight line of 3 or 6 steps (6 only on a patrol and on alternate turns).
   2. Then `StraightPathIgnoringCreatures 0x49e890` along the whole line. Only terrain and
      non-creature things block.
   3. Then the start guide is `Guide_FindNear(self pos)`.
3. **Mode 1:** the start guide is `ACreature::FindGuideTowardTarget 0x4a23f0`.
4. **Reverse local A\*** from W toward the unit. The guide nearest its best node becomes the goal
   guide; with none, W is the goal.
5. **Route:** `AIMovementGuide_FindRoute 0x4a47b0(&route, radius[type], startGuide, goalPos)`.
   - It drops the last route point if it is within 2 steps.
   - It drops the route if it has only one point and that point is farther from W than the unit.
6. **Use:** otherwise it pushes the route points onto the waypoint stack +0xac on top of W, sets
   +0x13c = 1 and +0x138 = the first route point, then plans the first leg to the top route point
   (straight line, else local A*).
   - If that leg is empty, it removes the route again.
- Each waypoint = guide pos + guide corner direction × (radius+1), which pushes it diagonally
  off the obstacle corner.

## 4. Local planner: `AILocalPathFinding` (H)

- **Object:** embedded at `ACreature+0x118` (0x1c bytes; constructor 0x4a3730(owner, stall)).
  - **Stall limit +4:** 0xAC for types 0/9/0x12/0x1b; 0x90 for others; 0x40 when the creature is
    in a squad (+0x28 ≠ 0).
  - **Other fields:** +8 owner, +0xc goal pixel, +0x14 goal cell.
- **`FindPath` 0x4a37a0** — thiscall(IStack\<tPathStep\>* out, IPixelC* start, IPixelC* goal), ret 0xc.
  - **Grid:** cell = `stepSize[type]` px. The window is `g_LocalPathGrid` (0x57abc8, short[193][193],
    cleared to -1 on every call) centred on the start (`g_LocalPathOrigin` 0x592334).
  - **Nodes:** `g_LocalPathNodes` 0x58ced0 (600 × 0x24); count `g_LocalPathNodeCount` 0x592338.
  - **Open list:** a linear scan for the open node with the lowest f (`LocalPath_PopBest 0x4a3c50`).
  - **Expansion is passability-lazy.** A popped node is tested with `ACreature::IsBlockedAt`;
    blocked nodes are closed, but they have already used a pool slot. A free node first gets its
    one neighbour toward the goal (`Expand 0x4a3cc0`, octant table 0x503210). Only when that adds
    or improves nothing are the other 7 directions tried, fanning out (+1,-1,+2,-2,+3,-3,+4 from
    0x5093f4), and the node closed.
  - **Costs:** straight step = `stepSize`, diagonal = `diagStep[type]` (0x54df10 = round(step·√2),
    filled by 0x464fd0), plus `stepSize` when `turnFlag[type]` and the direction changes.
  - **Stop conditions:**
    - goal cell reached (`IsGoalCell 0x4a3bf0`);
    - more than `stall` expansions without improvement;
    - more than 999 expansions;
    - out of the window or the pool is full; the abort flag is also checked.
  - **Output:** the chain from the end node (goal if reached, else min-h node) back to the start is
    pushed into `out`: data[0] = end, top = start. If the end is the goal cell and the exact goal is
    free, the exact goal point is pushed first. Callers pop the start node.
- **`ACreature::IsBlockedAt` 0x4a1870**(IPixelC*) returns non-zero when blocked.
  - **Out of map:** blocked.
  - **Fog of war** (64 px cells, APlayer +0x2426 / +0x26 maps, fog flag +0x484c, M): cells hidden
    from the owner are treated as **free**. Cells that are remembered but not visible test only
    static class-0x17 objects (`World::FindStaticObstacleAt 0x44ddd0`). Visible cells test every
    thing except self (`World::FindThingAt 0x44db90`).
  - **No thing:** it tests terrain over pos ± radius (`Terrain::IsRectBlocked`).
  - **Friendly creature in front:** if the blocker is the owner's own creature heading toward the
    same or a nearer goal, the unit starts **following** it (distance = 2·myRadius +
    otherRadius), the cell counts as free, and `g_PathAbort` is set.

## 5. Global planner: guide graph + `nAIAStar` (H)

- **Graph (AIMovementGuide, inside AITerrainAnalysis +0x1c; global points `g_GuidePoints` 0x592348):**
  - **Nodes:** `tGuidePoint` (0x28) records sit at **convex corners of blocked 32 px tiles**.
    - `AIMovementGuide::Build 0x4a4eb0` takes a blocked tile whose diagonal neighbour and both
      orthogonal neighbours toward it are free.
    - Point = tile centre + 16·(dx,dy); direction (dx,dy) at +0x14.
    - "Blocked" = `tObstacleMap` (0x4a7a70): terrain class 1 or 2 (`Terrain::GetTileClass
      0x42ea40`), plus the tiles covered by class-0x17 things (static map objects; buildings and
      devices in types 0x2c-0x42 are **not** included, M).
  - **Lookup:** `AddGuidePoint 0x4a4a00` registers each point in a tile→guide `IMap` over a 5x5
    tile area (nearest wins). `FillLookupAlongLinks 0x4a5f00` later extends this along each link
    to its midpoint.
  - **Edges:** `ProcessLinks 0x4a5200` links pairs with dist ≤ 900 px (`+0x2c` = 810000),
    provided that:
    - no third guide point lies on the line (`0x4a6230`);
    - the corridor is clear (`0x4a53f0`): the centre line is free on the obstacle map, and at least
      2 of 4 parallel lines offset by (±23, ±23) px are free.
    - Cost = round(Euclidean px). Neighbour lists are `IList<tNeighbor>` (8 bytes: index, cost);
      the slot count is read as a byte.
  - **Incremental:** ProcessLinks runs from `Terrain::TurnUpdate 0x42dcf0` once per game turn.
    It makes **200 (i,j) iterations per turn over all N² pairs**, then prunes redundant points
    (`0x4a5990`), fills the lookup and logs "Map processed".
  - **Serialised:** `Terrain::Serialize 0x42e080` → `AITerrainAnalysis::Serialize 0x4a7da0` →
    `AIMovementGuide::Serialize 0x4a4670`. That stores the tile map plus all points and neighbour
    lists, and on load sets the link cursor to "done". **So shipped maps normally load a
    precomputed graph.** If the chunk is missing, a resource-string prompt (0x4719b0(3,2,1))
    offers to generate one.
  - **The graph is static.** Nothing rebuilds it when buildings are placed or destroyed
    (`AITerrainAnalysis::Resize 0x4a8010` is called only on a map resize).
- **Search:** `AIMovementGuide_FindRoute 0x4a47b0` (cdecl(IList\<IPixelC\>* out, short radius,
  tGuidePoint* start, IPixelC* goal)) calls `nAIAStar::Search 0x4a6d40` (ecx = {tPointState
  {goal, startIndex}, stallLimit}).
  - **Nodes:** static `nAIAStar::tNodeList` `g_AStarNodes` 0x593310 (0x20 per node), grown on
    demand.
  - **Scans:** the open list is a linear min-f scan. Duplicates are found by a linear scan over all
    nodes; closed nodes are reopened when g ≤ the old g.
  - **Heuristic:** `0x4a7200` = squared distance. **Goal test** (`0x4a6200`): the guide position
    equals the goal exactly, so a goal that is not itself a guide point is never "reached".
  - **Limits:** more than 1000 expansions, or more than `Path_ScaleBudget(68)` (45 in multiplayer)
    expansions without improvement in the best f.
  - **Result:** the route to the goal, or to the min-f node; the start node comes first. FindRoute
    converts it to points: guide pos + dir·(radius+1).
- **Path budget:** `g_PathBudget` [0x51bedc] = 16 by default. In multiplayer, message case 0x1c
  in 0x45ab10 sets it to 1..17 from table 0x504ed8 (likely from network latency, L).
  - `Path_ScaleBudget 0x46f860(r)` = r at 16, r/16·b below 16, and r + (b-16)·r/16 above 16.
  - Several stages need a budget > 2 or > 3.

## 6. Passability layers (H)

| Layer | Owner / address | Cell | Used by |
|---|---|---|---|
| Terrain tile class (byte per 32 px tile in 64 px metatiles) | Terrain [0x55dd60]: +8 ushort metatile index[row][0x60], +0x93a4 uint masks[], +0x9054/0x9055 width/height | 32 px | `IsRectBlocked 0x42eb50` (bit 0 of each byte), `GetTileClass 0x42ea40` |
| Thing occupancy | World [0x579278] +0x10c ushort id[2 layers][192][192] (0xC01 = empty), +0x2410c byte "large thing" flag[192][192] | 32 px | `FindThingAt 0x44db90`, `FindStaticObstacleAt 0x44ddd0`, `ShoveFriendlies 0x447810`; scans the tile box ±2 (±6/4 near large things) and tests circles via thing vtbl+0x2c |
| Obstacle map | AITerrainAnalysis +4 `tObstacleMap` short[h][w] | 32 px | guide-graph build and link tests only |
| Fog | APlayer +0x26 / +0x2426 byte[..][0x60], +0x484c flag | 64 px | `IsBlockedAt` |

- **Unit size:** `radius[type]` short (0x54cf08) and `footprint[type]` short (0x54e370; game.h
  `FootprintTable`). These are 44-entry creature tables filled at run time, like `stepSize`
  (0x54d140), `diagStep` (0x54df10) and `turnFlag` (0x54caf0).
- **Bug (H, from the disassembly at 0x42ec81..0x42ecb6): `Terrain::IsRectBlocked` uses a stale
  metatile mask.**
  - For odd tile rows (bytes 2/3 of the mask), it never reloads the mask per column. It tests
    whichever metatile was loaded last (the rightmost even column of the previous row).
  - Even rows that start at an odd column also test a stale mask for the first column.
  - Effect: rectangles that cross metatiles can miss or invent blocked sub-tiles.
- **Possible bug (L):** in `World::FindThingAt` and `FindStaticObstacleAt`, the upper clamp of
  the box is applied only when the lower bound was not clamped.

## 7. Groups, squads, stuck handling (M)

- **No formations.** Groups share one point list. Only the arrival tolerance grows with group size
  (0x44ceb0), and their start is staggered over 3 turns (7 in multiplayer).
- **Friendly traffic:**
  - `IsBlockedAt` turns a blocked unit into a follower of the friendly unit ahead.
  - `OnBlocked` waits 9-12 ticks.
  - `OnBumped 0x49ff70`: an idle unit that is bumped computes a make-way point (`0x4a11e0` into
    +0x104) or nudges its stored point (+0x14c).
  - `ArriveOrGiveUp` can shove friendlies within 4·radius (`0x447810`) and sets bump retries
    +0x142 = 100.
- **Stuck counters:**
  - **+0x11c stall limit / patience:** reset to 14/28 on a block or failure, 48 when the target is
    blocked; it regrows by 10/18 per Think call.
  - **+0xbc detour tries:** starts at 4; reset to 4 when idle, set to 3 after a jitter.
  - **+0xc4 arrival tolerance:** grows by 25 per Think call while recovering and by 1764 per
    give-up step.
  - **+0x140 wait ticks.**
  - **More than 64 waypoints:** the order is aborted.
  - There is **no timeout**. "Giving up" is the tolerance growing until `CheckWaypointReached`
    passes, or `WaitOrCancel` popping all waypoints for passive units.
- **Squads:** `ACreature+0x28` = AISquad* (M). Squad members get a 64-expansion local stall limit
  instead of 144. AI code issues moves through `ExecuteMove` (callers 0x4afbe0, 0x4bb090, 0x462aa0).
- `AIRetina` / `AIRegion` / `AIRegionC` (0x4a8xxx-0x4adxxx) is the AI's region and threat map.
  It is not used by movement; ACreature +0xa0 holds an AIRegionC (M).

## 8. Debug hooks (H)

- **Developer console commands:** "guide on/off", "graph generate" (0x4a60b0), "obstacles
  on/off/generate" (0x4a8040) and "creature paths on/off" (ACreature vtbl+0x40, 0x4a29d0). They
  are dispatched from 0x4825a0 / 0x482a90 / 0x482ab0.
- **Display flags:** AIMovementGuide +4 (guide display) and AITerrainAnalysis +0x54 (obstacle
  display) are useful to visualise an overhaul.

## 9. Patch points for an overhaul

1. **Replace `ACreature::PlanNextLeg` 0x49d130** (`void __fastcall(ACreature*)`). This is the
   single place that turns "next waypoint" into steps.
   - **Contract:**
     - Read the waypoint stack +0xac (top `data[count-1]`; `data[0]` = final goal).
     - Write steps with `ACreatureBody::SetPath 0x404750(this, IStack<tPathStep>*)`.
     - **Step spacing:** steps must be at most `stepSize[type]` px apart (Chebyshev), because
       `Step` teleports one step per tick after a collision test. `dir` (0..7, N=0 clockwise) is
       the facing.
     - The stack top is the next step.
   - **Failure:** leave the stack empty and keep the give-up state machine (+0x11c, +0xbc, +0xc4,
     0x49daa0) or replace it. Think calls PlanNextLeg again every tick while the stack is empty,
     so it needs a rate limit.
   - **Callers:** `ACreature::Think 0x49bd50` and itself (recursion after a detour).
2. **Or replace only the searches, keeping the cascade:**
   - **`AILocalPathFinding::FindPath` 0x4a37a0**
     - Signature: thiscall `(this = creature+0x118, IStack<tPathStep>* out, IPixelC* start,
       IPixelC* goal)`, ret 0xc.
     - Out: data[0] = end, top = start; callers pop the start. Respect `g_PathAbort`.
     - **Partial results must be distinguishable**, otherwise the C-shape bug remains: fix the
       acceptance in 0x49d130 and 0x49e430.
   - **`AIMovementGuide_FindRoute` 0x4a47b0**
     - Signature: cdecl `(IList<IPixelC>* out, short radius, tGuidePoint* startGuide, IPixelC*
       goal)`, returns out. Route order: start guide first.
     - **Or replace `nAIAStar::Search` 0x4a6d40.** Keep the result node chain in
       `g_AStarNodes`, or return an `IStack<tPointState>`.
3. **Better heuristics with minimal change:**
   - In 0x4a3cc0 / 0x4a37a0, h is `dx²+dy²` clamped to 0xFFFF (the computation is in 0x4a3cc0 and
     at the start node in 0x4a37a0). Switch it to an octile distance in step units.
   - In 0x4a7200, return the square root (an admissible distance).
   - Raise the caps: 600 nodes at 0x4a3f12 (`cmp ax,0x258`), 999 at 0x4a396f, 1000 at 0x4a6f0a,
     and the 193² window, which needs a bigger grid array.
4. **Global plan gating:**
   - Call `GlobalPlan` before accepting a partial local path.
   - Make `Guide_FindNear 0x4a22f0` search wider (the 0x2400 limit), or run a tile-level A*
     over `tObstacleMap` plus World occupancy instead of the guide graph.
5. **Terrain bug:** fix the stale mask in `Terrain::IsRectBlocked` 0x42eb50 (reload the metatile
   mask whenever (x>>1) changes, in both row parities).
6. **Static graph:** rebuild or patch `tObstacleMap` and the guide links when buildings appear or
   disappear (see World's placement code, `World_ValidatePlacement` 0x4517e0 and 0x446db0).

## Open questions / uncertain

- Type classes 0x2c-0x3e / 0x3f-0x41 / 0x42 / other (class 0x15/0x16/?/0x17): which are
  buildings, pods and map objects? (L; the thing-types topic should settle it.) This decides
  whether buildings are in the guide graph (they appear not to be).
- Exact fog-map semantics of APlayer +0x26 and +0x2426 (M).
- `0x49cba0` sidestep geometry, `0x4a11e0` make-way, `0x4a0150` route drop and `0x4a57c0` line
  test were not traced in detail (L/M).
- `GetBehaviour 0x4a22b0` returns +0x44 (stance?) in the default case. Its value meanings were not
  confirmed (L).
- `0x407170` order lock (+0x79 < 0 and a human player) (L).
- How often `GameTurn` runs relative to the movement tick, and therefore the real time to finish
  `ProcessLinks` after "graph generate" (not traced; see the main-loop topic).
- `g_PathBudget` source in multiplayer (the meaning of the message payload) (L).

## 10. Findings from the Improved planner (src/pathing.cpp, 0.20.0)

Answers to the open questions above. All are confirmed in game by the pathing tests (`[Debug] PathTest`, autopilot mode 26; oracle = test 2), H unless marked.

- **Turn structure.** `GameTurn 0x463080`: `inc [cmd+0x238]`; `call 0x462550` (deferred moves, hooked at `0x463086` as the start of every turn); players; effects; things `0x449ab0` (one step per creature per turn); `AGrid::Tick 0x42dcf0`; `jmp 0x449f90`. Called from `0x45bc90` (multiplayer network turn) and `0x4603d0` (single player). Single-player turns run at about 20 a second (50 turns ≈ 2.43 s).
- **Clock writes** (used as the mission-start signal): reset `0x4630e0` from `0x48066e` and `0x481c6f`; set `0x4630f0(turn)` from `0x481d4d` (save load).
- **TestMoveTo 0x4020b0.** First `FindThingAt(selfId, x, y, radius, footprint/2, flyer)`, then terrain `IsRectBlocked` over tiles `[(x-r)>>5, (x+r)>>5] × [(y-r)>>5, (y+r)>>5]`, inclusive (a negative coordinate gives -1, which is blocked).
- **IsRectBlocked 0x42eb50** (thiscall on AGrid; `(ILogicalC* tl, ILogicalC* br, int flyer)`, `ret 0xc`).
  - Off the map is blocked; `flyer == 1` is free.
  - Mask bytes: byte0 = (even x, even y), byte1 = (odd x, even y), byte2 = (even x, odd y), byte3 = (odd x, odd y); bit 0 = blocked.
  - Even rows reload the mask at even x only. Odd rows never reload it.
  - Ids ≥ the table count (animated water 0x1a2..0x1a7) give mask 0, so they read as open.
  - A C++ emulation agrees with it at every sampled point. The fixed rule differs from stock at about 15,000 of a million sampled points on 4 Corners.
- **FindThingAt 0x44db90** (`self, x, y, r, halfFoot, layer`, `ret 0x18`).
  1. A thing registered (by its top-left, +0xa/+0xc) at the tile of `(x - halfFoot, y - halfFoot)` is a hit outright.
  2. Then it scans the tiles `[(x-r)>>5 - 2, (x+r)>>5] × [(y-r)>>5 - 2, (y+r)>>5]`. With the World "large thing" flag at the probe tile, the box is -6/-4 and +2/+1 instead.
  3. Each thing found is tested with its hit test, vtable +0x2c.
- **Hit tests (vtable +0x2c, `ICircle*` probe, strict `d² < (R + r)²`, exact in x87):**
  - `0x445ac0`: one circle of `g_ThingRadius[type]`.
  - `0x434df0` (AObstacle): type 0x43 ruins use circles per variant (+0x28 byte) at `[0x53df9c + v*16]` (12-byte records: dx +4, dy +6, r +8), count at +4. Other obstacles use the base test.
  - `0x430c80` (vtable 0x50437c, e.g. the Summoner): circles per device type at `[0x528264 + (type-0x2c)*16]`, same records.
  - `0x457dd0` (the Warp, vtable 0x504e14): its own circle, plus `[0x54c3d0]` extra circles at `[0x54c3cc] + i*8` (dx +4, dy +6), radii `[0x54c3dc] + i*2`. These reach about 170 px from the centre.
  - Corpses (0x42) and pods collide like buildings.
- **Fog (APlayer).** 64 px cells, 96 per row. +0x2426 byte: 1 = never seen. +0x26 byte: count of own units seeing the cell. +0x484c short: fog on. Visible = (explored) and (fog off, or count non-zero): `0x4a35e0`. Hidden = fog on and never seen: `0x4a3620`.
- **Step records.** `IStack<tPathStep>` at +0x90 is {vtbl, data +0x94, count +0x98, cap +0x9c}. tPathStep (16 bytes) is {vtbl 0x5031b4, IPixelC {vtbl 0x5031c4, x +8, y +0xa}, dir +0xc}.
  - `SetPath 0x404750` clears the blocked and follow fields (`0x4041e0`), then copies x, y, dir from the source stack (`0x40a420`, which only reads it).
  - The stack top (last entry) is the next step.
- **Facing** (StraightPath 0x49e4d0, table 0x503210 = {3,2,1,0,4,5,6,7}): `a = 2dx < dy`, `c = -dx > 2dy`, `d = (a^c) ? 2dx < -dy : dx > 2dy`, dir = `table[a*4 + c*2 + d]` (0 = north, clockwise).
- **OnBlocked** (vtable +0x90: ACreature `0x5093a4`, AShaman `0x504abc`, both `0x49bab0`) is `thiscall()`. **OnBumped** (+0xc4, `0x49ff70`) is `thiscall(bumper*)`, `ret 4`, called on the bumped unit. The wait counter +0x140 is a short. +0x148 is the building being entered (EnterBuilding 0x49f320), 0xc01 = none.
- **ExecuteMove 0x461b00** (`IdList*, PtList*, short tol`, `ret 0xc`). Fewer than 3 units: each creature's vtable +0xb4 (OrderMove) at once. 3 or more: the stagger queue. Fresh callers: `0x46045a`, `0x45ad24`, `0x4b014b`, `0x4bb21f`.
- **SetStance executor 0x462e20** (`stdcall(id, stance)`) ignores ids past the thing table. The multiplayer Execute calls it at `0x45bcb7`, with the stance as a sign-extended 16-bit word. The Improved mode's multiplayer hello uses it with id 0xffff.
- **Synced random state:** index byte `0x5792b4`, counter word `0x5792ac`. NewRandomSeed (`0x461430`) seeds the C runtime's `srand 0x4eccb0` from the time. Tests pin all three; two pinned runs give identical state hashes.
- **Building (AShaman).** Build (activity 0x2e) stores its site at +0x16c/+0x16e, its type at +0x184, and the building id at +0x18e (0xc01 until it exists).
  - The work counter +0x180 (short) is set to S = `0x443d10()` = `0x54c670[type]` when work starts, about 1200. It is -1 before then, and is decremented once per work tick (`0x4436b0`).
  - The building is created (`0x447410`) when `S - counter == 0x40`. If that fails, +0x170 counts the failure and the Shaman retries.
  - The Shaman works from where it stops, at its radius + 64 px from the site (IsAtWorkSite), on its approach side, facing the site. A working Shaman's animation state +0x88 stays 0: the build pose comes from the Shaman's own activity.
  - Creation collides with every object but the builder, using the device's shape circles (`0x528264`), which reach past its footprint.
  - The stock Build order clears the site of units standing on it when it is given; units arriving later still block it.
- **Repair executor** `0x4621b0` is `stdcall(builderId, targetId)`, `ret 8` (→ AShaman `0x441660`). Improved switches a group's other builders to it as soon as the lead's building stands.
- **ADevice construction step** `0x40c8a0` is `thiscall(int)`, `ret 4`; its callers pass 0. The autopilot used to call it without the argument, which unbalanced its stack (fixed in 0.20.0).
