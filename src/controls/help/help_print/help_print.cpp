#include "gui_forms/controls/help_print.hpp"

#include "gui_forms/theme/part_painting/part_painting.hpp"

#include <algorithm>
#include <string_view>

namespace gui_forms {
namespace {

constexpr double header_text_size = 12.0;
// The header and footer text's baseline above the band's foot.
constexpr double header_baseline_inset = 8.0;
constexpr double header_title_share = 0.5;

// `format` with "{0}" replaced by `number` and "{1}" by `count`.
void format_footer(std::string_view format, std::size_t number, std::size_t count, std::string& text) {
    text.clear();
    const std::string number_text = std::to_string(number);
    const std::string count_text = std::to_string(count);
    std::size_t index = 0U;
    while (index < format.size()) {
        if (format.compare(index, 3U, "{0}") == 0) {
            text.append(number_text);
            index += 3U;
        } else if (format.compare(index, 3U, "{1}") == 0) {
            text.append(count_text);
            index += 3U;
        } else {
            text.push_back(format[index]);
            ++index;
        }
    }
}

void paint_sheet_text(Painter& painter, const HelpBook& book, std::string_view text, Point baseline, double width,
                      std::string_view key) {
    if (text.empty()) return;
    PartPaint part{};
    part.family = ControlVisualRole::tool_tip;
    part.role = PartRole::label;
    part.font = book.text_font(FontSpec{FontRole::control, header_text_size, 400, false});
    part.text = lend_paint_text(text);
    part.tone = PartTextTone::secondary;
    part.key = PartKey{std::string(key), {}, {}};
    paint_text_part_at(painter, book, part, baseline, width);
}

} // namespace

void plan_help_print(HelpBook& book, const HelpPrintOptions& options, HelpPrintPlan& plan) {
    plan.sheets.clear();
    plan.content_width = std::max(0.0, options.sheet.width - 2.0 * options.margin);
    const double band = std::max(
        1.0, options.sheet.height - 2.0 * options.margin - options.header_height - options.footer_height);
    plan.content_height = book.layout_for_print(plan.content_width);
    std::vector<double> breaks;
    book.print_break_positions(breaks);
    // Greedy: each sheet ends at the last line top that fits; a line taller
    // than the band is cut at the band's foot.
    double top = 0.0;
    std::size_t next = 0U;
    while (top < plan.content_height) {
        const double limit = top + band;
        double end = std::min(limit, plan.content_height);
        if (limit < plan.content_height) {
            double best = top;
            while (next < breaks.size() && breaks[next] <= limit) {
                if (breaks[next] > top) best = breaks[next];
                ++next;
            }
            if (best > top) end = best;
        }
        plan.sheets.push_back(HelpPrintSheet{top, end - top});
        top = end;
    }
    if (plan.sheets.empty()) plan.sheets.push_back(HelpPrintSheet{0.0, 0.0});
}

void paint_help_print_sheet(Painter& painter, const HelpBook& book, const HelpPrintPlan& plan, std::size_t sheet,
                            const HelpPrintOptions& options, Point origin) {
    if (sheet >= plan.sheets.size()) return;
    const double left = origin.x + options.margin;
    const double width = plan.content_width;
    const double header_top = origin.y + options.margin;
    const double header_baseline = header_top + options.header_height - header_baseline_inset;
    // The header: the program at the left, the page's title at the right.
    const double half = width * header_title_share;
    paint_sheet_text(painter, book, options.program_title, Point{left, header_baseline}, half, "print_program");
    paint_sheet_text(painter, book, book.page().title, Point{left + half, header_baseline}, width - half,
                     "print_title");
    PartPaint rule{};
    rule.family = ControlVisualRole::tool_tip;
    rule.role = PartRole::divider;
    rule.bounds = Rect{left, header_top + options.header_height - 1.0, width, 1.0};
    rule.key = PartKey{"print_rule", {}, {}};
    paint_part(painter, book, rule);
    // The content band.
    const HelpPrintSheet& band = plan.sheets[sheet];
    book.paint_print_band(painter, Point{left, header_top + options.header_height}, band.top, band.height);
    // The footer.
    std::string footer;
    format_footer(options.footer_format, sheet + 1U, plan.sheets.size(), footer);
    const double footer_baseline = origin.y + options.sheet.height - options.margin - header_baseline_inset;
    paint_sheet_text(painter, book, footer, Point{left, footer_baseline}, width, "print_footer");
}

} // namespace gui_forms
