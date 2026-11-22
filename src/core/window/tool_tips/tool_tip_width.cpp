#include "tool_tip_width.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace gui_forms::detail {
namespace {

// The widest share of the even split a candidate also tries: greedy
// wrapping fills lines short of the split, so the even split often needs a
// line more than it was chosen for.
constexpr double wrap_slack = 1.12;

// Adds `width` to `candidates` unless it is within half a unit of one
// there or the array is full.
void add_candidate(ToolTipWrapCandidates& candidates, double width) noexcept {
    if (candidates.count >= candidates.widths.size()) return;
    for (std::size_t index = 0U; index < candidates.count; ++index) {
        if (std::abs(candidates.widths[index] - width) < 0.5) return;
    }
    candidates.widths[candidates.count] = width;
    ++candidates.count;
}

} // namespace

ToolTipWrapRange tool_tip_wrap_range(double font_size, double cap, double room) noexcept {
    double maximum = cap;
    if (room > 0.0) maximum = std::min(maximum, room);
    maximum = std::max(maximum, 1.0);
    const double em = std::max(font_size, 1.0);
    const double one_line = std::min(em * tool_tip_one_line_ems, maximum);
    const double minimum = std::min(em * tool_tip_minimum_wrap_ems, one_line);
    const ToolTipWrapRange range{minimum, one_line, maximum};
    return range;
}

ToolTipWrapCandidates tool_tip_wrap_candidates(const ToolTipWrapInput& input) noexcept {
    ToolTipWrapCandidates candidates{};
    const ToolTipWrapRange range = input.range;
    if (input.widest_width <= range.one_line || input.total_width <= 0.0 || input.line_height <= 0.0) {
        add_candidate(candidates, range.one_line);
        return candidates;
    }
    // golden * line_height * n^2 + (golden * chrome.height - chrome.width) * n - total_width = 0.
    const double a = tool_tip_golden_ratio * input.line_height;
    const double b = tool_tip_golden_ratio * input.chrome.height - input.chrome.width;
    const double c = -input.total_width;
    const double root = (-b + std::sqrt(b * b - 4.0 * a * c)) / (2.0 * a);
    const double below = std::max(1.0, std::floor(root));
    const double first = std::max(1.0, below - 1.0);
    for (double lines = first; lines <= below + 2.0; lines += 1.0) {
        const double even = input.total_width / lines;
        add_candidate(candidates, std::clamp(even, range.minimum, range.maximum));
        add_candidate(candidates, std::clamp(even * wrap_slack, range.minimum, range.maximum));
    }
    return candidates;
}

double tool_tip_aspect_error(Size face) noexcept {
    if (!(face.width > 0.0) || !(face.height > 0.0)) return std::numeric_limits<double>::infinity();
    const double ratio = face.width / (tool_tip_golden_ratio * face.height);
    const double error = std::abs(std::log(ratio));
    return error;
}

} // namespace gui_forms::detail
