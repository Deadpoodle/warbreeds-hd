// Diagnostic: learn how the engine's current drawing port is laid out and
// what it redraws each frame. Logs for the first few frames in which a
// widget draws ([Debug] PortProbe=1).
#include "portprobe.h"

#include <windows.h>

#include "game.h"

namespace portprobe {
namespace {

void (*g_log)(const char*, ...);
const uint8_t* (*g_frameBits)();
HDC (*g_frameDC)();
int g_framesLogged;
bool g_thisFrame;
bool g_inWidget;

decltype(&TextOutA) o_TextOutA;
decltype(&DrawTextA) o_DrawTextA;

BOOL WINAPI h_TextOutA(HDC dc, int x, int y, LPCSTR s, int n) {
    if (g_thisFrame) g_log("probe:   TextOutA dc %p (%s) at %d,%d '%.*s'", dc, g_inWidget ? "in widget" : "outside", x, y, n, s);
    return o_TextOutA(dc, x, y, s, n);
}
int WINAPI h_DrawTextA(HDC dc, LPCSTR s, int n, LPRECT r, UINT f) {
    if (g_thisFrame) g_log("probe:   DrawTextA dc %p (%s) '%.*s'", dc, g_inWidget ? "in widget" : "outside", n < 0 ? 40 : n, s);
    return o_DrawTextA(dc, s, n, r, f);
}

template <typename T>
void Hook(uintptr_t slot, T fn, T* orig) {
    DWORD old;
    VirtualProtect((void*)slot, 4, PAGE_READWRITE, &old);
    *orig = *(T*)slot;
    *(T*)slot = fn;
    VirtualProtect((void*)slot, 4, old, &old);
}

}  // namespace

void Install(void (*log)(const char*, ...), const uint8_t* (*frameBits)(), HDC (*frameDC)()) {
    g_log = log;
    g_frameBits = frameBits;
    g_frameDC = frameDC;
    Hook(0x595670, &h_TextOutA, &o_TextOutA);
    Hook(0x5959b8, &h_DrawTextA, &o_DrawTextA);
}

void OnWidgetDraw(void* widget, bool first) {
    g_inWidget = true;
    if (!first || g_framesLogged >= 4) return;
    const uint8_t* bits = g_frameBits();
    if (!bits) return;
    g_thisFrame = true;
    ++g_framesLogged;
    uintptr_t port = game::at<uintptr_t>(0x5928f0);
    g_log("probe: frame %d: first widget %p, current port %p, frame bits %p, frame dc %p", g_framesLogged, widget,
          (void*)port, bits, g_frameDC());
    if (port && !IsBadReadPtr((void*)port, 0x200)) {
        const uintptr_t* p = (const uintptr_t*)port;
        for (int i = 0; i < 0x80; ++i) {
            if (p[i] == (uintptr_t)bits) g_log("probe:   port+%03x = frame bits", i * 4);
            if (p[i] == (uintptr_t)g_frameDC()) g_log("probe:   port+%03x = frame dc", i * 4);
            // one level down
            if (p[i] > 0x10000 && !IsBadReadPtr((void*)p[i], 0x80)) {
                const uintptr_t* q = (const uintptr_t*)p[i];
                for (int j = 0; j < 0x20; ++j)
                    if (q[j] == (uintptr_t)bits) g_log("probe:   [port+%03x]+%03x = frame bits", i * 4, j * 4);
            }
        }
    }
    HDC dc = g_frameDC();
    if (dc) {
        HRGN rgn = CreateRectRgn(0, 0, 0, 0);
        int r = GetClipRgn(dc, rgn);
        if (r == 1) {
            RECT box;
            GetRgnBox(rgn, &box);
            DWORD size = GetRegionData(rgn, 0, nullptr);
            g_log("probe:   clip region on frame dc: box (%ld,%ld)-(%ld,%ld), %lu bytes", box.left, box.top, box.right,
                  box.bottom, size);
        } else {
            g_log("probe:   no clip region on frame dc (%d)", r);
        }
        DeleteObject(rgn);
    }
}

void OnWidgetDrawDone() { g_inWidget = false; }

void OnPresent() { g_thisFrame = false; }

}  // namespace portprobe
