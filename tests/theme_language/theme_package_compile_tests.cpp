// The public seam (stage T3, phase P10, owner decisions of 2026-10-28):
// compile_theme_package compiles a package with its `@import` closure as
// one cascade and gives a Theme or a refusal with its diagnostics, each
// named for its own file; the caller applies the theme through the atomic
// Window::set_theme. Gate (RC003): a refused theme leaves the running
// theme and the window's focus, capture, selection and edit unchanged, and
// a good swap preserves them too.

#include "../../src/core/theme_language/package/theme_manifest.hpp"
#include "gui_forms/engine/effect_registry/effect_registry.hpp"
#include "gui_forms/gui_forms.hpp"

#include <array>
#include <cstddef>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace gui_forms;

int failures = 0;

void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

// A 2 × 2 RGBA PNG.
const std::string tiny_png("\x89\x50\x4e\x47\x0d\x0a\x1a\x0a\x00\x00\x00\x0d\x49\x48\x44\x52\x00\x00\x00\x02\x00\x00"
                           "\x00\x02\x08\x06\x00\x00\x00\x72\xb6\x0d\x24\x00\x00\x00\x12\x49\x44\x41\x54\x78\x9c\x63"
                           "\xf8\xcf\xc0\xf0\x1f\x84\x19\xa0\xf4\x7f\x00\x43\xce\x07\xf9\xfa\x81\x4a\xfd\x00\x00\x00"
                           "\x00\x49\x45\x4e\x44\xae\x42\x60\x82",
                           75U);

// Another 2 × 2 RGBA PNG.
const std::string green_png("\x89\x50\x4e\x47\x0d\x0a\x1a\x0a\x00\x00\x00\x0d\x49\x48\x44\x52\x00\x00"
                            "\x00\x02\x00\x00\x00\x02\x08\x06\x00\x00\x00\x72\xb6\x0d\x24\x00\x00\x00\x0e"
                            "\x49\x44\x41\x54\x78\x9c\x63\x60\xf8\x0f\x85\x30\x06\x00\x43\xce\x07\xf9\xf7"
                            "\xa2\xe2\xef\x00\x00\x00\x00\x49\x45\x4e\x44\xae\x42\x60\x82",
                            71U);

constexpr std::array<PartDeclaration, 1> pad_parts{{
    {.name = "face", .role = PartRole::face, .composed_from = {}, .states = {}},
}};
const std::array<ControlAnatomy, 1> controls{{
    {.control_id = "button", .parts = pad_parts, .states = {}, .custom_states = {}, .item_systems = {}},
}};

std::string hex_of(std::string_view bytes) {
    return theme_language::package_sha256_hex(theme_language::package_sha256(bytes));
}

// What a package holds besides its sheet.
struct Extras final {
    std::vector<std::pair<std::string, std::string>> pins{};
    std::string resource{};
    std::string published{};
    std::string coverage{};
};

ThemePackage package(std::string_view name, std::string_view sheet, const Extras& extras = {}) {
    ThemePackage made{};
    made.manifest = "{\"format\": \"gui-forms-theme/1\", \"name\": \"" + std::string(name) +
                    "\", \"version\": \"1.0.0\", \"engine\": \">=0.3\", \"sheet\": {\"path\": \"theme.theme.css\", "
                    "\"sha256\": \"" + hex_of(sheet) + "\"}";
    if (!extras.pins.empty()) {
        made.manifest += ", \"imports\": {";
        for (std::size_t index = 0U; index < extras.pins.size(); ++index) {
            if (index != 0U) made.manifest += ", ";
            made.manifest += "\"" + extras.pins[index].first + "\": \"" + extras.pins[index].second + "\"";
        }
        made.manifest += "}";
    }
    if (!extras.resource.empty()) {
        made.manifest += ", \"resources\": [{\"name\": \"icon.check\", \"path\": \"check.png\", \"sha256\": \"" +
                         hex_of(extras.resource) + "\"}]";
        made.files.push_back(ThemePackageFile{"check.png", extras.resource});
    }
    if (!extras.published.empty()) made.manifest += ", \"published_values\": [\"" + extras.published + "\"]";
    if (!extras.coverage.empty()) made.manifest += ", \"coverage\": " + extras.coverage;
    made.manifest += "}";
    made.files.push_back(ThemePackageFile{"theme.theme.css", std::string(sheet)});
    return made;
}

