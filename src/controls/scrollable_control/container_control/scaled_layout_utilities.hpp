#pragma once

#include "gui_forms/control.hpp"
#include "gui_forms/controls/scaled_layout_values.hpp"
#include "gui_forms/layout/viewport_fit/viewport_fit.hpp"

#include <cmath>
#include <stxlib/optional.hpp>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace gui_forms::scaled_layout_detail {

struct SlotForRemovedChild final {
    const std::unordered_set<std::uint64_t>* retained{};

    [[nodiscard]] bool operator()(
        const std::pair<const std::uint64_t, Rect>& entry) const {
        return !(*retained).contains(entry.first);
    }
};

// The design space a scaled container maps from: Control::design_size,
// or 1 x 1 while that is zero (the unit square, the containers' default
// before design_size moved to Control).
[[nodiscard]] inline Size effective_design_size(Size design_size) noexcept {
    const bool unset = design_size.width <= 0.0 || design_size.height <= 0.0;
    const Size space = unset ? Size{1.0, 1.0} : design_size;
    return space;
}

// The smaller axis ratio of the stretch from `design_size` to `arranged`:
// what a scaled container reports as its applied viewport scale (stage VA),
// in either aspect (a uniform aspect applies exactly that scale).
// None while design_size is zero or the arranged extent is empty.
[[nodiscard]] inline stxlib::Optional<double> stretch_scale(Size design_size, Rect arranged) noexcept {
    const bool unset = design_size.width <= 0.0 || design_size.height <= 0.0;
    const bool empty = arranged.width <= 0.0 || arranged.height <= 0.0;
    if (unset || empty) return stxlib::Optional<double>::none();
    const double ratio_x = arranged.width / design_size.width;
    const double ratio_y = arranged.height / design_size.height;
    const double ratio = ratio_x < ratio_y ? ratio_x : ratio_y;
    const stxlib::Optional<double> scale = stxlib::Optional<double>::of(ratio);
    return scale;
}

inline void validate_design_bounds(Rect bounds) {
    if (!std::isfinite(bounds.x) || !std::isfinite(bounds.y) ||
        !std::isfinite(bounds.width) || !std::isfinite(bounds.height) ||
        bounds.width < 0.0 || bounds.height < 0.0) {
        throw std::invalid_argument(
            "scaled layout bounds must be finite and nonnegative");
    }
}

template <typename Parent>
void reconcile_slots(Parent& parent,
                     std::unordered_map<std::uint64_t, Rect>& slots) {
    std::unordered_set<std::uint64_t> retained;
    retained.reserve(parent.children().size());
    for (const Control::Ptr& child : parent.children()) {
        retained.insert((*child).runtime_id().value);
    }
    std::erase_if(slots, SlotForRemovedChild{&retained});
}

// How design coordinates map into a scaled container of `final_bounds`'
// extent (its own coordinates, from its top left): a scale per axis and an
// offset. A stretch takes each axis' ratio; a uniform aspect (stage VA,
// decision 10) takes the visible-area guarantee's contain of the design
// space (place_viewport_content: one scale, the smaller ratio, unbounded
// below and up to maximum_viewport_scale), aligned by `alignment`.
struct DesignMap final {
    double scale_x{1.0};
    double scale_y{1.0};
    Point offset{};
};

[[nodiscard]] inline DesignMap design_map(Size design_space, Rect final_bounds, ScaledAspect aspect,
                                          ContentAlignment alignment) noexcept {
    DesignMap map{};
    if (aspect == ScaledAspect::uniform) {
        const ViewportFit fit{OverflowAnswer::contain, 0.0, maximum_viewport_scale, OverflowAnswer::scroll, alignment};
        const Rect extent{0.0, 0.0, final_bounds.width, final_bounds.height};
        const ViewportPlacement placed = place_viewport_content(design_space, extent, fit);
        map = DesignMap{placed.scale, placed.scale, Point{placed.destination.x, placed.destination.y}};
        return map;
    }
    map.scale_x = final_bounds.width / design_space.width;
    map.scale_y = final_bounds.height / design_space.height;
    return map;
}

[[nodiscard]] inline Rect child_bounds(const DesignMap& map, Rect bounds) noexcept {
    const Rect placed{map.offset.x + bounds.x * map.scale_x, map.offset.y + bounds.y * map.scale_y,
                      bounds.width * map.scale_x, bounds.height * map.scale_y};
    return placed;
}

} // namespace gui_forms::scaled_layout_detail
