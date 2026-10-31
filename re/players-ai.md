# Players, alliances, AI and the command model

> Research report (static analysis, nothing run). Confidence: **H** read from the code, **M** mechanism clear but purpose inferred, **L** a guess. Addresses are authoritative; where a name here differs from `re/names.tsv`, the TSV is the current one (and the export `re/ghidra-export-v2/` uses it). Index: `re/subsystems.md`.


Sources: `re/ghidra-export/` (decompiler, xrefs, functions), `re/text.asm` (disassembly), RTTI vtables.
Confidence: **H** = read from the instructions (disassembly checked for the key claims), **M** = mechanism
clear, purpose inferred, **L** = guess. All addresses are in WARBREED.EXE (image base 0x400000).

Globals used throughout:

| global | type | meaning | conf |
|---|---|---|---|
| `[0x579260]` | `CommandModel*` | `g_Cmd`, the active command model; set by `SelectCommandModel 0x470780(mode)`: 0x32 → `&g_SinglePlayerCmd 0x5511c8`, 0x33 → `&g_MultiPlayerCmd 0x578d40` | H |
| `[0x579278]` | `World*` | the world (size 0x2d140, created in `0x471c40`) | H |
| `[0x57927c]` | `APlayerRegistry*` | `g_Players` (size 0x30, ctor `0x43a8c0`) | H |
| `[0x579280]` | `Effects*` | effects list (size 500) | H |
| `[0x579284]` | | mission event/trigger list (size 0x24, ctor `0x41d740`) | M |
| `[0x579288]` | `APlayer*` | `g_LocalPlayer` (0 when the local player is gone/spectating). `0x46f8e0()` returns it. | H |
| `0x53e0e8` | `u8[8]` | `g_PlayerBit[i] = 1<<i` (filled by `0x43a6a0`; the next 6 bytes are another small table) | H |
| `[0x53e0fc]` | `u8` | `g_AILevel` from the SP "AI Options" slider (see §4.6) | H |
| `[0x53e100]` | `u16` | `g_EliminationArmed`: 0 at player ctor, set to 1 by the first `APlayer::OnThingRemoved` | H |
| `0x567ef8` | `SGame` | the static mission scene | H |

---

## 1. Players (`APlayer`, `APlayerRegistry`)

### 1.1 Overview
There are at most 8 players (ids 0..7; id 9 means "none"). `APlayerRegistry` holds `APlayer* players[8]`.
A player is either a plain `APlayer` (human or passive, 0x4a4c bytes) or an `AIComputerPlayer`
(0x4e60 bytes, derives from `APlayer`). Players are not created by the setup screens. They come out of the
map file (`APlayerRegistry::Serialize 0x43ae20`) and are then converted (human ↔ AI) or deleted to
match the setup slots (§3). **H**

### 1.2 APlayerRegistry (vtable 0x504754, size 0x30)
| function | address | notes | conf |
|---|---|---|---|
| ctor | `0x43a8c0` | players[]=0, +0x28=9, +0x2a=+0x2c=1 | H |
| dtor body | `0x43cfe0` → `Clear 0x43ac60(deleteGraveyard)` | | H |
| vt+0 `Serialize(stream)` | `0x43ae20` | keys `PRHO` (+0x2a), `PRFO` (+0x2c), per player `F` clan, `G` colour, `H` isAI, `I` id, then `player->vt+0x6c`; `;` = +0x24 | H |
| vt+4 | `0x43b460` | second serializer (save game variant) | M |
| `AddPlayer(clan, colour, id, isAI)` | `0x43b850` | `new APlayer`/`new AIComputerPlayer`, then `0x43abd0` | H |
| `SetSlot(p, noCount)` | `0x43abd0` | players[p->id]=p; ++count | H |
| `DeletePlayer(id, notify, removeThings)` | `0x43abf0` | defeat, optionally remove its things (`World 0x44f100`), delete | H |
| `GetPlayerRaw(id)` | `0x43ad10` | no alive check | H |
| `GetPlayer(id)` | `0x43ada0` | returns only if `+0x48f6 == 1` (alive) | H |
| `FindByColour(c)` | `0x43add0` | alive players only; used by all mission conditions/actions | H |
| `FindByMachine(m)` | `0x43ad30` | searches each player's controller list `+0x8` | H |
| `ToggleComputerPlayer(id)` | `0x43aa70` | console only; dynamic_cast, swaps APlayer↔AIComputerPlayer | H |
| `TickPlayers()` | `0x43a900` | `vt+0` of each alive player; if exactly one alive and no winner yet → that player wins | H |
| `NotifyEnemiesThingSeen(thing)` | `0x43a970` | for each alive player NOT allied with the owner who can see it: `vt+0x10(thingId)` | H |
| `BroadcastThingEvent(thing)` | `0x43b940` | for each other alive player who can see it: `vt+0x20(thing)` | M |
| `RescanAll()` | `0x43ba20` | `vt+8` on every alive player (console) | M |
| `PlayerDefeated(id, recheck)` | `0x43b780` | see §5 | H |
| `CheckGameEnd()` | `0x43baf0` | `APlayer::CheckGameEnd 0x439810` for every alive player | H |
| `DeclareWinner(id)` | `0x43bb20` | first call only (+0x28==9): `SGame::EndGame 0x470820(localWon)`; sets +0x28=id, +0x26=1 | H |
| `AllMutuallyAllied()` | `0x43bb90` | 1 if every pair of alive players has each other's bit in `+0x18` | H |
| `SendMessage(from, mask, text)` | `0x43bc40` | `APlayer::PostMessage 0x439600` to each player in mask | H |
| `SetupFromSession(cmd)` | `0x43bd40` | skirmish/MP slot → player conversion (§3) | H |
| `SetupMP()` | `0x43bd00` | colours, setup from `g_MultiPlayerCmd`, `ApplyTeams`, fill colours | H |
| `SetupSP()` | `0x43bd30` | setup from `g_SinglePlayerCmd` | H |
| `ApplySlotToPlayer(cmd, slot, idx)` | `0x43c130` | SP: keep/convert/delete player `idx` from slot (§3) | H |
| `ConfigureAI(ai)` | `0x43c2f0` | per-clan default aggression (§4.4) | H |
| `ChooseStartForSlot(...)` | `0x43c3b0` | MP: picks a start spot, near the team centroid when the slot has a team | M |
| `TeamStartCentroid(pt*, team)` | `0x43c610` | averages start points of players in team | H |
| `MakeHuman(idx)` / `MakeComputer(idx)` | `0x43c710` / `0x43c7d0` | rebuild the player object in the other class | H |
| `AddMachineToSlot(slot, idx)` | `0x43c890` | MP: same colour → co-op (machine joins existing player), else new player | M |
| `ApplyTeams()` | `0x43c8f0` | **players with the same non-zero `+0x4a30` team become mutually allied** | H |
| `CountComputerPlayers()` | `0x43cc40` | dynamic_cast count | H |
| `PostSetupAI()` | `0x43cc90` | `AIComputerPlayer::PostSetup 0x4b9c60` for each AI | M |
| `SetFogMode(reveal, fogOn)` | `0x43ce00` | sets every player's `+0x484c`; reveal==0 → `0x43a240` explores the whole map | M |
| `AnyPlayerHasStartUnits()` / `StripToShamans()` | `0x43cf30` / `0x43cf70` | "Start With: Shaman Only" | M |
| `PickFreeColour()` | `0x43ba50` | | M |

