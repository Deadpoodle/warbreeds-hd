# WarBreeds subsystem map

> New here? Start with [docs/reverse-engineering.md](../docs/reverse-engineering.md): the tools, methods and conventions, and a dated log of the investigations.

The index future work starts from: for each subsystem, the entry points and main functions of
`WARBREED.EXE` (GOG build, image base 0x400000), with a pointer to the detailed report.
Everything is from static analysis; nothing was run. Confidence: **H** read from the code (decompiler
checked against the disassembly), **M** mechanism clear but purpose inferred, **L** a guess to test.

- Names below are the ones in `re/names.tsv`, which is authoritative; struct fields are in
  `re/types.tsv`. Both are applied to the Ghidra project and the export `re/ghidra-export-v2/`
  (see `re/ghidra.md`). The detailed reports may use an older name for the same address.
- **Two "world" objects.** `World` in `src/game.h`, `re/hotkeys.md` and `re/mouse.md` is the object at
  `[0x579278]` (`g_World`), RTTI class **`AThingRegistry`**: the thing table, object grids,
  selection and groups. The static object at **0x55dd60** (`g_Map`) is RTTI class **`AGrid`**: the
  terrain cells, tile types, dirty buffers and map size (`0x566db4` = AGrid+0x9054). `re/wbm-format.md`
  calls 0x55dd60 "World" and [0x579278] "Things"; same objects.
- Other singletons: `g_Game` 0x567ef8 (the static `SGame` scene), `g_Players` [0x57927c]
  (`APlayerRegistry`), `[0x579280]` effects (`AEffectRegistry`), `[0x579284]` events
  (`AEventRegistry`), `g_LocalPlayer` [0x579288], `g_Campaign` [0x57928c], `g_Commands` [0x579260]
  (the command model: `CSinglePlayer` 0x5511c8 or `CMultiPlayer` 0x578d40).

| Subsystem | Report |
|---|---|
| Map loading, terrain, map format | `re/map-loading.md`, `re/wbm-format.md`, `re/tutorial-terrain.md`, `re/editor-original.md` |
| In-game map editor (WarBreeds HD) | `re/editor-shell.md` (shell, hooks, saving, New Map), `re/terrain-painter.md` (smart painter) |
| Save/load, campaign record | `re/save-load.md` |
| Pathfinding and movement | `re/pathfinding.md` |
| Players, AI, alliances, commands | `re/players-ai.md` |
| Units, stats, gene pool, rally points | `re/units.md`, `re/fogbuild.md` (placement, fog) |
| Scenes, widgets, main loop, input | `re/scenes.md`, `re/hotkeys.md`, `re/mouse.md` |

---

## 1. Map loading

**Shape (H).** A map (`.WBM`) is a flat little-endian dump of the whole game state, the same format
as a save (`.wbs`). It is loaded in stages 2..8, one per frame of the progress screen, by the same
serializer that saves (`IResourceStream+8`: 0 read, 1 write). Tags (`?`, `+`, `=`, `FX`, `!`, `ER`,
`CAMP`, `Z1`..) are not stored in the file; `.dbg` is a developer variant where they are Mohawk resources.

| Address | Name | Role |
|---|---|---|
| 0x4926c0 | `SProgress::Tick` | drives the load, one stage per tick |
| 0x46f930 | `Game_LoadStage` | stage dispatcher |
| 0x47ab30 | `SGame::LoadStage` | stage 0 HUD, 1 palette, 2 reset + `Game_CreateRegistries` + terrain set, 8 camera/minimap/view |
| 0x480610 | `SGame::LoadMapStage` | opens the stream (stage 2) and runs one serializer stage |
| 0x4812f0 | `SGame::SerializeStage` | the stage table (MapInfo, terrain, players, effects, things, events, campaign, clock/camera/waypoints) |
| 0x471c40 | `Game_CreateRegistries` | creates AThingRegistry (0x2d140 bytes), players, effects, events |
| 0x42d790 | `AGrid::LoadTerrainSet` | `terrain.hoo` + `TERRAIN.MHK` tile classes and types |
| 0x42e080 | `AGrid::Serialize` | cells (`u16[96*96]`, column-major), then AITerrainAnalysis with the precomputed movement guide (0x4a4670) |
| 0x450fa0 | `AThingRegistry::Serialize` | things: type, id, owner, x, y, then each thing's virtual Serialize (vtable +0x48) |
| 0x446db0 | `AThingRegistry::CreateAt` | creates and places a thing (load uses force=1) |
| 0x46bee0 / 0x46c8f0 | `IResourceStream` ctor / `OpenTag` | stream backend chosen by extension |

