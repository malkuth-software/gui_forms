#pragma once

// The Win32 host's accessibility publisher for one window's semantic tree,
// served through WM_GETOBJECT: MSAA (IAccessible) objects for OBJID_CLIENT
// and UI Automation providers (windows_automation.hpp) for
// UiaRootObjectId. Every member is called on the window's UI thread.

#include "gui_forms/window.hpp"

#include <windows.h>

#include <memory>

namespace gui_forms::host {

struct SemanticSource;

class WindowsAccessibility final {
public:
    WindowsAccessibility();
    ~WindowsAccessibility();
    WindowsAccessibility(const WindowsAccessibility&) = delete;
    WindowsAccessibility& operator=(const WindowsAccessibility&) = delete;

    // Publishes `model`'s tree for `hwnd`. The window must outlive the
    // attachment; detach ends it.
    void attach(HWND hwnd, Window& model, double scale);
    // Ends the attachment: objects already handed out stay valid COM
    // objects and report CO_E_OBJNOTCONNECTED (MSAA) or
    // UIA_E_ELEMENTNOTAVAILABLE (UI Automation) from then on, and UI
    // Automation's references for the window are released.
    void detach() noexcept;
    // Answers WM_GETOBJECT for OBJID_CLIENT with the client object (an
    // LRESULT from LresultFromObject), or 0 when nothing is attached.
    LRESULT object(WPARAM parameter) noexcept;
    // Answers WM_GETOBJECT for UiaRootObjectId with the window's fragment
    // root (an LRESULT from UiaReturnRawElementProvider), or 0 when nothing
    // is attached.
    LRESULT automation_object(WPARAM wparam, LPARAM lparam);
    // Whether a WM_GETOBJECT `lparam` asks for the UI Automation root.
    [[nodiscard]] static bool automation_request(LPARAM lparam) noexcept;
    // Raises the MSAA and UI Automation events for a tree that changed since
    // the last call.
    void notify(double scale);

private:
    // Takes one reference on the calling thread's COM apartment
    // (CoInitializeEx) the first time the thread serves a UI Automation
    // provider, and never releases it.
    // - Owner: the thread, not this object. A thread_local flag records that
    //   the thread holds it; WindowsAccessibility objects come and go with
    //   their windows and never release it.
    // - Lifetime: from that first provider until the thread exits; the
    //   apartment then ends with the thread, after every run on it. A run's
    //   own CoInitializeEx/CoUninitialize pair no longer ends the apartment.
    // - Why: UI Automation refuses a thread's providers
    //   (UiaReturnRawElementProvider answers E_INVALIDARG) once that thread's
    //   apartment has ended and begun again while a client still held an
    //   element. UiaDisconnectAllProviders before the apartment ended was
    //   not enough: it releases the providers UI Automation holds, but the
    //   refusal persisted in the next apartment (measured; see
    //   windows_accessibility.cpp).
    static void pin_automation_apartment() noexcept;

    std::shared_ptr<SemanticSource> source_;
};

} // namespace gui_forms::host
