// Inline custom properties (stage T3, phase P12b, the owner's decision of
// 2026-10-31): an inline style defining custom properties is compiled
// against the theme in effect for its control, the default theme where
// none is set. Gate: the style's definitions win over the theme's, palette
// and scoped, for the theme's rules reading them; the style reads its own
// definitions under the default theme; a theme change compiles the style
// again, and is refused, with nothing changed, where it does not compile;
// so are a theme override and its removal; attachment compiles a style
// made against another theme again. The style's own var() of a name only
// the theme defines reads the theme's definition, palette or scoped,
// before its fallback; the default theme, which keeps the empty sheet's
// source, defines none.

#include "../../src/core/theme/theme/theme_access.hpp"
#include "../../src/core/theme/theme_compiler/theme_source_record.hpp"
#include "../../src/core/theme_language/compiler/theme_compiler.hpp"
#include "../support/professional_theme.hpp"
#include "../support/theme_definition_heap.hpp"
#include "gui_forms/anatomy/types/anatomy_types.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/engine/primitive_registry/primitive_registry.hpp"
#include "gui_forms/engine/theme_engine/theme_engine.hpp"
#include "gui_forms/engine/theme_rules/theme_rules.hpp"
#include "gui_forms/engine/types/engine_types.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/theme/theme/theme.hpp"
#include "gui_forms/theme/theme_compiler/theme_compiler.hpp"
#include "gui_forms/theme/types/theme_types.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/window/window.hpp"

#include <algorithm>
#include <array>
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

int failures = 0;

void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

constexpr std::array<PartDeclaration, 1> pad_parts{{
    {.name = "face", .role = PartRole::face, .composed_from = {}, .states = {}},
}};
const ControlAnatomy pad_anatomy{
    .control_id = "pad", .parts = pad_parts, .states = {}, .custom_states = {}, .item_systems = {}};

// A control with one keyed face.
class Pad final : public Control {
public:
    explicit Pad(StableId stable_id) : Control(std::move(stable_id)) {}
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override { return pad_anatomy; }
    [[nodiscard]] PartPaint face() const {
        PartPaint part{};
        part.role = PartRole::face;
        part.family = ControlVisualRole::button;
        part.bounds = Rect{20.0, 20.0, 40.0, 24.0};
        part.key = PartKey{"face", {}, {}};
        return part;
    }
};

struct PadWindow final {
    std::shared_ptr<Panel> root;
    std::shared_ptr<Pad> pad;
    std::unique_ptr<Window> window;
};

std::unique_ptr<PadWindow> make_pad_window(const std::shared_ptr<const Theme>& theme) {
    std::unique_ptr<PadWindow> made = std::make_unique<PadWindow>();
    PadWindow& held = *made;
    held.root = make_control<Panel>(StableId("root"));
    held.pad = make_control<Pad>(StableId("pad"));
    (*held.pad).set_requested_bounds(Rect{100.0, 100.0, 100.0, 80.0});
    (*held.root).add_child(held.pad);
    held.window = std::make_unique<Window>(held.root, Size{400.0, 300.0});
    (*held.window).set_theme(theme);
    return made;
}

