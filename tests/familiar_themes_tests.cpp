// The familiar themes (stage T4, phase 7): each floor sheet (and dark
// floor sheet) compiles with no diagnostics within the work bound and
// round-trips to identical bytes; each theme carries its id, is built
// once, passes every gated contrast pair in light, dark (macos-style) and
// high contrast, and resolves its own recipes, unlike the others'; the
// ids name the themes and nothing else.
// Disabled text and disabled boundaries are exempt, not gated.
// With GUI_FORMS_CONTRAST_REPORT_DIRECTORY=<path> it writes each theme's
// report as <path>/<id>.tsv, for the stage record.

#include "../src/core/theme/contrast/theme_contrast.hpp"
#include "../src/core/theme_language/diagnostics/theme_diagnostics.hpp"
#include "../src/core/theme_language/source_text/theme_source_text.hpp"
#include "../src/core/theme_language/syntax_tree/theme_syntax_tree.hpp"
#include "../src/core/theme_language/tokenizer/theme_tokenizer.hpp"
#include "gui_forms/theme.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace gui_forms;
using gui_forms::detail::ContrastRow;
using gui_forms::detail::ContrastVerdict;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

// The work bound of the default theme's sheets (gui_forms_theme_tests).
constexpr std::uint64_t operations_per_byte_bound = 64U;

void print_diagnostics(std::string_view id, const std::vector<ThemeCompileDiagnostic>& diagnostics) {
    for (const ThemeCompileDiagnostic& diagnostic : diagnostics) {
        std::cerr << id << ": " << diagnostic.code << ' ' << diagnostic.name << ' ' << diagnostic.line << ':'
                  << diagnostic.column << ' ' << diagnostic.rule << ' ' << diagnostic.property << ' '
                  << diagnostic.detail << '\n';
    }
}

void round_trip(std::string_view id, std::string_view text) {
    const std::string file = std::string(id) + "/floor.css";
    const theme_language::ThemeSource source{.file = file, .text = text};
    theme_language::ThemeTokenBuffer buffer{};
    theme_language::LineStartTable lines{};
    theme_language::ThemeDiagnosticList diagnostics{};
    static_cast<void>(theme_language::tokenize_theme_sheet(source, buffer, lines, diagnostics));
    theme_language::ThemeSyntaxTree tree{};
    const theme_language::ParseOutcome parsed =
        theme_language::parse_theme_sheet(source, buffer, lines, tree, diagnostics);
    require(parsed == theme_language::ParseOutcome::tree_complete, "the floor sheet parses completely");
    std::string written{};
    const theme_language::WriteOutcome write = theme_language::write_theme_sheet(tree, buffer, text, written);
    require(write == theme_language::WriteOutcome::written, "the floor sheet's tree nests");
    require(written == text, "the floor sheet round-trips to identical bytes");
}

void write_report(std::string_view id, const std::vector<ContrastRow>& rows, const char* directory) {
    const std::string path = std::string(directory) + "/" + std::string(id) + ".tsv";
    std::ofstream out(path, std::ios::binary);
    require(static_cast<bool>(out), "the report file opens");
    out << "presentation\tfamily\tstate\tselected\tpair\tforeground\tbackground\tratio\tminimum\tverdict\n";
    for (const ContrastRow& row : rows) {
        char colours[40]{};
        std::snprintf(colours, sizeof(colours), "#%02X%02X%02X\t#%02X%02X%02X\t%.2f", row.foreground.red,
                      row.foreground.green, row.foreground.blue, row.background.red, row.background.green,
                      row.background.blue, row.ratio);
        const char* const presentation = row.high_contrast ? "high_contrast" : row.dark ? "dark" : "ordinary";
        out << presentation << '\t' << control_visual_role_name(row.family) << '\t'
            << detail::control_surface_state_name(row.state) << '\t' << (row.selected ? "yes" : "no") << '\t'
            << detail::contrast_pair_name(row.pair) << '\t' << colours << '\t' << row.minimum << '\t'
            << detail::contrast_verdict_name(row.verdict) << '\n';
    }
}

