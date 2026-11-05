// Native popups (owner, 2026-11-04): Window::open_native_popup and the
// token that keeps a popup open. See native_popup.hpp.
#include "gui_forms/window/native_popup/native_popup.hpp"

#include "gui_forms/window.hpp"
#include "../../application/native_popup_record.hpp"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace gui_forms {

namespace {
// Largest popup extent, as a frame request's (HostWindowing::request_frame).
constexpr double maximum_popup_extent = 32768.0;

bool popup_bounds_valid(Rect bounds) noexcept {
    const double bound = HostWindowing::maximum_desktop_coordinate;
    const bool finite = std::isfinite(bounds.x) && std::isfinite(bounds.y) && std::isfinite(bounds.width) &&
                        std::isfinite(bounds.height);
    if (!finite) return false;
    const bool extents = bounds.width >= 1.0 && bounds.height >= 1.0 && bounds.width <= maximum_popup_extent &&
                         bounds.height <= maximum_popup_extent;
    const bool origin = std::abs(bounds.x) <= bound && std::abs(bounds.y) <= bound;
    return extents && origin;
}

bool detached_root(const Control::Ptr& content) noexcept {
    const bool detached = content && (*content).attached_surface_root() == nullptr && !(*content).parent() &&
                          (*content).is_alive();
    return detached;
}
} // namespace

NativePopupToken::~NativePopupToken() {
    disconnect();
}

NativePopupToken::NativePopupToken(NativePopupToken&& other) noexcept
    : native_(std::move(other.native_)), overlay_(std::move(other.overlay_)), surface_(other.surface_),
      refusal_(other.refusal_) {
    other.surface_ = NativePopupSurface::none;
    other.refusal_ = NativePopupRefusal::none;
}

NativePopupToken& NativePopupToken::operator=(NativePopupToken&& other) noexcept {
    if (this == &other) return *this;
    disconnect();
    native_ = std::move(other.native_);
    overlay_ = std::move(other.overlay_);
    surface_ = other.surface_;
    refusal_ = other.refusal_;
    other.surface_ = NativePopupSurface::none;
    other.refusal_ = NativePopupRefusal::none;
    return *this;
}

void NativePopupToken::disconnect() noexcept {
    if (native_) {
        // Held until the window is destroyed: destroy_window runs the
        // popup's closed callback, which may release other tokens.
        const std::shared_ptr<detail::NativePopupRecord> record = std::move(native_);
        (*record).disconnect();
    }
    overlay_.disconnect();
}

bool NativePopupToken::connected() const noexcept {
    if (native_) return (*native_).connected();
    const bool overlay = overlay_.connected();
    return overlay;
}

Window* NativePopupToken::window() const noexcept {
    if (!native_) return nullptr;
    Window* model = (*native_).model();
    return model;
}

HostWindowId NativePopupToken::id() const noexcept {
    if (!native_) return HostWindowId{};
    const HostWindowId window = (*native_).id();
    return window;
}

NativePopupToken Window::open_native_popup(const Control::Ptr& owner, const Control::Ptr& content,
                                           const Rect bounds, const NativePopupOptions& options) {
    require_root_thread("native popup");
    const bool owner_available = owner && (*owner).attached_surface_root() == this && (*owner).is_alive() &&
                                 (*owner).effectively_visible();
    if (!owner_available) throw std::logic_error("GUI.Forms native popup requires a visible attached owner");
    if (!detached_root(content)) throw std::logic_error("GUI.Forms native popup content must be a live detached root");
    if (!popup_bounds_valid(bounds)) throw std::invalid_argument("GUI.Forms native popup bounds are out of range");
    if (options.backdrop.has_value() && !backdrop_chain_valid(*options.backdrop)) {
        throw std::invalid_argument("GUI.Forms native popup backdrop chain is not valid");
    }

    NativePopupToken token;
    token.refusal_ = detail::native_popup_refusal(application_window_);
    if (token.refusal_ == NativePopupRefusal::none) {
        native_popup_serial_ = native_popup_serial_ + 1U;
        detail::NativePopupOpening opening{};
        opening.owner = application_window_;
        opening.serial = native_popup_serial_;
        opening.content = content;
        opening.bounds = bounds;
        opening.focus = options.focus;
        opening.backdrop = options.backdrop;
        opening.key_bindings = key_bindings_;
        opening.resources = resources();
        std::shared_ptr<detail::NativePopupRecord> record = detail::open_native_popup_window(opening);
        if (record) {
            // The opener holds it weakly: its death closes the popup.
            (*owner).own_revocable(record);
            token.native_ = std::move(record);
            token.surface_ = NativePopupSurface::native;
            return token;
        }
        token.refusal_ = NativePopupRefusal::host_refused;
    }
    if (options.fallback == NativePopupFallback::refuse) return token;
    // A refusal after the native request started consumed the content.
    if (!detached_root(content)) return token;
    (*content).set_requested_bounds(bounds);
    // The owner need only be visible, as for the native path.
    PopupOptions overlay{};
    overlay.require_enabled_owner = false;
    token.overlay_ = open_popup(owner, content, overlay);
    token.surface_ = NativePopupSurface::overlay;
    return token;
}

} // namespace gui_forms
