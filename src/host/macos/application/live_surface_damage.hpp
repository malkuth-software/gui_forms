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
[[nodiscard]] inline Rect opaque_live_clip(const Rect clip, const double scale) noexcept {
    if (!clip.finite() || clip.empty() || !std::isfinite(scale) || scale <= 0.0) return {};
    const double left = std::ceil(clip.x * scale) / scale;
    const double top = std::ceil(clip.y * scale) / scale;
    const double right = std::floor(clip.right() * scale) / scale;
    const double bottom = std::floor(clip.bottom() * scale) / scale;
    const Rect result{left, top, right - left, bottom - top};
    if (!result.finite() || result.empty()) return {};
    return result;
}

// Clips have already been rounded inward by opaque_live_clip. Sweep their
// vertical edges, extending each horizontal interval only through overlapping
// coverage. No bounding-box approximation, pixel loop or scratch allocation.
[[nodiscard]] inline bool opaque_live_damage_covers(
    const Rect dirty, const double scale, const std::span<const Rect> clips,
    const bool reusable_raster, const bool retained_damage) noexcept {
    if (!reusable_raster || retained_damage || clips.empty() || !dirty.finite()) return false;
    const Rect target = align_damage_outward(dirty, scale);
    if (target.empty() || !target.finite()) return false;
    double top = target.y;
    while (top < target.bottom()) {
        double bottom = target.bottom();
        for (const Rect clip : clips) {
            if (clip.y > top) bottom = std::min(bottom, clip.y);
            if (clip.bottom() > top) bottom = std::min(bottom, clip.bottom());
        }
        double left = target.x;
        while (left < target.right()) {
            double right = left;
            for (const Rect clip : clips) {
                if (clip.y <= top && clip.bottom() >= bottom && clip.x <= left) {
                    right = std::max(right, clip.right());
                }
            }
            if (right <= left) return false;
            left = right;
        }
        top = bottom;
    }
    return true;
}

// Fragments beyond this fall back to the whole dirty rectangle: retained
// painting more than needed is slower, never wrong.
inline constexpr std::size_t maximum_uncovered_live_fragments = 64U;

// Appends the parts of one dirty rectangle that no opaque live clip covers,
// such as an overlay floating over a live surface. Clips come from
// opaque_live_clip, so only whole covered device pixels are subtracted.
inline void append_uncovered_live_damage(const Rect dirty, const double scale,
                                         const std::span<const Rect> clips,
                                         std::vector<Rect>& uncovered) {
    const Rect target = align_damage_outward(dirty, scale);
    if (target.empty() || !target.finite()) return;
    std::vector<Rect> fragments{target};
    for (const Rect clip : clips) {
        std::vector<Rect> remaining;
        for (const Rect fragment : fragments) {
            const Rect overlap = Rect::intersection(fragment, clip);
            if (overlap.empty()) {
                remaining.push_back(fragment);
                continue;
            }
            const Rect above{fragment.x, fragment.y, fragment.width, overlap.y - fragment.y};
            const Rect below{fragment.x, overlap.bottom(), fragment.width, fragment.bottom() - overlap.bottom()};
            const Rect left{fragment.x, overlap.y, overlap.x - fragment.x, overlap.height};
            const Rect right{overlap.right(), overlap.y, fragment.right() - overlap.right(), overlap.height};
            if (!above.empty()) remaining.push_back(above);
            if (!below.empty()) remaining.push_back(below);
            if (!left.empty()) remaining.push_back(left);
            if (!right.empty()) remaining.push_back(right);
        }
        if (remaining.size() > maximum_uncovered_live_fragments) {
            uncovered.push_back(target);
            return;
        }
        fragments = std::move(remaining);
        if (fragments.empty()) return;
    }
    uncovered.insert(uncovered.end(), fragments.begin(), fragments.end());
}

// On a raster that still holds the last presentation, the rectangles AppKit
// hands drawRect: are copy requests: AppKit coalesces the host's
// setNeedsDisplayInRect: calls (into one rectangle on a layer-backed view),
// and outside the host's own damage the raster already holds the frame's
// pixels (stage DP decision 2: compose the gathered set, then copy). Only
// where a live surface is composed (a frame that is not opaque, or an overlay
// above one) do they still need retained painting. Appends to `kept` the
// parts of each `requested` rectangle inside one of `destinations`.
inline void append_within_live_destinations(const std::span<const Rect> requested,
                                            const std::span<const Rect> destinations,
                                            std::vector<Rect>& kept) {
    for (const Rect rectangle : requested) {
        for (const Rect destination : destinations) {
            const Rect inside = Rect::intersection(rectangle, destination);
            if (!inside.empty()) kept.push_back(inside);
        }
    }
}

} // namespace gui_forms::detail
