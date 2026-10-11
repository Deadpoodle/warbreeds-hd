# The WarBreeds map format (.WBM)

Static analysis of `WARBREED.EXE` (GOG build, image base 0x400000) and of the 1998 map editor `wbedit/MapEditor.exe`, checked against all 54 shipped maps (19 in `DATA/MAPS`, 35 in `CAMPAIGN`). Neither program was run. `tools/wbm.py` implements everything below and round-trips every shipped map byte for byte (§11).

Confidence: **H** = read directly from code and confirmed by the data. **M** = inferred from code plus data or naming. **L** = a guess.

Names follow the other `re/*.md` notes:
- `World` is the terrain object at `0x55dd60`, so World+0x9054 is `0x566db4`.
- `Things` is `[0x579278]`, the thing registry (`AThingRegistry`, vtable `0x504cb4`). `re/hotkeys.md` and `re/mouse.md` call it "World".
- `Players` is `[0x57927c]` (`APlayerRegistry`, vtable `0x504754`).
- `Effects` is `[0x579280]` (`AEffectRegistry`, vtable `0x50416c`).
- `Events` is `[0x579284]` (`AEventRegistry`, vtable `0x503e5c`).
- `MapInfo` is the map header object at `0x567578`.

---

## 1. Summary

- A `.WBM` is **not** a Mohawk archive. It is a **save game**: the engine's own serialization archive in binary mode, written by the same staged saver that writes `.WBS` saved games. **H**
- There is no file magic, no chunk directory, no length prefixes per section and no compression. Values are little-endian and packed with no padding, in the exact order the `Serialize()` functions run. **H**
- Every serializer is written as `if (field name exists in the archive) ...` with a short tag string (`"MPWD"`, `"GQF"`, `"+"`). In binary mode the tags are ignored and nothing is written for them. A map saved with the extension `.dbg` uses a tagged text format instead (§5.3). **H**
- The file holds the complete game state, not just map design: the editor runs the world simulation while you edit, so a map contains:
  - the tick counter;
  - the AI's internal state;
  - the precomputed pathfinding regions;
  - per-creature runtime fields;
  - uninitialised bytes (§6).
- Sections, in file order:

| # | Stage | Tag | Content | Serializer | Lands in |
|---|---|---|---|---|---|
| 1 | 2 | `?` | header: version, player counts, size, name | `0x432330` | `MapInfo 0x567578` |
| 2 | 2 | `+` | terrain tiles, world counters, mission clock | `World::Serialize 0x42e080` | `World 0x55dd60` |
| 3 | 2 | `+`/`7` | pathfinding guide and regions | `0x4a7da0` → `0x4a4670` | `World+4` → `AIMovementGuide`, regions `0x592348` |
| 4 | 3 | `=` | players, part 1 (8 slots) | `0x43ae20`, `APlayer 0x438720` | `Players+4[8]` |
| 5 | 3 | `FX` | effects | `0x425160` | `Effects` |
| 6 | 4 | `!` | things: units, devices, pods, trees, rocks | `0x450fa0` | `Things+0x10` table, grid `+0x10c` |
| 7 | 5 | `ER` | triggers (conditions → actions) | `0x41d940` | `Events` |
| 8 | 6 | `=` | players, part 2 (AI state) | `0x43b460` → `0x4b7280` | `AIComputerPlayer` |
| 9 | 7 | `CAMP` | campaign state flag (0 in every map) | `0x482150` | `[0x57928c]` |
| 10 | 8 | `CLOCK`, `Z1`–`Z4`, `PW` | game clock, scroll bounds, saved routes | inline in `0x4812f0` | Cmd clock, `SGame+0x44`, `SGame+0x1a8` |

Stages are the cases of the staged loader `0x4812f0` (§5).

---

## 2. Conventions

- **Types:**
  - `u8`, `u16`, `u32` are little-endian unsigned integers.
  - `pt` is the engine's `Pt {vtbl; short x; short y}`, written as `u16 x, u16 y` (`0x4236b0`).
  - `ptlist` is `u32 n` followed by `n × pt` (`IList<Pt>`, `0x485a10`).
- **Units:**
  - A **tile** is 32 world px. The game's terrain-type, collision and pathing grids use tiles. World+0x9054/0x9055 count tiles.
  - A **cell** is 64 px = 2×2 tiles. The terrain art, the fog maps and the map header use cells.
  - World coordinates are in pixels.
- **Uninitialised bytes:** the editor is a debug build, and **`0xCD`** bytes in a file are its uninitialised heap fill (MSVC debug `0xCDCDCDCD`). A writer must preserve them to round-trip byte for byte. The game reads them and they do no harm.
- **"None" values:**
  - `0x0C01` (3073) means "no thing".
  - `0xFFFF` means "no region" or "none".
  - `0xFD66` (−666) is the default of an unset `Pt`.
- **Player references:** the engine stores a player pointer and writes the player's slot index (0–7). The value 9 means "no owner" (nature: trees, rocks, neutral pods), and 10 is the reader's default. Triggers instead identify a player by its colour byte (§4.5, `G`).

---

## 3. Byte layout

Offsets are absolute up to the end of the fixed part. `N` is the name length including its NUL.

### 3.1 Header `?` (`MapInfo`, `0x432330`) — **H**

