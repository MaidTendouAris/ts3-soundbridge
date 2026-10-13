#include <Windows.h>
#include <d2d1_3.h>
#include <d2d1svg.h>
#include <d3d11.h>
#include <wincodec.h>
#include <shlwapi.h>
#include <wrl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
void verify(HRESULT result) { if (FAILED(result)) throw result; }
void render(ID2D1DeviceContext5* context, IWICImagingFactory* wic, const std::filesystem::path& svgPath,
    const std::filesystem::path& pngPath, UINT size) {
    ComPtr<IStream> svgStream;
    verify(SHCreateStreamOnFileEx(svgPath.c_str(), STGM_READ | STGM_SHARE_DENY_WRITE, 0, FALSE, nullptr, &svgStream));
    ComPtr<ID2D1SvgDocument> svg;
    verify(context->CreateSvgDocument(svgStream.Get(), D2D1::SizeF(256, 256), &svg));
    const auto properties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    ComPtr<ID2D1Bitmap1> target;
    verify(context->CreateBitmap(D2D1::SizeU(size, size), nullptr, 0, properties, &target));
    context->SetTarget(target.Get()); context->BeginDraw(); context->Clear(D2D1::ColorF(0, 0));
    context->SetTransform(D2D1::Matrix3x2F::Scale(size / 256.0f, size / 256.0f));
    context->DrawSvgDocument(svg.Get()); verify(context->EndDraw()); context->SetTarget(nullptr);
    const auto cpuProperties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW, properties.pixelFormat);
    ComPtr<ID2D1Bitmap1> cpu;
    verify(context->CreateBitmap(D2D1::SizeU(size, size), nullptr, 0, cpuProperties, &cpu));
    verify(cpu->CopyFromBitmap(nullptr, target.Get(), nullptr));
    D2D1_MAPPED_RECT mapped{}; verify(cpu->Map(D2D1_MAP_OPTIONS_READ, &mapped));
    ComPtr<IWICBitmap> bitmap;
    const auto result = wic->CreateBitmapFromMemory(size, size, GUID_WICPixelFormat32bppPBGRA,
        mapped.pitch, mapped.pitch * size, mapped.bits, &bitmap);
    cpu->Unmap(); verify(result);
    ComPtr<IWICFormatConverter> converter; verify(wic->CreateFormatConverter(&converter));
    verify(converter->Initialize(bitmap.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom));
    ComPtr<IWICStream> stream; verify(wic->CreateStream(&stream));
    verify(stream->InitializeFromFilename(pngPath.c_str(), GENERIC_WRITE));
    ComPtr<IWICBitmapEncoder> encoder; verify(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder));
    verify(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache));
    ComPtr<IWICBitmapFrameEncode> frame; verify(encoder->CreateNewFrame(&frame, nullptr));
    verify(frame->Initialize(nullptr)); verify(frame->SetSize(size, size));
    auto pixelFormat = GUID_WICPixelFormat32bppBGRA; verify(frame->SetPixelFormat(&pixelFormat));
    verify(frame->WriteSource(converter.Get(), nullptr)); verify(frame->Commit()); verify(encoder->Commit());
}
#pragma pack(push, 1)
struct IconHeader { WORD reserved = 0, type = 1, count = 7; };
struct IconEntry { BYTE width, height, colors = 0, reserved = 0; WORD planes = 1, bits = 32; DWORD bytes = 0, offset = 0; };
#pragma pack(pop)
int wmain(int argc, wchar_t** argv) {
    if (argc != 3) return 2;
    const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initialized)) return 1;
    int exitCode = 0;
    try {
        const std::filesystem::path source = argv[1], directory = argv[2];
        std::filesystem::create_directories(directory);
        ComPtr<ID3D11Device> device;
        verify(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, nullptr));
        ComPtr<IDXGIDevice> dxgi; verify(device.As(&dxgi));
        ComPtr<ID2D1Factory1> factory; verify(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, IID_PPV_ARGS(&factory)));
        ComPtr<ID2D1Device> d2d; verify(factory->CreateDevice(dxgi.Get(), &d2d));
        ComPtr<ID2D1DeviceContext> base; verify(d2d->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &base));
        ComPtr<ID2D1DeviceContext5> context; verify(base.As(&context));
        ComPtr<IWICImagingFactory> wic; verify(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)));
        const UINT sizes[] = {16, 24, 32, 48, 64, 128, 256};
        std::vector<std::vector<char>> images;
        std::vector<IconEntry> entries;
        DWORD offset = sizeof(IconHeader) + 7 * sizeof(IconEntry);
        for (const auto size : sizes) {
            const auto png = directory / (L"icon-" + std::to_wstring(size) + L".png");
            render(context.Get(), wic.Get(), source, png, size);
            std::ifstream file(png, std::ios::binary);
            images.emplace_back(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
            IconEntry entry{}; entry.width = entry.height = size == 256 ? 0 : static_cast<BYTE>(size);
            entry.bytes = static_cast<DWORD>(images.back().size()); entry.offset = offset;
            entries.push_back(entry); offset += entry.bytes;
        }
        std::ofstream ico(directory / "soundbridge.ico", std::ios::binary);
        const IconHeader header{}; ico.write(reinterpret_cast<const char*>(&header), sizeof(header));
        for (const auto& entry : entries) ico.write(reinterpret_cast<const char*>(&entry), sizeof(entry));
        for (const auto& png : images) ico.write(png.data(), static_cast<std::streamsize>(png.size()));
        if (!ico) throw E_FAIL;
        std::wcout << L"Rendered SVG to PNG sizes 16..256 and soundbridge.ico.\n";
    } catch (HRESULT result) { std::cerr << "Icon rendering failed: 0x" << std::hex << static_cast<unsigned long>(result) << '\n'; exitCode = 1; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; exitCode = 1; }
    CoUninitialize(); return exitCode;
}
