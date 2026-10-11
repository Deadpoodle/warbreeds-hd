#include "mov.h"

#include <windows.h>
#include <string.h>

#include <algorithm>

namespace movie {
namespace {

uint32_t BE32(const uint8_t* p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
uint16_t BE16(const uint8_t* p) { return (uint16_t)(p[0] << 8 | p[1]); }
uint64_t BE64(const uint8_t* p) { return (uint64_t)BE32(p) << 32 | BE32(p + 4); }
constexpr uint32_t FourCC(const char* s) { return (uint32_t)s[0] << 24 | s[1] << 16 | s[2] << 8 | s[3]; }

// IMA ADPCM tables.
const int kSteps[89] = {7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,
                        25,    28,    31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,
                        88,    97,    107,   118,   130,   143,   157,   173,   190,   209,   230,   253,   279,
                        307,   337,   371,   408,   449,   494,   544,   598,   658,   724,   796,   876,   963,
                        1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,  2272,  2499,  2749,  3024,  3327,
                        3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,  9493,  10442, 11487,
                        12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
const int kIndex[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

// One QuickTime IMA4 packet: 2-byte header + 32 bytes = 64 samples.
void DecodeIma4Packet(const uint8_t* p, int16_t* out, int stride) {
    int header = BE16(p);
    int pred = (int16_t)(header & 0xff80);
    int index = std::clamp(header & 0x7f, 0, 88);
    for (int i = 0; i < 64; ++i) {
        int nib = (p[2 + i / 2] >> ((i & 1) * 4)) & 0xf;
        int step = kSteps[index];
        int diff = step >> 3;
        if (nib & 1) diff += step >> 2;
        if (nib & 2) diff += step >> 1;
        if (nib & 4) diff += step;
        if (nib & 8) diff = -diff;
        pred = std::clamp(pred + diff, -32768, 32767);
        index = std::clamp(index + kIndex[nib], 0, 88);
        out[i * stride] = (int16_t)pred;
    }
}

inline uint8_t Clip(int v) { return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v); }

}  // namespace

Mov::~Mov() {
    if (f_) fclose(f_);
    if (handle_) CloseHandle(handle_);
}

bool Mov::Read(uint64_t off, void* buf, size_t n) {
    if (off + n > fileSize_) return false;
    if (handle_) {
        OVERLAPPED ov{};
        ov.Offset = (DWORD)off;
        ov.OffsetHigh = (DWORD)(off >> 32);
        DWORD got = 0;
        return ReadFile(handle_, buf, (DWORD)n, &got, &ov) && got == n;
    }
    if (!f_) return false;
    if (_fseeki64(f_, (long long)off, SEEK_SET) != 0) return false;
    return fread(buf, 1, n, f_) == n;
}

bool Mov::Open(const std::string& path) {
    f_ = fopen(path.c_str(), "rb");
    if (!f_) return false;
    _fseeki64(f_, 0, SEEK_END);
    fileSize_ = (uint64_t)_ftelli64(f_);
    return Parse();
}

bool Mov::OpenHandle(void* winHandle) {
    HANDLE dup = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), (HANDLE)winHandle, GetCurrentProcess(), &dup, 0, FALSE,
                         DUPLICATE_SAME_ACCESS))
        return false;
    handle_ = dup;
    LARGE_INTEGER size;
    if (!GetFileSizeEx(dup, &size)) return false;
    fileSize_ = (uint64_t)size.QuadPart;
    return Parse();
}

bool Mov::Parse() {
    if (!ParseAtoms(0, fileSize_, nullptr)) return false;
    for (Track& t : tracks_) {
        if (t.handler == FourCC("vide") && t.codec == FourCC("cvid") && frames_.empty()) {
            frames_ = t.samples;
            width = t.w;
            height = t.h;
        } else if (t.handler == FourCC("soun") && t.codec == FourCC("ima4") && !sound_) {
            sound_ = &t;
            sampleRate = t.rate;
            channels = t.channels;
        }
    }
    if (frames_.empty() || width <= 0 || height <= 0) return false;
    rgb_.assign((size_t)width * height, 0xff000000);
    return true;
}

