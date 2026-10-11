#include "image.h"

#include <windows.h>
#include <initguid.h>  // define the WIC GUIDs here
#include <wincodec.h>

namespace render {

bool LoadImageBGRA(const std::string& path, int& w, int& h, std::vector<uint32_t>& pixels) {
    if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) return false;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);  // fine if already initialised
    IWICImagingFactory* factory = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* conv = nullptr;
    bool ok = false;
    std::wstring wpath(path.begin(), path.end());
    UINT uw = 0, uh = 0;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory,
                                   (void**)&factory)) &&
        SUCCEEDED(factory->CreateDecoderFromFilename(wpath.c_str(), nullptr, GENERIC_READ,
                                                     WICDecodeMetadataCacheOnDemand, &decoder)) &&
        SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(factory->CreateFormatConverter(&conv)) &&
        SUCCEEDED(conv->Initialize(frame, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                                   WICBitmapPaletteTypeCustom)) &&
        SUCCEEDED(conv->GetSize(&uw, &uh)) && uw > 0 && uh > 0 && uw <= 16384 && uh <= 16384) {
        pixels.resize((size_t)uw * uh);
        ok = SUCCEEDED(conv->CopyPixels(nullptr, uw * 4, uw * uh * 4, (BYTE*)pixels.data()));
        w = (int)uw;
        h = (int)uh;
    }
    if (conv) conv->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    if (factory) factory->Release();
    return ok;
}

}  // namespace render
