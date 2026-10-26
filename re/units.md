# Units: object hierarchy, stats, gene pool, rally points

> Research report (static analysis, nothing run). Confidence: **H** read from the code, **M** mechanism clear but purpose inferred, **L** a guess. Addresses are authoritative; where a name here differs from `re/names.tsv`, the TSV is the current one (and the export `re/ghidra-export-v2/` uses it). Index: `re/subsystems.md`.


Confidence: **H** = read from the instructions (decompiler checked against `re/text.asm`), **M** = mechanism clear, purpose inferred, **L** = guess.
All addresses are in WARBREED.EXE (GOG build). "World" is the object at `[0x579278]`; its RTTI class is **`AThingRegistry`** (H, see 1.6).
Gameplay data offsets refer to `DATA/GAMEPLAY.DAT` (12 826 bytes).

---

### 1. Object hierarchy (things on the map)

```
AThing (0x504bb4, 31 slots, base size 0x28)
 |- AObstacle  (0x50453c)  types 0x43..0x49 ruins/trees/rocks       size 0x2c
 |- AMushroom  (0x504b04)  types 0x3f..0x41 Pod/Spore/Weed (planted) size 0x34
 |- ADeath     (0x503354)  type 0x42 Corpse                         size 0x2c
 |- ADevice    (0x5033ec, 43 slots)  buildings 0x2c..0x3e          size 0x40
 |    |- ARefinery 0x2c (44 slots)  ARelay 0x2e/0x36  ABattery ("Psi") 0x2f/0x38
 |    |- AGeneLab 0x2d/0x35  AMine 0x30  ATurret 0x31/0x37  AElectricTower 0x32
 |    |- ARadar ("Listener") 0x33  ARevitalizer ("Healer") 0x34  AWarp 0x39
 |    |- AHowitzer ("Summoner") 0x3a  ACollector 0x3b  (0x3c..0x3e: plain ADevice, unused)
 |- ACreatureBody (0x5030fc, 43 slots; slots 0x8c..0x98 pure)
      |- ACreature (0x509314, 51 slots)  creature types 0..0x2b       size 0x154
           |- AShaman (0x504a2c, 53 slots)  types 0, 9, 0x12, 0x1b   size 0x1b8
```

#### 1.1 Type ids (`AThing+0x18`, int) (H)
Ranges are tested everywhere (`World::Click`, `0x447a90`, `0x4464c0`): **0..0x2b creatures, 0x2c..0x3e devices, 0x3f..0x41 pods, 0x42..0x49 other**; 0x4a (74) = "none" (`AShaman+0x184/+0x188` init).
Display names are in the gameplay table `g_ThingDisplayName` 0x550178 (char[74][20]); art/file stems in `g_ThingFileName` 0x54eeb8 (char[74][20], e.g. `tanu2` → `TANU2.MHK` + `TANU2.SKT`). Script/debug names: `ThingTypeName 0x410df0`.

| ids | clan block (9 slots each) | 0 | 1 | 2 | 3 | 4 | 5..8 |
|---|---|---|---|---|---|---|---|
| 0x00..0x08 | Tanu | Shaman | Scout | Raptor | Ophidian | Jubjub (air) | unused |
| 0x09..0x11 | Sen-Soth | Shaman | Snipe | Myrmidon | Dhuganaya | Aphid (air) | unused |
| 0x12..0x1a | Kelika | Shaman | Thumper | Mongrel | Behemoth | Leviathan | unused |
| 0x1b..0x22 | Magha | Shaman | Shadow | Dervish | Reaver | Viper (air) | unused |
| 0x23, 0x24 | neutral | Yedda (0x23), Dhuerenya (0x24); 0x25..0x2b unused | | | | | |

Air units: types 4, 0xd, 0x1f (layer 1 of the World grid; they get an `AShadow` effect, `ACreatureBody` ctor; `0x446db0`). (H)

Devices (= build-list index i + 0x2c; the build list is `AShaman` mode 2, see `re/hotkeys.md`): 0x2c Refinery, 0x2d Gene Lab, 0x2e Relay, 0x2f Psi (Psi Tower, class ABattery), 0x30 Mine, 0x31 Turret, 0x32 E. Tower, 0x33 Listener (ARadar), 0x34 Healer (ARevitalizer), 0x35 Gene Lab+, 0x36 Relay+, 0x37 Cannon ("Turret+"), 0x38 Psi+, 0x39 Warp, 0x3a Summoner (AHowitzer), 0x3b Collector. Upgrades (Upgrade button, `0x4428b0`/`0x4436b0` case 0x38): 0x2d→0x35, 0x2e→0x36, 0x2f→0x38, 0x31→0x37. (H)

