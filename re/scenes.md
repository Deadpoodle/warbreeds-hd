# Scenes, widgets, input and the main loop

> Research report (static analysis, nothing run). Confidence: **H** read from the code, **M** mechanism clear but purpose inferred, **L** a guess. Addresses are authoritative; where a name here differs from `re/names.tsv`, the TSV is the current one (and the export `re/ghidra-export-v2/` uses it). Index: `re/subsystems.md`.


Static analysis of `WARBREED.EXE` (GOG build, image base 0x400000), from `re/ghidra-export/`,
`re/text.asm` and the RTTI vtable dump. Nothing was run. Confidence: **H** read from the
instructions, **M** mechanism clear but purpose inferred, **L** guess.

Function and field names used here are in `re/names.tsv` / `re/types.tsv`. Input details
(key table, mouse actions) are in `re/hotkeys.md` and `re/mouse.md`; this section only adds what
those files lack.

---

## 1. Main loop and frame pipeline

### Overview (H)
WarBreeds has no game loop of its own. `WinMain 0x4743d0` creates the window, calls
`GameInit 0x4720f0`, and then sits in a blocking `GetMessage`/`DispatchMessage` loop. All per-frame
work runs in **Mohawk timer tasks**, which are called from a Windows message hook:

- `Mohawk_OSManagerInit 0x4e3f32` (called from `MohawkInit 0x4689b0`) creates a hidden "MOHAWK OS
  Manager" window and installs `Mohawk_MessageHook 0x4e447f`.
- `Mohawk_CreateTask 0x4e3ce7(fn, arg, periodMs)` allocates a 0x20-byte task (`MohawkTask`, magic
  `'ITSK'`), links it into the list `[0x592bf0]`, and starts `SetTimer(id 0x5449, 1 ms)` on the
  manager window when the first task is created.
- The hook calls `Mohawk_RunTasks 0x4e3e59(now, force)` for every message the thread retrieves.
  On the task timer's `WM_TIMER` it uses force = 1, so every task runs. Otherwise only overdue tasks
  run (due time = `timeGetTime` + period). **M**: the periods are therefore only maxima; in practice
  the tasks run at the `WM_TIMER` rate.

`StartTimerTasks 0x471b50` (called from `App_StartRunning 0x471eb0` and `GameInit`) registers three
tasks:

| task | fn | period | job |
|---|---|---|---|
| frame | `FrameTick 0x46f400` | 200 | compose a frame (below) |
| present | `PresentTick 0x46f570` | 200 | if `[0x579294]` is set: `Present 0x4693a0`, clear it |
| logic | `LogicTick 0x46f6b0` | 500 | input-journal playback, `++[0x57929c]`, `Tick` (vtbl+0x10) on every scene bottom-up, scene-command countdown |

A fourth task, `NetPumpTick 0x46f7b0` (20 ms, `StartNetTask 0x46fb60`), pumps `CMultiPlayer 0x578d40`.
`StopTimerTasks 0x471ae0(all)` deletes the tasks. Cutscenes stop them while QuickTime plays,
because `SCut::OnStart` runs its own modal `GetMessage` loop.

All three game tasks return at once when `g_AppRunning [0x5792c8]` is 0 or `g_InWndProc [0x579268]`
is 1. WndProc sets `g_InWndProc` around every input dispatch, so a task never interrupts input
handling. FrameTick and PresentTick also return when the command model's vtbl+0x10 reports 1
(multiplayer net wait).

### FrameTick `0x46f400` (H)
1. Return if a frame is already waiting to be presented (`[0x579294]` = 1).
2. Return unless `g_SceneDrawingEnabled [0x51bee4]` is 1 and the stack is not empty.
3. `GetCursorPos`, then `Input_MouseMove 0x474230`. This is the only mouse-move path; see `re/mouse.md`.
4. `full` = 1 if a scene command is due within 2 ticks, or if any scene's `Scene::NeedsRedraw 0x496250`
   is true (a dirty widget, `+0x34`, or `g_IncrementalRedraw [0x51d9a8]` = 0).
5. For each scene from `scenes[0]` up: `Draw(full)` (vtbl+0x04). The top scene then gets
   `UpdateCursor(full)` (vtbl+0x08).
6. Draw the status line `WText 0x567740` (`Game_Printf 0x470a10` writes to it), then set `[0x579294]` = 1.

The mission scene redraws everything every frame: `SGame::LoadStage(1)` calls
`Scenes_SetIncrementalRedraw(0)`. Menus only redraw dirty widgets and keep the software cursor's
save-under (`Cursor_SaveUnder 0x4951b0` / `Cursor_RestoreUnder 0x495180`).

