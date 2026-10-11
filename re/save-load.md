# Save/load game and the campaign progress record

> Research report (static analysis, nothing run). Confidence: **H** read from the code, **M** mechanism clear but purpose inferred, **L** a guess. Addresses are authoritative; where a name here differs from `re/names.tsv`, the TSV is the current one (and the export `re/ghidra-export-v2/` uses it). Index: `re/subsystems.md`.


Confidence: **H** means read from the instructions (disassembly checked), **M** means the mechanism is clear but the purpose is inferred, and **L** means a guess.

### Overview

- **A saved game uses the same format as a map.** A `.WBS` save is written by the same serializer that writes `.WBM` maps. That serializer is `SGame::SerializeStage` 0x4812f0, which runs over "stages" 2..8. The loader runs the same function, one stage per frame under the `SProgress` scene. **H**
- **The serializer works in both directions.** Every class has a single `Serialize(IResourceStream*)` method. It reads or writes depending on the stream's direction word at `IResourceStream+8` (1 = write, 0 = read). **H**
- **The file is a flat stream.** For `.wbm` and `.wbs`, `IResourceStream` writes a plain little-endian sequential stream. The string tags passed to `OpenTag` ("?", "+", "=", "CAMP" and so on) are **not** stored in the file. For example, `CAMPAIGN\SPTRN1A.WBM` starts `05 00 | 02 00 | 02 00 | 2c 00 | 2c 00 | 07 00 "Combat\0"`. That is exactly MapInfo's version, NPSP, NPRC, MPWD, MPHT, NMLN and name. **H**
  - With the extension `.dbg`, the stream instead becomes a tagged, Mohawk-style debug file: the ctor 0x46bee0 compares the extension with "dbg". **M**
  - Because the file is sequential, field order matters, and data appended after the last stage is never read by the stock loader. **M**
- **Where saves go.** Saves go to `<install>\DATA\Saves\<name>.wbs`. The folder ships with the game. The executable imports no CreateDirectory, so it never creates the folder. **H**

### Save path (in-mission Esc menu → file)

1. **Esc menu.** The in-mission Esc menu is the scene **`SMenu`** (scene id 7, object 0x579118, `MENU.MHK`). Note that `SMissionMenu` (id 0x13) is the *main* menu, and `SMissionDialog` (id 0x1f) is the Objectives screen.
   - `SMenu::Init` 0x489370 builds one of three button layouts (described below under "Where skirmish save is disabled"). **H**
   - `SMenu::OnCommand` 0x489830 handles the buttons. **H**
     - Save (+0x60) does `Scene_SendMessage(0x1c SSaveDialog, 0, 2)`, which stores the target scene SGame in `SSaveDialog+0x1d4`. It then calls `Scene_PostTransition(0x22 replace-top, 0x1c)`.
     - Load (+0x5c) does the same with `SOpenDialog` (0x15). See the load path.
2. **Save dialog.** **`SSaveDialog`** (id 0x1c, 0x550fe8, `SAVDLG.MHK`) derives from `SFileDialog`.
   - `Init` 0x493b20 does the following. **M/H**
     - It saves the current directory in +0x184 and makes `DATA\Saves` current (0x46fcd0).
     - It copies that directory into the browse dir +0x84.
     - It lists only `.wbs` files (`+0x1ca=1`, `+0x1cc=0`; the filter is in 0x479bc0).
     - It creates the name field (+0x1d0) and the "Save Game" and "Cancel" buttons.
   - OK or Enter (0x493f40 / 0x493ef0) calls slot 0x88 **`SSaveDialog::DoSave` 0x494030**. **H**
     - DoSave copies the edit text. If the text is not empty, it calls `Scene_SendMessage(target, 1, name)`, which is SGame message 1.
     - The dialog then pops itself (op 0x23).
