// Direct3D 11 presenter.
//   pass 1: 8-bit index textures + 256-entry palette -> game-resolution RGBA:
//           the battlefield (opaque) and, in missions, the interface and cursor
//           (premultiplied alpha, transparent where the battlefield shows)
//   pass 2: layers into the back buffer: battlefield scaled into the viewport,
//           interface in place, then enlarged interface elements; filtering
//           is nearest / bilinear / sharp bilinear
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <string>

#include "render.h"
#include "../prof.h"

namespace render {
namespace d3d11 {
namespace {

void (*g_log)(const char*, ...);
Settings g_s;
HWND g_hwnd;

ID3D11Device* g_dev;
ID3D11DeviceContext* g_ctx;
IDXGISwapChain* g_swap;
ID3D11RenderTargetView* g_backRtv;
int g_backW, g_backH;

struct IndexTex {
    ID3D11Texture2D* tex = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
};
struct ColorTex {
    ID3D11Texture2D* tex = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
};
IndexTex g_final, g_hud, g_cur;  // 8-bit: battlefield (or whole frame), interface, cursor (0 = empty)
ColorTex g_worldRgb, g_hudRgb, g_curRgb;  // pass-1 results
ID3D11Texture2D* g_palTex;
ID3D11ShaderResourceView* g_palSrv;
int g_texW, g_texH;
uint32_t g_uploadedSerial = ~0u;

ID3D11VertexShader* g_vs;
ID3D11PixelShader* g_psPalette;
ID3D11PixelShader* g_psInterface;
ID3D11PixelShader* g_psScale;
ID3D11SamplerState* g_linear;
ID3D11SamplerState* g_point;
ID3D11BlendState* g_blend;  // premultiplied alpha
ID3D11Buffer* g_cb;

std::string g_shotPath;  // set by Screenshot(); saved on the next present
std::string g_recordPrefix;  // set by Record(): save presented frames (debug, for GIFs)
DWORD g_recordStart, g_recordLast;

// Menus: widescreen background (full colour) and the cursor drawn on its own.
ColorTex g_bg, g_bgGlow;  // normal and highlight-glow backgrounds
bool g_glowOn;
int g_bgW, g_bgH;
bool g_bgOn;
RECT g_bgDst;
IndexTex g_mcur;
ColorTex g_mcurRgb;
int g_mcurSize;
uint32_t g_mcurSerial = ~0u, g_mcurFrameSerial = ~0u;

// Cutscene movie frames (full colour) and the Enhanced scaling chain.
ColorTex g_movie;
int g_movieW, g_movieH;
int g_movieMode;  // 0 Enhanced, 1 Smooth, 2 Sharp
ColorTex g_movieClean, g_movieUp;  // float16, movie size / output size
int g_movieUpW, g_movieUpH, g_movieCleanW, g_movieCleanH;
ID3D11PixelShader *g_psMovieClean, *g_psMovieUpscale, *g_psMovieSharpen;
ID3D11Buffer* g_movieCb;
bool g_movieShadersTried;
struct MovieParams {
    float srcW, srcH, dstW, dstH;
    float sharpness, pad[3];
};

// Text overlay (FPS counter): drawn with GDI into a premultiplied texture.
constexpr int kOverlayW = 128, kOverlayH = 24;
ColorTex g_overlay;
bool g_overlayOn;

constexpr int kMaxExclusions = 8;

struct Params {
    float texW, texH, scaleX, scaleY;
    float u0, v0, u1, v1;
    int filter, exclCount;
    float pad[2];
    float excl[kMaxExclusions][4];  // UV rects the in-place interface layer skips
};
static_assert(sizeof(Params) % 16 == 0, "constant buffer size");

template <typename T>
void SafeRelease(T*& p) {
    if (p) p->Release();
    p = nullptr;
}

void Release(IndexTex& t) {
    SafeRelease(t.srv);
    SafeRelease(t.tex);
}
void Release(ColorTex& t) {
    SafeRelease(t.rtv);
    SafeRelease(t.srv);
    SafeRelease(t.tex);
}

// Writes a BGRA texture as a 32-bit BMP.
void SaveTexture(ID3D11Texture2D* back, const std::string& path, bool quiet = false) {
    D3D11_TEXTURE2D_DESC d;
    back->GetDesc(&d);
    d.Usage = D3D11_USAGE_STAGING;
    d.BindFlags = 0;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    d.MiscFlags = 0;
    ID3D11Texture2D* staging = nullptr;
    if (SUCCEEDED(g_dev->CreateTexture2D(&d, nullptr, &staging))) {
        g_ctx->CopyResource(staging, back);
        D3D11_MAPPED_SUBRESOURCE m;
        if (SUCCEEDED(g_ctx->Map(staging, 0, D3D11_MAP_READ, 0, &m))) {
            if (FILE* f = fopen(path.c_str(), "wb")) {
                BITMAPFILEHEADER fh{};
                BITMAPINFOHEADER ih{};
                ih.biSize = sizeof ih;
                ih.biWidth = d.Width;
                ih.biHeight = -(LONG)d.Height;
                ih.biPlanes = 1;
                ih.biBitCount = 32;
                fh.bfType = 0x4d42;
                fh.bfOffBits = sizeof fh + sizeof ih;
                fh.bfSize = fh.bfOffBits + d.Width * 4 * d.Height;
                fwrite(&fh, sizeof fh, 1, f);
                fwrite(&ih, sizeof ih, 1, f);
                for (UINT y = 0; y < d.Height; ++y) fwrite((char*)m.pData + y * m.RowPitch, d.Width * 4, 1, f);
                fclose(f);
                if (!quiet) g_log("saved %s (%ux%u, as displayed)", path.c_str(), d.Width, d.Height);
            }
            g_ctx->Unmap(staging, 0);
        }
        staging->Release();
    }
}

// The composited back buffer (what the player sees).
void SaveBackBuffer() {
    std::string path = g_shotPath;
    g_shotPath.clear();
    ID3D11Texture2D* back = nullptr;
    if (FAILED(g_swap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&back))) return;
    SaveTexture(back, path);
    back->Release();
}

const char kShaders[] = R"(
Texture2D<uint>   idxTex  : register(t0);
Texture2D<float4> palTex  : register(t1);
Texture2D<uint>   hudTex  : register(t2);
Texture2D<float4> srcTex  : register(t0);
SamplerState      samp    : register(s0);

cbuffer Params : register(b0) {
    float2 texSize;    // game frame size in pixels
    float2 scale;      // output pixels per source pixel
    float4 uvRect;     // source rectangle in UV (x0, y0, x1, y1)
    int    filter;     // 0 nearest, 1 bilinear, 2 sharp bilinear
    int    exclCount;  // number of excl rects
    float2 pad;
    float4 excl[8];    // UV rects to leave transparent
};

struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

VSOut VSMain(uint id : SV_VertexID) {
    VSOut o;
    o.uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}

float4 PSPalette(VSOut i) : SV_Target {
    uint idx = idxTex.Load(int3(i.pos.xy, 0));
    return float4(palTex.Load(int3(idx, 0, 0)).rgb, 1);
}

// Interface layer: index 0 is empty (transparent, premultiplied).
float4 PSInterface(VSOut i) : SV_Target {
    uint idx = hudTex.Load(int3(i.pos.xy, 0));
    if (idx == 0) return float4(0, 0, 0, 0);
    return float4(palTex.Load(int3(idx, 0, 0)).rgb, 1);
}

float4 PSScale(VSOut i) : SV_Target {
    float2 uv = lerp(uvRect.xy, uvRect.zw, i.uv);
    for (int e = 0; e < exclCount; ++e)
        if (all(uv >= excl[e].xy) && all(uv < excl[e].zw)) return float4(0, 0, 0, 0);
    if (filter == 2) {
        // Sharp bilinear: each texel is drawn as a solid block and only the
        // ~1 output pixel seam between neighbouring texels is blended.
        float2 sc = max(scale, 1.0);
        float2 texel = uv * texSize;
        float2 s = frac(texel) - 0.5;
        float2 range = 0.5 - 0.5 / sc;
        float2 f = (s - clamp(s, -range, range)) * sc + 0.5;
        uv = (floor(texel) + f) / texSize;
    }
    // Stay inside the source rectangle (no bleeding from neighbouring pixels).
    float2 halfTexel = 0.5 / texSize;
    uv = clamp(uv, uvRect.xy + halfTexel, uvRect.zw - halfTexel);
    return srcTex.Sample(samp, uv);
}
)";

// Cutscene "Enhanced" scaling: clean (Cinepak deblock + deband) at movie size,
// Lanczos-2 upscale with anti-ringing, then contrast-adaptive sharpening
// (after AMD FidelityFX CAS, MIT) with a little dither.
const char kMovieShaders[] = R"(
Texture2D<float4> src : register(t0);
cbuffer MovieParams : register(b0) {
    float2 srcSize;   // movie pixels
    float2 dstSize;   // output pixels
    float  sharpness; // 0..1
    float3 pad;
};
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

float luma(float3 c) { return dot(c, float3(0.299, 0.587, 0.114)); }
float3 at(int2 p) { return src.Load(int3(clamp(p, int2(0, 0), int2(srcSize) - 1), 0)).rgb; }

// Pass 1 (movie size): soften 4x4 (and 2x2) block edges where the step across
// them is small, then replace near-flat areas with their local average.
float4 PSMovieClean(VSOut i) : SV_Target {
    int2 p = int2(i.pos.xy);
    float3 c = at(p);
    // Block boundaries: between x = 4k-1 and 4k (and 2k-1 / 2k, weaker).
    [unroll] for (int axis = 0; axis < 2; ++axis) {
        int2 d = axis == 0 ? int2(1, 0) : int2(0, 1);
        int q = axis == 0 ? p.x : p.y;
        int m4 = q & 3, m2 = q & 1;
        if (m2 == 0 || m2 == 1) {
            int2 a = (m2 == 1) ? p : p - d;  // pixel left/above the boundary
            int2 b = a + d;
            float3 ca = at(a), cb = at(b);
            float step = abs(luma(ca) - luma(cb));
            float t = (m4 == 0 || m4 == 3) ? 0.10 : 0.05;  // 4x4 edges get more help
            float w = saturate(1.0 - step / t) * ((m4 == 0 || m4 == 3) ? 0.5 : 0.3);
            c = lerp(c, (ca + cb) * 0.5, w);
        }
    }
    // Deband: average of 8 samples 3 px away; used where everything is close.
    float3 sum = 0; float maxd = 0;
    [unroll] for (int k = 0; k < 8; ++k) {
        float ang = k * 0.785398;
        float3 s = at(p + int2(round(cos(ang) * 3), round(sin(ang) * 3)));
        sum += s;
        maxd = max(maxd, abs(luma(s) - luma(c)));
    }
    float3 avg = sum / 8;
    c = lerp(avg, c, saturate(maxd / 0.035));
    return float4(c, 1);
}

// Pass 2 (output size): Lanczos-2, clamped to the 2x2 neighbourhood (no halos).
float lanczos2(float x) {
    x = abs(x);
    if (x < 1e-4) return 1;
    if (x >= 2) return 0;
    float px = 3.14159265 * x;
    return 2 * sin(px) * sin(px * 0.5) / (px * px);
}
float4 PSMovieUpscale(VSOut i) : SV_Target {
    float2 sp = i.uv * srcSize - 0.5;
    int2 b = int2(floor(sp));
    float2 f = sp - b;
    float3 acc = 0; float wsum = 0;
    float3 mn = 1e9, mx = -1e9;
    [unroll] for (int y = -1; y <= 2; ++y) {
        float wy = lanczos2(y - f.y);
        [unroll] for (int x = -1; x <= 2; ++x) {
            float w = wy * lanczos2(x - f.x);
            float3 s = at(b + int2(x, y));
            acc += s * w;
            wsum += w;
            if (x >= 0 && x <= 1 && y >= 0 && y <= 1) { mn = min(mn, s); mx = max(mx, s); }
        }
    }
    return float4(clamp(acc / wsum, mn, mx), 1);
}

// Pass 3 (to screen): contrast-adaptive sharpening + dither.
float4 PSMovieSharpen(VSOut i) : SV_Target {
    int2 p = int2(i.uv * dstSize);
    float3 e = src.Load(int3(p, 0)).rgb;
    float3 b = src.Load(int3(p + int2(0, -1), 0)).rgb, d = src.Load(int3(p + int2(-1, 0), 0)).rgb;
    float3 f = src.Load(int3(p + int2(1, 0), 0)).rgb, h = src.Load(int3(p + int2(0, 1), 0)).rgb;
    float3 mn = min(e, min(min(b, d), min(f, h))), mx = max(e, max(max(b, d), max(f, h)));
    float3 amp = sqrt(saturate(min(mn, 2 - mx) / max(mx, 1e-4)));
    float peak = -1.0 / lerp(8.0, 5.0, sharpness);
    float3 w = amp * peak;
    float3 c = saturate((e + (b + d + f + h) * w) / (1 + 4 * w));
    float n = frac(sin(dot(float2(p), float2(12.9898, 78.233))) * 43758.5453) - 0.5;
    return float4(c + n / 255.0, 1);
}
)";

