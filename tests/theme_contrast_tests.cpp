// The contrast report (stage T4 phase 1; design language 5.7): the WCAG
// arithmetic, compositing, which pairs are gated for which family and
// state, and the report of the built-in theme. With
// GUI_FORMS_CONTRAST_REPORT=<path> it writes the built-in theme's report as
// tab-separated rows there, for the stage record.

#include "../src/core/theme/contrast/theme_contrast.hpp"
#include "support/theme_definition_heap.hpp"
#include "gui_forms/theme.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {
using namespace gui_forms;
using gui_forms::detail::ContrastBlend;
using gui_forms::detail::ContrastCounts;
using gui_forms::detail::ContrastPair;
using gui_forms::detail::ContrastRow;
using gui_forms::detail::ContrastVerdict;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

bool near(double value, double expected, double tolerance) {
    return std::abs(value - expected) <= tolerance;
}

void wcag_arithmetic() {
    const Color black = Color::rgba(0, 0, 0);
    const Color white = Color::rgba(255, 255, 255);
    require(near(detail::relative_luminance(white), 1.0, 1e-12), "white has luminance 1");
    require(near(detail::relative_luminance(black), 0.0, 1e-12), "black has luminance 0");
    require(near(detail::contrast_ratio(black, white), 21.0, 1e-9), "black on white is 21:1");
    require(near(detail::contrast_ratio(white, black), 21.0, 1e-9), "the ratio is symmetric");
    require(near(detail::contrast_ratio(white, white), 1.0, 1e-12), "a colour on itself is 1:1");
    // #767676 is the lightest grey that passes 4.5:1 on white; #777777 fails.
    require(detail::contrast_ratio(Color::rgba(0x76, 0x76, 0x76), white) >= 4.5, "#767676 passes 4.5 on white");
    require(detail::contrast_ratio(Color::rgba(0x77, 0x77, 0x77), white) < 4.5, "#777777 fails 4.5 on white");
    // Alpha is ignored by the ratio.
    require(near(detail::contrast_ratio(Color::rgba(0, 0, 0, 10), white), 21.0, 1e-9), "the ratio ignores alpha");
}

void compositing() {
    const Color black = Color::rgba(0, 0, 0);
    const Color white = Color::rgba(255, 255, 255);
    const Color half_white = Color::rgba(255, 255, 255, 128);
    const Color normal = detail::composite_over(half_white, black, ContrastBlend::normal);
    require(normal.red == 128 && normal.green == 128 && normal.blue == 128 && normal.alpha == 255,
            "half white over black is opaque mid grey");
    const Color opaque = detail::composite_over(Color::rgba(10, 20, 30), white, ContrastBlend::normal);
    require(opaque.red == 10 && opaque.green == 20 && opaque.blue == 30, "an opaque colour covers its ground");
    const Color clear = detail::composite_over(Color::rgba(10, 20, 30, 0), white, ContrastBlend::normal);
    require(clear.red == 255 && clear.alpha == 255, "a clear colour leaves its ground");
    const Color multiplied = detail::composite_over(Color::rgba(128, 255, 0), Color::rgba(200, 200, 200),
                                                    ContrastBlend::multiply);
    require(multiplied.red == 100 && multiplied.green == 200 && multiplied.blue == 0, "multiply darkens");
    const Color screened = detail::composite_over(Color::rgba(128, 0, 255), Color::rgba(100, 100, 100),
                                                  ContrastBlend::screen);
    require(screened.red == 178 && screened.green == 100 && screened.blue == 255, "screen lightens");
}

std::size_t count_rows(const std::vector<ContrastRow>& rows, ControlVisualRole family, ControlSurfaceState state,
                       bool selected, bool high_contrast, ContrastPair pair) {
    std::size_t count = 0U;
    for (const ContrastRow& row : rows) {
        if (row.family == family && row.state == state && row.selected == selected &&
            row.high_contrast == high_contrast && row.pair == pair) {
            ++count;
        }
    }
    return count;
}

