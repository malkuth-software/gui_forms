#include "gui_forms/controls/scrollable_control/container_control/split_container/split_container.hpp"
#include "splitter_grip.hpp"
#include "gui_forms/window.hpp"
#include "../../scrollable_anatomy.hpp"

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

void SplitContainer::set_orientation(Orientation orientation) {
    require_mutable();
    if (orientation_ == orientation) return;
    orientation_ = orientation;
    previous_axis_extent_ = 0.0;
    previous_second_extent_ = 0.0;
    update_splitter_cursor();
    invalidate(invalidation::bounds);
}

void SplitContainer::set_splitter_distance(double distance) {
    require_finite_nonnegative(distance,
                               "splitter distance must be finite and nonnegative");
    set_distance(distance, SplitChangeReason::programmatic);
}

void SplitContainer::set_splitter_width(double width) {
    require_mutable();
    if (!std::isfinite(width) || width <= 0.0) {
        throw std::invalid_argument("splitter width must be finite and positive");
    }
    if (splitter_width_ == width) return;
    SplitSeamGeometry next = splitter_geometry_;
    const double target = std::max(splitter_hit_width_, width);
    next.visible_thickness = width;
    next.minimum_hit_target = target;
    const double extra = next.thickness_policy ==
            SplitSeamThicknessPolicy::logical
        ? std::max(0.0, target - width)
        : target;
    next.hit_before = extra * 0.5;
    next.hit_after = extra - next.hit_before;
    set_splitter_geometry(next);
}

void SplitContainer::set_splitter_hit_width(double width) {
    require_mutable();
    if (!std::isfinite(width) || width <= 0.0) {
        throw std::invalid_argument(
            "splitter hit width must be finite and positive");
    }
    const double target = std::max(width, splitter_geometry_.visible_thickness);
    if (splitter_hit_width_ == target) return;
    SplitSeamGeometry next = splitter_geometry_;
    next.minimum_hit_target = target;
    const double extra = next.thickness_policy ==
            SplitSeamThicknessPolicy::logical
        ? std::max(0.0, target - next.visible_thickness)
        : target;
    next.hit_before = extra * 0.5;
    next.hit_after = extra - next.hit_before;
    set_splitter_geometry(next);
}

void SplitContainer::set_splitter_geometry(SplitSeamGeometry geometry) {
    require_mutable();
    validate_splitter_geometry(geometry);
    if (splitter_geometry_ == geometry) return;
    splitter_geometry_ = geometry;
    splitter_width_ = geometry.visible_thickness;
    splitter_hit_width_ = geometry.minimum_hit_target;
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint |
               Dirty::hit_test | Dirty::semantics | Dirty::accessibility);
}

SplitSeamSnapshot SplitContainer::splitter_seam_snapshot() const noexcept {
    const double scale = window() != nullptr ? (*window()).scale() : 1.0;
    const double visible = effective_splitter_width();
    SplitSeamSnapshot result;
    result.orientation = orientation_;
    result.geometry = splitter_geometry_;
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
    if (orientation_ == Orientation::vertical) {
        result.visible_bounds = {effective_distance_, 0.0, visible,
                                 committed_arranged_bounds().height};
    } else {
        result.visible_bounds = {0.0, effective_distance_,
                                 committed_arranged_bounds().width, visible};
    }
    return result;
}

FrameInterval SplitContainer::splitter_transition_duration() const noexcept {
    return static_cast<const SplitterGrip&>(*splitter_).transition_duration();
}

void SplitContainer::set_splitter_transition_duration(FrameInterval duration) {
    require_mutable();
    static_cast<SplitterGrip&>(*splitter_).set_transition_duration(duration);
}

void SplitContainer::set_first_minimum(double extent) {
    require_mutable();
    require_finite_nonnegative(extent,
                               "first panel minimum must be finite and nonnegative");
    if (first_maximum_ && extent > *first_maximum_) {
        throw std::invalid_argument(
            "first panel minimum may not exceed its maximum");
    }
    if (first_minimum_ == extent) return;
    first_minimum_ = extent;
    invalidate(invalidation::bounds);
}

void SplitContainer::set_second_minimum(double extent) {
    require_mutable();
    require_finite_nonnegative(extent,
                               "second panel minimum must be finite and nonnegative");
    if (second_maximum_ && extent > *second_maximum_) {
        throw std::invalid_argument(
            "second panel minimum may not exceed its maximum");
    }
    if (second_minimum_ == extent) return;
    second_minimum_ = extent;
    invalidate(invalidation::bounds);
}

void SplitContainer::set_first_maximum(std::optional<double> extent) {
    require_mutable();
    if (extent) {
        require_finite_nonnegative(*extent,
                                   "first panel maximum must be finite and nonnegative");
        if (*extent < first_minimum_) {
            throw std::invalid_argument(
                "first panel maximum may not be below its minimum");
        }
    }
    if (first_maximum_ == extent) return;
    first_maximum_ = extent;
    invalidate(invalidation::bounds);
}

