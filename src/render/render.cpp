#include "render.h"

#include "../meter.h"
#include "../prof.h"
#include "image.h"

#include <limits.h>
#include <math.h>
#include <string.h>

#include <algorithm>
#include <map>

#include "ddfake.h"

namespace render {
void LoadBackgrounds(const std::vector<std::string>& scenes);
namespace {

void (*g_log)(const char*, ...);
Settings g_settings;
std::string g_fpsText;
// Cutscene movie being shown (copied: the decoder reuses its buffer).
std::vector<uint32_t> g_movieFrame;
int g_movieW, g_movieH;
int g_movieScaling;      // 0 Enhanced, 1 Smooth, 2 Sharp
bool g_movieOriginalSize;
DWORD g_lastPresent;  // GetTickCount of the last PresentFrame

// Widescreen menu background for the current base scene (Menus\<Scene>.png).
std::string g_scene;
bool g_bgDirty;  // reload the background (scene or MenuArt changed)
int g_bgW, g_bgH;
// A scene's widescreen backgrounds: <Scene>.png, plus <Scene>~2.png ... for
// scenes that show different pictures (loading, victory); the one whose
// columns at the frame's edges match the game's frame is shown. Each may have
// a -glow variant for hover highlights that reach the frame edge.
struct Background {
    std::vector<uint32_t> px, glow;
    int w = 0, h = 0;
    std::vector<uint32_t> edgeL, edgeR;  // 480 rows each, BGRA, just inside the frame edges
};
std::vector<Background> g_bgs;
int g_bgShown = -1;  // index uploaded to the presenter
std::vector<std::string> (*g_sceneStack)();  // top first (set by main)
bool g_captureScenes;  // [Debug] CaptureScenes: screenshot every new screen
std::string g_captureKey;
int g_capturePresents;
int g_captureAt = 4;  // [Debug] CaptureScenes=n: the n-th presented frame (default 4)
bool g_active;
bool g_useD3D;
HWND g_hwnd;
Mode g_mode{640, 480, 8};
Frame g_frame;
std::vector<Mode> g_modes;  // advertised: settings + one extra (zoom)
double g_hudScale = 1.0;  // screen px per game px for the interface (missions), at g_hudBaseH
int g_hudBaseH;            // window height g_hudScale is meant for (0 = any)
std::vector<Layer> g_layers;  // last presented; [0] = whole frame
// The interface size is capped so the tallest panel fits the window: the
// Gene Lab+'s Select Components with all 15 components reaches 585 game px
// down from the top of the screen. Every panel keeps the same size relative
// to the others; a short window gets a smaller interface.
constexpr int kTallestPanel = 585;
constexpr int kFitMargin = 8;  // client px left below it
bool g_gdiLog;  // [Debug] GdiLog: log the game's frame-DIB handling

// The part of the current mode's frames that is shown (frame pixels).
struct CropF {
    double x, y, w, h;
};
bool g_cropOn;
CropF g_crop;
int g_cropModeW, g_cropModeH;

CropF ShownPart(int frameW, int frameH) {
    if (g_cropOn && g_cropModeW == frameW && g_cropModeH == frameH) return g_crop;
    return {0, 0, (double)frameW, (double)frameH};
}


// The game's frame DIB (source of the per-frame StretchBlt).
struct {
    const uint8_t* bits;
    int w, h, pitch;
    bool bottomUp;
} g_dib;
HDC g_frameDC;  // memory DC the frame DIB is presented from (the frame port's DC)

// Mission interface/cursor layers handed over by layers.cpp at the end of
// each mission frame, attached to the next presented frame.
struct {
    int w = 0, h = 0;
    std::vector<uint8_t> ui, cursor;
    int cursorX = 0, cursorY = 0;
    bool fresh = false;
} g_pending;

bool Inside(const RECT& r, POINT p) { return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom; }

// ---------------------------------------------------------------------------
// Window placement: the game window always covers its monitor; the game's
// own attempts to size it to the "display mode" are overridden.

RECT MonitorRect(HWND hwnd) {
    MONITORINFO mi{};
    mi.cbSize = sizeof mi;
    HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
    if (GetMonitorInfoA(mon, &mi)) return mi.rcMonitor;
    return {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
}

decltype(&SetWindowPos) o_SetWindowPos;

// True if the game window is already where it belongs: exactly over its
// monitor (or, headless, monitor-sized at the hidden off-screen position).
bool CoversMonitor(HWND hwnd, const RECT& m) {
    RECT w, want = m;
    if (g_settings.headless) want = {-32000, -32000, -32000 + (m.right - m.left), -32000 + (m.bottom - m.top)};
    return GetWindowRect(hwnd, &w) && EqualRect(&w, &want);
}

// Windowed: a normal, movable, resizable window with the requested client
// size, centred on the monitor's work area. Placed when switching (or forced);
// otherwise the player's position and size are kept.
void PlaceWindow(bool force);

void CoverMonitor() {
    if (!g_hwnd) return;
    if (g_settings.windowed && !g_settings.headless) {
        PlaceWindow(false);
        return;
    }
    prof::Scope scope("cover");
    RECT m = MonitorRect(g_hwnd);
    if (g_settings.headless) {  // monitor-sized, but hidden and off-screen
        if (CoversMonitor(g_hwnd, m)) return;
        o_SetWindowPos(g_hwnd, HWND_BOTTOM, -32000, -32000, m.right - m.left, m.bottom - m.top,
                       SWP_NOACTIVATE | SWP_HIDEWINDOW);
        return;
    }
    LONG style = GetWindowLongA(g_hwnd, GWL_STYLE);
    LONG want = (style & ~(WS_CAPTION | WS_THICKFRAME | WS_SYSMENU)) | WS_POPUP;
    // Mode changes happen on every zoom; only touch the window if it needs it
    // (SWP_FRAMECHANGED forces a full frame recalculation and repaint).
    if (want == style && CoversMonitor(g_hwnd, m)) return;
    if (want != style) SetWindowLongA(g_hwnd, GWL_STYLE, want);
    o_SetWindowPos(g_hwnd, HWND_TOP, m.left, m.top, m.right - m.left, m.bottom - m.top,
                   SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

void PlaceWindow(bool force) {
    if (!g_hwnd) return;
    if (!g_settings.windowed || g_settings.headless) {
        CoverMonitor();
        return;
    }
    LONG style = GetWindowLongA(g_hwnd, GWL_STYLE);
    LONG want = (style & ~WS_POPUP) | WS_OVERLAPPEDWINDOW;
    if (want == style && !force) return;
    if (want != style) SetWindowLongA(g_hwnd, GWL_STYLE, want);
    MONITORINFO mi{};
    mi.cbSize = sizeof mi;
    GetMonitorInfoA(MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTOPRIMARY), &mi);
    RECT work = mi.rcWork;
    RECT r{0, 0, std::max(320, g_settings.windowW), std::max(240, g_settings.windowH)};
    AdjustWindowRectEx(&r, want, FALSE, GetWindowLongA(g_hwnd, GWL_EXSTYLE));
    int w = std::min<int>(r.right - r.left, work.right - work.left);
    int h = std::min<int>(r.bottom - r.top, work.bottom - work.top);
    int x = work.left + (work.right - work.left - w) / 2, y = work.top + (work.bottom - work.top - h) / 2;
    o_SetWindowPos(g_hwnd, HWND_NOTOPMOST, x, y, w, h, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    g_log("render: windowed %dx%d", w, h);
}

// Besides DirectDraw, the engine's mode switch (0x4695f0 -> 0x4d8943 ->
// 0x4d71dd) calls ChangeDisplaySettingsA(mode, CDS_FULLSCREEN) itself. Our
// renderer never changes the real display mode, so report success without
// asking the driver (that round-trip cost ~25 ms per zoom resize).
decltype(&ChangeDisplaySettingsA) o_ChangeDisplaySettingsA;
LONG WINAPI h_ChangeDisplaySettingsA(DEVMODEA* dm, DWORD flags) {
    static int logged;
    if (logged < 8) {
        ++logged;
        if (dm)
            g_log("render: ChangeDisplaySettings(%lux%lu %lu bpp, fields %#lx, flags %#lx) ignored", dm->dmPelsWidth,
                  dm->dmPelsHeight, dm->dmBitsPerPel, dm->dmFields, flags);
        else
            g_log("render: ChangeDisplaySettings(restore, flags %#lx) ignored", flags);
    }
    return DISP_CHANGE_SUCCESSFUL;
}

BOOL WINAPI h_SetWindowPos(HWND hwnd, HWND after, int x, int y, int cx, int cy, UINT flags) {
    if (hwnd == g_hwnd && g_hwnd) {
        prof::Scope scope("swp");
        RECT m = MonitorRect(hwnd);
        // The game resizes its window to each new mode; ours always covers the
        // monitor (or is a window the player sized), so keep only z-order and
        // show/hide changes.
        if ((g_settings.windowed && !g_settings.headless) || CoversMonitor(hwnd, m))
            return o_SetWindowPos(hwnd, after, 0, 0, 0, 0, flags | SWP_NOMOVE | SWP_NOSIZE);
        if (g_settings.headless)
            return o_SetWindowPos(hwnd, HWND_BOTTOM, -32000, -32000, m.right - m.left, m.bottom - m.top,
                                  SWP_NOACTIVATE | SWP_HIDEWINDOW);
        return o_SetWindowPos(hwnd, after, m.left, m.top, m.right - m.left, m.bottom - m.top,
                              flags & ~(SWP_NOMOVE | SWP_NOSIZE));
    }
    return o_SetWindowPos(hwnd, after, x, y, cx, cy, flags);
}

// ---------------------------------------------------------------------------
// Coordinate mapping between the real client area and game space.

int ClientW() {
    RECT r;
    return g_hwnd && GetClientRect(g_hwnd, &r) ? r.right : g_mode.w;
}
int ClientH() {
    RECT r;
    return g_hwnd && GetClientRect(g_hwnd, &r) ? r.bottom : g_mode.h;
}

POINT ClientToGame(POINT c) {
    // Interface elements drawn at their own scale take the click first (topmost
    // last). The cursor layer is drawn wherever the mouse is, so it never maps.
    for (size_t i = g_layers.size(); i-- > 1;) {
        const Layer& l = g_layers[i];
        if (l.kind == Layer::Cursor || l.kind == Layer::MenuCursor || !Inside(l.dst, c)) continue;
        int dw = std::max<int>(1, l.dst.right - l.dst.left), dh = std::max<int>(1, l.dst.bottom - l.dst.top);
        return {(LONG)(l.src.left + (int64_t)(c.x - l.dst.left) * (l.src.right - l.src.left) / dw),
                (LONG)(l.src.top + (int64_t)(c.y - l.dst.top) * (l.src.bottom - l.src.top) / dh)};
    }
    return ClientToFrame(c);
}

}  // namespace

POINT ClientToFrame(POINT c) {
    RECT v = Viewport();
    CropF s = ShownPart(g_mode.w, g_mode.h);
    int vw = std::max<int>(1, v.right - v.left), vh = std::max<int>(1, v.bottom - v.top);
    POINT g{(LONG)floor(s.x + (c.x - v.left) * s.w / vw), (LONG)floor(s.y + (c.y - v.top) * s.h / vh)};
    // Showing part of the frame (zoomed in past the smallest frame): the mouse at
    // the window's edge is at the frame's edge, so the game's edge scrolling works.
    if (s.w < g_mode.w) {
        if (c.x <= v.left) g.x = 0;
        if (c.x >= v.right - 1) g.x = g_mode.w - 1;
    }
    if (s.h < g_mode.h) {
        if (c.y <= v.top) g.y = 0;
        if (c.y >= v.bottom - 1) g.y = g_mode.h - 1;
    }
    g.x = std::clamp<LONG>(g.x, 0, g_mode.w - 1);
    g.y = std::clamp<LONG>(g.y, 0, g_mode.h - 1);
    return g;
}

bool CursorFrame(POINT& out) {
    if (!g_hwnd) return false;
    POINT c;
    if (!GetCursorPos(&c)) return false;  // our own import, not the hooked one
    ScreenToClient(g_hwnd, &c);
    RECT cr;
    GetClientRect(g_hwnd, &cr);
    if (!Inside(cr, c)) return false;
    out = ClientToFrame(c);
    return true;
}

namespace {

POINT GameToClient(POINT g) {
    RECT v = Viewport();
    CropF s = ShownPart(g_mode.w, g_mode.h);
    return {(LONG)lround(v.left + (g.x + 0.5 - s.x) * (v.right - v.left) / s.w - 0.5),
            (LONG)lround(v.top + (g.y + 0.5 - s.y) * (v.bottom - v.top) / s.h - 0.5)};
}

// The game believes its window sits at the screen origin and is exactly the
// display mode in size, so "screen" coordinates it sees are game coordinates.
decltype(&GetCursorPos) o_GetCursorPos;
decltype(&SetCursorPos) o_SetCursorPos;
decltype(&ClientToScreen) o_ClientToScreen;
decltype(&GetClientRect) o_GetClientRect;
decltype(&GetSystemMetrics) o_GetSystemMetrics;

// Debug autopilot: a virtual mouse position in client coordinates (the real
// mouse is never read or moved while it is on). It goes through the same
// client-to-game mapping as the real mouse.
bool g_fakeCursor;
POINT g_fakePos;

BOOL WINAPI h_GetCursorPos(LPPOINT p) {
    if (g_fakeCursor && p) {
        *p = ClientToGame(g_fakePos);
        return TRUE;
    }
    if (!o_GetCursorPos(p)) return FALSE;
    if (g_hwnd) {
        ScreenToClient(g_hwnd, p);
        *p = ClientToGame(*p);
    }
    return TRUE;
}

BOOL WINAPI h_SetCursorPos(int x, int y) {
    if (g_fakeCursor) {
        g_fakePos = GameToClient({x, y});
        return TRUE;
    }
    if (!g_hwnd) return o_SetCursorPos(x, y);
    POINT c = GameToClient({x, y});
    o_ClientToScreen(g_hwnd, &c);
    return o_SetCursorPos(c.x, c.y);
}

BOOL WINAPI h_ClientToScreen(HWND hwnd, LPPOINT p) {
    if (hwnd == g_hwnd && g_hwnd) return TRUE;  // client == screen in game space
    return o_ClientToScreen(hwnd, p);
}

BOOL WINAPI h_GetClientRect(HWND hwnd, LPRECT r) {
    if (hwnd == g_hwnd && g_hwnd && r) {
        *r = {0, 0, g_mode.w, g_mode.h};
        return TRUE;
    }
    return o_GetClientRect(hwnd, r);
}

int WINAPI h_GetSystemMetrics(int i) {
    if (i == SM_CXSCREEN) return g_mode.w;
    if (i == SM_CYSCREEN) return g_mode.h;
    return o_GetSystemMetrics(i);
}

// ---------------------------------------------------------------------------
// Frame capture

decltype(&StretchBlt) o_StretchBlt;
decltype(&BitBlt) o_BitBlt;

// Copies (sx,sy,w,h) of an 8-bit DIB selected into `src` to (dx,dy) of the
// frame. Returns false if src is not an 8-bit DIB section.
bool CaptureFrom(HDC src, int dx, int dy, int w, int h, int sx, int sy) {
    if (GetObjectType(src) != OBJ_MEMDC) return false;
    HBITMAP bmp = (HBITMAP)GetCurrentObject(src, OBJ_BITMAP);
    DIBSECTION ds;
    if (GetObjectA(bmp, sizeof ds, &ds) != sizeof ds || ds.dsBm.bmBitsPixel != 8 || !ds.dsBm.bmBits) return false;
    GdiFlush();

    if (g_frame.w != g_mode.w || g_frame.h != g_mode.h) {
        g_frame.w = g_mode.w;
        g_frame.h = g_mode.h;
        g_frame.pixels.assign((size_t)g_frame.w * g_frame.h, 0);
    }
    int bw = ds.dsBm.bmWidth, bh = ds.dsBm.bmHeight;
    int pitch = ds.dsBm.bmWidthBytes;
    bool bottomUp = ds.dsBmih.biHeight > 0;
    auto* bits = (const uint8_t*)ds.dsBm.bmBits;
    if (bw == g_mode.w && bh == g_mode.h) {
        if (g_gdiLog && (g_frameDC != src || g_dib.bits != bits))
            g_log("gdi: frame presented from dc %p bmp %p bits %p (%dx%d)", src, bmp, bits, bw, bh);
        g_dib = {bits, bw, bh, pitch, bottomUp};
        g_frameDC = src;
    }

    // Clip to both bitmaps.
    if (sx < 0) { dx -= sx; w += sx; sx = 0; }
    if (sy < 0) { dy -= sy; h += sy; sy = 0; }
    if (dx < 0) { sx -= dx; w += dx; dx = 0; }
    if (dy < 0) { sy -= dy; h += dy; dy = 0; }
    w = std::min({w, bw - sx, g_frame.w - dx});
    h = std::min({h, bh - sy, g_frame.h - dy});
    for (int y = 0; y < h; ++y) {
        int srow = bottomUp ? bh - 1 - (sy + y) : sy + y;
        memcpy(&g_frame.pixels[(size_t)(dy + y) * g_frame.w + dx], bits + (size_t)srow * pitch + sx, std::max(w, 0));
    }

    if (meter::Enabled()) meter::Frame(g_frame.pixels.data(), g_frame.w, g_frame.h, g_frame.w, false);

    RGBQUAD pal[256];
    UINT n = GetDIBColorTable(src, 0, 256, pal);
    if (n) memcpy(g_frame.palette, pal, n * sizeof(RGBQUAD));
    g_frame.serial++;
    return true;
}

// Attaches the mission layers drawn for this frame (if any) to it.
void CaptureInterface() {
    Frame& f = g_frame;
    f.split = g_pending.fresh && g_pending.w == f.w && g_pending.h == f.h;
    g_pending.fresh = false;
    if (!f.split) return;
    f.hud.swap(g_pending.ui);
    f.cursor.swap(g_pending.cursor);
    f.cursorX = g_pending.cursorX;
    f.cursorY = g_pending.cursorY;
}

// Groups the interface layer into elements: connected runs of 16x16 tiles
// holding any interface pixel (diagonal neighbours and one-tile gaps join).
std::vector<RECT> InterfaceElements(const Frame& f) {
    constexpr int T = 16;
    int gw = (f.w + T - 1) / T, gh = (f.h + T - 1) / T;
    std::vector<uint8_t> occ((size_t)gw * gh, 0);
    for (int y = 0; y < f.h; ++y) {
        const uint8_t* row = &f.hud[(size_t)y * f.w];
        uint8_t* orow = &occ[(size_t)(y / T) * gw];
        for (int tx = 0; tx < gw; ++tx) {
            if (orow[tx]) continue;
            int x0 = tx * T, x1 = std::min(f.w, x0 + T);
            for (int x = x0; x < x1; ++x)
                if (row[x]) {
                    orow[tx] = 1;
                    break;
                }
        }
    }
    std::vector<int> label((size_t)gw * gh, -1);
    std::vector<RECT> out;
    std::vector<int> stack;
    for (int i = 0; i < gw * gh; ++i) {
        if (!occ[i] || label[i] >= 0) continue;
        RECT box{gw, gh, -1, -1};
        int id = (int)out.size();
        stack.assign(1, i);
        label[i] = id;
        while (!stack.empty()) {
            int c = stack.back();
            stack.pop_back();
            int cx = c % gw, cy = c / gw;
            box.left = std::min<LONG>(box.left, cx);
            box.top = std::min<LONG>(box.top, cy);
            box.right = std::max<LONG>(box.right, cx);
            box.bottom = std::max<LONG>(box.bottom, cy);
            for (int dy = -2; dy <= 2; ++dy)
                for (int dx = -2; dx <= 2; ++dx) {
                    int nx = cx + dx, ny = cy + dy;
                    if (nx < 0 || ny < 0 || nx >= gw || ny >= gh) continue;
                    int n = ny * gw + nx;
                    if (occ[n] && label[n] < 0) {
                        label[n] = id;
                        stack.push_back(n);
                    }
                }
        }
        out.push_back({box.left * T, box.top * T, std::min<LONG>(f.w, (box.right + 1) * T),
                       std::min<LONG>(f.h, (box.bottom + 1) * T)});
    }
    return out;
}

// True when the 640x480 menu frame's edge column differs clearly from the
// background's (a hover highlight reaching the edge is lit): enough rows of
// either edge more than 40 levels off.
// Sum of edge differences between the frame and background i (lower = closer).
long EdgeDiff(const Background& bg) {
    long sum = 0;
    for (int side = 0; side < 2; ++side) {
        int x = side ? 638 : 1;
        const std::vector<uint32_t>& ref = side ? bg.edgeR : bg.edgeL;
        for (int y = 0; y < 480; y += 4) {
            const RGBQUAD& c = g_frame.palette[g_frame.pixels[(size_t)y * 640 + x]];
            uint32_t r = ref[y];
            sum += std::max({abs(c.rgbBlue - (int)(r & 0xff)), abs(c.rgbGreen - (int)((r >> 8) & 0xff)),
                             abs(c.rgbRed - (int)((r >> 16) & 0xff))});
        }
    }
    return sum;
}

// Shows the background matching the current frame (if a scene has several).
void PickBackground() {
    if (g_bgs.empty() || !g_useD3D) return;
    int best = 0;
    if (g_bgs.size() > 1 && g_frame.w == 640 && g_frame.h == 480) {
        long bestDiff = LONG_MAX;
        for (size_t i = 0; i < g_bgs.size(); ++i) {
            long d = EdgeDiff(g_bgs[i]);
            if (d < bestDiff) {
                bestDiff = d;
                best = (int)i;
            }
        }
    }
    if (best == g_bgShown) return;
    const Background& bg = g_bgs[best];
    d3d11::SetBackground(bg.px.data(), bg.w, bg.h);
    if (!bg.glow.empty()) d3d11::SetBackgroundGlow(bg.glow.data(), bg.w, bg.h);
    g_bgW = bg.w;
    g_bgH = bg.h;
    g_bgShown = best;
    if (g_bgs.size() > 1) g_log("render: menu background variant %d", best + 1);
}

bool EdgeLit() {
    if (g_frame.w != 640 || g_frame.h != 480 || g_bgShown < 0) return false;
    const Background& bg = g_bgs[g_bgShown];
    if (bg.glow.empty()) return false;
    for (int side = 0; side < 2; ++side) {
        int x = side ? 638 : 1, rows = 0;
        const std::vector<uint32_t>& ref = side ? bg.edgeR : bg.edgeL;
        for (int y = 0; y < 480; y += 2) {
            const RGBQUAD& c = g_frame.palette[g_frame.pixels[(size_t)y * 640 + x]];
            uint32_t r = ref[y];
            int d = std::max({abs(c.rgbBlue - (int)(r & 0xff)), abs(c.rgbGreen - (int)((r >> 8) & 0xff)),
                              abs(c.rgbRed - (int)((r >> 16) & 0xff))});
            if (d > 40) ++rows;
        }
        if (rows > 20) return true;
    }
    return false;
}

// Battlefield layer, then (in missions) each interface element at the fixed
// interface scale, kept against the screen edge or centre it sits nearest,
// then the cursor at the interface scale at the real mouse position.
void BuildLayers() {
    RECT v = Viewport();
    RECT full{0, 0, g_frame.w, g_frame.h};
    g_layers.clear();
    {
        Layer world{full, v, Layer::World};
        CropF s = ShownPart(g_frame.w, g_frame.h);
        world.fractional = true;
        world.fsrc[0] = s.x;
        world.fsrc[1] = s.y;
        world.fsrc[2] = s.x + s.w;
        world.fsrc[3] = s.y + s.h;
        world.src = {(LONG)s.x, (LONG)s.y, (LONG)ceil(s.x + s.w), (LONG)ceil(s.y + s.h)};
        g_layers.push_back(world);
    }
    if (!g_frame.split || g_frame.w <= 0) {
        PickBackground();
        // Menus: the background fills the client height, centred, under the
        // frame (its middle 4:3 is the original art the frame covers).
        int cw = ClientW(), ch = ClientH();
        if (g_useD3D && g_bgW > 0 && g_bgH > 0) {
            int bw = (int)((int64_t)g_bgW * ch / g_bgH);
            int bx = (cw - bw) / 2;  // centred, like the frame
            d3d11::SetBackgroundRect(true, {bx, 0, bx + bw, ch});
            d3d11::SetGlow(EdgeLit());
        } else if (g_useD3D) {
            d3d11::SetBackgroundRect(false, {});
        }
        // The menu cursor at the real mouse position, at the menu's scale.
        int n = g_frame.menuCursorSize;
        POINT m;
        bool have = g_fakeCursor ? (m = g_fakePos, true) : (GetCursorPos(&m) && g_hwnd && ScreenToClient(g_hwnd, &m));
        if (n > 0 && have && g_frame.w > 0) {
            double s = (double)(v.right - v.left) / g_frame.w;
            double x = m.x - n / 2 * s, y = m.y - n / 2 * s;
            g_layers.push_back({{0, 0, n, n}, {(LONG)x, (LONG)y, (LONG)(x + n * s), (LONG)(y + n * s)}, Layer::MenuCursor});
        }
        return;
    }
    if (g_useD3D) d3d11::SetBackgroundRect(false, {});
    int vw = v.right - v.left;
    double world = (double)vw / g_frame.w;  // screen px per frame px
    int vh = v.bottom - v.top;
    double want = g_hudBaseH > 0 ? g_hudScale * ClientH() / g_hudBaseH : g_hudScale;
    double s = std::min(want, (double)(vh - kFitMargin) / kTallestPanel);
    static double logged;
    if (s < want && fabs(s - logged) > 0.01) {
        logged = s;
        g_log("render: interface %.3f (capped from %.3f so the Gene Lab+ panel fits %d px)", s, want, vh);
    }
    (void)world;
    for (const RECT& r : InterfaceElements(g_frame)) {
        // Keep the element's distance from its nearest screen edge (or the
        // centre) in interface pixels, so it sits in the same place at every zoom.
        // Anything still taller than the window (none known) is drawn smaller to fit.
        double full = (r.bottom - r.top) * s;
        double es = full > vh ? s * vh / full : s;
        double w = (r.right - r.left) * es, hgt = (r.bottom - r.top) * es;
        double cx = (r.left + r.right) * 0.5, cy = (r.top + r.bottom) * 0.5;
        double x, y;
        // The anchor is the edge the element is nearest, or the centre when it has
        // about as much room on both sides (dialogs). Not the third its centre is
        // in: an animated element (the Gene Lab's spinning preview) changes its
        // box every frame, and a centre near a third's line flipped it between
        // anchors, so the panel jumped.
        auto anchor = [](double before, double after, double frame) {
            if (fabs(before - after) < frame * 0.08) return 1;
            return before < after ? 0 : 2;
        };
        int ax = anchor(r.left, g_frame.w - r.right, g_frame.w), ay = anchor(r.top, g_frame.h - r.bottom, g_frame.h);
        if (ax == 1)
            x = v.left + vw * 0.5 + (cx - g_frame.w * 0.5) * es - w * 0.5;
        else if (ax == 0)
            x = v.left + r.left * es;
        else
            x = v.right - (g_frame.w - r.right) * es - w;
        if (ay == 1)
            y = v.top + vh * 0.5 + (cy - g_frame.h * 0.5) * es - hgt * 0.5;
        else if (ay == 0)
            y = v.top + r.top * es;
        else
            y = v.bottom - (g_frame.h - r.bottom) * es - hgt;
        // Kept inside the window where it fits (an element anchored near an edge
        // may otherwise hang off the other one).
        if (w <= vw) x = std::clamp(x, (double)v.left, (double)v.right - w);
        if (hgt <= vh) y = std::clamp(y, (double)v.top, (double)v.bottom - hgt);
        g_layers.push_back({r, {(LONG)x, (LONG)y, (LONG)(x + w), (LONG)(y + hgt)}, Layer::InterfaceScaled});
    }
    // Cursor: a box around the game cursor position, drawn so that position
    // lands on the real mouse position.
    POINT m;
    if (g_fakeCursor) {
        m = g_fakePos;
    } else if (GetCursorPos(&m) && g_hwnd) {
        ScreenToClient(g_hwnd, &m);
    } else {
        return;
    }
    {
        constexpr int R = 48;
        RECT c{std::max(0, g_frame.cursorX - R), std::max(0, g_frame.cursorY - R),
               std::min(g_frame.w, g_frame.cursorX + R), std::min(g_frame.h, g_frame.cursorY + R)};
        if (c.right > c.left && c.bottom > c.top) {
            double x = m.x + (c.left - g_frame.cursorX) * s, y = m.y + (c.top - g_frame.cursorY) * s;
            g_layers.push_back({c,
                                {(LONG)x, (LONG)y, (LONG)(x + (c.right - c.left) * s),
                                 (LONG)(y + (c.bottom - c.top) * s)},
                                Layer::Cursor});
        }
    }
}

// Game frames composed per second (distinct frames reaching the window), for
// the FPS counter and the log.
void CountFrame() {
    static DWORD start, lastLog;
    static uint32_t startSerial;
    DWORD now = GetTickCount();
    if (!start) {
        start = now;
        startSerial = g_frame.serial;
        return;
    }
    if (now - start < 500) return;
    double fps = (g_frame.serial - startSerial) * 1000.0 / (now - start);
    start = now;
    startSerial = g_frame.serial;
    char text[32];
    snprintf(text, sizeof text, "%.0f FPS", fps);
    g_fpsText = text;
    if (g_settings.showFps && g_useD3D) d3d11::SetOverlayText(text);
    if (now - lastLog >= 10000) {
        lastLog = now;
        g_log("fps: %.1f game frames/s at %dx%d", fps, g_frame.w, g_frame.h);
    }
}

void PresentFrame() {
    if (!g_hwnd) return;
    if (g_sceneStack && !g_frame.split) {
        std::vector<std::string> stack = g_sceneStack();
        LoadBackgrounds(stack);
        if (g_captureScenes && !stack.empty() && g_frame.w == 640) {
            std::string key = stack[0];
            if (key != g_captureKey) {
                g_captureKey = key;
                g_capturePresents = 0;
            }
            if (++g_capturePresents == g_captureAt && g_useD3D) {  // the screen has been drawn by now
                static int n;
                std::string name = key.size() > 6 ? key.substr(4, key.size() - 6) : key;
                char exe[MAX_PATH];
                GetModuleFileNameA(nullptr, exe, sizeof exe);
                std::string dir(exe);
                dir = dir.substr(0, dir.find_last_of("\\/") + 1);
                d3d11::Screenshot(dir + "capture-" + std::to_string(++n) + "-" + name + "-screen.bmp");
                g_log("render: captured %s", name.c_str());
            }
        }
    }
    if (!g_movieFrame.empty()) {  // a cutscene is on screen
        ShowMovieFrame(std::vector<uint32_t>(g_movieFrame).data(), g_movieW, g_movieH);
        return;
    }
    g_lastPresent = GetTickCount();
    CountFrame();
    {
        prof::Scope scope("layout");
        BuildLayers();
    }
    RECT v = g_layers[0].dst;
    if (g_useD3D) {
        prof::Scope scope("gpu");
        d3d11::Present(g_frame, g_layers, ClientW(), ClientH());
        return;
    }
    // GDI fallback: palette-convert and stretch with halftone filtering.
    HDC dc = GetDC(g_hwnd);
    struct {
        BITMAPINFOHEADER h;
        RGBQUAD pal[256];
    } bi{};
    bi.h = {sizeof(BITMAPINFOHEADER), g_frame.w, -g_frame.h, 1, 8, BI_RGB, 0, 0, 0, 256, 256};
    memcpy(bi.pal, g_frame.palette, sizeof bi.pal);
    SetStretchBltMode(dc, HALFTONE);
    SetBrushOrgEx(dc, 0, 0, nullptr);
    // The GDI path draws the layers merged in place (no interface scaling).
    const uint8_t* px = g_frame.pixels.data();
    std::vector<uint8_t> merged;
    if (g_frame.split) {
        merged = g_frame.pixels;
        for (size_t i = 0; i < merged.size(); ++i) {
            if (g_frame.hud[i]) merged[i] = g_frame.hud[i];
            if (g_frame.cursor[i]) merged[i] = g_frame.cursor[i];
        }
        px = merged.data();
    }
    // Top-down DIB (negative height): source origin is the top-left corner.
    const RECT& ws = g_layers[0].src;
    StretchDIBits(dc, v.left, v.top, v.right - v.left, v.bottom - v.top, ws.left, ws.top, ws.right - ws.left,
                  ws.bottom - ws.top, px,
                  (BITMAPINFO*)&bi, DIB_RGB_COLORS, SRCCOPY);
    // Black bars around the viewport.
    int cw = ClientW(), ch = ClientH();
    HBRUSH black = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RECT bars[4] = {{0, 0, cw, v.top}, {0, v.bottom, cw, ch}, {0, v.top, v.left, v.bottom}, {v.right, v.top, cw, v.bottom}};
    for (RECT& b : bars)
        if (b.right > b.left && b.bottom > b.top) FillRect(dc, &b, black);
    if (g_settings.showFps && !g_fpsText.empty()) {
        SetBkColor(dc, RGB(0, 0, 0));
        SetTextColor(dc, RGB(255, 255, 255));
        TextOutA(dc, 8, 8, g_fpsText.c_str(), (int)g_fpsText.size());
    }
    ReleaseDC(g_hwnd, dc);
}

bool IsGameWindowDC(HDC dc) { return g_hwnd && WindowFromDC(dc) == g_hwnd; }

// The game recreates its frame DIB after each mode change. It makes other
// 8-bit DIBs too, so a new DIB only counts as the frame once it is selected
// into the memory DC the frame is presented from (remembered at capture).
struct DibInfo {
    const uint8_t* bits;
    int w, h;
    bool bottomUp;
};
std::map<HBITMAP, DibInfo> g_dibs;  // the game's 8-bit DIB sections

decltype(&CreateDIBSection) o_CreateDIBSection;
decltype(&SelectObject) o_SelectObject;
decltype(&DeleteObject) o_DeleteObject;

HBITMAP WINAPI h_CreateDIBSection(HDC dc, const BITMAPINFO* bi, UINT usage, void** bits, HANDLE sec, DWORD off) {
    double t0 = prof::Enabled() ? prof::Now() : 0;
    HBITMAP bmp = o_CreateDIBSection(dc, bi, usage, bits, sec, off);
    if (t0) prof::Add("dib-create", prof::Now() - t0);
    if (bmp && bits && *bits && bi && bi->bmiHeader.biBitCount == 8) {
        int w = bi->bmiHeader.biWidth, h = bi->bmiHeader.biHeight;
        g_dibs[bmp] = {(const uint8_t*)*bits, w, abs(h), h > 0};
        if (g_gdiLog) g_log("gdi: CreateDIBSection %dx%d 8bpp -> bmp %p bits %p (mode %dx%d)", w, h, bmp, *bits,
                            g_mode.w, g_mode.h);
    }
    return bmp;
}

HGDIOBJ WINAPI h_SelectObject(HDC dc, HGDIOBJ obj) {
    HGDIOBJ prev = o_SelectObject(dc, obj);
    if (g_gdiLog && g_dibs.count((HBITMAP)obj)) g_log("gdi: SelectObject(dc %p, bmp %p)%s", dc, obj, dc == g_frameDC ? " [frame dc]" : "");
    if (dc && dc == g_frameDC) {
        auto it = g_dibs.find((HBITMAP)obj);
        if (it != g_dibs.end() && it->second.w == g_mode.w && it->second.h == g_mode.h) {
            const DibInfo& d = it->second;
            g_dib = {d.bits, d.w, d.h, (d.w + 3) & ~3, d.bottomUp};
        }
    }
    return prev;
}

BOOL WINAPI h_DeleteObject(HGDIOBJ obj) {
    auto it = g_dibs.find((HBITMAP)obj);
    if (it != g_dibs.end()) {
        if (g_dib.bits == it->second.bits) g_dib.bits = nullptr;
        g_dibs.erase(it);
    }
    prof::Scope scope("delete-obj");
    return o_DeleteObject(obj);
}

BOOL WINAPI h_StretchBlt(HDC d, int x, int y, int w, int h, HDC s, int sx, int sy, int sw, int sh, DWORD rop) {
    double t0 = prof::Enabled() ? prof::Now() : 0;
    if (IsGameWindowDC(d) && w == sw && h == sh && rop == SRCCOPY && CaptureFrom(s, x, y, w, h, sx, sy)) {
        CaptureInterface();
        if (t0) prof::Add("capture", prof::Now() - t0);
        PresentFrame();
        return TRUE;
    }
    if (IsGameWindowDC(d)) g_log("render: unhandled StretchBlt to window %dx%d<-%dx%d rop %#lx", w, h, sw, sh, rop);
    return o_StretchBlt(d, x, y, w, h, s, sx, sy, sw, sh, rop);
}

BOOL WINAPI h_BitBlt(HDC d, int x, int y, int w, int h, HDC s, int sx, int sy, DWORD rop) {
    if (IsGameWindowDC(d) && rop == SRCCOPY && CaptureFrom(s, x, y, w, h, sx, sy)) {
        CaptureInterface();
        PresentFrame();
        return TRUE;
    }
    if (IsGameWindowDC(d)) g_log("render: unhandled BitBlt to window %dx%d rop %#lx", w, h, rop);
    return o_BitBlt(d, x, y, w, h, s, sx, sy, rop);
}

}  // namespace

// ---------------------------------------------------------------------------

bool Active() { return g_active; }
const std::vector<Mode>& AdvertisedModes() { return g_modes; }

void SetWorldCrop(bool on, double x, double y, double w, double h) {
    g_cropOn = on && w > 0 && h > 0;
    g_crop = {x, y, w, h};
    g_cropModeW = g_mode.w;
    g_cropModeH = g_mode.h;
}

bool CursorClientFraction(double& fx, double& fy) {
    if (!g_hwnd) return false;
    POINT c = g_fakePos;  // the autopilot's virtual mouse, or the real one
    if (!g_fakeCursor) {
        if (!GetCursorPos(&c)) return false;  // our own import, not the hooked one
        ScreenToClient(g_hwnd, &c);
    }
    int cw = ClientW(), ch = ClientH();
    if (c.x < 0 || c.y < 0 || c.x >= cw || c.y >= ch || cw <= 0 || ch <= 0) return false;
    fx = (c.x + 0.5) / cw;
    fy = (c.y + 0.5) / ch;
    return true;
}

void AllowMode(int w, int h) {
    g_modes = g_settings.modes;
    for (const Mode& m : g_modes)
        if (m.w == w && m.h == h) return;
    g_modes.push_back({w, h, 8});
}

void SetHudScale(double scale, int baseHeight) {
    g_hudScale = scale;
    g_hudBaseH = baseHeight;
}

void SetWindowed(bool on, int clientW, int clientH) {
    bool change = on != g_settings.windowed || clientW != g_settings.windowW || clientH != g_settings.windowH;
    g_settings.windowed = on;
    g_settings.windowW = clientW;
    g_settings.windowH = clientH;
    if (change && g_hwnd) PlaceWindow(true);
}

void SetShowFps(bool on) {
    g_settings.showFps = on;
    if (g_useD3D) d3d11::SetOverlayText(on ? g_fpsText.c_str() : "");
}
bool ShowFps() { return g_settings.showFps; }

void SetFilter(int filter) {
    g_settings.filter = filter;
    if (g_useD3D) d3d11::SetFilter(filter);
}
void SetMovieOptions(int scaling, bool originalSize) {
    g_movieScaling = scaling;
    g_movieOriginalSize = originalSize;
    if (g_useD3D) d3d11::SetMovieScaling(scaling);
}

void SetMenuArt(bool widescreen) {
    if (g_settings.menuArt == widescreen) return;
    g_settings.menuArt = widescreen;
    g_bgDirty = true;
}

void SetVSync(bool on) {
    g_settings.vsync = on;
    if (g_useD3D) d3d11::SetVSync(on);
}

void SetFakeCursor(bool on, POINT game) {
    g_fakeCursor = on;
    g_fakePos = GameToClient(game);
}

POINT GamePointToClient(POINT game) { return GameToClient(game); }
POINT InterfacePointToClient(POINT game) {
    // The inverse of ClientToGame: the topmost interface element showing this frame pixel.
    for (size_t i = g_layers.size(); i-- > 1;) {
        const Layer& l = g_layers[i];
        if (l.kind == Layer::Cursor || l.kind == Layer::MenuCursor || !Inside(l.src, game)) continue;
        int sw = std::max<int>(1, l.src.right - l.src.left), sh = std::max<int>(1, l.src.bottom - l.src.top);
        return {(LONG)(l.dst.left + ((int64_t)(game.x - l.src.left) * 2 + 1) * (l.dst.right - l.dst.left) / (2 * sw)),
                (LONG)(l.dst.top + ((int64_t)(game.y - l.src.top) * 2 + 1) * (l.dst.bottom - l.dst.top) / (2 * sh))};
    }
    return GameToClient(game);
}
void SetFakeCursorClient(POINT client) {
    g_fakeCursor = true;
    g_fakePos = client;
}
HWND GameWindow() { return g_hwnd; }

void SetSceneStackProvider(std::vector<std::string> (*fn)(), int captureScenes) {
    g_sceneStack = fn;
    g_captureScenes = captureScenes > 0;
    if (captureScenes > 1) g_captureAt = captureScenes;
}

void ShowMovieFrame(const uint32_t* bgra, int w, int h) {
    if (!bgra) {
        g_movieFrame.clear();
        g_movieW = g_movieH = 0;
        return;
    }
    g_movieFrame.assign(bgra, bgra + (size_t)w * h);
    g_movieW = w;
    g_movieH = h;
    if (!g_hwnd) return;
    int cw = ClientW(), ch = ClientH();
    // Fit the screen, keeping the movie's shape (the wide cutscenes fill the
    // width), or as large as it was in the original 640x480 screen.
    double s = std::min((double)cw / w, (double)ch / h);
    if (g_movieOriginalSize) s = std::min(s, ch / 480.0);
    int dw = (int)(w * s + 0.5), dh = (int)(h * s + 0.5);
    RECT dst{(cw - dw) / 2, (ch - dh) / 2, (cw - dw) / 2 + dw, (ch - dh) / 2 + dh};
    static int logged;
    if (logged++ < 2) g_log("render: movie %dx%d -> %ld,%ld-%ld,%ld (client %dx%d)", w, h, dst.left, dst.top, dst.right, dst.bottom, cw, ch);
    if (g_useD3D) {
        d3d11::PresentMovie(g_movieFrame.data(), w, h, dst, cw, ch);
        return;
    }
    HDC dc = GetDC(g_hwnd);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    PatBlt(dc, 0, 0, cw, ch, BLACKNESS);
    SetStretchBltMode(dc, HALFTONE);
    StretchDIBits(dc, dst.left, dst.top, dw, dh, 0, 0, w, h, g_movieFrame.data(), &bi, DIB_RGB_COLORS, SRCCOPY);
    ReleaseDC(g_hwnd, dc);
}

void SetMenuCursor(const uint8_t* px, int size, int, int) {
    g_frame.menuCursor.assign(px, px + (size_t)size * size);
    g_frame.menuCursorSize = size;
    g_frame.menuCursorSerial++;
}

// Loads the backgrounds for the scene stack (top first) when it or MenuArt changes.
void LoadBackgrounds(const std::vector<std::string>& scenes) {
    std::string key;
    for (const std::string& n : scenes) key += n + "|";
    if (key != g_scene) {
        g_scene = key;
        // A new screen: forget the old cursor image (a scene that draws its
        // own cursor in the frame must not get a stale one on top).
        g_frame.menuCursor.clear();
        g_frame.menuCursorSize = 0;
        g_bgDirty = true;
    }
    if (!g_bgDirty) return;
    g_bgDirty = false;
    g_bgW = g_bgH = 0;
    g_bgs.clear();
    g_bgShown = -1;
    if (g_useD3D) d3d11::SetBackground(nullptr, 0, 0);
    if (!g_settings.menuArt) return;  // original art: no background
    // The top scene's images, else the next one down (dialogs over the main
    // menu keep its background): .?AVSHeadquarters@@ -> Menus\SHeadquarters.png.
    char exe[MAX_PATH];
    GetModuleFileNameA(nullptr, exe, sizeof exe);
    std::string dir(exe);
    dir = dir.substr(0, dir.find_last_of("\\/") + 1) + "Menus\\";
    // Ultrawide screens prefer <Scene>-21x9, others <Scene>; either falls back
    // to the other (a wider image is just cropped on a narrower screen).
    bool wide = ClientH() > 0 && ClientW() * 100 / ClientH() > 200;
    const char* sizes[2] = {wide ? "-21x9" : "", wide ? "" : "-21x9"};
    for (const std::string& scene : scenes) {
        std::string base = scene;
        if (base.size() > 6 && base.compare(0, 4, ".?AV") == 0) base = base.substr(4, base.size() - 6);
        if (base.empty()) continue;
        for (const char* size : sizes) {
            for (const char* ext : {".png", ".jpg", ".bmp"}) {
                for (int k = 1; k <= 8; ++k) {
                    std::string name = base + (k > 1 ? "~" + std::to_string(k) : std::string()) + size;
                    Background bg;
                    if (!LoadImageBGRA(dir + name + ext, bg.w, bg.h, bg.px)) {
                        if (k == 1) break;
                        continue;
                    }
                    int w = bg.w, h = bg.h;
                    double fw = h * 4.0 / 3, fx = (w - fw) / 2;
                    bg.edgeL.assign(480, 0);
                    bg.edgeR.assign(480, 0);
                    for (int y = 0; y < 480; ++y) {
                        int sy = std::min(h - 1, (int)((y + 0.5) * h / 480));
                        bg.edgeL[y] = bg.px[(size_t)sy * w + std::clamp((int)(fx + 1.5 * fw / 640), 0, w - 1)];
                        bg.edgeR[y] = bg.px[(size_t)sy * w + std::clamp((int)(fx + fw - 1.5 * fw / 640), 0, w - 1)];
                    }
                    int gw, gh;
                    if (!LoadImageBGRA(dir + name + "-glow" + ext, gw, gh, bg.glow) || gw != w || gh != h)
                        bg.glow.clear();
                    g_log("render: menu background %s%s (%dx%d)%s", name.c_str(), ext, w, h,
                          bg.glow.empty() ? "" : " + glow");
                    g_bgs.push_back(std::move(bg));
                }
                if (!g_bgs.empty()) return;
            }
        }
    }
}

void Tick(const std::vector<std::string>& scenes) {
    LoadBackgrounds(scenes);
    // Menus only present when something changes; keep the cursor moving.
    if (g_hwnd && !g_frame.split && g_frame.menuCursorSize > 0 && GetTickCount() - g_lastPresent > 20) PresentFrame();
}

void SetInterfaceLayers(const uint8_t* ui, const uint8_t* cursor, int w, int h, int cursorX, int cursorY) {
    size_t n = (size_t)w * h;
    g_pending.ui.assign(ui, ui + n);
    g_pending.cursor.assign(cursor, cursor + n);
    g_pending.w = w;
    g_pending.h = h;
    g_pending.cursorX = cursorX;
    g_pending.cursorY = cursorY;
    g_pending.fresh = true;
}

bool DibIsBottomUp(const void* bits, bool fallback) {
    for (auto& [bmp, d] : g_dibs)
        if (d.bits == bits) return d.bottomUp;
    return fallback;
}

const uint8_t* FrameBits() { return g_dib.bits; }
HDC FrameDC() { return g_frameDC; }
uint8_t* FramePixel(int x, int y) {
    if (!g_dib.bits || x < 0 || y < 0 || x >= g_dib.w || y >= g_dib.h) return nullptr;
    int row = g_dib.bottomUp ? g_dib.h - 1 - y : y;
    return (uint8_t*)g_dib.bits + (size_t)row * g_dib.pitch + x;
}
POINT GameCursor() {
    POINT c{0, 0};
    GetCursorPos(&c);
    if (g_hwnd) ScreenToClient(g_hwnd, &c);
    return ClientToFrame(c);
}

Mode CurrentMode() { return g_mode; }
const Frame& CurrentFrame() { return g_frame; }

RECT Viewport() {
    int cw = ClientW(), ch = ClientH();
    CropF s = ShownPart(g_mode.w, g_mode.h);
    // Fit the shown part inside the client area, keeping its aspect ratio.
    int64_t vw = cw, vh = (int64_t)(cw * s.h / s.w);
    if (vh > ch) {
        vh = ch;
        vw = (int64_t)(ch * s.w / s.h);
    }
    // Original menu art: 4:3 screens at the left edge (black beside them).
    int x = g_settings.menuArt ? (int)((cw - vw) / 2) : 0, y = (int)((ch - vh) / 2);
    return {x, y, x + (int)vw, y + (int)vh};
}

void OnSetCooperativeLevel(HWND hwnd, DWORD flags) {
    g_log("render: SetCooperativeLevel(%p, %#lx)", hwnd, flags);
    if (!hwnd) return;
    bool first = g_hwnd != hwnd;
    g_hwnd = hwnd;
    CoverMonitor();
    if (first && g_settings.d3d11) {
        g_useD3D = d3d11::Init(hwnd, g_settings, g_log);
        if (g_useD3D) d3d11::SetMovieScaling(g_movieScaling);
        g_log("render: presenter = %s", g_useD3D ? "Direct3D 11" : "GDI");
    }
}

void OnSetDisplayMode(int w, int h, int bpp) {
    prof::Scope scope("setmode");
    g_log("render: SetDisplayMode(%d, %d, %d)", w, h, bpp);
    g_mode = {w, h, bpp};
    g_dib.bits = nullptr;  // the game recreates its frame DIB after a mode change
    CoverMonitor();
}

void FilterMessage(UINT msg, WPARAM, LPARAM& lp) {
    if (!g_active || !g_hwnd) return;
    bool clientCoords = (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST && msg != WM_MOUSEWHEEL && msg != WM_MOUSEHWHEEL);
    if (!clientCoords) return;
    POINT c{(short)LOWORD(lp), (short)HIWORD(lp)};
    POINT g = ClientToGame(c);
    if (g_settings.clickLog && (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN)) {
        // Which layer mapped the click (0 = battlefield), to diagnose misplaced clicks.
        size_t hit = 0;
        for (size_t i = g_layers.size(); i-- > 1;)
            if (g_layers[i].kind != Layer::Cursor && Inside(g_layers[i].dst, c)) {
                hit = i;
                break;
            }
        POINT real;
        o_GetCursorPos(&real);
        ScreenToClient(g_hwnd, &real);
        g_log("click %s client %ld,%ld (mouse now %ld,%ld) -> game %ld,%ld via layer %zu of %zu, frame %dx%d mode %dx%d, "
              "game cursor %d,%d",
              msg == WM_LBUTTONDOWN ? "L" : "R", c.x, c.y, real.x, real.y, g.x, g.y, hit, g_layers.size(), g_frame.w,
              g_frame.h, g_mode.w, g_mode.h, g_frame.cursorX, g_frame.cursorY);
        for (size_t i = 1; i < g_layers.size(); ++i) {
            const Layer& l = g_layers[i];
            g_log("  layer %zu kind %d src %ld,%ld-%ld,%ld dst %ld,%ld-%ld,%ld", i, (int)l.kind, l.src.left, l.src.top,
                  l.src.right, l.src.bottom, l.dst.left, l.dst.top, l.dst.right, l.dst.bottom);
        }
    }
    lp = MAKELPARAM(g.x, g.y);
}

bool Install(HMODULE self, const Settings& s, void (*log)(const char*, ...)) {
    g_log = log;
    g_settings = s;
    g_modes = s.modes;
    g_gdiLog = s.gdiLog;
    memset(g_frame.palette, 0, sizeof g_frame.palette);

    // Render at true pixel size even with Windows display scaling.
    if (HMODULE u = GetModuleHandleA("user32.dll")) {
        typedef BOOL(WINAPI * SetCtx)(HANDLE);
        if (auto f = (SetCtx)GetProcAddress(u, "SetProcessDpiAwarenessContext")) f((HANDLE)-4);  // PER_MONITOR_AWARE_V2
    }

    bool ok = InstallFakeDirectDraw(self, log);
    // USER32 / GDI32 import slots in WARBREED.EXE.
    ok &= HookImport(0x5959fc, &h_SetWindowPos, &o_SetWindowPos);
    ok &= HookImport(0x59599c, &h_ChangeDisplaySettingsA, &o_ChangeDisplaySettingsA);
    ok &= HookImport(0x595a00, &h_GetCursorPos, &o_GetCursorPos);
    ok &= HookImport(0x5959c0, &h_SetCursorPos, &o_SetCursorPos);
    ok &= HookImport(0x59596c, &h_ClientToScreen, &o_ClientToScreen);
    ok &= HookImport(0x595944, &h_GetClientRect, &o_GetClientRect);
    ok &= HookImport(0x595a04, &h_GetSystemMetrics, &o_GetSystemMetrics);
    ok &= HookImport(0x5956b0, &h_StretchBlt, &o_StretchBlt);
    ok &= HookImport(0x59566c, &h_BitBlt, &o_BitBlt);
    ok &= HookImport(0x5956bc, &h_CreateDIBSection, &o_CreateDIBSection);
    ok &= HookImport(0x5956f8, &h_SelectObject, &o_SelectObject);
    ok &= HookImport(0x59562c, &h_DeleteObject, &o_DeleteObject);
    g_active = ok;
    log("render: hooks %s", ok ? "installed" : "FAILED");
    return ok;
}

}  // namespace render
