// A control's recipe fields (T3 P6f): a rule's recipe fields (the text
// colour, the focus ring's outline, the text shadow) are checked when the
// rules compile, are the control's and not a part's, cascade one by one
// over every matching rule, and are put in place over the theme's built-in
// recipe for the control's role and state (Theme::resolve, the floor). A
// theme whose rules set none resolves every control to the floor itself,
// and forced high contrast keeps the floor. The built-in recipe draws a
// label part in the control's resolved text colour.

#include "gui_forms/control.hpp"
#include "support/theme_definition_heap.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/engine.hpp"
#include "gui_forms/theme.hpp"
#include "gui_forms/window.hpp"

#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "gui_forms_engine_recipe_field_tests: " << message << '\n';
    std::exit(1);
}

ThemeRule control_rule(std::string_view label, std::string_view stable_id) {
    ThemeRule rule{};
    rule.label = std::string(label);
    rule.level = CascadeLevel::theme_sheet;
    rule.selector.stable_id = std::string(stable_id);
    return rule;
}

RecipeDeclaration colour_field(RecipeField field, Color colour) {
    RecipeDeclaration declaration{};
    declaration.field = field;
    declaration.colour = colour;
    return declaration;
}

RecipeDeclaration number_field(RecipeField field, double number) {
    RecipeDeclaration declaration{};
    declaration.field = field;
    declaration.number = number;
    return declaration;
}

RecipeDeclaration style_field(RecipeOutlineStyle style) {
    RecipeDeclaration declaration{};
    declaration.field = RecipeField::outline_style;
    declaration.outline_style = style;
    return declaration;
}

RecipeDeclaration shadow_field(Point offset, double blur, Color colour) {
    RecipeDeclaration declaration{};
    declaration.field = RecipeField::text_shadow;
    declaration.shadowed = true;
    declaration.shadow_offset = offset;
    declaration.shadow_blur = blur;
    declaration.colour = colour;
    return declaration;
}

bool has_diagnostic(const CompiledThemeRules& rules, RuleDiagnosticKind kind, std::string_view label,
                    std::string_view property) {
    for (const RuleDiagnostic& diagnostic : rules.diagnostics()) {
        if (diagnostic.kind == kind && diagnostic.label == label && diagnostic.property == property) return true;
    }
    return false;
}

const Color red = Color::rgba(200, 30, 40);
const Color green = Color::rgba(20, 160, 60);
const Color blue = Color::rgba(30, 60, 200);

// -- compiling ------------------------------------------------------------------------

void test_field_names() {
    const std::vector<std::string_view> names{
        "text", "outline_style", "outline_colour", "outline_width", "outline_offset", "text_shadow",
        "font_family", "font_size",     "font_weight",   "font_style",     "letter_spacing",
        "muted_text",  "glyph",         "default_outline_width", "default_outline_colour",
        "fill_text", "stroke_width", "needle_shape", "scrim_colour",
        "block_size", "minimum_inline_size", "padding_inline", "gap", "margin_block_start", "margin_block_end",
        "taper", "taper_colour", "flare", "flare_colour", "edge_shadow_width", "edge_shadow_colour",
        "stripe_pitch", "stripe_ratio", "stripe_angle", "stripe_lean", "stripe_direction", "stripe_speed",
        "stripe_colour", "stripe_ground_colour", "stripe_gloss", "stripe_reduced_motion",
        "fill_colour", "stroke_colour", "stroke_dash", "stroke_gap", "halo_width", "halo_colour", "corner_radius",
        "grid_spacing", "grid_band", "texture_layers", "flow_style", "flow_speed", "flow_spacing",
    };
    require(names.size() == recipe_field_count, "every recipe field is named");
    for (std::size_t index = 0U; index < names.size(); ++index) {
        require(recipe_field_name(static_cast<RecipeField>(index)) == names[index], "the fields' names");
    }
    require(recipe_field_name(RecipeField::count).empty(), "count names no field");
}

