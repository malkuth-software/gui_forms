// Theme variants (stage T4, the D1/D2 design note, owner-approved
// 2026-11-04): a theme declares its colour schemes with `color-scheme`; a
// window in a scheme its theme lacks shows GUI.Forms' own theme in that
// scheme, warns once (W403) and suspends the application's colours; the
// application may follow the host's scheme; colours set on controls warn
// (W404); env() reads the host's colour hints with a required fallback.

#include "../src/core/theme_language/compiler/theme_compiler.hpp"
#include "../src/core/theme_language/package/theme_manifest.hpp"
#include "support/theme_definition_heap.hpp"
#include "gui_forms/gui_forms.hpp"

#include <cstddef>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
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

Compilation compile(std::string_view text, SystemColourHints hints) {
    const PrimitiveRegistry& registry = builtin_primitive_registry();
    ThemeSource source{};
    source.file = "variants.theme.css";
    source.text = text;
    ThemeCompileInput input{.source = source,
                            .registry = registry,
                            .vocabulary = SelectorVocabulary{},
                            .published_values = {},
                            .resources = {},
                            .images = {},
                            .requires_engine = current_engine_version};
    input.system_colours = hints;
    ThemeCompiler compiler{};
    ThemeDiagnosticList diagnostics{};
    Compilation result{};
    result.outcome = compiler.compile(input, result.sheet, diagnostics);
    for (const ThemeDiagnostic& diagnostic : diagnostics.records()) result.diagnostics.push_back(diagnostic);
    return result;
}

