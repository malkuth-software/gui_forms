// The default theme `gui-forms` (stage T4, phase 3): the stock anatomy
// table, the light sheet compiled at first use with no diagnostics and
// within its work bound, the sheet's byte-preserving round trip (T3 P11
// item 5), and the light contrast report passing every gated pair (design
// language 5.7). With GUI_FORMS_CONTRAST_REPORT=<path> it writes the
// theme's light report as tab-separated rows there, for the stage record.

#include "../src/core/theme/contrast/theme_contrast.hpp"
#include "../src/core/theme_language/diagnostics/theme_diagnostics.hpp"
#include "../src/core/theme_language/source_text/theme_source_text.hpp"
#include "../src/core/theme_language/syntax_tree/theme_syntax_tree.hpp"
#include "../src/core/theme_language/tokenizer/theme_tokenizer.hpp"
#include "gui_forms/theme.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace gui_forms;
using gui_forms::detail::ContrastCounts;
using gui_forms::detail::ContrastRow;
using gui_forms::detail::ContrastVerdict;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

bool anatomy_named(std::span<const ControlAnatomy> anatomies, std::string_view id) {
    for (const ControlAnatomy& anatomy : anatomies) {
        if (anatomy.control_id == id) return true;
    }
    return false;
}

bool id_less(const ControlAnatomy& left, const ControlAnatomy& right) noexcept {
    return left.control_id < right.control_id;
}

bool id_equal(const ControlAnatomy& left, const ControlAnatomy& right) noexcept {
    return left.control_id == right.control_id;
}

void stock_anatomies() {
    const std::span<const ControlAnatomy> anatomies = stock_control_anatomies();
    require(anatomies.size() >= 90U, "the table holds every stock control type");
    require(anatomy_named(anatomies, "button"), "the table holds Button");
    require(anatomy_named(anatomies, "tab_control"), "the table holds TabControl");
    std::vector<ControlAnatomy> sorted(anatomies.begin(), anatomies.end());
    std::sort(sorted.begin(), sorted.end(), id_less);
    require(std::adjacent_find(sorted.begin(), sorted.end(), id_equal) == sorted.end(),
            "each control type appears once");
    for (const ControlAnatomy& anatomy : anatomies) {
        require(!anatomy.parts.empty(), "each stock control declares its parts");
    }
}

// The work bound (T3 P11 1a): at most 4 x maximum_variable_combinations
// operations per byte plus 4,096; the light sheet is far inside it, so the
// stated bound here is 64 operations per byte.
constexpr std::uint64_t operations_per_byte_bound = 64U;

void compiled_at_first_use() {
    const GuiFormsThemeBuild& build = gui_forms_theme_build();
    for (const ThemeCompileDiagnostic& diagnostic : build.diagnostics) {
        std::cerr << diagnostic.code << ' ' << diagnostic.name << ' ' << diagnostic.line << ':' << diagnostic.column
                  << ' ' << diagnostic.rule << ' ' << diagnostic.property << ' ' << diagnostic.detail << '\n';
    }
    require(build.diagnostics.empty(), "the light sheet compiles with no diagnostics");
    require(build.theme != nullptr, "the build makes a theme");
    require((*build.theme).id() == "gui-forms", "the theme's id is gui-forms (T4-2)");
    require(!(*build.theme).definition().rules.rules.empty(), "the theme holds the sheet's rules");
    require(&gui_forms_theme_build() == &build, "the theme is built once");
    require(gui_forms_theme() == build.theme, "gui_forms_theme() is the build's theme");
    const std::uint64_t bytes = gui_forms_light_sheet().size();
    require(build.compile_operations > 0U, "the compiler's work is counted");
    require(build.compile_operations <= bytes * operations_per_byte_bound, "the compile stays within its work bound");
    const double milliseconds = std::chrono::duration<double, std::milli>(build.build_time).count();
    char line[160]{};
    std::snprintf(line, sizeof(line), "gui-forms build: %llu sheet bytes, %llu operations, %.3f ms\n",
                  static_cast<unsigned long long>(bytes), static_cast<unsigned long long>(build.compile_operations),
                  milliseconds);
    std::cout << line;
}

void round_trip() {
    const std::string_view text = gui_forms_light_sheet();
    const theme_language::ThemeSource source{.file = "gui-forms/light.css", .text = text};
    theme_language::ThemeTokenBuffer buffer{};
    theme_language::LineStartTable lines{};
    theme_language::ThemeDiagnosticList diagnostics{};
    static_cast<void>(theme_language::tokenize_theme_sheet(source, buffer, lines, diagnostics));
    theme_language::ThemeSyntaxTree tree{};
    const theme_language::ParseOutcome parsed =
        theme_language::parse_theme_sheet(source, buffer, lines, tree, diagnostics);
    require(parsed == theme_language::ParseOutcome::tree_complete, "the light sheet parses completely");
    std::string written{};
    const theme_language::WriteOutcome write = theme_language::write_theme_sheet(tree, buffer, text, written);
    require(write == theme_language::WriteOutcome::written, "the light sheet's tree nests");
    require(written == text, "the light sheet round-trips to identical bytes");
}

