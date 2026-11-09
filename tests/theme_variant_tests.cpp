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
#include <initializer_list>
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

// The sum of a colour's channels: a dark ground's is the smaller.
unsigned brightness(Color colour) noexcept {
    const unsigned sum = static_cast<unsigned>(colour.red) + static_cast<unsigned>(colour.green) +
                         static_cast<unsigned>(colour.blue);
    return sum;
}

// One stock control whose ground is checked, with its name for messages.
struct GroundCase final {
    std::string name{};
    std::shared_ptr<Panel> control{};
};

// T4 defect: controls that wrote the light theme's paper (or another light
// colour) as their background at construction stayed light in a dark
// window. Each ground is now the theme's role, so it resolves per scheme:
// no background override, a different ground in dark, and a darker one.
// A tab page has a transparent ground over its TabControl's display.
void test_control_grounds_follow_scheme() {
    std::vector<GroundCase> cases{};
    cases.push_back({"NumericUpDown", make_control<NumericUpDown>(StableId("ground.numeric"))});
    cases.push_back({"DomainUpDown", make_control<DomainUpDown>(StableId("ground.domain"))});
    cases.push_back({"TreeView", make_control<TreeView>(StableId("ground.tree"))});
    cases.push_back({"BreadcrumbTrail", make_control<BreadcrumbTrail>(StableId("ground.breadcrumb"))});
    cases.push_back({"ObjectView", make_control<ObjectView>(StableId("ground.objects"))});
    cases.push_back({"CorrespondenceView", make_control<CorrespondenceView>(StableId("ground.correspondence"))});
    cases.push_back({"DateTimePicker", make_control<DateTimePicker>(StableId("ground.date"))});
    cases.push_back({"PictureBox", make_control<PictureBox>(StableId("ground.picture"))});
    cases.push_back({"PropertyGrid", make_control<PropertyGrid>(StableId("ground.property-grid"))});
    cases.push_back({"PropertyList", make_control<PropertyList>(StableId("ground.property-list"))});
    cases.push_back({"InstrumentRack", make_control<InstrumentRack>(StableId("ground.rack"))});
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("ground.root"));
    for (const GroundCase& entry : cases) (*root).add_child(entry.control);
    const std::shared_ptr<TabPage> page = make_control<TabPage>(StableId("ground.page"), "Page");
    (*root).add_child(page);
    Window window(root, Size{400.0, 300.0});
    window.set_theme(gui_forms_theme());
    set_scheme(window, ColourScheme::light);
    std::vector<Color> light{};
    light.reserve(cases.size());
    for (const GroundCase& entry : cases) {
        require(!(*entry.control).has_background_override(), entry.name + ": no colour written at construction");
        light.push_back((*entry.control).background());
    }
    set_scheme(window, ColourScheme::dark);
    require(!window.theme_fallback() && window.theme().id() == "gui-forms", "the dark variant is shown");
    for (std::size_t index = 0U; index < cases.size(); ++index) {
        const Color dark = (*cases[index].control).background();
        require(dark != light[index], cases[index].name + ": the ground changes with the scheme");
        require(brightness(dark) < brightness(light[index]), cases[index].name + ": the dark ground is darker");
    }
    require((*page).background().alpha == 0U, "a tab page's ground is transparent over the display");
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

BackdropChain chain_of(std::initializer_list<BackdropOperation> operations) {
    BackdropChain chain{};
    for (const BackdropOperation& operation : operations) static_cast<void>(chain.append(operation));
    return chain;
}

// `window { backdrop: ... }` lowers onto the window's chain (owner decisions
// 2026-11-05): the winning rule for the window's media and its root's
// classes, a role tint in the scheme in force, lowered again when the scheme
// changes; `popup` rules wait for native popup windows; an application's
// own chain holds against the theme.
void test_window_backdrop() {
    const Compilation compiled =
        compile(":root { color-scheme: light dark; ink-on-paper: #1b2733 on #f4f6fa }\n"
                "window { backdrop: blur(12px) saturate(140%) tint(role(ink-on-paper) / 0.6) }\n"
                "window.plain { backdrop: none }\n"
                "popup { backdrop: tint(black) }\n"
                "@media (prefers-color-scheme: dark) { window { backdrop: tint(#202020 / 50%) } }\n");
    require(compiled.outcome == CompileOutcome::compiled && compiled.diagnostics.empty(),
            "the backdrop sheet compiles cleanly");
    const std::shared_ptr<const Theme> theme = theme_from(compiled.sheet, "backdrops");
    const BackdropChain light = chain_of({BackdropOperation::blur(12.0), BackdropOperation::saturate(1.4),
                                          BackdropOperation::tint(Color::rgba(0xf4, 0xf6, 0xfa, 0x99))});
    const BackdropChain dark = chain_of({BackdropOperation::tint(Color::rgba(0x20, 0x20, 0x20, 0x80))});

    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("root"));
    Window window(root, Size{200.0, 100.0});
    require(!window.backdrop_chain(), "a theme without window rules gives no chain");
    window.set_theme(theme);
    require(window.backdrop_chain() && *window.backdrop_chain() == light,
            "the window rule's chain, its role tint in the light scheme");
    set_scheme(window, ColourScheme::dark);
    require(window.backdrop_chain() && *window.backdrop_chain() == dark, "the dark scheme's rule wins in dark");
    set_scheme(window, ColourScheme::light);
    require(window.backdrop_chain() && *window.backdrop_chain() == light, "and light again");
    window.set_theme(gui_forms_theme());
    require(!window.backdrop_chain(), "another theme without window rules removes it");

    // A root with the class: `backdrop: none` wins, and no chain is set.
    const std::shared_ptr<Panel> plain_root = make_control<Panel>(StableId("plain-root"));
    std::vector<std::string> classes{};
    classes.push_back("plain");
    (*plain_root).set_classes(std::move(classes));
    Window plain(plain_root, Size{200.0, 100.0});
    plain.set_theme(theme);
    require(!plain.backdrop_chain(), "backdrop: none on the root's class removes the chain");

    // The application's chain holds against the scheme and the theme.
    const BackdropChain own = chain_of({BackdropOperation::tint(Color::rgba(10, 20, 30, 40))});
    window.set_theme(theme);
    window.set_backdrop_chain(own);
    set_scheme(window, ColourScheme::dark);
    window.set_theme(gui_forms_theme());
    require(window.backdrop_chain() && *window.backdrop_chain() == own, "the application's chain holds");
    window.set_backdrop_chain(std::nullopt);
    window.set_theme(theme);
    require(!window.backdrop_chain(), "an application's removal holds too");
}

} // namespace

int main() {
    test_control_grounds_follow_scheme();
    test_declaring_schemes();
    test_dark_rules_without_variant();
    test_theme_supports();
    test_fallback();
    test_automatic_scheme();
    test_system_colour_hints();
    test_window_backdrop();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "theme variant tests passed\n";
    return 0;
}