void test_compile() {
    ThemeRuleSheet sheet{};
    ThemeRule kept = control_rule("kept", "pad");
    kept.recipe_fields.push_back(colour_field(RecipeField::text, red));
    kept.recipe_fields.push_back(number_field(RecipeField::outline_width, 2.0));
    kept.recipe_fields.push_back(number_field(RecipeField::outline_width, 3.0));
    kept.recipe_fields.push_back(number_field(RecipeField::outline_offset, 65.0));
    kept.recipe_fields.push_back(shadow_field(Point{0.0, 17.0}, 1.0, blue));
    sheet.rules.push_back(kept);
    ThemeRule on_part = control_rule("on-part", "pad");
    on_part.selector.part = "label";
    on_part.recipe_fields.push_back(colour_field(RecipeField::text, green));
    sheet.rules.push_back(on_part);
    ThemeRule on_part_outline = control_rule("on-part-outline", "pad");
    on_part_outline.selector.part = "label";
    on_part_outline.recipe_fields.push_back(number_field(RecipeField::outline_width, 2.0));
    sheet.rules.push_back(on_part_outline);
    ThemeRule on_role = control_rule("on-role", "pad");
    on_role.selector.role = PartRole::label;
    on_role.recipe_fields.push_back(colour_field(RecipeField::text, green));
    sheet.rules.push_back(on_role);
    ThemeRule wide = control_rule("wide", "pad");
    wide.recipe_fields.push_back(number_field(RecipeField::outline_width, 20.0));
    wide.recipe_fields.push_back(shadow_field(Point{}, 9.0, blue));
    RecipeDeclaration reset{};
    reset.field = RecipeField::outline_offset;
    reset.initial = true;
    reset.number = 1000.0;
    wide.recipe_fields.push_back(reset);
    sheet.rules.push_back(wide);
    const CompiledThemeRules rules = CompiledThemeRules::compile(sheet, builtin_primitive_registry());
    require(has_diagnostic(rules, RuleDiagnosticKind::duplicate_declaration, "kept", "outline_width"),
            "a second declaration of one field is dropped");
    require(has_diagnostic(rules, RuleDiagnosticKind::recipe_field_invalid, "kept", "outline_offset") &&
                has_diagnostic(rules, RuleDiagnosticKind::recipe_field_invalid, "kept", "text_shadow") &&
                has_diagnostic(rules, RuleDiagnosticKind::recipe_field_invalid, "wide", "outline_width") &&
                has_diagnostic(rules, RuleDiagnosticKind::recipe_field_invalid, "wide", "text_shadow"),
            "a field outside its range is dropped");
    require(has_diagnostic(rules, RuleDiagnosticKind::recipe_field_on_part, "on-role", "text") &&
                !has_diagnostic(rules, RuleDiagnosticKind::recipe_field_on_part, "on-part", "text") &&
                has_diagnostic(rules, RuleDiagnosticKind::recipe_field_on_part, "on-part-outline", "outline_width"),
            "a rule naming a role sets no recipe field, one naming a part only its text colours");
    require(has_diagnostic(rules, RuleDiagnosticKind::rule_empty, "on-role", {}) &&
                has_diagnostic(rules, RuleDiagnosticKind::rule_empty, "on-part-outline", {}),
            "and a rule left with nothing is dropped");
    require(!has_diagnostic(rules, RuleDiagnosticKind::rule_empty, "kept", {}) &&
                !has_diagnostic(rules, RuleDiagnosticKind::recipe_field_invalid, "wide", "outline_offset"),
            "a rule of recipe fields alone stands, and an initial declaration's value is not read");
    require(rules.rule_count() == 3U, "the three rules that keep a field");
}

// -- the cascade -----------------------------------------------------------------------