`Present 0x4693a0` copies the back-buffer port `[0x5509a4]` to the screen port `[0x55099c]` over the
screen rect `0x5508f8` (`0x4d878e`). It optionally draws blit-rate text first; the console's
"framerate" command toggles that. `ForceRedraw 0x46f5d0(showBusy)` runs present + FrameTick +
present synchronously (with an optional busy box) during long loads.

### Resolution (H)
`SetResolution 0x46fa80(w, h, depth, paletteMode)` does the following:
- blacks out the palette (`0x469290`) and sets the mode (`0x4695f0`);
- realizes the root scene's palette (vtbl+0x78);
- calls `SetWindowPos` on `g_MainWindow [0x579290]`;
- calls `OnResize(oldW, oldH)` (vtbl+0x4c) on every scene.

The base `Scene::OnResize 0x496280` moves every widget by the change in screen centre.
`SGame::OnResize 0x47b2f0` also calls `SetViewSize 0x47b520`, re-anchors the chat widgets and the info
panel `0x5476e8`, and copies the new size to `+0x28/+0x2a`. The root scene's mode is applied by
`Scenes_Push` (when the scene becomes the root) and by `App_Activate`.

### Start-up (M)
`WinMain` steps, in order:
1. creates the single-instance mutex "APP_INSTANCE_RUNNING";
2. calls `RegisterWindowClass 0x474300` (`CS_DBLCLKS`) and `CreateMainWindow 0x474380` (`WS_POPUP`, desktop size);
3. parses the command line: `skip` → first scene 0x10, `ROZ` → 0x0a (Red Orb Zone lobby launch),
   `-ir/-ip` input-journal record/playback, `-sr/-sp` a second record/playback pair;
4. calls `GameInit`, which ends with `StartTimerTasks` and `Scenes_PushInitial 0x471c00` (queues push of `g_InitialSceneId [0x51bed8]` = 0x18).

The boot chain is driven by each scene's "done" handler:
- SRedOrbCut (0x18, movie) → replace root 0x19 SWarBreedsScreen (3 s still);
- → 0x17 SIntroCut (movie);
- → 0x10 SHeadquarters (main hub; `src/menu.cpp` describes its SMissionMenu overlay).

SOrbFlat (0x26) also leads to 0x19.

---

## 2. Window procedure and input

`WndProc 0x4731a0` (class registered at `0x474300`).
- While the input journal is in playback mode (`0x46ddf0()` = 1), keyboard and mouse messages go
  to `DefWindowProc`.
- Mouse and key handling is covered in `re/mouse.md` §1 and `re/hotkeys.md` §1.1.

The full message map (**H**):

| msg | action |
|---|---|
| `WM_DESTROY` 0x02 | `App_Quit 0x4741c0` (PostQuitMessage) |
| `WM_SIZE` 0x05, wParam 0 | realize root palette, `App_Activate(hwnd, 1)` |
| `WM_PAINT` 0x0f | forwards 0x6003 to the Red Orb Zone object `[0x579258]` if present |
| `WM_SYSCOLORCHANGE` 0x15 | `RedrawWindow` |
| `WM_ACTIVATEAPP` 0x1c | `App_Activate 0x473a40(hwnd, wParam)`. Off: restore static colours, stop audio and tasks, minimise. On: restart, re-apply the root scene's mode and palette, invalidate all scenes, then synthesise a Space key-down/up pair |
| `WM_SETCURSOR` 0x20 | returns 1 (no system cursor; the scenes draw a software cursor) |
| `WM_KEYDOWN`/`WM_SYSKEYDOWN` | jump table **`0x47399c`** (6 entries for 0x100..0x105: 0x100/0x104 → `0x473399`, 0x101/0x105 → `0x4733ae`, 0x102/0x103 → default) → `Input_KeyDown 0x473b90` / `Input_KeyUp 0x473d20` |
| `WM_SYSCOMMAND` 0x112 | swallows `SC_KEYMENU`, `SC_SCREENSAVE`, `SC_MOVE` and 0xf090 (`SysCommandFilter 0x4739d0`) |
| `WM_TIMER` 0x113 | forwarded to the Red Orb Zone object only. The Mohawk task timer goes to the manager window, not here |
| `WM_LBUTTONDOWN..WM_RBUTTONUP` 0x201..0x205 | 0x201 directly, 0x202..0x205 via jump table `0x4739b4` → `Input_LButtonDown 0x473e40`, `Input_LButtonUp 0x473f00`, `Input_LButtonDblClk 0x473fc0`, `Input_RButtonDown 0x474080`, `Input_RButtonUp 0x474120` |
| `WM_QUERYNEWPALETTE` 0x30f | `App_RealizePalette 0x4741f0` |
| `WM_PALETTECHANGED` 0x311 | if another window changed it, same path as `WM_SIZE` |
| `MM_MCINOTIFY` 0x3b9 | single player, successful: `0x470900` (CD-audio track handling) |
| 0x6000 | creates the Red Orb Zone client `[0x579258]` ("RedOrbZn", "Red Orb Zone") |
| 0x6010 / 0x6020 | Red Orb Zone events: result 2 → replace root with SMultiSetup (5), 3 → SJoinCreate (0x0b) |