// A theme of `sheet`, keeping its source as a compiled package does.
std::shared_ptr<const Theme> theme_from(std::string_view sheet) {
    const PrimitiveRegistry& registry = builtin_primitive_registry();
    theme_language::ThemeSource source{};
    source.file = "test.theme.css";
    source.text = sheet;
    const std::array<ControlAnatomy, 1> controls{{pad_anatomy}};
    const theme_language::ThemeCompileInput input{.source = source,
                                                  .registry = registry,
                                                  .vocabulary = theme_language::SelectorVocabulary{controls},
                                                  .published_values = {},
                                                  .resources = {},
                                                  .images = {},
                                                  .requires_engine = current_engine_version};
    const std::unique_ptr<ThemeDefinition> definition = test_support::professional_definition();
    (*definition).id = "theme-inline-custom-property-test";
    theme_language::ThemeCompiler compiler{};
    theme_language::ThemeDiagnosticList diagnostics{};
    const theme_language::CompileOutcome outcome = compiler.compile(input, (*definition).rules, diagnostics);
    require(outcome == theme_language::CompileOutcome::compiled && diagnostics.records().empty(),
            std::string("the theme compiles: ") + std::string(sheet));
    std::shared_ptr<detail::ThemeSourceRecord> record = std::make_shared<detail::ThemeSourceRecord>();
    (*record).text = std::string(sheet);
    (*record).file = "test.theme.css";
    (*record).controls.assign(controls.begin(), controls.end());
    compiler.append_defined_names((*record).defined_names);
    std::sort((*record).defined_names.begin(), (*record).defined_names.end());
    (*record).requires_engine = current_engine_version;
    std::shared_ptr<const detail::ThemeSourceRecord> kept(std::move(record));
    std::shared_ptr<const Theme> theme = detail::ThemeAccess::create_holding(*definition, {}, std::move(kept));
    return theme;
}

std::size_t amplitude_index() {
    const EnginePrimitive* const bulge = builtin_primitive_registry().find("bulge");
    const std::optional<std::size_t> found =
        bulge == nullptr ? std::optional<std::size_t>{} : find_parameter((*bulge).descriptor(), "amplitude");
    return found ? *found : 0U;
}

double face_amplitude(const PadWindow& held) {
    const Pad& pad = *held.pad;
    const ThemeEngine::PartPlan plan = (*held.window).theme().engine().plan(pad, pad.face());
    if (plan.resolution.outcome != CompiledThemeRules::Outcome::primitive) return -1.0;
    return plan.resolution.parameters.values[amplitude_index()].number;
}

// Whether the pad face's amplitude is `expected`, printing it where not.
bool amplitude_is(const PadWindow& held, double expected) {
    const double value = face_amplitude(held);
    const bool same = value > expected - 1e-6 && value < expected + 1e-6;
    if (!same) std::cerr << "  amplitude " << value << ", expected " << expected << '\n';
    return same;
}

bool has(const InlineStyleResult& result, std::string_view code) {
    for (const ThemeCompileDiagnostic& diagnostic : result.diagnostics()) {
        if (diagnostic.code == code) return true;
    }
    return false;
}

void print(const InlineStyleResult& result) {
    for (const ThemeCompileDiagnostic& diagnostic : result.diagnostics()) {
        std::cerr << "  " << diagnostic.code << " " << diagnostic.file << " " << diagnostic.detail << '\n';
    }
}

void set_style(Pad& pad, std::string_view text, std::string_view message) {
    const InlineStyleResult result = pad.set_inline_style(text);
    if (!result.succeeded()) print(result);
    require(result.succeeded(), message);
}

std::uint64_t compiled_against(const Pad& pad) {
    const CompiledThemeRules* const rules = pad.inline_style_rules();
    return rules == nullptr ? 0U : (*rules).theme_serial();
}

std::uint64_t serial_of(const Theme& theme) { return theme.engine().rules().serial(); }


// A palette name the theme reads, defined again inline.
void test_over_a_palette_name() {
    const std::shared_ptr<const Theme> theme =
        theme_from(":root { --lift: 0.4 } pad::part(face) { primitive: bulge; amplitude: var(--lift) }");
    const std::unique_ptr<PadWindow> held = make_pad_window(theme);
    require(amplitude_is(*held, 0.4), "the theme's palette value");
    set_style(*(*held).pad, "--lift: 0.8", "an inline definition");
    require(compiled_against(*(*held).pad) == serial_of(*theme), "compiled against the window's theme");
    require(amplitude_is(*held, 0.8), "the inline definition wins for the theme's rule");
    set_style(*(*held).pad, "", "cleared");
    require(amplitude_is(*held, 0.4), "cleared, the palette value again");
}

