#include "gui_forms/controls/scrollable_control/container_control/split_container/split_container.hpp"
#include "splitter_grip.hpp"
#include "gui_forms/window.hpp"
#include "../../scrollable_anatomy.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include <vector>
#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>

namespace gui_forms {

namespace {

void require_finite_nonnegative(double value, const char* message) {
    if (!std::isfinite(value) || value < 0.0) throw std::invalid_argument(message);
}

void validate_splitter_geometry(const SplitSeamGeometry& geometry) {
    if (!std::isfinite(geometry.visible_thickness) ||
        geometry.visible_thickness <= 0.0 ||
        geometry.visible_thickness >
            SplitSeamGeometry::maximum_visible_thickness) {
        throw std::invalid_argument(
            "split seam visible thickness is outside its bounded range");
    }
    if (!std::isfinite(geometry.hit_before) || geometry.hit_before < 0.0 ||
        geometry.hit_before > SplitSeamGeometry::maximum_hit_extension ||
        !std::isfinite(geometry.hit_after) || geometry.hit_after < 0.0 ||
        geometry.hit_after > SplitSeamGeometry::maximum_hit_extension) {
        throw std::invalid_argument(
            "split seam hit extents are outside their bounded range");
    }
    if (!std::isfinite(geometry.minimum_hit_target) ||
        geometry.minimum_hit_target <= 0.0 ||
        geometry.minimum_hit_target >
            SplitSeamGeometry::maximum_hit_target) {
        throw std::invalid_argument(
            "split seam minimum hit target is outside its bounded range");
    }
    const double guaranteed_hit_extent =
        geometry.hit_before + geometry.hit_after +
        (geometry.thickness_policy == SplitSeamThicknessPolicy::logical
             ? geometry.visible_thickness
             : 0.0);
    if (guaranteed_hit_extent < geometry.minimum_hit_target) {
        throw std::invalid_argument(
            "split seam hit extents do not satisfy its declared minimum target");
    }
}

} // namespace

SplitContainer::SplitContainer(StableId stable_id)
    : ContainerControl(std::move(stable_id)) {
    const std::string prefix((*this).stable_id().value());
    first_panel_ = make_control<SplitterPanel>(StableId(prefix + ".panel1"));
    second_panel_ = make_control<SplitterPanel>(StableId(prefix + ".panel2"));
    splitter_ = make_control<SplitterGrip>(StableId(prefix + ".splitter"));
    update_splitter_cursor();
}

void SplitContainer::initialize_control_tree() {
    require_mutable();
    if (tree_initialized_) return;
    add_child(first_panel_);
    add_child(second_panel_);
    add_child(splitter_);
    tree_initialized_ = true;
}

void SplitContainer::prepare_write(const decltype(orientation)&, Orientation&) {
    require_mutable();
}

// A new orientation measures along the other axis, so the remembered extents
// no longer apply and the cursor turns.
void SplitContainer::written(const decltype(orientation)&) {
    previous_axis_extent_ = 0.0;
    previous_second_extent_ = 0.0;
    update_splitter_cursor();
    invalidate(invalidation::bounds);
}

// The write is the requested distance (set_distance keeps it and applies the
// panel limits); the read is the effective distance.
void SplitContainer::write_view(const decltype(splitter_distance)&, double incoming) {
    require_finite_nonnegative(incoming,
                               "splitter distance must be finite and nonnegative");
    set_distance(incoming, SplitChangeReason::programmatic);
}

double SplitContainer::read_view(const decltype(splitter_distance)&) const {
    return effective_distance_;
}

// The width and hit width are shorthands: each derives a geometry with the
// extra hit extent split evenly and writes splitter_geometry.
void SplitContainer::write_view(const decltype(splitter_width)&, double incoming) {
    require_mutable();
    if (!std::isfinite(incoming) || incoming <= 0.0) {
        throw std::invalid_argument("splitter width must be finite and positive");
    }
    if (splitter_width_ == incoming) return;
    SplitSeamGeometry next = splitter_geometry.read();
    const double target = std::max(splitter_hit_width_, incoming);
    next.visible_thickness = incoming;
    next.minimum_hit_target = target;
    const double extra = next.thickness_policy ==
            SplitSeamThicknessPolicy::logical
        ? std::max(0.0, target - incoming)
        : target;
    next.hit_before = extra * 0.5;
    next.hit_after = extra - next.hit_before;
    splitter_geometry = next;
}

double SplitContainer::read_view(const decltype(splitter_width)&) const {
    return splitter_width_;
}

void SplitContainer::write_view(const decltype(splitter_hit_width)&, double incoming) {
    require_mutable();
    if (!std::isfinite(incoming) || incoming <= 0.0) {
        throw std::invalid_argument(
            "splitter hit width must be finite and positive");
    }
    const SplitSeamGeometry& current = splitter_geometry.read();
    const double target = std::max(incoming, current.visible_thickness);
    if (splitter_hit_width_ == target) return;
    SplitSeamGeometry next = current;
    next.minimum_hit_target = target;
    const double extra = next.thickness_policy ==
            SplitSeamThicknessPolicy::logical
        ? std::max(0.0, target - next.visible_thickness)
        : target;
    next.hit_before = extra * 0.5;
    next.hit_after = extra - next.hit_before;
    splitter_geometry = next;
}

double SplitContainer::read_view(const decltype(splitter_hit_width)&) const {
    return splitter_hit_width_;
}

// The whole geometry is validated before any of it is published.
void SplitContainer::prepare_write(const decltype(splitter_geometry)&,
                                   SplitSeamGeometry& incoming) {
    require_mutable();
    validate_splitter_geometry(incoming);
}

void SplitContainer::written(const decltype(splitter_geometry)&) {
    const SplitSeamGeometry& geometry = splitter_geometry.read();
    splitter_width_ = geometry.visible_thickness;
    splitter_hit_width_ = geometry.minimum_hit_target;
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint |
               Dirty::hit_test | Dirty::semantics | Dirty::accessibility);
}

