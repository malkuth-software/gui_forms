#pragma once

// A composite-control document's model and checker (cp34, the
// composite-control language; design note COMPOSITE_CONTROL_LANGUAGE.md,
// sections 3 and 5.3). Phase 1: the language and its checks, with no run
// time. A `.control` document is XML in GUI.Forms' bounded subset, read by
// the same located reader as a form document (form_xml.hpp), and its
// children are form elements checked against the same form schema.
// Internal.
//
// The vocabulary (the design note has an example of each element):
//
//   <?xml version="1.0" encoding="UTF-8"?>
//   <control xmlns="urn:gui-forms:control:1" name="toggle-pill" version="1.0"
//            base="user-control" family="choice">
//     <property name="checked" kind="boolean" default="false" .../>
//     <event name="checked-changed" payload="boolean"/>
//     <command name="toggle" enabled="enabled(self)" run="toggle(checked)"/>
//     <publish state="checked" when="property(checked)"/>
//     <state name="on" when="property(checked)"/>
//     <part name="thumb" role="thumb" box="..." press="click drag" activates="toggle">
//       <drag kind="value" mode="commit" axis="inline" maps-to="checked" .../>
//     </part>
//     <children> form elements with local IDs (`id="@title"`), one <slot> </children>
//     <focus target="self" visible-cue="part(focus)"/>
//     <key gesture="space" phase="key-up"><run command="toggle"/></key>
//     <on trigger="activated(@action)"><raise event="action-invoked"/></on>
//     <semantics role="toggle-switch" name="property(text)" .../>
//     <look> theme-language rules for this type </look>
//   </control>
//
// Spelling follows the forms': names are written in kebab case and the
// schema's registered names are snake case (`read-only` is `read_only`).
// A property's `default` is a value of its kind written as a form document
// writes it (form_model.hpp: `true`, `0.5`, `16 52`, `#3366cc`). The
// language's own flags (`localizable`, `presentational`, `settles`,
// `tab-stop`) are `yes` or `no`. Conditions, geometry and actions are
// expressions (control_expression.hpp).
//
// The checks are section 5.3's, each with its code (E900 to E925, W950;
// theme_diagnostics.hpp). Every defect is reported, in document order of
// the element it concerns, and a refused document leaves the model as it
// was. Values of a child's own properties are checked when the child is
// made (phase 2, through the form loader), not here.

#include "../../form_language/form_model/form_model.hpp"
#include "../../theme_language/diagnostics/theme_diagnostics.hpp"
#include "../../theme_language/source_text/theme_source_text.hpp"
#include "../../xml/xml_reader/xml_located_reader.hpp"

#include "gui_forms/anatomy/types/anatomy_types.hpp"
#include "gui_forms/dirty.hpp"
#include "gui_forms/semantics/types/semantic_types.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/theme/types/control_visual_types.hpp"
#include "gui_forms/xml/types/xml_types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms::control_language {

// The default namespace a composite document's root declares: the
// language and its version. Version 1 embeds version 1 of the form
// vocabulary inside <children>, unprefixed.
inline constexpr std::string_view control_namespace = "urn:gui-forms:control:1";

// Declaration bounds (design note 5.3). A composite's anatomy is every
// control's region and focus parts followed by its own, within the
// anatomy's 32 (gui_forms::maximum_control_parts), so a document declares
// at most 30.
inline constexpr std::size_t maximum_control_parts = gui_forms::maximum_control_parts - 2U;
inline constexpr std::size_t maximum_custom_states = 16U;

// One read document: the elements, where each was written, and the line
// table for diagnostics. Caller-owned and reused; replaced whole by
// read_control_syntax, left empty by one that refuses.
struct ControlSyntax final {
    XmlElement root{};
    detail::XmlLocations locations{};
    theme_language::LineStartTable lines{};
};

enum class ControlSyntaxOutcome : std::uint8_t {
    read,
    // Not a document of the subset: one E900 whose detail is the reader's
    // XmlError name, at the reader's position.
    refused,
};

// Reads `source` into `syntax` under the form documents' limits
// (form_xml_limits). Both are borrowed for the call.
[[nodiscard]] ControlSyntaxOutcome read_control_syntax(const theme_language::ThemeSource& source,
                                                       ControlSyntax& syntax,
                                                       theme_language::ThemeDiagnosticList& diagnostics);