3. **Queueing the save.** **`SGame::OnMessage` 0x4803e0** (SGame vtable slot 0x58) handles these codes: **H**
   - code 0 queues a load: 0x480540 copies the path to +0x1fe and sets +0x1fc.
   - code 1 queues a save: 0x480580 copies the name to +0x300 and sets +0x2fe.
   - code 2 sets **+0x82 = arg (the skirmish flag)**.
   - code 0x58 clears +0x1a0.
   - code 0x64 sets +0x86, which means "overwrite confirmed".
4. **Running the save.** **`SGame::Idle` 0x47ce80** (slot 0x10) runs the queued operations only when SGame is alone on the scene stack (`Scene_Depth() <= 1`). **H**
   - If +0x2fe is set, it redraws (0x46f5d0) and calls **`SGame::SaveGame(name)` 0x480ac0**.
   - If +0x86 is set, it calls **`SGame::WriteMapFile(+0x88)` 0x480be0**.
5. **Building the file name.** **`SGame::SaveGame` 0x480ac0**: **H**
   - It changes to `DATA\Saves`.
   - It builds the name with `SaveGame_MakeFileName` 0x480fe0, which appends `.wbs` unless the extension is already `wbm`, `dbg` or `wbs`. The result goes to +0x88.
   - It checks whether the file exists using tFileName stat 0x4d4165, which returns 0 when the file exists.
     - If the file exists, it pushes **`SOverwrite`** (id 0x29, "Overwrite this Game?"). Yes (0x4924c0) sends SGame message 0x64 and pops. No (0x4924e0) goes back to SSaveDialog.
     - Otherwise it sets +0x86, and the next Idle writes the file.
6. **Writing the file.** **`SGame::WriteMapFile(path)` 0x480be0**: **H**
   - It sets the map name in g_MapInfo to the file's base name (0x432150). The saved game therefore loses the original map's name.
   - It shows "Saving...".
   - It stores the view centre in the local player (0x481260, **M**).
   - It builds `IResourceStream(dir=1, path)` (0x46bee0, on the stack).
   - It calls **`SGame::SerializeAll` 0x4812c0**, which loops stage 2..8 over `SerializeStage` 0x4812f0. Then it prints "Map Saved".
   - The debug console's `save map` (0x4830b0; default `Map.Dbg` or `E3Multi.dbg`) and the official map editor's save use this same path.

### File layout: `SGame::SerializeStage(stream, stage)` 0x4812f0 (jump table 0x482090)

| Stage | Tag | Object (global) | Serializer | Notes |
|---|---|---|---|---|
| 2 | `?` | MapInfo (0x567578) | 0x432330 | Starts with a reset of the radar 0x5517b0 (0x433fb0). Fields: version short (must be 5), NPSP, NPRC, MPWD, MPHT, NMLN plus name, OBJC objectives and more. **H** |
| 2 | `+` | AGrid terrain (0x55dd60) | 0x42e080 (AGrid vtbl slot 6) | **H** |
| 3 | `=` | APlayerRegistry [0x57927c] | slot 0 = 0x43ae20 | PRHO (+0x2a), PRFO (+0x2c), then 8 player records. **H** (details below) |
| 3 | `FX` | AEffectRegistry [0x579280] | slot 0 = 0x425160 | **H** |
| 4 | `!` | AThingRegistry = World [0x579278] | slot 0 = 0x450fa0 | Per-player lists (8 × `FMSZ` count and ids), then `#` thing count, then each thing. **H** |
| 5 | `ER` | AEventRegistry [0x579284] | slot 1 = 0x41d940 | Triggers, conditions and actions (AElement::Serialize = slot 6). Afterwards: on write, 0x43cc90 (AI pre-save); on read, 0x43ba20 (players with +0x48f6==1 get vtbl+8, i.e. AI re-init). **M** |
| 6 | `=` | APlayerRegistry | slot 1 = 0x43b460 | Second pass: `BRN` per player calls player vtbl **+0x70**. On read, it also resets the CommandModel clock (0x4630e0). **H** |
| 7 | `CAMP` | ACampaign | `SGame::SerializeCampaign` 0x482150 | `CFLG` short = 1 if a campaign exists. If set: `CMID` id. On read, a new ACampaign is created and `ACampaign::Load(id)` reads MISSION.MHK and sets g_CampaignSide. Then `ACampaign::Serialize` 0x40abd0. **H** |
| 8 | `CLOCK` | CommandModel+0x238 | inline | Game clock, read and written through 0x4630d0 / 0x4630f0. **H** |
| 8 | `Z1`..`Z4` | SGame view rect (0x488020) | inline | On read, applied with 0x47d3b0 if it lies inside the map. **M** |
| 8 | `PW` | SGame waypoint list +0x1a8 (count +0x1b0) | inline + 0x485a10 | `tWayPoints@SGame` records. **M** |
| 8 | (end) | | | On read only: World 0x4473d0, radar 0x433820, "Map loaded.". **H** |

