// The opaque live-surface coverage rules (live_surface_damage.hpp).

#include "live_surface_damage.hpp"

#include <algorithm>
#include <cmath>

namespace gui_forms::detail {

Rect opaque_live_clip(const Rect clip, const double scale) noexcept {
    if (!clip.finite() || clip.empty() || !std::isfinite(scale) || scale <= 0.0) return {};
    const double left = std::ceil(clip.x * scale) / scale;
    const double top = std::ceil(clip.y * scale) / scale;
    const double right = std::floor(clip.right() * scale) / scale;
    const double bottom = std::floor(clip.bottom() * scale) / scale;
    const Rect result{left, top, right - left, bottom - top};
    if (!result.finite() || result.empty()) return {};
    return result;
}

bool opaque_live_damage_covers(
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

void append_uncovered_live_damage(const Rect dirty, const double scale,
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

void append_within_live_destinations(const std::span<const Rect> requested,
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
