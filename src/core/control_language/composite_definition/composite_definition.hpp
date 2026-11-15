#pragma once

// A composite control's compiled definition (cp34 composite controls,
// phase 2; design note COMPOSITE_CONTROL_LANGUAGE.md, section 4.1): what
// one checked `.control` document compiles to, immutable, shared by every
// instance of the type. CompositeControl (composite_control.hpp) runs it.
// Internal: the public seam names the type only (CompositeDefinition is
// declared, not defined, in composite_control.hpp).
//
// What compiling adds to the checked model (control_model.hpp):
// - every expression parsed once into its tree, and every name a
//   declaration refers to (an activated command, a drag's property, a
//   rule's action targets) resolved to its index;
// - the anatomy: every control's region and focus parts, then the declared
//   parts, with the standard states the composite shows and its custom
//   states;
// - the property descriptors the instances register, with their defaults
//   read by kind;
// - the children: one form document made from <children> (the local IDs
//   written under a placeholder prefix, the bound and `self.` attributes
//   taken out and kept as child bindings) and its model, which each
//   instance loads with its own stable IDs.
//
// Lifetime: the definition holds views into its own strings (the anatomy's
// names and state lists), so it is made in place by
// compile_composite_definition, held by a shared_ptr and never moved or
// copied. Every instance holds a reference until it is destroyed.
//
// What phase 2 runs and refuses: value drags (placement and reorder
// drags are refused with E999), the key and pointer rules, commands,
// semantics and focus. <open> and <close> actions and a rule on a child's
// typed interaction other than a property change are refused with E999
// until overlays and typed child events reach composites. The <look> sheet
// is compiled once with the definition into a rule set of its own (phase
// 3), which layers over the theme in effect.

#include "../control_expression/control_expression.hpp"
#include "../control_model/control_model.hpp"

#include "../../form_language/form_model/form_model.hpp"
#include "../../theme_language/diagnostics/theme_diagnostics.hpp"
#include "../../theme_language/source_text/theme_source_text.hpp"

#include "gui_forms/anatomy/types/anatomy_types.hpp"
#include "gui_forms/binding/value/binding_value.hpp"
#include "gui_forms/dirty.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/semantics/types/semantic_types.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/theme/types/control_visual_types.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

class CompiledThemeRules;

namespace control_language {

// One expression attribute: its text and its tree. An empty one (an
// attribute not written) has no terms.
struct CompositeExpression final {
    std::string text{};
    ParsedExpression parsed{};