// A scoped name the theme defines under a class, defined again inline.
void test_over_a_scoped_name() {
    const std::shared_ptr<const Theme> theme =
        theme_from(".loud { --lift: 0.6 } .quiet { --lift: 0.1 } pad::part(face) { primitive: bulge; amplitude: var(--lift, 0.2) }");
    const std::unique_ptr<PadWindow> held = make_pad_window(theme);
    require(amplitude_is(*held, 0.2), "the fallback without the class");
    static_cast<void>((*(*held).pad).add_class("loud"));
    require(amplitude_is(*held, 0.6), "the class's definition");
    set_style(*(*held).pad, "--lift: 0.9", "an inline definition");
    require(amplitude_is(*held, 0.9), "the inline definition wins over the class's");
    static_cast<void>((*(*held).pad).remove_class("loud"));
    require(amplitude_is(*held, 0.9), "and without the class");
}

// Under the default theme the style reads its own definitions.
void test_under_the_default_theme() {
    const std::unique_ptr<PadWindow> held = make_pad_window(default_theme());
    set_style(*(*held).pad, "--lift: 0.7; ::part(face) { primitive: bulge; amplitude: var(--lift) }",
              "a style reading its own definition");
    require(compiled_against(*(*held).pad) == serial_of(*default_theme()), "compiled against the default theme");
    require(amplitude_is(*held, 0.7), "its own definition");
}

// A theme change compiles the style again; one it does not compile
// against is refused with nothing changed.
void test_theme_change() {
    const std::shared_ptr<const Theme> first =
        theme_from(":root { --lift: 0.4 } pad::part(face) { primitive: bulge; amplitude: var(--lift) }");
    const std::shared_ptr<const Theme> second =
        theme_from(":root { --lift: 0.1 } pad::part(face) { primitive: bulge; amplitude: var(--lift) } "
                   "pad { opacity: 0.9 }");
    const std::shared_ptr<const Theme> refusing =
        theme_from(":root { --lift: 0.5 } @keyframes k { from { opacity: var(--lift) } to { opacity: 1 } }");
    const std::unique_ptr<PadWindow> held = make_pad_window(first);
    set_style(*(*held).pad, "--lift: 0.8", "an inline definition");
    (*(*held).window).set_theme(second);
    require(compiled_against(*(*held).pad) == serial_of(*second), "compiled again for the new theme");
    require(amplitude_is(*held, 0.8), "the inline definition wins under the new theme");

    bool refused = false;
    try {
        (*(*held).window).set_theme(refusing);
    } catch (const std::invalid_argument& error) {
        refused = std::string_view(error.what()).find("E416") != std::string_view::npos;
        if (!refused) std::cerr << "  " << error.what() << '\n';
    }
    require(refused, "a theme the style does not compile against is refused, naming the error");
    require(&(*(*held).window).theme() == second.get(), "the window keeps its theme");
    require(compiled_against(*(*held).pad) == serial_of(*second), "the style is unchanged");

    // The same refusal where the style is set under that theme.
    const std::unique_ptr<PadWindow> other = make_pad_window(refusing);
    const InlineStyleResult result = (*(*other).pad).set_inline_style("--lift: 0.8");
    if (result.succeeded() || !has(result, "E416")) print(result);
    require(!result.succeeded() && has(result, "E416"), "set under it, the style is refused");
}

// An override compiles its subtree's styles again, and so does removing it.
void test_theme_override() {
    const std::shared_ptr<const Theme> first =
        theme_from(":root { --lift: 0.4 } pad::part(face) { primitive: bulge; amplitude: var(--lift) }");
    const std::shared_ptr<const Theme> refusing =
        theme_from(":root { --lift: 0.5 } @keyframes k { from { opacity: var(--lift) } to { opacity: 1 } }");
    const std::unique_ptr<PadWindow> held = make_pad_window(default_theme());
    set_style(*(*held).pad, "--lift: 0.8", "an inline definition");
    (*(*held).root).set_theme_override(first);
    require(compiled_against(*(*held).pad) == serial_of(*first), "compiled for the override");
    bool refused = false;
    try {
        (*(*held).root).set_theme_override(refusing);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*(*held).root).theme_override() == first, "an override it does not compile against");
    (*(*held).root).clear_theme_override();
    require(compiled_against(*(*held).pad) == serial_of(*default_theme()), "compiled again for the window's");
}

