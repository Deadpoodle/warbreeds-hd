// Diagnostics hotkeys:
//   F10 - log the scene stack and every scene's widgets (RTTI class + raw fields)
//   F11 - save the current game frame to WarBreedsHD-shot-N.bmp
#include "diag.h"

#include <windows.h>
#include <stdio.h>

#include <map>
#include <string>

#include "game.h"
#include "render/render.h"

namespace diag {
namespace {

void (*g_log)(const char*, ...);
std::string g_dir;

const char* Rtti(const void* obj) {
    if (!obj || IsBadReadPtr(obj, 4)) return "(bad)";
    uintptr_t vt = *(const uintptr_t*)obj;
    if (IsBadReadPtr((void*)(vt - 4), 4)) return "(?)";
    uintptr_t col = *(uintptr_t*)(vt - 4);
    if (IsBadReadPtr((void*)(col + 0xc), 4)) return "(?)";
    uintptr_t td = *(uintptr_t*)(col + 0xc);
    if (IsBadReadPtr((void*)(td + 8), 8)) return "(?)";
    return (const char*)(td + 8);
}

void DumpShorts(const char* prefix, const void* p, int bytes) {
    if (IsBadReadPtr(p, bytes)) return;
    const short* s = (const short*)p;
    for (int row = 0; row < bytes / 2; row += 16) {
        char line[256];
        int n = snprintf(line, sizeof line, "%s+%03x:", prefix, row * 2);
        for (int i = row; i < row + 16 && i < bytes / 2; ++i) n += snprintf(line + n, sizeof line - n, " %6d", s[i]);
        g_log("%s", line);
    }
}

}  // namespace

void Init(const std::string& dir, void (*log)(const char*, ...)) {
    g_dir = dir;
    g_log = log;
}

void DumpWidgets() {
    g_log("info panel %p %s", (void*)game::InfoPanel, Rtti((void*)game::InfoPanel));
    DumpShorts("  panel", (void*)game::InfoPanel, 0x60);
    DumpShorts("  panel+40", (char*)game::InfoPanel + 0x40, 0x20);
    DumpShorts("  panel+47c", (char*)game::InfoPanel + 0x47c, 0x20);
    short top = game::at<short>(game::SceneTop);
    g_log("=== widget dump: %d scene(s), screen %dx%d", top + 1, game::at<short>(game::ScreenRight),
          game::at<short>(game::ScreenBottom));
    for (int i = 0; i <= top; ++i) {
        char* scene = (&game::at<char*>(game::Scenes))[i];
        g_log("scene[%d] %p %s", i, scene, Rtti(scene));
        if (!scene || IsBadReadPtr(scene, 0x20)) continue;
        DumpShorts("  scene", scene, 0x80);
        // Scene embeds IList<Widget*> at +8: {vtbl, data(+0xc), count(+0x10), capacity(+0x14)}
        void** items = *(void***)(scene + 0xc);
        int count = *(int*)(scene + 0x10);
        g_log("  widgets: %d", count);
        if (count < 0 || count > 500 || IsBadReadPtr(items, count * 4)) continue;
        for (int w = 0; w < count; ++w) {
            g_log("  [%d] %p %s", w, items[w], Rtti(items[w]));
            char prefix[32];
            snprintf(prefix, sizeof prefix, "    w%d", w);
            DumpShorts(prefix, items[w], 0x60);
        }
    }
}

void SaveFrame() {
    static int n;
    char name[64];
    snprintf(name, sizeof name, "WarBreedsHD-shot-%d.bmp", ++n);
    SaveFrameAs(name);
}

namespace {
bool WriteBmp8(const std::string& path, int w, int h, const uint8_t* px, const RGBQUAD* pal) {
    FILE* out = fopen(path.c_str(), "wb");
    if (!out) return false;
    int pitch = (w + 3) & ~3;
    BITMAPFILEHEADER fh{};
    BITMAPINFOHEADER ih{};
    ih.biSize = sizeof ih;
    ih.biWidth = w;
    ih.biHeight = -h;
    ih.biPlanes = 1;
    ih.biBitCount = 8;
    ih.biClrUsed = 256;
    fh.bfType = 0x4d42;
    fh.bfOffBits = sizeof fh + sizeof ih + 256 * sizeof(RGBQUAD);
    fh.bfSize = fh.bfOffBits + pitch * h;
    fwrite(&fh, sizeof fh, 1, out);
    fwrite(&ih, sizeof ih, 1, out);
    fwrite(pal, sizeof(RGBQUAD), 256, out);
    static const char pad[4] = {};
    for (int y = 0; y < h; ++y) {
        fwrite(px + (size_t)y * w, w, 1, out);
        fwrite(pad, pitch - w, 1, out);
    }
    fclose(out);
    return true;
}
}  // namespace

// Saves the frame; in missions the frame is the battlefield and the interface
// and cursor layers are saved as <name>-hud.bmp / <name>-cursor.bmp (0 = empty).
void SaveFrameAs(const char* fileName) {
    const render::Frame& f = render::CurrentFrame();
    if (f.w <= 0) return;
    std::string path = g_dir + fileName;
    if (!WriteBmp8(path, f.w, f.h, f.pixels.data(), f.palette)) return;
    g_log("saved %s (%dx%d)", path.c_str(), f.w, f.h);
    size_t n = (size_t)f.w * f.h;
    if (!f.split || f.hud.size() != n || f.cursor.size() != n) return;
    std::string base = path.substr(0, path.size() - 4);
    WriteBmp8(base + "-hud.bmp", f.w, f.h, f.hud.data(), f.palette);
    WriteBmp8(base + "-cursor.bmp", f.w, f.h, f.cursor.data(), f.palette);
}

// --- caller log (debug) ---------------------------------------------------
// Logs each distinct caller of the engine's image-draw routines, and whether
// it was inside a widget Draw, to find non-widget drawing such as the cursor.
namespace {
int (*g_depth)();
std::map<uint64_t, int> g_seen;

void __cdecl LogDrawCaller(uint32_t id, uint32_t ret) {
    uint64_t key = ((uint64_t)id << 32) | ret;
    if (g_seen.count(key) || g_seen.size() > 200) return;
    g_seen[key] = 1;
    g_log("drawcaller: %s from %08x (%s)", id == 1 ? "DrawImage 4d6935" : "DrawImage2 4d69a6", ret,
          g_depth && g_depth() > 0 ? "in widget" : "OUTSIDE widgets");
}

void HookCaller(uintptr_t addr, int len, uint32_t id) {
    auto* t = (uint8_t*)VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    uint8_t* p = t;
    *p++ = 0xff; *p++ = 0x34; *p++ = 0x24;            // push dword [esp]  (return address)
    *p++ = 0x68; *(uint32_t*)p = id; p += 4;          // push id
    *p++ = 0xe8; *(int32_t*)p = (int32_t)((uintptr_t)&LogDrawCaller - ((uintptr_t)p + 4)); p += 4;
    *p++ = 0x83; *p++ = 0xc4; *p++ = 0x08;            // add esp, 8
    memcpy(p, (void*)addr, len); p += len;            // original prologue
    *p++ = 0xe9; *(int32_t*)p = (int32_t)((addr + len) - ((uintptr_t)p + 4));
    uint8_t jmp[5] = {0xe9};
    *(int32_t*)(jmp + 1) = (int32_t)((uintptr_t)t - (addr + 5));
    DWORD old;
    VirtualProtect((void*)addr, 5, PAGE_EXECUTE_READWRITE, &old);
    memcpy((void*)addr, jmp, 5);
    VirtualProtect((void*)addr, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
}
}  // namespace

void InstallDrawCallerLog(int (*widgetDepth)()) {
    g_depth = widgetDepth;
    HookCaller(0x4d6935, 5, 1);
    HookCaller(0x4d69a6, 6, 2);
}

// --- write tracer (debug) --------------------------------------------------
// Sets a hardware write breakpoint on one byte (via a helper thread, on the
// main thread) and logs who writes it: EIP plus likely return addresses.
namespace {
volatile LONG g_traceHits;
DWORD g_traceTid;
uintptr_t g_traceAddr;

void Describe(uintptr_t a, char* out, size_t n) {
    HMODULE m = nullptr;
    char name[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)a, &m) && m) {
        GetModuleFileNameA(m, name, sizeof name);
        const char* b = strrchr(name, '\\');
        snprintf(out, n, "%08x %s+%x", (unsigned)a, b ? b + 1 : name, (unsigned)(a - (uintptr_t)m));
    } else {
        snprintf(out, n, "%08x", (unsigned)a);
    }
}

LONG CALLBACK TraceHandler(EXCEPTION_POINTERS* ep) {
    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP || !(ep->ContextRecord->Dr6 & 1))
        return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT* c = ep->ContextRecord;
    LONG hit = InterlockedIncrement(&g_traceHits);
    if (hit <= 30) {
        char buf[200];
        Describe(c->Eip, buf, sizeof buf);
        g_log("trace: write #%ld to %08x by %s", hit, (unsigned)g_traceAddr, buf);
        const uintptr_t* sp = (const uintptr_t*)c->Esp;
        for (int i = 0, shown = 0; i < 256 && shown < 6; ++i) {
            if (IsBadReadPtr(sp + i, 4)) break;
            uintptr_t v = sp[i];
            if (v >= 0x401000 && v < 0x503000) {
                Describe(v, buf, sizeof buf);
                g_log("trace:   [esp+%03x] %s", i * 4, buf);
                ++shown;
            }
        }
    }
    c->Dr6 = 0;
    if (hit >= 30) c->Dr7 = 0;  // done
    return EXCEPTION_CONTINUE_EXECUTION;
}