| Offset | Type | Tag | Meaning |
|---|---|---|---|
| 0 | u16 | `8` | format version, always **5**. The loader throws an error otherwise (`0x432330`, `*this != 5` → `0x4eba00`). |
| 2 | u16 | `NPSP` | **players supported** (maximum), 2–8 in the editor |
| 4 | u16 | `NPRC` | **players recommended**, 2–NPSP in the editor |
| 6 | u16 | `MPWD` | width in cells. On save it is recomputed as `0x566db4 >> 1`. |
| 8 | u16 | `MPHT` | height in cells, `0x566db5 >> 1` |
| 10 | u16 | `NMLN` | N = name length including the NUL |
| 12 | N bytes | `OBJC` | map name, NUL-terminated. Each byte is serialized separately; there is no fixed buffer. |

- The `NPSP`/`NPRC` names come from the editor's registry keys `NumPlayers\Supported` and `NumPlayers\Recommended` (editor `0x4038f0`).
- `0x4321b0` reads only these fields, to build the map list (`0x4703d0`, `SFileDialog::MapInfo`).
- The multiplayer setup also loads the `+` and `=` sections of the selected map (`0x48ef70`, `0x48b5c0`, `0x490f70`), to draw the preview and enable one lobby slot per player in the file. The map's race becomes the lobby default for that slot (`0x48f410`).

### 3.2 Terrain `+` (`World::Serialize 0x42e080`) — **H**

| Offset | Type | Tag | Meaning |
|---|---|---|---|
| 12+N | u8 | `4` | width in tiles → World+0x9054 (`0x566db4`). Always 2×MPWD. |
| 13+N | u8 | `5` | height in tiles → World+0x9055 (`0x566db5`) |
| 14+N | 9216 × u16 | `6` | terrain cells, **96 × 96, always the full array, column by column** (for x in 0..95: for y in 0..95). In memory the array is row-major at World+8: `cell(x,y) = *(u16*)(World + 8 + (y*96 + x)*2)`. |

- **Tile id = frame of the terrain art.** The value is an index into image set 9000 of `DATA/TERRAIN.MHK` (`tBMH` 9000, 64×64 px frames, palette `tPAL` 9000). The game loads it into `0x550740` (`0x467130(&0x550740, 9000, 0)`), and `World::DrawCells 0x42deb0` blits `frames[cell]` for each visible cell.
  - The shipped maps use ids 0–417.
  - `wbedit/EditData/Terrain.MHK` holds the same art, with 424 frames.
- **Outside the map,** cells beyond (width, height) hold leftover values. The game never reads them.
- **Terrain type,** per tile, comes from `DATA/TERRAIN.HOO`, not from the map:
  - The file is `u32 n` (418) followed by `n × 4` bytes, one entry per tile id. The 4 bytes are the types of the 4 tiles of the cell, in the order (even x, even y), (odd x, even y), (even x, odd y), (odd x, odd y).
  - It is loaded by `0x42d790` → `0x42e960` into World+0x93a0/0x93a4/0x93a8.
  - Lookup: `World::TerrainType 0x42ea40(Pt tile)`. It returns 1 outside the map and 0 for ids ≥ n.
  - Values used:
    - **0 = ground**;
    - **1 = blocked**. Cliffs, chasms, water and lava all use type 1: the footprint check `0x42eb50` rejects any tile whose type has bit 0 set. There is no separate cliff or chasm type.
    - **2 = pod soil** (the "lavender" ground). `AShaman::CanPlantAt 0x442b10` and the thing factory `0x446db0` both require type 2 for pods, spores and weeds (types 0x3F–0x41).
- **Lava bubbles:** after load, `0x42da30` scans the cells for tile ids 0xE8, 0xE9 and 0xEA. On every 5th of them it spawns effect 0x49 (0xE8/0xEA) or 0x4A (0xE9). **H**
- **Editor paint data** in `TERRAIN.MHK` (**M** for the meanings, **L** for the first TRVA field):
  - There are 222 "terrain kinds" (ids 1000–1221).
  - `TRVA`: `u16 ?, u16 n, n × (u16 cumulative percent, u16 tile id), u16 100`. These are the random variants of a kind.
  - `TRST`: 4 × u16 material per corner, used for edge transitions.
  - `TRID`: u16 material class.
  - `TRTH` and `TRPL`: transition and palette tables.
  - The game uses only TRID/TRVA: `0x42db60` fills World+0x905c with the material class of each tile id. The map stores only the final tile ids.

### 3.3 World counters and mission clock (end of `+`) — **H** for layout, **M** for meaning

| Offset | Type | Tag | Field | Meaning |
|---|---|---|---|---|
| 18446+N | u16 | `GQF` | World+0x904a | quake energy this tick. Explosions add to it (`0x42de60`); above 2000 the screen shakes (`0x42dd70`). |
| 18448+N | u8 | `GSS` | World+0x9056 | unknown, 0 in all maps |
| 18449+N | u32 | `GSC` | World+0x9058 | world tick counter (`0x42dd60` adds 1 per tick). 17,836–611,696 in the shipped maps, because the editor runs the simulation. |
| 18453+N | u8 | `GQS` | World+0x93b0 | screen-shake step (`0x42dde0`) |
| 18454+N | u16 | `GAGC` | – | 1 if a mission clock object exists (World+0x93b4, `AGameClock`, vtable `0x504374`) |
| 18456+N | u32 | `GGCC` | clock+4 | clock value |
| 18460+N | u16 | `GGCST` | clock+0xc | clock setting. `AAction_GameClock` sets it with `CL/ST/TI`. |
| 18462+N | u8 | `GGCTI` | clock+0xe | clock flag |

On load, a clock is recreated with `0x42d950(GGCC, GGCST, GGCTI)` if `GAGC` ≠ 0. No shipped map has one.

### 3.4 Pathfinding (`7` → `AITerrainAnalysis::Serialize 0x4a7da0`) — **H**