ThemeCompileOptions options() {
    ThemeCompileOptions made{};
    made.controls = controls;
    // These expectations were written onto windows-professional's recipes;
    // the default base is gui-forms (T4 phase 6).
    made.base = ThemeBase::windows_professional;
    return made;
}

bool has(const ThemeCompileResult& result, std::string_view code, std::string_view file, std::uint32_t line,
         std::string_view detail) {
    for (const ThemeCompileDiagnostic& diagnostic : result.diagnostics()) {
        if (diagnostic.code == code && diagnostic.file == file && diagnostic.line == line &&
            (detail.empty() || diagnostic.detail == detail)) {
            return true;
        }
    }
    for (const ThemeCompileDiagnostic& diagnostic : result.diagnostics()) {
        std::cerr << "  got " << diagnostic.code << " " << diagnostic.name << " in " << diagnostic.file << ":"
                  << diagnostic.line << ":" << diagnostic.column << " (" << diagnostic.detail << ")\n";
    }
    return false;
}

class Pad final : public Control {
public:
    explicit Pad(StableId stable_id) : Control(std::move(stable_id)) {}
};

ResolvedControlRecipe resolved(const Theme& theme, const Control& control) {
    return theme.resolve(control, ControlVisualRole::button, ControlVisualContext{});
}

void test_one_package() {
    ImageRegistry images{};
    const ThemeLibrary library{};
    const ThemeCompileResult solo = compile_theme_package(package("solo", "#pad { outline: 3px solid }"), library,
                                                          images, options());
    require(solo.succeeded() && solo.diagnostics().empty() && (*solo.theme()).id() == "solo",
            "a package compiles to a theme");
    const std::shared_ptr<Pad> pad = make_control<Pad>(StableId("pad"));
    if (solo.succeeded()) {
        const ResolvedControlRecipe recipe = resolved(*solo.theme(), *pad);
        require(recipe.recipe().focus_width == 3.0, "its rules apply");
    }
    const ThemeCompileResult refused =
        compile_theme_package(package("solo", "#pad {}\n#pad { opacity: 2 }"), library, images, options());
    require(!refused.succeeded() && has(refused, "E403", "solo/theme.theme.css", 2U, ""),
            "a refusal names the sheet and line");
    ThemePackage edited = package("solo", "#pad { opacity: 1 }");
    edited.files[0].bytes += " ";
    const ThemeCompileResult tampered = compile_theme_package(edited, library, images, options());
    require(!tampered.succeeded() && has(tampered, "E602", "theme.theme.css", 0U, "theme.theme.css"),
            "a file whose hash fails is E602");
    const ThemeCompileResult covered = compile_theme_package(
        package("solo", "#pad { opacity: 1 }", Extras{.coverage = "[\"button\", \"rocket\"]"}), library, images,
        options());
    require(covered.succeeded() && covered.diagnostics().size() == 1U &&
                has(covered, "W106", "solo/theme.json", 0U, "rocket"),
            "coverage of an unknown control type is W106, and the theme compiles");
}

void test_published_values() {
    ImageRegistry images{};
    const ThemeLibrary library{};
    const ThemePackage reading = package("solo", "#pad { opacity: 1 }", Extras{.published = "progress"});
    const ThemeCompileResult unknown = compile_theme_package(reading, library, images, options());
    require(!unknown.succeeded() && has(unknown, "E606", "solo/theme.json", 0U, "progress"),
            "a published value the application does not publish is E606");
    const std::array<std::string, 1> offered{"progress"};
    ThemeCompileOptions publishing = options();
    publishing.published_values = offered;
    require(compile_theme_package(reading, library, images, publishing).succeeded(),
            "one it publishes compiles");
}