SplitSeamSnapshot SplitContainer::splitter_seam_snapshot() const noexcept {
    const double scale = window() != nullptr ? (*window()).scale() : 1.0;
    const double visible = effective_splitter_width();
    SplitSeamSnapshot result;
    result.orientation = orientation.read();
    result.geometry = splitter_geometry.read();
    const SplitterGrip& grip = static_cast<const SplitterGrip&>(*splitter_);
    result.state = grip.seam_state();
    result.hit_bounds = (*splitter_).committed_arranged_bounds();
    result.actuator_bounds = grip.actuator_bounds();
    result.device_scale = scale;
    result.visible_device_pixels = visible * scale;
    result.transition_duration_milliseconds =
        std::chrono::duration<double, std::milli>(
            grip.transition_duration()).count();
    result.transition_progress = grip.transition_progress();
    result.transition_active = grip.transition_active();
    if (orientation.read() == Orientation::vertical) {
        result.visible_bounds = {effective_distance_, 0.0, visible,
                                 committed_arranged_bounds().height};
    } else {
        result.visible_bounds = {0.0, effective_distance_,
                                 committed_arranged_bounds().width, visible};
    }
    return result;
}

// The grip holds the transition duration.
FrameInterval SplitContainer::read_view(
    const decltype(splitter_transition_duration)&) const {
    return static_cast<const SplitterGrip&>(*splitter_).transition_duration();
}

void SplitContainer::write_view(const decltype(splitter_transition_duration)&,
                                FrameInterval incoming) {
    require_mutable();
    static_cast<SplitterGrip&>(*splitter_).set_transition_duration(incoming);
}

void SplitContainer::set_first_collapsed(bool collapsed,
                                         SplitCollapseOrigin origin) {
    require_mutable();
    if (first_collapsed_ == collapsed) return;
    if (collapsed && second_collapsed_) {
        throw std::logic_error("both split panels may not be collapsed");
    }
    const double old = effective_distance_;
    const SplitCollapseOrigin previous_origin = first_collapse_origin_;
    if (collapsed) {
        if (effective_distance_ > 0.0) remembered_distance_ = effective_distance_;
        transfer_focus_from(first_panel_);
    } else if (remembered_distance_ >= 0.0) {
        requested_distance_ = remembered_distance_;
    }
    first_collapsed_ = collapsed;
    first_collapse_origin_ = collapsed ? origin : SplitCollapseOrigin::none;
    if (!collapsed && previous_origin == SplitCollapseOrigin::automatic_accommodation &&
        origin == SplitCollapseOrigin::user &&
        axis_extent(committed_arranged_bounds()) <
            automatic_collapse_threshold.read()) {
        automatic_collapse_suppressed_ = true;
    }
    (*first_panel_).visible = !collapsed;
    static_cast<SplitterGrip&>(*splitter_).set_collapse_appearance(
        collapse_panel.read(), collapse_target_is_collapsed());
    const double total = axis_extent(committed_arranged_bounds());
    effective_distance_ = collapsed ? 0.0
                                    : constrained_distance(requested_distance_, total);
    invalidate(invalidation::bounds);
    const SplitChangeEvent change{old, effective_distance_,
                                  SplitChangeReason::collapse, origin};
    publish_change(splitter_changed_, change);
}