// The C++ base a composite runs on.
enum class CompositeBase : std::uint8_t {
    user_control,
    scrollable_control,
};

// A declared property's kind: the schema kinds, plus `command` (Q7).
enum class ControlPropertyKind : std::uint8_t {
    boolean,
    number,
    signed_integer,
    unsigned_integer,
    text,
    enumeration,
    color,
    font,
    image,
    size,
    point,
    rectangle,
    insets,
    command,
};

// The kebab-case word a document writes for a kind.
[[nodiscard]] std::string_view control_property_kind_name(ControlPropertyKind kind) noexcept;

enum class ControlBindable : std::uint8_t {
    no,
    one_way,
    two_way,
};

// Where an element was written: its position in ControlSyntax::locations
// and where its start tag begins.
struct ControlSource final {
    std::uint32_t element{};
    theme_language::SourcePosition position{};
};

struct ControlProperty final {
    std::string name{};
    // None for an alias, which takes its target's kind.
    std::optional<ControlPropertyKind> kind{};
    // As written; empty for none.
    std::string default_value{};
    std::vector<std::string> choices{};
    // The two property names (or numbers) a number is clamped between.
    std::string range{};
    ControlBindable bindable{ControlBindable::no};
    std::string category{"Behavior"};
    // The event its change raises; empty for none.
    std::string changed{};
    Dirty affects{Dirty::none};
    bool localizable{};
    bool presentational{};
    // `@child.property` or a property name; empty for a property of its own.
    std::string alias{};
    std::string description{};
    ControlSource source{};
};

struct ControlEvent final {
    std::string name{};
    // None for an event without a payload.
    std::optional<ControlPropertyKind> payload{};
    ControlSource source{};
};

struct ControlCommand final {
    std::string name{};
    // Expressions as written; empty for none.
    std::string enabled{};
    std::string run{};
    std::optional<SemanticAction> semantic_action{};
    ControlSource source{};
};

// A derived state: a standard state the composite publishes (`<publish>`)
// or a custom state it declares (`<state>`).
struct ControlState final {
    std::string name{};
    bool custom{};
    // The standard state, for a published one.
    std::optional<AnatomyState> standard{};
    std::string when{};
    ControlSource source{};
};

enum class ControlPartShape : std::uint8_t {
    unspecified,
    rounded_rectangle,
    pill,
    circle,
    cap_start,
    cap_end,
};

enum class ControlPartHit : std::uint8_t {
    none,
    shape,
    box,
    expand,
};

// Bits of ControlPart::press.
enum class ControlPress : std::uint8_t {
    none = 0U,
    click = 1U,
    down = 2U,
    repeat = 4U,
    drag = 8U,
};

enum class ControlDragKind : std::uint8_t {
    value,
    placement,
    reorder,
};

enum class ControlDragMode : std::uint8_t {
    live,
    commit,
};

enum class ControlDragAxis : std::uint8_t {
    inline_axis,
    block_axis,
    free,
    dominant,
};

struct ControlDrag final {
    ControlDragKind kind{ControlDragKind::value};
    ControlDragMode mode{ControlDragMode::commit};
    ControlDragAxis axis{ControlDragAxis::inline_axis};
    std::string range{};
    std::string maps_to{};
    std::string grid{};
    std::string targets{};
    std::string slots{};
    double threshold{4.0};
    bool settles{};
    // The gesture that begins the keyboard path; empty for `none`.
    std::string keyboard{};
    std::string preview{};
    ControlSource source{};
};

struct ControlPart final {
    std::string name{};
    PartRole role{PartRole::region};
    std::string composed_from{};
    std::vector<AnatomyState> states{};
    ControlPartShape shape{ControlPartShape::unspecified};
    std::vector<double> corner_radius{};
    std::string box{};
    std::string present{};
    ControlPartHit hit{ControlPartHit::none};
    double hit_expand{};
    std::uint8_t press{};
    std::string activates{};
    std::string disabled_when{};
    PartMark mark{PartMark::none};
    // Its drag's position in ControlModel::drags, for a part pressing drag.
    std::optional<std::size_t> drag{};
    ControlSource source{};
};

