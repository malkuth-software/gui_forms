#include "gui_forms/controls/scrollable_control/scrollable_control.hpp"
#include "gui_forms/detail/property_binding_adapters.hpp"
#include "scrolling_utilities.hpp"
#include "scrollable_anatomy.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"
#include "gui_forms/surface_root/surface_root.hpp"
#include "gui_forms/window.hpp"
#include "../range_control/scroll_bar/scroll_bar_painting.hpp"

#include <array>
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace gui_forms {

using namespace scrolling_detail;

ScrollableControl::ScrollableControl(StableId stable_id)
    : Control(std::move(stable_id)),
      horizontal_scroll_(*this, ScrollOrientation::horizontal),
      vertical_scroll_(*this, ScrollOrientation::vertical),
      horizontal_glide_(*this, MotionCurve::follow()),
      vertical_glide_(*this, MotionCurve::follow()) {
    // A scrollable control paints only its scroll bars and their corner, at
    // its right and bottom edges, and update_scrollbars repaints those when
    // its viewport or content changes; the rest is its children's. So a
    // resize in place keeps its pixels and damages only the strips it
    // exposed or vacated (resize R-8). A derived type whose own paint
    // depends on its size sets resize_redraw again.
    Control::set_style(ControlStyles::resize_redraw, false);
    // Each glide's step moves its axis; the step's scroll invalidates.
    horizontal_glide_.set_listener(MotionListener::bind<ScrollableControl, &ScrollableControl::glide_stepped>(*this));
    vertical_glide_.set_listener(MotionListener::bind<ScrollableControl, &ScrollableControl::glide_stepped>(*this));
}

void ScrollableControl::validate_size(Size size, const char* message) {
    if (!std::isfinite(size.width) || !std::isfinite(size.height) ||
        size.width < 0.0 || size.height < 0.0) {
        throw std::invalid_argument(message);
    }
}

void ScrollableControl::validate_axis_value(double value,
                                            const char* message) {
    if (!std::isfinite(value)) throw std::invalid_argument(message);
}

// The capabilities' hooks (study section 14; G9).

void ScrollableControl::prepare_write(const decltype(auto_scroll)&, bool&) {
    require_mutable();
}

// Turning auto scroll off returns the content to its origin; a moved
// position is reported before the derived class's reaction.
void ScrollableControl::written(const decltype(auto_scroll)&) {
    const Point previous_position = scroll_position_;
    if (auto_scroll.read()) {
        scroll_state_ |= scroll_state_auto_scrolling;
    } else {
        stop_glides();
        scroll_state_ &= ~scroll_state_auto_scrolling;
        scroll_position_ = {};
        horizontal_scroll_.value_ = horizontal_scroll_.minimum_;
        vertical_scroll_.value_ = vertical_scroll_.minimum_;
    }
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test |
               Dirty::semantics | Dirty::accessibility);
    if (previous_position != scroll_position_) on_scroll_position_changed();
    on_auto_scroll_changed(previous_position);
}

void ScrollableControl::on_auto_scroll_changed(Point) {}

void ScrollableControl::prepare_write(const decltype(auto_scroll_margin)&, Size& incoming) {
    require_mutable();
    validate_size(incoming, "AutoScrollMargin must be finite and nonnegative");
}

void ScrollableControl::written(const decltype(auto_scroll_margin)&) {
    if (!auto_scroll.read()) return;
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test |
               Dirty::semantics | Dirty::accessibility);
}

void ScrollableControl::prepare_write(const decltype(auto_scroll_min_size)&, Size& incoming) {
    require_mutable();
    validate_size(incoming, "AutoScrollMinSize must be finite and nonnegative");
}

// As Windows Forms' setter writes AutoScroll, a minimum size turns auto
// scroll on through its own write (and its derived reaction).
void ScrollableControl::written(const decltype(auto_scroll_min_size)&) {
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test |
               Dirty::semantics | Dirty::accessibility);
    if (!auto_scroll.read()) auto_scroll = true;
}

// auto_scroll_position is a view of the scroll position: the read negates
// it, as Windows Forms reports it; a write scrolls to the given position,
// clamped at zero.
void ScrollableControl::write_view(const decltype(auto_scroll_position)&, Point incoming) {
    require_mutable();
    if (!std::isfinite(incoming.x) || !std::isfinite(incoming.y)) {
        throw std::invalid_argument("AutoScrollPosition must be finite");
    }
    static_cast<void>(scroll_to({std::max(0.0, incoming.x), std::max(0.0, incoming.y)}));
}

Point ScrollableControl::read_view(const decltype(auto_scroll_position)&) const {
    const Point negated{-scroll_position_.x, -scroll_position_.y};
    return negated;
}

Rect ScrollableControl::display_rectangle() const noexcept {
    const Rect viewport = effective_viewport_rectangle();
    return {viewport.x - scroll_position_.x,
            viewport.y - scroll_position_.y,
            std::max(viewport.width, content_extent_.width),
            std::max(viewport.height, content_extent_.height)};
}

Rect ScrollableControl::effective_viewport_rectangle() const noexcept {
    // Composite controls commonly own their child arrangement while retaining
    // Panel/ContainerControl behavior. With no scrolling requested, their
    // viewport is always the live client rectangle even when they deliberately
    // bypass ScrollableControl's child-layout pass.
    if (!auto_scroll.read() && !horizontal_scroll_.visible_ &&
        !vertical_scroll_.visible_) {
        return client_rectangle();
    }
    return viewport_rectangle_;
}

void ScrollableControl::set_hscroll(bool visible) {
    require_mutable();
    if (auto_scroll.read()) return;
    horizontal_scroll_.set_visible(visible);
}

void ScrollableControl::set_vscroll(bool visible) {
    require_mutable();
    if (auto_scroll.read()) return;
    vertical_scroll_.set_visible(visible);
}

bool ScrollableControl::get_scroll_state(std::uint32_t bit) const noexcept {
    return (scroll_state_ & bit) != 0U;
}

void ScrollableControl::set_scroll_state(std::uint32_t bit, bool value) {
    require_mutable();
    constexpr std::uint32_t known = scroll_state_auto_scrolling |
        scroll_state_hscroll_visible | scroll_state_vscroll_visible |
        scroll_state_user_has_scrolled | scroll_state_full_drag;
    if (bit == 0U || (bit & ~known) != 0U) {
        throw std::invalid_argument("unknown ScrollableControl state bit");
    }
    if ((bit & scroll_state_auto_scrolling) != 0U) auto_scroll = value;
    if ((bit & scroll_state_hscroll_visible) != 0U) set_hscroll(value);
    if ((bit & scroll_state_vscroll_visible) != 0U) set_vscroll(value);
    const std::uint32_t passive = bit &
        (scroll_state_user_has_scrolled | scroll_state_full_drag);
    if (value) scroll_state_ |= passive;
    else scroll_state_ &= ~passive;
}

ScrollSnapshot ScrollableControl::scroll_snapshot() const noexcept {
    return {auto_scroll.read(), scroll_position_, auto_scroll_margin.read(),
            auto_scroll_min_size.read(), display_rectangle(),
            effective_viewport_rectangle(),
            horizontal_scroll_.snapshot(), vertical_scroll_.snapshot()};
}

