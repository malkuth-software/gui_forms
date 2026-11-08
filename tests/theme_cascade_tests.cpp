// The cascade (T2 plan, phase E4; GIVEN F1-1): one fixture per level, each
// higher level winning whatever its specificity; specificity and its ties
// within a level; invalid rules and declarations dropped with diagnostics
// that name their rule while the rest stands; primitives over the per-pixel
// budget or without a cost refused at load, and animated parts over the
// per-frame budget drawn by the built-in recipe; rules needing a newer
// engine set aside; forced high contrast drawing every part with the
// built-in recipe; a control's style classes; and the paint path compositing
// a primitive's raster for keyed and unkeyed parts.

#include "gui_forms/control.hpp"
#include "support/theme_definition_heap.hpp"
#include "gui_forms/engine.hpp"
#include "gui_forms/theme.hpp"
#include "gui_forms/window.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "gui_forms_theme_cascade_tests: " << message << '\n';
    std::exit(1);
}

// -- fixtures ------------------------------------------------------------------

// A part's facts, as the engine would gather them from a control.
CompiledThemeRules::PartFacts face_of(std::string_view stable_id, std::span<const std::string> classes,
                                      AnatomyStateSet states = {}) {
    CompiledThemeRules::PartFacts facts{};
    facts.control_type = "button";
    facts.stable_id = stable_id;
    facts.classes = classes;
    facts.part = "face";
    facts.role = PartRole::face;
    facts.states = states;
    return facts;
}

ThemeRule rule(std::string label, CascadeLevel level, RuleSelector selector, std::vector<RuleDeclaration> declarations) {
    ThemeRule result{};
    result.label = std::move(label);
    result.level = level;
    result.selector = std::move(selector);
    result.declarations = std::move(declarations);
    return result;
}

RuleSelector by_type() {
    RuleSelector selector{};
    selector.control_type = "button";
    return selector;
}

RuleSelector by_class(std::string name) {
    RuleSelector selector{};
    selector.classes.push_back(std::move(name));
    return selector;
}

RuleSelector by_id(std::string id) {
    RuleSelector selector{};
    selector.stable_id = std::move(id);
    return selector;
}

// The bulge with its amplitude set, as one rule's declarations.
std::vector<RuleDeclaration> bulge(double amplitude) {
    std::vector<RuleDeclaration> declarations{};
    declarations.push_back(RuleDeclaration::primitive("bulge"));
    declarations.push_back(RuleDeclaration::number_value("amplitude", amplitude));
    return declarations;
}

double amplitude_of(const CompiledThemeRules::Resolution& resolution) {
    const std::optional<std::size_t> index = find_parameter((*resolution.primitive).descriptor(), "amplitude");
    require(index.has_value(), "the bulge has an amplitude");
    const double value = resolution.parameters.values[*index].number;
    return value;
}

bool has_diagnostic(const CompiledThemeRules& rules, RuleDiagnosticKind kind, std::string_view label,
                    std::string_view property = {}) {
    for (const RuleDiagnostic& diagnostic : rules.diagnostics()) {
        if (diagnostic.kind == kind && diagnostic.label == label && diagnostic.property == property) return true;
    }
    return false;
}

// -- the cascade ---------------------------------------------------------------

