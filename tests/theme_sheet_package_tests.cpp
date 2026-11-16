// theme_sheet_package: an application's theme as one plain CSS file. The
// package compiles as any package does, a changed sheet compiles again
// without anyone writing its digest, a refused sheet's finding names its
// line, and a name is escaped so the manifest stays JSON. A control's
// theme sheet (Control::set_theme_sheet) compiles at once when attached and
// at attachment otherwise, and reports both.

#include "gui_forms/basic_controls.hpp"
#include "gui_forms/resources/application_resources/application_resources.hpp"
#include "gui_forms/resources/image_registry/image_registry.hpp"
#include "gui_forms/window.hpp"
#include "gui_forms/theme/stock_anatomies/stock_anatomies.hpp"
#include "gui_forms/theme/theme_compiler/theme_compiler.hpp"
#include "gui_forms/theme/theme_overrides/theme_overrides.hpp"

#include <stxlib/optional.hpp>

#include <cstddef>
#include <memory>
#include <iostream>
#include <string>
#include <string_view>

using namespace gui_forms;

namespace {

int failures = 0;

void require(bool condition, std::string_view message) {
    if (condition) return;
    ++failures;
    std::cerr << "FAILED: " << message << '\n';
}

// set_role_colour's background is stxlib::Optional<Color>: a plain colour
// and a bare {} are refused at compile time instead of reaching the
// deprecated std::optional form (D15 Q3); two arguments still pick none.
template <typename Overrides>
concept takes_plain_background = requires(Overrides& overrides) {
    overrides.set_role_colour(ColourRole::ink_on_paper, Color{}, Color{});
};
template <typename Overrides>
concept takes_empty_background = requires(Overrides& overrides) {
    overrides.set_role_colour(ColourRole::ink_on_paper, Color{}, {});
};
template <typename Overrides>
concept takes_no_background = requires(Overrides& overrides) { overrides.set_role_colour(ColourRole::focus, Color{}); };
static_assert(!takes_plain_background<ThemeOverrides> && !takes_empty_background<ThemeOverrides> &&
                  takes_no_background<ThemeOverrides>,
              "set_role_colour refuses a plain background and a bare {}");

ThemeCompileResult compile_sheet(std::string_view sheet, std::string_view name) {
    ImageRegistry images{};
    const ThemeLibrary library{};
    // The stock anatomies, so that the sheet's parts are known (a package
    // compiled without them warns of every part).
    ThemeCompileOptions options{};
    options.controls = stock_control_anatomies();
    const ThemePackage package = theme_sheet_package(sheet, name);
    ThemeCompileResult result = compile_theme_package(package, library, images, options);
    for (const ThemeCompileDiagnostic& diagnostic : result.diagnostics()) {
        std::cerr << "  finding: " << diagnostic.code << " line " << diagnostic.line << " " << diagnostic.name << '\n';
    }
    return result;
}

void test_a_sheet_compiles_and_recompiles_when_edited() {
    const ThemeCompileResult first = compile_sheet("button::part(face) { background-color: #3366cc }\n", "app-sheet");
    require(first.succeeded() && first.diagnostics().empty(), "a clean sheet compiles without findings");
    // An edit needs no new digest: the package is made again from the text.
    const ThemeCompileResult edited = compile_sheet("button::part(face) { background-color: #cc6633 }\n", "app-sheet");
    require(edited.succeeded() && edited.diagnostics().empty(), "the edited sheet compiles as well");
}

void test_a_refused_sheet_names_its_line() {
    const ThemeCompileResult refused = compile_sheet("button { opacity: 0.9 }\n/* never closed\n", "app-sheet");
    require(!refused.succeeded() && !refused.diagnostics().empty(), "a sheet that does not parse is refused");
    if (refused.diagnostics().empty()) return;
    const ThemeCompileDiagnostic& finding = refused.diagnostics().front();
    require(finding.severity == ThemeDiagnosticSeverity::error && finding.line == 2U,
            "the refusal is an error on the sheet's second line");
    require(finding.file.find("sheet.theme.css") != std::string::npos, "the finding names the sheet's file");
}

void test_the_name_is_escaped() {
    const ThemePackage package = theme_sheet_package("button { opacity: 0.9 }\n", "odd\"name\\\n");
    require(package.manifest.find("\"name\": \"odd\\\"name\\\\\\u000a\"") != std::string::npos,
            "a quote, a backslash and a control character in the name are escaped");
    require(package.files.size() == 1U && package.files.front().path == "sheet.theme.css",
            "the package holds the one sheet");
}

// Counts theme_sheet_compiled and keeps the last result's outcome.
struct SheetReports final {
    int count{};
    bool succeeded{};
    std::size_t diagnostics{};
    void record(const ThemeCompileResult& result) {
        count = count + 1;
        succeeded = result.succeeded();
        diagnostics = result.diagnostics().size();
    }
};

void test_a_control_theme_sheet_compiles_when_set_or_at_attachment() {
    const std::string_view sheet = "button::part(face) { background-color: #3366cc }\n";
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("sheet.root"));
    std::shared_ptr<Panel> preview = make_control<Panel>(StableId("sheet.preview"));
    SheetReports reports{};
    SubscriptionToken token = (*preview).theme_sheet_compiled().subscribe(
        Delegate<const ThemeCompileResult&>::bind<SheetReports, &SheetReports::record>(reports));
    // Detached: the sheet waits.
    require(!(*preview).set_theme_sheet(sheet, "preview-sheet") && (*preview).theme_sheet_pending(),
            "a sheet set while detached waits for an attachment");
    require(reports.count == 0 && (*preview).theme_override() == nullptr, "nothing compiles before the attachment");
    (*root).add_child(preview);
    const std::shared_ptr<ApplicationResources> resources = std::make_shared<ApplicationResources>();
    Window window(root, Size{320.0, 160.0}, resources);
    require(reports.count == 1 && reports.succeeded && !(*preview).theme_sheet_pending(),
            "the attachment compiles the waiting sheet and reports it");
    require((*preview).theme_override() != nullptr, "the compiled sheet is the control's theme override");
    // Attached: a new sheet compiles at once; a refused one keeps the theme.
    const std::shared_ptr<const Theme> applied = (*preview).theme_override();
    require(!(*preview).set_theme_sheet("button { opacity: 0.9 }\n/* never closed\n", "preview-sheet"),
            "a refused sheet returns false");
    require(reports.count == 2 && !reports.succeeded && reports.diagnostics > 0U,
            "the refusal is reported with its diagnostics");
    require((*preview).theme_override() == applied, "a refused sheet keeps the override it found");
    require((*preview).set_theme_sheet(sheet, "preview-sheet") && reports.count == 3 && reports.succeeded,
            "an attached control compiles its sheet at once");
    static_cast<void>((*preview).set_theme_sheet("", "preview-sheet"));
    require((*preview).theme_override() == nullptr && (*preview).theme_sheet().empty(),
            "an empty sheet removes the sheet and its override");
    token.disconnect();
}