void test_resources() {
    ImageRegistry images{};
    const ThemeLibrary library{};
    const std::string sheet = "#pad::part(face) { background-image: url(\"resource:icon.check\") }";
    const ThemeCompileResult drawn =
        compile_theme_package(package("solo", sheet, Extras{.resource = tiny_png}), library, images, options());
    require(drawn.succeeded() && images.image_ids().size() == 1U, "a resource is loaded as an image");
    ImageRegistry untouched{};
    const ThemeCompileResult bad = compile_theme_package(
        package("solo", sheet + "\n#pad { opacity: 2 }", Extras{.resource = tiny_png}), library, untouched,
        options());
    require(!bad.succeeded() && untouched.image_ids().empty(), "a refused compilation leaves no image behind");
    const ThemeCompileResult broken =
        compile_theme_package(package("solo", sheet, Extras{.resource = "not a png"}), library, untouched, options());
    require(!broken.succeeded() && untouched.image_ids().empty() && !broken.diagnostics().empty() &&
                broken.diagnostics()[0].code == "E601" && broken.diagnostics()[0].file == "solo/theme.json",
            "a resource that is not an image is E601");
}

void test_closure() {
    ImageRegistry images{};
    ThemeLibrary library{};
    const ThemePackage base = package("base",
                                      ":root { signal-on-paper: #a01010 on #ffffff }\n"
                                      "#pad { outline: 1px solid; color: role(signal-on-paper) }\n"
                                      "#pad { frobnicate: 1 }");
    require(library.install(base).empty() && library.contains("base") && library.size() == 1U,
            "a package is installed");
    const ThemePackage top = package("top",
                                     "@import \"base\";\n"
                                     ":root { signal-on-paper: #10a010 on #ffffff }",
                                     Extras{.pins = {{"base", hex_of(base.manifest)}}});
    const ThemeCompileResult cascade = compile_theme_package(top, library, images, options());
    require(cascade.succeeded(), "a closure compiles");
    require(has(cascade, "W101", "base/theme.theme.css", 3U, "frobnicate"),
            "a warning names the imported sheet and its own line");
    const std::shared_ptr<Pad> pad = make_control<Pad>(StableId("pad"));
    if (cascade.succeeded()) {
        const ResolvedControlRecipe recipe = resolved(*cascade.theme(), *pad);
        require(recipe.recipe().focus_width == 1.0, "the imported sheet's rules apply");
        require(recipe.recipe().text == Color::rgba(0x10U, 0xA0U, 0x10U, 255U),
                "the importing sheet's later role definition wins");
    }
    const ThemeCompileResult stale = compile_theme_package(
        package("top", "@import \"base\";", Extras{.pins = {{"base", hex_of("an older base")}}}), library, images,
        options());
    require(!stale.succeeded() && has(stale, "E604", "top/theme.theme.css", 1U, "base"),
            "an import other than its pin is E604");
    require(compile_installed_theme("base", library, images, options()).succeeded(),
            "an installed package compiles by name");
    const ThemeCompileResult nameless = compile_installed_theme("nothing", library, images, options());
    require(!nameless.succeeded() && !nameless.diagnostics().empty() && nameless.diagnostics()[0].code == "E603",
            "an unknown name is E603");
    ThemePackage broken = base;
    broken.manifest = "{}";
    const std::vector<ThemeCompileDiagnostic> refused = library.install(broken);
    require(refused.size() == 1U && refused[0].code == "E601" && library.size() == 1U,
            "a refused install changes nothing");
    require(library.remove("base") && !library.contains("base") && library.size() == 0U, "a package is removed");
}