void SplitContainer::set_second_collapsed(bool collapsed,
                                          SplitCollapseOrigin origin) {
    require_mutable();
    if (second_collapsed_ == collapsed) return;
    if (collapsed && first_collapsed_) {
        throw std::logic_error("both split panels may not be collapsed");
    }
    const double old = effective_distance_;
    const SplitCollapseOrigin previous_origin = second_collapse_origin_;
    if (collapsed) {
        if (effective_distance_ > 0.0) remembered_distance_ = effective_distance_;
        transfer_focus_from(second_panel_);
    } else if (remembered_distance_ >= 0.0) {
        requested_distance_ = remembered_distance_;
    }
    second_collapsed_ = collapsed;
    second_collapse_origin_ = collapsed ? origin : SplitCollapseOrigin::none;
    if (!collapsed && previous_origin == SplitCollapseOrigin::automatic_accommodation &&
        origin == SplitCollapseOrigin::user &&
        axis_extent(committed_arranged_bounds()) <
            automatic_collapse_threshold.read()) {
        automatic_collapse_suppressed_ = true;
    }
    (*second_panel_).visible = !collapsed;
    static_cast<SplitterGrip&>(*splitter_).set_collapse_appearance(
        collapse_panel.read(), collapse_target_is_collapsed());
    const double total = axis_extent(committed_arranged_bounds());
    effective_distance_ = collapsed
        ? std::max(0.0, total - effective_splitter_width())
        : constrained_distance(requested_distance_, total);
    invalidate(invalidation::bounds);
    const SplitChangeEvent change{old, effective_distance_,
                                  SplitChangeReason::collapse, origin};
    publish_change(splitter_changed_, change);
}

void SplitContainer::prepare_write(const decltype(collapse_panel)&, SplitFixedPanel&) {
    require_mutable();
}

// A new collapse target starts unsuppressed and redraws the grip's tab.
void SplitContainer::written(const decltype(collapse_panel)&) {
    automatic_collapse_suppressed_ = false;
    static_cast<SplitterGrip&>(*splitter_).set_collapse_appearance(
        collapse_panel.read(), collapse_target_is_collapsed());
    invalidate(Dirty::paint | Dirty::hit_test | Dirty::semantics |
               Dirty::accessibility);
}

void SplitContainer::prepare_write(const decltype(automatic_collapse_threshold)&,
                                   double& incoming) {
    require_mutable();
    require_finite_nonnegative(
        incoming, "automatic collapse threshold must be finite and nonnegative");
}

void SplitContainer::written(const decltype(automatic_collapse_threshold)&) {
    automatic_collapse_suppressed_ = false;
    if (automatic_collapse_threshold.read() == 0.0 &&
        collapse_target_origin() == SplitCollapseOrigin::automatic_accommodation) {
        // With no collapse target there is nothing to restore.
        if (collapse_panel.read() == SplitFixedPanel::first) {
            set_first_collapsed(false,
                                SplitCollapseOrigin::automatic_accommodation);
        } else if (collapse_panel.read() == SplitFixedPanel::second) {
            set_second_collapsed(false,
                                 SplitCollapseOrigin::automatic_accommodation);
        }
    }
    invalidate(invalidation::bounds);
}

Size SplitContainer::measure(Size available) {
    const Rect requested = requested_bounds();
    return {std::min(available.width,
                     requested.width > 0.0 ? requested.width : available.width),
            std::min(available.height,
                     requested.height > 0.0 ? requested.height : available.height)};
}

void SplitContainer::arrange(Rect final_bounds) {
    arrange_self(final_bounds);
    const double total = axis_extent(final_bounds);
    const double visible_width = effective_splitter_width();
    reconcile_automatic_collapse(total);
    const double available = std::max(0.0, total - visible_width);
    double desired = requested_distance_ < 0.0 ? available * 0.5
                                               : requested_distance_;
    if (previous_axis_extent_ > 0.0 && total != previous_axis_extent_ &&
        fixed_panel.read() == SplitFixedPanel::second && !second_collapsed_) {
        desired = std::max(0.0, available - previous_second_extent_);
    }
    const double old = effective_distance_;
    effective_distance_ = constrained_distance(desired, total);
    if (requested_distance_ >= 0.0 || fixed_panel.read() == SplitFixedPanel::second) {
        requested_distance_ = effective_distance_;
    }
    const double second_extent = std::max(0.0, available - effective_distance_);
    const double hit_width = std::min(
        total, splitter_geometry.read().hit_before + visible_width +
                   splitter_geometry.read().hit_after);
    const double hit_origin = std::clamp(
        effective_distance_ - splitter_geometry.read().hit_before, 0.0,
        std::max(0.0, total - hit_width));
    static_cast<SplitterGrip&>(*splitter_).set_visible_geometry(
        effective_distance_ - hit_origin, visible_width);
    if (orientation.read() == Orientation::vertical) {
        set_child_layout(first_panel_,
            {0.0, 0.0, effective_distance_, final_bounds.height});
        set_child_layout(second_panel_,
            {effective_distance_ + visible_width, 0.0, second_extent,
             final_bounds.height});
        set_child_layout(splitter_,
            {hit_origin, 0.0, hit_width, final_bounds.height});
    } else {
        set_child_layout(first_panel_,
            {0.0, 0.0, final_bounds.width, effective_distance_});
        set_child_layout(second_panel_,
            {0.0, effective_distance_ + visible_width, final_bounds.width,
             second_extent});
        set_child_layout(splitter_,
            {0.0, hit_origin, final_bounds.width, hit_width});
    }
    previous_axis_extent_ = total;
    previous_second_extent_ = second_extent;
    if (old != effective_distance_) {
        const SplitChangeEvent change{old, effective_distance_,
                                      SplitChangeReason::container_resize};
        publish_change(splitter_changed_, change);
    }
}

