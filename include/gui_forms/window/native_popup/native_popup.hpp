#pragma once

#include "gui_forms/host/windowing/host_windowing.hpp"
#include "gui_forms/surface_root/surface_root.hpp"
#include "gui_forms/window/backdrop/backdrop_chain.hpp"

#include <cstdint>
#include <memory>
#include <optional>

namespace gui_forms {

namespace detail {
class NativePopupRecord;
}

// Native popups (owner, 2026-11-04): a popup is its own undecorated native
// window, owned by the window that opens it, with per-pixel alpha, so it
// may extend past its owner's edges. GUI.Forms paints its shadow inside
// that alpha surface. Overlays (menus, drop-downs and tooltips composited
// into their owner, SurfaceRoot::open_popup) are a different mechanism and
// are unchanged. Modality stays with the caller: a dialog that wants a
// scrim keeps drawing it in its owner, and only the panel goes native.

// What happens when the run cannot open a native popup: no run (a window
// outside Application::run), a host without native popups, or a refusal.
enum class NativePopupFallback : std::uint8_t {
    // The content opens as an overlay in its owner instead.
    overlay,
    // Nothing opens; the token reports NativePopupSurface::none.
    refuse,
};

// Why the native path did not open a popup window (NativePopupToken::
// refusal); the fallback then decides what opens.
enum class NativePopupRefusal : std::uint8_t {
    // The native path opened the popup window.
    none,
    // The window is not a ready window of a live application run.
    no_run,
    // The run's host does not open popup windows.
    unsupported,
    // The host refused this popup window (placement or creation failed).
    host_refused,
};

// Where an opened popup's content went.
enum class NativePopupSurface : std::uint8_t {
    none,
    native,
    overlay,
};

struct NativePopupOptions final {
    // Declared per popup; there is no global rule (owner, 2026-11-04).
    HostPopupFocus focus{HostPopupFocus::passive};
    // The popup window's backdrop chain (Window::set_backdrop_chain): a blur
    // is the system material granted to the popup window, and GUI.Forms
    // paints only the tint. None by default.
    std::optional<BackdropChain> backdrop{};
    NativePopupFallback fallback{NativePopupFallback::overlay};
};

class Window;

// Keeps one popup open (Window::open_native_popup). Destroying or
// disconnecting the token closes the popup, as does the death of the
// control that opened it or the closing of its window by the host. UI
// thread only.
class NativePopupToken final {
public:
    NativePopupToken() noexcept = default;
    ~NativePopupToken();
    NativePopupToken(NativePopupToken&& other) noexcept;
    NativePopupToken& operator=(NativePopupToken&& other) noexcept;
    NativePopupToken(const NativePopupToken&) = delete;
    NativePopupToken& operator=(const NativePopupToken&) = delete;

    // Closes the popup: destroys its native window, or closes the overlay.
    void disconnect() noexcept;
    // Whether the popup is still open.
    [[nodiscard]] bool connected() const noexcept;
    [[nodiscard]] NativePopupSurface surface() const noexcept { return surface_; }
    // Why the native path did not open the popup; none when it did, and for
    // an empty token.
    [[nodiscard]] NativePopupRefusal refusal() const noexcept { return refusal_; }
    // The native popup's own window model while it is open; null for an
    // overlay or once it has closed.
    [[nodiscard]] Window* window() const noexcept;
    // The native popup window's identity in the run; invalid for an overlay
    // or once it has closed.
    [[nodiscard]] HostWindowId id() const noexcept;

private:
    friend class Window;
    std::shared_ptr<detail::NativePopupRecord> native_{};
    PopupToken overlay_{};
    NativePopupSurface surface_{NativePopupSurface::none};
    NativePopupRefusal refusal_{NativePopupRefusal::none};
};

} // namespace gui_forms
