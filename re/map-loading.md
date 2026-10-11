# Map loading, the terrain grid and the map file format

> Research report (static analysis, nothing run). Confidence: **H** read from the code, **M** mechanism clear but purpose inferred, **L** a guess. Addresses are authoritative; where a name here differs from `re/names.tsv`, the TSV is the current one (and the export `re/ghidra-export-v2/` uses it). Index: `re/subsystems.md`.


Static analysis of `WARBREED.EXE` (GOG build, image base 0x400000): `re/ghidra-export/` (decompiler output), `re/text.asm` (disassembly), the RTTI table, plus a small parser run against the shipped `.WBM` maps in `testgame/CAMPAIGN/` and `testgame/DATA/MAPS/`. Nothing was run.

Confidence: **H** = read from the instructions (disassembly checked, or the file layout confirmed against real map files). **M** = the mechanism is clear but the purpose is inferred. **L** = a guess.

## 0. Naming: there are two "world" objects

| object | where | RTTI class | what it holds | names used here |
|---|---|---|---|---|
| global at **0x55dd60** | static, built by `0x46e730` → `0x42d510` | **AGrid** (vtable 0x504334, base ISurface) | terrain cells, tile types, dirty-cell buffers, tile animation, quake, game clock, AI terrain analysis pointer | `AGrid::`, `g_Grid` |
| object at **[0x579278]** | heap, 0x2d140 bytes, built by `0x471c40` | **AThingRegistry** (vtable 0x504cb4) | thing table, object grids, selection, groups | `AThingRegistry::` |

- 0x55dd60 has been called "World" in passing (e.g. in `re/tutorial-terrain.md`), as if it were non-polymorphic with no RTTI. That is not right: the constructor `0x42d510` stores the AGrid vtable 0x504334 at +0, the destructor `0x42f4a0` stores it again, and RTTI names the class `AGrid : ISurface`. **H**
- `re/mouse.md`, `re/hotkeys.md`, `re/fogbuild.md`, `src/game.h` and `re/names.tsv` use **"World" for [0x579278]**: selection +0x80/+0x84, thing table +0x10/+0x14, `World::CanPlace 0x4517e0` and so on. That object is the RTTI class **AThingRegistry**. The selection list (+0x80/+0x84) belongs to it, not to 0x55dd60. **H**
- Convention (decided in `re/names.tsv`): methods of [0x579278] are `AThingRegistry::…` (the RTTI name; `World` in `src/game.h` is an alias for it), the global at 0x55dd60 is `g_Map` of type `AGrid`, and "World" is never used for 0x55dd60.

## 1. Overview

A mission or skirmish map is a **`.WBM` file: a flat, little-endian dump of the whole game state**. It has exactly the same format as a saved game (`.wbs`). The game loads it in 7 "stages" (2..8), one per frame of the progress screen. Each stage clears a subsystem and fills it from the stream:

```
SProgress tick (0x4926c0, SProgress vtable slot 4), stage 0..8, one per tick
  -> Game_LoadStage 0x46f930(stage)          (stage 0 also: 0x4713a0, Cmd->vtbl[0xbc])
     -> SGame::SetupStage 0x47ab30(&g_SGame 0x567ef8, stage)
          stage 0: HUD widgets from GAME.MHK;  stage 1: palette / sounds
          stage 2: reset SGame camera/UI state, Game_CreateRegistries 0x471c40,
                   AGrid::LoadTerrainSet 0x42d790 (terrain.hoo + TERRAIN.MHK)
          stages 2..8: SGame::LoadMapStage 0x480610(path, stage)
               stage 2 opens the file: stream = new 0x1a4-byte IResourceStream (0x46bee0, mode 0 = read)
               -> SGame::SerializeStage 0x4812f0(stream, stage)      (same code saves; stream+8: 0 read, 1 write)
               stage 8 closes the stream, picks the local player [0x579288], centres the camera
          stage 8: minimap fit 0x432e50, SGame::SetViewSize 0x47b520, camera to the start position
```

The per-stage contents of `SGame::SerializeStage 0x4812f0` (**H**, decompiled and checked against the tag strings):

