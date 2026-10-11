#pragma once
// Mission controls added on top of the game's own (see re/hotkeys.md and
// re/mouse.md for the stock key and mouse maps):
//   - control groups: Shift+N adds group N to the selection, a double tap of
//     N centres the camera on it (Ctrl+N / N / Alt+N are the game's own);
//   - quick-build: with a Shaman's build list open (B), a letter picks the
//     building; with the pod list open (P), 1-3 pick a pod;
//   - builders clicked onto an own unfinished or damaged building repair it
//     (Repair is the game's only way to resume construction);
//   - planting into fog: the Shaman walks there and plants (buildings already
//     could), with the move cursor over fog in build/plant mode;
//   - the Modern mouse scheme (option): left-click selects, right-click
//     orders / cancels a mode.

namespace controls {

struct Options {
    bool builderRepair = true;
    bool quickBuild = true;
    bool buildInFog = true;
    bool modernMouse = false;  // left selects, right orders (Classic: left does both)
};

void Install(void (*log)(const char*, ...));
void SetOptions(const Options& o);

}  // namespace controls
