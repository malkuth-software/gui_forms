#include "gpu.hpp"

#include <dcomp.h>

// The Composition Swapchain (presentation manager) API. mingw-w64 has no
// presentation.h; the interfaces below are copied from the Windows SDK
// 10.0.28000.0 um/Presentation.h (IPresentationBuffer line 146,
// IPresentationContent 241, IPresentationSurface 325, IPresentationManager
// 753, IPresentationFactory 975, CreatePresentationFactory 1580), which
// exports from dcomp.dll.
namespace lab {

struct SystemInterruptTimeLab final {
    UINT64 value{};
};

struct PresentationTransformLab final {
    float m11{};
    float m12{};
    float m21{};
    float m22{};
    float m31{};
    float m32{};
};

#undef INTERFACE
#define INTERFACE IPresentationBufferLab
DECLARE_INTERFACE_IID_(IPresentationBufferLab, IUnknown, "2E217D3A-5ABB-4138-9A13-A775593C89CA") {
    STDMETHOD(GetAvailableEvent)(THIS_ HANDLE* availableEventHandle) PURE;
    STDMETHOD(IsAvailable)(THIS_ boolean* isAvailable) PURE;
};

#undef INTERFACE
#define INTERFACE IPresentationContentLab
DECLARE_INTERFACE_IID_(IPresentationContentLab, IUnknown, "5668BB79-3D8E-415C-B215-F38020F2D252") {
    STDMETHOD_(void, SetTag)(THIS_ UINT_PTR tag) PURE;
};

#undef INTERFACE
#define INTERFACE IPresentationSurfaceLab
DECLARE_INTERFACE_IID_(IPresentationSurfaceLab, IPresentationContentLab, "956710FB-EA40-4EBA-A3EB-4375A0EB4EDC") {
    STDMETHOD(SetBuffer)(THIS_ IPresentationBufferLab* presentationBuffer) PURE;
    STDMETHOD(SetColorSpace)(THIS_ DXGI_COLOR_SPACE_TYPE colorSpace) PURE;
    STDMETHOD(SetAlphaMode)(THIS_ DXGI_ALPHA_MODE alphaMode) PURE;
    STDMETHOD(SetSourceRect)(THIS_ const RECT* sourceRect) PURE;
    STDMETHOD(SetTransform)(THIS_ PresentationTransformLab* transform) PURE;
    STDMETHOD(RestrictToOutput)(THIS_ IUnknown* output) PURE;
    STDMETHOD(SetDisableReadback)(THIS_ boolean value) PURE;
    STDMETHOD(SetLetterboxingMargins)(THIS_ float left, float top, float right, float bottom) PURE;
};

#undef INTERFACE
#define INTERFACE IPresentationManagerLab
DECLARE_INTERFACE_IID_(IPresentationManagerLab, IUnknown, "FB562F82-6292-470A-88B1-843661E7F20C") {
    STDMETHOD(AddBufferFromResource)(THIS_ IUnknown* resource, IPresentationBufferLab** presentationBuffer) PURE;
    STDMETHOD(CreatePresentationSurface)(THIS_ HANDLE compositionSurfaceHandle,
                                         IPresentationSurfaceLab** presentationSurface) PURE;
    STDMETHOD_(UINT64, GetNextPresentId)(THIS) PURE;
    STDMETHOD(SetTargetTime)(THIS_ SystemInterruptTimeLab targetTime) PURE;
    STDMETHOD(SetPreferredPresentDuration)(THIS_ SystemInterruptTimeLab preferred, SystemInterruptTimeLab tolerance) PURE;
    STDMETHOD(ForceVSyncInterrupt)(THIS_ boolean force) PURE;
    STDMETHOD(Present)(THIS) PURE;
    STDMETHOD(GetPresentRetiringFence)(THIS_ REFIID riid, void** fence) PURE;
    STDMETHOD(CancelPresentsFrom)(THIS_ UINT64 presentIdToCancelFrom) PURE;
    STDMETHOD(GetLostEvent)(THIS_ HANDLE* lostEventHandle) PURE;
    STDMETHOD(GetPresentStatisticsAvailableEvent)(THIS_ HANDLE* eventHandle) PURE;
    STDMETHOD(EnablePresentStatisticsKind)(THIS_ int kind, boolean enabled) PURE;
    STDMETHOD(GetNextPresentStatistics)(THIS_ IUnknown** statistics) PURE;
};

#undef INTERFACE
#define INTERFACE IPresentationFactoryLab
DECLARE_INTERFACE_IID_(IPresentationFactoryLab, IUnknown, "8FB37B58-1D74-4F64-A49C-1F97A80A2EC0") {
    STDMETHOD_(boolean, IsPresentationSupported)(THIS) PURE;
    STDMETHOD_(boolean, IsPresentationSupportedWithIndependentFlip)(THIS) PURE;
    STDMETHOD(CreatePresentationManager)(THIS_ IPresentationManagerLab** manager) PURE;
};
#undef INTERFACE

} // namespace lab