**Correction to `re/mouse.md`:** `[0x5792dc]` is not a general "input blocked" flag. It is
`g_SceneCmdPending`: all mouse buttons are ignored while a scene transition is queued (§3).

Key events reach the top scene only. The base `Scene::KeyDown 0x495530` (`Scene::OnKeyDown` in
`re/hotkeys.md`) does three things:
- Enter releases the active widget;
- when a `WTextEdit` (type 0x67) is active, keys go to `WTextEdit::OnChar 0x496f70`;
- otherwise widget hotkeys fire (mods 0 or Shift only).

Base mouse handling, all in Scene:
- `LButtonDown 0x4951f0` presses the topmost pressable widget (`Scene::WidgetAt 0x496040`).
- `LButtonUp 0x495290` calls `OnCommand` (vtbl+0x6c) if the release is over the pressed widget.
- `MouseMove 0x495360` updates `[0x5794d8/dc]`, hover on `WMouseOverButton`s, and press/release
  visuals while dragging.

---

## 3. Scene stack and scene commands

### Data (H)
- `g_Scenes 0x55aea8`: `Scene*` array, root first. WinMain clears three entries and `0x55aeb8` is
  the sound player, so the stack is at most 3 (maybe 4) deep. No bounds check.
- `g_SceneTop 0x51bee8` (short): top index, -1 when empty. `Scenes_Count 0x470960` = top + 1.
- **Every scene is a static object.** `Scene_GetById 0x4713c0` maps an id to its object (§4).
  Nothing is ever allocated or deleted. Constructors run from CRT static initialisers
  (`0x46e990`…`0x46f3a0`).

### Push / pop (H)
- **`Scenes_Push 0x471580(id)`** (returns short error):
  1. `++top`, store the scene, call `Init` (vtbl+0x14). A non-zero result aborts; the caller pops.
  2. If the scene is the root: `SetResolution` from its `+0x28/+0x2a/+0x2c/+0x30`, and
     `UseFullPalette` (vtbl+0x7c) → system-palette mode `0x4d717b`.
  3. If it is not the root, the scene below gets `Deactivate` (vtbl+0x54).
  4. The new scene gets `Activate` (vtbl+0x50).
  5. If it is the root: `GetPalette` → set palette, `Draw(1)`, `UpdateCursor(1)`, `Present`,
     `FadePalette(GetFadeInMode())`.
- **`Scenes_Pop 0x471690`**: top `Deactivate` + `Close` (vtbl+0x18), `--top`, clear the queued
  command, new top `Activate` + `Invalidate` (vtbl+0x48).
- `Scenes_Replace 0x471750(id)` = Pop + Push.
- `Scenes_PopToRoot 0x471770`, `Scenes_PopAll 0x471c20`.
- `Scenes_PopToRootChecked 0x471710(force)` refuses while multiplayer is busy.
- **`Scenes_ReplaceRoot 0x471790(id)`**: pop to root, `FadePalette(GetFadeOutMode())`, push id
  as the new root, then `Close` the old root.

### The command queue: how a button opens a scene (H)
Menus never push directly. They call **`Scenes_QueueCommand 0x4709a0(cmd, id, delay)`**. That is
the "open scene by id" entry used by the autopilot (`src/autopilot.cpp` mode 7: `(0x22, id, 5)`).
- It stores `[0x5792d0]` cmd, `[0x5792d4]` id, `[0x5792d8]` delay and sets `[0x5792dc]` = 1.
  A second request is ignored while one is pending.
- `LogicTick` decrements the delay and, at 0, calls **`Scenes_ProcessCommand 0x4717e0`**. If the
  sound player `0x55aeb8` is still playing (the click sound), it first adds 2 ticks, up to 0x50 times.

Commands:

| cmd | action |
|---|---|
| 0x1f | `Scenes_Push(id)` (dialogs over the current scene) |
| 0x20 | `Scenes_PopToRoot` |
| 0x21 | `Scenes_ReplaceRoot(id)` (main transitions: mission start, end, boot chain) |
| 0x22 | `Scenes_Replace(id)` (menu page to menu page) |
| 0x23 | `Scenes_Pop` (Resume, Cancel) |
| 0x25 | quit: `PostQuitMessage` (SGame cleanup first if it is the root) |