Pods (Plant, `AShaman+0x188 = 0x3f + i`): 0x3f Pod, 0x40 Spore, 0x41 Weed. Others: 0x42 Corpse, 0x43 Yedda Ruins, 0x44/0x45 Crystal Tree/Rock, 0x46/0x47 Sand Tree/Rock, 0x48/0x49 Tar Tree/Rock. (H, names from the table)

#### 1.2 Construction: the factory (H)
- **`World_NewThing 0x447a90`** `AThing* __stdcall (short id, int type, short playerIndex, short x, short y)` (ret 0x14): `operator new 0x4eba50(size)` then the ctor; owner = `APlayerRegistry 0x43ada0([0x57927c], playerIndex)`. x/y are the **top-left** corner; the ctor adds footprint/2 to get the centre.

| type | class | new size | ctor | dtor (vtbl reset) |
|---|---|---|---|---|
| 0,9,0x12,0x1b | AShaman | 0x1b8 | 0x4406b0 | 0x440940 |
| other 0..0x2b | ACreature | 0x154 | 0x49a980 (pos passed as `IPixelC*`) | — (slot 0 0x49ac80) |
| 0x2c | ARefinery | 0x98 | 0x43dc70 | 0x43eb40 |
| 0x2d, 0x35 | AGeneLab | 0x228 | 0x428540 | 0x42a550 |
| 0x2e, 0x36 | ARelay | 0x84 | 0x43ed90 | 0x43f430 |
| 0x2f, 0x38 | ABattery | 0x40 | 0x4010a0 | 0x4017c0 |
| 0x30 | AMine | 0x40 | 0x432910 | 0x432cb0 |
| 0x31, 0x37 | ATurret | 0x48 | 0x455da0 | 0x456c00 |
| 0x32 | AElectricTower | 0x44 | 0x40fc20 | 0x4107e0 |
| 0x33 | ARadar | 0x44 | 0x43d100 | 0x43daa0 |
| 0x34 | ARevitalizer | 0x44 | 0x43f570 | 0x43fd50 |
| 0x39 | AWarp | 0x54 | 0x4573d0 | 0x458700 |
| 0x3a | AHowitzer | 0x50 | 0x430590 | 0x4314b0 |
| 0x3b | ACollector | 0x44 | 0x40b590 | 0x40bdf0 |
| 0x3c..0x3e | ADevice | 0x40 | 0x40c810 | 0x40f640 |
| 0x3f..0x41 | AMushroom | 0x34 | 0x444aa0 | 0x445610 |
| 0x42 | ADeath | 0x2c | 0x40c300 | 0x40c490 |
| 0x43..0x49 | AObstacle | 0x2c | 0x434ae0 | 0x435240 |

Base ctors: `AThing 0x445740` (dtor 0x445800), `ACreatureBody 0x402200`, `AGeneLabInterface 0x42b5d0` (embedded in AGeneLab at +0x40, dtor 0x42b780).

Callers of the factory: **`AThingRegistry::CreateThing 0x446db0`** `(short player, int type, short x, short y, short skipChecks, short activate, short ownerArg)` (ret 0x1c) does the placement checks (grid cell `World+0x10c` free, footprint, Refinery site `0x4518c0`, power `0x44ed20` with `g_DevicePower`, pathing `0x44db90`), takes a recycled id from `World+0x40/+0x44` during load (`World+0x78`) or appends to the thing table (`World+0xc` list, data `+0x10`, count `+0x14`), links the grid (`0x448460`) and returns the id or 0xc01. Wrappers: **`PlaceCreature 0x4472d0`** (adds 4 gizmos, 100 = none; this is what the Gene Lab, scripts and the HD autopilot use) and `0x447410` (device from a builder, see `src/game.h`). (H)

