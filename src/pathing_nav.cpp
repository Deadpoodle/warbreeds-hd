#include "pathing_nav.h"

#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <chrono>

namespace nav {
namespace {

constexpr int kMargin = 128;  // px^2 added to the circle test on the lattice (see the header)
constexpr int kCell = 64;     // px, spatial buckets for circles
constexpr int kNoRegion = 0;

int g_w, g_h;                   // tiles
int g_lw, g_lh;                 // lattice nodes (2w+1 x 2h+1)
int g_cw, g_ch;                 // 64 px bucket cells
std::vector<uint8_t> g_tile;    // 1 = blocked
bool g_ready;

// Static circles and their buckets.
std::vector<Circle> g_static;
std::vector<std::vector<int>> g_staticCells;
std::vector<uint8_t> g_staticTile;  // tile holds a static thing's top-left
std::vector<uint8_t> g_clearS;      // per node: radii below this fit (0 = nothing fits)

// Dynamic things.
struct Dyn {
    int id, x, y, r, tlx, tly;  // r: bounding radius of the circles round (x, y)
    uint32_t known;             // bit per player
    bool alive;
    std::vector<Circle> c;
};
std::vector<Dyn> g_dyn;
int g_dynMaxR;  // largest bounding radius so far (sets how far circle queries look)
std::vector<int> g_dynIndex(65536, -1);
std::vector<std::vector<int>> g_dynCells;
struct Layer {
    bool active = false;
    int version = 0;
    std::vector<uint8_t> clear;  // per node, as g_clearS, buildings only
    std::vector<uint8_t> occ;    // per tile: known things with their top-left here
};
Layer g_layer[kMaxPlayers];
struct Dirty {
    int x0, y0, x1, y1;  // px
    uint32_t players;
};
std::vector<Dirty> g_dirty;

std::vector<Circle> g_temp;  // SetTemporary
std::vector<Site> g_sites;   // SetSites
int g_sitesVersion;
int g_queryUnit = -1, g_querySiteX = -1, g_querySiteY = -1;

bool SiteHit(int x, int y, int r, int p, int margin) {
    for (const Site& s : g_sites)
        if (s.player == p && s.builder != g_queryUnit && !(s.siteX == g_querySiteX && s.siteY == g_querySiteY)) {
            int dx = x - s.x, dy = y - s.y, reach = s.fixed ? s.half : s.half + r;
            if (dx * dx + dy * dy < reach * reach + margin) return true;
        }
    return false;
}

bool TempHit(int x, int y, int r, int margin) {
    for (const Circle& c : g_temp) {
        int dx = x - c.x, dy = y - c.y, s = c.r + r;
        if (dx * dx + dy * dy < s * s + margin) return true;
    }
    return false;
}

// Static regions per radius (computed on first use).
std::vector<uint16_t> g_region[kMaxR + 1];

int ISqrt(int v) {
    if (v <= 0) return 0;
    int r = 0;
    for (int b = 1 << 15; b; b >>= 1)
        if ((r + b) * (r + b) <= v) r += b;
    return r;
}

int FloorDiv(int v, int d) { return v >= 0 ? v / d : -((-v + d - 1) / d); }

bool TileBlocked(int tx, int ty) {
    return tx < 0 || ty < 0 || tx >= g_w || ty >= g_h || g_tile[ty * g_w + tx];
}

int CellOf(int x, int y) {
    int cx = std::clamp(x / kCell, 0, g_cw - 1), cy = std::clamp(y / kCell, 0, g_ch - 1);
    return cy * g_cw + cx;
}

// The clearance a circle (cx, cy, R) leaves the point (x, y), with the lattice margin.
int CircleClear(int x, int y, int cx, int cy, int R) {
    int dx = x - cx, dy = y - cy, d2 = dx * dx + dy * dy - kMargin;
    if (d2 < R * R) return 0;
    return std::min(kMaxR + 1, ISqrt(d2) - R + 1);
}

// Apply circle (cx, cy, R) to a clearance array over the nodes it reaches.
void StampCircle(std::vector<uint8_t>& clear, int cx, int cy, int R, int x0, int y0, int x1, int y1) {
    int reach = R + kMaxR + 12;
    int i0 = std::max(0, FloorDiv(std::max(cx - reach, x0), kLattice));
    int i1 = std::min(g_lw - 1, FloorDiv(std::min(cx + reach, x1), kLattice));
    int j0 = std::max(0, FloorDiv(std::max(cy - reach, y0), kLattice));
    int j1 = std::min(g_lh - 1, FloorDiv(std::min(cy + reach, y1), kLattice));
    for (int j = j0; j <= j1; ++j)
        for (int i = i0; i <= i1; ++i) {
            int c = CircleClear(i * kLattice, j * kLattice, cx, cy, R);
            uint8_t& v = clear[j * g_lw + i];
            if (c < v) v = (uint8_t)c;
        }
}

int TerrainClear(int x, int y) {
    int bx0 = 1 << 30, bx1 = -1, by0 = 1 << 30, by1 = -1;
    for (int r = 0; r <= kMaxR; ++r) {
        if (x - r < 0 || y - r < 0) return r;
        int x0 = (x - r) >> 5, x1 = (x + r) >> 5, y0 = (y - r) >> 5, y1 = (y + r) >> 5;
        if (x0 == bx0 && x1 == bx1 && y0 == by0 && y1 == by1) continue;
        for (int ty = y0; ty <= y1; ++ty)
            for (int tx = x0; tx <= x1; ++tx)
                if (TileBlocked(tx, ty)) return r;
        bx0 = x0, bx1 = x1, by0 = y0, by1 = y1;
    }
    return kMaxR + 1;
}

bool Knows(const Dyn& d, int p) { return p == kAll || (p >= 0 && p < kMaxPlayers && (d.known >> p & 1)); }

bool Occupied(int x, int y, int hf, int p) {
    int tx = std::max(0, FloorDiv(x - hf, 32)), ty = std::max(0, FloorDiv(y - hf, 32));
    if (tx >= g_w || ty >= g_h) return false;
    if (g_staticTile[ty * g_w + tx]) return true;
    // Things are bucketed by centre; a top-left is within 64 px of it.
    int cx = tx * 32 / kCell, cy = ty * 32 / kCell;
    for (int j = cy - 1; j <= cy + 2; ++j)
        for (int i = cx - 1; i <= cx + 2; ++i) {
            if (i < 0 || j < 0 || i >= g_cw || j >= g_ch) continue;
            for (int idx : g_dynCells[j * g_cw + i]) {
                const Dyn& d = g_dyn[idx];
                if (d.tlx == tx && d.tly == ty && Knows(d, p)) return true;
            }
        }
    return false;
}

// --- lattice nodes ----------------------------------------------------------

bool NodeFree(int i, int j, int r, int hf, int p) {
    if (i < 0 || j < 0 || i >= g_lw || j >= g_lh) return false;
    int n = j * g_lw + i;
    if (g_clearS[n] <= r) return false;
    int tx = std::max(0, FloorDiv(i * kLattice - hf, 32)), ty = std::max(0, FloorDiv(j * kLattice - hf, 32));
    bool inMap = tx < g_w && ty < g_h;
    if (inMap && g_staticTile[ty * g_w + tx]) return false;
    if (!g_temp.empty() && TempHit(i * kLattice, j * kLattice, r, kMargin)) return false;
    if (!g_sites.empty() && SiteHit(i * kLattice, j * kLattice, r, p, kMargin)) return false;
    if (p >= 0 && p < kMaxPlayers && g_layer[p].active) {
        if (g_layer[p].clear[n] <= r) return false;
        if (inMap && g_layer[p].occ[ty * g_w + tx]) return false;
    }
    return true;
}

bool NodeFreeStatic(int n, int r) { return g_clearS[n] > r; }

constexpr int kDx[8] = {1, 0, -1, 0, 1, -1, -1, 1};
constexpr int kDy[8] = {0, 1, 0, -1, 1, 1, -1, -1};
constexpr int kCost[8] = {10, 10, 10, 10, 14, 14, 14, 14};

const std::vector<uint16_t>& Regions(int r) {
    r = std::clamp(r, 0, kMaxR);
    std::vector<uint16_t>& reg = g_region[r];
    if (!reg.empty()) return reg;
    reg.assign(g_lw * g_lh, kNoRegion);
    std::vector<int> queue;
    uint16_t next = 1;
    for (int start = 0; start < g_lw * g_lh; ++start) {
        if (reg[start] != kNoRegion || !NodeFreeStatic(start, r)) continue;
        uint16_t label = next < 0xffff ? next++ : next;
        reg[start] = label;
        queue.assign(1, start);
        for (size_t q = 0; q < queue.size(); ++q) {
            int n = queue[q], i = n % g_lw, j = n / g_lw;
            for (int d = 0; d < 8; ++d) {
                int ni = i + kDx[d], nj = j + kDy[d];
                if (ni < 0 || nj < 0 || ni >= g_lw || nj >= g_lh) continue;
                int m = nj * g_lw + ni;
                if (reg[m] != kNoRegion || !NodeFreeStatic(m, r)) continue;
                if (d >= 4 && (!NodeFreeStatic(j * g_lw + ni, r) || !NodeFreeStatic(nj * g_lw + i, r))) continue;
                reg[m] = label;
                queue.push_back(m);
            }
        }
    }
    return reg;
}

int RegionOfNode(int i, int j, int r) {
    if (i < 0 || j < 0 || i >= g_lw || j >= g_lh) return kNoRegion;
    return Regions(r)[j * g_lw + i];
}

// Regions with a player's known buildings as well (its layer; not the build sites
// or temporary circles, which depend on the asking unit): the static regions know
// only terrain and rocks, so a goal walled off by buildings looked reachable, and
// the search explored everything the unit could reach (some 100,000 nodes, tens of
// ms) before settling for the nearest point. Filled on first use for each player,
// radius and half footprint, and again once the layer has changed, but at most
// every kDynRefresh turns (1.5 s; a battle changes it all the time): a stale fill only
// costs a search more expansions.
constexpr int kDynRefresh = 60;
int g_clock;
struct DynRegions {
    int p, r, hf, version, filledAt;
    std::vector<uint16_t> reg;
};
std::vector<DynRegions> g_dynRegions;
int g_fills;
int64_t g_fillUs;

bool NodeFreeLayer(int i, int j, int r, int hf, int p) {
    int n = j * g_lw + i;
    if (g_clearS[n] <= r) return false;
    int tx = std::max(0, FloorDiv(i * kLattice - hf, 32)), ty = std::max(0, FloorDiv(j * kLattice - hf, 32));
    bool inMap = tx < g_w && ty < g_h;
    if (inMap && g_staticTile[ty * g_w + tx]) return false;
    if (g_layer[p].clear[n] <= r) return false;
    return !(inMap && g_layer[p].occ[ty * g_w + tx]);
}

DynRegions* FindLayerRegions(int r, int hf, int p) {
    for (DynRegions& e : g_dynRegions)
        if (e.p == p && e.r == r && e.hf == hf) return &e;
    return nullptr;
}

// fresh: refill a fill the layer has changed since, however recent.
const std::vector<uint16_t>* LayerRegions(int r, int hf, int p, bool fresh = false) {
    if (p < 0 || p >= kMaxPlayers || !g_layer[p].active) return nullptr;
    DynRegions* d = FindLayerRegions(r, hf, p);
    if (d && (d->version == g_layer[p].version || (!fresh && g_clock - d->filledAt < kDynRefresh))) return &d->reg;
    if (!d) {
        if (g_dynRegions.size() >= g_dynRegions.capacity()) return nullptr;  // (never: 64 is plenty)
        g_dynRegions.push_back({p, r, hf, -1, 0, {}});
        d = &g_dynRegions.back();
    }
    d->version = g_layer[p].version, d->filledAt = g_clock;
    ++g_fills;
    auto t0 = std::chrono::steady_clock::now();
    // Each node's test once (the flood would ask up to 24 times), then the flood over bytes.
    static std::vector<uint8_t> freeNode;
    freeNode.resize(g_lw * g_lh);
    for (int j = 0; j < g_lh; ++j)
        for (int i = 0; i < g_lw; ++i) freeNode[j * g_lw + i] = NodeFreeLayer(i, j, r, hf, p);
    std::vector<uint16_t>& reg = d->reg;
    reg.assign(g_lw * g_lh, kNoRegion);
    static std::vector<int> queue;
    uint16_t next = 1;
    for (int start = 0; start < g_lw * g_lh; ++start) {
        if (reg[start] != kNoRegion || !freeNode[start]) continue;
        uint16_t label = next < 0xffff ? next++ : next;
        reg[start] = label;
        queue.assign(1, start);
        for (size_t q = 0; q < queue.size(); ++q) {
            int n = queue[q], i = n % g_lw, j = n / g_lw;
            for (int dd = 0; dd < 8; ++dd) {
                int ni = i + kDx[dd], nj = j + kDy[dd];
                if (ni < 0 || nj < 0 || ni >= g_lw || nj >= g_lh) continue;
                int m = nj * g_lw + ni;
                if (reg[m] != kNoRegion || !freeNode[m]) continue;
                if (dd >= 4 && (!freeNode[j * g_lw + ni] || !freeNode[nj * g_lw + i])) continue;
                reg[m] = label;
                queue.push_back(m);
            }
        }
    }
    g_fillUs += std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
    return &reg;
}

// The region of node (i, j) for the unit: with its player's buildings where known.
int UnitRegion(const std::vector<uint16_t>* layer, int i, int j, int r) {
    if (i < 0 || j < 0 || i >= g_lw || j >= g_lh) return kNoRegion;
    return layer ? (*layer)[j * g_lw + i] : RegionOfNode(i, j, r);
}

// Nodes around (x, y) in rings of growing Chebyshev radius, each ring in a fixed
// order; `visit(i, j)` returns true to stop. Stops after maxRing rings.
template <typename F>
void Rings(int x, int y, int maxRing, F visit) {
    int ci = (x + kLattice / 2) / kLattice, cj = (y + kLattice / 2) / kLattice;
    for (int k = 0; k <= maxRing; ++k) {
        if (k == 0) {
            if (visit(ci, cj, 0)) return;
            continue;
        }
        for (int t = -k; t < k; ++t) {  // top row left to right, right column down, ...
            if (visit(ci + t, cj - k, k)) return;
        }
        for (int t = -k; t < k; ++t)
            if (visit(ci + k, cj + t, k)) return;
        for (int t = k; t > -k; --t)
            if (visit(ci + t, cj + k, k)) return;
        for (int t = k; t > -k; --t)
            if (visit(ci - k, cj + t, k)) return;
    }
}

// The nearest free node to (x, y) that passes `ok`, searched up to maxRing rings;
// false if none.
template <typename F>
bool NearestNode(int x, int y, int maxRing, int& bi, int& bj, F ok) {
    int best = -1;
    int stopRing = maxRing;
    Rings(x, y, maxRing, [&](int i, int j, int k) {
        if (k > stopRing) return true;
        if (!ok(i, j)) return false;
        int dx = i * kLattice - x, dy = j * kLattice - y, d2 = dx * dx + dy * dy;
        if (best < 0 || d2 < best) {
            best = d2, bi = i, bj = j;
            // A nearer node can only be in a ring whose inner edge is closer than this one.
            int s = ISqrt(d2) / kLattice + 2;
            if (s < stopRing) stopRing = s;
        }
        return false;
    });
    return best >= 0;
}

int MaxRing() { return std::max(g_lw, g_lh); }

// --- A* ---------------------------------------------------------------------

std::vector<int> g_g, g_parent;
std::vector<uint32_t> g_seen, g_closed;
uint32_t g_gen;
struct Open {
    int f, h;
    uint32_t seq;
    int node;
};
struct OpenLess {  // min-heap order: f, then h, then first pushed
    bool operator()(const Open& a, const Open& b) const {
        if (a.f != b.f) return a.f > b.f;
        if (a.h != b.h) return a.h > b.h;
        return a.seq > b.seq;
    }
};
std::vector<Open> g_open;

int Octile(int i, int j, int gi, int gj) {
    int dx = abs(i - gi), dy = abs(j - gj);
    return 10 * std::max(dx, dy) + 4 * std::min(dx, dy);
}

// A* from node s to node t over free nodes; fills `path` (start first). Returns
// true when t was reached, else the path leads to the closed node nearest t.
bool Search(int si, int sj, int ti, int tj, int r, int hf, int p, int maxExpansions, std::vector<Pt>& path,
            int& expansions) {
    int count = g_lw * g_lh;
    if ((int)g_g.size() != count) {
        g_g.assign(count, 0);
        g_parent.assign(count, -1);
        g_seen.assign(count, 0);
        g_closed.assign(count, 0);
        g_gen = 0;
    }
    if (++g_gen == 0) {
        std::fill(g_seen.begin(), g_seen.end(), 0);
        std::fill(g_closed.begin(), g_closed.end(), 0);
        g_gen = 1;
    }
    g_open.clear();
    uint32_t seq = 0;
    int s = sj * g_lw + si, t = tj * g_lw + ti;
    g_g[s] = 0;
    g_parent[s] = -1;
    g_seen[s] = g_gen;
    int h0 = Octile(si, sj, ti, tj);
    g_open.push_back({h0, h0, seq++, s});
    int best = s, bestH = h0;
    expansions = 0;
    bool found = false;
    while (!g_open.empty()) {
        std::pop_heap(g_open.begin(), g_open.end(), OpenLess());
        Open o = g_open.back();
        g_open.pop_back();
        int n = o.node;
        if (g_closed[n] == g_gen) continue;
        g_closed[n] = g_gen;
        if (o.h < bestH) best = n, bestH = o.h;
        if (n == t) {
            found = true;
            best = n;
            break;
        }
        if (++expansions > maxExpansions) break;
        int i = n % g_lw, j = n / g_lw;
        for (int d = 0; d < 8; ++d) {
            int ni = i + kDx[d], nj = j + kDy[d];
            if (!NodeFree(ni, nj, r, hf, p)) continue;
            if (d >= 4 && (!NodeFree(ni, j, r, hf, p) || !NodeFree(i, nj, r, hf, p))) continue;
            int m = nj * g_lw + ni;
            if (g_closed[m] == g_gen) continue;
            int g = g_g[n] + kCost[d];
            if (g_seen[m] == g_gen && g >= g_g[m]) continue;
            g_seen[m] = g_gen;
            g_g[m] = g;
            g_parent[m] = n;
            int h = Octile(ni, nj, ti, tj);
            g_open.push_back({g + h, h, seq++, m});
            std::push_heap(g_open.begin(), g_open.end(), OpenLess());
        }
    }
    path.clear();
    for (int n = best; n >= 0; n = g_parent[n]) path.push_back({n % g_lw * kLattice, n / g_lw * kLattice});
    std::reverse(path.begin(), path.end());
    return found;
}

void MarkDirty(int x, int y, int r, uint32_t players) {
    int reach = r + kMaxR + 12 + 64;
    g_dirty.push_back({x - reach, y - reach, x + reach, y + reach, players});
}

uint32_t ActivePlayers() {
    uint32_t m = 0;
    for (int p = 0; p < kMaxPlayers; ++p)
        if (g_layer[p].active) m |= 1u << p;
    return m;
}

void RebuildLayerRect(int p, int x0, int y0, int x1, int y1) {
    Layer& L = g_layer[p];
    ++L.version;
    int i0 = std::max(0, FloorDiv(x0, kLattice)), i1 = std::min(g_lw - 1, FloorDiv(x1, kLattice));
    int j0 = std::max(0, FloorDiv(y0, kLattice)), j1 = std::min(g_lh - 1, FloorDiv(y1, kLattice));
    if (i0 > i1 || j0 > j1) return;
    for (int j = j0; j <= j1; ++j)
        for (int i = i0; i <= i1; ++i) L.clear[j * g_lw + i] = kMaxR + 1;
    int tx0 = std::max(0, FloorDiv(x0, 32)), tx1 = std::min(g_w - 1, FloorDiv(x1, 32));
    int ty0 = std::max(0, FloorDiv(y0, 32)), ty1 = std::min(g_h - 1, FloorDiv(y1, 32));
    for (int ty = ty0; ty <= ty1; ++ty)
        for (int tx = tx0; tx <= tx1; ++tx) L.occ[ty * g_w + tx] = 0;
    for (const Dyn& d : g_dyn) {
        if (!d.alive || !(d.known >> p & 1)) continue;
        for (const Circle& c : d.c)
            StampCircle(L.clear, c.x, c.y, c.r, i0 * kLattice, j0 * kLattice, i1 * kLattice, j1 * kLattice);
        if (d.tlx >= tx0 && d.tlx <= tx1 && d.tly >= ty0 && d.tly <= ty1 && d.tlx >= 0 && d.tly >= 0 && d.tlx < g_w &&
            d.tly < g_h)
            L.occ[d.tly * g_w + d.tlx] = 1;
    }
}

void ActivateLayer(int p) {
    Layer& L = g_layer[p];
    if (L.active) return;
    L.active = true;
    L.clear.assign(g_lw * g_lh, kMaxR + 1);
    L.occ.assign(g_w * g_h, 0);
    RebuildLayerRect(p, 0, 0, g_lw * kLattice, g_lh * kLattice);
}

bool CirclesHit(int x, int y, int r, int p) {
    int cx = x / kCell, cy = y / kCell;
    int reach = std::max(2, (std::max(g_dynMaxR, 2 * kMaxR) + r) / kCell + 1);
    for (int j = cy - reach; j <= cy + reach; ++j)
        for (int i = cx - reach; i <= cx + reach; ++i) {
            if (i < 0 || j < 0 || i >= g_cw || j >= g_ch) continue;
            for (int idx : g_staticCells[j * g_cw + i]) {
                const Circle& c = g_static[idx];
                int dx = x - c.x, dy = y - c.y, s = c.r + r;
                if (dx * dx + dy * dy < s * s) return true;
            }
            for (int idx : g_dynCells[j * g_cw + i]) {
                const Dyn& d = g_dyn[idx];
                if (!Knows(d, p)) continue;
                int dx = x - d.x, dy = y - d.y, s = d.r + r;
                if (dx * dx + dy * dy >= s * s) continue;
                for (const Circle& c : d.c) {
                    int cx = x - c.x, cy = y - c.y, cs = c.r + r;
                    if (cx * cx + cy * cy < cs * cs) return true;
                }
            }
        }
    return false;
}

void DynCellRemove(int idx) {
    std::vector<int>& v = g_dynCells[CellOf(g_dyn[idx].x, g_dyn[idx].y)];
    v.erase(std::remove(v.begin(), v.end(), idx), v.end());
}

void DynCellAdd(int idx) {
    std::vector<int>& v = g_dynCells[CellOf(g_dyn[idx].x, g_dyn[idx].y)];
    v.insert(std::upper_bound(v.begin(), v.end(), idx), idx);
}

}  // namespace

// --- building ---------------------------------------------------------------

void BeginStatic(int w, int h, const uint8_t* blocked) {
    g_ready = false;
    g_w = w, g_h = h;
    g_lw = 2 * w + 1, g_lh = 2 * h + 1;
    g_cw = (w * 32 + kCell - 1) / kCell + 1, g_ch = (h * 32 + kCell - 1) / kCell + 1;
    g_tile.assign(blocked, blocked + w * h);
    g_static.clear();
    g_staticCells.assign(g_cw * g_ch, {});
    g_staticTile.assign(w * h, 0);
    if ((int)g_dynCells.size() != g_cw * g_ch) {
        g_dynCells.assign(g_cw * g_ch, {});
        for (size_t i = 0; i < g_dyn.size(); ++i)
            if (g_dyn[i].alive) DynCellAdd((int)i);
    }
}

void AddStaticCircle(int x, int y, int r) {
    g_staticCells[CellOf(x, y)].push_back((int)g_static.size());
    g_static.push_back({x, y, r});
}

void AddStaticTile(int tx, int ty) {
    if (tx >= 0 && ty >= 0 && tx < g_w && ty < g_h) g_staticTile[ty * g_w + tx] = 1;
}

void FinishStatic() {
    g_clearS.assign(g_lw * g_lh, 0);
    for (int j = 0; j < g_lh; ++j)
        for (int i = 0; i < g_lw; ++i) g_clearS[j * g_lw + i] = (uint8_t)TerrainClear(i * kLattice, j * kLattice);
    for (const Circle& c : g_static) StampCircle(g_clearS, c.x, c.y, c.r, 0, 0, g_lw * kLattice, g_lh * kLattice);
    for (auto& reg : g_region) reg.clear();
    g_dynRegions.clear();
    g_dynRegions.reserve(64);  // pointers into it stay valid while it grows (a handful of sizes per player)
    for (int p = 0; p < kMaxPlayers; ++p)
        if (g_layer[p].active) {
            g_layer[p].active = false;
            ActivateLayer(p);
        }
    g_dirty.clear();
    g_ready = true;
}

bool Ready() { return g_ready; }
int TilesW() { return g_w; }
int TilesH() { return g_h; }

void UpsertDynamic(int id, int x, int y, int tlx, int tly, const Circle* circles, int n) {
    id &= 0xffff;
    int r = 0;
    for (int k = 0; k < n; ++k) {
        int dx = circles[k].x - x, dy = circles[k].y - y;
        r = std::max(r, ISqrt(dx * dx + dy * dy) + 1 + circles[k].r);
    }
    g_dynMaxR = std::max(g_dynMaxR, r);
    auto same = [&](const Dyn& d) {
        if (d.x != x || d.y != y || d.tlx != tlx || d.tly != tly || (int)d.c.size() != n) return false;
        for (int k = 0; k < n; ++k)
            if (d.c[k].x != circles[k].x || d.c[k].y != circles[k].y || d.c[k].r != circles[k].r) return false;
        return true;
    };
    int idx = g_dynIndex[id];
    if (idx >= 0) {
        Dyn& d = g_dyn[idx];
        if (same(d)) return;
        MarkDirty(d.x, d.y, d.r, d.known);
        DynCellRemove(idx);
        d.x = x, d.y = y, d.r = r, d.tlx = tlx, d.tly = tly;
        d.c.assign(circles, circles + n);
        DynCellAdd(idx);
        MarkDirty(x, y, r, d.known);
        return;
    }
    // Reuse a free slot (lowest first) so indices stay small and the order fixed.
    idx = -1;
    for (size_t i = 0; i < g_dyn.size(); ++i)
        if (!g_dyn[i].alive) {
            idx = (int)i;
            break;
        }
    if (idx < 0) {
        idx = (int)g_dyn.size();
        g_dyn.push_back({});
    }
    g_dyn[idx] = {id, x, y, r, tlx, tly, 0, true, std::vector<Circle>(circles, circles + n)};
    g_dynIndex[id] = idx;
    if (!g_dynCells.empty()) DynCellAdd(idx);
}

void RemoveDynamic(int id) {
    id &= 0xffff;
    int idx = g_dynIndex[id];
    if (idx < 0) return;
    Dyn& d = g_dyn[idx];
    MarkDirty(d.x, d.y, d.r, d.known);
    DynCellRemove(idx);
    d.alive = false;
    g_dynIndex[id] = -1;
}

void ClearDynamic() {
    g_sites.clear();
    ++g_sitesVersion;
    for (const Dyn& d : g_dyn)
        if (d.alive) g_dynIndex[d.id] = -1;
    g_dyn.clear();
    for (auto& v : g_dynCells) v.clear();
    for (Layer& L : g_layer) L = Layer();
    g_dirty.clear();
}

void SetKnown(int id, int p) {
    int idx = g_dynIndex[id & 0xffff];
    if (idx < 0 || p < 0 || p >= kMaxPlayers) return;
    Dyn& d = g_dyn[idx];
    if (d.known >> p & 1) return;
    d.known |= 1u << p;
    MarkDirty(d.x, d.y, d.r, 1u << p);
}

bool IsKnown(int id, int p) {
    int idx = g_dynIndex[id & 0xffff];
    return idx >= 0 && p >= 0 && p < kMaxPlayers && (g_dyn[idx].known >> p & 1);
}

void DynamicIds(std::vector<int>& out) {
    out.clear();
    for (const Dyn& d : g_dyn)
        if (d.alive) out.push_back(d.id);
    std::sort(out.begin(), out.end());
}

void RefreshDynamic() {
    if (!g_ready) return;
    uint32_t active = ActivePlayers();
    for (const Dirty& d : g_dirty)
        for (int p = 0; p < kMaxPlayers; ++p)
            if ((d.players & active) >> p & 1) RebuildLayerRect(p, d.x0, d.y0, d.x1, d.y1);
    g_dirty.clear();
}

// --- queries ----------------------------------------------------------------

bool TerrainBoxBlocked(int x0, int y0, int x1, int y1) {
    if (x0 < 0 || y0 < 0) return true;
    for (int ty = y0 >> 5; ty <= y1 >> 5; ++ty)
        for (int tx = x0 >> 5; tx <= x1 >> 5; ++tx)
            if (TileBlocked(tx, ty)) return true;
    return false;
}

bool Passable(int x, int y, int r, int hf, int p) {
    if (!g_ready) return true;
    if (TerrainBoxBlocked(x - r, y - r, x + r, y + r)) return false;
    if (CirclesHit(x, y, r, p)) return false;
    if (!g_temp.empty() && TempHit(x, y, r, 0)) return false;
    if (!g_sites.empty() && SiteHit(x, y, r, p, 0)) return false;
    return !Occupied(x, y, hf, p);
}

void SetSites(const std::vector<Site>& sites) {
    auto same = [](const Site& a, const Site& b) {
        return a.x == b.x && a.y == b.y && a.half == b.half && a.player == b.player && a.builder == b.builder &&
               a.siteX == b.siteX && a.siteY == b.siteY && a.fixed == b.fixed;
    };
    if (sites.size() == g_sites.size() && std::equal(sites.begin(), sites.end(), g_sites.begin(), same)) return;
    g_sites = sites;
    ++g_sitesVersion;  // routes re-check (LayerVersion)
}

void SetQueryUnit(int id, int siteX, int siteY) {
    g_queryUnit = id, g_querySiteX = siteX, g_querySiteY = siteY;
}

bool OnSite(int x, int y, int r, int p) { return SiteHit(x, y, r, p, 0); }

void SetTemporary(const Circle* circles, int n) { g_temp.assign(circles, circles + n); }

int RouteLength(const Route& route, size_t from, Pt pos) {
    int len = 0;
    for (size_t k = from; k < route.nodes.size(); ++k) {
        int dx = route.nodes[k].x - pos.x, dy = route.nodes[k].y - pos.y;
        len += ISqrt(dx * dx + dy * dy);
        pos = route.nodes[k];
    }
    return len;
}

bool ThingsBlocked(int x, int y, int r, int hf, int p) {
    return g_ready && (CirclesHit(x, y, r, p) || Occupied(x, y, hf, p));
}

int Region(int x, int y, int r) {
    if (!g_ready) return kNoRegion;
    return RegionOfNode((x + kLattice / 2) / kLattice, (y + kLattice / 2) / kLattice, r);
}

bool LineClear(Pt a, Pt b, int step, int r, int hf, int p) {
    bool reached = false;
    int dx = abs(b.x - a.x), dy = abs(b.y - a.y), n = (std::max(dx, dy) + step - 1) / std::max(1, step);
    if (n == 0) return true;
    std::vector<Pt> tmp(n);
    return Steps(a, b, step, r, hf, p, tmp.data(), n, &reached) == n && reached;
}

int Steps(Pt a, Pt b, int step, int r, int hf, int p, Pt* out, int max, bool* reached) {
    *reached = false;
    int dx = b.x - a.x, dy = b.y - a.y;
    int major = std::max(abs(dx), abs(dy));
    step = std::max(1, step);
    int n = (major + step - 1) / step;
    if (n == 0) {
        *reached = true;
        return 0;
    }
    auto div = [](int v, int d) { return v >= 0 ? (v + d / 2) / d : -((-v + d / 2) / d); };
    int made = 0;
    for (int i = 1; i <= n && made < max; ++i) {
        Pt s{a.x + div(dx * i, n), a.y + div(dy * i, n)};
        if (!Passable(s.x, s.y, r, hf, p)) return made;
        out[made++] = s;
        if (i == n) *reached = true;
    }
    return made;
}

// --- routes -----------------------------------------------------------------

bool NearestReachable(int sx, int sy, int gx, int gy, int r, int hf, int p, Pt& out) {
    if (!g_ready) return false;
    if (p >= 0 && p < kMaxPlayers) ActivateLayer(p);
    RefreshDynamic();
    int si, sj;
    if (!NearestNode(sx, sy, 3, si, sj, [&](int i, int j) { return NodeFree(i, j, r, hf, p); }) &&
        !NearestNode(sx, sy, MaxRing(), si, sj, [&](int i, int j) { return NodeFree(i, j, r, hf, p); }))
        return false;
    const std::vector<uint16_t>* layer = LayerRegions(r, hf, p);
    auto RegionOfNode = [&](int i, int j, int rr) { return UnitRegion(layer, i, j, rr); };
    int region = RegionOfNode(si, sj, r);
    if (region == kNoRegion) layer = nullptr, region = UnitRegion(nullptr, si, sj, r);  // stale fill: the static one
    // The goal itself: passable and next to a node of the unit's region it can be walked to from.
    if (Passable(gx, gy, r, hf, p)) {
        int gi = gx / kLattice, gj = gy / kLattice;
        for (int dj = 0; dj <= 1; ++dj)
            for (int di = 0; di <= 1; ++di) {
                int i = gi + di, j = gj + dj;
                if (NodeFree(i, j, r, hf, p) && RegionOfNode(i, j, r) == region &&
                    LineClear({i * kLattice, j * kLattice}, {gx, gy}, 4, r, hf, p)) {
                    out = {gx, gy};
                    return true;
                }
            }
    }
    int bi, bj;
    if (!NearestNode(gx, gy, MaxRing(), bi, bj,
                     [&](int i, int j) { return NodeFree(i, j, r, hf, p) && RegionOfNode(i, j, r) == region; }))
        return false;
    out = {bi * kLattice, bj * kLattice};
    return true;
}

void SetClock(int turn) { g_clock = turn; }
int TakeFills() {
    int n = g_fills;
    g_fills = 0;
    return n;
}
double TakeFillMs() {
    double ms = g_fillUs / 1000.0;
    g_fillUs = 0;
    return ms;
}

bool FindRoute(int sx, int sy, int gx, int gy, int r, int hf, int p, int maxExpansions, Route& out) {
    out = Route();
    if (!g_ready) return false;
    if (p >= 0 && p < kMaxPlayers) ActivateLayer(p);
    RefreshDynamic();
    // Start: the nearest free node the unit can walk straight to, else the nearest free one.
    auto free = [&](int i, int j) { return NodeFree(i, j, r, hf, p); };
    int si, sj;
    if (!NearestNode(sx, sy, 3, si, sj,
                     [&](int i, int j) { return free(i, j) && LineClear({sx, sy}, {i * kLattice, j * kLattice}, 4, r, hf, p); }) &&
        !NearestNode(sx, sy, MaxRing(), si, sj, free))
        return false;
    Pt target;
    if (!NearestReachable(sx, sy, gx, gy, r, hf, p, target)) return false;
    if ((target.x != gx || target.y != gy) && p >= 0 && p < kMaxPlayers) {  // unreachable by a stale fill? refill, look again
        DynRegions* d = FindLayerRegions(r, hf, p);
        if (d && d->version != g_layer[p].version) {
            LayerRegions(r, hf, p, true);
            if (!NearestReachable(sx, sy, gx, gy, r, hf, p, target)) return false;
        }
    }
    out.exact = target.x == gx && target.y == gy;
    const std::vector<uint16_t>* layer = LayerRegions(r, hf, p);
    int region = UnitRegion(layer, si, sj, r);
    if (region == kNoRegion) layer = nullptr, region = RegionOfNode(si, sj, r);
    auto RegionOfNode = [&](int i, int j, int rr) { return UnitRegion(layer, i, j, rr); };
    int ti, tj;
    if (!NearestNode(target.x, target.y, MaxRing(), ti, tj,
                     [&](int i, int j) { return free(i, j) && RegionOfNode(i, j, r) == region; }))
        return false;
    bool found = Search(si, sj, ti, tj, r, hf, p, maxExpansions, out.nodes, out.expansions);
    if (found) {
        out.end = target;
        if (!(target.x == ti * kLattice && target.y == tj * kLattice)) out.nodes.push_back(target);
    } else {
        out.end = out.nodes.back();
        out.exact = false;
    }
    return true;
}

int LayerVersion(int p) { return (p >= 0 && p < kMaxPlayers ? g_layer[p].version : 0) + g_sitesVersion * 7919; }

bool RouteFree(const Route& route, size_t from, int r, int hf, int p) {
    for (size_t k = from; k < route.nodes.size(); ++k) {
        const Pt& n = route.nodes[k];
        bool onLattice = n.x % kLattice == 0 && n.y % kLattice == 0;
        if (onLattice ? !NodeFree(n.x / kLattice, n.y / kLattice, r, hf, p) : !Passable(n.x, n.y, r, hf, p)) return false;
    }
    return true;
}

// --- debug ------------------------------------------------------------------

int StaticClearance(int i, int j) { return g_clearS[j * g_lw + i]; }
int PlayerClearance(int i, int j, int p) {
    return g_layer[p].active ? g_layer[p].clear[j * g_lw + i] : kMaxR + 1;
}
int LatticeW() { return g_lw; }
int LatticeH() { return g_lh; }

}  // namespace nav