| stage | tag(s) | content | reader |
|---|---|---|---|
| 2 | `?` | **MapInfo** (version, players, size, name), global 0x567578 | `MapInfo::Serialize 0x432330` (minimap reset `0x433fb0` first) |
| 2 | `+` | **terrain**: AGrid, then AITerrainAnalysis | `AGrid::Serialize 0x42e080` (AGrid vtable slot 6) |
| 3 | `=`, `FX` | players (first part), then effects | APlayerRegistry vtbl[0] `0x43ae20`; AEffectRegistry vtbl[0] `0x425160` |
| 4 | `!` | **things** (all units, buildings, pods, rocks/trees) | `AThingRegistry::Serialize 0x450fa0` (vtbl[0]) |
| 5 | `ER` | **events** (triggers, conditions, actions) | `AEventRegistry::Serialize 0x41d940` (vtbl[1]); then, on read, `APlayerRegistry 0x43ba20` (each player with +0x48f6==1 → vtbl[2]); on write `0x43cc90` (AI players → `0x4b9c60`) |
| 6 | `=` | players (second part) | APlayerRegistry vtbl[1] `0x43b460`; on read, Cmd `0x4630e0` |
| 7 | `CAMP` | campaign link: `CFLG` short, then `CMID` (campaign id 1000..1002) and the ACampaign [0x57928c] | `0x482150` |
| 8 | `CLOCK`, `Z1`..`Z4`, `PW` | game clock (Cmd), camera rect, SGame waypoint lists (+0x1a8 list of 0x14-byte entries, `0x485a10`) | inline in `0x4812f0`; on read also `AThingRegistry::RegisterAllWithPlayers 0x4473d0`, `Minimap::Refresh 0x433820`, console "Map loaded." |

Save is the same code with a write stream: `SGame::SaveMapFile 0x480be0(path)` sets the MapInfo name from the file name (`0x432150`), opens `0x46bee0(&s, 1, path, 1)`, runs all stages through `0x4812c0` and prints "Map Saved". It is deferred: `SGame::RequestSave 0x480ac0` stores the path at SGame+0x88 and sets +0x86. The SGame tick calls the save at `0x47cfe4`. **H**

Other readers of the same format: the skirmish/multiplayer setup preview `0x48ef70` (reads `?`, `+`, `=` only, for the preview minimap), `SMultiplayerOption` slot 22 `0x48b5c0`, `0x490f70`. **M**

## 2. The `.WBM` / `.wbs` / `.dbg` file format (H for the parts listed)

`IResourceStream` (`0x46bee0`, vtables 0x503244/0x503234/0x50322c) chooses a backend from the file extension:
- `wbm`, `wbs`: a plain file. Sub-blocks opened with `Stream_OpenTag 0x46c8f0(stream, &sub, "tag")` **share the file position**, so the tag names are not stored. The file is the sequence of raw values in the order the serializers touch them.
- `dbg` (`Map.Dbg`, `E3Multi.dbg`, stream+0x14 = 1): a Mohawk archive where each tag is its own named resource (INameMapper). This is a developer format.
- All values are **little-endian**. Integers are written at their natural size: `0x408f00(p, n)` raw bytes, `0x409030`/`0x409070` u16, `0x409050`/`0x4090a0` u32, `0x4090c0` u16. The direction comes from stream+8 (0 read, 1 write).

Layout, confirmed by a parser on SPMAG2B, SPMAG12A, SPTAN1A, MPMAP1 and MPMAP25 (all parse to ITA version 4):

```
MapInfo ("?", 0x432330)
  u16 version            = 5 (anything else → error 1 via 0x4eba00)
  u16 supportedPlayers   (+2; console "supports N" → 0x432100)
  u16 recommendedPlayers (+4; console "recommended N" → 0x4320c0)
  u16 widthCells, heightCells   (map size in 64 px cells, = tiles/2)
  u16 nameLen; u8 name[nameLen] (NUL-terminated)
AGrid ("+", 0x42e080)
  u8  tilesW ("4"), u8 tilesH ("5")          map size in 32 px tiles (max 192)
  u16 cells[96*96] ("6")                     COLUMN-major: for x in 0..95: for y in 0..95: cells[y*96+x]
                                             (always 96x96, also for small maps)
  u16 quakeForce ("GQF", +0x904a)
  u8  ("GSS", +0x9056, unused)
  u32 stepCounter ("GSC", +0x9058; non-zero in shipped maps: they were saved from a running game)
  u8  quakeShake ("GQS", +0x93b0)
  u16 hasClock ("GAGC"); u32 clockRemaining ("GGCC"); u16 clockFiresTrigger ("GGCST"); u8 clockTrigger ("GGCTI")
  AITerrainAnalysis ("7", vtbl[0] 0x4a7da0):
     u16 version (=4); if 4 (or when writing): AIMovementGuide data (vtbl 0x50942c slot 0 0x4a4670)
     otherwise a yes/no message box (0x4719b0(3,2,1)) and, on yes, a rebuild 0x4a8030
Players (stage 3), Effects, Things (stage 4), Events (stage 5), Players (stage 6), CAMP (7), stage 8 data
```