void test_typed_overrides_check_names_and_write_a_sheet() {
    ThemeOverrides overrides{};
    ThemeOverrideTarget face{};
    face.control = "button";
    face.part = "face";
    require(overrides.set_colour(face, ThemeOverrideProperty::background_colour, Color::rgba(51, 102, 204)) ==
                ThemeOverrideCheck::accepted,
            "a stock control's part takes a colour");
    face.state.set(AnatomyState::hot);
    require(overrides.set_colour(face, ThemeOverrideProperty::background_colour, Color::rgba(68, 119, 221, 128)) ==
                ThemeOverrideCheck::accepted,
            "a state the control takes is accepted");
    ThemeOverrideTarget button{};
    button.control = "button";
    require(overrides.set_length(button, ThemeOverrideProperty::corner_radius, 6.0) == ThemeOverrideCheck::accepted,
            "a radius in range is accepted");
    require(overrides.set_role_colour(ColourRole::focus, Color::rgba(200, 0, 0)) == ThemeOverrideCheck::accepted,
            "a single role takes one colour");
    require(overrides.size() == 4U, "four entries are held");
    // Refusals change nothing.
    ThemeOverrideTarget misspelt{};
    misspelt.control = "buton";
    require(overrides.set_colour(misspelt, ThemeOverrideProperty::text_colour, Color::rgba(0, 0, 0)) ==
                ThemeOverrideCheck::unknown_control,
            "a misspelt control is refused");
    ThemeOverrideTarget no_part = button;
    no_part.part = "fase";
    require(overrides.set_colour(no_part, ThemeOverrideProperty::text_colour, Color::rgba(0, 0, 0)) ==
                ThemeOverrideCheck::unknown_part,
            "a misspelt part is refused");
    require(overrides.set_length(button, ThemeOverrideProperty::corner_radius, 65.0) ==
                    ThemeOverrideCheck::value_out_of_range &&
                overrides.set_length(button, ThemeOverrideProperty::text_colour, 1.0) ==
                    ThemeOverrideCheck::wrong_value_kind &&
                overrides.set_role_colour(ColourRole::ink_on_paper, Color::rgba(0, 0, 0)) ==
                    ThemeOverrideCheck::wrong_value_kind,
            "out-of-range lengths, wrong kinds and a pair given one colour are refused");
    require(overrides.size() == 4U, "refusals add nothing");
    ThemeOverrides paired{};
    require(paired.set_role_colour(ColourRole::ink_on_paper, Color::rgba(0, 0, 0),
                                   stxlib::Optional<Color>::of(Color::rgba(255, 255, 255))) ==
                    ThemeOverrideCheck::accepted &&
                paired.set_role_colour(ColourRole::focus, Color::rgba(200, 0, 0),
                                       stxlib::Optional<Color>::of(Color::rgba(255, 255, 255))) ==
                    ThemeOverrideCheck::wrong_value_kind &&
                paired.size() == 1U,
            "a pair takes a present background, and a single role refuses one");
    const std::string sheet = overrides.sheet_text();
    require(sheet.find(":root {\n  focus: #c80000;\n}") != std::string::npos, "the role is written in :root");
    require(sheet.find("button::part(face) {\n  background-color: #3366cc;\n}") != std::string::npos,
            "the part's colour is written as hex");
    require(sheet.find("button::part(face):hover {") != std::string::npos, "hot is written :hover");
    require(sheet.find("border-radius: 6px;") != std::string::npos, "the radius is written in px");
    require(sheet == overrides.sheet_text(), "the sheet is deterministic");
    const ThemeCompileResult compiled = compile_sheet(sheet, "overrides");
    require(compiled.succeeded() && compiled.diagnostics().empty(), "the written sheet compiles without findings");
    // Applied to a control through its theme sheet.
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("overrides.root"));
    const std::shared_ptr<ApplicationResources> resources = std::make_shared<ApplicationResources>();
    Window window(root, Size{320.0, 160.0}, resources);
    require((*root).set_theme_overrides(overrides) && (*root).theme_override() != nullptr,
            "a control applies typed overrides as its theme");
    require((*root).theme_sheet() == sheet, "the control's sheet is the overrides' sheet");
    overrides.clear();
    static_cast<void>((*root).set_theme_overrides(overrides));
    require((*root).theme_override() == nullptr, "empty overrides remove the sheet");
}

} // namespace

int main() {
    test_a_sheet_compiles_and_recompiles_when_edited();
    test_a_refused_sheet_names_its_line();
    test_the_name_is_escaped();
    test_a_control_theme_sheet_compiles_when_set_or_at_attachment();
    test_typed_overrides_check_names_and_write_a_sheet();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "theme sheet package tests passed\n";
    return 0;
}