#### 1.3 AThing virtual slots (vtable 0x504bb4; offset = slot*4) 
| off | name | base impl | notes / overrides |
|---|---|---|---|
| 0x00 | DeletingDtor | 0x4457e0 | |
| 0x04 | Draw(camX, camY) | 0x445810 | base draws the HP bar when selected (`0x445850`, sprite 0x157+frame; offsets `g_HpBarX`/`g_HpBarY`); ADevice 0x40ccc0, ACreature 0x49b500, AShaman 0x440a10 (H) |
| 0x08 | Tick() | 0x445950 | per game tick; base decrements `+0x04`. ADevice 0x40d5c0 (death anim, power reconnect, anims), ACreatureBody 0x4037e0 = `Think()` (+0x84) then `Act()` (+0x88), AGeneLab 0x428f30 (breeding), AMushroom 0x444d00 (growth) (H) |
| 0x0c | OnRemoved(short quiet) | 0x445960 | releases mouse capture; ADevice 0x40d8c0 / ACreatureBody 0x403ee0 play the "lost" voice for the local player and drop the population count (M) |
| 0x10 | ValidateRefs() | 0x40d5b0 (empty) | clears ids of things that no longer exist: ADevice 0x40dfa0 (+0x34), AShaman 0x443500 (+0x18e) (M) |
| 0x14 | DrawPanel(x, y, camX, camY) | 0x445a50 | info panel (title = display name). AShaman 0x442f10, AGeneLab 0x429730 (also draws the rally path: `World::DrawWaypointPath 0x448f10` at 0x4297f9, in battlefield pixels but in the interface phase; WarBreeds HD draws it into the back buffer, `layers.cpp`. The other callers, `SGame::DrawPlacement 0x47bd95` and `SGame::DrawWorldMarkers 0x47bef6` (waypoint mode), draw in the battlefield phase), ACreature 0x4a17b0 (H) |
| 0x18 | PanelClickDown(sx, sy, wx, wy, zoom) | 0x445a70 | AShaman 0x4419c0, AGeneLab 0x429990 (H) |
| 0x1c | PanelClickUp(sx, sy, wx, wy) | 0x435b00 (ret 0) | AShaman 0x442230, AGeneLab 0x429a30 (H) |
| 0x20 | MouseMove | 0x435b00 | AShaman 0x442d60 (H) |
| 0x24 | CursorId(sx, sy, wx, wy, zoom) | 0x446460 (ret 0) | AShaman 0x442db0 (H) |
| 0x28 | OnAttacked(attackerId) | 0x445b90 | forwards to owner `APlayer` vtbl+0x10; towers/mines/creatures override to retaliate (M) |
| 0x2c | HitTestCircle(ICircle*) | 0x445ac0 | distance < `g_ThingRadius[type]` + circle radius (H) |
| 0x30 | IntersectsThing(AThing*) | 0x445b40 | builds an `ICircle` from own centre/radius and calls other+0x2c (H) |
| 0x34 | SetSelected(short) | 0x445a30 | writes `+0x06`; AShaman 0x443430 also cancels its mode (H) |
| 0x38 | Die(killerId) | 0x401ab0 | kill statistics `0x4383d0`, owner vtbl+0x30 (M); ACreature 0x440890 |
| 0x3c | (unknown, draws via 0x4695b0/0x4d680b) | 0x445980 | AObstacle 0x435150 (L) |
| 0x40 | DebugCommand(char*) | 0x47a530 (empty) | ACreature 0x4a29d0 handles "creature paths on/off" (H) |
| 0x44 | TakeDamage(int dmgClass, short amount, short extra, short attackerId) | 0x445ba0 | `hp -= (8-armor)*amount/8 + extra + rand&1`, armor = `g_Armor[dmgClass][type]` (+single-player difficulty bonus `0x53e0fc`, cap 8); at hp<1 calls Die (+0x38) and `World::RemoveThing 0x4481e0`. Marks the attacker `+0x04 = 0x20` (H) |
| 0x48 | Serialize(archive*) | 0x446020 | tagged fields "A" type, "B" id, "C" owner index, "D"/"E" top-left x/y (header pass), then centre and HP. Every class overrides it (save games, H) |
| 0x4c | SetOwner(APlayer*, short notify) | 0x4464c0 | capture/convert: unlinks grid, notifies old owner (vtbl+0x1c) and new owner (vtbl+0x18) (H) |
| 0x50 | GetMaxHP() | 0x446010 | `g_MaxHP[type]`; creatures 0x405390: `base + base/2 * veteranLevel` (H) |
| 0x54 | GetSightRange() | 0x446480 | `g_SightRange[type]` px; creatures 0x4053c0: +0x80 with Eyestalk, +0x100 with Eyestalk+ in gizmo slot 1 (H) |
| 0x58 | Checksum() | 0x446720 | mixes type, id, hp, x, y (multiplayer sync); ADevice 0x40f600, ACreature 0x4a2ec0 (M) |
| 0x5c | SetHP(short) | 0x446490 | clamps to max; creatures 0x406e80 also store `+0x80 = max - hp` (H) |
| 0x60 | IsActive()? | 0x412ca0 (ret 0) | AMine returns 1; ACreatureBody 0x407150: `+0x40 == 0` (L) |
| 0x64 | GetPosition(IPixelC* out) | 0x4466a0 | copies the centre (H) |
| 0x68 | IsBusy() | 0x401a00 (ret 0) | ACreature 0x440860: activity != idle or a target is set (H) |
| 0x6c | GetAttackRange()? | 0x401830 (ret 0) | ACreatureBody 0x404990 sums gizmo reach (`g_GizmoReach`, projectile range `0x550018[effect]`) (M) |
| 0x70 | GetBuildTime() | 0x446470 | `g_BuildTime[type]`; creatures 0x406d00 add `g_GizmoBreedTime[g]` per gizmo (H) |
| 0x74 | GetAnimRate() | 0x401a10 (0x10000) | creatures 0x409e80: by `g_BodySize` 0/1/2 → 0x11000/0x10000/0xf000, ±0x800 jitter (H) |
| 0x78 | (unknown) | 0x412ca0 (ret 0) | AElectricTower, AMine: 0x4107d0 (ret 1) (L) |

