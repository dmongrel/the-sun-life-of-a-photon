#include "render/image_io.h"

#include "app/log.h"

#include <windows.h>
#include <wincodec.h>

#include <cwctype>

namespace render {
namespace {

// A COM pointer that releases itself; WIC hands out a lot of them.
template <typename T>
struct Com {
    T* p = nullptr;
    ~Com() {
        if (p) p->Release();
    }
    T** operator&() { return &p; }
    T*  operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

// COM may already be initialised on this thread by someone else; either way is fine.
bool EnsureCom() {
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    return SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
}

bool Factory(Com<IWICImagingFactory>& factory) {
    return EnsureCom() && SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                                     IID_PPV_ARGS(&factory)));
}

}  // namespace

Pixels LoadResourceImage(int resourceId) {
    Pixels    out;
    HMODULE   module = GetModuleHandleW(nullptr);
    HRSRC     found  = FindResourceW(module, MAKEINTRESOURCEW(resourceId), MAKEINTRESOURCEW(10));  // RT_RCDATA
    HGLOBAL   loaded = found ? LoadResource(module, found) : nullptr;
    const void* data = loaded ? LockResource(loaded) : nullptr;
    const DWORD size = found ? SizeofResource(module, found) : 0;
    if (!data || !size) {
        app::Log("image %d: no such resource", resourceId);
        return out;
    }

    Com<IWICImagingFactory>    factory;
    Com<IWICStream>            stream;
    Com<IWICBitmapDecoder>     decoder;
    Com<IWICBitmapFrameDecode> frame;
    Com<IWICFormatConverter>   converter;
    if (!Factory(factory) || FAILED(factory->CreateStream(&stream)) ||
        FAILED(stream->InitializeFromMemory(static_cast<BYTE*>(const_cast<void*>(data)), size)) ||
        FAILED(factory->CreateDecoderFromStream(stream.p, nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) ||
        FAILED(decoder->GetFrame(0, &frame)) || FAILED(factory->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(frame.p, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                     WICBitmapPaletteTypeCustom))) {
        app::Log("image %d: decode failed", resourceId);
        return out;
    }

    UINT w = 0, h = 0;
    converter->GetSize(&w, &h);
    out.rgba.resize(std::size_t(w) * h * 4);
    if (FAILED(converter->CopyPixels(nullptr, w * 4, static_cast<UINT>(out.rgba.size()), out.rgba.data()))) {
        out.rgba.clear();
        return out;
    }
    out.width  = w;
    out.height = h;
    return out;
}

bool SaveImage(const std::wstring& path, std::uint32_t width, std::uint32_t height, const std::uint8_t* bgra,
               std::uint32_t rowPitch) {
    std::wstring ext = path.substr(path.find_last_of(L'.') == std::wstring::npos ? path.size() : path.find_last_of(L'.'));
    for (auto& c : ext) c = static_cast<wchar_t>(std::towlower(c));
    const GUID container = ext == L".bmp"                     ? GUID_ContainerFormatBmp
                           : (ext == L".jpg" || ext == L".jpeg") ? GUID_ContainerFormatJpeg
                                                               : GUID_ContainerFormatPng;

    Com<IWICImagingFactory>    factory;
    Com<IWICStream>            stream;
    Com<IWICBitmapEncoder>     encoder;
    Com<IWICBitmapFrameEncode> frame;
    if (!Factory(factory) || FAILED(factory->CreateStream(&stream)) ||
        FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) ||
        FAILED(factory->CreateEncoder(container, nullptr, &encoder)) ||
        FAILED(encoder->Initialize(stream.p, WICBitmapEncoderNoCache)) ||
        FAILED(encoder->CreateNewFrame(&frame, nullptr)) || FAILED(frame->Initialize(nullptr)) ||
        FAILED(frame->SetSize(width, height))) {
        return false;
    }
    // The encoder may pick a different layout (JPEG has no alpha); WIC converts on WriteSource.
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGR;
    frame->SetPixelFormat(&format);
    Com<IWICBitmap> bitmap;
    if (FAILED(factory->CreateBitmapFromMemory(width, height, GUID_WICPixelFormat32bppBGR, rowPitch,
                                               rowPitch * height, const_cast<BYTE*>(bgra), &bitmap)) ||
        FAILED(frame->WriteSource(bitmap.p, nullptr)) || FAILED(frame->Commit()) || FAILED(encoder->Commit())) {
        return false;
    }
    return true;
}

}  // namespace render
