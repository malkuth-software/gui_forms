#include "gui_forms/controls/scrollable_control/scrollable_control.hpp"
#include "gui_forms/detail/property_binding_adapters.hpp"
#include "scrolling_utilities.hpp"
#include "scrollable_anatomy.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"

#include <array>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>

namespace gui_forms {

using namespace scrolling_detail;

ScrollableControl::ScrollableControl(StableId stable_id)
    : Control(std::move(stable_id)),
      horizontal_scroll_(*this, ScrollOrientation::horizontal),
      vertical_scroll_(*this, ScrollOrientation::vertical) {
    PropertyDescriptor auto_scroll;
    auto_scroll.name = "auto_scroll";
    auto_scroll.kind = BindingValueKind::boolean;
    auto_scroll.category = "Layout";
    auto_scroll.description = "Whether scroll bars appear when content exceeds the client area.";
    auto_scroll.default_value = BindingValue{false};
    auto_scroll.invalidation_effects = Dirty::measure | Dirty::arrange | Dirty::paint;
    auto_scroll.bindable = false;
    define_bindable_property({
        std::move(auto_scroll),
        detail::BindingNoexceptMethodGetter<ScrollableControl, bool>(*this, &ScrollableControl::auto_scroll),
        detail::ConvertedPropertySetter<ScrollableControl, bool>(
            *this, &ScrollableControl::set_auto_scroll, BindingValueKind::boolean,
            "ScrollableControl.AutoScroll requires a Boolean"),
        {}, {}, {}});

    PropertyDescriptor auto_scroll_margin;
    auto_scroll_margin.name = "auto_scroll_margin";
    auto_scroll_margin.kind = BindingValueKind::size;
    auto_scroll_margin.category = "Layout";
    auto_scroll_margin.description = "Logical space kept around the content when scrolling; nonnegative.";
    auto_scroll_margin.default_value = BindingValue{Size{}};
    auto_scroll_margin.invalidation_effects = Dirty::measure | Dirty::arrange | Dirty::paint;
    auto_scroll_margin.bindable = false;
    define_bindable_property({
        std::move(auto_scroll_margin),
        detail::BindingNoexceptMethodGetter<ScrollableControl, Size>(*this,
                                                                     &ScrollableControl::auto_scroll_margin),
        detail::ConvertedPropertySetter<ScrollableControl, Size>(
            *this, &ScrollableControl::set_auto_scroll_margin, BindingValueKind::size,
            "ScrollableControl.AutoScrollMargin requires a size"),
        {}, {}, {}});

    PropertyDescriptor auto_scroll_min_size;
    auto_scroll_min_size.name = "auto_scroll_min_size";
    auto_scroll_min_size.kind = BindingValueKind::size;
    auto_scroll_min_size.category = "Layout";
    auto_scroll_min_size.description = "Smallest logical content size scrolling allows for; nonnegative.";
    auto_scroll_min_size.default_value = BindingValue{Size{}};
    auto_scroll_min_size.invalidation_effects = Dirty::measure | Dirty::arrange | Dirty::paint;
    auto_scroll_min_size.bindable = false;
    define_bindable_property({
        std::move(auto_scroll_min_size),
        detail::BindingNoexceptMethodGetter<ScrollableControl, Size>(*this,
                                                                     &ScrollableControl::auto_scroll_min_size),
        detail::ConvertedPropertySetter<ScrollableControl, Size>(
            *this, &ScrollableControl::set_auto_scroll_min_size, BindingValueKind::size,
            "ScrollableControl.AutoScrollMinSize requires a size"),
        {}, {}, {}});

    PropertyDescriptor auto_scroll_position;
    auto_scroll_position.name = "auto_scroll_position";
    auto_scroll_position.kind = BindingValueKind::point;
    auto_scroll_position.category = "Layout";
    auto_scroll_position.description = "The scrolled content's offset, negated as Windows Forms reports it.";
    auto_scroll_position.default_value = BindingValue{Point{}};
    auto_scroll_position.invalidation_effects = Dirty::arrange | Dirty::paint;
    auto_scroll_position.bindable = false;
    define_bindable_property({
        std::move(auto_scroll_position),
        detail::BindingNoexceptMethodGetter<ScrollableControl, Point>(*this,
                                                                      &ScrollableControl::auto_scroll_position),
        detail::ConvertedPropertySetter<ScrollableControl, Point>(
            *this, &ScrollableControl::set_auto_scroll_position, BindingValueKind::point,
            "ScrollableControl.AutoScrollPosition requires a point"),
        {}, {}, {}});
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

void ScrollableControl::set_auto_scroll(bool enabled) {
    require_mutable();
    if (auto_scroll_ == enabled) return;
    const Point previous_position = scroll_position_;
    auto_scroll_ = enabled;
    if (enabled) scroll_state_ |= scroll_state_auto_scrolling;
    else {
        scroll_state_ &= ~scroll_state_auto_scrolling;
        scroll_position_ = {};
        horizontal_scroll_.value_ = horizontal_scroll_.minimum_;
        vertical_scroll_.value_ = vertical_scroll_.minimum_;
    }
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test |
               Dirty::semantics | Dirty::accessibility);
    if (previous_position != scroll_position_) on_scroll_position_changed();
}

void ScrollableControl::set_auto_scroll_margin(Size margin) {
    require_mutable();
    validate_size(margin,
                  "AutoScrollMargin must be finite and nonnegative");
    if (auto_scroll_margin_ == margin) return;
    auto_scroll_margin_ = margin;
    if (auto_scroll_) {
        invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test |
                   Dirty::semantics | Dirty::accessibility);
    }
}

