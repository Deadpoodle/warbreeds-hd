#include "mapfile.h"

#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>

namespace editor {
namespace {

const char* const kShipped[] = {"MPMAP1",  "MPMAP2",  "MPMAP3",  "MPMAP4",  "MPMAP5",  "MPMAP6",  "MPMAP7",
                                "MPMAP8",  "MPMAP9",  "MPMAP10", "MPMAP11", "MPMAP12", "MPMAP13", "MPMAP14",
                                "MPMAP15", "MPMAP16", "MPMAP17", "MPMAP18", "MPMAP25"};

struct Out {
    std::vector<uint8_t> b;
    void u8(unsigned v) { b.push_back((uint8_t)v); }
    void u16(unsigned v) {
        u8(v & 0xff);
        u8((v >> 8) & 0xff);
    }
    void u32(uint32_t v) {
        u16(v & 0xffff);
        u16(v >> 16);
    }
    void fill(size_t n, uint8_t v) { b.insert(b.end(), n, v); }
};

}  // namespace

bool IsShippedMapName(const std::string& base) {
    for (const char* s : kShipped)
        if (_stricmp(s, base.c_str()) == 0) return true;
    return false;
}

bool ReadHeader(const std::string& path, MapEntry& out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    uint8_t h[12 + 256];
    size_t n = fread(h, 1, sizeof h, f);
    fclose(f);
    auto u16 = [&](int o) { return (int)(h[o] | h[o + 1] << 8); };
    if (n < 12 || u16(0) != 5) return false;
    int len = u16(10);
    if (len < 1 || 12 + len > (int)n) return false;
    out.players = u16(2);
    out.width = u16(6);
    out.height = u16(8);
    out.name.assign((const char*)h + 12, strnlen((const char*)h + 12, len));
    return true;
}

std::vector<MapEntry> ListMaps(const std::string& gameDir) {
    std::vector<MapEntry> maps;
    std::string dir = gameDir + "DATA\\MAPS\\";
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "*.WBM").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return maps;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        MapEntry e;
        e.file = fd.cFileName;
        e.path = dir + e.file;
        if (!ReadHeader(e.path, e)) continue;
        std::string base = e.file.substr(0, e.file.find_last_of('.'));
        e.shipped = IsShippedMapName(base);
        maps.push_back(e);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    // Shipped maps in their number order, then the player's maps by name.
    auto key = [](const MapEntry& e) {
        int num = e.shipped ? atoi(e.file.c_str() + 5) : 0;
        return std::make_pair(e.shipped ? 0 : 1, num);
    };
    std::sort(maps.begin(), maps.end(), [&](const MapEntry& a, const MapEntry& b) {
        if (key(a) != key(b)) return key(a) < key(b);
        return _stricmp(a.file.c_str(), b.file.c_str()) < 0;
    });
    return maps;
}

void StartCell(int i, int n, int w, int h, int& x, int& y) {
    (void)n;
    // Opposite corners first, so two players start far apart.
    static const double kAt[8][2] = {{0.12, 0.12}, {0.88, 0.88}, {0.88, 0.12}, {0.12, 0.88},
                                     {0.50, 0.10}, {0.50, 0.90}, {0.10, 0.50}, {0.90, 0.50}};
    const double* a = kAt[i & 7];
    x = std::clamp((int)(a[0] * w), 2, w - 3);
    y = std::clamp((int)(a[1] * h), 2, h - 3);
}

// Field order and meanings: re/wbm-format.md §3 and tools/wbm.py (serialize()).
bool WriteBlankMap(const std::string& path, const NewMapSpec& spec, unsigned short tile, const std::string& name) {
    int w = std::clamp(spec.width, 32, 96), h = std::clamp(spec.height, 32, 96);
    int np = std::clamp(spec.players, 2, 8);
    int wt = w * 2, ht = h * 2;  // 32 px tiles
    Out o;
    // "?" header
    o.u16(5);
    o.u16(np);  // players supported
    o.u16(np);  // recommended
    o.u16(w);
    o.u16(h);
    o.u16((unsigned)name.size() + 1);
    for (char c : name) o.u8((uint8_t)c);
    o.u8(0);
    // "+" terrain: tile counts, then 96 x 96 cells column by column
    o.u8(wt);
    o.u8(ht);
    for (int i = 0; i < 96 * 96; ++i) o.u16(tile);
    // world counters and mission clock: all zero, no clock
    o.u16(0);
    o.u8(0);
    o.u32(0);
    o.u8(0);
    o.u16(0);
    o.u32(0);
    o.u16(0);
    o.u8(0);
    // "7" pathing: format 4 (no reprocess prompt), no region anywhere, no regions
    o.u16(4);
    for (int i = 0; i < wt * ht; ++i) o.u16(0xffff);
    o.u32(0);
    // "=" players, part 1
    static const int kRace[8] = {0, 2, 1, 3, 0, 2, 1, 3};  // Tanu, Kelika, Sen-Soth, Magha
    o.u16(1);  // PRHO
    o.u16(1);  // PRFO
    for (int i = 0; i < 8; ++i) {
        if (i >= np) {
            o.u16(5);  // empty slot
            continue;
        }
        int race = kRace[i];
        o.u16(race);             // F
        o.u8(238 - 8 * i);       // G colour
        o.u16(i ? 1 : 0);        // H computer
        o.u16(i);                // I index
        o.u8(1 << i);            // {
        o.u8(1 << i);            // J
        for (int k = 0; k < 4; ++k) o.u16(5);  // _ CGK limits (as the 8-player shipped maps)
        o.u16(2);
        o.fill(10, 0);           // XU
        o.u16(1);                // }
        int sx, sy;
        StartCell(i, np, w, h, sx, sy);
        o.u16(sx);               // : start cell
        o.u16(sy);
        o.u16(1);                // a fog on
        o.u8(0);                 // dc
        o.fill(10, 0);           // b
        o.fill(96 * 96, 1);      // c explored: nothing yet
        o.fill(48, 0);           // d
        o.fill(6, 0);            // km
        o.fill(32, 0);           // e name
        o.u8(0xff);              // PMSB
        for (int k = 0; k < 5; ++k) o.u16(k == race ? 1 : 0);  // PCBCF
        o.fill(4, 0);            // PEMM
        o.u16(i == 0 ? 1 : 0);   // MPTY
    }
    o.u8(np);  // ; players present
    // "FX" effects: 10 + 8 empty lists
    o.fill(36, 0);
    // "!" things: 8 empty id lists, an empty thing table
    o.fill(32, 0);
    o.u32(0);
    // "ER" triggers: 8 flags, no events
    o.fill(16, 0);
    o.u16(0);
    // "=" players, part 2: each computer player's AI state. Marker 0 (not 0x0D):
    // the loader reads nothing more and the AI starts fresh.
    for (int i = 1; i < np; ++i) o.u16(0);
    // tail: no campaign, clock 0, scroll bounds (x1, x2, y1, y2), no routes
    o.u16(0);
    o.u32(0);
    o.u16(0);
    o.u16(w * 64 - 1);
    o.u16(0);
    o.u16(h * 64 - 1);
    o.u16(0);
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(o.b.data(), 1, o.b.size(), f) == o.b.size();
    return fclose(f) == 0 && ok;
}

}  // namespace editor
