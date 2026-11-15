#pragma once

// Composite controls (cp34, phase 2; design note
// COMPOSITE_CONTROL_LANGUAGE.md, section 4.1): a control type defined by a
// `.control` document instead of a C++ class, and the one control class
// that runs any such definition.
//
// A definition is made once per type, from the document, by
// load_composite_definition (at run time) or by the header
// gui_forms_add_control generates (at build time, which checks the
// document when the target builds and loads it on first use). It is
// immutable and shared: every instance holds it until it is destroyed.
//
// An instance is a real control: its anatomy (region, focus and the
// declared parts), its part geometry (the parts' boxes, evaluated on
// layout and on property and state changes, never per frame), its hit
// regions, its properties (registered on the control, so bindings, forms
// and the property grid reach them by name), its states, its commands, its
// key and pointer rules, its drags (one DragTracker, made only when the
// definition declares a drag), its children (made from the document with
// stable IDs under the instance's own) and its semantics.
//
// Event order:
// - A property write (set_property_value, a binding, a rule, a drag)
//   validates first and changes nothing when refused; then stores, marks
//   the declared effects dirty, raises the property's change notification,
//   raises the declared change event through raised(), runs the `changed`
//   rules, and writes the children bound to it.
// - Rules of one trigger run in document order, a rule's actions in
//   order. Nested firings (a write inside a rule firing further rules) are
//   limited to maximum_composite_rule_depth; the excess is refused and
//   counted (statistics().refused_firings). Disposing the composite inside a
//   firing drops the remaining actions.
// - A press on a part that activates a command runs it in the click (on
//   release over the same part), on the press (`press="down"`), or on the
//   press and then repeatedly while held (`press="repeat"`, the scroll
//   bar's timing). A part that drags hands the press to the tracker; a
//   release under the threshold is the click.
// - A commit-mode drag writes only presentational properties while it
//   runs, and its property once at an end that commits; a live drag writes
//   its property on every frame and writes the pressed value back at an end
//   that does not commit.
//
// Threads: UI thread of the instance's window, as every control. A
// definition may be shared by instances on several UI threads (it does not
// change after it is made).

#include "gui_forms/binding/value/binding_value.hpp"
#include "gui_forms/commands/command/command.hpp"
#include "gui_forms/controls/user_control.hpp"
#include "gui_forms/drag/drag_tracker/drag_tracker.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/export/application_export.hpp"
#include "gui_forms/scheduler.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

// The compiled, immutable definition of one composite type (internal).
class CompositeDefinition;

// One control type a composite's children may name, besides the base
// (user_control): its registered name and how to make one. A definition
// loaded at run time names the types its document uses; the generated
// header names them for the build.
// A type whose objects take a Command (a button's bind(Command&)) also
// names how to bind one, for a child bound to a command property.
using CompositeChildCreator = Component::Ptr (*)(StableId stable_id);
using CompositeChildCommandBinder = void (*)(Component& owner, Command& command);
struct GUI_FORMS_API CompositeChildType final {
    std::string_view name{};
    CompositeChildCreator create{};
    CompositeChildCommandBinder bind_command{};
};

// The creator of a control type, for CompositeChildType::create.
template <typename ControlType>
[[nodiscard]] Component::Ptr create_composite_child(StableId stable_id) {
    std::shared_ptr<ControlType> made = make_control<ControlType>(std::move(stable_id));
    Component::Ptr component = std::move(made);
    return component;
}

// The Command binder of a control type with bind(Command&).
template <typename ControlType>
void bind_composite_child_command(Component& owner, Command& command) {
    ControlType& control = static_cast<ControlType&>(owner);
    control.bind(command);
}

// The entry of a control type: its creator, and its binder when it takes a
// Command.
template <typename ControlType>
[[nodiscard]] constexpr CompositeChildType composite_child_type(std::string_view name) noexcept {
    CompositeChildType type{};
    type.name = name;
    type.create = &create_composite_child<ControlType>;
    if constexpr (requires(ControlType& control, Command& command) { control.bind(command); }) {
        type.bind_command = &bind_composite_child_command<ControlType>;
    }
    return type;
}

// What load_composite_definition made: the definition, or null with every
// refusal in `diagnostics`, one line each in the compilers' form
// (`file:line:column: error: E918 control_reference_unknown: rule property:
// detail`). Warnings (W950) are listed with a definition too.
struct GUI_FORMS_API CompositeLoadResult final {
    std::shared_ptr<const CompositeDefinition> definition{};
    std::vector<std::string> diagnostics{};