Compilation compile(std::string_view text) {
    const Compilation result = compile(text, SystemColourHints{});
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

class Pad final : public Control {
public:
    explicit Pad(StableId stable_id) : Control(std::move(stable_id)) {}
};

void set_scheme(Window& window, ColourScheme scheme) {
    PresentationSettings settings = window.presentation_settings();
    settings.colour_scheme = scheme;
    window.set_presentation_settings(settings);
}

void test_declaring_schemes() {
    const Compilation both = compile(":root { color-scheme: light dark }");
    require(both.outcome == CompileOutcome::compiled && both.diagnostics.empty(), "light dark compiles cleanly");
    require(both.sheet.colour_schemes && (*both.sheet.colour_schemes).light && (*both.sheet.colour_schemes).dark,
            "light dark declares both variants");
    const Compilation dark = compile(":root { COLOR-SCHEME: Dark }");
    require(dark.sheet.colour_schemes && !(*dark.sheet.colour_schemes).light && (*dark.sheet.colour_schemes).dark,
            "dark alone, in any case");
    const Compilation none = compile("#pad { opacity: 0.5 }");
    require(!none.sheet.colour_schemes, "a sheet that says nothing declares nothing");

    const char* const refused[] = {
        ":root { color-scheme: only light }",
        ":root { color-scheme: normal }",
        ":root { color-scheme: light light }",
        ":root { color-scheme: dim }",
        "@media (prefers-color-scheme: dark) { :root { color-scheme: dark } }",
    };
    for (const char* const text : refused) {
        const Compilation compiled = compile(text);
        require(compiled.outcome == CompileOutcome::refused && !compiled.sheet.colour_schemes,
                std::string("refused: ") + text);
    }
    const Compilation outside = compile("#pad { color-scheme: dark }");
    require(outside.outcome == CompileOutcome::compiled && has_code(outside, DiagnosticCode::property_not_admitted),
            "color-scheme outside :root is dropped with a warning");

}

std::string hex_of(std::string_view bytes) {
    return theme_language::package_sha256_hex(theme_language::package_sha256(bytes));
}

ThemePackage package(std::string_view name, std::string_view sheet) {
    ThemePackage made{};
    made.manifest = "{\"format\": \"gui-forms-theme/1\", \"name\": \"" + std::string(name) +
                    "\", \"version\": \"1.0.0\", \"engine\": \">=0.3\", \"sheet\": {\"path\": "
                    "\"theme.theme.css\", \"sha256\": \"" + hex_of(sheet) + "\"}}";
    made.files.push_back(ThemePackageFile{"theme.theme.css", std::string(sheet)});
    return made;
}

bool warns(const ThemeCompileResult& result, std::string_view code) {
    for (const ThemeCompileDiagnostic& diagnostic : result.diagnostics()) {
        if (diagnostic.code == code) return true;
    }
    return false;
}

void test_dark_rules_without_variant() {
    ImageRegistry images{};
    const ThemeLibrary library{};
    const ThemeCompileResult unvaried = compile_theme_package(
        package("unvaried", "@media (prefers-color-scheme: dark) { #pad { opacity: 0.5 } }"), library, images,
        ThemeCompileOptions{});
    require(unvaried.succeeded() && warns(unvaried, "W402"), "dark rules without a dark variant warn");
    const ThemeCompileResult varied = compile_theme_package(
        package("varied", ":root { color-scheme: light dark } @media (prefers-color-scheme: dark) { #pad { opacity: 0.5 } }"),
        library, images, ThemeCompileOptions{});
    require(varied.succeeded() && varied.diagnostics().empty() && (*varied.theme()).supports(ColourScheme::dark),
            "dark rules with a dark variant compile cleanly, and the theme has the variant");
}

void test_theme_supports() {
    require((*default_theme()).supports(ColourScheme::light) && !(*default_theme()).supports(ColourScheme::dark),
            "the professional theme is light only");
    require((*gui_forms_theme()).supports(ColourScheme::light) && (*gui_forms_theme()).supports(ColourScheme::dark),
            "GUI.Forms' own theme has both variants");
    const std::shared_ptr<const Theme> dark = theme_from(compile(":root { color-scheme: dark }").sheet, "dark-only");
    require(!(*dark).supports(ColourScheme::light) && (*dark).supports(ColourScheme::dark),
            "a sheet's color-scheme is the theme's");
    const std::unique_ptr<ThemeDefinition> neither = test_support::professional_definition();
    (*neither).colour_schemes = ColourSchemeSet{.light = false, .dark = false};
    bool thrown = false;
    try {
        static_cast<void>(Theme::create(*neither));
    } catch (const std::invalid_argument&) {
        thrown = true;
    }
    require(thrown, "a theme with no variant is refused");
}

struct ThemeChanges final {
    std::size_t count{};
    std::string last{};
};

struct RecordThemeChange final {
    ThemeChanges* changes{};
    void operator()(const Theme& theme) const {
        (*changes).count = (*changes).count + 1U;
        (*changes).last = std::string(theme.id());
    }
};

void test_fallback() {
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("root"));
    const std::shared_ptr<Pad> pad = make_control<Pad>(StableId("pad"));
    (*root).add_child(pad);
    Window window(root, Size{200.0, 100.0});
    ThemeChanges changes{};
    SubscriptionToken subscription = window.theme_changed().subscribe(RecordThemeChange{&changes});
    require(!window.theme_fallback() && window.theme().id() == "windows-professional",
            "a light window shows its light theme");
    (*pad).back_color = Color::rgba(200, 30, 30);
    require((*pad).back_color_warned() && !(*pad).fore_color_warned(), "a colour set on a control is warned");
    require(!(*pad).application_colours_suspended(), "and applies under the application's theme");

    set_scheme(window, ColourScheme::dark);
    require(window.theme().id() == "gui-forms" && window.application_theme().id() == "windows-professional",
            "dark, the window shows GUI.Forms' own theme and keeps the application's");
    require(window.theme_fallback() && (*window.theme_fallback()).theme_id == "windows-professional" &&
                (*window.theme_fallback()).missing == ColourScheme::dark &&
                (*window.theme_fallback()).fallback_id == "gui-forms",
            "the fallback is queryable");
    require((*window.resources()).theme_variant_missing_reported("windows-professional", ColourScheme::dark),
            "the fallback is warned");
    require(&(*pad).effective_theme() == &window.theme(), "controls take the fallback");
    require((*pad).application_colours_suspended(), "the application's colours are suspended on the fallback");
    require(changes.count == 1U && changes.last == "gui-forms", "theme_changed is raised for the fallback");

    set_scheme(window, ColourScheme::light);
    require(!window.theme_fallback() && window.theme().id() == "windows-professional",
            "back to light, the application's theme returns");
    require(!(*pad).application_colours_suspended(), "and with it the application's colours");
    require(changes.count == 2U && changes.last == "windows-professional", "theme_changed again");

    // A theme with both variants needs no fallback.
    window.set_theme(gui_forms_theme());
    set_scheme(window, ColourScheme::dark);
    require(!window.theme_fallback() && window.theme().id() == "gui-forms", "a dark variant is shown");
    // A dark-only theme in a light window falls back to light.
    window.set_theme(theme_from(compile(":root { color-scheme: dark }").sheet, "dark-only"));
    require(!window.theme_fallback(), "a dark-only theme in a dark window is shown");
    set_scheme(window, ColourScheme::light);
    require(window.theme_fallback() && (*window.theme_fallback()).missing == ColourScheme::light &&
                window.theme().id() == "gui-forms",
            "a dark-only theme in a light window falls back to light");
}

