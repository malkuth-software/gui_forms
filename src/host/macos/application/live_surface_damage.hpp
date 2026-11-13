#pragma once

#include "../../../core/damage/device_damage/device_damage.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace gui_forms::detail {

// Only whole device pixels inside the actual hard clip are guaranteed covered.
// Outward rounding a clip would incorrectly erase a fractional overlay gap.
[[nodiscard]] Rect opaque_live_clip(const Rect clip, const double scale) noexcept;

// Clips have already been rounded inward by opaque_live_clip. Sweep their
// vertical edges, extending each horizontal interval only through overlapping
// coverage. No bounding-box approximation, pixel loop or scratch allocation.
[[nodiscard]] bool opaque_live_damage_covers(
    const Rect dirty, const double scale, const std::span<const Rect> clips,
    const bool reusable_raster, const bool retained_damage) noexcept;

// Fragments beyond this fall back to the whole dirty rectangle: retained
// painting more than needed is slower, never wrong.
inline constexpr std::size_t maximum_uncovered_live_fragments = 64U;

// Appends the parts of one dirty rectangle that no opaque live clip covers,
// such as an overlay floating over a live surface. Clips come from
// opaque_live_clip, so only whole covered device pixels are subtracted.
void append_uncovered_live_damage(const Rect dirty, const double scale,
                                  const std::span<const Rect> clips,
                                  std::vector<Rect>& uncovered);

// On a raster that still holds the last presentation, the rectangles AppKit
// hands drawRect: are copy requests: AppKit coalesces the host's
// setNeedsDisplayInRect: calls (into one rectangle on a layer-backed view),
// and outside the host's own damage the raster already holds the frame's
// pixels (stage DP decision 2: compose the gathered set, then copy). Only
// where a live surface is composed (a frame that is not opaque, or an overlay
// above one) do they still need retained painting. Appends to `kept` the
// parts of each `requested` rectangle inside one of `destinations`.
void append_within_live_destinations(const std::span<const Rect> requested,
                                     const std::span<const Rect> destinations,
                                     std::vector<Rect>& kept);

} // namespace gui_forms::detail