Size ScrollableControl::content_extent() {
    Size extent{
        std::max(0.0, auto_scroll_min_size.read().width),
        std::max(0.0, auto_scroll_min_size.read().height)};
    const Insets own_padding = padding.read();
    const std::vector<Control::Ptr> retained = snapshot_layout_children();
    for (const Control::Ptr& child : retained) {
        if (!is_current_layout_child(child) || !(*child).visible.read() ||
            (*child).dock.read() != DockStyle::none) continue;
        Rect bounds = (*child).requested_bounds();
        if ((*child).auto_size.read()) {
            const Size preferred = (*child).get_preferred_size({0.0, 0.0});
            if (!is_alive()) return extent;
            if (!is_current_layout_child(child) || !(*child).visible.read() ||
                (*child).dock.read() != DockStyle::none) {
                continue;
            }
            bounds.width = (*child).auto_size_mode.read() == AutoSizeMode::grow_only
                ? std::max(bounds.width, preferred.width) : preferred.width;
            bounds.height = (*child).auto_size_mode.read() == AutoSizeMode::grow_only
                ? std::max(bounds.height, preferred.height) : preferred.height;
        }
        const Insets child_margin = (*child).margin.read();
        extent.width = std::max(
            extent.width,
            std::max(0.0, bounds.x) + bounds.width + child_margin.right +
                auto_scroll_margin.read().width + own_padding.right);
        extent.height = std::max(
            extent.height,
            std::max(0.0, bounds.y) + bounds.height + child_margin.bottom +
                auto_scroll_margin.read().height + own_padding.bottom);
    }
    return extent;
}

void ScrollableControl::recompute_scroll_layout(Size client_size) {
    const Size extent = content_extent();
    if (!is_alive()) return;
    arrange_scroll_viewport(client_size, extent);
}

void ScrollableControl::arrange_scroll_viewport(Size client_size, Size measured_content) {
    const Rect previous_viewport = viewport_rectangle_;
    const Size previous_content = content_extent_;
    const bool previous_h = horizontal_scroll_.visible_;
    const bool previous_v = vertical_scroll_.visible_;
    const Point previous_position = scroll_position_;

    content_extent_ = {std::max(auto_scroll_min_size.read().width, measured_content.width),
                       std::max(auto_scroll_min_size.read().height, measured_content.height)};
    if (!is_alive()) return;
    const double width = std::max(0.0, client_size.width);
    const double height = std::max(0.0, client_size.height);
    bool h = auto_scroll.read() ? false : horizontal_scroll_.visible_;
    bool v = auto_scroll.read() ? false : vertical_scroll_.visible_;
    if (auto_scroll.read()) {
        for (int pass = 0; pass != 3; ++pass) {
            const double available_width = std::max(
                0.0, width - (v ? scrollbar_thickness_ : 0.0));
            const double available_height = std::max(
                0.0, height - (h ? scrollbar_thickness_ : 0.0));
            const bool next_h = approximately_greater(
                content_extent_.width, available_width);
            const bool next_v = approximately_greater(
                content_extent_.height, available_height);
            if (h == next_h && v == next_v) break;
            h = next_h;
            v = next_v;
        }
    }
    viewport_rectangle_ = {
        0.0, 0.0,
        std::max(0.0, width - (v ? scrollbar_thickness_ : 0.0)),
        std::max(0.0, height - (h ? scrollbar_thickness_ : 0.0))};
    content_extent_.width = std::max(content_extent_.width,
                                     viewport_rectangle_.width);
    content_extent_.height = std::max(content_extent_.height,
                                      viewport_rectangle_.height);

    if (auto_scroll.read()) {
        horizontal_scroll_.set_automatic(viewport_rectangle_.width,
                                         content_extent_.width,
                                         scroll_position_.x);
        vertical_scroll_.set_automatic(viewport_rectangle_.height,
                                       content_extent_.height,
                                       scroll_position_.y);
        h = horizontal_scroll_.visible_;
        v = vertical_scroll_.visible_;
    } else {
        horizontal_scroll_.value_ = std::clamp(
            horizontal_scroll_.value_, horizontal_scroll_.minimum_,
            horizontal_scroll_.maximum_position());
        vertical_scroll_.value_ = std::clamp(
            vertical_scroll_.value_, vertical_scroll_.minimum_,
            vertical_scroll_.maximum_position());
    }
    scroll_position_ = {horizontal_scroll_.visible_
                            ? horizontal_scroll_.value_ : 0.0,
                        vertical_scroll_.visible_
                            ? vertical_scroll_.value_ : 0.0};
    if (h) scroll_state_ |= scroll_state_hscroll_visible;
    else scroll_state_ &= ~scroll_state_hscroll_visible;
    if (v) scroll_state_ |= scroll_state_vscroll_visible;
    else scroll_state_ &= ~scroll_state_vscroll_visible;

    // A new scroll bar or position moves what is drawn. A viewport or
    // content extent that only changed size repaints the control only when
    // its paint depends on its size; otherwise the resize damage of its
    // exposed strips is enough (Window::keeps_pixels_on_resize).
    const bool scrolled = previous_h != h || previous_v != v || previous_position != scroll_position_;
    const bool resized = previous_viewport != viewport_rectangle_ || previous_content != content_extent_;
    if (scrolled || resized) {
        Dirty effects = Dirty::hit_test | Dirty::semantics | Dirty::accessibility;
        if (scrolled || paint_depends_on_size()) effects |= Dirty::paint;
        invalidate(effects);
    }
    // A resize that keeps the bars still moves and rescales them: repaint
    // the strips they occupy, before and after, when the rest keeps its
    // pixels.
    if (resized && !scrolled && !paint_depends_on_size() && (h || v)) invalidate_scroll_bar_strips(previous_viewport);
    if (previous_position != scroll_position_) on_scroll_position_changed();
}

// The strips the scroll bars and their corner occupy, right of and below
// the narrower and shorter of `previous_viewport` and the current one, in
// local coordinates (Control::invalidate clips them to the client area).
void ScrollableControl::invalidate_scroll_bar_strips(Rect previous_viewport) {
    const Rect bounds = committed_arranged_bounds();
    const double right = std::min(previous_viewport.width, viewport_rectangle_.width);
    const double bottom = std::min(previous_viewport.height, viewport_rectangle_.height);
    if (vscroll() && bounds.width > right) invalidate(Rect{right, 0.0, bounds.width - right, bounds.height});
    if (hscroll() && bounds.height > bottom) invalidate(Rect{0.0, bottom, bounds.width, bounds.height - bottom});
}

void ScrollableControl::adjust_scrollbars(bool display_scrollbars) {
    if (!display_scrollbars) {
        stop_glides();
        const Point previous_position = scroll_position_;
        horizontal_scroll_.visible_ = false;
        vertical_scroll_.visible_ = false;
        scroll_position_ = {};
        scroll_state_ &= ~(scroll_state_hscroll_visible |
                           scroll_state_vscroll_visible);
        if (previous_position != scroll_position_) on_scroll_position_changed();
    }
}

void ScrollableControl::arrange(Rect final_bounds) {
    if (arranging_scroll_) {
        Control::arrange(final_bounds);
        return;
    }
    arranging_scroll_ = true;
    struct Reset final {
        bool& value;
        ~Reset() { value = false; }
    } reset{arranging_scroll_};
    const Rect previous_bounds = committed_arranged_bounds();
    const Rect previous_viewport = viewport_rectangle_;
    arrange_self(final_bounds);
    recompute_scroll_layout({final_bounds.width, final_bounds.height});
    if (!is_alive()) return;
    if (auto_scroll.read()) adjust_scrollbars(true);
    Control::arrange(final_bounds);
    copy_arranged_ = true;
    const Point previous_painted = painted_scroll_position_;
    const Point painted = device_scroll_position();
    painted_scroll_position_ = painted;
    // Right to left, the base arrange mirrored the slots, so scrolling the
    // logical content toward the left moves them to the right.
    const bool mirrored = effective_layout_direction() == LayoutDirection::right_to_left;
    if (painted != Point{}) {
        for (const Control::Ptr& child : children()) {
            if (!child || !(*child).is_alive() || !(*child).visible.read() ||
                (*child).dock.read() != DockStyle::none) continue;
            // The base arrange just assigned the unscrolled slot; offset that, not
            // the committed bounds, which still carry the previous scroll offset.
            Rect bounds = (*child).pending_layout_bounds();
            bounds.x += mirrored ? painted.x : -painted.x;
            bounds.y -= painted.y;
            set_child_layout(child, bounds);
        }
    }
    if (!scroll_copy_pending_) return;
    // A scroll left the viewport's repaint to this arrange: the children
    // moved by whole device pixels, so their pixels are copied and only the
    // band exposed and the bars are repainted, when this arrange changed
    // nothing else in the viewport and the surface accepts the copy.
    scroll_copy_pending_ = false;
    const Point moved{mirrored ? painted.x - previous_painted.x : previous_painted.x - painted.x,
                      previous_painted.y - painted.y};
    const Rect arranged = committed_arranged_bounds();
    const bool unchanged = previous_viewport == viewport_rectangle_ &&
        previous_bounds.width == arranged.width && previous_bounds.height == arranged.height;
    bool copied = unchanged && moved == Point{};
    if (unchanged && !copied && surface_root() != nullptr) {
        copied = (*surface_root()).request_scroll_copy(*this, moved);
    }
    // The bars were repainted by the scroll (apply_axis_value).
    if (!copied) invalidate(Dirty::paint);
}

