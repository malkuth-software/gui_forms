#include "gpu.hpp"

#include <dcomp.h>

#include <cstring>

// DXGI flip-model routes. Each needs a D3D11 device (a GPU API symbol) to
// create the swap chain; the back buffers are GPU textures with no CPU
// pointer, so CPU pixels reach them through GDI (GDI-compatible buffers) or
// a GPU copy the program issues.
namespace lab {

namespace {

DXGI_SWAP_CHAIN_DESC1 flip_description() {
    DXGI_SWAP_CHAIN_DESC1 description{};
    description.Width = window_width;
    description.Height = window_height;
    description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.BufferCount = 2;
    description.Scaling = DXGI_SCALING_NONE;
    description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    description.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    return description;
}

// Which phase a sampled pixel (at x, y of the image, shifted by
// `scrolled_rows`) belongs to: the phase number, or -1 for none of 0..15.
int classify_pixel(std::uint32_t pixel, int x, int y, int scrolled_rows) {
    for (int phase = 0; phase < 16; ++phase) {
        const std::uint32_t expected = pattern_pixel(x, y + scrolled_rows, phase, false) & 0x00FFFFFFU;
        if ((pixel & 0x00FFFFFFU) == expected) return phase;
    }
    return -1;
}

void probe_variant(const char* route, const char* key, const Gpu& gpu, const DXGI_SWAP_CHAIN_DESC1& description,
                   bool try_map) {
    HWND window = create_window(L"dxgi_variant", 0, nullptr, true);
    IDXGISwapChain1* chain = nullptr;
    const HRESULT created = (*gpu.factory).CreateSwapChainForHwnd(gpu.device, window, &description, nullptr,
                                                                  nullptr, &chain);
    report_hr(route, key, created);
    if (SUCCEEDED(created) && try_map) {
        IDXGISurface* surface = nullptr;
        HRESULT result = (*chain).GetBuffer(0, __uuidof(IDXGISurface), reinterpret_cast<void**>(&surface));
        if (SUCCEEDED(result)) {
            DXGI_MAPPED_RECT mapped{};
            result = (*surface).Map(&mapped, DXGI_MAP_WRITE);
            char map_key[96]{};
            std::snprintf(map_key, sizeof(map_key), "%s.map_back_buffer", key);
            report_hr(route, map_key, result);
            if (SUCCEEDED(result)) (*surface).Unmap();
        }
        release(surface);
    }
    release(chain);
    DestroyWindow(window);
    pump(50);
}

struct HostDib final {
    HDC dc{};
    HBITMAP bitmap{};
    HGDIOBJ previous{};
    std::uint32_t* pixels{};
};

bool allocate_host_dib(HostDib& dib) {
    dib.dc = CreateCompatibleDC(nullptr);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = window_width;
    info.bmiHeader.biHeight = -window_height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* storage = nullptr;
    dib.bitmap = CreateDIBSection(dib.dc, &info, DIB_RGB_COLORS, &storage, nullptr, 0);
    if (dib.bitmap == nullptr) return false;
    dib.pixels = static_cast<std::uint32_t*>(storage);
    dib.previous = SelectObject(dib.dc, dib.bitmap);
    return true;
}

void release_host_dib(HostDib& dib) {
    if (dib.previous != nullptr) SelectObject(dib.dc, dib.previous);
    if (dib.bitmap != nullptr) DeleteObject(dib.bitmap);
    if (dib.dc != nullptr) DeleteDC(dib.dc);
    dib = HostDib{};
}

// Copies `area` of the DIB into the chain's current back buffer through
// IDXGISurface1::GetDC; `dirty` (may be null) goes to ReleaseDC.
HRESULT gdi_write_back_buffer(IDXGISwapChain1* chain, const HostDib& dib, const RECT& area, RECT* dirty,
                              const char* route, const char* key) {
    IDXGISurface1* surface = nullptr;
    HRESULT result = (*chain).GetBuffer(0, __uuidof(IDXGISurface1), reinterpret_cast<void**>(&surface));
    if (FAILED(result)) return result;
    HDC dc = nullptr;
    result = (*surface).GetDC(FALSE, &dc);
    report_hr(route, key, result);
    if (SUCCEEDED(result)) {
        BitBlt(dc, area.left, area.top, area.right - area.left, area.bottom - area.top, dib.dc, area.left, area.top,
               SRCCOPY);
        (*surface).ReleaseDC(dirty);
    }
    release(surface);
    return result;
}

void report_back_buffer_phase(const char* route, const char* key, const Gpu& gpu, IDXGISwapChain1* chain,
                              int scrolled_rows) {
    ID3D11Texture2D* back = nullptr;
    HRESULT result = (*chain).GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back));
    std::uint32_t pixel = 0;
    const int x = window_width - 10;
    const int y = 10;
    if (SUCCEEDED(result)) result = read_pixel(gpu, back, x, y, pixel);
    release(back);
    const int phase = classify_pixel(pixel, x, y, scrolled_rows);
    const int unscrolled_phase = classify_pixel(pixel, x, y, 0);
    report(route, key, "hr=0x%08lX pixel=0x%08X phase_if_scrolled=%d phase_unscrolled=%d",
           static_cast<unsigned long>(result), pixel, phase, unscrolled_phase);
}