bool Mov::ParseAtoms(uint64_t start, uint64_t end, Track* trak) {
    uint64_t pos = start;
    while (pos + 8 <= end) {
        uint8_t h[16];
        if (!Read(pos, h, 8)) return false;
        uint64_t size = BE32(h);
        uint32_t type = BE32(h + 4);
        uint64_t hdr = 8;
        if (size == 1) {
            if (!Read(pos + 8, h + 8, 8)) return false;
            size = BE64(h + 8);
            hdr = 16;
        } else if (size == 0) {
            size = end - pos;
        }
        if (size < hdr || pos + size > end) break;
        uint64_t body = pos + hdr, len = size - hdr;
        auto payload = [&](std::vector<uint8_t>& v) {
            v.resize((size_t)len);
            return len == 0 || Read(body, v.data(), (size_t)len);
        };
        if (type == FourCC("moov") || type == FourCC("mdia") || type == FourCC("minf") || type == FourCC("stbl")) {
            if (!ParseAtoms(body, body + len, trak)) return false;
        } else if (type == FourCC("trak")) {
            Track t;
            stts_.clear(); stsc_.clear(); stsz_.clear(); stco_.clear(); stss_.clear();
            co64_ = false;
            if (!ParseAtoms(body, body + len, &t)) return false;
            if (BuildSamples(t, stts_, stsc_, stsz_, stco_, co64_, stss_)) tracks_.push_back(t);
        } else if (trak && type == FourCC("mdhd")) {
            std::vector<uint8_t> v;
            if (!payload(v) || v.size() < 24) return false;
            trak->timescale = v[0] == 1 ? BE32(&v[20]) : BE32(&v[12]);
        } else if (trak && type == FourCC("hdlr")) {
            std::vector<uint8_t> v;
            if (!payload(v) || v.size() < 12) return false;
            uint32_t sub = BE32(&v[8]);
            if (sub == FourCC("vide") || sub == FourCC("soun")) trak->handler = sub;
        } else if (trak && type == FourCC("stsd")) {
            std::vector<uint8_t> v;
            if (!payload(v) || v.size() < 16 + 20) return false;
            const uint8_t* e = &v[8];  // first sample description
            trak->codec = BE32(e + 4);
            if (trak->handler == FourCC("vide") && v.size() >= 8 + 36) {
                trak->w = BE16(e + 32);
                trak->h = BE16(e + 34);
            } else if (trak->handler == FourCC("soun") && v.size() >= 8 + 36) {
                int ver = BE16(e + 16);
                trak->channels = BE16(e + 24);
                trak->rate = (int)(BE32(e + 32) >> 16);
                if (ver == 1 && v.size() >= 8 + 52) {
                    trak->samplesPerPacket = (int)BE32(e + 36);
                    trak->bytesPerFrame = (int)BE32(e + 44);
                }
                if (trak->codec == FourCC("ima4")) {  // defaults for v0 descriptions
                    if (!trak->samplesPerPacket) trak->samplesPerPacket = 64;
                    if (!trak->bytesPerFrame) trak->bytesPerFrame = 34 * trak->channels;
                }
            }
        } else if (trak && type == FourCC("stts")) {
            if (!payload(stts_)) return false;
        } else if (trak && type == FourCC("stsc")) {
            if (!payload(stsc_)) return false;
        } else if (trak && type == FourCC("stsz")) {
            if (!payload(stsz_)) return false;
        } else if (trak && (type == FourCC("stco") || type == FourCC("co64"))) {
            if (!payload(stco_)) return false;
            co64_ = type == FourCC("co64");
        } else if (trak && type == FourCC("stss")) {
            if (!payload(stss_)) return false;
        }
        pos += size;
    }
    return true;
}