Things (stage 4, `AThingRegistry::Serialize 0x450fa0`, **M** for the field meanings):
- 8 group lists: each is `FMSZ` u32 count, then count × u32 (the lists at AThingRegistry+0x8c+16n).
- `#` u32 thing-slot count. On read the registry is cleared first (`AThingRegistry::Clear 0x44a0b0`).
- Per slot: `A` u16 type. **0x4a** = empty slot, pushed as a null so ids stay stable. Otherwise `B` u16 id, `C` u16 owner player index (default 10), `D` u16 x, `E` u16 y. Then `CreateAt 0x446db0(owner, type, x, y, force=1, notify=0, 0xc01)`, and the new thing's own serializer `vtbl[0x48/4]` reads the rest.
- After reading: `AGrid::SpawnTileAmbience 0x42da30` (effects on tiles 0xe8..0xea) and `AThingRegistry::PostLoadLink 0x453a30` (links pods to Refineries; devices without a power provider re-run vtbl[0x90] 32 times).

Events (stage 5, `AEventRegistry::Serialize 0x41d940` → `AEvent::Serialize 0x41d000`), **H** for the structure:
- 8 × u16 trigger flags (AEventRegistry+0x14), then u16 event count, then per AEvent (0x28 bytes, ctor `0x41cce0`):
  - `AJ` u16 (+0x20);
  - `C0` u16 condition count, then per condition `CT` u8 type → `ConditionFactory 0x4123d0`, then the condition's `vtbl[0x18/4]` serializer;
  - `E0` u16 action count, then per action `AT` u8 type → `ActionFactory 0x412600`, then `vtbl[0x18/4]`.
- Condition types: 0 Timed, 1 Position, 2 PlayerDeath, 3 PlayerCGK, 4 PlayerDevices, 5 PlayerSeen, 6 Trigger, 7 Rect, 8 ThingMissing (ACondition_*).
- Action types: 0 AddCreature, 1 Effect, 2 GameClock, 3 Audio, 4 CDAudio, 5 Capture, 6 Warp, 7 Quake, 8 VeteranStatus, 9 Heal, 0xa Trigger, 0xb PlayerEnd, 0xc Message, 0xd Script, 0xe Remove, 0xf SetGizmo, 0x10 SetAggression, 0x11 SetBounds, 0x12 SetImportance, 0x13 SetSquadChanging, 0x14 **CopyTerrain**, 0x15 OKDialog, 0x16 MoveIntoView, 0x17 RevealMap, 0x18 Move2Point, 0x19 Warp2Point, 0x1a SetMobility, 0x1b CGK (AAction_*). Each factory case was matched to its RTTI vtable through the ctor. **H**

The official editor (`wbedit/`) saves the same `.WBM`. Its readme says only that maps are `.WBM` files placed in `DATA\MAPS`, so the format above comes from the game's own code.

## 3. AGrid (g_Grid at 0x55dd60): layout and methods

Size 0x93b8 (the last field the ctor initialises is +0x93b4; another global starts at 0x567118). **M** for the size.

- **Cells.** `u16 cells[96][96]` at +0x08 (**0x55dd68**). Index `cy*96 + cx`, where a cell is 64 px and `cx = tileX>>1`.
- **Tile ids and types.** A cell holds a *tile id* (0..0x1a1 for terrain, 0x1a2..0x1a7 for animated frames). The id indexes:
  - the terrain picture set `g_TerrainPictures` 0x550740 (picture = `base + index[id]*4`, `0x434060`);
  - the **terrain-type table** at +0x93a0, an `IList<u32>` (data **0x567104**, count **0x567108**) loaded from `DATA\TERRAIN.HOO`.