void levels_in_order() {
    const std::vector<std::string> classes{"loud"};
    ThemeRuleSheet sheet{};
    // Authored highest level first, and the theme sheet's selector the most
    // specific: the level alone decides.
    RuleSelector specific = by_id("save");
    specific.control_type = "button";
    specific.classes.push_back("loud");
    specific.part = "face";
    sheet.rules.push_back(rule("inline", CascadeLevel::inline_style, by_id("save"), bulge(0.9)));
    sheet.rules.push_back(rule("id", CascadeLevel::stable_id_rule, by_id("save"), bulge(0.7)));
    sheet.rules.push_back(rule("class", CascadeLevel::class_rule, by_class("loud"), bulge(0.5)));
    sheet.rules.push_back(rule("sheet", CascadeLevel::theme_sheet, specific, bulge(0.3)));
    const CompiledThemeRules rules = CompiledThemeRules::compile(sheet, builtin_primitive_registry());
    require(rules.diagnostics().empty() && rules.rule_count() == 4U, "four valid rules compile cleanly");

    const CompiledThemeRules::Resolution inline_wins = rules.resolve(face_of("save", classes));
    require(inline_wins.outcome == CompiledThemeRules::Outcome::primitive &&
                inline_wins.level == CascadeLevel::inline_style && amplitude_of(inline_wins) == 0.9,
            "the inline style wins over every other level");
    const CompiledThemeRules::Resolution id_wins = rules.resolve(face_of("save", classes));
    require(id_wins.level == CascadeLevel::inline_style, "resolution is the same every time");

    ThemeRuleSheet without_inline = sheet;
    without_inline.rules.erase(without_inline.rules.begin());
    const CompiledThemeRules lower = CompiledThemeRules::compile(without_inline, builtin_primitive_registry());
    require(amplitude_of(lower.resolve(face_of("save", classes))) == 0.7, "then the stable-ID rule");
    require(amplitude_of(lower.resolve(face_of("other", classes))) == 0.5, "then the class rule");
    const std::vector<std::string> quiet{};
    const CompiledThemeRules::Resolution sheet_only = lower.resolve(face_of("other", quiet));
    require(sheet_only.outcome == CompiledThemeRules::Outcome::no_rule,
            "the sheet's selector needs the class too, so nothing selects");
    ThemeRuleSheet plain{};
    plain.rules.push_back(rule("sheet", CascadeLevel::theme_sheet, by_type(), bulge(0.3)));
    const CompiledThemeRules sheet_rules = CompiledThemeRules::compile(plain, builtin_primitive_registry());
    require(amplitude_of(sheet_rules.resolve(face_of("other", quiet))) == 0.3 &&
                sheet_rules.resolve(face_of("other", quiet)).level == CascadeLevel::theme_sheet,
            "and the theme sheet below them");

    // A higher level selecting built_in takes the part back to the built-in
    // recipe.
    ThemeRuleSheet back = plain;
    std::vector<RuleDeclaration> built_in{};
    built_in.push_back(RuleDeclaration::primitive(built_in_primitive_name));
    back.rules.push_back(rule("calm", CascadeLevel::class_rule, by_class("calm"), built_in));
    const CompiledThemeRules backed = CompiledThemeRules::compile(back, builtin_primitive_registry());
    const std::vector<std::string> calm{"calm"};
    require(backed.resolve(face_of("other", calm)).outcome == CompiledThemeRules::Outcome::built_in_selected,
            "a rule may select the built-in recipe");
}

