// QuickTime for Windows 2.x stand-in.
//
// The game reaches QuickTime through static glue: ~100 stubs (0x4eae6a..)
// `push ebx; mov bx, sel; call [0x520f0c]` for QTIM32's _EntryPoint and
// ~30 more (0x4eac8a..) through [0x520f18] for CMGR32 (movie controllers).
// Both pointers start at lazy stubs that call the loader 0x4eb3b0, which
// LoadLibrary's the DLLs, checks QTIM32's version and fills the pointers. We
// replace the loader so it installs our two dispatchers (version 2.1.3), and
// make the unloader 0x4eb700 a no-op. Arguments are cdecl 32-bit values at
// [esp+12] inside the dispatcher (stub return, saved ebx, caller return).
//
// Playback (see WIKI_LOG): the cutscene scene (SCut/SIntroCut/SRedOrbCut)
// stops the frame tick, opens the movie, makes a controller on the game window
// and starts it, then loops GetMessage while the movie's "playing" status is
// set. That status is only changed by the game's action filter (called with
// mcActionPlay), so we call the filter just as QuickTime would. A window timer
// keeps messages (and so the loop) going; on each tick the movie advances by
// the audio clock and the current frame is presented by our renderer.
#include "qt.h"

#define _WIN32_WINNT 0x0601  // GetFinalPathNameByHandleA
#include <windows.h>
#include <string.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "audio.h"
#include "mov.h"

namespace movie {
namespace {

Hooks g_h;

constexpr uintptr_t kLoader = 0x4eb3b0;    // cdecl long Load(long* version)
constexpr uintptr_t kUnloader = 0x4eb700;
constexpr uintptr_t kQtEntry = 0x520f0c;   // QTIM32 _EntryPoint pointer
constexpr uintptr_t kCmgrEntry = 0x520f18; // CMGR32 _EntryPoint pointer
constexpr uintptr_t kLoaded = 0x520f08;    // lazy stubs skip the loader when set
constexpr int kTimeScale = 600;            // TimeValue units per second
constexpr UINT_PTR kTimerId = 0x5157;      // 'QW'

// Movie controller actions (MCDoAction).
enum { mcActionIdle = 1, mcActionPlay = 8, mcActionGoToTime = 12, mcActionSetVolume = 14,
       mcActionGetFlags = 39, mcActionGetPlayRate = 42 };

typedef BOOL(__stdcall* ActionFilter)(void* mc, long action, void* param, long refcon);

struct Movie {
    Mov mov;
    std::vector<int16_t> pcm;
    AudioOut audio;
    HWND hwnd = nullptr;
    RECT box{};
    bool playing = false;
    long rate = 0;          // Fixed 16.16
    double base = 0;        // movie time where the current play started
    double paused = 0;      // movie time while stopped
    int shown = -1;         // last frame presented
    ActionFilter filter = nullptr;
    long refcon = 0;
    int filterDepth = 0;
    bool timer = false;

