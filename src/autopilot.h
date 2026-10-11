#pragma once
#include <string>

namespace autopilot {

struct Hooks {
    std::string dir;                    // game folder (with trailing backslash)
    void (*log)(const char*, ...);
    void (*zoomStep)(int step);         // +1 = zoom out, -1 = zoom in
    int mode = 1;  // 1 mission test, 2 Settings test, 3 bare main menu, 4 capture a menu screen,
                   // ... 13 map editor (target 0 edit, 1 New Map, 2 Exit; tools/editor_check.py)
    int target = 0;                     // mode 4: main-menu item to open (0 = Load Game ... 6 = Credits)
    const char* (*topScene)() = nullptr;  // RTTI name of the top scene
    bool bare = false;                  // mode 4: hide the screen's widgets before capturing
};

void Init(const Hooks& h);
// During cutscenes (the frame tick is stopped): playback time in seconds.
void MovieTick(double seconds);
void Save(const char* tag);  // displayed image + layers as auto-<tag>*.bmp
// Every frame tick, with the base scene's RTTI name and the frame size.
void Tick(const std::string& scene, int frameW, int frameH);

}  // namespace autopilot