| Offset | Type | Tag | Meaning |
|---|---|---|---|
| 18463+N | u16 | `<` | processed-map format, **4** in every map |
| 18465+N | wT·hT × u16 | `>` | **guide**: region id of every tile, **row-major** (`y*wT + x`), 0xFFFF = blocked or no region (`AIMovementGuide 0x4a4670`, cell `0x4a4480`) |
| … | u32 | | number of regions R (table `0x592348`: data `0x59234c`, count `0x592350`; `0x4a65d0`) |
| … | R × region | | each `0x4a4220`: `u16 id`, `pt centre` (world px), `pt f14` (values ±1, **L**: a direction), `u8 n`, `n × (u16 region, u16 distance px)` (`0x4a3fd0`) |

- If the format is **not 4** on load, the game reads nothing more of this section. It shows "The map format has changed since this map was saved. You must reprocess this map before use. Press OK to reprocess the map now." (`0x4719b0(3,2,1)`), and on OK it rebuilds the guide with `0x4a8030` → `0x4a4eb0`, which prints "Map processed". **H**
  - So a writer that cannot compute regions can store format 0 and omit the guide.
  - An in-game editor can instead call the game's own `0x4a4eb0` on `World+4` before saving, which avoids the prompt.
- The guide and region table are **derived data**. They must be recomputed after any change to terrain or to blocking things (rocks and trees occupy tiles).
- Regions with no tiles can remain in the table (ids stay stable).

### 3.5 Players, part 1 (`=` → `APlayerRegistry 0x43ae20`, `APlayer::Serialize 0x438720`) — **H** for layout

| Size | Tag | Field | Meaning |
|---|---|---|---|
| u16 | `PRHO` | +0x2a | 1 in every map |
| u16 | `PRFO` | +0x2c | 1 in every map. It is forced to 1 if 0. |
| 8 × slot | | +4[8] | player slots 0–7, below |
| u8 | `;` | +0x24 | number of players present |

Each slot starts with `u16 F`:
- If F ≥ 5, the slot is empty (the saver writes 5) and nothing follows.
- Otherwise the reader takes `G`, `H` and `I`, creates the player with `0x43b850(F, G, I, H)`, and calls `player->vtbl[0x6c]` (`0x438720`) for the rest:
  - H = 0 → `APlayer` (0x4a4c bytes, ctor `0x435ba0`);
  - H ≠ 0 → `AIComputerPlayer` (0x4e60 bytes, ctor `0x4b58b0`).

| Size | Tag | Offset | Meaning |
|---|---|---|---|
| u16 | `F` | +0x4878 | **race**: 0 Tanu, 1 Sen-Soth, 2 Kelika, 3 Magha (`0x410b70`). 4 is accepted but never used. |
| u8 | `G` | +0x484e | **colour**, a palette index: 238, 230, 222 … (238 − 8·slot) in every map. **Triggers name players by this value.** |
| u16 | `H` | – | 1 = computer player (`AIComputerPlayer`) |
| u16 | `I` | +4 | slot index |
| u8 | `{` | +0x18 | `1 << index` |
| u8 | `J` | +0x1a | bit mask (**L**). It equals `{` in every MP map; in campaign maps it is 3, 7, or uninitialised 0xCD/0xCF. |
| 5 × u16 | `_` | +0x4850 (as int) | **maximum CGK level per race** (Tanu, Sen-Soth, Kelika, Magha, 5th = 2). MP maps give the own race 4–5 and the others 2–3. **M** |
| 5 × u16 | `XU` | +0x4864 | 0 in every map |
| u16 | `}` | +0x48f6 | 1 in every map |
| pt | `:` | +0x4a1c | **start position, in cells.** It matches the slot's shaman (e.g. (7,10) ↔ shaman at px (517,628) on 4 Corners). **M** |
| u16 | `a` | +0x484c | **fog of war on** (1 in every map). See `re/fogbuild.md` §2. |
| u8 | `dc` | +0x4a31 | 0 |
| 5 × u16 | `b` | +0x486e | 0 |
| 9216 bytes | `c` | +0x2426 | **explored map**, 96×96 cells, column by column. 1 = unexplored (all 1 except one player in MPMAP2). The visibility counts `+0x26` are not stored; they are rebuilt from sight sources. |
| 24 × u16 | `d` | +0x4888 (3×8 int) | counters (**L**: statistics), 0 |
| 3 × u16 | `km` | +0x48e8 | 0 |
| 32 bytes | `e` | +0x48f8 | name, `char[32]`. Empty plus 0xCD fill in maps. |
| u8 | `PMSB` | +0x19 | 0xFF |
| 5 × u16 | `PCBCF` | +0x1c | 1 at the player's own race. Set again on load. |
| 2 × u16 | `PEMM` | +0x4882 | 0 |
| u16 | `MPTY` | +0x4a44 | 1 or 2 for slot 0, 0 for the others. The ctor default is 2. Meaning unknown. |

- Total size per used slot: 9,367 bytes.
- On load `0x438720` also sets `+0x1c[race] = 1`, and `+0x4850[race] = 1` if it was 0.

### 3.6 Effects `FX` (`AEffectRegistry 0x425160`) — **H** for layout, **L** for meaning

Every shipped map has empty lists: 36 zero bytes.

**Part A** (`0x425430`):
- 5 × 2 lists, each `u16 n` followed by `n` entries. Each entry is:
  - `u16 kind` (`YY`);
  - if 0 ≤ kind < 0xAF and kind ≠ 0x8A: `u16 ZZ`, `pt aA`, `u16 bB`, `u16 cC`, `u8 dD`, `u16 MOVN`, and, if MOVN ≠ 0, `pt pP` (target of a moving effect).