void SplitContainer::on_pointer_preview(PointerEvent& event) {
    if ((pointer_tracking_ || collapse_tab_tracking_) &&
        !(*splitter_).has_pointer_capture() &&
        event.action != PointerAction::up) {
        cancel_splitter_interaction(false);
    }
    const bool on_collapse_tab = collapse_panel.read() != SplitFixedPanel::none &&
                                 collapse_tab_bounds().contains(event.position);
    if (event.action == PointerAction::down &&
        event.button == PointerButton::primary && on_collapse_tab) {
        collapse_tab_tracking_ = true;
        pointer_tracking_ = false;
        static_cast<SplitterGrip&>(*splitter_).set_actuator_pressed(true);
        if (window() != nullptr) (*window()).request_focus(splitter_);
        (*splitter_).set_pointer_capture(true);
        event.handled = true;
        return;
    }
    if (collapse_tab_tracking_) {
        if (event.action == PointerAction::up) {
            collapse_tab_tracking_ = false;
            static_cast<SplitterGrip&>(*splitter_).set_actuator_pressed(false);
            (*splitter_).set_pointer_capture(false);
            if (on_collapse_tab) {
                toggle_collapse_target(SplitCollapseOrigin::user);
            }
        }
        event.handled = true;
        return;
    }
    if (splitter_fixed.read() || first_collapsed_ || second_collapsed_) return;
    if (event.action == PointerAction::down &&
        event.button == PointerButton::primary &&
        (*splitter_).absolute_bounds().contains(event.position)) {
        pointer_tracking_ = true;
        pointer_start_distance_ = effective_distance_;
        static_cast<SplitterGrip&>(*splitter_).set_dragging(true);
        pointer_offset_ = pointer_axis(event.position) - effective_distance_;
        if (window() != nullptr) (*window()).request_focus(splitter_);
        (*splitter_).set_pointer_capture(true);
        event.handled = true;
    } else if (event.action == PointerAction::move && pointer_tracking_) {
        set_distance(pointer_axis(event.position) - pointer_offset_,
                     SplitChangeReason::pointer);
        event.handled = true;
    } else if (event.action == PointerAction::up && pointer_tracking_) {
        pointer_tracking_ = false;
        static_cast<SplitterGrip&>(*splitter_).set_dragging(false);
        set_distance(pointer_axis(event.position) - pointer_offset_,
                     SplitChangeReason::pointer);
        event.handled = true;
    }
}

void SplitContainer::on_key_preview(KeyEvent& event) {
    if (event.action != KeyAction::down || window() == nullptr ||
        (*window()).focused_control() != splitter_) return;
    if (event.physical_key == PhysicalKey::escape &&
        (pointer_tracking_ || collapse_tab_tracking_)) {
        cancel_splitter_interaction(pointer_tracking_);
        event.handled = true;
        return;
    }
    if (collapse_panel.read() != SplitFixedPanel::none &&
        (event.physical_key == PhysicalKey::enter ||
         event.physical_key == PhysicalKey::space)) {
        toggle_collapse_target(SplitCollapseOrigin::user);
        event.handled = true;
        return;
    }
    if (splitter_fixed.read() || first_collapsed_ || second_collapsed_) return;
    double delta{};
    if (orientation.read() == Orientation::vertical) {
        if (event.physical_key == PhysicalKey::left) delta = -keyboard_increment.read();
        else if (event.physical_key == PhysicalKey::right) delta = keyboard_increment.read();
        else return;
    } else {
        if (event.physical_key == PhysicalKey::up) delta = -keyboard_increment.read();
        else if (event.physical_key == PhysicalKey::down) delta = keyboard_increment.read();
        else return;
    }
    if ((static_cast<std::uint8_t>(event.modifiers) &
         static_cast<std::uint8_t>(Modifier::shift)) != 0U) {
        delta *= 10.0;
    }
    set_distance(effective_distance_ + delta, SplitChangeReason::keyboard);
    event.handled = true;
}