// A compiled theme owns its images (owner decision of 2026-10-28): the
// last theme holding an image removes it from the registry; themes loading
// the same bytes into one registry share one image; repeated swaps leave
// the registry the same size.
void test_image_ownership() {
    const std::string sheet = "#pad::part(face) { background-image: url(\"resource:icon.check\") }";
    const ThemeLibrary library{};
    ImageRegistry images{};
    std::shared_ptr<const Theme> first =
        compile_theme_package(package("first", sheet, Extras{.resource = tiny_png}), library, images, options())
            .theme();
    std::shared_ptr<const Theme> same =
        compile_theme_package(package("same", sheet, Extras{.resource = tiny_png}), library, images, options())
            .theme();
    std::shared_ptr<const Theme> other =
        compile_theme_package(package("other", sheet, Extras{.resource = green_png}), library, images, options())
            .theme();
    require(first != nullptr && same != nullptr && other != nullptr && images.image_ids().size() == 2U,
            "two themes loading the same bytes share one image");
    first.reset();
    require(images.image_ids().size() == 2U, "an image stays while a theme holds it");
    same.reset();
    require(images.image_ids().size() == 1U, "and goes with the last");
    other.reset();
    require(images.image_ids().empty(), "a released theme leaves no image");

    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("root"));
    const std::shared_ptr<Pad> pad = make_control<Pad>(StableId("pad"));
    (*root).add_child(pad);
    Window window(root, Size{200.0, 100.0});
    for (std::size_t swap = 0U; swap < 24U; ++swap) {
        const bool green = swap % 2U == 1U;
        std::shared_ptr<const Theme> next =
            compile_theme_package(package("swap", sheet, Extras{.resource = green ? green_png : tiny_png}), library,
                                  images, options())
                .theme();
        require(next != nullptr, "each swap compiles");
        if (next != nullptr) window.set_theme(next);
        next.reset();
        require(images.image_ids().size() == 1U, "each swap leaves the registry one image");
    }
    window.set_theme(default_theme());
    require(images.image_ids().empty(), "and none once the window drops the theme");

    ImageRegistry moved_from{};
    std::shared_ptr<const Theme> travelling =
        compile_theme_package(package("moved", sheet, Extras{.resource = tiny_png}), library, moved_from, options())
            .theme();
    ImageRegistry moved_to(std::move(moved_from));
    require(travelling != nullptr && moved_to.image_ids().size() == 1U, "the image moves with its registry");
    travelling.reset();
    require(moved_to.image_ids().empty(), "and is removed from where it went");

    std::shared_ptr<const Theme> outliving{};
    {
        ImageRegistry brief{};
        outliving =
            compile_theme_package(package("brief", sheet, Extras{.resource = tiny_png}), library, brief, options())
                .theme();
        require(outliving != nullptr, "a theme may outlive its registry");
    }
    outliving.reset();
}

// RC003: a refused theme changes nothing; a good swap keeps the window's
// focus, capture, selection and edit.
void test_atomic_swap() {
    ImageRegistry images{};
    const ThemeLibrary library{};
    const ThemeCompileResult first =
        compile_theme_package(package("first", "#pad { outline: 1px solid }"), library, images, options());
    require(first.succeeded(), "the first theme compiles");
    if (!first.succeeded()) return;

    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("root"));
    const std::shared_ptr<TextBox> edit = make_control<TextBox>(StableId("edit"), "hello world");
    const std::shared_ptr<Pad> pad = make_control<Pad>(StableId("pad"));
    (*root).add_child(edit);
    (*root).add_child(pad);
    Window window(root, Size{400.0, 300.0});
    window.set_theme(first.theme());
    require(window.request_focus(edit), "the edit takes focus");
    (*edit).select(Utf8Offset{6U}, Utf8Offset{11U});
    window.capture_pointer(pad);
    const Theme* const running = &window.theme();

    const ThemeCompileResult refused =
        compile_theme_package(package("second", "#pad { opacity: 2 }"), library, images, options());
    require(!refused.succeeded() && refused.theme() == nullptr, "the second theme is refused");
    if (refused.succeeded()) window.set_theme(refused.theme());
    require(&window.theme() == running, "a refused theme leaves the running theme");
    require(window.focused_control() == edit && window.captured_control() == pad &&
                (*edit).selected_text() == "world" && (*edit).text.read() == "hello world",
            "and the focus, capture, selection and edit");

    const ThemeCompileResult good =
        compile_theme_package(package("second", "#pad { outline: 4px solid }"), library, images, options());
    require(good.succeeded(), "the corrected theme compiles");
    if (good.succeeded()) window.set_theme(good.theme());
    require(window.theme().id() == "second", "a good theme replaces it");
    require(window.focused_control() == edit && window.captured_control() == pad &&
                (*edit).selected_text() == "world" && (*edit).text.read() == "hello world",
            "keeping the focus, capture, selection and edit");
    const ResolvedControlRecipe recipe = resolved(window.theme(), *pad);
    require(recipe.recipe().focus_width == 4.0, "and its rules apply");
}

} // namespace