// The scroll position rounded to whole device pixels, where the children are
// placed (a scroll then moves their pixels exactly).
Point ScrollableControl::device_scroll_position() const noexcept {
    const double scale = surface_root() != nullptr ? (*surface_root()).scale() : 1.0;
    if (!(scale > 0.0)) return scroll_position_;
    const Point rounded{std::round(scroll_position_.x * scale) / scale,
                        std::round(scroll_position_.y * scale) / scale};
    return rounded;
}

// Whether a scroll may leave its repaint to the next arrange's copy.
bool ScrollableControl::scroll_copies_children() const noexcept {
    if (!copy_arranged_ || paints_scrolled_content() || surface_root() == nullptr) return false;
    const bool supported = (*surface_root()).scroll_copies_supported();
    return supported;
}

double ScrollableControl::maximum_offset(
    ScrollOrientation orientation) const noexcept {
    const ScrollProperties& axis = orientation == ScrollOrientation::horizontal
        ? horizontal_scroll_ : vertical_scroll_;
    return axis.visible_ ? axis.maximum_position() : 0.0;
}

bool ScrollableControl::apply_axis_value(ScrollOrientation orientation,
                                         double value,
                                         ScrollEventType type,
                                         bool notify) {
    require_mutable();
    validate_axis_value(value, "scroll position must be finite");
    ScrollProperties& axis = orientation == ScrollOrientation::horizontal
        ? horizontal_scroll_ : vertical_scroll_;
    const double old_value = axis.value_;
    const double next = std::clamp(
        value, axis.minimum_, maximum_offset(orientation));
    if (std::abs(next - old_value) <= comparison_epsilon) return false;
    axis.value_ = next;
    if (orientation == ScrollOrientation::horizontal) scroll_position_.x = next;
    else scroll_position_.y = next;
    if (notify) scroll_state_ |= scroll_state_user_has_scrolled;
    // The children this class arranges move by a copy of their pixels when
    // the surface accepts one (arrange); otherwise the viewport repaints.
    // The bars repaint now either way, which also asks the host for a frame.
    Dirty effects = Dirty::arrange | Dirty::hit_test | Dirty::semantics | Dirty::accessibility;
    const bool copy = scroll_copies_children() && (vscroll() || hscroll());
    if (copy) scroll_copy_pending_ = true;
    else effects |= Dirty::paint;
    invalidate(effects);
    if (copy) invalidate_scroll_bar_strips(viewport_rectangle_);
    on_scroll_position_changed();
    if (notify) notify_scroll(orientation, type, old_value, next);
    return true;
}

bool ScrollableControl::scroll_to(Point position, ScrollEventType type,
                                  bool notify) {
    if (!std::isfinite(position.x) || !std::isfinite(position.y)) {
        throw std::invalid_argument("scroll position must be finite");
    }
    const bool horizontal = apply_axis_input(
        ScrollOrientation::horizontal, position.x, type, notify);
    const bool vertical = apply_axis_input(
        ScrollOrientation::vertical, position.y, type, notify);
    return horizontal || vertical;
}

bool ScrollableControl::scroll_by(Point delta,
                                  ScrollEventType horizontal_type,
                                  ScrollEventType vertical_type,
                                  bool notify) {
    if (!std::isfinite(delta.x) || !std::isfinite(delta.y)) {
        throw std::invalid_argument("scroll delta must be finite");
    }
    const bool horizontal = apply_axis_input(
        ScrollOrientation::horizontal, scroll_position_.x + delta.x,
        horizontal_type, notify);
    const bool vertical = apply_axis_input(
        ScrollOrientation::vertical, scroll_position_.y + delta.y,
        vertical_type, notify);
    return horizontal || vertical;
}

bool ScrollableControl::apply_axis_input(ScrollOrientation orientation,
                                         double value,
                                         ScrollEventType type,
                                         bool notify) {
    validate_axis_value(value, "scroll position must be finite");
    const ScrollProperties& axis = orientation == ScrollOrientation::horizontal
        ? horizontal_scroll_ : vertical_scroll_;
    const double next = std::clamp(
        value, axis.minimum_, maximum_offset(orientation));
    // A write of the position the axis already stands at (a text box
    // restating its offset on paint) leaves a smooth scroll moving.
    if (std::abs(next - axis.value_) > comparison_epsilon) stop_glide(orientation);
    const bool changed = apply_axis_value(orientation, value, type, notify);
    return changed;
}

double ScrollableControl::glide_base(ScrollOrientation orientation) const noexcept {
    const bool horizontal = orientation == ScrollOrientation::horizontal;
    const MotionValue& glide = horizontal ? horizontal_glide_ : vertical_glide_;
    if (!glide.settled()) return glide.target();
    const double position = horizontal ? horizontal_scroll_.value_ : vertical_scroll_.value_;
    return position;
}

void ScrollableControl::stop_glide(ScrollOrientation orientation) noexcept {
    MotionValue& glide = orientation == ScrollOrientation::horizontal
        ? horizontal_glide_ : vertical_glide_;
    if (!glide.settled()) glide.finish();
}

void ScrollableControl::stop_glides() noexcept {
    stop_glide(ScrollOrientation::horizontal);
    stop_glide(ScrollOrientation::vertical);
}

bool ScrollableControl::glide_axis(ScrollOrientation orientation,
                                   double target,
                                   ScrollEventType type,
                                   bool notify) {
    require_mutable();
    validate_axis_value(target, "scroll position must be finite");
    const bool horizontal = orientation == ScrollOrientation::horizontal;
    const ScrollProperties& axis = horizontal ? horizontal_scroll_ : vertical_scroll_;
    MotionValue& glide = horizontal ? horizontal_glide_ : vertical_glide_;
    const double position = axis.value_;
    const double clamped = std::clamp(
        target, axis.minimum_, maximum_offset(orientation));
    if (std::abs(clamped - glide_base(orientation)) <= comparison_epsilon) return false;
    // A settled glide starts from where the position stands now (it may
    // have moved by other means since the last glide settled).
    if (glide.settled()) glide.jump_to(position);
    const MotionStart started = glide.animate_to(clamped);
    if (started == MotionStart::refused || started == MotionStart::unchanged) return false;
    if (started != MotionStart::started) {
        // Reduced motion, an occluded window or no window: the glide stands
        // at its target and the position moves there at once.
        const bool moved = apply_axis_value(orientation, clamped, type, notify);
        return moved;
    }
    if (notify) {
        scroll_state_ |= scroll_state_user_has_scrolled;
        notify_scroll(orientation, type, position, clamped);
    }
    return true;
}

void ScrollableControl::glide_stepped(const MotionStep& step) {
    if (!is_alive()) return;
    const ScrollOrientation orientation = step.value == &horizontal_glide_
        ? ScrollOrientation::horizontal : ScrollOrientation::vertical;
    static_cast<void>(apply_axis_value(orientation, step.current,
                                       ScrollEventType::thumb_track, false));
}

