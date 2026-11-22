#pragma once

#include "gui_forms/control.hpp"

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
// what a scaled container reports as its applied viewport scale (stage VA).
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

[[nodiscard]] inline Rect child_bounds(Size design_size, Rect bounds,
                                       Rect final_bounds) {
    const double scale_x = final_bounds.width / design_size.width;
    const double scale_y = final_bounds.height / design_size.height;
    return {bounds.x * scale_x, bounds.y * scale_y,
            bounds.width * scale_x, bounds.height * scale_y};
}

} // namespace gui_forms::scaled_layout_detail
