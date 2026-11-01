#include "gui_forms/window.hpp"
#include "gui_forms/host/services/host_services.hpp"
#include "../../dispatcher/state/dispatcher_state.hpp"
#include "../../scheduler/request/scheduled_frame_request.hpp"
#include "../accelerator/accelerator_attachment.hpp"
#include "../popup/popup_attachment.hpp"
#include "../tool_tips/tool_tip_presenter.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
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
    : root_(std::move(root)), client_size_(client_size),
      theme_(resources ? (*resources).theme_ptr() : default_theme()),
      resources_(std::move(resources)),
      lifetime_(std::make_shared<detail::WindowLifetime>()),
      ui_thread_(std::this_thread::get_id()),
      dispatcher_state_(std::make_shared<detail::DispatcherState>(ui_thread_)) {
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
    const Rect initial_damage{0.0, 0.0, client_size_.width, client_size_.height};
    add_damage_all_planes(initial_damage);
    metrics_.record_dirty_mark(initial_damage.area());
    // Last: a constructor that throws never leaves itself registered.
    (*resources_).attach(*this);
}
Window::~Window() {
    // The tooltip presenter goes first: it holds a popup and a wake, and
    // raises nothing while it shuts down.
    if (tool_tip_presenter_) (*tool_tip_presenter_).shutdown();
    invalidate_cursor_interaction(CursorError::closing);
    paint_wake_handler_ = {};
    paint_wake_pending_ = false;
    abandon_deferred_input();
    // Teardown order (house style §4): the controls are told everything
    // (popups closing, capture and focus revoked, the tree detached) while
    // the dispatcher still accepts work, and the dispatcher is shut down
    // only after them, which cancels whatever they posted. If a host shut it
    // down already (it closed the window), a post from these notifications
    // is refused by name instead of throwing out of this destructor.
    begin_dispatcher_teardown();
    while (!accelerators_.empty()) {
        (*accelerators_.back()).disconnect();
    }
    while (!popups_.empty()) {
        (*popups_.back()).disconnect();
    }
    focus_scopes_.clear();
    // The clock is cancelled by name before its request goes with the rest.
    motion_clock_.cancel();
    for (const std::shared_ptr<gui_forms::detail::ScheduledFrameRequest>& request : frame_requests_) {
        (*request).disconnect();
    }
    frame_requests_.clear();
    update_frame_schedule_metrics();
    if (root_) {
        detach_subtree(root_);
    }
    shutdown_dispatcher();
    (*resources_).detach(*this);
    (*lifetime_).window = nullptr;
}
void Window::resize(Size client_size) {
    require_ui_thread("window resize");
    if (client_size.width < 0.0 || client_size.height < 0.0) {
        throw std::invalid_argument("GUI.Forms client size may not be negative");
    }
    if (client_size_ == client_size) {
        return;
    }
    const Rect old_bounds{0.0, 0.0, client_size_.width, client_size_.height};
    const bool width_changed = client_size_.width != client_size.width;
    const bool height_changed = client_size_.height != client_size.height;
    client_size_ = client_size;
    invalidate_cursor_interaction(CursorError::stale_metrics);
    ++surface_epoch_;
    if (surface_epoch_ == 0U) ++surface_epoch_;
    add_damage_all_planes(old_bounds);
    // Every control is arranged, painted and hit-tested again, as before.
    // Only the controls whose measure input the client size reaches are
    // measured again; the others are handed the same available size as
    // before, so their measure would answer as it did (stage L, L7).
    mark_subtree_dirty(*root_, without_dirty(invalidation::bounds, Dirty::measure));
    mark_resized_measure(*root_, width_changed, height_changed);
    for (const std::shared_ptr<gui_forms::detail::PopupAttachment>& popup : popups_) {
        if (const Control::Ptr overlay = (*popup).popup()) {
            mark_subtree_dirty(*overlay, invalidation::bounds);
        }
    }
}

// The window's measure pass hands the root the client size, and hands each
// child its parent's requested extent on each axis, or, where that is zero,
// what the parent was handed (measure_dirty_recursive). A client-size change
// therefore changes what a control is handed exactly when it reaches the
// control: on a changed axis, through ancestors that request no extent on
// that axis. Those controls, and only those, are marked for measure. The
// reached set contains each reached control's ancestors, so their subtree
// flags already cover it.
void Window::mark_resized_measure(Control& control, bool width_reaches, bool height_reaches) {
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
    require_ui_thread("scale mutation");
    if (!std::isfinite(scale) || scale <= 0.0) {
        throw std::invalid_argument("GUI.Forms scale must be finite and positive");
    }
    if (scale_ == scale) {
        return;
    }
    scale_ = scale;
    invalidate_cursor_interaction(CursorError::stale_metrics);
    ++surface_epoch_;
    if (surface_epoch_ == 0U) ++surface_epoch_;
    mark_subtree_dirty(*root_, invalidation::conservative_subtree);
    for (const std::shared_ptr<gui_forms::detail::PopupAttachment>& popup : popups_) {
        if (const Control::Ptr overlay = (*popup).popup()) {
            mark_subtree_dirty(*overlay, invalidation::conservative_subtree);
        }
    }
}

