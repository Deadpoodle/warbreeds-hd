#pragma once
// Minimal QuickTime (.MOV) reader for WarBreeds' cutscenes: Cinepak video and
// IMA4 ADPCM sound, decoded without QuickTime.
#include <stdint.h>
#include <stdio.h>

#include <string>
#include <vector>

namespace movie {

class Mov {
public:
    ~Mov();
    bool Open(const std::string& path);
    // Reads through an already-open Win32 file handle (duplicated; the game
    // opens the movie itself and may not share it for a second open).
    bool OpenHandle(void* winHandle);

    // Video (Cinepak). Frames decode in order (inter frames build on the last).
    int width = 0, height = 0;
    int frameCount() const { return (int)frames_.size(); }
    double frameTime(int i) const;  // start time of frame i, seconds
    double duration() const { return duration_; }
    // Decodes frame i into the internal top-down BGRA buffer (pixels()).
    // Decoding out of order restarts from the previous key frame.
    bool DecodeFrame(int i);
    const std::vector<uint32_t>& pixels() const { return rgb_; }

    // Sound (IMA4): the whole track as interleaved 16-bit PCM.
    int sampleRate = 0, channels = 0;
    bool DecodeAudio(std::vector<int16_t>& pcm);

private:
    struct Sample {
        uint64_t offset;
        uint32_t size;
        double time;
        bool key;
    };
    struct Track {
        uint32_t handler = 0, codec = 0, timescale = 0;
        std::vector<Sample> samples;
        // sound description
        int channels = 0, rate = 0, samplesPerPacket = 0, bytesPerFrame = 0;
        int w = 0, h = 0;
    };
    bool ParseAtoms(uint64_t start, uint64_t end, Track* trak);
    bool BuildSamples(Track& t, const std::vector<uint8_t>& stts, const std::vector<uint8_t>& stsc,
                      const std::vector<uint8_t>& stsz, const std::vector<uint8_t>& stco, bool co64,
                      const std::vector<uint8_t>& stss);
    bool Read(uint64_t off, void* buf, size_t n);
    bool DecodeCinepak(const uint8_t* data, size_t size);

    bool Parse();
    FILE* f_ = nullptr;
    void* handle_ = nullptr;  // duplicated Win32 handle (OpenHandle)
    uint64_t fileSize_ = 0;
    std::vector<Track> tracks_;
    std::vector<Sample> frames_;
    Track* sound_ = nullptr;
    double duration_ = 0;
    int decoded_ = -1;  // last decoded frame
    std::vector<uint32_t> rgb_;
    std::vector<uint8_t> buf_;
    // Cinepak codebooks per strip: [strip][v1/v4][256][6] (Y0..Y3, U, V)
    std::vector<std::vector<int8_t>> books_;
    // pending stbl tables while parsing a trak
    std::vector<uint8_t> stts_, stsc_, stsz_, stco_, stss_;
    bool co64_ = false;
};

}  // namespace movie