void specificity_and_ties() {
    require(rule_specificity(by_id("a")).ids == 1U && rule_specificity(by_class("a")).classes == 1U,
            "the ID and the classes count");
    RuleSelector many = by_class("a");
    many.classes.push_back("b");
    many.role = PartRole::face;
    many.part = "face";
    many.states.push_back(AnatomyState::hot);
    const RuleSpecificity counted = rule_specificity(many);
    require(counted.classes == 3U && counted.types == 2U && counted.ids == 0U,
            "each class and state counts in the class column, the part's name and role one each as types");
    require(rule_specificity(by_class("a")) < rule_specificity(by_id("x")) && rule_specificity(by_type()) <
                                                                                rule_specificity(by_class("a")),
            "an ID outranks any number of classes, a class outranks a type");

    const std::vector<std::string> none{};
    ThemeRuleSheet sheet{};
    RuleSelector hot = by_type();
    hot.states.push_back(AnatomyState::hot);
    sheet.rules.push_back(rule("hot", CascadeLevel::theme_sheet, hot, bulge(0.8)));
    sheet.rules.push_back(rule("type", CascadeLevel::theme_sheet, by_type(), bulge(0.2)));
    sheet.rules.push_back(rule("type again", CascadeLevel::theme_sheet, by_type(), bulge(0.4)));
    const CompiledThemeRules rules = CompiledThemeRules::compile(sheet, builtin_primitive_registry());
    AnatomyStateSet hot_states{};
    hot_states.insert(AnatomyState::hot);
    require(amplitude_of(rules.resolve(face_of("x", none, hot_states))) == 0.8,
            "the more specific rule wins although it comes first");
    require(amplitude_of(rules.resolve(face_of("x", none))) == 0.4, "among equals, the later rule wins");

    // A given specificity (EC2) stands in for the selector's: the type rule
    // given two classes and a type beats the later class rule with a state
    // (two in the class column, as CSS counts a pseudo-class), and
    // a class rule may then carry no class of its own.
    ThemeRuleSheet given{};
    ThemeRule lifted = rule("lifted", CascadeLevel::class_rule, by_type(), bulge(0.3));
    RuleSpecificity of_class{};
    of_class.classes = 2U;
    of_class.types = 1U;
    lifted.specificity = of_class;
    given.rules.push_back(lifted);
    RuleSelector loud_hot = by_class("loud");
    loud_hot.states.push_back(AnatomyState::hot);
    given.rules.push_back(rule("loud", CascadeLevel::class_rule, loud_hot, bulge(0.5)));
    const CompiledThemeRules given_rules = CompiledThemeRules::compile(given, builtin_primitive_registry());
    require(given_rules.diagnostics().empty(), "a given specificity satisfies the class level");
    const std::vector<std::string> loud{"loud"};
    require(amplitude_of(given_rules.resolve(face_of("x", loud, hot_states))) == 0.3,
            "the given specificity orders the rule within its level");
    ThemeRule unlifted = rule("unlifted", CascadeLevel::stable_id_rule, by_type(), bulge(0.3));
    unlifted.specificity = of_class;
    ThemeRuleSheet missing{};
    missing.rules.push_back(unlifted);
    const CompiledThemeRules missing_rules = CompiledThemeRules::compile(missing, builtin_primitive_registry());
    require(has_diagnostic(missing_rules, RuleDiagnosticKind::selector_missing_level_field, "unlifted"),
            "a stable-ID rule whose given specificity has no ID is dropped");

    // A parameter cascades on its own: a more specific rule for the same
    // primitive sets only the light colour; the amplitude comes from below.
    ThemeRuleSheet split{};
    split.rules.push_back(rule("type", CascadeLevel::theme_sheet, by_type(), bulge(0.6)));
    std::vector<RuleDeclaration> tint{};
    tint.push_back(RuleDeclaration::primitive("bulge"));
    tint.push_back(RuleDeclaration::colour_value("light_colour", Color::rgba(10, 20, 30)));
    split.rules.push_back(rule("tint", CascadeLevel::class_rule, by_class("tinted"), tint));
    const CompiledThemeRules cascaded = CompiledThemeRules::compile(split, builtin_primitive_registry());
    const std::vector<std::string> tinted{"tinted"};
    const CompiledThemeRules::Resolution both = cascaded.resolve(face_of("x", tinted));
    const std::optional<std::size_t> light = find_parameter((*both.primitive).descriptor(), "light_colour");
    require(both.level == CascadeLevel::class_rule && amplitude_of(both) == 0.6 &&
                both.parameters.values[*light].colour == Color::rgba(10, 20, 30),
            "each parameter takes its winning declaration among the rules for the same primitive");
}

// -- refusals ------------------------------------------------------------------

// A primitive whose cost the test chooses; it draws nothing.
class CostlyPrimitive final : public EnginePrimitive {
public:
    explicit CostlyPrimitive(const PrimitiveDescriptor& described) : described_(described) {}
    [[nodiscard]] const PrimitiveDescriptor& descriptor() const noexcept override { return described_; }

private:
    void derive(const PrimitiveParameterBlock&, const PrimitiveTarget&, std::span<double>) const noexcept override {}
    void fill(const PrimitiveParameterBlock&, const PrimitiveTarget&, std::span<const double>,
              const PartRasterView&) const noexcept override {}
    PrimitiveDescriptor described_;
};

const PartRole face_role[] = {PartRole::face};