bool CompileSrc(const char* srcText, size_t len, const char* entry, const char* target, ID3DBlob** out);

bool Compile(const char* entry, const char* target, ID3DBlob** out) {
    return CompileSrc(kShaders, sizeof kShaders - 1, entry, target, out);
}

bool CompileSrc(const char* srcText, size_t len, const char* entry, const char* target, ID3DBlob** out) {
    static HMODULE dll;
    if (!dll) dll = LoadLibraryA("d3dcompiler_47.dll");
    if (!dll) dll = LoadLibraryA("d3dcompiler_43.dll");
    if (!dll) {
        g_log("d3d11: no d3dcompiler DLL");
        return false;
    }
    auto compile = (pD3DCompile)GetProcAddress(dll, "D3DCompile");
    if (!compile) return false;
    ID3DBlob* errors = nullptr;
    HRESULT hr = compile(srcText, len, "warbreedshd", nullptr, nullptr, entry, target,
                         D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, out, &errors);
    if (FAILED(hr)) {
        g_log("d3d11: shader %s failed: %s", entry, errors ? (const char*)errors->GetBufferPointer() : "?");
        SafeRelease(errors);
        return false;
    }
    SafeRelease(errors);
    return true;
}

bool CreateMoviePS(const char* entry, ID3D11PixelShader** out) {
    ID3DBlob* b = nullptr;
    if (!CompileSrc(kMovieShaders, sizeof kMovieShaders - 1, entry, "ps_4_0", &b)) return false;
    bool ok = SUCCEEDED(g_dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, out));
    b->Release();
    return ok;
}

