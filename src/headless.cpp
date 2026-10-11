// [Debug] Headless=1: for automated test runs on a machine someone is using.
// The game window is created hidden and off-screen, can never be shown or
// take the foreground, and the game finds no sound, MIDI or CD audio device.
// The renderer still composes every frame, so screenshots and layer dumps
// work as normal.
#include "headless.h"

#include <windows.h>

#include "render/render.h"

namespace headless {
namespace {

decltype(&CreateWindowExA) o_CreateWindowExA;
decltype(&ShowWindow) o_ShowWindow;
decltype(&SetForegroundWindow) o_SetForegroundWindow;
decltype(&FindWindowA) o_FindWindowA;

HWND WINAPI h_CreateWindowExA(DWORD ex, LPCSTR cls, LPCSTR name, DWORD style, int, int, int w, int h, HWND parent,
                              HMENU menu, HINSTANCE inst, LPVOID param) {
    return o_CreateWindowExA(ex | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, cls, name, style & ~WS_VISIBLE, -32000, -32000,
                             w, h, parent, menu, inst, param);
}

BOOL WINAPI h_ShowWindow(HWND, int) { return FALSE; }

// The game's start-up check (0x47449d) quits if a WarBreeds window already
// exists; a test copy must run alongside one being played.
HWND WINAPI h_FindWindowA(LPCSTR cls, LPCSTR name) {
    if (__builtin_return_address(0) == (void*)0x4744a3) return nullptr;
    return o_FindWindowA(cls, name);
}
BOOL WINAPI h_SetForegroundWindow(HWND) { return TRUE; }

UINT WINAPI h_NoDevices() { return 0; }
UINT WINAPI h_WaveOutOpen(void*, UINT, const void*, DWORD_PTR, DWORD_PTR, DWORD) { return 6; }  // MMSYSERR_NODRIVER
UINT WINAPI h_MidiOutOpen(void*, UINT, DWORD_PTR, DWORD_PTR, DWORD) { return 6; }
DWORD WINAPI h_MciSendCommandA(UINT, UINT, DWORD_PTR, DWORD_PTR) { return 256 + 6; }  // MCIERR_HARDWARE

template <typename T>
void Slot(uintptr_t slot, T fn) {
    T unused;
    render::HookImport(slot, fn, &unused);
}

}  // namespace

void Install(void (*log)(const char*, ...)) {
    render::HookImport(0x5959d8, &h_CreateWindowExA, &o_CreateWindowExA);
    render::HookImport(0x5959e8, &h_ShowWindow, &o_ShowWindow);
    render::HookImport(0x595a50, &h_SetForegroundWindow, &o_SetForegroundWindow);
    render::HookImport(0x5959c4, &h_FindWindowA, &o_FindWindowA);
    // _INMM (GOG's winmm) import slots.
    Slot(0x595aa4, (UINT(WINAPI*)()) & h_NoDevices);  // waveOutGetNumDevs
    Slot(0x595aa8, (UINT(WINAPI*)()) & h_NoDevices);  // midiOutGetNumDevs
    Slot(0x595a84, &h_WaveOutOpen);
    Slot(0x595a78, &h_MidiOutOpen);
    Slot(0x595ab0, &h_MciSendCommandA);
    log("headless: window hidden off-screen, no activation, audio disabled");
}

}  // namespace headless
