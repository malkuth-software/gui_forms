#include "gpu.hpp"

#include <vector>

namespace lab {

namespace {

const char* driver_name(D3D_DRIVER_TYPE driver) {
    if (driver == D3D_DRIVER_TYPE_HARDWARE) return "hardware";
    if (driver == D3D_DRIVER_TYPE_WARP) return "warp";
    return "other";
}

void report_adapter(const char* route, const Gpu& gpu) {
    IDXGIAdapter* adapter = nullptr;
    if (FAILED((*gpu.dxgi_device).GetAdapter(&adapter))) return;
    IDXGIAdapter1* adapter1 = nullptr;
    DXGI_ADAPTER_DESC1 description{};
    if (SUCCEEDED((*adapter).QueryInterface(__uuidof(IDXGIAdapter1), reinterpret_cast<void**>(&adapter1)))) {
        (*adapter1).GetDesc1(&description);
        release(adapter1);
    }
    report(route, "adapter", "%ls vendor=0x%04X device=0x%04X software_flag=%u dedicated_mb=%llu",
           description.Description, description.VendorId, description.DeviceId,
           (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0 ? 1U : 0U,
           static_cast<unsigned long long>(description.DedicatedVideoMemory / (1024U * 1024U)));
    release(adapter);
}

} // namespace

bool create_gpu(const char* route, Gpu& gpu, bool allow_warp, bool details) {
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                        D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL obtained{};
    gpu.driver = D3D_DRIVER_TYPE_HARDWARE;
    HRESULT result = D3D11CreateDevice(nullptr, gpu.driver, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 4,
                                       D3D11_SDK_VERSION, &gpu.device, &obtained, &gpu.context);
    if (details) report_hr(route, "d3d11_hardware", result);
    if (FAILED(result) && allow_warp) {
        gpu.driver = D3D_DRIVER_TYPE_WARP;
        result = D3D11CreateDevice(nullptr, gpu.driver, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 4,
                                   D3D11_SDK_VERSION, &gpu.device, &obtained, &gpu.context);
        if (details) report_hr(route, "d3d11_warp", result);
    }
    if (FAILED(result)) return false;
    result = (*gpu.device).QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&gpu.dxgi_device));
    if (FAILED(result)) return false;
    IDXGIAdapter* adapter = nullptr;
    result = (*gpu.dxgi_device).GetAdapter(&adapter);
    if (FAILED(result)) return false;
    result = (*adapter).GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&gpu.factory));
    release(adapter);
    if (FAILED(result)) return false;
    if (details) {
        report(route, "driver", "%s feature_level=0x%X", driver_name(gpu.driver), static_cast<unsigned>(obtained));
        report_adapter(route, gpu);
        D3D11_FEATURE_DATA_D3D11_OPTIONS2 options2{};
        const HRESULT options_result = (*gpu.device).CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS2, &options2,
                                                                         sizeof(options2));
        report(route, "options2", "hr=0x%08lX unified_memory=%d map_on_default_textures=%d",
               static_cast<unsigned long>(options_result), options2.UnifiedMemoryArchitecture,
               options2.MapOnDefaultTextures);
        IDXGIFactory5* factory5 = nullptr;
        BOOL tearing = FALSE;
        HRESULT tearing_result = E_NOINTERFACE;
        if (SUCCEEDED((*gpu.factory).QueryInterface(__uuidof(IDXGIFactory5), reinterpret_cast<void**>(&factory5)))) {
            tearing_result = (*factory5).CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing,
                                                             sizeof(tearing));
            release(factory5);
        }
        report(route, "allow_tearing", "%d hr=0x%08lX", tearing, static_cast<unsigned long>(tearing_result));
    }
    return true;
}

void release_gpu(Gpu& gpu) {
    release(gpu.factory);
    release(gpu.dxgi_device);
    release(gpu.context);
    release(gpu.device);
}

HRESULT create_texture(const Gpu& gpu, D3D11_USAGE usage, UINT cpu_access, UINT bind, UINT misc,
                       ID3D11Texture2D** texture) {
    D3D11_TEXTURE2D_DESC description{};
    description.Width = window_width;
    description.Height = window_height;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = usage;
    description.BindFlags = bind;
    description.CPUAccessFlags = cpu_access;
    description.MiscFlags = misc;
    const HRESULT result = (*gpu.device).CreateTexture2D(&description, nullptr, texture);
    return result;
}