void test_cascade() {
    ThemeRuleSheet sheet{};
    ThemeRule general = control_rule("general", {});
    general.selector.control_type = "pad";
    general.recipe_fields.push_back(colour_field(RecipeField::text, red));
    general.recipe_fields.push_back(colour_field(RecipeField::outline_colour, red));
    sheet.rules.push_back(general);
    ThemeRule specific = control_rule("specific", "first");
    specific.recipe_fields.push_back(colour_field(RecipeField::text, green));
    sheet.rules.push_back(specific);
    ThemeRule focused = control_rule("focused", "first");
    focused.selector.states.push_back(AnatomyState::focus_visible);
    focused.recipe_fields.push_back(colour_field(RecipeField::outline_colour, blue));
    sheet.rules.push_back(focused);
    const CompiledThemeRules rules = CompiledThemeRules::compile(sheet, builtin_primitive_registry());
    require(rules.diagnostics().empty(), "the cascade's rules compile cleanly");

    CompiledThemeRules::PartFacts facts{};
    facts.control_type = "pad";
    facts.stable_id = "first";
    CompiledThemeRules::RecipeResolution resolved = rules.resolve_recipe_fields(facts);
    const RecipeDeclaration* const text = resolved.fields[static_cast<std::size_t>(RecipeField::text)];
    const RecipeDeclaration* const ring = resolved.fields[static_cast<std::size_t>(RecipeField::outline_colour)];
    require(resolved.authored && text != nullptr && (*text).colour == green && ring != nullptr &&
                (*ring).colour == red,
            "each field from its own winner: the stable ID's text, the type's ring");
    require(resolved.fields[static_cast<std::size_t>(RecipeField::outline_width)] == nullptr,
            "a field no rule sets has no winner");
    facts.states.insert(AnatomyState::focus_visible);
    resolved = rules.resolve_recipe_fields(facts);
    const RecipeDeclaration* const shown = resolved.fields[static_cast<std::size_t>(RecipeField::outline_colour)];
    require(shown != nullptr && (*shown).colour == blue, "a state rule wins in its state");
    facts.control_type = "other";
    facts.stable_id = "second";
    resolved = rules.resolve_recipe_fields(facts);
    require(!resolved.authored, "a control no rule matches has no field");

    const CompiledThemeRules plain = CompiledThemeRules::compile_with_built_in(ThemeRuleSheet{},
                                                                              builtin_primitive_registry());
    facts.control_type = "pad";
    facts.stable_id = "first";
    require(!plain.resolve_recipe_fields(facts).authored, "the built-in rules set no recipe field");
}

// -- the theme's resolution ------------------------------------------------------------

class Labelled final : public Control {
public:
    explicit Labelled(StableId stable_id) : Control(std::move(stable_id)) {}
    void on_paint(Painter& painter, Rect) override { paint_part(painter, *this, label()); }
    [[nodiscard]] PartPaint label() const {
        PartPaint part{};
        part.role = PartRole::label;
        part.family = ControlVisualRole::button;
        part.bounds = Rect{10.0, 10.0, 60.0, 20.0};
        part.text = lend_paint_text("Label");
        return part;
    }
};

class TextWatch final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color color) override {
        last = color;
        texts = texts + 1U;
    }
    void draw_image(ImageId, Rect, double) override {}
    Color last{};
    std::size_t texts{};
};

struct Fixture final {
    std::shared_ptr<Panel> root;
    std::shared_ptr<Labelled> first;
    std::shared_ptr<Labelled> second;
    std::unique_ptr<Window> window;
};

std::unique_ptr<Fixture> make_fixture(const std::vector<ThemeRule>& rules) {
    std::unique_ptr<Fixture> fixture = std::make_unique<Fixture>();
    Fixture& made = *fixture;
    made.root = make_control<Panel>(StableId("root"));
    made.first = make_control<Labelled>(StableId("first"));
    made.second = make_control<Labelled>(StableId("second"));
    (*made.first).set_requested_bounds(Rect{0.0, 0.0, 80.0, 40.0});
    (*made.second).set_requested_bounds(Rect{0.0, 40.0, 80.0, 40.0});
    (*made.root).add_child(made.first);
    (*made.root).add_child(made.second);
    made.window = std::make_unique<Window>(made.root, Size{100.0, 100.0});
    const std::unique_ptr<ThemeDefinition> held_definition = test_support::professional_definition();
    ThemeDefinition& definition = *held_definition;
    definition.id = "engine-recipe-field-test";
    definition.rules.rules = rules;
    (*made.window).set_theme(Theme::create(definition));
    require((*made.window).theme().engine().rules().diagnostics().empty(), "the fixture theme compiles cleanly");
    (*made.window).perform_layout();
    return fixture;
}

void test_floor() {
    const std::unique_ptr<Fixture> fixture = make_fixture({});
    const Theme& theme = (*(*fixture).window).theme();
    const ControlVisualContext context = (*(*fixture).first).visual_context();
    const ResolvedControlRecipe resolved = theme.resolve(*(*fixture).first, ControlVisualRole::button, context);
    require(!resolved.authored() && &resolved.recipe() == &theme.resolve(ControlVisualRole::button, context),
            "with no recipe field set, a control's recipe is the floor itself");
}