bool Mov::BuildSamples(Track& t, const std::vector<uint8_t>& stts, const std::vector<uint8_t>& stsc,
                       const std::vector<uint8_t>& stsz, const std::vector<uint8_t>& stco, bool co64,
                       const std::vector<uint8_t>& stss) {
    if (stsc.size() < 8 || stco.size() < 8 || stsz.size() < 12 || !t.timescale) return false;
    uint32_t chunks = BE32(&stco[4]);
    if (stco.size() < 8 + (size_t)chunks * (co64 ? 8 : 4)) return false;
    auto chunkOffset = [&](uint32_t c) { return co64 ? BE64(&stco[8 + c * 8]) : BE32(&stco[8 + c * 4]); };
    uint32_t stscCount = BE32(&stsc[4]);
    uint32_t fixedSize = BE32(&stsz[4]), sizeCount = BE32(&stsz[8]);
    // Sample durations.
    std::vector<uint32_t> durs;
    if (stts.size() >= 8) {
        uint32_t n = BE32(&stts[4]);
        for (uint32_t i = 0; i < n && 8 + i * 8 + 8 <= stts.size(); ++i)
            durs.insert(durs.end(), BE32(&stts[8 + i * 8]), BE32(&stts[12 + i * 8]));
    }
    bool sound = t.handler == FourCC("soun");
    uint32_t sample = 0;
    double time = 0;
    for (uint32_t c = 0; c < chunks; ++c) {
        uint32_t perChunk = 0;
        for (uint32_t e = 0; e < stscCount && 8 + e * 12 + 12 <= stsc.size(); ++e)
            if (BE32(&stsc[8 + e * 12]) <= c + 1) perChunk = BE32(&stsc[12 + e * 12]);
        uint64_t off = chunkOffset(c);
        if (sound) {
            // QuickTime sound: one entry per chunk; perChunk counts PCM frames.
            if (!t.samplesPerPacket) return false;
            uint32_t bytes = perChunk / t.samplesPerPacket * t.bytesPerFrame;
            t.samples.push_back({off, bytes, time, true});
            time += (double)perChunk / t.rate;
            continue;
        }
        for (uint32_t s = 0; s < perChunk; ++s, ++sample) {
            uint32_t sz = fixedSize ? fixedSize : (sample < sizeCount && 12 + sample * 4 + 4 <= stsz.size()
                                                      ? BE32(&stsz[12 + sample * 4]) : 0);
            t.samples.push_back({off, sz, time, stss.empty()});
            off += sz;
            time += (sample < durs.size() ? durs[sample] : 0) / (double)t.timescale;
        }
    }
    if (!stss.empty()) {
        uint32_t n = BE32(&stss[4]);
        for (uint32_t i = 0; i < n && 8 + i * 4 + 4 <= stss.size(); ++i) {
            uint32_t k = BE32(&stss[8 + i * 4]);
            if (k >= 1 && k <= t.samples.size()) t.samples[k - 1].key = true;
        }
    }
    if (!sound) duration_ = std::max(duration_, time);
    return !t.samples.empty();
}

double Mov::frameTime(int i) const { return i >= 0 && i < (int)frames_.size() ? frames_[i].time : duration_; }

bool Mov::DecodeFrame(int i) {
    if (i < 0 || i >= (int)frames_.size()) return false;
    if (i == decoded_) return true;
    int from = decoded_ + 1;
    if (i < from) from = 0;
    // Restart from the nearest key frame at or before i if that's closer.
    for (int k = i; k > from; --k)
        if (frames_[k].key) {
            from = k;
            break;
        }
    for (int k = from; k <= i; ++k) {
        const Sample& s = frames_[k];
        buf_.resize(s.size);
        if (!s.size || !Read(s.offset, buf_.data(), s.size)) continue;
        DecodeCinepak(buf_.data(), s.size);
    }
    decoded_ = i;
    return true;
}

bool Mov::DecodeAudio(std::vector<int16_t>& pcm) {
    pcm.clear();
    if (!sound_ || sound_->channels < 1 || sound_->channels > 2) return false;
    int ch = sound_->channels;
    std::vector<uint8_t> chunk;
    for (const Sample& s : sound_->samples) {
        chunk.resize(s.size);
        if (!s.size || !Read(s.offset, chunk.data(), s.size)) continue;
        size_t frameBytes = 34u * ch;
        for (size_t p = 0; p + frameBytes <= chunk.size(); p += frameBytes) {
            size_t at = pcm.size();
            pcm.resize(at + 64 * ch);
            for (int c = 0; c < ch; ++c) DecodeIma4Packet(&chunk[p + 34 * c], &pcm[at + c], ch);
        }
    }
    return !pcm.empty();
}