- **terrain.hoo.** u32 count = 418, then 418 × u32. Each u32 packs four u8 terrain types for the 32 px quadrants of the 64 px tile: byte0 top-left, byte1 top-right, byte2 bottom-left, byte3 bottom-right. **H** (file and `0x42ea40`)
- **Terrain types.**
  - 0 = open;
  - 1 = blocked (bit 0: no building, `IsAreaBlocked`);
  - 2 = cropland / pod soil (pods require it, `CreateAt` and `AShaman_CanPlantAt`). **M** for "cropland".
  - The AI obstacle map treats both 1 and 2 as obstacles (`0x4a7a70`). **H**
- **Tile class.** `u16 tileClass[418]` at +0x905c is built from `TERRAIN.MHK`:
  - `AGrid::LoadTileClasses 0x42db60` reads 222 `TRID` resources (ids 1000..1221); the first BE u16 is the class.
  - It also reads 222 `TRVA` resources through temporary `ATerrainTile` objects: the variant tile-id list of each terrain family, with weights.
  - `tileClass[id] = TRID[family]` for every id in the family. Class 5 = animated (water-like) tiles. **M**
- No height map: WarBreeds terrain is flat tiles plus per-quadrant types. **H** (no other per-cell arrays exist)

Fields: see `re/types.tsv`. The important ones:

| off | VA | type | meaning |
|---|---|---|---|
| +0x04 | 0x55dd64 | AITerrainAnalysis* | AI obstacle map + movement guide (0x58 bytes), rebuilt per map |
| +0x08 | 0x55dd68 | u16[96*96] | tile id per 64 px cell |
| +0x4808 | 0x562568 | TileAnimator (0x40) | water animation (lists of 10-byte TileAnimID) |
| +0x4848 | 0x5625a8 | u8[2][96*96] | **dirty cells**, double-buffered |
| +0x9048 | 0x566da8 | u8 | current dirty buffer (0/1) |
| +0x904a | | u16 | quake force accumulator |
| +0x904c | | IPixelC | invalidation origin (+0x9050/+0x9052 = camera x/y), set by `0x42f290` |
| +0x9054/5 | **0x566db4/5** | u8, u8 | **MapTilesW / MapTilesH** (32 px tiles, ctor default 192) |
| +0x9058 | | u32 | simulation step counter |
| +0x905c | 0x566dbc | u16[418] | tile class per tile id |
| +0x93a0 | 0x567100 | IList<u32> | terrain-type quads per tile id (data 0x567104, count 0x567108) |
| +0x93b0 | 0x567110 | u8 | quake shake countdown (non-zero forces a full redraw, see tutorial-terrain.md) |
| +0x93b4 | 0x567114 | AGameClock* | mission countdown clock (AAction_GameClock) |

### Main methods (all thiscall on 0x55dd60, **H** unless marked)

Load and reset:
- `AGrid::AGrid 0x42d510`. Static construction via `0x46e730`, `atexit(0x46e750 → ~AGrid 0x42f4a0)`. It zeroes the cells, marks all dirty and sets up the tile animator (base id 0x1a2, stride 2, frames {1,2,1}, 40 % of animatable cells active).
- `AGrid::SetMapSize 0x42d740(u8 w, u8 h)`: sets +0x9054/5 and resizes the AI maps (`AITerrainAnalysis::Resize 0x4a8010`).
- `AGrid::LoadTerrainSet 0x42d790`, at stage 2:
  1. reads terrain.hoo (`ReadTerrainTypes 0x42e960`);
  2. reads TERRAIN.MHK (`0x42db60`);
  3. deletes and recreates the AITerrainAnalysis for the *current* size.
- `AGrid::Serialize 0x42e080(stream)`, vtable slot 6:
  - Saving: animation is stopped first (`TileAnimator::Stop 0x455420` restores base ids) and restarted after.
  - Loading:
    - recreates the AITerrainAnalysis with the map size just read;
    - rebuilds the water-animation list (`AGrid::BuildTileAnimList 0x42ff30`: class-5 cells with ids 0x13e/0x14b/0x158 → variants 0/1/2);
    - restarts the game clock;
    - reads the AI analysis (`0x4a7da0`).