void SplitContainer::set_second_maximum(std::optional<double> extent) {
    require_mutable();
    if (extent) {
        require_finite_nonnegative(*extent,
                                   "second panel maximum must be finite and nonnegative");
        if (*extent < second_minimum_) {
            throw std::invalid_argument(
                "second panel maximum may not be below its minimum");
        }
    }
    if (second_maximum_ == extent) return;
    second_maximum_ = extent;
    invalidate(invalidation::bounds);
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
            automatic_collapse_threshold_) {
        automatic_collapse_suppressed_ = true;
    }
    (*first_panel_).visible = !collapsed;
    static_cast<SplitterGrip&>(*splitter_).set_collapse_appearance(
        collapse_panel_, collapse_target_is_collapsed());
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
            automatic_collapse_threshold_) {
        automatic_collapse_suppressed_ = true;
    }
    (*second_panel_).visible = !collapsed;
    static_cast<SplitterGrip&>(*splitter_).set_collapse_appearance(
        collapse_panel_, collapse_target_is_collapsed());
    const double total = axis_extent(committed_arranged_bounds());
    effective_distance_ = collapsed
        ? std::max(0.0, total - effective_splitter_width())
        : constrained_distance(requested_distance_, total);
    invalidate(invalidation::bounds);
    const SplitChangeEvent change{old, effective_distance_,
                                  SplitChangeReason::collapse, origin};
    publish_change(splitter_changed_, change);
}

void SplitContainer::set_splitter_fixed(bool fixed) {
    require_mutable();
    if (splitter_fixed_ == fixed) return;
    splitter_fixed_ = fixed;
    invalidate(Dirty::semantics | Dirty::accessibility);
}

void SplitContainer::set_fixed_panel(SplitFixedPanel panel) {
    require_mutable();
    if (fixed_panel_ == panel) return;
    fixed_panel_ = panel;
    invalidate(Dirty::semantics | Dirty::accessibility);
}

void SplitContainer::set_collapse_panel(SplitFixedPanel panel) {
    require_mutable();
    if (collapse_panel_ == panel) return;
    collapse_panel_ = panel;
    automatic_collapse_suppressed_ = false;
    static_cast<SplitterGrip&>(*splitter_).set_collapse_appearance(
        collapse_panel_, collapse_target_is_collapsed());
    invalidate(Dirty::paint | Dirty::hit_test | Dirty::semantics |
               Dirty::accessibility);
}

void SplitContainer::set_automatic_collapse_threshold(double extent) {
    require_mutable();
    require_finite_nonnegative(
        extent, "automatic collapse threshold must be finite and nonnegative");
    if (automatic_collapse_threshold_ == extent) return;
    automatic_collapse_threshold_ = extent;
    automatic_collapse_suppressed_ = false;
    if (extent == 0.0 && collapse_target_origin() ==
                             SplitCollapseOrigin::automatic_accommodation) {
        // With no collapse target there is nothing to restore.
        if (collapse_panel_ == SplitFixedPanel::first) {
            set_first_collapsed(false,
                                SplitCollapseOrigin::automatic_accommodation);
        } else if (collapse_panel_ == SplitFixedPanel::second) {
            set_second_collapsed(false,
                                 SplitCollapseOrigin::automatic_accommodation);
        }
    }
    invalidate(invalidation::bounds);
}