If a transition fails, the code pops everything, formats "Couldn't transition -- error code %ld."
and queues 0x25.

Typical button handler (`SMenu::OnCommand 0x489830`): compare the widget pointer with a member,
call `PlayClickSound 0x46fa70`, then `Scenes_QueueCommand(0x22, nextId, 5)`.
- Parameters go to the target scene first through **`Scene_Send 0x471560(id, what, value)`**,
  which calls `SetParam` (vtbl+0x58). Example: the Load button sends four `SetParam`s to
  SOpenDialog 0x15, then queues `(0x22, 0x15)`.
- Simple message boxes use `ShowDialog 0x471950` (SDialog 0x11, push or replace).
- Fatal errors use `MessageBoxRes 0x4719b0`.

Esc-menu (SMenu, id 7) buttons, by member offset:

| member | opens |
|---|---|
| +0x58 | resume (0x23 pop) |
| +0x5c | load: SOpenDialog 0x15 (loads OPNDLG.MHK first) |
| +0x60 | save: SSaveDialog 0x1c |
| +0x4c | SOption 0x0d |
| +0x48 | SAudioOption 0x1e |
| +0x44 | SAIOption 0x21 |
| +0x50 | SAllies 0x0c |
| +0x54 | objectives: SMissionDialog 0x1f |
| +0x38 | SLeave 0x22 |
| +0x3c | SQuit 0x2b |
| +0x40 | SMultiResign 0x2a (multiplayer) |

Mission start and end:
- `Game_StartCommandModel 0x470780(type)` selects `CSinglePlayer 0x5511c8` (type 0x32) or
  `CMultiPlayer 0x578d40`, starts it, and queues SProgress 0x25 as push (empty stack) or replace-root.
- `SGame::EndMission 0x47ee30(result)` pops to root, sends the model type and result to
  SReward (id 4), and queues replace-root 4 after 30 ticks.

---

## 4. Scene ids

`Scene_GetById 0x4713c0` (switch). Ids 3, 6, 8, 9 and 0x1b return NULL. Each static scene object
has its constructor in `re/names.tsv` and its vtable in the RTTI dump. Resource file = the
`.MHK` its Init/LoadResources opens (**M**, taken from string references).

| id | class | object | resource |
|---|---|---|---|
| 0x02 | SGame | 0x567ef8 | GAME.MHK |
| 0x04 | SReward | 0x5788f0 | reward.mhk |
| 0x05 | SMultiSetup | 0x55af18 | MSETUP.MHK |
| 0x07 | SMenu (in-mission Esc menu) | 0x579118 | MENU.MHK |
| 0x0a | SMultiDialog | 0x5676f0 | MULTI.MHK |
| 0x0b | SJoinCreate | 0x578b68 | JOIN.MHK |
| 0x0c | SAllies | 0x567e40 | ALLIES.MHK |
| 0x0d | SOption | 0x567588 | OPTION.MHK |
| 0x0e | SDeviceLab | 0x55ade8 | DEVLAB.MHK |
| 0x0f | SGeneLabScene | 0x578c00 | GENESCN.MHK |
| 0x10 | SHeadquarters (hub) | 0x551738 | HEADQT.MHK |
| 0x11 | SDialog (message box) | 0x5674e8 | DIALOG.MHK |
| 0x12 | SCredits | 0x5672a8 | CREDITS.MHK |
| 0x13 | SMissionMenu (main menu) | 0x579210 | MSMENU.MHK |
| 0x14 | SSinglePlayer | 0x579098 | SINGLE.MHK |
| 0x15 | SOpenDialog | 0x5784a0 | OPNDLG.MHK |
| 0x16 | SEvent | 0x5790d8 | ? |
| 0x17 | SIntroCut | 0x567118 | INTRO.MHK |
| 0x18 | SRedOrbCut | 0x5791c8 | REDORB.MHK |
| 0x19 | SWarBreedsScreen | 0x567538 | WARBRD.MHK |
| 0x1a | SMissionBrief | 0x578cd0 | MISSN.MHK |
| 0x1c | SSaveDialog | 0x550fe8 | SAVDLG.MHK |
| 0x1d | SMultiplayerOption | 0x567bc8 | MOPTN.MHK |
| 0x1e | SAudioOption | 0x567618 | AUDIOPT.MHK |
| 0x1f | SMissionDialog | 0x551560 | MSNDLG.MHK |
| 0x20 | SKickOut | 0x578460 | KCKOUT.MHK |
| 0x21 | SAIOption | 0x5515a8 | AIOPT.MHK |
| 0x22 | SLeave (yes/no) | 0x5675d0 | QUIT.MHK |
| 0x23 | SCGK | 0x5676a0 | AIOPT.MHK |
| 0x24 | STCPIP | 0x579180 | TCPIP.MHK |
| 0x25 | SProgress (loading bar) | 0x55aed0 | PROGRESS.MHK |
| 0x26 | SOrbFlat | 0x567660 | ORBFLAT.MHK |
| 0x27 | SQuitMain | 0x5515f0 | QUITMAIN.MHK |
| 0x28 | SIPJoin | 0x579050 | IPJOIN.MHK |
| 0x29 | SOverwrite (yes/no) | 0x567260 | QUIT.MHK |
| 0x2a | SMultiResign (yes/no) | 0x5788a8 | QUIT.MHK |
| 0x2b | SQuit (yes/no) | 0x567b80 | QUIT.MHK |