// Prints each failing row and returns how many there are. Disabled text
// and disabled boundaries are exempt in the report itself (WCAG's
// inactive components, owner 2026-11-08), so no theme keeps exceptions.
std::size_t print_failures(std::string_view id, const std::vector<ContrastRow>& rows) {
    std::size_t failed = 0U;
    for (const ContrastRow& row : rows) {
        if (row.verdict != ContrastVerdict::fail) continue;
        ++failed;
        char line[200]{};
        std::snprintf(line, sizeof(line), "%s fails: %s %s %s %s %s #%02X%02X%02X on #%02X%02X%02X %.2f < %.1f\n",
                      std::string(id).c_str(), row.high_contrast ? "high-contrast" : row.dark ? "dark" : "light",
                      std::string(control_visual_role_name(row.family)).c_str(),
                      std::string(detail::control_surface_state_name(row.state)).c_str(),
                      row.selected ? "selected" : "-", std::string(detail::contrast_pair_name(row.pair)).c_str(),
                      row.foreground.red, row.foreground.green, row.foreground.blue, row.background.red,
                      row.background.green, row.background.blue, row.ratio, row.minimum);
        std::cerr << line;
    }
    return failed;
}

void each_theme_builds() {
    for (std::size_t index = 0U; index < familiar_theme_count; ++index) {
        const FamiliarTheme which = static_cast<FamiliarTheme>(index);
        const std::string_view id = familiar_theme_id(which);
        require(!id.empty(), "each theme has an id");
        require(familiar_theme_named(id) == which, "the id names its theme");
        const FamiliarThemeBuild& build = familiar_theme_build(which);
        print_diagnostics(id, build.diagnostics);
        require(build.diagnostics.empty(), "each floor sheet compiles with no diagnostics");
        require(build.theme != nullptr, "each build makes a theme");
        require((*build.theme).id() == id, "the theme carries its id");
        require(&familiar_theme_build(which) == &build, "each theme is built once");
        require(familiar_theme(which) == build.theme, "familiar_theme() is the build's theme");
        const std::string_view sheet = familiar_theme_floor_sheet(which);
        require(build.compile_operations > 0U, "the compiler's work is counted");
        require(build.compile_operations <= sheet.size() * operations_per_byte_bound,
                "the compile stays within its work bound");
        round_trip(id, sheet);
        const std::string_view dark_sheet = familiar_theme_dark_floor_sheet(which);
        if (!dark_sheet.empty()) round_trip(id, dark_sheet);
        const bool dark = (*build.theme).definition().dark_variant == ThemeDarkVariant::authored;
        require(dark == !dark_sheet.empty(), "exactly a theme with a dark sheet has an authored dark variant");
        require((*build.theme).supports(ColourScheme::dark) == dark,
                "exactly a theme with a dark sheet offers the dark colour scheme");
    }
    require(!familiar_theme_dark_floor_sheet(FamiliarTheme::macos_style).empty(), "macos-style has a dark sheet");
    require(!familiar_theme_named("gui-forms"), "the default theme is not a familiar one");
    // The stock Windows-familiar theme is a familiar one, light and dark
    // (owner, 2026-11-10).
    require(familiar_theme_named("windows-professional") == FamiliarTheme::windows_professional,
            "windows-professional is the stock Windows-familiar theme");
    require(!familiar_theme_dark_floor_sheet(FamiliarTheme::windows_professional).empty(),
            "windows-professional has a dark sheet");
    require(familiar_theme_id(FamiliarTheme::count).empty(), "count names no theme");
}

// Light, dark (for a theme with a dark sheet) and high contrast: every
// gated pair meets its minimum. A light-only theme is not measured dark:
// its dark rows are its light ones.
void each_theme_contrast() {
    const char* const directory = std::getenv("GUI_FORMS_CONTRAST_REPORT_DIRECTORY");
    std::size_t failed = 0U;
    for (std::size_t index = 0U; index < familiar_theme_count; ++index) {
        const FamiliarTheme which = static_cast<FamiliarTheme>(index);
        std::vector<ContrastRow> rows{};
        detail::measure_theme_contrast(*familiar_theme(which), rows);
        require(!rows.empty(), "the theme's contrast is measured");
        failed += print_failures(familiar_theme_id(which), rows);
        const detail::ContrastCounts counts = detail::count_contrast_verdicts(rows);
        std::cout << familiar_theme_id(which) << " contrast: " << counts.passed << " pass, " << counts.failed
                  << " fail, " << counts.exempt << " exempt, " << counts.not_measured
                  << " not measured\n";
        if (directory != nullptr && directory[0] != '\0') write_report(familiar_theme_id(which), rows, directory);
    }
    require(failed == 0U, "every gated pair of every familiar theme meets its minimum");
}

