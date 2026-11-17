#include "gpu.hpp"

#include <dispatcherqueue.h>
#include <roapi.h>
#include <winstring.h>
#include <windows.ui.composition.interop.h>

// Windows.UI.Composition from plain Win32: a DispatcherQueue on this
// thread, the Compositor activated by class name, and its interop
// interfaces. Drawing surfaces need a graphics device made from a D3D11 or
// D2D device (ICompositorInterop::CreateGraphicsDevice).
namespace lab {

#undef INTERFACE
#define INTERFACE ICompositorDesktopInteropLab
// Windows SDK 10.0.28000.0 winrt/windows.ui.composition.interop.h.
DECLARE_INTERFACE_IID_(ICompositorDesktopInteropLab, IUnknown, "29E691FA-4567-4DCA-B319-D0F207EB6807") {
    STDMETHOD(CreateDesktopWindowTarget)(THIS_ HWND hwndTarget, BOOL isTopmost, IUnknown** result) PURE;
    STDMETHOD(EnsureOnThread)(THIS_ DWORD threadId) PURE;
};
#undef INTERFACE

} // namespace lab

__CRT_UUID_DECL(lab::ICompositorDesktopInteropLab, 0x29e691fa, 0x4567, 0x4dca, 0xb3, 0x19, 0xd0, 0xf2, 0x07, 0xeb, 0x68, 0x07)

namespace lab {

void run_winrt_route() {
    const char* route = "winrt_composition";
    HRESULT result = RoInitialize(RO_INIT_SINGLETHREADED);
    report_hr(route, "ro_initialize", result);
    DispatcherQueueOptions options{};
    options.dwSize = sizeof(options);
    options.threadType = DQTYPE_THREAD_CURRENT;
    options.apartmentType = DQTAT_COM_STA;
    PDISPATCHERQUEUECONTROLLER controller = nullptr;
    result = CreateDispatcherQueueController(options, &controller);
    report_hr(route, "dispatcher_queue", result);
    HSTRING class_name = nullptr;
    const wchar_t* name = L"Windows.UI.Composition.Compositor";
    WindowsCreateString(name, static_cast<UINT32>(wcslen(name)), &class_name);
    IInspectable* compositor = nullptr;
    result = RoActivateInstance(class_name, &compositor);
    report_hr(route, "activate_compositor", result);
    WindowsDeleteString(class_name);
    if (SUCCEEDED(result)) {
        ABI::Windows::UI::Composition::ICompositorInterop* interop = nullptr;
        result = (*compositor).QueryInterface(__uuidof(ABI::Windows::UI::Composition::ICompositorInterop),
                                              reinterpret_cast<void**>(&interop));
        report_hr(route, "compositor_interop", result);
        ICompositorDesktopInteropLab* desktop = nullptr;
        report_hr(route, "compositor_desktop_interop",
                  (*compositor).QueryInterface(__uuidof(ICompositorDesktopInteropLab),
                                               reinterpret_cast<void**>(&desktop)));
        if (interop != nullptr) {
            ABI::Windows::UI::Composition::ICompositionGraphicsDevice* graphics = nullptr;
            report_hr(route, "graphics_device_null", (*interop).CreateGraphicsDevice(nullptr, &graphics));
            release(graphics);
            Gpu gpu{};
            if (create_gpu(route, gpu, true, false)) {
                report_hr(route, "graphics_device_d3d11", (*interop).CreateGraphicsDevice(gpu.device, &graphics));
                release(graphics);
            }
            release_gpu(gpu);
        }
        if (desktop != nullptr) {
            HWND window = create_window(L"winrt_composition", WS_EX_NOREDIRECTIONBITMAP, nullptr, false);
            IUnknown* target = nullptr;
            report_hr(route, "desktop_window_target", (*desktop).CreateDesktopWindowTarget(window, TRUE, &target));
            release(target);
            DestroyWindow(window);
        }
        release(desktop);
        release(interop);
    }
    release(compositor);
    if (controller != nullptr) (*controller).Release();
    RoUninitialize();
}

} // namespace lab