Abstract bases, with no id: Scene, SYesNoDialog, SCut, SFileDialog, SStaticScene.

---

## 5. Scene base class (vtable `0x508bec`, 32 slots)

### Layout (H, `Scene::Scene 0x494f60`, size 0x38)
| off | field |
|---|---|
| +0x00 | vptr |
| +0x04 | `IPalette*` palette (vtbl+0x78 returns it) |
| +0x08 | `IList<Widget*>` widgets: vptr 0x5075bc, +0x0c array, +0x10 count, +0x14 grow (4) |
| +0x18 | image sheet for buttons/statics (`Scene::LoadResources 0x495670`) |
| +0x1c | Mohawk resource file of the scene's .MHK |
| +0x20 | active widget (pressed, or the focused text edit) |
| +0x24 | hovered `WMouseOverButton` |
| +0x28/+0x2a/+0x2c/+0x30 | desired mode: 640, 480, 256, paletteMode 1 |
| +0x32 | OnStart done |
| +0x34 | needs full redraw |
| +0x36 | is top (Activate/Deactivate) |

`Scene::LoadResources(fileSpec, resId, wantPalette, wantImages)` is how every Init loads its .MHK.
Widgets are added with the `Scene::Add*` helpers (`0x495840`…`0x495fb0`: Button, Static, Text,
TextEdit, TextPage, ScrollBar, TextButton, OptionButton, TextButton2, NumberBar, CycleButton,
MouseOverButton, RadioButton, Cycle). Each helper allocates the widget, centres it on the screen
(`Widget::CentreOnScreen 0x49a8b0`), sets its image or text and hotkey, and appends it
(`Scene::AddWidget 0x495820`). Close (`0x495010`) deletes all of it.

### Slots (base implementations; M unless noted)
| slot | name | base | notes |
|---|---|---|---|
| 0x00 | ScalarDtor | 0x494fe0 | H |
| 0x04 | Draw(full) | 0x495090 | full=1: every widget's Draw, then clear dirty flags and +0x34. H |
| 0x08 | UpdateCursor(force) | 0x4950e0 | software cursor; SGame: empty. H |
| 0x0c | DrawCursor | 0x495800 | frame 0 of sheet 0x550770 at the cursor. H |
| 0x10 | Tick | 0x4957e0 | from LogicTick; base runs OnStart once. H |
| 0x14 | Init → short | 0x495000 | on push; 0 = ok. H |
| 0x18 | Close | 0x495010 | on pop. H |
| 0x1c..0x30 | LButtonDown, LButtonUp, RButtonDown, RButtonUp, MouseMove, LButtonDblClk (x, y) | 0x4951f0, 0x495290, ret0, ret0, 0x495360, ret0 | H (`re/mouse.md`) |
| 0x34 / 0x38 | KeyDown / KeyUp (vk, mods, ascii) | 0x495530 / 0x495610 | H |
| 0x3c | AcceptsMouse(x, y) | 0x495520 (1) | the left-button stack walk. H |
| 0x40 | OnStart | 0x4961a0 → Invalidate | first Tick after push |
| 0x44 | (unused) | empty | no caller found. L |
| 0x48 | Invalidate | 0x4961b0 | +0x34 = 1. H |
| 0x4c | OnResize(oldW, oldH) | 0x496280 | H |
| 0x50 / 0x54 | Activate / Deactivate | 0x4962e0 / 0x4962f0 | +0x36. H |
| 0x58 | SetParam(what, value) | empty | target of `Scene_Send`. H |
| 0x5c | SetMouseCapture(id) | empty | SGame `0x480520`. H |
| 0x60 | ResetHover | empty | SGame clears edge-scroll flags; no stack caller. L |
| 0x64 / 0x68 | GetFadeInMode / GetFadeOutMode | 0 / 1 | used with `Scene::FadePalette 0x496300` |
| 0x6c | OnCommand(Widget*) | empty | button clicked. H |
| 0x70 / 0x74 | OnSliderChange / OnSliderRelease (Widget*) | empty | WSlider (type 0x69) drag / release. H |
| 0x78 | GetPalette | 0x4746f0 | H |
| 0x7c | UseFullPalette | 1 | 1 → `SYSPAL_NOSTATIC` via `0x4d717b`, also 8-step fades; SJoinCreate/SMultiDialog return 0 |