// Each theme's button rests on its own face, unlike the default theme's
// and the other familiar themes'.
void each_theme_is_its_own() {
    const ControlVisualContext resting{};
    std::array<ControlVisualRecipe, familiar_theme_count> buttons{};
    for (std::size_t index = 0U; index < familiar_theme_count; ++index) {
        const std::shared_ptr<const Theme> theme = familiar_theme(static_cast<FamiliarTheme>(index));
        buttons[index] = (*theme).resolve(ControlVisualRole::button, resting);
    }
    const ControlVisualRecipe& own = (*gui_forms_theme()).resolve(ControlVisualRole::button, resting);
    for (std::size_t index = 0U; index < familiar_theme_count; ++index) {
        require(!(buttons[index] == own), "a familiar theme's button is not the default theme's");
        for (std::size_t other = index + 1U; other < familiar_theme_count; ++other) {
            require(!(buttons[index] == buttons[other]), "no two familiar themes share a button");
        }
    }
    const ControlVisualRecipe& classic = buttons[static_cast<std::size_t>(FamiliarTheme::classic)];
    require(classic.material.keylines.size() == 4U, "classic's button is bevelled by four inner lines");
    require(classic.dotted_focus, "classic's focus ring is dotted");
    const ControlVisualRecipe& flat = buttons[static_cast<std::size_t>(FamiliarTheme::modern_flat)];
    require(flat.material.keylines.empty() && !flat.dotted_focus, "modern-flat's button is flat, its ring solid");
    // macos-style's dark records are its dark sheet's; a light-only
    // theme's dark records are its light ones.
    ControlVisualContext dark{};
    dark.dark = true;
    const std::shared_ptr<const Theme> macos = familiar_theme(FamiliarTheme::macos_style);
    const ControlVisualRecipe& macos_light = (*macos).resolve(ControlVisualRole::button, resting);
    const ControlVisualRecipe& macos_dark = (*macos).resolve(ControlVisualRole::button, dark);
    require(!(macos_dark == macos_light), "macos-style's dark button is not its light one");
    const std::shared_ptr<const Theme> classic_theme = familiar_theme(FamiliarTheme::classic);
    require((*classic_theme).resolve(ControlVisualRole::button, dark) == classic,
            "a light-only theme's dark button is its light one");
    // windows-professional: a bevelled button (Windows 2000, softened), a
    // solid focus rectangle (Windows 10), and its own dark records.
    const std::shared_ptr<const Theme> professional = familiar_theme(FamiliarTheme::windows_professional);
    const ControlVisualRecipe& professional_light = (*professional).resolve(ControlVisualRole::button, resting);
    require(professional_light.material.keylines.size() == 4U, "windows-professional's button keeps a soft bevel");
    require(!professional_light.dotted_focus, "windows-professional's focus rectangle is solid");
    require(!((*professional).resolve(ControlVisualRole::button, dark) == professional_light),
            "windows-professional's dark button is its dark sheet's");
}

// Tooltip text is black on the light tooltip plate (owner mandate
// 2026-11-11), in every familiar theme's light records, resting and
// selected, and in the default theme. The dark records (macos-style's and
// windows-professional's dark sheets draw a dark plate) are the owner's to
// decide and are not held here.
void each_light_tooltip_is_black() {
    const Color black = Color::rgba(0, 0, 0);
    const ControlVisualContext resting{};
    for (std::size_t index = 0U; index < familiar_theme_count; ++index) {
        const std::shared_ptr<const Theme> theme = familiar_theme(static_cast<FamiliarTheme>(index));
        const ControlVisualRecipe& tool_tip = (*theme).resolve(ControlVisualRole::tool_tip, resting);
        require(tool_tip.text == black, "a familiar theme's tooltip text is black");
    }
    const ControlVisualRecipe& own = (*gui_forms_theme()).resolve(ControlVisualRole::tool_tip, resting);
    require(own.text == black, "the default theme's tooltip text is black");
}

} // namespace

int main() {
    each_theme_builds();
    each_theme_contrast();
    each_theme_is_its_own();
    each_light_tooltip_is_black();
    std::cout << "familiar themes: builds, round trips, contrast and identities passed\n";
    return 0;
}