    [[nodiscard]] bool empty() const noexcept { return parsed.terms.empty(); }
};

// The prefix the children's document writes before each local ID; an
// instance replaces it with its own stable ID.
inline constexpr std::string_view composite_child_prefix = "composite";

struct CompositePropertyDefinition final {
    // As written (kebab case); the descriptor holds the registered name.
    std::string name{};
    ControlPropertyKind kind{ControlPropertyKind::text};
    PropertyDescriptor descriptor{};
    // The value an instance starts with: the default read by kind, or the
    // kind's zero; null for a presentational property without a default.
    BindingValue initial{};
    // Paint only: kept by the instance, not registered, raising nothing.
    bool presentational{};
    // A command-kind property (Q7): bound with CompositeControl::
    // bind_command, not registered as a value.
    bool command{};
    // The event its change raises, in CompositeDefinition::events.
    std::optional<std::size_t> changed_event{};
    // `range`: the two terms (a property name or a number) a number is
    // clamped between; empty for none.
    CompositeExpression range{};
    // An alias of a child's property: the child's local ID and the
    // property's registered name; empty for a property of its own.
    std::string alias_child{};
    std::string alias_property{};
    Dirty affects{Dirty::none};
};

struct CompositeEventDefinition final {
    std::string name{};
    std::optional<ControlPropertyKind> payload{};
};

struct CompositeCommandDefinition final {
    std::string name{};
    CompositeExpression enabled{};
    CompositeExpression run{};
    std::optional<SemanticAction> semantic_action{};
};

struct CompositeStateDefinition final {
    // As written, and registered (the anatomy's custom state name).
    std::string name{};
    std::string registered{};
    // A published standard state, or a custom state at custom_index.
    std::optional<AnatomyState> standard{};
    std::optional<CustomStateIndex> custom_index{};
    CompositeExpression when{};
};

struct CompositeDragDefinition final {
    ControlDragMode mode{ControlDragMode::commit};
    ControlDragAxis axis{ControlDragAxis::inline_axis};
    // Two geometry terms: the logical inline (or block) coordinates the
    // dragged point travels between.
    CompositeExpression range{};
    std::size_t maps_to{};
    double threshold{4.0};
    bool settles{};
    // The key that begins the keyboard path, none for `keyboard="none"`.
    std::uint32_t keyboard_key{};
    Modifier keyboard_modifiers{Modifier::none};
    // Commit mode: the action calls run on every frame.
    CompositeExpression preview{};
};

struct CompositePartDefinition final {
    // As written, and the registered (snake case) name the anatomy holds.
    std::string name{};
    std::string registered{};
    PartRole role{PartRole::region};
    std::string composed_from{};
    std::vector<AnatomyState> states{};
    ControlPartShape shape{ControlPartShape::unspecified};
    std::vector<double> corner_radius{};
    // Empty for region and focus, which cover the client rectangle.
    CompositeExpression box{};
    CompositeExpression present{};
    CompositeExpression disabled_when{};
    ControlPartHit hit{ControlPartHit::none};
    double hit_expand{};
    std::uint8_t press{};
    std::optional<std::size_t> activates{};
    PartMark mark{PartMark::none};
    std::optional<std::size_t> drag{};
    // The property whose text the part shows: the one its box measures
    // with text-extent(), for a label, caption or content role.
    std::optional<std::size_t> text_property{};
};

enum class CompositeTrigger : std::uint8_t {
    click,
    mouse_down,
    mouse_up,
    activated,
    focus_gained,
    focus_lost,
    mouse_wheel,
    // A property of the composite changed (`changed(value)`).
    changed,
    // A child's property changed (`text-changed(@field)`).
    child_property_changed,
    key,
};

struct CompositeRuleDefinition final {
    CompositeTrigger trigger{CompositeTrigger::click};
    // The child the trigger watches, by local ID; empty for the composite.
    std::string child{};
    // changed: the composite's property; child_property_changed: none.
    std::optional<std::size_t> property{};
    std::string child_property{};
    std::uint32_t key{};
    Modifier modifiers{Modifier::none};
    ControlKeyPhase phase{ControlKeyPhase::key_down};
    bool claims_dialog_key{};
    CompositeExpression when{};
    // CompositeDefinition::actions[first_action, + action_count).
    std::size_t first_action{};
    std::size_t action_count{};
};

struct CompositeActionDefinition final {
    ControlActionKind kind{ControlActionKind::run};
    // The property (set, toggle, step, invoke), command (run) or event
    // (raise) the action names.
    std::size_t target{};
    // focus: the child's local ID; empty for the composite itself.
    std::string child{};
    // set's `to`, step's `by`.
    CompositeExpression value{};
};

// A child property bound to the composite's (`bind(self.<property>)`).
struct CompositeChildBinding final {
    std::string child{};
    std::string property{};
    std::size_t source{};
    // Whether the child's changes flow back: its property's default
    // direction, unless the document narrowed it with one_way.
    bool two_way{};
};

// A child property that holds a `self.` expression (Q10), written again
// whenever a composite property changes.
struct CompositeChildExpression final {
    std::string child{};
    std::string property{};
    CompositeExpression expression{};
};

// One <semantics> state: the state and the node of its condition in
// CompositeSemanticsDefinition::states_expression.
struct CompositeSemanticState final {
    SemanticState state{SemanticState::none};
    ExpressionNodeIndex condition{};
};

// One <semantics> action: what the action reaches, a command or (for
// set-value) a property.
struct CompositeSemanticAction final {
    SemanticAction action{SemanticAction::press};
    std::optional<std::size_t> command{};
    std::optional<std::size_t> property{};
};

struct CompositeSemanticsDefinition final {
    SemanticRole role{SemanticRole::generic};
    CompositeExpression name{};
    CompositeExpression value{};
    // Up to three terms: minimum, maximum and step.
    CompositeExpression range{};
    CompositeExpression states_expression{};
    std::vector<CompositeSemanticState> states{};
    std::vector<CompositeSemanticAction> actions{};
    bool children_exposed{};
};

struct CompositeFocusDefinition final {
    ControlFocusTarget target{ControlFocusTarget::none};
    std::string child{};
    bool tab_stop{};
    std::optional<std::size_t> visible_cue{};
    std::optional<std::size_t> mnemonic{};
};

} // namespace control_language

