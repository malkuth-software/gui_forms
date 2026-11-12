// Inline styles (stage T3, phase P9, R2-10, the owner's rule E1): a
// control's own declarations and part blocks, its selector implied, owned
// by the control so that they survive theme replacement and need no
// stable ID. Gate: the cascade of each case of E1 (neither important, the
// theme only, the inline style only, both) for a primitive's parameter and
// a recipe field, ordinarily and under forced high contrast, whose colours
// stand over all; a refused inline style keeps the previous one; the forms
// an inline style refuses; and W401 once per window.

#include "../../src/core/theme/theme/forced_high_contrast.hpp"
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
#include "gui_forms/rules/rule/rule.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/theme/theme/theme.hpp"
#include "gui_forms/theme/theme_compiler/theme_compiler.hpp"
#include "gui_forms/theme/types/theme_types.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/window/presentation/presentation_types.hpp"
#include "gui_forms/window/window.hpp"

#include <array>
#include <cstddef>
#include <iostream>
#include <memory>
#include <optional>
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

// Draws nothing; a paint only plans and records the parts.
class QuietPainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override {}
    void fill_rounded_rect(Rect, double, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void stroke_rounded_rect(Rect, double, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}
};

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
    void on_paint(Painter& painter, Rect) override { paint_part(painter, *this, face()); }
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

// A theme of `sheet`, its rules compiled by the theme compiler.
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
    (*definition).id = "theme-inline-style-test";
    theme_language::ThemeCompiler compiler{};
    theme_language::ThemeDiagnosticList diagnostics{};
    const theme_language::CompileOutcome outcome = compiler.compile(input, (*definition).rules, diagnostics);
    require(outcome == theme_language::CompileOutcome::compiled && diagnostics.records().empty(),
            std::string("the theme compiles: ") + std::string(sheet));
    std::shared_ptr<const Theme> theme = Theme::create(*definition);
    return theme;
}

std::size_t amplitude_index() {
    const EnginePrimitive* const bulge = builtin_primitive_registry().find("bulge");
    const std::optional<std::size_t> found =
        bulge == nullptr ? std::optional<std::size_t>{} : find_parameter((*bulge).descriptor(), "amplitude");
    return found ? *found : 0U;
}

// The pad face's plan in its window.
ThemeEngine::PartPlan face_plan(const PadWindow& held) {
    const Pad& pad = *held.pad;
    return (*held.window).theme().engine().plan(pad, pad.face());
}

double face_amplitude(const PadWindow& held) {
    const ThemeEngine::PartPlan plan = face_plan(held);
    if (plan.resolution.outcome != CompiledThemeRules::Outcome::primitive) return -1.0;
    return plan.resolution.parameters.values[amplitude_index()].number;
}

double focus_width(const PadWindow& held) {
    const ResolvedControlRecipe recipe =
        (*held.window).theme().resolve(*held.pad, ControlVisualRole::button, ControlVisualContext{});
    return recipe.recipe().focus_width;
}

// The pad's window under forced high contrast, or not.
void force_high_contrast(PadWindow& held, bool forced) {
    PresentationSettings settings = (*held.window).presentation_settings();
    settings.high_contrast = forced;
    (*held.window).set_presentation_settings(settings);
}

ControlVisualContext high_contrast_context() {
    ControlVisualContext context{};
    context.high_contrast = true;
    return context;
}

ControlVisualRecipe high_contrast_recipe(const PadWindow& held) {
    const ResolvedControlRecipe recipe =
        (*held.window).theme().resolve(*held.pad, ControlVisualRole::button, high_contrast_context());
    return recipe.recipe();
}

std::size_t parameter_index(std::string_view primitive, std::string_view parameter) {
    const EnginePrimitive* const found = builtin_primitive_registry().find(primitive);
    const std::optional<std::size_t> index =
        found == nullptr ? std::optional<std::size_t>{} : find_parameter((*found).descriptor(), parameter);
    return index ? *index : 0U;
}