HRESULT upload_pattern(const Gpu& gpu, ID3D11Texture2D* target, const D3D11_BOX* box, int phase, bool with_alpha) {
    ID3D11Texture2D* upload = nullptr;
    HRESULT result = create_texture(gpu, D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE, D3D11_BIND_SHADER_RESOURCE, 0,
                                    &upload);
    if (FAILED(result)) return result;
    D3D11_MAPPED_SUBRESOURCE mapped{};
    result = (*gpu.context).Map(upload, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (SUCCEEDED(result)) {
        fill_pattern(static_cast<std::uint32_t*>(mapped.pData), window_width, window_height,
                     static_cast<int>(mapped.RowPitch / 4U), 0, 0, phase, with_alpha);
        (*gpu.context).Unmap(upload, 0);
        if (box == nullptr) {
            (*gpu.context).CopyResource(target, upload);
        } else {
            (*gpu.context).CopySubresourceRegion(target, 0, (*box).left, (*box).top, 0, upload, 0, box);
        }
    }
    release(upload);
    return result;
}

HRESULT create_cpu_texture(const Gpu& gpu, D3D11_TEXTURE_LAYOUT layout, UINT misc, ID3D11Texture2D** texture) {
    ID3D11Device3* device3 = nullptr;
    HRESULT result = (*gpu.device).QueryInterface(__uuidof(ID3D11Device3), reinterpret_cast<void**>(&device3));
    if (FAILED(result)) return result;
    D3D11_TEXTURE2D_DESC1 description{};
    description.Width = window_width;
    description.Height = window_height;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    description.MiscFlags = misc;
    description.TextureLayout = layout;
    ID3D11Texture2D1* created = nullptr;
    result = (*device3).CreateTexture2D1(&description, nullptr, &created);
    release(device3);
    if (SUCCEEDED(result)) *texture = created;
    return result;
}

HRESULT cpu_write_texture(const Gpu& gpu, ID3D11Texture2D* texture, D3D11_TEXTURE_LAYOUT layout, int phase,
                          bool with_alpha) {
    if (layout == D3D11_TEXTURE_LAYOUT_ROW_MAJOR) {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        const HRESULT result = (*gpu.context).Map(texture, 0, D3D11_MAP_WRITE, 0, &mapped);
        if (FAILED(result)) return result;
        fill_pattern(static_cast<std::uint32_t*>(mapped.pData), window_width, window_height,
                     static_cast<int>(mapped.RowPitch / 4U), 0, 0, phase, with_alpha);
        (*gpu.context).Unmap(texture, 0);
        return result;
    }
    ID3D11Device3* device3 = nullptr;
    HRESULT result = (*gpu.device).QueryInterface(__uuidof(ID3D11Device3), reinterpret_cast<void**>(&device3));
    if (FAILED(result)) return result;
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(window_width) * window_height);
    fill_pattern(pixels.data(), window_width, window_height, window_width, 0, 0, phase, with_alpha);
    result = (*gpu.context).Map(texture, 0, D3D11_MAP_WRITE, 0, nullptr);
    if (SUCCEEDED(result)) {
        (*device3).WriteToSubresource(texture, 0, nullptr, pixels.data(), window_width * 4U,
                                      window_width * window_height * 4U);
        (*gpu.context).Unmap(texture, 0);
    }
    release(device3);
    return result;
}

HRESULT read_pixel(const Gpu& gpu, ID3D11Texture2D* source, int x, int y, std::uint32_t& pixel) {
    ID3D11Texture2D* staging = nullptr;
    HRESULT result = create_texture(gpu, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_READ, 0, 0, &staging);
    if (FAILED(result)) return result;
    (*gpu.context).CopyResource(staging, source);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    result = (*gpu.context).Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
    if (SUCCEEDED(result)) {
        const std::uint8_t* const base = static_cast<const std::uint8_t*>(mapped.pData);
        const std::uint32_t* const row = reinterpret_cast<const std::uint32_t*>(base + static_cast<std::size_t>(y) * mapped.RowPitch);
        pixel = row[x];
        (*gpu.context).Unmap(staging, 0);
    }
    release(staging);
    return result;
}

} // namespace lab