#### 1.4 ADevice extra slots (vtable 0x5033ec)
0x7c 0x410990 (empty, ret 0xc; ARefinery 0x43e060, ARelay 0x43f1a0: power-client bookkeeping, L) · 0x80 empty (ARefinery 0x43e0d0, ARelay 0x43f210, L) · **0x84 DyingTick 0x40d790** (collapse animation, removes the thing at the last frame; +0x2e counts frames, M) · **0x88 SwitchOn 0x40e1d0** (`+0x2a = +0x28 = 1`, then 0x90) · **0x8c SwitchOff 0x40e1f0** · **0x90 ConnectPower 0x40e020** (`World 0x44eac0` finds a provider, else unpowered) · **0x94 AttachToProvider(id) 0x40e130** · **0x98 DetachPower 0x40e1b0** · 0x9c NotifyOwnerPower(flag) 0x40e310 (owner vtbl+0x24) · **0xa0 OnCompleted 0x40de70** ("X complete" voice; AGeneLab 0x428d40 also resets the rally point) · 0xa4 empty · **0xa8 SetOn(short) 0x40e2a0** (the double-click / `Cmd+0x50` toggle). ARefinery has one more slot 0xac (0x43e310). (H unless marked)

#### 1.5 ACreatureBody / ACreature / AShaman extra slots
ACreatureBody 0x7c DrawBody? 0x402520 (L) · 0x80 IsMoving 0x409880 (ACreature 0x4a2240: has a path, no target) (M) · **0x84 Think** (base empty; ACreature 0x49b670) · **0x88 Act** (ACreatureBody 0x403800, ACreature 0x49b6b0, AShaman 0x440e00: runs the activity) (M) · 0x8c..0x98 pure (ACreature 0x49bd50, 0x49bab0, 0x49c200, 0x49c430: movement/pathing steps, L) · 0x9c empty (ACreature 0x49cb80: stop and guard here, M) · 0xa0 IdleFidget 0x404b20 (random idle behaviours, L; AShaman 0x443ef0) · 0xa4 / 0xa8 empty (ACreature 0x4a1c10 / 0x4a16f0, L).
ACreature (orders; each first clears the previous one via 0xc8 and wakes an immobile/autonomous stance to normal): **0xac Attack(targetId) 0x49f6c0** (activity 0x2a) · **0xb0 SpecialAbility 0x49f640** (`Cmd+0x74`, "Leviathan special") · **0xb4 FollowPath(PtList*, flags) 0x49f0c0** · **0xb8 Stop 0x49f600** (AShaman 0x441980 also cancels its mode) · **0xbc Hold 0x49f710** (`Cmd+0x78`; guard point +0x104) · **0xc0 EnterBuilding(id) 0x49f320** (+0x148; Healer 0x34 / Warp 0x39) · 0xc4 0x49ff70 (reacts to another creature, L) · **0xc8 ClearOrders 0x49ac10** (AShaman 0x441250). AShaman: **0xcc IsAtWorkSite 0x4411d0** (within radius + `0x51b2dc` = 64 px of +0x16c/+0x16e) · 0xd0 0x4434e0 (0xc4 unless working). (H unless marked)
Non-virtual: `Patrol(PtList*, IPixelC* loopPt) 0x49f1e0`, `Follow(id) 0x49f7e0`, `Halt(short) 0x49f870`, `SetStance(int) 0x49f8c0`, `ACreatureBody::SetGizmo(slot, gizmo) 0x405230`, `ACreatureBody::SetVeteranLevel(int) 0x406e20` (rescales HP). Shaman: see `src/game.h` / `re/fogbuild.md`, plus `GetGenes(PtList*) 0x441500` (activity 0x2f), `Upgrade(id) 0x4415c0` (activity 0x38), `WalkTo 0x441750`, `FinishWork 0x441790`, `BeginWork 0x4435a0`, `WorkTick 0x4436b0`, `WorkDuration 0x443d10`.