// The definition. Its members are filled once by
// compile_composite_definition and read only afterwards, by any number of
// instances on any UI thread (nothing in it changes after compiling).
class CompositeDefinition final {
public:
    CompositeDefinition() = default;
    CompositeDefinition(const CompositeDefinition&) = delete;
    CompositeDefinition& operator=(const CompositeDefinition&) = delete;
    CompositeDefinition(CompositeDefinition&&) = delete;
    CompositeDefinition& operator=(CompositeDefinition&&) = delete;
    ~CompositeDefinition() = default;

    // The type's name as written and registered (`toggle-pill`,
    // `toggle_pill`), and its contract version.
    std::string name{};
    std::string registered{};
    std::uint32_t version_major{};
    std::uint32_t version_minor{};
    control_language::CompositeBase base{control_language::CompositeBase::user_control};
    ControlVisualRole family{ControlVisualRole::panel};

    std::vector<control_language::CompositePropertyDefinition> properties{};
    std::vector<control_language::CompositeEventDefinition> events{};
    std::vector<control_language::CompositeCommandDefinition> commands{};
    std::vector<control_language::CompositeStateDefinition> states{};
    // Region (0), focus (1), then the declared parts in document order.
    std::vector<control_language::CompositePartDefinition> parts{};
    std::vector<control_language::CompositeDragDefinition> drags{};
    std::vector<control_language::CompositeRuleDefinition> rules{};
    std::vector<control_language::CompositeActionDefinition> actions{};
    std::vector<control_language::CompositeChildBinding> child_bindings{};
    std::vector<control_language::CompositeChildExpression> child_expressions{};
    control_language::CompositeFocusDefinition focus{};
    std::optional<control_language::CompositeSemanticsDefinition> semantics{};
    std::string look{};

    // The children: the made document, named `children_file`, and its
    // model against `schema`; `local_ids` holds each model object's local
    // ID (empty for the wrapping root, object 0). No children: an empty
    // model.
    std::string children_file{};
    std::string children_document{};
    form_language::FormSchema schema{};
    form_language::FormModel children{};
    std::vector<std::string> local_ids{};
    // The slot's local ID; empty without a slot.
    std::string slot{};

    // The anatomy's storage, views into `parts` and the vectors below.
    std::vector<PartDeclaration> part_declarations{};
    std::vector<AnatomyState> anatomy_states{};
    std::vector<CustomStateDeclaration> custom_states{};
    ControlAnatomy anatomy{};
    // The schema's interactions: the declared events, by name.
    std::vector<std::string> interaction_names{};
    // The <look>, compiled once for the definition (compile_look in
    // composite_definition.cpp, CompiledThemeRules::compile_look); null
    // without a look. Every instance shares it: the composite and the
    // controls its definition made consult it beside the theme in effect
    // (Control::set_look_rules), so it reads nothing of one theme and is
    // not compiled again for another. `look_compiles` counts its
    // compilations (one with a look, none without); `look_rule_diagnostics`
    // the rules the compiled set dropped (CompiledThemeRules::diagnostics).
    std::shared_ptr<const CompiledThemeRules> look_rules{};
    std::uint32_t look_compiles{};
    std::size_t look_rule_diagnostics{};
};

namespace control_language {

// Compiles `model`, built and checked from `syntax` (read from `source`)
// against `schema`, into a new definition that owns `schema`. Returns null
// when something the run time cannot run yet is declared, every such
// declaration reported to `diagnostics` (E999, or the form loader's codes
// for the children's document). Borrowed for the call: everything but
// `schema`. UI thread (it reads the schema's types).
[[nodiscard]] std::shared_ptr<const CompositeDefinition> compile_composite_definition(
    const theme_language::ThemeSource& source, const ControlSyntax& syntax, const ControlModel& model,
    form_language::FormSchema schema, theme_language::ThemeDiagnosticList& diagnostics);

// The physical key and modifiers of a gesture (`ctrl+page-up`, `space`);
// none for one the vocabulary does not name.
struct CompositeKeyGesture final {
    std::uint32_t key{};
    Modifier modifiers{Modifier::none};
};
[[nodiscard]] std::optional<CompositeKeyGesture> composite_key_gesture(std::string_view gesture) noexcept;

// A registered (snake case) name from a written (kebab case) one.
[[nodiscard]] std::string composite_registered_name(std::string_view written);

} // namespace control_language

} // namespace gui_forms