void invalid_rules_are_dropped() {
    ThemeRuleSheet sheet{};
    std::vector<RuleDeclaration> mixed = bulge(0.5);
    mixed.push_back(RuleDeclaration::number_value("amplitude", 0.25));
    mixed.push_back(RuleDeclaration::number_value("exponent", 99.0));
    mixed.push_back(RuleDeclaration::number_value("light_colour", 1.0));
    mixed.push_back(RuleDeclaration::number_value("sparkle", 1.0));
    mixed.push_back(RuleDeclaration::number_value("light_peak", std::nan("")));
    mixed.push_back(RuleDeclaration::primitive("dithered_bulge"));
    sheet.rules.push_back(rule("mixed", CascadeLevel::theme_sheet, by_type(), mixed));
    std::vector<RuleDeclaration> unknown{};
    unknown.push_back(RuleDeclaration::primitive("glitter"));
    unknown.push_back(RuleDeclaration::number_value("amplitude", 0.5));
    sheet.rules.push_back(rule("unknown", CascadeLevel::theme_sheet, by_type(), unknown));
    std::vector<RuleDeclaration> named_number{};
    named_number.push_back(RuleDeclaration::number_value("primitive", 1.0));
    sheet.rules.push_back(rule("number primitive", CascadeLevel::theme_sheet, by_type(), named_number));
    sheet.rules.push_back(rule("classless", CascadeLevel::class_rule, by_type(), bulge(0.5)));
    sheet.rules.push_back(rule("idless", CascadeLevel::inline_style, by_class("a"), bulge(0.5)));
    sheet.rules.push_back(rule("bad class", CascadeLevel::class_rule, by_class("Loud"), bulge(0.5)));
    sheet.rules.push_back(rule("built in level", CascadeLevel::built_in, by_type(), bulge(0.5)));
    sheet.rules.push_back(rule("forced", CascadeLevel::forced_high_contrast, by_type(), bulge(0.5)));
    RuleSelector label = by_type();
    label.role = PartRole::label;
    sheet.rules.push_back(rule("label bulge", CascadeLevel::theme_sheet, label, bulge(0.5)));
    const CompiledThemeRules rules = CompiledThemeRules::compile(sheet, builtin_primitive_registry());

    require(has_diagnostic(rules, RuleDiagnosticKind::duplicate_declaration, "mixed", "amplitude") &&
                has_diagnostic(rules, RuleDiagnosticKind::parameter_out_of_range, "mixed", "exponent") &&
                has_diagnostic(rules, RuleDiagnosticKind::parameter_wrong_kind, "mixed", "light_colour") &&
                has_diagnostic(rules, RuleDiagnosticKind::unknown_parameter, "mixed", "sparkle") &&
                has_diagnostic(rules, RuleDiagnosticKind::parameter_not_finite, "mixed", "light_peak") &&
                has_diagnostic(rules, RuleDiagnosticKind::duplicate_declaration, "mixed", "primitive"),
            "each invalid declaration is dropped with a diagnostic naming its rule and property");
    require(has_diagnostic(rules, RuleDiagnosticKind::unknown_primitive, "unknown", "primitive") &&
                has_diagnostic(rules, RuleDiagnosticKind::parameter_without_primitive, "unknown", "amplitude") &&
                has_diagnostic(rules, RuleDiagnosticKind::rule_empty, "unknown"),
            "an unknown primitive is dropped, and its parameters and then its empty rule with it");
    require(has_diagnostic(rules, RuleDiagnosticKind::primitive_value_not_a_name, "number primitive", "primitive"),
            "the primitive property takes a name");
    require(has_diagnostic(rules, RuleDiagnosticKind::selector_missing_level_field, "classless") &&
                has_diagnostic(rules, RuleDiagnosticKind::selector_missing_level_field, "idless") &&
                has_diagnostic(rules, RuleDiagnosticKind::selector_invalid, "bad class") &&
                has_diagnostic(rules, RuleDiagnosticKind::level_not_authored, "built in level") &&
                has_diagnostic(rules, RuleDiagnosticKind::level_not_authored, "forced"),
            "rules whose selector does not fit their level, or whose level is not authored, are dropped");
    require(has_diagnostic(rules, RuleDiagnosticKind::primitive_role_mismatch, "label bulge", "primitive"),
            "a primitive bound to a role it does not paint is refused at load");
    const std::vector<std::string> none{};
    const CompiledThemeRules::Resolution kept = rules.resolve(face_of("x", none));
    require(rules.rule_count() == 1U && kept.outcome == CompiledThemeRules::Outcome::primitive &&
                amplitude_of(kept) == 0.5 && (*kept.primitive).descriptor().name == "bulge",
            "the valid rest of a rule stands: the first primitive and the first amplitude");

    // A part whose role the chosen primitive does not paint is resolved as
    // a mismatch, and keeps the built-in recipe.
    CompiledThemeRules::PartFacts divider = face_of("x", none);
    divider.role = PartRole::divider;
    divider.part = "divider";
    require(rules.resolve(divider).outcome == CompiledThemeRules::Outcome::role_mismatch,
            "a selector without a role still never paints a role the primitive does not");
}