void Window::set_presentation_settings(PresentationSettings settings) {
    require_ui_thread("presentation settings mutation");
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
    presentation_settings_ = settings;
    // Transient overlays own geometry resolved for the opening text metrics.
    // A text-scale transition dismisses them deterministically; reopening
    // rebuilds rows, hit regions, focus scope, and screen-edge placement from
    // the new logical metrics.
    if (text_scale_changed) {
        while (!popups_.empty()) {
            (*popups_.back()).disconnect();
        }
    }
    mark_subtree_dirty(*root_, invalidation::conservative_subtree);
    for (const std::shared_ptr<gui_forms::detail::PopupAttachment>& popup : popups_) {
        if (const Control::Ptr overlay = (*popup).popup()) {
            mark_subtree_dirty(*overlay, invalidation::conservative_subtree);
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
}

void Window::set_theme(std::shared_ptr<const Theme> theme) {
    require_ui_thread("theme mutation");
    if (!theme) throw std::invalid_argument("window theme may not be null");
    // Every window sharing the resources takes the theme, this one included.
    (*resources_).set_theme(std::move(theme));
}

void Window::apply_theme(const std::shared_ptr<const Theme>& theme) {
    if (theme_ == theme) return;
    theme_ = theme;
    // Another theme draws parts differently: no cached part raster is
    // current.
    engine_parts_.invalidate_rasters();
    // The window's backplane material may differ (E9-B).
    damage_backplane_readers(nullptr);
    // A window following its theme's backdrop request takes the new
    // theme's, and its grant is resolved again before theme_changed is
    // raised, so handlers see the grant the theme brings.
    if (!application_backdrop_request_) {
        backdrop_request_ = (*theme_).definition().window_backdrop.request;
        refresh_backdrop();
    }
    // Themes include structural spacing/geometry/type tokens as well as paint
    // recipes, so replacement must remeasure and re-hit-test inherited content.
    mark_subtree_dirty(*root_, invalidation::conservative_subtree);
    for (const std::shared_ptr<gui_forms::detail::PopupAttachment>& popup : popups_) {
        if (const Control::Ptr overlay = (*popup).popup()) {
            mark_subtree_dirty(*overlay,
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

void Window::set_active(bool active) {
    require_ui_thread("activation mutation");
    if (active_ == active) return;
    if (!active && host_services_ != nullptr) { (*host_services_).revoke_cursor_interaction(); }
    active_ = active;
    // The window's backplane material follows activation (E9-B).
    damage_backplane_readers(nullptr);
    mark_subtree_dirty(*root_, Dirty::style | Dirty::paint | Dirty::semantics);
    for (const std::shared_ptr<gui_forms::detail::PopupAttachment>& popup : popups_) {
        if (const Control::Ptr overlay = (*popup).popup()) {
            mark_subtree_dirty(*overlay,
                               Dirty::style | Dirty::paint | Dirty::semantics);
        }
    }
    active_changed_.emit(active_);
}

// Nothing a control shows depends on the key map yet (no control displays a
// command's keys), so changing it invalidates nothing.
void Window::set_key_bindings(std::shared_ptr<KeyBindings> bindings) {
    require_ui_thread("key binding mutation");
    if (!bindings) throw std::invalid_argument("GUI.Forms window key bindings may not be null");
    key_bindings_ = std::move(bindings);
}

void Window::rebind_key_command(KeyCommand command, KeyBinding binding) {
    require_ui_thread("key binding mutation");
    (*key_bindings_).rebind(command, binding);
}

void Window::restore_key_command(KeyCommand command) {
    require_ui_thread("key binding mutation");
    (*key_bindings_).restore(command);
}

void Window::install_host_key_map(const KeyMap& defaults) {
    require_ui_thread("host key map installation");
    static_cast<void>((*key_bindings_).seed(defaults));
}

void Window::set_text_scale(double text_scale) {
    PresentationSettings settings = presentation_settings_;
    settings.text_scale = text_scale;
    set_presentation_settings(settings);
}

UpdateScope Window::begin_update() {
    require_ui_thread("update scope");
    ++update_depth_;
    metrics_.enter_update_scope();
    return UpdateScope(*this);
}

void Window::perform_layout() {
    require_ui_thread("layout");
    ensure_layout(false);
}

void Window::flush() {
    require_ui_thread("flush");
    ensure_layout(false);
}
void Window::leave_update_scope() {
    require_ui_thread("update scope close");
    if (update_depth_ == 0) {
        throw std::logic_error("GUI.Forms update scope underflow");
    }
    --update_depth_;
    metrics_.leave_update_scope();
    flush_if_outermost();
}

void Window::flush_if_outermost() {
    if (update_depth_ == 0) {
        ensure_layout(false);
    }
}
} // namespace gui_forms