// A style made while detached is compiled again on attachment.
void test_attachment() {
    const std::shared_ptr<const Theme> theme =
        theme_from(":root { --lift: 0.4 } pad::part(face) { primitive: bulge; amplitude: var(--lift) }");
    const std::unique_ptr<PadWindow> held = make_pad_window(theme);
    const std::shared_ptr<Pad> late = make_control<Pad>(StableId("late"));
    set_style(*late, "--lift: 0.8", "a detached inline definition");
    require(compiled_against(*late) == serial_of(*default_theme()), "detached, against the default theme");
    (*(*held).root).add_child(late);
    require(compiled_against(*late) == serial_of(*theme), "attached, against the window's theme");
}

std::size_t count_of(const InlineStyleResult& result, std::string_view code) {
    std::size_t count = 0U;
    for (const ThemeCompileDiagnostic& diagnostic : result.diagnostics()) {
        if (diagnostic.code == code) count = count + 1U;
    }
    return count;
}

// The style's own var() of the theme's palette name reads it, before its
// fallback.
void test_reading_a_palette_name() {
    const std::shared_ptr<const Theme> theme = theme_from(":root { --lift: 0.4 } pad { opacity: 0.9 }");
    const std::unique_ptr<PadWindow> held = make_pad_window(theme);
    set_style(*(*held).pad, "::part(face) { primitive: bulge; amplitude: var(--lift, 0.9) }",
              "a style reading the theme's name");
    require(compiled_against(*(*held).pad) == serial_of(*theme), "compiled against the theme it reads");
    require(amplitude_is(*held, 0.4), "the theme's palette value, not the fallback");
}

// The style's own var() of a scoped name reads the definition holding for
// the part, else its fallback.
void test_reading_a_scoped_name() {
    const std::shared_ptr<const Theme> theme =
        theme_from(".loud { --lift: 0.6 } .quiet { --lift: 0.1 } pad { opacity: 0.9 }");
    const std::unique_ptr<PadWindow> held = make_pad_window(theme);
    set_style(*(*held).pad, "::part(face) { primitive: bulge; amplitude: var(--lift, 0.2) }",
              "a style reading a scoped name");
    require(amplitude_is(*held, 0.2), "the fallback where no definition holds");
    static_cast<void>((*(*held).pad).add_class("loud"));
    require(amplitude_is(*held, 0.6), "the loud class's definition");
    static_cast<void>((*(*held).pad).remove_class("loud"));
    static_cast<void>((*(*held).pad).add_class("quiet"));
    require(amplitude_is(*held, 0.1), "the quiet class's definition");
}

// A style both defining a name and reading the theme's: each holds where
// it applies, and the theme's readers of the style's name read the style's.
void test_defining_and_reading() {
    const std::shared_ptr<const Theme> theme = theme_from(
        ":root { --lift: 0.4; --scale: 0.5 } pad::part(face) { primitive: bulge; amplitude: var(--scale) }");
    const std::unique_ptr<PadWindow> held = make_pad_window(theme);
    require(amplitude_is(*held, 0.5), "the theme's rule");
    set_style(*(*held).pad, "--scale: 0.3", "an inline definition the theme reads");
    require(amplitude_is(*held, 0.3), "the theme's rule reads the style's definition");
    set_style(*(*held).pad, "--scale: 0.3; ::part(face) { primitive: bulge; amplitude: var(--lift) }",
              "and a declaration reading the theme's name");
    require(amplitude_is(*held, 0.4), "the style's declaration reads the theme's name");
}

