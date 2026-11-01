// The user theme's levels (T3 user themes, notes:planning/stages/
// T3-theme-language/USER_THEMES.md section 4): a user theme's ordinary
// declarations stand only where the program's theme and inline style
// declare nothing; its `!important` ones over every ordinary declaration
// and under the program's `!important` ones, in its sheet or inline; forced
// high contrast passes over it; its custom properties are its own; its
// opacity is at least 0.5; and the application's policy chooses it once,
// before the first window.

#include "gui_forms/control.hpp"
#include "gui_forms/engine.hpp"
#include "gui_forms/resources.hpp"
#include "gui_forms/theme.hpp"
#include "gui_forms/window.hpp"
#include "support/theme_definition_heap.hpp"

#include <cstdlib>
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

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "gui_forms_theme_user_layer_tests: " << message << '\n';
    std::exit(1);
}

// -- fixtures ------------------------------------------------------------------

CompiledThemeRules::PartFacts face_facts() {
    CompiledThemeRules::PartFacts facts{};
    facts.control_type = "button";
    facts.stable_id = "ok";
    facts.part = "face";
    facts.role = PartRole::face;
    return facts;
}

RuleSelector by_type() {
    RuleSelector selector{};
    selector.control_type = "button";
    return selector;
}

ThemeRule bulge_rule(std::string label, CascadeLevel level, double amplitude) {
    ThemeRule result{};
    result.label = std::move(label);
    result.level = level;
    result.selector = by_type();
    if (level == CascadeLevel::inline_style || level == CascadeLevel::important_inline_style) {
        result.selector = RuleSelector{};
    }
    result.declarations.push_back(RuleDeclaration::primitive("bulge"));
    result.declarations.push_back(RuleDeclaration::number_value("amplitude", amplitude));
    return result;
}

ThemeRuleSheet sheet_of(std::vector<ThemeRule> rules) {
    ThemeRuleSheet sheet{};
    sheet.rules = std::move(rules);
    return sheet;
}

// The amplitude the cascade gives the face, or none where no rule selects
// the bulge.
std::optional<double> amplitude(const CompiledThemeRules& theme, const CompiledThemeRules* user,
                                const CompiledThemeRules* style, bool forced = false) {
    CompiledThemeRules::PartFacts facts = face_facts();
    facts.user_rules = user;
    facts.inline_rules = style;
    facts.forced_high_contrast = forced;
    const CompiledThemeRules::Resolution resolution = theme.resolve(facts);
    if (resolution.outcome != CompiledThemeRules::Outcome::primitive) return std::nullopt;
    const std::optional<std::size_t> index = find_parameter((*resolution.primitive).descriptor(), "amplitude");
    require(index.has_value(), "the bulge has an amplitude");
    const double value = resolution.parameters.values[*index].number;
    return value;
}

bool has_diagnostic(const CompiledThemeRules& rules, RuleDiagnosticKind kind) {
    for (const RuleDiagnostic& diagnostic : rules.diagnostics()) {
        if (diagnostic.kind == kind) return true;
    }
    return false;
}

// -- the levels ----------------------------------------------------------------

void test_order() {
    const PrimitiveRegistry& registry = builtin_primitive_registry();
    const CompiledThemeRules none = CompiledThemeRules::compile(ThemeRuleSheet{}, registry);
    const CompiledThemeRules user =
        CompiledThemeRules::compile_user(sheet_of({bulge_rule("user", CascadeLevel::user_theme, 0.1)}), registry);
    const CompiledThemeRules user_important = CompiledThemeRules::compile_user(
        sheet_of({bulge_rule("user important", CascadeLevel::important_user_theme, 0.2)}), registry);
    const CompiledThemeRules program =
        CompiledThemeRules::compile(sheet_of({bulge_rule("program", CascadeLevel::theme_sheet, 0.3)}), registry);
    const CompiledThemeRules program_important = CompiledThemeRules::compile(
        sheet_of({bulge_rule("program important", CascadeLevel::important_theme_sheet, 0.4)}), registry);
    const CompiledThemeRules style =
        CompiledThemeRules::compile_inline(sheet_of({bulge_rule("inline", CascadeLevel::inline_style, 0.5)}), registry);
    const CompiledThemeRules style_important = CompiledThemeRules::compile_inline(
        sheet_of({bulge_rule("inline important", CascadeLevel::important_inline_style, 0.6)}), registry);
    require(user.user_theme() && user.diagnostics().empty(), "a user theme compiles at the user levels");

    // Case 1: no program sheet, no inline style; the user theme applies.
    require(amplitude(none, &user, nullptr) == 0.1, "a user theme applies where the program declares nothing");
    // Case 3: the six-level order, weakest first.
    require(amplitude(program, &user, nullptr) == 0.3, "the program's ordinary declaration wins over the user's");
    require(amplitude(none, &user, &style) == 0.5, "the program's inline style wins over the user's ordinary");
    require(amplitude(program, &user_important, nullptr) == 0.2,
            "the user's important declaration wins over the program's ordinary");
    require(amplitude(program, &user_important, &style) == 0.2,
            "the user's important declaration wins over the inline style's ordinary");
    require(amplitude(program_important, &user_important, nullptr) == 0.4,
            "the program's important declaration wins over the user's");
    require(amplitude(none, &user_important, &style_important) == 0.6,
            "the inline style's important declaration wins over the user's");
    // Forced high contrast stands over every user declaration.
    require(!amplitude(none, &user_important, nullptr, true).has_value(),
            "forced high contrast passes over the user theme");
}