// Cinepak (Radius/SuperMac "cvid"). Frames are split into horizontal strips of
// 4x4 blocks; each block is drawn from a V1 codebook entry (one 2x2-Y + UV
// entry stretched over the block) or four V4 entries (one per 2x2 quadrant).
bool Mov::DecodeCinepak(const uint8_t* d, size_t size) {
    if (size < 10) return false;
    int flags = d[0];
    int strips = BE16(d + 8);
    const uint8_t* p = d + 10;
    const uint8_t* end = d + size;
    if ((int)books_.size() < strips) books_.resize(strips, std::vector<int8_t>(2 * 256 * 6, 0));
    int y0 = 0;
    for (int s = 0; s < strips; ++s) {
        if (p + 12 > end) break;
        int stripSize = BE16(p + 2);
        const uint8_t* stripEnd = std::min(end, p + std::max(stripSize, 12));
        int y1 = BE16(p + 4), x1 = BE16(p + 6), y2 = BE16(p + 8), x2 = BE16(p + 10);
        if (y1 == 0) {  // relative to the previous strip
            y1 = y0;
            y2 = y0 + BE16(p + 8);
        }
        y0 = y2;
        (void)x1;
        if (s > 0 && !(flags & 1)) books_[s] = books_[s - 1];  // inherit codebooks
        std::vector<int8_t>& book = books_[s];
        const uint8_t* q = p + 12;
        while (q + 4 <= stripEnd) {
            int id = BE16(q), csize = BE16(q + 2);
            if (csize < 4) break;
            const uint8_t* c = q + 4;
            const uint8_t* cend = std::min(stripEnd, q + csize);
            if ((id & 0xff00) == 0x2000 || (id & 0xff00) == 0x2100 || (id & 0xff00) == 0x2200 ||
                (id & 0xff00) == 0x2300 || (id & 0xff00) == 0x2400 || (id & 0xff00) == 0x2500 ||
                (id & 0xff00) == 0x2600 || (id & 0xff00) == 0x2700) {
                // Codebook: 0x20/0x24 V4 full, 0x21/0x25 V4 partial, 0x22/0x26 V1 full,
                // 0x23/0x27 V1 partial; 0x24..0x27 are greyscale (4 bytes per entry).
                int hi = id >> 8;
                bool v1 = hi == 0x22 || hi == 0x23 || hi == 0x26 || hi == 0x27;
                bool partial = hi == 0x21 || hi == 0x23 || hi == 0x25 || hi == 0x27;
                bool grey = hi >= 0x24;
                int8_t* cb = &book[(v1 ? 256 : 0) * 6];
                uint32_t mask = 0;
                int bits = 0;
                for (int e = 0; e < 256 && c < cend; ++e) {
                    if (partial) {
                        if (bits == 0) {
                            if (c + 4 > cend) break;
                            mask = BE32(c);
                            c += 4;
                            bits = 32;
                        }
                        bool take = mask & 0x80000000u;
                        mask <<= 1;
                        --bits;
                        if (!take) continue;
                    }
                    int n = grey ? 4 : 6;
                    if (c + n > cend) break;
                    int8_t* ent = cb + e * 6;
                    for (int k = 0; k < 4; ++k) ent[k] = (int8_t)c[k];
                    ent[4] = grey ? 0 : (int8_t)c[4];
                    ent[5] = grey ? 0 : (int8_t)c[5];
                    c += n;
                }
            } else if (id == 0x3000 || id == 0x3100 || id == 0x3200) {
                // Vectors: blocks in raster order over the strip.
                uint32_t mask = 0;
                int bits = 0;
                auto bit = [&]() -> int {
                    if (bits == 0) {
                        if (c + 4 > cend) return -1;
                        mask = BE32(c);
                        c += 4;
                        bits = 32;
                    }
                    int b = (mask >> 31) & 1;
                    mask <<= 1;
                    --bits;
                    return b;
                };
                auto put = [&](int bx, int by, const int8_t* e) {  // one 2x2 from an entry
                    int u = e[4], v = e[5];
                    for (int k = 0; k < 4; ++k) {
                        int x = bx + (k & 1), y = by + (k >> 1);
                        if (x >= width || y >= height) continue;
                        int yv = (uint8_t)e[k];
                        uint8_t r = Clip(yv + 2 * v), g = Clip(yv - u / 2 - v), b = Clip(yv + 2 * u);
                        rgb_[(size_t)y * width + x] = 0xff000000u | r << 16 | g << 8 | b;
                    }
                };
                for (int by = y1; by < y2 && c < cend; by += 4) {
                    for (int bx = 0; bx < x2 && bx < width; bx += 4) {
                        if (id == 0x3100) {
                            int coded = bit();
                            if (coded < 0) break;
                            if (!coded) continue;  // unchanged from the previous frame
                        }
                        int v4 = id == 0x3200 ? 0 : bit();
                        if (v4 < 0) break;
                        if (v4) {
                            if (c + 4 > cend) break;
                            for (int k = 0; k < 4; ++k) put(bx + (k & 1) * 2, by + (k >> 1) * 2, &book[c[k] * 6]);
                            c += 4;
                        } else {
                            if (c + 1 > cend) break;
                            const int8_t* e = &book[(256 + c[0]) * 6];
                            c += 1;
                            // V1: each Y covers a 2x2 quadrant of the 4x4 block.
                            for (int k = 0; k < 4; ++k) {
                                int8_t q2[6] = {e[k], e[k], e[k], e[k], e[4], e[5]};
                                put(bx + (k & 1) * 2, by + (k >> 1) * 2, q2);
                            }
                        }
                    }
                }
            }
            q += csize;
        }
        p += stripSize > 0 ? stripSize : 12;
    }
    return true;
}

}  // namespace movie