std::size_t field_index(MaterialField field) { return static_cast<std::size_t>(field); }

bool has(const InlineStyleResult& result, std::string_view code, std::string_view detail) {
    for (const ThemeCompileDiagnostic& diagnostic : result.diagnostics()) {
        if (diagnostic.code == code && diagnostic.file == "inline:pad" &&
            diagnostic.detail.find(detail) != std::string::npos) {
            return true;
        }
    }
    return false;
}

// -- the cascade (E1) ---------------------------------------------------------------

void test_e1_cascade() {
    struct Case final {
        std::string_view theme{};
        std::string_view inline_style{};
        double amplitude{};
        double width{};
        CascadeLevel level{};
        std::string_view name{};
    };
    const std::array<Case, 4> cases{{
        {"#pad::part(face) { primitive: bulge; amplitude: 0.3 } #pad { outline: 3px solid }",
         "outline: 5px solid; ::part(face) { primitive: bulge; amplitude: 0.9 }", 0.9, 5.0,
         CascadeLevel::inline_style, "neither important"},
        {"#pad::part(face) { primitive: bulge !important; amplitude: 0.3 !important } "
         "#pad { outline: 3px solid !important }",
         "outline: 5px solid; ::part(face) { primitive: bulge; amplitude: 0.9 }", 0.3, 3.0,
         CascadeLevel::important_theme_sheet, "the theme only"},
        {"#pad::part(face) { primitive: bulge; amplitude: 0.3 } #pad { outline: 3px solid }",
         "outline: 5px solid !important; ::part(face) { primitive: bulge !important; amplitude: 0.9 !important }",
         0.9, 5.0, CascadeLevel::important_inline_style, "the inline style only"},
        {"#pad::part(face) { primitive: bulge !important; amplitude: 0.3 !important } "
         "#pad { outline: 3px solid !important }",
         "outline: 5px solid !important; ::part(face) { primitive: bulge !important; amplitude: 0.9 !important }",
         0.9, 5.0, CascadeLevel::important_inline_style, "both important"},
    }};
    for (const Case& entry : cases) {
        const std::unique_ptr<PadWindow> held = make_pad_window(theme_from(entry.theme));
        const InlineStyleResult set = (*(*held).pad).set_inline_style(entry.inline_style);
        require(set.succeeded() && set.diagnostics().empty(), std::string(entry.name) + ": the inline style applies");
        const ThemeEngine::PartPlan plan = face_plan(*held);
        require(plan.resolution.level == entry.level && face_amplitude(*held) == entry.amplitude,
                std::string(entry.name) + ": the primitive's parameter, " +
                    std::string(cascade_level_name(plan.resolution.level)));
        require(focus_width(*held) == entry.width, std::string(entry.name) + ": the recipe field");
    }
}

