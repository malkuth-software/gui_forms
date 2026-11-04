// The window's backdrop request and grant (stage T5, compositor
// cooperation; notes:planning/stages/T5-compositor/PLAN.md §2.3 and §2.4).
#include "gui_forms/window.hpp"

#include "gui_forms/host/services/host_services.hpp"
#include "../popup/popup_attachment.hpp"

#include <stdexcept>

namespace gui_forms {
namespace {

// Whether two grants differ in what a handler is told: the backdrop, the
// reason or the request. A new generation alone is not a change.
bool grant_outcome_differs(const WindowBackdropGrant& left, const WindowBackdropGrant& right) noexcept {
    const bool differs = left.requested != right.requested || left.granted != right.granted ||
                         left.reason != right.reason;
    return differs;
}

} // namespace

bool Window::declare_non_opaque() {
    require_root_thread("non-opaque declaration");
    if (host_session_bound_) return false;
    if (non_opaque_declared_) return true;
    non_opaque_declared_ = true;
    refresh_backdrop();
    return true;
}

void Window::set_compositor_state(const CompositorState& state, bool host_preferences) {
    require_root_thread("compositor state");
    const CompositorState previous = compositor_state_;
    compositor_state_ = state;
    PresentationSettings settings = presentation_settings_;
    if (host_preferences) {
        if (state.high_contrast != previous.high_contrast) settings.high_contrast = state.high_contrast;
        if (state.reduced_transparency != previous.reduced_transparency) {
            settings.reduced_transparency = state.reduced_transparency;
        }
    }
    if (settings == presentation_settings_) {
        refresh_backdrop();
        return;
    }
    // Resolves the grant again after presentation_changed is raised.
    set_presentation_settings(settings);
}

WindowBackdropGrant Window::request_backdrop(const WindowBackdropRequest& request) {
    require_root_thread("backdrop request");
    if (!window_backdrop_request_valid(request)) {
        throw std::invalid_argument(
            "GUI.Forms backdrop request needs one to three distinct entries with opaque only last");
    }
    backdrop_request_ = request;
    application_backdrop_request_ = true;
    refresh_backdrop();
    const WindowBackdropGrant grant = backdrop_grant_;
    return grant;
}

void Window::refresh_backdrop() {
    const WindowBackdropConditions conditions{non_opaque_declared_, presentation_settings_.high_contrast,
                                              presentation_settings_.reduced_transparency};
    WindowBackdropGrant next = resolve_window_backdrop(backdrop_request_, compositor_state_, conditions);
    const WindowBackdrop previous_backdrop = backdrop_grant_.granted;
    if (next.granted != previous_backdrop && host_services_ != nullptr) {
        const HostServiceStatus applied = (*host_services_).apply_window_backdrop(next.granted);
        if (!applied.accepted() && next.granted != WindowBackdrop::opaque) {
            // A failed application leaves the native window as it was
            // (HostServices contract); the window then falls back to opaque,
            // which an adapter does not fail.
            if (previous_backdrop != WindowBackdrop::opaque) {
                static_cast<void>((*host_services_).apply_window_backdrop(WindowBackdrop::opaque));
            }
            next.granted = WindowBackdrop::opaque;
            next.reason = WindowBackdropReason::backend_failure;
        }
    }
    const bool outcome_changed = grant_outcome_differs(next, backdrop_grant_);
    const bool backdrop_changed = next.granted != previous_backdrop;
    backdrop_grant_ = next;
    if (!outcome_changed) return;
    if (backdrop_changed) {
        // The backplane's base depends on the backdrop (PLAN.md §4.1): every
        // part that sampled it is baked again, and the window repaints.
        damage_backplane_readers(nullptr);
        mark_subtree_dirty(*root_, Dirty::paint);
        for (const std::shared_ptr<gui_forms::detail::PopupAttachment>& popup : popups_) {
            if (const Control::Ptr overlay = (*popup).popup()) mark_subtree_dirty(*overlay, Dirty::paint);
        }
    }
    // Held here: a handler that requests another backdrop replaces
    // backdrop_grant_ while later handlers of this emission still run.
    const WindowBackdropGrant emitted = backdrop_grant_;
    backdrop_changed_.emit(emitted);
}

const SurfaceMaterial& Window::backplane_base(const ControlVisualContext& context) const noexcept {
    const ThemeWindowBackdrop& themed = (*theme_).definition().window_backdrop;
    const SurfaceMaterial* const base = themed.base_for(backdrop_grant_.granted);
    if (backdrop_grant_.granted != WindowBackdrop::opaque && base != nullptr) return *base;
    const SurfaceMaterial& recipe = (*theme_).resolve(ControlVisualRole::window, context).material;
    return recipe;
}

} // namespace gui_forms