void SplitContainer::set_keyboard_increment(double increment) {
    require_mutable();
    if (!std::isfinite(increment) || increment <= 0.0) {
        throw std::invalid_argument(
            "splitter keyboard increment must be finite and positive");
    }
    keyboard_increment_ = increment;
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
        fixed_panel_ == SplitFixedPanel::second && !second_collapsed_) {
        desired = std::max(0.0, available - previous_second_extent_);
    }
    const double old = effective_distance_;
    effective_distance_ = constrained_distance(desired, total);
    if (requested_distance_ >= 0.0 || fixed_panel_ == SplitFixedPanel::second) {
        requested_distance_ = effective_distance_;
    }
    const double second_extent = std::max(0.0, available - effective_distance_);
    const double hit_width = std::min(
        total, splitter_geometry_.hit_before + visible_width +
                   splitter_geometry_.hit_after);
    const double hit_origin = std::clamp(
        effective_distance_ - splitter_geometry_.hit_before, 0.0,
        std::max(0.0, total - hit_width));
    static_cast<SplitterGrip&>(*splitter_).set_visible_geometry(
        effective_distance_ - hit_origin, visible_width);
    if (orientation_ == Orientation::vertical) {
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
    const bool on_collapse_tab = collapse_panel_ != SplitFixedPanel::none &&
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
    if (splitter_fixed_ || first_collapsed_ || second_collapsed_) return;
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
    if (collapse_panel_ != SplitFixedPanel::none &&
        (event.physical_key == PhysicalKey::enter ||
         event.physical_key == PhysicalKey::space)) {
        toggle_collapse_target(SplitCollapseOrigin::user);
        event.handled = true;
        return;
    }
    if (splitter_fixed_ || first_collapsed_ || second_collapsed_) return;
    double delta{};
    if (orientation_ == Orientation::vertical) {
        if (event.physical_key == PhysicalKey::left) delta = -keyboard_increment_;
        else if (event.physical_key == PhysicalKey::right) delta = keyboard_increment_;
        else return;
    } else {
        if (event.physical_key == PhysicalKey::up) delta = -keyboard_increment_;
        else if (event.physical_key == PhysicalKey::down) delta = keyboard_increment_;
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
    descriptor.name = accessible_name.type();
    descriptor.description = accessible_description.type();
    descriptor.value = std::to_string(effective_distance_);
    descriptor.numeric_value = effective_distance_;
    const double total = axis_extent(committed_arranged_bounds());
    descriptor.minimum_value = constrained_distance(0.0, total);
    descriptor.maximum_value = constrained_distance(total, total);
    descriptor.actions = {SemanticAction::focus};
    if (!splitter_fixed_ && !first_collapsed_ && !second_collapsed_) {
        descriptor.actions.push_back(SemanticAction::decrement);
        descriptor.actions.push_back(SemanticAction::increment);
        descriptor.actions.push_back(SemanticAction::set_value);
    }
    if (collapse_panel_ != SplitFixedPanel::none) {
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
        collapse_panel_ != SplitFixedPanel::none &&
        collapse_target_is_collapsed()) {
        toggle_collapse_target(SplitCollapseOrigin::user);
        return true;
    }
    if (action == SemanticAction::collapse &&
        collapse_panel_ != SplitFixedPanel::none &&
        !collapse_target_is_collapsed()) {
        toggle_collapse_target(SplitCollapseOrigin::user);
        return true;
    }
    if (!splitter_fixed_ && !first_collapsed_ && !second_collapsed_) {
        if (action == SemanticAction::increment) {
            set_distance(effective_distance_ + keyboard_increment_,
                         SplitChangeReason::semantic);
            return true;
        }
        if (action == SemanticAction::decrement) {
            set_distance(std::max(0.0,
                                  effective_distance_ - keyboard_increment_),
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
    return orientation_ == Orientation::vertical ? bounds.width : bounds.height;
}

double SplitContainer::pointer_axis(Point point) const noexcept {
    const Rect absolute = absolute_bounds();
    return orientation_ == Orientation::vertical ? point.x - absolute.x
                                                  : point.y - absolute.y;
}

double SplitContainer::effective_splitter_width() const noexcept {
    if (splitter_geometry_.thickness_policy ==
        SplitSeamThicknessPolicy::logical) {
        return splitter_geometry_.visible_thickness;
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
    double lower = std::min(first_minimum_, available);
    double upper = std::max(0.0, available - second_minimum_);
    if (lower > upper) {
        const double requested_minimum = first_minimum_ + second_minimum_;
        const double compromise = requested_minimum > 0.0
            ? available * first_minimum_ / requested_minimum
            : available * 0.5;
        lower = upper = compromise;
    }
    if (first_maximum_) upper = std::min(upper, *first_maximum_);
    if (second_maximum_) {
        lower = std::max(lower, std::max(0.0, available - *second_maximum_));
    }
    if (lower > upper) {
        double compromise = available * 0.5;
        if (first_maximum_ && second_maximum_ &&
            *first_maximum_ + *second_maximum_ > 0.0) {
            compromise = available * *first_maximum_ /
                         (*first_maximum_ + *second_maximum_);
        }
        lower = upper = std::clamp(compromise, 0.0, available);
    }
    double resolved = std::clamp(requested, lower, upper);
    if (splitter_geometry_.thickness_policy ==
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
    if (collapse_panel_ == SplitFixedPanel::first) return first_collapsed_;
    if (collapse_panel_ == SplitFixedPanel::second) return second_collapsed_;
    return false;
}

SplitCollapseOrigin SplitContainer::collapse_target_origin() const noexcept {
    if (collapse_panel_ == SplitFixedPanel::first) return first_collapse_origin_;
    if (collapse_panel_ == SplitFixedPanel::second) return second_collapse_origin_;
    return SplitCollapseOrigin::none;
}

void SplitContainer::toggle_collapse_target(SplitCollapseOrigin origin) {
    switch (collapse_panel_) {
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
    if (collapse_panel_ == SplitFixedPanel::none ||
        automatic_collapse_threshold_ <= 0.0) return;
    const bool constrained = total_extent < automatic_collapse_threshold_;
    if (!constrained) {
        automatic_collapse_suppressed_ = false;
        if (collapse_target_origin() ==
            SplitCollapseOrigin::automatic_accommodation) {
            if (collapse_panel_ == SplitFixedPanel::first) {
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
        if (collapse_panel_ == SplitFixedPanel::first) {
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
    splitter.set_orientation(orientation_);
    splitter.set_visible_geometry(splitter_geometry_.hit_before,
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

constexpr std::array<PartDeclaration, scrollable_detail::part_count + 2>
    split_container_parts = joined_declarations(scrollable_detail::parts,
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

} // namespace gui_forms
