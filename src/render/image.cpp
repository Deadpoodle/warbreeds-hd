#include "image.h"

#include <windows.h>
#include <initguid.h>  // define the WIC GUIDs here
#include <wincodec.h>

namespace render {

bool LoadImageBGRA(const std::string& path, int& w, int& h, std::vector<uint32_t>& pixels) {
    if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES) return false;
    // COM only for this load, undone below: left initialised on the game's thread,
    // it made the game's own CoInitialize (multiplayer start-up, 0x4c7a30) return
    // S_FALSE, which the game treats as an error ("Hey!!", then no multiplayer).
    HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
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
    if (SUCCEEDED(com)) CoUninitialize();  // S_OK and S_FALSE both count
    return ok;
}

}  // namespace render