DWORD WINAPI ArmThread(LPVOID) {
    HANDLE t = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT, FALSE, g_traceTid);
    if (!t) return 0;
    SuspendThread(t);
    CONTEXT c{};
    c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    GetThreadContext(t, &c);
    c.Dr0 = g_traceAddr;
    c.Dr7 = (c.Dr7 & ~0xF0003ul) | 1 | (1u << 16);  // local enable DR0, break on write, length 1
    SetThreadContext(t, &c);
    ResumeThread(t);
    CloseHandle(t);
    g_log("trace: armed write breakpoint on %08x", (unsigned)g_traceAddr);
    return 0;
}
}  // namespace

// --- crash log ----------------------------------------------------------------
// An unhandled exception: the code, where (module+offset), the registers, likely
// return addresses on the stack, which DirectPlay DLLs are loaded (from where), and
// WarBreedsHD-crash.dmp (a minidump, for a debugger). Players send the log and dump.
namespace {
LPTOP_LEVEL_EXCEPTION_FILTER g_prevFilter;
volatile LONG g_crashed;

void LogModule(const char* name) {
    HMODULE m = GetModuleHandleA(name);
    char path[MAX_PATH] = "";
    if (m) GetModuleFileNameA(m, path, sizeof path);
    g_log("crash:   %s: %s", name, m ? path : "not loaded");
}

LONG WINAPI CrashFilter(EXCEPTION_POINTERS* ep) {
    if (InterlockedExchange(&g_crashed, 1) == 0) {
        const EXCEPTION_RECORD* r = ep->ExceptionRecord;
        const CONTEXT* c = ep->ContextRecord;
        char where[200];
        Describe((uintptr_t)r->ExceptionAddress, where, sizeof where);
        g_log("crash: exception %08lx at %s (thread %lu)", r->ExceptionCode, where, GetCurrentThreadId());
        if (r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2)
            g_log("crash:   %s address %08lx", r->ExceptionInformation[0] ? "writing" : "reading",
                  (unsigned long)r->ExceptionInformation[1]);
        g_log("crash:   eax %08lx ebx %08lx ecx %08lx edx %08lx esi %08lx edi %08lx ebp %08lx esp %08lx", c->Eax,
              c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp);
        const uintptr_t* sp = (const uintptr_t*)c->Esp;
        for (int i = 0, shown = 0; i < 1024 && shown < 16; ++i) {
            if (IsBadReadPtr(sp + i, 4)) break;
            HMODULE m = nullptr;
            if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                    (LPCSTR)sp[i], &m) || !m)
                continue;
            char buf[200];
            Describe(sp[i], buf, sizeof buf);
            g_log("crash:   [esp+%03x] %s", i * 4, buf);
            ++shown;
        }
        LogModule("dplayx.dll");
        LogModule("dpwsockx.dll");
        // A minidump beside the log.
        if (HMODULE dh = LoadLibraryA("dbghelp.dll")) {
            typedef BOOL(WINAPI * Dump_t)(HANDLE, DWORD, HANDLE, int, void*, void*, void*);
            struct Info {
                DWORD tid;
                EXCEPTION_POINTERS* ep;
                BOOL client;
            } info{GetCurrentThreadId(), ep, FALSE};
            auto dump = (Dump_t)GetProcAddress(dh, "MiniDumpWriteDump");
            HANDLE f = CreateFileA((g_dir + "WarBreedsHD-crash.dmp").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                   FILE_ATTRIBUTE_NORMAL, nullptr);
            if (dump && f != INVALID_HANDLE_VALUE) {
                BOOL ok = dump(GetCurrentProcess(), GetCurrentProcessId(), f, 0x1 /* MiniDumpWithDataSegs */, &info,
                               nullptr, nullptr);
                g_log("crash: %s WarBreedsHD-crash.dmp", ok ? "wrote" : "could not write");
            }
            if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
        }
    }
    return g_prevFilter ? g_prevFilter(ep) : EXCEPTION_CONTINUE_SEARCH;
}
}  // namespace

void InstallCrashLog() {
    LPTOP_LEVEL_EXCEPTION_FILTER prev = SetUnhandledExceptionFilter(CrashFilter);
    if (prev != CrashFilter) g_prevFilter = prev;  // installed again later: keep the first one we replaced
}

void TraceWrites(uintptr_t addr) {
    static bool installed;
    if (!installed) {
        AddVectoredExceptionHandler(1, TraceHandler);
        installed = true;
    }
    g_traceHits = 0;
    g_traceAddr = addr;
    g_traceTid = GetCurrentThreadId();  // the caller's thread is the one traced
    CloseHandle(CreateThread(nullptr, 0, ArmThread, nullptr, 0, nullptr));
}

}  // namespace diag
