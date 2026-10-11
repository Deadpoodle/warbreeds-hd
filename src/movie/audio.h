#pragma once
// Plays a cutscene's decoded sound through waveOut and reports the playback
// position, which the video follows. Without a sound device (or when muted)
// it falls back to a wall clock, so the video still plays at the right speed.
#include <stdint.h>

#include <vector>

namespace movie {

class AudioOut {
public:
    ~AudioOut();
    // Takes the samples (interleaved 16-bit). Silent = clock only.
    bool Start(std::vector<int16_t>&& pcm, int rate, int channels, bool silent);
    double Position() const;  // seconds since Start
    void Stop();

private:
    std::vector<int16_t> pcm_;
    int rate_ = 0, channels_ = 0;
    void* wave_ = nullptr;   // HWAVEOUT
    void* header_ = nullptr; // WAVEHDR*
    double startClock_ = 0;
    bool clockOnly_ = true;
    // After the last sample has played the position carries on by the wall
    // clock (the video can be a little longer than its sound).
    mutable double endClock_ = -1;
};

}  // namespace movie
