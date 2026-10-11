// Hang detector: if the game's main thread stops making progress for a few
// seconds, log where it is stuck (instruction pointer + likely return
// addresses from its stack), once.
#include "watchdog.h"

#include <windows.h>
#include <psapi.h>
#include <stdio.h>

namespace watchdog {
namespace {

void (*g_log)(const char*, ...);
HWND (*g_window)();
DWORD g_mainTid;
volatile LONG g_beat;

void Describe(uintptr_t a, char* out, size_t n) {
    HMODULE m = nullptr;
    char name[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)a, &m) &&
        m) {
        GetModuleFileNameA(m, name, sizeof name);
        const char* base = strrchr(name, '\\');
        snprintf(out, n, "%08x %s+%x", (unsigned)a, base ? base + 1 : name, (unsigned)(a - (uintptr_t)m));
    } else {
        snprintf(out, n, "%08x", (unsigned)a);
    }
}

bool IsCode(uintptr_t a) {
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery((void*)a, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT) return false;
    return mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY);
}

void DumpMainThread() {
    HANDLE t = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, g_mainTid);
    if (!t) return;
    SuspendThread(t);
    CONTEXT ctx{};
    ctx.ContextFlags = CONTEXT_FULL;
    if (GetThreadContext(t, &ctx)) {
        char buf[300];
        Describe(ctx.Eip, buf, sizeof buf);
        g_log("watchdog: main thread stuck at %s", buf);
        // Likely return addresses: stack slots pointing into executable code.
        const uintptr_t* sp = (const uintptr_t*)ctx.Esp;
        int shown = 0;
        for (int i = 0; i < 2048 && shown < 24; ++i) {
            if (IsBadReadPtr(sp + i, 4)) break;
            uintptr_t v = sp[i];
            if (v < 0x10000 || !IsCode(v)) continue;
            Describe(v, buf, sizeof buf);
            g_log("watchdog:   [esp+%03x] %s", i * 4, buf);
            ++shown;
        }
    }
    ResumeThread(t);
    CloseHandle(t);
}

DWORD WINAPI Run(LPVOID) {
    LONG last = g_beat;
    int stalled = 0;
    bool dumped = false;
    for (;;) {
        Sleep(1000);
        LONG now = g_beat;
        if (now != last) {
            last = now;
            stalled = 0;
            dumped = false;
            continue;
        }
        // Only a real hang counts: Windows' own test (the window has not
        // pumped messages for several seconds), not merely an idle game.
        HWND w = g_window ? g_window() : nullptr;
        if (!w || !IsWindow(w) || !IsHungAppWindow(w)) {
            stalled = 0;
            continue;
        }
        if (++stalled >= 2 && !dumped) {
            dumped = true;
            g_log("watchdog: game window is not responding");
            DumpMainThread();
        }
    }
}

}  // namespace

void Start(void (*log)(const char*, ...), HWND (*window)()) {
    g_log = log;
    g_window = window;
    g_mainTid = GetCurrentThreadId();  // called from DllMain, on the process's main thread
    CloseHandle(CreateThread(nullptr, 0, Run, nullptr, 0, nullptr));
}

void Beat() { InterlockedIncrement(&g_beat); }

}  // namespace watchdog