void test_resolve() {
    std::vector<ThemeRule> rules{};
    ThemeRule written = control_rule("first", "first");
    written.recipe_fields.push_back(colour_field(RecipeField::text, red));
    written.recipe_fields.push_back(style_field(RecipeOutlineStyle::solid));
    written.recipe_fields.push_back(colour_field(RecipeField::outline_colour, green));
    written.recipe_fields.push_back(number_field(RecipeField::outline_width, 2.0));
    written.recipe_fields.push_back(number_field(RecipeField::outline_offset, 1.5));
    written.recipe_fields.push_back(shadow_field(Point{1.0, 2.0}, 3.0, blue));
    rules.push_back(written);
    ThemeRule disabled = control_rule("first-disabled", "first");
    disabled.selector.states.push_back(AnatomyState::disabled);
    disabled.recipe_fields.push_back(style_field(RecipeOutlineStyle::none));
    disabled.recipe_fields.push_back(number_field(RecipeField::outline_width, 4.0));
    RecipeDeclaration plain_text{};
    plain_text.field = RecipeField::text;
    plain_text.initial = true;
    disabled.recipe_fields.push_back(plain_text);
    RecipeDeclaration no_shadow{};
    no_shadow.field = RecipeField::text_shadow;
    disabled.recipe_fields.push_back(no_shadow);
    rules.push_back(disabled);
    ThemeRule ring = control_rule("second", "second");
    ring.recipe_fields.push_back(style_field(RecipeOutlineStyle::ring));
    ring.recipe_fields.push_back(number_field(RecipeField::outline_offset, 3.0));
    ring.recipe_fields.push_back(number_field(RecipeField::outline_width, 2.5));
    rules.push_back(ring);
    const std::unique_ptr<Fixture> fixture = make_fixture(rules);
    const Theme& theme = (*(*fixture).window).theme();
    const Labelled& first = *(*fixture).first;
    const Labelled& second = *(*fixture).second;

    const ControlVisualContext context = first.visual_context();
    const ControlVisualRecipe& floor = theme.resolve(ControlVisualRole::button, context);
    const ResolvedControlRecipe resolved = theme.resolve(first, ControlVisualRole::button, context);
    const ControlVisualRecipe& recipe = resolved.recipe();
    require(resolved.authored() && recipe.text == red && recipe.authored_focus_outline &&
                recipe.focus_ring == green && recipe.focus_width == 2.0 && recipe.focus_offset == 1.5,
            "the text colour and a solid outline outside the bounds");
    require(recipe.text_shadow && (*recipe.text_shadow).offset == Point{1.0, 2.0} &&
                (*recipe.text_shadow).blur_radius == 3.0 && (*recipe.text_shadow).color == blue,
            "the text shadow");
    require(recipe.material == floor.material && recipe.muted_text == floor.muted_text &&
                recipe.default_ring == floor.default_ring,
            "the fields no rule sets keep the floor's values");

    ControlVisualContext off = context;
    off.surface = ControlSurfaceState::disabled;
    const ControlVisualRecipe& disabled_floor = theme.resolve(ControlVisualRole::button, off);
    const ResolvedControlRecipe dimmed = theme.resolve(first, ControlVisualRole::button, off);
    const ControlVisualRecipe& dimmed_recipe = dimmed.recipe();
    require(dimmed_recipe.text == disabled_floor.text && dimmed_recipe.focus_width == 0.0 &&
                !dimmed_recipe.text_shadow && dimmed_recipe.focus_ring == green,
            "disabled: initial keeps the floor's text, no outline has no width, none has no shadow");

    const ControlVisualContext second_context = second.visual_context();
    const ResolvedControlRecipe ringed = theme.resolve(second, ControlVisualRole::button, second_context);
    const ControlVisualRecipe& ringed_recipe = ringed.recipe();
    require(ringed.authored() && !ringed_recipe.authored_focus_outline && ringed_recipe.focus_offset == 0.0 &&
                ringed_recipe.focus_width == 2.5,
            "the theme's ring takes a width and no offset");

    ControlVisualContext contrast = context;
    contrast.high_contrast = true;
    const ResolvedControlRecipe forced = theme.resolve(first, ControlVisualRole::button, contrast);
    require(!forced.authored() && &forced.recipe() == &theme.resolve(ControlVisualRole::button, contrast),
            "forced high contrast keeps the floor");
}

