#pragma once

#include "gui_forms/types/paint_marks/paint_marks.hpp"

#include <cstdint>

namespace gui_forms::detail {

// The most lines of one direction a grid fill draws; a fill that would draw
// more along either axis is refused.
inline constexpr std::int64_t maximum_grid_lines = 65536;

// The indices of the grid lines along one axis whose stroke, `half_width`
// either side of `origin + index * spacing`, reaches into [low, high]:
// `first` through `last` inclusive. `count` is 0 when none does or when the
// range would hold more than maximum_grid_lines.
struct GridLineRange final {
    std::int64_t first{};
    std::int64_t last{};
    std::int64_t count{};
};

// Inputs as the painter validated them: finite, `spacing` at least
// minimum_grid_spacing, `half_width` non-negative.
[[nodiscard]] GridLineRange grid_line_range(double low, double high, double origin, double spacing,
                                            double half_width) noexcept;

// Whether line `index` is a major line of a grid whose majors fall every
// `major_every` lines from the origin (index 0 is always major).
[[nodiscard]] bool grid_line_is_major(std::int64_t index, std::uint32_t major_every) noexcept;

} // namespace gui_forms::detail