void run_flip_gdi_route(const Gpu& gpu) {
    const char* route = "dxgi_flip_gdi";
    HostDib dib{};
    if (!allocate_host_dib(dib)) return;
    HWND window = create_window(L"dxgi_flip_gdi", 0, nullptr, true);
    DXGI_SWAP_CHAIN_DESC1 description = flip_description();
    description.Flags = DXGI_SWAP_CHAIN_FLAG_GDI_COMPATIBLE;
    IDXGISwapChain1* chain = nullptr;
    const HRESULT created = (*gpu.factory).CreateSwapChainForHwnd(gpu.device, window, &description, nullptr,
                                                                  nullptr, &chain);
    report_hr(route, "create", created);
    if (FAILED(created)) {
        DestroyWindow(window);
        release_host_dib(dib);
        return;
    }
    // Frame A: the whole buffer.
    fill_pattern(dib.pixels, window_width, window_height, window_width, 0, 0, 7, false);
    const RECT whole{0, 0, window_width, window_height};
    gdi_write_back_buffer(chain, dib, whole, nullptr, route, "frame7_getdc");
    DXGI_PRESENT_PARAMETERS parameters{};
    report_hr(route, "frame7_present1", (*chain).Present1(0, 0, &parameters));
    pump(300);
    report_readback(route, "frame7", window, 7, 0);

    // Frame B: only the right half is written and named dirty.
    fill_pattern(dib.pixels + window_width / 2, window_width / 2, window_height, window_width, window_width / 2, 0,
                 8, false);
    RECT dirty{window_width / 2, 0, window_width, window_height};
    gdi_write_back_buffer(chain, dib, dirty, &dirty, route, "frame8_getdc");
    parameters.DirtyRectsCount = 1;
    parameters.pDirtyRects = &dirty;
    report_hr(route, "frame8_present1_dirty", (*chain).Present1(0, 0, &parameters));
    pump(200);
    report_readback(route, "frame8_dirty", window, 8, 0);
    // The next back buffer last held frame A (phase 7 everywhere); if DXGI
    // copied frame B's dirty area forward, its right half reads phase 8.
    report_back_buffer_phase(route, "next_back_buffer_after_dirty", gpu, chain, 0);

    // Frame C: scroll up 24 rows. Only the exposed strip is written; the
    // scroll rectangle and offset ask the runtime to move the rest.
    fill_pattern(dib.pixels, window_width / 2, window_height, window_width, 0, 0, 8, false);
    const int scroll_rows = 24;
    std::memmove(dib.pixels, dib.pixels + static_cast<std::size_t>(scroll_rows) * window_width,
                 static_cast<std::size_t>(window_height - scroll_rows) * window_width * sizeof(std::uint32_t));
    fill_pattern(dib.pixels + static_cast<std::size_t>(window_height - scroll_rows) * window_width, window_width,
                 scroll_rows, window_width, 0, window_height, 8, false);
    RECT strip{0, window_height - scroll_rows, window_width, window_height};
    gdi_write_back_buffer(chain, dib, strip, &strip, route, "frame8s_getdc");
    RECT scrolled{0, 0, window_width, window_height - scroll_rows};
    POINT offset{0, -scroll_rows};
    parameters.DirtyRectsCount = 1;
    parameters.pDirtyRects = &strip;
    parameters.pScrollRect = &scrolled;
    parameters.pScrollOffset = &offset;
    report_hr(route, "frame8s_present1_scroll", (*chain).Present1(0, 0, &parameters));
    pump(200);
    report_readback(route, "frame8s_scrolled", window, 8, scroll_rows);
    report_back_buffer_phase(route, "next_back_buffer_after_scroll", gpu, chain, scroll_rows);

    report_hr(route, "present_test", (*chain).Present(0, DXGI_PRESENT_TEST));
    DXGI_FRAME_STATISTICS statistics{};
    report_hr(route, "frame_statistics", (*chain).GetFrameStatistics(&statistics));
    // Resizing keeps the chain: ResizeBuffers with the flags it was made with.
    report_hr(route, "resize_buffers",
              (*chain).ResizeBuffers(0, window_width, window_height, DXGI_FORMAT_UNKNOWN,
                                     DXGI_SWAP_CHAIN_FLAG_GDI_COMPATIBLE));
    release(chain);
    DestroyWindow(window);
    pump(50);
    release_host_dib(dib);
}