void ScrollableControl::glide_to(Point target) {
    if (!std::isfinite(target.x) || !std::isfinite(target.y)) {
        throw std::invalid_argument("scroll position must be finite");
    }
    static_cast<void>(glide_axis(ScrollOrientation::horizontal, target.x,
                                 ScrollEventType::thumb_position, false));
    static_cast<void>(glide_axis(ScrollOrientation::vertical, target.y,
                                 ScrollEventType::thumb_position, false));
}

Point ScrollableControl::scroll_target() const noexcept {
    const Point target{
        horizontal_glide_.settled() ? scroll_position_.x : horizontal_glide_.target(),
        vertical_glide_.settled() ? scroll_position_.y : vertical_glide_.target()};
    return target;
}

bool ScrollableControl::scrolling_smoothly() const noexcept {
    const bool moving = !horizontal_glide_.settled() || !vertical_glide_.settled();
    return moving;
}

void ScrollableControl::set_display_rect_location(Point location) {
    if (!std::isfinite(location.x) || !std::isfinite(location.y)) {
        throw std::invalid_argument("display rectangle location must be finite");
    }
    static_cast<void>(scroll_to({std::max(0.0, -location.x),
                                 std::max(0.0, -location.y)}));
}

Point ScrollableControl::scroll_to_control(const Control& control) const {
    Rect bounds;
    if (attached() && control.attached()) {
        const Rect local = rectangle_from_window(control.rectangle_to_window(
            {0.0, 0.0, control.committed_arranged_bounds().width,
             control.committed_arranged_bounds().height}));
        bounds = local;
        if (effective_layout_direction() == LayoutDirection::right_to_left) {
            // Right to left, the content was mirrored about the viewport:
            // its logical place, which the scroll offset moves, is the
            // mirror image.
            const Rect viewport = effective_viewport_rectangle();
            bounds.x = 2.0 * viewport.x + viewport.width - local.x - local.width;
        }
        bounds.x += scroll_position_.x;
        bounds.y += scroll_position_.y;
    } else {
        // Detached controls still have a complete authored retained tree even
        // though no Window has committed descendant layout slots. Walk that
        // logical tree so designer/tests and pre-show initialization can reveal
        // a control without inventing zero-sized arranged geometry.
        bounds = control.requested_bounds();
        for (Control::Ptr ancestor = control.parent(); ancestor;
             ancestor = (*ancestor).parent()) {
            if (ancestor.get() == this) break;
            const Rect parent_bounds = (*ancestor).requested_bounds();
            bounds.x += parent_bounds.x;
            bounds.y += parent_bounds.y;
        }
    }
    Point next = scroll_position_;
    if (bounds.x - auto_scroll_margin.read().width < next.x) {
        next.x = bounds.x - auto_scroll_margin.read().width;
    } else if (bounds.right() + auto_scroll_margin.read().width >
               next.x + viewport_rectangle_.width) {
        next.x = bounds.right() + auto_scroll_margin.read().width -
                 viewport_rectangle_.width;
    }
    if (bounds.y - auto_scroll_margin.read().height < next.y) {
        next.y = bounds.y - auto_scroll_margin.read().height;
    } else if (bounds.bottom() + auto_scroll_margin.read().height >
               next.y + viewport_rectangle_.height) {
        next.y = bounds.bottom() + auto_scroll_margin.read().height -
                 viewport_rectangle_.height;
    }
    const Point offset = control.auto_scroll_offset();
    next.x -= offset.x;
    next.y -= offset.y;
    return {std::max(0.0, next.x), std::max(0.0, next.y)};
}

void ScrollableControl::scroll_control_into_view(
    const Control::Ptr& control) {
    require_mutable();
    if (!control || !contains(*control) || !auto_scroll.read() ||
        (!hscroll() && !vscroll()) || viewport_rectangle_.empty()) return;
    static_cast<void>(scroll_to(scroll_to_control(*control)));
    scroll_state_ &= ~scroll_state_user_has_scrolled;
}

void ScrollableControl::notify_scroll(ScrollOrientation orientation,
                                      ScrollEventType type,
                                      double old_value,
                                      double new_value) {
    ScrollEvent event{type, old_value, new_value, orientation};
    last_scroll_event_ = event;
    ++scroll_event_revision_;
    scroll_event_.emit(event);
}

ScrollableControl::AxisGeometry ScrollableControl::axis_geometry(
    ScrollOrientation orientation) const noexcept {
    const ScrollProperties& axis = orientation == ScrollOrientation::horizontal
        ? horizontal_scroll_ : vertical_scroll_;
    const AxisGeometry geometry = axis_geometry(orientation, axis.value_);
    return geometry;
}

ScrollableControl::AxisGeometry ScrollableControl::axis_geometry(
    ScrollOrientation orientation, double value) const noexcept {
    AxisGeometry geometry;
    // A theme without scroll arrows (ThemeGeometryTokens::scroll_arrows)
    // gives the track the whole bar. Below twice the button extent the two
    // buttons share the bar (scroll_bar_detail draws no mark in a button
    // smaller than the mark) and the track is gone.
    const double button_extent =
        effective_theme().structure().geometry.scroll_arrows ? scrollbar_button_extent_ : 0.0;
    if (orientation == ScrollOrientation::horizontal) {
        if (!horizontal_scroll_.visible_) return geometry;
        geometry.bar = {0.0, viewport_rectangle_.height,
                        viewport_rectangle_.width, scrollbar_thickness_};
        const double button = std::min(button_extent, geometry.bar.width * 0.5);
        geometry.first_button = {geometry.bar.x, geometry.bar.y, button,
                                 geometry.bar.height};
        geometry.last_button = {geometry.bar.right() - button, geometry.bar.y,
                                button, geometry.bar.height};
        geometry.track = {geometry.first_button.right(), geometry.bar.y,
                          std::max(0.0, geometry.bar.width - button * 2.0),
                          geometry.bar.height};
    } else {
        if (!vertical_scroll_.visible_) return geometry;
        geometry.bar = {viewport_rectangle_.width, 0.0,
                        scrollbar_thickness_, viewport_rectangle_.height};
        const double button = std::min(button_extent, geometry.bar.height * 0.5);
        geometry.first_button = {geometry.bar.x, geometry.bar.y,
                                 geometry.bar.width, button};
        geometry.last_button = {geometry.bar.x,
                                geometry.bar.bottom() - button,
                                geometry.bar.width, button};
        geometry.track = {geometry.bar.x, geometry.first_button.bottom(),
                          geometry.bar.width,
                          std::max(0.0, geometry.bar.height - button * 2.0)};
    }
    const bool horizontal = orientation == ScrollOrientation::horizontal;
    const double track_start = horizontal ? geometry.track.x : geometry.track.y;
    const double track_extent = horizontal ? geometry.track.width : geometry.track.height;
    const double viewport_extent = horizontal ? viewport_rectangle_.width : viewport_rectangle_.height;
    const double content = horizontal ? content_extent_.width : content_extent_.height;
    const double proportion = content <= 0.0 ? 1.0 : viewport_extent / content;
    const double maximum = maximum_offset(orientation);
    const double fraction = maximum <= 0.0 ? 0.0 : value / maximum;
    const scroll_bar_detail::ThumbPlacement placed = scroll_bar_detail::place_thumb(
        track_extent, proportion, fraction, minimum_thumb_extent_);
    // The thumb's ends on whole device pixels, within the track.
    const double scale = surface_root() != nullptr ? (*surface_root()).scale() : 1.0;
    const double device = scale > 0.0 ? scale : 1.0;
    const double track_end = track_start + track_extent;
    const double start = std::clamp(std::round((track_start + placed.offset) * device) / device,
                                    track_start, track_end);
    const double end = std::clamp(std::round((track_start + placed.offset + placed.extent) * device) / device,
                                  start, track_end);
    geometry.thumb = horizontal
        ? Rect{start, geometry.track.y, end - start, geometry.track.height}
        : Rect{geometry.track.x, start, geometry.track.width, end - start};
    return geometry;
}

