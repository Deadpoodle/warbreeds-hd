#include "controls.h"

#include <windows.h>
#include <stdint.h>
#include <stdlib.h>

#include <algorithm>

#include "game.h"

namespace controls {
namespace {

void (*g_log)(const char*, ...);
Options g_opt;

// --- engine addresses (re/hotkeys.md, re/mouse.md) -------------------------

// SGame vtable slots and their stock handlers.
constexpr uintptr_t kSlotLButtonUp = game::VtblSGame + 0x20, kLButtonUp = 0x47e430;
constexpr uintptr_t kSlotRButtonUp = game::VtblSGame + 0x28, kRButtonUp = 0x47e6f0;
constexpr uintptr_t kSceneRButtonUp = 0x435a70;  // base the stock RButtonUp calls first (x, y)
constexpr uintptr_t kSlotKeyDown = game::VtblSGame + 0x34, kKeyDown = 0x47f000;
constexpr uintptr_t kSlotKeyUp = game::VtblSGame + 0x38, kKeyUp = 0x47fd90;

constexpr uintptr_t kWorld = 0x579278;     // World*
constexpr uintptr_t kCommands = 0x579260;  // every gameplay order goes through this object
constexpr uintptr_t kLocalPlayer = 0x46f8e0;  // cdecl APlayer*()

// World (thiscall). Things: table data +0x10 / count +0x14; selection data +0x80 / count +0x84.
constexpr uintptr_t kWorldSelect = 0x44b5d0;   // (AThing*): add to the selection
constexpr uintptr_t kWorldPickAt = 0x44c530;   // AThing*(short rect[4]*, x, y, zoom)
constexpr uintptr_t kWorldAck = 0x451cd0;      // (int): the selection's acknowledge voice
constexpr uintptr_t kWorldDeselect = 0x44b620;  // (AThing*)
constexpr uintptr_t kWorldClearSel = 0x44b680;  // (short saveToPrevious)
constexpr uintptr_t kWorldCommandable = 0x44a1e0;  // short(): the selection takes orders
constexpr uintptr_t kWorldSelKind = 0x44a1c0;  // int(): 0 none, 1 several, 2 one unit, 3 one Shaman, 4 building, 5 pod
// The stock click: select what's there, or order the selection (rect*, x, y, zoom, pgdn, pgup, shift).
constexpr uintptr_t kWorldClick = 0x44a860;
// The stock order for a click: move or target, as the cursor shows (rect*, x, y, zoom, wx, wy).
constexpr uintptr_t kWorldOrderAt = 0x44c6d0;
// Box select own units: (short rect[4]* world x0,y0,x1,y1, ground, air, add); 0 = nothing in it.
constexpr uintptr_t kWorldBoxSelect = 0x44afe0;
// SGame (thiscall).
constexpr uintptr_t kViewRect = 0x47dc70;      // (short rect[4]*): visible world rect
constexpr uintptr_t kIsDragBox = 0x4804c0;     // short(): the pending click is a drag box
constexpr uintptr_t kCentreOnSel = 0x4802d0;   // (): centre the camera on the selection
constexpr uintptr_t kSceneLButtonUp = 0x495290;  // Scene base (x, y): scene widgets
// Shaman.
constexpr uintptr_t kCapture = 0x445a90;       // GameObject (short on): capture the mouse
constexpr uintptr_t kStartPlacement = 0x442c90;  // AShaman (): start placing +0x188 at +0x194/+0x196
constexpr uintptr_t kPlantCursor = 0x471560;  // cdecl (2, 0x58, 0), as the pod buttons call it
constexpr uintptr_t kCanBuild = 0x437b50;     // APlayer short(int deviceType): in the build list (tech)

// SGame fields (short).
constexpr uintptr_t kInputOff = 0x5c, kAttackHeld = 0x5a, kSpaceHeld = 0x62, kShiftHeld = 0x64,
                    kTabHeld = 0x68, kCtrlTabHeld = 0x6a, kMinimapDrag = 0x6c, kClickPending = 0x6e,
                    kPgDnHeld = 0x70, kPgUpHeld = 0x72, kGHeld = 0x78, kCHeld = 0x7c, kCaptured = 0x1f0,
                    kCheatPlace = 0x43c;
constexpr uintptr_t kHudDrag = 0x1cc;  // int
constexpr uintptr_t kDragX0 = 0x52, kDragY0 = 0x54, kDragX1 = 0x56, kDragY1 = 0x58;  // short, world
constexpr uintptr_t kWorldTHeld = 0x6e;  // short on World: T held (order only)
constexpr short kNoObject = 0xc01;

// AThing fields.
constexpr uintptr_t kType = 0x18;  // int: 0..0x2b creatures, 0x2c..0x3e devices, 0x3f..0x41 pods
constexpr uintptr_t kId = 0x1c, kOwner = 0x20, kHp = 0x24, kGroup = 0x8d;
// ADevice fields (short) and virtuals.
constexpr uintptr_t kUnpowered = 0x28, kSwitchedOn = 0x2a, kProgress = 0x32;
constexpr uintptr_t kVBuildTime = 0x70, kVMaxHp = 0x50;
// AShaman.
constexpr uintptr_t kShamanState = 0x1ac;  // int: 2 = build list, 3 = placing
constexpr uintptr_t kShamanBuildType = 0x188, kShamanPlaceX = 0x194, kShamanPlaceY = 0x196;
constexpr uintptr_t kCaptureFlag = 0x08;
// AShaman vtable 0x504a2c: click-down +0x18 (sx, sy, wx, wy, zoom), cursor +0x24 (same args).
constexpr uintptr_t kSlotShamanDown = 0x504a44, kShamanDown = 0x4419c0;
constexpr uintptr_t kSlotShamanCursor = 0x504a50, kShamanCursor = 0x442db0;
constexpr uintptr_t kResetMode = 0x442c00;   // AShaman (): leave the build/plant mode
constexpr uintptr_t kClickSound = 0x46fa70;  // cdecl ()
constexpr uintptr_t kCmdPlant = 0x58;        // Commands vtable: (short shamanId, int podType, PtList*)
constexpr uintptr_t kPlayerTileVisible = 0x453cc0;  // APlayer short(tileX, tileY): currently seen
constexpr uintptr_t kLocalPlayerPtr = 0x579288;
constexpr uintptr_t kPanelWidgetAt = 0x497c60, kPanel = 0x5476e8;  // WInfoBox widget*(sx, sy)
constexpr uintptr_t kWorldInPanel = 0x4497d0;  // World short(sx, sy): inside the info panel
constexpr uintptr_t kRadar = 0x5517b0, kRadarShown = 0x433e10, kRadarHit = 0x4335b0;
constexpr uintptr_t kToWorldX = 0x47ec60, kToWorldY = 0x47ec80;  // SGame int(screen coord)
constexpr uintptr_t kCmdRepair = 0x88;  // Commands vtable: (short builderId, short targetId)

template <typename T>
T& F(void* p, uintptr_t off) {
    return *(T*)((char*)p + off);
}
void* World() { return *(void**)kWorld; }
void* LocalPlayer() { return ((void* (*)())kLocalPlayer)(); }
int Type(void* t) { return F<int>(t, kType); }
bool Mine(void* t) { return F<void*>(t, kOwner) == LocalPlayer(); }
bool IsCreature(void* t) { return Type(t) >= 0 && Type(t) <= 0x2b; }
bool IsDevice(void* t) { return Type(t) >= 0x2c && Type(t) <= 0x3e; }
bool IsBuilder(void* t) {
    int ty = Type(t);
    return ty == 0 || ty == 9 || ty == 0x12 || ty == 0x1b;
}

void** Selection(int& count) {
    void* w = World();
    count = w ? F<int>(w, 0x84) : 0;
    return w ? F<void**>(w, 0x80) : nullptr;
}

typedef short(__thiscall* Handler_t)(void* sg, int a, int b);
typedef short(__thiscall* KeyHandler_t)(void* sg, int vk, int mods, int ascii);

// --- control groups ---------------------------------------------------------

int g_lastDigit = -1;
DWORD g_lastDigitTime;
bool g_digitReleased;

bool GroupExists(int n) {
    void* w = World();
    if (!w) return false;
    void** things = F<void**>(w, 0x10);
    int count = F<int>(w, 0x14);
    for (int i = 0; i < count; ++i) {
        void* t = things[i];
        if (t && IsCreature(t) && Mine(t) && F<uint8_t>(t, kGroup) == n) return true;
    }
    return false;
}

// Shift+N: add group N to the selection (the game's N always replaces it).
void AddGroup(int n) {
    void* w = World();
    if (!w) return;
    void** things = F<void**>(w, 0x10);
    int count = F<int>(w, 0x14), added = 0;
    for (int i = 0; i < count; ++i) {
        void* t = things[i];
        if (!t || !IsCreature(t) || !Mine(t) || F<uint8_t>(t, kGroup) != n) continue;
        ((void(__thiscall*)(void*, void*))kWorldSelect)(w, t);
        ++added;
    }
    if (added) ((void(__thiscall*)(void*, int))kWorldAck)(w, 0);
    g_log("controls: Shift+%d added %d units", n, added);
}

// --- quick-build --------------------------------------------------------------

// Build list order (re/hotkeys.md §4) and the key for each; the upgraded
// versions are Shift + the same letter.
struct BuildKey {
    char key;
    bool shift;
    const char* name;
};
const BuildKey kBuildKeys[16] = {
    {'R', false, "Refinery"}, {'G', false, "Gene Lab"},   {'L', false, "Relay"},    {'P', false, "Psi Tower"},
    {'M', false, "Mine"},     {'T', false, "Turret"},     {'E', false, "Electric"}, {'N', false, "Listener"},
    {'H', false, "Healer"},   {'G', true, "Gene Lab+"},   {'L', true, "Relay+"},    {'T', true, "Turret+"},
    {'P', true, "Psi Tower+"}, {'W', false, "Warp"},      {'S', false, "Summoner"}, {'C', false, "Collector"},
};

// The one selected own Shaman, if its build list (state 2) or pod list (4) is open.
void* ShamanInState(int state) {
    int n;
    void** sel = Selection(n);
    if (n != 1 || !sel || !sel[0]) return nullptr;
    void* s = sel[0];
    if (!IsBuilder(s) || !Mine(s) || F<int>(s, kShamanState) != state) return nullptr;
    return s;
}

// Does what clicking build-list button i does (0x442613..0x442673); the list
// shows the types the player can build (0x442fbb).
bool QuickBuild(int vk, bool shift) {
    void* shaman = ShamanInState(2);
    if (!shaman) return false;
    for (int i = 0; i < 16; ++i) {
        if (kBuildKeys[i].key != vk || kBuildKeys[i].shift != shift) continue;
        void* owner = F<void*>(shaman, kOwner);
        if (((short(__thiscall*)(void*, int))kCanBuild)(owner, 0x2c + i) != 1) {
            g_log("controls: quick-build %s not available", kBuildKeys[i].name);
            return false;  // the game's own key (e.g. S = stop) still works
        }
        if (F<short>(shaman, kCaptureFlag) == 0) ((void(__thiscall*)(void*, int))kCapture)(shaman, 1);
        F<int>(shaman, kShamanState) = 3;
        F<int>(shaman, kShamanBuildType) = 0x2c + i;
        // The ghost's world position (the button path takes it from its click;
        // mouse moves update it from then on).
        void* sg = (void*)((uintptr_t*)game::Scenes)[0];
        F<short>(shaman, kShamanPlaceX) = (short)((int(__thiscall*)(void*, int))kToWorldX)(sg, game::at<short>(game::CursorX));
        F<short>(shaman, kShamanPlaceY) = (short)((int(__thiscall*)(void*, int))kToWorldY)(sg, game::at<short>(game::CursorY));
        ((void(__thiscall*)(void*))kStartPlacement)(shaman);
        g_log("controls: quick-build %s", kBuildKeys[i].name);
        return true;
    }
    return false;
}

// Pod list (P): 1-3 pick a pod, as clicking pod button i does (0x4424b3..0x442504).
bool QuickPlant(int digit) {
    void* shaman = ShamanInState(4);
    if (!shaman || digit < 1 || digit > 3) return false;
    int type = 0x3f + digit - 1;
    if (((short(__thiscall*)(void*, int))kCanBuild)(F<void*>(shaman, kOwner), type) != 1) {
        g_log("controls: quick-plant pod %d not available", digit);
        return false;
    }
    F<int>(shaman, kShamanState) = 5;
    if (F<short>(shaman, kCaptureFlag) == 0) ((void(__thiscall*)(void*, int))kCapture)(shaman, 1);
    F<int>(shaman, kShamanBuildType) = type;
    ((void(__cdecl*)(int, int, int))kPlantCursor)(2, 0x58, 0);
    g_log("controls: quick-plant pod %d", digit);
    return true;
}

short __thiscall HookKeyDown(void* sg, int vk, int mods, int ascii) {
    int k = vk & 0xff, m = mods & 0xff;
    if (g_opt.quickBuild && k >= 'A' && k <= 'Z' && (m == 0 || m == 4) && QuickBuild(k, m == 4)) return 1;
    if (g_opt.quickBuild && k >= '1' && k <= '3' && m == 0 && QuickPlant(k - '0')) return 1;
    if (k >= '0' && k <= '9') {
        int n = k - '0';
        if (m == 4) {  // Shift
            AddGroup(n);
            return 1;
        }
        short r = ((KeyHandler_t)kKeyDown)(sg, vk, mods, ascii);
        if (m == 0) {
            DWORD now = GetTickCount();
            if (n == g_lastDigit && g_digitReleased && now - g_lastDigitTime < 350 && GroupExists(n)) {
                ((void(__thiscall*)(void*))kCentreOnSel)(sg);
                g_log("controls: double-tap %d, centred", n);
            }
            g_lastDigit = n;
            g_lastDigitTime = now;
            g_digitReleased = false;
        }
        return r;
    }
    return ((KeyHandler_t)kKeyDown)(sg, vk, mods, ascii);
}

short __thiscall HookKeyUp(void* sg, int vk, int mods, int ascii) {
    if ((vk & 0xff) == '0' + g_lastDigit) g_digitReleased = true;
    return ((KeyHandler_t)kKeyUp)(sg, vk, mods, ascii);
}

// --- builders finish / repair on click ----------------------------------------

// Mirrors the Repair button's target check (0x442780): own building, switched
// on, powered (the power source itself is exempt), and unfinished or damaged.
bool RepairTarget(void* t) {
    if (!t || !IsDevice(t) || !Mine(t)) return false;
    if (F<short>(t, kSwitchedOn) != 1) return false;
    if (F<short>(t, kUnpowered) == 1 && Type(t) != 0x2c) return false;
    void** vt = *(void***)t;
    short buildTime = ((short(__thiscall*)(void*))vt[kVBuildTime / 4])(t);
    short maxHp = ((short(__thiscall*)(void*))vt[kVMaxHp / 4])(t);
    return F<short>(t, kProgress) < buildTime || F<short>(t, kHp) < maxHp;
}

// A plain pending left click: no mode, no held-key mode, not a drag box.
bool PlainClick(void* sg) {
    void* w = World();
    return w && F<short>(sg, kInputOff) != 1 && F<short>(sg, kCheatPlace) != 1 &&
           F<short>(sg, kCaptured) == kNoObject && F<short>(sg, kClickPending) != 0 && F<short>(sg, kTabHeld) != 1 &&
           F<short>(sg, kCtrlTabHeld) != 1 && F<short>(sg, kSpaceHeld) != 1 && F<short>(sg, kGHeld) != 1 &&
           F<short>(sg, kCHeld) != 1 && F<short>(sg, kAttackHeld) != 1 && F<short>(sg, kShiftHeld) != 1 &&
           F<short>(sg, kPgDnHeld) != 1 && F<short>(sg, kPgUpHeld) != 1 && F<short>(w, kWorldTHeld) == 0 &&
           ((short(__thiscall*)(void*))kIsDragBox)(sg) == 0;
}

int SelectedBuilders(void** sel, int n) {
    int builders = 0;
    for (int i = 0; i < n && sel; ++i)
        if (sel[i] && IsBuilder(sel[i]) && Mine(sel[i])) ++builders;
    return builders;
}

// Every selected builder gets the game's Repair order on target (it finishes
// an unfinished building first), with the acknowledge voice.
void SendBuildersToRepair(void** sel, int n, void* target) {
    void* cmd = *(void**)kCommands;
    typedef void(__thiscall * Repair_t)(void* cmd, int builder, int target);
    Repair_t repair = (Repair_t)(*(void***)cmd)[kCmdRepair / 4];
    int builders = 0;
    for (int i = 0; i < n; ++i)
        if (sel[i] && IsBuilder(sel[i]) && Mine(sel[i])) {
            repair(cmd, F<short>(sel[i], kId), F<short>(target, kId));
            ++builders;
        }
    ((void(__thiscall*)(void*, int))kWorldAck)(World(), 0);
    g_log("controls: %d builder(s) sent to repair/finish building %d (type %#x)", builders, F<short>(target, kId),
          Type(target));
}

void* PickAt(void* sg, int x, int y, short* rect) {
    ((void(__thiscall*)(void*, short*))kViewRect)(sg, rect);
    return ((void*(__thiscall*)(void*, short*, int, int, int))kWorldPickAt)(World(), rect, (short)x, (short)y,
                                                                           F<short>(sg, game::SGameZoomedOut));
}

void WorldClick(void* sg, short* rect, int x, int y, int shift) {
    typedef short(__thiscall * Click_t)(void*, short*, int, int, int, int, int, int);
    ((Click_t)kWorldClick)(World(), rect, (short)x, (short)y, F<short>(sg, game::SGameZoomedOut), 0, 0, shift);
}

void ClearSelection() { ((void(__thiscall*)(void*, int))kWorldClearSel)(World(), 0); }

// --- Modern mouse scheme: left selects, right orders -------------------------

// Left button up. Anything that isn't a plain click (a mode, a held-key mode,
// a drag box, the minimap or panel, which act on button down) keeps the
// stock behaviour. A plain click only selects: what's under the cursor
// replaces the selection; Shift adds or removes own units; empty ground
// clears it.
bool ModernLeftUp(void* sg, int x, int y) {
    void* w = World();
    if (!w || F<short>(sg, kInputOff) == 1 || F<short>(sg, kCheatPlace) == 1 || F<short>(sg, kCaptured) != kNoObject ||
        F<short>(sg, kClickPending) == 0 || F<short>(sg, kTabHeld) == 1 || F<short>(sg, kCtrlTabHeld) == 1 ||
        F<short>(sg, kSpaceHeld) == 1 || F<short>(sg, kGHeld) == 1 || F<short>(sg, kCHeld) == 1 ||
        F<short>(sg, kAttackHeld) == 1 || F<short>(sg, kPgDnHeld) == 1 || F<short>(sg, kPgUpHeld) == 1 ||
        F<short>(w, kWorldTHeld) != 0 || ((short(__thiscall*)(void*))kIsDragBox)(sg) != 0)
        return false;
    ((Handler_t)kSceneLButtonUp)(sg, x, y);
    F<short>(sg, kMinimapDrag) = 0;
    F<int>(sg, kHudDrag) = 0;
    F<short>(sg, kClickPending) = 0;
    short rect[4];
    void* obj = PickAt(sg, x, y, rect);
    bool shift = F<short>(sg, kShiftHeld) == 1;
    if (!obj) {
        if (!shift) ClearSelection();
    } else if (shift) {
        if (IsCreature(obj) && Mine(obj)) WorldClick(sg, rect, x, y, 1);  // stock add / toggle
    } else {
        ClearSelection();  // with nothing selected the stock click only selects
        WorldClick(sg, rect, x, y, 0);
    }
    return true;
}

bool OnMinimap(int x, int y) {
    void* radar = (void*)kRadar;
    return ((short(__thiscall*)(void*))kRadarShown)(radar) == 1 &&
           ((short(__thiscall*)(void*, int, int))kRadarHit)(radar, x, y) == 1;
}

// Right button up: in a build/plant/repair mode, leave the mode but keep the
// selection; otherwise the order the cursor shows (move / attack / gather...),
// builders onto an own unfinished or damaged building repair it, and units
// onto the buildings they can enter enter them. The minimap keeps its stock
// right-click (move there).
short __thiscall HookRButtonUp(void* sg, int x, int y) {
    if (!g_opt.modernMouse || OnMinimap((short)x, (short)y)) return ((Handler_t)kRButtonUp)(sg, x, y);
    ((Handler_t)kSceneRButtonUp)(sg, x, y);
    void* w = World();
    if (!w || F<short>(sg, kInputOff) == 1) return 0;
    unsigned short cap = F<unsigned short>(sg, kCaptured);
    if (cap != (unsigned short)kNoObject) {
        void* obj = cap < (unsigned)F<int>(w, 0x14) ? F<void**>(w, 0x10)[cap] : nullptr;
        if (obj) {  // deselecting resets its mode (SetSelected(0)); then select it again
            ((void(__thiscall*)(void*, void*))kWorldDeselect)(w, obj);
            ((void(__thiscall*)(void*, void*))kWorldSelect)(w, obj);
        }
        return 1;
    }
    if (((short(__thiscall*)(void*))kWorldCommandable)(w) != 1) return 1;
    short rect[4];
    void* obj = PickAt(sg, x, y, rect);
    int n;
    void** sel = Selection(n);
    int kind = ((int(__thiscall*)(void*))kWorldSelKind)(w);
    if (obj && IsDevice(obj) && Mine(obj) && (Type(obj) == 0x34 || Type(obj) == 0x39) && kind >= 1 && kind <= 3) {
        WorldClick(sg, rect, x, y, 0);  // the stock "enter" order
        return 1;
    }
    if (g_opt.builderRepair && SelectedBuilders(sel, n) && RepairTarget(obj)) {
        SendBuildersToRepair(sel, n, obj);
        return 1;
    }
    int wx = ((int(__thiscall*)(void*, int))kToWorldX)(sg, (short)x);
    int wy = ((int(__thiscall*)(void*, int))kToWorldY)(sg, (short)y);
    typedef short(__thiscall * OrderAt_t)(void*, short*, int, int, int, int, int);
    ((OrderAt_t)kWorldOrderAt)(w, rect, (short)x, (short)y, F<short>(sg, game::SGameZoomedOut), (short)wx, (short)wy);
    return 1;
}

// Shift+drag adds the units in the box to the selection (both schemes). The
// stock game turns off the drag box while Shift is held (0x4804c0), so a
// Shift+drag was just a Shift+click at the release point.
bool ShiftBox(void* sg, int x, int y) {
    void* w = World();
    if (!w || F<short>(sg, kShiftHeld) != 1 || F<short>(sg, kClickPending) == 0 || F<short>(sg, kInputOff) == 1 ||
        F<short>(sg, kCheatPlace) == 1 || F<short>(sg, kCaptured) != kNoObject || F<short>(sg, kTabHeld) == 1 ||
        F<short>(sg, kCtrlTabHeld) == 1 || F<short>(sg, kSpaceHeld) == 1 || F<short>(sg, kGHeld) == 1 ||
        F<short>(sg, kCHeld) == 1)
        return false;
    int x0 = F<short>(sg, kDragX0), y0 = F<short>(sg, kDragY0);
    int x1 = (short)((int(__thiscall*)(void*, int))kToWorldX)(sg, (short)x);
    int y1 = (short)((int(__thiscall*)(void*, int))kToWorldY)(sg, (short)y);
    if (abs(x1 - x0) <= 32 && abs(y1 - y0) <= 32) return false;  // a click (the stock 32 px test)
    ((Handler_t)kSceneLButtonUp)(sg, x, y);
    F<short>(sg, kMinimapDrag) = 0;
    F<int>(sg, kHudDrag) = 0;
    F<short>(sg, kClickPending) = 0;
    short rect[4] = {(short)std::min(x0, x1), (short)std::min(y0, y1), (short)std::max(x0, x1), (short)std::max(y0, y1)};
    int ground = F<short>(sg, kPgUpHeld) == 1 ? 0 : 1, air = F<short>(sg, kPgDnHeld) == 1 ? 0 : 1;
    short got = ((short(__thiscall*)(void*, short*, int, int, int))kWorldBoxSelect)(w, rect, ground, air, 1);
    F<short>(sg, kDragX0) = F<short>(sg, kDragY0) = F<short>(sg, kDragX1) = F<short>(sg, kDragY1) = 0;
    g_log("controls: Shift+drag added (%s)", got ? "units" : "nothing in the box");
    return true;
}

short __thiscall HookLButtonUp(void* sg, int x, int y) {
    if (ShiftBox(sg, x, y)) return 1;
    if (g_opt.modernMouse) {
        if (ModernLeftUp(sg, x, y)) return 1;
        return ((Handler_t)kLButtonUp)(sg, x, y);
    }
    if (g_opt.builderRepair && PlainClick(sg)) {
        short rect[4];
        void* target = PickAt(sg, x, y, rect);
        int n;
        void** sel = Selection(n);
        if (SelectedBuilders(sel, n) && RepairTarget(target)) {
            // The stock handler's own start (scene widgets, drag state), then the order.
            ((Handler_t)kSceneLButtonUp)(sg, x, y);
            F<short>(sg, kMinimapDrag) = 0;
            F<int>(sg, kHudDrag) = 0;
            F<short>(sg, kClickPending) = 0;
            SendBuildersToRepair(sel, n, target);
            return 1;
        }
    }
    return ((Handler_t)kLButtonUp)(sg, x, y);
}

// --- planting and building into fog (re/fogbuild.md) -----------------------

// Placing a building on fogged ground already works (the Shaman walks there),
// only the ghost isn't drawn there. Pods are refused in fog by the click's
// check (0x442b10), though planting itself has no such check: a fogged click
// sends the plant order and the Shaman walks there and plants (or does nothing
// if the spot turns out unsuitable, as with buildings).

bool SeenByMe(int wx, int wy) {
    void* me = *(void**)kLocalPlayerPtr;
    if (!me) return true;
    return ((short(__thiscall*)(void*, int, int))kPlayerTileVisible)(me, wx >> 5, wy >> 5) != 0;
}

// A click on the battlefield, not on the info panel or the minimap (the game
// gives those to the Shaman first too).
bool OnBattlefield(int sx, int sy) {
    if (((void*(__thiscall*)(void*, int, int))kPanelWidgetAt)((void*)kPanel, sx, sy)) return false;
    if (((short(__thiscall*)(void*, int, int))kWorldInPanel)(World(), sx, sy) == 1) return false;
    void* radar = (void*)kRadar;
    if (((short(__thiscall*)(void*))kRadarShown)(radar) == 1 &&
        ((short(__thiscall*)(void*, int, int))kRadarHit)(radar, sx, sy) == 1)
        return false;
    return true;
}

struct Pt {
    uint32_t vtbl;
    short x, y;
};
struct PtList {
    uint32_t vtbl;
    Pt* data;
    uint32_t count, cap;
};

typedef short(__thiscall* ShamanClick_t)(void* sh, int sx, int sy, int wx, int wy, int zoom);

short __thiscall HookShamanDown(void* sh, int sx, int sy, int wx, int wy, int zoom) {
    if (g_opt.buildInFog && F<int>(sh, kShamanState) == 5 && ((short)wx || (short)wy) && !SeenByMe((short)wx, (short)wy) &&
        OnBattlefield((short)sx, (short)sy)) {
        static Pt pt;
        static PtList path;
        pt = {0x5031c4, (short)wx, (short)wy};
        path = {0x5033d4, &pt, 1, 1};
        int podType = F<int>(sh, kShamanBuildType);
        ((void(__cdecl*)())kClickSound)();
        void* cmd = *(void**)kCommands;
        ((void(__thiscall*)(void*, int, int, PtList*))(*(void***)cmd)[kCmdPlant / 4])(cmd, F<short>(sh, kId), podType, &path);
        ((void(__thiscall*)(void*))kResetMode)(sh);
        g_log("controls: plant pod %#x in fog at %d,%d", podType, (short)wx, (short)wy);
        return 1;
    }
    return ((ShamanClick_t)kShamanDown)(sh, sx, sy, wx, wy, zoom);
}

// Over fog the game shows a plain arrow in build/plant mode; show the move
// cursor instead: a click there sends the Shaman.
short __thiscall HookShamanCursor(void* sh, int sx, int sy, int wx, int wy, int a5) {
    int state = F<int>(sh, kShamanState);
    if (g_opt.buildInFog && (state == 3 || state == 5) && !SeenByMe((short)wx, (short)wy) &&
        OnBattlefield((short)sx, (short)sy))
        return 1;
    return ((ShamanClick_t)kShamanCursor)(sh, sx, sy, wx, wy, a5);
}

bool PatchSlot(uintptr_t slot, uintptr_t expect, void* hook) {
    if (*(uintptr_t*)slot != expect) return false;
    DWORD old;
    if (!VirtualProtect((void*)slot, 4, PAGE_READWRITE, &old)) return false;
    *(void**)slot = hook;
    VirtualProtect((void*)slot, 4, old, &old);
    return true;
}

}  // namespace

void Install(void (*log)(const char*, ...)) {
    g_log = log;
    bool ok = PatchSlot(kSlotKeyDown, kKeyDown, (void*)&HookKeyDown) &&
              PatchSlot(kSlotKeyUp, kKeyUp, (void*)&HookKeyUp) &&
              PatchSlot(kSlotLButtonUp, kLButtonUp, (void*)&HookLButtonUp) &&
              PatchSlot(kSlotRButtonUp, kRButtonUp, (void*)&HookRButtonUp) &&
              PatchSlot(kSlotShamanDown, kShamanDown, (void*)&HookShamanDown) &&
              PatchSlot(kSlotShamanCursor, kShamanCursor, (void*)&HookShamanCursor);
    log("controls: %s", ok ? "installed" : "FAILED (unexpected SGame vtable)");
}

void SetOptions(const Options& o) { g_opt = o; }

}  // namespace controls