- `AGrid::ReadTerrainTypes 0x42e960` and `WriteTerrainTypes 0x42e8d0` (terrain.hoo). Console "load terrain" `0x483020` and "save terrain" `0x482fa0`.

Terrain queries (32 px tile coordinates in an ILogicalC {vptr 0x5030ec, short x, short y}):
- `AGrid::TerrainTypeAt 0x42ea40(ILogicalC*)` → 0/1/2.
  - Outside the map it returns 1.
  - A tile id ≥ the table count gives 0. 13 callers: placement, pods, creatures, AI.
- `AGrid::IsAreaBlocked 0x42eb50(ILogicalC* tl, ILogicalC* br, int skip)` → 1 if any quadrant in the tile rectangle has bit 0 set (type 1), or the rectangle leaves the map. `skip == 1` returns 0.
- `AGrid::IsFootprintBlocked 0x42ecf0(obj, skip)`: a box of half-size obj+8 around obj+4/+6 (px) → IsAreaBlocked. Used by `0x44e5c0` (footprint terrain check in CanPlace and CreateAt).
- `AGrid::CellAt 0x434080(short cy, short cx)`, `AGrid::TileClass 0x42fed0(id)`, `AGrid::SurfaceKindAt 0x42fea0(px, py)` (class → 1/2/3, used by death effects; **L** for the meaning), `AGrid::IsTileClassLiquid 0x42fef0(id)` (class 2/0xb/0x10; **L**).
- `AGrid::ClampRect 0x42f170(short rect[4], units)`: clamps to the map. `units`: 0x4d = 64 px cells, 0x4f = pixels, otherwise 32 px tiles.

Cell and pixel conversion: there is no helper. Everything is inline: tile = px >> 5, cell = px >> 6, cell = tile >> 1. Screen → world goes through `SGame 0x47ec60/0x47ec80`, which add the camera and double in 2x zoom-out (see fogbuild.md). **H**

Drawing and redraw marking (the dirty-cell system):
- **AGrid is the world's ISurface.**
  - `ISurface` vtable: [1] `0x46ae60` MakeCurrent (sets `g_CurrentSurface` 0x550850), [2] `0x46ae70` Release, [3] InvalidateRect, [4] InvalidateText.
  - AGrid overrides [3] with **`AGrid::InvalidateRect 0x42f2b0(short rect[4])`**. It adds the origin (+0x9050/52), doubles when zoomed out ([0x550854]==1 or [0x53ccb8]==1), shifts >>6, clamps, and sets `dirty[cur][cy][cx] = 1`.
  - `[4] 0x42f410` invalidates the box around a text string.
- `SGame::RecordDirty 0x47cdb0`, the incremental path of `SGame::Draw 0x47b770`:
  1. makes AGrid current;
  2. sets the origin to the camera;
  3. runs the world and interface draw in a recording pass, so every object that would draw marks its cells;
  4. tests the cursor cell (`AGrid::IsCellDirty 0x42fe60`).
  The full-redraw conditions call `AGrid::MarkAllDirty 0x42f210(1, 0)` instead (see tutorial-terrain.md). **M** for "recording pass".
- `AGrid::DrawTerrain 0x42deb0(short viewRect[4], short zoomedOut)` blits the 64 px tile picture (`0x467920`, or half size `0x467a10`) for every cell in the view with `dirty[0][c] + dirty[1][c] != 0`.
- Then `APlayer::DrawFog 0x437000` (local player vtbl[1]) draws the fog and shroud on the same dirty cells. At the end of the frame, `AGrid::SwapDirtyBuffers 0x42f470` flips +0x9048 and clears the new current buffer. A change therefore stays visible for two frames, which suits page flipping. **H**
- Debug overlays, set by the console:
  - `AGrid::DrawTerrainTypes 0x42edc0` (SGame+0x43a, "terrain types on/off"): draws the four quadrant types per cell;
  - `AGrid::DrawDirtyCells 0x42efc0` (SGame+0x432);
  - AThingRegistry grid views `0x449860`/`0x448c80` (SGame+0x42e/+0x430);
  - the fog grid `0x436970` (SGame+0x436).