void ScrollableControl::set_auto_scroll_margin(double x, double y) {
    if (!std::isfinite(x) || !std::isfinite(y)) {
        throw std::invalid_argument("AutoScrollMargin must be finite");
    }
    set_auto_scroll_margin(Size{std::max(0.0, x), std::max(0.0, y)});
}

void ScrollableControl::set_auto_scroll_min_size(Size size) {
    require_mutable();
    validate_size(size,
                  "AutoScrollMinSize must be finite and nonnegative");
    if (auto_scroll_min_size_ == size) return;
    auto_scroll_min_size_ = size;
    if (!auto_scroll_) {
        auto_scroll_ = true;
        scroll_state_ |= scroll_state_auto_scrolling;
    }
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test |
               Dirty::semantics | Dirty::accessibility);
}

void ScrollableControl::set_auto_scroll_position(Point position) {
    require_mutable();
    if (!std::isfinite(position.x) || !std::isfinite(position.y)) {
        throw std::invalid_argument("AutoScrollPosition must be finite");
    }
    static_cast<void>(scroll_to(
        {std::max(0.0, position.x), std::max(0.0, position.y)}));
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
    if (!auto_scroll_ && !horizontal_scroll_.visible_ &&
        !vertical_scroll_.visible_) {
        return client_rectangle();
    }
    return viewport_rectangle_;
}

void ScrollableControl::set_hscroll(bool visible) {
    require_mutable();
    if (auto_scroll_) return;
    horizontal_scroll_.set_visible(visible);
}

void ScrollableControl::set_vscroll(bool visible) {
    require_mutable();
    if (auto_scroll_) return;
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
    if ((bit & scroll_state_auto_scrolling) != 0U) set_auto_scroll(value);
    if ((bit & scroll_state_hscroll_visible) != 0U) set_hscroll(value);
    if ((bit & scroll_state_vscroll_visible) != 0U) set_vscroll(value);
    const std::uint32_t passive = bit &
        (scroll_state_user_has_scrolled | scroll_state_full_drag);
    if (value) scroll_state_ |= passive;
    else scroll_state_ &= ~passive;
}

ScrollSnapshot ScrollableControl::scroll_snapshot() const noexcept {
    return {auto_scroll_, scroll_position_, auto_scroll_margin_,
            auto_scroll_min_size_, display_rectangle(),
            effective_viewport_rectangle(),
            horizontal_scroll_.snapshot(), vertical_scroll_.snapshot()};
}