**Part B** (`0x426080`):
- 8 lists (one per player), each `u16 n` followed by `n × (u16 lL kind, u16 kK, u16 nN, u16 mM, u8 oO, pt aA, u8 dD)`.

### 3.7 Things `!` (`AThingRegistry 0x450fa0`) — **H**

| Size | Meaning |
|---|---|
| 8 × (u32 n, n × u32) | lists at Things+0x8c (16 bytes each) of thing ids. Empty in every map; meaning **L**. |
| u32 | S = size of the thing table (Things+0x14) |
| S × slot | one slot per thing id, **in id order** |

**Slot layout:**
- `u16 A` = type. **0x4A means an empty slot.** The loader appends a null so ids stay aligned.
- Otherwise:
  - `u16 B` id. It must equal the slot index; this holds in every map.
  - `u16 C` owner slot, 9 = none.
  - `u16 D` x, `u16 E` y: the placement corner in px (Thing+0x0a/0x0c).
  - the class body, `vtbl[0x48]`.
- The loader creates each thing through the normal factory, `0x446db0(owner, type, x, y, 1, 0, id)` → `0x447a90`. That runs the same placement checks the game uses: terrain footprint, pod soil for pods, and an occupied tile grid. So a thing on a blocked or occupied spot is dropped (**M**: the return value is not checked).
- The loader then looks up id B and calls its `Serialize`.
- After the section, `0x42da30` (lava bubbles) and `0x453a30` run.

**Type ids** (`TYPE_ART` and `TYPE_NAMES` in `tools/wbm.py`):
- Art names come from the 20-byte name table at offset 0xA1E of `GAMEPLAY.DAT`. The art file is `DATA/<name>.MHK`.
- UI names come from `0x410df0`.

| Types | Class (vtable, Serialize) | Contents |
|---|---|---|
| 0, 9, 0x12, 0x1B | `AShaman` (`0x504a2c`, `0x4440e0`) | Tanu, Sen-Soth, Kelika, Magha shaman. Shaman type = race × 9. |
| 1–4, 10–13, 19–22, 28–31, 35, 36 | `ACreature` (`0x509314`, `0x49aca0`) | Tanu: scout, raptor, T-Rex, dragon. Sen-Soth: flea, myrmidon, beetle, dragonfly. Kelika: tribble, sasquatch, behemoth, leviathan. Magha: shadow, dervish, heavy, 31. 35 = Yedda, 36 = dhuer. 5–8, 14–17, 23–26, 32–34 and 37–43 are "x"/"crx" variants: the art exists, but none are in any map. |
| 0x2C | `ARefinery` (`0x43e7e0`) | refinery |
| 0x2D, 0x35 | `AGeneLab` (`0x429e60`) | gene lab, advanced gene lab |
| 0x2E, 0x36 | `ARelay` (ADevice body) | relay, advanced relay |
| 0x2F, 0x38 | `ABattery` (ADevice) | battery ("psi tower"), advanced |
| 0x30 | `AMine` (ADevice) | mine |
| 0x31, 0x37 | `ATurret` (`0x456820`) | turret, advanced turret |
| 0x32 | `AElectricTower` (`0x410590`) | electric (ion) tower |
| 0x33 | `ARadar` (`0x43d720`) | observation tower ("listener") |
| 0x34 | `ARevitalizer` (ADevice) | revitalizer ("healer") |
| 0x39 | `AWarp` (`0x458370`) | warp |
| 0x3A | `AHowitzer` (`0x431250`) | howitzer ("summoner") |
| 0x3B | `ACollector` (ADevice) | gene collector |
| 0x3C–0x3E | `ADevice` (`0x40e5c0`) | "devx" placeholders |
| 0x3F, 0x40, 0x41 | `AMushroom` (`0x4451d0`) | **pod**, spore, weed. They must sit on pod soil. Owner 9 = neutral, or a player. |
| 0x42 | `ADeath` (AThing body) | corpse |
| 0x43–0x49 | `AObstacle` (`0x434f10`) | ruins, ctree, crock, stree, srock, ttree, trock |

**Bodies** (`fN` = in-memory offset N where the meaning is unknown):

- **AThing** (`0x446020`), 6 bytes:
  - `pt pos` (+0x14, centre px);
  - `u16 hp` (+0x24).
- **ADevice** (`0x40e5c0`), AThing plus 18 bytes:

  | Size | Field | Notes |
  |---|---|---|
  | u16 | f2a | 1 |
  | u16 | f32 | build time + 1, e.g. 1201, 801 |
  | u16 | built | +0x2c. If non-zero after load, `vtbl[0xa0]` activates the device. |
  | u16 | f28 | |
  | u16 | f2e | |
  | u8 | f30 | |
  | u8 | f38 | |
  | u16 | f3a | |
  | u16 | power_from | +0x36: id of the relay or refinery powering it, 0x0C01 = none (**M**) |
  | u8 | f3c | |
  | u8 | group | +0x3d, 0xFF |

- Device extras, after the ADevice part:

  | Class | Extra fields |
  |---|---|
  | ARefinery | `u16 f48, u16 f4a` |
  | ATurret | `u16 f40` (stored as u8), `u8 f41`, `u16 f42`, `u16 f44`, `u16 f46` |
  | AElectricTower | `u8 f40, u16 f42` |
  | ARadar | 4 × u8 |
  | AWarp | 5 × u16 |
  | AHowitzer | `u16 f48, u16 f4a, u16 f40` |
  | AGeneLab | `ptlist`, 7 × u16 (+0x1fc … +0x206, +0x1e8), 4 × u16 (+0x1ec), `pt f218` |

