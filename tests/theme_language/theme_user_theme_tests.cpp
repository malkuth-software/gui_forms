// User themes compiled from their files (T3 user themes, notes:planning/
// stages/T3-theme-language/USER_THEMES.md sections 2 and 3): the `@theme`
// header that makes a main file a theme; the sub-sheets in filename order
// before the main file, or where an `@import` of one stands; the program's
// packages imported by name; the folder's images as resources; the folder's
// limits and kinds of file; the diagnostics named for their own files; and
// the levels, the user levels for a layered theme and the program's for a
// replacing one; and the icon overrides (section 6), checked, held by the
// theme and resolved by the application's icon registry.

#include "../../src/core/theme_language/package/theme_manifest.hpp"
#include "gui_forms/gui_forms.hpp"

#include <array>
#include <cstddef>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
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

// A 2 x 2 PNG every pixel of which is transparent.
const std::string clear_png("\x89\x50\x4e\x47\x0d\x0a\x1a\x0a\x00\x00\x00\x0d\x49\x48\x44\x52\x00\x00"
                            "\x00\x02\x00\x00\x00\x02\x08\x06\x00\x00\x00\x72\xb6\x0d\x24\x00\x00\x00\x0b"
                            "\x49\x44\x41\x54\x78\xda\x63\x60\x40\x07\x00\x00\x12\x00\x01\xe4\x55\x8d\xe7"
                            "\x00\x00\x00\x00\x49\x45\x4e\x44\xae\x42\x60\x82",
                            68U);
// A 1025 x 1 opaque PNG: one pixel wider than an icon may be.
const std::string wide_png("\x89\x50\x4e\x47\x0d\x0a\x1a\x0a\x00\x00\x00\x0d\x49\x48\x44\x52\x00\x00"
                           "\x04\x01\x00\x00\x00\x01\x08\x06\x00\x00\x00\xaf\xcc\xb7\x0b\x00\x00\x00\x1a"
                           "\x49\x44\x41\x54\x78\xda\xed\xc1\x01\x01\x00\x00\x00\x01\x20\xff\x4f\x33\x44"
                           "\x95\x02\x00\x00\x00\x17\x06\x4f\x1c\xf4\xde\xd0\xfc\xa5\xea\x00\x00\x00\x00"
                           "\x49\x45\x4e\x44\xae\x42\x60\x82",
                           83U);
// A 2 x 2 PNG whose compressed data is damaged.
const std::string damaged_png("\x89\x50\x4e\x47\x0d\x0a\x1a\x0a\x00\x00\x00\x0d\x49\x48\x44\x52\x00\x00"
                              "\x00\x02\x00\x00\x00\x02\x08\x06\x00\x00\x00\x72\xb6\x0d\x24\x00\x00\x00"
                              "\x0e\x49\x44\x41\x54\x78\xda\xff\xff\xff\xff\x0c\x30\x06\x00\x8f\x82\x0f\xf1"
                              "\x72\xb7\x07\xa4\x00\x00\x00\x00\x49\x45\x4e\x44\xae\x42\x60\x82",
                              71U);

constexpr std::array<PartDeclaration, 1> pad_parts{{
    {.name = "face", .role = PartRole::face, .composed_from = {}, .states = {}},
}};
const std::array<ControlAnatomy, 1> controls{{
    {.control_id = "button", .parts = pad_parts, .states = {}, .custom_states = {}, .item_systems = {}},
}};

ThemeCompileOptions options() {
    ThemeCompileOptions made{};
    made.controls = controls;
    // These expectations were written onto windows-professional's recipes;
    // the default base is gui-forms (T4 phase 6).
    made.base = ThemeBase::windows_professional;
    return made;
}

constexpr std::string_view header = "@theme { version: \"1.0.0\"; author: \"A. User\"; engine: \">=0.3\"; }\n";

UserThemeFiles theme_of(std::string main_sheet, std::vector<ThemePackageFile> files = {}) {
    UserThemeFiles made{};
    made.name = "harbour";
    made.main_sheet = std::move(main_sheet);
    made.files = std::move(files);
    return made;
}