    [[nodiscard]] explicit operator bool() const noexcept {
        const bool loaded = definition != nullptr;
        return loaded;
    }
};

// Reads, checks and compiles `document` (named `file` in the diagnostics)
// against the base types and `child_types`. Everything is borrowed for the
// call. UI thread: each child type is described from a prototype it makes
// and releases.
[[nodiscard]] GUI_FORMS_API CompositeLoadResult load_composite_definition(
    std::string_view file, std::string_view document, std::span<const CompositeChildType> child_types);

// The type's registered (snake case) name.
[[nodiscard]] GUI_FORMS_API std::string_view composite_type_name(const CompositeDefinition& definition) noexcept;

// An event a composite raised (a declared <event>): its name as written,
// borrowed from the definition, and its payload (null for none).
struct GUI_FORMS_API CompositeEvent final {
    std::string_view name{};
    BindingValue payload{};
};

// The deepest nesting of rule firings (Q12).
inline constexpr std::size_t maximum_composite_rule_depth = 8U;

// Counters (AGENTS.md: structured instrumentation).
//   rule_firings: rules whose condition held and whose actions ran;
//   refused_firings: firings past maximum_composite_rule_depth;
//   commands_run / commands_refused: perform and activations, refused when
//     the command is not enabled;
//   refused_writes: property writes refused before any change;
//   geometry_passes: part geometry evaluations;
//   drags_committed / drags_restored: drag ends.
struct GUI_FORMS_API CompositeStatistics final {
    std::uint64_t rule_firings{};
    std::uint64_t refused_firings{};
    std::uint64_t commands_run{};
    std::uint64_t commands_refused{};
    std::uint64_t refused_writes{};
    std::uint64_t geometry_passes{};
    std::uint64_t drags_committed{};
    std::uint64_t drags_restored{};
};

class CompositeEvaluator;
class CompositeInstanceAccess;

// The control that runs a definition. Not final: the header
// gui_forms_add_control generates derives a typed class from it.
class GUI_FORMS_API CompositeControl : public UserControl {
public:
    static constexpr bool initialize_tree_after_construction = true;

    // Throws std::invalid_argument for a null definition.
    CompositeControl(StableId stable_id, std::shared_ptr<const CompositeDefinition> definition);
    ~CompositeControl() override;
    // Makes the children and their bindings (make_control calls it once,
    // after construction). Throws std::invalid_argument when a child is
    // refused; the composite then has no children.
    void initialize_control_tree();

    [[nodiscard]] const CompositeDefinition& definition() const noexcept { return *definition_; }
    [[nodiscard]] std::string_view type_name() const noexcept;

    // The declared events, as they are raised.
    [[nodiscard]] Event<const CompositeEvent&>& raised() noexcept { return raised_; }

    // Runs the command `name` (the path a part, a key, a semantic action
    // and generated code share). False, changing nothing, for an unknown
    // command or one that is not enabled.
    bool perform(std::string_view name);
    [[nodiscard]] bool command_enabled(std::string_view name) const;

    // Binds `command` to the command-kind property `name`: the children
    // that bind that property take it, and <invoke> runs it. Borrowed: the
    // command must outlive the composite or be unbound (a null command)
    // first. Throws std::invalid_argument for an unknown property or one of
    // another kind.
    void bind_command(std::string_view name, Command* command);

    // A child by its local ID (`title` for `@title`); null when none.
    [[nodiscard]] Control::Ptr child(std::string_view local_id) const;
    // The slot a form's content goes into; null without one.
    [[nodiscard]] Control::Ptr slot() const;
    // A part's published bounds in local coordinates; empty when absent
    // or not declared.
    [[nodiscard]] Rect part_bounds(std::string_view part) const;
    // A presentational property's value (null when unset).
    [[nodiscard]] BindingValue presentational_value(std::string_view name) const;

    [[nodiscard]] const CompositeStatistics& statistics() const noexcept { return statistics_; }

    [[nodiscard]] Size measure(Size available) override;
    void arrange(Rect final_bounds) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    [[nodiscard]] AnatomyStateSet anatomy_states() const override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    bool on_semantic_action(SemanticAction action, std::string_view value) override;
    [[nodiscard]] Ptr focus_target() const override;
    bool begin_keyboard_drag() override;

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;
    void on_mouse_down(MouseEventArgs& arguments) override;
    void on_mouse_up(MouseEventArgs& arguments) override;
    void on_mouse_move(MouseEventArgs& arguments) override;
    void on_mouse_leave() override;
    void on_mouse_click(MouseEventArgs& arguments) override;
    void on_mouse_wheel(MouseEventArgs& arguments) override;
    void on_preview_key_down(PreviewKeyDownEventArgs& arguments) override;
    void on_key_down(KeyEventArgs& arguments) override;
    void on_key_up(KeyEventArgs& arguments) override;
    void on_got_focus() override;
    void on_lost_focus() override;
    void on_detaching_from_window(SurfaceRoot& former_root) noexcept override;

private:
    friend class CompositeEvaluator;
    friend class CompositeInstanceAccess;

