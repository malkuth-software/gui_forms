#include "gui_forms/controls/scrollable_control/scroll_properties/scroll_properties.hpp"
#include "../scrolling_utilities.hpp"
#include "gui_forms/controls/scrollable_control/scrollable_control.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>

namespace gui_forms {

void ScrollProperties::set_enabled(bool enabled) {
    if (owner_ == nullptr || (*owner_).auto_scroll.read()) return;
    if (enabled_ == enabled) return;
    enabled_ = enabled;
    (*owner_).axis_properties_changed(orientation_, false);
}

void ScrollProperties::set_visible(bool visible) {
    if (owner_ == nullptr || (*owner_).auto_scroll.read()) return;
    if (visible_ == visible) return;
    visible_ = visible;
    (*owner_).axis_properties_changed(orientation_, false);
}

void ScrollProperties::set_minimum(double minimum) {
    ScrollableControl::validate_axis_value(
        minimum, "scroll minimum must be finite and nonnegative");
    if (minimum < 0.0) {
        throw std::invalid_argument(
            "scroll minimum must be finite and nonnegative");
    }
    if (owner_ != nullptr && (*owner_).auto_scroll.read()) return;
    if (minimum_ == minimum) return;
    minimum_ = minimum;
    maximum_ = std::max(maximum_, minimum_);
    value_ = std::clamp(value_, minimum_, maximum_);
    (*owner_).axis_properties_changed(orientation_, true);
}

void ScrollProperties::set_maximum(double maximum) {
    ScrollableControl::validate_axis_value(
        maximum, "scroll maximum must be finite");
    if (owner_ != nullptr && (*owner_).auto_scroll.read()) return;
    if (maximum_ == maximum) return;
    maximum_ = maximum;
    if (minimum_ > maximum_) minimum_ = maximum_;
    value_ = std::clamp(value_, minimum_, maximum_);
    (*owner_).axis_properties_changed(orientation_, true);
}

double ScrollProperties::large_change() const noexcept {
    return std::min(large_change_, std::max(0.0, maximum_ - minimum_ + 1.0));
}

void ScrollProperties::set_large_change(double value) {
    ScrollableControl::validate_axis_value(
        value, "scroll large change must be finite and nonnegative");
    if (value < 0.0) {
        throw std::invalid_argument(
            "scroll large change must be finite and nonnegative");
    }
    if (large_change_ == value) return;
    large_change_ = value;
    large_change_authored_ = true;
    (*owner_).axis_properties_changed(orientation_, true);
}

double ScrollProperties::small_change() const noexcept {
    return std::min(small_change_, large_change());
}

void ScrollProperties::set_small_change(double value) {
    ScrollableControl::validate_axis_value(
        value, "scroll small change must be finite and nonnegative");
    if (value < 0.0) {
        throw std::invalid_argument(
            "scroll small change must be finite and nonnegative");
    }
    if (small_change_ == value) return;
    small_change_ = value;
    small_change_authored_ = true;
    (*owner_).axis_properties_changed(orientation_, false);
}

void ScrollProperties::set_value(double value) {
    ScrollableControl::validate_axis_value(value,
                                            "scroll value must be finite");
    if (value < minimum_ || value > maximum_) {
        throw std::out_of_range("scroll value is outside Minimum/Maximum");
    }
    if (owner_ == nullptr) {
        value_ = value;
        return;
    }
    static_cast<void>((*owner_).apply_axis_input(
        orientation_, value, ScrollEventType::thumb_position, false));
}

ScrollAxisSnapshot ScrollProperties::snapshot() const noexcept {
    return {enabled_, visible_, minimum_, maximum_, large_change(),
            small_change(), value_};
}

double ScrollProperties::maximum_position() const noexcept {
    return std::max(minimum_, maximum_ - large_change() + 1.0);
}

void ScrollProperties::set_automatic(double viewport_extent,
                                     double content_extent,
                                     double value) noexcept {
    minimum_ = 0.0;
    maximum_ = std::max(0.0, std::ceil(content_extent) - 1.0);
    if (!large_change_authored_) large_change_ = std::max(0.0, viewport_extent);
    if (!small_change_authored_) {
        small_change_ = std::min(48.0, std::max(1.0, viewport_extent * 0.1));
    }
    visible_ = scrolling_detail::approximately_greater(content_extent, viewport_extent);
    enabled_ = true;
    value_ = std::clamp(value, minimum_, maximum_position());
}

} // namespace gui_forms
