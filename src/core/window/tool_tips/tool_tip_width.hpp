#pragma once

#include "gui_forms/types/geometry/geometry.hpp"

#include <array>
#include <cstddef>

namespace gui_forms::detail {

// Every tooltip's width follows its text (owner, 2026-11-22), the plain
// tooltip's bubble and the RibbonToolTip's alike. Text whose widest
// paragraph fits the one-line width stays on one line per paragraph.
// Longer text wraps to the candidate width whose face comes nearest a
// golden-ratio box (width = 1.618 times height); the caller breaks the
// text at each candidate, measures the face and keeps the nearest
// (tool_tip_aspect_error). Every width stays between the readable minimum
// and the maximum (the program's ToolTipSettings::maximum_width and the
// room the work area leaves); a maximum below the others wins.

inline constexpr double tool_tip_golden_ratio = 1.6180339887498949;
// The widest text that stays on one line, and the narrowest a longer text
// wraps to, in ems of the tooltip's text font.
inline constexpr double tool_tip_one_line_ems = 24.0;
inline constexpr double tool_tip_minimum_wrap_ems = 16.0;
inline constexpr std::size_t maximum_tool_tip_wrap_candidates = 8U;

// The widths a tooltip's text may wrap to, in logical units:
// 0 < minimum <= one_line <= maximum.
struct ToolTipWrapRange final {
    double minimum{};
    double one_line{};
    double maximum{};
};

// The wrap range for text in a font of `font_size` logical units, below
// the program's `cap` and the `room` the work area leaves for the text
// (both in logical units; a room that is not positive is ignored).
[[nodiscard]] ToolTipWrapRange tool_tip_wrap_range(double font_size, double cap, double room) noexcept;

// What the rule reads about one tooltip, in logical units: its paragraphs'
// unwrapped widths summed and the widest of them, one line's height, what
// the face adds round the text block (margins, an image column), and the
// wrap range.
struct ToolTipWrapInput final {
    double total_width{};
    double widest_width{};
    double line_height{};
    Size chrome{};
    ToolTipWrapRange range{};
};

// The widths to try, each within the range and none within half a unit of
// another; `count` of `widths` are live, at least one.
struct ToolTipWrapCandidates final {
    std::array<double, maximum_tool_tip_wrap_candidates> widths{};
    std::size_t count{};
};

// One candidate, the range's one-line width, when the widest paragraph
// fits in it (the text stays on one line per paragraph). Otherwise the widths that
// spread the text over the line counts round the golden solution: n lines
// of total_width / n each give a face of (total_width / n + chrome.width)
// by (n * line_height + chrome.height); setting the first to 1.618 times
// the second gives a quadratic in n. For the two counts either side of its
// root and one more each way, the even split and the split 12 percent
// wider (greedy wrapping leaves lines short), each clamped to the range.
[[nodiscard]] ToolTipWrapCandidates tool_tip_wrap_candidates(const ToolTipWrapInput& input) noexcept;

// How far `face` is from the golden box: the absolute natural logarithm of
// width / (1.618 * height); 0 for the golden box, and the same for a face
// too wide as for one too tall by the same factor. Infinite for an empty
// face.
[[nodiscard]] double tool_tip_aspect_error(Size face) noexcept;

} // namespace gui_forms::detail