- **ACreatureBody** (`0x405400`), after AThing:

  | Size | Field | Notes |
  |---|---|---|
  | 4 × u16 | genes | +0x54, default 100 (**M**: gene strengths) |
  | 4 bytes | f64 | |
  | 4 bytes | f68 | |
  | u16 ×4 | f4c, f2c, f2e, f30 | |
  | pt | f38 | |
  | u32 | f3c | |
  | u8 | f40 | |
  | u16 | f48 | |
  | u16 | f4a | |
  | u16 | f4e | |
  | u8 | f50 | |
  | u16 | f52 | |
  | u16 | f70 | |
  | u8 | f72 | |
  | u16 | f74 | |
  | u8 | f77 | |
  | u8 | f78 | |
  | u16 | f7c | |
  | u16 | f80 | |
  | u8 | f8c | |
  | u8 | group | +0x8d, control group, 0xFF = none (`re/hotkeys.md`) |
  | u8 | f79 | |
  | u32 n, n × 3 u16 | f90 | `0x409500` |
  | u16 | f88 | |
  | u16 | f44 | |
  | u16 | f32 | |
  | u8 | f76 | |

- **ACreature** (`0x49aca0`), after the body:

  | Size | Field | Notes |
  |---|---|---|
  | pt | f138 | |
  | u16 | f13c | |
  | u16 | f140 | |
  | ptlist | path | |
  | u16 | fc0 | |
  | u32 | fc4 | |
  | u16 ×4 | fec, f100, f10c, ffc | |
  | pt | f104 | |
  | pt | f14c | |
  | u16 | f32 | |
  | u8 | f44 | |
  | u16 | fd8 | |
  | u8 | fbc | |
  | ptlist | waypoints | +200 |

- **AShaman** (`0x4440e0`), after the creature:

  | Size | Field |
  |---|---|
  | pt | f16c |
  | ptlist | build list (**L**) |
  | u16 | f17e |
  | u16 | f180 |
  | u16 | f18e |
  | u8 | f198 |
  | u16 | f184 |
  | u8 | f170 |
  | u16 | f1b4 |
  | pt | f178 |
  | u8 | f17c |
  | u8 | f154 |

- **AMushroom** (`0x4451d0`), after AThing: `u16 f28` (growth, **L**), `u8 f2c`, `u8 f32`, `u8 f2d`, `u8 f30`, `u8 f31`.
- **AObstacle** (`0x434f10`), after AThing: `u8 state`, `u16 phase`. The ctor sets phase to a random 0–359.
- Record sizes: shaman 153 bytes; creature 127–135; device 34–43, gene lab 68+; pod 23; obstacle 19.

### 3.8 Triggers `ER` (`AEventRegistry 0x41d940`, `AEvent 0x41d000`) — **H** for layout, **M** for meaning

Registry:
- `8 × u16`: +0x14, 0 in every map.
- `u16` event count.
- events.

Event:
- `u16 AJ` (+0x20, 0 in every map);
- `u16` condition count (`C0`), then each condition as `u8 type` (`CT`) followed by its fields;
- `u16` action count (`E0`), then each action as `u8 type` (`AT`), `u32 AD` (the delay; `AAction 0x415050`), and its fields.

An event fires when all its conditions hold (**M**). Factories: conditions `0x4123d0`, actions `0x412600`. Each class serializes at `vtbl[0x18]`.

**Conditions:**

| Type | Class (serializer) | Fields |
|---|---|---|
| 0 | Timed (`0x412dc0`) | `CC u32` time |
| 1 | Position (`0x413200`) | `CS pt`, `CP u8` player colour, `Cs u16` thing type (0x4A = any) |
| 2 | PlayerDeath (`0x413c70`) | `CP u8` |
| 3 | PlayerCGK (`0x413fa0`) | `CP u8`, `CK u16` race, `CA u16` level |
| 4 | PlayerDevices (`0x414490`) | `CP u8`, `Cs u16` type, `CN u8` count |
| 5 | PlayerSeen (`0x414970`) | `CV u8` viewer, `CX u8` seen |
| 6 | Trigger (`0x413a10`) | `CI u8` trigger number |
| 7 | Rect (`0x413740`) | Position fields + `P2 pt` (opposite corner) + `CN u8` count |
| 8 | ThingMissing (`0x414d70`) | `u16, u16` (**L**: the second may be part of a 32-bit id) |

**Actions** (after `AD`):

