// Decoder check: movtest <file.mov> <outdir> [frame...]
// Writes frame-<n>.bmp for each listed frame and audio.wav, and prints the
// movie's properties.
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <string>
#include <vector>

#include "../src/movie/mov.h"

static void WriteBmp(const std::string& path, const std::vector<uint32_t>& px, int w, int h) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return;
    int row = (w * 3 + 3) & ~3;
    uint32_t dataSize = row * h, fileSize = 54 + dataSize;
    uint8_t hdr[54] = {'B', 'M'};
    *(uint32_t*)(hdr + 2) = fileSize;
    *(uint32_t*)(hdr + 10) = 54;
    *(uint32_t*)(hdr + 14) = 40;
    *(int32_t*)(hdr + 18) = w;
    *(int32_t*)(hdr + 22) = -h;
    *(uint16_t*)(hdr + 26) = 1;
    *(uint16_t*)(hdr + 28) = 24;
    *(uint32_t*)(hdr + 34) = dataSize;
    fwrite(hdr, 1, 54, f);
    std::vector<uint8_t> line(row, 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            uint32_t c = px[(size_t)y * w + x];
            line[x * 3] = c & 0xff;
            line[x * 3 + 1] = (c >> 8) & 0xff;
            line[x * 3 + 2] = (c >> 16) & 0xff;
        }
        fwrite(line.data(), 1, row, f);
    }
    fclose(f);
}

static void WriteWav(const std::string& path, const std::vector<int16_t>& pcm, int rate, int ch) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return;
    uint32_t data = (uint32_t)pcm.size() * 2;
    uint8_t h[44] = {'R', 'I', 'F', 'F'};
    *(uint32_t*)(h + 4) = 36 + data;
    memcpy(h + 8, "WAVEfmt ", 8);
    *(uint32_t*)(h + 16) = 16;
    *(uint16_t*)(h + 20) = 1;
    *(uint16_t*)(h + 22) = (uint16_t)ch;
    *(uint32_t*)(h + 24) = rate;
    *(uint32_t*)(h + 28) = rate * ch * 2;
    *(uint16_t*)(h + 32) = (uint16_t)(ch * 2);
    *(uint16_t*)(h + 34) = 16;
    memcpy(h + 36, "data", 4);
    *(uint32_t*)(h + 40) = data;
    fwrite(h, 1, 44, f);
    fwrite(pcm.data(), 2, pcm.size(), f);
    fclose(f);
}

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    movie::Mov m;
    if (!m.Open(argv[1])) {
        printf("open failed\n");
        return 1;
    }
    printf("video %dx%d, %d frames, %.2f s; audio %d Hz x%d\n", m.width, m.height, m.frameCount(), m.duration(),
           m.sampleRate, m.channels);
    std::string out = argv[2];
    for (int i = 3; i < argc; ++i) {
        int n = atoi(argv[i]);
        if (m.DecodeFrame(n)) WriteBmp(out + "/frame-" + argv[i] + ".bmp", m.pixels(), m.width, m.height);
    }
    std::vector<int16_t> pcm;
    if (m.DecodeAudio(pcm)) {
        WriteWav(out + "/audio.wav", pcm, m.sampleRate, m.channels);
        printf("audio: %zu samples (%.2f s)\n", pcm.size() / m.channels, (double)pcm.size() / m.channels / m.sampleRate);
    }
    return 0;
}
