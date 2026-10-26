#include "controls.h"

#include <windows.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <algorithm>
#include <vector>

#include "game.h"
#include "pathing.h"

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
constexpr uintptr_t kSlotDoubleClick = game::VtblSGame + 0x30, kDoubleClick = 0x47eea0;  // (x, y): replaces the 2nd LDown

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
constexpr uintptr_t kCmdUpgrade = 0x84;   // Commands vtable: (short builderId, short deviceId)
constexpr uintptr_t kCmdWarpTo = 0x54;    // Commands vtable: (short warpId, int x, int y)
constexpr uintptr_t kCmdSetRally = 0x4c;  // Commands vtable: (short labId, PtList* path, IPixelC* loopPt)
constexpr uintptr_t kComplete = 0x2c;     // ADevice short: 1 = construction finished

template <typename T>
T& F(void* p, uintptr_t off) {
    return *(T*)((char*)p + off);
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

void* World() { return *(void**)kWorld; }
void* Cmd() { return *(void**)kCommands; }
void* Slot(uintptr_t off) { return (*(void***)Cmd())[off / 4]; }
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

// Group build: two or more own builders and nothing else selected. The first
// one leads: the panel shows its buttons and lists, and its modes are used.
void* GroupLead() {
    if (!g_opt.groupBuild) return nullptr;
    int n;
    void** sel = Selection(n);
    if (n < 2 || !sel) return nullptr;
    for (int i = 0; i < n; ++i)
        if (!sel[i] || !IsBuilder(sel[i]) || !Mine(sel[i])) return nullptr;
    return sel[0];
}

bool InGroup(void* sh) {
    if (!GroupLead()) return false;
    int n;
    void** sel = Selection(n);
    for (int i = 0; i < n; ++i)
        if (sel[i] == sh) return true;
    return false;
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

// The one selected own Shaman (or a group's lead), if its build list (state 2)
// or pod list (4) is open.
void* ShamanInState(int state) {
    int n;
    void** sel = Selection(n);
    if (!sel || n < 1) return nullptr;
    void* s = n == 1 ? sel[0] : GroupLead();
    if (!s) return nullptr;
    if (!IsBuilder(s) || !Mine(s) || F<int>(s, kShamanState) != state) return nullptr;
    return s;
}

// Places `type` at the cursor, as clicking its build-list button does
// (0x442613..0x442673).
void StartBuilding(void* shaman, int type) {
    if (F<short>(shaman, kCaptureFlag) == 0) ((void(__thiscall*)(void*, int))kCapture)(shaman, 1);
    F<int>(shaman, kShamanState) = 3;
    F<int>(shaman, kShamanBuildType) = type;
    // The ghost's world position (the button path takes it from its click;
    // mouse moves update it from then on).
    void* sg = (void*)((uintptr_t*)game::Scenes)[0];
    F<short>(shaman, kShamanPlaceX) = (short)((int(__thiscall*)(void*, int))kToWorldX)(sg, game::at<short>(game::CursorX));
    F<short>(shaman, kShamanPlaceY) = (short)((int(__thiscall*)(void*, int))kToWorldY)(sg, game::at<short>(game::CursorY));
    ((void(__thiscall*)(void*))kStartPlacement)(shaman);
}

// The build list's buttons as keys; the list shows the types the player can
// build (0x442fbb).
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
        StartBuilding(shaman, 0x2c + i);
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

// Delete with only own buildings and units selected destroys them (pathing::
// DestroyBuilding: at once, or as a command in multiplayer). Otherwise, and in
// waypoint mode (Tab), the stock Delete: the route under the cursor.
bool DeleteSelected(void* sg) {
    int n;
    void** sel = Selection(n);
    if (!sel || n < 1 || F<short>(sg, kTabHeld) == 1 || F<short>(sg, kInputOff) == 1) return false;
    for (int i = 0; i < n; ++i)
        if (!sel[i] || !(IsDevice(sel[i]) || IsCreature(sel[i])) || !Mine(sel[i])) return false;
    std::vector<short> ids;
    for (int i = 0; i < n; ++i) ids.push_back(F<short>(sel[i], kId));
    int done = 0;
    for (short id : ids) done += pathing::DestroyBuilding(id);
    if (!done) {
        g_log("controls: Delete: not every player has this WarBreeds HD; nothing destroyed");
        return false;
    }
    ((void(__cdecl*)())kClickSound)();
    g_log("controls: Delete: %d of your own building(s) and unit(s) destroyed", done);
    return true;
}

short __thiscall HookKeyDown(void* sg, int vk, int mods, int ascii) {
    int k = vk & 0xff, m = mods & 0xff;
    if (k == VK_DELETE && m == 0 && DeleteSelected(sg)) return 1;
    if (g_opt.quickBuild && k >= 'A' && k <= 'Z' && (m == 0 || m == 4) && QuickBuild(k, m == 4)) return 1;
    if (g_opt.quickBuild && k >= '1' && k <= '3' && m == 0 && QuickPlant(k - '0')) return 1;
    // B/P/R/U for a group of builders: the stock keys need exactly one Shaman
    // (SelectionKind 3), then set the mode on the first selected (0x452b60).
    if (m == 0 || m == 4) {
        int mode = k == 'B' ? 2 : k == 'P' ? 4 : k == 'R' ? 6 : k == 'U' ? 7 : 0;
        if (void* lead = mode ? GroupLead() : nullptr) {
            ((void(__thiscall*)(void*, int))game::ShamanSetMode)(lead, mode);
            g_log("controls: group of builders, key %c -> mode %d on the first", k, mode);
            return 1;
        }
    }
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
    // One own Gene Lab or Gene Lab+ selected: the right-click sets its rally point
    // (the stock order for a building does nothing; Classic's left-click and Tab
    // set it through Cmd+0x4c SetRallyPoint(labId, path, loop point)).
    if (n == 1 && sel[0] && Mine(sel[0]) && (Type(sel[0]) == 0x2d || Type(sel[0]) == 0x35) &&
        F<short>(sel[0], kComplete) == 1) {
        Pt pt{0x5031c4, (short)((int(__thiscall*)(void*, int))kToWorldX)(sg, (short)x),
              (short)((int(__thiscall*)(void*, int))kToWorldY)(sg, (short)y)};
        PtList path{0x5033d4, &pt, 1, 1};
        Pt loop{0x5031c4, -1, -1};  // no patrol loop
        ((void(__thiscall*)(void*, int, PtList*, Pt*))Slot(kCmdSetRally))(Cmd(), F<short>(sel[0], kId), &path, &loop);
        ((void(__cdecl*)())kClickSound)();
        g_log("controls: rally point of Gene Lab %d set to %d,%d (right-click)", F<short>(sel[0], kId), pt.x, pt.y);
        return 1;
    }
    // One own working Warp selected: the right-click sets its target (as its panel's
    // button and a click do: Cmd+0x54 WarpTo(warpId, x, y); the Warp keeps its own
    // position for a target too near, AWarp::SetTarget 0x457ce0).
    if (n == 1 && sel[0] && Mine(sel[0]) && Type(sel[0]) == 0x39 && F<short>(sel[0], kComplete) == 1 &&
        F<short>(sel[0], kSwitchedOn) == 1 && F<short>(sel[0], kUnpowered) == 0) {
        int wx = ((int(__thiscall*)(void*, int))kToWorldX)(sg, (short)x);
        int wy = ((int(__thiscall*)(void*, int))kToWorldY)(sg, (short)y);
        ((void(__thiscall*)(void*, int, int, int))Slot(kCmdWarpTo))(Cmd(), F<short>(sel[0], kId), wx, wy);
        ((void(__cdecl*)())kClickSound)();
        g_log("controls: Warp %d target set to %d,%d (right-click)", F<short>(sel[0], kId), wx, wy);
        return 1;
    }
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

// Double-click on an own unit: every own unit of that type on screen is selected
// (both schemes). The double-click replaces the second button-down; the
// button-up after it would select the clicked unit alone again, so it is skipped.
bool g_skipNextUp;

// --- Listener: cloaked enemies on the minimap --------------------------------
// The Shadow (0x1c) and Reaver (0x1e) cloak (ACreatureBody +0x40 = 0; vtbl+0x60
// IsCloaked), and the minimap colour (World 0x4340c0(id), its one call 0x433c8c)
// is then 0: terrain. Within 480 px of one of the local player's working
// Listeners (complete, switched on, powered) such a unit blinks on the minimap
// in the colour it would have uncloaked. Display only: nothing on the battlefield.
constexpr uintptr_t kMinimapColourCall = 0x433c8c, kMinimapColour = 0x4340c0;
constexpr uintptr_t kGameTurn = 0x4708b0;  // cdecl int(): the game turn
constexpr int kListenerReach = 480;

struct Listener {
    short x, y;
};
Listener g_listeners[32];
int g_listenerCount, g_listenerTurn = -1;

void FindListeners(void* w, int turn) {
    g_listenerTurn = turn;
    g_listenerCount = 0;
    void** things = F<void**>(w, 0x10);
    int count = F<int>(w, 0x14);
    for (int i = 0; i < count && g_listenerCount < 32; ++i) {
        void* t = things[i];
        if (!t || Type(t) != 0x33 || !Mine(t)) continue;
        if (F<short>(t, 0x2c) != 1 || F<short>(t, kSwitchedOn) != 1 || F<short>(t, kUnpowered) != 0) continue;
        g_listeners[g_listenerCount++] = {F<short>(t, 0x14), F<short>(t, 0x16)};
    }
}

uint8_t __fastcall HookMinimapColour(void* w, void*, int id) {
    typedef uint8_t(__thiscall * Colour_t)(void*, int);
    uint8_t c = ((Colour_t)kMinimapColour)(w, id);
    if (c || !g_opt.listenerDetect) return c;
    unsigned short i = (unsigned short)id;
    if (i >= (unsigned)F<int>(w, 0x14)) return c;
    char* t = F<char**>(w, 0x10)[i];
    if (!t || (Type(t) != 0x1c && Type(t) != 0x1e) || Mine(t)) return c;
    if (((short(__thiscall*)(void*))(*(void***)t)[0x60 / 4])(t) != 1) return c;  // not cloaked: fogged, say
    int turn = ((int (*)())kGameTurn)();
    if (turn != g_listenerTurn) FindListeners(w, turn);
    if (!(turn >> 3 & 1)) return c;  // the off half of the blink
    for (int k = 0; k < g_listenerCount; ++k) {
        int dx = F<short>(t, 0x14) - g_listeners[k].x, dy = F<short>(t, 0x16) - g_listeners[k].y;
        if (dx * dx + dy * dy > kListenerReach * kListenerReach) continue;
        uint8_t timer = F<uint8_t>(t, 0x40);  // the colour it would have uncloaked
        F<uint8_t>(t, 0x40) = 1;
        c = ((Colour_t)kMinimapColour)(w, id);
        F<uint8_t>(t, 0x40) = timer;
        return c;
    }
    return c;
}

short __thiscall HookDoubleClick(void* sg, int x, int y) {
    void* w = World();
    short rect[4];
    bool world = w && F<short>(sg, kInputOff) != 1 && !OnMinimap((short)x, (short)y) &&
                 ((short(__thiscall*)(void*, int, int))kWorldInPanel)(w, (short)x, (short)y) != 1;
    void* obj = world ? PickAt(sg, x, y, rect) : nullptr;
    if (!obj || !IsCreature(obj) || !Mine(obj)) return ((Handler_t)kDoubleClick)(sg, x, y);
    int type = Type(obj), picked = 0;
    ClearSelection();
    void** things = F<void**>(w, 0x10);
    int count = F<int>(w, 0x14);
    for (int i = 0; i < count; ++i) {
        void* t = things[i];
        if (!t || Type(t) != type || !Mine(t)) continue;
        short tx = F<short>(t, 0x14), ty = F<short>(t, 0x16);
        if (tx < rect[0] || ty < rect[1] || tx > rect[2] || ty > rect[3]) continue;  // the visible world rect
        ((void(__thiscall*)(void*, void*))kWorldSelect)(w, t);
        ++picked;
    }
    ((void(__thiscall*)(void*, int))kWorldAck)(w, 0);
    g_skipNextUp = true;
    g_log("controls: double-click selected %d own units of type %#x on screen", picked, type);
    return 1;
}

short __thiscall HookLButtonUp(void* sg, int x, int y) {
    if (g_skipNextUp) {  // the button-up of a double-click that selected a unit type
        g_skipNextUp = false;
        ((Handler_t)kSceneLButtonUp)(sg, x, y);
        F<short>(sg, kClickPending) = 0;
        return 1;
    }
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


typedef short(__thiscall* ShamanClick_t)(void* sh, int sx, int sy, int wx, int wy, int zoom);

// --- group build: several builders build one building -----------------------

// The info panel, its clicks and the placement cursor treat a group of
// builders as its lead alone: for the duration of those calls the selection
// count reads 1 (the lead is sel[0]). Nothing in them changes the selection.
// The panel's second page (World+0x6c, the stance page) keeps the group view.
void* g_shownLead;  // the lead while its panel is drawn for the group
int g_groupCount;

struct AsLead {
    void* w = nullptr;
    int n = 0;
    explicit AsLead(bool on) {
        void* lead = on ? GroupLead() : nullptr;
        if (!lead) return;
        w = World();
        n = F<int>(w, game::WorldSelCount);
        F<int>(w, game::WorldSelCount) = 1;
        g_shownLead = lead;
        g_groupCount = n;
    }
    ~AsLead() {
        if (!w) return;
        F<int>(w, game::WorldSelCount) = n;
        g_shownLead = nullptr;
    }
};

bool StancePage(void* w) { return F<short>(w, game::WorldPanelPage) == 1; }

void __thiscall HookDrawPanel(void* w, int camX, int camY) {
    AsLead lead(!StancePage(w));
    ((void(__thiscall*)(void*, int, int))game::WorldDrawInfoPanel)(w, camX, camY);
}

short __thiscall HookPanelDown(void* w, int sx, int sy) {
    AsLead lead(!StancePage(w));
    return ((short(__thiscall*)(void*, int, int))game::WorldPanelClickDown)(w, sx, sy);
}

// While the lead places a building, the ghost is the pointer (as for one Shaman).
void __thiscall HookUpdateCursor(void* sg) {
    void* lead = GroupLead();
    AsLead as(lead && F<int>(lead, kShamanState) == 3);
    ((void(__thiscall*)(void*))game::SGameUpdateCursor)(sg);
}

// The lead's panel with no mode open is titled like the stock group panel,
// "Squad (n)" ("Squad g (n)" when all share control group g).
typedef short(__thiscall* ShamanPanel_t)(void* sh, int x, int y, int camX, int camY);
short __thiscall HookShamanPanel(void* sh, int x, int y, int camX, int camY) {
    short r = ((ShamanPanel_t)game::ShamanPanel)(sh, x, y, camX, camY);
    if (sh == g_shownLead && F<int>(sh, kShamanState) == 0) {
        const char* squad = **(const char***)(*(char**)game::StringTable + game::StrSquadOffset);
        void** sel = F<void**>(World(), 0x80);
        uint8_t group = F<uint8_t>(sel[0], kGroup);
        for (int i = 1; i < g_groupCount && group != 0xff; ++i)
            if (F<uint8_t>(sel[i], kGroup) != group) group = 0xff;
        char title[64];
        if (group != 0xff)
            snprintf(title, sizeof title, "%s %d (%d)", squad, group, g_groupCount);
        else
            snprintf(title, sizeof title, "%s (%d)", squad, g_groupCount);
        ((void(__thiscall*)(void*, const char*))game::PanelSetTitle)((void*)game::InfoPanel, title);
    }
    return r;
}

struct IdList {
    uint32_t vtbl;
    short* data;
    uint32_t count, cap;
};


void MoveTo(void* unit, short x, short y) {
    Pt pt{0x5031c4, x, y};
    PtList path{0x5033d4, &pt, 1, 1};
    short id = F<short>(unit, kId);
    IdList ids{0x5030dc, &id, 1, 1};
    ((void(__thiscall*)(void*, IdList*, PtList*, int))Slot(game::CmdMove))(Cmd(), &ids, &path, 0);
}

void Repair(void* builder, short target) {
    ((void(__thiscall*)(void*, int, int))Slot(game::CmdRepair))(Cmd(), F<short>(builder, kId), target);
}

// The selected builder nearest to a world point: it gets the order.
void* NearestBuilder(int x, int y) {
    int n;
    void** sel = Selection(n);
    void* best = nullptr;
    double bestD = 1e18;
    for (int i = 0; i < n; ++i) {
        double dx = F<short>(sel[i], 0x14) - x, dy = F<short>(sel[i], 0x16) - y, d = dx * dx + dy * dy;
        if (d < bestD) {
            bestD = d;
            best = sel[i];
        }
    }
    return best;
}

bool Alive(short id, void* p) {
    void* w = World();
    return w && (unsigned short)id < (unsigned)F<int>(w, 0x14) && F<void**>(w, 0x10)[(unsigned short)id] == p &&
           Mine(p) && IsBuilder(p);
}

// A placed building: the founder builds it; the helpers walk to just outside
// its footprint and get the Repair order (which finishes construction) as
// soon as it exists. They can't be sent earlier: there is no building to
// repair until the founder has worked 0x40 ticks on site, and a second Build
// order on the same spot would only fail when it finds the first building.
struct Helper {
    short id;
    void* p;
};
struct GroupBuild {
    short founderId;
    void* founder;
    int type;
    short x, y, reach;  // reach: how far from the site a helper may wait or walk
    DWORD start;
    bool armed;  // the founder's Build order has taken effect (multiplayer: a turn later)
    Helper helpers[16];
    int helperCount;
};
GroupBuild g_builds[8];
int g_buildCount;

void StartGroupBuild(void* founder, int type, short x, short y) {
    int n;
    void** sel = Selection(n);
    // Improved pathfinding: every builder gets the same Build order. They all walk
    // to the site, work at their own spots round it with the build animation, and
    // start it together; the game turn (pathing.cpp) pools their work and has the
    // others repair (build) it once it stands.
    if (pathing::Improved()) {
        int sent = 0;
        for (int i = 0; i < n; ++i)
            if (sel[i] != founder && IsBuilder(sel[i]) && Mine(sel[i])) {
                ((void(__thiscall*)(void*, int, int, int, int))Slot(game::CmdBuild))(Cmd(), F<short>(sel[i], kId), type, x,
                                                                                    y);
                ++sent;
            }
        g_log("controls: group build together: type %#x at %d,%d by builder %d and %d more", type, x, y,
              F<short>(founder, kId), sent);
        return;
    }
    if (g_buildCount == 8) g_builds[0] = g_builds[--g_buildCount];  // drop the oldest
    GroupBuild& b = g_builds[g_buildCount++];
    b = GroupBuild{};
    b.founder = founder;
    b.founderId = F<short>(founder, kId);
    b.type = type;
    b.x = x;
    b.y = y;
    b.start = GetTickCount();
    // Helpers wait on the square just outside the footprint's 32 px tiles, on
    // their own side of it: the founder can only create the building on free
    // ground (anything else in it makes it step aside and try again).
    int half = game::at<short>(game::FootprintTable + type * 2) / 2;
    int edge = half + 56;
    b.reach = (short)(edge + 160);
    for (int i = 0; i < n && b.helperCount < 16; ++i) {
        void* h = sel[i];
        if (h == founder) continue;
        double dx = F<short>(h, 0x14) - x, dy = F<short>(h, 0x16) - y, m = std::max(fabs(dx), fabs(dy));
        if (m < 1) dx = 1, dy = 0, m = 1;
        short gx = (short)std::max(16, (int)(x + dx * edge / m)), gy = (short)std::max(16, (int)(y + dy * edge / m));
        MoveTo(h, gx, gy);
        b.helpers[b.helperCount++] = {F<short>(h, kId), h};
    }
    g_log("controls: group build: %s (type %#x, footprint %d) at %d,%d by builder %d (nearest), %d helper(s) heading "
          "there",
          kBuildKeys[std::min(15, std::max(0, type - 0x2c))].name, type, half * 2, x, y, b.founderId, b.helperCount);
}

// The lead placing a building: the stock click (mode 3, 0x441d2e) with the
// nearest selected builder as the one who builds it, then the others join.
// An invalid spot falls through to the stock handler, which keeps the mode.
bool GroupPlace(void* lead) {
    short x = F<short>(lead, kShamanPlaceX), y = F<short>(lead, kShamanPlaceY);
    int type = F<int>(lead, kShamanBuildType);
    short player = F<short>(F<void*>(lead, kOwner), 4);
    typedef int(__thiscall * CanPlace_t)(void*, int, int, int, int, int, int, int);
    if (((CanPlace_t)game::WorldCanPlace)(World(), player, x, y, type, 1, 0, 0) != 0) return false;
    void* founder = NearestBuilder(x, y);
    ((void(__thiscall*)(void*))game::ButtonRelease)((void*)game::BuildButton);
    ((void(__thiscall*)(void*))game::ShamanRemoveGhost)(lead);
    ((void(__cdecl*)())kClickSound)();
    ((void(__thiscall*)(void*))kResetMode)(lead);
    ((void(__thiscall*)(void*, int, int, int, int))Slot(game::CmdBuild))(Cmd(), F<short>(founder, kId), type, x, y);
    StartGroupBuild(founder, type, x, y);
    return true;
}

// The lead planting a pod: the nearest selected builder plants it (a pod is
// one quick action; nothing to join, and one pod each would cost each).
bool GroupPlant(void* lead, short wx, short wy) {
    if (!wx && !wy) return false;
    if (SeenByMe(wx, wy) ? ((short(__thiscall*)(void*, int, int))game::ShamanCanPlantAt)(lead, wx, wy) != 1
                         : !g_opt.buildInFog)
        return false;
    void* planter = NearestBuilder(wx, wy);
    Pt pt{0x5031c4, wx, wy};
    PtList path{0x5033d4, &pt, 1, 1};
    int podType = F<int>(lead, kShamanBuildType);
    ((void(__cdecl*)())kClickSound)();
    ((void(__thiscall*)(void*, int, int, PtList*))Slot(kCmdPlant))(Cmd(), F<short>(planter, kId), podType, &path);
    ((void(__thiscall*)(void*))kResetMode)(lead);
    g_log("controls: group plant: pod %#x at %d,%d by builder %d (nearest)", podType, wx, wy, F<short>(planter, kId));
    return true;
}

// Is a helper still free to join: idle, or walking to somewhere near the site
// (our move, or the stock "clear the site" nudge)? Anything else is a new
// order from the player.
bool FreeToJoin(const GroupBuild& b, void* h) {
    int act = F<int>(h, game::ThingActivity);
    if (act == 0x28) return true;
    if (act != 0x29 || F<uint32_t>(h, game::ThingPathCount) == 0) return false;
    Pt* goal = F<Pt*>(h, game::ThingPathData);
    return abs(goal->x - b.x) <= b.reach && abs(goal->y - b.y) <= b.reach;
}

void JoinBuilding(const GroupBuild& b, short building) {
    char ids[128] = "";
    int sent = 0, len = 0;
    for (int i = 0; i < b.helperCount; ++i) {
        const Helper& h = b.helpers[i];
        if (!Alive(h.id, h.p)) continue;
        if (!FreeToJoin(b, h.p)) {
            g_log("controls: group build: builder %d has a new order (activity %#x), not joining", h.id,
                  F<int>(h.p, game::ThingActivity));
            continue;
        }
        Repair(h.p, building);
        ++sent;
        if (len < (int)sizeof ids - 8) len += snprintf(ids + len, sizeof ids - len, " %d", h.id);
    }
    g_log("controls: group build: building %d (type %#x) is up; %d helper(s) sent to build it:%s", building, b.type, sent,
          ids);
}

// Returns false when the group build is finished or dropped.
bool UpdateGroupBuild(GroupBuild& b) {
    DWORD age = GetTickCount() - b.start;
    if (!Alive(b.founderId, b.founder)) {
        g_log("controls: group build: builder %d is gone, helpers stay", b.founderId);
        return false;
    }
    int act = F<int>(b.founder, game::ThingActivity);
    if (!b.armed) {
        if (act == 0x2e && F<short>(b.founder, game::ShamanWalkX) == b.x && F<short>(b.founder, game::ShamanWalkY) == b.y) {
            b.armed = true;
        } else if (age > 5000) {
            g_log("controls: group build: the build order never took effect (activity %#x)", act);
            return false;
        }
        return true;
    }
    void* w = World();
    unsigned short id = F<unsigned short>(b.founder, game::ShamanWorkTarget);
    void* bld = id != 0xc01 && id < (unsigned)F<int>(w, 0x14) ? F<void**>(w, 0x10)[id] : nullptr;
    if (bld && IsDevice(bld) && Mine(bld) && Type(bld) == b.type) {
        JoinBuilding(b, (short)id);
        return false;
    }
    // Still on it: building, or idle for a moment after a failed attempt (the
    // site wasn't free: +0x1b4 = 1, it steps aside and orders itself the same
    // Build again, up to 4 tries; work tick 0x4436b0, retry 0x443f10).
    bool onIt = act == 0x2e ? F<short>(b.founder, game::ShamanWalkX) == b.x && F<short>(b.founder, game::ShamanWalkY) == b.y
                            : act == 0x28 && F<short>(b.founder, game::ShamanBuildRetry) == 1;
    if (!onIt) {
        g_log("controls: group build: builder %d stopped (activity %#x) before the building stood; helpers stay",
              b.founderId, act);
        return false;
    }
    if (age > 300000) {
        g_log("controls: group build: gave up waiting for the building");
        return false;
    }
    return true;
}

// The panel's cancel while the lead works (build/plant/repair...): the stock
// click-up stops the lead (Cmd+0x2c); a group's other builders stop with it.
typedef short(__thiscall* ShamanUp_t)(void* sh, int sx, int sy, int wx, int wy);
short __thiscall HookShamanUp(void* sh, int sx, int sy, int wx, int wy) {
    bool cancel = InGroup(sh) && ((short(__thiscall*)(void*))game::ShamanWorking)(sh) == 1 &&
                  game::at<short>(game::CancelButton + 0x16) == 1 &&
                  ((void*(__thiscall*)(void*, int, int))kPanelWidgetAt)((void*)kPanel, (short)sx, (short)sy) ==
                      (void*)game::CancelButton;
    short r = ((ShamanUp_t)game::ShamanClickUp)(sh, sx, sy, wx, wy);
    if (cancel) {
        int n;
        void** sel = Selection(n);
        for (int i = 0; i < n; ++i)
            if (sel[i] != sh) ((void(__thiscall*)(void*, int))Slot(game::CmdStop))(Cmd(), F<short>(sel[i], kId));
        g_log("controls: group cancel: %d builder(s) stopped", n);
    }
    return r;
}

short __thiscall HookShamanDown(void* sh, int sx, int sy, int wx, int wy, int zoom) {
    int state = F<int>(sh, kShamanState);
    if (state == 7 && InGroup(sh) && OnBattlefield((short)sx, (short)sy)) {
        // Upgrade mode: the stock order goes to the lead alone (AShaman::ClickDown
        // case 7, Cmd+0x84); every other selected builder gets it too, and each
        // steps the upgrade on (WorkTick case 0x38). Gene Lab, Relay, Psi Tower, Turret.
        short rect[4];
        void* target = PickAt((void*)((uintptr_t*)game::Scenes)[0], (short)sx, (short)sy, rect);
        short r = ((ShamanClick_t)kShamanDown)(sh, sx, sy, wx, wy, zoom);
        int type = target && IsDevice(target) ? Type(target) : -1;
        if (r == 1 && F<int>(sh, kShamanState) == 0 && (type == 0x2d || type == 0x2e || type == 0x2f || type == 0x31) &&
            Mine(target) && F<short>(target, kComplete) == 1) {
            int n;
            void** sel = Selection(n);
            int sent = 0;
            for (int i = 0; i < n; ++i)
                if (sel[i] != sh && IsBuilder(sel[i]) && Mine(sel[i])) {
                    ((void(__thiscall*)(void*, int, int))Slot(kCmdUpgrade))(Cmd(), F<short>(sel[i], kId),
                                                                            F<short>(target, kId));
                    ++sent;
                }
            g_log("controls: group upgrade: building %d (type %#x) by builder %d and %d more", F<short>(target, kId), type,
                  F<short>(sh, kId), sent);
        }
        return r;
    }
    if ((state == 3 || state == 5 || state == 6) && InGroup(sh) && OnBattlefield((short)sx, (short)sy)) {
        if (state == 3 && GroupPlace(sh)) return 1;
        if (state == 5 && GroupPlant(sh, (short)wx, (short)wy)) return 1;
        if (state == 6) {  // Repair mode: the lead's stock order, then every other builder too
            short rect[4];
            void* target = PickAt((void*)((uintptr_t*)game::Scenes)[0], (short)sx, (short)sy, rect);
            short r = ((ShamanClick_t)kShamanDown)(sh, sx, sy, wx, wy, zoom);
            if (r == 1 && F<int>(sh, kShamanState) == 0 && RepairTarget(target)) {
                int n;
                void** sel = Selection(n);
                for (int i = 0; i < n; ++i)
                    if (sel[i] != sh) Repair(sel[i], F<short>(target, kId));
                g_log("controls: group repair: %d builder(s) on building %d", n, F<short>(target, kId));
            }
            return r;
        }
    }
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

// Placement range (option): while a Refinery, Relay or Relay+ (or a turret or the
// Electric Tower, below) is being placed, its ring at the cursor, as the game draws
// the rings of the field buildings already built.
// Placing any building makes the game show the player's power fields
// (StartPlacement 0x442c90 sets World+0x06; the world tick fades World+0x2d13c
// from 0 to 8); SGame::DrawWorldLayer draws them by FUN_00448b90(World, camX, camY)
// at 0x47bb37, each device through its vtable+0x7c into FUN_00459200(ring, camX,
// camY, fade, type, selected): sparks running round a circle of the type's range
// (0x54de68[type]: Refinery 300 px, Relay 275, Relay+ 400; a Refinery collects
// every pod within it). A ring is 0x44 bytes: 4 x 16 bits (a set bit = a point
// shown), then its centre (+0x40/+0x42); FUN_004591a0(ring, x, y) sets it up.
constexpr uintptr_t kFieldsDrawCall = 0x47bb37, kFieldsDraw = 0x448b90;
constexpr uintptr_t kRingInit = 0x4591a0, kRingDraw = 0x459200, kWorldFieldFade = 0x2d13c;

// The own builder placing a building: the selected builder, or group build's lead.
void* Placer() {
    int n;
    void** sel = Selection(n);
    for (int i = 0; sel && i < n; ++i)
        if (sel[i] && IsBuilder(sel[i]) && Mine(sel[i]) && F<int>(sel[i], kShamanState) == 3) return sel[i];
    return nullptr;
}

// The turrets and the Electric Tower get a ring too. The ring draw takes its circle
// from a table with rows for the three field buildings only (0x54d170, short[3][64]
// = the quarter-circle table 0x505538 (100..2) x range / 100), so for these one row
// is lent for the draw and put back. Their radius (re/units.md): the distance they
// pick a target at by themselves (FUN_004501e0: a box of 2 x sight/64 tiles round
// their own tile; g_SightRange Turret 290, Cannon 330, E. Tower 256), as a circle:
// Turret 8.5 tiles (272 px), Plasma Cannon 10.5 (336 px); the E. Tower's zap
// reaches 226 px (FUN_004104a0, d^2 <= 51200). Turrets keep firing at a target up
// to 400 px (ATurret::vf08).
constexpr uintptr_t kRingRows = 0x54d170, kQuarterCircle = 0x505538;

int TurretRange(int type) {
    switch (type) {
    case 0x31: return 272;  // Turret
    case 0x37: return 336;  // Plasma Cannon
    case 0x32: return 226;  // Electric Tower
    default: return 0;
    }
}

void __fastcall HookFieldsDraw(void* world, void*, int camX, int camY) {
    ((void(__thiscall*)(void*, int, int))kFieldsDraw)(world, camX, camY);
    if (!g_opt.placementRange || !world) return;
    void* sh = Placer();
    if (!sh) return;
    int type = F<int>(sh, kShamanBuildType), range = TurretRange(type);
    if (type != 0x2c && type != 0x2e && type != 0x36 && !range) return;
    alignas(4) uint8_t ring[0x44];
    ((void*(__thiscall*)(void*, int, int))kRingInit)(ring, F<short>(sh, kShamanPlaceX), F<short>(sh, kShamanPlaceY));
    memset(ring, 0xff, 0x40);  // every point of the circle
    int fade = F<uint8_t>(world, kWorldFieldFade);  // grows in with the other rings
    short* row = (short*)kRingRows;  // the Refinery's row, lent to a turret
    short saved[64];
    if (range) {
        memcpy(saved, row, sizeof saved);
        for (int i = 0; i < 64; ++i) row[i] = (short)(((const short*)kQuarterCircle)[i] * range / 100);
    }
    ((void(__thiscall*)(void*, int, int, int, int, int))kRingDraw)(ring, camX, camY, fade ? fade : 8,
                                                                  range ? 0x2c : type, 0);
    if (range) memcpy(row, saved, sizeof saved);
}

// A 1998 crash: the player's memory of a building seen earlier (ADevicePlaceholder,
// drawn while its tile is fogged) refreshes itself from the building, by id (+0x28),
// whenever the tile is in view (vf0c 0x420060, every turn). If the building was
// destroyed out of sight and its id reused by a creature, pod or corpse, it reads
// that thing as a building; with an "upgrading" flag (+0x3a) set it divides by
// GetBuildTime/4 (0 for a pod or corpse): integer divide by zero at 0x42014c.
// The memory now refreshes only from a building; for anything else it stays as
// it is, as when the id is empty.
constexpr uintptr_t kSlotMemoryTick = 0x503fb0, kMemoryTick = 0x420060;

uint32_t __fastcall HookMemoryTick(void* mem, void*) {
    void* w = World();
    unsigned id = F<unsigned short>(mem, 0x28);
    void* t = w && id < (unsigned)F<int>(w, 0x14) ? F<void**>(w, 0x10)[id] : nullptr;
    if (t && (!IsDevice(t) || ((short(__thiscall*)(void*))(*(void***)t)[kVBuildTime / 4])(t) < 4)) {
        static int logged;
        if (logged++ < 5)
            g_log("controls: memory of a building at %d,%d points at thing %u (type %#x), no longer that building: "
                  "kept as it was",
                  F<short>(mem, 8), F<short>(mem, 0xa), id, Type(t));
        return 0;
    }
    return ((uint32_t(__thiscall*)(void*))kMemoryTick)(mem);
}

bool PatchSlot(uintptr_t slot, uintptr_t expect, void* hook) {
    if (*(uintptr_t*)slot != expect) return false;
    DWORD old;
    if (!VirtualProtect((void*)slot, 4, PAGE_READWRITE, &old)) return false;
    *(void**)slot = hook;
    VirtualProtect((void*)slot, 4, old, &old);
    return true;
}

// Point a `call rel32` at `site` (which must call `expect`) to `hook`, a
// function with the same convention.
bool PatchCall(uintptr_t site, uintptr_t expect, void* hook) {
    uint8_t* p = (uint8_t*)site;
    if (p[0] != 0xe8 || site + 5 + *(int32_t*)(p + 1) != expect) return false;
    DWORD old;
    if (!VirtualProtect(p + 1, 4, PAGE_EXECUTE_READWRITE, &old)) return false;
    *(int32_t*)(p + 1) = (int32_t)((uintptr_t)hook - (site + 5));
    VirtualProtect(p + 1, 4, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 5);
    return true;
}

}  // namespace

void Install(void (*log)(const char*, ...)) {
    g_log = log;
    bool ok = PatchSlot(kSlotKeyDown, kKeyDown, (void*)&HookKeyDown) &&
              PatchSlot(kSlotKeyUp, kKeyUp, (void*)&HookKeyUp) &&
              PatchSlot(kSlotLButtonUp, kLButtonUp, (void*)&HookLButtonUp) &&
              PatchSlot(kSlotRButtonUp, kRButtonUp, (void*)&HookRButtonUp) &&
              PatchSlot(kSlotDoubleClick, kDoubleClick, (void*)&HookDoubleClick) &&
              PatchSlot(kSlotShamanDown, kShamanDown, (void*)&HookShamanDown) &&
              PatchSlot(kSlotShamanCursor, kShamanCursor, (void*)&HookShamanCursor);
    log("controls: %s", ok ? "installed" : "FAILED (unexpected SGame vtable)");
    // Group build: the panel, its clicks and the placement cursor (see AsLead).
    bool group = PatchSlot(game::SlotShamanPanel, game::ShamanPanel, (void*)&HookShamanPanel) &&
                 PatchSlot(game::SlotShamanClickUp, game::ShamanClickUp, (void*)&HookShamanUp) &&
                 PatchCall(game::DrawInfoPanelCall, game::WorldDrawInfoPanel, (void*)&HookDrawPanel) &&
                 PatchCall(game::UpdateCursorCall, game::SGameUpdateCursor, (void*)&HookUpdateCursor);
    for (uintptr_t site : game::PanelClickDownCalls)
        group = group && PatchCall(site, game::WorldPanelClickDown, (void*)&HookPanelDown);
    log("controls: group build hooks %s", group ? "installed" : "FAILED (unexpected code)");
    bool range = PatchCall(kFieldsDrawCall, kFieldsDraw, (void*)&HookFieldsDraw);
    log("controls: placement range %s", range ? "installed" : "FAILED (unexpected code)");
    bool memory = PatchSlot(kSlotMemoryTick, kMemoryTick, (void*)&HookMemoryTick);
    log("controls: building memory guard %s", memory ? "installed" : "FAILED (unexpected vtable)");
    bool listener = PatchCall(kMinimapColourCall, kMinimapColour, (void*)&HookMinimapColour);
    log("controls: Listener minimap detection %s", listener ? "installed" : "FAILED (unexpected code)");
}

void SetOptions(const Options& o) { g_opt = o; }

bool DebugPlace(int type) {
    void* shaman = ShamanInState(2);
    if (!shaman) return false;
    StartBuilding(shaman, type);
    g_log("controls: placing type %#x (test, not checked for availability)", type);
    return true;
}

void Tick(void* sgame) {
    if (!sgame) {
        g_buildCount = 0;
        return;
    }
    for (int i = 0; i < g_buildCount;)
        if (UpdateGroupBuild(g_builds[i]))
            ++i;
        else
            g_builds[i] = g_builds[--g_buildCount];
}

}  // namespace controls
