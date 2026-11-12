#pragma once

#include "gui_forms/control.hpp"

#include <cstdint>
#include <optional>
#include <string_view>

namespace gui_forms {

enum class ScrollEventType : std::uint8_t {
    small_decrement = 0,
    small_increment = 1,
    large_decrement = 2,
    large_increment = 3,
    thumb_position = 4,
    thumb_track = 5,
    first = 6,
    last = 7,
    end_scroll = 8,
};

enum class ScrollOrientation : std::uint8_t {
    horizontal = 0,
    vertical = 1,
};

struct ScrollEvent final {
    ScrollEventType type{ScrollEventType::thumb_position};
    double old_value{};
    double new_value{};
    ScrollOrientation orientation{ScrollOrientation::horizontal};
};

struct ScrollAxisSnapshot final {
    bool enabled{true};
    bool visible{};
    double minimum{};
    double maximum{100.0};
    double large_change{10.0};
    double small_change{1.0};
    double value{};
};

struct ScrollSnapshot final {
    bool auto_scroll{};
    Point position{};
    Size margin{};
    Size minimum_content_size{};
    Rect display_rectangle{};
    Rect viewport_rectangle{};
    ScrollAxisSnapshot horizontal;
    ScrollAxisSnapshot vertical;
};

class ScrollableControl;

class ScrollProperties final {
public:
    [[nodiscard]] bool enabled() const noexcept { return enabled_; }
    void set_enabled(bool enabled);
    [[nodiscard]] bool visible() const noexcept { return visible_; }
    void set_visible(bool visible);
    [[nodiscard]] double minimum() const noexcept { return minimum_; }
    void set_minimum(double minimum);
    [[nodiscard]] double maximum() const noexcept { return maximum_; }
    void set_maximum(double maximum);
    [[nodiscard]] double large_change() const noexcept;
    void set_large_change(double value);
    [[nodiscard]] double small_change() const noexcept;
    void set_small_change(double value);
    [[nodiscard]] double value() const noexcept { return value_; }
    void set_value(double value);
    [[nodiscard]] ScrollOrientation orientation() const noexcept {
        return orientation_;
    }
    [[nodiscard]] ScrollAxisSnapshot snapshot() const noexcept;

private:
    friend class ScrollableControl;
    ScrollProperties(ScrollableControl& owner, ScrollOrientation orientation)
        : owner_(&owner), orientation_(orientation) {}
    void set_automatic(double viewport_extent, double content_extent,
                       double value) noexcept;
    [[nodiscard]] double maximum_position() const noexcept;

    ScrollableControl* owner_{};
    ScrollOrientation orientation_{ScrollOrientation::horizontal};
    double minimum_{};
    double maximum_{100.0};
    double large_change_{10.0};
    double small_change_{1.0};
    double value_{};
    bool enabled_{true};
    bool visible_{};
    bool large_change_authored_{};
    bool small_change_authored_{};
};

} // namespace gui_forms
