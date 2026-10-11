#pragma once
// Main-menu "Settings" page, built from the game's own menu buttons.
#include <string>

namespace menu {

struct Hooks {
    void (*log)(const char*, ...);
    std::string ini;          // WarBreedsHD.ini path
    std::string dir;          // game folder, with trailing backslash
    void (*applySettings)();  // re-read the ini and apply what can change live
};

bool Install(const Hooks& h);
// Every frame tick: highlight the text button under the mouse (the game only
// highlights a button while it is pressed).
void Tick();
// Right mouse button pressed: steps the setting under the mouse backwards.
// True if handled (swallow the click).
bool OnRightClick();
// Mouse wheel over the settings page: scrolls a long tab. True if the page is open.
bool OnWheel(int notches);
// Debug (autopilot): hide every text item on the top scene, to capture the
// bare menu background.
void HideAllText();
// Debug (autopilot): centre of the top scene's widget whose text starts with
// `text` (any widget class with the common text field), in game pixels.
bool FindWidget(const char* text, long& x, long& y);
// The top scene's widget texts with their centres, "text@x,y; ..." (tests).
std::string WidgetTexts();
// Debug (autopilot): move every widget of the top scene off-screen (bare background).
void HideAllWidgets();

}  // namespace menu