ThemeCompileResult compile(const UserThemeFiles& files, ThemeRuleSet set = ThemeRuleSet::user,
                           UserThemeHeader* read = nullptr) {
    ImageRegistry images{};
    const ThemeLibrary library{};
    ThemeCompileResult result = compile_user_theme(files, library, images, options(), set, read);
    return result;
}

bool has(const ThemeCompileResult& result, std::string_view code, std::string_view file, std::uint32_t line) {
    for (const ThemeCompileDiagnostic& diagnostic : result.diagnostics()) {
        if (diagnostic.code == code && diagnostic.file == file && diagnostic.line == line) return true;
    }
    for (const ThemeCompileDiagnostic& diagnostic : result.diagnostics()) {
        std::cerr << "  got " << diagnostic.code << " " << diagnostic.name << " in " << diagnostic.file << ":"
                  << diagnostic.line << ":" << diagnostic.column << " (" << diagnostic.detail << ")\n";
    }
    return false;
}

// The classes of the compiled rules' selectors, in sheet order, joined.
std::string class_order(const ThemeCompileResult& result) {
    std::string order{};
    if (!result.succeeded()) return order;
    for (const ThemeRule& rule : (*result.theme()).definition().rules.rules) {
        for (const std::string& name : rule.selector.classes) order += name + " ";
    }
    return order;
}

void test_header() {
    UserThemeHeader read{};
    const ThemeCompileResult compiled = compile(theme_of(std::string(header) + "button { opacity: 0.9 }"),
                                                ThemeRuleSet::user, &read);
    require(compiled.succeeded(), "a main file with a header compiles");
    require(read.version == "1.0.0" && read.author == "A. User", "the header's descriptors are read");
    const ThemeCompileResult bare = compile(theme_of("button { opacity: 0.9 }"));
    require(!bare.succeeded() && has(bare, "E601", "harbour.css", 0U), "a main file without a header is no theme");
    const ThemeCompileResult late = compile(theme_of("button { opacity: 0.9 }\n" + std::string(header)));
    require(!late.succeeded() && has(late, "E207", "harbour.css", 2U), "the header opens the sheet");
    const ThemeCompileResult twice = compile(theme_of(std::string(header) + std::string(header)));
    require(!twice.succeeded() && has(twice, "E207", "harbour.css", 2U), "a sheet has one header");
    const ThemeCompileResult number = compile(theme_of("@theme { version: 1; }"));
    require(!number.succeeded() && has(number, "E401", "harbour.css", 1U), "a descriptor's value is a string");
    const ThemeCompileResult bound = compile(theme_of("@theme { engine: \"0.3\"; }"));
    require(!bound.succeeded() && has(bound, "E401", "harbour.css", 1U), "the engine descriptor is a bound");
    const ThemeCompileResult newer = compile(theme_of("@theme { engine: \">=99.0\"; }"));
    require(!newer.succeeded() && has(newer, "E607", "harbour.css", 0U), "a newer engine bound is E607");
    const ThemeCompileResult unknown = compile(theme_of("@theme { colour: \"red\"; }\nbutton { opacity: 0.9 }"));
    require(unknown.succeeded() && has(unknown, "W101", "harbour.css", 1U), "an unknown descriptor is a warning");
}