void test_levels_kept_apart() {
    const PrimitiveRegistry& registry = builtin_primitive_registry();
    const CompiledThemeRules program =
        CompiledThemeRules::compile(sheet_of({bulge_rule("user in program", CascadeLevel::user_theme, 0.1)}), registry);
    require(program.empty() && has_diagnostic(program, RuleDiagnosticKind::level_not_authored),
            "a program theme cannot author the user levels");
    const CompiledThemeRules user = CompiledThemeRules::compile_user(
        sheet_of({bulge_rule("program in user", CascadeLevel::theme_sheet, 0.1)}), registry);
    require(user.empty() && has_diagnostic(user, RuleDiagnosticKind::level_not_authored),
            "a user theme authors only the user levels");
    ThemeRuleSheet published = sheet_of({bulge_rule("user", CascadeLevel::user_theme, 0.1)});
    published.published_values.push_back("progress");
    const CompiledThemeRules reading = CompiledThemeRules::compile_user(published, registry);
    require(reading.published_values().empty() && has_diagnostic(reading, RuleDiagnosticKind::published_value_invalid),
            "a user theme publishes no values");
}

// A user theme's custom property definitions hold for its own rules alone.
void test_scoped_names() {
    const PrimitiveRegistry& registry = builtin_primitive_registry();
    ThemeRule defines = bulge_rule("defines", CascadeLevel::user_theme, 0.1);
    defines.scoped_definitions.push_back(ScopedDefinition{.name = "--lift", .definition = 1U});
    ThemeRule reads = bulge_rule("reads", CascadeLevel::user_theme, 0.7);
    reads.scoped_conditions.push_back(ScopedCondition{.name = "--lift", .definition = 1U});
    const CompiledThemeRules user = CompiledThemeRules::compile_user(sheet_of({defines, reads}), registry);
    const CompiledThemeRules none = CompiledThemeRules::compile(ThemeRuleSheet{}, registry);
    require(amplitude(none, &user, nullptr) == 0.7, "a user rule's condition reads the user theme's definition");
    // The same condition without the definition does not hold: the
    // defining rule alone draws.
    ThemeRule other = defines;
    other.scoped_definitions.clear();
    const CompiledThemeRules alone = CompiledThemeRules::compile_user(sheet_of({other, reads}), registry);
    require(amplitude(none, &alone, nullptr) == 0.1, "a condition no definition meets passes the rule over");
}

// -- the opacity floor ---------------------------------------------------------

void test_opacity_floor() {
    const PrimitiveRegistry& registry = builtin_primitive_registry();
    ThemeRule faded{};
    faded.label = "faded";
    faded.level = CascadeLevel::user_theme;
    faded.selector = by_type();
    PropertyTransition opacity{};
    opacity.property = std::string(opacity_property);
    PresentationState resting{};
    resting.target = 0.1;
    PresentationState hot{};
    hot.when.push_back(AnatomyState::hot);
    hot.target = 0.8;
    hot.enter_from = 0.2;
    opacity.states.push_back(resting);
    opacity.states.push_back(hot);
    faded.transitions.push_back(opacity);
    const CompiledThemeRules user = CompiledThemeRules::compile_user(sheet_of({faded}), registry);
    require(has_diagnostic(user, RuleDiagnosticKind::user_opacity_raised), "a raised opacity is reported");
    CompiledThemeRules::PartFacts facts = face_facts();
    facts.user_rules = &user;
    const CompiledThemeRules none = CompiledThemeRules::compile(ThemeRuleSheet{}, registry);
    const CompiledThemeRules::Resolution resolution = none.resolve(facts);
    require(resolution.transition_count == 1U && resolution.user_serial == user.serial(),
            "the user theme's opacity reaches the part");
    const CompiledThemeRules::CompiledTransition& made = *resolution.transitions[0];
    require(made.states.size() == 2U && made.states[0].target == user_theme_minimum_opacity,
            "a target under the floor is raised to it");
    require(made.states[1].target == 0.8 && made.states[1].enter_from == user_theme_minimum_opacity,
            "an entry value under the floor is raised, a target above it kept");
    // The same transition in a program theme keeps its value.
    faded.level = CascadeLevel::theme_sheet;
    const CompiledThemeRules program = CompiledThemeRules::compile(sheet_of({faded}), registry);
    CompiledThemeRules::PartFacts plain = face_facts();
    const CompiledThemeRules::Resolution kept = program.resolve(plain);
    require(kept.transition_count == 1U && (*kept.transitions[0]).states[0].target == 0.1,
            "a program theme's opacity has no floor");
}

// -- the policy ----------------------------------------------------------------

