// Main-menu "Settings" page.
//
// The main menu is the SMissionMenu scene (vtable 0x508304), pushed over
// SHeadquarters (the animated cave). Its Init (vtable slot 5, 0x48fcf0) adds
// eight text buttons with Scene::AddTextButton (0x495cb0); clicks arrive in
// OnCommand (slot 27, 0x48ff40) with the button, whose +0x20 is the command
// id (0x64..0x6b). We replace both vtable slots:
//   Init:      after the original, add "Settings" between Credits and Quit
//              (Quit moves down a row) and the settings page's buttons
//              (hidden).
//   OnCommand: "Settings" swaps the main items for the settings page; a
//              setting cycles its value, saves it to WarBreedsHD.ini and
//              applies it; "Back" swaps back. Anything else goes to the game.
// Everything is drawn, highlighted and hit-tested by the engine itself.
//
// Text button (WTextButton2, vtable 0x50925c, 0x78 bytes) fields we use:
//   +0x0c rect (left, top, right, bottom shorts; right is rewritten to the
//         text width when drawn), +0x14 hotkey (char), +0x1a needs-redraw,
//   +0x20 command id (short), +0x2c text (up to ~60 chars, set with
//   0x49a470), +0x74 visible (draw only; hit-testing and hotkeys ignore it,
//   so hidden buttons are also moved off-screen and lose their hotkey).
#include "menu.h"

#include "version.h"
#include "fonts.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include <map>
#include <vector>

