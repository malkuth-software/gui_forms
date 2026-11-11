// theme_sheet_package: an application's theme as one plain CSS file. The
// package compiles as any package does, a changed sheet compiles again
// without anyone writing its digest, a refused sheet's finding names its
// line, and a name is escaped so the manifest stays JSON.

#include "gui_forms/gui_forms.hpp"
#include "gui_forms/theme/theme_compiler/theme_compiler.hpp"

#include <cstddef>
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

} // namespace

int main() {
    test_a_sheet_compiles_and_recompiles_when_edited();
    test_a_refused_sheet_names_its_line();
    test_the_name_is_escaped();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "theme sheet package tests passed\n";
    return 0;
}