bool CreatePS(const char* entry, ID3D11PixelShader** out) {
    ID3DBlob* b = nullptr;
    if (!Compile(entry, "ps_4_0", &b)) return false;
    bool ok = SUCCEEDED(g_dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, out));
    b->Release();
    return ok;
}

bool CreateBackBuffer() {
    SafeRelease(g_backRtv);
    ID3D11Texture2D* back = nullptr;
    if (FAILED(g_swap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&back))) return false;
    HRESULT hr = g_dev->CreateRenderTargetView(back, nullptr, &g_backRtv);
    D3D11_TEXTURE2D_DESC d;
    back->GetDesc(&d);
    g_backW = d.Width;
    g_backH = d.Height;
    back->Release();
    return SUCCEEDED(hr);
}

bool CreateIndex(int w, int h, IndexTex& t) {
    D3D11_TEXTURE2D_DESC d{};
    d.Width = w;
    d.Height = h;
    d.MipLevels = 1;
    d.ArraySize = 1;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    d.Format = DXGI_FORMAT_R8_UINT;
    return SUCCEEDED(g_dev->CreateTexture2D(&d, nullptr, &t.tex)) &&
           SUCCEEDED(g_dev->CreateShaderResourceView(t.tex, nullptr, &t.srv));
}

bool CreateColor(int w, int h, ColorTex& t) {
    D3D11_TEXTURE2D_DESC d{};
    d.Width = w;
    d.Height = h;
    d.MipLevels = 1;
    d.ArraySize = 1;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    return SUCCEEDED(g_dev->CreateTexture2D(&d, nullptr, &t.tex)) &&
           SUCCEEDED(g_dev->CreateShaderResourceView(t.tex, nullptr, &t.srv)) &&
           SUCCEEDED(g_dev->CreateRenderTargetView(t.tex, nullptr, &t.rtv));
}

