// Addresses in WARBREED.EXE (GOG build 59030556949195281, image base 0x400000).
// See WIKI_LOG.md for how each one was found.
#pragma once
#include <stdint.h>

namespace game {

// short __cdecl SetResolution(short w, short h, int depth, short flag)
// Sets the DirectDraw mode, resizes the window and notifies every scene.
constexpr uintptr_t SetResolution = 0x46fa80;
// Its first 7 bytes: push ecx; push edi; call 0x469290
constexpr uint8_t SetResolutionPrologue[] = {0x51, 0x57, 0xe8, 0x09, 0x98, 0xff, 0xff};
constexpr uintptr_t SetResolutionPrologueCall = 0x469290;

// Inside SetResolution: call dword ptr [SetWindowPos] that resizes the window to the mode.
constexpr uintptr_t SetResolutionSetWindowPos = 0x46faf6;
constexpr uint8_t SetResolutionSetWindowPosBytes[] = {0xff, 0x15, 0xfc, 0x59, 0x59, 0x00};

// Per-frame task (Mohawk timer task on the main thread, registered at 0x471b60).
constexpr uintptr_t FrameTick = 0x46f400;
// sub esp, 0xc; push ebx; push esi; push edi
constexpr uint8_t FrameTickPrologue[] = {0x83, 0xec, 0x0c, 0x53, 0x56, 0x57};

// Current resolution as a short RECT {left, top, right, bottom}.
constexpr uintptr_t ScreenRight = 0x5508fc;
constexpr uintptr_t ScreenBottom = 0x5508fe;

// Scene stack: Scene* scenes[]; short topIndex (-1 when empty).
constexpr uintptr_t Scenes = 0x55aea8;
constexpr uintptr_t SceneTop = 0x51bee8;

// HWND of the game window.
constexpr uintptr_t MainWindow = 0x579290;

// Scene vtables (identified through RTTI).
constexpr uintptr_t VtblSGame = 0x507534;  // in-mission scene
constexpr uintptr_t SGameOnResize = 0x47b2f0;  // SGame vtable slot 0x4c

// void __thiscall SGame::ToggleZoom() - the Z key: switches the world view
// between 1x and 2x zoom-out (half-size world, HUD unchanged). Only switches to
// 2x if twice the view still fits inside the map.
constexpr uintptr_t SGameToggleZoom = 0x4802a0;
// void __thiscall SGame::ClampCamera() - clamps +0x38/+0x3a into the scroll
// bounds and notifies the view that the camera moved.
constexpr uintptr_t SGameClampCamera = 0x47d340;
constexpr uintptr_t SGameCameraX = 0x38;    // short, world pixels
constexpr uintptr_t SGameCameraY = 0x3a;    // short
// The camera's scroll bounds (world pixels), what SGame::ClampCamera clamps against:
// camera x in [MinX, MaxX - ViewW], y in [MinY, MaxY - ViewH].
constexpr uintptr_t SGameScrollMinX = 0x44;  // short
constexpr uintptr_t SGameScrollMinY = 0x46;  // short
constexpr uintptr_t SGameScrollMaxX = 0x48;  // short
constexpr uintptr_t SGameScrollMaxY = 0x4a;  // short
constexpr uintptr_t SGameViewW = 0x4c;      // short, world pixels visible
constexpr uintptr_t SGameViewH = 0x4e;      // short
constexpr uintptr_t SGameZoomedOut = 0x5e;  // short, 1 = 2x zoom-out active
// void __thiscall SGame::SetZoomedOut(short on) - on: doubles the view if twice it fits the
// map; off: clears +0x5e and halves the view (unconditionally), re-centring the camera.
constexpr uintptr_t SGameSetZoomedOut = 0x47d430;
// SGame::SetViewSize 0x47b520 (from the resize handler 0x47b2f0): view = frame size (x2 if
// zoomed out); then "call SGameSetZoomedOut(0)" here if the view is >= the map in either
// dimension, which halves a plain view too. See camera.cpp (FitViewToMap).
constexpr uintptr_t SetViewSizeFitCall = 0x47b581;
// Map size in 32 px tiles (bytes).
constexpr uintptr_t MapTilesW = 0x566db4;
constexpr uintptr_t MapTilesH = 0x566db5;

// void __thiscall Scene::UpdateCursor(int) - erases, saves-under and draws the
// software mouse cursor (menus; SGame overrides it with an empty function).
constexpr uintptr_t SceneUpdateCursor = 0x4950e0;

// Frame composition (see layers.cpp).
constexpr uintptr_t BackBufferPort = 0x5509a4;       // port handle everything is composed into
constexpr uintptr_t SelectBackBuffer = 0x469140;     // cdecl void(): SetPort([0x5509a4])
constexpr uintptr_t RefreshBlitterCache = 0x469180;  // cdecl void(): cache current port's pixmap at 0x550820
constexpr uintptr_t ClearCurrentPort = 0x469250;     // cdecl void(): fill current port with index 0
constexpr uintptr_t CreatePort = 0x4d83a0;           // stdcall port*(w, h, depth, flags): offscreen port
constexpr uintptr_t DisposePort = 0x4d9285;          // stdcall void(port*)
constexpr uintptr_t LockPort = 0x4d863f;             // stdcall short(port*): lock pixels (required by SetPort)
constexpr uintptr_t UnlockPort = 0x4d92d3;           // stdcall short(port*)
constexpr uintptr_t InterfaceDrawCall = 0x47b9a7;    // in SGame::Draw: call 0x47c000 (interface overlay)
constexpr uintptr_t InterfaceDraw = 0x47c000;
// Inside 0x47c000, after its world-space part (effects list [0x579280] incl.
// order markers 0x423c20, placement/drag 0x47bc90, world markers 0x47bdc0):
// mov ebp, [esp+0x1c]; mov ecx, esi
constexpr uintptr_t InterfaceWorldDone = 0x47c072;
constexpr uintptr_t FrameComplete = 0x46f54e;        // in the compose tick: mov word [0x579294], di
constexpr uintptr_t Present = 0x4693a0;              // cdecl void(): copy the back buffer to the window
constexpr uintptr_t MissionCursorDraw = 0x47c950;    // thiscall, 1 arg: draws the mission cursor
constexpr uintptr_t SceneCursorDraw = 0x495800;      // thiscall, no args: draws the scene cursor
constexpr uintptr_t CursorX = 0x5794d8;              // short, game cursor position
constexpr uintptr_t CursorY = 0x5794dc;

// Interface objects.
constexpr uintptr_t InfoPanel = 0x5476e8;          // static WInfoBox: selection / build panel (top right)
constexpr uintptr_t SGameMessageBox1 = 0x400;      // SGame member widget pointers (bottom centre)
constexpr uintptr_t SGameMessageBox2 = 0x404;

// --- Group build: several selected builders (controls.cpp) ------------------
// The stock game offers a builder's panel buttons (build, plant, repair,
// upgrade) and its B/P/R/U keys only when exactly one own Shaman is selected:
// the info panel tests the selection count, the keys SelectionKind() == 3.
// World selection count (int): World + 0x84 (data World + 0x80).
constexpr uintptr_t WorldSelCount = 0x84;
// short World + 0x6c: 1 = the info panel shows its second page (the stance page
// for creatures; the I key / the panel's page button toggle it).
constexpr uintptr_t WorldPanelPage = 0x6c;
// void __thiscall World::DrawInfoPanel(short camX, short camY) (ret 8): one own
// thing selected -> its panel (thing vtable +0x14); several -> "Squad (n)" only.
constexpr uintptr_t WorldDrawInfoPanel = 0x449410;
constexpr uintptr_t DrawInfoPanelCall = 0x47c1a9;  // in SGame_DrawInterface 0x47c000
// short __thiscall World::PanelClickDown(short sx, short sy) (ret 8): one thing
// selected -> thing vtable +0x18 (sx, sy, 0, 0, 0), which presses its panel
// button and captures the mouse; several -> the panel's own hit test only.
constexpr uintptr_t WorldPanelClickDown = 0x44f620;
constexpr uintptr_t PanelClickDownCalls[] = {
    0x47dad6,  // SGame::LButtonDown 0x47d500
    0x47eec9,  // SGame vtable slot 0x24 0x47eea0 (the second click of a double click)
    0x47d97b,  // SGame::LButtonDown with F10 held (debug panel drag)
};
// void __thiscall SGame::UpdateCursor 0x47c3d0 (no args): picks the mission
// cursor. With one own builder placing a building (World 0x452bc0: kind 3 and
// state 3) it draws no pointer, because the building's ghost is the pointer.
constexpr uintptr_t SGameUpdateCursor = 0x47c3d0;
constexpr uintptr_t UpdateCursorCall = 0x47c26b;  // in SGame_DrawInterface
// AShaman (vtable 0x504a2c) panel draw, slot +0x14: 0x442f10 (x, y, camX, camY),
// ret 0x10; per mode (+0x1ac) it adds the buttons, the build list or the pod list.
constexpr uintptr_t SlotShamanPanel = 0x504a40, ShamanPanel = 0x442f10;
// AShaman click-up, slot +0x1c: 0x442230 (sx, sy, wx, wy), ret 0x10; reached
// through the mouse capture. It releases the pressed panel button and acts:
// SetMode for the buttons, or, while working (AShaman 0x443410() == 1), the
// cancel button 0x546df8 (pressed word +0x16) stops it: Cmd+0x2c(shamanId).
constexpr uintptr_t SlotShamanClickUp = 0x504a48, ShamanClickUp = 0x442230;
constexpr uintptr_t ShamanWorking = 0x443410, CancelButton = 0x546df8;
constexpr uintptr_t CmdStop = 0x2c;  // (short id)
// void __thiscall AShaman::SetMode(int mode) 0x442720: from mode 0 only; 1 Get
// Genes, 2 build list, 4 pod list, 6 repair, 7 upgrade (what B/P/R/U/G call).
constexpr uintptr_t ShamanSetMode = 0x442720;
// void __thiscall WInfoBox::SetTitle(const char*) 0x497c00 on InfoPanel (copies).
constexpr uintptr_t PanelSetTitle = 0x497c00;
// char** strings[] (string table, id - 1): [0x55dd54] + 0xc8 = id 51 "Squad",
// the multi-selection title ("%s (%ld)", or "%s %ld (%ld)" when they share a group).
constexpr uintptr_t StringTable = 0x55dd54;
constexpr uintptr_t StrSquadOffset = 0xc8;
// int __thiscall World::CanPlace(short player, short x, short y, int type, short
// ignoreUnits, short skipSite, int) 0x4517e0 (ret 0x1c): 0 valid, 1 blocked,
// 2 no power / no pod near a Refinery. The placement click calls it with (1, 0, 0).
constexpr uintptr_t WorldCanPlace = 0x4517e0;
// The stock placement click (AShaman click-down 0x4419c0, mode 3, 0x441d2e):
// CanPlace == 0 -> Button::Release 0x49a6f0 (the build button 0x5452b0),
// AShaman::RemoveGhost 0x442d20, PlayClickSound 0x46fa70, AShaman::ResetMode
// 0x442c00, then Cmd+0x5c Build(shamanId, type, x, y). Nothing reads Shift.
constexpr uintptr_t ButtonRelease = 0x49a6f0, BuildButton = 0x5452b0, ShamanRemoveGhost = 0x442d20;
// short __thiscall AShaman::CanPlantAt(short wx, short wy) 0x442b10: pod soil,
// free, currently visible.
constexpr uintptr_t ShamanCanPlantAt = 0x442b10;
// Building footprint size in world pixels: short[type] (filled at run time).
constexpr uintptr_t FootprintTable = 0x54e370;
// Commands object [0x579260] vtable slots (single player executes at once,
// multiplayer serialises them for the lockstep turn).
constexpr uintptr_t CmdMove = 0x34;    // (IdList*, PtList*, short flag)
constexpr uintptr_t CmdPlant = 0x58;   // (short shamanId, int podType, PtList*)
constexpr uintptr_t CmdBuild = 0x5c;   // (short shamanId, int type, short x, short y), centre
constexpr uintptr_t CmdRepair = 0x88;  // (short builderId, short targetId): also finishes construction
// Creature / AShaman fields.
constexpr uintptr_t ThingActivity = 0xc0;  // int: 0x28 idle, 0x29 move, 0x2d plant, 0x2e build, 0x39 repair
constexpr uintptr_t ThingPathData = 0xb0, ThingPathCount = 0xb4;  // Pt[]: data[0] = final goal
constexpr uintptr_t ShamanWorkTarget = 0x18e;  // short: building being built/repaired (0xc01 none)
constexpr uintptr_t ShamanWalkX = 0x16c, ShamanWalkY = 0x16e;  // short: Build's site (0x441750)
// The build work tick 0x4436b0 (activity 0x2e): 0x40 ticks after arriving it
// creates the building (World 0x447410 -> 0x446db0, collision against every
// object but the builder itself) and stores its id in +0x18e. If that fails it
// goes idle with +0x1b4 = 1 and its failure count +0x170 (byte) raised; while
// +0x1b4 is set 0x443f10 steps it aside and then repeats the Build at +0x16c,
// +0x16e (gives up after 4 failures).
constexpr uintptr_t ShamanBuildRetry = 0x1b4;  // short

template <typename T>
inline T& at(uintptr_t addr) { return *reinterpret_cast<T*>(addr); }

}  // namespace game
