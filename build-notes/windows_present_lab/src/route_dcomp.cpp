#include "gpu.hpp"

#include <dcomp.h>

#include <cstring>

// DirectComposition routes: a device with no rendering device, surfaces
// (BeginDraw) on a D3D11 device, surface scroll, a GDI window's redirection
// as a visual (CreateSurfaceFromHwnd), and composition textures (Windows 11
// 22H2 / 24H2) declared here from the Windows SDK 10.0.28000.0 dcomp.h,
// lines 2424-2465 and 2477-2518, which mingw-w64 lacks.
namespace lab {

#undef INTERFACE
#define INTERFACE IDCompositionTextureLab
DECLARE_INTERFACE_IID_(IDCompositionTextureLab, IUnknown, "929BB1AA-725F-433B-ABD7-273075A835F2") {
    STDMETHOD(SetSourceRect)(THIS_ const D2D_RECT_U& sourceRect) PURE;
    STDMETHOD(SetColorSpace)(THIS_ DXGI_COLOR_SPACE_TYPE colorSpace) PURE;
    STDMETHOD(SetAlphaMode)(THIS_ DXGI_ALPHA_MODE alphaMode) PURE;
    STDMETHOD(GetAvailableFence)(THIS_ UINT64* fenceValue, REFIID iid, void** availableFence) PURE;
};

#undef INTERFACE
#define INTERFACE IDCompositionDevice4Lab
DECLARE_INTERFACE_IID_(IDCompositionDevice4Lab, IDCompositionDevice3, "85FC5CCA-2DA6-494C-86B6-4A775C049B8A") {
    STDMETHOD(CheckCompositionTextureSupport)(THIS_ IUnknown* renderingDevice, BOOL* supportsCompositionTextures) PURE;
    STDMETHOD(CreateCompositionTexture)(THIS_ IUnknown* d3dTexture, IDCompositionTextureLab** compositionTexture) PURE;
};

#undef INTERFACE
#define INTERFACE IDCompositionDynamicTextureLab
DECLARE_INTERFACE_IID_(IDCompositionDynamicTextureLab, IUnknown, "A1DE1D3F-6405-447F-8E95-1383A34B0277") {
    // The SDK declares two SetTexture overloads; MSVC places overloads in
    // the vtable in reverse declaration order, and clang targeting MinGW
    // follows the Itanium order (declaration order). The second slot is
    // named apart here; only slot 0 is called.
    STDMETHOD(SetTextureSlot0)(THIS_ IDCompositionTextureLab* texture, const D2D_RECT_L* rects, size_t count) PURE;
    STDMETHOD(SetTextureSlot1)(THIS_ IDCompositionTextureLab* texture) PURE;
};

#undef INTERFACE
#define INTERFACE IDCompositionDevice5Lab
DECLARE_INTERFACE_IID_(IDCompositionDevice5Lab, IDCompositionDevice4Lab, "2C6BEBFE-A603-472F-AF34-D2443356E61B") {
    STDMETHOD(CreateDynamicTexture)(THIS_ IDCompositionDynamicTextureLab** dynamicTexture) PURE;
};
#undef INTERFACE

} // namespace lab

__CRT_UUID_DECL(lab::IDCompositionTextureLab, 0x929bb1aa, 0x725f, 0x433b, 0xab, 0xd7, 0x27, 0x30, 0x75, 0xa8, 0x35, 0xf2)
__CRT_UUID_DECL(lab::IDCompositionDevice4Lab, 0x85fc5cca, 0x2da6, 0x494c, 0x86, 0xb6, 0x4a, 0x77, 0x5c, 0x04, 0x9b, 0x8a)
__CRT_UUID_DECL(lab::IDCompositionDynamicTextureLab, 0xa1de1d3f, 0x6405, 0x447f, 0x8e, 0x95, 0x13, 0x83, 0xa3, 0x4b, 0x02, 0x77)
__CRT_UUID_DECL(lab::IDCompositionDevice5Lab, 0x2c6bebfe, 0xa603, 0x472f, 0xaf, 0x34, 0xd2, 0x44, 0x33, 0x56, 0xe6, 0x1b)

