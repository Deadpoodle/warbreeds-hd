// WarBreeds HD: widescreen resolutions and camera zoom for WarBreeds (1998).
#include <windows.h>

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <string>
#include <vector>

#include "game.h"
#include "camera.h"
#include "diag.h"
#include "gditrace.h"
#include "render/render.h"
#include "scan.h"
#include "menu.h"
#include "movie/qt.h"
#include "meter.h"
#include "prof.h"
#include "zoom.h"
#include "watchdog.h"
#include "layers.h"
#include "autopilot.h"
#include "headless.h"
#include "fonts.h"
#include "controls.h"
#include "editor/editor.h"
#include "skirmish.h"
#include "skirmish_teams.h"
#include "campaign.h"

#include "version.h"

namespace {

// Multiplayer start-up (UComms 0x4c7a30): CoInitialize, then DirectPlay
// through the game's one CoCreateInstance call (0x4c7a77). GOG's WARBREED.DLL
// patches that call to its own loader, which takes DllGetClassObject from
// LoadLibraryW("dplayx"); that name resolves to GOG's DirectPlay stub in the
// game folder, which has no DllGetClassObject, so it always failed with
// REGDB_E_CLASSNOTREG ("An error occurred while attempting to open multiplayer
// communications"). The call goes back to the game's import (COM, which loads
// the system's DirectPlay when the Windows feature is on), with GOG's loader
// kept as the fallback.
decltype(&CoInitialize) o_CoInitialize;
decltype(&CoCreateInstance) o_CoCreateInstance;
decltype(&CoCreateInstance) g_gogCreate;  // GOG's loader, if it had patched the call
void Log(const char* fmt, ...);
constexpr uintptr_t kCommsCoInitReturn = 0x4c7a43, kCommsCreateOperand = 0x4c7a79, kCreateSlot = 0x595b30;
const GUID kClsidDirectPlay = {0xd1eb6d20, 0x8923, 0x11d0, {0x9d, 0x97, 0x00, 0xa0, 0xc9, 0x0a, 0x43, 0xcb}};

HRESULT WINAPI h_CoInitialize(LPVOID reserved) {
    HRESULT hr = o_CoInitialize(reserved);
    if (__builtin_return_address(0) == (void*)kCommsCoInitReturn) {
        uintptr_t& operand = *(uintptr_t*)kCommsCreateOperand;
        if (operand != kCreateSlot) {
            g_gogCreate = *(decltype(&CoCreateInstance)*)operand;
            DWORD old;
            if (VirtualProtect(&operand, 4, PAGE_EXECUTE_READWRITE, &old)) {
                operand = kCreateSlot;
                VirtualProtect(&operand, 4, old, &old);
                FlushInstructionCache(GetCurrentProcess(), &operand, 4);
            }
        }
        Log("multiplayer: CoInitialize -> %#lx%s", (unsigned long)hr,
            hr == S_OK ? "" : " (the game expects S_OK: something left COM initialised on this thread)");
    }
    return hr;
}

// [Debug] CommsLog=1: the multiplayer engine's own debug text (0x4c60f0,
// printf-style, most of it filtered out before OutputDebugString) to our log.
void __cdecl h_CommsDebug(const char* fmt, ...) {
    if (!fmt) return;
    char text[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof text, fmt, ap);
    va_end(ap);
    size_t n = strlen(text);
    while (n && (text[n - 1] == 10 || text[n - 1] == 13)) text[--n] = 0;
    Log("comms: %s", text);
}

HRESULT WINAPI h_CoCreateInstance(REFCLSID clsid, LPUNKNOWN outer, DWORD ctx, REFIID iid, LPVOID* out) {
    HRESULT hr = o_CoCreateInstance(clsid, outer, ctx, iid, out);
    if (IsEqualGUID(clsid, kClsidDirectPlay)) {
        Log("multiplayer: DirectPlay through COM -> %#lx", (unsigned long)hr);
        if (FAILED(hr) && g_gogCreate) {
            hr = g_gogCreate(clsid, outer, ctx, iid, out);
            Log("multiplayer: DirectPlay through GOG's loader -> %#lx", (unsigned long)hr);
        }
    }
    return hr;
}


std::string g_dir;  // game folder, with trailing backslash
FILE* g_log;

void Log(const char* fmt, ...) {
    if (!g_log) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(g_log, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

// ---------------------------------------------------------------------------
// Config

struct Res {
    int w, h;
    bool operator==(const Res& o) const { return w == o.w && h == o.h; }
};

enum class ZoomMode { Smooth, Native, Resolution };

struct Config {
    bool enabled = true;
    Res base{0, 0};
    std::vector<double> levels;
    double defaultLevel = 4.0 / 3.0;
    ZoomMode zoomMode = ZoomMode::Smooth;
    bool nativeZoom = false;  // Mode=native: wheel drives the game's own 2x zoom (HUD fixed)
    zoom::Settings smooth;
    double hudScale = 4.0 / 3.0;
    bool mouseWheel = true;
    bool keys = true;
    bool altF4 = true;
    bool captureMouse = false;  // [Input] CaptureMouse: keep the pointer inside the game window
    bool manageDDrawCompat = true;
    bool cameraScan = false;
    std::string renderer = "d3d11";  // d3d11 | gdi | off (off = GOG's DDrawCompat draws)
    int filter = 2;
    bool vsync = true;
    camera::Settings cam;
} g_cfg;

const char kDefaultLevels[] = "2.0, 1.6, 1.333, 1.143, 1.0";

bool g_smoothZoom;  // Mode=smooth with the renderer active


std::vector<Res> g_zoom;  // sorted by width ascending: index 0 = most zoomed in
int g_zoomIndex;

std::string IniPath() { return g_dir + "WarBreedsHD.ini"; }

std::string IniGet(const char* sec, const char* key, const char* def) {
    char buf[512];
    GetPrivateProfileStringA(sec, key, def, buf, sizeof buf, IniPath().c_str());
    return buf;
}

int IniInt(const char* sec, const char* key, int def) {
    return GetPrivateProfileIntA(sec, key, def, IniPath().c_str());
}

void WriteDefaultIni() {
    if (GetFileAttributesA(IniPath().c_str()) != INVALID_FILE_ATTRIBUTES) return;
    FILE* f = fopen(IniPath().c_str(), "w");
    if (!f) return;
    fputs(
        "; WarBreeds HD settings\n"
        "\n"
        "[General]\n"
        "; 0 disables every patch (the game runs as originally released).\n"
        "Enabled=1\n"
        "\n"
        "[Display]\n"
        "; The resolution the game runs at, scaled to fill your screen: auto = your desktop\n"
        "; resolution, or e.g. 1920x1080 (everything looks as it would on a 1080p screen;\n"
        "; a different shape from your monitor's gets black bars). Takes effect from the next mission.\n"
        "Resolution=auto\n"
        "; fullscreen (borderless, the default) or windowed (a normal window; Alt+Enter toggles).\n"
        "; The window starts at the Resolution above (Auto: 3/4 of the desktop) and can be resized.\n"
        "DisplayMode=fullscreen\n"
        "; Menu screens (4:3) on a wider screen:\n"
        ";   widescreen - centred, with widescreen background art from Menus\\<Scene>.png\n"
        ";                (Menus\\<Scene>-21x9.png on ultrawide screens) filling the sides\n"
        ";   original   - the original art only: menus at the left edge, black beside them\n"
        "MenuArt=widescreen\n"
        "; Text font: original (the game's own) or orbitron (Orbitron, a modern sci-fi font\n"
        "; from Fonts\\Orbitron[wght].ttf, SIL Open Font License). Changes live.\n"
        "Font=original\n"
        "\n"
        "[Zoom]\n"
        "; Zoom is a magnification: screen pixels per game pixel. 1.0 shows the most detail\n"
        "; per pixel; 2.0 doubles every pixel; 0.5 shows four times the area.\n"
        ";\n"
        "; smooth     - (needs Renderer=d3d11 or gdi) mouse wheel zooms smoothly between Min and\n"
        ";              Max, centred on the cursor; the interface stays a fixed size (HUD Scale).\n"
        "; native     - the mouse wheel uses the game's own zoom-out (the Z key): half-size\n"
        ";              battlefield, interface unchanged. Two levels.\n"
        "; resolution - the mouse wheel steps the game's resolution through Levels; the\n"
        ";              interface scales too.\n"
        "Mode=smooth\n"
        "; Magnification when a mission starts.\n"
        "Default=1.333\n"
        "; smooth: furthest out / in, and the change per wheel notch.\n"
        "Min=0.5\n"
        "Max=2.0\n"
        "Step=1.15\n"
        "; smooth: largest size the game renders at (megapixels) before it switches to its own\n"
        "; half-scale view for zooming further out. Lower this if zooming far out is slow.\n"
        "MaxRenderMegapixels=8.3\n"
        "; resolution: magnification steps.\n"
        "Levels=2.0, 1.6, 1.333, 1.143, 1.0\n"
        "; Zoom with the mouse wheel / numpad + and -.\n"
        "MouseWheel=1\n"
        "Keys=1\n"
        "\n"
        "[HUD]\n"
        "; smooth zoom: size of the in-mission interface (panels, menus, cursor) in screen\n"
        "; pixels per game pixel; it stays this size at every zoom. auto = the Max zoom above.\n"
        "Scale=auto\n"
        "\n"
        "[Input]\n"
        "; Alt+F4 quits the game.\n"
        "AltF4=1\n"
        "; Builders (Shamans) clicked onto your own unfinished or damaged building go and\n"
        "; finish or repair it (the game's Repair order). 0 = the click selects the building.\n"
        "BuilderRepair=1\n"
        "; With a Shaman's build list open (B), a letter starts placing a building:\n"
        ";   R Refinery, G Gene Lab, L Relay, P Psi Tower, M Mine, T Turret, E Electric,\n"
        ";   N Listener, H Healer, W Warp, S Summoner, C Collector;\n"
        ";   Shift+G/L/T/P the upgraded Gene Lab+/Relay+/Turret+/Psi Tower+.\n"
        "; With the pod list open (P), 1-3 pick a pod. Unavailable letters keep their usual meaning.\n"
        "QuickBuild=1\n"
        "; Pods can be planted on fogged ground: the Shaman walks there and plants (buildings\n"
        "; already could). In build/plant mode the cursor over fog shows that it will walk.\n"
        "BuildInFog=1\n"
        "; With several builders (and nothing else) selected, the build/plant/repair/upgrade\n"
        "; buttons and B/P/R/U still work (the original needs exactly one). A placed building\n"
        "; is started by the nearest of them; the others walk over and help build it as soon\n"
        "; as it stands. A pod is planted by the nearest; Repair sends them all. 0 = original.\n"
        "GroupBuild=1\n"
        "; Mouse in missions:\n"
        ";   classic - the original: left-click selects and orders, right-click deselects\n"
        ";   modern  - left-click selects, right-click moves / attacks / acts (and cancels a\n"
        ";             build or repair mode); the minimap's right-click still moves there\n"
        "MouseScheme=classic\n"
        "\n"
        "[Camera]\n"
        "; Arrow keys scroll the camera smoothly (instead of the original big jumps).\n"
        "SmoothKeys=1\n"
        "; Also scroll with W/A/S/D. Off by default: the game may use these letters as hotkeys.\n"
        "WASD=0\n"
        "; Keyboard scroll speed in screen widths per second.\n"
        "KeySpeed=1.0\n"
        "\n"
        "[Compat]\n"
        "; Run the game on a single CPU core. Its 1998 thread code can deadlock on\n"
        "; multi-core CPUs (GOG's DDrawCompat does the same). Leave on.\n"
        "SingleCPU=1\n"
        "; Which core: -1 = the first one no other running copy of the game uses\n"
        "; (two copies on one PC, e.g. to try multiplayer); 0, 1, 2... = that core.\n"
        "CPU=-1\n"
        "\n"
        "[Render]\n"
        "; d3d11 - WarBreeds HD draws the game itself (recommended; no flicker on zoom)\n"
        "; gdi   - same, drawn with plain GDI (slower; use if d3d11 has problems)\n"
        "; off   - leave drawing to GOG's ddraw.dll (DDrawCompat) as before\n"
        "Renderer=d3d11\n"
        "; Scaling filter: sharp (crisp pixels, smooth edges), bilinear (soft), nearest (blocky)\n"
        "Filter=sharp\n"
        "VSync=1\n"
        "; Show the frame rate in the top-left corner (F2 toggles it in game)\n"
        "ShowFPS=0\n"
        "; Redraw the whole battlefield every frame. The game only redraws what it\n"
        "; marked as changed, and fast flying units left trails. 0 = the original way.\n"
        "FullRedraw=1\n"
        "\n"
        "[DDrawCompat]\n"
        "; Only used with Renderer=off.\n"
        "; Let WarBreeds HD write DDrawCompat.ini so DDrawCompat (GOG's ddraw.dll) offers\n"
        "; the zoom resolutions. Your own DDrawCompat settings belong in\n"
        "; DDrawCompat-WARBREED.ini, which takes priority.\n"
        "Manage=1\n",
        f);
    fclose(f);
}

bool InMission();  // defined below
const char* SceneName(void* scene);

// RTTI names of the scene stack, top first.
std::vector<std::string> SceneStack() {
    std::vector<std::string> stack;
    for (short i = game::at<short>(game::SceneTop); i >= 0 && i < 16; --i)
        stack.push_back(SceneName((&game::at<void*>(game::Scenes))[i]));
    return stack;
}

Res DesktopSize() {
    DEVMODEA dm{};
    dm.dmSize = sizeof dm;
    if (EnumDisplaySettingsA(nullptr, ENUM_CURRENT_SETTINGS, &dm)) return {(int)dm.dmPelsWidth, (int)dm.dmPelsHeight};
    return {1920, 1080};
}

// [Display] Resolution=auto|WxH (else the older BaseWidth/BaseHeight); {0,0} = desktop.
Res ConfiguredBase() {
    std::string r = IniGet("Display", "Resolution", "");
    int w = 0, h = 0;
    if (!r.empty() && _stricmp(r.c_str(), "auto") != 0 && sscanf(r.c_str(), "%dx%d", &w, &h) == 2 && w >= 640 &&
        h >= 480)
        return {w, h};
    if (r.empty()) return {IniInt("Display", "BaseWidth", 0), IniInt("Display", "BaseHeight", 0)};
    return {0, 0};
}

// The interface is sized in screen pixels; at a lower game resolution it grows
// with everything else, as it would on a real screen of that size.
double EffectiveHudScale() { return g_cfg.hudScale * DesktopSize().h / std::max(1, g_cfg.base.h); }

bool Windowed() { return _stricmp(IniGet("Display", "DisplayMode", "fullscreen").c_str(), "windowed") == 0; }

// Windowed client size: the chosen Resolution, or 3/4 of the desktop for Auto.
Res WindowSize() {
    Res r = ConfiguredBase();
    if (r.w > 0 && r.h > 0) return r;
    Res d = DesktopSize();
    return {(d.w * 3 / 4) & ~1, (d.h * 3 / 4) & ~1};
}

// Settings the in-game Settings page can change while the game runs: re-read
// them from the ini and apply them.
void ApplyMovieOptions() {
    std::string ms = IniGet("Render", "MovieScaling", "enhanced");
    render::SetMovieOptions(_stricmp(ms.c_str(), "smooth") == 0 ? 1 : _stricmp(ms.c_str(), "sharp") == 0 ? 2 : 0,
                            _stricmp(IniGet("Render", "MovieSize", "full").c_str(), "original") == 0);
}

void ApplyControlOptions() {
    controls::Options o;
    o.builderRepair = IniInt("Input", "BuilderRepair", 1) != 0;
    o.quickBuild = IniInt("Input", "QuickBuild", 1) != 0;
    o.buildInFog = IniInt("Input", "BuildInFog", 1) != 0;
    o.modernMouse = _stricmp(IniGet("Input", "MouseScheme", "classic").c_str(), "modern") == 0;
    o.groupBuild = IniInt("Input", "GroupBuild", 1) != 0;
    controls::SetOptions(o);
}

void ApplyFontOption() {
    fonts::SetModern(_stricmp(IniGet("Display", "Font", "original").c_str(), "orbitron") == 0);
}

void ApplyLiveSettings() {
    g_cfg.smooth.minMag = std::clamp(atof(IniGet("Zoom", "Min", "0.5").c_str()), 0.25, 4.0);
    g_cfg.smooth.maxMag = std::clamp(atof(IniGet("Zoom", "Max", "2.0").c_str()), g_cfg.smooth.minMag, 8.0);
    std::string hs = IniGet("HUD", "Scale", "auto");
    g_cfg.hudScale = _stricmp(hs.c_str(), "auto") == 0 ? g_cfg.smooth.maxMag : std::max(0.5, atof(hs.c_str()));
    g_cfg.altF4 = IniInt("Input", "AltF4", 1) != 0;
    g_cfg.captureMouse = IniInt("Input", "CaptureMouse", 0) != 0;
    std::string filter = IniGet("Render", "Filter", "sharp");
    g_cfg.filter = _stricmp(filter.c_str(), "nearest") == 0 ? 0 : _stricmp(filter.c_str(), "bilinear") == 0 ? 1 : 2;
    g_cfg.vsync = IniInt("Render", "VSync", 1) != 0;
    g_cfg.cam.enabled = IniInt("Camera", "SmoothKeys", 1) != 0;
    g_cfg.cam.wasd = IniInt("Camera", "WASD", 0) != 0;
    g_cfg.cam.speed = atof(IniGet("Camera", "KeySpeed", "1.0").c_str());
    if (g_cfg.cam.speed <= 0) g_cfg.cam.speed = 1.0;
    camera::Configure(g_cfg.cam);
    Res base = ConfiguredBase();
    if (base.w <= 0 || base.h <= 0) base = DesktopSize();
    bool newBase = !(base == g_cfg.base);
    if (newBase && !InMission()) {  // from the main menu: next missions run at the new resolution
        g_cfg.base = base;
        Log("resolution: %dx%d", base.w, base.h);
        if (g_smoothZoom) zoom::Init(g_cfg.smooth, base.w, base.h, Log);
    }
    if (g_smoothZoom) {
        zoom::SetRange(g_cfg.smooth.minMag, g_cfg.smooth.maxMag);
        render::SetHudScale(g_cfg.hudScale, g_cfg.base.h);
    }
    if (render::Active()) {
        Res w = WindowSize();
        render::SetWindowed(Windowed(), w.w, w.h);
    }
    if (render::Active()) {
        render::SetFilter(g_cfg.filter);
        render::SetVSync(g_cfg.vsync);
        render::SetShowFps(IniInt("Render", "ShowFPS", 0) != 0);
        render::SetMenuArt(_stricmp(IniGet("Display", "MenuArt", "widescreen").c_str(), "original") != 0);
    }
    ApplyMovieOptions();
    ApplyFontOption();
    ApplyControlOptions();
}

void LoadConfig() {
    WriteDefaultIni();
    g_cfg.enabled = IniInt("General", "Enabled", 1) != 0;
    g_cfg.base = ConfiguredBase();
    std::string levels = IniGet("Zoom", "Levels", kDefaultLevels);
    g_cfg.levels.clear();
    for (const char* p = levels.c_str(); *p;) {
        char* end;
        double v = strtod(p, &end);
        if (end == p) { ++p; continue; }
        if (v >= 0.25 && v <= 8.0) g_cfg.levels.push_back(v);
        p = end;
    }
    if (g_cfg.levels.empty()) g_cfg.levels.push_back(1.0);
    g_cfg.defaultLevel = atof(IniGet("Zoom", "Default", "1.333").c_str());
    std::string mode = IniGet("Zoom", "Mode", "smooth");
    g_cfg.zoomMode = _stricmp(mode.c_str(), "resolution") == 0 ? ZoomMode::Resolution
                     : _stricmp(mode.c_str(), "native") == 0   ? ZoomMode::Native
                                                               : ZoomMode::Smooth;
    g_cfg.nativeZoom = g_cfg.zoomMode == ZoomMode::Native;
    g_cfg.smooth.defaultMag = g_cfg.defaultLevel > 0 ? g_cfg.defaultLevel : 4.0 / 3.0;
    g_cfg.smooth.minMag = std::clamp(atof(IniGet("Zoom", "Min", "0.5").c_str()), 0.25, 4.0);
    g_cfg.smooth.maxMag = std::clamp(atof(IniGet("Zoom", "Max", "2.0").c_str()), g_cfg.smooth.minMag, 8.0);
    g_cfg.smooth.step = std::clamp(atof(IniGet("Zoom", "Step", "1.15").c_str()), 1.01, 2.0);
    g_cfg.smooth.maxPixels = (int)(std::clamp(atof(IniGet("Zoom", "MaxRenderMegapixels", "8.3").c_str()), 0.5, 40.0) * 1e6);
    std::string hs = IniGet("HUD", "Scale", "auto");
    // auto: the interface at its full zoom-in size (as at the Max zoom).
    g_cfg.hudScale = _stricmp(hs.c_str(), "auto") == 0 ? g_cfg.smooth.maxMag : std::max(0.5, atof(hs.c_str()));
    g_cfg.mouseWheel = IniInt("Zoom", "MouseWheel", 1) != 0;
    g_cfg.keys = IniInt("Zoom", "Keys", 1) != 0;
    g_cfg.altF4 = IniInt("Input", "AltF4", 1) != 0;
    g_cfg.captureMouse = IniInt("Input", "CaptureMouse", 0) != 0;
    g_cfg.manageDDrawCompat = IniInt("DDrawCompat", "Manage", 1) != 0;
    g_cfg.cameraScan = IniInt("Debug", "MemoryScan", 0) != 0;
    g_cfg.renderer = IniGet("Render", "Renderer", "d3d11");
    for (char& c : g_cfg.renderer) c = (char)tolower((unsigned char)c);
    std::string filter = IniGet("Render", "Filter", "sharp");
    g_cfg.filter = _stricmp(filter.c_str(), "nearest") == 0 ? 0 : _stricmp(filter.c_str(), "bilinear") == 0 ? 1 : 2;
    g_cfg.vsync = IniInt("Render", "VSync", 1) != 0;
    g_cfg.cam.enabled = IniInt("Camera", "SmoothKeys", 1) != 0;
    g_cfg.cam.wasd = IniInt("Camera", "WASD", 0) != 0;
    g_cfg.cam.speed = atof(IniGet("Camera", "KeySpeed", "1.0").c_str());
    if (g_cfg.cam.speed <= 0) g_cfg.cam.speed = 1.0;

    if (g_cfg.base.w <= 0 || g_cfg.base.h <= 0) g_cfg.base = DesktopSize();

    // Zoom levels -> resolutions (even sizes, at least 640x480).
    double bestDiff = 1e9;
    std::vector<std::pair<Res, double>> tmp;
    for (double m : g_cfg.levels) {
        Res r{(int)lround(g_cfg.base.w / m) & ~1, (int)lround(g_cfg.base.h / m) & ~1};
        r.w = std::max(r.w, 640);
        r.h = std::max(r.h, 480);
        tmp.push_back({r, m});
    }
    std::sort(tmp.begin(), tmp.end(), [](auto& a, auto& b) { return a.first.w < b.first.w; });
    g_zoom.clear();
    for (auto& [r, m] : tmp) {
        if (!g_zoom.empty() && g_zoom.back() == r) continue;
        g_zoom.push_back(r);
        double d = fabs(m - g_cfg.defaultLevel);
        if (d < bestDiff) { bestDiff = d; g_zoomIndex = (int)g_zoom.size() - 1; }
    }
    Log("base %dx%d, %zu zoom levels, default index %d", g_cfg.base.w, g_cfg.base.h, g_zoom.size(), g_zoomIndex);
    for (size_t i = 0; i < g_zoom.size(); ++i) Log("  zoom[%zu] = %dx%d", i, g_zoom[i].w, g_zoom[i].h);
}

// DDrawCompat only lets the game set display modes it advertises, and the game
// refuses modes DirectDraw doesn't list, so every zoom resolution must be in
// SupportedResolutions. We own the directory-level DDrawCompat.ini (marked by
// its first line); DDrawCompat-WARBREED.ini stays the user's and overrides it.
const char kManagedMarker[] = "; Managed by WarBreeds HD";

void WriteDDrawCompatIni() {
    if (!g_cfg.manageDDrawCompat) return;
    std::string path = g_dir + "DDrawCompat.ini";
    if (FILE* f = fopen(path.c_str(), "r")) {
        char first[128] = {};
        fgets(first, sizeof first, f);
        fclose(f);
        if (strncmp(first, kManagedMarker, sizeof kManagedMarker - 1) != 0) {
            Log("DDrawCompat.ini exists and is not ours; leaving it alone");
            return;
        }
    }
    std::string modes = "native, 640x480, 800x600, 1024x768";
    for (auto& r : g_zoom) modes += ", " + std::to_string(r.w) + "x" + std::to_string(r.h);
    FILE* f = fopen(path.c_str(), "w");
    if (!f) { Log("cannot write %s", path.c_str()); return; }
    fprintf(f,
            "%s (regenerated at every launch; set [DDrawCompat] Manage=0 in WarBreedsHD.ini to stop).\n"
            "; Put your own DDrawCompat settings in DDrawCompat-WARBREED.ini instead.\n"
            "SupportedResolutions = %s\n",
            kManagedMarker, modes.c_str());
    fclose(f);
    Log("wrote DDrawCompat.ini: SupportedResolutions = %s", modes.c_str());
}

// ---------------------------------------------------------------------------
// Code patching

bool WriteCode(uintptr_t addr, const void* bytes, size_t n) {
    DWORD old;
    if (!VirtualProtect((void*)addr, n, PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy((void*)addr, bytes, n);
    VirtualProtect((void*)addr, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void*)addr, n);
    return true;
}

void WriteRel32(uint8_t* at, uint8_t op, uintptr_t target) {
    at[0] = op;
    int32_t rel = (int32_t)(target - ((uintptr_t)at + 5));
    memcpy(at + 1, &rel, 4);
}

bool HookJmp(uintptr_t addr, void* fn) {
    uint8_t jmp[5];
    int32_t rel = (int32_t)((uintptr_t)fn - (addr + 5));
    jmp[0] = 0xe9;
    memcpy(jmp + 1, &rel, 4);
    return WriteCode(addr, jmp, 5);
}

// MSVC RTTI: vtable[-1] -> CompleteObjectLocator, +0xc -> TypeDescriptor, +8 -> name.
const char* RttiName(uintptr_t vtbl) {
    if (IsBadReadPtr((void*)(vtbl - 4), 4)) return "(?)";
    uintptr_t col = *(uintptr_t*)(vtbl - 4);
    if (IsBadReadPtr((void*)(col + 0xc), 4)) return "(?)";
    uintptr_t td = *(uintptr_t*)(col + 0xc);
    if (IsBadReadPtr((void*)(td + 8), 16)) return "(?)";
    return (const char*)(td + 8);
}

const char* SceneName(void* scene) {
    if (!scene || IsBadReadPtr(scene, 4)) return "(none)";
    return RttiName(*(uintptr_t*)scene);
}

void* BaseScene() {
    return game::at<short>(game::SceneTop) < 0 ? nullptr : game::at<void*>(game::Scenes);
}

bool IsSGame(void* scene) { return scene && *(uintptr_t*)scene == game::VtblSGame; }

// ---------------------------------------------------------------------------
// SetResolution hook

typedef short(__cdecl* SetResolution_t)(int w, int h, int depth, int flag);
SetResolution_t g_origSetResolution;  // trampoline
bool g_inSetResolution;

short __cdecl HookSetResolution(int w, int h, int depth, int flag) {
    prof::Scope scope("setres");
    prof::SetPhase("setres");
    struct PhaseReset {  // the redraw that follows is "post-setres" until the frame ends
        ~PhaseReset() { prof::SetPhase("post-setres"); }
    } phaseReset;
    void* caller = __builtin_return_address(0);
    short rw = (short)w, rh = (short)h;
    void* base = BaseScene();
    bool mission = IsSGame(base);
    if (mission && g_smoothZoom) {
        int zw, zh;
        zoom::MissionResolution(zw, zh);
        rw = (short)zw;
        rh = (short)zh;
    } else if (mission && !g_zoom.empty()) {
        const Res& z = g_zoom[g_zoomIndex];
        rw = (short)z.w;
        rh = (short)z.h;
    }
    if (!g_smoothZoom || !mission || (short)w != rw || (short)h != rh)  // smooth zoom resizes every frame
    Log("SetResolution(%d, %d, %d, %d) from %p, scenes=%d base=%s -> %dx%d", (short)w, (short)h, depth,
        (short)flag, caller, game::at<short>(game::SceneTop), SceneName(base), rw, rh);
    g_inSetResolution = true;
    short ok = g_origSetResolution((w & ~0xffff) | (uint16_t)rw, (h & ~0xffff) | (uint16_t)rh, depth, flag);
    g_inSetResolution = false;
    if (!g_smoothZoom || !mission || (short)w != rw || (short)h != rh)
        Log("  -> %d, screen now %dx%d", ok, game::at<short>(game::ScreenRight), game::at<short>(game::ScreenBottom));
    return ok;
}

bool InstallSetResolutionHook() {
    if (memcmp((void*)game::SetResolution, game::SetResolutionPrologue, sizeof game::SetResolutionPrologue) != 0)
        return false;
    // Trampoline: the original 7 bytes (call fixed up) then jmp back.
    auto* t = (uint8_t*)VirtualAlloc(nullptr, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!t) return false;
    t[0] = 0x51;  // push ecx
    t[1] = 0x57;  // push edi
    WriteRel32(t + 2, 0xe8, game::SetResolutionPrologueCall);
    WriteRel32(t + 7, 0xe9, game::SetResolution + 7);
    g_origSetResolution = (SetResolution_t)t;
    return HookJmp(game::SetResolution, (void*)&HookSetResolution);
}

// ---------------------------------------------------------------------------
// Zoom input (window subclass)

WNDPROC g_origWndProc;
HWND g_hwnd;
bool g_debugHotkeys;
bool g_saveSceneFrames;
bool g_cursorTrace;
bool g_autopilot;
int g_zoomTarget = -1;  // requested zoom index, applied on the next frame tick
int g_wheelAccum;

int g_nativeTarget = -1;  // Mode=native: 0 = normal, 1 = zoomed out

bool InMission() { return IsSGame(BaseScene()) && game::at<short>(game::SceneTop) == 0; }

// atCursor: the wheel zooms on the map point under the cursor, the keys on
// the screen centre.
void RequestZoomStep(int step, bool atCursor = true) {
    // Only zoom in a mission with no menu on top of it.
    if (!InMission()) return;
    if (g_smoothZoom) {
        zoom::Wheel(-step, atCursor);
        return;
    }
    if (g_cfg.nativeZoom) {
        g_nativeTarget = step > 0 ? 1 : 0;
        return;
    }
    int from = g_zoomTarget >= 0 ? g_zoomTarget : g_zoomIndex;
    g_zoomTarget = std::clamp(from + step, 0, (int)g_zoom.size() - 1);
}

typedef void(__thiscall* SGameToggleZoom_t)(void* sgame);

void ApplyNativeZoom() {
    int target = g_nativeTarget;
    g_nativeTarget = -1;
    if (!InMission()) return;
    char* sgame = (char*)BaseScene();
    short& zoomed = *(short*)(sgame + game::SGameZoomedOut);
    if ((zoomed == 1) == (target == 1)) return;
    ((SGameToggleZoom_t)game::SGameToggleZoom)(sgame);
    Log("native zoom %s -> view %dx%d", zoomed == 1 ? "out" : "in", *(short*)(sgame + game::SGameViewW),
        *(short*)(sgame + game::SGameViewH));
}

// Runs at the start of every frame tick, on the game's main thread, outside any
// event handler: the safe place to change resolution. Rapid wheel input
// coalesces into one change per frame.
void EnsureSubclassed();
void UpdateMouseCapture();

void __cdecl OnFrameTick() {
    prof::FrameTick();
    watchdog::Beat();
    menu::Tick();
    if (render::Active()) render::Tick(SceneStack());
    layers::OnFrameStart();
    EnsureSubclassed();
    if (g_cfg.cameraScan) scan::Tick(InMission() ? BaseScene() : nullptr);
    camera::Tick(InMission() ? BaseScene() : nullptr, g_hwnd);
    controls::Tick(InMission() ? BaseScene() : nullptr);
    editor::Tick();
    UpdateMouseCapture();
    // [Render] FullRedraw=1 (default): the whole battlefield is redrawn every frame
    // (AGrid::MarkAllCellsDirty 0x42f210 on g_Map). The game only redraws the 64 px
    // cells something marked, and fast flyers (the Tanu Jubjub) left trails of old
    // copies where nothing did. A modern PC redraws the lot in well under a frame.
    static int fullRedraw = IniInt("Render", "FullRedraw", 1);
    if (fullRedraw && InMission()) ((void(__thiscall*)(uintptr_t, int, int))0x42f210)(0x55dd60, 1, 0);
    // [Debug] NoFog=1: switch the local player's fog of war off (screenshots only;
    // not in the map editor, which keeps it off itself and saves the map's own).
    static int noFog = IniInt("Debug", "NoFog", 0);
    if (noFog && InMission() && !editor::Active())
        if (char* me = *(char**)0x579288) *(short*)(me + 0x484c) = 0;
    if (g_autopilot)
        autopilot::Tick(SceneName(BaseScene()), game::at<short>(game::ScreenRight), game::at<short>(game::ScreenBottom));
    if (g_cursorTrace) {
        static int ticks;
        if (++ticks == 600) {  // ~3 s in: trap writes to the canvas just inside the cursor
            POINT c = render::GameCursor();
            uint8_t* px = render::FramePixel(c.x + 3, c.y + 3);
            Log("cursor trace: cursor at %ld,%ld, pixel %p", c.x, c.y, px);
            if (px) diag::TraceWrites((uintptr_t)px);
        }
    }
    if (g_saveSceneFrames) {
        // Save one frame of each screen ~1 s after it appears (for widescreen menu art).
        static std::string current;
        static int countdown;
        std::string name = SceneName(BaseScene());
        if (name != current) {
            current = name;
            countdown = 60;
        } else if (countdown > 0 && --countdown == 0 && name.size() > 6) {
            std::string file = "scene-" + name.substr(4, name.size() - 6) + ".bmp";  // .?AVSName@@ -> SName
            diag::SaveFrameAs(file.c_str());
        }
    }
    if (g_debugHotkeys && GetForegroundWindow() == g_hwnd) {
        // Polled rather than message-driven, so it works whatever the game does with keys.
        static bool f10, f11, f12;
        bool d10 = GetAsyncKeyState(VK_F10) & 0x8000, d11 = GetAsyncKeyState(VK_F11) & 0x8000;
        bool d12 = GetAsyncKeyState(VK_F12) & 0x8000;
        if (d12 && !f12) {
            static int n;
            render::d3d11::Screenshot(g_dir + "WarBreedsHD-screen-" + std::to_string(++n) + ".bmp");
            diag::SaveFrameAs(("WarBreedsHD-frame-" + std::to_string(n) + ".bmp").c_str());
        }
        f12 = d12;
        if (d10 && !f10) {
            diag::DumpWidgets();
            Log("  zoom %.3f", zoom::Current());
        }
        if (d11 && !f11) diag::SaveFrame();
        f10 = d10;
        f11 = d11;
    }
    if (g_smoothZoom) {
        prof::Scope scope("zoomtick");
        zoom::Tick(InMission() ? BaseScene() : nullptr);
    }
    if (g_nativeTarget >= 0) ApplyNativeZoom();
    if (g_zoomTarget < 0) return;
    int target = g_zoomTarget;
    g_zoomTarget = -1;
    if (target == g_zoomIndex || g_inSetResolution || !InMission()) return;
    g_zoomIndex = target;
    const Res& z = g_zoom[target];
    Log("zoom -> [%d] %dx%d", target, z.w, z.h);
    ((SetResolution_t)game::SetResolution)(z.w, z.h, 0x100, 1);
}

bool InstallFrameTickHook() {
    if (memcmp((void*)game::FrameTick, game::FrameTickPrologue, sizeof game::FrameTickPrologue) != 0) return false;
    // pushad; pushfd; call OnFrameTick; popfd; popad; <original 6 bytes>; jmp FrameTick+6
    auto* t = (uint8_t*)VirtualAlloc(nullptr, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!t) return false;
    t[0] = 0x60;
    t[1] = 0x9c;
    WriteRel32(t + 2, 0xe8, (uintptr_t)&OnFrameTick);
    t[7] = 0x9d;
    t[8] = 0x61;
    memcpy(t + 9, game::FrameTickPrologue, sizeof game::FrameTickPrologue);
    WriteRel32(t + 15, 0xe9, game::FrameTick + sizeof game::FrameTickPrologue);
    return HookJmp(game::FrameTick, t);
}

// SetResolution shrinks the window to the new mode size, which briefly shows
// the desktop around it before DDrawCompat stretches it back to fullscreen.
// DDrawCompat keeps the window covering the monitor itself, so keep its size.
BOOL WINAPI SetWindowPosKeepSize(HWND hwnd, HWND after, int x, int y, int cx, int cy, UINT flags) {
    return SetWindowPos(hwnd, after, x, y, cx, cy, flags | SWP_NOSIZE | SWP_NOMOVE);
}

bool PatchSetWindowPos() {
    if (memcmp((void*)game::SetResolutionSetWindowPos, game::SetResolutionSetWindowPosBytes, 6) != 0) return false;
    uint8_t call[6];
    WriteRel32(call, 0xe8, 0);
    int32_t rel = (int32_t)((uintptr_t)&SetWindowPosKeepSize - (game::SetResolutionSetWindowPos + 5));
    memcpy(call + 1, &rel, 4);
    call[5] = 0x90;
    return WriteCode(game::SetResolutionSetWindowPos, call, 6);
}

DWORD WINAPI QuitWatchdog(LPVOID) {
    Sleep(3000);
    Log("game did not exit after Alt+F4; terminating");
    ExitProcess(0);
}

// Mouse capture ([Input] CaptureMouse): armed by a click in the game window, confines the
// pointer to the client area while the game is in front, released when it isn't.
bool g_captureArmed;
RECT g_captureRect;
bool g_captured;

void ReleaseMouseCapture() {
    if (g_captured) ClipCursor(nullptr);
    g_captured = false;
}

void UpdateMouseCapture() {
    bool want = g_cfg.captureMouse && g_captureArmed && g_hwnd && GetForegroundWindow() == g_hwnd && !IsIconic(g_hwnd);
    if (!want) {
        if (!g_cfg.captureMouse || GetForegroundWindow() != g_hwnd) g_captureArmed = false;
        ReleaseMouseCapture();
        return;
    }
    RECT r;
    GetClientRect(g_hwnd, &r);
    MapWindowPoints(g_hwnd, nullptr, (POINT*)&r, 2);
    if (r.right <= r.left || r.bottom <= r.top) return;
    if (!g_captured || !EqualRect(&r, &g_captureRect)) {
        ClipCursor(&r);
        g_captureRect = r;
        g_captured = true;
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    watchdog::Beat();
    // The game ignores WM_RBUTTONDBLCLK (its window class has CS_DBLCLKS), so a
    // quick second right-click would be lost.
    if (msg == WM_RBUTTONDBLCLK) msg = WM_RBUTTONDOWN;
    if (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN || msg == WM_MBUTTONDOWN) {
        g_captureArmed = true;  // a click in the client area: capture (if enabled) from now on
        UpdateMouseCapture();
    } else if ((msg == WM_ACTIVATEAPP && !wp) || msg == WM_KILLFOCUS || msg == WM_ENTERSIZEMOVE) {
        g_captureArmed = false;
        ReleaseMouseCapture();
    }
    switch (msg) {
    case WM_MOUSEWHEEL:
        {  // the settings page scrolls with the wheel (whole notches)
            static int menuAccum;
            if (menu::OnWheel(0)) {
                menuAccum += GET_WHEEL_DELTA_WPARAM(wp);
                int notches = menuAccum / WHEEL_DELTA;
                menuAccum -= notches * WHEEL_DELTA;
                if (notches) menu::OnWheel(notches);
                return 0;
            }
        }
        if (g_cfg.mouseWheel) {
            // One step per notch (WHEEL_DELTA); high-resolution wheels send smaller deltas.
            g_wheelAccum += GET_WHEEL_DELTA_WPARAM(wp);
            while (g_wheelAccum >= WHEEL_DELTA) { RequestZoomStep(-1); g_wheelAccum -= WHEEL_DELTA; }
            while (g_wheelAccum <= -WHEEL_DELTA) { RequestZoomStep(+1); g_wheelAccum += WHEEL_DELTA; }
            return 0;
        }
        break;
    case WM_RBUTTONDOWN:
        if (menu::OnRightClick()) return 0;
        break;
    case WM_KEYUP:
        if (InMission() && camera::OwnsKey(wp)) return 0;
        if (editor::OnMessage(msg, wp, lp)) return 0;
        break;
    case WM_CHAR:
        if (editor::OnMessage(msg, wp, lp)) return 0;  // the map editor: no game keys, typed names
        break;
    case WM_KEYDOWN:
        if (InMission() && camera::OwnsKey(wp)) return 0;
        if (g_debugHotkeys && (wp == VK_F10 || wp == VK_F11)) return 0;  // handled in OnFrameTick
        if (wp == VK_F2 && render::Active()) {  // F2 is unused by the game
            if (!(lp & 0x40000000)) {  // not auto-repeat
                bool on = !render::ShowFps();
                render::SetShowFps(on);
                WritePrivateProfileStringA("Render", "ShowFPS", on ? "1" : "0", IniPath().c_str());
            }
            return 0;
        }
        if (g_cfg.keys) {
            if (wp == VK_ADD) { RequestZoomStep(-1, false); return 0; }
            if (wp == VK_SUBTRACT) { RequestZoomStep(+1, false); return 0; }
        }
        if (editor::OnMessage(msg, wp, lp)) return 0;
        break;
    case WM_SYSKEYDOWN:  // F10 arrives as a system key
        if (wp == VK_RETURN && (lp & (1 << 29)) && render::Active()) {  // Alt+Enter: fullscreen <-> windowed
            if (!(lp & 0x40000000)) {
                WritePrivateProfileStringA("Display", "DisplayMode", Windowed() ? "fullscreen" : "windowed",
                                           IniPath().c_str());
                ApplyLiveSettings();
            }
            return 0;
        }
        if (g_debugHotkeys && (wp == VK_F10 || wp == VK_F11)) return 0;  // handled in OnFrameTick
        if (g_cfg.altF4 && wp == VK_F4) {
            Log("Alt+F4: quitting");
            PostQuitMessage(0);
            CloseHandle(CreateThread(nullptr, 0, QuitWatchdog, nullptr, 0, nullptr));
            return 0;
        }
        break;
    }
    render::FilterMessage(msg, wp, lp);
    return CallWindowProcA(g_origWndProc, hwnd, msg, wp, lp);
}

// The window is created after we load; subclass it from a watcher thread once it exists.
// Subclass the game window from the game's own thread (called every frame
// tick until it succeeds); doing it from another thread raced with the game.
void EnsureSubclassed() {
    if (g_origWndProc) return;
    HWND h = game::at<HWND>(game::MainWindow);
    if (!h || !IsWindow(h)) return;
    g_hwnd = h;
    g_origWndProc = (WNDPROC)SetWindowLongA(h, GWL_WNDPROC, (LONG)(LONG_PTR)&WndProc);
    Log("subclassed game window %p", h);
}

// ---------------------------------------------------------------------------

DWORD WINAPI ShowError(LPVOID msg) {
    MessageBoxA(nullptr, (const char*)msg, "WarBreeds HD", MB_OK | MB_ICONWARNING);
    return 0;
}

void Init(HMODULE self) {
    char path[MAX_PATH];
    GetModuleFileNameA(self, path, MAX_PATH);
    g_dir = path;
    g_dir.resize(g_dir.find_last_of("\\/") + 1);
    g_log = fopen((g_dir + "WarBreedsHD.log").c_str(), "w");
    Log("WarBreeds HD " WBHD_VERSION " loaded from %s", path);
    watchdog::Start(Log, [] { return game::at<HWND>(game::MainWindow); });

    char exe[MAX_PATH];
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    const char* exeName = strrchr(exe, '\\') ? strrchr(exe, '\\') + 1 : exe;
    if (_stricmp(exeName, "WARBREED.EXE") != 0) {
        Log("host is %s, not WARBREED.EXE; acting as a plain wsock32 proxy", exeName);
        return;
    }

    LoadConfig();
    if (!g_cfg.enabled) { Log("disabled in WarBreedsHD.ini"); return; }

    if (IniInt("Debug", "Headless", 0)) headless::Install(Log);

    // The engine's timer/audio threads (timeSetEvent callbacks, SuspendThread)
    // race and can deadlock on multi-core CPUs; GOG's DDrawCompat pins the game
    // to one core for this reason. Do the same, before any game thread starts.
    if (IniInt("Compat", "SingleCPU", 1)) {
        DWORD_PTR proc, sys;
        if (GetProcessAffinityMask(GetCurrentProcess(), &proc, &sys) && proc) {
            DWORD_PTR one = proc & (~proc + 1);  // lowest allowed CPU
            // [Compat] CPU=n picks the n-th allowed CPU. By default each copy of
            // the game claims the first core no other copy holds: two copies on
            // one core starve each other's network threads, and a multiplayer
            // host then drops the other copy as too slow (ping over 1.2 s).
            int n = IniInt("Compat", "CPU", -1);
            for (DWORD_PTR m = proc, i = 0; m; m &= m - 1, ++i) {
                DWORD_PTR bit = m & (~m + 1);
                if (n < 0) {
                    char name[64];
                    snprintf(name, sizeof name, "Local\\WarBreedsHD-CPU-%u", (unsigned)i);
                    HANDLE claim = CreateMutexA(nullptr, FALSE, name);  // held until exit
                    if (claim && GetLastError() != ERROR_ALREADY_EXISTS) { one = bit; break; }
                    if (claim) CloseHandle(claim);
                } else if ((int)i == n) {
                    one = bit;
                }
            }
            SetProcessAffinityMask(GetCurrentProcess(), one);
            Log("pinned to one CPU (mask %#lx)", (unsigned long)one);
        }
    }

    bool known = *(uintptr_t*)(game::VtblSGame + 0x4c) == game::SGameOnResize &&
                 strcmp(RttiName(game::VtblSGame), ".?AVSGame@@") == 0;
    if (!known || !InstallSetResolutionHook()) {
        Log("unrecognised WARBREED.EXE; no patches applied");
        CloseHandle(CreateThread(nullptr, 0, ShowError,
                                 (LPVOID) "This version of WARBREED.EXE is not recognised, so WarBreeds HD "
                                          "has not changed anything. The game will run as normal.\n\n"
                                          "Tested with the GOG.com release. See WarBreedsHD.log.",
                                 0, nullptr));
        return;
    }
    Log("hooked SetResolution at %#x", (unsigned)game::SetResolution);
    if (IniInt("Debug", "NoFrameTickHook", 0))
        Log("frame tick hook: skipped (debug)");
    else
        Log("frame tick hook: %s", InstallFrameTickHook() ? "ok" : "FAILED (zoom disabled)");
    diag::Init(g_dir, Log);
    camera::Init(g_cfg.cam, Log);
    // Needed whatever the zoom mode: any frame as large as a small map halved the view.
    Log("view fix: %s", camera::InstallViewFix() ? "installed" : "FAILED (unexpected code)");
    // [Debug] NoMenu=1: the stock main menu (no Settings item), e.g. for before/after shots.
    if (!IniInt("Debug", "NoMenu", 0)) menu::Install({Log, IniPath(), g_dir, ApplyLiveSettings});
    controls::Install(Log);
    ApplyControlOptions();
    editor::Install({Log, g_dir, IniInt("Debug", "Autopilot", 0) != 0});  // after controls: its mouse hooks chain
    skirmish::Install(Log);
    teams::Install(Log);
    campaign::Install(g_dir, Log);
    {
        movie::Hooks mh;
        mh.log = Log;
        mh.silent = IniInt("Debug", "Headless", 0) != 0;
        mh.present = [](const unsigned* px, int w, int h) { render::ShowMovieFrame((const uint32_t*)px, w, h); };
        if (IniInt("Debug", "Autopilot", 0)) mh.tick = autopilot::MovieTick;
        movie::Install(mh);
    }
    g_debugHotkeys = IniInt("Debug", "Hotkeys", 0) != 0;
    g_saveSceneFrames = IniInt("Debug", "SaveSceneFrames", 0) != 0;
    g_cursorTrace = IniInt("Debug", "CursorTrace", 0) != 0;
    g_autopilot = IniInt("Debug", "Autopilot", 0) != 0;
    if (IniInt("Debug", "Profile", 0)) prof::Enable(Log);
    if (IniInt("Debug", "GlobeMeter", 0)) meter::Enable(Log);
    if (g_autopilot)
        autopilot::Init({g_dir, Log, [](int step) { RequestZoomStep(step); }, IniInt("Debug", "Autopilot", 0),
                         IniInt("Debug", "AutopilotTarget", 0), [] {
                             short top = game::at<short>(game::SceneTop);
                             return top < 0 ? "(none)" : SceneName((&game::at<void*>(game::Scenes))[top]);
                         },
                         IniInt("Debug", "AutopilotBare", 0) != 0});
    if (g_cfg.cameraScan) {
        scan::Open(g_dir + "WarBreedsHD-scan.bin");
        Log("memory scan recording enabled");
    }

    if (g_cfg.renderer == "d3d11" || g_cfg.renderer == "gdi") {
        render::Settings rs;
        rs.d3d11 = g_cfg.renderer == "d3d11";
        rs.filter = g_cfg.filter;
        rs.vsync = g_cfg.vsync;
        rs.gdiLog = IniInt("Debug", "GdiLog", 0) != 0;
        rs.headless = IniInt("Debug", "Headless", 0) != 0;
        rs.showFps = IniInt("Render", "ShowFPS", 0) != 0;
        rs.clickLog = IniInt("Debug", "ClickLog", 0) != 0;
        rs.menuArt = _stricmp(IniGet("Display", "MenuArt", "widescreen").c_str(), "original") != 0;
        rs.windowed = Windowed();
        if (IniInt("Debug", "CaptureScenes", 0)) Log("capturing every new screen ([Debug] CaptureScenes)");
        Res ws = WindowSize();
        rs.windowW = ws.w;
        rs.windowH = ws.h;
        for (Res r : {Res{640, 480}, Res{800, 600}, Res{1024, 768}}) rs.modes.push_back({r.w, r.h, 8});
        for (const Res& r : g_zoom) rs.modes.push_back({r.w, r.h, 8});
        if (render::Install(self, rs, Log)) {
            render::SetSceneStackProvider(SceneStack, IniInt("Debug", "CaptureScenes", 0));
            ApplyMovieOptions();
        }
        if (!render::Active()) {
            Log("renderer failed to install; GOG's ddraw.dll will draw");
        } else if (g_cfg.zoomMode == ZoomMode::Smooth) {
            g_smoothZoom = true;
            zoom::Init(g_cfg.smooth, g_cfg.base.w, g_cfg.base.h, Log);
            render::SetHudScale(g_cfg.hudScale, g_cfg.base.h);
            Log("interface scale %.3f (%.3f at %dx%d)", EffectiveHudScale(), g_cfg.hudScale, g_cfg.base.w, g_cfg.base.h);
            // Interface and cursor are drawn into their own engine ports in
            // missions, so they can stay one size while the battlefield zooms.
            if (!IniInt("Debug", "NoInterfaceLayers", 0))
                layers::Install(
                    Log,
                    [](const layers::Captured& c) {
                        render::SetInterfaceLayers(c.ui.data(), c.cursor.data(), c.w, c.h, c.cursorX, c.cursorY);
                    },
                    [](const void* bits) { return render::DibIsBottomUp(bits, true); },
                    IniInt("Debug", "LayerLog", 0) != 0);
            // Menus: the cursor drawn on its own, so it can follow the mouse
            // over the widescreen sides.
            layers::SetMenuCursorSink(
                [](const uint8_t* px, int size, int x, int y) { render::SetMenuCursor(px, size, x, y); });
        }
    } else {
        // DDrawCompat draws: keep its window at desktop size and offer our modes.
        // Smooth zoom needs our renderer; fall back to the game's own zoom.
        if (g_cfg.zoomMode == ZoomMode::Smooth) g_cfg.nativeZoom = true;
        if (IniInt("Debug", "NoWindowPatch", 0))
            Log("keep window size: skipped (debug)");
        else
            Log("keep window size on mode change: %s", PatchSetWindowPos() ? "ok" : "FAILED");
        WriteDDrawCompatIni();
        if (IniInt("Debug", "GdiTrace", 0)) gditrace::Install(Log, [] { return game::at<HWND>(game::MainWindow); });
    }
    // After the renderer: its SelectObject/DeleteObject hooks are chained.
    fonts::Install(g_dir.c_str(), Log);
    ApplyFontOption();
    // Multiplayer start-up: DirectPlay through COM again (see h_CoInitialize).
    render::HookImport(0x595b34, &h_CoInitialize, &o_CoInitialize);
    render::HookImport(0x595b30, &h_CoCreateInstance, &o_CoCreateInstance);
    if (IniInt("Debug", "CommsLog", 0)) {
        uint8_t* p = (uint8_t*)0x4c60f0;
        const uint8_t expect[] = {0x8b, 0x4c, 0x24, 0x04, 0x81, 0xec};
        DWORD old;
        if (memcmp(p, expect, sizeof expect) == 0 && VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old)) {
            p[0] = 0xe9;  // jmp h_CommsDebug (same cdecl arguments)
            *(int32_t*)(p + 1) = (int32_t)((uintptr_t)&h_CommsDebug - (uintptr_t)(p + 5));
            VirtualProtect(p, 5, old, &old);
            FlushInstructionCache(GetCurrentProcess(), p, 5);
            Log("comms log: on");
        }
    }
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        Init(inst);
    }
    return TRUE;
}