#### 1.6 Field layouts (H unless marked; full list in re/types.tsv)
- **AThing** (0x28): +0x04 u8 attack-reveal timer (set to 0x20 on the attacker, counts down, M) · +0x06 short selected · +0x08 short has mouse capture · +0x0a/+0x0c short top-left x/y · +0x10 `IPixelC` centre (vptr 0x5031c4, **+0x14 x, +0x16 y**) · **+0x18 int type** · **+0x1c short id** · **+0x20 APlayer\* owner** · **+0x24 short HP**.
- **ADevice** (0x40): +0x28 unpowered · +0x2a switched on · +0x2c complete · +0x2e dying-animation frame (>0 = being destroyed; BuildStep does nothing) · +0x30 u8 anim frame · +0x32 build progress · +0x34/+0x36 power provider id · +0x38 u8 timer (L) · +0x3a upgrading · +0x3c u8 build-anim counter (L) · +0x3d u8 (0xff, L).
- **ACreatureBody/ACreature** (0x154): +0x28 `AISquad*` · +0x2e/+0x30/+0x32 target ids (0xc01) · +0x34 IPixelC move/attack point (+0x38/+0x3a) · +0x40 u8 (0x50 at spawn, L) · **+0x44 int stance** (0 autonomous, 1 aggressive, 2 normal, 3 timid, 4 immobile; strings at 0x4825e0; Shamans/Yedda start timid) · **+0x48 u8 facing 0..7** · +0x52 short idle timer (rand % `g_IdleTime`) · **+0x54 int gizmo[4]** (100 = none) · +0x64 u8[4], +0x68 u8[4] gizmo animation phases · +0x6c/+0x6e/+0x70 short gizmo flags (armed / quill / missile) · +0x78 u8 · +0x79 u8 · **+0x7c int veteran level** · +0x80 short damage taken · +0x84 `AEffect*` shadow (flyers) · **+0x8d u8 control group** (0xff none) · +0x8e short weapon cooldown/energy (L) · +0x90 `IStack<tPathStep>` (data +0x94, count +0x98) · +0xa0 local path finder · **+0xac `IStack<IPixelC>` path (data +0xb0, count +0xb4)** · **+0xc0 int activity** (0x28 idle, 0x29 move, 0x2a attack, 0x2d plant, 0x2e build, 0x2f get genes, 0x38 upgrade, 0x39 repair) · +0xc8 / +0xdc `IList<IPixelC>` (waypoint/patrol lists, M) · +0xec short target id · +0x104 IPixelC guard point · +0x118 movement guide (0x4a3730) · +0x144 short · +0x148 short building to enter.
- **AShaman** (0x1b8): +0x168 IPixelC walk target (+0x16c/+0x16e) · +0x170 u8 failure count · +0x184 int work type · +0x188 int selected build/pod type (0x4a none) · +0x18c u8 · +0x18e short work target id · +0x190 IPixelC click (+0x194/+0x196) · +0x19c `IList<IPixelC>` plant/gene points (data +0x1a0, count +0x1a4) · +0x1ac int panel mode · +0x1b4 short build retry.
- **AMushroom** (0x34): +0x28 int growth stage (1..4, `tGrowthStage`) · +0x2c u8 stage ticks (stage length `g_PodStageTicks[pod]`) · +0x2e short refinery id · +0x30/+0x31 s8 jitter · +0x32 u8 variant. **AObstacle** (0x2c): +0x28 u8, +0x2a short rotation 0..359. **ADeath** (0x2c): +0x28 int.
- **AThingRegistry = World** (H): constructed at 0x471c80 with `new(0x2d140)` + ctor 0x446aa0, stored in `[0x579278]`; its only virtual (0x450fa0) serialises the whole thing table (M). The thing table is `+0x10` data / `+0x14` count; the selection and grid layout are in `re/hotkeys.md`.

---

### 2. Unit types and stats

#### 2.1 Where stats come from (H)
- **`DATA/GAMEPLAY.DAT`**: a raw dump of 69 fixed-size arrays with no header, read in order by **`GameData_SerializeTables 0x4651c0`** (also used to write them: direction flag `0x5507dc`) from `GameData_ReadFile 0x464e70`, inside **`GameData_LoadGameplay 0x465b60`** (thiscall on the `IGlobalData` static `0x55d890`, called at start-up `0x472c40`). Then: `.skt` gizmo skeletons per creature (`0x465bb0` → `0x465c00`, `<g_ThingFileName>.skt`), derived maxima (`0x4668d0`), stat ranges for the encyclopedia scenes (`0x466a30`), **`DATA/DEVICE.DTH`** (`0x465fb0`: 19 device lists + 2×4 lists of (short, short, int) records, likely the device build/animation sequences, L) and `globe.ani` (`0x466ca0`). Art: `0x466580` loads `<name>.MHK` for every type with `g_HasArt` and the gizmo archives `GIZMO?.MHK` (`g_GizmoFile`).
- The tables live at fixed addresses (0x54c588..0x5507c0, plus 7 scalars). Index domains by size: 0x94 = short[74] per type; 0x4a = u8[74]; 0x2c/0x58/0xb0 = per creature (44); 0x26/0x13/0x4c = per device (19, index type−0x2c); 0x19/0x32/0x64 = per gizmo (25); 0xaf/0x15e/0x2bc = per effect type (175); 3 = per pod (index type−0x3f). The game indexes the device/pod tables with a biased base, e.g. `0x54cfb8 + 2*type` = `g_DevicePower[type−0x2c]`.