**Things.** For each slot, `AThingRegistry::Serialize` 0x450fa0 does the following. **H**
- When writing, it calls the thing's **vtable slot 18 (+0x48), `Serialize(IResourceStream*)`**. The first thing that method writes is the header:
  - `A` type (0x4a means an empty slot)
  - `B` id (0xc01 means none)
  - `C` owner
  - `D` x
  - `E` y
  - `TLOC`
- When reading, it reads A..E, creates the thing with `AThingRegistry::CreateThing` 0x446db0, finds it by id (0x454a20) and calls +0x48 on it.

Each class's `Serialize` override calls its base, checked through the call chain:

| Class | Serialize |
|---|---|
| AThing / ADeath | 0x446020 |
| ACreatureBody | 0x405400 → AThing |
| ACreature | 0x49aca0 → ACreatureBody |
| AShaman | 0x4440e0 → ACreature |
| ADevice (also ABattery, ACollector, AMine, ARelay, ARevitalizer) | 0x40e5c0 → AThing |
| AElectricTower | 0x410590 |
| AGeneLab | 0x429e60 |
| AHowitzer | 0x431250 |
| ARadar | 0x43d720 |
| ARefinery | 0x43e7e0 |
| ATurret | 0x456820 |
| AWarp | 0x458370 |
| AObstacle | 0x434f10 → AThing |
| AMushroom | 0x4451d0 → AThing |

**Players.** `APlayerRegistry::Serialize` 0x43ae20 handles 8 records. **H**
- Each record has `F` clan (0..4; 5 = empty), `G` byte, `H` isAI and `I` index.
- When reading, it calls `APlayerRegistry::CreatePlayer` 0x43b850. If isAI, that builds `AIComputerPlayer` (0x4e60 bytes, ctor 0x4b58b0); otherwise it builds `APlayer` (0x4a4c bytes, ctor 0x435ba0).
- It then calls player **vtable +0x6c `APlayer::Serialize` 0x438720**, which `AIComputerPlayer` does not override. That method covers:
  - the alliance mask +0x18 (`{`)
  - +0x19 (`PMSB`) and +0x1a (`J`)
  - CGK `int[4]` +0x4850 (`_`)
  - +0x4864 (`XU`)
  - +0x486e (`b`)
  - the start/view point +0x4a1c/+0x4a1e (`:`)
  - and more
- The vtable +0x70 slot is a no-op `ret 4` for APlayer (0x47a530). For AI players it is **`AIComputerPlayer::SerializeBrain` 0x4b7280**, which saves the AI's fields from +0x4a50 to +0x4da4. On read it first clears the squad list (0x4b81e0). **H** for the call; **M** for full coverage of the AI state.

**Stream primitives.** These are thiscall on the stream's sub-objects. **H**
- 0x409030 / 0x409050: `tMohawkInput` read short / long
- 0x409070 / 0x4090a0: `tMohawkOutput` write short / long
- 0x4090c0: `tDirectedIO` short in either direction
- 0x408f00: `tDirectedIO` bytes(buf, n)
- 0x46c8f0: `IResourceStream::OpenTag(out, tag)`
- 0x46cc50: `IResourceStream::OpenItem(out)`, the next list item
- 0x409130: child stream dtor