namespace menu {
namespace {

Hooks g_h;

constexpr uintptr_t kMenuVtbl = 0x508304;           // SMissionMenu
constexpr uintptr_t kSlotInit = kMenuVtbl + 5 * 4;  // 0x508318
constexpr uintptr_t kSlotCommand = kMenuVtbl + 27 * 4;  // 0x508370
constexpr uintptr_t kInit = 0x48fcf0;
constexpr uintptr_t kCommand = 0x48ff40;
constexpr uintptr_t kTextButtonVtbl = 0x50925c;
constexpr uintptr_t kAddTextButton = 0x495cb0;  // thiscall scene (text, cmd, hotkey) -> button
constexpr uintptr_t kSetStyle = 0x49a500;       // thiscall button (normal, hover)
constexpr uintptr_t kSetText = 0x49a470;        // thiscall button (text)
constexpr uintptr_t kPlaySound = 0x464000;      // thiscall 0x55aeb8 (id)
constexpr uintptr_t kSoundPlayer = 0x55aeb8;
constexpr int kSelectSound = 0x68;
constexpr uintptr_t kTextButtonDraw = 0x49a3a0;  // vtable slot 1: thiscall, no args
constexpr uintptr_t kScenes = 0x55aea8, kSceneTop = 0x51bee8;
constexpr uintptr_t kCursorX = 0x5794d8, kCursorY = 0x5794dc;

constexpr short kCmdQuit = 0x6b;
constexpr short kCmdSettings = 0x6c;
constexpr short kCmdVersion = 0x7c;  // text only, on both pages
constexpr short kCmdFirstTab = 0x79;  // 0x79..0x7b: the settings tabs
constexpr short kCmdReset = 0x7d;
constexpr short kCmdHint = 0x7e;  // text only
constexpr short kCmdBack = 0x7f;
constexpr short kCmdFirstSetting = 0x80;

typedef void(__thiscall* Draw_t)(void* button);
typedef short(__thiscall* Init_t)(void* scene);
typedef int(__thiscall* Command_t)(void* scene, void* button);
typedef void*(__thiscall* AddTextButton_t)(void* scene, const char* text, int cmd, int hotkey);
typedef void(__thiscall* SetStyle_t)(void* button, int normal, int hover);
typedef void(__thiscall* SetText_t)(void* button, const char* text);
typedef int(__thiscall* PlaySound_t)(void* player, int id);

struct Choice {
    const char* shown;
    const char* value;  // as written to the ini
};
enum Tab { kVideo, kControls, kInterface, kTabCount };
const char* const kTabNames[kTabCount] = {"Video", "Controls", "Interface"};
const char kVersion[] = "WarBreeds HD v" WBHD_VERSION;
constexpr int kTabGap = 30;  // between tab headers

struct Setting {
    const char* label;
    const char* section;
    const char* key;
    const char* fallback;  // ini default
    std::vector<Choice> choices;
    int tab;
};

const std::vector<Setting>& Settings() {
    static const std::vector<Setting> s = {
        // Video
        {"Display Mode", "Display", "DisplayMode", "fullscreen", {{"Fullscreen", "fullscreen"}, {"Windowed", "windowed"}},
         kVideo},
        {"Resolution", "Display", "Resolution", "auto",
         {{"Auto (desktop)", "auto"}, {"1280x720", "1280x720"}, {"1600x900", "1600x900"}, {"1920x1080", "1920x1080"},
          {"2560x1080", "2560x1080"}, {"2560x1440", "2560x1440"}, {"3440x1440", "3440x1440"},
          {"3840x2160", "3840x2160"}},
         kVideo},
        {"Scaling", "Render", "Filter", "sharp", {{"Sharp", "sharp"}, {"Smooth", "bilinear"}, {"Pixelated", "nearest"}},
         kVideo},
        {"VSync", "Render", "VSync", "1", {{"On", "1"}, {"Off", "0"}}, kVideo},
        {"FPS Counter", "Render", "ShowFPS", "0", {{"Off", "0"}, {"On", "1"}}, kVideo},
        {"Movie Scaling", "Render", "MovieScaling", "enhanced",
         {{"Enhanced", "enhanced"}, {"Smooth", "smooth"}, {"Sharp", "sharp"}}, kVideo},
        {"Movie Size", "Render", "MovieSize", "full", {{"Full Width", "full"}, {"Original", "original"}}, kVideo},
        // Controls
        {"Smooth Key Scrolling", "Camera", "SmoothKeys", "1", {{"On", "1"}, {"Off", "0"}}, kControls},
        {"WASD Scrolling", "Camera", "WASD", "0", {{"Off", "0"}, {"On", "1"}}, kControls},
        {"Scroll Speed", "Camera", "KeySpeed", "1.0",
         {{"1x", "1.0"}, {"1.5x", "1.5"}, {"2x", "2.0"}, {"0.5x", "0.5"}, {"0.75x", "0.75"}}, kControls},
        {"Zoom Out Limit", "Zoom", "Min", "0.5", {{"0.5x", "0.5"}, {"0.67x", "0.667"}, {"0.75x", "0.75"}, {"1x", "1.0"}},
         kControls},
        {"Zoom In Limit", "Zoom", "Max", "2.0", {{"2x", "2.0"}, {"2.5x", "2.5"}, {"3x", "3.0"}, {"1.5x", "1.5"}},
         kControls},
        {"Alt+F4 Quits", "Input", "AltF4", "1", {{"On", "1"}, {"Off", "0"}}, kControls},
        {"Mouse Scheme", "Input", "MouseScheme", "classic", {{"Classic", "classic"}, {"Modern", "modern"}}, kControls},
        {"Builder Click", "Input", "BuilderRepair", "1", {{"Finish/Repair", "1"}, {"Select", "0"}}, kControls},
        {"Quick-Build Keys", "Input", "QuickBuild", "1", {{"On", "1"}, {"Off", "0"}}, kControls},
        // Interface
        {"Menu Art", "Display", "MenuArt", "widescreen", {{"Widescreen", "widescreen"}, {"Original", "original"}},
         kInterface},
        {"Font", "Display", "Font", "original", {{"Original", "original"}, {"Orbitron", "orbitron"}}, kInterface},
        {"Interface Size", "HUD", "Scale", "auto",
         {{"Auto", "auto"}, {"1x", "1.0"}, {"1.5x", "1.5"}, {"2x", "2.0"}, {"2.5x", "2.5"}, {"3x", "3.0"}}, kInterface},
    };
    return s;
}

// Row of setting i on its tab's page (below the tab headers).
int SettingRow(size_t i) {
    const auto& all = Settings();
    int row = 2;
    for (size_t k = 0; k < i; ++k)
        if (all[k].tab == all[i].tab) ++row;
    return row;
}

// Layout (640x480 menu): rows 28 px apart, 19 px tall, from (18, 17).
constexpr short kLeft = 18, kRight = 298, kTop = 17, kRowH = 19, kRowStep = 28;
RECT RowRect(int row) {
    short t = (short)(kTop + row * kRowStep);
    return {kLeft, t, kRight, (short)(t + kRowH)};
}

// --- button access --------------------------------------------------------

short& Cmd(void* b) { return *(short*)((char*)b + 0x20); }
short& Visible(void* b) { return *(short*)((char*)b + 0x74); }
short& Dirty(void* b) { return *(short*)((char*)b + 0x1a); }
char& Hotkey(void* b) { return *((char*)b + 0x14); }
short* Rect(void* b) { return (short*)((char*)b + 0xc); }

std::vector<void*> Buttons(void* scene) {
    std::vector<void*> out;
    void** list = *(void***)((char*)scene + 0xc);
    int count = *(int*)((char*)scene + 0x10);
    if (!list || count < 0 || count > 256) return out;
    for (int i = 0; i < count; ++i)
        if (list[i] && *(uintptr_t*)list[i] == kTextButtonVtbl) out.push_back(list[i]);
    return out;
}

void* Find(void* scene, short cmd) {
    for (void* b : Buttons(scene))
        if (Cmd(b) == cmd) return b;
    return nullptr;
}

// Where each of our-managed buttons belongs when shown.
struct Home {
    short rect[4];
    char hotkey;
};
std::map<void*, Home> g_home;

void Show(void* b, bool on) {
    auto it = g_home.find(b);
    if (it == g_home.end()) {  // first time: remember where the game put it
        Home h;
        memcpy(h.rect, Rect(b), sizeof h.rect);
        h.hotkey = Hotkey(b);
        it = g_home.emplace(b, h).first;
    }
    Visible(b) = on ? 1 : 0;
    if (on) {
        memcpy(Rect(b), it->second.rect, sizeof it->second.rect);
        Hotkey(b) = it->second.hotkey;
    } else {
        short off[4] = {-1000, -1000, -1000, -1000};  // can't be hit
        memcpy(Rect(b), off, sizeof off);
        Hotkey(b) = 0;
    }
    Dirty(b) = 1;
}

void SetHome(void* b, RECT r, char hotkey) {
    Home h{{(short)r.left, (short)r.top, (short)r.right, (short)r.bottom}, hotkey};
    g_home[b] = h;
}

// Move one of our buttons (where it shows, and now if it is showing).
void Place(void* b, RECT r) {
    SetHome(b, r, g_home.count(b) ? g_home[b].hotkey : Hotkey(b));
    if (Visible(b)) {
        short rr[4] = {(short)r.left, (short)r.top, (short)r.right, (short)r.bottom};
        memcpy(Rect(b), rr, sizeof rr);
        Dirty(b) = 1;
    }
}

// Styles (resolved by 0x4698c0): 0x10/0x11 = the menu's WBLg24, white/yellow;
// 25 = WBMd14 white, the same typeface smaller.
constexpr int kStyleMenu = 0x10, kStyleMenuHover = 0x11, kStyleSmall = 25;

void* AddButton(void* scene, const char* text, short cmd, char hotkey, RECT r, int style = kStyleMenu,
                int hoverStyle = kStyleMenuHover) {
    void* b = ((AddTextButton_t)kAddTextButton)(scene, text, cmd, (unsigned char)hotkey);
    if (!b) return nullptr;
    ((SetStyle_t)kSetStyle)(b, style, hoverStyle);
    short rr[4] = {(short)r.left, (short)r.top, (short)r.right, (short)r.bottom};
    memcpy(Rect(b), rr, sizeof rr);
    SetHome(b, r, hotkey);
    return b;
}

void PlaySelect() { ((PlaySound_t)kPlaySound)((void*)kSoundPlayer, kSelectSound); }

// --- settings values ------------------------------------------------------

std::string IniGet(const Setting& s) {
    char buf[64];
    GetPrivateProfileStringA(s.section, s.key, s.fallback, buf, sizeof buf, g_h.ini.c_str());
    return buf;
}

bool SameValue(const std::string& a, const char* b) {
    if (_stricmp(a.c_str(), b) == 0) return true;
    char* end1;
    char* end2;
    double x = strtod(a.c_str(), &end1), y = strtod(b, &end2);
    return end1 != a.c_str() && *end1 == 0 && end2 != b && *end2 == 0 && fabs(x - y) < 0.01;
}

int CurrentChoice(const Setting& s) {
    std::string v = IniGet(s);
    for (size_t i = 0; i < s.choices.size(); ++i)
        if (SameValue(v, s.choices[i].value)) return (int)i;
    return -1;
}

std::string Label(const Setting& s) {
    int c = CurrentChoice(s);
    std::string v = c >= 0 ? s.choices[c].shown : "Custom (" + IniGet(s) + ")";
    return std::string(s.label) + ": " + v;
}

// Positions that depend on text widths, measured in the font being shown (the
// game's or the Font option's): the tab headers side by side, the version
// label against the right edge.
void Layout(void* scene) {
    int x = kLeft;
    for (int t = 0; t < kTabCount; ++t) {
        int w = fonts::TextWidth("WBLg24", kTabNames[t]);
        if (void* b = Find(scene, (short)(kCmdFirstTab + t))) {
            RECT r = RowRect(0);
            r.left = x;
            r.right = x + w;
            Place(b, r);
        }
        x += w + kTabGap;
    }
    if (void* v = Find(scene, kCmdVersion)) {
        int w = fonts::TextWidth("WBMd14", kVersion);
        Place(v, {622 - w, 452, 640, 468});  // 18 px margin, like the menu's left edge
    }
}

void Refresh(void* scene) {
    Layout(scene);
    const auto& all = Settings();
    for (size_t i = 0; i < all.size(); ++i)
        if (void* b = Find(scene, (short)(kCmdFirstSetting + i))) ((SetText_t)kSetText)(b, Label(all[i]).c_str());
}

// "Reset to Defaults" asks for a second click before acting.
constexpr const char* kResetText = "Reset to Defaults";
bool g_resetArmed;
void ArmReset(void* scene, bool armed) {
    g_resetArmed = armed;
    if (void* b = Find(scene, kCmdReset))
        ((SetText_t)kSetText)(b, armed ? "Click again to reset all settings" : kResetText);
}

void ResetToDefaults(void* scene) {
    for (const Setting& s : Settings()) WritePrivateProfileStringA(s.section, s.key, s.fallback, g_h.ini.c_str());
    g_h.log("menu: settings reset to defaults");
    if (g_h.applySettings) g_h.applySettings();
    Refresh(scene);
}

int g_tab = kVideo;  // settings tab shown

// The main menu's two picture hotspots (WMouseOverButtons over the cave art,
// command 1/2: Gene Lab / Device Lab) would still light up and open those labs
// under the settings page; move them out of reach while it is open.
bool g_hotspotsAway;
void MoveHotspots(void* scene, bool away) {
    if (away == g_hotspotsAway) return;
    void** list = *(void***)((char*)scene + 0xc);
    int count = *(int*)((char*)scene + 0x10);
    if (!list || count <= 0 || count > 256) return;
    typedef void(__thiscall * Offset_t)(void* widget, int dx, int dy);
    for (int i = 0; i < count; ++i) {
        void* w = list[i];
        if (!w || IsBadReadPtr(w, 0x24) || *(uintptr_t*)w == kTextButtonVtbl) continue;
        short c = Cmd(w);
        if (c == 1 || c == 2) ((Offset_t)(*(uintptr_t**)w)[5])(w, 0, away ? 5000 : -5000);
    }
    g_hotspotsAway = away;
}

void ShowSettingsPage(void* scene, bool settings) {
    const auto& all = Settings();
    Layout(scene);
    MoveHotspots(scene, settings);
    for (void* b : Buttons(scene)) {
        short c = Cmd(b);
        bool mainItem = (c >= 0x64 && c <= kCmdQuit) || c == kCmdSettings;
        bool page = c == kCmdBack || c == kCmdHint || c == kCmdReset || (c >= kCmdFirstTab && c < kCmdFirstTab + kTabCount);
        bool setting = c >= kCmdFirstSetting && c < kCmdFirstSetting + (short)all.size();
        if (mainItem) Show(b, !settings);
        if (page) Show(b, settings);
        if (setting) Show(b, settings && all[c - kCmdFirstSetting].tab == g_tab);
        if (c >= kCmdFirstTab && c < kCmdFirstTab + kTabCount) {
            bool active = c - kCmdFirstTab == g_tab;  // the open tab stays highlighted
            ((SetStyle_t)kSetStyle)(b, active ? kStyleMenuHover : kStyleMenu, kStyleMenuHover);
            Dirty(b) = 1;
        }
    }
    if (settings) Refresh(scene);
    ArmReset(scene, false);
}

// --- hooks ----------------------------------------------------------------

short __thiscall HookInit(void* scene) {
    short r = ((Init_t)kInit)(scene);
    if (Find(scene, kCmdSettings)) {  // already set up (scene re-shown)
        ShowSettingsPage(scene, false);
        return r;
    }
    g_home.clear();
    g_hotspotsAway = false;
    void* quit = Find(scene, kCmdQuit);
    if (!quit) {
        g_h.log("menu: Quit button not found; Settings not added");
        return r;
    }
    // Settings takes Quit's row; Quit moves down one.
    RECT qr{Rect(quit)[0], Rect(quit)[1], Rect(quit)[2], Rect(quit)[3]};
    RECT below{qr.left, qr.top + kRowStep, qr.right, qr.bottom + kRowStep};
    AddButton(scene, "Settings", kCmdSettings, 'O', qr);
    short qrect[4] = {(short)below.left, (short)below.top, (short)below.right, (short)below.bottom};
    memcpy(Rect(quit), qrect, sizeof qrect);
    SetHome(quit, below, Hotkey(quit));

    const auto& all = Settings();
    for (int t = 0; t < kTabCount; ++t)  // placed by Layout
        if (void* b = AddButton(scene, kTabNames[t], (short)(kCmdFirstTab + t), 0, RowRect(0))) Show(b, false);
    for (size_t i = 0; i < all.size(); ++i) {
        void* b = AddButton(scene, Label(all[i]).c_str(), (short)(kCmdFirstSetting + i), 0, RowRect(SettingRow(i)));
        if (b) Show(b, false);
    }
    if (void* reset = AddButton(scene, kResetText, kCmdReset, 'R', RowRect(13))) Show(reset, false);
    if (void* back = AddButton(scene, "Back", kCmdBack, 'B', RowRect(14))) Show(back, false);
    if (void* hint = AddButton(scene, "Click: next    Right-click: previous    Saved automatically",
                               kCmdHint, 0, {kLeft, 452, 450, 468}, kStyleSmall, kStyleSmall))
        Show(hint, false);
    // Bottom right: shows the mod is running (placed by Layout).
    AddButton(scene, kVersion, kCmdVersion, 0, {0, 452, 640, 468}, kStyleSmall, kStyleSmall);
    Layout(scene);
    g_h.log("menu: Settings added to the main menu");
    return r;
}

// Step a setting button's value forwards (+1) or backwards (-1): save it to
// the ini, apply it and relabel the button. False if it isn't a setting.
bool Cycle(void* button, int dir) {
    const auto& all = Settings();
    short c = Cmd(button);
    if (c < kCmdFirstSetting || c >= kCmdFirstSetting + (short)all.size()) return false;
    const Setting& s = all[c - kCmdFirstSetting];
    int n = (int)s.choices.size();
    int cur = CurrentChoice(s);
    int next = cur < 0 ? 0 : ((cur + dir) % n + n) % n;
    WritePrivateProfileStringA(s.section, s.key, s.choices[next].value, g_h.ini.c_str());
    g_h.log("menu: %s = %s", s.label, s.choices[next].value);
    if (g_h.applySettings) g_h.applySettings();
    PlaySelect();
    ((SetText_t)kSetText)(button, Label(s).c_str());
    return true;
}

int __thiscall HookCommand(void* scene, void* button) {
    short c = button ? Cmd(button) : 0;
    if (c == kCmdSettings) {
        PlaySelect();
        g_tab = kVideo;
        ShowSettingsPage(scene, true);
        return 0;
    }
    if (c == kCmdBack) {
        PlaySelect();
        ShowSettingsPage(scene, false);
        return 0;
    }
    if (c == kCmdHint || c == kCmdVersion) return 0;
    if (c >= kCmdFirstTab && c < kCmdFirstTab + kTabCount) {
        PlaySelect();
        g_tab = c - kCmdFirstTab;
        if (g_resetArmed) ArmReset(scene, false);
        ShowSettingsPage(scene, true);
        return 0;
    }
    if (c == kCmdReset) {
        PlaySelect();
        if (g_resetArmed) ResetToDefaults(scene);
        ArmReset(scene, !g_resetArmed);
        return 0;
    }
    if (g_resetArmed) ArmReset(scene, false);
    if (Cycle(button, +1)) return 0;
    return ((Command_t)kCommand)(scene, button);
}

// --- hover highlight --------------------------------------------------------

void* g_hover;  // text button under the mouse, in the top scene

void* TopScene() {
    short top = *(short*)kSceneTop;
    if (top < 0 || top > 16) return nullptr;
    return ((void**)kScenes)[top];
}

bool Contains(void* scene, void* b) {
    for (void* x : Buttons(scene))
        if (x == b) return true;
    return false;
}

// Draw the hovered button with its highlight style (+0x70) in place of the
// normal one (+0x6c); unlike the pressed look it doesn't nudge the text.
void __thiscall HookDraw(void* b) {
    if (b != g_hover) {
        ((Draw_t)kTextButtonDraw)(b);
        return;
    }
    int* normal = (int*)((char*)b + 0x6c);
    int saved = *normal;
    *normal = *(int*)((char*)b + 0x70);
    ((Draw_t)kTextButtonDraw)(b);
    *normal = saved;
}

bool PatchSlot(uintptr_t slot, uintptr_t expect, void* hook) {
    if (*(uintptr_t*)slot != expect) return false;
    DWORD old;
    if (!VirtualProtect((void*)slot, 4, PAGE_READWRITE, &old)) return false;
    *(void**)slot = hook;
    VirtualProtect((void*)slot, 4, old, &old);
    return true;
}

bool g_installed;  // hover highlight

// In-mission "Game Options" (SOption, vtable 0x50850c): its Resolution slider
// picks 640x480..1280x1024 for the game, which WarBreeds HD overrides (the
// resolution is set from the main menu's Settings). After SOption's Init
// (slot 5, 0x491750) the Resolution row (the first slider, vtable 0x509134,
// and the label/value text under it) moves out of reach and the Game Speed row
// takes its place. Widgets move with the common Offset method (slot 5,
// 0x49a760: thiscall (dx, dy), marks them for redraw).
constexpr uintptr_t kOptionVtbl = 0x50850c;
constexpr uintptr_t kOptionInit = 0x491750;
constexpr uintptr_t kSliderVtbl = 0x509134;
typedef void(__thiscall* Offset_t)(void* widget, int dx, int dy);

short __thiscall HookOptionInit(void* scene) {
    short r = ((Init_t)kOptionInit)(scene);
    void** list = *(void***)((char*)scene + 0xc);
    int count = *(int*)((char*)scene + 0x10);
    if (!list || count <= 0 || count > 256) return r;
    void* sliders[2] = {};
    int n = 0;
    for (int i = 0; i < count && n < 2; ++i)
        if (list[i] && *(uintptr_t*)list[i] == kSliderVtbl) sliders[n++] = list[i];
    if (n < 2) return r;
    int resTop = Rect(sliders[0])[1], speedTop = Rect(sliders[1])[1];
    int row = speedTop - resTop;
    if (row <= 0) return r;
    auto move = [](void* w, int dx, int dy) { ((Offset_t)(*(uintptr_t**)w)[5])(w, dx, dy); };
    int hidden = 0, moved = 0;
    for (int i = 0; i < count; ++i) {
        void* w = list[i];
        if (!w || IsBadReadPtr(w, 0x14)) continue;
        short* rc = Rect(w);
        int cy = (rc[1] + rc[3]) / 2;
        if (rc[3] - rc[1] > row * 2) continue;  // the dialog frame spans every row
        if (cy >= resTop && cy < speedTop) {
            move(w, 0, 5000);  // the Resolution row: out of sight and reach
            ++hidden;
        } else if (cy >= speedTop && cy < speedTop + row) {
            move(w, 0, -row);  // the Game Speed row moves up
            ++moved;
        }
    }
    g_h.log("menu: Game Options: resolution row hidden (%d widgets), game speed moved up (%d)", hidden, moved);
    return r;
}

}  // namespace

bool Install(const Hooks& h) {
    g_h = h;
    bool ok = PatchSlot(kSlotInit, kInit, (void*)&HookInit) && PatchSlot(kSlotCommand, kCommand, (void*)&HookCommand);
    h.log("menu: settings page %s", ok ? "installed" : "FAILED (unexpected vtable)");
    g_installed = PatchSlot(kTextButtonVtbl + 4, kTextButtonDraw, (void*)&HookDraw);
    bool opt = PatchSlot(kOptionVtbl + 5 * 4, kOptionInit, (void*)&HookOptionInit);
    h.log("menu: Game Options resolution row %s", opt ? "will be hidden" : "hook FAILED (unexpected vtable)");
    h.log("menu: hover highlight %s", g_installed ? "installed" : "FAILED (unexpected vtable)");
    return ok;
}

bool FindWidget(const char* text, long& x, long& y) {
    void* scene = TopScene();
    if (!scene || IsBadReadPtr(scene, 0x14)) return false;
    void** list = *(void***)((char*)scene + 0xc);
    int count = *(int*)((char*)scene + 0x10);
    if (!list || count < 0 || count > 256) return false;
    size_t n = strlen(text);
    for (int i = 0; i < count; ++i) {
        char* w = (char*)list[i];
        if (!w || IsBadReadPtr(w, 0x2c + n + 1)) continue;
        if (strncmp(w + 0x2c, text, n) == 0) {
            short* r = (short*)(w + 0xc);
            x = (r[0] + r[2]) / 2;
            y = (r[1] + r[3]) / 2;
            return true;
        }
    }
    return false;
}

void HideAllWidgets() {
    void* scene = TopScene();
    if (!scene || IsBadReadPtr(scene, 0x14)) return;
    void** list = *(void***)((char*)scene + 0xc);
    int count = *(int*)((char*)scene + 0x10);
    if (!list || count <= 0 || count > 256) return;
    typedef void(__thiscall * Offset_t)(void* widget, int dx, int dy);
    int n = 0;
    for (int i = 0; i < count; ++i) {
        void* w = list[i];
        if (!w || IsBadReadPtr(w, 0x24)) continue;
        ((Offset_t)(*(uintptr_t**)w)[5])(w, 0, 5000);
        ++n;
    }
    g_h.log("menu: moved %d widgets off-screen", n);
    *(short*)0x51d9a8 = 0;  // full redraw every frame (as in missions), so the background shows
}

void HideAllText() {
    if (void* scene = TopScene())
        for (void* b : Buttons(scene)) Show(b, false);
}

bool OnRightClick() {
    void* scene = TopScene();
    if (!g_installed || !g_hover || !scene || *(uintptr_t*)scene != kMenuVtbl || !Contains(scene, g_hover)) return false;
    if (g_resetArmed) ArmReset(scene, false);
    return Cycle(g_hover, -1);
}

void Tick() {
    if (!g_installed) return;
    void* scene = TopScene();
    void* hover = nullptr;
    if (scene && !IsBadReadPtr(scene, 0x14)) {
        POINT p{*(short*)kCursorX, *(short*)kCursorY};
        for (void* b : Buttons(scene)) {
            short* r = Rect(b);
            RECT rc{r[0], r[1], r[2], r[3]};
            if (Visible(b) && Cmd(b) != kCmdHint && Cmd(b) != kCmdVersion && *(short*)((char*)b + 0x68) > 0 && PtInRect(&rc, p)) hover = b;
        }
    }
    if (hover == g_hover) return;
    // The old button may belong to a scene that has gone; only touch it if
    // it is still in the top scene.
    if (g_hover && scene && Contains(scene, g_hover)) Dirty(g_hover) = 1;
    if (hover) Dirty(hover) = 1;
    g_hover = hover;
}

}  // namespace menu