**For the map editor (H unless noted):** the developer console table `g_ConsoleCommands` 0x507360 already
has `save map`, `load map`, `process map` (AI guide rebuild), `save/load terrain`, `place`, `makemap`,
`quake`, terrain-type and grid overlays. After editing: a tile change only needs its cell marked dirty
(terrain queries read tile ids live); a passability change also needs `AITerrainAnalysis::Rebuild`
0x4a8030; size changes go through `AGrid::SetMapSize` 0x42d740; objects through `CreateAt` /
`AThingRegistry::RemoveThing` 0x4481e0; then `Minimap::Refresh` 0x433820. Full table:
`re/map-loading.md` §9. Saving is `SGame::SaveGame` 0x480be0 with a `.wbm` path.

Uncertain: AGrid+0x9056, whether CreateAt's x/y is the corner or the centre for every class (M), the
minimap object 0x5517b0 (no RTTI, M).

## 2. Save / load game

**Save (H).** Esc menu `SMenu` (scene 7) → `SSaveDialog` (scene 0x1c) → `SSaveDialog::OnFileChosen`
0x494030 sends SGame message 1 → `SGame::SetParam` 0x4803e0 (codes 0 load, 1 save, 2 set skirmish
flag) → `SGame::RequestSave` 0x480ac0 (deferred to `SGame::Tick` 0x47ce80; `DATA\Saves\<name>.wbs`,
`SOverwrite` if it exists) → `SGame::SaveGame` 0x480be0 → `SGame::SerializeAllStages` 0x4812c0 →
`SerializeStage` 0x4812f0 for stages 2..8.

**Load (H).** `SOpenDialog` sends message 0 (path at SGame+0x1fe, flag +0x1fc) → `Game_StartCommandModel`
0x470780 (single-player model, `SProgress` as root) → the map-loading stages above. Players and AI
brains are saved (`APlayerRegistry::Serialize` 0x43ae20, `AIComputerPlayer::SerializeBrain` 0x4b7280),
including each player's alliance mask.

**Where skirmish save is disabled (H).** `SMenu::Init` at **0x4895a8**: `call Game_IsSkirmish 0x46f960;
test ax,ax; jne 0x48968c` skips creating the Save/Load buttons. `Game_IsSkirmish` reads
`SGame+0x82` (word 0x567f7a), set to 1 only by `SMultiSetup::StartSkirmish` 0x48d440 (at 0x48d556)
and cleared by the campaign briefing (0x48a1b2) and main-menu loads (0x487bc4). The save code itself
never checks it; multiplayer is excluded separately at 0x489512.
A skirmish save would also need: the skirmish flag (restored without re-running
`SGame::ApplyStartSettings` 0x47aff0 / `APlayerRegistry::AssignPlayers` 0x43bd40 at stage 8), the
local-player index, the session/GameSettings block (`CommandModel+4`) and the original map name.
Whether the saved AI brain is enough for the AI to keep playing after a load is **L** (test in game).

## 3. Pathfinding and unit movement

**Verdict: the C-shape hypothesis is confirmed (H).** Both planners are greedy best-first searches
(squared-distance heuristic) with hard caps, and both return the path to the closest node reached;
the caller accepts that partial path, so a unit walks into a pocket and then finds no way out.

| Address | Name | Role |
|---|---|---|
| 0x44ccd0 | `AThingRegistry::MoveSelection` | right-click move → `g_Commands` Move (slot 0x34) |
| 0x460430 / 0x461b00 | `CSinglePlayer::Move` / `Cmd_ExecMove` | groups of 3+ are queued, delayed `turn+1+i%3` (`%7` MP) |
| 0x44ceb0 | `AThingRegistry::GroupArrivalTolerance` | no formations: one shared point list, a tolerance growing with group size |
| 0x49f0c0 | `ACreature::Move` (vtable +0xb4) | loads the waypoint stack (+0xac; data +0xb0, count +0xb4, `data[0]` = goal) |
| 0x49b6b0 → 0x49bd50 | `ACreature::Act` → `ACreature::Think` | per tick |
| **0x49d130** | `ACreature::PlanNextLeg` | the planning cascade: straight line → local A* → global plan; accepts partial paths |
| **0x4a37a0** | `AILocalPathFinding::FindPath` | grid search: caps 600 nodes, 1000 expansions, ±96-cell window, stall limit `creature+0x11c` (144/172/64, down to 48/28/14 after failures) |
| 0x49df30 | `ACreature::GlobalPlan` | needs a guide point within 96 px (0x4a22f0) |
| **0x4a6d40** | `nAIAStar::Search` | guide-graph search: 1000 expansions, stall 68 (45 MP); returns the best node |
| 0x4a47b0 | `AIMovementGuide_FindRoute` | global route over guide points |
| 0x4a4eb0 / 0x4a5200 | `AIMovementGuide::Build` / `ConnectStep` | guide points at convex corners of static obstacles; links ≤ 900 px, 200 checks per turn; saved with the map, never rebuilt in play |
| 0x404750 / 0x404200 | `ACreatureBody::SetPath` / `Step` | step stack at +0x90; one `stepSize[type]` (0x54d140) per tick |
| 0x4020b0 | `ACreatureBody::TestMoveTo` | collision: things + `AGrid::CheckSite` 0x42eb50 |
| 0x49bab0 | `ACreature::OnBlocked` | waits 9-12 ticks or replans; sidesteps, then re-issues with ±4 px jitter; no timeout |
| 0x51bedc | `g_PathBudget` | path-effort setting (default 16) scaling the stall limits; MP sets it by message |