### Load path

**Loading from the Esc menu.** **H**
- `SMenu::OnCommand` (+0x5c) calls `SOpenDialog::Setup("OPNDLG.MHK", 0x1518, 0)` 0x491440.
- It sends SOpenDialog these messages: target SGame (code 0 → +0x2d8 = 2), saves only (code 1 arg 0 → +0x1ca = 1), code 2 and code 3.
- It replaces the top scene with SOpenDialog.

**Loading from the main menu.** **H**
- `SMissionMenu::OnCommand` 0x48ff40, at 0x490011, does the same with `OPNDLG2.MHK` and fromMainMenu = 1.
- The target SGame was set earlier by `SHeadquarters::Activate` 0x487af0.

**Picking the file.** `SFileDialog` builds the full path as browse dir +0x84 + `\` + name (0x4796e0). Open (0x491350 → slot 0x88 `SOpenDialog::DoOpen` 0x491420) sends `Scene_SendMessage(+0x2d8, 0, path)`. That is SGame message 0, which sets +0x1fc and copies the path to +0x1fe. **H/M**

**Starting the load.**
- In mission: `SGame::Idle` sees +0x1fc set. It destroys the command model (0x4707f0) and calls **`Game_Start(0x32)` 0x470780**. Game_Start points `g_CommandModel` at `CSinglePlayer` 0x5511c8 and makes **SProgress** (id 0x25) the root scene. SGame+0x80 is set to 1 so that SGame's exit (0x47b6a0) keeps the new command model. **H**
- From the main menu: `SHeadquarters::Activate` 0x487af0 sees `SGame::IsLoadPending` 0x4805c0. It sends SGame message 2 with arg 0, so **skirmish = 0**, and then calls `Game_Start(0x32)`. **H**

**Running the stages.** `SProgress::Idle` 0x4926c0 runs one stage per frame: `Game_LoadStage(stage)` 0x46f930 → **`SGame::LoadStage` 0x47ab30**.
- Stage 0 sets up the SGame UI (GAME.MHK).
- Stage 2 resets the SGame fields and builds a new World, player registry, effects and events (0x471c40, the "create world" routine).
- Stages 2..8 call **`SGame::LoadMapStage(path, stage)` 0x480610**. The path is SGame+0x1fe in single player, or the CMultiPlayer settings map path in multiplayer.
  - At stage 2, LoadMapStage changes directory. It uses g_CampaignDir 0x5673e8 (`<configured dir>\Campaign`) if a campaign object exists; otherwise it uses g_MapsDir `DATA\Maps`. Paths from the open dialog are absolute anyway.
  - Still at stage 2, it opens `new IResourceStream(dir=0, path)` (0x1a4 bytes) into SGame+0x40c. Every stage then calls `SerializeStage`, and the stream is closed after stage 8.
- At stage 8, `SGame::LoadStage` picks **g_LocalPlayer** [0x579288] as follows:
  - Multiplayer: by communication id.
  - Single player with **SGame+0x82 == 0** (campaign or loaded game): the first non-null player in 0..7.
  - Skirmish (+0x82 != 0): `Game_ApplyOptions(settings)` 0x47aff0 (fog, random gizmo scatter, …) and `APlayerRegistry::SetupSkirmish` 0x43bd30 → 0x43bd40 (assigns start slots and the local player).
  - It then centres the view on the local player's +0x4a1c/+0x4a1e.
- After stage 8, SProgress replaces the root scene with SGame (op 0x21, scene 2). **H**
- The objects that a loaded game re-creates are created by 0x471c40 at stage 2, by `CreatePlayer` at stage 3 and by `CreateThing` at stage 4. The SGame scene object itself is the static 0x567ef8 and is never re-created. **H**

### Where skirmish save is disabled

`SMenu::Init` 0x489370 chooses the layout of the Esc menu. **H**
- At **0x489512**, `g_CommandModel->vtbl[0x18]()` returns the kind: 0x33 for CMultiPlayer (0x45d4a0) and 0x32 for CSinglePlayer (0x4603c0). For 0x33 it builds the multiplayer layout: Resign and Alliances, with no Load or Save.
- At **0x4895a8**, `call 0x46f960; test ax,ax; jne 0x48968c`.
  - **`Game_IsSkirmish` 0x46f960** is `mov ax, [0x567f7a]`, i.e. **`g_SGame.isSkirmish` (SGame 0x567ef8 + 0x82)**.
  - When it is non-zero, the skirmish layout (0x48968c / helper 0x489750) adds only "AI Options" (+0x44).
  - Otherwise the campaign layout adds Objectives (+0x54), AI Options (+0x44), **Load Game (+0x5c)** and **Save Game (+0x60)** (helper 0x4897b0).
- The save code itself does not check for skirmish. Only the buttons are missing.

What sets the flag (always through SGame message 2 → 0x4804ac `mov [ebx+0x82], ax`): **H**
- **1**: `SMultiSetup::StartSkirmish` 0x48d440, at 0x48d556. Before that, `Scene_SendMessage(2,0,settings+0x170)` stores the map path, and afterwards it calls `Game_Start(0x32)`.
- **0**: `SMissionBrief::OnCommand` 0x48a140, at 0x48a1b2, when it starts a campaign mission, and `SHeadquarters::Activate` at 0x487bc4, when it starts a loaded game.
- The SGame ctor zeroes the flag (0x47a732).
- `Game_IsSkirmish` has 75 call sites. Most are draw code that picks player colours (for example 0x4010f0 uses player+0x484e when the game is multiplayer or skirmish). The rest are `SReward::Continue` 0x493740 (skirmish end → back to SMultiSetup), SProgress and others.

What a skirmish save would also need:

| Item | Saved now? | Detail |
|---|---|---|
| Skirmish flag (SGame+0x82) | **No** | The main-menu load forces it to 0. The flag must be restored *without* letting stage 8 run `Game_ApplyOptions` / `AssignPlayers`: those would re-scatter gizmos and re-assign start slots over the loaded world. **H** |
| Local-player index | **No** | Load picks the first non-null player. A skirmish whose human player is not the lowest slot would load as the wrong player. **H** |
| `GameSettings` (CommandModel+4) | **No** | Original map path settings+0x170, options +0x214, +0x218 (random gizmos), +0x220 (start positions), +0x224, and the per-player setup. The save stores the save's own name as the map name. Needed for "restart" or return-to-setup and for the AI options screen. **M** |
| AI players | **Yes** | Recreated as `AIComputerPlayer` (`H` = isAI) with their BRN brain block. Squads are cleared and rebuilt. Whether all of AISquad's and the AI difficulty state survives is **not verified (L)**. |
| Alliances | **Yes** | APlayer+0x18 is the per-player alliance bitmask (also used by SAllies 0x474e30, bit table 0x53e0e8); +0x19 is a second mask (shared vision? **L**). **M** |
| Victory and end-of-game flow | **No** | `SReward::Continue` 0x493740 uses the flag and g_Campaign. **H** |

Suggested place for the extra data: append it after stage 8 (hook the end of `SerializeStage` case 8, or `SerializeAll`). The stock loader stops after stage 8 and never reads the extra bytes. **M**

### Campaign progress record

**Definitions.** The campaign definitions are read-only Mohawk resources in **`DATA\MISSION.MHK`**. **H**
- `CAMP` id: a flags word (0x100 → ACampaign+0x1a) and a list of chapter ids.
- `CHAP`: a chapter. Its `CHMV` resource holds the chapter movie name, stored at +0x14.
- `MSET`: a mission set, i.e. alternative missions such as `SPTAN1A` / `SPTAN1B`. It holds AMission ids.
- `MISS`: the mission map file name, 0x51 bytes. The maps themselves are `CAMPAIGN\SP*.WBM`.

**Campaign ids.** The campaign ids are 1000, 1001 and 1002 (`[0x51d904]`=1000 plus the button). `SSinglePlayer::OnCommand` 0x494430 sets `g_CampaignSide` [0x5794bc] = 1, 2 or 3. Which of Tanu, Magha or Training each id is: **L**.

**Object model.**
- `ACampaign` (0x1c bytes; vtbl 0x50328c; slot 0 = Serialize 0x40abd0) holds `IList<AChapter*>`.
- `AChapter` (0x114 bytes) holds `IList<AMissionSet*>`.
- `AMissionSet` (0x24 bytes) holds `IList<AMission*>`.
- `AMission` is just the file name.
- The singleton is `g_Campaign` [0x57928c]. `SSinglePlayer::Activate` 0x494380 creates it, `ACampaign::Load` 0x40a830 fills it, and it is deleted when you leave.

**Progress.** Progress is three indexes: campaign id (+0x18), current chapter (+4), and the current set in that chapter (AChapter+0).
- After a mission, **`SReward::Continue` 0x493740** advances progress if the local player won: `NextMissionSet` 0x40ab10, or at the end of a chapter `NextChapter` 0x40ab00 plus the chapter movie (SCut id 0x17). At the end of the campaign it sets `g_CampaignComplete` [0x5792ec], or restarts (0x40ab60) for "looping" campaigns. **H**
- **Nothing is written to disk between missions.** There is no progress file, no registry write (only RegQuery/RegOpen are imported) and nothing in WARBREED.CFG. Progress persists only inside a saved game. **H**
  - That save holds the `CAMP` section: CFLG, CMID, then `ACampaign::Serialize` (`CMID` id, `CRCH` chapter, `CRMS` current set).
  - The current mission inside a set is not saved; it is selected again when the briefing starts. **M**

**Carry-over between missions (CGK, veterans, gizmos).** Nothing carries over. Each mission map is loaded fresh: the registry clears and re-creates every player at stage 3. The "rewards" the story mentions are scripted into each mission's events and run as actions (AElement vtbl slot 9 = Execute, slot 6 = Serialize). **H** for the mechanism, **M** that this is the only carry-over.
- **CGK = Clan Genetic Knowledge** (mission text: "Advance the Tanu Clan Genetic Knowledge (CGK) for the Sen-Soth to level 2"). Each player has `int cgk[4]` at APlayer+0x4850, one level per clan whose genes have been researched.
  - `AAction_CGK::Execute` 0x418260 raises player(+0x10) cgk[clan +0x14] to the level +0x18 using `APlayer::AdvanceCGK` 0x4376a0.
  - `ACondition_PlayerCGK::Test` 0x413e80 is true when cgk[+0x14] >= +0x10.
  - SCGK (scene 0x23) is the CGK screen. The console has `cgk` (0x485690). **H**
- `AAction_VeteranStatus::Execute` 0x417c00 finds a thing (+0x10) and raises its veteran level ACreatureBody+0x7c to +0x18 (0x406e20). **H** for the code, **M** for the field meaning.
- `AAction_SetGizmo::Execute` 0x418760 gives a gizmo (+0x18) to a creature (0x405230). **M**
- `AAction_Reward` (base; `PO` thing reference +0x14/+0x16) has the serializer 0x417930. **H**

### Open questions / uncertain

- Whether `AIComputerPlayer::SerializeBrain` covers squads, retina and difficulty well enough for the AI to keep playing after a load. Test by saving and loading a campaign mission that has an AI opponent. **L**
- What the APlayer fields +0x19, +0x1a, +0x4864, +0x486e mean. **L/M**
- The map-file version check: MapInfo version must be 5 (0x43249a). Is any other version accepted? **M**
- `.dbg` stream mode (0x46c710) and the exact Mohawk layout of tagged output. **L**
- Which campaign ids 1000, 1001 and 1002 are, and what the CAMP 0x100 "loop" flag is for (probably training). **L**