SemanticDescriptor SplitContainer::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.role = SemanticRole::split_pane;
    descriptor.name = accessible_name.read();
    descriptor.description = accessible_description.read();
    descriptor.value = std::to_string(effective_distance_);
    descriptor.numeric_value = effective_distance_;
    const double total = axis_extent(committed_arranged_bounds());
    descriptor.minimum_value = constrained_distance(0.0, total);
    descriptor.maximum_value = constrained_distance(total, total);
    descriptor.actions = {SemanticAction::focus};
    if (!splitter_fixed.read() && !first_collapsed_ && !second_collapsed_) {
        descriptor.actions.push_back(SemanticAction::decrement);
        descriptor.actions.push_back(SemanticAction::increment);
        descriptor.actions.push_back(SemanticAction::set_value);
    }
    if (collapse_panel.read() != SplitFixedPanel::none) {
        const bool collapsed = collapse_target_is_collapsed();
        descriptor.description = collapsed
            ? "Split pane collapsed; activate to restore its remembered extent"
            : "Split pane expanded; activate to collapse it";
        descriptor.actions.push_back(
            collapsed ? SemanticAction::expand : SemanticAction::collapse);
        if (!collapsed) descriptor.states |= SemanticState::expanded;
    }
    descriptor.exposed = true;
    return descriptor;
}

bool SplitContainer::on_semantic_action(SemanticAction action,
                                        std::string_view value) {
    if (!effectively_enabled()) return false;
    if (action == SemanticAction::focus && window() != nullptr) {
        return (*window()).request_focus(splitter_);
    }
    if (action == SemanticAction::expand &&
        collapse_panel.read() != SplitFixedPanel::none &&
        collapse_target_is_collapsed()) {
        toggle_collapse_target(SplitCollapseOrigin::user);
        return true;
    }
    if (action == SemanticAction::collapse &&
        collapse_panel.read() != SplitFixedPanel::none &&
        !collapse_target_is_collapsed()) {
        toggle_collapse_target(SplitCollapseOrigin::user);
        return true;
    }
    if (!splitter_fixed.read() && !first_collapsed_ && !second_collapsed_) {
        if (action == SemanticAction::increment) {
            set_distance(effective_distance_ + keyboard_increment.read(),
                         SplitChangeReason::semantic);
            return true;
        }
        if (action == SemanticAction::decrement) {
            set_distance(std::max(0.0,
                                  effective_distance_ - keyboard_increment.read()),
                         SplitChangeReason::semantic);
            return true;
        }
        if (action == SemanticAction::set_value) {
            try {
                std::size_t consumed{};
                const std::string owned(value);
                const double requested = std::stod(owned, &consumed);
                if (consumed != owned.size() || !std::isfinite(requested) ||
                    requested < 0.0) {
                    return false;
                }
                set_distance(requested, SplitChangeReason::semantic);
                return true;
            } catch (...) {
                return false;
            }
        }
    }
    return ContainerControl::on_semantic_action(action, value);
}

void SplitContainer::on_detaching_from_window(SurfaceRoot& former_window) noexcept {
    cancel_splitter_interaction(false);
    static_cast<SplitterGrip&>(*splitter_).reset_interaction();
    ContainerControl::on_detaching_from_window(former_window);
}

void SplitContainer::on_detached_from_window() noexcept {
    pointer_tracking_ = false;
    collapse_tab_tracking_ = false;
    static_cast<SplitterGrip&>(*splitter_).reset_interaction();
    ContainerControl::on_detached_from_window();
}

double SplitContainer::axis_extent(Rect bounds) const noexcept {
    return orientation.read() == Orientation::vertical ? bounds.width : bounds.height;
}

double SplitContainer::pointer_axis(Point point) const noexcept {
    const Rect absolute = absolute_bounds();
    return orientation.read() == Orientation::vertical ? point.x - absolute.x
                                                  : point.y - absolute.y;
}

double SplitContainer::effective_splitter_width() const noexcept {
    if (splitter_geometry.read().thickness_policy ==
        SplitSeamThicknessPolicy::logical) {
        return splitter_geometry.read().visible_thickness;
    }
    const double scale = window() != nullptr ? (*window()).scale() : 1.0;
    return 1.0 / std::max(scale, std::numeric_limits<double>::epsilon());
}