void test_levels() {
    const std::string sheet = std::string(header) + ".a { opacity: 0.9 } .b { opacity: 0.8 !important }";
    const ThemeCompileResult layered = compile(theme_of(sheet));
    require(layered.succeeded() && (*layered.theme()).definition().rule_set == ThemeRuleSet::user,
            "a layered user theme is compiled as one");
    bool user_levels = layered.succeeded();
    if (layered.succeeded()) {
        for (const ThemeRule& rule : (*layered.theme()).definition().rules.rules) {
            const bool important = !rule.selector.classes.empty() && rule.selector.classes[0] == "b";
            const CascadeLevel wanted = important ? CascadeLevel::important_user_theme : CascadeLevel::user_theme;
            if (rule.level != wanted) user_levels = false;
        }
        require((*layered.theme()).engine().rules().user_theme(), "its engine holds a user theme's rules");
    }
    require(user_levels, "a layered theme's rules are at the user levels");
    const ThemeCompileResult replacing = compile(theme_of(sheet), ThemeRuleSet::program);
    bool program_levels = replacing.succeeded();
    if (replacing.succeeded()) {
        for (const ThemeRule& rule : (*replacing.theme()).definition().rules.rules) {
            if (rule.level == CascadeLevel::user_theme || rule.level == CascadeLevel::important_user_theme) {
                program_levels = false;
            }
        }
    }
    require(program_levels, "a replacing theme's rules are at the program's levels");
    const ThemeCompileResult published = compile(theme_of(std::string(header) + "button { opacity: var(--app) }"));
    require(!published.succeeded(), "a layered user theme reads no published value");
}

void test_sub_sheets() {
    const std::vector<ThemePackageFile> subs{
        {"b.css", ".b { opacity: 0.8 }"},
        {"a.css", ".a { opacity: 0.7 }"},
    };
    const std::string main_sheet = std::string(header) + ".m { opacity: 0.9 }";
    require(class_order(compile(theme_of(main_sheet, subs))) == "a b m ",
            "sub-sheets load in filename order, then the main file");
    const std::string importing = std::string(header) + "@import \"a.css\";\n.m { opacity: 0.9 }";
    require(class_order(compile(theme_of(importing, subs))) == "b a m ",
            "an imported sub-sheet loads where its import stands, once");
    const std::vector<ThemePackageFile> chained{
        {"b.css", "@import \"a.css\";\n.b { opacity: 0.8 }"},
        {"a.css", ".a { opacity: 0.7 }"},
    };
    require(class_order(compile(theme_of(main_sheet, chained))) == "a b m ", "a sub-sheet may import another");
    const ThemeCompileResult missing = compile(theme_of(std::string(header) + "@import \"c.css\";", subs));
    require(!missing.succeeded() && has(missing, "E603", "harbour.css", 2U), "an import of no sub-sheet is E603");
    const std::vector<ThemePackageFile> cycle{
        {"b.css", "@import \"a.css\";"},
        {"a.css", "@import \"b.css\";"},
    };
    const ThemeCompileResult cyclic = compile(theme_of(main_sheet, cycle));
    require(!cyclic.succeeded() && !cyclic.diagnostics().empty() && cyclic.diagnostics()[0].code == "E605",
            "sub-sheets importing each other are E605");
    const std::vector<ThemePackageFile> broken{{"b.css", ".b { opacity: 0.8 }\n.b { opacity: 2 }"}};
    const ThemeCompileResult placed = compile(theme_of(main_sheet, broken));
    require(!placed.succeeded() && has(placed, "E403", "harbour/b.css", 2U),
            "a sub-sheet's error names its own file and line");
    const ThemeCompileResult warned = compile(theme_of(std::string(header) + "button { frobnicate: 1 }"));
    require(warned.succeeded() && has(warned, "W101", "harbour.css", 2U), "the main file's lines are its own");
}

void test_program_packages() {
    ImageRegistry images{};
    ThemeLibrary library{};
    const std::string sheet = ".p { opacity: 0.6 }";
    ThemePackage base{};
    base.manifest = "{\"format\": \"gui-forms-theme/1\", \"name\": \"base\", \"version\": \"1.0.0\", \"engine\": "
                    "\">=0.3\", \"sheet\": {\"path\": \"theme.theme.css\", \"sha256\": \"" +
                    theme_language::package_sha256_hex(theme_language::package_sha256(sheet)) + "\"}}";
    base.files.push_back(ThemePackageFile{"theme.theme.css", sheet});
    require(library.install(base).empty(), "the program's package is installed");
    const UserThemeFiles files = theme_of(std::string(header) + "@import \"base\";\n.m { opacity: 0.9 }");
    const ThemeCompileResult compiled = compile_user_theme(files, library, images, options(), ThemeRuleSet::user);
    require(class_order(compiled) == "p m ", "a user theme imports the program's package by name");
    const UserThemeFiles other = theme_of(std::string(header) + "@import \"nothing\";");
    const ThemeCompileResult unknown = compile_user_theme(other, library, images, options(), ThemeRuleSet::user);
    require(!unknown.succeeded() && has(unknown, "E603", "harbour.css", 2U), "an unknown package is E603");
}