Per-turn update: `CommandModel::SimulateTurn 0x463080` → … → **`AGrid::Tick 0x42dcf0`**. It runs, in order:
1. the quake check (`0x42dd70`: force > 2000 → `StartQuake 0x42de10`);
2. the game clock (`0x42d9c0`: when it reaches 0, sound 0x133 and `AEventRegistry::SetTrigger 0x41d900`);
3. the shake step;
4. step counter ++;
5. `AIMovementGuide::ConnectStep 0x4a5200` (200 guide-point pairs per turn; "Map processed" when done);
6. only after that, `TileAnimator::Tick 0x455220` (every 8th frame; it writes animated ids into the cells and marks them dirty).

## 4. AITerrainAnalysis, AIMovementGuide, AIRetina

- **`AITerrainAnalysis`** (0x58 bytes, vtable 0x50435c; built in `0x42d790`/`0x42e080`; dtor `0x4a7d60`):
  - +0x04 `tObstacleMap` (u16 per 32 px tile; 1 = obstacle);
  - +0x1c `AIMovementGuide`;
  - +0x4c AGrid*, +0x50 AThingRegistry*, +0x54 "show obstacles".
  - `tObstacleMap::Regenerate 0x4a7a70` sets 1 where `TerrainTypeAt` is 1 or 2, plus the footprint (radius table 0x54cf08) of every thing whose type is ≥ 0x43 (rocks and trees: AObstacle). **H**
- **`AIMovementGuide::Build 0x4a4eb0`** (reached via `AITerrainAnalysis::Rebuild 0x4a8030`, console "process map" `0x482ab0`, and "ai terrain obstacles generate" `0x4a8040`):
  1. stops the tile animation;
  2. regenerates the obstacle map;
  3. clears the global guide-point list (data **0x59234c**, count **0x592350**, 0x28-byte `tGuidePoint`);
  4. creates a guide point at every convex obstacle corner;
  5. restarts the animation.
  `ConnectStep 0x4a5200` then links mutually visible points over later turns: max distance² 810000 = 900 px, at AIMovementGuide+0x2c. This is the pathfinding graph; the pathfinding topic owns the details.
- The guide is **saved in the map** (ITA version 4), so loading does not rebuild it. Shipped maps were "processed" by the developers.
- **AIRetina** (vtable 0x50951c, ctor `0x4a9140`) is created per AI player by the AIComputerPlayer ctors `0x4b58b0`/`0x4b5af0`. It is a region map for AI threat and things, not part of map loading. **M**

## 5. Things on the map: creation and occupancy

- `AThingRegistry::CreateAt 0x446db0(u16 owner, int type, int x, int y, short force, short notify, u16 id)` returns the new id, or 0xc01 if refused.
  - Unless `force`, it validates:
    - the object grid cell is free;
    - pods need terrain type 2;
    - devices need power or (Refinery) a pod nearby;
    - terrain via `0x44e5c0` / `IsAreaBlocked`.
  - It then calls `Things_NewThing 0x447a90(id, type, owner, x, y)` (the class factory, a free function) and `AThingRegistry::GridStamp 0x448460(thing, 1)`.
  - GridStamp:
    - writes the id into the object grid over the footprint (`0x44e8e0`);
    - updates the u8 counter grid for types 0x2d/0x35/0x39/0x3a/0x43 (`0x448550`);
    - **reveals fog** around owned things (`0x448730` → `APlayer::SetSeen 0x4360c0`).
  - `notify` → `Things_RegisterWithPlayer 0x4473a0`. **H**
- `CreateCentered 0x447410(owner, type, cx, cy, id)` subtracts half the footprint (`0x54e370[type]/2`), so CreateAt's x, y are the footprint's top-left corner (**M**). `CreateWithStats 0x4472d0(owner, type, x, y, s0..s3)` is used by AAction_AddCreature, the cheat placement (SGame+0x43c/+0x454) and `0x429350`.
- Type ranges (factory `0x447a90`, **H**):
  - 0..0x2b creatures (0/9/0x12/0x1b AShaman, others ACreature);
  - 0x2c..0x3e devices (0x2c Refinery … 0x3b);
  - 0x3f..0x41 pods (`AMushroom`, ctor 0x444aa0);
  - 0x42 `ADeath` (0x40c300);
  - 0x43..0x49 `AObstacle` (rocks and trees, ctor 0x434ae0, random rotation +0x2a);
  - 0x4a = none.
  - Types 4, 0xd and 0x1f use the *air* object grid. **M** (flyers)
