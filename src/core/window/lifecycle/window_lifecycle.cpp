#include "gui_forms/window.hpp"
#include "gui_forms/application.hpp"
#include "gui_forms/theme/gui_forms_theme/gui_forms_theme.hpp"
#include "gui_forms/host/services/host_services.hpp"
#include "gui_forms/surface_material/painting/surface_material_painting.hpp"
#include "../../dispatcher/state/dispatcher_state.hpp"
#include "../../scheduler/request/scheduled_frame_request.hpp"
#include "../accelerator/accelerator_attachment.hpp"
#include "../overlay/overlay_attachment.hpp"
#include "../alerts/window_alerts.hpp"
#include "../interaction/interaction_state.hpp"
#include "../tool_tips/tool_tip_presenter.hpp"
#include "../toasts/toast_presenter.hpp"

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
namespace {

bool known_setting_origin(SettingOrigin origin) noexcept {
    return origin == SettingOrigin::default_value || origin == SettingOrigin::host;
}

bool valid_pointer_distance(double distance) noexcept {
    return std::isfinite(distance) && distance >= 0.0 && distance <= maximum_pointer_distance;
}

// The current application, when a window made on this thread may join it.
Application* joinable_application() noexcept {
    Application* const current = Application::current();
    if (current == nullptr) return nullptr;
    const bool here = (*current).resources() && (*(*current).resources()).check_access();
    return here ? current : nullptr;
}

// The resources of a window made without its own: the application's, or
// fresh ones with the built-in theme.
std::shared_ptr<ApplicationResources> default_window_resources(Application* application) {
    if (application != nullptr) return (*application).resources();
    std::shared_ptr<ApplicationResources> own = std::make_shared<ApplicationResources>(ImageRegistryLimits{});
    return own;
}

// Fresh resources with `limits`, taking the application's theme.
std::shared_ptr<ApplicationResources> limited_window_resources(Application* application,
                                                               ImageRegistryLimits limits) {
    std::shared_ptr<ApplicationResources> own = std::make_shared<ApplicationResources>(limits);
    if (application != nullptr) (*own).set_theme((*(*application).resources()).program_theme_ptr());
    return own;
}

// The application an explicit window joins: refused off its thread or
// once closed.
Application& checked_application(Application& application) {
    if (application.closed()) throw std::logic_error("GUI.Forms window: the application is closed");
    if (!(*application.resources()).check_access()) {
        throw std::logic_error("GUI.Forms window: the application belongs to another thread");
    }
    return application;
}

} // namespace

Window::Window(Control::Ptr root, Size client_size)
    : Window(std::move(root), client_size, default_window_resources(joinable_application()),
             joinable_application()) {}

Window::Window(Application& application, Control::Ptr root, Size client_size)
    : Window(std::move(root), client_size, checked_application(application).resources(), &application) {}

Window::Window(Control::Ptr root, Size client_size, ImageRegistryLimits image_limits)
    : Window(std::move(root), client_size, limited_window_resources(joinable_application(), image_limits),
             joinable_application()) {}

Window::Window(Control::Ptr root, Size client_size,
               std::shared_ptr<ApplicationResources> resources)
    : Window(std::move(root), client_size, std::move(resources), joinable_application()) {}

