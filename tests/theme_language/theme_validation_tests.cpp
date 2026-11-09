// Early theme validation (owner request 2026-11-09, the product ports): a
// program checks its theme packages in a test or a build step with
// validate_theme_packages, before Application::create would refuse them.
// The packages here are embedded by gui_forms_add_theme, as a product's
// are; the build-time check (gui_forms_theme_check) runs on the same two
// directories as the ctest cases gui_forms_theme_check_accepts and
// gui_forms_theme_check_refuses.

#include "gui_forms/engine/effect_registry/effect_registry.hpp"
#include "gui_forms/theme.hpp"

#include "refused_theme_package.hpp"
#include "valid_theme_package.hpp"

#include <array>
#include <iostream>
#include <string>
#include <string_view>

namespace {

using namespace gui_forms;

int failures = 0;

void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

ThemeCompileOptions stock_options() {
    ThemeCompileOptions options{};
    options.controls = stock_control_anatomies();
    options.effects = &built_in_effect_registry();
    return options;
}

void test_valid_package() {
    const std::array<ThemePackage, 1> packages{gui_forms_tests::valid_theme_package()};
    const ThemeLibrary library{};
    const ThemeValidation validation = validate_theme_packages(packages, library, stock_options());
    for (const ThemeCompileDiagnostic& diagnostic : validation.diagnostics) {
        std::cerr << "  " << format_theme_diagnostic(diagnostic) << '\n';
    }
    require(validation.valid, "a package with application roles validates");
    require(validation.diagnostics.empty(), "with no finding");
}

void test_refused_package() {
    const std::array<ThemePackage, 1> packages{gui_forms_tests::refused_theme_package()};
    const ThemeLibrary library{};
    const ThemeValidation validation = validate_theme_packages(packages, library, stock_options());
    require(!validation.valid, "a sheet reading an undeclared role is refused");
    bool named = false;
    for (const ThemeCompileDiagnostic& diagnostic : validation.diagnostics) {
        const std::string line = format_theme_diagnostic(diagnostic);
        const bool keyword = diagnostic.code == "E406" && diagnostic.severity == ThemeDiagnosticSeverity::error;
        const bool placed = line.find("refused.theme.css:4:") != std::string::npos &&
                            line.find("error E406") != std::string::npos &&
                            line.find("syntax-keyword-misspelt") != std::string::npos;
        if (keyword && placed) named = true;
    }
    require(named, "the refusal is E406, placed in its sheet and naming the role");
}

void test_formatting() {
    ThemeCompileDiagnostic diagnostic{};
    diagnostic.code = "W107";
    diagnostic.name = "property_not_admitted";
    diagnostic.severity = ThemeDiagnosticSeverity::warning;
    diagnostic.file = "app/app.theme.css";
    diagnostic.line = 3U;
    diagnostic.column = 7U;
    diagnostic.rule = "label";
    diagnostic.property = "float";
    diagnostic.detail = "float";
    require(format_theme_diagnostic(diagnostic) ==
                "app/app.theme.css:3:7: warning W107 property_not_admitted: float (in label, float)",
            "a finding as one line");
    ThemeCompileDiagnostic unplaced{};
    unplaced.code = "E601";
    unplaced.name = "manifest_invalid";
    require(format_theme_diagnostic(unplaced) == "error E601 manifest_invalid", "a finding without a place");
}

} // namespace

int main() {
    test_valid_package();
    test_refused_package();
    test_formatting();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "theme validation tests passed\n";
    return 0;
}