| Type | Class (serializer) | Fields |
|---|---|---|
| 0 | AddCreature (`0x415990`) | `SP u16` type, `PL u8` colour, `PO pt`, `G1`–`G4 u16` genes, `VI u8` |
| 1 | Effect (`0x416150`) | `FT u16` effect, `LY u16`, `FC u16`, `PO pt`, `CO u8` colour |
| 2 | GameClock (`0x416670`) | `CL u32`, `ST u16`, `TI u8` |
| 3 | Audio (`0x416a20`) | `RI u16`, `PO pt` |
| 4 | CDAudio (`0x416d70`) | `TR u8`, `TX u8` |
| 5 | Capture (`0x417570`) | `PL u8`, `PD u8` |
| 6 | Warp | same as AddCreature (`0x415990`) |
| 7 | Quake (`0x418b20`) | `QU u8` |
| 8 | VeteranStatus (`0x417c90`) | `PO pt` (`0x417930`), `EX u16` |
| 9 | Heal (`0x417f80`) | `PO pt`, `PT u16` |
| 10 | Trigger (`0x418d60`) | `TI u8` |
| 11 | PlayerEnd (`0x419050`) | `PL u8`, `VI u16` (1 = victory, **M**) |
| 12 | Message (`0x419430`) | `PL u8`, `ST char[80]` |
| 13 | Script (`0x4198c0`) | `SC char[80]` |
| 14 | Remove (`0x419c40`) | `u16, u16` |
| 15 | SetGizmo (`0x418840`) | `PO pt`, `SG u16` gizmo |
| 16 | SetAggression (`0x41a0d0`) | `PL u8`, `AG u8`, `AH u8`, `AI u8` |
| 17 | SetBounds (`0x41a6c0`) | `PO pt`, `P2 pt` |
| 18 | SetImportance (`0x41b8f0`) | `PL u8`, `AG u8`, `PO pt` |
| 19 | SetSquadChanging (`0x41bd40`) | `PL u8`, `AJ u16` |
| 20 | CopyTerrain (`0x41b0c0`) | `PO pt`, `P2 pt`, `P3 pt` |
| 21 | OKDialog (`0x41c0b0`) | `T1`, `T2`, `T3 u16` |
| 22 | MoveIntoView | base only |
| 23 | RevealMap (`0x41b520`) | SetBounds fields + `PL u8` |
| 24 | Move2Point (`0x41c4f0`) | `PO pt`, `P2 pt`, `RM u16` |
| 25 | Warp2Point (`0x41cb00`) | `WSCY u32` |
| 26 | SetMobility (`0x41a9d0`) | SetBounds fields + `T1 u16` on/off |
| 27 | CGK (`0x4182b0`) | `CP u8` colour, `CL u16` level, `CK u16` race |

- Strings are 80 bytes, NUL plus 0xCD fill, and are written byte by byte (loops at `0x41963e` and `0x419910`).
- Every condition and action type in the tables appears in the shipped maps except condition types 2–4 and actions 3, 4, 13, 19, 23–25.
- Example, the Combat tutorial: "Rect(player 238 in (1500,2350)–(2158,2780), any type, 1) → SetMobility(rect, on)".

### 3.9 Players, part 2 — AI state (`=` → `0x43b460`) — **H** for layout, **L** for meaning

The registry calls `player->vtbl[0x70]` for every used slot, in slot order:
- `APlayer` (human) writes **nothing** (`0x47a530` is an empty function).
- `AIComputerPlayer` (`0x4b7280`) writes:

| Size | Meaning |
|---|---|
| u16 | marker. On save it is always 0x0D. On load, any other value skips the rest and calls `vtbl[8]`, which re-initialises the AI (**M**). |
| gw·gh × 28 bytes | AIRetina grid 1 (`0x4a9790`, cell `0x4a8850`): `u16 x, u16 y, u32, u16, u32, u16, u32, u32, u32`. gw = ⌈wT/8⌉, gh = ⌈hT/8⌉ (sectors of 8×8 tiles; **M**, from the data, which confirms it on all 54 maps). |
| 5 × gw·gh × 52 bytes | five grids of 13 × u32 (`0x4a8eb0`) |
| 4 × u32, 2 × u16, 13 × u32 | AI parameters (+0x4a48 …) |
| 5 × 4 bytes | five float32 (0.5, 0.5, 0.5, 1.0, 0.5 in the maps) |
| 74 × u16, u16 | |
| u16 n, n × (u8, u16, u16, u32), u16 | target list (`0x4b5170`). On load the entries are read into a temporary and dropped (**M**). |
| u32 n, n × squad | squads (`0x4ba950`): `u32 m`, `m × u16` member ids, then 100 bytes: `u32, u32, 4×u16, 3×(u16,u16), u32, 8 bytes, 8 bytes, 4×u16, 5×u32, 3×u32, 2×u16, pt, u32` |

This section is most of a big map's size: about 145 KB per AI player on 176×184 tiles.

### 3.10 Tail: campaign, clock, bounds, routes (stages 7–8 of `0x4812f0`) — **H**

| Size | Tag | Meaning |
|---|---|---|
| u16 | `CFLG` | 1 if campaign state follows. **0 in every map.** If 1: `u16 CMID` (mission id 1000–1002), then `ACampaign::Serialize 0x40abd0` (`CMID`, `CRCH`, `CRMS` …). Only saved games use it. |
| u32 | `CLOCK` | game clock (Cmd `0x4630d0`/`0x4630f0`), 0 |
| u16 | `Z1` | scroll bounds x1 (`SGame+0x44`, `0x488020`/`0x47d3b0`) |
| u16 | `Z2` | x2 |
| u16 | `Z3` | y1 |
| u16 | `Z4` | y2. Every map has (0, 0)–(w·32−1, h·32−1). On load they are applied only if valid. |
| u16 n | `PW` | saved routes (`SGame+0x1a8`, 0x14 bytes each): `ptlist`, `u16 +0x10`, `u8 +0x12`. 0 in every map. |

The file ends here, with no trailer.

---

## 4. Things a map does *not* contain

- **Elevation:** there is none. Cliffs and chasms are tile art with terrain type 1.
- **Doodads:** the decorative scatter is part of the tile art (the TRVA variants). Trees, rocks and ruins are things (types 0x43–0x49).
- **Gizmos:** these are creature equipment (missile, C4 pack, …), not map objects. The `GIZMO*.MHK` files are their art. A map can grant them only through the trigger action SetGizmo.
- **Description, briefing and preview image:** none.
  - Campaign briefings and mission order come from `MISSION.MHK` and the campaign data (`ACampaign`, `0x40a830`).
  - The lobby preview is drawn from the `+` and `=` sections (§3.1).