void test_automatic_scheme() {
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("root"));
    Window window(root, Size{200.0, 100.0});
    CompositorState state{};
    state.generation = CompositorGeneration{1U};
    state.dark_scheme = true;
    window.set_compositor_state(state, true);
    require(window.presentation_settings().colour_scheme == ColourScheme::light,
            "off by default: the host's dark preference is not taken");
    ApplicationResources& resources = *window.resources();
    require(!resources.automatic_colour_scheme(), "automatic is off by default");
    resources.set_automatic_colour_scheme(true);
    require(window.presentation_settings().colour_scheme == ColourScheme::dark && window.theme_fallback(),
            "on, the window takes the host's last report");
    state.generation = CompositorGeneration{2U};
    state.dark_scheme = false;
    window.set_compositor_state(state, true);
    require(window.presentation_settings().colour_scheme == ColourScheme::light && !window.theme_fallback(),
            "and follows each later report");
    resources.set_automatic_colour_scheme(false);
    state.generation = CompositorGeneration{3U};
    state.dark_scheme = true;
    window.set_compositor_state(state, true);
    require(window.presentation_settings().colour_scheme == ColourScheme::light, "off again, it is not followed");
}

void test_system_colour_hints() {
    const std::string sheet = "#pad { color: env(system-accent, #102030) } "
                              "#other { color: env(system-highlight, red) }";
    const Compilation plain = compile(sheet);
    // A text colour from env() is not a role, so forced high contrast
    // cannot remap it (W201), as with a literal; nothing else is reported.
    bool only_unremappable = true;
    for (const ThemeDiagnostic& diagnostic : plain.diagnostics) {
        only_unremappable = only_unremappable && diagnostic.code == DiagnosticCode::literal_colour_unremappable;
    }
    require(plain.outcome == CompileOutcome::compiled && only_unremappable, "env() compiles, warning as a literal");
    SystemColourHints hints{};
    hints.accent = Color::rgba(1, 2, 3);
    const Compilation hinted = compile(sheet, hints);
    require(hinted.outcome == CompileOutcome::compiled && !(hinted.sheet == plain.sheet),
            "a hint changes what env() reads");

    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("root"));
    const std::shared_ptr<Pad> pad = make_control<Pad>(StableId("pad"));
    (*root).add_child(pad);
    Window window(root, Size{200.0, 100.0});
    window.set_theme(theme_from(plain.sheet, "env-plain"));
    const ResolvedControlRecipe plain_recipe =
        window.theme().resolve(*pad, ControlVisualRole::button, ControlVisualContext{});
    require(plain_recipe.recipe().text == Color::rgba(16, 32, 48), "without a hint, env() reads its fallback");
    window.set_theme(theme_from(hinted.sheet, "env-hinted"));
    const ResolvedControlRecipe hinted_recipe =
        window.theme().resolve(*pad, ControlVisualRole::button, ControlVisualContext{});
    require(hinted_recipe.recipe().text == Color::rgba(1, 2, 3), "with one, the host's accent");

    const char* const refused[] = {
        "#pad { color: env(system-accent) }",
        "#pad { color: env(safe-area-inset-top, #000) }",
        "#pad { color: env(system-accent, 4px) }",
        "#pad { color: env(system-accent #000) }",
    };
    for (const char* const text : refused) {
        require(compile(text).outcome == CompileOutcome::refused, std::string("refused: ") + text);
    }
}

} // namespace

int main() {
    test_declaring_schemes();
    test_dark_rules_without_variant();
    test_theme_supports();
    test_fallback();
    test_automatic_scheme();
    test_system_colour_hints();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "theme variant tests passed\n";
    return 0;
}
