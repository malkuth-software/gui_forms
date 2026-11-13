// Theme fonts (stage T4, owner ruling of 2026-11-04): the font longhands
// font-family (bundled families, then a role), font-size, font-weight, font-style and
// letter-spacing are theme properties of a control's primary text font.
// They cascade per field with provenance, may differ by colour scheme, are
// refused in a state rule, lose to a font set in code, and a change of the
// resolved font is a layout change.

#include "../src/core/theme_language/compiler/theme_compiler.hpp"
#include "support/theme_definition_heap.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/button.hpp"
#include "gui_forms/controls/group_box.hpp"
#include "gui_forms/controls/label.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/controls/text_box.hpp"
#include "gui_forms/dirty.hpp"
#include "gui_forms/engine/primitive_registry/primitive_registry.hpp"
#include "gui_forms/engine/theme_rules/theme_rules.hpp"
#include "gui_forms/engine/types/engine_types.hpp"
#include "gui_forms/theme/familiar_themes/familiar_themes.hpp"
#include "gui_forms/theme/stock_anatomies/stock_anatomies.hpp"
#include "gui_forms/theme/theme/theme.hpp"
#include "gui_forms/theme/types/theme_types.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/typography/font_families/font_families.hpp"
#include "gui_forms/window/presentation/colour_scheme.hpp"
#include "gui_forms/window/presentation/presentation_types.hpp"
#include "gui_forms/window/window.hpp"

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
        "#a { font-family: Selawik }",
        "#a { font-family: \"Selawik\" }",
        "#a { font-family: 'Portsmouth Rapids' }",
        "#a { font-family: Portsmouth Rapids }",
        "#a { font-family: carlito }",
        "#a { font-family: Selawik, content }",
        "#a { font-family: Selawik, Cousine, monospace }",
        "#a { font-family: 'Portsmouth Rapids', Carlito }",
        "#a { font-family: sans-serif }",
        "#a { font-family: serif }",
        "#a { font-family: system-ui }",
        "#a { font-family: ui-monospace }",
        "#a { font-family: cursive, fantasy }",
        "#a { font-family: Selawik, sans-serif, monospace }",
        "#a { font-family: monospace, Selawik }",
        "#a { font-family: emoji, Selawik }",
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
    // A family the registry does not have is passed over with a warning
    // (owner decision of 2026-11-13), never refused; GUI.Forms still draws
    // only bundled faces.
    const char* const warned[] = {
        "#a { font-family: Arial }",
        "#a { font-family: \"Lucida Grande\" }",
        "#a { font-family: \"sans-serif\" }",
        "#a { font-family: Arimo }",
        "#a { font-family: \"Segoe UI\" }",
        "#a { font-family: Comic Sans }",
        "#a { font-family: Arial, Selawik }",
        "#a { font-family: \"Lucida Grande\", monospace }",
    };
    for (const char* const text : warned) {
        const Compilation compiled = compile(text);
        require(compiled.outcome == CompileOutcome::compiled &&
                    has_code(compiled, DiagnosticCode::font_family_unavailable),
                std::string("warned, not refused: ") + text);
    }
    const char* const refused[] = {
        "#a { font-family: control, content }",
        "#a { font-family: control, Selawik }",
        "#a { font-family: Selawik, , Carlito }",
        "#a { font-family: Selawik, Carlito, Cousine, Portsmouth Rapids, Selawik, Carlito }",
        "#a { font-family: 12px }",
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
    const Compilation misplaced = compile("#a { font-family: content, Selawik }");
    require(has_code(misplaced, DiagnosticCode::value_form_not_admitted), "a role is admitted only last");
    // With no family available and no role, the declaration is dropped: the
    // rule keeps its other declarations.
    const Compilation dropped = compile("#a { font-family: \"Comic Sans\"; font-weight: bold }");
    bool family_declared = false;
    bool weight_declared = false;
    for (const ThemeRule& rule : dropped.sheet.rules) {
        for (const RecipeDeclaration& declaration : rule.recipe_fields) {
            if (declaration.field == RecipeField::font_family) family_declared = true;
            if (declaration.field == RecipeField::font_weight) weight_declared = true;
        }
    }
    require(dropped.outcome == CompileOutcome::compiled && !family_declared && weight_declared,
            "an unavailable family alone drops only its declaration");
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

// A named bundled family resolves into the font: Selawik as its numbered family over the
// content role, a role's face name as that role.
void test_named_family() {
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("root"));
    const std::shared_ptr<Label> label = make_control<Label>(StableId("caption"), "Caption");
    (*root).add_child(label);
    Window window(root, Size{300.0, 100.0});
    const FontSpec own = (*label).resolved_font();
    window.set_theme(theme_of("label { font-family: Selawik; font-weight: 600 }", "fonts-selawik"));
    const ResolvedFont selawik = (*label).resolved_text_font(own);
    require(selawik.font.family == selawik_font_family && selawik.font.role == FontRole::content &&
                selawik.font.weight == 600,
            "Selawik is its family over the content role");
    require(selawik.themed[static_cast<std::size_t>(FontField::family)], "provenance: the family from the sheet");
    window.perform_layout();
    window.set_theme(theme_of("label { font-family: \"Cousine\" }", "fonts-cousine"));
    require(((*label).dirty() & Dirty::measure) == Dirty::measure, "a change of family is a layout change");
    const ResolvedFont cousine = (*label).resolved_text_font(own);
    require(cousine.font.family.value == 0U && cousine.font.role == FontRole::monospace,
            "a role's face name is that role");
    // A list compiles to the first available family and a stack of the rest;
    // a family that is not bundled is passed over (owner decisions of
    // 2026-11-13).
    window.set_theme(theme_of("label { font-family: \"Arial\", Selawik, Cousine }", "fonts-stack"));
    const ResolvedFont stacked = (*label).resolved_text_font(own);
    const FontFamilyStack rest = font_family_stack(stacked.font.stack);
    require(stacked.font.family == selawik_font_family && stacked.font.role == FontRole::content &&
                rest.count == 1U && rest.families[0] == cousine_font_family,
            "a list is its first available family, then a stack of the rest, then the role");
    window.set_theme(theme_of("label { font-family: Carlito, Selawik, monospace }", "fonts-stack-role"));
    const ResolvedFont ended = (*label).resolved_text_font(own);
    require(ended.font.family == carlito_font_family && ended.font.role == FontRole::monospace &&
                font_family_stack(ended.font.stack).count == 1U,
            "a role written last is the final fallback, and a role's family in a list is a family");
    // CSS generics draw the bundled families our mapping chooses
    // (generic_font_families): sans-serif alone is the content role, a
    // generic in a list is a family, and monospace last is still the role.
    window.set_theme(theme_of("label { font-family: sans-serif }", "fonts-generic"));
    const ResolvedFont generic = (*label).resolved_text_font(own);
    require(generic.font.role == FontRole::content && generic.font.family.value == 0U,
            "sans-serif alone is the content family's role");
    window.set_theme(theme_of("label { font-family: Selawik, ui-monospace, serif, monospace }", "fonts-generic-list"));
    const ResolvedFont generics = (*label).resolved_text_font(own);
    const FontFamilyStack generic_rest = font_family_stack(generics.font.stack);
    require(generics.font.family == selawik_font_family && generics.font.role == FontRole::monospace &&
                generic_rest.count == 2U && generic_rest.families[0] == cousine_font_family &&
                generic_rest.families[1] == carlito_font_family,
            "generics in a list are their families, in order, before the role");
    // The stock Windows-familiar theme writes its text in Selawik (owner, 2026-11-10).
    window.set_theme(familiar_theme(FamiliarTheme::windows_professional));
    const ResolvedFont professional = (*label).resolved_text_font(own);
    require(professional.font.family == selawik_font_family && professional.font.size == 12.0,
            "windows-professional's text is Selawik at 12px");
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
    test_named_family();
    test_scheme_and_em();
    test_controls_read_it();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "theme font tests passed\n";
    return 0;
}