Patch points for an overhaul (contracts in `re/pathfinding.md` §9): replace `PlanNextLeg` 0x49d130
(read the waypoint stack, output steps ≤ one stepSize apart through `SetPath`), or replace
`FindPath` 0x4a37a0 / `nAIAStar::Search` 0x4a6d40 and stop accepting partial paths. Buildings appear
not to be in the guide graph (M). Hidden (fogged) cells count as passable during planning (M).

## 4. Players, AI and alliances

**Players (H).** `APlayerRegistry` [0x57927c] (size 0x30) holds the `APlayer` objects (size 0x4a4c;
`AIComputerPlayer` 0x4e60 derives from it). Key APlayer fields: `+0x18` one-way ally bitmask,
`+0x19` chat mask, `+0x1a` seen-enemy mask, `+0x26`/`+0x2426` fog maps (96x96), `+0x4850` CGK levels
(int[4], one per clan), `+0x4a30` team byte. `APlayer::CanBuild` 0x437b50.

**Alliances (H).** Every test is `viewer->allyMask(+0x18) & g_AllianceTable[other->id]` (bit table
0x53e0e8; 25 readers listed in `re/players-ai.md` §2.3: auto-targeting 0x44f8d0, turret/mine/creature
target acquisition, retaliation, cursors, AI target search 0x453d20, AI threat map, victory check).
Changed by `Cmd_SetAlly` 0x462240 (command slot +0x94, MP opcode 0x3b) and by
`APlayerRegistry::ApplyTeams` 0x43c8f0 (MP teams). In single player
`APlayerRegistry::AssignPlayers` 0x43bd40 allies every computer player with every other one
(0x43c06f..0x43c0b3); the human never gets allies. The `SAllies` dialog (scene 0xc) opens only in
multiplayer, from `SMenu::OnCommand` 0x489830. Vision is never shared (0x448730); minimap colours are
per owner.

**Skirmish alliances plan (M):** show the per-slot team in `SMultiSetup` (it is both the skirmish
setup and the MP lobby; slots are `tPlayerSlot[8]` at `CommandModel+4`, team at slot+0x1e), then after
`APlayerRegistry::SetupSkirmish` 0x43bd30 returns (call site 0x47aec9 in `SGame::LoadStage`) reset each
`+0x18` to its own bit, copy teams to `+0x4a30` and call `ApplyTeams`. Optionally patch the
single-player branch of `APlayerRegistry::PlayerDefeated` 0x43b780.

**Victory (H).** A player with no creatures or devices is out (`AThingRegistry::PlayerHasUnitsOrDevices`
0x4536a0); the game ends when all remaining players are mutually allied or one is left
(`APlayerRegistry::TickPlayers` 0x43a900). Team victory already works.

**AI (M).** `AIComputerPlayer::Think` 0x4b5fe0: squads (`AISquad`, 0xb8 bytes), the AIRetina region
threat map, the base manager, then `AssignSquadGoals` 0x4b97b0. The AI calls the `Cmd_Exec*`
executors directly. There is no skirmish difficulty: the "AI Options" slider (0x53e0fc) only scales
damage taken by the human's things. Aggression per clan in 0x43c2f0 and `AAction_SetAggression`.