// One written attribute of a child: its registered (snake case) name and
// its value as written.
struct ControlChildAttribute final {
    std::string name{};
    std::string value{};
    bool bound{};
};

struct ControlChild final {
    // Its type's position in the form schema; none for the slot.
    std::optional<std::size_t> schema_type{};
    bool slot{};
    // Without the `@`.
    std::string local_id{};
    // Its parent's position in ControlModel::children; none at the top.
    std::optional<std::size_t> parent{};
    std::vector<ControlChildAttribute> attributes{};
    ControlSource source{};
};

enum class ControlActionKind : std::uint8_t {
    set,
    toggle,
    step,
    run,
    raise,
    focus,
    invoke,
    open,
    close,
};

struct ControlAction final {
    ControlActionKind kind{ControlActionKind::run};
    // The property, command, event or target the action names.
    std::string target{};
    // `to` or `by`, as written.
    std::string value{};
    ControlSource source{};
};

enum class ControlKeyPhase : std::uint8_t {
    key_down,
    key_up,
};

// An `<on>` rule or a `<key>` rule; its actions are
// ControlModel::actions[first_action, + action_count).
struct ControlRule final {
    bool key{};
    // The trigger expression of an <on>, or the gesture of a <key>.
    std::string trigger{};
    std::string when{};
    ControlKeyPhase phase{ControlKeyPhase::key_down};
    bool claims_dialog_key{};
    std::size_t first_action{};
    std::size_t action_count{};
    ControlSource source{};
};

enum class ControlFocusTarget : std::uint8_t {
    self,
    child,
    none,
};

struct ControlFocus final {
    ControlFocusTarget target{ControlFocusTarget::none};
    std::string child{};
    bool tab_stop{};
    // The cue's part; empty for none.
    std::string visible_cue{};
    bool roving{};
    std::string activation{};
    std::string mnemonic{};
    ControlSource source{};
};

struct ControlSemantics final {
    SemanticRole role{SemanticRole::generic};
    std::string name{};
    std::string value{};
    std::string range{};
    std::string states{};
    std::string actions{};
    bool children_exposed{};
    ControlSource source{};
};

// Caller-owned: replaced whole by a build that succeeds, left as it was by
// one that refuses. Each list is in document order.
struct ControlModel final {
    std::string name{};
    std::uint32_t version_major{};
    std::uint32_t version_minor{};
    CompositeBase base{CompositeBase::user_control};
    std::optional<ControlVisualRole> family{};
    std::vector<ControlProperty> properties{};
    std::vector<ControlEvent> events{};
    std::vector<ControlCommand> commands{};
    std::vector<ControlState> states{};
    std::vector<ControlPart> parts{};
    std::vector<ControlDrag> drags{};
    std::vector<ControlChild> children{};
    std::vector<ControlRule> rules{};
    std::vector<ControlAction> actions{};
    std::optional<ControlFocus> focus{};
    std::optional<ControlSemantics> semantics{};
    std::string look{};
    ControlSource source{};
};

enum class ControlModelOutcome : std::uint8_t {
    model_built,
    // At least one error: every defect found is reported and `model` is
    // unchanged. Warnings alone do not refuse.
    refused,
};

// Builds the model of the document `syntax` holds, read from `source`,
// checking it against `schema` (the types its children and base name).
// All three are borrowed for the call. Diagnostics are appended to
// `diagnostics`, with the declaration's name as their rule and the
// attribute as their property.
[[nodiscard]] ControlModelOutcome build_control_model(const theme_language::ThemeSource& source,
                                                      const ControlSyntax& syntax,
                                                      const form_language::FormSchema& schema, ControlModel& model,
                                                      theme_language::ThemeDiagnosticList& diagnostics);

// read_control_syntax, then build_control_model when it reads.
[[nodiscard]] ControlModelOutcome read_control_model(const theme_language::ThemeSource& source,
                                                     ControlSyntax& syntax, const form_language::FormSchema& schema,
                                                     ControlModel& model,
                                                     theme_language::ThemeDiagnosticList& diagnostics);

} // namespace gui_forms::control_language
