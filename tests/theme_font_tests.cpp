// Theme fonts (stage T4, owner ruling of 2026-11-04): the font longhands
// font-family (a bundled font role), font-size, font-weight, font-style and
// letter-spacing are theme properties of a control's primary text font.
// They cascade per field with provenance, may differ by colour scheme, are
// refused in a state rule, lose to a font set in code, and a change of the
// resolved font is a layout change.

#include "../src/core/theme_language/compiler/theme_compiler.hpp"
#include "support/theme_definition_heap.hpp"
#include "gui_forms/gui_forms.hpp"

#include <cstddef>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace gui_forms;
using theme_language::CompileOutcome;
using theme_language::DiagnosticCode;
using theme_language::SelectorVocabulary;
using theme_language::ThemeCompileInput;
using theme_language::ThemeCompiler;
using theme_language::ThemeDiagnostic;
using theme_language::ThemeDiagnosticList;
using theme_language::ThemeSource;

int failures = 0;

void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

struct Compilation final {
    CompileOutcome outcome{CompileOutcome::refused};
    ThemeRuleSheet sheet{};
    std::vector<ThemeDiagnostic> diagnostics{};
};

Compilation compile(std::string_view text) {
    const PrimitiveRegistry& registry = builtin_primitive_registry();
    ThemeSource source{};
    source.file = "fonts.theme.css";
    source.text = text;
    ThemeCompileInput input{.source = source,
                            .registry = registry,
                            .vocabulary = SelectorVocabulary{stock_control_anatomies()},
                            .published_values = {},
                            .resources = {},
                            .images = {},
                            .requires_engine = current_engine_version};
    ThemeCompiler compiler{};
    ThemeDiagnosticList diagnostics{};
    Compilation result{};
    result.outcome = compiler.compile(input, result.sheet, diagnostics);
    for (const ThemeDiagnostic& diagnostic : diagnostics.records()) result.diagnostics.push_back(diagnostic);
    return result;
}

bool has_code(const Compilation& compilation, DiagnosticCode code) {
    for (const ThemeDiagnostic& diagnostic : compilation.diagnostics) {
        if (diagnostic.code == code) return true;
    }
    return false;
}

std::shared_ptr<const Theme> theme_from(const ThemeRuleSheet& sheet, std::string_view id) {
    const std::unique_ptr<ThemeDefinition> definition = test_support::professional_definition();
    (*definition).id = std::string(id);
    (*definition).rules = sheet;
    std::shared_ptr<const Theme> theme = Theme::create(*definition);
    return theme;
}

std::shared_ptr<const Theme> theme_of(std::string_view text, std::string_view id) {
    const Compilation compiled = compile(text);
    require(compiled.outcome == CompileOutcome::compiled, std::string("compiles: ") + std::string(text));
    std::shared_ptr<const Theme> theme = theme_from(compiled.sheet, id);
    return theme;
}

void set_scheme(Window& window, ColourScheme scheme) {
    PresentationSettings settings = window.presentation_settings();
    settings.colour_scheme = scheme;
    window.set_presentation_settings(settings);
}

void test_lowering() {
    const char* const admitted[] = {
        "#a { font-family: content }",
        "#a { font-family: MONOSPACE }",
        "#a { font-size: 18px }",
        "#a { font-size: 1.25em }",
        "#a { font-weight: bold }",
        "#a { font-weight: 300 }",
        "#a { font-style: italic }",
        "#a { letter-spacing: normal }",
        "#a { letter-spacing: -0.5px }",
        "#a { letter-spacing: 0.1em }",
        "#a { font-size: initial }",
        "@media (prefers-color-scheme: dark) { #a { font-weight: 600 } }",
    };
    for (const char* const text : admitted) {
        const Compilation compiled = compile(text);
        require(compiled.outcome == CompileOutcome::compiled && compiled.diagnostics.empty(),
                std::string("admitted: ") + text);
    }
    const char* const refused[] = {
        "#a { font-family: Arial }",
        "#a { font-family: \"Lucida Grande\" }",
        "#a { font-family: sans-serif }",
        "#a { font-family: control, content }",
        "#a { font-size: 2px }",
        "#a { font-size: 200px }",
        "#a { font-size: 50% }",
        "#a { font-weight: bolder }",
        "#a { font-weight: 450.5 }",
        "#a { font-weight: 1000 }",
        "#a { font-style: oblique }",
        "#a { letter-spacing: 200px }",
        "#a { font-size: inherit }",
    };
    for (const char* const text : refused) {
        require(compile(text).outcome == CompileOutcome::refused, std::string("refused: ") + text);
    }
    const Compilation stated = compile("#a:hover { font-size: 20px }");
    require(has_code(stated, DiagnosticCode::property_not_admitted), "a state rule's font is not admitted");
    // A named part's font is its own, in any state (look integration: a
    // selected tab's label): Theme::resolve_font with the part reads it.
    const Compilation part = compile("button::part(label):hover { font-weight: bold }");
    require(part.outcome == CompileOutcome::compiled && !has_code(part, DiagnosticCode::property_not_admitted),
            "a named part's font is admitted, in a state rule too");
    const Compilation left_out = compile("#a { font-variant: small-caps }");
    require(left_out.outcome == CompileOutcome::compiled, "the rest of the font family is still left out");
}

