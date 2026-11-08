// The Windows-familiar themes (stage T4, phase 7): each floor sheet
// compiles with no diagnostics within the work bound and round-trips to
// identical bytes; each theme carries its id, is built once, passes every
// gated contrast pair in light and high contrast, and resolves its own
// recipes, unlike the others'; the ids name the themes and nothing else.
// The owner's accepted exceptions (period values kept below the gate) are
// listed, not counted.
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

// A failing pair the owner accepted (2026-11-08): the period value of a
// familiar theme kept below the gate. It matches a row of theme `id`
// whose pair is `pair` (or the text and muted text pairs of a disabled
// state, when `disabled_text`), drawn in `foreground`, and, when
// `family_bound`, of `family` alone.
struct AcceptedException final {
    std::string_view id{};
    bool disabled_text{};
    detail::ContrastPair pair{detail::ContrastPair::text};
    bool family_bound{};
    ControlVisualRole family{ControlVisualRole::window};
    Color foreground{};
};

constexpr std::array<AcceptedException, 3> accepted_exceptions{{
    // Classic's embossed disabled text, #808080.
    {.id = "classic", .disabled_text = true, .foreground = Color::rgba(128, 128, 128)},
    // Luna's disabled text, #ACA899.
    {.id = "xp-style", .disabled_text = true, .foreground = Color::rgba(172, 168, 153)},
    // Aero's default button frame, #3399FF.
    {.id = "windows7-style",
     .pair = detail::ContrastPair::boundary,
     .family_bound = true,
     .family = ControlVisualRole::accent_button,
     .foreground = Color::rgba(51, 153, 255)},
}};

bool accepted(std::string_view id, const ContrastRow& row) noexcept {
    for (const AcceptedException& exception : accepted_exceptions) {
        if (exception.id != id || row.foreground != exception.foreground) continue;
        if (exception.family_bound && row.family != exception.family) continue;
        if (exception.disabled_text) {
            const bool text_pair = row.pair == detail::ContrastPair::text || row.pair == detail::ContrastPair::muted_text;
            if (text_pair && row.state == ControlSurfaceState::disabled && !row.high_contrast) return true;
            continue;
        }
        if (row.pair == exception.pair && !row.high_contrast) return true;
    }
    return false;
}

// Prints each failing row and returns how many there are, not counting
// (but listing) the owner's accepted exceptions.
std::size_t print_failures(std::string_view id, const std::vector<ContrastRow>& rows) {
    std::size_t failed = 0U;
    std::size_t excepted = 0U;
    for (const ContrastRow& row : rows) {
        if (row.verdict != ContrastVerdict::fail) continue;
        if (accepted(id, row)) {
            ++excepted;
            continue;
        }
        ++failed;
        char line[200]{};
        std::snprintf(line, sizeof(line), "%s fails: %s %s %s %s %s #%02X%02X%02X on #%02X%02X%02X %.2f < %.1f\n",
                      std::string(id).c_str(), row.high_contrast ? "high-contrast" : "light",
                      std::string(control_visual_role_name(row.family)).c_str(),
                      std::string(detail::control_surface_state_name(row.state)).c_str(),
                      row.selected ? "selected" : "-", std::string(detail::contrast_pair_name(row.pair)).c_str(),
                      row.foreground.red, row.foreground.green, row.foreground.blue, row.background.red,
                      row.background.green, row.background.blue, row.ratio, row.minimum);
        std::cerr << line;
    }
    if (excepted > 0U) std::cout << id << ": " << excepted << " rows are owner-accepted exceptions\n";
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
    }
    require(!familiar_theme_named("gui-forms"), "the default theme is not a familiar one");
    require(!familiar_theme_named("windows-professional"), "windows-professional keeps its own entry");
    require(familiar_theme_id(FamiliarTheme::count).empty(), "count names no theme");
}

// Light and high contrast: every gated pair meets its minimum. The themes
// are light only, so their dark rows are their light ones.
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
                  << " fail, " << counts.not_measured << " not measured\n";
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
}

} // namespace

int main() {
    each_theme_builds();
    each_theme_contrast();
    each_theme_is_its_own();
    std::cout << "familiar themes: builds, round trips, contrast and identities passed\n";
    return 0;
}