void budgets_refuse_unbounded_costs() {
    const PrimitiveDescriptor heavy{"heavy", EngineVersion{0, 1}, face_role, {}, {}, PrimitiveCost{513U, 0U}, {}};
    const PrimitiveDescriptor unbounded{"unbounded", EngineVersion{0, 1}, face_role, {}, {}, PrimitiveCost{0U, 0U}, {}};
    const PrimitiveDescriptor ceiling{"ceiling", EngineVersion{0, 1}, face_role, {}, {}, PrimitiveCost{512U, 0U}, {}};
    const CostlyPrimitive heavy_primitive{heavy};
    const CostlyPrimitive unbounded_primitive{unbounded};
    const CostlyPrimitive ceiling_primitive{ceiling};
    PrimitiveRegistry registry{};
    require(registry.register_primitive(heavy_primitive) == RegistrationOutcome::registered &&
                registry.register_primitive(unbounded_primitive) == RegistrationOutcome::registered &&
                registry.register_primitive(ceiling_primitive) == RegistrationOutcome::registered,
            "the test primitives register");
    ThemeRuleSheet sheet{};
    const char* const names[] = {"heavy", "unbounded", "ceiling"};
    for (const char* const name : names) {
        std::vector<RuleDeclaration> declarations{};
        declarations.push_back(RuleDeclaration::primitive(name));
        sheet.rules.push_back(rule(name, CascadeLevel::theme_sheet, by_type(), declarations));
    }
    const CompiledThemeRules rules = CompiledThemeRules::compile(sheet, registry);
    require(has_diagnostic(rules, RuleDiagnosticKind::primitive_over_budget, "heavy", "primitive") &&
                has_diagnostic(rules, RuleDiagnosticKind::primitive_cost_unbounded, "unbounded", "primitive"),
            "a primitive over 512 operations per device pixel, or declaring none, is refused at load");
    const std::vector<std::string> none{};
    require(rules.rule_count() == 1U && (*rules.resolve(face_of("x", none)).primitive).descriptor().name == "ceiling",
            "512 operations per device pixel is within the budget");
}

void newer_engine_rules_fall_back() {
    ThemeRuleSheet sheet{};
    // The version after this engine's, whatever this engine's is.
    const EngineVersion next{current_engine_version.major,
                             static_cast<std::uint16_t>(current_engine_version.minor + 1U)};
    sheet.requires_engine = next;
    sheet.rules.push_back(rule("future", CascadeLevel::theme_sheet, by_type(), bulge(0.5)));
    ThemeRule today = rule("today", CascadeLevel::class_rule, by_class("loud"), bulge(0.7));
    today.requires_engine = EngineVersion{0, 1};
    sheet.rules.push_back(today);
    const CompiledThemeRules rules = CompiledThemeRules::compile(sheet, builtin_primitive_registry());
    require(has_diagnostic(rules, RuleDiagnosticKind::needs_newer_engine, "future") && rules.rule_count() == 1U,
            "a rule of a sheet needing a newer engine is set aside");
    const std::vector<std::string> none{};
    const std::vector<std::string> loud{"loud"};
    require(rules.resolve(face_of("x", none)).outcome == CompiledThemeRules::Outcome::no_rule,
            "its parts fall back to the built-in recipe");
    require(amplitude_of(rules.resolve(face_of("x", loud))) == 0.7,
            "a rule declaring that it needs only this engine still applies");
}

// -- classes -------------------------------------------------------------------

void style_classes() {
    std::shared_ptr<Control> control = make_control<Control>(StableId("styled"));
    std::vector<std::string> initial{"loud", "wide", "loud"};
    (*control).set_classes(initial);
    require((*control).classes().size() == 2U && (*control).has_class("loud") && (*control).has_class("wide"),
            "classes are kept once each, in order");
    require(!(*control).add_class("loud") && (*control).add_class("tall") && (*control).classes().back() == "tall",
            "add_class adds a class once");
    require((*control).remove_class("wide") && !(*control).remove_class("wide") && !(*control).has_class("wide"),
            "remove_class removes it");
    bool refused = false;
    try {
        std::vector<std::string> bad{"fine", "Not Fine"};
        (*control).set_classes(bad);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*control).classes().size() == 2U && (*control).has_class("tall"),
            "an invalid class name is refused and changes nothing");
    require(valid_style_class_name("a-b_c1") && !valid_style_class_name("1a") && !valid_style_class_name("") &&
                !valid_style_class_name(std::string(65U, 'a')),
            "class names are lowercase, start with a letter and are at most 64 characters");
}

// -- the paint path ------------------------------------------------------------

// Counts the engine's composites and the built-in recipe's fills.
class CountingPainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override { ++fills; }
    void fill_rounded_rect(Rect, double, Color) override { ++fills; }
    void stroke_rect(Rect, Color, double) override {}
    void stroke_rounded_rect(Rect, double, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}
    void draw_part_raster(const std::shared_ptr<const PartRaster>& raster, Point origin, double) override {
        ++composites;
        last_raster = raster;
        last_origin = origin;
    }

    std::size_t fills{};
    std::size_t composites{};
    std::shared_ptr<const PartRaster> last_raster{};
    Point last_origin{};
};