void test_resolution() {
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("root"));
    const std::shared_ptr<Label> label = make_control<Label>(StableId("caption"), "Caption");
    (*root).add_child(label);
    Window window(root, Size{300.0, 100.0});
    const FontSpec own = (*label).resolved_font();
    require((*label).text_font(own) == (*label).effective_font(own), "no rule: the control's own font");

    window.set_theme(theme_of("label { font-family: monospace; font-size: 20px } "
                              "#caption { font-weight: bold; letter-spacing: 0.1em } "
                              ".quiet { font-style: italic }",
                              "fonts"));
    const ResolvedFont resolved = (*label).resolved_text_font(own);
    require(resolved.font.role == FontRole::monospace && resolved.font.size == 20.0 &&
                resolved.font.weight == 700 && !resolved.font.italic,
            "the rules' fields stand over the control's font");
    require(resolved.font.letter_spacing == 2.0, "an em spacing is of the resolved size");
    const std::size_t family = static_cast<std::size_t>(FontField::family);
    const std::size_t weight = static_cast<std::size_t>(FontField::weight);
    const std::size_t style = static_cast<std::size_t>(FontField::style);
    require(resolved.themed[family] && resolved.levels[family] == CascadeLevel::theme_sheet,
            "provenance: the family from the type rule");
    require(resolved.themed[weight] && resolved.levels[weight] == CascadeLevel::theme_sheet,
            "provenance: the weight from a sheet rule (the stable-ID rule)");
    require(!resolved.themed[style], "provenance: an unset field is the control's own");

    window.perform_layout();
    (*label).classes = {"quiet"};
    require(((*label).dirty() & Dirty::measure) == Dirty::measure, "a class that changes the font is a layout change");
    require((*label).resolved_text_font(own).font.italic, "the class rule's style");
    require((*label).resolved_text_font(own).levels[style] == CascadeLevel::theme_sheet, "provenance: a sheet rule (the class rule)");

    (*label).font = FontSpec{FontRole::content, 11.0, 400, false};
    const ResolvedFont coded = (*label).resolved_text_font((*label).resolved_font());
    require(coded.from_code && coded.font == FontSpec{FontRole::content, 11.0, 400, false},
            "a font set in code wins over the theme");
    (*label).font = std::nullopt;
    require(!(*label).resolved_text_font((*label).resolved_font()).from_code, "cleared, the theme's again");
}

void test_scheme_and_em() {
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("root"));
    const std::shared_ptr<Label> label = make_control<Label>(StableId("caption"), "Caption");
    (*root).add_child(label);
    Window window(root, Size{300.0, 100.0});
    window.set_theme(theme_of(":root { color-scheme: light dark } "
                              "#caption { font-size: 1.5em } "
                              "@media (prefers-color-scheme: dark) { #caption { font-weight: 600 } }",
                              "fonts-schemes"));
    const FontSpec own = (*label).resolved_font();
    const ResolvedFont light = (*label).resolved_text_font(own);
    require(light.font.size == own.size * 1.5 && light.font.weight == own.weight, "an em size is of the own size");
    window.perform_layout();
    set_scheme(window, ColourScheme::dark);
    require(((*label).dirty() & Dirty::measure) == Dirty::measure, "a scheme switch measures again");
    require((*label).resolved_text_font(own).font.weight == 600, "the dark variant's font");

    window.set_theme(theme_of("#caption { font-size: 10em }", "fonts-too-large"));
    const ResolvedFont refused = (*label).resolved_text_font(own);
    require(refused.font == own && !refused.themed[static_cast<std::size_t>(FontField::size)],
            "a size outside the bounds once resolved keeps the control's own font");
}

void test_controls_read_it() {
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("root"));
    const std::shared_ptr<Button> button = make_control<Button>(StableId("go"), "Go");
    const std::shared_ptr<GroupBox> group = make_control<GroupBox>(StableId("group"));
    const std::shared_ptr<TextBox> box = make_control<TextBox>(StableId("field"));
    (*root).add_child(button);
    (*root).add_child(group);
    (*root).add_child(box);
    Window window(root, Size{400.0, 300.0});
    window.perform_layout();
    const Size before = (*button).get_preferred_size(Size{400.0, 300.0});
    window.set_theme(theme_of("button { font-size: 24px } group-box { font-size: 22px } text-box { font-size: 21px }",
                              "fonts-controls"));
    window.perform_layout();
    const Size after = (*button).get_preferred_size(Size{400.0, 300.0});
    require(after.height > before.height && after.width > before.width, "a button measures its themed text");
    require((*button).text_font((*button).font.read()).size == 24.0 * (*button).effective_text_scale(),
            "a button's text font");
    require((*group).text_font((*group).font.read()).size == 22.0 * (*group).effective_text_scale(),
            "a group box's caption font");
    require((*box).text_font((*box).font.read()).size == 21.0 * (*box).effective_text_scale(), "a text box's font");
}

} // namespace

int main() {
    test_lowering();
    test_resolution();
    test_scheme_and_em();
    test_controls_read_it();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "theme font tests passed\n";
    return 0;
}