bool CreateColorF(int w, int h, ColorTex& t) {
    D3D11_TEXTURE2D_DESC d{};
    d.Width = w;
    d.Height = h;
    d.MipLevels = 1;
    d.ArraySize = 1;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    d.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    return SUCCEEDED(g_dev->CreateTexture2D(&d, nullptr, &t.tex)) &&
           SUCCEEDED(g_dev->CreateShaderResourceView(t.tex, nullptr, &t.srv)) &&
           SUCCEEDED(g_dev->CreateRenderTargetView(t.tex, nullptr, &t.rtv));
}

bool CreateFrameTextures(int w, int h) {
    for (IndexTex* t : {&g_final, &g_hud, &g_cur}) Release(*t);
    for (ColorTex* t : {&g_worldRgb, &g_hudRgb, &g_curRgb}) Release(*t);
    bool ok = CreateIndex(w, h, g_final) && CreateIndex(w, h, g_hud) && CreateIndex(w, h, g_cur) &&
              CreateColor(w, h, g_worldRgb) && CreateColor(w, h, g_hudRgb) && CreateColor(w, h, g_curRgb);
    g_texW = w;
    g_texH = h;
    g_uploadedSerial = ~0u;
    return ok;
}

// Draws a full-target triangle with `ps` reading `srvs` into `rtv`.
void RunPass(ID3D11RenderTargetView* rtv, ID3D11PixelShader* ps, ID3D11ShaderResourceView* const* srvs, int n, int w,
             int h) {
    D3D11_VIEWPORT vp{0, 0, (float)w, (float)h, 0, 1};
    g_ctx->OMSetRenderTargets(1, &rtv, nullptr);
    g_ctx->OMSetBlendState(nullptr, nullptr, 0xffffffff);
    g_ctx->RSSetViewports(1, &vp);
    g_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_ctx->VSSetShader(g_vs, nullptr, 0);
    g_ctx->PSSetShader(ps, nullptr, 0);
    g_ctx->PSSetShaderResources(0, n, srvs);
    g_ctx->Draw(3, 0);
    ID3D11ShaderResourceView* none[3] = {};
    g_ctx->PSSetShaderResources(0, n, none);
}

}  // namespace

bool Init(HWND hwnd, const Settings& s, void (*log)(const char*, ...)) {
    g_log = log;
    g_s = s;
    g_hwnd = hwnd;

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 3,
                                               D3D11_SDK_VERSION, &sd, &g_swap, &g_dev, nullptr, &g_ctx);
    if (FAILED(hr)) {
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;  // pre-Windows 10
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 3,
                                           D3D11_SDK_VERSION, &sd, &g_swap, &g_dev, nullptr, &g_ctx);
    }
    if (FAILED(hr)) {
        g_log("d3d11: device creation failed %#lx", hr);
        return false;
    }
    // We handle Alt+Enter / fullscreen ourselves (the window is borderless).
    IDXGIFactory* factory = nullptr;
    if (SUCCEEDED(g_swap->GetParent(__uuidof(IDXGIFactory), (void**)&factory))) {
        factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
        factory->Release();
    }

    ID3DBlob* vs = nullptr;
    bool ok = Compile("VSMain", "vs_4_0", &vs) &&
              SUCCEEDED(g_dev->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &g_vs)) &&
              CreatePS("PSPalette", &g_psPalette) && CreatePS("PSInterface", &g_psInterface) &&
              CreatePS("PSScale", &g_psScale);
    SafeRelease(vs);
    if (!ok) {
        Shutdown();
        return false;
    }

    D3D11_SAMPLER_DESC smp{};
    smp.AddressU = smp.AddressV = smp.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    smp.MaxLOD = D3D11_FLOAT32_MAX;
    smp.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    g_dev->CreateSamplerState(&smp, &g_linear);
    smp.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    g_dev->CreateSamplerState(&smp, &g_point);

    D3D11_BLEND_DESC bl{};
    bl.RenderTarget[0].BlendEnable = TRUE;
    bl.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    bl.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bl.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bl.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bl.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    bl.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bl.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    g_dev->CreateBlendState(&bl, &g_blend);

    D3D11_BUFFER_DESC bd{};
    bd.ByteWidth = sizeof(Params);
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    g_dev->CreateBuffer(&bd, nullptr, &g_cb);

    D3D11_TEXTURE2D_DESC pd{};
    pd.Width = 256;
    pd.Height = 1;
    pd.MipLevels = 1;
    pd.ArraySize = 1;
    pd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;  // RGBQUAD byte order
    pd.SampleDesc.Count = 1;
    pd.Usage = D3D11_USAGE_DEFAULT;
    pd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    g_dev->CreateTexture2D(&pd, nullptr, &g_palTex);
    g_dev->CreateShaderResourceView(g_palTex, nullptr, &g_palSrv);

    if (!CreateBackBuffer()) {
        Shutdown();
        return false;
    }
    g_log("d3d11: ready, back buffer %dx%d", g_backW, g_backH);
    return true;
}