Registry fields: +4 `APlayer*[8]`, +0x24 u8 alive count, +0x26 u16 game over, +0x28 u16 winner id (9 = none),
+0x2a u16 (PRHO, reveal/explored flag), +0x2c u16 (PRFO, fog on). **H/M**

### 1.3 APlayer (vtable 0x504664, size 0x4a4c)
Constructor `0x435ba0(id, clan, colour)` (thiscall; `ret 0xc`). Main fields (see `re/types.tsv` for all):

| offset | type | meaning | conf |
|---|---|---|---|
| +0x04 | u16 | id 0..7 | H |
| +0x08 | IList | controlling machine ids (data +0x0c, count +0x10); one APlayer can be shared by several MP machines ("co-op team") | M |
| **+0x18** | **u8** | **ally mask**: bit i set = this player treats player i as an ally. Init = own bit. Saved (key `{`) | **H** |
| +0x19 | u8 | chat mask: whom this player's messages go to (SAllies right column). Init 0xff | H |
| +0x1a | u8 | "enemy players seen" mask (`OnEnemySeen` sets the owner's bit). Used by `ACondition_PlayerSeen 0x4147f0`. Saved (key `J`) | H |
| +0x1c | u16[5] | clan genes available, index = clan | H |
| +0x26 | u8[96*96] | fog visibility counts (see `re/fogbuild.md`) | H |
| +0x2426 | u8[96*96] | unexplored shroud | H |
| +0x47ce | u16[0x3f] | owned thing count by type (devices 0x2c..0x3e live at +0x4826..+0x484a); tech prerequisites | M |
| +0x484c | u16 | fog of war enabled | H |
| +0x484e | u8 | colour (0xf7 default; mission scripts address players by colour) | H |
| +0x4850 | i32[5] | CGK (clan gene knowledge) level per clan; set from Native/Foreign CGK | H |
| +0x4878 | i32 | clan (0 Tanu .. 3, 4 = generic) | M |
| +0x487c / +0x487e | u16 | supply / demand used by `CanBuild` for creatures | M |
| +0x48e8 | i32[3] | thing counts by category (creature, device, pod) (`0x439880`) | M |
| +0x48f6 | u16 | alive/active (0 after defeat) | H |
| +0x48f8 | char[8][32] | controller (machine) names | H |
| +0x4a08 | IList | timed message queue (`PostMessage 0x439600`, 300 ticks) | H |
| +0x4a18 | Pt | start position (+0x4a1c x, +0x4a1e y); camera starts there | H |
| +0x4a20 | IList<Pt> | per-machine start points | M |
| **+0x4a30** | **u8** | **team** (0 = none); from MP slot +0x1e; `ApplyTeams` allies equal teams | **H** |
| +0x4a34 | IList | own devices | M |

Non-virtual methods: `IsAlly(id) 0x4adaa0`, `SetAllyBit(id,on) 0x439580`, `SetAlly(id,on) 0x439560`
(= SetAllyBit + `Registry::CheckGameEnd`), `SetChatBit 0x4395c0`, `PostMessage 0x439600`,
`CheckGameEnd 0x439810`, `DeclareVictory 0x439840`, `Defeat 0x439860`, `Deactivate 0x437930`,
`SetClan 0x4379a0`, `SetCGK 0x438700`, `SetClanGenes 0x435ed0`, `CanBuild 0x437b50`, `HasDevice 0x43a7a0`,
`SetSeen 0x4360c0`, `IsTileVisible 0x453cc0`, `RemoveMachine 0x43a470`, names `0x438500/0x438540/0x4384b0/0x438440`.

Virtual slots (APlayer impl / AIComputerPlayer impl):

| slot | APlayer | AI | meaning | conf |
|---|---|---|---|---|
| +0x00 | 0x435ef0 | 0x4b5fe0 | Tick (APlayer: vision, messages, elimination check) / AI Think | H |
| +0x04 | 0x437000 | 0x4b6750 | DrawFog(rect, zoomedOut) (AI adds debug overlays) | M |
| +0x08 | ret | 0x4b5ee0 | Rescan owned things | M |
| +0x0c | ret 0 | 0x4ba2e0 | AI query on a location (2 args) | L |
| +0x10 | 0x437810 | same | OnEnemySeen(thingId): +0x1a bit, device ghost marker | H |
| +0x18 | ret | 0x4b69c0 | OnThingAdded(id) | M |
| +0x1c | 0x437710 | 0x4b6a70 | OnThingRemoved(thing, transfer): elimination check, release sight | H |
| +0x20 | ret | ret | OnThingEvent(thing) (from `BroadcastThingEvent`) | L |
| +0x30 | 0x439880 | 0x4b8290 | OnThingCreated(thing): category counts | M |
| +0x38 | ret | 0x4b6b80 | AI reaction (uses Howitzer fire) | L |
| +0x3c | ret | 0x4b8bf0 | OnCreatureBorn(lab, creature): put into a squad | M |
| +0x40 | ret | 0x4b8d30 | AddCreature(creature) | M |
| +0x60 | ret 0xc | 0x4b8600 | **SetAggression(base, squad, explore)** (floats) | H |
| +0x64 | ret 8 | 0x4b9fc0 | SetImportance(…) → AIRetina `0x4a9710` | M |
| +0x68 | ret | 0x4b9fe0 | SetSquadChanging(short) (+0x4cc8) | H |
| +0x6c | 0x438720 | same | Serialize | H |
| +0x70 | ret | 0x4b7280 | AI extra serialize | M |
| +0x7c | 0x421180 (1) | 0x435ad0 (0) | **IsHuman()** | H |

---

## 2. Alliances

### 2.1 Storage (H)
Alliances are a **per-player bit mask**, `APlayer+0x18`. It is directional: every test reads the
*viewer's* mask, `viewer->allyMask & g_PlayerBit[other->id]`. The registry treats a pair as allied only when
both bits are set (`AllMutuallyAllied 0x43bb90`). The mask is saved with the player (key `{`), so maps and
saved games can carry alliances. There is no matrix elsewhere; `+0x4a30` (team) is only used at setup
time to fill the masks.

Ways the mask changes:
- `APlayer::SetAllyBit 0x439580` (raw) and `SetAlly 0x439560` (raw + game-end check);
- command `Cmd+0x94 SetAlly(playerId, otherId, on)` → executor `0x462240` → `SetAlly` (lockstep-safe; MP opcode 0x3b);
- MP setup `ApplyTeams 0x43c8f0` (same team → mutual);
- SP skirmish setup `0x43bd40` (all computer players allied with each other, see §3.3);
- map/save load.

No mission action and no console command changes alliances.

### 2.2 SAllies scene (vtable 0x5060e4; static object at 0x567e40; scene id 0xc) (H)
- **Reachable only in multiplayer.** The in-game Esc menu `SMenu` (`0x489370`) creates the "Allies and Messages"
  button (+0x50) only when `Cmd+0x18() == 0x33`. Its click handler (`0x489830`) opens scene 0xc
  (`0x4709a0(0x22, 0xc, 5)`), or shows an error if there is no local player.
- Open `0x474b10` loads `ALLIES.MHK` and builds one row per player via `0x475200`. **Only players with
  `vt+0x7c IsHuman() != 0`** (and not the local player) get a row: AI players never appear.
- Columns: `+0x38[i]` "ally" checkbox, `+0x58[i]` "send messages" checkbox, `+0x78[i]` name, `+0x18..` colour swatch.
- Click (`0x474f70`, slot +0x1c): the ally box sends `Cmd+0x94(localId, i, !(local+0x18 & bit))` (networked);
  the message box toggles `local+0x19` directly (`0x4395c0`, local only).
- Buttons (`0x474d80`, slot +0x6c): id 1 close; 7/8/9 → `0x474e30(alliesFlag, othersFlag)` = set the chat
  mask for allies/non-allies in bulk.
- Refresh tick `0x474eb0` (slot +0x10): rebuilds if a listed player disappeared, mirrors both masks into the boxes.
- Close `0x474d60`, delete rows `0x475090`.

### 2.3 Every "ally/enemy" test (all read `+0x18` through `g_PlayerBit 0x53e0e8`)
Found by xref on 0x53e0e8 (complete list of its readers, H for the list).

| site (function) | what it decides | conf |
|---|---|---|
| `0x40e528` in `ADevice::TakeDamage 0x40e360` (+0x44) | non-allied attacker → `World::CallForHelp`, damage cooldown | H |
| `0x41481e` in `ACondition_PlayerSeen::Evaluate 0x4147f0` | uses `+0x1a` seen mask (not +0x18) | H |
| `0x42696e` in `0x426920` (fog-ghost click, from World_Click) | remembered building of a non-ally → attack order | M |
| `0x432b9f` in `AMine::AcquireTarget 0x432b00` (+0x28) | mine triggers only for non-allied creatures | H |
| `0x437844` in `APlayer::OnEnemySeen 0x437810` | sets `+0x1a` (caller already filtered enemies) | H |
| `0x43a9ba` in `Registry::NotifyEnemiesThingSeen 0x43a970` | which players are told about a thing | H |
| `0x43bbea/0x43bbfb` in `Registry::AllMutuallyAllied 0x43bb90` | **victory check** | H |
| `0x44762d` in `World 0x447460` (called by Plant/Build) | allied idle creatures in a build site are asked to move aside | M |
| `0x4496bf` in `World::DrawInfoPanel 0x449410` | "ally"/"enemy" label for a selected foreign thing | M |
| `0x44a741` in `World 0x44a690` (from `SGame::UpdateCursor`) | cursor over an allied thing | M |
| `0x44ab97`, `0x44ae7f` in `World_Click 0x44a860` | click on a non-allied thing = target, on allied = no attack | M |
| `0x44c635` in `World::RevealObjectsInCell 0x44c570` (from `SetSeen`) | newly seen non-allied things → `OnEnemySeen` | H |
| `0x44e3fd` in `World::FindPlacementBlockers 0x44e270` | allied idle creatures do not block placement | M |
| `0x44f8ec` in `World::OfferTarget 0x44f8d0` | **auto-acquire**: viewer `vt+0x28(target)` only for non-allies (driven by `World::ScanSight 0x44f740`) | H |
| `0x450526` in `World 0x4502d0` (nearest-target search, via `0x4501e0`/`0x450870`) | candidate filter for creatures and AI squads | M |
| `0x450af0` in `World::CallForHelp 0x450970` | **allied** creatures near a victim join in | H |
| `0x450e1d`, `0x450f6e` in `World::CursorForThing 0x450d40` | 0x5b friendly vs 0x2a attack cursor | H |
| `0x45406e` in `World::FindAITarget 0x453d20` (from `AISquad 0x4bb250/0x4bb840`) | **AI target selection** | H |
| `0x4567e6` in `ATurret::SetTarget 0x456790` | turret refuses allied targets | H |
| `0x474e3c..0x47505a` in SAllies | UI | H |
| `0x47e081` in `SGame 0x47e040` | hovered thing is an enemy of the local player | H |
| `0x4a0360` in `ACreature::AcquireTarget 0x4a0300` (+0x28) | creatures ignore allied candidates | H |
| `0x4a056a` in `ACreature::OnAttackedBy 0x4a0520` | ignore friendly fire, otherwise call for help and retaliate | H |
| `0x4a09ce`, `0x4a0c07` in `ACreature::TakeDamage 0x4a08e0` (+0x44) | retaliation target choice | M |
| `0x4adaac` in `APlayer::IsAlly 0x4adaa0` (called by `AIRetina 0x4aa500`) | AI threat map ALLY/ENEMY profiles | H |

Not alliance-aware (plain owner comparisons or none):
- **Shots hit allies.** A shot's impact damages whatever is there with no alliance test: World `0x44f930` (x, y, attacker, layer, class, amount, extra) the thing at the point (`0x44b9b0`), `0x44f970` (x, y, radius, attacker, amount, layer) everything within the radius (`AMovingEffect`, `ANuclearEffect` and others; 11 call sites). A turret's shot at an enemy that has moved, or a blast beside an ally, hurts the ally. The victim doesn't hit back: `OnAttackedBy 0x4a0520` returns 0 at 0x4a0575 for an allied attacker, and a turret retaliates through `SetTarget`, which refuses allies. WarBreeds HD logs each allied hit (`teams: friendly fire`). **H**
- **Vision is never shared.** `World 0x448730` adds sight only to the thing's owner (`0x436280`/`0x4363a0`). **H**
- Minimap and unit colours come from the owner's colour `+0x484e`; no ally test was found in drawing. **M**
- The "under attack" alert in `0x40e360` tests `owner == local && attacker owner != local`. **H**
- Selection and orders use `owner == g_LocalPlayer` (`World 0x44a1e0`). **H**

---

## 3. Skirmish and multiplayer setup

### 3.1 Scenes (H)
- `SMultiSetup` (vtable 0x508214) is **both** the skirmish setup ("Skirmish Setup") and the MP lobby;
  `+0x292e` = 1 for multiplayer, 0 for skirmish. `SSinglePlayer` is the campaign screen.
- Option sub-dialog `SMultiplayerOption` (0x507e64) returns values through `SMultiSetup::SetParam 0x48e5c0`
  (slot +0x58): id 0/0xe map, 1 MP flag, 10 → `+0x214`, 0xb → `+0x220`, 0xc → `+0x218`, 0xd → `+0x21c`,
  0xf → `+0x224`, 200 → CGK pair, 100 → MP-only.
- Buttons in `SMultiSetup::OnCommand 0x48ce30`: Start (+0x28fc), Cancel (+0x2900), Options (+0x2904),
  Genetic Tech (+0x2908). The skirmish Start shuffles start slots (`0x48f530`, random placement) or keeps
  them (`0x48f880`), then `0x48d440` starts the game. MP Start refuses when `CMultiPlayer::AllSameTeam
  0x45dc50` ("All players are allied.").

### 3.2 Where the settings live (H for offsets, M for labels)
The session settings are **inside the command model**, at `cmd+4` (`0x41e380` returns `cmd+4`):

| offset from cmd+4 | meaning |
|---|---|
| +0 + i*0x28 | `tPlayerSlot[8]`: +0 u8 used (1), +1 name, +0x18 machine id (MP) / slot ordinal (SP), +0x1c clan, +0x1d colour (same colour in MP = co-op on one APlayer), **+0x1e team**, +0x24 u16 isHuman |
| +0x170 | map file name |
| +0x214 | Fog of War (0 map default; 1..3 → `Registry::SetFogMode` in `0x47aff0`) |
| +0x218 | Extras (2 = scatter 18 random power-ups/gizmos, `0x47aff0`) |
| +0x21c | Placement (0 random, 1 map default; MP also "near allies/enemies") |
| +0x220 | AI Players count (0..7) |
| +0x224 | Start With (1 = Shaman Only → `0x43cf70`) |
| +0x228 / +0x22c | Native / Foreign CGK |

`CommandModel` slots +0xc0 `IsSlotUsed(i)`, +0xc4 `GetSlot(i)`, +0xc8 `CountUsedSlots`, +0xcc `GetLocalSlotInfo`,
+0xd0 `GetLocalSlotIndex`, +0xd4 `ResetSession` expose them.

### 3.3 How slots become players (H)
`SGame::LoadMission 0x47ab30`, step 8:
- MP: `0x47afd0` (apply session options) → `Registry::SetupMP 0x43bd00` → local player = `FindByMachine(local machine)`.
- SP campaign (`SGame+0x82 == 0`): local player = first alive player; no setup.
- **SP skirmish** (`SGame+0x82 != 0`): `0x47afb0` → `0x47aff0(session)` (fog, extras, start-with) and
  `Registry::SetupSP 0x43bd30` → `SetupFromSession 0x43bd40(g_SinglePlayerCmd)`.

`SetupFromSession 0x43bd40` (both modes) walks the map's players. In SP it calls
`ApplySlotToPlayer 0x43c130` for each: a slot with ordinal ≤ AI count is kept and converted to human
(`0x43c710`, becomes `g_LocalPlayer`) or AI (`0x43c7d0`), gets the slot clan (`0x4379a0`), CGK levels and
colour, and AIs get `ConfigureAI 0x43c2f0`. Unused map players are deleted (`0x43abf0`). At the end, **in SP only
(`Cmd+0x18 != 0x33`), every computer player is made mutually allied with every other computer player**
(loop at `0x43c06f..0x43c0b3` calling `SetAllyBit`). The human gets no allies. Then `PostSetupAI 0x43cc90`.
In MP the slot team (+0x1e) is copied into `APlayer+0x4a30` (`0x43ca20`) and `ApplyTeams 0x43c8f0` runs from
`SetupMP`. SP never reads the slot team.

---

## 4. AI (`AIComputerPlayer`, `AISquad`, `AIRetina`)

### 4.1 Layout (vtable 0x509734, size 0x4e60, ctor `0x4b58b0(id, clan, colour)`)
Inherits `APlayer`; `+0x4a50` embedded `AIRetina` (ctor `0x4a9140(owner)`; region grid width +0x4ae4,
regions +0x4ae8), `+0x4ccc` embedded base/builder manager (non-RTTI, ctor `0x4addf0`, tick `0x4ae470`),
`+0x4d84/+0x4d88/+0x4d8c` squad/base/explore aggression floats, `+0x4d94 IList<AISquad*>`,
`+0x4cc8` squad-changing flag. Copy-constructors `0x4b5af0` (from APlayer) and `0x4398e0`/`0x439ad0` (APlayer from player). **H/M**

### 4.2 Think loop `0x4b5fe0` (vt+0, called by `Registry::TickPlayers` every simulation tick) (M)
1. Decrement cooldown `+0x4cb0`.
2. Every `0x4b5f60()` ticks: `AISquad::Update 0x4bb250` for every squad.
3. Interval scaled by the number of AI players (`0x43cc40`): alternately `AIRetina::ScanThings 0x4aa500`
   (counts ALLY/ENEMY strength per region with `IsAlly`) and `AIRetina 0x4aa8d0`.
4. `Base manager tick 0x4ae470` (production, build orders, uses `0x4afbe0`, `0x4b0830`).
5. Strategy interval (`0x4b5f80`, +0x4d48): if squad changing is on, `0x4b8a00` (re-form squads); `0x4ba150`;
   per squad `0x4bb490`/`0x4bdd10`; then **`AssignSquadGoals 0x4b97b0(role)`** for roles 2 (explore),
   1 (base defence), 0 (attack), 7 (other). Each idle squad of that role scores every AIRetina region
   (`0x4b9030` explore, `0x4b92b0` base, `0x4b8db0` attack, `0x4b9530` other), and the best region becomes
   the squad goal (squad +0x18/+0x1a, state +0x2c = 2) if the average score reaches a threshold (`[0x509720]`).
6. Squads with the same goal are merged and `0x4ba2f0` picks the attack target.

### 4.3 Orders
The AI calls the **command executors directly** (not through `g_Cmd`): `0x461df0` Build, `0x461da0` Plant,
`0x461e50` Get Genes, `0x462160` Upgrade, `0x4621b0` Repair, `0x461b00` Move (all from `0x4afbe0`),
`0x461f60` Gene Lab build (`0x4b0830`), `0x461c50` power on/off (`0x4b0780`), `0x4620e0` kill (`0x4ae510`,
`0x4af7f0`, `0x4b5420`), `0x461ea0` attack (`AISquad 0x4baf10/0x4bbb70`), `0x461d00` stop (`0x4bbd20`),
`0x462200` Howitzer fire, `0x461cc0` warp. The AI runs identically on every machine, so this stays in sync. **H** (call sites)

### 4.4 Aggression and difficulty
- `SetAggression(base, squad, explore)` = vt+0x60 `0x4b8600` → +0x4d88, +0x4d84, +0x4d8c. **H**
- Defaults per clan (`ConfigureAI 0x43c2f0`): clan 0 (0.5, 1.0, 1.0), 1 (0.5, 0.9, 1.0), 2 (0.25, 0.8, 0.75),
  3 (0.75, 1.0, 1.0). Then `0x4b9ff0` sets +0x4d90. **H** (values), **M** (clan names)
- `AAction_SetAggression` (vtable 0x503c3c): fields +0x10 base, +0x14 squad, +0x18 explore, +0x1c player
  colour; Execute `0x419fa0` → `FindByColour` → vt+0x60. **H**
- `AAction_SetSquadChanging` → vt+0x68; `AAction_SetImportance` → vt+0x64 (AIRetina). **H/M**
- **There is no AI difficulty in skirmish.** `SAIOption` (vtable 0x50604c, static 0x5515a8, `AIOPT.MHK`) is a
  1..4 slider in the SP Esc menu writing `g_AILevel [0x53e0fc]` (`0x43a3a0`/`0x43a3b0`, saved in the
  config `0x470f70`). The only gameplay readers are `ADevice::TakeDamage 0x40e360` and `AThing+0x44 0x445ba0`:
  in single player, **damage to the local player's things is reduced** by adding the level to the armour
  index (capped at 8). It is a handicap for the human, not an AI setting. **H**

### 4.5 AISquad (vtable 0x5097d4, size 0xb8, ctor `0x4ba800(owner)`) (M)
Derived from `IList<ACreature*>` (+4 data, +8 count). Created in `OnCreatureBorn 0x4b8bf0` (one squad per
role, found by `0x4b8b50`). Key fields: +0x18/+0x1a goal region, +0x2c state, +0x30 goal score (double),
+0x74 role (0 attack, 1 base, 2 explore, 3 ignored, 7 default), +0x78 current target id (0xc01 none),
+0x7c..+0x88 strength sums, +0x8c rally point, +0xa0 owner. `Update 0x4bb250`: without a target, after 200
idle ticks it searches 256 px around its region with `World::FindAITarget 0x453d20` (ally-filtered) and
attacks (`0x4bbb70`); with a target it re-checks every 100 ticks and falls back to `World 0x4501e0`.

---

## 5. Victory and defeat (H unless noted)
- **Elimination**: `APlayer::Tick 0x435ef0` (once armed by the first `OnThingRemoved`, every 40 ticks,
  staggered by id) and `APlayer::OnThingRemoved 0x437710` call `World::PlayerHasUnitsOrDevices 0x4536a0`;
  a player with no creature or device left in bounds → `Registry::PlayerDefeated(id, 1)`.
- `PlayerDefeated 0x43b780`: `Deactivate 0x437930` (+0x48f6 = 0). If it is the local player: in SP the
  first other player is declared winner and `SGame::EndGame 0x470820(0)` shows defeat; `g_LocalPlayer = 0`
  and the map is revealed (`0x42f210`). Count--, then `CheckGameEnd`.
- `CheckGameEnd 0x43baf0` → per alive player `0x439810`: **if `AllMutuallyAllied`** → `DeclareVictory`
  (`Registry::DeclareWinner 0x43bb20`). The first call ends the game with `localWon` = local player still
  alive and everyone allied. So **an allied team that outlives every enemy wins**.
- `Registry::TickPlayers 0x43a900`: exactly one player alive and no winner → that player wins.
- Missions: `ACondition_PlayerDeath` (vtable 0x5037e4; +0xc colour; Evaluate `0x413bf0` = `FindByColour == 0`),
  `AAction_PlayerEnd` (vtable 0x503b7c; +0x10 colour `PL`, +0x12 victory flag `VI`; Execute `0x418fd0` →
  `DeclareVictory 0x439840` or `Defeat 0x439860`). Other conditions read player state: `ACondition_PlayerCGK`
  (+0x4850), `ACondition_PlayerDevices`, `ACondition_PlayerSeen` (+0x1a).
- `Cmd+0xa8 PlayerQuit` / `+0xac RemovePlayer` (`0x462e70`/`0x462ed0`): MP resign/leave; removes the machine
  and, if no machine is left, the player's things and the player.

---

## 6. Command model (`CommandModel` 0x5053e4, `CSinglePlayer` 0x504ff4, `CMultiPlayer` 0x504f04)

### 6.1 Overview (H)
Every player order goes through `g_Cmd [0x579260]`. `CSinglePlayer` (static `0x5511c8`) executes at once:
each slot calls a shared **executor** (`0x461b00..0x462ed0`, cdecl/thiscall free functions that look the thing
up by id and type-check it), then, unless replaying (`[0x51b5ac]`), records a `CommandScript` into the debug
journal stream `[0x54c4ec]` (`0x45ff90`; the "Map.Dbg"/"E3Multi.dbg" replay files). `CMultiPlayer` (static `0x578d40`)
builds a `CommandList` (vtable 0x504efc, size 0x1c: +4 time, +8 opcode, +0xc `IList<short>` words) with a
per-command builder and sends it with `Send 0x45d6e0`. The wire format is one byte (opcode) followed by the
16-bit words, sent through the net session `+0x290` (`0x4c7950`). Every machine, the sender included,
executes the command when it arrives: `OnReceive 0x45d2a0/0x45d2c0` → `CommandList::Deserialize 0x45a9b0`
→ **`CommandList::Execute 0x45ab10`** (switch on `opcode - 0x17`) → the same executors. The turn/lockstep
delay is in the net layer (`0x4c6xxx`), so local orders take effect only after the round trip. **M** for the turn mechanics.

### 6.2 Slot map (offset: SP impl / MP impl → executor; MP opcode)

| slot | name | SP | MP | executor | opcode | conf |
|---|---|---|---|---|---|---|
| +0x00 | dtor | 0x460330 | 0x45c560 | | | H |
| +0x04 | Init | 0x461470 | 0x45d7a0 | | | M |
| +0x08 | Shutdown (free queues) | 0x461750 | 0x45d860 | | | M |
| +0x0c | ProcessPending (SP: replay journal) | 0x460360 | ret 1 | | | M |
| +0x10 | ShouldSkipFrame (FrameTick) | ret 0 | 0x45f9b0 | | | M |
| +0x14 | (ret 1) | 0x4107d0 | 0x4107d0 | | | L |
| +0x18 | **GetMode** 0x32 SP / 0x33 MP | 0x4603c0 | 0x45d4a0 | | | H |
| +0x1c | GetLocalMachineId | ret 0 | 0x45ca80 (+0x2a4) | | | H |
| +0x20 | IsLocalInGame | ret 1 | 0x45ca90 | | | M |
| +0x24 | EndGame(result 0/1/2) | 0x463100 | 0x45f8b0 | | | M |
| +0x28 | SimulationStep (SP; MP steps on network turns) | 0x4603d0→0x463080 | ret | | | M |
| +0x2c | **Stop**(id) | 0x460660 | 0x45dd20 | 0x461d00 (vt+0xb8) | 0x28 | H |
| +0x30 | Scatter(id) (X) | 0x4606e0 | 0x45dd90 | 0x461d50 | 0x41 | M |
| +0x34 | **Move**(ids, path, flag) | 0x460430 | 0x45dfc0 | 0x461b00 (vt+0xb4; >2 units staggered via `0x462750`) | 0x29 | H |
| +0x38 | Patrol(ids, points, flag) | 0x4610b0 | 0x45e1f0 | 0x462380 | 0x40 | M |
| +0x3c | ClusterMove (C + click) | 0x4611c0 | 0x45e3d0 | 0x462400 | 0x43 | L |
| +0x40 | FormationMove(pt, ids) (Space) | 0x461250 | 0x45e650 | 0x462aa0 | 0x45 | M |
| +0x44 | **Attack**(ids, targetId) | 0x460900 | 0x45e830 | 0x461ea0 (vt+0xac, activity 0x2a) | 0x2a | H |
| +0x48 | MoveToThing(ids, thingId) | 0x461030 | 0x45ea10 | 0x462300 (vt+0xc0) | 0x3f | L |
| +0x4c | SetRallyPoint(labId, pts, flag) | 0x4604c0 | 0x45ebf0 | 0x461c00 | 0x30 | H |
| +0x50 | SetPowered(deviceId, on) | 0x460550 | 0x45ec70 | 0x461c50 (vt+0xa8) | 0x2b | M |
| +0x54 | WarpTo(warpId, x, y) | 0x4605d0 | 0x45ece0 | 0x461cc0 | 0x2c | M |
| +0x58 | **Plant**(shaman, pod, pts) | 0x460760 | 0x45ed60 | 0x461da0 | 0x2d | H |
| +0x5c | **Build**(shaman, type, x, y) | 0x4607f0 | 0x45ede0 | 0x461df0 | 0x2e | H |
| +0x60 | GetGenes(shaman, pts) | 0x460880 | 0x45ee60 | 0x461e50 | 0x2f | M |
| +0x64 | TurretTarget(id, target) | 0x460980 | 0x45eed0 | 0x461f20 | 0x37 | M |
| +0x68 | GeneLabBuild(lab, 6 args) | 0x460a00 | 0x45ef40 | 0x461f60 | 0x32 | M |
| +0x6c | GeneLabCancel(lab) | 0x460ab0 | 0x45efd0 | 0x461fc0 | 0x33 | M |
| +0x70 | GeneLabSelect(lab, value) | 0x460b30 | 0x45f040 | 0x462000 | 0x31 | L |
| +0x74 | Special(id) (J, Leviathan) | 0x460bb0 | 0x45f0b0 | 0x462040 (vt+0xb0) | 0x34 | M |
| +0x78 | Hold(id) (Ctrl+X) | 0x460c30 | 0x45f120 | 0x462090 (vt+0xbc) | 0x35 | M |
| +0x7c | Kill(id) (Ctrl+Alt+K) | 0x460cb0 | 0x45f190 | 0x4620e0 | 0x36 | H |
| +0x80 | Follow(id, target) | 0x460d30 | 0x45f200 | 0x462110 | 0x42 | M |
| +0x84 | Upgrade(shaman, target) | 0x460db0 | 0x45f270 | 0x462160 | 0x38 | M |
| +0x88 | **Repair**(shaman, target) | 0x460e30 | 0x45f2e0 | 0x4621b0 | 0x39 | H |
| +0x8c | HowitzerFire(id, x, y) | 0x460eb0 | 0x45f350 | 0x462200 | 0x3a | M |
| +0x90 | Pause(on) (not journaled) | 0x461010 | 0x45f550 | 0x4622c0 | 0x23 | M |
| +0x94 | **SetAlly(player, other, on)** | 0x460f40 | 0x45f3d0 | 0x462240 | 0x3b | H |
| +0x98 | SetMobile(id, on) | 0x461140 | 0x45f460 | 0x462930 | 0x44 | M |
| +0x9c | Flare(pt, player, mask, flag) | 0x4612d0 | 0x45f4d0 | 0x462980 | 0x46 | M |
| +0xa0 | SetStance(id, stance) | 0x461360 | 0x45f6c0 | 0x462e20 | 0x48 | M |
| +0xa4 | (MP only, 6 args) | ret 0x18 | 0x45f730 | 0x461420 | 0x49 | L |
| +0xa8 | PlayerQuit(machine, player, flag) | 0x4613e0 | 0x45f7c0 | 0x462e70 | 0x25 | M |
| +0xac | RemovePlayer(machine, player, flag) | 0x461400 | 0x45f840 | 0x462ed0 | 0x4a | M |
| +0xb0 | Chat(player, mask, text) | 0x460ff0 | 0x45f5d0 | 0x4622a0 | 0x47 | M |
| +0xb4 | SystemMessage(…, mask, text) | 0x460fd0 | ret 0xc | 0x462280 | | M |
| +0xb8 | SetGameSpeed(v) | 0x461020 | 0x45f650 | 0x4622f0 | 0x24 | M |
| +0xbc | NewRandomSeed (mission start) | 0x461430 | 0x45f8d0 | | | M |
| +0xc0..+0xd4 | slot/session helpers (§3.2) | | | | | H |

Opcodes 0x1a..0x20 (switch cases 3..9) are lobby/system messages. The base `CommandModel` fills
+0x2c..+0xbc with `_purecall 0x4ebf50`.

---

## 7. What skirmish alliances need

> **Done in WarBreeds HD v0.14.0** (`src/skirmish_teams.cpp`): Team and Color columns on the skirmish setup, teams applied after SetupSkirmish with `ApplyTeams`, team-grouped random starts, watching after defeat with a living ally, Victory for the team. Details in the development log (2026-10-12).

The game already supports everything after setup: the mask drives targeting, auto-attack, mines, turrets,
retaliation, AI targeting and AI threat maps, and team victory. What is missing is a way to set the masks
for a skirmish. Plan:

1. **UI.** `SMultiSetup` already has a per-slot Team value (slot +0x1e, "Team | Color | Clan | Player" header)
   for multiplayer. Expose it in skirmish mode (`+0x292e == 0`), or add our own per-slot team picker and
   write `cmd+4 + i*0x28 + 0x1e` of `g_SinglePlayerCmd 0x5511c8`. **M**
2. **Apply after setup.** Hook just after `Registry::SetupSP 0x43bd30` returns (call at `0x47aec9` in
   `SGame::LoadMission`): reset every player's `+0x18` to its own bit (this undoes the stock "all AIs
   allied" loop), copy each slot's team into `APlayer+0x4a30`, and call `ApplyTeams 0x43c8f0`
   (`__fastcall(registry)`). Or call `SetAllyBit 0x439580` per pair. Always set both directions:
   every test is one-sided. **H** for the mechanism
3. **Placement** "near allies" exists only in MP (`0x43c3b0`/`0x43c610`); for skirmish, sort the start-slot
   permutation `SMultiSetup+0x2968` by team, or call `TeamStartCentroid`. **M**
4. **AI** needs nothing more: its targets (`0x453d20`, `0x4502d0`) and threat map (`0x4aa500`) use the mask.
   `ConfigureAI` aggression can stay. **H**
5. **Victory.** Team victory works through `AllMutuallyAllied`. One gap: in SP, if the human is eliminated
   while an allied AI still fights, `PlayerDefeated` shows defeat at once (`0x43b7c0..0x43b811`). To let the
   team play on, patch that branch, for example to spectate until `CheckGameEnd`. **H** for the behaviour
6. **Optional.** The SAllies dialog (MP only, humans only) could be opened in SP and list AI rows (the
   `vt+0x7c` test in `0x475200`) for in-game diplomacy. `Cmd+0x94` already works in `CSinglePlayer`.
   Shared vision would need new code in `World 0x448730`. Saves already keep `+0x18`; `+0x4a30` is not
   serialized, but nothing needs it after setup.

---

## 8. Open questions / uncertain
- Labels of the session option values (+0x214 fog enum order, +0x218 extras values) are inferred from
  `0x47aff0` and the string order. **M**
- Turn scheduling inside the net layer (`0x4c6xxx`) and `CMultiPlayer +0x10` (frame skip under lag) were not traced. **L/M**
- Exact meaning of the APlayer slots +0x0c, +0x20, +0x24, +0x28, +0x38, +0x44..+0x5c and AI base-manager
  internals (`+0x4ccc`, `0x4ae470`, `0x4afbe0`). **L**
- Commands +0x3c, +0x48, +0x70, +0xa4: argument meaning. **L**
- Whether the official map editor writes per-player ally masks (key `{`) into maps; campaign co-operation
  (e.g. Kelika joining) may rely on it or on `AAction_Capture`. **L**
- `APlayer+0x487c/+0x487e` (supply/demand) and the `+0x4864..+0x4870` block. **L**
