# The smart terrain painter

A re-implementation of the 1998 map editor's automatic terrain transitions (the "Smart Palette") for the in-game editor. The research behind it is in `re/editor-original.md` §5.1. This note describes what was built:

- the code: `src/editor/terrain_paint.h` and `src/editor/terrain_paint.cpp`;
- the tests: `tests/terrain_paint/` (`build.sh`, `test_main.cpp`);
- the renderer for the test pictures: `tools/terrain_paint_render.py`.

Addresses are `MapEditor.exe` (image base 0x400000) unless marked "game" (`WARBREED.EXE`, GOG build).

---

## 1. Data: TERRAIN.MHK

The tables are read at run time from the game's own `DATA\TERRAIN.MHK`; nothing from the game is shipped. The file is a Mohawk archive (the same container `tools/mhkimg.py` reads), and every value in it is big-endian.

| Field (from the start of the file) | Meaning |
|---|---|
| 0 | `"MHWK"`, then a u32 size |
| 8 | `"RSRC"`, u16 version, u16 compaction, u32 size |
| 20 | u32 offset of the resource directory |
| 24 | u16 offset of the file table, counted from the directory |
| directory | u16 name-list offset, u16 type count, then 8 bytes per type: 4-char tag, u16 resource-table offset, u16 name-table offset (offsets from the directory) |
| resource table | u16 count, then (u16 id, u16 file index, 1-based) |
| file table | u32 count, then 10 bytes per file: u32 offset, u16 size, u8 size bits 16–23, u8 flags, u16 unused |

The painter uses five resource types. They are identical in the editor's data and the retail data.

| Resource | Ids | Contents |
|---|---|---|
| `TRID` | 1000–1221 | u16 **class** (0–23) of each of the 222 tile **kinds** |
| `TRST` | 1000–1221 | 4 × u16 **corner types** of the kind: top-left, top-right, bottom-left, bottom-right (0–13) |
| `TRVA` | 1000–1221 | u16 thumbnail (an editor bitmap resource), u16 *n*, then *n* × (u16 cumulative percent, u16 tile id), then 100 |
| `TRPL` | 1000–1044, 2000 | palettes: u16 count, u16 0, then count × (u16 default flag, u16 id, u16 category) |
| `TRTH` | 1000–1046 | (u16 thumbnail, u16 world thing type) for obstacles, crops, creatures, devices |

**Tile ids.** A tile id is a frame of `tBMH` 9000 (64×64 px), the value stored per cell in the map and in AGrid (game 0x55dd60, cells at +8 = 0x55dd68, `cells[y*96+x]`). The kinds list tile ids 0–417; tile 167 is in no kind (see below) and isn't used by any shipped map.

**Corner types** (re/editor-original.md §5.1): 0 crystal, 1 crushed crystal, 2 crystal canyon, 3 tar, 4 hardened tar, 5 lava, 6 sand, 7 roots, 8 hot mud, 9 cropland (edge), 10 cropland interior, 11 canyon wall, 12 cliff plateau, 13 cliff detail; 14 means "keep the existing corner" in lookups.

**What the corner types mean, as the data and the code show:**

- 9 against 10. Cropland corners are 9; a cropland corner whose four surrounding cells are all cropland becomes 10, the lighter interior texture.
- 11, canyon wall. A canyon is vertex type 2. The vertex directly above a canyon vertex is always 2 or 11, never 0: going down a column you read crystal, wall, canyon. The cell holding the wall face has top corners 11 and bottom corners 2 (kind 1043). So a canyon's far (north) wall is a whole cell tall and is drawn automatically.
- 12, cliff plateau. The 12 cliff kinds (1201–1212) form a 0/12 border set. The all-12 kind 1221 reuses crystal's tiles (0, 1, 2, 38–43). The original builds its tile-to-kind map without kind 1221, so a plateau top reads back as plain crystal (corners 0000). This is deliberate: only the edge tiles carry the cliff.
- 13, cliff detail. Kinds 1213–1220 are only in the Crystal Cliffs palette; the smart painter never makes them. Kinds 1219 and 1220 have the same corners, so the table gives 1220.

