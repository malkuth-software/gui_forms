#pragma once

// Radial layout (NODE_ENGINE_AND_VALUE_BAR_STUDY 1A, piece C21): items at
// equal angles around a centre, as a pie menu places them, the pick a
// pointer's direction makes, and the direction ring that shows it.
//
// Angles are degrees measured clockwise from +x in y-down coordinates (the
// painter's), so -90 is straight up. Directions come from the engine's own
// sine, cosine and arctangent, so every host places and picks alike. The
// layout is a value: placing fills caller storage, picking is a pure
// function of the layout and the pointer, and nothing allocates.

#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace gui_forms {

class Painter;

// The most items one radial layout places (a pie of more stops being
// pickable by direction alone).
inline constexpr std::size_t maximum_radial_items = 16U;

// Where each item's box sits about its anchor, the point `radius` from the
// centre in the item's direction.
enum class RadialItemAlignment : std::uint8_t {
    // The box is centred on the anchor.
    centred,
    // The box's edge nearest the centre passes through the anchor, so a
    // label grows away from the centre (left of it on the left side, below
    // it at the bottom); an axis the direction barely leans along stays
    // centred.
    outward,
};

struct RadialLayout final {
    Point center{};
    // From the centre to each anchor; finite and positive.
    double radius{};
    // Each item's box; finite and non-negative.
    Size item_size{};
    // The first item's direction; finite. The rest follow clockwise.
    double start_degrees{-90.0};
    // 1 to maximum_radial_items.
    std::uint32_t count{};
    RadialItemAlignment alignment{RadialItemAlignment::centred};
};

// One placed item: its box, its anchor and direction, and the sector of
// directions that picks it, from `sweep_start` to `sweep_start + sweep`
// degrees (sweep = 360 / count, centred on the direction).
struct RadialSlot final {
    Rect bounds{};
    Point anchor{};
    double direction_degrees{};
    double sweep_start{};
    double sweep{};

    friend constexpr bool operator==(const RadialSlot& left, const RadialSlot& right) noexcept {
        return left.bounds == right.bounds && left.anchor == right.anchor &&
               left.direction_degrees == right.direction_degrees && left.sweep_start == right.sweep_start &&
               left.sweep == right.sweep;
    }
};

[[nodiscard]] bool valid_radial_layout(const RadialLayout& layout) noexcept;

// Fills the first `layout.count` entries of `slots` and returns that count;
// 0, writing nothing, when the layout is not valid or `slots` is shorter.
std::size_t place_radial_items(const RadialLayout& layout, std::span<RadialSlot> slots) noexcept;

// What a pointer picks: nothing within `dead_zone` of the centre (or for a
// layout that is not valid, or a point or dead zone that is not finite),
// otherwise the item whose sector holds the pointer's direction. A
// direction on a boundary picks the item clockwise of it.
struct RadialPick final {
    bool picked{};
    std::uint32_t index{};
    double direction_degrees{};

    friend constexpr bool operator==(const RadialPick& left, const RadialPick& right) noexcept {
        return left.picked == right.picked && left.index == right.index &&
               left.direction_degrees == right.direction_degrees;
    }
};

[[nodiscard]] RadialPick pick_radial_item(const RadialLayout& layout, Point pointer, double dead_zone) noexcept;

// The direction ring: an annulus between `inner_radius` and `outer_radius`
// (finite, 0 <= inner < outer) about the layout's centre in `track`, with
// the picked item's sector of it in `pick`.
struct DirectionRingStyle final {
    double inner_radius{};
    double outer_radius{};
    Color track{};
    Color pick{};
};

// The most arc steps one sector's outline takes (6 degrees each, so a
// sector of a two-item pie, half a turn, takes 30).
inline constexpr std::size_t maximum_ring_arc_steps = 30U;

// Paints the ring: the track as one round stroke, then, when `pick` picked
// an item, its sector as one polygon (the whole annulus for a one-item
// layout, as a second round stroke). An invalid layout or style paints
// nothing.
void paint_direction_ring(Painter& painter, const RadialLayout& layout, const RadialPick& pick,
                          const DirectionRingStyle& style);

} // namespace gui_forms
