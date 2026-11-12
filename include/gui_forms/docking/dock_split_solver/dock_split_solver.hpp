#pragma once

#include "gui_forms/export/application_export.hpp"
#include <cstdint>

namespace gui_forms {

// DK's own small split solver (DK decisions 5 and 18), which stage L may
// adopt or replace. It divides one span between the two children of a split.
struct GUI_FORMS_API DockSplitSizes final {
    double first{};
    double second{};
    // False when the span is smaller than the two minimums together; the
    // children are then sized in proportion to their minimums and the caller
    // decides what to collapse (DK decision 23).
    bool minimums_met{true};

    friend bool operator==(const DockSplitSizes& left, const DockSplitSizes& right) noexcept {
        return left.first == right.first && left.second == right.second &&
               left.minimums_met == right.minimums_met;
    }
};

// span is the length shared by the two children (any seam already
// subtracted); ratio is the first child's share, in (0, 1); the minimums are
// at least zero. The first child gets span * ratio, clamped so that each
// child keeps its minimum. first + second equals span exactly. A span that is
// negative or not finite gives two zero sizes; a ratio outside (0, 1) or not
// finite is treated as 0.5.
[[nodiscard]] GUI_FORMS_API DockSplitSizes solve_dock_split(double span, double ratio, double first_minimum,
                                              double second_minimum) noexcept;

} // namespace gui_forms