void run_flip_upload_route(const Gpu& gpu) {
    const char* route = "dxgi_flip_upload";
    HWND window = create_window(L"dxgi_flip_upload", 0, nullptr, true);
    const DXGI_SWAP_CHAIN_DESC1 description = flip_description();
    IDXGISwapChain1* chain = nullptr;
    const HRESULT created = (*gpu.factory).CreateSwapChainForHwnd(gpu.device, window, &description, nullptr,
                                                                  nullptr, &chain);
    report_hr(route, "create", created);
    if (SUCCEEDED(created)) {
        ID3D11Texture2D* back = nullptr;
        (*chain).GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back));
        report_hr(route, "upload", upload_pattern(gpu, back, nullptr, 9, false));
        release(back);
        DXGI_PRESENT_PARAMETERS parameters{};
        report_hr(route, "present1", (*chain).Present1(0, 0, &parameters));
        pump(300);
        report_readback(route, "frame9", window, 9, 0);
        IDXGISwapChain1* second = nullptr;
        // A second flip chain on a window that already has one is refused.
        report_hr(route, "second_chain_same_window",
                  (*gpu.factory).CreateSwapChainForHwnd(gpu.device, window, &description, nullptr, nullptr, &second));
        release(second);
    }
    release(chain);
    DestroyWindow(window);
    pump(50);
}

void run_composition_route(const Gpu& gpu) {
    const char* route = "dxgi_composition";
    DXGI_SWAP_CHAIN_DESC1 description = flip_description();
    description.Scaling = DXGI_SCALING_STRETCH;
    description.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    description.Flags = DXGI_SWAP_CHAIN_FLAG_GDI_COMPATIBLE;
    IDXGISwapChain1* probe = nullptr;
    report_hr(route, "create_premultiplied_gdi_compatible",
              (*gpu.factory).CreateSwapChainForComposition(gpu.device, &description, nullptr, &probe));
    release(probe);
    description.Flags = 0;
    IDXGISwapChain1* chain = nullptr;
    const HRESULT created = (*gpu.factory).CreateSwapChainForComposition(gpu.device, &description, nullptr, &chain);
    report_hr(route, "create_premultiplied", created);
    if (FAILED(created)) return;
    IDCompositionDevice* composition = nullptr;
    HRESULT result = DCompositionCreateDevice(gpu.dxgi_device, __uuidof(IDCompositionDevice),
                                              reinterpret_cast<void**>(&composition));
    report_hr(route, "dcomp_device", result);
    HWND window = create_window(L"dxgi_composition", WS_EX_NOREDIRECTIONBITMAP, nullptr, true);
    IDCompositionTarget* target = nullptr;
    IDCompositionVisual* visual = nullptr;
    if (SUCCEEDED(result)) result = (*composition).CreateTargetForHwnd(window, TRUE, &target);
    report_hr(route, "target", result);
    if (SUCCEEDED(result)) result = (*composition).CreateVisual(&visual);
    if (SUCCEEDED(result)) result = (*visual).SetContent(chain);
    if (SUCCEEDED(result)) result = (*target).SetRoot(visual);
    ID3D11Texture2D* back = nullptr;
    (*chain).GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back));
    report_hr(route, "upload", upload_pattern(gpu, back, nullptr, 10, true));
    release(back);
    DXGI_PRESENT_PARAMETERS parameters{};
    report_hr(route, "present1", (*chain).Present1(0, 0, &parameters));
    if (SUCCEEDED(result)) result = (*composition).Commit();
    report_hr(route, "commit", result);
    pump(300);
    report_readback(route, "frame10", window, 10, 0);
    if (composition != nullptr) {
        BOOL valid = FALSE;
        const HRESULT state = (*composition).CheckDeviceState(&valid);
        report(route, "check_device_state", "valid=%d hr=0x%08lX", valid, static_cast<unsigned long>(state));
    }
    release(visual);
    release(target);
    release(composition);
    release(chain);
    DestroyWindow(window);
    pump(50);
}