**Command model (H).** `CommandModel` (vtable 0x5053e4, 54 slots, all mapped in
`re/players-ai.md` §6 and `re/types.tsv` `CommandModel_vftable`): `CSinglePlayer` runs commands at
once; `CMultiPlayer` serialises them into a `CommandList` (opcode byte + 16-bit words), sends it
(`CMultiPlayer::Send` 0x45d6e0) and every machine runs it in `CommandList::Execute` 0x45ab10.
`CommandModel::SimulateTurn` 0x463080 is the turn step. Lockstep scheduling in the network layer
(0x4c6xxx) is not traced.

## 5. Unit types and stats

**Hierarchy (H).** `AThing` (vtable 0x504bb4, 31 slots, base size 0x28) → `ADevice` (buildings,
0x40) → ABattery, ACollector, AElectricTower, AGeneLab (0x228), AHowitzer, AMine, ARadar, ARefinery
(0x98), ARelay, ARevitalizer, ATurret, AWarp; `AThing` → `ACreatureBody` → `ACreature` (0x154) →
`AShaman` (0x1b8); `AObstacle`, `AMushroom` (pods, 0x34), `ADeath`. Factory `World_NewThing` 0x447a90
(size, ctor and dtor per type id 0..0x49). Main slots: Draw +0x04, Tick +0x08, panel draw/click
+0x14/+0x18/+0x1c, TakeDamage +0x44, Serialize +0x48, MaxHP +0x50, SightRange +0x54; creature orders
+0xac..+0xc8. Every class's vtable is in `re/ghidra-export-v2/classes.tsv`.

**Stats (H).** `DATA/GAMEPLAY.DAT` is a headerless dump of 69 fixed arrays read in order by
`GameData_SerializeTables` 0x4651c0 (footprint `g_FootprintSize` 0x54e370, radius, sight, max HP
`g_MaxHp` 0x54cdb0, build/breed time, armour[3], tier, population cost, gizmo requirements);
`DEVICE.DTH` by 0x465fb0 (record meaning M) and per-creature `.skt` gizmo attachment points.
Max HP grows with veteran level (+0x7c). Several per-type tables are unexplained (0x54d2f0, 0x54eca0,
0x54ed20, 0x54e920, 0x54c638, 0x54cb50, 0x54c9c8) — L.

## 6. Gene pool and rally points

**Gene pool (H).** Per-clan technology on the player: genes collected `APlayer+0x486e` (short[4]) and
levels `+0x4850` (int[4], "CGK" = Clan Genetic Knowledge); genes per level {3,4,6,8,9} (0x53e0f0).
A Shaman in Get Genes mode collects remains of other clans' creatures: command +0x60 →
`AShaman::GetGenes` 0x441500 → `AShaman::FinishWork` 0x441790 → `APlayer::AddGene` 0x4375f0. Levels
unlock the 25 gizmos (`APlayer::HasGizmoTech` 0x43a420; art in `GIZMO?.MHK`). A creature design is
(type, gizmo[4], count) sent to one Gene Lab (command +0x68); there is no design library, each lab
saves its current design.

**Rally points (H): only Gene Lab / Gene Lab+.** Path list at AGeneLab+0x208 (data +0x20c, count
+0x210), patrol point +0x218. Set by selecting the lab and giving a move order:
`AThingRegistry::MoveSelection` 0x44ccd0 → command +0x4c (`CSinglePlayer::SetRally` 0x4604c0) →
`Cmd_ExecSetRally` 0x461c00 → `AGeneLab::SetRally` 0x429ce0. A new creature:
`AGeneLab::SpawnCreature` 0x429350 → PlaceCreature, then `ACreature::Patrol` 0x49f1e0 or Move.
AWarp only has a teleport destination (+0x46/+0x48, command +0x54).

## 7. Campaign progress record

**H:** the campaign definitions are read-only `CAMP`/`CHAP`/`CHMV`/`MSET`/`MISS` resources in
`DATA/MISSION.MHK` (`ACampaign` → `AChapter` → `AMissionSet` → `AMission`). Progress is three numbers
(campaign id 1000..1002, chapter index, mission-set index), held only in memory (`g_Campaign`
[0x57928c]) and inside a `.wbs` save (`SGame::SerializeCampaign` 0x482150: `CMID`/`CRCH`/`CRMS`).
Nothing is written between missions (no progress file, registry or `WARBREED.CFG` entry). CGK levels,
veteran status and gizmos do not carry over: each mission's script sets them (`AAction_CGK`,
`AAction_VeteranStatus`, `AAction_SetGizmo`). `SReward::Continue` 0x493740 advances after a win.
Uncertain: which campaign ids are which; the `CAMP` 0x100 flag (L).