void write_report(const std::vector<ContrastRow>& rows, const char* path) {
    std::ofstream out(path, std::ios::binary);
    require(static_cast<bool>(out), "the report file opens");
    out << "presentation\tfamily\tstate\tselected\tpair\tforeground\tbackground\tratio\tminimum\tverdict\n";
    for (const ContrastRow& row : rows) {
        char colours[40]{};
        std::snprintf(colours, sizeof(colours), "#%02X%02X%02X\t#%02X%02X%02X\t%.2f", row.foreground.red,
                      row.foreground.green, row.foreground.blue, row.background.red, row.background.green,
                      row.background.blue, row.ratio);
        const char* const presentation = row.high_contrast ? "high_contrast" : row.dark ? "dark" : "ordinary";
        out << presentation << '\t' << control_visual_role_name(row.family)
            << '\t' << detail::control_surface_state_name(row.state) << '\t' << (row.selected ? "yes" : "no") << '\t'
            << detail::contrast_pair_name(row.pair) << '\t' << colours << '\t' << row.minimum << '\t'
            << detail::contrast_verdict_name(row.verdict) << '\n';
    }
}

// Prints each failing row of `rows` and returns how many there are.
std::size_t print_failures(const std::vector<ContrastRow>& rows) {
    std::size_t failed = 0U;
    for (const ContrastRow& row : rows) {
        if (row.verdict != ContrastVerdict::fail) continue;
        ++failed;
        char line[200]{};
        std::snprintf(line, sizeof(line), "fails: %s %s %s %s #%02X%02X%02X on #%02X%02X%02X %.2f < %.1f\n",
                      std::string(control_visual_role_name(row.family)).c_str(),
                      std::string(detail::control_surface_state_name(row.state)).c_str(),
                      row.selected ? "selected" : "-", std::string(detail::contrast_pair_name(row.pair)).c_str(),
                      row.foreground.red, row.foreground.green, row.foreground.blue, row.background.red,
                      row.background.green, row.background.blue, row.ratio, row.minimum);
        std::cerr << line;
    }
    return failed;
}

// The rows of one presentation.
std::vector<ContrastRow> presentation_rows(const std::vector<ContrastRow>& rows, bool high_contrast, bool dark) {
    std::vector<ContrastRow> result{};
    for (const ContrastRow& row : rows) {
        if (row.high_contrast == high_contrast && row.dark == dark) result.push_back(row);
    }
    return result;
}

// Light (the gate of phase 3) and dark (phase 5): every gated pair meets
// its minimum.
void light_contrast() {
    std::vector<ContrastRow> rows{};
    detail::measure_theme_contrast(*gui_forms_theme(), rows);
    const std::vector<ContrastRow> light = presentation_rows(rows, false, false);
    const std::size_t failed = print_failures(light);
    const std::vector<ContrastRow> dark = presentation_rows(rows, false, true);
    require(!dark.empty(), "the derived dark presentation is measured");
    const ContrastCounts dark_counts = detail::count_contrast_verdicts(dark);
    std::cout << "gui-forms dark contrast: " << dark_counts.passed << " pass, " << dark_counts.failed << " fail, "
              << dark_counts.not_measured << " not measured\n";
    const std::size_t dark_failed = print_failures(dark);
    const ContrastCounts counts = detail::count_contrast_verdicts(light);
    std::cout << "gui-forms light contrast: " << counts.passed << " pass, " << counts.failed << " fail, "
              << counts.not_measured << " not measured\n";
    const char* const path = std::getenv("GUI_FORMS_CONTRAST_REPORT");
    if (path != nullptr && path[0] != '\0') write_report(rows, path);
    require(failed == 0U, "every gated light pair meets its minimum");
    require(dark_failed == 0U, "every gated dark pair meets its minimum");
}

// The dark records (phase 5): gui-forms derives them, so a dark context
// resolves colours unlike the light ones with the same geometry; high
// contrast wins over dark; a theme without a dark variant resolves its
// ordinary records in a dark context.
void dark_records() {
    const std::shared_ptr<const Theme> theme = gui_forms_theme();
    ControlVisualContext light{};
    ControlVisualContext dark{};
    dark.dark = true;
    const ControlVisualRecipe& light_button = (*theme).resolve(ControlVisualRole::button, light);
    const ControlVisualRecipe& dark_button = (*theme).resolve(ControlVisualRole::button, dark);
    require(!(light_button.text == dark_button.text), "a dark button's text is derived");
    require(light_button.material.corner_radius == dark_button.material.corner_radius,
            "a dark button keeps its geometry");
    require(detail::relative_luminance(dark_button.text) > detail::relative_luminance(light_button.text),
            "dark text is lighter than light text");
    ControlVisualContext forced = dark;
    forced.high_contrast = true;
    ControlVisualContext forced_light{};
    forced_light.high_contrast = true;
    require((*theme).resolve(ControlVisualRole::button, forced) ==
                (*theme).resolve(ControlVisualRole::button, forced_light),
            "high contrast wins over dark");
    const std::shared_ptr<const Theme> professional = Theme::create(windows_professional_theme_definition());
    require((*professional).resolve(ControlVisualRole::button, dark) ==
                (*professional).resolve(ControlVisualRole::button, light),
            "a theme without a dark variant keeps its ordinary records");
}

} // namespace

int main() {
    stock_anatomies();
    compiled_at_first_use();
    round_trip();
    light_contrast();
    dark_records();
    std::cout << "gui-forms theme: anatomies, first-use compile, round trip, light and dark contrast and dark records passed\n";
    return 0;
}