double SplitContainer::constrained_distance(double requested,
                                            double total_extent) const noexcept {
    const double available =
        std::max(0.0, total_extent - effective_splitter_width());
    if (first_collapsed_) return 0.0;
    if (second_collapsed_) return available;
    if (!std::isfinite(requested) || requested < 0.0) requested = available * 0.5;
    double lower = std::min(first_minimum.read(), available);
    double upper = std::max(0.0, available - second_minimum.read());
    if (lower > upper) {
        const double requested_minimum = first_minimum.read() + second_minimum.read();
        const double compromise = requested_minimum > 0.0
            ? available * first_minimum.read() / requested_minimum
            : available * 0.5;
        lower = upper = compromise;
    }
    if (first_maximum.read()) upper = std::min(upper, *first_maximum.read());
    if (second_maximum.read()) {
        lower = std::max(lower, std::max(0.0, available - *second_maximum.read()));
    }
    if (lower > upper) {
        double compromise = available * 0.5;
        if (first_maximum.read() && second_maximum.read() &&
            *first_maximum.read() + *second_maximum.read() > 0.0) {
            compromise = available * *first_maximum.read() /
                         (*first_maximum.read() + *second_maximum.read());
        }
        lower = upper = std::clamp(compromise, 0.0, available);
    }
    double resolved = std::clamp(requested, lower, upper);
    if (splitter_geometry.read().thickness_policy ==
        SplitSeamThicknessPolicy::device_pixel_hairline) {
        const double scale = window() != nullptr ? (*window()).scale() : 1.0;
        const double snapped = std::round(resolved * scale) / scale;
        if (snapped >= lower && snapped <= upper) {
            resolved = snapped;
        } else if (snapped < lower) {
            const double inside = std::ceil(lower * scale) / scale;
            if (inside <= upper) resolved = inside;
        } else {
            const double inside = std::floor(upper * scale) / scale;
            if (inside >= lower) resolved = inside;
        }
    }
    return resolved;
}

Rect SplitContainer::collapse_tab_bounds() const noexcept {
    const Rect bounds = (*splitter_).absolute_bounds();
    const Rect actuator =
        static_cast<const SplitterGrip&>(*splitter_).actuator_bounds();
    return {bounds.x + actuator.x, bounds.y + actuator.y,
            actuator.width, actuator.height};
}

bool SplitContainer::collapse_target_is_collapsed() const noexcept {
    if (collapse_panel.read() == SplitFixedPanel::first) return first_collapsed_;
    if (collapse_panel.read() == SplitFixedPanel::second) return second_collapsed_;
    return false;
}

SplitCollapseOrigin SplitContainer::collapse_target_origin() const noexcept {
    if (collapse_panel.read() == SplitFixedPanel::first) return first_collapse_origin_;
    if (collapse_panel.read() == SplitFixedPanel::second) return second_collapse_origin_;
    return SplitCollapseOrigin::none;
}

void SplitContainer::toggle_collapse_target(SplitCollapseOrigin origin) {
    switch (collapse_panel.read()) {
    case SplitFixedPanel::first:
        set_first_collapsed(!first_collapsed_, origin);
        return;
    case SplitFixedPanel::second:
        set_second_collapsed(!second_collapsed_, origin);
        return;
    case SplitFixedPanel::none:
        // No panel is the collapse target.
        return;
    }
    // A panel outside the enumeration is no target.
    return;
}

void SplitContainer::reconcile_automatic_collapse(double total_extent) {
    if (collapse_panel.read() == SplitFixedPanel::none ||
        automatic_collapse_threshold.read() <= 0.0) return;
    const bool constrained = total_extent < automatic_collapse_threshold.read();
    if (!constrained) {
        automatic_collapse_suppressed_ = false;
        if (collapse_target_origin() ==
            SplitCollapseOrigin::automatic_accommodation) {
            if (collapse_panel.read() == SplitFixedPanel::first) {
                set_first_collapsed(
                    false, SplitCollapseOrigin::automatic_accommodation);
            } else {
                set_second_collapsed(
                    false, SplitCollapseOrigin::automatic_accommodation);
            }
        }
        return;
    }
    if (!automatic_collapse_suppressed_ && !collapse_target_is_collapsed()) {
        if (collapse_panel.read() == SplitFixedPanel::first) {
            set_first_collapsed(true,
                SplitCollapseOrigin::automatic_accommodation);
        } else {
            set_second_collapsed(true,
                SplitCollapseOrigin::automatic_accommodation);
        }
    }
}

void SplitContainer::set_distance(double distance, SplitChangeReason reason) {
    require_mutable();
    require_finite_nonnegative(distance,
                               "splitter distance must be finite and nonnegative");
    const double old = effective_distance_;
    requested_distance_ = distance;
    const double total = axis_extent(committed_arranged_bounds());
    effective_distance_ = total > 0.0 ? constrained_distance(distance, total)
                                      : distance;
    if (old == effective_distance_) return;
    invalidate(invalidation::bounds);
    const SplitChangeEvent change{old, effective_distance_, reason};
    publish_change(splitter_changed_, change);
}