Window::Window(Control::Ptr root, Size client_size, std::shared_ptr<ApplicationResources> resources,
               Application* application)
    : SurfaceRoot(std::move(root), client_size, std::move(resources)) {
    presenting_window_ = this;
    if (application != nullptr) application_tool_tips_shown_ = (*application).tool_tips_shown_;
    // The application's key table, read until a window-local change (K.1).
    if (application != nullptr) inherited_key_bindings_ = (*application).key_bindings();
    tool_tips_ = std::make_unique<detail::ToolTipPresenter>(*this);
    toasts_ = std::make_unique<detail::ToastPresenter>(*this);
    alerts_ = std::make_unique<detail::WindowAlerts>(*this);
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
    // A joined window takes the application's presentation as its own, with
    // its consequences (the scheme's theme among them) once the tree is
    // attached.
    if (application != nullptr && (*application).presentation_settings() != presentation_settings_) {
        presentation_base_ = (*application).presentation_settings();
        apply_presentation(presentation_base_);
    }
    // Last: a constructor that throws never leaves itself registered.
    (*resources_).attach(*this);
    if (application != nullptr) (*application).join(*this);
}
Window::~Window() {
    // The tooltip presenter goes first: it holds an overlay and a wake, and
    // raises nothing while it shuts down.
    if (tool_tips_) (*tool_tips_).shutdown();
    // The toast presenter likewise holds an overlay and a wake.
    if (toasts_) (*toasts_).shutdown();
    // Waiting alerts go unshown; one showing returns to nothing.
    if (alerts_) (*alerts_).shutdown();
    invalidate_cursor_interaction(CursorError::closing);
    release_tree();
    if (application_ != nullptr) (*application_).leave(*this);
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
    if (toasts_) (*toasts_).client_resized();
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
    // What the root is handed changes with its slot, which the insets keep
    // apart from the client area's edges.
    const Rect old_slot = root_slot();
    client_size_ = client_size;
    const Rect new_slot = root_slot();
    const bool width_changed = old_slot.width != new_slot.width;
    const bool height_changed = old_slot.height != new_slot.height;
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
// damage for a client area that went from `old_bounds` to `new_bounds`,
// both at the window origin, and layout marks for a root slot whose width
// or height changed.
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
        // The outer band moves with the edges too, its rounded corners
        // reaching in by their radius.
        double reach = std::max(*base_band, band_.reserved);
        if (band_.reserved > 0.0 && band_.corners.shape == WindowCornerShape::rounded) {
            reach = std::max(reach, band_.corners.radius);
        }
        add_resize_strip_damage(old_bounds, new_bounds, reach);
    } else {
        add_damage_all_planes(old_bounds);
        add_damage_all_planes(new_bounds);
    }
    mark_child_layout_slot(*root_);
    mark_resized_measure(*root_, width_changed, height_changed);
}

Rect SurfaceRoot::root_slot() const noexcept {
    // The outer band lies along the client area's edges; the chrome insets
    // are measured inside it.
    const Insets& insets = chrome_insets_;
    const double band = band_.reserved;
    const double width = client_size_.width - insets.left - insets.right - 2.0 * band;
    const double height = client_size_.height - insets.top - insets.bottom - 2.0 * band;
    const Rect slot{band + insets.left, band + insets.top, width > 0.0 ? width : 0.0, height > 0.0 ? height : 0.0};
    return slot;
}

void SurfaceRoot::set_chrome_insets(Insets insets) {
    require_root_thread("chrome insets mutation");
    const bool finite = std::isfinite(insets.left) && std::isfinite(insets.top) && std::isfinite(insets.right) &&
                        std::isfinite(insets.bottom);
    const bool not_negative = insets.left >= 0.0 && insets.top >= 0.0 && insets.right >= 0.0 && insets.bottom >= 0.0;
    if (!finite || !not_negative) {
        throw std::invalid_argument("GUI.Forms chrome insets must be finite and not negative");
    }
    if (chrome_insets_ == insets) return;
    // Pending touches place their damage where the controls were.
    settle_dirty();
    const Rect old_slot = root_slot();
    chrome_insets_ = insets;
    const Rect new_slot = root_slot();
    add_damage_all_planes({0.0, 0.0, client_size_.width, client_size_.height});
    mark_child_layout_slot(*root_);
    mark_resized_measure(*root_, old_slot.width != new_slot.width, old_slot.height != new_slot.height);
}

// The window's measure pass hands the root its slot's size, and hands each
// child its parent's requested extent on each axis, or, where that is zero,
// what the parent was handed (measure_dirty_recursive). A slot-size change
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
    // The band is snapped to whole device pixels of the new scale.
    refresh_band();
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

bool valid_pointer_settings(const PointerSettings& settings) noexcept {
    const bool drag = valid_pointer_distance(settings.drag_width) && valid_pointer_distance(settings.drag_height);
    const bool hover = settings.hover_milliseconds != 0U &&
                       settings.hover_milliseconds <= maximum_hover_milliseconds &&
                       valid_pointer_distance(settings.hover_width) &&
                       valid_pointer_distance(settings.hover_height);
    const bool origins = known_setting_origin(settings.drag_origin) && known_setting_origin(settings.hover_origin);
    return drag && hover && origins;
}

