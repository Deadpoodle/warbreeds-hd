// Debug memory recorder used to locate game variables (e.g. the camera).
// While a mission runs it snapshots the exe's .data section plus the objects
// behind a few global pointers to WarBreedsHD-scan.bin, a few times a second.
// tools/scan.py analyses the result.
#include "scan.h"

#include <windows.h>
#include <stdio.h>

#include <vector>

#include "game.h"

namespace scan {
namespace {

FILE* g_file;
DWORD g_last;

struct Region {
    uintptr_t base;
    uint32_t size;
};

bool Readable(uintptr_t p, uint32_t n) { return p && !IsBadReadPtr((void*)p, n); }

void Add(std::vector<Region>& out, uintptr_t p, uint32_t n) {
    if (Readable(p, n)) out.push_back({p, n});
}

}  // namespace

void Open(const std::string& path) { g_file = fopen(path.c_str(), "wb"); }

void Tick(void* missionScene) {
    if (!g_file || !missionScene) return;
    DWORD now = GetTickCount();
    if (now - g_last < 250) return;
    g_last = now;

    std::vector<Region> regions;
    Add(regions, 0x519000, 0x7c000);  // .data + .bss
    Add(regions, (uintptr_t)missionScene, 0x1000);
    for (uintptr_t g = 0x579260; g < 0x5792c0; g += 4) Add(regions, game::at<uintptr_t>(g), 0x800);

    uint32_t hdr[3] = {0x50414e53 /* SNAP */, now, (uint32_t)regions.size()};
    fwrite(hdr, sizeof hdr, 1, g_file);
    for (auto& r : regions) {
        uint32_t rh[2] = {r.base, r.size};
        fwrite(rh, sizeof rh, 1, g_file);
        fwrite((void*)r.base, r.size, 1, g_file);
    }
    fflush(g_file);
}

}  // namespace scan