    double Now() const { return playing ? std::min(base + audio.Position(), mov.duration()) : paused; }
};

std::vector<std::unique_ptr<Movie>> g_movies;
Movie* g_current;  // the one on screen

Movie* FromHandle(uint32_t h) {
    for (auto& m : g_movies)
        if ((uint32_t)(uintptr_t)m.get() == h) return m.get();
    return nullptr;
}

long ToTime(double s) { return (long)(s * kTimeScale + 0.5); }

void Present(Movie* m) {
    if (!g_h.present) return;
    double t = m->Now();
    int n = m->mov.frameCount(), f = 0;
    while (f + 1 < n && m->mov.frameTime(f + 1) <= t) ++f;
    if (f != m->shown && m->mov.DecodeFrame(f)) m->shown = f;
    if (m->shown >= 0) g_h.present(m->mov.pixels().data(), m->mov.width, m->mov.height);
    g_current = m;
}

// Calls the game's action filter like QuickTime does; true = it handled the action.
bool Filter(Movie* m, long action, void* param) {
    if (!m->filter || m->filterDepth > 0) return false;
    ++m->filterDepth;
    BOOL r = m->filter(m, action, param, m->refcon);
    --m->filterDepth;
    return (r & 0xffff) != 0;
}

void StartTimer(Movie* m) {
    if (!m->timer && m->hwnd) m->timer = SetTimer(m->hwnd, kTimerId, 5, nullptr) != 0;
}
void StopTimer(Movie* m) {
    if (m->timer && m->hwnd) KillTimer(m->hwnd, kTimerId);
    m->timer = false;
}

void Play(Movie* m, long rate) {
    if (rate != 0 && !m->playing) {
        double from = m->paused >= m->mov.duration() ? 0 : m->paused;
        size_t at = (size_t)(from * m->mov.sampleRate) * std::max(1, m->mov.channels);
        std::vector<int16_t> rest(m->pcm.begin() + std::min(at, m->pcm.size()), m->pcm.end());
        m->audio.Start(std::move(rest), m->mov.sampleRate, m->mov.channels, g_h.silent);
        m->base = from;
        m->playing = true;
        m->rate = rate;
        StartTimer(m);
        g_h.log("movie: play from %.2f s", from);
    } else if (rate == 0 && m->playing) {
        m->paused = m->Now();
        m->playing = false;
        m->rate = 0;
        m->audio.Stop();
        g_h.log("movie: stop at %.2f s", m->paused);
    }
}

// One step: show the current frame; at the end, stop the way QuickTime does
// (the filter hears mcActionPlay with rate 0 and clears the game's status).
void Tick(Movie* m) {
    if (!m) return;
    Present(m);
    if (g_h.tick && m->playing) g_h.tick(m->Now());
    if (m->playing && m->Now() >= m->mov.duration()) {
        m->paused = m->mov.duration();
        long zero = 0;
        if (!Filter(m, mcActionPlay, (void*)(intptr_t)zero)) Play(m, 0);
        Play(m, 0);
        if (m->hwnd) PostMessageA(m->hwnd, WM_NULL, 0, 0);
        g_h.log("movie: finished");
    }
}

void Dispose(Movie* m) {
    StopTimer(m);
    m->audio.Stop();
    if (g_current == m) {
        g_current = nullptr;
        if (g_h.present) g_h.present(nullptr, 0, 0);
    }
    g_movies.erase(std::remove_if(g_movies.begin(), g_movies.end(), [m](auto& p) { return p.get() == m; }),
                   g_movies.end());
}

void Unknown(const char* lib, int sel) {
    static int logged;
    if (logged++ < 40) g_h.log("movie: %s selector %#x not implemented (returning 0)", lib, sel);
}

}  // namespace

// --- QTIM32 ---------------------------------------------------------------

extern "C" long __cdecl QtCall(int sel, uint32_t* a) {
    switch (sel) {
    case 0x0b:  // EnterMovies
    case 0x0c:  // ExitMovies
    case 0x39:  // (init)
    case 0x3a:  // (term)
    case 0x5e:  // GetMoviesError
    case 0x62:  // (palette sync)
    case 0x2f:  // PrerollMovie
    case 0x31:  // SetMovieActive
        return 0;
    case 0x29: {  // NewMovieFromDataFork(Movie* out, HANDLE file, long offset, short flags)
        char path[MAX_PATH * 2];
        DWORD n = GetFinalPathNameByHandleA((HANDLE)(uintptr_t)a[1], path, sizeof path, 0);
        if (!n || n >= sizeof path) {
            g_h.log("movie: can't get the movie file's path");
            return -2010;
        }
        std::string p = path;
        if (p.compare(0, 4, "\\\\?\\") == 0) p = p.substr(4);
        auto m = std::make_unique<Movie>();
        if (!m->mov.OpenHandle((void*)(uintptr_t)a[1])) {
            g_h.log("movie: can't decode %s", p.c_str());
            return -2010;
        }
        m->mov.DecodeAudio(m->pcm);
        m->box = {0, 0, m->mov.width, m->mov.height};
        g_h.log("movie: opened %s (%dx%d, %.1f s, %d frames)", p.c_str(), m->mov.width, m->mov.height,
                m->mov.duration(), m->mov.frameCount());
        *(uint32_t*)(uintptr_t)a[0] = (uint32_t)(uintptr_t)m.get();
        g_movies.push_back(std::move(m));
        return 0;
    }
    case 0x07:  // DisposeMovie(movie)
        if (Movie* m = FromHandle(a[0])) Dispose(m);
        return 0;
    case 0x0f:  // GetMovieBox(movie, RECT*)
        if (Movie* m = FromHandle(a[0])) *(RECT*)(uintptr_t)a[1] = {0, 0, m->mov.width, m->mov.height};
        return 0;
    case 0x1c:  // GetMovieTimeScale
        return kTimeScale;
    case 0x12:  // GetMovieDuration
        if (Movie* m = FromHandle(a[0])) return ToTime(m->mov.duration());
        return 0;
    case 0x17:  // GetMoviePreferredRate
        return 0x10000;
    case 0x18:  // GetMoviePreferredVolume
        return 0x100;
    case 0x3f: {  // GetMovieIndTrackType(movie, index, type, flags)
        Movie* m = FromHandle(a[0]);
        if (!m) return 0;
        if (a[2] == 0x76696465 /* 'vide' */) return 1;
        if (a[2] == 0x736f756e /* 'soun' */) return m->mov.sampleRate ? 2 : 0;
        return 0;
    }
    case 0x1d:  // GetMovieUserData: none (skips the LOOP lookup)
        return 0;
    case 0x27:  // (fills a 0x68-byte info block; only a codec check reads it)
        if (a[1]) memset((void*)(uintptr_t)a[1], 0, 0x68);
        return 0;
    case 0x38: {  // NewMovieController(movie, RECT*, flags, HWND)
        Movie* m = FromHandle(a[0]);
        if (!m) return 0;
        if (a[1]) m->box = *(RECT*)(uintptr_t)a[1];
        m->hwnd = (HWND)(uintptr_t)a[3];
        g_h.log("movie: controller on window %p, box %ld,%ld-%ld,%ld", m->hwnd, m->box.left, m->box.top,
                m->box.right, m->box.bottom);
        return (long)(uintptr_t)m;  // the controller is the movie
    }
    case 0x37:  // DisposeMovieController(mc)
        if (Movie* m = FromHandle(a[0])) {
            Play(m, 0);
            StopTimer(m);
        }
        return 0;
    }
    Unknown("QTIM32", sel);
    return 0;
}

// --- CMGR32 (movie controller) ----------------------------------------------

extern "C" long __cdecl CmgrCall(int sel, uint32_t* a) {
    Movie* m = FromHandle(a[0]);
    switch (sel) {
    case 1: {  // MCDoAction(mc, action, param)
        if (!m) return 0;
        long action = (short)a[1];
        void* param = (void*)(uintptr_t)a[2];
        if (Filter(m, action, param)) return 0;
        switch (action) {
        case mcActionIdle: Tick(m); break;
        case mcActionPlay: Play(m, (long)a[2]); break;
        case mcActionGoToTime:
            if (param) {
                const int32_t* rec = (const int32_t*)param;  // {value lo, value hi, scale, base}
                double s = rec[2] ? (double)rec[0] / rec[2] : (double)rec[0] / kTimeScale;
                bool was = m->playing;
                long r = m->rate;
                Play(m, 0);
                m->paused = std::clamp(s, 0.0, m->mov.duration());
                if (was) Play(m, r);
            }
            break;
        case mcActionGetFlags:
            if (param) *(long*)param = 0;
            break;
        case mcActionGetPlayRate:
            if (param) *(long*)param = m->rate;
            break;
        default: break;  // activate, volume, keys, flags, loop: nothing to do
        }
        return 0;
    }
    case 4:  // MCGetControllerBoundsRect(mc, RECT*)
        if (m && a[1]) *(RECT*)(uintptr_t)a[1] = m->box;
        return 0;
    case 5:  // (mc, long*) controller info
        if (a[1]) *(long*)(uintptr_t)a[1] = 0;
        return 0;
    case 6:  // MCGetCurrentTime(mc, TimeScale* out)
        if (a[1]) *(long*)(uintptr_t)a[1] = kTimeScale;
        return m ? ToTime(m->Now()) : 0;
    case 9:  // MCIdle(mc)
        Tick(m);
        return 0;
    case 0xb:  // MCIsPlayerEvent(mc, hwnd, msg, wParam, lParam)
        if (m && a[2] == WM_TIMER && a[3] == kTimerId) {
            Tick(m);
            return 1;
        }
        return 0;
    case 0xe:  // MCPositionController(mc, RECT* movieRect, RECT*, info)
        if (m && a[1]) m->box = *(RECT*)(uintptr_t)a[1];
        return 0;
    case 0x10:  // MCSetActionFilterWithRefCon(mc, filter, refcon)
        if (m) {
            m->filter = (ActionFilter)(uintptr_t)a[1];
            m->refcon = (long)a[2];
        }
        return 0;
    }
    Unknown("CMGR32", sel);
    return 0;
}

// The two _EntryPoint dispatchers: selector in bx, args at [esp+12].
extern "C" void QtEntry();
extern "C" void CmgrEntry();
asm(".globl _QtEntry\n"
    "_QtEntry:\n"
    "  movzwl %bx, %eax\n"
    "  lea 12(%esp), %edx\n"
    "  push %edx\n"
    "  push %eax\n"
    "  call _QtCall\n"
    "  add $8, %esp\n"
    "  ret\n"
    ".globl _CmgrEntry\n"
    "_CmgrEntry:\n"
    "  movzwl %bx, %eax\n"
    "  lea 12(%esp), %edx\n"
    "  push %edx\n"
    "  push %eax\n"
    "  call _CmgrCall\n"
    "  add $8, %esp\n"
    "  ret\n");

namespace {

// Replacement loader: cdecl long Load(long* version) -> 0 = loaded.
long __cdecl Load(long* version) {
    *(uint32_t*)kQtEntry = (uint32_t)(uintptr_t)&QtEntry;
    *(uint32_t*)kCmgrEntry = (uint32_t)(uintptr_t)&CmgrEntry;
    *(uint32_t*)kLoaded = 1;
    if (version) *version = 0x2103;  // QuickTime for Windows 2.1.3 passes the game's check
    return 0;
}

bool Patch(uintptr_t at, const uint8_t* expect, size_t n, const uint8_t* code, size_t len) {
    if (memcmp((void*)at, expect, n) != 0) return false;
    DWORD old;
    if (!VirtualProtect((void*)at, len, PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy((void*)at, code, len);
    VirtualProtect((void*)at, len, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void*)at, len);
    return true;
}

}  // namespace

bool Install(const Hooks& h) {
    g_h = h;
    // 0x4eb3b0: mov dword ptr [0x520f08], 1  ->  jmp Load
    const uint8_t loader[] = {0xc7, 0x05, 0x08, 0x0f, 0x52, 0x00, 0x01, 0x00, 0x00, 0x00};
    uint8_t jmp[5] = {0xe9};
    *(int32_t*)(jmp + 1) = (int32_t)((uintptr_t)&Load - (kLoader + 5));
    // 0x4eb700: call 0x4eb6b0; jmp 0x4eb650  ->  xor eax, eax; ret
    const uint8_t unloader[] = {0xe8, 0xab, 0xff, 0xff, 0xff};
    const uint8_t ret0[] = {0x31, 0xc0, 0xc3};
    bool ok = Patch(kLoader, loader, sizeof loader, jmp, sizeof jmp) &&
              Patch(kUnloader, unloader, sizeof unloader, ret0, sizeof ret0);
    h.log("movie: QuickTime stand-in %s", ok ? "installed" : "FAILED (unexpected code)");
    return ok;
}

bool Playing() { return g_current != nullptr; }

}  // namespace movie