ScrollableControl::ScrollPart ScrollableControl::part_at(
    Point local) const noexcept {
    const AxisGeometry horizontal = axis_geometry(ScrollOrientation::horizontal);
    if (horizontal.bar.contains(local)) {
        if (horizontal.first_button.contains(local)) return ScrollPart::horizontal_first;
        if (horizontal.last_button.contains(local)) return ScrollPart::horizontal_last;
        if (horizontal.thumb.contains(local)) return ScrollPart::horizontal_thumb;
        return local.x < horizontal.thumb.x
            ? ScrollPart::horizontal_track_before
            : ScrollPart::horizontal_track_after;
    }
    const AxisGeometry vertical = axis_geometry(ScrollOrientation::vertical);
    if (vertical.bar.contains(local)) {
        if (vertical.first_button.contains(local)) return ScrollPart::vertical_first;
        if (vertical.last_button.contains(local)) return ScrollPart::vertical_last;
        if (vertical.thumb.contains(local)) return ScrollPart::vertical_thumb;
        return local.y < vertical.thumb.y
            ? ScrollPart::vertical_track_before
            : ScrollPart::vertical_track_after;
    }
    return ScrollPart::none;
}

bool ScrollableControl::handle_scroll_pointer(PointerEvent& event) {
    if (event.action == PointerAction::leave) {
        set_hot_part(ScrollPart::none);
        return false;
    }
    const Point local = point_from_window(event.position);
    if (dragging_orientation_) {
        if (event.pointer_id != 0U && dragging_pointer_id_ != 0U &&
            event.pointer_id != dragging_pointer_id_) return false;
        const AxisGeometry geometry = axis_geometry(*dragging_orientation_);
        const double track_extent = *dragging_orientation_ ==
                ScrollOrientation::horizontal
            ? geometry.track.width : geometry.track.height;
        const double thumb_extent = *dragging_orientation_ ==
                ScrollOrientation::horizontal
            ? geometry.thumb.width : geometry.thumb.height;
        const double travel = std::max(0.0, track_extent - thumb_extent);
        if (event.action == PointerAction::move && travel > 0.0) {
            const double movement = *dragging_orientation_ ==
                    ScrollOrientation::horizontal
                ? local.x - dragging_origin_.x
                : local.y - dragging_origin_.y;
            const double next = dragging_start_value_ + movement *
                maximum_offset(*dragging_orientation_) / travel;
            static_cast<void>(apply_axis_input(*dragging_orientation_, next,
                                               ScrollEventType::thumb_track,
                                               true));
            event.handled = true;
            return true;
        }
        if (event.action == PointerAction::up) {
            const ScrollOrientation orientation = *dragging_orientation_;
            const double value = orientation == ScrollOrientation::horizontal
                ? scroll_position_.x : scroll_position_.y;
            dragging_orientation_.reset();
            dragging_pointer_id_ = 0U;
            set_pointer_capture(false);
            notify_scroll(orientation, ScrollEventType::thumb_position,
                          value, value);
            // The thumb shows pressed no longer; the hover is where the
            // pointer was released.
            invalidate_scroll_bar_strips(viewport_rectangle_);
            set_hot_part(part_at(local));
            event.handled = true;
            return true;
        }
        event.handled = true;
        return true;
    }
    if (pressed_part_ != ScrollPart::none) {
        if (event.pointer_id != 0U && pressed_pointer_id_ != 0U &&
            event.pointer_id != pressed_pointer_id_) return false;
        if (event.action == PointerAction::move) {
            repeat_pointer_ = local;
            set_hot_part(part_at(local));
        } else if (event.action == PointerAction::up) {
            stop_press();
            set_pointer_capture(false);
            invalidate_scroll_bar_strips(viewport_rectangle_);
            set_hot_part(part_at(local));
        } else {
            // Other buttons and wheels while the press holds are taken.
        }
        event.handled = true;
        return true;
    }
    if (event.action == PointerAction::move) {
        set_hot_part(part_at(local));
        return false;
    }
    if (event.action != PointerAction::down ||
        event.button != PointerButton::primary) return false;
    const ScrollPart part = part_at(local);
    if (part == ScrollPart::none) return false;
    const bool horizontal = part >= ScrollPart::horizontal_first &&
        part <= ScrollPart::horizontal_last;
    const ScrollOrientation orientation = horizontal
        ? ScrollOrientation::horizontal : ScrollOrientation::vertical;
    ScrollProperties& axis = horizontal ? horizontal_scroll_ : vertical_scroll_;
    if (!axis.enabled_) {
        event.handled = true;
        return true;
    }
    if (part == ScrollPart::horizontal_thumb ||
        part == ScrollPart::vertical_thumb) {
        // The thumb is direct manipulation: it takes the axis from a
        // smooth scroll where the position stands.
        stop_glide(orientation);
        dragging_orientation_ = orientation;
        dragging_pointer_id_ = event.pointer_id == 0U ? 1U : event.pointer_id;
        dragging_origin_ = local;
        dragging_start_value_ = axis.value_;
        set_pointer_capture(true);
    } else {
        pressed_part_ = part;
        pressed_pointer_id_ = event.pointer_id == 0U ? 1U : event.pointer_id;
        repeat_pointer_ = local;
        set_pointer_capture(true);
        static_cast<void>(step_part(part));
        begin_repeat();
    }
    invalidate_scroll_bar_strips(viewport_rectangle_);
    event.handled = true;
    return true;
}

bool ScrollableControl::step_part(ScrollPart part) {
    const bool horizontal = part >= ScrollPart::horizontal_first &&
        part <= ScrollPart::horizontal_last;
    const ScrollOrientation orientation = horizontal
        ? ScrollOrientation::horizontal : ScrollOrientation::vertical;
    const ScrollProperties& axis = horizontal ? horizontal_scroll_ : vertical_scroll_;
    if (!axis.enabled_ || part == ScrollPart::none ||
        part == ScrollPart::horizontal_thumb || part == ScrollPart::vertical_thumb) {
        return false;
    }
    // A step while the axis glides goes on from its target, so that quick
    // presses and repeats add up.
    double next = glide_base(orientation);
    ScrollEventType type = ScrollEventType::small_increment;
    if (part == ScrollPart::horizontal_first || part == ScrollPart::vertical_first) {
        next -= axis.small_change();
        type = ScrollEventType::small_decrement;
    } else if (part == ScrollPart::horizontal_last || part == ScrollPart::vertical_last) {
        next += axis.small_change();
        type = ScrollEventType::small_increment;
    } else if (part == ScrollPart::horizontal_track_before ||
               part == ScrollPart::vertical_track_before) {
        next -= axis.large_change();
        type = ScrollEventType::large_decrement;
    } else {
        next += axis.large_change();
        type = ScrollEventType::large_increment;
    }
    const bool moved = glide_axis(orientation, next, type, true);
    return moved;
}

void ScrollableControl::begin_repeat() {
    repeat_frames_.disconnect();
    if (window() == nullptr || pressed_part_ == ScrollPart::none) return;
    const FrameTime now = FrameClock::now();
    const std::shared_ptr<ScrollableControl> self =
        std::static_pointer_cast<ScrollableControl>(shared_from_this());
    repeat_frames_ = (*window()).schedule_ui_timer(
        *this, scroll_repeat_interval_default, now + scroll_repeat_delay_default, RepeatTick{self});
}

void ScrollableControl::RepeatTick::operator()(FrameTime now) const {
    static_cast<void>(now);
    const std::shared_ptr<ScrollableControl> live = control.lock();
    if (live) (*live).repeat_step();
}