## 8. Scene and menu system

**H.** Every scene is a static object; `Scene_GetById` 0x4713c0 maps ids 2..0x2b to them (table in
`re/scenes.md` §4). The stack is `g_Scenes` 0x55aea8 (`Scene *[]`, 3-4 entries, **no bounds check**)
with top index `g_SceneTop` 0x51bee8. `Scenes_Push` 0x471580, `Scenes_Pop` 0x471690,
`Scenes_Replace` 0x471750, `Scenes_ReplaceRoot` 0x471790. Menus queue changes with
`QueueSceneChange` 0x4709a0(cmd, id, delay) (0x1f push, 0x20 pop to root, 0x21 replace root, 0x22
replace, 0x23 pop, 0x25 quit), run from `LogicTick` via `Scenes_ProcessCommand` 0x4717e0;
parameters go first through `Scene_Send` 0x471560 (vtable slot 0x58 SetParam).
`[0x5792dc]` (`g_SceneCmdPending`) is the "scene command pending" flag (`re/mouse.md` calls it an input block).

`Scene` (vtable 0x508bec, 32 slots, size 0x38): 0x14 Init, 0x18 Close, 0x34/0x38 key down/up,
0x40 OnStart, 0x48 Invalidate, 0x4c OnResize, 0x50/0x54 Activate/Deactivate, 0x58 SetParam, 0x6c
OnCommand(Widget*). `Widget` (0x5092a4, 17 slots, 0x2c bytes). `SGame` (vtable 0x507534, static at
0x567ef8, 0x10464 bytes): camera +0x38/+0x3a, scroll bounds +0x44..+0x4a, view +0x4c/+0x4e, zoom +0x5e,
skirmish flag +0x82, turn period +0x188 (25 ms SP / 40 ms MP: the game-speed knob), pause +0x460.

**Main loop (H).** `WinMain` 0x4743d0 runs a GetMessage loop; all work runs in Mohawk timer tasks
fired from a message hook (0x4e447f) on a 1 ms timer: `FrameTick` 0x46f400 (compose),
`PresentTick` 0x46f570, `LogicTick` 0x46f6b0 (scene Tick + scene-command countdown). The simulation
advances in `SGame::Tick` 0x47ce80 → `SGame::IsTurnDue` 0x47d090 / `SGame::RunTurn` 0x47d100.
Task periods vs the real frame rate were not measured (L).

## 9. Input

Covered in depth by `re/hotkeys.md` and `re/mouse.md`; the WndProc map is in `re/scenes.md` §2.
Entry points (H): WndProc jump tables `WndProc_MsgTable` 0x47399c (WM_KEYDOWN..WM_SYSKEYUP) and
0x4739b4 (0x202..0x205); `WndProc_KeyDown` 0x473b90 → `scenes[top]->vtbl[0x34](vk, mods, ascii)`;
`BuildKeyEvent` 0x4e47b8 (mods bit0 Ctrl, bit1 Alt, bit2 Shift). Mission: `SGame::OnKeyDown` 0x47f000,
`SGame::LButtonDown` 0x47d500, `SGame::LButtonUp` 0x47e430, `SGame::RButtonUp` 0x47e6f0,
`AThingRegistry::Click` 0x44a860, `AThingRegistry::OrderAt` 0x44c6d0. Cursor position
0x5794d8/0x5794dc.

## 10. Crash and bug leads

| Lead | Where | Confidence |
|---|---|---|
| Animated water frames (ids 0x1a2..0x1a7) are past the 418-entry terrain table, so they read as open ground ~40 % of the time: placement and maybe movement accept water | `AGrid::TerrainTypeAt` 0x42ea40, `AGrid::CheckSite` 0x42eb50, `TileAnimator::Tick` 0x455220 | M |
| `AGrid::CheckSite` tests a stale metatile mask on odd tile rows (0x42ec81..0x42ecb6): collision misses or invents blocked sub-tiles | 0x42eb50 | H |
| Units stuck in concave obstacles; endless sidestep/retry loop with no timeout | §3 | H |
| Scene stack has no bounds check | `Scenes_Push` 0x471580 | M |
| Repair/Upgrade target searches use a screen-sized rectangle (a widescreen gameplay side effect) | 0x4427d0 in `AShaman::CanRepairTarget` 0x442780, 0x442900 in `AShaman::CanUpgradeTarget` 0x4428b0 | M |
| Stat category 11 appears to index `g_MaxHp[type-0x3f]` (wrong table?) | `re/units.md` §2 | L |