void Present(const Frame& f, const std::vector<Layer>& layers, int clientW, int clientH) {
    if (!g_swap || f.w <= 0 || f.h <= 0) return;
    if (clientW != g_backW || clientH != g_backH) {
        SafeRelease(g_backRtv);
        g_ctx->ClearState();
        g_swap->ResizeBuffers(0, clientW, clientH, DXGI_FORMAT_UNKNOWN, 0);
        CreateBackBuffer();
    }
    if (f.w != g_texW || f.h != g_texH) {
        prof::Scope scope("gpu-textures");
        CreateFrameTextures(f.w, f.h);
    }
    bool split = f.split && f.hud.size() == f.pixels.size() && f.cursor.size() == f.pixels.size();

    if (f.serial != g_uploadedSerial) {
        g_ctx->UpdateSubresource(g_final.tex, 0, nullptr, f.pixels.data(), f.w, 0);
        g_ctx->UpdateSubresource(g_palTex, 0, nullptr, f.palette, sizeof f.palette, 0);
        if (split) {
            g_ctx->UpdateSubresource(g_hud.tex, 0, nullptr, f.hud.data(), f.w, 0);
            g_ctx->UpdateSubresource(g_cur.tex, 0, nullptr, f.cursor.data(), f.w, 0);
        }
        g_uploadedSerial = f.serial;

        // Pass 1: palette lookups.
        ID3D11ShaderResourceView* world[2] = {g_final.srv, g_palSrv};
        RunPass(g_worldRgb.rtv, g_psPalette, world, 2, f.w, f.h);
        if (split) {
            ID3D11ShaderResourceView* hud[3] = {g_final.srv, g_palSrv, g_hud.srv};
            RunPass(g_hudRgb.rtv, g_psInterface, hud, 3, f.w, f.h);
            ID3D11ShaderResourceView* cur[3] = {g_final.srv, g_palSrv, g_cur.srv};
            RunPass(g_curRgb.rtv, g_psInterface, cur, 3, f.w, f.h);
        }
    }

    // Menu cursor: its own small index texture through the same palette.
    if (f.menuCursorSize > 0 && (f.menuCursorSerial != g_mcurSerial || f.serial != g_mcurFrameSerial)) {
        if (f.menuCursorSize != g_mcurSize) {
            Release(g_mcur);
            Release(g_mcurRgb);
            g_mcurSize = CreateIndex(f.menuCursorSize, f.menuCursorSize, g_mcur) &&
                                 CreateColor(f.menuCursorSize, f.menuCursorSize, g_mcurRgb)
                             ? f.menuCursorSize
                             : 0;
        }
        if (g_mcurSize) {
            g_ctx->UpdateSubresource(g_mcur.tex, 0, nullptr, f.menuCursor.data(), g_mcurSize, 0);
            ID3D11ShaderResourceView* mc[3] = {g_final.srv, g_palSrv, g_mcur.srv};
            RunPass(g_mcurRgb.rtv, g_psInterface, mc, 3, g_mcurSize, g_mcurSize);
        }
        g_mcurSerial = f.menuCursorSerial;
        g_mcurFrameSerial = f.serial;
    }

    // Pass 2: compose the layers into the back buffer.
    const float black[4] = {0, 0, 0, 1};
    g_ctx->OMSetRenderTargets(1, &g_backRtv, nullptr);
    g_ctx->ClearRenderTargetView(g_backRtv, black);
    g_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_ctx->VSSetShader(g_vs, nullptr, 0);
    g_ctx->PSSetShader(g_psScale, nullptr, 0);
    g_ctx->PSSetConstantBuffers(0, 1, &g_cb);
    ID3D11SamplerState* smp = g_s.filter == 0 ? g_point : g_linear;
    g_ctx->PSSetSamplers(0, 1, &smp);

    if (g_bgOn && g_bg.srv && g_bgDst.right > g_bgDst.left && g_bgDst.bottom > g_bgDst.top) {
        // Widescreen menu background: smooth scaling, under the frame.
        Params cb{};
        cb.texW = (float)g_bgW;
        cb.texH = (float)g_bgH;
        cb.scaleX = (g_bgDst.right - g_bgDst.left) / (float)g_bgW;
        cb.scaleY = (g_bgDst.bottom - g_bgDst.top) / (float)g_bgH;
        cb.u1 = cb.v1 = 1;
        cb.filter = 1;
        g_ctx->UpdateSubresource(g_cb, 0, nullptr, &cb, 0, 0);
        g_ctx->PSSetSamplers(0, 1, &g_linear);
        g_ctx->OMSetBlendState(nullptr, nullptr, 0xffffffff);
        g_ctx->PSSetShaderResources(0, 1, g_glowOn && g_bgGlow.srv ? &g_bgGlow.srv : &g_bg.srv);
        D3D11_VIEWPORT vp{(float)g_bgDst.left, (float)g_bgDst.top, (float)(g_bgDst.right - g_bgDst.left),
                          (float)(g_bgDst.bottom - g_bgDst.top), 0, 1};
        g_ctx->RSSetViewports(1, &vp);
        g_ctx->Draw(3, 0);
        g_ctx->PSSetSamplers(0, 1, &smp);
    }

    for (const Layer& l : layers) {
        if ((l.kind == Layer::InterfaceScaled || l.kind == Layer::Cursor) && !split) continue;
        if (l.kind == Layer::MenuCursor && (!g_mcurSize || !g_mcurRgb.srv)) continue;
        bool menuCur = l.kind == Layer::MenuCursor;
        double s0 = l.src.left, t0 = l.src.top, s1 = l.src.right, t1 = l.src.bottom;
        if (l.fractional) {
            s0 = l.fsrc[0];
            t0 = l.fsrc[1];
            s1 = l.fsrc[2];
            t1 = l.fsrc[3];
        }
        float sw = (float)(s1 - s0), sh = (float)(t1 - t0);
        float dw = (float)(l.dst.right - l.dst.left), dh = (float)(l.dst.bottom - l.dst.top);
        if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) continue;
        Params cb{};
        float tw = menuCur ? (float)g_mcurSize : (float)f.w, th = menuCur ? (float)g_mcurSize : (float)f.h;
        cb.texW = tw;
        cb.texH = th;
        cb.scaleX = dw / sw;
        cb.scaleY = dh / sh;
        cb.u0 = (float)(s0 / tw);
        cb.v0 = (float)(t0 / th);
        cb.u1 = (float)(s1 / tw);
        cb.v1 = (float)(t1 / th);
        cb.filter = g_s.filter;
        g_ctx->UpdateSubresource(g_cb, 0, nullptr, &cb, 0, 0);
        ID3D11ShaderResourceView* src = l.kind == Layer::World           ? g_worldRgb.srv
                                        : l.kind == Layer::InterfaceScaled ? g_hudRgb.srv
                                        : menuCur                          ? g_mcurRgb.srv
                                                                           : g_curRgb.srv;
        g_ctx->OMSetBlendState(l.kind == Layer::World ? nullptr : g_blend, nullptr, 0xffffffff);
        g_ctx->PSSetShaderResources(0, 1, &src);
        D3D11_VIEWPORT vp{(float)l.dst.left, (float)l.dst.top, dw, dh, 0, 1};
        g_ctx->RSSetViewports(1, &vp);
        g_ctx->Draw(3, 0);
    }
    if (g_overlayOn && g_overlay.srv) {
        Params cb{};
        cb.texW = kOverlayW;
        cb.texH = kOverlayH;
        cb.scaleX = cb.scaleY = 1;
        cb.u1 = cb.v1 = 1;
        g_ctx->UpdateSubresource(g_cb, 0, nullptr, &cb, 0, 0);
        g_ctx->OMSetBlendState(g_blend, nullptr, 0xffffffff);
        g_ctx->PSSetSamplers(0, 1, &g_point);
        g_ctx->PSSetShaderResources(0, 1, &g_overlay.srv);
        D3D11_VIEWPORT vp{8, 8, (float)kOverlayW, (float)kOverlayH, 0, 1};
        g_ctx->RSSetViewports(1, &vp);
        g_ctx->Draw(3, 0);
    }
    ID3D11ShaderResourceView* none = nullptr;
    g_ctx->PSSetShaderResources(0, 1, &none);
    g_ctx->OMSetBlendState(nullptr, nullptr, 0xffffffff);

    if (!g_shotPath.empty()) SaveBackBuffer();
    if (!g_recordPrefix.empty()) {  // at most 20 frames a second, named by elapsed ms
        DWORD now = GetTickCount();
        if (now - g_recordLast >= 50) {
            g_recordLast = now;
            ID3D11Texture2D* back = nullptr;
            if (SUCCEEDED(g_swap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&back))) {
                char name[32];
                wsprintfA(name, "-%06lu.bmp", now - g_recordStart);
                SaveTexture(back, g_recordPrefix + name, true);
                back->Release();
            }
        }
    }
    double tp = prof::Enabled() ? prof::Now() : 0;
    HRESULT hr = g_swap->Present(g_s.vsync ? 1 : 0, 0);
    if (tp) prof::Add("gpu-present", prof::Now() - tp);
    if (FAILED(hr)) {
        static int n;
        if (n++ < 5) g_log("d3d11: Present failed %#lx", hr);
    }
}