`re/names.tsv` names every override as `<Class>::<slot name>`, generated from the vtables (one
line per distinct function; shared functions are named after the class that owns them).

### Subclass extensions
- **SYesNoDialog** (41 slots; SQuit, SQuitMain, SLeave, SIPJoin, SOverwrite, SMultiResign):
  - Init `0x494cc0` builds a WDialogBox, a message WText (+0x38), and Yes (+0x3c, id 1) and No
    (+0x40, id 3) WTextButtons.
  - OnCommand `0x494e80` calls slot 0x80 **OnYes** or 0x84 **OnNo**.
  - The other slots are 0x88 GetMessage, 0x8c/0x90 Yes/No hotkey (string table), 0x94
    LoadResources (QUIT.MHK), 0x98/0x9c/0xa0 text styles. **M**
  - Adding a confirm dialog = subclassing this and overriding OnYes/OnNo/GetMessage.
- **SCut** (38; SIntroCut, SRedOrbCut): QuickTime cutscenes.
  - OnStart `0x476a50` stops the timer tasks, opens the movie (+0x38, name from slot 0x88
    GetMovieName) and plays it in a modal loop. Space (KeyDown `0x476c70`) skips.
  - Tick calls 0x90 FinishMovie (cleanup, restart tasks), which queues the next scene.
  - The other slots are 0x80 StopMovie, 0x84 ResumeMovie, 0x8c HaltPlayback, 0x94 LoadResources. **M**
- **SStaticScene** (34; SWarBreedsScreen, SOrbFlat): a still image. Tick waits slot 0x80
  GetDuration ms (3000 in both subclasses) or a click/key (+0x38), then slot 0x84 OnDone queues the
  next scene. **H**
- **SFileDialog** (38; SOpenDialog, SSaveDialog): a file list in an AListBox (+0x40). Slots: 0x80
  OnItemChosen(index), 0x84 ChangeDirectory(name), 0x88 OnFileChosen, 0x8c AllowFileSelect, 0x90
  OnSelectionChanged, 0x94 Cancel (queue pop). **L/M**
- **SDeviceLab / SGeneLabScene** (35): 0x80 RestorePalette, 0x84 LoadPalette, 0x88 CreateWidgets. **L**

---

## 6. Widgets (vtable `0x5092a4`, 17 slots)

### Layout (H, `Widget::Widget 0x49a590`, base size 0x2c)
| off | field |
|---|---|
| +0x04 | type id (table in `re/types.tsv`, e.g. 0x6c WButton, 0x69 WSlider, 0x67 WTextEdit, 0x71 WMouseOverButton, 0x75 WScrollBar, 0x76 WTextButton2) |
| +0x08/+0x0a | origin |
| +0x0c | rect (l, t, r, b) |
| +0x14 | hotkey |
| +0x16 | mouse-pressed |
| +0x18 | key-pressed |
| +0x1a | dirty |
| +0x1c | image sheet |
| +0x20 | frame / command id (-1) |
| +0x22 | pressable |
| +0x24 | clickable |
| +0x28 | linked widget |

### Slots (H unless noted)
| slot | name | slot | name |
|---|---|---|---|
| 0x00 | ScalarDtor | 0x24 | SetImage(sheet, frame) |
| 0x04 | Draw (pure; per-class list in `src/widgets.h`) | 0x28 | GetTop |
| 0x08 | MatchesHotkey(char) | 0x2c | GetBottom |
| 0x0c | Contains(x, y) | 0x30 | GetLeft |
| 0x10 | MoveTo(x, y) | 0x34 | GetRight |
| 0x14 | MoveBy(dx, dy) | 0x38 | IsDirty |
| 0x18 | KeyPress | 0x3c | ClearDirty |
| 0x1c | MousePress | 0x40 | SetLinked(Widget*) |
| 0x20 | Release | | |

The getters 0x28..0x34, 0x38/0x3c and 0x40 are compiler-folded stubs shared with unrelated
classes. Their addresses are in the RTTI dump; `re/names.tsv` leaves them unnamed.

Extra slots:
- WCycle (22): SetItems, GetIndex, Next, Prev, SetVisible. **M**
- WRadioButton (20): SetGroup, GetState, SetState. **M**
- WScrollBar (23): scroll by / line back / line forward / part hit test / part rect. **L**
- WText (19): 0x44 SetMaxLength, 0x48 SetIndent. **L**
- WTextPage (22): 0x50 DrawLines, 0x54 Reflow. **L**

