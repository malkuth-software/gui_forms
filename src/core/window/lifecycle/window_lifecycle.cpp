#include "gui_forms/window.hpp"
#include "gui_forms/theme/gui_forms_theme/gui_forms_theme.hpp"
#include "gui_forms/host/services/host_services.hpp"
#include "gui_forms/surface_material/painting/surface_material_painting.hpp"
#include "../../dispatcher/state/dispatcher_state.hpp"
#include "../../scheduler/request/scheduled_frame_request.hpp"
#include "../accelerator/accelerator_attachment.hpp"
#include "../overlay/overlay_attachment.hpp"
#include "../tool_tips/tool_tip_presenter.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <optional>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <functional>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <unordered_set>

namespace gui_forms {

Window::Window(Control::Ptr root, Size client_size)
    : Window(std::move(root), client_size, ImageRegistryLimits{}) {}

Window::Window(Control::Ptr root, Size client_size, ImageRegistryLimits image_limits)
    : Window(std::move(root), client_size, std::make_shared<ApplicationResources>(image_limits)) {}

Window::Window(Control::Ptr root, Size client_size,
               std::shared_ptr<ApplicationResources> resources)
    : SurfaceRoot(std::move(root), client_size, std::move(resources)) {
    presenting_window_ = this;
    tool_tip_presenter_ = std::make_unique<detail::ToolTipPresenter>(*this);
    initialize_cursor_identity();
    (*lifetime_).window = this;
    if (!resources_) {
        throw std::invalid_argument("GUI.Forms window requires application resources");
    }
    if (!(*resources_).check_access()) {
        throw std::logic_error("GUI.Forms window resources belong to another thread");
    }
    if (!root_) {
        throw std::invalid_argument("GUI.Forms window requires a retained root control");
    }
    if ((*root_).parent()) {
        throw std::logic_error("GUI.Forms window root may not already have a parent");
    }
    if (!(*root_).is_alive()) {
        throw std::logic_error("GUI.Forms window root may not be disposed");
    }
    attach_subtree(root_, {});
    // The theme's backdrop request (stage T5) until the application makes
    // its own. Undeclared, the window is granted `opaque` whatever it asks.
    backdrop_request_ = (*theme_).definition().window_backdrop.request;
    refresh_backdrop();
    // The theme's `window { backdrop: ... }`, which may imply its own
    // request (implied_backdrop_request).
    lower_theme_backdrop_chain();
    const Rect initial_damage{0.0, 0.0, client_size_.width, client_size_.height};
    add_damage_all_planes(initial_damage);
    metrics_.record_dirty_mark(initial_damage.area());
    // Last: a constructor that throws never leaves itself registered.
    (*resources_).attach(*this);
}
Window::~Window() {
    // The tooltip presenter goes first: it holds an overlay and a wake, and
    // raises nothing while it shuts down.
    if (tool_tip_presenter_) (*tool_tip_presenter_).shutdown();
    invalidate_cursor_interaction(CursorError::closing);
    release_tree();
    (*resources_).detach(*this);
    (*lifetime_).window = nullptr;
}

void SurfaceRoot::release_tree() noexcept {
    paint_wake_handler_ = {};
    paint_wake_pending_ = false;
    abandon_deferred_input();
    // Teardown order (house style §4): the controls are told everything
    // (overlays closing, capture and focus revoked, the tree detached) while
    // the dispatcher still accepts work, and the dispatcher is shut down
    // only after them, which cancels whatever they posted. If a host shut it
    // down already (it closed the window), a post from these notifications
    // is refused by name instead of throwing out of this destructor.
    begin_dispatcher_teardown();
    while (!accelerators_.empty()) {
        (*accelerators_.back()).disconnect();
    }
    while (!overlays_.empty()) {
        (*overlays_.back()).disconnect();
    }
    focus_scopes_.clear();
    // The clock is cancelled by name before its request goes with the rest.
    motion_clock_.cancel();
    for (const std::shared_ptr<gui_forms::detail::ScheduledFrameRequest>& request : frame_requests_) {
        (*request).disconnect();
    }
    frame_requests_.clear();
    if (root_) {
        detach_subtree(root_);
    }
    shutdown_dispatcher();
}

// Here, where every type the root owns is complete. The owners call
// release_tree first; a root whose construction failed part way did not, so
// whatever is still attached is forgotten before the members it would reach
// from ~Control are destroyed.
SurfaceRoot::~SurfaceRoot() {
    for (const std::shared_ptr<gui_forms::detail::OverlayAttachment>& overlay : overlays_) {
        const Control::Ptr overlay_root = (*overlay).overlay();
        if (overlay_root && (*overlay_root).surface_root_ == this) forget_destroyed_subtree(*overlay_root);
    }
    if (root_ && (*root_).surface_root_ == this) forget_destroyed_subtree(*root_);
}

void SurfaceRoot::shutdown_dispatcher() noexcept {
    // Input deferred during a paint would be drained by posted work the
    // close cancels: abandon it first, on the window's own thread only.
    if (check_access()) {
        abandon_deferred_input();
    }
    close_dispatcher();
}

void Window::resize(Size client_size) {
    require_root_thread("window resize");
    if (!resize_surface(client_size)) return;
    invalidate_cursor_interaction(CursorError::stale_metrics);
}

bool SurfaceRoot::resize_surface(Size client_size) {
    if (client_size.width < 0.0 || client_size.height < 0.0) {
        throw std::invalid_argument("GUI.Forms client size may not be negative");
    }
    if (client_size_ == client_size) {
        return false;
    }
    // Pending touches place their damage in the client area they were made
    // in.
    settle_dirty();
    const Rect old_bounds{0.0, 0.0, client_size_.width, client_size_.height};
    const Rect new_bounds{0.0, 0.0, client_size.width, client_size.height};
    const bool width_changed = client_size_.width != client_size.width;
    const bool height_changed = client_size_.height != client_size.height;
    client_size_ = client_size;
    ++surface_epoch_;
    if (surface_epoch_ == 0U) ++surface_epoch_;
    apply_client_resize(old_bounds, new_bounds, width_changed, height_changed);
    for (const std::shared_ptr<gui_forms::detail::OverlayAttachment>& overlay : overlays_) {
        if (const Control::Ptr overlay_root = (*overlay).overlay()) {
            mark_subtree_dirty(*overlay_root, invalidation::bounds);
        }
    }
    return true;
}

// The surface side of a client resize (Window::resize is its host entry):
// damage and layout marks for a root that went from `old_bounds` to
// `new_bounds`, both at the window origin.
void SurfaceRoot::apply_client_resize(Rect old_bounds, Rect new_bounds, bool width_changed,
                                      bool height_changed) {
    // What the resize touched, and whether it needs repainting (the owner,
    // 2026-11-03). The window's base is drawn in window coordinates: one
    // whose only size-dependent parts are its border and corners keeps its
    // pixels, and only the exposed strips and the bands at the moving edges
    // are damaged (a corner touches both axes); any other is damaged whole.
    // The root is arranged again; a container whose children respond to
    // the changed axis gives them new slots, and only those are arranged
    // again (set_child_layout), damaging what their own bounds changed
    // (arrange_dirty_recursive). Controls
    // whose bounds stay keep their chunks and pixels. Only the controls
    // whose measure input the client size reaches are measured again; the
    // others are handed the same available size as before, so their
    // measure would answer as it did (stage L, L7).
    ControlVisualContext base_context;
    base_context.surface = active_ ? ControlSurfaceState::normal : ControlSurfaceState::deactivated;
    base_context.high_contrast = presentation_settings_.high_contrast;
    const std::optional<double> base_band = surface_material_resize_band(backplane_base(base_context));
    if (base_band) {
        add_resize_strip_damage(old_bounds, new_bounds, *base_band);
    } else {
        add_damage_all_planes(old_bounds);
        add_damage_all_planes(new_bounds);
    }
    mark_child_layout_slot(*root_);
    mark_resized_measure(*root_, width_changed, height_changed);
}

// The window's measure pass hands the root the client size, and hands each
// child its parent's requested extent on each axis, or, where that is zero,
// what the parent was handed (measure_dirty_recursive). A client-size change
// therefore changes what a control is handed exactly when it reaches the
// control: on a changed axis, through ancestors that request no extent on
// that axis. Those controls, and only those, are marked for measure. The
// reached set contains each reached control's ancestors, so their subtree
// flags already cover it.
void SurfaceRoot::mark_resized_measure(Control& control, bool width_reaches, bool height_reaches) {
    // add_dirty advances the measure generation, so a reached control's
    // cached measure answers (measure_for_layout) are not answered again.
    control.add_dirty(Dirty::measure);
    control.subtree_dirty_ |= Dirty::measure;
    const bool child_width_reaches = width_reaches && control.requested_bounds_.width <= 0.0;
    const bool child_height_reaches = height_reaches && control.requested_bounds_.height <= 0.0;
    if (!child_width_reaches && !child_height_reaches) return;
    for (const Control::Ptr& child : control.children_) {
        if (!child) continue;
        mark_resized_measure(*child, child_width_reaches, child_height_reaches);
    }
}

void Window::set_scale(double scale) {
    require_root_thread("scale mutation");
    if (!set_surface_scale(scale)) return;
    invalidate_cursor_interaction(CursorError::stale_metrics);
}

bool SurfaceRoot::set_surface_scale(double scale) {
    if (!std::isfinite(scale) || scale <= 0.0) {
        throw std::invalid_argument("GUI.Forms scale must be finite and positive");
    }
    if (scale_ == scale) {
        return false;
    }
    scale_ = scale;
    // A resolution media query may give the window another backdrop.
    lower_theme_backdrop_chain();
    ++surface_epoch_;
    if (surface_epoch_ == 0U) ++surface_epoch_;
    mark_subtree_dirty(*root_, invalidation::conservative_subtree);
    for (const std::shared_ptr<gui_forms::detail::OverlayAttachment>& overlay : overlays_) {
        if (const Control::Ptr overlay_root = (*overlay).overlay()) {
            mark_subtree_dirty(*overlay_root, invalidation::conservative_subtree);
        }
    }
    return true;
}

void SurfaceRoot::set_presentation_settings(PresentationSettings settings) {
    require_root_thread("presentation settings mutation");
    if (!std::isfinite(settings.text_scale) ||
        settings.text_scale < 0.5 || settings.text_scale > 4.0) {
        throw std::invalid_argument(
            "GUI.Forms text scale must be finite and between 0.5 and 4.0");
    }
    if (settings.double_click_milliseconds == 0U ||
        settings.double_click_milliseconds > maximum_double_click_milliseconds) {
        throw std::invalid_argument(
            "GUI.Forms double-click interval must be between 1 and 5000 milliseconds");
    }
    if (settings.double_click_origin != SettingOrigin::default_value &&
        settings.double_click_origin != SettingOrigin::host) {
        throw std::invalid_argument("GUI.Forms double-click origin is not a SettingOrigin");
    }
    if (settings.colour_scheme != ColourScheme::light && settings.colour_scheme != ColourScheme::dark) {
        throw std::invalid_argument("GUI.Forms colour scheme is not a ColourScheme");
    }
    if (presentation_settings_ == settings) {
        return;
    }
    const bool text_scale_changed =
        presentation_settings_.text_scale != settings.text_scale;
    const bool motion_reduced_now =
        settings.reduced_motion && !presentation_settings_.reduced_motion;
    const bool ambient_switched_off =
        !settings.ambient_motion && presentation_settings_.ambient_motion;
    const bool transparency_switched =
        settings.reduced_transparency != presentation_settings_.reduced_transparency;
    const bool scheme_switched = settings.colour_scheme != presentation_settings_.colour_scheme;
    presentation_settings_ = settings;
    // Transient overlays own geometry resolved for the opening text metrics.
    // A text-scale transition dismisses them deterministically; reopening
    // rebuilds rows, hit regions, focus scope, and screen-edge placement from
    // the new logical metrics.
    if (text_scale_changed) {
        while (!overlays_.empty()) {
            (*overlays_.back()).disconnect();
        }
    }
    mark_subtree_dirty(*root_, invalidation::conservative_subtree);
    for (const std::shared_ptr<gui_forms::detail::OverlayAttachment>& overlay : overlays_) {
        if (const Control::Ptr overlay_root = (*overlay).overlay()) {
            mark_subtree_dirty(*overlay_root, invalidation::conservative_subtree);
        }
    }
    // Reduced motion (GIVEN D2-1, design language §9.5): every value jumps
    // to its end in this call, and later moves jump at once.
    if (motion_reduced_now) motion_clock_.finish_all(MotionStepCause::reduced_motion);
    // The application's motion switch (E7): ambient effects stop. Every
    // part repaints above, at its pose; one resuming is repainted too, and
    // its paint starts its effect again.
    if (ambient_switched_off) motion_clock_.stop_ambient(false);
    // Reduced transparency (stage T5, PLAN.md §4.5): glass draws flattened,
    // so every part that sampled the backplane is baked again.
    if (transparency_switched) damage_backplane_readers(nullptr);
    presentation_changed_.emit(presentation_settings_);
    // High contrast and reduced transparency withdraw a backdrop (stage T5).
    refresh_backdrop();
    // The theme for the new scheme: the application's where it has the
    // variant, else GUI.Forms' own (T4 theme variants). A handler above may
    // have changed the scheme again; the scheme in force decides.
    if (scheme_switched && resources_) show_theme(scheme_theme((*resources_).theme_ptr()));
    // The window's media changed: its rules may give another backdrop, or
    // another colour for a role in tint(). Where a theme shown above has
    // lowered it already, this changes nothing.
    lower_theme_backdrop_chain();
}

const Theme& SurfaceRoot::application_theme() const noexcept {
    if (!resources_) return *default_theme();
    const Theme& chosen = (*resources_).theme();
    return chosen;
}

std::shared_ptr<const Theme> SurfaceRoot::scheme_theme(const std::shared_ptr<const Theme>& application) {
    const ColourScheme scheme = presentation_settings_.colour_scheme;
    if ((*application).supports(scheme)) {
        theme_fallback_.reset();
        return application;
    }
    std::shared_ptr<const Theme> fallback = gui_forms_theme();
    theme_fallback_ = ThemeFallback{.theme_id = std::string((*application).id()),
                                    .missing = scheme,
                                    .fallback_id = std::string((*fallback).id())};
    (*resources_).note_theme_variant_missing(*application, scheme);
    return fallback;
}

void SurfaceRoot::follow_host_colour_scheme(bool automatic) {
    if (!automatic || presentation_settings_.colour_scheme == host_colour_scheme_) return;
    PresentationSettings settings = presentation_settings_;
    settings.colour_scheme = host_colour_scheme_;
    set_presentation_settings(settings);
}

const CompiledThemeRules* SurfaceRoot::user_theme_rules() const noexcept {
    const CompiledThemeRules* const rules = (*resources_).user_theme_rules();
    return rules;
}

void SurfaceRoot::set_theme(std::shared_ptr<const Theme> theme) {
    require_root_thread("theme mutation");
    if (!theme) throw std::invalid_argument("window theme may not be null");
    // Every window sharing the resources takes the theme, this one included.
    (*resources_).set_theme(std::move(theme));
}

void SurfaceRoot::apply_theme(const std::shared_ptr<const Theme>& theme) {
    show_theme(scheme_theme(theme));
}

void SurfaceRoot::show_theme(const std::shared_ptr<const Theme>& shown) {
    if (theme_ == shown) return;
    theme_ = shown;
    // Another theme draws parts differently: no cached part raster is
    // current.
    engine_parts_.invalidate_rasters();
    // The window's backplane material may differ (E9-B).
    damage_backplane_readers(nullptr);
    // The new theme's window backdrop chain first: the request below is
    // implied by it.
    lower_theme_backdrop_chain();
    // A window following its theme's backdrop request takes the new
    // theme's, and its grant is resolved again before theme_changed is
    // raised, so handlers see the grant the theme brings.
    if (!application_backdrop_request_) {
        backdrop_request_ = implied_backdrop_request();
        refresh_backdrop();
    }
    // Themes include structural spacing/geometry/type tokens as well as paint
    // recipes, so replacement must remeasure and re-hit-test inherited content.
    mark_subtree_dirty(*root_, invalidation::conservative_subtree);
    for (const std::shared_ptr<gui_forms::detail::OverlayAttachment>& overlay : overlays_) {
        if (const Control::Ptr overlay_root = (*overlay).overlay()) {
            mark_subtree_dirty(*overlay_root,
                               Dirty::style | Dirty::paint | Dirty::semantics);
        }
    }
    // Held here: a handler that sets another theme replaces theme_, which
    // can release this theme while later handlers of this emission still
    // read it. They see this theme, after the handlers of the nested
    // emission have seen the newer one.
    const std::shared_ptr<const Theme> emitted = theme_;
    theme_changed_.emit(*emitted);
}

void SurfaceRoot::set_active(bool active) {
    require_root_thread("activation mutation");
    if (active_ == active) return;
    if (!active && host_services_ != nullptr) { (*host_services_).revoke_cursor_interaction(); }
    active_ = active;
    // The window's backplane material follows activation (E9-B).
    damage_backplane_readers(nullptr);
    mark_subtree_dirty(*root_, Dirty::style | Dirty::paint | Dirty::semantics);
    for (const std::shared_ptr<gui_forms::detail::OverlayAttachment>& overlay : overlays_) {
        if (const Control::Ptr overlay_root = (*overlay).overlay()) {
            mark_subtree_dirty(*overlay_root,
                               Dirty::style | Dirty::paint | Dirty::semantics);
        }
    }
    active_changed_.emit(active_);
}

// Nothing a control shows depends on the key map yet (no control displays a
// command's keys), so changing it invalidates nothing.
void SurfaceRoot::set_key_bindings(std::shared_ptr<KeyBindings> bindings) {
    require_root_thread("key binding mutation");
    if (!bindings) throw std::invalid_argument("GUI.Forms window key bindings may not be null");
    key_bindings_ = std::move(bindings);
}

void SurfaceRoot::rebind_key_command(KeyCommand command, KeyBinding binding) {
    require_root_thread("key binding mutation");
    (*key_bindings_).rebind(command, binding);
}

void SurfaceRoot::restore_key_command(KeyCommand command) {
    require_root_thread("key binding mutation");
    (*key_bindings_).restore(command);
}

void Window::install_host_key_map(const KeyMap& defaults) {
    require_root_thread("host key map installation");
    static_cast<void>((*key_bindings_).seed(defaults));
}

void SurfaceRoot::set_text_scale(double text_scale) {
    PresentationSettings settings = presentation_settings_;
    settings.text_scale = text_scale;
    set_presentation_settings(settings);
}

UpdateScope SurfaceRoot::begin_update() {
    require_root_thread("update scope");
    ++update_depth_;
    metrics_.enter_update_scope();
    return UpdateScope(*this);
}

void SurfaceRoot::perform_layout() {
    require_root_thread("layout");
    ensure_layout(false);
}

void SurfaceRoot::flush() {
    require_root_thread("flush");
    ensure_layout(false);
}
void SurfaceRoot::leave_update_scope() {
    require_root_thread("update scope close");
    if (update_depth_ == 0) {
        throw std::logic_error("GUI.Forms update scope underflow");
    }
    --update_depth_;
    metrics_.leave_update_scope();
    flush_if_outermost();
}

void SurfaceRoot::flush_if_outermost() {
    if (update_depth_ == 0) {
        ensure_layout(false);
    }
}
} // namespace gui_forms
