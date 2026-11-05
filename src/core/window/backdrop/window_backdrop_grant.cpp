// The window's backdrop request and grant (stage T5, compositor
// cooperation; notes:planning/stages/T5-compositor/PLAN.md §2.3 and §2.4).
#include "gui_forms/window.hpp"

#include "gui_forms/host/services/host_services.hpp"
#include "gui_forms/resources/application_resources/application_resources.hpp"
#include "../overlay/overlay_attachment.hpp"

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

bool SurfaceRoot::declare_non_opaque() {
    require_root_thread("non-opaque declaration");
    if (host_session_bound_) return false;
    if (non_opaque_declared_) return true;
    non_opaque_declared_ = true;
    refresh_backdrop();
    return true;
}

void SurfaceRoot::set_compositor_state(const CompositorState& state, bool host_preferences) {
    require_root_thread("compositor state");
    const CompositorState previous = compositor_state_;
    compositor_state_ = state;
    PresentationSettings settings = presentation_settings_;
    if (host_preferences) {
        if (state.high_contrast != previous.high_contrast) settings.high_contrast = state.high_contrast;
        if (state.reduced_transparency != previous.reduced_transparency) {
            settings.reduced_transparency = state.reduced_transparency;
        }
        // The host's light or dark preference and colour hints (T4 theme
        // variants): the scheme is taken only while the application
        // follows the host (ApplicationResources::automatic_colour_scheme).
        host_colour_scheme_ = state.dark_scheme ? ColourScheme::dark : ColourScheme::light;
        (*resources_).note_system_colour_hints(state.system_colours);
        if ((*resources_).automatic_colour_scheme()) settings.colour_scheme = host_colour_scheme_;
    }
    if (settings == presentation_settings_) {
        refresh_backdrop();
        return;
    }
    // Resolves the grant again after presentation_changed is raised.
    set_presentation_settings(settings);
}

WindowBackdropGrant SurfaceRoot::request_backdrop(const WindowBackdropRequest& request) {
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

void SurfaceRoot::refresh_backdrop() {
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
        for (const std::shared_ptr<gui_forms::detail::OverlayAttachment>& overlay : overlays_) {
            if (const Control::Ptr overlay_root = (*overlay).overlay()) mark_subtree_dirty(*overlay_root, Dirty::paint);
        }
    }
    // Held here: a handler that requests another backdrop replaces
    // backdrop_grant_ while later handlers of this emission still run.
    const WindowBackdropGrant emitted = backdrop_grant_;
    backdrop_changed_.emit(emitted);
}

void SurfaceRoot::set_backdrop_chain(std::optional<BackdropChain> chain) {
    require_root_thread("backdrop chain");
    if (chain.has_value() && !backdrop_chain_valid(*chain)) {
        throw std::invalid_argument(
            "GUI.Forms backdrop chain needs at most eight operations in range with at most one blur");
    }
    application_backdrop_chain_ = true;
    apply_backdrop_chain(chain);
}

void SurfaceRoot::lower_theme_backdrop_chain() {
    if (application_backdrop_chain_) return;
    const Window* const window = (*root_).attached_window();
    if (window == nullptr) return;
    const std::optional<BackdropChain> chain = (*theme_).window_backdrop_chain(*window);
    apply_backdrop_chain(chain);
}

void SurfaceRoot::apply_backdrop_chain(const std::optional<BackdropChain>& chain) {
    if (chain == backdrop_chain_) return;
    backdrop_chain_ = chain;
    // A window following its theme's request follows the chain's instead
    // while the chain has a blur (implied_backdrop_request).
    if (!application_backdrop_request_) {
        backdrop_request_ = implied_backdrop_request();
        refresh_backdrop();
    }
    // The tint lies under everything: every part that sampled the
    // backplane is baked again, and the window repaints.
    damage_backplane_readers(nullptr);
    mark_subtree_dirty(*root_, Dirty::paint);
    for (const std::shared_ptr<gui_forms::detail::PopupAttachment>& popup : popups_) {
        if (const Control::Ptr overlay = (*popup).popup()) mark_subtree_dirty(*overlay, Dirty::paint);
    }
}

SurfaceMaterial SurfaceRoot::backplane_base(const ControlVisualContext& context) const {
    const ThemeWindowBackdrop& themed = (*theme_).definition().window_backdrop;
    const bool clear_grant = backdrop_grant_.granted != WindowBackdrop::opaque;
    if (backdrop_chain_.has_value() && !context.high_contrast) {
        const BackdropChainResolution resolution =
            resolve_backdrop_chain(*backdrop_chain_, backdrop_grant_, context.high_contrast);
        // A chain without a tint leaves the base to the theme.
        if (resolution.tint.alpha != 0U) {
            const Color tint = resolution.solid ? Color::rgba(resolution.tint.red, resolution.tint.green,
                                                              resolution.tint.blue, 255U)
                                                : resolution.tint;
            SurfaceMaterial tinted{};
            tinted.fills.clear();
            tinted.fills.push_back(MaterialFillLayer::solid(tint));
            return tinted;
        }
    }
    const SurfaceMaterial* const base = themed.base_for(backdrop_grant_.granted);
    if (clear_grant && base != nullptr) {
        SurfaceMaterial copied = *base;
        return copied;
    }
    SurfaceMaterial recipe = (*theme_).resolve(ControlVisualRole::window, context).material;
    return recipe;
}

std::optional<BackdropChainResolution> SurfaceRoot::backdrop_chain_resolution() const noexcept {
    if (!backdrop_chain_.has_value()) return std::nullopt;
    const BackdropChainResolution resolution =
        resolve_backdrop_chain(*backdrop_chain_, backdrop_grant_, presentation_settings_.high_contrast);
    return resolution;
}

WindowBackdropRequest SurfaceRoot::implied_backdrop_request() const {
    if (backdrop_chain_.has_value() && backdrop_chain_blur(*backdrop_chain_) > 0.0) {
        const WindowBackdropRequest material =
            WindowBackdropRequest::with_fallback(WindowBackdrop::window_material, WindowBackdrop::transparent);
        return material;
    }
    const WindowBackdropRequest themed = (*theme_).definition().window_backdrop.request;
    return themed;
}

} // namespace gui_forms
