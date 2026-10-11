#include "audio.h"

#include <windows.h>
#include <mmsystem.h>

#include <algorithm>

namespace movie {
namespace {

// winmm is loaded on demand (the patch doesn't link it, and the game's own
// winmm imports may be hooked, e.g. muted in headless test runs).
struct WinMM {
    decltype(&waveOutOpen) open;
    decltype(&waveOutPrepareHeader) prepare;
    decltype(&waveOutUnprepareHeader) unprepare;
    decltype(&waveOutWrite) write;
    decltype(&waveOutReset) reset;
    decltype(&waveOutClose) close;
    decltype(&waveOutGetPosition) position;
    bool ok = false;
};

const WinMM& MM() {
    static WinMM m;
    static bool tried;
    if (!tried) {
        tried = true;
        if (HMODULE h = LoadLibraryA("winmm.dll")) {
            m.open = (decltype(m.open))GetProcAddress(h, "waveOutOpen");
            m.prepare = (decltype(m.prepare))GetProcAddress(h, "waveOutPrepareHeader");
            m.unprepare = (decltype(m.unprepare))GetProcAddress(h, "waveOutUnprepareHeader");
            m.write = (decltype(m.write))GetProcAddress(h, "waveOutWrite");
            m.reset = (decltype(m.reset))GetProcAddress(h, "waveOutReset");
            m.close = (decltype(m.close))GetProcAddress(h, "waveOutClose");
            m.position = (decltype(m.position))GetProcAddress(h, "waveOutGetPosition");
            m.ok = m.open && m.prepare && m.unprepare && m.write && m.reset && m.close && m.position;
        }
    }
    return m;
}

double Now() {
    LARGE_INTEGER c, f;
    QueryPerformanceCounter(&c);
    QueryPerformanceFrequency(&f);
    return (double)c.QuadPart / f.QuadPart;
}

}  // namespace

AudioOut::~AudioOut() { Stop(); }

bool AudioOut::Start(std::vector<int16_t>&& pcm, int rate, int channels, bool silent) {
    Stop();
    pcm_ = std::move(pcm);
    rate_ = rate;
    channels_ = channels;
    startClock_ = Now();
    clockOnly_ = true;
    const WinMM& mm = MM();
    if (silent || !mm.ok || pcm_.empty() || rate <= 0 || channels <= 0) return true;
    WAVEFORMATEX fmt{};
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = (WORD)channels;
    fmt.nSamplesPerSec = rate;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = (WORD)(channels * 2);
    fmt.nAvgBytesPerSec = rate * fmt.nBlockAlign;
    HWAVEOUT h = nullptr;
    if (mm.open(&h, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) return true;
    WAVEHDR* hdr = new WAVEHDR{};
    hdr->lpData = (LPSTR)pcm_.data();
    hdr->dwBufferLength = (DWORD)(pcm_.size() * 2);
    if (mm.prepare(h, hdr, sizeof *hdr) != MMSYSERR_NOERROR || mm.write(h, hdr, sizeof *hdr) != MMSYSERR_NOERROR) {
        mm.close(h);
        delete hdr;
        return true;
    }
    wave_ = h;
    header_ = hdr;
    clockOnly_ = false;
    endClock_ = -1;
    startClock_ = Now();
    return true;
}

double AudioOut::Position() const {
    if (!clockOnly_ && wave_) {
        MMTIME t{};
        t.wType = TIME_SAMPLES;
        if (MM().position((HWAVEOUT)wave_, &t, sizeof t) == MMSYSERR_NOERROR && t.wType == TIME_SAMPLES) {
            size_t frames = pcm_.size() / std::max(1, channels_);
            double played = (double)t.u.sample / rate_;
            if (t.u.sample < frames) return played;
            // All the sound has played: continue in real time from its end.
            if (endClock_ < 0) endClock_ = Now();
            return (double)frames / rate_ + (Now() - endClock_);
        }
    }
    return Now() - startClock_;
}

void AudioOut::Stop() {
    if (wave_) {
        const WinMM& mm = MM();
        mm.reset((HWAVEOUT)wave_);
        mm.unprepare((HWAVEOUT)wave_, (WAVEHDR*)header_, sizeof(WAVEHDR));
        mm.close((HWAVEOUT)wave_);
        delete (WAVEHDR*)header_;
        wave_ = nullptr;
        header_ = nullptr;
    }
    clockOnly_ = true;
}

}  // namespace movie