void SplitContainer::cancel_splitter_interaction(
    const bool restore_distance) noexcept {
    const bool was_tracking = pointer_tracking_ || collapse_tab_tracking_;
    pointer_tracking_ = false;
    collapse_tab_tracking_ = false;
    SplitterGrip& grip = static_cast<SplitterGrip&>(*splitter_);
    grip.set_dragging(false);
    grip.set_actuator_pressed(false);
    if ((*splitter_).has_pointer_capture()) {
        try {
            (*splitter_).set_pointer_capture(false);
        } catch (...) {
        }
    }
    if (restore_distance && was_tracking &&
        pointer_start_distance_ != effective_distance_) {
        try {
            set_distance(pointer_start_distance_, SplitChangeReason::cancel);
        } catch (...) {
        }
    }
}

void SplitContainer::transfer_focus_from(
    const std::shared_ptr<SplitterPanel>& panel) {
    if (window() == nullptr) return;
    const Control::Ptr focused = (*window()).focused_control();
    if (focused == panel || (*panel).contains_descendant(focused)) {
        (*window()).request_focus(splitter_);
    }
}

void SplitContainer::update_splitter_cursor() {
    SplitterGrip& splitter = static_cast<SplitterGrip&>(*splitter_);
    splitter.set_orientation(orientation.read());
    splitter.set_visible_geometry(splitter_geometry.read().hit_before,
                                  effective_splitter_width());
}

namespace {

constexpr std::array<AnatomyState, 3> split_seam_states{{
    AnatomyState::hot, AnatomyState::pressed, AnatomyState::captured}};
constexpr std::array<AnatomyState, 2> split_grip_states{{
    AnatomyState::open, AnatomyState::closed}};

constexpr std::array<PartField<SplitContainer::Parts>, 2> split_container_extra_part_fields{{
    {.declaration = {.name = "seam", .role = PartRole::seam, .composed_from = {},
                     .states = split_seam_states},
     .member = &SplitContainer::Parts::seam},
    {.declaration = {.name = "grip", .role = PartRole::grip, .composed_from = {},
                     .states = split_grip_states},
     .member = &SplitContainer::Parts::grip},
}};

constexpr std::array<StateField<SplitContainer::States>, 10> split_container_state_fields{{
    {.state = AnatomyState::normal, .member = &SplitContainer::States::normal},
    {.state = AnatomyState::pressed, .member = &SplitContainer::States::pressed},
    {.state = AnatomyState::captured, .member = &SplitContainer::States::captured},
    {.state = AnatomyState::enabled, .member = &SplitContainer::States::enabled},
    {.state = AnatomyState::disabled, .member = &SplitContainer::States::disabled},
    {.state = AnatomyState::focused, .member = &SplitContainer::States::focused},
    {.state = AnatomyState::focus_visible, .member = &SplitContainer::States::focus_visible},
    {.state = AnatomyState::hot, .member = &SplitContainer::States::hot},
    {.state = AnatomyState::open, .member = &SplitContainer::States::open},
    {.state = AnatomyState::closed, .member = &SplitContainer::States::closed},
}};

constexpr std::array<AnatomyState, 10> split_container_states =
    state_declarations(split_container_state_fields);

constexpr std::array<PartDeclaration, scrollable_detail::part_count + 3>
    split_container_parts = joined_declarations(scrollable_detail::grounded_parts,
                                         part_declarations(split_container_extra_part_fields));

constexpr ControlAnatomy split_container_anatomy{
    .control_id = "split_container",
    .parts = split_container_parts,
    .states = split_container_states,
    .custom_states = {},
};

} // namespace

const ControlAnatomy& SplitContainer::anatomy() const noexcept {
    return split_container_anatomy;
}

void SplitContainer::publish_part_geometry(std::span<PartGeometry> geometry) const {
    Parts record;
    record.scrollable = scrollable_parts();

    const SplitSeamSnapshot seam = splitter_seam_snapshot();
    record.seam = PartGeometry::from_bounds(seam.visible_bounds);
    if (seam.state == SplitSeamState::near || seam.state == SplitSeamState::hot) {
        record.seam.states.insert(AnatomyState::hot);
    } else if (seam.state == SplitSeamState::dragging) {
        record.seam.states.insert(AnatomyState::pressed);
        record.seam.states.insert(AnatomyState::captured);
    }
    // The collapse actuator, given in the grip's coordinates.
    if (!seam.actuator_bounds.empty()) {
        const Rect actuator{seam.hit_bounds.x + seam.actuator_bounds.x,
                            seam.hit_bounds.y + seam.actuator_bounds.y,
                            seam.actuator_bounds.width, seam.actuator_bounds.height};
        record.grip = PartGeometry::from_bounds(actuator);
        if (seam.state == SplitSeamState::collapsed) {
            record.grip.states.insert(AnatomyState::closed);
        } else {
            record.grip.states.insert(AnatomyState::open);
        }
    }
    write_parts(anatomy(), scrollable_detail::part_fields, record.scrollable, geometry);
    write_parts(anatomy(), split_container_extra_part_fields, record, geometry);
}