**TRVA, the variants.** `0x452b60` reads exactly *n* pairs. Variant *i* is chosen when `rand() % 101` is at most the *next* pair's percent (the last one takes the rest; `0x452fa0`, game `0x455a30`).

- Kinds 1088 and 1163 list one pair more than their count. The original ignores the extra pair, so tile 167 belongs to no kind. Tile 100 belongs only to kind 1047 (cropland), not to lava border 1163.
- The game picks variants with its own deterministic RNG (game 0x471320); the editor uses `rand()`. The painter uses MSVC's `rand()` sequence, seedable.

**The 15⁴ table** (`0x43c600`). Kinds are entered in id order, so a later kind wins. Four corner tuples are shared:

| Corners | Kinds | Table gives |
|---|---|---|
| 0000 | 1000, 1018 | 1018 |
| 6666 | 1083, 1108 | 1108 |
| 3333 | 1135, 1148 | 1148 |
| 0,0,13,12 | 1219, 1220 | 1220 |

The first three losers' tiles are all also in the winner, so nothing is lost. The tile-to-kind map (`0x43c600`/`0x43c7b0`) skips the last kind (1221) and is also "later wins".

**TRPL, the palettes.** `TRPL 1000` is the drop-down: 16 entries of category 1, each pointing at a one-item TRPL whose category-0 link is the palette. Their names are the editor's strings 342–357; the shell gets them from `Palettes()`:

1. Smart Palette (1022: 11 terrains, 6 obstacles, ruins, 3 crops)
2. Roots
3. Sand Cropland
4. Hot Mud
5. Crushed Crystal
6. Crystal Cropland
7. Crystal Canyons
8. Hardened Tar
9. Tar Cropland
10. Lava
11. Cropland
12. Sand/Crystal
13. Tar/Crystal
14. Sand/Tar
15. Sand/Crystal/Tar
16. Crystal Cliffs

`TRPL 2000` adds Creatures (1020) and Devices (1021). The categories are 2 = tile kind, 3 = obstacle, 4 = creature, 5 = device, 6 = crop. The Smart Palette's terrains, in its order, are crystal (1018), canyon (1017), crushed crystal (1044), sand (1108), hot mud (1121), roots (1122), tar (1148), cliff (1221), lava (1161), hardened tar (1174) and cropland (1045).

**The compatibility table** (`0x440aa0`, `Allowed(paint, existingClass)`) is the table in re/editor-original.md §5.1, checked against the decompile.

---

## 2. The algorithm as implemented

The painter works directly on tile ids through `editor::Grid` (`src/editor/painter.h`). A cell's corners are always read from its tile, as the original does; there is no separate corner layer to keep in sync, so maps edited by hand or by other tools need no conversion.

Each brush cell runs the original's dispatcher (`0x43ca70`) on the Smart Palette kind of the chosen terrain. Each routine below is a port of the original named by its address.

**Generic painter** (`0x43cc70`), for sand, tar, lava, hot mud and crystal:

1. **Area check** (`0x440720`): every cell of the 3×3 must accept the new class. If not: "You cannot paint that there".
2. **Propagation** (`0x43f730` → `0x43f850`), for the four diagonal neighbours.
   - A diagonal neighbour is about to get the new type T at the one corner it shares with the painted cell. If that would make a **saddle**, the same type on two diagonal corners and something else on the other two (no such tile exists), its two side corners also become T. Each side changes only if a cell beyond it accepts T.
   - That neighbour's new kind is then placed.
   - Neighbours of it that now have no tile ("retry list") in its row or column are painted T in full, by the generic painter.
   - Propagation then continues from the neighbour, recursively.
3. **Re-validation** (`0x440510`): a diagonal neighbour still in a saddle with T on its far corner must accept T all round.
4. **Placing** (`0x440030`): the kind is stamped, and each of the 8 neighbours gets the kind's corners where they touch, keeping its other corners (`0x4403c0`). If no kind has those corners, the cell is left as it is; that is the original's "Couldn't find tile!".

**Subtypes: crushed crystal, hardened tar, roots** (`0x43dab0`).