#### 2.2 Per-type tables (selected; file offset in GAMEPLAY.DAT, all in re/names.tsv)
| global | file off | type | meaning | evidence | conf |
|---|---|---|---|---|---|
| 0x54e370 `g_Footprint` | 0x000 | short[74] | footprint px (32, 44 big creatures, 56 Refinery) | ctor centre, CreateThing | H |
| 0x54cf08 `g_ThingRadius` | 0x094 | short[74] | collision/selection radius px | 0x445ac0, pathing | H |
| 0x54eb28 `g_SightRange` | 0x128 | short[74] | sight px (192..384; Mine 64) | slot 0x54, Eyestalk +0x80 | H |
| 0x54cdb0 `g_MaxHP` | 0x1bc | short[74] | max HP (Shaman 100, Leviathan 384, Refinery 1800; obstacles 999) | slot 0x50 | H |
| 0x54c670 `g_BuildTime` | 0x250 | short[74] | build (device) / breed (creature) time in ticks | BuildStep, slot 0x70, Gene Lab | H |
| 0x54ebc0 `g_Armor` | 0x2e4 | u8[3][74] | armour per damage class 0..2 (0..8, 8 = immune) | TakeDamage | H |
| 0x54ddf8 `g_HasArt` | 0x98a | short[74] | load `<name>.MHK` | 0x466580 | M |
| 0x54d680 / 0x54d438 `g_HpBarX/Y` | 0xfe6/0x1030 | u8[74] | HP-bar offsets | AThing::Draw | H |
| 0x54cb50 | 0x107a | short[74] | 1 for creatures and weapon devices ("can attack", L) | 0x44f740 | L |
| 0x54c9c8 | 0x110e | int[74] | category 0..9 (Shaman, creature, Refinery, Relay, Psi, Gene Lab, weapon, pod, utility, none) | no code xref found | L |
| 0x54e7c0 `g_CreatureTier` | 0x27c5 | int[44] | tier 1..4; Gene Lab only breeds tier ≤ 2 | 0x42a480 | H |
| 0x5507b0 `g_PopCost` | 0x24d9 | u8[44] | population cost (1; 0 for Yedda) | `APlayer 0x437e30`, Gene Lab cap | H |
| 0x54feb8 `g_BodySize` | 0x2429 | int[44] | 0/1/2 body size | anim rate | M |
| 0x54c590 / 0x54ce48 | 0x297d/0x29d5 | short[44] | creature takes arm gizmos / head gizmo | SetGizmo action, Gene Lab UI | H |
| 0x54d838 | 0x2505 | int[4][44] | equivalent creature type per clan (99 = none) | `0x4385d0` | H |
| 0x54ecf0 | 0x23d1 | u8[44] | initial `+0x8e` | ctor | M |
| 0x54e478 `g_IdleTime` | 0x2379 | short[44] | idle timer modulus | ctor | L |
| 0x54d140 | 0x234d | u8[44] | movement stat (stat category 6; pathing code) | 0x466a30, 0x49c.. | L |
| 0x54d010 `g_DevicePower` | 0x2a2d | short[19] | power drawn (Gene Lab 260, Summoner 425) | `0x44ed20`, 0x40e130 | M |
| 0x54dec0 | 0x2a53 | short[19] | power field range % (Refinery 300, Relay 275, Relay+ 400) | 0x4668d0 | M |
| 0x54c588 `g_PodStageTicks` | 0x31fa | u8[3] | ticks per growth stage (80/64/48) | AMushroom tick | H |
| scalars | 0x320c.. | short | 0x51b2dc 64 Shaman work reach; 0x51b2e4 50 default work time; 0x51b2e0 50; 0x51b2b8 32 (Healer); 0x51dbcc 130 (pathing); 0x53d128 3000 (Summoner) | | M |