void test_folder() {
    const std::string sheet = std::string(header) + "button::part(face) { background-image: url(\"resource:wood.png\") }";
    ImageRegistry images{};
    const ThemeLibrary library{};
    const ThemeCompileResult drawn = compile_user_theme(theme_of(sheet, {{"wood.png", tiny_png}}), library, images,
                                                        options(), ThemeRuleSet::user);
    require(drawn.succeeded() && images.image_ids().size() == 1U, "a folder's image is a resource");
    ImageRegistry untouched{};
    const ThemeCompileResult broken = compile_user_theme(theme_of(sheet, {{"wood.png", "not a png"}}), library,
                                                         untouched, options(), ThemeRuleSet::user);
    require(!broken.succeeded() && untouched.image_ids().empty() && has(broken, "E601", "harbour.css", 0U),
            "an image that does not decode is E601");
    const ThemeCompileResult icon =
        compile(theme_of(std::string(header), {{"icons/toolbar.save.png", tiny_png}}));
    require(icon.succeeded(), "an icon override is admitted");
    const ThemeCompileResult text = compile(theme_of(std::string(header), {{"notes.txt", "hello"}}));
    require(!text.succeeded() && has(text, "E601", "harbour/notes.txt", 0U), "another kind of file is refused");
    const ThemeCompileResult nested = compile(theme_of(std::string(header), {{"deep/a.css", ".a {}"}}));
    require(!nested.succeeded() && has(nested, "E601", "harbour/deep/a.css", 0U), "sheets are at the top level");
    const ThemeCompileResult escape = compile(theme_of(std::string(header), {{"../a.css", ".a {}"}}));
    require(!escape.succeeded() && has(escape, "E601", "harbour/../a.css", 0U), "a path leaving the folder is refused");
    UserThemeFiles badly_named = theme_of(std::string(header));
    badly_named.name = "Harbour";
    const ThemeCompileResult named = compile(badly_named);
    require(!named.succeeded() && has(named, "E601", "Harbour.css", 0U), "a name breaking the rule is refused");
    std::vector<ThemePackageFile> many{};
    for (std::size_t index = 0U; index <= maximum_user_theme_files; ++index) {
        many.push_back(ThemePackageFile{"icons/i" + std::to_string(index) + ".png", tiny_png});
    }
    const ThemeCompileResult crowded = compile(theme_of(std::string(header), std::move(many)));
    require(!crowded.succeeded() && has(crowded, "E601", "harbour.css", 0U), "at most 256 files");
}