// Under forced high contrast only the inline style's important
// declarations stand over the pass, and the pass's colours over them.
void test_e1_under_forced_high_contrast() {
    struct Case final {
        std::string_view theme{};
        std::string_view inline_style{};
        bool inline_draws{};
        std::string_view name{};
    };
    const std::array<Case, 4> cases{{
        {"#pad::part(face) { primitive: bulge; amplitude: 0.3 } #pad { outline: 3px solid }",
         "outline: 5px solid; ::part(face) { primitive: bulge; amplitude: 0.9 }", false, "neither important"},
        {"#pad::part(face) { primitive: bulge !important; amplitude: 0.3 !important } "
         "#pad { outline: 3px solid !important }",
         "outline: 5px solid; ::part(face) { primitive: bulge; amplitude: 0.9 }", false, "the theme only"},
        {"#pad::part(face) { primitive: bulge; amplitude: 0.3 } #pad { outline: 3px solid }",
         "outline: 5px solid !important; outline-color: red !important; "
         "::part(face) { primitive: bulge !important; amplitude: 0.9 !important; light-colour: red !important }",
         true, "the inline style only"},
        {"#pad::part(face) { primitive: bulge !important; amplitude: 0.3 !important } "
         "#pad { outline: 3px solid !important }",
         "outline: 5px solid !important; outline-color: red !important; "
         "::part(face) { primitive: bulge !important; amplitude: 0.9 !important; light-colour: red !important }",
         true, "both important"},
    }};
    const std::size_t light = parameter_index("bulge", "light_colour");
    for (const Case& entry : cases) {
        const std::unique_ptr<PadWindow> held = make_pad_window(theme_from(entry.theme));
        const InlineStyleResult set = (*(*held).pad).set_inline_style(entry.inline_style);
        require(set.succeeded(), std::string(entry.name) + ": the inline style applies");
        force_high_contrast(*held, true);
        const ThemeEngine::PartPlan plan = face_plan(*held);
        const ControlVisualContext forced = high_contrast_context();
        const ControlVisualRecipe& floor = (*(*held).window).theme().resolve(ControlVisualRole::button, forced);
        const ControlVisualRecipe drawn = high_contrast_recipe(*held);
        if (!entry.inline_draws) {
            require(plan.drawing == ThemeEngine::PartDrawing::forced_high_contrast,
                    std::string(entry.name) + ": the pass draws the part");
            require(drawn.focus_width == floor.focus_width, std::string(entry.name) + ": the pass's outline");
            continue;
        }
        require(plan.drawing == ThemeEngine::PartDrawing::primitive &&
                    plan.resolution.level == CascadeLevel::important_inline_style && face_amplitude(*held) == 0.9,
                std::string(entry.name) + ": the inline style's important primitive stands over the pass");
        const Color expected = high_contrast_colour(ColourOrigin{.source = ColourSource::literal},
                                                    ColourPosition::fill, ControlSurfaceState::normal, false);
        require(plan.resolution.parameters.values[light].colour == expected,
                std::string(entry.name) + ": with the pass's colour");
        require(drawn.focus_width == 5.0 && drawn.focus_ring == floor.focus_ring && drawn.text == floor.text,
                std::string(entry.name) + ": its outline width stands, the pass's colours over it");
        force_high_contrast(*held, false);
        const ThemeEngine::PartPlan ordinary = face_plan(*held);
        require(ordinary.resolution.parameters.values[light].colour == Color::rgba(255, 0, 0),
                std::string(entry.name) + ": its own colour without the pass");
    }
}

// The material fields an important inline style keeps under the pass.
void test_material_under_forced_high_contrast() {
    const std::unique_ptr<PadWindow> held = make_pad_window(theme_from("#pad { outline: 3px solid }"));
    Pad& pad = *(*held).pad;
    require(pad.set_inline_style("border-radius: 6px").succeeded(), "an ordinary material field");
    force_high_contrast(*held, true);
    require(face_plan(*held).drawing == ThemeEngine::PartDrawing::forced_high_contrast,
            "an ordinary material field lies under the pass");

    const InlineStyleResult set = pad.set_inline_style(
        "border-radius: 6px !important; border-width: 2px !important; background-color: red !important; "
        "box-shadow: 0 2px 4px black !important; border-color: blue !important");
    require(set.succeeded(), "important material fields");
    const ThemeEngine::PartPlan plan = face_plan(*held);
    require(plan.drawing == ThemeEngine::PartDrawing::primitive &&
                plan.resolution.level == CascadeLevel::important_inline_style &&
                plan.resolution.primitive == builtin_primitive_registry().find("material"),
            "select the material primitive over the pass");
    detail::ForcedMaterialFields forced{};
    detail::forced_material_fields(plan.resolution.materials, ControlSurfaceState::normal, false, forced);
    const ColourOrigin literal{.source = ColourSource::literal};
    const std::array<const MaterialDeclaration*, material_field_count>& resolved = plan.resolution.materials;
    require(forced.any && forced.fields[field_index(MaterialField::corner_top_left)] ==
                              resolved[field_index(MaterialField::corner_top_left)] &&
                forced.fields[field_index(MaterialField::border_top_width)] ==
                    resolved[field_index(MaterialField::border_top_width)],
            "the corners and border widths stand");
    const MaterialDeclaration* const background = forced.fields[field_index(MaterialField::background_colour)];
    const MaterialDeclaration* const edge = forced.fields[field_index(MaterialField::border_top_colour)];
    require(background != nullptr && (*background).colour == high_contrast_colour(literal, ColourPosition::fill,
                                                                                    ControlSurfaceState::normal,
                                                                                    false),
            "the background colour is the pass's plate");
    require(edge != nullptr && (*edge).colour == high_contrast_colour(literal, ColourPosition::edge,
                                                                        ControlSurfaceState::normal, false),
            "a border colour is the pass's edge");
    require(resolved[field_index(MaterialField::shadows)] != nullptr && forced.fields[field_index(MaterialField::shadows)] == nullptr,
            "the shadows are dropped");
    // A paint under the pass assembles the material for that paint.
    QuietPainter painter{};
    static_cast<void>((*(*held).window).paint(painter, Rect{0.0, 0.0, 400.0, 300.0}));
}