// A part's own text colour (`::part(label) { color }`): Theme::resolve
// with the part's name reads it over the control's; without the name, or
// for another part, the control's stands.
void test_part_text_colour() {
    std::vector<ThemeRule> rules{};
    ThemeRule control = control_rule("first", "first");
    control.recipe_fields.push_back(colour_field(RecipeField::text, red));
    rules.push_back(control);
    ThemeRule label = control_rule("first-label", "first");
    label.selector.part = "label";
    label.recipe_fields.push_back(colour_field(RecipeField::text, blue));
    label.recipe_fields.push_back(colour_field(RecipeField::glyph, green));
    label.recipe_fields.push_back(colour_field(RecipeField::fill_text, red));
    rules.push_back(label);
    const std::unique_ptr<Fixture> fixture = make_fixture(rules);
    const Theme& theme = (*(*fixture).window).theme();
    const Labelled& first = *(*fixture).first;
    const ControlVisualContext context = first.visual_context();
    const AnatomyStateSet states{};
    const ResolvedControlRecipe whole = theme.resolve(first, ControlVisualRole::button, context, states);
    const ResolvedControlRecipe part = theme.resolve(first, ControlVisualRole::button, context, states, "label");
    const ResolvedControlRecipe other = theme.resolve(first, ControlVisualRole::button, context, states, "face");
    require(whole.recipe().text == red && other.recipe().text == red, "the control's text colour without the part");
    require(part.recipe().text == blue && part.recipe().glyph == green, "the part's own text and glyph colours");
    require(part.recipe().fill_text == std::optional<Color>{red} && !whole.recipe().fill_text,
            "the part's own colour over a fill, absent on the control");
}

void test_states() {
    std::vector<ThemeRule> rules{};
    ThemeRule focused = control_rule("first-focus", "first");
    focused.selector.states.push_back(AnatomyState::focus_visible);
    focused.recipe_fields.push_back(colour_field(RecipeField::outline_colour, red));
    rules.push_back(focused);
    ThemeRule hot = control_rule("first-hot", "first");
    hot.selector.states.push_back(AnatomyState::hot);
    hot.recipe_fields.push_back(colour_field(RecipeField::text, blue));
    rules.push_back(hot);
    const std::unique_ptr<Fixture> fixture = make_fixture(rules);
    const Theme& theme = (*(*fixture).window).theme();
    const Labelled& first = *(*fixture).first;
    const ResolvedControlRecipe plain = theme.resolve(first, ControlVisualRole::button, first.visual_context());
    require(!plain.authored(), "a state rule does not match outside its state");
    const ResolvedControlRecipe focus =
        theme.resolve(first, ControlVisualRole::button, first.visual_context({.focused = true}));
    const ResolvedControlRecipe hovered =
        theme.resolve(first, ControlVisualRole::button, first.visual_context({.hovered = true}));
    require(focus.recipe().focus_ring == red && hovered.recipe().text == blue,
            "the context's focus is focus_visible and its hot surface hot");
}

void test_label_part() {
    std::vector<ThemeRule> rules{};
    ThemeRule written = control_rule("first", "first");
    written.recipe_fields.push_back(colour_field(RecipeField::text, red));
    rules.push_back(written);
    const std::unique_ptr<Fixture> fixture = make_fixture(rules);
    Window& window = *(*fixture).window;
    const ControlVisualRecipe& floor =
        window.theme().resolve(ControlVisualRole::button, (*(*fixture).second).visual_context());
    TextWatch first_watch{};
    (*(*fixture).first).on_paint(first_watch, Rect{});
    TextWatch second_watch{};
    (*(*fixture).second).on_paint(second_watch, Rect{});
    require(first_watch.texts == 1U && first_watch.last == red,
            "the built-in recipe draws the control's label in its resolved text colour");
    require(second_watch.texts == 1U && second_watch.last == floor.text, "and another control's in the floor's");
}

