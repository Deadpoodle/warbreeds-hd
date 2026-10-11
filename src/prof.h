#pragma once
// [Debug] Profile=1: times stages of each frame and logs per-frame averages
// and maxima every 250 ms.

namespace prof {
void Enable(void (*log)(const char*, ...));
bool Enabled();
void Add(const char* stage, double ms);  // `stage` must be a string literal
void FrameTick();  // once per compose tick: frame interval and the periodic log

double Now();  // milliseconds
double FrameStart();  // time of the current compose tick's start

struct Scope {
    const char* stage;
    double start;
    explicit Scope(const char* s) : stage(s), start(Enabled() ? Now() : 0) {}
    ~Scope() {
        if (Enabled()) Add(stage, Now() - start);
    }
};
}  // namespace prof

namespace prof {
// Statistical sampler: while on, a background thread samples the given
// thread's instruction pointer and the first WARBREED.EXE return addresses on
// its stack about every millisecond, tagged with the current phase.
void StartSampling(unsigned long threadId);
void StopSampling(const char* path);  // writes the samples (one per line) to path
void SetPhase(const char* phase);     // string literal
}  // namespace prof