Size ScrollableControl::content_extent() {
    Size extent{
        std::max(0.0, auto_scroll_min_size_.width),
        std::max(0.0, auto_scroll_min_size_.height)};
    const Insets own_padding = padding();
    const std::vector<Control::Ptr> retained = snapshot_layout_children();
    for (const Control::Ptr& child : retained) {
        if (!is_current_layout_child(child) || !(*child).visible() ||
            (*child).dock() != DockStyle::none) continue;
        Rect bounds = (*child).requested_bounds();
        if ((*child).auto_size()) {
            const Size preferred = (*child).get_preferred_size({0.0, 0.0});
            if (!is_alive()) return extent;
            if (!is_current_layout_child(child) || !(*child).visible() ||
                (*child).dock() != DockStyle::none) {
                continue;
            }
            bounds.width = (*child).auto_size_mode() == AutoSizeMode::grow_only
                ? std::max(bounds.width, preferred.width) : preferred.width;
            bounds.height = (*child).auto_size_mode() == AutoSizeMode::grow_only
                ? std::max(bounds.height, preferred.height) : preferred.height;
        }
        const Insets child_margin = (*child).margin();
        extent.width = std::max(
            extent.width,
            std::max(0.0, bounds.x) + bounds.width + child_margin.right +
                auto_scroll_margin_.width + own_padding.right);
        extent.height = std::max(
            extent.height,
            std::max(0.0, bounds.y) + bounds.height + child_margin.bottom +
                auto_scroll_margin_.height + own_padding.bottom);
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

    content_extent_ = {std::max(auto_scroll_min_size_.width, measured_content.width),
                       std::max(auto_scroll_min_size_.height, measured_content.height)};
    if (!is_alive()) return;
    const double width = std::max(0.0, client_size.width);
    const double height = std::max(0.0, client_size.height);
    bool h = auto_scroll_ ? false : horizontal_scroll_.visible_;
    bool v = auto_scroll_ ? false : vertical_scroll_.visible_;
    if (auto_scroll_) {
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

    if (auto_scroll_) {
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
    if (previous_position != scroll_position_) on_scroll_position_changed();
}

void ScrollableControl::adjust_scrollbars(bool display_scrollbars) {
    if (!display_scrollbars) {
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
    arrange_self(final_bounds);
    recompute_scroll_layout({final_bounds.width, final_bounds.height});
    if (!is_alive()) return;
    if (auto_scroll_) adjust_scrollbars(true);
    Control::arrange(final_bounds);
    if (scroll_position_ == Point{}) return;
    // Right to left, the base arrange mirrored the slots, so scrolling the
    // logical content toward the left moves them to the right.
    const bool mirrored = effective_layout_direction() == LayoutDirection::right_to_left;
    for (const Control::Ptr& child : children()) {
        if (!child || !(*child).is_alive() || !(*child).visible() ||
            (*child).dock() != DockStyle::none) continue;
        // The base arrange just assigned the unscrolled slot; offset that, not
        // the committed bounds, which still carry the previous scroll offset.
        Rect bounds = (*child).pending_layout_bounds();
        bounds.x += mirrored ? scroll_position_.x : -scroll_position_.x;
        bounds.y -= scroll_position_.y;
        set_child_layout(child, bounds);
    }
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
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test |
               Dirty::semantics | Dirty::accessibility);
    on_scroll_position_changed();
    if (notify) notify_scroll(orientation, type, old_value, next);
    return true;
}

bool ScrollableControl::scroll_to(Point position, ScrollEventType type,
                                  bool notify) {
    if (!std::isfinite(position.x) || !std::isfinite(position.y)) {
        throw std::invalid_argument("scroll position must be finite");
    }
    const bool horizontal = apply_axis_value(
        ScrollOrientation::horizontal, position.x, type, notify);
    const bool vertical = apply_axis_value(
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
    const bool horizontal = apply_axis_value(
        ScrollOrientation::horizontal, scroll_position_.x + delta.x,
        horizontal_type, notify);
    const bool vertical = apply_axis_value(
        ScrollOrientation::vertical, scroll_position_.y + delta.y,
        vertical_type, notify);
    return horizontal || vertical;
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
    if (bounds.x - auto_scroll_margin_.width < next.x) {
        next.x = bounds.x - auto_scroll_margin_.width;
    } else if (bounds.right() + auto_scroll_margin_.width >
               next.x + viewport_rectangle_.width) {
        next.x = bounds.right() + auto_scroll_margin_.width -
                 viewport_rectangle_.width;
    }
    if (bounds.y - auto_scroll_margin_.height < next.y) {
        next.y = bounds.y - auto_scroll_margin_.height;
    } else if (bounds.bottom() + auto_scroll_margin_.height >
               next.y + viewport_rectangle_.height) {
        next.y = bounds.bottom() + auto_scroll_margin_.height -
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
    if (!control || !contains(*control) || !auto_scroll_ ||
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
    AxisGeometry geometry;
    if (orientation == ScrollOrientation::horizontal) {
        if (!horizontal_scroll_.visible_) return geometry;
        geometry.bar = {0.0, viewport_rectangle_.height,
                        viewport_rectangle_.width, scrollbar_thickness_};
        const double button = std::min(scrollbar_button_extent_,
                                       geometry.bar.width * 0.5);
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
        const double button = std::min(scrollbar_button_extent_,
                                       geometry.bar.height * 0.5);
        geometry.first_button = {geometry.bar.x, geometry.bar.y,
                                 geometry.bar.width, button};
        geometry.last_button = {geometry.bar.x,
                                geometry.bar.bottom() - button,
                                geometry.bar.width, button};
        geometry.track = {geometry.bar.x, geometry.first_button.bottom(),
                          geometry.bar.width,
                          std::max(0.0, geometry.bar.height - button * 2.0)};
    }
    const ScrollProperties& axis = orientation == ScrollOrientation::horizontal
        ? horizontal_scroll_ : vertical_scroll_;
    const double track_extent = orientation == ScrollOrientation::horizontal
        ? geometry.track.width : geometry.track.height;
    const double viewport_extent = orientation == ScrollOrientation::horizontal
        ? viewport_rectangle_.width : viewport_rectangle_.height;
    const double content = orientation == ScrollOrientation::horizontal
        ? content_extent_.width : content_extent_.height;
    const double thumb_extent = std::min(
        track_extent, std::max(minimum_thumb_extent_,
            content <= 0.0 ? track_extent : track_extent * viewport_extent / content));
    const double travel = std::max(0.0, track_extent - thumb_extent);
    const double maximum = maximum_offset(orientation);
    const double offset = maximum <= 0.0 ? 0.0 : travel * axis.value_ / maximum;
    geometry.thumb = orientation == ScrollOrientation::horizontal
        ? Rect{geometry.track.x + offset, geometry.track.y,
               thumb_extent, geometry.track.height}
        : Rect{geometry.track.x, geometry.track.y + offset,
               geometry.track.width, thumb_extent};
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
            static_cast<void>(apply_axis_value(*dragging_orientation_, next,
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
            event.handled = true;
            return true;
        }
        event.handled = true;
        return true;
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
        dragging_orientation_ = orientation;
        dragging_pointer_id_ = event.pointer_id == 0U ? 1U : event.pointer_id;
        dragging_origin_ = local;
        dragging_start_value_ = axis.value_;
        set_pointer_capture(true);
    } else {
        double next = axis.value_;
        ScrollEventType type = ScrollEventType::small_increment;
        if (part == ScrollPart::horizontal_first ||
            part == ScrollPart::vertical_first) {
            next -= axis.small_change();
            type = ScrollEventType::small_decrement;
        } else if (part == ScrollPart::horizontal_last ||
                   part == ScrollPart::vertical_last) {
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
        static_cast<void>(apply_axis_value(orientation, next, type, true));
    }
    event.handled = true;
    return true;
}

bool ScrollableControl::handle_wheel(PointerEvent& event) {
    if (event.action != PointerAction::wheel || event.handled) return false;
    const double vertical_delta = -normalize_wheel_delta(event.wheel_delta.y);
    const double horizontal_delta = -normalize_wheel_delta(event.wheel_delta.x);
    bool changed{};
    if (vertical_scroll_.visible_ && vertical_scroll_.enabled_ &&
        vertical_delta != 0.0) {
        changed = apply_axis_value(
            ScrollOrientation::vertical,
            scroll_position_.y + vertical_delta,
            vertical_delta < 0.0 ? ScrollEventType::small_decrement
                                 : ScrollEventType::small_increment,
            false);
    }
    if (!changed && horizontal_scroll_.visible_ &&
        horizontal_scroll_.enabled_) {
        const double delta = horizontal_delta != 0.0
            ? horizontal_delta : vertical_delta;
        if (delta != 0.0) {
            changed = apply_axis_value(
                ScrollOrientation::horizontal,
                scroll_position_.x + delta,
                delta < 0.0 ? ScrollEventType::small_decrement
                            : ScrollEventType::small_increment,
                false);
        }
    }
    if (changed) event.handled = true;
    return changed;
}

double ScrollableControl::normalize_wheel_delta(double delta) noexcept {
    if (delta == 0.0) return 0.0;
    return std::abs(delta) <= 8.0 ? delta * 48.0 : delta;
}

void ScrollableControl::on_pointer(PointerEvent& event) {
    if (handle_scroll_pointer(event)) return;
    static_cast<void>(handle_wheel(event));
}

void ScrollableControl::on_pointer_bubble(PointerEvent& event) {
    static_cast<void>(handle_wheel(event));
}

void ScrollableControl::axis_properties_changed(
    ScrollOrientation orientation, bool position_changed) {
    require_mutable();
    const Point previous_position = scroll_position_;
    ScrollProperties& axis = orientation == ScrollOrientation::horizontal
        ? horizontal_scroll_ : vertical_scroll_;
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
    const ScrollProperties& axis = orientation == ScrollOrientation::horizontal
        ? horizontal_scroll_ : vertical_scroll_;
    AnatomyStateSet states{};
    if (!axis.enabled_) states.insert(AnatomyState::disabled);
    PartPaint trough;
    trough.family = ControlVisualRole::panel;
    trough.role = PartRole::track;
    trough.bounds = geometry.bar;
    trough.states = states;
    paint_part(painter, *this, trough);
    const bool horizontal = orientation == ScrollOrientation::horizontal;
    const std::array<Rect, 2> buttons{{geometry.first_button, geometry.last_button}};
    for (std::size_t index = 0U; index < buttons.size(); ++index) {
        const Rect button = buttons[index];
        if (button.empty()) continue;
        PartPaint actuator;
        actuator.family = ControlVisualRole::button;
        actuator.role = PartRole::actuator;
        actuator.bounds = button;
        actuator.states = states;
        paint_part(painter, *this, actuator);
        // An 8-unit square holds the arrow's four rows.
        const double center_x = std::floor(button.x + button.width * 0.5);
        const double center_y = std::floor(button.y + button.height * 0.5);
        PartPaint arrow;
        arrow.family = ControlVisualRole::button;
        arrow.role = PartRole::glyph;
        arrow.mark = horizontal ? (index == 0U ? PartMark::arrow_left : PartMark::arrow_right)
                                : (index == 0U ? PartMark::arrow_up : PartMark::arrow_down);
        arrow.bounds = {center_x - 4.0, center_y - 4.0, 8.0, 8.0};
        arrow.states = states;
        paint_part(painter, *this, arrow);
    }
    PartPaint thumb;
    thumb.family = ControlVisualRole::button;
    thumb.role = PartRole::thumb;
    thumb.bounds = geometry.thumb;
    thumb.states = states;
    paint_part(painter, *this, thumb);
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
    static_cast<void>(apply_axis_value(*orientation, next, type, true));
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

} // namespace gui_forms
