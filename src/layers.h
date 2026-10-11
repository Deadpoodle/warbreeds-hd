#pragma once
#include <stdint.h>

#include <vector>

namespace layers {

// One mission frame's interface and cursor layers (top-down, index 0 = empty).
struct Captured {
    int w = 0, h = 0;
    std::vector<uint8_t> ui;       // everything drawn after the world
    std::vector<uint8_t> cursor;   // the mouse cursor only
    int cursorX = 0, cursorY = 0;  // game cursor position (frame pixels)
};

// Patches the engine so interface and cursor drawing go to their own ports in
// missions. `publish` receives the layers at the end of each mission frame.
// `bottomUp` reports the row order of a port's DIB given its pixel pointer.
bool Install(void (*log)(const char*, ...), void (*publish)(const Captured&), bool (*bottomUp)(const void* bits),
             bool debug = false);
bool Active();
// Menus: receive the cursor drawn on its own (size x size, 8-bit, 0 = empty,
// hotspot at the centre) with the game cursor position. Off until set.
void SetMenuCursorSink(void (*sink)(const uint8_t* px, int size, int gameX, int gameY));
void OnFrameStart();  // call at the start of every compose tick
// Debug: memory address of the interface port's pixel (x, y), or 0.
uintptr_t InterfacePixelAddress(int x, int y);
// Missions: called once a frame when the world-space drawing is done (order
// markers, placement ghosts), before the interface starts, with the back
// buffer current. WorldPixelAddress is valid only inside that call: the
// battlefield port's pixel (x, y) in frame pixels, or 0.
void SetWorldOverlay(void (*draw)());
uintptr_t WorldPixelAddress(int x, int y);

}  // namespace layers