// -- ownership --------------------------------------------------------------------

void test_owned_by_the_control() {
    const std::unique_ptr<PadWindow> held = make_pad_window(theme_from("#pad { outline: 3px solid }"));
    Pad& pad = *(*held).pad;
    require(pad.inline_style().empty() && pad.inline_style_rules() == nullptr, "no inline style at first");
    const InlineStyleResult first = pad.set_inline_style("outline: 5px solid");
    require(first.succeeded() && pad.inline_style() == "outline: 5px solid" && pad.inline_style_rules() != nullptr &&
                (*pad.inline_style_rules()).inline_style(),
            "an inline style is the control's, as set");
    const CompiledThemeRules* const kept = pad.inline_style_rules();

    const InlineStyleResult refused = pad.set_inline_style("outline: 5px solid; opacity: 2");
    require(!refused.succeeded() && has(refused, "E403", "2"), "a bad value refuses the inline style, naming the control");
    require(pad.inline_style() == "outline: 5px solid" && pad.inline_style_rules() == kept &&
                focus_width(*held) == 5.0,
            "and the control keeps its previous one");

    (*(*held).window).set_theme(theme_from("#pad, #other { outline: 2px solid; opacity: 0.5 }"));
    require(pad.inline_style_rules() == kept && focus_width(*held) == 5.0,
            "an inline style survives theme replacement");

    const std::shared_ptr<Pad> other = make_control<Pad>(StableId("other"));
    (*other).set_requested_bounds(Rect{220.0, 100.0, 100.0, 80.0});
    (*(*held).root).add_child(other);
    const ResolvedControlRecipe beside =
        (*(*held).window).theme().resolve(*other, ControlVisualRole::button, ControlVisualContext{});
    require(beside.recipe().focus_width == 2.0, "another control is not styled by it");

    require(pad.set_inline_style("").succeeded() && pad.inline_style().empty() &&
                pad.inline_style_rules() == nullptr && focus_width(*held) == 2.0,
            "empty text clears it");
}

// -- forms ----------------------------------------------------------------------------