void ScrollableControl::repeat_step() {
    if (pressed_part_ == ScrollPart::none || !has_pointer_capture()) {
        stop_press();
        invalidate_scroll_bar_strips(viewport_rectangle_);
        return;
    }
    const bool track = pressed_part_ == ScrollPart::horizontal_track_before ||
        pressed_part_ == ScrollPart::horizontal_track_after ||
        pressed_part_ == ScrollPart::vertical_track_before ||
        pressed_part_ == ScrollPart::vertical_track_after;
    if (track && !track_press_continues()) {
        repeat_frames_.disconnect();
        return;
    }
    if (!step_part(pressed_part_)) repeat_frames_.disconnect();
}

bool ScrollableControl::track_press_continues() const noexcept {
    const bool horizontal = pressed_part_ == ScrollPart::horizontal_track_before ||
        pressed_part_ == ScrollPart::horizontal_track_after;
    const ScrollOrientation orientation = horizontal
        ? ScrollOrientation::horizontal : ScrollOrientation::vertical;
    const AxisGeometry geometry = axis_geometry(orientation, glide_base(orientation));
    const double pointer = horizontal ? repeat_pointer_.x : repeat_pointer_.y;
    const double thumb_start = horizontal ? geometry.thumb.x : geometry.thumb.y;
    const double thumb_end = horizontal ? geometry.thumb.right() : geometry.thumb.bottom();
    const bool before = pressed_part_ == ScrollPart::horizontal_track_before ||
        pressed_part_ == ScrollPart::vertical_track_before;
    const bool continues = before ? pointer < thumb_start : pointer >= thumb_end;
    return continues;
}

void ScrollableControl::stop_press() noexcept {
    repeat_frames_.disconnect();
    pressed_part_ = ScrollPart::none;
    pressed_pointer_id_ = 0U;
}

void ScrollableControl::set_hot_part(ScrollPart part) {
    if (hot_part_ == part) return;
    hot_part_ = part;
    if (hscroll() || vscroll()) invalidate_scroll_bar_strips(viewport_rectangle_);
}

void ScrollableControl::on_detached_from_window() noexcept {
    stop_press();
    hot_part_ = ScrollPart::none;
    Control::on_detached_from_window();
}

bool ScrollableControl::reveal_focused_descendant(const Control& descendant) {
    if (!auto_scroll.read()) return false;
    if ((hscroll() || vscroll()) && !viewport_rectangle_.empty()) {
        glide_to(scroll_to_control(descendant));
        scroll_state_ &= ~scroll_state_user_has_scrolled;
    }
    return true;
}

bool ScrollableControl::handle_wheel(PointerEvent& event) {
    if (event.action != PointerAction::wheel || event.handled) return false;
    const double vertical_delta = -normalize_wheel_delta(event.wheel_delta.y, event.wheel_unit);
    const double horizontal_delta = -normalize_wheel_delta(event.wheel_delta.x, event.wheel_unit);
    // A detented wheel's lines add to the axis's glide target. A precise
    // device's pixels (a trackpad, whose stream the system already smooths
    // and carries on with momentum) move the position at once, as do lines
    // a host reports in pixels (normalize_wheel_delta), stopping any glide.
    const bool precise = event.wheel_unit == WheelDeltaUnit::pixels ||
        std::abs(event.wheel_delta.y) > 8.0 || std::abs(event.wheel_delta.x) > 8.0;
    bool changed{};
    if (vertical_scroll_.visible_ && vertical_scroll_.enabled_ &&
        vertical_delta != 0.0) {
        const ScrollEventType type = vertical_delta < 0.0
            ? ScrollEventType::small_decrement : ScrollEventType::small_increment;
        changed = precise
            ? apply_axis_input(ScrollOrientation::vertical,
                               scroll_position_.y + vertical_delta, type, false)
            : glide_axis(ScrollOrientation::vertical,
                         glide_base(ScrollOrientation::vertical) + vertical_delta, type, false);
    }
    if (!changed && horizontal_scroll_.visible_ &&
        horizontal_scroll_.enabled_) {
        const double delta = horizontal_delta != 0.0
            ? horizontal_delta : vertical_delta;
        if (delta != 0.0) {
            const ScrollEventType type = delta < 0.0
                ? ScrollEventType::small_decrement : ScrollEventType::small_increment;
            changed = precise
                ? apply_axis_input(ScrollOrientation::horizontal,
                                   scroll_position_.x + delta, type, false)
                : glide_axis(ScrollOrientation::horizontal,
                             glide_base(ScrollOrientation::horizontal) + delta, type, false);
        }
    }
    if (changed) event.handled = true;
    return changed;
}

// The wheel delta in logical pixels. A precise device's pixels are taken
// exactly, fractions included: multiplying a trackpad's few pixels by a
// line step made scrolling coarse and skip. Lines are 48 pixels each; a
// line count above 8 is a host reporting pixels without saying so, taken
// as pixels.
double ScrollableControl::normalize_wheel_delta(double delta, WheelDeltaUnit unit) noexcept {
    if (delta == 0.0) return 0.0;
    if (unit == WheelDeltaUnit::pixels) return delta;
    const double pixels = std::abs(delta) <= 8.0 ? delta * 48.0 : delta;
    return pixels;
}

void ScrollableControl::on_pointer(PointerEvent& event) {
    if (handle_scroll_pointer(event)) return;
    static_cast<void>(handle_wheel(event));
}

void ScrollableControl::on_pointer_bubble(PointerEvent& event) {
    // A move bubbling from a child is over the child, not over a bar.
    if (event.action == PointerAction::move && !dragging_orientation_ &&
        pressed_part_ == ScrollPart::none) {
        set_hot_part(ScrollPart::none);
    }
    static_cast<void>(handle_wheel(event));
}

void ScrollableControl::axis_properties_changed(
    ScrollOrientation orientation, bool position_changed) {
    require_mutable();
    const Point previous_position = scroll_position_;
    ScrollProperties& axis = orientation == ScrollOrientation::horizontal
        ? horizontal_scroll_ : vertical_scroll_;
    // An authored change of the axis ends its smooth scroll.
    stop_glide(orientation);
    if (position_changed) {
        axis.value_ = std::clamp(axis.value_, axis.minimum_,
                                 axis.maximum_position());
        if (orientation == ScrollOrientation::horizontal) {
            scroll_position_.x = axis.visible_ ? axis.value_ : 0.0;
        } else {
            scroll_position_.y = axis.visible_ ? axis.value_ : 0.0;
        }
    }
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test |
               Dirty::semantics | Dirty::accessibility);
    if (previous_position != scroll_position_) on_scroll_position_changed();
}