std::unique_ptr<ThemeDefinition> bulge_theme() {
    std::unique_ptr<ThemeDefinition> held = test_support::professional_definition();
    ThemeDefinition& definition = *held;
    definition.id = "cascade-test";
    RuleSelector faces{};
    faces.role = PartRole::face;
    definition.rules.rules.push_back(rule("faces", CascadeLevel::theme_sheet, faces, bulge(0.5)));
    return held;
}

void paint_path() {
    std::shared_ptr<Control> control = make_control<Control>(StableId("painted"));
    Window window(control, Size{400.0, 300.0});
    std::shared_ptr<const Theme> plain = Theme::create(*test_support::professional_definition());
    require((*plain).engine().rules().rule_count() == (*plain).engine().rules().built_in_rule_count(),
            "the professional theme has no rules of its own, only the engine's built-in ones (E7)");
    std::shared_ptr<const Theme> theme = Theme::create(*bulge_theme());
    require((*theme).engine().rules().rule_count() == (*theme).engine().rules().built_in_rule_count() + 1U &&
                (*theme).engine().rules().diagnostics().empty(),
            "a theme's rules compile when it is made");
    window.set_theme(theme);

    PartPaint face{};
    face.role = PartRole::face;
    face.family = ControlVisualRole::button;
    face.bounds = {10.0, 20.0, 40.0, 24.0};
    face.key = PartKey{"face", {}, {}};
    CountingPainter painter{};
    (*theme).engine().paint(painter, *control, face);
    require(painter.composites == 1U && painter.fills == 0U && painter.last_raster &&
                (*painter.last_raster).extent() == DeviceExtent{40U, 24U} && painter.last_origin == Point{10.0, 20.0},
            "a keyed face the cascade gives the bulge is composited from its raster, at its place");
    const RasterCacheStatistics after_first = window.engine_parts().cache_statistics();
    (*theme).engine().paint(painter, *control, face);
    require(window.engine_parts().cache_statistics().hits == after_first.hits + 1U,
            "the keyed part's raster is reused when nothing changed");

    PartPaint unkeyed = face;
    unkeyed.key.reset();
    (*theme).engine().paint(painter, *control, unkeyed);
    require(painter.composites == 3U, "an unkeyed face is drawn into a raster of its own");

    PartPaint label = face;
    label.role = PartRole::label;
    label.text = "Text";
    const ThemeEngine::PartPlan label_plan = (*theme).engine().plan(*control, label);
    require(label_plan.drawing == ThemeEngine::PartDrawing::built_in_recipe, "a part no rule matches is built in");

    // The bulge's amplitude is animated, so a large face is an animated part
    // over 4 million operations per frame: 320 per pixel over 200 x 100.
    PartPaint large = face;
    large.bounds = {0.0, 0.0, 200.0, 100.0};
    const ThemeEngine::PartPlan large_plan = (*theme).engine().plan(*control, large);
    require(large_plan.drawing == ThemeEngine::PartDrawing::over_frame_budget,
            "an animated part over the per-frame budget keeps the built-in recipe");
    const std::size_t composites = painter.composites;
    (*theme).engine().paint(painter, *control, large);
    require(painter.composites == composites && painter.fills > 0U, "and is painted by it");

    // Forced high contrast: the engine's last pass draws every part built in.
    PresentationSettings settings = window.presentation_settings();
    settings.high_contrast = true;
    window.set_presentation_settings(settings);
    require((*theme).engine().plan(*control, face).drawing == ThemeEngine::PartDrawing::forced_high_contrast,
            "under forced high contrast the primitive is not used");

    // A control in no window has nothing to paint into.
    std::shared_ptr<Control> loose = make_control<Control>(StableId("loose"));
    require((*theme).engine().plan(*loose, face).drawing == ThemeEngine::PartDrawing::not_paintable,
            "a control in no window is not painted by a primitive");
}

} // namespace