    // The part geometry, cached between changes: `logical` before the
    // right-to-left mirror (what expressions read), `published` after.
    struct Geometry final {
        std::array<Rect, maximum_control_parts> logical{};
        std::array<Rect, maximum_control_parts> published{};
        std::array<bool, maximum_control_parts> present{};
        std::array<bool, maximum_control_parts> disabled{};
        std::size_t count{};
        bool valid{};
    };
    // One made child, by its local ID.
    struct MadeChild final {
        std::string local_id{};
        Control::Ptr control{};
        // Its type's index in the definition's schema.
        std::size_t schema_type{};
    };
    // The drag in progress, if any.
    struct DragState final {
        std::optional<std::size_t> part{};
        // The maps-to value at the press (live mode's restore).
        BindingValue pressed_value{};
        // The range's logical ends, fixed when the drag plans.
        double range_start{};
        double range_end{};
        double frame_value{};
        bool planned{};
    };

    void ensure_geometry() const;
    void invalidate_geometry() noexcept;
    [[nodiscard]] std::optional<std::size_t> part_at(Point local_point) const;
    [[nodiscard]] AnatomyStateSet part_states(std::size_t part) const;

    // What a property write did.
    enum class WriteOutcome : std::uint8_t {
        written,
        unchanged,
        // The value does not convert to the property's kind (or it is a
        // command): nothing changed.
        refused,
    };

    // Properties (composite_control.cpp).
    void register_properties();
    [[nodiscard]] BindingValue read_property(std::size_t index) const;
    // Validates, clamps to the range, stores and notifies.
    WriteOutcome write_property(std::size_t index, const BindingValue& value);
    void property_changed(std::size_t index);
    void raise(std::size_t event, BindingValue payload);

    // Rules, commands and actions (composite_control.cpp; the internal
    // CompositeInstanceAccess runs what takes the definition's types).
    void fire(std::size_t rule, double wheel_notches);
    bool run_command(std::size_t command);
    [[nodiscard]] bool command_enabled_at(std::size_t command) const;
    void activate_part(std::size_t part);
    bool handle_key(const KeyEventArgs& arguments, bool key_up);
    [[nodiscard]] bool claims_key(std::uint32_t physical_key, Modifier modifiers) const noexcept;

    // Children (composite_children.cpp).
    void make_children();
    void write_bound_children(std::optional<std::size_t> changed);
    void child_changed(std::size_t binding);
    void child_activated(std::size_t child);
    void child_property_fired(std::size_t rule);

    // Drags and press repetition.
    void plan_drag(DragPlan& plan);
    void preview_drag(const DragTrackerFrame& frame);
    void finish_drag(const DragTrackerFrame& frame);
    [[nodiscard]] double drag_fraction(double value) const noexcept;
    void begin_repeat(std::size_t part);
    void repeat_step();

    std::shared_ptr<const CompositeDefinition> definition_;
    // One value per declared property, by index (aliases and commands
    // keep theirs elsewhere), and one change notification each.
    std::vector<BindingValue> values_{};
    std::vector<std::unique_ptr<Event<>>> changed_events_{};
    std::vector<Command*> commands_{};
    std::vector<MadeChild> children_{};
    std::vector<SubscriptionToken> child_subscriptions_{};
    Event<const CompositeEvent&> raised_{};
    mutable Geometry geometry_{};
    mutable CompositeStatistics statistics_{};
    std::optional<std::size_t> hot_part_{};
    std::optional<std::size_t> pressed_part_{};
    // The part a primary release ended on, when it is the pressed one: the
    // click that follows activates it.
    std::optional<std::size_t> released_part_{};
    bool pressed_inside_{};
    std::unique_ptr<DragTracker> drag_{};
    DragState drag_state_{};
    FrameRequestToken repeat_frames_{};
    std::size_t firing_depth_{};
    // Set while the children's bound properties are written from the
    // composite, so their change notifications do not flow back.
    bool writing_children_{};
};

} // namespace gui_forms