// An axis is a scroll bar's parts, drawn by the theme's engine as ScrollBar
// draws them: the trough, two arrow buttons with arrow marks and the thumb,
// all disabled when the axis is.
void ScrollableControl::paint_axis(Painter& painter,
                                   ScrollOrientation orientation,
                                   const AxisGeometry& geometry) const {
    if (geometry.bar.empty()) return;
    const bool horizontal = orientation == ScrollOrientation::horizontal;
    const ScrollProperties& axis = horizontal ? horizontal_scroll_ : vertical_scroll_;
    // The bar part a ScrollPart of this axis is, for the shared drawing.
    struct PartMap final {
        ScrollPart first;
        ScrollPart track_before;
        ScrollPart thumb;
        ScrollPart track_after;
        ScrollPart last;
    };
    const PartMap map = horizontal
        ? PartMap{ScrollPart::horizontal_first, ScrollPart::horizontal_track_before,
                  ScrollPart::horizontal_thumb, ScrollPart::horizontal_track_after,
                  ScrollPart::horizontal_last}
        : PartMap{ScrollPart::vertical_first, ScrollPart::vertical_track_before,
                  ScrollPart::vertical_thumb, ScrollPart::vertical_track_after,
                  ScrollPart::vertical_last};
    scroll_bar_detail::ScrollBarPaintParts parts;
    parts.orientation = horizontal ? Orientation::horizontal : Orientation::vertical;
    parts.bar = geometry.bar;
    parts.track = geometry.track;
    parts.decrement = geometry.first_button;
    parts.increment = geometry.last_button;
    parts.thumb = geometry.thumb;
    // The bar takes only the disabled state from its control (a scrollable
    // control's own states describe its content); the press and hover are
    // its own parts'.
    AnatomyStateSet bar_states{};
    bar_states.insert(axis.enabled_ ? AnatomyState::normal : AnatomyState::disabled);
    parts.states = bar_states;
    const bool dragging = dragging_orientation_ == orientation;
    if (dragging) {
        parts.pressed = ScrollBarPart::thumb;
    } else if (pressed_part_ == map.first) {
        parts.pressed = ScrollBarPart::decrement_button;
    } else if (pressed_part_ == map.last) {
        parts.pressed = ScrollBarPart::increment_button;
    } else if (pressed_part_ == map.track_before) {
        parts.pressed = ScrollBarPart::decrement_track;
    } else if (pressed_part_ == map.track_after) {
        parts.pressed = ScrollBarPart::increment_track;
    } else {
        parts.pressed = ScrollBarPart::none;
    }
    parts.thumb_captured = dragging;
    // A disabled axis shows no hover. (The pointer leaving this control,
    // onto a child or away, clears the hot part: handle_scroll_pointer.)
    if (!axis.enabled_) {
        parts.hot = ScrollBarPart::none;
    } else if (hot_part_ == map.first) {
        parts.hot = ScrollBarPart::decrement_button;
    } else if (hot_part_ == map.last) {
        parts.hot = ScrollBarPart::increment_button;
    } else if (hot_part_ == map.track_before) {
        parts.hot = ScrollBarPart::decrement_track;
    } else if (hot_part_ == map.track_after) {
        parts.hot = ScrollBarPart::increment_track;
    } else if (hot_part_ == map.thumb || (dragging && hot_part_ != ScrollPart::none)) {
        parts.hot = ScrollBarPart::thumb;
    } else {
        parts.hot = ScrollBarPart::none;
    }
    // Styled as the stock bar of the same orientation, so that a theme's
    // `v-scroll-bar::part(thumb)` rules draw both kinds of bar alike.
    parts.control_type = horizontal ? std::string_view{"h_scroll_bar"} : std::string_view{"v_scroll_bar"};
    parts.item = horizontal ? std::string_view{"horizontal"} : std::string_view{"vertical"};
    scroll_bar_detail::paint_scroll_bar_parts(painter, *this, parts);
}

void ScrollableControl::on_paint_overlay(Painter& painter, Rect) {
    paint_axis(painter, ScrollOrientation::horizontal,
               axis_geometry(ScrollOrientation::horizontal));
    paint_axis(painter, ScrollOrientation::vertical,
               axis_geometry(ScrollOrientation::vertical));
    if (hscroll() && vscroll()) {
        PartPaint corner;
        corner.family = ControlVisualRole::panel;
        corner.role = PartRole::face;
        corner.bounds = {viewport_rectangle_.width, viewport_rectangle_.height,
                         scrollbar_thickness_, scrollbar_thickness_};
        corner.corner_radius = 0.0;
        paint_part(painter, *this, corner);
    }
}

std::vector<SemanticNode>
ScrollableControl::semantic_virtual_children() const {
    std::vector<SemanticNode> result = Control::semantic_virtual_children();
    const Rect absolute = absolute_bounds();
    append_semantic_axis(result, absolute, ScrollOrientation::horizontal,
                         horizontal_scroll_);
    append_semantic_axis(result, absolute, ScrollOrientation::vertical,
                         vertical_scroll_);
    return result;
}

void ScrollableControl::append_semantic_axis(
    std::vector<SemanticNode>& nodes, Rect absolute,
    ScrollOrientation orientation, const ScrollProperties& axis) const {
    if (!axis.visible_) return;
    SemanticNode node;
    node.stable_id = std::string(stable_id().value()) +
        (orientation == ScrollOrientation::horizontal
             ? ".horizontal-scroll" : ".vertical-scroll");
    node.runtime_id = virtual_semantic_runtime_id(node.stable_id);
    node.role = SemanticRole::scroll_bar;
    node.name = orientation == ScrollOrientation::horizontal
        ? "Horizontal scroll bar" : "Vertical scroll bar";
    node.numeric_value = axis.value_;
    node.minimum_value = axis.minimum_;
    node.maximum_value = axis.maximum_position();
    const Rect local = axis_geometry(orientation).bar;
    node.bounds = {absolute.x + local.x, absolute.y + local.y,
                   local.width, local.height};
    node.states = SemanticState::visible;
    if (axis.enabled_ && effectively_enabled()) {
        node.states |= SemanticState::enabled;
        node.actions = {SemanticAction::increment,
                        SemanticAction::decrement,
                        SemanticAction::set_value};
    }
    nodes.push_back(std::move(node));
}

bool ScrollableControl::on_semantic_child_action(
    std::string_view child_stable_id, SemanticAction action,
    std::string_view value) {
    const std::string prefix(stable_id().value());
    std::optional<ScrollOrientation> orientation;
    if (child_stable_id == prefix + ".horizontal-scroll") {
        orientation = ScrollOrientation::horizontal;
    } else if (child_stable_id == prefix + ".vertical-scroll") {
        orientation = ScrollOrientation::vertical;
    } else {
        return Control::on_semantic_child_action(
            child_stable_id, action, value);
    }
    ScrollProperties& axis = *orientation == ScrollOrientation::horizontal
        ? horizontal_scroll_ : vertical_scroll_;
    if (!axis.visible_ || !axis.enabled_ || !effectively_enabled()) return false;
    double next = axis.value_;
    ScrollEventType type = ScrollEventType::thumb_position;
    if (action == SemanticAction::increment) {
        next += axis.small_change();
        type = ScrollEventType::small_increment;
    } else if (action == SemanticAction::decrement) {
        next -= axis.small_change();
        type = ScrollEventType::small_decrement;
    } else if (action == SemanticAction::set_value) {
        if (!scrolling_detail::parse_finite_number(value, next)) return false;
    } else {
        return false;
    }
    static_cast<void>(apply_axis_input(*orientation, next, type, true));
    return true;
}

namespace {

constexpr ControlAnatomy scrollable_control_anatomy{
    .control_id = "scrollable_control",
    .parts = scrollable_detail::parts,
    .states = scrollable_detail::states,
    .custom_states = {},
};

} // namespace

const ControlAnatomy& ScrollableControl::anatomy() const noexcept {
    return scrollable_control_anatomy;
}

