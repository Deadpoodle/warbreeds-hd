#pragma once

namespace zoom {

struct Settings {
    double defaultMag = 4.0 / 3.0;  // screen pixels per world pixel at mission start
    double minMag = 0.5;            // furthest zoom out
    double maxMag = 2.0;            // furthest zoom in
    double step = 1.15;             // magnification change per wheel notch
    double speed = 14.0;            // easing rate (1/s)
    int maxPixels = 3840 * 2160;    // largest render size before half-scale view kicks in
    bool halfScale = true;          // allow the game's half-scale view for far zoom-out
};

void Init(const Settings& s, int screenW, int screenH, void (*log)(const char*, ...));
void MissionResolution(int& w, int& h);  // resolution the mission should run at now
void Wheel(int notches, bool atCursor);  // + = zoom in; anchored on the cursor or the screen centre
void Tick(void* sgame);                  // every frame; sgame = null outside missions
double Current();
void SetRange(double minMag, double maxMag);  // live: furthest zoom out / in

}  // namespace zoom