- `AThingRegistry::RemoveThing 0x4481e0(id, flag)` clears the grid stamp (`0x448460(...,0)`).
- `AThingRegistry::Clear 0x44a0b0` removes everything. It is used before a load and by `0x44a190` (teardown).
- **Fog** lives in each APlayer (`+0x26` visible counts, `+0x2426` explored, `+0x484c` enabled; see fogbuild.md). It is not rebuilt by a separate pass: visibility counts come from GridStamp as things are created during stage 4. `APlayer::SetSeen` also marks the 3x3 cells around a changed cell dirty, for the local player. **H**

## 6. Minimap (global 0x5517b0, no RTTI; the RTTI class `ARadar` is the Radar building)

- `Minimap::FitToMap 0x432e50`: picks a scale so that the map is ≤ 100 px, then `Update`.
- `Minimap::SetScale 0x433d00`: marks all cells dirty.
- `Minimap::Update 0x433840(viewRect)` re-renders the pixel buffer at +5 (stride 0xc4). It uses tile pictures (via `AGrid::CellAt` + `0x434060`), fog, and the thing grid.
- `Minimap::Tick 0x433520` (SGame tick) re-renders every (0x16 − scale) ticks, or when +0x9620 is set.
- `Minimap::Refresh 0x433820` re-renders now (console "makemap", end of load).
- `Minimap::Draw 0x432eb0` is called from the interface draw. **M**

## 7. ATerrainTile, APaintObject, ISurface

- **ISurface** (vtable 0x50560c, 6 slots): the "invalidate rect" interface. Drawing code reports what it touched to the current surface ([0x550850]). Subclasses:
  - **AGrid**: world cells;
  - IWorldViewPort (0x509464; [3] 0x4a6d10);
  - IRegionSurface, IInfoBoxSurface, IRectSurface (UI).
- **APaintObject** (vtable 0x5045bc, 29 slots, ctor `0x4359d0`, 0x18 bytes) is a clickable, paintable UI item: +4 rect (short[4]), +0xe id, +0x10 enabled, +0x14 flag; `0x435a40` is the point-in-rect test. Its only subclass is ATerrainTile. **H**
- **ATerrainTile** (vtable 0x504cdc, ctor `0x4554d0`, 0x40 bytes) is one **terrain family / brush** from `TERRAIN.MHK`:
  - `Load` (slot 1) `0x455600(resId)` reads `TRVA resId`: u16 ?, u16 count, u16 ?, then count × (u16 tileId, u16 weight), big-endian;
  - +0x20 tile-id list and +0x30 weight list;
  - `0x455a30` picks a random variant by weight (+0x1a).
  - Slot 3 `0x4557e0` draws the tile and, with +0x14 set, its four quadrant types.
  - Slot 7 `0x455ad0` is a click: it cycles the clicked quadrant's terrain type 0→1→2 for every tile id of the family (`AGrid::SetTileTypes 0x42e9f0`).
  - This is a **leftover in-game terrain-type painter** (with "save terrain" writing terrain.hoo). In the shipped code the objects are only created temporarily inside `AGrid::LoadTileClasses`. **M**

## 8. The built-in developer console (useful for an editor)

`0x482480` dispatches typed commands (from `SGame_OnKeyDown`, `0x47f0d5`/`0x47f264`). The command table is at **0x507360**: {char* name, handler} pairs up to 0x507510. Map-related entries:
- "save map" `0x4830b0` (→ RequestSave, default "Map.Dbg");
- "load map" `0x4830a0` and "netcheat" `0x4855a0` (→ `0x47b620`, `0x47ab00` stages 2..8, then `0x4805d0` loads the given file or Map.Dbg / E3Multi.dbg);
- "process map" `0x482ab0` (rebuild the AI guide);
- "save terrain" / "load terrain";
- "terrain types on" / "off" `0x483550`/`0x483570`;
- "thing grid on" / "off", "mask grid on" / "off";
- "makemap" `0x485510` (minimap refresh), "minimap", "show map" `0x485580`;
- "place" `0x483590` (arms the cheat placement of a named thing: rocks, trees, weed, spore, ruins, creatures…);
- "quake" `0x4834c0` (`StartQuake(4)`);
- "supports N" / "recommended N" (MapInfo players);
- "ai terrain obstacles generate|on|off" `0x4825a0` → `0x4a8040`;
- "guide on|off|erase" `0x4a60b0`.

