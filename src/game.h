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
constexpr uintptr_t SGameViewW = 0x4c;      // short, world pixels visible
constexpr uintptr_t SGameViewH = 0x4e;      // short
constexpr uintptr_t SGameZoomedOut = 0x5e;  // short, 1 = 2x zoom-out active

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

template <typename T>
inline T& at(uintptr_t addr) { return *reinterpret_cast<T*>(addr); }

}  // namespace game