SplitContainer::Parts SplitContainer::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), scrollable_detail::part_fields, published.parts(),
               record.scrollable);
    read_parts(anatomy(), split_container_extra_part_fields, published.parts(), record);
    return record;
}

AnatomyStateSet SplitContainer::anatomy_states() const {
    AnatomyStateSet states = ScrollableControl::anatomy_states();
    const SplitSeamSnapshot seam = splitter_seam_snapshot();
    if (seam.state == SplitSeamState::near || seam.state == SplitSeamState::hot) {
        states.erase(AnatomyState::normal);
        states.insert(AnatomyState::hot);
    } else if (seam.state == SplitSeamState::dragging) {
        states.erase(AnatomyState::normal);
        states.insert(AnatomyState::pressed);
        states.insert(AnatomyState::captured);
    }
    if (!seam.actuator_bounds.empty()) {
        if (seam.state == SplitSeamState::collapsed) {
            states.insert(AnatomyState::closed);
        } else {
            states.insert(AnatomyState::open);
        }
    }
    return states;
}

SplitContainer::States SplitContainer::states() const {
    States record;
    read_states(split_container_state_fields, anatomy_states(), record);
    return record;
}

namespace {

// The events SplitContainer adds to those of ContainerControl, in declaration order.
constexpr std::array<InteractionDeclaration, 1> split_container_interactions{{
    {.name = "splitter_changed", .payload = "SplitChangeEvent", .mutable_payload = false},
}};

} // namespace

void SplitContainer::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    ContainerControl::append_interactions(interactions);
    interactions.insert(interactions.end(), split_container_interactions.begin(), split_container_interactions.end());
}

// property_codemod begin SplitContainer (tools/property_families/split_container.json; generated, do not edit)

void SplitContainer::prepare_write(const decltype(first_minimum)&, double& incoming) {
    require_mutable();
    require_finite_nonnegative(incoming, "first panel minimum must be finite and nonnegative");
    const std::optional<double>& maximum = first_maximum.read();
    if (maximum && incoming > *maximum) {
        throw std::invalid_argument("first panel minimum may not exceed its maximum");
    }
}

void SplitContainer::written(const decltype(first_minimum)&) {
    invalidate(invalidation::bounds);
}

void SplitContainer::prepare_write(const decltype(second_minimum)&, double& incoming) {
    require_mutable();
    require_finite_nonnegative(incoming, "second panel minimum must be finite and nonnegative");
    const std::optional<double>& maximum = second_maximum.read();
    if (maximum && incoming > *maximum) {
        throw std::invalid_argument("second panel minimum may not exceed its maximum");
    }
}

void SplitContainer::written(const decltype(second_minimum)&) {
    invalidate(invalidation::bounds);
}

void SplitContainer::prepare_write(const decltype(first_maximum)&, std::optional<double>& incoming) {
    require_mutable();
    if (incoming) {
        require_finite_nonnegative(*incoming, "first panel maximum must be finite and nonnegative");
        if (*incoming < first_minimum.read()) {
            throw std::invalid_argument("first panel maximum may not be below its minimum");
        }
    }
}

void SplitContainer::written(const decltype(first_maximum)&) {
    invalidate(invalidation::bounds);
}

void SplitContainer::prepare_write(const decltype(second_maximum)&, std::optional<double>& incoming) {
    require_mutable();
    if (incoming) {
        require_finite_nonnegative(*incoming, "second panel maximum must be finite and nonnegative");
        if (*incoming < second_minimum.read()) {
            throw std::invalid_argument("second panel maximum may not be below its minimum");
        }
    }
}

void SplitContainer::written(const decltype(second_maximum)&) {
    invalidate(invalidation::bounds);
}

void SplitContainer::prepare_write(const decltype(splitter_fixed)&, bool&) {
    require_mutable();
}

void SplitContainer::written(const decltype(splitter_fixed)&) {
    invalidate(Dirty::semantics | Dirty::accessibility);
}

void SplitContainer::prepare_write(const decltype(fixed_panel)&, SplitFixedPanel&) {
    require_mutable();
}

void SplitContainer::written(const decltype(fixed_panel)&) {
    invalidate(Dirty::semantics | Dirty::accessibility);
}

void SplitContainer::prepare_write(const decltype(keyboard_increment)&, double& incoming) {
    require_mutable();
    if (!std::isfinite(incoming) || incoming <= 0.0) {
        throw std::invalid_argument("splitter keyboard increment must be finite and positive");
    }
}

void SplitContainer::written(const decltype(keyboard_increment)&) {}

// property_codemod end SplitContainer

} // namespace gui_forms
