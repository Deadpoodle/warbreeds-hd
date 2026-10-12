// Map editor, Stage 2: the map checker (validate.h, re/editor-stage2.md §7).
#include "validate.h"

#include <stdint.h>

#include <algorithm>
#include <vector>

#include "../lang.h"
#include "world.h"

namespace editor {
namespace {

using namespace world;

constexpr uintptr_t kTerrainAnalysis = 0x55dd64;  // AGrid +4: AITerrainAnalysis*
constexpr uintptr_t kObstacleRegenerate = 0x4a7a70;  // tObstacleMap (AITerrainAnalysis +4)
constexpr uintptr_t kWorldPtr = 0x579278;
constexpr uintptr_t kPodNearby = 0x4518c0;   // AThingRegistry short(u8 tx, u8 ty): a crop within reach of a Refinery
constexpr uintptr_t kDevicePower = 0x54d010;  // short[19]: power a device draws (type - 0x2c)
constexpr uintptr_t kPowerRange = 0x54de68;   // short[type]: a provider's power radius, px (FindPowerProvider 0x44eb00)

std::string Num(int n) { return std::to_string(n); }

int Cells(int px) { return px / 64; }

// Ground connectivity on the game's obstacle map (the guide graph's input:
// blocked terrain, cropland, trees, rocks and ruins), 32 px tiles.
struct Ground {
    int w = 0, h = 0;
    const short* cells = nullptr;
    bool Open(int x, int y) const { return x >= 0 && y >= 0 && x < w && y < h && cells[y * w + x] == 0; }
    // The first open tile of a cell.
    bool Seed(int cx, int cy, int& x, int& y) const {
        for (int dy = 0; dy < 2; ++dy)
            for (int dx = 0; dx < 2; ++dx)
                if (Open(2 * cx + dx, 2 * cy + dy)) {
                    x = 2 * cx + dx;
                    y = 2 * cy + dy;
                    return true;
                }
        return false;
    }
    std::vector<uint8_t> Fill(int x, int y) const {
        std::vector<uint8_t> seen((size_t)w * h);
        std::vector<int> stack{y * w + x};
        seen[y * w + x] = 1;
        while (!stack.empty()) {
            int i = stack.back();
            stack.pop_back();
            int cx = i % w, cy = i / w;
            const int d[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            for (auto& o : d) {
                int nx = cx + o[0], ny = cy + o[1];
                if (Open(nx, ny) && !seen[ny * w + nx]) {
                    seen[ny * w + nx] = 1;
                    stack.push_back(ny * w + nx);
                }
            }
        }
        return seen;
    }
};

bool LoadGround(Ground& g) {
    uintptr_t ita = *(uintptr_t*)kTerrainAnalysis;
    if (!ita) return false;
    ((void(__thiscall*)(uintptr_t))kObstacleRegenerate)(ita + 4);
    g.w = *(short*)(ita + 0x10);
    g.h = *(short*)(ita + 0x12);
    g.cells = *(const short**)(ita + 0x14);
    return g.cells && g.w > 0 && g.h > 0;
}

void CheckPower(std::vector<Finding>& out) {
    // The game's power model, read statically (the simulation is stopped, so the
    // runtime links aren't there): a Refinery is a source when a crop is within
    // its reach (RefineryPodNearby, as CanPlace asks); a Relay passes power on
    // when it sits in the field of a powered Refinery or Relay of its owner; a
    // device that draws power needs to sit in such a field.
    struct Prov {
        uintptr_t t;
        int owner, x, y, r;
        bool on;
    };
    std::vector<Prov> provs;
    uintptr_t w = *(uintptr_t*)kWorldPtr;
    for (int id = 0; id < ThingCount(); ++id) {
        uintptr_t t = Thing(id);
        if (!t) continue;
        int type = TypeOf(t);
        if (type != kRefinery && type != kRelay && type != kRelayPlus) continue;
        Prov p{t, OwnerOf(t), CentreX(t), CentreY(t), ((short*)kPowerRange)[type], false};
        if (type == kRefinery)
            p.on = (((short(__thiscall*)(uintptr_t, int, int))kPodNearby)(w, (uint8_t)(p.x >> 5), (uint8_t)(p.y >> 5)) &
                    0xffff) == 1;
        provs.push_back(p);
    }
    auto inField = [&](int owner, int x, int y, uintptr_t self) {
        for (const Prov& p : provs)
            if (p.on && p.t != self && p.owner == owner) {
                long dx = p.x - x, dy = p.y - y;
                if (dx * dx + dy * dy < (long)p.r * p.r) return true;
            }
        return false;
    };
    for (bool more = true; more;) {
        more = false;
        for (Prov& p : provs)
            if (!p.on && TypeOf(p.t) != kRefinery && inField(p.owner, p.x, p.y, p.t)) more = p.on = true;
    }
    int unpowered = 0;
    for (int id = 0; id < ThingCount(); ++id) {
        uintptr_t t = Thing(id);
        if (!t || !IsDevice(TypeOf(t))) continue;
        int type = TypeOf(t), owner = OwnerOf(t);
        bool ok;
        if (type == kRefinery) {
            ok = std::any_of(provs.begin(), provs.end(), [&](const Prov& p) { return p.t == t && p.on; });
            if (!ok) {
                if (++unpowered <= 4)
                    out.push_back({lang::trf("P{1}'s Refinery at {2},{3} has no crops in reach",
                                             {Num(owner + 1), Num(Cells(CentreX(t))), Num(Cells(CentreY(t)))})});
                continue;
            }
        } else {
            bool relay = type == kRelay || type == kRelayPlus;
            if (!relay && ((short*)kDevicePower)[type - 0x2c] <= 0) continue;
            ok = relay ? std::any_of(provs.begin(), provs.end(), [&](const Prov& p) { return p.t == t && p.on; })
                       : inField(owner, CentreX(t), CentreY(t), t);
            if (!ok && ++unpowered <= 4)
                out.push_back({lang::trf("P{1}'s {2} at {3},{4} has no power",
                                         {Num(owner + 1), Name(type), Num(Cells(CentreX(t))), Num(Cells(CentreY(t)))})});
        }
    }
    if (unpowered > 4) out.push_back({lang::trf("... and {1} more devices without power", {Num(unpowered - 4)})});
}

void CheckCroplandEdges(std::vector<Finding>& out) {
    // The manual: keep cropland right at the map edge or well away from it, so
    // creatures can't get stuck behind it. Cropland 1-2 tiles in from an edge
    // with open ground between leaves such a gap.
    int W = TilesW(), H = TilesH();
    const char* const kEdge[] = {"west", "east", "north", "south"};
    for (int e = 0; e < 4; ++e) {
        int n = (e < 2 ? H : W), gaps = 0;
        for (int i = 0; i < n; ++i) {
            auto at = [&](int d) {
                switch (e) {
                case 0: return TerrainType(d, i);
                case 1: return TerrainType(W - 1 - d, i);
                case 2: return TerrainType(i, d);
                default: return TerrainType(i, H - 1 - d);
                }
            };
            for (int d = 1; d <= 2; ++d)
                if (at(d) == 2) {
                    bool open = true;
                    for (int k = 0; k < d; ++k) open = open && at(k) == 0;
                    if (open) {
                        ++gaps;
                        break;
                    }
                }
        }
        if (gaps)
            out.push_back({lang::trf("Cropland leaves a narrow gap at the {1} edge ({2} places)",
                                     {lang::tr(kEdge[e]), Num(gaps)})});
    }
}

}  // namespace

std::vector<Finding> Validate() {
    std::vector<Finding> out;
    int sup = Supported(), rec = Recommended();
    if (rec > sup)
        out.push_back({lang::trf("Recommended players ({1}) is more than supported ({2})", {Num(rec), Num(sup)})});

    // Players, start points, Shamans.
    std::vector<int> present;
    int w = TilesW() / 2, h = TilesH() / 2;
    for (int i = 0; i < 8; ++i) {
        if (!Player(i)) {
            if (i < sup) out.push_back({lang::trf("Player {1} is None, but the map supports {2}", {Num(i + 1), Num(sup)})});
            continue;
        }
        present.push_back(i);
        if (i >= sup)
            out.push_back({lang::trf("Player {1} exists, but the map supports only {2}", {Num(i + 1), Num(sup)})});
        PlayerRec r = CapturePlayer(i);
        if (r.startX < 0 || r.startY < 0 || r.startX >= w || r.startY >= h)
            out.push_back({lang::trf("Player {1} has no start point on the map", {Num(i + 1)})});
        else if (r.startX == 0 && r.startY == 0)
            out.push_back({lang::trf("Player {1}'s start point is still the top-left corner", {Num(i + 1)})});
    }
    std::vector<int> shamans(10), labs(10), psi(10);
    int ownerless = 0, cropsOff = 0;
    for (int id = 0; id < ThingCount(); ++id) {
        uintptr_t t = Thing(id);
        if (!t) continue;
        int type = TypeOf(t), owner = std::min(OwnerOf(t), 9);
        if ((IsCreature(type) || IsDevice(type)) && (owner >= 8 || !Player(owner))) ++ownerless;
        if (IsShaman(type)) ++shamans[owner];
        if (type == kGeneLab || type == kGeneLabPlus) ++labs[owner];
        if (type == kPsi || type == kPsiPlus) ++psi[owner];
        if (IsCrop(type) && TerrainType(CentreX(t) >> 5, CentreY(t) >> 5) != 2) ++cropsOff;
    }
    for (int i : present)
        if (!shamans[i] && !(labs[i] && psi[i]))
            out.push_back({lang::trf("Player {1} has no Shaman (or Gene Lab and Psi Tower)", {Num(i + 1)})});
    if (ownerless) out.push_back({lang::trf("{1} units or devices belong to no player", {Num(ownerless)})});
    if (cropsOff) out.push_back({lang::trf("{1} crops sit off cropland (the game drops them)", {Num(cropsOff)})});

    CheckPower(out);

    // Ground reachability between start points.
    Ground g;
    if (LoadGround(g)) {
        int ref = -1, rx = 0, ry = 0;
        std::vector<uint8_t> reach;
        for (int i : present) {
            PlayerRec r = CapturePlayer(i);
            if (r.startX < 0 || r.startY < 0 || r.startX >= w || r.startY >= h) continue;
            int x, y;
            if (!g.Seed(r.startX, r.startY, x, y)) {
                out.push_back({lang::trf("Player {1}'s start point is on blocked ground", {Num(i + 1)})});
                continue;
            }
            if (ref < 0) {
                ref = i;
                rx = x;
                ry = y;
                reach = g.Fill(rx, ry);
            } else if (!reach[y * g.w + x]) {
                out.push_back({lang::trf("Player {1} can't reach player {2}'s start on foot", {Num(i + 1), Num(ref + 1)})});
            }
        }
    }
    CheckCroplandEdges(out);
    return out;
}

}  // namespace editor