namespace lab {

namespace {

// Writes the pattern for image area `area` into a surface texture at
// `offset` through a dynamic texture and CopySubresourceRegion.
HRESULT upload_at(const Gpu& gpu, ID3D11Texture2D* target, POINT offset, const RECT& area, int phase, bool alpha,
                  int scrolled_rows) {
    ID3D11Texture2D* upload = nullptr;
    HRESULT result = create_texture(gpu, D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE, D3D11_BIND_SHADER_RESOURCE, 0,
                                    &upload);
    if (FAILED(result)) return result;
    D3D11_MAPPED_SUBRESOURCE mapped{};
    result = (*gpu.context).Map(upload, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (SUCCEEDED(result)) {
        const int columns = area.right - area.left;
        const int rows = area.bottom - area.top;
        fill_pattern(static_cast<std::uint32_t*>(mapped.pData), columns, rows, static_cast<int>(mapped.RowPitch / 4U),
                     area.left, area.top + scrolled_rows, phase, alpha);
        (*gpu.context).Unmap(upload, 0);
        const D3D11_BOX box{0, 0, 0, static_cast<UINT>(columns), static_cast<UINT>(rows), 1};
        (*gpu.context).CopySubresourceRegion(target, 0, static_cast<UINT>(offset.x), static_cast<UINT>(offset.y), 0,
                                             upload, 0, &box);
    }
    release(upload);
    return result;
}

// One BeginDraw/EndDraw over `area` (whole surface when null): first tries
// the CPU-reachable forms (IDXGISurface1::GetDC, IDXGISurface::Map), then
// falls back to a GPU copy into the ID3D11Texture2D form.
void draw_surface(const char* route, const char* key, const Gpu& gpu, IDCompositionSurface* surface,
                  const RECT* area, int phase, int scrolled_rows) {
    char line_key[128]{};
    POINT offset{};
    IDXGISurface1* dxgi_surface = nullptr;
    HRESULT result = (*surface).BeginDraw(area, __uuidof(IDXGISurface1), reinterpret_cast<void**>(&dxgi_surface),
                                          &offset);
    std::snprintf(line_key, sizeof(line_key), "%s.begin_draw_dxgi_surface1", key);
    report_hr(route, line_key, result);
    if (SUCCEEDED(result)) {
        report(route, key, "offset=%ld,%ld", offset.x, offset.y);
        HDC dc = nullptr;
        const HRESULT dc_result = (*dxgi_surface).GetDC(FALSE, &dc);
        std::snprintf(line_key, sizeof(line_key), "%s.get_dc", key);
        report_hr(route, line_key, dc_result);
        if (SUCCEEDED(dc_result)) (*dxgi_surface).ReleaseDC(nullptr);
        DXGI_MAPPED_RECT mapped{};
        const HRESULT map_result = (*dxgi_surface).Map(&mapped, DXGI_MAP_WRITE);
        std::snprintf(line_key, sizeof(line_key), "%s.map", key);
        report_hr(route, line_key, map_result);
        if (SUCCEEDED(map_result)) (*dxgi_surface).Unmap();
        release(dxgi_surface);
        (*surface).EndDraw();
    }
    ID3D11Texture2D* texture = nullptr;
    result = (*surface).BeginDraw(area, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&texture), &offset);
    std::snprintf(line_key, sizeof(line_key), "%s.begin_draw_texture", key);
    report_hr(route, line_key, result);
    if (FAILED(result)) return;
    D3D11_TEXTURE2D_DESC description{};
    (*texture).GetDesc(&description);
    std::snprintf(line_key, sizeof(line_key), "%s.texture", key);
    report(route, line_key, "offset=%ld,%ld atlas=%ux%u usage=%d cpu_access=0x%X bind=0x%X misc=0x%X", offset.x,
           offset.y, description.Width, description.Height, static_cast<int>(description.Usage),
           description.CPUAccessFlags, description.BindFlags, description.MiscFlags);
    const RECT whole{0, 0, window_width, window_height};
    const RECT drawn = area != nullptr ? *area : whole;
    std::snprintf(line_key, sizeof(line_key), "%s.upload", key);
    report_hr(route, line_key, upload_at(gpu, texture, offset, drawn, phase, true, scrolled_rows));
    release(texture);
    std::snprintf(line_key, sizeof(line_key), "%s.end_draw", key);
    report_hr(route, line_key, (*surface).EndDraw());
}

void run_null_device_route() {
    const char* route = "dcomp_null_device";
    IDCompositionDesktopDevice* device = nullptr;
    HRESULT result = DCompositionCreateDevice2(nullptr, __uuidof(IDCompositionDesktopDevice),
                                               reinterpret_cast<void**>(&device));
    report_hr(route, "create_device2_null", result);
    if (FAILED(result)) return;
    IDCompositionSurface* surface = nullptr;
    report_hr(route, "create_surface",
              (*device).CreateSurface(window_width, window_height, DXGI_FORMAT_B8G8R8A8_UNORM,
                                      DXGI_ALPHA_MODE_PREMULTIPLIED, &surface));
    release(surface);
    // A GDI window's redirection surface as visual content: the content
    // window paints with StretchDIBits (the GDI route) and stays hidden
    // behind the target.
    HWND content = create_window(L"dcomp_hwnd_content", WS_EX_TOOLWINDOW, nullptr, true);
    HWND window = create_window(L"dcomp_null_device", WS_EX_NOREDIRECTIONBITMAP, nullptr, true);
    IUnknown* hwnd_surface = nullptr;
    result = (*device).CreateSurfaceFromHwnd(content, &hwnd_surface);
    report_hr(route, "surface_from_hwnd", result);
    IDCompositionTarget* target = nullptr;
    IDCompositionVisual2* visual = nullptr;
    if (SUCCEEDED(result)) result = (*device).CreateTargetForHwnd(window, TRUE, &target);
    if (SUCCEEDED(result)) result = (*device).CreateVisual(&visual);
    if (SUCCEEDED(result)) result = (*visual).SetContent(hwnd_surface);
    if (SUCCEEDED(result)) result = (*target).SetRoot(visual);
    if (SUCCEEDED(result)) result = (*device).Commit();
    report_hr(route, "commit_hwnd_visual", result);
    release(visual);
    release(target);
    release(hwnd_surface);
    release(device);
    DestroyWindow(window);
    DestroyWindow(content);
    pump(50);
}

void run_surface_route(const Gpu& gpu) {
    const char* route = "dcomp_surface";
    IDCompositionDesktopDevice* device = nullptr;
    HRESULT result = DCompositionCreateDevice2(gpu.dxgi_device, __uuidof(IDCompositionDesktopDevice),
                                               reinterpret_cast<void**>(&device));
    report_hr(route, "create_device2", result);
    if (FAILED(result)) return;
    HWND window = create_window(L"dcomp_surface", WS_EX_NOREDIRECTIONBITMAP, nullptr, true);
    IDCompositionTarget* target = nullptr;
    IDCompositionVisual2* visual = nullptr;
    IDCompositionSurface* surface = nullptr;
    result = (*device).CreateSurface(window_width, window_height, DXGI_FORMAT_B8G8R8A8_UNORM,
                                     DXGI_ALPHA_MODE_PREMULTIPLIED, &surface);
    report_hr(route, "create_surface", result);
    if (SUCCEEDED(result)) {
        draw_surface(route, "frame12", gpu, surface, nullptr, 12, 0);
        result = (*device).CreateTargetForHwnd(window, TRUE, &target);
        if (SUCCEEDED(result)) result = (*device).CreateVisual(&visual);
        if (SUCCEEDED(result)) result = (*visual).SetContent(surface);
        if (SUCCEEDED(result)) result = (*target).SetRoot(visual);
        if (SUCCEEDED(result)) result = (*device).Commit();
        report_hr(route, "commit", result);
        pump(300);
        report_readback(route, "frame12", window, 12, 0);

        // Damage-limited: BeginDraw over the right half only.
        const RECT right_half{window_width / 2, 0, window_width, window_height};
        draw_surface(route, "frame13_partial", gpu, surface, &right_half, 13, 0);
        report_hr(route, "commit_partial", (*device).Commit());
        pump(200);
        report_readback(route, "frame13_partial", window, 13, 0);

        // Host scroll inside the surface (Windows 8.1): move, then draw the
        // exposed strip.
        const int scroll_rows = 24;
        const RECT scrolled{0, 0, window_width, window_height};
        report_hr(route, "surface_scroll", (*surface).Scroll(&scrolled, &scrolled, 0, -scroll_rows));
        const RECT strip{0, window_height - scroll_rows, window_width, window_height};
        draw_surface(route, "frame13_strip", gpu, surface, &strip, 13, scroll_rows);
        report_hr(route, "commit_scroll", (*device).Commit());
        pump(200);
        report_readback(route, "frame13_scrolled", window, 13, scroll_rows);
    }
    IDCompositionVirtualSurface* virtual_surface = nullptr;
    report_hr(route, "create_virtual_surface",
              (*device).CreateVirtualSurface(1U << 16, 1U << 16, DXGI_FORMAT_B8G8R8A8_UNORM,
                                             DXGI_ALPHA_MODE_PREMULTIPLIED, &virtual_surface));
    release(virtual_surface);
    release(visual);
    release(target);
    release(surface);
    release(device);
    DestroyWindow(window);
    pump(50);
}

struct TextureVariant final {
    const char* name{};
    bool cpu{};
    D3D11_TEXTURE_LAYOUT layout{};
    UINT misc{};
};

const TextureVariant texture_variants[] = {
    {"default_plain", false, D3D11_TEXTURE_LAYOUT_UNDEFINED, 0},
    {"default_shared_nthandle", false, D3D11_TEXTURE_LAYOUT_UNDEFINED,
     D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE},
    {"default_shared_displayable", false, D3D11_TEXTURE_LAYOUT_UNDEFINED,
     D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_DISPLAYABLE},
    {"cpu_undefined_plain", true, D3D11_TEXTURE_LAYOUT_UNDEFINED, 0},
    {"cpu_undefined_shared_nthandle", true, D3D11_TEXTURE_LAYOUT_UNDEFINED,
     D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE},
    {"cpu_row_major_shared_nthandle", true, D3D11_TEXTURE_LAYOUT_ROW_MAJOR,
     D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE},
    {"cpu_undefined_shared_displayable", true, D3D11_TEXTURE_LAYOUT_UNDEFINED,
     D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_DISPLAYABLE},
};

HRESULT create_variant(const Gpu& gpu, const TextureVariant& variant, ID3D11Texture2D** texture) {
    if (variant.cpu) return create_cpu_texture(gpu, variant.layout, variant.misc, texture);
    const HRESULT result = create_texture(gpu, D3D11_USAGE_DEFAULT, 0,
                                          D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, variant.misc,
                                          texture);
    return result;
}

HRESULT write_variant(const Gpu& gpu, const TextureVariant& variant, ID3D11Texture2D* texture, int phase) {
    if (variant.cpu) return cpu_write_texture(gpu, texture, variant.layout, phase, true);
    const HRESULT result = upload_pattern(gpu, texture, nullptr, phase, true);
    return result;
}

void run_composition_texture_route(const Gpu& gpu) {
    const char* route = "dcomp_texture";
    IDCompositionDesktopDevice* device = nullptr;
    HRESULT result = DCompositionCreateDevice3(gpu.dxgi_device, __uuidof(IDCompositionDesktopDevice),
                                               reinterpret_cast<void**>(&device));
    report_hr(route, "create_device3", result);
    if (FAILED(result)) return;
    IDCompositionDevice4Lab* device4 = nullptr;
    result = (*device).QueryInterface(__uuidof(IDCompositionDevice4Lab), reinterpret_cast<void**>(&device4));
    report_hr(route, "query_device4", result);
    IDCompositionDevice5Lab* device5 = nullptr;
    report_hr(route, "query_device5",
              (*device).QueryInterface(__uuidof(IDCompositionDevice5Lab), reinterpret_cast<void**>(&device5)));
    if (device4 == nullptr) {
        release(device);
        return;
    }
    BOOL supported = FALSE;
    const HRESULT support_result = (*device4).CheckCompositionTextureSupport(gpu.device, &supported);
    report(route, "support", "%d hr=0x%08lX", supported, static_cast<unsigned long>(support_result));

    HWND window = create_window(L"dcomp_texture", WS_EX_NOREDIRECTIONBITMAP, nullptr, true);
    IDCompositionTarget* target = nullptr;
    IDCompositionVisual2* visual = nullptr;
    result = (*device).CreateTargetForHwnd(window, TRUE, &target);
    if (SUCCEEDED(result)) result = (*device).CreateVisual(&visual);
    if (SUCCEEDED(result)) result = (*target).SetRoot(visual);
    bool shown = false;
    IDCompositionTextureLab* kept = nullptr;
    const TextureVariant* kept_variant = nullptr;
    for (const TextureVariant& variant : texture_variants) {
        char key[128]{};
        ID3D11Texture2D* texture = nullptr;
        HRESULT variant_result = create_variant(gpu, variant, &texture);
        std::snprintf(key, sizeof(key), "%s.create", variant.name);
        report_hr(route, key, variant_result);
        if (FAILED(variant_result)) continue;
        variant_result = write_variant(gpu, variant, texture, 14);
        std::snprintf(key, sizeof(key), "%s.write", variant.name);
        report_hr(route, key, variant_result);
        IDCompositionTextureLab* composition_texture = nullptr;
        variant_result = (*device4).CreateCompositionTexture(texture, &composition_texture);
        std::snprintf(key, sizeof(key), "%s.composition_texture", variant.name);
        report_hr(route, key, variant_result);
        if (SUCCEEDED(variant_result) && !shown && visual != nullptr) {
            (*composition_texture).SetAlphaMode(DXGI_ALPHA_MODE_PREMULTIPLIED);
            HRESULT show_result = (*visual).SetContent(composition_texture);
            if (SUCCEEDED(show_result)) show_result = (*device).Commit();
            std::snprintf(key, sizeof(key), "%s.commit", variant.name);
            report_hr(route, key, show_result);
            pump(300);
            std::snprintf(key, sizeof(key), "%s.frame14", variant.name);
            report_readback(route, key, window, 14, 0);
            shown = SUCCEEDED(show_result);
            kept = composition_texture;
            composition_texture = nullptr;
            kept_variant = &variant;
        }
        release(composition_texture);
        release(texture);
    }
    // Dynamic texture (24H2): a second texture with a dirty rectangle.
    if (device5 != nullptr && kept != nullptr && visual != nullptr) {
        IDCompositionDynamicTextureLab* dynamic = nullptr;
        result = (*device5).CreateDynamicTexture(&dynamic);
        report_hr(route, "dynamic.create", result);
        if (SUCCEEDED(result)) {
            // Slot 0 only: the three-argument form under MSVC's reverse
            // overload order, and harmless (extra arguments ignored) if the
            // order is the other way.
            const D2D_RECT_L whole{0, 0, window_width, window_height};
            report_hr(route, "dynamic.set_texture_whole_slot0", (*dynamic).SetTextureSlot0(kept, &whole, 1U));
            report_hr(route, "dynamic.set_content", (*visual).SetContent(dynamic));
            report_hr(route, "dynamic.commit", (*device).Commit());
            pump(200);
            ID3D11Texture2D* next = nullptr;
            IDCompositionTextureLab* next_texture = nullptr;
            HRESULT next_result = create_variant(gpu, *kept_variant, &next);
            if (SUCCEEDED(next_result)) next_result = write_variant(gpu, *kept_variant, next, 15);
            if (SUCCEEDED(next_result)) next_result = (*device4).CreateCompositionTexture(next, &next_texture);
            report_hr(route, "dynamic.second_texture", next_result);
            if (SUCCEEDED(next_result)) {
                (*next_texture).SetAlphaMode(DXGI_ALPHA_MODE_PREMULTIPLIED);
                const D2D_RECT_L dirty{window_width / 2, 0, window_width, window_height};
                report_hr(route, "dynamic.set_texture_dirty_slot0", (*dynamic).SetTextureSlot0(next_texture, &dirty, 1U));
                report_hr(route, "dynamic.commit_dirty", (*device).Commit());
                pump(200);
                report_readback(route, "dynamic.frame15_dirty", window, 15, 0);
            }
            release(next_texture);
            release(next);
        }
        release(dynamic);
    }
    release(kept);
    release(visual);
    release(target);
    release(device5);
    release(device4);
    release(device);
    DestroyWindow(window);
    pump(50);
}

} // namespace

void run_dcomp_routes() {
    run_null_device_route();
    Gpu gpu{};
    if (!create_gpu("dcomp", gpu, true, false)) {
        report("dcomp", "device", "unavailable");
        release_gpu(gpu);
        return;
    }
    run_surface_route(gpu);
    run_composition_texture_route(gpu);
    release_gpu(gpu);
}

} // namespace lab