- **Visibility:** only the per-player "explored" map and the fog flag are stored. Current visibility is rebuilt.

---

## 5. How the game loads and saves a map

### 5.1 Load

1. `SGame 0x480610(path, stage)` is called repeatedly with stages 2–8, so the progress screen can update between stages. It is called from `0x47ab30` and `0x4805d0`.
   - At stage 2 it checks the extension (`wbm`/`dbg`/`wbs`) and opens the archive with `0x46bee0(…, read = 0, path, 1)`. The archive is binary unless the extension is `dbg`.
   - At stage 8 it closes the archive.
2. `0x4812f0(archive, stage)`:
   - stage 2: `MapInfo 0x432330` ("?"), then `World 0x42e080` ("+"). World also builds the new `AITerrainAnalysis` (0x58 bytes, `0x4a7980` + `0x4a4550`, sized from `0x566db4`/`0x566db5`) and reads the pathing into it.
   - stage 3: `Players->vtbl[0]` (part 1), then `Effects->vtbl[0]` ("FX").
   - stage 4: `Things->vtbl[0]` ("!").
   - stage 5: `Events->vtbl[1]` ("ER").
   - stage 6: `Players->vtbl[1]` (part 2), then `0x4630e0` on Cmd.
   - stage 7: `0x482150` ("CAMP").
   - stage 8: clock, bounds and routes; then `0x4473d0` on Things, `0x433820(0x5517b0)`, and "Map loaded."

Terrain art (`TERRAIN.MHK`, `0x42db60`) and terrain types (`TERRAIN.HOO`, `0x42d790`) are loaded separately from the game folder, not from the map.

### 5.2 Save

- Game: `0x480be0(path)` opens a write archive and runs `0x4812c0`, which is stages 2–8 of `0x4812f0` in write mode. Its caller is at `0x47cfe4`.
- Editor: "save map" → `0x485360` → `0x4857e0` → `0x485810(stage)` for stages 2–8. This is the same code compiled into the editor.
- So the editor's output and a game save of a running mission differ only in content. That is also why a map is a full game state.

### 5.3 Archive

- Constructor `0x46bee0`.
- Field lookup `0x46c8f0(archive, out, tag)`. In binary mode it returns a view onto the same stream and the tag is ignored.
- Sub-archives `0x46cc50` and `0x46cfb0`.
- Primitives:
  - read u16 `0x409030`, write u16 `0x409070`;
  - read u32 `0x409050`, write u32 `0x4090a0`;
  - either direction: u16 `0x4090c0`, u32 `0x423660`, bytes `0x408f00(ptr, n)`;
  - `Pt` `0x4236b0`, `IList<Pt>` `0x485a10`, list of 3 × u16 `0x409500`.
- Archive+8 is the direction: 0 = load, 1 = save.
- **Text mode:** saving to a path ending in `.dbg` (string `0x5056a8`) writes a tagged text archive using the tag strings shown above. The game and the editor can both read it. This is a convenient way to inspect a map without a parser. **M** (from the code; not tried).

---

## 6. Editor and game differences

- **Same engine:** the editor is the game engine plus MFC tools. It shares the classes and the `0x4812f0`-style saver; its tag strings are identical. It writes nothing the game cannot read, and there are no editor-only sections.
- **Uninitialised bytes:** being a debug build, the editor writes 0xCD bytes in:
  - player names (`e`) and some `J` masks;
  - unset `Pt` y values (0xFFFF, 0xCDCD);
  - AGeneLab `f1e8`/`f1ec`;
  - trigger strings past the NUL;
  - parts of AI squads.

  The game loads them as they are.
- **Runtime state:** the editor saves its running simulation:
  - tick counter `GSC`;
  - creature paths and waypoints;
  - device power links;
  - AI grids and squads;
  - the target list, which the loader reads and discards (§3.9).
- **Recomputed or overwritten by the game on load:**
  - player `+0x1c[race]` and `+0x4850[race]`;
  - device activation when `built` ≠ 0;
  - the creature flag `+0x6c` from its genes (`0x405400`);
  - gene lab `+0x210`/`+0x21c` (reset before reading);
  - visibility counts (rebuilt).

  Pathing is **not** recomputed when the format is 4: a stale guide gives wrong pathing.
- **Ignored or redundant:**
  - The header MPWD/MPHT duplicate the tile counts. Every map has width tiles = 2·MPWD. The world uses the tile counts.
  - Terrain cells outside the map are never used.
  - Header `NPSP`/`NPRC` matter only to the map list and lobby.
- **Editor limits** (new-map dialog, editor DDX `0x405f60`; defaults read and written by `0x4038f0`):
  - width and height 32–96 cells, default 64;
  - players supported 2–8; recommended 2 to supported (spin ranges set in `0x406000`).

---

## 7. Limits

| What | Limit | Where |
|---|---|---|
| Map size | **96 × 96 cells = 192 × 192 tiles** (6144 px) | terrain array 96×96 (World+8 … +0x4808); fog arrays 96×96 per player; thing grid Things+0x10c 2 layers × 192 × 192; tile counts are u8 |
| Smallest map | editor minimum 32 cells (shipped minimum 32 × 32). Maps smaller than the HD frame need the view fix in `re/tutorial-terrain.md`. | |
| Players | 8 slots (fixed loops), races 0–3 (4 accepted), colours are palette bytes | `0x43ae20` |
| Things | ids are u16 and **id = slot index**. When the table reaches 0x0C01 the game inserts a null so id 0x0C01 ("none") is never used (`0x446e9f`, `0x450fa0`). Shipped maximum: 1340 slots. One ground thing per footprint, enforced by the placement checks and the per-tile grid. | `0x446db0` |
| Regions | u32 count, u16 ids (0xFFFF reserved), ≤ 255 links each | `0x4a65d0` |
| Triggers | u16 events, conditions and actions; strings 79 chars + NUL | `0x41d000` |
| Name | u16 length, no fixed buffer (the lobby list may truncate, **L**) | `0x432330` |
| Terrain ids | < 418 for a terrain type (TERRAIN.HOO), < 425 for art | |