void validate_presentation_settings(const PresentationSettings& settings) {
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
    if (!valid_pointer_settings(settings.pointer)) {
        throw std::invalid_argument(
            "GUI.Forms pointer settings are outside their ranges or enums");
    }
    if (settings.colour_scheme != ColourScheme::light && settings.colour_scheme != ColourScheme::dark) {
        throw std::invalid_argument("GUI.Forms colour scheme is not a ColourScheme");
    }
}

PresentationSettings overridden_presentation(PresentationSettings settings,
                                             const PresentationOverrides& overrides) noexcept {
    if (overrides.text_scale) settings.text_scale = *overrides.text_scale;
    if (overrides.high_contrast) settings.high_contrast = *overrides.high_contrast;
    if (overrides.reduced_motion) settings.reduced_motion = *overrides.reduced_motion;
    if (overrides.sound_enabled) settings.sound_enabled = *overrides.sound_enabled;
    if (overrides.ambient_motion) settings.ambient_motion = *overrides.ambient_motion;
    if (overrides.reduced_transparency) settings.reduced_transparency = *overrides.reduced_transparency;
    if (overrides.colour_scheme) settings.colour_scheme = *overrides.colour_scheme;
    return settings;
}

WindowOverrides& SurfaceRoot::edit_overrides() {
    if (!overrides_) overrides_ = std::make_unique<WindowOverrides>();
    return *overrides_;
}

void SurfaceRoot::release_empty_overrides() noexcept {
    if (overrides_ && (*overrides_).empty()) overrides_.reset();
}

const PresentationOverrides* SurfaceRoot::presentation_overrides() const noexcept {
    if (!overrides_ || (*overrides_).presentation.empty()) return nullptr;
    return &(*overrides_).presentation;
}

void SurfaceRoot::set_presentation_settings(PresentationSettings settings) {
    require_root_thread("presentation settings mutation");
    validate_presentation_settings(settings);
    PresentationSettings effective = settings;
    if (overrides_) effective = overridden_presentation(settings, (*overrides_).presentation);
    validate_presentation_settings(effective);
    presentation_base_ = settings;
    apply_presentation(effective);
}

void SurfaceRoot::set_presentation_overrides(const PresentationOverrides& overrides) {
    require_root_thread("presentation overrides");
    const PresentationSettings effective = overridden_presentation(presentation_base_, overrides);
    validate_presentation_settings(effective);
    if (overrides.empty()) {
        if (overrides_) (*overrides_).presentation = PresentationOverrides{};
        release_empty_overrides();
    } else {
        edit_overrides().presentation = overrides;
    }
    apply_presentation(effective);
}

void SurfaceRoot::set_host_presentation(PresentationSettings settings) {
    Application* const application = presenting_window_ == this ? (*presenting_window_).application_ : nullptr;
    if (application != nullptr) {
        (*application).set_presentation_settings(settings);
        return;
    }
    set_presentation_settings(settings);
}

void SurfaceRoot::apply_presentation(PresentationSettings settings) {
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
    if (scheme_switched && resources_) show_theme(scheme_theme(followed_theme()));
    // The window's media changed: its rules may give another backdrop, or
    // another colour for a role in tint(). Where a theme shown above has
    // lowered it already, this changes nothing.
    lower_theme_backdrop_chain();
    // High contrast, the scheme and the motion settings change the band.
    refresh_band();
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
    if (!automatic || presentation_base_.colour_scheme == host_colour_scheme_) return;
    PresentationSettings settings = presentation_base_;
    settings.colour_scheme = host_colour_scheme_;
    set_host_presentation(settings);
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
    // A window showing its own theme keeps it.
    if (overrides_ && (*overrides_).theme) return;
    show_theme(scheme_theme(theme));
}

std::shared_ptr<const Theme> SurfaceRoot::followed_theme() const {
    if (overrides_ && (*overrides_).theme) return (*overrides_).theme;
    const std::shared_ptr<const Theme> shared = (*resources_).theme_ptr();
    return shared;
}

void SurfaceRoot::set_theme_override(std::shared_ptr<const Theme> theme) {
    require_root_thread("theme override");
    if (theme) {
        edit_overrides().theme = std::move(theme);
    } else if (overrides_) {
        (*overrides_).theme = nullptr;
        release_empty_overrides();
    }
    if (!resources_) return;
    show_theme(scheme_theme(followed_theme()));
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
    // The new theme's `window` band rule.
    refresh_band();
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
    // Keys and buttons released while the window was inactive never reach
    // it: forget them.
    if (!active && interaction_) (*interaction_).deactivated();
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
    // Reactivated (a modal window over it closed, or the platform gave it
    // back the keyboard): its last focused control takes focus again.
    if (active && active_ && root_) static_cast<void>(restore_focus());
}