void test_forms() {
    const std::shared_ptr<Pad> pad = make_control<Pad>(StableId("pad"));
    const std::array<std::string_view, 6> admitted{{
        "opacity: 0.5",
        "opacity: 0.5;",
        " ; opacity: 0.5 ; scale: 1.1 ",
        "opacity: 0.5 !important",
        "::part(face):hover { scale: 1.04 } ::part(face) { primitive: bulge }",
        "opacity: 0.5; ::part(face):hover { scale: 1.04; transition: scale 90ms ease; }",
    }};
    for (const std::string_view text : admitted) {
        const InlineStyleResult result = (*pad).set_inline_style(text);
        require(result.succeeded() && result.diagnostics().empty(), std::string("admitted: ") + std::string(text));
    }
    const CompiledThemeRules* const rules = (*pad).inline_style_rules();
    require(rules != nullptr && (*rules).rule_count() == 2U, "the declarations and the block are two rules");

    struct Refusal final {
        std::string_view text{};
        std::string_view code{};
        std::string_view detail{};
    };
    const std::array<Refusal, 9> refusals{{
        {"@media (forced-colors: active) { opacity: 1 }", "E206", "@media"},
        {"::part(face) { opacity: 1 } opacity: 1", "E203", ""},
        {"#other { opacity: 1 }", "E301", "its own control"},
        {"pad::part(face) { opacity: 1 }", "E206", ""},
        {".loud { opacity: 1 }", "E301", "its own control"},
        {"opacity: 1 }", "E202", "no opening"},
        {"opacity", "E201", "end of sheet"},
        {"opacity: 2", "E403", "2"},
        {"primitive: bulge; amplitude: 0.5 !important", "E405", "!important"},
    }};
    const InlineStyleResult good = (*pad).set_inline_style("opacity: 0.25");
    require(good.succeeded(), "a good inline style");
    for (const Refusal& entry : refusals) {
        const InlineStyleResult result = (*pad).set_inline_style(entry.text);
        if (result.succeeded() || !has(result, entry.code, entry.detail)) {
            for (const ThemeCompileDiagnostic& diagnostic : result.diagnostics()) {
                std::cerr << "  " << diagnostic.code << " " << diagnostic.file << " " << diagnostic.detail << '\n';
            }
        }
        require(!result.succeeded() && has(result, entry.code, entry.detail),
                std::string("refused: ") + std::string(entry.text) + " as " + std::string(entry.code));
        require((*pad).inline_style() == "opacity: 0.25", "keeping the previous style");
    }
    const InlineStyleResult unknown = (*pad).set_inline_style("::part(lid) { opacity: 1 }");
    require(unknown.succeeded() && has(unknown, "W106", "lid") && (*(*pad).inline_style_rules()).rule_count() == 0U,
            "a part the control lacks is dropped with W106");
}

// -- W401 -------------------------------------------------------------------------------

void paint(PadWindow& held) {
    QuietPainter painter{};
    static_cast<void>((*held.window).paint(painter, Rect{0.0, 0.0, 400.0, 300.0}));
}

void test_inline_with_theme() {
    // windows-professional has no rules; the default, gui-forms, has its
    // light sheet's.
    const std::unique_ptr<PadWindow> plain = make_pad_window(test_support::professional_theme());
    require((*(*plain).pad).set_inline_style("opacity: 0.5").succeeded(), "an inline style");
    paint(*plain);
    require(!(*(*plain).window).inline_with_theme_reported(), "without a theme of rules, no warning");

    const std::unique_ptr<PadWindow> themed = make_pad_window(theme_from("#pad { opacity: 0.75 }"));
    paint(*themed);
    require(!(*(*themed).window).inline_with_theme_reported(), "a theme alone, no warning");
    require((*(*themed).pad).set_inline_style("opacity: 0.5").succeeded(), "then an inline style");
    paint(*themed);
    require((*(*themed).window).inline_with_theme_reported(), "a theme and an inline style: W401");
    paint(*themed);
    require((*(*themed).window).inline_with_theme_reported(), "reported once per window, and noted");
}

} // namespace

int main() {
    test_e1_cascade();
    test_e1_under_forced_high_contrast();
    test_material_under_forced_high_contrast();
    test_owned_by_the_control();
    test_forms();
    test_inline_with_theme();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "theme inline style tests passed\n";
    return 0;
}