ScrollableControl::Parts ScrollableControl::scrollable_parts() const {
    const Rect region = client_rectangle();
    const AxisGeometry horizontal = axis_geometry(ScrollOrientation::horizontal);
    const AxisGeometry vertical = axis_geometry(ScrollOrientation::vertical);
    Parts record;
    record.region = PartGeometry::from_bounds(region);
    record.viewport = PartGeometry::from_bounds(viewport_rectangle_);
    record.horizontal_decrement_arrow = PartGeometry::from_bounds(horizontal.first_button);
    record.horizontal_track = PartGeometry::from_bounds(horizontal.track);
    record.horizontal_thumb = PartGeometry::from_bounds(horizontal.thumb);
    record.horizontal_increment_arrow = PartGeometry::from_bounds(horizontal.last_button);
    record.vertical_decrement_arrow = PartGeometry::from_bounds(vertical.first_button);
    record.vertical_track = PartGeometry::from_bounds(vertical.track);
    record.vertical_thumb = PartGeometry::from_bounds(vertical.thumb);
    record.vertical_increment_arrow = PartGeometry::from_bounds(vertical.last_button);
    if (dragging_orientation_ == ScrollOrientation::horizontal) {
        record.horizontal_thumb.states.insert(AnatomyState::pressed);
        record.horizontal_thumb.states.insert(AnatomyState::captured);
    } else if (dragging_orientation_ == ScrollOrientation::vertical) {
        record.vertical_thumb.states.insert(AnatomyState::pressed);
        record.vertical_thumb.states.insert(AnatomyState::captured);
    } else {
        // No thumb is dragged.
    }
    // The held arrow or track side, and the part under the pointer.
    struct PartState final {
        ScrollPart part;
        PartGeometry* geometry;
    };
    const std::array<PartState, 10> mapped{{
        {ScrollPart::horizontal_first, &record.horizontal_decrement_arrow},
        {ScrollPart::horizontal_track_before, &record.horizontal_track},
        {ScrollPart::horizontal_thumb, &record.horizontal_thumb},
        {ScrollPart::horizontal_track_after, &record.horizontal_track},
        {ScrollPart::horizontal_last, &record.horizontal_increment_arrow},
        {ScrollPart::vertical_first, &record.vertical_decrement_arrow},
        {ScrollPart::vertical_track_before, &record.vertical_track},
        {ScrollPart::vertical_thumb, &record.vertical_thumb},
        {ScrollPart::vertical_track_after, &record.vertical_track},
        {ScrollPart::vertical_last, &record.vertical_increment_arrow},
    }};
    for (const PartState& entry : mapped) {
        if (entry.part == pressed_part_) (*entry.geometry).states.insert(AnatomyState::pressed);
        if (entry.part == hot_part_) (*entry.geometry).states.insert(AnatomyState::hot);
    }
    if (hscroll() && vscroll()) {
        const Rect corner{viewport_rectangle_.width, viewport_rectangle_.height,
                          scrollbar_thickness_, scrollbar_thickness_};
        record.scroll_corner = PartGeometry::from_bounds(corner);
    }
    record.focus = PartGeometry::from_bounds(region);
    return record;
}

void ScrollableControl::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Parts record = scrollable_parts();
    write_parts(anatomy(), scrollable_detail::part_fields, record, geometry);
}

AnatomyStateSet ScrollableControl::anatomy_states() const {
    AnatomyStateSet states = base_anatomy_states();
    if (dragging_orientation_.has_value()) {
        states.erase(AnatomyState::normal);
        states.insert(AnatomyState::pressed);
        states.insert(AnatomyState::captured);
    }
    return states;
}

ScrollableControl::Parts ScrollableControl::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), scrollable_detail::part_fields, published.parts(), record);
    return record;
}

ScrollableControl::States ScrollableControl::states() const {
    States record;
    read_states(scrollable_detail::state_fields, anatomy_states(), record);
    return record;
}

namespace {

// The events ScrollableControl adds to those of Control, in declaration order.
constexpr std::array<InteractionDeclaration, 1> scrollable_control_interactions{{
    {.name = "scroll", .payload = "ScrollEvent", .mutable_payload = true},
}};

} // namespace

void ScrollableControl::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Control::append_interactions(interactions);
    interactions.insert(interactions.end(), scrollable_control_interactions.begin(), scrollable_control_interactions.end());
}

ResizeAxes ScrollableControl::layout_resize_response() const noexcept {
    if (!auto_scroll.read()) return Control::layout_resize_response();
    if (children().empty()) return ResizeAxes::none;
    return ResizeAxes::both;
}

// property_codemod begin ScrollableControl (tools/property_families/scrollable_control.json; generated, do not edit)

namespace {

PropertyDescriptor scrollable_control_auto_scroll_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "auto_scroll";
    descriptor.kind = BindingValueKind::boolean;
    descriptor.category = "Layout";
    descriptor.description = "Whether scroll bars appear when content exceeds the client area.";
    descriptor.default_value = BindingValue{false};
    descriptor.invalidation_effects = Dirty::measure | Dirty::arrange | Dirty::paint;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_scrollable_control_auto_scroll() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        scrollable_control_auto_scroll_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor scrollable_control_auto_scroll_margin_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "auto_scroll_margin";
    descriptor.kind = BindingValueKind::size;
    descriptor.category = "Layout";
    descriptor.description = "Logical space kept around the content when scrolling; nonnegative.";
    descriptor.default_value = BindingValue{Size{}};
    descriptor.invalidation_effects = Dirty::measure | Dirty::arrange | Dirty::paint;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_scrollable_control_auto_scroll_margin() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        scrollable_control_auto_scroll_margin_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor scrollable_control_auto_scroll_min_size_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "auto_scroll_min_size";
    descriptor.kind = BindingValueKind::size;
    descriptor.category = "Layout";
    descriptor.description = "Smallest logical content size scrolling allows for; nonnegative.";
    descriptor.default_value = BindingValue{Size{}};
    descriptor.invalidation_effects = Dirty::measure | Dirty::arrange | Dirty::paint;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_scrollable_control_auto_scroll_min_size() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        scrollable_control_auto_scroll_min_size_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor scrollable_control_auto_scroll_position_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "auto_scroll_position";
    descriptor.kind = BindingValueKind::point;
    descriptor.category = "Layout";
    descriptor.description = "The scrolled content's offset, negated as Windows Forms reports it.";
    descriptor.default_value = BindingValue{Point{}};
    descriptor.invalidation_effects = Dirty::arrange | Dirty::paint;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_scrollable_control_auto_scroll_position() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        scrollable_control_auto_scroll_position_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

} // namespace

const std::array<PropertyEntry, ScrollableControl::scrollable_control_property_count>& ScrollableControl::scrollable_control_property_entries() noexcept {
    static constexpr std::array<PropertyEntry, 4U> own{{
        {"auto_scroll", &describe_scrollable_control_auto_scroll,
         &read_cell_entry<ScrollableControl, decltype(&ScrollableControl::auto_scroll), &ScrollableControl::auto_scroll>,
         &write_cell_entry<ScrollableControl, decltype(&ScrollableControl::auto_scroll), &ScrollableControl::auto_scroll>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"auto_scroll_margin", &describe_scrollable_control_auto_scroll_margin,
         &read_cell_entry<ScrollableControl, decltype(&ScrollableControl::auto_scroll_margin), &ScrollableControl::auto_scroll_margin>,
         &write_cell_entry<ScrollableControl, decltype(&ScrollableControl::auto_scroll_margin), &ScrollableControl::auto_scroll_margin>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"auto_scroll_min_size", &describe_scrollable_control_auto_scroll_min_size,
         &read_cell_entry<ScrollableControl, decltype(&ScrollableControl::auto_scroll_min_size), &ScrollableControl::auto_scroll_min_size>,
         &write_cell_entry<ScrollableControl, decltype(&ScrollableControl::auto_scroll_min_size), &ScrollableControl::auto_scroll_min_size>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"auto_scroll_position", &describe_scrollable_control_auto_scroll_position,
         &read_cell_entry<ScrollableControl, decltype(&ScrollableControl::auto_scroll_position), &ScrollableControl::auto_scroll_position>,
         &write_cell_entry<ScrollableControl, decltype(&ScrollableControl::auto_scroll_position), &ScrollableControl::auto_scroll_position>,
         nullptr, nullptr, nullptr,
         nullptr},
    }};
    static_assert(property_names_distinct(own), "ScrollableControl's property table names a property twice");
    static const std::array<PropertyEntry, scrollable_control_property_count> entries =
        join_property_tables(Control::control_property_entries(), own);
    return entries;
}

std::span<const PropertyEntry> ScrollableControl::property_table() const noexcept {
    const std::array<PropertyEntry, scrollable_control_property_count>& entries = scrollable_control_property_entries();
    return std::span<const PropertyEntry>(entries);
}

// property_codemod end ScrollableControl

} // namespace gui_forms