const ContrastRow* find_row(const std::vector<ContrastRow>& rows, ControlVisualRole family,
                            ControlSurfaceState state, bool high_contrast, ContrastPair pair) {
    for (const ContrastRow& row : rows) {
        if (row.family == family && row.state == state && !row.selected && row.high_contrast == high_contrast &&
            row.pair == pair) {
            return &row;
        }
    }
    return nullptr;
}
// The row is borrowed from `rows`, so `rows` is never a temporary.
const ContrastRow* find_row(std::vector<ContrastRow>&& rows, ControlVisualRole family, ControlSurfaceState state,
                            bool high_contrast, ContrastPair pair) = delete;

// Which pairs each family gates, and the minimum each row carries.
void gated_pairs(const std::vector<ContrastRow>& rows) {
    for (const bool high_contrast : std::array<bool, 2>{false, true}) {
        for (const bool selected : std::array<bool, 2>{false, true}) {
            const ControlVisualRole button = ControlVisualRole::button;
            const ControlSurfaceState normal = ControlSurfaceState::normal;
            const ControlSurfaceState disabled = ControlSurfaceState::disabled;
            require(count_rows(rows, button, normal, selected, high_contrast, ContrastPair::text) == 1U,
                    "a button gates its text");
            require(count_rows(rows, button, normal, selected, high_contrast, ContrastPair::boundary) == 1U,
                    "a button gates its boundary");
            require(count_rows(rows, button, normal, selected, high_contrast, ContrastPair::focus_outer) == 1U,
                    "a button gates its focus ring");
            require(count_rows(rows, button, disabled, selected, high_contrast, ContrastPair::text) == 1U,
                    "a disabled button gates its text");
            require(count_rows(rows, button, disabled, selected, high_contrast, ContrastPair::boundary) == 0U,
                    "a disabled button gates no boundary");
            require(count_rows(rows, ControlVisualRole::menu_item, normal, selected, high_contrast,
                               ContrastPair::boundary) == 0U,
                    "a resting menu item is flat");
            require(count_rows(rows, ControlVisualRole::menu_item, ControlSurfaceState::hot, selected,
                               high_contrast, ContrastPair::boundary) == 1U,
                    "a hot menu item gates its boundary");
            require(count_rows(rows, ControlVisualRole::panel, normal, selected, high_contrast,
                               ContrastPair::focus_inner) == 0U,
                    "a panel gates no focus ring");
        }
    }
    for (const ContrastRow& row : rows) {
        double expected = 3.0;
        const bool text = row.pair == ContrastPair::text || row.pair == ContrastPair::muted_text;
        if (text && row.high_contrast) expected = 7.0;
        if (text && !row.high_contrast && row.state != ControlSurfaceState::disabled) expected = 4.5;
        require(row.minimum == expected, "each row carries its pair's minimum");
        if (row.verdict == ContrastVerdict::not_measured) continue;
        const bool verdict_agrees = (row.ratio >= row.minimum) == (row.verdict == ContrastVerdict::pass);
        require(verdict_agrees, "a measured row's verdict is its ratio against its minimum");
        require(row.foreground.alpha == 255 && row.background.alpha == 255, "measured pairs are opaque");
    }
}

