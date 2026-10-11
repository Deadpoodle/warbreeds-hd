// A stand-in for DirectDraw. WARBREED.EXE draws everything with GDI and uses
// DirectDraw only to list and set display modes, so this object answers those
// calls without touching the real display: "mode changes" become a resize of
// our own presentation, with no flicker.
#include "ddfake.h"

#include <initguid.h>  // define IID_IDirectDraw / IID_IDirectDraw2 here

#include <ddraw.h>
#include <string.h>

#include "render.h"

namespace render {
namespace {

void (*g_log)(const char*, ...);

#define UNSUPPORTED(name)                       \
    do {                                        \
        static bool logged;                     \
        if (!logged) {                          \
            logged = true;                      \
            g_log("ddfake: %s not supported", name); \
        }                                       \
        return DDERR_UNSUPPORTED;               \
    } while (0)

class FakeDirectDraw final : public IDirectDraw, public IDirectDraw2 {
  public:
    // IUnknown (shared by both interfaces)
    STDMETHOD(QueryInterface)(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IDirectDraw) {
            *out = static_cast<IDirectDraw*>(this);
        } else if (riid == IID_IDirectDraw2) {
            *out = static_cast<IDirectDraw2*>(this);
        } else {
            *out = nullptr;
            g_log("ddfake: QueryInterface for unknown interface");
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
    STDMETHOD_(ULONG, AddRef)() override { return ++refs_; }
    STDMETHOD_(ULONG, Release)() override {
        ULONG r = --refs_;
        if (r == 0) delete this;
        return r;
    }

    // Shared methods
    STDMETHOD(Compact)() override { return DD_OK; }
    STDMETHOD(CreateClipper)(DWORD, LPDIRECTDRAWCLIPPER*, IUnknown*) override { UNSUPPORTED("CreateClipper"); }
    STDMETHOD(CreatePalette)(DWORD, LPPALETTEENTRY, LPDIRECTDRAWPALETTE*, IUnknown*) override {
        UNSUPPORTED("CreatePalette");
    }
    STDMETHOD(CreateSurface)(LPDDSURFACEDESC, LPDIRECTDRAWSURFACE*, IUnknown*) override {
        UNSUPPORTED("CreateSurface");
    }
    STDMETHOD(DuplicateSurface)(LPDIRECTDRAWSURFACE, LPDIRECTDRAWSURFACE*) override {
        UNSUPPORTED("DuplicateSurface");
    }
    STDMETHOD(EnumDisplayModes)(DWORD, LPDDSURFACEDESC filter, LPVOID ctx, LPDDENUMMODESCALLBACK cb) override {
        if (!cb) return DDERR_INVALIDPARAMS;
        for (const Mode& m : AdvertisedModes()) {
            for (int bpp : {8, 16, 32}) {
                DDSURFACEDESC d;
                FillDesc(d, m.w, m.h, bpp);
                if (filter && (filter->dwFlags & DDSD_WIDTH) && filter->dwWidth != d.dwWidth) continue;
                if (filter && (filter->dwFlags & DDSD_HEIGHT) && filter->dwHeight != d.dwHeight) continue;
                if (cb(&d, ctx) == DDENUMRET_CANCEL) return DD_OK;
            }
        }
        return DD_OK;
    }
    STDMETHOD(EnumSurfaces)(DWORD, LPDDSURFACEDESC, LPVOID, LPDDENUMSURFACESCALLBACK) override {
        UNSUPPORTED("EnumSurfaces");
    }
    STDMETHOD(FlipToGDISurface)() override { return DD_OK; }
    STDMETHOD(GetCaps)(LPDDCAPS driver, LPDDCAPS hel) override {
        for (LPDDCAPS c : {driver, hel}) {
            if (!c || c->dwSize < 8) continue;
            DWORD size = c->dwSize;
            memset(c, 0, size);
            c->dwSize = size;
            c->dwCaps = DDCAPS_BLT | DDCAPS_BLTSTRETCH | DDCAPS_PALETTE;
            c->dwPalCaps = DDPCAPS_8BIT;
        }
        return DD_OK;
    }
    STDMETHOD(GetDisplayMode)(LPDDSURFACEDESC d) override {
        if (!d) return DDERR_INVALIDPARAMS;
        Mode m = CurrentMode();
        FillDesc(*d, m.w, m.h, m.bpp);
        return DD_OK;
    }
    STDMETHOD(GetFourCCCodes)(LPDWORD n, LPDWORD) override {
        if (n) *n = 0;
        return DD_OK;
    }
    STDMETHOD(GetGDISurface)(LPDIRECTDRAWSURFACE*) override { UNSUPPORTED("GetGDISurface"); }
    STDMETHOD(GetMonitorFrequency)(LPDWORD f) override {
        if (f) *f = 60;
        return DD_OK;
    }
    STDMETHOD(GetScanLine)(LPDWORD l) override {
        if (l) *l = 0;
        return DD_OK;
    }
    STDMETHOD(GetVerticalBlankStatus)(WINBOOL* b) override {
        if (b) *b = TRUE;
        return DD_OK;
    }
    STDMETHOD(Initialize)(GUID*) override { return DDERR_ALREADYINITIALIZED; }
    STDMETHOD(RestoreDisplayMode)() override { return DD_OK; }
    STDMETHOD(SetCooperativeLevel)(HWND hwnd, DWORD flags) override {
        OnSetCooperativeLevel(hwnd, flags);
        return DD_OK;
    }
    STDMETHOD(WaitForVerticalBlank)(DWORD, HANDLE) override { return DD_OK; }

    // IDirectDraw::SetDisplayMode and IDirectDraw2::SetDisplayMode
    STDMETHOD(SetDisplayMode)(DWORD w, DWORD h, DWORD bpp) override { return SetMode(w, h, bpp); }
    STDMETHOD(SetDisplayMode)(DWORD w, DWORD h, DWORD bpp, DWORD, DWORD) override { return SetMode(w, h, bpp); }

    // IDirectDraw2 only
    STDMETHOD(GetAvailableVidMem)(LPDDSCAPS, LPDWORD total, LPDWORD free) override {
        if (total) *total = 256u << 20;
        if (free) *free = 256u << 20;
        return DD_OK;
    }

  private:
    HRESULT SetMode(DWORD w, DWORD h, DWORD bpp) {
        for (const Mode& m : AdvertisedModes())
            if (m.w == (int)w && m.h == (int)h) {
                OnSetDisplayMode((int)w, (int)h, (int)bpp);
                return DD_OK;
            }
        g_log("ddfake: refused unlisted mode %lux%lu", w, h);
        return DDERR_INVALIDMODE;
    }

    static void FillDesc(DDSURFACEDESC& d, int w, int h, int bpp) {
        memset(&d, 0, sizeof d);
        d.dwSize = sizeof d;
        d.dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_PITCH | DDSD_PIXELFORMAT | DDSD_REFRESHRATE;
        d.dwWidth = w;
        d.dwHeight = h;
        d.lPitch = w * (bpp / 8);
        d.dwRefreshRate = 60;
        d.ddpfPixelFormat.dwSize = sizeof d.ddpfPixelFormat;
        d.ddpfPixelFormat.dwFlags = DDPF_RGB | (bpp == 8 ? DDPF_PALETTEINDEXED8 : 0);
        d.ddpfPixelFormat.dwRGBBitCount = bpp;
        if (bpp == 16) {
            d.ddpfPixelFormat.dwRBitMask = 0xf800;
            d.ddpfPixelFormat.dwGBitMask = 0x07e0;
            d.ddpfPixelFormat.dwBBitMask = 0x001f;
        } else if (bpp == 32) {
            d.ddpfPixelFormat.dwRBitMask = 0xff0000;
            d.ddpfPixelFormat.dwGBitMask = 0x00ff00;
            d.ddpfPixelFormat.dwBBitMask = 0x0000ff;
        }
        d.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
    }

    ULONG refs_ = 1;
};

HRESULT WINAPI FakeDirectDrawCreate(GUID*, LPDIRECTDRAW* out, IUnknown*) {
    if (!out) return DDERR_INVALIDPARAMS;
    *out = static_cast<IDirectDraw*>(new FakeDirectDraw);
    g_log("ddfake: DirectDrawCreate");
    return DD_OK;
}

// The exe loads DDRAW.DLL with LoadLibraryA and finds DirectDrawCreate with
// GetProcAddress, so hooking those two imports is enough.
HMODULE g_fakeDdraw;  // our own module handle, stands in for DDRAW.DLL
decltype(&LoadLibraryA) o_LoadLibraryA;
decltype(&GetProcAddress) o_GetProcAddress;
decltype(&FreeLibrary) o_FreeLibrary;

HMODULE WINAPI h_LoadLibraryA(LPCSTR name) {
    if (name) {
        const char* base = strrchr(name, '\\');
        base = base ? base + 1 : name;
        if (_stricmp(base, "ddraw.dll") == 0 || _stricmp(base, "ddraw") == 0) return g_fakeDdraw;
    }
    return o_LoadLibraryA(name);
}

FARPROC WINAPI h_GetProcAddress(HMODULE mod, LPCSTR name) {
    if (mod == g_fakeDdraw && (uintptr_t)name > 0xffff) {
        if (strcmp(name, "DirectDrawCreate") == 0) return (FARPROC)&FakeDirectDrawCreate;
        g_log("ddfake: GetProcAddress(ddraw, %s) not provided", name);
        return nullptr;
    }
    return o_GetProcAddress(mod, name);
}

BOOL WINAPI h_FreeLibrary(HMODULE mod) {
    if (mod == g_fakeDdraw) return TRUE;
    return o_FreeLibrary(mod);
}

}  // namespace

bool InstallFakeDirectDraw(HMODULE self, void (*log)(const char*, ...)) {
    g_log = log;
    g_fakeDdraw = self;
    return HookImport(0x595848, &h_LoadLibraryA, &o_LoadLibraryA) &&
           HookImport(0x595850, &h_GetProcAddress, &o_GetProcAddress) &&
           HookImport(0x59588c, &h_FreeLibrary, &o_FreeLibrary);
}

}  // namespace render