- If the 3×3 fails only because of cropland-interior cells (`0x440840`), the interior corners there first go back to edge (10 → 9, `0x43f530`).
- Then the generic painter runs.

**Cropland** (`0x43ea20`).

- It must go on crushed crystal, roots, hardened tar or cropland ("Invalid combination" otherwise), with a 3×3 that accepts it.
- After the generic paint, each of the cell's corners whose four surrounding cells are all cropland becomes interior (10) (`0x43eb90` with `0x43ed30`, `0x43ef30`, `0x43f130`, `0x43f330`). A corner is not promoted if that would leave one of those cells with interior on two diagonal corners only.
- The promoted kind is placed, so the neighbours follow.

**Canyon** (`0x43dbc0`).

- The area check covers 3 columns and 4 rows, and is repeated up and down past wall cells (`0x43de60`, `0x43deb0`, `0x43e140`).
- The cell becomes canyon (2222).
- The cell above becomes the far wall: kind (a, b, 2, 2), where a and b are 2 where that cell's top corner was canyon, else 11. They are both 2 if the cell two rows up has canyon on its top edge, which closes a one-row gap.
- If cells below can't follow, the canyon extends down; if the wall can't, it extends up. Corner cells beside the wall's top are trimmed (`0x43e300`).
- A canyon cell right above another canyon's wall joins the two (`0x43e5c0`).

**Crystal over a canyon** (`0x43e6c0`).

- The canyon is filled.
- The cell below gets a new wall top, (0, 0, 11, 11) where it had canyon.
- Cells above walk up through old wall cells while they can't follow.

**Crystal over a cliff edge** (`0x43daa0`) does nothing, as in the original.

**Cliff** (`0x43cd70`).

- **Over crystal** (`0x43ceb0`): a crystal cell touching a cliff tile's plateau corner is already on the plateau, so nothing happens. Otherwise the generic painter paints the plateau kind (1221), and every neighbour gets plateau corners.
- **Over a cliff edge** (`0x43d2e0`, check `0x43d640`): the cell becomes plateau, and the neighbours on its lowland sides get plateau corners.

**Brushes.**

- `PaintCircle` paints the cells with dx²+dy² ≤ r²+r. `PaintSquare` uses the shell's square brush (`BrushFirst`/`BrushLast`). `FillRect` is Auto Fill (`0x43c860`).
- Cells are painted one at a time in row order, like the original's per-cell Auto Fill.

**Per-cell transaction and guard.** Each brush cell is a transaction.

- If the dispatcher refuses it, every write it made is undone, and the original's message is returned.
- With the guard on (the default), a paint that would leave a **seam** where there was none is also undone and refused with "Couldn't find tile!" (see §3 for what a seam is).
- `Result.changes` lists each changed cell once, with its value before and after, for undo and redraw.

---

## 3. Validation: seams and cliff-line ends

`CellMismatch` compares each corner of a cell with the corners its neighbours give the same vertex:

- **2, a seam.** Two textures meet with no border tile, for example sand against tar.
- **1, crystal against plateau (or cliff detail) only.** All three are drawn with crystal texture, so this only ends a cliff line. The shipped maps are full of such "open terraces": rows of cliff-edge tiles with plain crystal on both sides, hand-placed from the Crystal Cliffs palette. The original painter also reads plateau tops as crystal, so painting another terrain on a plateau gives crystal-based borders that meet the cliff edges this way (see `cliffs_edited.png`).
- A plateau-art tile (kind 1221's crystal tiles) is judged from its own side: it is consistent when its other neighbours give crystal at every vertex, or plateau at every vertex.

On the 54 shipped maps, 38 of 251,608 cells have a seam, all hand-placed:

- canyon walls over crushed crystal (MPMAP1);
- wall corners of neighbouring canyons that don't meet (MPMAP15, SPTAN12A, SPMAG3A, SPMAG9A);
- a few crushed-crystal borders.

2,183 cells have crystal/plateau differences only.

---

## 4. Tests

`tests/terrain_paint/build.sh` builds the module and `test_main.cpp` with the 32-bit MinGW g++, runs them, and renders `build/terrain_paint/*.png`. The full output is `build/terrain_paint/report.txt`.

The maps are the 54 shipped maps:

- MPMAP*.WBM from DATA/MAPS and SP*.WBM from CAMPAIGN;
- read from the untouched `testgame/` copy;
- other .WBM files, such as the editor's own test maps, are ignored.

### 4.1 Re-derivation on the shipped maps

| Test | Result |
|---|---|
| **A.** Each tile's own corners (tile → kind → TRST) looked up in the 15⁴ table give a kind that has this tile as a variant | **251,578 / 251,608 = 99.988 %**. The 30 misses are all kind 1219, which shares its corners with 1220 and so can only have been placed by hand from the Crystal Cliffs palette. |
| Same, but the exact tile id with a deterministic variant choice (most likely variant) | 43.1 %. Variants are random in the original; see the distribution below. |
| **B.** Corners recovered from the **neighbours** (majority vote at each vertex), then table, then variant | **98.920 %**; **99.985 %** counting crystal-vs-plateau-only differences. |

The 38 remaining misses of test B:

| Count | Cause |
|---|---|
| 30 | kind 1219 (as in A) |
| 5 | neighbours give a combination with no tile (hand-placed canyon walls over crushed crystal, MPMAP1) |
| 3 | other hand-placed canyon wall or border tiles whose corners differ from their neighbours |

The 2,679 crystal/plateau-only differences are the open cliff lines described in §3 (47 of them on cliff detail pieces).

So wherever the original is deterministic, the selection logic reproduces every tile the smart palette could have placed. Every miss is a tile placed by hand from a terrain-specific palette.

**Variant use against the TRVA weights.** The designers' maps follow the random weights closely, which shows the tiles came from the random painter:

| Kind | TRVA weights | Shipped maps |
|---|---|---|
| Crystal 1018 | 32/31/32 % for tiles 0–2, 1 % each for 38–43 | 32/31/32 %, 1 % each |
| Sand 1108 | 31/30/31 % and 1 % each | 31/30/31 % and 1 % each |
| Tar 1148 | 31/31/31 % and 1 % each | 32/30/31 % and 1 % each |
| Lava 1161 | 34/33/33 % | 35/32/33 % |
| Canyon 1017 | 13/13/74 % | 9/9/83 % (the canyon code places this kind without the brush as well) |

### 4.2 Paint scenarios

All on 32×32 grids unless noted. Every one ends with **0 seams**, and 0 crystal/plateau differences except where a plateau is painted over on purpose.

- Circles of radius 0, 1, 2, 3, 4 and one at the map edge:
  - on crystal: sand, tar, crushed crystal, canyon, cliff;
  - on sand: crystal, tar, roots, hot mud;
  - on tar: crystal, sand, hardened tar, lava.
- Refusals as in the original: crushed crystal on sand ("You cannot paint that there"), cropland on sand ("Invalid combination"), canyon on sand.
- Cropland on roots, crushed crystal and hardened tar. Brushes and an Auto Fill each give an interior, with 81 interior tiles. The subtype painted back into the field turns the interior around it into edge.
- A sand/crystal split by Auto Fill, a tar line across both, a canyon line down the crystal, and crystal painted back over the canyon.
- Cliffs: single cells, a radius-2 brush, an Auto Fill block, and a radius-1 stroke; then edited with cliff, crystal and sand.
- Lava and hot mud pools, then their base terrains painted back.
- Undo puts a 109-cell paint back exactly.
- 2000 random brush paints (all 11 terrains, radius 0–3) on a mixed 48×48 map: 0 seams at any point. The guard never had to act, and the run with the guard off gives the same map.
- 400 random paints each on MPMAP1, MPMAP10, SPTAN5A and SPMAG8A: no new seams.
- Speed: Auto Fill of a whole 96×96 map takes about 34 ms; a radius-4 brush about 0.16 ms.

Each scenario is written as `<name>.tpg` (before and after) and rendered to `<name>.png`:

- before on the left, after on the right;
- seam cells outlined red, crystal/plateau cells cyan;
- changed cells marked with a small yellow tick.

---

## 5. Differences from the original

Deliberate:

1. **Transactions and the guard.** The original keeps whatever a refused or failed paint had already written, and leaves "Couldn't find tile!" cells with corners that disagree with their neighbours. Here a refused brush cell is undone, and one that would leave a new seam is refused.
   - The original's known seam case is a cliff painted on a cliff edge that touches a canyon wall: `0x43d2e0` has no area check.
2. **No variant re-roll.** The original stamps a fresh random variant every time a cell is painted, even with the same terrain. Here a cell that already shows a variant of the kind keeps it, so dragging over finished terrain changes nothing and the undo records stay small.
3. **Plateau interiors.**
   - The original only looks at a cell's 8 neighbours to see whether it is already on a plateau, so painting cliff deeper inside a plateau raises a second plateau on it. A brush hits such cells constantly.
   - Here a crystal cell is treated as plateau when the first non-crystal tiles along its row and column face it with plateau corners (`OnPlateau`; at least two such directions, and more than face it with lowland).
   - A plateau on a plateau can therefore no longer be painted with the uniform cliff.
4. **Retry list.** After a propagation, the original repaints only the first failed neighbour in the neighbour's row or column, because the list is emptied while it is walked. Here every such neighbour is repainted.
5. **Interior demotion** (`0x43f530`). The original repaints all nine cells of the 3×3; here only cells that have an interior corner are repainted.
6. **Crystal over canyon.** The cell below gets a wall top only where its bottom corners were canyon or wall. The original turns any non-crystal corner into wall, even sand or tar, which would push walls into other terrains.
7. **Cropland promotion.** If two promoted corners end up alone on a diagonal (no tile exists for that), the cell isn't promoted. The original would place a null kind.
8. **Recursion limit.** There is a depth limit of 4000; a paint that reaches it is refused. The original has none.
9. **Brushes and messages.**
   - The brushes are new: the original painted one cell per mouse cell.
   - Cliff painted on any other terrain, or a refused cliff-on-edge, says "You cannot paint that there"; the original says nothing.
   - Outside the map the original reads tile 0x13e (lava) from the 96×96 array; here the routines check the map bounds instead, with the same effect.

Not done, because the shell owns them:

- obstacles, ruins and crops (TRTH);
- creatures and devices;
- the event editor;
- pathfinding reprocessing before save.

---

## 6. Integration notes

**API.** `editor::TerrainPainter` holds the tables and the routines.

- `SmartPainter` adapts it to the shell's `editor::Painter` interface (`painter.h`): the types are the 11 Smart Palette terrains, and `Pick` gives the terrain under the cursor.
- `Palettes()` lists all 16 terrain palettes plus Creatures and Devices, with display names and a preview tile per kind.
- `StampKind` stamps one kind as is, for the terrain-specific palettes.

**Loading.**

- `LoadFirst({"DATA\\TERRAIN.MHK", ...})` reads the whole archive (1.7 MB) once.
- It opens the file with `CreateFileA` read-only and `FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE`, so a running game holding it open doesn't matter.
- `LoadFromMemory` takes a buffer instead.
- The game's own resource loader doesn't help here: it loads only TRID and TRVA (game `0x42db60`), not TRST or TRPL.

**The grid in the game.**

- Cells are `*(uint16_t*)(0x55dd68 + 2*(y*96 + x))`.
- Width and height in cells are `[0x566db4] >> 1` and `[0x566db5] >> 1` (map size in 32 px tiles).
- The game's tile write, `ATerrainTile::PaintAt` (game 0x4559e0), just stores the id and picks the next variant, so writing the array directly is equivalent.

**After a paint, the shell must:**

1. **Redraw.** Mark the changed cells dirty (AGrid +0x4848 dirty buffers), or call `AGrid::MarkAllCellsDirty` (game 0x42f210) with (1, 0).
2. **Lava animation.** Rebuild the tile animation list (`AGrid::BuildTileAnimList`, game 0x42ff30), which scans the class-5 cells, so new lava animates.
3. **Pathing.** Before saving, reprocess the map (re/wbm-format.md §3.4).

Passability comes from `TERRAIN.HOO` per tile id and needs nothing.