void run_surface_handle_route(const Gpu& gpu) {
    const char* route = "dxgi_surface_handle";
    HANDLE surface_handle = nullptr;
    HRESULT result = DCompositionCreateSurfaceHandle(COMPOSITIONOBJECT_ALL_ACCESS, nullptr, &surface_handle);
    report_hr(route, "create_surface_handle", result);
    if (FAILED(result)) return;
    IDXGIFactoryMedia* media = nullptr;
    result = (*gpu.factory).QueryInterface(__uuidof(IDXGIFactoryMedia), reinterpret_cast<void**>(&media));
    report_hr(route, "factory_media", result);
    IDXGISwapChain1* chain = nullptr;
    DXGI_SWAP_CHAIN_DESC1 description = flip_description();
    description.Scaling = DXGI_SCALING_STRETCH;
    description.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    if (SUCCEEDED(result)) {
        result = (*media).CreateSwapChainForCompositionSurfaceHandle(gpu.device, surface_handle, &description,
                                                                     nullptr, &chain);
    }
    report_hr(route, "create_chain", result);
    IDCompositionDesktopDevice* desktop = nullptr;
    IDCompositionTarget* target = nullptr;
    IDCompositionVisual2* visual = nullptr;
    IUnknown* surface = nullptr;
    HWND window = create_window(L"dxgi_surface_handle", WS_EX_NOREDIRECTIONBITMAP, nullptr, true);
    if (SUCCEEDED(result)) {
        result = DCompositionCreateDevice2(gpu.dxgi_device, __uuidof(IDCompositionDesktopDevice),
                                           reinterpret_cast<void**>(&desktop));
        report_hr(route, "desktop_device", result);
    }
    if (SUCCEEDED(result)) result = (*desktop).CreateTargetForHwnd(window, TRUE, &target);
    if (SUCCEEDED(result)) result = (*desktop).CreateSurfaceFromHandle(surface_handle, &surface);
    report_hr(route, "surface_from_handle", result);
    if (SUCCEEDED(result)) result = (*desktop).CreateVisual(&visual);
    if (SUCCEEDED(result)) result = (*visual).SetContent(surface);
    if (SUCCEEDED(result)) result = (*target).SetRoot(visual);
    if (SUCCEEDED(result)) {
        ID3D11Texture2D* back = nullptr;
        (*chain).GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back));
        report_hr(route, "upload", upload_pattern(gpu, back, nullptr, 11, true));
        release(back);
        DXGI_PRESENT_PARAMETERS parameters{};
        report_hr(route, "present1", (*chain).Present1(0, 0, &parameters));
        result = (*desktop).Commit();
        report_hr(route, "commit", result);
        pump(300);
        report_readback(route, "frame11", window, 11, 0);
    }
    release(visual);
    release(surface);
    release(target);
    release(desktop);
    release(chain);
    release(media);
    CloseHandle(surface_handle);
    DestroyWindow(window);
    pump(50);
}

} // namespace

void run_dxgi_routes() {
    Gpu gpu{};
    if (!create_gpu("dxgi", gpu, true, true)) {
        report("dxgi", "device", "unavailable");
        release_gpu(gpu);
        return;
    }
    DXGI_SWAP_CHAIN_DESC1 description = flip_description();
    probe_variant("dxgi_variants", "hwnd_flip_ignore_alpha", gpu, description, true);
    description.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    probe_variant("dxgi_variants", "hwnd_flip_premultiplied", gpu, description, false);
    description = flip_description();
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_CPU_ACCESS_DYNAMIC;
    probe_variant("dxgi_variants", "hwnd_flip_cpu_dynamic", gpu, description, true);
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_CPU_ACCESS_READ_WRITE;
    probe_variant("dxgi_variants", "hwnd_flip_cpu_read_write", gpu, description, true);
    description = flip_description();
    description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    description.BufferCount = 1;
    description.Scaling = DXGI_SCALING_STRETCH;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_CPU_ACCESS_READ_WRITE;
    probe_variant("dxgi_variants", "hwnd_bitblt_model_cpu_read_write", gpu, description, true);
    run_flip_gdi_route(gpu);
    run_flip_upload_route(gpu);
    run_composition_route(gpu);
    run_surface_handle_route(gpu);
    release_gpu(gpu);

    // The software rasterizer as the device: no GPU, the same API symbols.
    Gpu warp{};
    const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    D3D_FEATURE_LEVEL obtained{};
    const HRESULT warp_result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
                                                  D3D11_CREATE_DEVICE_BGRA_SUPPORT, &level, 1, D3D11_SDK_VERSION,
                                                  &warp.device, &obtained, &warp.context);
    report_hr("dxgi", "warp_device", warp_result);
    release_gpu(warp);
}

} // namespace lab