// Surface rules (`window`, `popup`; owner decision 2026-11-04): a surface
// rule matches only its own surface's facts, never a control's part, and a
// control rule never a surface; a backdrop stands only on a surface rule and
// only as a valid chain.
void surface_rules() {
    const std::vector<std::string> none{};
    RuleSelector window{};
    window.surface = SurfaceKind::window;
    ThemeRule backdrop_rule = rule("window", CascadeLevel::theme_sheet, window, bulge(0.9));
    BackdropDeclaration chain{};
    chain.steps[0].kind = BackdropStepKind::blur;
    chain.steps[0].amount = 12.0;
    chain.steps[1].kind = BackdropStepKind::tint;
    chain.steps[1].colour = Color::rgba(255, 255, 255, 128);
    chain.length = 2U;
    backdrop_rule.backdrop = chain;
    ThemeRuleSheet sheet{};
    sheet.rules.push_back(rule("any", CascadeLevel::theme_sheet, RuleSelector{}, bulge(0.4)));
    sheet.rules.push_back(backdrop_rule);
    // A backdrop on a control rule, and an invalid chain (two blurs, one out
    // of range), are dropped.
    ThemeRule on_control = rule("control", CascadeLevel::theme_sheet, by_type(), bulge(0.6));
    on_control.backdrop = chain;
    sheet.rules.push_back(on_control);
    BackdropDeclaration invalid = chain;
    invalid.steps[1].kind = BackdropStepKind::blur;
    invalid.steps[1].amount = 40.0;
    require(!backdrop_declaration_valid(invalid), "two blurs, one over 32, are no chain");
    ThemeRule invalid_rule = rule("invalid", CascadeLevel::theme_sheet, window, bulge(0.2));
    invalid_rule.backdrop = invalid;
    sheet.rules.push_back(invalid_rule);
    const CompiledThemeRules rules = CompiledThemeRules::compile(sheet, builtin_primitive_registry());
    require(has_diagnostic(rules, RuleDiagnosticKind::backdrop_invalid, "control", "backdrop"),
            "a backdrop on a control rule is dropped");
    require(has_diagnostic(rules, RuleDiagnosticKind::backdrop_invalid, "invalid", "backdrop"),
            "an invalid chain is dropped");
    require(!has_diagnostic(rules, RuleDiagnosticKind::backdrop_invalid, "window", "backdrop"),
            "a valid chain on a window rule stands");
    // A control's part: the surface rules never apply, though the window
    // rule is later and as specific as the type rule.
    require(amplitude_of(rules.resolve(face_of("x", none))) == 0.6, "a surface rule matches no control");
    CompiledThemeRules::PartFacts surface{};
    surface.surface = SurfaceKind::window;
    surface.classes = none;
    // The bulge paints a face: the facts name that role, so that the rule's
    // primitive is resolved rather than refused for its role.
    surface.role = PartRole::face;
    require(amplitude_of(rules.resolve(surface)) == 0.2, "a window's facts match the window rules only");
    surface.surface = SurfaceKind::popup;
    require(rules.resolve(surface).primitive == nullptr, "a popup's facts match no window rule");
    // The backdrop's winner: the window rule's chain for a window's facts,
    // none for a popup's or a control's.
    surface.surface = SurfaceKind::window;
    const BackdropDeclaration* const won = rules.resolve_backdrop(surface);
    require(won != nullptr && *won == chain, "a window's facts take the window rule's backdrop");
    surface.surface = SurfaceKind::popup;
    require(rules.resolve_backdrop(surface) == nullptr, "a popup's facts take no window backdrop");
    require(rules.resolve_backdrop(face_of("x", none)) == nullptr, "a control's part has no backdrop");
    RuleSelector specific = window;
    specific.stable_id = "settings";
    require(rule_specificity(specific).types == 1U && rule_specificity(specific).ids == 1U,
            "a surface selector counts as a type");
}

// A family and a `:not()` (selector step 2): the family rule matches only
// parts resolved for that family, and an exclusion keeps a rule off every
// part it matches.
void families_and_exclusions() {
    ThemeRuleSheet sheet{};
    sheet.rules.push_back(rule("type", CascadeLevel::theme_sheet, by_type(), bulge(0.1)));
    RuleSelector accent = by_type();
    accent.family = "accent_button";
    sheet.rules.push_back(rule("accent", CascadeLevel::theme_sheet, accent, bulge(0.5)));
    RuleSelector quiet_not_hot = by_type();
    RuleExclusion hot{};
    hot.states.push_back(AnatomyState::hot);
    RuleExclusion loud{};
    loud.classes.push_back("loud");
    quiet_not_hot.excluded.push_back(hot);
    quiet_not_hot.excluded.push_back(loud);
    sheet.rules.push_back(rule("calm", CascadeLevel::theme_sheet, quiet_not_hot, bulge(0.3)));
    const CompiledThemeRules rules = CompiledThemeRules::compile(sheet, builtin_primitive_registry());
    require(rules.diagnostics().empty(), "family and exclusion rules compile");
    const std::vector<std::string> none{};
    const std::vector<std::string> loud_class{"loud"};
    AnatomyStateSet hot_states{};
    hot_states.insert(AnatomyState::hot);
    CompiledThemeRules::PartFacts plain = face_of("x", none);
    require(amplitude_of(rules.resolve(plain)) == 0.3, "no exclusion matches: the calm rule");
    require(amplitude_of(rules.resolve(face_of("x", loud_class))) == 0.1, "the class excludes the calm rule");
    require(amplitude_of(rules.resolve(face_of("x", none, hot_states))) == 0.1, "the state excludes it too");
    CompiledThemeRules::PartFacts accented = face_of("x", loud_class);
    accented.family = "accent_button";
    require(amplitude_of(rules.resolve(accented)) == 0.5, "the family rule for the accent family");
    accented.family = "button";
    require(amplitude_of(rules.resolve(accented)) == 0.1, "and not for another family");
    require(rule_specificity(quiet_not_hot).classes == 1U && rule_specificity(accent).classes == 1U,
            "an exclusion counts its most specific argument, a family as a pseudo-class");
}