// The base theme (T4 phase 6): unset, a package compiles onto the default
// base, gui-forms; with ThemeBase::windows_professional, onto that theme's
// recipes alone; with ThemeBase::gui_forms, onto GUI.Forms' own theme, its
// light sheet first in the cascade, and a diagnostic in the package's sheet
// is still named for that sheet's line.
void test_base_theme() {
    ImageRegistry images{};
    const ThemeLibrary library{};
    std::vector<ControlAnatomy> stock(stock_control_anatomies().begin(), stock_control_anatomies().end());
    stock.push_back(controls[0]);
    ThemeCompileOptions unset{};
    unset.controls = stock;
    require(unset.base == default_theme_base && default_theme_base == ThemeBase::gui_forms,
            "unset, the base is the process default's, gui-forms");
    const ThemeCompileResult defaulted =
        compile_theme_package(package("based", "#pad { outline: 3px solid }"), library, images, unset);
    require(defaulted.succeeded() && defaulted.diagnostics().empty() &&
                (*defaulted.theme()).definition().dark_variant == ThemeDarkVariant::derived &&
                (*defaulted.theme()).definition().compatibility.face == gui_forms_theme_definition().compatibility.face,
            "unset, a package compiles onto gui-forms' recipes and dark variant");
    const ThemeCompileResult plain = compile_theme_package(package("based", "#pad { outline: 3px solid }"), library,
                                                           images, options());
    require(plain.succeeded() && plain.diagnostics().empty(), "a package compiles onto windows-professional");
    if (plain.succeeded()) {
        const ThemeDefinition& definition = (*plain.theme()).definition();
        require(definition.dark_variant == ThemeDarkVariant::none &&
                    definition.compatibility.face == windows_professional_theme_definition().compatibility.face,
                "with windows_professional, the base is windows-professional's recipes");
    }
    ThemeCompileOptions onto = options();
    onto.controls = stock;
    onto.base = ThemeBase::gui_forms;
    const ThemeCompileResult based =
        compile_theme_package(package("based", "#pad { outline: 3px solid }"), library, images, onto);
    require(based.succeeded() && based.diagnostics().empty(), "a package compiles onto gui-forms");
    if (based.succeeded()) {
        const ThemeDefinition& definition = (*based.theme()).definition();
        require(definition.dark_variant == ThemeDarkVariant::derived &&
                    definition.compatibility.face == gui_forms_theme_definition().compatibility.face &&
                    (*based.theme()).id() == "based",
                "with gui_forms, the base is gui-forms' recipes and dark variant, under the package's id");
        const std::shared_ptr<Pad> pad = make_control<Pad>(StableId("pad"));
        const ResolvedControlRecipe recipe = resolved(*based.theme(), *pad);
        require(recipe.recipe().focus_width == 3.0, "the package's rules apply over it");
    }
    const ThemeCompileResult reads_role = compile_theme_package(
        package("based", "#pad { color: role(ink-on-near-white-stock foreground) }"), library, images, onto);
    require(reads_role.succeeded() && reads_role.diagnostics().empty(),
            "the package reads a colour role gui-forms' light sheet defines");
    // The signal group of design language 5.4 (Plan Paint's problem text
    // reads signal-on-paper): gui-forms' palette defines it.
    const ThemeCompileResult reads_signal = compile_theme_package(
        package("based", "#pad { color: role(signal-on-paper) }"), library, images, onto);
    require(reads_signal.succeeded() && reads_signal.diagnostics().empty(),
            "the package reads signal-on-paper from gui-forms' palette");
    if (reads_signal.succeeded()) {
        const std::shared_ptr<Pad> pad = make_control<Pad>(StableId("pad"));
        const ResolvedControlRecipe signal = resolved(*reads_signal.theme(), *pad);
        require(signal.recipe().text == Color::rgba(0xb4U, 0x41U, 0x3cU, 255U),
                "signal-on-paper is the palette's deepened coral");
    }
    const ThemeCompileResult refused =
        compile_theme_package(package("based", "#pad {}\n#pad { opacity: 2 }"), library, images, onto);
    require(!refused.succeeded() && has(refused, "E403", "based/theme.theme.css", 2U, ""),
            "a refusal over gui-forms still names the package's sheet and line");
}