Per-gizmo tables (25 gizmos, `g_GizmoName` 0x54fcc0; 0 Hammer, 1 Blade, 2 Drill, 3 Circ Saw, 4 Whip, 5 Mortar, 6 Acid Gun, 7 Javelin, 8 Ion Gun, 9 Heat Gun, 10 Quill Gun, 11 Gas Gun, 12 Healer, 13 Eyestalk, 14 Eyestalk+, 15 Sonic Horn, 16 Plasma, 17 Kamikaze, 18 Laser, 19 Screamer, 20 Missile, 21 Ion Grenade, 22 Arrow, 23/24 unused): `g_GizmoTechLevel` 0x54cfa8 (int, required clan level) and `g_GizmoClan` 0x54e1a8 (int clan 0..3) (H, `APlayer 0x43a420`); `g_GizmoBreedTime` 0x54e958 (H); `g_GizmoDamageClass` 0x54cea0 (0..2, 3 = none, M); `g_GizmoProjectile` 0x54cd48 (effect type, M); `g_GizmoReach` 0x54ed40 (melee, M); `g_GizmoCooldown` 0x54dc60 (L); `g_GizmoSlotGroup` 0x54eaf0 (0 arm weapon, 1 head gizmo, H). Unknown: 0x54d2f0, 0x54eca0 (targeting flags, L), 0x54ed20, 0x54e920, 0x54c638.
Per-effect tables (175; effect types 0..36 and 100..137 are creature remains/genes): `g_EffectClan` 0x54d038 (clan 0..3 of a remains effect, 4 = not a gene) (H), lifetime 0x54d6d8 (M); the rest belongs to the effects subsystem.

Stat display: `0x466a30` computes min/avg/max for 14 categories into `IGlobalData+0x254/+0x28c/+0x2c4` (cats 0-6 creatures: sight>>6, breed time, HP, 3 armours, 0x54d140; 7-10 devices: sight, build time, HP, power; 11-13 pods). Category 11 reads `g_MaxHP[type−0x3f]` (indexes the wrong table, L bug).

---

### 3. Gene pool (genetic knowledge) and creature design

Overview (H): there is no free-form gene splicing. "Genes" are **clan technology**: a Shaman in **Get Genes** mode (SetMode 1; button `0x5449a0` shown when `APlayer+0x4844 > 0`) clicks a **remains effect** of another clan's creature (`Effects 0x424d50(clanBlocked[4], x, y)` finds an unclaimed effect whose `g_EffectClan` is 0..3 and whose clan is not blocked by `APlayer+0x1c..+0x22`), sends `Cmd+0x60 → AShaman::GetGenes 0x441500` (activity 0x2f), walks there, and on finishing (`FinishWork 0x441790`) claims the effect (`0x41f130`) and calls **`APlayer::AddGene(clan) 0x4375f0`**.
- Per-player pool (`APlayer`): `+0x486e short genes[4]` (count toward the next level), `+0x4850 int level[4]`, `+0x4864 short levelUpFlag[4]` (in single-player a clan that just levelled is skipped), thresholds `g_GenesPerLevel 0x53e0f0` = {3, 4, 6, 8, 9, 0} (set at 0x43a6a0). Reaching the threshold → `APlayer::GeneLevelUp 0x4376a0` (level+1, count 0, voice 0x115). Scripts most likely change and test it via `AAction_CGK` / `ACondition_PlayerCGK` ("CGK" = collected genetic knowledge, M).
- **Gizmos** (the body parts/weapons, art in `GIZMO?.MHK`, attachment points in `<creature>.SKT`): a creature carries `int gizmo[4]` at `+0x54` (100 = empty). Slots 0 and 2 are the paired arm weapons, slot 1 the head gizmo (`g_GizmoSlotGroup`), slot 3 only used by Dhuganaya (0xc) and Leviathan (0x16). `ACreatureBody::SetGizmo 0x405230(slot, gizmo)` applies side effects (Eyestalks relink sight, Screamer occupies two slots, raises `+0x8e`). A gizmo is available when `APlayer::HasGizmoTech 0x43a420(gizmo, plus)`: `level[g_GizmoClan[g]] >= g_GizmoTechLevel[g]`, levels ≥ 4 only with `plus` (Gene Lab+).
- **Design = (creature type, gizmo[4], count)**, composed in the Gene Lab's panel (`AGeneLabInterface` at AGeneLab+0x40; preview `ACreature` at interface+0x14, so the chosen type is interface+0x2c and gizmos interface+0x68..+0x74). `0x42c3e0` clears the slots a species cannot use; `0x42ce40` fills the available-gizmo flags (+0x16c short[21]). **Start** = `AGeneLabInterface::StartBreeding 0x42c3a0` → **`Cmd+0x68(labId, type, g0, g1, g2, g3, count)`** → `AGeneLab::SetDesign 0x429d50` (+0x1e8 type, +0x1ec int[4] gizmos, +0x200 count). `Cmd+0x6c` → `AGeneLab::CancelBreeding 0x429dd0`; `Cmd+0x70` → `SetBreedCount 0x429df0` (count from table 0x504220).
- Designs are **not** kept as a reusable library: the only persistent copy is per Gene Lab, written by `AGeneLab::Serialize 0x429e60` (rally list, +0x1fc..+0x206, type, gizmos, patrol point). Scenario-given gizmos use `AAction_SetGizmo 0x418760`; veteran status `AAction_VeteranStatus 0x417c00` (the only caller of `SetVeteranLevel`).
- `SGeneLabScene` (0x5075f4, `GENESCN.MHK`) and `SDeviceLab` (0x50640c, `DEVLAB.MHK`) are the main-menu encyclopedia screens, not the in-mission lab (M).
- Breeding (`AGeneLab::Tick 0x428f30`): while producing, progress +0x1fe grows 1/tick (Gene Lab) or 2/tick (Gene Lab+); population cap check (`APlayer+0x487c` max vs `+0x487e` used, `g_PopCost`) in the panel handlers 0x429990/0x429a30; at progress ≥ breed time (`0x429e10`) → `SpawnCreature 0x429350`.