How the console is opened is not traced here. **L**

## 9. What a map editor must rebuild

| change | must do | why |
|---|---|---|
| set a cell's tile id (`cells[cy*96+cx]`) | mark it dirty: `AGrid::MarkAllDirty(1,0)` as AAction_CopyTerrain `0x41ae20` does, or `dirty[cur][cy*96+cx]=1` (0x5625a8 + 0x2400*[0x566da8]) | DrawTerrain only draws dirty cells |
| same | nothing for terrain *queries*: TerrainTypeAt / IsAreaBlocked read the tile-id table live | passability is derived on demand |
| same, if the edit changes passability | `AITerrainAnalysis::Rebuild 0x4a8030(ecx=[0x55dd64])` (or the console "process map"); connection then runs over later turns | the obstacle map and guide graph are cached and saved in the map. CopyTerrain does **not** do this (a stock limitation) |
| same, water/animated tiles | `TileAnimator::Stop 0x455420(0x562568)` before editing, then `AGrid::BuildTileAnimList 0x42ff30` and `TileAnimator::Start 0x455490` | the animator keeps cell coordinates and restores base ids |
| change the map size | `AGrid::SetMapSize 0x42d740` (resizes the AI maps) + clear cells outside + `Minimap::FitToMap` + `SGame::SetViewSize 0x47b520` / `ClampCamera 0x47d340` | MapTilesW/H drive clamping everywhere |
| add a rock, tree or pod | `AThingRegistry::CreateAt(owner=8?, type, x, y, force=1, notify=1, 0xc01)`, then rebuild the AI guide for obstacles (types ≥ 0x43) | CreateAt stamps the object grid and fog; obstacles are part of the guide |
| remove a thing | `AThingRegistry::RemoveThing 0x4481e0` (+ guide rebuild for obstacles) | clears the grid stamp |
| any visible change | `Minimap+0x9620 = 1` (or `Minimap::Refresh 0x433820`) | the minimap buffer is cached |
| save | `SGame::SaveMapFile 0x480be0(path)` with a `.wbm` name (writes everything through the same serializers) | — |

Other notes:
- Cells outside tilesW/tilesH are still saved (96x96 always). Shipped small maps contain leftover ids there.
- Map limits: 192x192 tiles (96x96 cells, u8 sizes). Things: the object grids are fixed at 192x192.

## 10. Bug lead (for the crash/bug-fix work)

**Animated water reads as open ground (M).**
- `TileAnimator::Tick 0x455220` replaces about 40 % of the class-5 water cells (ids 0x13e/0x14b/0x158, all type 1 "blocked" in terrain.hoo) with frame ids 0x1a2..0x1a7.
- Those ids are ≥ the terrain.hoo count (418 = 0x1a2), so `TerrainTypeAt` returns 0 and `IsAreaBlocked` sees "not blocked" for those cells while the frame is shown.
- Building placement (`0x44e5c0`) and any creature or pathing code that uses these two functions could therefore accept water cells intermittently. The AI guide is unaffected, because Build stops the animation first.
- A fix: extend the terrain-type table with the animated ids (copy the base tile's quads), or map ids ≥ 0x1a2 back to their base in `0x42ea40`/`0x42eb50`. Not verified in game.

## Open questions / uncertain

- Meaning of AGrid+0x9056 ("GSS": serialized but no other reader), MapInfo+0x0a padding, AEvent+0x20/+0x22.
- The exact layout of the guide data after the ITA version word (`0x4a4670`). It is left to the pathfinding topic.
- Whether CreateAt's x, y are the footprint's top-left (suggested by `0x447410`) or the centre for every class. AThing+0x14/+0x16 is the thing's position.
- Which owner index neutral things (rocks, trees, pods) use in maps (`C` field default 10).
- The AThingRegistry +0x2410c u8 grid (counter for big footprints and obstacles) and +0x1c list (deferred deletes): purpose inferred only.
- The meaning of terrain type 2 beyond "pods need it": creature code (`0x49bd50`) reacts to stepping on it.