Sizes (from `operator new` in the Add* helpers): WButton 0x34, WStatic 0x2c, WText 0x43c (text at
+0x2c, style +0x434), WTextEdit 0x454, WTextPage 0x46c, WScrollBar 0x60, WTextButton 0x47c,
WOptionButton 0x8f8, WTextButton2 0x78, WNumberBar 0x30, WCycle/WCycleButton 0x40,
WMouseOverButton 0x38, WRadioButton 0x38, WSlider 0x34, WDialogBox 0x30, WLogo 0x34.

WInfoBox, WCGK and WFuzzyBar are only built as static or member objects. The mission info panel
is the static `WInfoBox 0x5476e8`, outside any scene list (`re/hotkeys.md` §4).

---

## 7. SGame (vtable `0x507534`, static object `0x567ef8`)

### Size and construction (M)
- `SGame::SGame 0x47a700`: base Scene, waypoint list (+0x198, 40 `IPixelC`), route list (+0x1a8),
  a second widget list (+0x1bc, purpose unknown), debug and console fields, and the 256 × 256-byte
  key-macro table at +0x464.
- Size **0x10464** (the next scene static is at 0x578460).
- Field table: `re/types.tsv`. It combines `re/mouse.md` §1, `re/hotkeys.md` and the new fields
  below.

### Slot overrides (H)
| slot | function | notes |
|---|---|---|
| 0x04 Draw | `0x47b770` | map view (`0x55dd60`; debug views +0x42e/+0x430/+0x432/+0x436), world layer `0x47b9d0`, `SGame::DrawInterface 0x47c000`, then widgets |
| 0x08 UpdateCursor | empty | the mission cursor is drawn inside DrawInterface |
| 0x10 Tick | `0x47ce80` | see below |
| 0x14 Init | `0x47a960` | |
| 0x18 Close | `0x47b6a0` | |
| 0x1c..0x38 | input | `re/mouse.md` / `re/hotkeys.md` |
| 0x40 OnStart | `0x47ce50` | |
| 0x48 Invalidate | `0x47ce40` | map-view full redraw |
| 0x4c OnResize | `0x47b2f0` | |
| 0x50 Activate | `0x47b410` | restores the previous selection |
| 0x54 Deactivate | `0x47b430` | clears the selection into "previous" and releases the capture |
| 0x58 SetParam | `0x4803e0` | 0 = mission name, 1 = save name, 0x64 = save pending |
| 0x5c SetMouseCapture | `0x480520` | |
| 0x60 ResetHover | `0x47dca0` | |

### Game turns (H)
**SGame::Tick 0x47ce80** is where the simulation advances. In order it:
1. keeps the chat widgets in sync;
2. pumps the sound player;
3. handles pending restart (+0x1fc), save requests (+0x2fe → `RequestSave(+0x300)`;
   +0x86 → `SaveGame(+0x88)`), and edge scrolling (+0x3c, `EdgeScroll 0x47d1e0`);
4. ticks the radar `0x5517b0`;
5. then `if (IsTurnDue 0x47d090) RunTurn 0x47d100`.

- **IsTurnDue**: `timeGetTime() - lastTurn(+0x18c) > turnPeriod(+0x188)`. Multiplayer is always
  due. It is false when paused (+0x460 = 1) or when input is disabled, and it also asks the command
  model (vtbl+0x0c).
- **RunTurn**: stores the time and calls command model vtbl+0x28 (one lockstep/simulation turn).
  It sets +0x418 if the turn took more than 3 ms. In single-step mode (+0x460 = 2) it drops back to
  paused.
- The turn period is 25 ms (single player) or 40 ms (multiplayer), set in LoadStage(0) and Init.
  The console command `update period <ms>` changes it, so this is the game-speed knob.

### Mission loading (M)
- `SGame::LoadStage 0x47ab30(stage)` (via `Game_LoadStage 0x46f930`). SProgress drives stages 0..8
  for its progress bar.
  - Stage 0 opens GAME.MHK and creates the chat entry (+0x400 WTextEdit), its background (+0x404
    WStatic) and the console (+0x41c WTextEdit). The game hides them right away; `ShowChat 0x47b460`
    / `HideChat 0x47b4a0` show and hide them.
  - Stage 1 turns off incremental redraw.
  - Stage 2 resets every input flag and the camera.
  - Stages 2..8 call `SGame::LoadMissionStage 0x480610(desc, stage)`. desc is the single-player
    name block at +0x1fe, or the CMultiPlayer setup.
  - Stage 8 chooses the local player `[0x579288]`, initialises the radar, calls `SetViewSize`, and
    centres on the player's start tile (player +0x4a1c, written by `StoreCameraInPlayer 0x481260`
    when saving).
