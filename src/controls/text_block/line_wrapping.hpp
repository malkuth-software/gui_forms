#pragma once

// Greedy line wrapping shared by TextBlock and DataGridView's wrapped cells:
// the text's lines at a width, from its line-break opportunities.

#include "gui_forms/control.hpp"

#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

namespace gui_forms::line_wrapping_detail {

// One line: a byte range of the text, its trailing spaces and line end
// excluded, and the range's measured width.
struct WrappedLine final {
    std::size_t begin_byte{};
    std::size_t end_byte{};
    double width{};
};

// A line holds a run that is at most this much wider than its width:
// float noise from adding and subtracting padding, well under Chromium's
// 1/64 px layout unit.
inline constexpr double wrap_fit_tolerance = 1.0 / 1024.0;

// The end of a line's content: trailing spaces hang and are not drawn or
// measured, as CSS's `white-space: normal` treats them, and a line's own
// line end (CR, LF) is not text.
[[nodiscard]] inline std::size_t wrapped_content_end(std::string_view text, std::size_t begin,
                                                     std::size_t end) noexcept {
    while (end > begin && (text[end - 1U] == ' ' || text[end - 1U] == '\n' || text[end - 1U] == '\r')) --end;
    return end;
}

// Measures a run in one control's text engine and one (already effective)
// font: its logical width.
struct ControlRunWidth final {
    const Control* control{};
    FontSpec font{};

    double operator()(std::string_view run) const {
        if (run.empty()) return 0.0;
        const ResolvedTextLayout layout = (*control).resolve_text_layout_utf8(run, font);
        return layout.logical_size.width;
    }
};

// Fills `lines` with `text` wrapped greedily at `width`, as CSS fills lines:
// each line takes the longest run up to a break opportunity that fits, a run
// with no opportunity inside it that is wider than the line overflows on a
// line of its own (`overflow-wrap: normal`), and a mandatory opportunity (a
// line end) always ends the line, an empty one included. `opportunities`
// holds the opportunity after each byte of `text`; when its length differs,
// the text is one line. `measure` (ControlRunWidth or a test's) gives a
// run's width; each candidate is measured whole, so a line of w words costs
// w measurements. `lines` is cleared first and its storage reused; `Line`
// is WrappedLine or a record with the same three fields in order.
template <typename Line, typename Measure>
void wrap_lines_greedy(std::string_view text, std::span<const LineBreakOpportunity> opportunities, double width,
                       const Measure& measure, std::vector<Line>& lines) {
    lines.clear();
    const std::size_t size = text.size();
    if (size == 0U) return;
    if (opportunities.size() != size) {
        const std::size_t end = wrapped_content_end(text, 0U, size);
        lines.push_back(Line{0U, end, measure(text.substr(0U, end))});
        return;
    }
    std::size_t line_begin = 0U;
    bool has_fit = false;
    Line fitted{};
    std::size_t fitted_next = 0U;
    for (std::size_t position = 1U; position <= size; ++position) {
        const bool last = position == size;
        const LineBreakOpportunity opportunity = opportunities[position - 1U];
        const bool forced = opportunity == LineBreakOpportunity::mandatory;
        if (!last && !forced && opportunity != LineBreakOpportunity::allowed) continue;
        const std::size_t end = wrapped_content_end(text, line_begin, position);
        const double run = measure(text.substr(line_begin, end - line_begin));
        if (!has_fit || run <= width + wrap_fit_tolerance) {
            has_fit = true;
            fitted = Line{line_begin, end, run};
            fitted_next = position;
        } else {
            lines.push_back(fitted);
            line_begin = fitted_next;
            const std::size_t next_end = wrapped_content_end(text, line_begin, position);
            const double next_run = measure(text.substr(line_begin, next_end - line_begin));
            fitted = Line{line_begin, next_end, next_run};
            fitted_next = position;
        }
        if (forced && !last) {
            lines.push_back(fitted);
            line_begin = position;
            has_fit = false;
        }
    }
    if (has_fit) lines.push_back(fitted);
}

} // namespace gui_forms::line_wrapping_detail