// Nothing a control shows depends on the key map yet (no control displays a
// command's keys), so changing it invalidates nothing.
namespace {

// The PC convention, read by a window holding no table and joined to no
// application. Made once; never changed.
const KeyBindings& pc_key_bindings() {
    static const KeyBindings convention{};
    return convention;
}

// The null table key_bindings() names for a window reading the convention.
const std::shared_ptr<KeyBindings>& no_key_bindings() {
    static const std::shared_ptr<KeyBindings> none{};
    return none;
}

} // namespace

const KeyMap& SurfaceRoot::key_map() const noexcept {
    if (overrides_ && (*overrides_).key_bindings) return (*(*overrides_).key_bindings).map();
    if (inherited_key_bindings_) return (*inherited_key_bindings_).map();
    return pc_key_bindings().map();
}

const std::shared_ptr<KeyBindings>& SurfaceRoot::key_bindings() const noexcept {
    if (overrides_ && (*overrides_).key_bindings) return (*overrides_).key_bindings;
    if (inherited_key_bindings_) return inherited_key_bindings_;
    return no_key_bindings();
}

KeyBindings& SurfaceRoot::held_key_bindings() {
    if (overrides_ && (*overrides_).key_bindings) return *(*overrides_).key_bindings;
    if (inherited_key_bindings_) return *inherited_key_bindings_;
    WindowOverrides& overrides = edit_overrides();
    overrides.key_bindings = std::make_shared<KeyBindings>();
    return *overrides.key_bindings;
}

std::shared_ptr<KeyBindings> SurfaceRoot::share_key_bindings() {
    require_root_thread("key binding sharing");
    static_cast<void>(held_key_bindings());
    std::shared_ptr<KeyBindings> shared = key_bindings();
    return shared;
}

void SurfaceRoot::set_key_bindings(std::shared_ptr<KeyBindings> bindings) {
    require_root_thread("key binding mutation");
    if (!bindings) throw std::invalid_argument("GUI.Forms window key bindings may not be null");
    // Sharing the application's table, which the window reads already,
    // stores nothing.
    const bool held = overrides_ && (*overrides_).key_bindings;
    if (!held && bindings == inherited_key_bindings_) return;
    edit_overrides().key_bindings = std::move(bindings);
}

void SurfaceRoot::rebind_key_command(KeyCommand command, KeyBinding binding) {
    require_root_thread("key binding mutation");
    const bool held = overrides_ && (*overrides_).key_bindings;
    if (held) {
        (*(*overrides_).key_bindings).rebind(command, binding);
        return;
    }
    // A window-local rebinding: the table it read, copied, then changed;
    // refused, the copy is dropped and nothing is stored.
    const KeyBindings& read = inherited_key_bindings_ ? *inherited_key_bindings_ : pc_key_bindings();
    std::shared_ptr<KeyBindings> local = read.copy_bindings();
    (*local).rebind(command, binding);
    edit_overrides().key_bindings = std::move(local);
}

void SurfaceRoot::restore_key_command(KeyCommand command) {
    require_root_thread("key binding mutation");
    const bool held = overrides_ && (*overrides_).key_bindings;
    if (held) {
        (*(*overrides_).key_bindings).restore(command);
        return;
    }
    const KeyBindings& read = inherited_key_bindings_ ? *inherited_key_bindings_ : pc_key_bindings();
    std::shared_ptr<KeyBindings> local = read.copy_bindings();
    (*local).restore(command);
    edit_overrides().key_bindings = std::move(local);
}

void SurfaceRoot::clear_key_bindings() {
    require_root_thread("key binding mutation");
    if (!overrides_) return;
    (*overrides_).key_bindings = nullptr;
    release_empty_overrides();
}

void Window::install_host_key_map(const KeyMap& defaults) {
    require_root_thread("host key map installation");
    // Seeds the table the window holds or reads (its application's, shared
    // by the application's windows), made now for a window with neither.
    static_cast<void>(held_key_bindings().seed(defaults));
}

void SurfaceRoot::set_text_scale(double text_scale) {
    PresentationSettings settings = presentation_base_;
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