---

### 4. Rally points: they exist, for Gene Lab and Gene Lab+ only (H)

- **Storage** (AGeneLab): `+0x208 IList<IPixelC>` rally path (data +0x20c, count +0x210, capacity +0x214) and `+0x218 IPixelC` loop point (+0x21c/+0x21e; invalid = 0xfd66 / -1 = no patrol). Default set in the ctor (lab x+0x46, y+0xf0, or y−200 near the map bottom) and again on completion by `AGeneLab::ResetRally 0x4291b0` (first free spot ±0xa0/±0x5a around the exit).
- **Set**: select one Gene Lab, then a move order: `World::OrderAt 0x44c6d0` → `World::MoveSelection 0x44ccd0` kind 4 → **`Cmd+0x4c(labId, PtList* path, IPixelC* loopPt)`** (loop = (-1,-1)); Tab waypoints use `0x44d290` with a loop point (patrol). Single player `0x4604c0`, multiplayer `0x45ebf0` (lockstep), executor `0x461c00` (only types 0x2d/0x35) → **`AGeneLab::SetRally 0x429ce0`** (copies the path, sets the loop point if it is on the map).
- **Draw**: `AGeneLab` panel draw 0x429730 calls `World::DrawWaypointPath 0x448f10(&path, camX, camY, loopIndex, …)` while the lab is selected.
- **Use**: **`AGeneLab::SpawnCreature 0x429350`**: `PlaceCreature 0x4472d0(owner, +0x1e8, exit point from 0x528264[lab] (flyers −0x16), gizmos)`, then if the loop point is valid and on the path → `ACreature::Patrol 0x49f1e0(&path, &loopPt)`, else `creature->FollowPath(&path, 0x20)` (vtbl+0xb4); then owner vtbl+0x3c(lab, creature) (AI notification), `AGeneLabInterface 0x42cdf0`, the "creature ready" message `0x4295d0` for the local player, and after `count` births owner vtbl+0x2c(lab).
- **Other producers**: none. Warp (`AWarp`, 0x39) has a teleport destination `+0x46/+0x48` set by `Cmd+0x54 → AWarp::SetTarget 0x457ce0` (reset to itself if too close); creatures reach it by entering the Warp (`Cmd+0x48` / slot 0xc0). Healer (0x34) is entered the same way. Scripted spawns (`AAction_AddCreature 0x415650`, `AAction_Warp`) have no rally. To add rally behaviour elsewhere, hook after `PlaceCreature` in the spawner, or for Gene Labs replace/extend the FollowPath call in 0x429350.

### Connections
Commands (`Cmd [0x579260]` slots 0x48..0x98, see the commands section), World grid/selection (`re/hotkeys.md`, `re/mouse.md`), players (`APlayer` 0x504664: gene pool, population, power), effects (remains/genes, shadows: `AEffectRegistry [0x579280]`), AI (`AISquad` at creature+0x28, `AIComputerPlayer` uses `0x4385d0`), save games (slot 0x48 tagged archive), placement/power (`re/fogbuild.md`).

### Open questions / uncertain
- Slots 0x3c, 0x60, 0x78, ACreatureBody 0x7c and 0x8c..0xa8 are only partly understood (L).
- The meaning of `+0x8e` (cooldown vs energy), several gizmo tables (0x54d2f0, 0x54eca0, 0x54ed20, 0x54e920, 0x54c638) and per-type tables 0x54cb50, 0x54c9c8, 0x54d140, 0x54e478 (L).
- DEVICE.DTH record meaning (two shorts + int per entry, 19 device lists) is unconfirmed (L).
- `APlayer+0x1c..+0x22` (per-clan "cannot collect" flags) and `+0x4844` (Get Genes allowed) are inferred from their use only (M).
- The link between "CGK" (`AAction_CGK`, `ACondition_PlayerCGK`, `SCGK`, `WCGK`) and the per-clan levels at `APlayer+0x4850` is inferred from the names, not traced (M).
- Remains/gene effects (effect types 0..36 and 100..137) are created on creature death by the effects code; which effect type a dead creature leaves was not traced (M).