// An application's theme (APP_WINDOW_RULES_DESIGN.md section A): the base,
// then the packages in order as one cascade, the later overriding, under
// the last package's id; no package gives the base alone; two of one name
// are refused; a finding names its own package's sheet. The process
// default is built once.
void test_application_theme() {
    ImageRegistry images{};
    const ThemeLibrary library{};
    std::vector<ControlAnatomy> stock(stock_control_anatomies().begin(), stock_control_anatomies().end());
    stock.push_back(controls[0]);
    ThemeCompileOptions onto = options();
    onto.controls = stock;
    onto.base = ThemeBase::gui_forms;
    const std::array<ThemePackage, 2> layered{
        {package("first", "#pad { outline: 3px solid }"), package("second", "#pad { outline: 5px solid }")}};
    const ThemeCompileResult both = compile_theme_packages(layered, library, images, onto);
    require(both.succeeded() && both.diagnostics().empty(), "two packages compile onto gui-forms");
    if (both.succeeded()) {
        require((*both.theme()).id() == "second" &&
                    (*both.theme()).definition().dark_variant == ThemeDarkVariant::derived,
                "the theme is named after the last package, on gui-forms' recipes");
        const std::shared_ptr<Pad> pad = make_control<Pad>(StableId("pad"));
        const ResolvedControlRecipe recipe = resolved(*both.theme(), *pad);
        require(recipe.recipe().focus_width == 5.0, "the later package overrides");
        // The base sheet is CSS's user-agent origin under the packages
        // (selector step 1): its rules at the base levels, theirs above.
        const std::vector<ThemeRule>& rules = (*both.theme()).definition().rules.rules;
        std::size_t base_rules = 0U;
        for (const ThemeRule& rule : rules) {
            if (rule.level == CascadeLevel::base_sheet || rule.level == CascadeLevel::important_base_sheet) ++base_rules;
        }
        require(base_rules > 0U && !rules.empty() && rules.back().level == CascadeLevel::theme_sheet,
                "the base's rules at the base levels, the packages' at the sheet's");
    }
    const ThemeCompileResult alone = compile_theme_packages(std::span<const ThemePackage>{}, library, images, onto);
    require(alone.succeeded() && alone.diagnostics().empty() && (*alone.theme()).id() == "gui-forms",
            "no package gives gui-forms alone");
    if (alone.succeeded()) {
        bool own = true;
        for (const ThemeRule& rule : (*alone.theme()).definition().rules.rules) {
            if (rule.level == CascadeLevel::base_sheet || rule.level == CascadeLevel::important_base_sheet) own = false;
        }
        require(own, "alone, the base is the program's own sheet");
    }
    const ThemeCompileResult plain =
        compile_theme_packages(std::span<const ThemePackage>{}, library, images, options());
    require(plain.succeeded() && (*plain.theme()).id() == "windows-professional" &&
                (*plain.theme()).definition().dark_variant == ThemeDarkVariant::none,
            "no package onto windows-professional gives it alone");
    const std::array<ThemePackage, 2> twice{{package("same", "#pad {}"), package("same", "#pad {}")}};
    const ThemeCompileResult doubled = compile_theme_packages(twice, library, images, onto);
    require(!doubled.succeeded() && has(doubled, "E601", "same/theme.json", 0U, "listed twice"),
            "two packages of one name are refused");
    const std::array<ThemePackage, 2> faulty{
        {package("first", "#pad {}"), package("second", "#pad {}\n#pad { opacity: 2 }")}};
    const ThemeCompileResult refused = compile_theme_packages(faulty, library, images, onto);
    require(!refused.succeeded() && has(refused, "E403", "second/theme.theme.css", 2U, ""),
            "a refusal names its own package's sheet and line");
    const std::shared_ptr<const Theme> process = default_compiled_theme();
    require(process != nullptr && process == default_compiled_theme() && (*process).id() == (*default_theme()).id(),
            "the process default is built once, from the default base");
}

