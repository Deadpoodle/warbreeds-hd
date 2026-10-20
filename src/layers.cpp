// Engine-level interface/cursor layers.
//
// In a mission the game composes each frame into one back buffer, addressed
// everywhere through the global port handle [0x5509a4]. SGame::Draw draws the
// world first, then the interface overlay (call at 0x47b9a7), then SGame's own
// widgets; overlay scenes (the Esc menu), the cursor and tooltip text follow;
// 0x46f54e marks the frame ready for presenting. The overlay function itself
// starts with world-space drawing (order markers and other effects, building
// placement, drag box) before the screen-space interface (0x47c072 onwards).
//
// We give the engine two extra ports of the same size, I (interface) and C
// (cursor), and swap the global handle for whole phases:
//   0x47c072 in the overlay call from 0x47b9a7 (interface starts)
//                                  [0x5509a4] = I, cleared
//   cursor draw functions          [0x5509a4] = C for their duration
//   0x46f54e (frame complete)      copy I and C out, [0x5509a4] = back buffer
// so the back buffer only ever holds the battlefield, and I and C are rebuilt
// from scratch every frame. Switching uses the engine's own SetPort
// (0x469140) and blitter-cache refresh (0x469180), so every drawing path
// follows. Colour index 0 (never used by the game's art) means empty.
#include "layers.h"

#include <windows.h>
#include <string.h>

#include <algorithm>
#include <set>

#include "game.h"
#include "prof.h"