__CRT_UUID_DECL(lab::IPresentationFactoryLab, 0x8fb37b58, 0x1d74, 0x4f64, 0xa4, 0x9c, 0x1f, 0x97, 0xa8, 0x0a, 0x2e, 0xc0)

namespace lab {

namespace {

using CreatePresentationFactoryFunction = HRESULT(WINAPI*)(IUnknown* d3d_device, REFIID riid, void** factory);

struct BufferVariant final {
    const char* name{};
    bool cpu{};
    D3D11_TEXTURE_LAYOUT layout{};
    UINT misc{};
};

const BufferVariant buffer_variants[] = {
    {"default_plain", false, D3D11_TEXTURE_LAYOUT_UNDEFINED, 0},
    {"default_shared_nthandle", false, D3D11_TEXTURE_LAYOUT_UNDEFINED,
     D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE},
    {"default_shared_displayable", false, D3D11_TEXTURE_LAYOUT_UNDEFINED,
     D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_DISPLAYABLE},
    {"cpu_undefined_shared_nthandle", true, D3D11_TEXTURE_LAYOUT_UNDEFINED,
     D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE},
    {"cpu_row_major_shared_nthandle", true, D3D11_TEXTURE_LAYOUT_ROW_MAJOR,
     D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE},
    {"cpu_undefined_shared_displayable", true, D3D11_TEXTURE_LAYOUT_UNDEFINED,
     D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_DISPLAYABLE},
};

HRESULT create_buffer_texture(const Gpu& gpu, const BufferVariant& variant, ID3D11Texture2D** texture) {
    if (variant.cpu) return create_cpu_texture(gpu, variant.layout, variant.misc, texture);
    const HRESULT result = create_texture(gpu, D3D11_USAGE_DEFAULT, 0,
                                          D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, variant.misc,
                                          texture);
    return result;
}

} // namespace

void run_presentation_manager_route() {
    const char* route = "presentation_manager";
    HMODULE dcomp = LoadLibraryW(L"dcomp.dll");
    CreatePresentationFactoryFunction create_factory = nullptr;
    if (dcomp != nullptr) {
        create_factory = reinterpret_cast<CreatePresentationFactoryFunction>(
            reinterpret_cast<void*>(GetProcAddress(dcomp, "CreatePresentationFactory")));
    }
    report(route, "export", "%s", create_factory != nullptr ? "present" : "absent");
    if (create_factory == nullptr) return;
    IPresentationFactoryLab* factory = nullptr;
    report_hr(route, "factory_null_device",
              create_factory(nullptr, __uuidof(IPresentationFactoryLab), reinterpret_cast<void**>(&factory)));
    release(factory);
    Gpu gpu{};
    if (!create_gpu(route, gpu, true, false)) {
        report(route, "device", "unavailable");
        release_gpu(gpu);
        return;
    }
    HRESULT result = create_factory(gpu.device, __uuidof(IPresentationFactoryLab), reinterpret_cast<void**>(&factory));
    report_hr(route, "factory", result);
    if (FAILED(result)) {
        release_gpu(gpu);
        return;
    }
    const boolean supported = (*factory).IsPresentationSupported();
    const boolean independent_flip = (*factory).IsPresentationSupportedWithIndependentFlip();
    report(route, "supported", "%d independent_flip=%d", supported, independent_flip);
    IPresentationManagerLab* manager = nullptr;
    result = (*factory).CreatePresentationManager(&manager);
    report_hr(route, "manager", result);
    HANDLE surface_handle = nullptr;
    IPresentationSurfaceLab* surface = nullptr;
    if (SUCCEEDED(result)) result = DCompositionCreateSurfaceHandle(COMPOSITIONOBJECT_ALL_ACCESS, nullptr, &surface_handle);
    if (SUCCEEDED(result)) result = (*manager).CreatePresentationSurface(surface_handle, &surface);
    report_hr(route, "surface", result);
    HANDLE lost_event = nullptr;
    if (manager != nullptr) {
        const HRESULT lost_result = (*manager).GetLostEvent(&lost_event);
        report(route, "lost_event", "hr=0x%08lX handle=%d", static_cast<unsigned long>(lost_result),
               lost_event != nullptr ? 1 : 0);
    }
    IPresentationBufferLab* chosen = nullptr;
    const BufferVariant* chosen_variant = nullptr;
    ID3D11Texture2D* chosen_texture = nullptr;
    for (const BufferVariant& variant : buffer_variants) {
        if (manager == nullptr) break;
        char key[128]{};
        ID3D11Texture2D* texture = nullptr;
        HRESULT variant_result = create_buffer_texture(gpu, variant, &texture);
        std::snprintf(key, sizeof(key), "%s.create", variant.name);
        report_hr(route, key, variant_result);
        if (FAILED(variant_result)) continue;
        IPresentationBufferLab* buffer = nullptr;
        variant_result = (*manager).AddBufferFromResource(texture, &buffer);
        std::snprintf(key, sizeof(key), "%s.add_buffer", variant.name);
        report_hr(route, key, variant_result);
        // Prefer a CPU-writable buffer: the one route here with no GPU copy.
        const bool better = chosen == nullptr || (variant.cpu && !(*chosen_variant).cpu);
        if (SUCCEEDED(variant_result) && better) {
            release(chosen);
            release(chosen_texture);
            chosen = buffer;
            buffer = nullptr;
            chosen_texture = texture;
            texture = nullptr;
            chosen_variant = &variant;
        }
        release(buffer);
        release(texture);
    }
    HWND window = create_window(L"presentation_manager", WS_EX_NOREDIRECTIONBITMAP, nullptr, true);
    if (chosen != nullptr && surface != nullptr) {
        report(route, "chosen_buffer", "%s", (*chosen_variant).name);
        HRESULT write_result = E_FAIL;
        if ((*chosen_variant).cpu) {
            write_result = cpu_write_texture(gpu, chosen_texture, (*chosen_variant).layout, 15, true);
        } else {
            write_result = upload_pattern(gpu, chosen_texture, nullptr, 15, true);
        }
        report_hr(route, "write", write_result);
        (*gpu.context).Flush();
        PresentationTransformLab identity{1.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F};
        const RECT source{0, 0, window_width, window_height};
        report_hr(route, "set_buffer", (*surface).SetBuffer(chosen));
        report_hr(route, "set_alpha", (*surface).SetAlphaMode(DXGI_ALPHA_MODE_PREMULTIPLIED));
        report_hr(route, "set_source_rect", (*surface).SetSourceRect(&source));
        report_hr(route, "set_transform", (*surface).SetTransform(&identity));
        report_hr(route, "present", (*manager).Present());
        IDCompositionDesktopDevice* desktop = nullptr;
        IDCompositionTarget* target = nullptr;
        IDCompositionVisual2* visual = nullptr;
        IUnknown* content = nullptr;
        HRESULT bind = DCompositionCreateDevice2(nullptr, __uuidof(IDCompositionDesktopDevice),
                                                 reinterpret_cast<void**>(&desktop));
        if (SUCCEEDED(bind)) bind = (*desktop).CreateTargetForHwnd(window, TRUE, &target);
        if (SUCCEEDED(bind)) bind = (*desktop).CreateSurfaceFromHandle(surface_handle, &content);
        if (SUCCEEDED(bind)) bind = (*desktop).CreateVisual(&visual);
        if (SUCCEEDED(bind)) bind = (*visual).SetContent(content);
        if (SUCCEEDED(bind)) bind = (*target).SetRoot(visual);
        if (SUCCEEDED(bind)) bind = (*desktop).Commit();
        report_hr(route, "bind_visual_null_device", bind);
        pump(300);
        report_readback(route, "frame15", window, 15, 0);
        boolean available = 0;
        const HRESULT available_result = (*chosen).IsAvailable(&available);
        report(route, "buffer_available_after_present", "%d hr=0x%08lX", available,
               static_cast<unsigned long>(available_result));
        if (lost_event != nullptr) {
            report(route, "lost_signalled", "%d", WaitForSingleObject(lost_event, 0) == WAIT_OBJECT_0 ? 1 : 0);
        }
        release(visual);
        release(content);
        release(target);
        release(desktop);
    }
    release(chosen);
    release(chosen_texture);
    release(surface);
    release(manager);
    release(factory);
    if (surface_handle != nullptr) CloseHandle(surface_handle);
    release_gpu(gpu);
    DestroyWindow(window);
    pump(50);
}

} // namespace lab
