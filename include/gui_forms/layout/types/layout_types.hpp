#pragma once

#include "gui_forms/binding/type_tag/type_tag.hpp"

#include "gui_forms/types/geometry/geometry.hpp"

#include <compare>
#include <cstdint>

namespace gui_forms {

// A layout length in logical units (device-independent pixels), the unit of
// every retained bound. Device pixels appear only where paint snaps edges, so
// a layout length is never added to a device-pixel quantity by accident.
class LogicalLength final {
public:
    explicit constexpr LogicalLength(double value = 0.0) noexcept : value_(value) {}
    [[nodiscard]] constexpr double value() const noexcept { return value_; }
    friend constexpr bool operator==(const LogicalLength& left,
                                     const LogicalLength& right) noexcept {
        return left.value_ == right.value_;
    }
    friend constexpr std::partial_ordering operator<=>(
        const LogicalLength& left, const LogicalLength& right) noexcept {
        return left.value_ <=> right.value_;
    }

private:
    double value_{};
};

// What a measure query asks of one axis (stage L, PLAN section 3.1).
enum class AvailableExtentKind : std::uint8_t {
    // Lay out to exactly this extent: a final or stretched size.
    definite,
    // Fit within this extent: the `available` of Control::measure(Size).
    at_most,
    // The narrowest the content can be without overflowing.
    min_content,
    // The content's size with no line breaking.
    max_content,
};

// One axis of a layout constraint. `extent` is read only for `definite` and
// `at_most`, and must then be finite and nonnegative; the content kinds
// carry a zero extent.
struct AvailableExtent final {
    AvailableExtentKind kind{AvailableExtentKind::at_most};
    LogicalLength extent{};

    friend constexpr bool operator==(const AvailableExtent& left,
                                     const AvailableExtent& right) noexcept {
        return left.kind == right.kind && left.extent == right.extent;
    }
};

// A measure query on both axes. Control::measure_for_layout answers it and
// caches the answer under exactly this value.
struct LayoutConstraint final {
    AvailableExtent width{};
    AvailableExtent height{};

    friend constexpr bool operator==(const LayoutConstraint& left,
                                     const LayoutConstraint& right) noexcept {
        return left.width == right.width && left.height == right.height;
    }
};

// The extent Control::measure(Size) and get_preferred_size(Size{}) already use
// for "unbounded", kept finite so retained state never holds an infinity.
inline constexpr double practical_unbounded_extent = 1.0e9;

[[nodiscard]] constexpr AvailableExtent definite_extent(LogicalLength extent) noexcept {
    return AvailableExtent{AvailableExtentKind::definite, extent};
}

[[nodiscard]] constexpr AvailableExtent at_most_extent(LogicalLength extent) noexcept {
    return AvailableExtent{AvailableExtentKind::at_most, extent};
}

[[nodiscard]] constexpr AvailableExtent min_content_extent() noexcept {
    return AvailableExtent{AvailableExtentKind::min_content, LogicalLength{}};
}

[[nodiscard]] constexpr AvailableExtent max_content_extent() noexcept {
    return AvailableExtent{AvailableExtentKind::max_content, LogicalLength{}};
}

// The query today's measure(Size available) answers: at most `available`
// on each axis.
[[nodiscard]] constexpr LayoutConstraint layout_constraint_at_most(Size available) noexcept {
    return LayoutConstraint{at_most_extent(LogicalLength{available.width}),
                            at_most_extent(LogicalLength{available.height})};
}

// True when every extent the constraint reads is finite and nonnegative and
// every content kind carries a zero extent.
[[nodiscard]] bool valid_layout_constraint(const LayoutConstraint& constraint) noexcept;

// The `available` argument of Control::measure(Size) for a constraint: a
// definite or at-most axis passes its extent, a content axis the practical
// unbounded extent.
[[nodiscard]] Size measure_available_size(const LayoutConstraint& constraint) noexcept;

// The inline direction a control lays its children and text out in (stage
// L, PLAN section 4): Windows Forms' RightToLeft with Inherit, CSS
// `direction`. A control left at `inherit` takes its parent's effective
// direction; a control with no parent is left to right.
enum class LayoutDirection : std::uint8_t {
    inherit,
    left_to_right,
    right_to_left,
};

// Windows Forms' RightToLeft values, in its order (No, Yes, Inherit): the
// vocabulary of Control's `right_to_left` property, which names the same
// authored value as `layout_direction` (owner ruling of 2026-11-11). There
// is one stored direction; these convert between the two spellings.
enum class RightToLeft : std::uint8_t {
    no,
    yes,
    inherit,
};

// The RightToLeft spelling of an authored direction. A value outside the
// enumeration reads as inherit.
[[nodiscard]] constexpr RightToLeft right_to_left_of(LayoutDirection direction) noexcept {
    switch (direction) {
    case LayoutDirection::left_to_right: return RightToLeft::no;
    case LayoutDirection::right_to_left: return RightToLeft::yes;
    case LayoutDirection::inherit:
    default: return RightToLeft::inherit;
    }
}

// The authored direction a RightToLeft value stands for. A value outside
// the enumeration reads as inherit.
[[nodiscard]] constexpr LayoutDirection layout_direction_of(RightToLeft value) noexcept {
    switch (value) {
    case RightToLeft::no: return LayoutDirection::left_to_right;
    case RightToLeft::yes: return LayoutDirection::right_to_left;
    case RightToLeft::inherit:
    default: return LayoutDirection::inherit;
    }
}

// `bounds` mirrored about the vertical centre line of `frame`, both in the
// same coordinates: how a directional control places a part laid out left
// to right when it resolves right to left (Control::
// effective_layout_direction). Mirroring twice gives `bounds` back.
[[nodiscard]] constexpr Rect horizontally_mirrored(Rect bounds, Rect frame) noexcept {
    Rect mirrored = bounds;
    mirrored.x = frame.x + frame.width - (bounds.x - frame.x) - bounds.width;
    return mirrored;
}

namespace types {

inline constexpr TypeTag<LayoutDirection> layout_direction{};

} // namespace types

} // namespace gui_forms