void Screenshot(const std::string& path) { g_shotPath = path; }

void Record(const std::string& prefix) {
    g_recordPrefix = prefix;
    g_recordStart = g_recordLast = GetTickCount() - 50;
    g_log("d3d11: recording %s", prefix.empty() ? "stopped" : prefix.c_str());
}

void PresentMovie(const uint32_t* px, int w, int h, RECT dst, int clientW, int clientH) {
    if (!g_swap || !px || w <= 0 || h <= 0) return;
    if (clientW != g_backW || clientH != g_backH) {
        SafeRelease(g_backRtv);
        g_ctx->ClearState();
        g_swap->ResizeBuffers(0, clientW, clientH, DXGI_FORMAT_UNKNOWN, 0);
        CreateBackBuffer();
    }
    if (w != g_movieW || h != g_movieH) {
        Release(g_movie);
        g_movieW = g_movieH = 0;
        if (!CreateColor(w, h, g_movie)) return;
        g_movieW = w;
        g_movieH = h;
        D3D11_TEXTURE2D_DESC td;
        g_movie.tex->GetDesc(&td);
        g_log("d3d11: movie texture %ux%u for %dx%d, dst %ld,%ld-%ld,%ld, back %dx%d", td.Width, td.Height, w, h,
              dst.left, dst.top, dst.right, dst.bottom, g_backW, g_backH);
    }
    g_ctx->UpdateSubresource(g_movie.tex, 0, nullptr, px, w * 4, 0);
    int dw = dst.right - dst.left, dh = dst.bottom - dst.top;
    bool enhanced = g_movieMode == 0 && dw > 0 && dh > 0;
    if (enhanced && !g_movieShadersTried) {
        g_movieShadersTried = true;
        D3D11_BUFFER_DESC bd{};
        bd.ByteWidth = sizeof(MovieParams);
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bool ok = SUCCEEDED(g_dev->CreateBuffer(&bd, nullptr, &g_movieCb)) && CreateMoviePS("PSMovieClean", &g_psMovieClean) &&
                  CreateMoviePS("PSMovieUpscale", &g_psMovieUpscale) && CreateMoviePS("PSMovieSharpen", &g_psMovieSharpen);
        g_log("d3d11: enhanced movie scaling %s", ok ? "ready" : "unavailable (using smooth)");
    }
    enhanced = enhanced && g_psMovieSharpen;
    if (enhanced) {
        if (w != g_movieCleanW || h != g_movieCleanH) {
            Release(g_movieClean);
            CreateColorF(w, h, g_movieClean);
            g_movieCleanW = w;
            g_movieCleanH = h;
        }
        if (dw != g_movieUpW || dh != g_movieUpH) {
            Release(g_movieUp);
            CreateColorF(dw, dh, g_movieUp);
            g_movieUpW = dw;
            g_movieUpH = dh;
        }
        MovieParams mp{(float)w, (float)h, (float)dw, (float)dh, 0.6f, {}};
        g_ctx->UpdateSubresource(g_movieCb, 0, nullptr, &mp, 0, 0);
        g_ctx->PSSetConstantBuffers(0, 1, &g_movieCb);
        ID3D11ShaderResourceView* s1 = g_movie.srv;
        RunPass(g_movieClean.rtv, g_psMovieClean, &s1, 1, w, h);
        ID3D11ShaderResourceView* s2 = g_movieClean.srv;
        RunPass(g_movieUp.rtv, g_psMovieUpscale, &s2, 1, dw, dh);
    }
    const float black[4] = {0, 0, 0, 1};
    g_ctx->OMSetRenderTargets(1, &g_backRtv, nullptr);
    g_ctx->ClearRenderTargetView(g_backRtv, black);
    g_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_ctx->VSSetShader(g_vs, nullptr, 0);
    if (enhanced) {
        g_ctx->PSSetShader(g_psMovieSharpen, nullptr, 0);
        g_ctx->PSSetConstantBuffers(0, 1, &g_movieCb);
        g_ctx->PSSetShaderResources(0, 1, &g_movieUp.srv);
    } else {
        g_ctx->PSSetShader(g_psScale, nullptr, 0);
        g_ctx->PSSetConstantBuffers(0, 1, &g_cb);
        Params cb{};
        cb.texW = (float)w;
        cb.texH = (float)h;
        cb.scaleX = (dst.right - dst.left) / (float)w;
        cb.scaleY = (dst.bottom - dst.top) / (float)h;
        cb.u1 = cb.v1 = 1;
        cb.filter = g_movieMode == 2 ? 2 : 1;  // Sharp: crisp pixels; Smooth: bilinear
        g_ctx->UpdateSubresource(g_cb, 0, nullptr, &cb, 0, 0);
        g_ctx->PSSetSamplers(0, 1, &g_linear);
        g_ctx->PSSetShaderResources(0, 1, &g_movie.srv);
    }
    g_ctx->OMSetBlendState(nullptr, nullptr, 0xffffffff);
    D3D11_VIEWPORT vp{(float)dst.left, (float)dst.top, (float)(dst.right - dst.left), (float)(dst.bottom - dst.top),
                      0, 1};
    g_ctx->RSSetViewports(1, &vp);
    g_ctx->Draw(3, 0);
    ID3D11ShaderResourceView* none = nullptr;
    g_ctx->PSSetShaderResources(0, 1, &none);
    if (!g_shotPath.empty()) SaveBackBuffer();
    g_swap->Present(g_s.vsync ? 1 : 0, 0);
}