// A button whose text is its face fails at 1:1; one in black on white
// passes at 21:1.
void synthetic_button() {
    std::unique_ptr<ThemeDefinition> definition = test_support::professional_definition();
    ControlRoleRecipes& button = (*definition).roles[static_cast<std::size_t>(ControlVisualRole::button)];
    ControlVisualRecipe& resting = button.ordinary[static_cast<std::size_t>(ControlSurfaceState::normal)];
    resting.material.fills = {MaterialFillLayer::solid(Color::rgba(90, 90, 90))};
    resting.text = Color::rgba(90, 90, 90);
    ControlVisualRecipe& hot = button.ordinary[static_cast<std::size_t>(ControlSurfaceState::hot)];
    hot.material.fills = {MaterialFillLayer::solid(Color::rgba(255, 255, 255))};
    hot.text = Color::rgba(0, 0, 0);
    const std::shared_ptr<const Theme> theme = Theme::create(*definition);
    std::vector<ContrastRow> rows;
    detail::measure_theme_contrast(*theme, rows);
    const ContrastRow* const same = find_row(rows, ControlVisualRole::button, ControlSurfaceState::normal, false,
                                             ContrastPair::text);
    require(same != nullptr, "the resting button's text row exists");
    require(near((*same).ratio, 1.0, 1e-12) && (*same).verdict == ContrastVerdict::fail,
            "text on its own colour fails at 1:1");
    const ContrastRow* const black = find_row(rows, ControlVisualRole::button, ControlSurfaceState::hot, false,
                                              ContrastPair::text);
    require(black != nullptr, "the hot button's text row exists");
    require(near((*black).ratio, 21.0, 1e-9) && (*black).verdict == ContrastVerdict::pass,
            "black on white passes at 21:1");
}

void write_report(const std::vector<ContrastRow>& rows, const char* path) {
    std::ofstream out(path, std::ios::binary);
    require(static_cast<bool>(out), "the report file opens");
    out << "presentation\tfamily\tstate\tselected\tpair\tforeground\tbackground\tratio\tminimum\tverdict\n";
    for (const ContrastRow& row : rows) {
        char foreground[10]{};
        char background[10]{};
        std::snprintf(foreground, sizeof(foreground), "#%02X%02X%02X", row.foreground.red, row.foreground.green,
                      row.foreground.blue);
        std::snprintf(background, sizeof(background), "#%02X%02X%02X", row.background.red, row.background.green,
                      row.background.blue);
        char ratio[16]{};
        std::snprintf(ratio, sizeof(ratio), "%.2f", row.ratio);
        out << (row.high_contrast ? "high_contrast" : "ordinary") << '\t' << control_visual_role_name(row.family)
            << '\t' << detail::control_surface_state_name(row.state) << '\t' << (row.selected ? "yes" : "no")
            << '\t' << detail::contrast_pair_name(row.pair) << '\t' << foreground << '\t' << background << '\t'
            << ratio << '\t' << row.minimum << '\t' << detail::contrast_verdict_name(row.verdict) << '\n';
    }
    require(static_cast<bool>(out), "the report is written");
}

void built_in_report() {
    const std::shared_ptr<const Theme> theme = default_theme();
    std::vector<ContrastRow> rows;
    detail::measure_theme_contrast(*theme, rows);
    require(!rows.empty(), "the built-in theme has a report");
    gated_pairs(rows);
    const ContrastCounts counts = detail::count_contrast_verdicts(rows);
    require(counts.passed + counts.failed + counts.not_measured == rows.size(), "every row has one verdict");
    std::array<std::size_t, 6> failures_by_pair{};
    for (const ContrastRow& row : rows) {
        if (row.verdict == ContrastVerdict::fail) ++failures_by_pair[static_cast<std::size_t>(row.pair)];
    }
    std::cout << "contrast report of " << (*theme).id() << ": " << rows.size() << " rows, " << counts.passed
              << " pass, " << counts.failed << " fail, " << counts.not_measured << " not measured; failures:";
    for (std::size_t pair = 0U; pair < failures_by_pair.size(); ++pair) {
        std::cout << ' ' << detail::contrast_pair_name(static_cast<ContrastPair>(pair)) << ' '
                  << failures_by_pair[pair];
    }
    std::cout << '\n';
    const char* const path = std::getenv("GUI_FORMS_CONTRAST_REPORT");
    if (path != nullptr && path[0] != '\0') write_report(rows, path);
}

} // namespace

int main() {
    wcag_arithmetic();
    compositing();
    synthetic_button();
    built_in_report();
    std::cout << "theme contrast: arithmetic, compositing, gated pairs and the built-in report passed\n";
    return 0;
}