---

## 8. Multiplayer, skirmish and campaign maps

- **Skirmish and multiplayer use the same files**, `DATA/MAPS/MPMAP*.WBM`. The default map is `MPMAP1.WBM` (`0x51b638`, `CMultiPlayer` settings `0x4619b0`).
  - The lobby enables one slot per player present in the file and defaults the slot's race to the file's race (`0x48f410`).
  - In every MP map:
    - slot 0 is human and slots 1… are computer players with full AI state;
    - each slot has one shaman;
    - `NPSP` equals the number of slots in the file.

    Thirteen of the 19 maps give the players nothing else. MPMAP2, 3, 9 and 11 also give some slots pods, devices or units. In MPMAP8 and MPMAP13 an extra computer player (slot 4) owns pods, refineries, relays and turrets, and on MPMAP8 also mines: it acts as a hostile "neutral". On MPMAP8 each of the 4 playable slots has two shamans and a starting unit.
  - How a lobby choice that differs from the file (a race, or human instead of computer) replaces the stored shaman and AI state was **not traced**; check it in game before relying on it.
- **Campaign maps** (`CAMPAIGN/SP<race><n><A|B>.WBM`) are the same format. They additionally rely on:
  - **triggers**: victory and defeat through PlayerEnd, timed reinforcements through AddCreature, scripted messages, terrain changes through CopyTerrain, movement locks through SetMobility;
  - fixed races and colours, since triggers name players by colour;
  - preset AI squads;
  - per-race CGK limits that follow the campaign's tech progression.

  Mission order, briefings and rewards are in `MISSION.MHK` and the campaign state, **not** in the map; `CFLG` is 0 in every map.
- **What an MP map needs:**
  - `NPSP` ≥ the number of player slots used;
  - one start cell (`:`) per slot (used to centre the camera, **M**), and normally a shaman per slot near it;
  - pod soil and pods;
  - processed pathing (format 4).

  Triggers are optional; MPMAP8 has two.

---

## 9. Writing a new map (for the in-game editor)

Minimum content that loads (**M**; none of this has been tried in game):

1. **Header:** version 5, NPSP/NPRC, size, name.
2. **Terrain:**
   - tile counts = 2 × the cell size;
   - 96×96 tile ids (use the TRVA variants of a kind to fill an area);
   - world counters 0;
   - `GAGC` 0.
3. **Pathing:** either write format 0 (the game will prompt and reprocess), or call `0x4a4eb0` in game and save its result.
4. **Players:** slots with race, colour 238−8·i, H, index, masks, CGK limits, start cell, fog 1, explored all 1, name 32 bytes, `PCBCF` one-hot.
5. **FX:** 36 zero bytes.
6. **Things:** in id order, ids equal to slot indexes, every body field set. Copy the defaults from a shipped thing of the same type; `tools/wbm.py --json` shows them.
7. **Triggers:** count 0, or events built from the tables above.
8. **AI:** for each computer player, marker 0 (the AI re-initialises; **M**) or a full block.
9. **Tail:** `CFLG` 0, clock 0, bounds (0, 0, w·32−1, h·32−1), routes 0.

Inside the game it is simpler to build the state in memory (`World`, `Players`, `Things`, `Events`) and call the game's own saver `0x4812c0` on a write archive. That guarantees the format.

---

## 10. Open questions

- The meanings of most `fN` fields in thing bodies, the player fields `J`, `XU`, `b`, `d`, `km`, `PEMM`, `MPTY`, and the AI grids. The layout is certain; the meanings are not.
- The first u16 of a `TRVA` resource.
- Whether marker 0 for a computer player gives the same behaviour as a freshly started AI.
- How the multiplayer lobby rebuilds a slot whose race or human/computer setting differs from the file.

---

## 11. tools/wbm.py

```
.venv/Scripts/python.exe tools/wbm.py --check              # all 54 shipped maps
.venv/Scripts/python.exe tools/wbm.py --dump <map.WBM>     # size, players, start cells, things by type, triggers
.venv/Scripts/python.exe tools/wbm.py --png <map.WBM> [out.png] [--scale 0.25] [--no-objects]
.venv/Scripts/python.exe tools/wbm.py --json <map.WBM>     # the parsed structure
```

- **Design:** one symmetric function per engine `Serialize()` (`ser_header`, `ser_terrain`, `ser_player`, `ser_object` …). The same code reads (it fills a dict) and writes (it emits bytes), so read and write cannot drift apart. `load()`/`parse()` return plain dicts; `dumps()`/`save()` write them back.
- **PNG:** the renderer draws each cell's frame from `DATA/TERRAIN.MHK`. If the running game has that file locked, it falls back to `wbedit/EditData/Terrain.MHK`. It then marks:
  - things: creatures as circles and devices as squares in the owner's colour, pods in lilac, obstacles in grey;
  - start cells as rings.

  Output goes to `build/`.
- **Round-trip result:** `--check` parses and re-serializes each file and compares byte for byte: **54/54 maps identical** (19 multiplayer, 35 campaign; 69 KB – 1.36 MB).