// Effect names against the registry (EP-3): given ThemeCompileOptions::
// effects, a package naming an effect the registry does not hold is refused
// with E417 at the name, in its own sheet, on either seam; a held name
// compiles; without a registry, only the name's form is checked.
void test_effect_names() {
    ImageRegistry images{};
    const ThemeLibrary library{};
    ThemeCompileOptions checked = options();
    checked.effects = &built_in_effect_registry();
    const ThemeCompileResult unknown =
        compile_theme_package(package("fx", "#pad {}\n#pad { effect: wobble-band }"), library, images, checked);
    require(!unknown.succeeded() && unknown.theme() == nullptr &&
                has(unknown, "E417", "fx/theme.theme.css", 2U, "wobble-band"),
            "an effect the registry does not hold refuses the package with E417 at its line");
    bool at_name = false;
    for (const ThemeCompileDiagnostic& diagnostic : unknown.diagnostics()) {
        if (diagnostic.code == "E417") at_name = diagnostic.column == 16U && diagnostic.name == "effect_unknown";
    }
    require(at_name, "E417 is at the effect's name, named effect_unknown");

    const ThemeCompileResult known =
        compile_theme_package(package("fx", "#pad { effect: progress-pulse }"), library, images, checked);
    require(known.succeeded() && known.diagnostics().empty(), "an effect the registry holds compiles");

    const ThemeCompileResult unchecked =
        compile_theme_package(package("fx", "#pad { effect: wobble-band }"), library, images, options());
    require(unchecked.succeeded(), "without a registry, a well-formed effect name compiles");

    std::vector<ControlAnatomy> stock(stock_control_anatomies().begin(), stock_control_anatomies().end());
    stock.push_back(controls[0]);
    ThemeCompileOptions onto = checked;
    onto.controls = stock;
    onto.base = ThemeBase::gui_forms;
    const std::array<ThemePackage, 2> layered{
        {package("first", "#pad { effect: progress-pulse }"), package("second", "#pad { effect: wobble-band }")}};
    const ThemeCompileResult layers = compile_theme_packages(layered, library, images, onto);
    require(!layers.succeeded() && has(layers, "E417", "second/theme.theme.css", 1U, "wobble-band"),
            "the application seam refuses the package naming the unknown effect");
}