- Save path: `SGame::RequestSave 0x480ac0(name)` copies the name to +0x88. It asks SOverwrite (0x29)
  if the file exists, otherwise sets +0x86. The next Tick calls `SGame::SaveGame 0x480be0(path)`,
  which streams `SGame::SerializeAllStages 0x4812c0` → `SerializeStage 0x4812f0(stream, 2..8)`.
  This is the save-game format (one routine for both directions; "Map Saved" / "Map loaded."
  strings).

### Cheat console (H for the table, M for most handlers)
- Typing `SPAMSPAMSPAMHUMBUG` sets +0x408; F9 then opens the console widget +0x41c (`re/hotkeys.md`).
- Each line goes to `Console_Execute 0x482480`, which prefix-matches the 54-entry table
  **`0x507360`** `{char *name, handler}`. Handlers are `thiscall SGame` with the rest of the line;
  output goes through `Game_Printf`.
- Commands: `ai`, `assimilate`, `bind`, `bounds on/off`, `create`, `egg boy returns`, `guide`, `kill`,
  `load map`, `player`, `simple load map`, `load terrain`, `mask grid on/off`, `place`,
  `process map`, `quake`, `registry`, `satan inside`, `save map`, `save terrain`, `selected`, `set`,
  `squad`, `whoami`, `terrain types on/off`, `thing grid on/off`, `search grid`, `things on/off`,
  `update period/pause/step/resume`, `ntd`, `makemap`, `minimap`, `netcheat`, `stop recording`,
  `damage`, `show map`, `target`, `turtle`, `say`, `supports`, `recommended`, `cgk`, `fog`, `crop`,
  `aggression`, `framerate`.

---

## 8. Connections to the planned features

- **Map editor**:
  - A new editor scene can follow the SGame pattern: a static Scene subclass, a scene id via
    `Scene_GetById`, and opening via `Scenes_QueueCommand(0x21/0x22, id, 5)`. Alternatively it can
    be a mode inside SGame.
  - The developer console already has `place` (object placement mode +0x43c/+0x454, with click
    handling in `SGame::LButtonDown`), `save map`/`load map` (the save-game stream), and
    `save terrain`/`load terrain` (terrain.hoo), plus `process map`, `makemap`, `crop`, and grid
    overlays (`thing grid`, `mask grid`, `search grid`, `terrain types`, `bounds`).
  - These are the fastest way into the map and object data; another section covers the data itself.
- **Pathfinding overhaul**: `search grid on` (+0x430) and `mask grid on` (+0x436) are existing
  debug overlays drawn by `SGame::Draw` through the map view.
- **Skirmish save/load**:
  - The save path is `RequestSave → SaveGame → SerializeStage(2..8)`, and the load path is
    `LoadMissionStage` / `SerializeStage`.
  - SSaveDialog/SOpenDialog reach SGame through `SetParam` and the +0x2fe/+0x300 and +0x86/+0x88
    request fields, processed in `SGame::Tick`.
  - The Esc menu's Save and Load buttons are SMenu +0x60 / +0x5c.
- **Game speed / pause**: +0x188 turn period, +0x460 pause/step; `Pause` key → command model +0x90
  (`re/hotkeys.md`).
- **Crash fixes**:
  - The scene stack has no bounds check (3–4 entries).
  - `Scenes_QueueCommand` silently drops a second request while one is pending.
  - Every scene is a static object, so re-entering a scene reuses its state; Init must reset
    everything.

---

## 9. Open questions / uncertain

- Mohawk task periods (200/200/500 ms) versus the actual rate. With the forced run on every
  `WM_TIMER` (1 ms timer, about 10–16 ms real), the tasks probably run every timer message. Not
  measured. **M**
- `g_Scenes` length 3 vs 4. **M**
- Scene slot 0x44 has no caller; slot 0x60 has no stack caller (SGame's version clears the edge
  scroll). **L**
- SGame +0x80, +0x82, +0x84, +0x414, +0x416, +0x432, +0x1bc (second widget list), +0x1f2/+0x1f4 are
  only partly understood. **L**
- SFileDialog and lab extension slots are named from a quick read. **L**
- `0x4d717b` is taken to set the system palette use (static / no-static) from its
  `GetDC`/`ReleaseDC` call pattern, not from the import name. **M**
- The second `-s?` command-line pair (`0x45fe10`/`0x45fe90`) is a record/playback of some
  synchronisation stream, distinct from the input journal. **L**
- Console handler names come from the command strings; most handler bodies were not read. **M**