std::shared_ptr<const Theme> theme_of(ThemeRuleSet set, ThemeRuleSheet rules) {
    std::unique_ptr<ThemeDefinition> definition = test_support::professional_definition();
    (*definition).rules = std::move(rules);
    (*definition).rule_set = set;
    std::shared_ptr<const Theme> theme = Theme::create(*definition);
    return theme;
}

template <typename Failure, typename Action>
bool throws(Action& action) {
    try {
        action();
    } catch (const Failure&) {
        return true;
    }
    return false;
}

struct SelectUser final {
    ApplicationResources* resources{};
    std::shared_ptr<const Theme> theme{};
    void operator()() const { (*resources).set_user_theme(theme); }
};

struct SetProgram final {
    ApplicationResources* resources{};
    std::shared_ptr<const Theme> theme{};
    void operator()() const { (*resources).set_theme(theme); }
};

struct ChoosePolicy final {
    ApplicationResources* resources{};
    UserThemePolicy policy{UserThemePolicy::layered};
    void operator()() const { (*resources).set_user_theme_policy(policy); }
};

struct OverrideTheme final {
    Control* control{};
    std::shared_ptr<const Theme> theme{};
    void operator()() const { (*control).set_theme_override(theme); }
};

void test_policy() {
    const std::shared_ptr<const Theme> user =
        theme_of(ThemeRuleSet::user, sheet_of({bulge_rule("user", CascadeLevel::user_theme, 0.1)}));
    const std::shared_ptr<const Theme> replacing =
        theme_of(ThemeRuleSet::program, sheet_of({bulge_rule("replacing", CascadeLevel::theme_sheet, 0.2)}));
    const std::shared_ptr<const Theme> program =
        theme_of(ThemeRuleSet::program, sheet_of({bulge_rule("program", CascadeLevel::theme_sheet, 0.3)}));

    // Layered: the user theme's rules beside every theme of the windows.
    const std::shared_ptr<ApplicationResources> layered = std::make_shared<ApplicationResources>();
    require((*layered).user_theme_policy() == UserThemePolicy::layered, "the default policy is layered");
    SelectUser wrong_kind{.resources = layered.get(), .theme = replacing};
    require(throws<std::invalid_argument>(wrong_kind), "a layered user theme is compiled as one");
    SetProgram user_as_program{.resources = layered.get(), .theme = user};
    require(throws<std::invalid_argument>(user_as_program), "a user theme is not an application theme");
    (*layered).set_user_theme(user);
    require((*layered).user_theme() == user.get() && (*layered).user_theme_rules() == &(*user).engine().rules(),
            "the layered user theme's rules are the resources'");
    (*layered).set_theme(program);
    require(&(*layered).theme() == program.get(), "a program theme is still set under a layered user theme");
    const std::shared_ptr<Control> root = make_control<Control>(StableId("root"));
    Window window(root, Size{200.0, 100.0}, layered);
    require(window.user_theme_rules() == &(*user).engine().rules(), "a window reaches the user theme's rules");
    SelectUser late{.resources = layered.get(), .theme = nullptr};
    require(throws<std::logic_error>(late), "a user theme is chosen before the first window");
    ChoosePolicy late_policy{.resources = layered.get(), .policy = UserThemePolicy::refused};
    require(throws<std::logic_error>(late_policy), "the policy is chosen before the first window");
    OverrideTheme override_user{.control = root.get(), .theme = user};
    require(throws<std::invalid_argument>(override_user), "a user theme is not a control theme override");

    // Replacing: the user theme is the windows' theme, whatever the program
    // sets later.
    const std::shared_ptr<ApplicationResources> replaced = std::make_shared<ApplicationResources>();
    (*replaced).set_user_theme_policy(UserThemePolicy::replaces_program_sheet);
    SelectUser layered_kind{.resources = replaced.get(), .theme = user};
    require(throws<std::invalid_argument>(layered_kind), "a replacing user theme is compiled as a program theme");
    (*replaced).set_user_theme(replacing);
    require(&(*replaced).theme() == replacing.get() && (*replaced).user_theme_rules() == nullptr,
            "a replacing user theme is the resources' theme");
    (*replaced).set_theme(program);
    require(&(*replaced).theme() == replacing.get() && &(*replaced).program_theme() == program.get(),
            "the program's theme is kept under a replacing user theme");
    (*replaced).set_user_theme(nullptr);
    require(&(*replaced).theme() == program.get(), "without the user theme the program's is in force again");

    // Refused: no user theme at all.
    const std::shared_ptr<ApplicationResources> refused = std::make_shared<ApplicationResources>();
    (*refused).set_user_theme_policy(UserThemePolicy::refused);
    SelectUser any{.resources = refused.get(), .theme = user};
    require(throws<std::logic_error>(any), "a program refusing user themes takes none");
    require((*refused).user_theme() == nullptr, "nothing is selected under the refused policy");
}

} // namespace

int main() {
    test_order();
    test_levels_kept_apart();
    test_scoped_names();
    test_opacity_floor();
    test_policy();
    return 0;
}
