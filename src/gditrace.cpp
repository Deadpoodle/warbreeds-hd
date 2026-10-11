// Debug tracer for the GDI calls WARBREED.EXE uses to draw. Hooks the exe's
// import table entries and logs an aggregated summary every few seconds.
#include "gditrace.h"

#include "meter.h"

#include <windows.h>
#include <stdio.h>

#include <map>
#include <string>
#include <tuple>

namespace gditrace {
namespace {

void (*g_log)(const char*, ...);
HWND (*g_window)();

struct Key {
    std::string fn;
    uintptr_t caller;
    std::string dst, src;
    int w, h;
    bool operator<(const Key& o) const {
        return std::tie(fn, caller, dst, src, w, h) < std::tie(o.fn, o.caller, o.dst, o.src, o.w, o.h);
    }
};
std::map<Key, int> g_counts;
DWORD g_lastDump;
CRITICAL_SECTION g_cs;

std::string DescribeDC(HDC dc) {
    if (!dc) return "null";
    DWORD type = GetObjectType(dc);
    char buf[128];
    if (type == OBJ_MEMDC) {
        HBITMAP bmp = (HBITMAP)GetCurrentObject(dc, OBJ_BITMAP);
        DIBSECTION ds{};
        int n = GetObjectA(bmp, sizeof ds, &ds);
        if (n == sizeof ds)
            snprintf(buf, sizeof buf, "memdc dib %ldx%ld %dbpp", ds.dsBm.bmWidth, ds.dsBm.bmHeight, ds.dsBm.bmBitsPixel);
        else
            snprintf(buf, sizeof buf, "memdc ddb %ldx%ld", ds.dsBm.bmWidth, ds.dsBm.bmHeight);
        return buf;
    }
    HWND w = WindowFromDC(dc);
    if (w && w == g_window()) return "GAME WINDOW";
    snprintf(buf, sizeof buf, "dc type %lu hwnd %p", type, (void*)w);
    return buf;
}

void Count(const char* fn, void* caller, HDC dst, HDC src, int w, int h) {
    EnterCriticalSection(&g_cs);
    g_counts[{fn, (uintptr_t)caller, DescribeDC(dst), src ? DescribeDC(src) : "-", w, h}]++;
    DWORD now = GetTickCount();
    if (now - g_lastDump > 3000) {
        g_lastDump = now;
        g_log("--- GDI trace (last ~3s) ---");
        for (auto& [k, n] : g_counts)
            g_log("%6d x %-14s from %08x  %dx%d  dst=[%s] src=[%s]", n, k.fn.c_str(), (unsigned)k.caller, k.w, k.h,
                  k.dst.c_str(), k.src.c_str());
        g_counts.clear();
    }
    LeaveCriticalSection(&g_cs);
}

decltype(&BitBlt) o_BitBlt;
decltype(&StretchBlt) o_StretchBlt;
decltype(&StretchDIBits) o_StretchDIBits;
decltype(&ScrollDC) o_ScrollDC;
decltype(&PatBlt) o_PatBlt;

BOOL WINAPI h_BitBlt(HDC d, int x, int y, int w, int h, HDC s, int sx, int sy, DWORD rop) {
    Count("BitBlt", __builtin_return_address(0), d, s, w, h);
    return o_BitBlt(d, x, y, w, h, s, sx, sy, rop);
}
BOOL WINAPI h_StretchBlt(HDC d, int x, int y, int w, int h, HDC s, int sx, int sy, int sw, int sh, DWORD rop) {
    Count("StretchBlt", __builtin_return_address(0), d, s, w, h);
    if (meter::Enabled() && g_window && WindowFromDC(d) == g_window()) {
        DIBSECTION ds;
        HGDIOBJ bmp = GetCurrentObject(s, OBJ_BITMAP);
        if (bmp && GetObject(bmp, sizeof ds, &ds) == sizeof ds && ds.dsBm.bmBitsPixel == 8)
            meter::Frame((const uint8_t*)ds.dsBm.bmBits, ds.dsBm.bmWidth, ds.dsBm.bmHeight, ds.dsBm.bmWidthBytes,
                         ds.dsBmih.biHeight > 0);
    }
    return o_StretchBlt(d, x, y, w, h, s, sx, sy, sw, sh, rop);
}
int WINAPI h_StretchDIBits(HDC d, int x, int y, int w, int h, int sx, int sy, int sw, int sh, const void* bits,
                           const BITMAPINFO* bi, UINT usage, DWORD rop) {
    Count("StretchDIBits", __builtin_return_address(0), d, nullptr, w, h);
    return o_StretchDIBits(d, x, y, w, h, sx, sy, sw, sh, bits, bi, usage, rop);
}
BOOL WINAPI h_ScrollDC(HDC d, int dx, int dy, const RECT* scroll, const RECT* clip, HRGN rgn, LPRECT upd) {
    Count("ScrollDC", __builtin_return_address(0), d, nullptr, dx, dy);
    return o_ScrollDC(d, dx, dy, scroll, clip, rgn, upd);
}
BOOL WINAPI h_PatBlt(HDC d, int x, int y, int w, int h, DWORD rop) {
    Count("PatBlt", __builtin_return_address(0), d, nullptr, w, h);
    return o_PatBlt(d, x, y, w, h, rop);
}

template <typename T>
bool HookIat(uintptr_t slot, T hook, T* orig) {
    *orig = *(T*)slot;
    DWORD old;
    if (!VirtualProtect((void*)slot, 4, PAGE_READWRITE, &old)) return false;
    *(T*)slot = hook;
    VirtualProtect((void*)slot, 4, old, &old);
    return true;
}

}  // namespace

void Install(void (*log)(const char*, ...), HWND (*window)()) {
    g_log = log;
    g_window = window;
    InitializeCriticalSection(&g_cs);
    // IAT slots in WARBREED.EXE (see tools/peinfo.py output).
    HookIat(0x59566c, &h_BitBlt, &o_BitBlt);
    HookIat(0x5956b0, &h_StretchBlt, &o_StretchBlt);
    HookIat(0x59570c, &h_StretchDIBits, &o_StretchDIBits);
    HookIat(0x595964, &h_ScrollDC, &o_ScrollDC);
    HookIat(0x595724, &h_PatBlt, &o_PatBlt);
    log("GDI trace installed (BitBlt IAT was %p)", (void*)o_BitBlt);
}

}  // namespace gditrace