// Ancestor compounds (selector step 4): `.panel button` and
// `#form > .row > button`, matched against the part's ancestor chain.
void ancestor_compounds() {
    ThemeRuleSheet sheet{};
    sheet.rules.push_back(rule("type", CascadeLevel::theme_sheet, by_type(), bulge(0.1)));
    RuleSelector in_panel = by_type();
    RuleAncestor panel{};
    panel.classes.push_back("panel");
    in_panel.ancestors.push_back(panel);
    sheet.rules.push_back(rule("in panel", CascadeLevel::theme_sheet, in_panel, bulge(0.4)));
    RuleSelector in_row = by_type();
    RuleAncestor row{};
    row.classes.push_back("row");
    row.combinator = AncestorCombinator::child;
    RuleAncestor form{};
    form.stable_id = "form";
    form.combinator = AncestorCombinator::child;
    in_row.ancestors.push_back(row);
    in_row.ancestors.push_back(form);
    sheet.rules.push_back(rule("in row", CascadeLevel::theme_sheet, in_row, bulge(0.7)));
    const CompiledThemeRules rules = CompiledThemeRules::compile(sheet, builtin_primitive_registry());
    require(rules.diagnostics().empty() && rules.uses_ancestors(), "ancestor rules compile and are reported");
    const std::vector<std::string> none{};
    const std::vector<std::string> panel_class{"panel"};
    const std::vector<std::string> row_class{"row"};
    // Builds the facts for a chain of (stable ID, classes), parent first.
    std::vector<AncestorFacts> chain{};
    AncestorFacts parent_row{};
    parent_row.stable_id = "r";
    parent_row.classes = row_class;
    AncestorFacts grand_form{};
    grand_form.stable_id = "form";
    grand_form.classes = none;
    AncestorFacts outer_panel{};
    outer_panel.stable_id = "p";
    outer_panel.classes = panel_class;
    chain.push_back(parent_row);
    chain.push_back(grand_form);
    chain.push_back(outer_panel);
    CompiledThemeRules::PartFacts facts = face_of("x", none);
    facts.ancestors = chain;
    for (const AncestorFacts& ancestor : chain) {
        if (!ancestor.stable_id.empty()) facts.ancestor_bloom.add("#", ancestor.stable_id);
        for (const std::string& name : ancestor.classes) facts.ancestor_bloom.add(".", name);
    }
    require(amplitude_of(rules.resolve(facts)) == 0.7, "the child chain #form > .row > the part matches");
    // Swap the row and the form: the child chain breaks, the descendant
    // rule still holds.
    chain[0] = grand_form;
    chain[1] = parent_row;
    facts.ancestors = chain;
    require(amplitude_of(rules.resolve(facts)) == 0.4, "a broken child chain falls to the descendant rule");
    CompiledThemeRules::PartFacts bare = face_of("x", none);
    require(amplitude_of(rules.resolve(bare)) == 0.1, "no ancestors: only the type rule");
    require(rule_specificity(in_row).ids == 1U && rule_specificity(in_row).classes == 1U,
            "ancestor compounds count");
}

int main() {
    levels_in_order();
    families_and_exclusions();
    ancestor_compounds();
    specificity_and_ties();
    invalid_rules_are_dropped();
    budgets_refuse_unbounded_costs();
    newer_engine_rules_fall_back();
    style_classes();
    paint_path();
    surface_rules();
    std::cout << "theme cascade: levels, specificity, diagnostics, budgets, engine versions, classes and the paint "
                 "path passed\n";
    return EXIT_SUCCESS;
}