// A theme made from a definition (the windows-professional fixture) keeps
// the empty sheet's source: it defines no names.
void test_reading_under_a_theme_without_a_source() {
    const std::shared_ptr<const Theme> professional = test_support::professional_theme();
    const std::unique_ptr<PadWindow> held = make_pad_window(professional);
    require(detail::ThemeAccess::source(*professional).text.empty(), "a theme made from a definition keeps an empty source");
    const InlineStyleResult refused = (*(*held).pad).set_inline_style("::part(face) { primitive: bulge; amplitude: var(--lift) }");
    if (refused.succeeded() || !has(refused, "E409")) print(refused);
    require(!refused.succeeded() && has(refused, "E409"), "a name nothing defines is E409");
    set_style(*(*held).pad, "::part(face) { primitive: bulge; amplitude: var(--lift, 0.7) }", "with a fallback");
    require(amplitude_is(*held, 0.7), "the fallback");
}

// A theme change compiles the reading style again: a theme defining the
// name differently is read, one defining it nowhere is refused.
void test_reading_across_a_theme_change() {
    const std::shared_ptr<const Theme> first = theme_from(":root { --lift: 0.4 } pad { opacity: 0.9 }");
    const std::shared_ptr<const Theme> second = theme_from(":root { --lift: 0.6 } pad { opacity: 0.8 }");
    const std::shared_ptr<const Theme> lacking = theme_from("pad { opacity: 0.7 }");
    const std::unique_ptr<PadWindow> held = make_pad_window(first);
    set_style(*(*held).pad, "::part(face) { primitive: bulge; amplitude: var(--lift) }", "a style reading the theme's name");
    (*(*held).window).set_theme(second);
    require(amplitude_is(*held, 0.6), "the new theme's definition");
    bool refused = false;
    try {
        (*(*held).window).set_theme(lacking);
    } catch (const std::invalid_argument& error) {
        refused = std::string_view(error.what()).find("E409") != std::string_view::npos;
        if (!refused) std::cerr << "  " << error.what() << '\n';
    }
    require(refused, "a theme not defining the name is refused, naming E409");
    require(&(*(*held).window).theme() == second.get(), "the window keeps its theme");
    require(amplitude_is(*held, 0.6), "and the style its reading");
}

// The style's own warning is reported once, placed in the style, though
// the style is lowered with the theme's text too.
void test_diagnostics_once() {
    const std::shared_ptr<const Theme> theme = theme_from(":root { --lift: 0.4 } pad { opacity: 0.9 }");
    const std::unique_ptr<PadWindow> held = make_pad_window(theme);
    const InlineStyleResult result =
        (*(*held).pad).set_inline_style("::part(face) { primitive: bulge; amplitude: var(--lift); frobnicate: 1 }");
    require(result.succeeded(), "a style with a warning");
    require(count_of(result, "W101") == 1U, "its warning once");
    for (const ThemeCompileDiagnostic& diagnostic : result.diagnostics()) {
        if (diagnostic.code != "W101") continue;
        require(diagnostic.file == "inline:pad" && diagnostic.line == 1U && diagnostic.column == 58U,
                "placed in the style");
        if (diagnostic.column != 58U) std::cerr << "  column " << diagnostic.column << '\n';
    }
    require(amplitude_is(*held, 0.4), "and it applies");
}

} // namespace

int main() {
    test_reading_a_palette_name();
    test_reading_a_scoped_name();
    test_defining_and_reading();
    test_reading_under_a_theme_without_a_source();
    test_reading_across_a_theme_change();
    test_diagnostics_once();
    test_over_a_palette_name();
    test_over_a_scoped_name();
    test_under_the_default_theme();
    test_theme_change();
    test_theme_override();
    test_attachment();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "theme inline custom property tests passed\n";
    return 0;
}