void test_icons() {
    ApplicationResources resources{};
    const ImageLoadResult save = resources.load_png(std::as_bytes(std::span<const char>(tiny_png)));
    const ImageLoadResult shield = resources.load_png(std::as_bytes(std::span<const char>(tiny_png)));
    require(save && shield, "the program's icons load");
    resources.register_icon("toolbar.save", save.image, IconOverride::overridable);
    resources.register_icon("security.shield", shield.image, IconOverride::fixed);
    bool refused = false;
    try {
        resources.register_icon("toolbar.save", save.image, IconOverride::fixed);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && resources.icon_declarations().size() == 2U, "an identifier is registered once");
    require(icon_identifier_valid("check-mark") && icon_identifier_valid("toolbar.save.2"),
            "identifiers of lowercase segments");
    require(!icon_identifier_valid("Toolbar") && !icon_identifier_valid("a..b") && !icon_identifier_valid(".a") &&
                !icon_identifier_valid("a.") && !icon_identifier_valid("-a") && !icon_identifier_valid(""),
            "the identifier rule");

    ThemeCompileOptions with_icons = options();
    with_icons.icons = resources.icon_declarations();
    ImageRegistry images{};
    const ThemeLibrary library{};
    const UserThemeFiles files = theme_of(std::string(header), {{"icons/toolbar.save.png", tiny_png},
                                                                 {"icons/security.shield.png", tiny_png},
                                                                 {"icons/toolbar.open.png", tiny_png}});
    const ThemeCompileResult compiled = compile_user_theme(files, library, images, with_icons, ThemeRuleSet::user);
    require(compiled.succeeded(), "a theme with icon overrides compiles");
    require(has(compiled, "W502", "harbour/icons/security.shield.png", 0U), "a fixed icon's override is W502");
    require(has(compiled, "W501", "harbour/icons/toolbar.open.png", 0U), "an unknown icon is W501");
    const std::shared_ptr<const Theme> theme = compiled.theme();
    const std::optional<ImageId> replaced = theme == nullptr ? std::nullopt : (*theme).icon_override("toolbar.save");
    require(replaced && images.find(*replaced), "the override is the theme's image");
    require(theme != nullptr && !(*theme).icon_override("security.shield") && !(*theme).icon_override("toolbar.open"),
            "an override refused is ignored");

    require(resources.icon("toolbar.save") == save.image, "the program's icon without a user theme");
    resources.set_user_theme(theme);
    require(resources.icon("toolbar.save") == replaced, "an overridable icon takes the user theme's");
    require(resources.icon("security.shield") == shield.image, "a fixed icon stays the program's");
    require(!resources.icon("toolbar.open"), "an icon not registered is none");
    resources.set_user_theme(nullptr);
    require(resources.icon("toolbar.save") == save.image, "the program's icon once no user theme is selected");

    // The image rules: each refusal a warning, the theme compiling.
    const UserThemeFiles bad = theme_of(std::string(header), {{"icons/toolbar.save.png", clear_png}});
    const ThemeCompileResult clear = compile_user_theme(bad, library, images, with_icons, ThemeRuleSet::user);
    require(clear.succeeded() && has(clear, "W503", "harbour/icons/toolbar.save.png", 0U) &&
                !(*clear.theme()).icon_override("toolbar.save"),
            "a fully transparent icon is W503");
    const UserThemeFiles wide = theme_of(std::string(header), {{"icons/toolbar.save.png", wide_png}});
    const ThemeCompileResult over = compile_user_theme(wide, library, images, with_icons, ThemeRuleSet::user);
    require(over.succeeded() && has(over, "W503", "harbour/icons/toolbar.save.png", 0U),
            "an icon over 1024 pixels wide is W503");
    const UserThemeFiles damaged = theme_of(std::string(header), {{"icons/toolbar.save.png", damaged_png}});
    const ThemeCompileResult broken = compile_user_theme(damaged, library, images, with_icons, ThemeRuleSet::user);
    require(broken.succeeded() && has(broken, "W503", "harbour/icons/toolbar.save.png", 0U),
            "an icon that does not decode is W503");
    const UserThemeFiles large =
        theme_of(std::string(header), {{"icons/toolbar.save.png", tiny_png + std::string(std::size_t{1} << 20U, '\0')}});
    const ThemeCompileResult heavy = compile_user_theme(large, library, images, with_icons, ThemeRuleSet::user);
    require(heavy.succeeded() && has(heavy, "W503", "harbour/icons/toolbar.save.png", 0U),
            "an icon over 1 MiB is W503");
    // Only the first theme's override is still held; the refused icons
    // left nothing in the registry.
    require(images.image_ids().size() == 1U, "a refused icon is not kept");
}

} // namespace

int main() {
    test_header();
    test_levels();
    test_sub_sheets();
    test_program_packages();
    test_folder();
    test_icons();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    return 0;
}