void SetMovieScaling(int mode) { g_movieMode = mode; }

void SetBackgroundGlow(const uint32_t* px, int w, int h) {
    Release(g_bgGlow);
    if (!px || w != g_bgW || h != g_bgH || !g_dev || !CreateColor(w, h, g_bgGlow)) return;
    g_ctx->UpdateSubresource(g_bgGlow.tex, 0, nullptr, px, w * 4, 0);
}
void SetGlow(bool on) { g_glowOn = on; }

void SetBackground(const uint32_t* px, int w, int h) {
    Release(g_bg);
    Release(g_bgGlow);
    g_bgW = g_bgH = 0;
    if (!px || w <= 0 || h <= 0 || !g_dev || !CreateColor(w, h, g_bg)) return;
    g_ctx->UpdateSubresource(g_bg.tex, 0, nullptr, px, w * 4, 0);
    g_bgW = w;
    g_bgH = h;
}
void SetBackgroundRect(bool on, RECT dst) {
    g_bgOn = on;
    g_bgDst = dst;
}
void SetFilter(int filter) { g_s.filter = filter; }
void SetVSync(bool on) { g_s.vsync = on; }

void SetOverlayText(const char* text) {
    g_overlayOn = text && *text;
    if (!g_overlayOn || !g_dev) return;
    if (!g_overlay.tex && !CreateColor(kOverlayW, kOverlayH, g_overlay)) {
        g_overlayOn = false;
        return;
    }
    // White text on black, then coverage -> white over a translucent black box.
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = kOverlayW;
    bi.bmiHeader.biHeight = -kOverlayH;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp) {
        DeleteDC(dc);
        return;
    }
    HGDIOBJ oldBmp = SelectObject(dc, bmp);
    static HFONT font = CreateFontA(-17, 0, 0, 0, FW_BOLD, 0, 0, 0, ANSI_CHARSET, 0, 0, ANTIALIASED_QUALITY,
                                    FIXED_PITCH, "Consolas");
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetBkColor(dc, RGB(0, 0, 0));
    SetTextColor(dc, RGB(255, 255, 255));
    RECT r{0, 0, kOverlayW, kOverlayH};
    ExtTextOutA(dc, 6, 3, ETO_OPAQUE, &r, text, (UINT)strlen(text), nullptr);
    GdiFlush();
    uint8_t* px = (uint8_t*)bits;
    for (int i = 0; i < kOverlayW * kOverlayH; ++i) {
        float t = px[i * 4 + 1] / 255.0f;
        uint8_t c = (uint8_t)(t * 255 + 0.5f), a = (uint8_t)((t + 0.6f * (1 - t)) * 255 + 0.5f);
        px[i * 4] = px[i * 4 + 1] = px[i * 4 + 2] = c;
        px[i * 4 + 3] = a;
    }
    g_ctx->UpdateSubresource(g_overlay.tex, 0, nullptr, bits, kOverlayW * 4, 0);
    SelectObject(dc, oldFont);
    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(dc);
}

