#pragma once
// Stand-in for QuickTime for Windows 2.x (QTIM32.DLL / CMGR32.DLL), which the
// GOG release doesn't ship: the game's cutscenes play through it again, decoded
// by our own Cinepak/IMA4 decoder and shown by our renderer.

namespace movie {

struct Hooks {
    void (*log)(const char*, ...);
    bool silent = false;  // no sound (headless test runs)
    // Show one decoded frame (top-down BGRA, w x h) full-screen; null = movie over.
    void (*present)(const unsigned* bgra, int w, int h);
    void (*tick)(double seconds) = nullptr;  // debug: every playback tick (the frame tick stops in cutscenes)
};

bool Install(const Hooks& h);
bool Playing();  // a cutscene movie is on screen

}  // namespace movie
