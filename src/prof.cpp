#include "prof.h"

#include <windows.h>

#include <stdio.h>
#include <string.h>

namespace prof {
namespace {

void (*g_log)(const char*, ...);
double g_freq;

struct Slot {
    const char* stage;
    int count;
    double total, max;
};
Slot g_slots[16];
int g_slotCount;
int g_frames;
double g_windowStart, g_lastTick, g_frameMax;

}  // namespace

double Now() {
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return c.QuadPart * 1000.0 / g_freq;
}

void Enable(void (*log)(const char*, ...)) {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    g_freq = (double)f.QuadPart;
    g_log = log;
}

bool Enabled() { return g_log != nullptr; }
double FrameStart() { return g_lastTick; }

void Add(const char* stage, double ms) {
    Slot* s = nullptr;
    for (int i = 0; i < g_slotCount; ++i)
        if (g_slots[i].stage == stage) s = &g_slots[i];
    if (!s) {
        if (g_slotCount == 16) return;
        s = &g_slots[g_slotCount++];
        s->stage = stage;
    }
    s->count++;
    s->total += ms;
    if (ms > s->max) s->max = ms;
}

void FrameTick() {
    if (!g_log) return;
    double now = Now();
    if (g_lastTick > 0) {
        double dt = now - g_lastTick;
        if (dt > g_frameMax) g_frameMax = dt;
        ++g_frames;
    }
    g_lastTick = now;
    if (g_windowStart == 0) g_windowStart = now;
    if (now - g_windowStart < 250 || g_frames == 0) return;
    char line[1024];
    int n = snprintf(line, sizeof line, "prof: %.0f fps (worst %.0f ms)", g_frames * 1000.0 / (now - g_windowStart),
                     g_frameMax);
    for (int i = 0; i < g_slotCount && n < (int)sizeof line - 64; ++i) {
        Slot& s = g_slots[i];
        if (!s.count) continue;
        n += snprintf(line + n, sizeof line - n, " | %s %.1f/f max %.1f x%d", s.stage, s.total / g_frames, s.max,
                      s.count);
        s.count = 0;
        s.total = s.max = 0;
    }
    g_log("%s", line);
    g_frames = 0;
    g_frameMax = 0;
    g_windowStart = now;
}

}  // namespace prof

#include <vector>

namespace prof {
namespace {

struct Sample {
    const char* phase;
    uintptr_t eip;
    uintptr_t ret[4];
};
std::vector<Sample> g_samples;
volatile bool g_sampling;
volatile const char* g_phase = "other";
HANDLE g_target, g_sampler;

DWORD WINAPI SamplerThread(LPVOID) {
    // 1 ms sleeps need a 1 ms timer period (winmm, loaded on demand).
    typedef UINT(WINAPI * Period)(UINT);
    HMODULE mm = LoadLibraryA("winmm.dll");
    Period begin = mm ? (Period)GetProcAddress(mm, "timeBeginPeriod") : nullptr;
    Period end = mm ? (Period)GetProcAddress(mm, "timeEndPeriod") : nullptr;
    if (begin) begin(1);
    // The game is pinned to one CPU; preempt it so samples are evenly spaced.
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    while (g_sampling) {
        if (SuspendThread(g_target) != (DWORD)-1) {
            CONTEXT c{};
            c.ContextFlags = CONTEXT_CONTROL;
            if (GetThreadContext(g_target, &c)) {
                Sample s{(const char*)g_phase, c.Eip, {}};
                const uintptr_t* sp = (const uintptr_t*)c.Esp;
                int n = 0;
                for (int i = 0; i < 512 && n < 4; ++i) {
                    if (IsBadReadPtr(sp + i, 4)) break;
                    uintptr_t v = sp[i];
                    if (v >= 0x401000 && v < 0x503000) s.ret[n++] = v;
                }
                g_samples.push_back(s);
            }
            ResumeThread(g_target);
        }
        Sleep(1);
    }
    if (end) end(1);
    return 0;
}

}  // namespace

void SetPhase(const char* phase) { g_phase = phase; }

void StartSampling(unsigned long threadId) {
    if (g_sampling) return;
    g_target = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, threadId);
    if (!g_target) return;
    g_samples.clear();
    g_samples.reserve(100000);
    g_sampling = true;
    g_sampler = CreateThread(nullptr, 0, SamplerThread, nullptr, 0, nullptr);
}

void StopSampling(const char* path) {
    if (!g_sampling) return;
    g_sampling = false;
    WaitForSingleObject(g_sampler, 2000);
    CloseHandle(g_sampler);
    CloseHandle(g_target);
    FILE* f = fopen(path, "w");
    if (!f) return;
    for (const Sample& s : g_samples) {
        HMODULE m = nullptr;
        char mod[MAX_PATH] = "?";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR)s.eip, &m) && m) {
            GetModuleFileNameA(m, mod, sizeof mod);
            const char* b = strrchr(mod, '\\');
            if (b) memmove(mod, b + 1, strlen(b));
        }
        fprintf(f, "%s %08x %s+%x %08x %08x %08x %08x\n", s.phase, (unsigned)s.eip, mod,
                (unsigned)(s.eip - (uintptr_t)m), (unsigned)s.ret[0], (unsigned)s.ret[1], (unsigned)s.ret[2],
                (unsigned)s.ret[3]);
    }
    fclose(f);
    if (g_log) g_log("prof: wrote %zu samples to %s", g_samples.size(), path);
}

}  // namespace prof