// Authored colours (owner decision T4-1) stand at the inline level: over a
// theme's ordinary text colour, under an important one, and never in high
// contrast.
void test_authored_colours() {
    std::vector<ThemeRule> rules{};
    ThemeRule ordinary = control_rule("first", "first");
    ordinary.recipe_fields.push_back(colour_field(RecipeField::text, red));
    rules.push_back(ordinary);
    ThemeRule important = control_rule("second", "second");
    important.level = CascadeLevel::important_theme_sheet;
    important.recipe_fields.push_back(colour_field(RecipeField::text, red));
    rules.push_back(important);
    const std::unique_ptr<Fixture> fixture = make_fixture(rules);
    Window& window = *(*fixture).window;
    const Labelled& first = *(*fixture).first;
    const Labelled& second = *(*fixture).second;
    PartPaint label = first.label();
    label.authored_foreground = green;
    TextWatch over{};
    paint_part(over, first, label);
    TextWatch under{};
    paint_part(under, second, label);
    require(over.texts == 1U && over.last == green,
            "an authored foreground stands over a theme's ordinary text colour");
    require(under.texts == 1U && under.last == red,
            "and under an important one");
    PresentationSettings settings = window.presentation_settings();
    settings.high_contrast = true;
    window.set_presentation_settings(settings);
    const ControlVisualContext contrast = first.visual_context();
    const ControlVisualRecipe& forced = window.theme().resolve(ControlVisualRole::button, contrast);
    TextWatch high{};
    paint_part(high, first, label);
    require(contrast.high_contrast && high.texts == 1U && high.last == forced.text,
            "high contrast drops the authored foreground for the role's colour");
}

} // namespace

// N90: a control keeps the recipes it resolved, and a kept recipe never
// outlives the facts it was resolved in: a class written on the control, or
// on the parent whose text colour it inherits, is seen by the next resolve.
void test_kept_recipes() {
    std::vector<ThemeRule> rules{};
    ThemeRule warm{};
    warm.label = "warm";
    warm.level = CascadeLevel::theme_sheet;
    warm.selector.classes.push_back("warm");
    warm.recipe_fields.push_back(colour_field(RecipeField::text, red));
    rules.push_back(warm);
    ThemeRule cool{};
    cool.label = "cool";
    cool.level = CascadeLevel::theme_sheet;
    cool.selector.classes.push_back("cool");
    cool.recipe_fields.push_back(colour_field(RecipeField::text, blue));
    rules.push_back(cool);
    ThemeRule inherits = control_rule("second", "second");
    RecipeDeclaration inherited{};
    inherited.field = RecipeField::text;
    inherited.inherit = true;
    inherits.recipe_fields.push_back(inherited);
    rules.push_back(inherits);
    const std::unique_ptr<Fixture> fixture = make_fixture(rules);
    const Theme& theme = (*(*fixture).window).theme();
    Labelled& first = *(*fixture).first;
    const ControlVisualContext context = first.visual_context();
    const ControlVisualRecipe& floor = theme.resolve(ControlVisualRole::button, context);

    const ResolvedControlRecipe plain = theme.resolve(first, ControlVisualRole::button, context);
    const ResolvedControlRecipe again = theme.resolve(first, ControlVisualRole::button, context);
    require(!plain.authored() && !again.authored() && &again.recipe() == &floor,
            "a kept recipe is the recipe the cascade gave");
    first.classes = {"warm"};
    const ResolvedControlRecipe warmed = theme.resolve(first, ControlVisualRole::button, context);
    require(warmed.authored() && warmed.recipe().text == red, "a class written after a resolve is seen");
    first.classes = std::vector<std::string>{};
    const ResolvedControlRecipe cleared = theme.resolve(first, ControlVisualRole::button, context);
    require(!cleared.authored(), "a class removed after a resolve is seen");

    Labelled& second = *(*fixture).second;
    const ControlVisualContext second_context = second.visual_context();
    const ResolvedControlRecipe before = theme.resolve(second, ControlVisualRole::button, second_context);
    require(before.recipe().text == theme.resolve(ControlVisualRole::button, second_context).text,
            "an inherited text colour is the floor's under a parent that sets none");
    (*(*fixture).root).classes = {"cool"};
    const ResolvedControlRecipe after = theme.resolve(second, ControlVisualRole::button, second_context);
    require(after.authored() && after.recipe().text == blue, "a parent's class written after a resolve is inherited");
}

int main() {
    test_field_names();
    test_compile();
    test_cascade();
    test_floor();
    test_resolve();
    test_part_text_colour();
    test_states();
    test_label_part();
    test_authored_colours();
    test_kept_recipes();
    std::cout << "gui_forms_engine_recipe_field_tests: passed\n";
    return 0;
}