// Provenance per field (T4 cascade spec, item 5): each rule of a closure
// compiled onto gui-forms is placed in its own sheet with its layer, its
// pack and the pack's manifest hash; a control's recipe fields name the
// rule that won each, and an unset field is the built-in recipe's.
void test_provenance() {
    ImageRegistry images{};
    ThemeLibrary library{};
    const ThemePackage base = package("base", "#pad { outline: 1px solid }\n#pad { color: #a01010 }");
    require(library.install(base).empty(), "the imported package is installed");
    const ThemePackage top = package("top", "@import \"base\";\n\n#pad { outline: 2px solid }",
                                     Extras{.pins = {{"base", hex_of(base.manifest)}}});
    std::vector<ControlAnatomy> stock(stock_control_anatomies().begin(), stock_control_anatomies().end());
    stock.push_back(controls[0]);
    ThemeCompileOptions onto = options();
    onto.controls = stock;
    onto.base = ThemeBase::gui_forms;
    const std::array<ThemePackage, 1> programs{{top}};
    const ThemeCompileResult result = compile_theme_packages(programs, library, images, onto);
    require(result.succeeded(), "the closure compiles onto gui-forms");
    if (!result.succeeded()) return;
    const Theme& theme = *result.theme();
    bool gui_forms_placed = false;
    bool base_placed = false;
    bool top_placed = false;
    for (const ThemeRule& rule : theme.definition().rules.rules) {
        const RuleProvenance& provenance = rule.provenance;
        if (provenance.pack == "gui-forms") {
            gui_forms_placed = provenance.file == "gui-forms/light.css" &&
                               provenance.layer == ThemeLayer::library_pack && provenance.pack_hash.empty() &&
                               provenance.line >= 1U;
        }
        if (provenance.pack == "base" && provenance.line == 2U) {
            base_placed = provenance.file == "base/theme.theme.css" && provenance.layer == ThemeLayer::library_pack &&
                          provenance.pack_hash == hex_of(base.manifest) && provenance.column >= 1U;
        }
        if (provenance.pack == "top") {
            top_placed = provenance.file == "top/theme.theme.css" && provenance.layer == ThemeLayer::project_theme &&
                         provenance.line == 3U;
        }
    }
    require(gui_forms_placed, "the base sheet's rules are a library pack's, in gui-forms/light.css");
    require(base_placed, "an imported package's rule is a library pack's, at its own line, with its hash");
    require(top_placed, "the program's package is the project theme, at its own line");
    const std::shared_ptr<Pad> pad = make_control<Pad>(StableId("pad"));
    CompiledThemeRules::PartProvenance fields{};
    theme.provenance(*pad, ControlVisualRole::button, ControlVisualContext{}, fields);
    const CompiledThemeRules::FieldProvenance& width =
        fields.recipe_fields[static_cast<std::size_t>(RecipeField::outline_width)];
    require(width.authored && width.layer == ThemeLayer::project_theme && width.source != nullptr &&
                (*width.source).pack == "top" && width.selector != nullptr && (*width.selector).stable_id == "pad",
            "the outline width is the program's rule");
    const CompiledThemeRules::FieldProvenance& text =
        fields.recipe_fields[static_cast<std::size_t>(RecipeField::text)];
    require(text.authored && text.layer == ThemeLayer::library_pack && text.source != nullptr &&
                (*text.source).pack == "base" && (*text.source).line == 2U &&
                text.origin.source == ColourSource::literal,
            "the text colour is the imported pack's literal");
    const CompiledThemeRules::FieldProvenance& glyph =
        fields.recipe_fields[static_cast<std::size_t>(RecipeField::glyph)];
    require(!glyph.authored || glyph.layer == ThemeLayer::library_pack,
            "a field no package sets is the built-in recipe's or the base sheet's");
    require(fields.parameter_count == 0U, "a recipe has no primitive parameters");
    ControlVisualContext forced{};
    forced.high_contrast = true;
    theme.provenance(*pad, ControlVisualRole::button, forced, fields);
    const CompiledThemeRules::FieldProvenance& pass =
        fields.recipe_fields[static_cast<std::size_t>(RecipeField::text)];
    require(!pass.authored && pass.layer == ThemeLayer::forced_high_contrast,
            "in forced high contrast the pass draws the text colour");
    require(theme_layer_name(ThemeLayer::inline_code) == "inline_code" && theme_layer_name(ThemeLayer::count).empty(),
            "the layers are named");
}

int main() {
    test_provenance();
    test_base_theme();
    test_application_theme();
    test_effect_names();
    test_one_package();
    test_published_values();
    test_resources();
    test_closure();
    test_image_ownership();
    test_atomic_swap();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "theme package compile tests passed\n";
    return 0;
}