namespace layers {
namespace {

void (*g_log)(const char*, ...);
void (*g_publish)(const Captured&);
bool g_debug;  // [Debug] LayerLog
bool (*g_bottomUp)(const void* bits);  // row order of a port's DIB

typedef void*(__stdcall* CreatePort_t)(int w, int h, int depth, int flags);
typedef void(__stdcall* DisposePort_t)(void* port);
typedef short(__stdcall* PortOp_t)(void* port);
typedef void(__cdecl* Void_t)();

struct Port {
    void* handle = nullptr;
    int w = 0, h = 0;
};
Port g_ui, g_cursor;
void* g_backBuffer;   // the real back buffer while a phase is active
bool g_armed;         // the drawing pass of the overlay (0x47b9a7) has started
bool g_active;        // interface phase in progress
int g_cursorDepth;
Captured g_captured;  // reused buffers

void*& BackBufferHandle() { return game::at<void*>(game::BackBufferPort); }

void SelectBackBuffer() {
    ((Void_t)game::SelectBackBuffer)();
    ((Void_t)game::RefreshBlitterCache)();
}

void Recreate(Port& p, int w, int h) {
    if (p.handle && p.w == w && p.h == h) return;
    prof::Scope scope("layer-ports");
    if (p.handle) {
        ((PortOp_t)game::UnlockPort)(p.handle);
        ((DisposePort_t)game::DisposePort)(p.handle);
    }
    // As the engine does for its own ports (0x468cf7..0x468d5e): create, then
    // lock its pixels; SetPort refuses a port that is not locked (+0x68).
    p.handle = ((CreatePort_t)game::CreatePort)(w, h, 8, 0);
    if (p.handle) ((PortOp_t)game::LockPort)(p.handle);
    p.w = w;
    p.h = h;
    g_log("layers: created %dx%d port %p", w, h, p.handle);
}

// The port's pixels: port +0xd0 -> pixmap, pixmap +0xc -> base address.
const uint8_t* PortBits(const Port& p) {
    if (!p.handle || IsBadReadPtr(p.handle, 0xd4)) return nullptr;
    auto* pixmap = *(const uint8_t**)((const uint8_t*)p.handle + 0xd0);
    if (!pixmap || IsBadReadPtr(pixmap, 0x10)) return nullptr;
    return *(const uint8_t**)(pixmap + 0xc);
}

// [Debug] LayerLog: the blitter cache's pixel base (0x550820+0x18) and the
// current port, against each port's pixel memory.
void Trace(const char* where) {
    static int frames;
    if (!g_debug || frames > 12) return;
    if (!strcmp(where, "begin")) ++frames;
    Port b;
    b.handle = g_backBuffer ? g_backBuffer : BackBufferHandle();
    g_log("layers: %-12s slot=%p cur=%p cache=%p | B=%p bits %p | I=%p bits %p | C=%p bits %p", where,
          BackBufferHandle(), game::at<void*>(0x5928f0), game::at<void*>(0x550838), b.handle, PortBits(b),
          g_ui.handle, PortBits(g_ui), g_cursor.handle, PortBits(g_cursor));
}

bool InMissionScene() {
    if (game::at<short>(game::SceneTop) < 0) return false;
    void* base = game::at<void*>(game::Scenes);
    return base && !IsBadReadPtr(base, 4) && *(uintptr_t*)base == game::VtblSGame;
}

void __cdecl BeginInterface() {
    if (g_active || !InMissionScene()) return;
    int w = game::at<short>(game::ScreenRight), h = game::at<short>(game::ScreenBottom);
    if (w <= 0 || h <= 0) return;
    Recreate(g_ui, w, h);
    Recreate(g_cursor, w, h);
    if (!g_ui.handle || !g_cursor.handle) return;
    g_backBuffer = BackBufferHandle();
    Trace("begin");
    BackBufferHandle() = g_cursor.handle;
    SelectBackBuffer();
    ((Void_t)game::ClearCurrentPort)();
    BackBufferHandle() = g_ui.handle;
    SelectBackBuffer();
    ((Void_t)game::ClearCurrentPort)();
    g_active = true;
    Trace("begin->ui");
}

// 0x47b9a7 calls the overlay for drawing; 0x47ce2e calls it to record dirty
// areas only. The interface starts part-way through, and only when drawing.
void __cdecl ArmInterface() { g_armed = true; }

// The world overlay (the map editor's start points), drawn into the back
// buffer after the world-space part of the overlay call.
void (*g_worldOverlay)();
Port g_worldPort;  // the back buffer while the overlay draws

void __cdecl BeginArmedInterface() {
    if (!g_armed) return;
    g_armed = false;
    if (g_worldOverlay && !g_active && InMissionScene()) {
        g_worldPort.handle = BackBufferHandle();
        g_worldPort.w = game::at<short>(game::ScreenRight);
        g_worldPort.h = game::at<short>(game::ScreenBottom);
        g_worldOverlay();
        g_worldPort.handle = nullptr;
    }
    BeginInterface();
}

void CopyOut(const Port& p, std::vector<uint8_t>& out) {
    const uint8_t* bits = PortBits(p);
    out.resize((size_t)p.w * p.h);
    if (!bits) {
        memset(out.data(), 0, out.size());
        return;
    }
    int pitch = (p.w + 3) & ~3;
    bool bottomUp = g_bottomUp ? g_bottomUp(bits) : true;
    for (int y = 0; y < p.h; ++y) {
        int row = bottomUp ? p.h - 1 - y : y;
        memcpy(&out[(size_t)y * p.w], bits + (size_t)row * pitch, p.w);
    }
}

void __cdecl EndInterface() {
    if (!g_active) return;
    if (prof::Enabled()) prof::Add("compose", prof::Now() - prof::FrameStart());
    prof::Scope scope("copyout");
    prof::SetPhase("other");
    Trace("end");
    GdiFlush();
    g_captured.w = g_ui.w;
    g_captured.h = g_ui.h;
    CopyOut(g_ui, g_captured.ui);
    CopyOut(g_cursor, g_captured.cursor);
    g_captured.cursorX = game::at<short>(game::CursorX);
    g_captured.cursorY = game::at<short>(game::CursorY);
    BackBufferHandle() = g_backBuffer;
    SelectBackBuffer();
    g_active = false;
    Trace("end->back");
    if (g_publish) g_publish(g_captured);
    if (g_debug) {
        static DWORD last;
        DWORD now = GetTickCount();
        if (now - last > 1000) {
            last = now;
            size_t ui = 0, cur = 0;
            for (uint8_t v : g_captured.ui) ui += v != 0;
            for (uint8_t v : g_captured.cursor) cur += v != 0;
            g_log("layers: frame %dx%d back=%p ui=%p cur=%p port=%p flags=%d,%d ui px=%zu cursor px=%zu", g_ui.w,
                  g_ui.h, BackBufferHandle(), g_ui.handle, g_cursor.handle, game::at<void*>(0x5928f0),
                  game::at<short>(0x550994), game::at<short>(0x550990), ui, cur);
        }
    }
}

void __cdecl Nothing() {}

void __cdecl BeginCursor() {
    if (!g_active || g_cursorDepth++ != 0) return;
    BackBufferHandle() = g_cursor.handle;
    SelectBackBuffer();
}

void __cdecl EndCursor() {
    if (!g_active || g_cursorDepth == 0 || --g_cursorDepth != 0) return;
    BackBufferHandle() = g_ui.handle;
    SelectBackBuffer();
}

// Menus: Scene::UpdateCursor (0x4950e0) restores the saved-under pixels,
// saves under the new position and calls the scene's cursor draw (vtable
// +0xc; eleven variants, all drawing a cursor-sheet frame at the cursor
// position). We redirect that call (0x49512f) into a small port of its own,
// with the cursor at the port's centre, so the menu image never contains the
// cursor and it is never clipped at the frame edge; the renderer draws it at
// the real mouse position (also over the widescreen sides). The save-under
// and restore around it then only ever see clean menu pixels.
constexpr int kMenuCursorSize = 128;
void (*g_menuSink)(const uint8_t* px, int size, int gameX, int gameY);
Port g_menuCursor;
std::vector<uint8_t> g_menuCursorPx;
bool g_menuPath;
void* g_menuSaved;
short g_menuX, g_menuY;

void __cdecl BeginMenuCursor() {
    g_menuPath = !g_active && g_menuSink;
    if (!g_menuPath) return;
    Recreate(g_menuCursor, kMenuCursorSize, kMenuCursorSize);
    if (!g_menuCursor.handle) {
        g_menuPath = false;
        return;
    }
    g_menuSaved = BackBufferHandle();
    BackBufferHandle() = g_menuCursor.handle;
    SelectBackBuffer();
    ((Void_t)game::ClearCurrentPort)();
    g_menuX = game::at<short>(game::CursorX);
    g_menuY = game::at<short>(game::CursorY);
    game::at<short>(game::CursorX) = kMenuCursorSize / 2;
    game::at<short>(game::CursorY) = kMenuCursorSize / 2;
}

void __cdecl EndMenuCursor() {
    if (!g_menuPath) return;
    g_menuPath = false;
    game::at<short>(game::CursorX) = g_menuX;
    game::at<short>(game::CursorY) = g_menuY;
    GdiFlush();
    CopyOut(g_menuCursor, g_menuCursorPx);
    BackBufferHandle() = g_menuSaved;
    SelectBackBuffer();
    g_menuSink(g_menuCursorPx.data(), kMenuCursorSize, g_menuX, g_menuY);
}

// --- code patching ---------------------------------------------------------

uint8_t* EmitRel(uint8_t* p, uint8_t op, uintptr_t target) {
    *p++ = op;
    *(int32_t*)p = (int32_t)(target - ((uintptr_t)p + 4));
    return p + 4;
}

uint8_t* AllocCode(size_t n) {
    return (uint8_t*)VirtualAlloc(nullptr, n, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
}

bool WriteJmp(uintptr_t at, uintptr_t target, int len) {
    uint8_t buf[16];
    memset(buf, 0x90, sizeof buf);
    buf[0] = 0xe9;
    *(int32_t*)(buf + 1) = (int32_t)(target - (at + 5));
    DWORD old;
    if (!VirtualProtect((void*)at, len, PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy((void*)at, buf, len);
    VirtualProtect((void*)at, len, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void*)at, len);
    return true;
}

// Wraps a function (cdecl or thiscall, `argBytes` of stack arguments popped
// by the callee): before() runs first, after() once it returns. `prologue`
// (len >= 5 bytes, no relative branches) is relocated into a trampoline.
bool Wrap(uintptr_t addr, const uint8_t* prologue, int len, int argBytes, void (*before)(), void (*after)()) {
    if (memcmp((void*)addr, prologue, len) != 0) return false;
    uint8_t* mem = AllocCode(128);
    if (!mem) return false;
    uint8_t* tramp = mem + 96;
    memcpy(tramp, prologue, len);
    EmitRel(tramp + len, 0xe9, addr + len);

    // pushad; call before; popad; (push dword [esp+argBytes]) x n; call tramp;
    // push eax; push edx; call after; pop edx; pop eax; ret argBytes
    uint8_t* p = mem;
    *p++ = 0x60;
    p = EmitRel(p, 0xe8, (uintptr_t)before);
    *p++ = 0x61;
    for (int i = 0; i < argBytes / 4; ++i) {
        *p++ = 0xff;
        *p++ = 0x74;
        *p++ = 0x24;
        *p++ = (uint8_t)argBytes;
    }
    p = EmitRel(p, 0xe8, (uintptr_t)tramp);
    *p++ = 0x50;
    *p++ = 0x52;
    p = EmitRel(p, 0xe8, (uintptr_t)after);
    *p++ = 0x5a;
    *p++ = 0x58;
    if (argBytes) {
        *p++ = 0xc2;
        *(uint16_t*)p = (uint16_t)argBytes;
    } else {
        *p++ = 0xc3;
    }
    FlushInstructionCache(GetCurrentProcess(), mem, 128);
    return WriteJmp(addr, (uintptr_t)mem, 5);
}

// 0x47b9a7: call 0x47c000  ->  call thunk: pushad; call ArmInterface; popad; jmp 0x47c000
bool PatchInterfaceCall() {
    const uint8_t expect[] = {0xe8, 0x54, 0x06, 0x00, 0x00};
    if (memcmp((void*)game::InterfaceDrawCall, expect, sizeof expect) != 0) return false;
    uint8_t* t = AllocCode(32);
    if (!t) return false;
    uint8_t* p = t;
    *p++ = 0x60;
    p = EmitRel(p, 0xe8, (uintptr_t)&ArmInterface);
    *p++ = 0x61;
    EmitRel(p, 0xe9, game::InterfaceDraw);
    uint8_t call[5];
    call[0] = 0xe8;
    *(int32_t*)(call + 1) = (int32_t)((uintptr_t)t - (game::InterfaceDrawCall + 5));
    DWORD old;
    VirtualProtect((void*)game::InterfaceDrawCall, 5, PAGE_EXECUTE_READWRITE, &old);
    memcpy((void*)game::InterfaceDrawCall, call, 5);
    VirtualProtect((void*)game::InterfaceDrawCall, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void*)game::InterfaceDrawCall, 5);
    return true;
}

// 0x47c072: mov ebp, [esp+0x1c]; mov ecx, esi
//   -> jmp stub: pushad; pushfd; call BeginArmedInterface; popfd; popad; <original 6 bytes>; jmp 0x47c078
bool PatchInterfaceStart() {
    const uint8_t expect[] = {0x8b, 0x6c, 0x24, 0x1c, 0x8b, 0xce};
    if (memcmp((void*)game::InterfaceWorldDone, expect, sizeof expect) != 0) return false;
    uint8_t* t = AllocCode(32);
    if (!t) return false;
    uint8_t* p = t;
    *p++ = 0x60;
    *p++ = 0x9c;
    p = EmitRel(p, 0xe8, (uintptr_t)&BeginArmedInterface);
    *p++ = 0x9d;
    *p++ = 0x61;
    memcpy(p, expect, sizeof expect);
    p += sizeof expect;
    EmitRel(p, 0xe9, game::InterfaceWorldDone + sizeof expect);
    return WriteJmp(game::InterfaceWorldDone, (uintptr_t)t, sizeof expect);
}

// 0x49512f in Scene::UpdateCursor: mov edx, [esi]; mov ecx, esi; call [edx+0xc]
//   -> jmp stub: pushad; call BeginMenuCursor; popad; <original 7 bytes>;
//      pushad; call EndMenuCursor; popad; jmp 0x495136
bool PatchMenuCursorCall() {
    const uintptr_t at = 0x49512f;
    const uint8_t expect[] = {0x8b, 0x16, 0x8b, 0xce, 0xff, 0x52, 0x0c};
    if (memcmp((void*)at, expect, sizeof expect) != 0) return false;
    uint8_t* t = AllocCode(48);
    if (!t) return false;
    uint8_t* p = t;
    *p++ = 0x60;
    p = EmitRel(p, 0xe8, (uintptr_t)&BeginMenuCursor);
    *p++ = 0x61;
    memcpy(p, expect, sizeof expect);
    p += sizeof expect;
    *p++ = 0x60;
    p = EmitRel(p, 0xe8, (uintptr_t)&EndMenuCursor);
    *p++ = 0x61;
    EmitRel(p, 0xe9, at + sizeof expect);
    FlushInstructionCache(GetCurrentProcess(), t, 48);
    return WriteJmp(at, (uintptr_t)t, sizeof expect);
}

// 0x46f54e: mov word ptr [0x579294], di  (frame complete)
//   -> jmp stub: pushad; call EndInterface; popad; <original 7 bytes>; jmp 0x46f555
bool PatchFrameComplete() {
    const uint8_t expect[] = {0x66, 0x89, 0x3d, 0x94, 0x92, 0x57, 0x00};
    if (memcmp((void*)game::FrameComplete, expect, sizeof expect) != 0) return false;
    uint8_t* t = AllocCode(32);
    if (!t) return false;
    uint8_t* p = t;
    *p++ = 0x60;
    p = EmitRel(p, 0xe8, (uintptr_t)&EndInterface);
    *p++ = 0x61;
    memcpy(p, expect, sizeof expect);
    p += sizeof expect;
    EmitRel(p, 0xe9, game::FrameComplete + sizeof expect);
    return WriteJmp(game::FrameComplete, (uintptr_t)t, sizeof expect);
}

}  // namespace

// A text page (WTextPage: the Objectives window's text, ...) draws its text over a
// copy of what was under it, taken from the current port when its text is laid out
// (Reflow, 0x498750). In a mission the current port then holds only the battlefield:
// the window's art is drawn on the interface port, every frame. The text area showed
// the terrain (a player's report). A page laid out outside the interface phase takes
// that copy again at its first Draw inside it, from the interface port, where the
// window's art has just been drawn, and lays its text out over it again.
constexpr uintptr_t kTextPageReflowSlot = 0x509068, kTextPageReflow = 0x498750;  // vtable +0x54
constexpr uintptr_t kTextPageDrawSlot = 0x509018, kTextPageDraw = 0x498b90;      // vtable +4
constexpr uintptr_t kTextPageDrawLines = 0x4989a0;                                // vtable +0x50
constexpr int kPageBackground = 0x454, kPageSize = 0x464, kWidgetRect = 0xc;  // size: short w, h
std::set<void*> g_stalePages;

void __fastcall TextPageReflow(char* page) {
    ((void(__thiscall*)(void*))kTextPageReflow)(page);
    if (g_active)
        g_stalePages.erase(page);
    else
        g_stalePages.insert(page);
}

// The copy is of the colour indices as they are: the engine's port copy (0x4d878e)
// maps colours through each port's colour table, and the interface port's isn't the
// game palette (the game's art reaches it through the blitter, as indices).
void CopyFromInterface(void* to, int w, int h, int x0, int y0) {
    Port dst;
    dst.handle = to;
    dst.w = w;
    dst.h = h;
    uint8_t* d = (uint8_t*)PortBits(dst);
    const uint8_t* s = PortBits(g_ui);
    if (!d || !s) return;
    int dPitch = (w + 3) & ~3, sPitch = (g_ui.w + 3) & ~3;
    bool dUp = g_bottomUp ? g_bottomUp(d) : true, sUp = g_bottomUp ? g_bottomUp(s) : true;
    for (int y = 0; y < h; ++y) {
        int sy = y0 + y;
        if (sy < 0 || sy >= g_ui.h) continue;
        int x = std::max(0, -x0), n = std::min(w, g_ui.w - x0);
        if (n <= x) continue;
        memcpy(d + (size_t)(dUp ? h - 1 - y : y) * dPitch + x, s + (size_t)(sUp ? g_ui.h - 1 - sy : sy) * sPitch + x0 + x,
               n - x);
    }
}

void __fastcall TextPageDraw(char* page) {
    void* background = *(void**)(page + kPageBackground);
    if (g_active && background && g_stalePages.erase(page)) {
        const short* rect = (const short*)(page + kWidgetRect);
        const short* size = (const short*)(page + kPageSize);
        ((PortOp_t)game::LockPort)(background);
        CopyFromInterface(background, size[0], size[1], rect[0], rect[1]);
        ((PortOp_t)game::UnlockPort)(background);
        ((void(__thiscall*)(void*))kTextPageDrawLines)(page);
    }
    ((void(__thiscall*)(void*))kTextPageDraw)(page);
}

bool PatchTextPages() {
    uintptr_t* reflow = (uintptr_t*)kTextPageReflowSlot;
    uintptr_t* draw = (uintptr_t*)kTextPageDrawSlot;
    if (*reflow != kTextPageReflow || *draw != kTextPageDraw) return false;
    DWORD old;
    if (!VirtualProtect((void*)kTextPageDrawSlot, kTextPageReflowSlot + 4 - kTextPageDrawSlot, PAGE_READWRITE, &old))
        return false;
    *reflow = (uintptr_t)&TextPageReflow;
    *draw = (uintptr_t)&TextPageDraw;
    VirtualProtect((void*)kTextPageDrawSlot, kTextPageReflowSlot + 4 - kTextPageDrawSlot, old, &old);
    return true;
}

bool Install(void (*log)(const char*, ...), void (*publish)(const Captured&), bool (*bottomUp)(const void* bits), bool debug) {
    g_log = log;
    g_debug = debug;
    g_publish = publish;
    g_bottomUp = bottomUp;
    // 0x47c950: sub esp, 8; push ebp; push esi   (mission cursor, 1 stack arg)
    static const uint8_t kMissionCursor[] = {0x83, 0xec, 0x08, 0x55, 0x56};
    // 0x495800: mov ax, word ptr [0x5794dc]       (scene/menu cursor, no args)
    static const uint8_t kSceneCursor[] = {0x66, 0xa1, 0xdc, 0x94, 0x57, 0x00};
    // 0x4693a0 (present): sub esp, 0x110. Also closes the phase when a frame
    // is presented directly, as when a mission starts (0x471664).
    static const uint8_t kPresent[] = {0x81, 0xec, 0x10, 0x01, 0x00, 0x00};
    bool ok = PatchInterfaceCall();
    ok = ok && PatchInterfaceStart();
    ok = ok && PatchFrameComplete();
    ok = ok && Wrap(game::Present, kPresent, sizeof kPresent, 0, EndInterface, Nothing);
    ok = ok && Wrap(game::MissionCursorDraw, kMissionCursor, sizeof kMissionCursor, 4, BeginCursor, EndCursor);
    ok = ok && Wrap(game::SceneCursorDraw, kSceneCursor, sizeof kSceneCursor, 0, BeginCursor, EndCursor);
    ok = ok && PatchMenuCursorCall();
    log("layers: %s", ok ? "installed" : "FAILED (unexpected code; interface stays in the frame)");
    if (ok) log("layers: text page backgrounds %s", PatchTextPages() ? "hooked" : "FAILED (unexpected code)");
    return ok;
}

bool Active() { return g_active; }

void SetMenuCursorSink(void (*sink)(const uint8_t* px, int size, int gameX, int gameY)) { g_menuSink = sink; }

void OnFrameStart() {
    // A phase must never outlive its frame (e.g. a mode change would dispose
    // our port as if it were the back buffer): put the real one back.
    g_armed = false;
    if (!g_active) return;
    g_log("layers: interface phase was not closed; restoring the back buffer");
    BackBufferHandle() = g_backBuffer;
    SelectBackBuffer();
    g_active = false;
    g_cursorDepth = 0;
}

uintptr_t InterfacePixelAddress(int x, int y) {
    const uint8_t* bits = PortBits(g_ui);
    if (!bits || x < 0 || y < 0 || x >= g_ui.w || y >= g_ui.h) return 0;
    int pitch = (g_ui.w + 3) & ~3;
    int row = (g_bottomUp ? g_bottomUp(bits) : true) ? g_ui.h - 1 - y : y;
    return (uintptr_t)(bits + (size_t)row * pitch + x);
}

void SetWorldOverlay(void (*draw)()) { g_worldOverlay = draw; }

uintptr_t WorldPixelAddress(int x, int y) {
    const Port& p = g_worldPort;
    const uint8_t* bits = PortBits(p);
    if (!bits || x < 0 || y < 0 || x >= p.w || y >= p.h) return 0;
    int pitch = (p.w + 3) & ~3;
    int row = (g_bottomUp ? g_bottomUp(bits) : true) ? p.h - 1 - y : y;
    return (uintptr_t)(bits + (size_t)row * pitch + x);
}

}  // namespace layers