void Shutdown() {
    SafeRelease(g_cb);
    SafeRelease(g_blend);
    SafeRelease(g_linear);
    SafeRelease(g_point);
    SafeRelease(g_vs);
    SafeRelease(g_psPalette);
    SafeRelease(g_psInterface);
    SafeRelease(g_psScale);
    SafeRelease(g_palSrv);
    SafeRelease(g_palTex);
    for (IndexTex* t : {&g_final, &g_hud, &g_cur}) Release(*t);
    for (ColorTex* t : {&g_worldRgb, &g_hudRgb, &g_curRgb, &g_overlay, &g_bg, &g_bgGlow, &g_mcurRgb, &g_movie, &g_movieClean, &g_movieUp})
        Release(*t);
    SafeRelease(g_psMovieClean);
    SafeRelease(g_psMovieUpscale);
    SafeRelease(g_psMovieSharpen);
    SafeRelease(g_movieCb);
    g_movieW = g_movieH = g_movieUpW = g_movieUpH = g_movieCleanW = g_movieCleanH = 0;
    g_movieShadersTried = false;
    Release(g_mcur);
    SafeRelease(g_backRtv);
    SafeRelease(g_swap);
    SafeRelease(g_ctx);
    SafeRelease(g_dev);
}

}  // namespace d3d11
}  // namespace render
