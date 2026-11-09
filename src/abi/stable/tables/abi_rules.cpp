// GF_TABLE_RULES (since 1.1): a window's rules over the C++ RuleSet. See
// gf_abi.h and notes:planning APP_WINDOW_RULES_DESIGN Part C.
//
// add translates a caller's gf_rule into a C++ Rule: objects become the
// controls and commands they name, an interaction trigger becomes a source
// that subscribes through the generated event catalog, a callback action
// becomes a program action whose context (the callback's binding) the rule
// owns, and SET and TOGGLE become program actions over the target's
// property. The rule set does the rest (validation, duplicates, evaluation,
// disarming). Tokens are identities in a table of this file, used on the
// UI thread only (every slot enters the UI first).

#include "abi_tables.hpp"

#include "../events/abi_interactions.hpp"
#include "../registry/abi_registry.hpp"
#include "../values/abi_values.hpp"

#include "gui_forms/commands/command/command.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/diagnostics/shadow_frames/shadow_frames.hpp"
#include "gui_forms/rules.hpp"
#include "gui_forms/window.hpp"
#include "../windows/abi_window_handle.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gui_forms::abi1 {
namespace {

constexpr SlotIdentity add_slot{GF_TABLE_RULES, 0U};
constexpr SlotIdentity remove_slot{GF_TABLE_RULES, 1U};
constexpr SlotIdentity state_slot{GF_TABLE_RULES, 2U};

// The 1.1 sizes of the input records.
constexpr std::uint32_t rule_size_1_1 = 72U;
constexpr std::uint32_t condition_size_1_1 = 32U;
constexpr std::uint32_t action_size_1_1 = 48U;
static_assert(sizeof(gf_rule) == rule_size_1_1);
static_assert(sizeof(gf_rule_condition) == condition_size_1_1);
static_assert(sizeof(gf_rule_action) == action_size_1_1);

// The most tokens add has given that remove has not freed (gf_abi.h).
constexpr std::size_t maximum_rule_tokens = 4096U;

// The modifiers GF_RULE_CONDITION_MODIFIERS admits.
constexpr std::uint32_t all_modifiers =
    GF_RULE_MODIFIER_SHIFT | GF_RULE_MODIFIER_CONTROL | GF_RULE_MODIFIER_ALT | GF_RULE_MODIFIER_META;
static_assert(GF_RULE_MODIFIER_SHIFT == static_cast<std::uint32_t>(Modifier::shift));
static_assert(GF_RULE_MODIFIER_CONTROL == static_cast<std::uint32_t>(Modifier::control));
static_assert(GF_RULE_MODIFIER_ALT == static_cast<std::uint32_t>(Modifier::alt));
static_assert(GF_RULE_MODIFIER_META == static_cast<std::uint32_t>(Modifier::meta));

// The C kinds are the C++ enumerators' values.
static_assert(GF_RULE_TRIGGER_KEY_PRESSED == static_cast<std::uint32_t>(RuleTriggerKind::key_pressed));
static_assert(GF_RULE_TRIGGER_FOCUS_LOST == static_cast<std::uint32_t>(RuleTriggerKind::focus_lost));
static_assert(GF_RULE_TRIGGER_INTERACTION == static_cast<std::uint32_t>(RuleTriggerKind::interaction));
static_assert(GF_RULE_TRIGGER_COMMAND_EXECUTED == static_cast<std::uint32_t>(RuleTriggerKind::command_executed));
static_assert(GF_RULE_CONDITION_KEY_DOWN == static_cast<std::uint32_t>(RuleConditionKind::key_down));
static_assert(GF_RULE_CONDITION_WINDOW_ACTIVE == static_cast<std::uint32_t>(RuleConditionKind::window_active));

// One token: the rule it observes, while add's caller has not freed it.
struct TokenSlot final {
    std::uint32_t generation{1U};
    bool occupied{};
    RuleToken token{};
};

// The token table. UI thread only.
struct TokenTable final {
    std::vector<TokenSlot> slots{};
    std::size_t occupied{};
};

TokenTable& tokens() noexcept {
    static TokenTable table{};
    return table;
}

// The slot of a token add gave and remove has not freed, or null.
TokenSlot* find_token(gf_rule_token token) noexcept {
    const Identity identity = decode_identity(token);
    TokenTable& table = tokens();
    if (identity.slot == 0U || identity.slot > table.slots.size()) {
        return nullptr;
    }
    TokenSlot& slot = table.slots[identity.slot - 1U];
    if (!slot.occupied || slot.generation != identity.generation) {
        return nullptr;
    }
    return &slot;
}

// Stores token in a free slot (the table has room: add checked).
gf_rule_token issue_token(RuleToken token) {
    TokenTable& table = tokens();
    std::size_t index = 0U;
    while (index < table.slots.size() && table.slots[index].occupied) {
        ++index;
    }
    if (index == table.slots.size()) {
        table.slots.emplace_back();
    }
    TokenSlot& slot = table.slots[index];
    slot.occupied = true;
    slot.token = token;
    ++table.occupied;
    const gf_rule_token issued =
        encode_identity(Identity{static_cast<std::uint32_t>(index + 1U), slot.generation});
    return issued;
}

void free_token(TokenSlot& slot) noexcept {
    slot.occupied = false;
    slot.token = RuleToken{};
    ++slot.generation;
    if (slot.generation == 0U) {
        slot.generation = 1U;
    }
    --tokens().occupied;
}

// An interaction named by its schema name. Each emission reaches the tap
// through a subscription binding whose callback is fired() below; the
// binding releases nothing (its context is the tap, which the rule set
// owns and which outlives the subscription).
class AbiInteractionSource final : public detail::RuleSource {
public:
    AbiInteractionSource(Component& component, std::string name) : component_(&component), name_(std::move(name)) {}

    [[nodiscard]] SubscriptionToken connect(detail::RuleTap& tap) override {
        gf_callback callback{};
        callback.struct_size = static_cast<std::uint32_t>(sizeof(gf_callback));
        callback.invoke = &AbiInteractionSource::fired;
        callback.release = nullptr;
        callback.context = &tap;
        const std::shared_ptr<SubscriptionBinding> binding =
            std::make_shared<SubscriptionBinding>(callback, 0U, component_, name_);
        SubscriptionToken subscription = connect_interaction(*component_, name_, binding);
        return subscription;
    }
    [[nodiscard]] const void* identity() const noexcept override { return component_; }
    [[nodiscard]] std::string_view name() const noexcept override { return name_; }

private:
    static void GF_CALL fired(void* context, gf_event*) noexcept {
        try {
            RuleFiring firing{};
            detail::fire_rule_tap(*static_cast<detail::RuleTap*>(context), firing);
        } catch (...) {
            // The actions of a C rule report their own failures; this is
            // what the C++ side might still throw (an allocation).
            static_cast<void>(fail_with_current_exception(add_slot));
        }
    }

    Component* component_;
    std::string name_;
};

// A CALLBACK action: the caller's callback, owned by the rule, and the
// trigger's source (the sender; null for a key trigger).
struct CallbackAction final {
    std::unique_ptr<CallbackBinding> callback{};
    const Component* source{};
};

void run_callback(CallbackAction& callback_action, const RuleFiring& firing) {
    gf_event event{};
    event.struct_size = static_cast<std::uint32_t>(sizeof(gf_event));
    event.sender = callback_action.source != nullptr ? registry().identity_of(callback_action.source) : 0U;
    const std::string_view name = rule_trigger_kind_name(firing.trigger);
    event.name.data = name.data();
    event.name.size = name.size();
    event.payload = nullptr;
    (*callback_action.callback).invoke(event);
}

// A SET or TOGGLE action: the target's property, written as
// properties.set writes it. A failure (the property refuses the value) is
// the calling thread's error detail; the firing goes on.
struct PropertyAction final {
    Control::WeakPtr target{};
    std::string property{};
    BindingValue value{};
    bool toggle{};
};

void run_property(PropertyAction& property_action, const RuleFiring&) {
    const Control::Ptr control = property_action.target.lock();
    if (!control || !(*control).is_alive()) {
        return;
    }
    try {
        if (!property_action.toggle) {
            BindingValue written = property_action.value;
            (*control).set_property_value(property_action.property, std::move(written));
            return;
        }
        const std::optional<BindingValue> current = (*control).property_value(property_action.property);
        if (!current) {
            return;
        }
        const std::optional<bool> held = binding_value_to_bool(*current);
        if (!held) {
            return;
        }
        const bool toggled = !*held;
        (*control).set_property_value(property_action.property, BindingValue{toggled});
    } catch (...) {
        static_cast<void>(fail_with_current_exception(add_slot));
    }
}

// What add built besides the rule: the callback bindings, which a refused
// add disarms (the library never took those callbacks).
struct Building final {
    std::vector<CallbackBinding*> callbacks{};

    void disarm() noexcept {
        for (CallbackBinding* binding : callbacks) {
            (*binding).disarm();
        }
    }
};

// The live control behind object for field `field`.
gf_status find_live_control(gf_object object, Control::Ptr& control) {
    if (object == 0U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, add_slot, "a rule field that names a control is not zero");
    }
    const gf_status found = registry().find_object(object, add_slot, control);
    if (found != GF_STATUS_OK) {
        return found;
    }
    if (!(*control).is_alive()) {
        return fail(GF_STATUS_DISPOSED, add_slot, "a control the rule names has been disposed");
    }
    return GF_STATUS_OK;
}

// The live command behind object.
gf_status find_live_command(gf_object object, std::shared_ptr<Command>& command) {
    if (object == 0U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, add_slot, "a rule field that names a command is not zero");
    }
    Component::Ptr component;
    const gf_status found = registry().find_component(object, add_slot, component);
    if (found != GF_STATUS_OK) {
        return found;
    }
    command = std::dynamic_pointer_cast<Command>(component);
    if (!command) {
        return fail(GF_STATUS_WRONG_HANDLE_KIND, add_slot, "the object is not a command");
    }
    if (!(*command).is_alive()) {
        return fail(GF_STATUS_DISPOSED, add_slot, "a command the rule names has been disposed");
    }
    return GF_STATUS_OK;
}

// The rule's trigger, and the sender its callbacks report.
gf_status build_trigger(const gf_rule& record, RuleTrigger& made, const Component*& sender) {
    const gf_rule_trigger_kind kind = record.trigger;
    if (kind >= GF_RULE_TRIGGER_KEY_PRESSED && kind <= GF_RULE_TRIGGER_KEY_REPEAT) {
        if (record.physical_key == 0U || record.source != 0U || record.interaction.size != 0U) {
            return fail(GF_STATUS_INVALID_ARGUMENT, add_slot, "a key trigger names a key and nothing else");
        }
        made.kind = static_cast<RuleTriggerKind>(kind);
        made.physical_key = record.physical_key;
        return GF_STATUS_OK;
    }
    if (record.physical_key != 0U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, add_slot, "only a key trigger names a key");
    }
    if (kind >= GF_RULE_TRIGGER_MOUSE_DOWN && kind <= GF_RULE_TRIGGER_FOCUS_LOST) {
        if (record.interaction.size != 0U) {
            return fail(GF_STATUS_INVALID_ARGUMENT, add_slot, "only an interaction trigger names an interaction");
        }
        Control::Ptr control;
        const gf_status found = find_live_control(record.source, control);
        if (found != GF_STATUS_OK) {
            return found;
        }
        made.kind = static_cast<RuleTriggerKind>(kind);
        made.control = control;
        Component& anchor = *control;
        made.anchor = &anchor;
        sender = &anchor;
        return GF_STATUS_OK;
    }
    if (kind == GF_RULE_TRIGGER_INTERACTION) {
        std::string_view name;
        const gf_status read = read_text(record.interaction, add_slot, name);
        if (read != GF_STATUS_OK) {
            return read;
        }
        if (name.empty() || record.source == 0U) {
            return fail(GF_STATUS_INVALID_ARGUMENT, add_slot, "an interaction trigger names a source and an interaction");
        }
        Component::Ptr component;
        const gf_status found = registry().find_component(record.source, add_slot, component);
        if (found != GF_STATUS_OK) {
            return found;
        }
        if (!(*component).is_alive()) {
            return fail(GF_STATUS_DISPOSED, add_slot, "the trigger's source has been disposed");
        }
        if (!raises_interaction(*component, name)) {
            return fail(GF_STATUS_NOT_FOUND, add_slot, "the source's type does not raise an interaction of that name");
        }
        made.kind = RuleTriggerKind::interaction;
        made.control = std::dynamic_pointer_cast<Control>(component);
        made.source = std::make_shared<AbiInteractionSource>(*component, std::string(name));
        made.anchor = component.get();
        sender = component.get();
        return GF_STATUS_OK;
    }
    if (kind == GF_RULE_TRIGGER_COMMAND_EXECUTED) {
        if (record.interaction.size != 0U) {
            return fail(GF_STATUS_INVALID_ARGUMENT, add_slot, "only an interaction trigger names an interaction");
        }
        std::shared_ptr<Command> command;
        const gf_status found = find_live_command(record.source, command);
        if (found != GF_STATUS_OK) {
            return found;
        }
        made = trigger::command_executed(*command);
        sender = command.get();
        return GF_STATUS_OK;
    }
    return fail(GF_STATUS_INVALID_ARGUMENT, add_slot, "the trigger kind is not one of GF_RULE_TRIGGER_*");
}

gf_status build_condition(const gf_rule_condition* record, RuleCondition& made) {
    if (record == nullptr || (*record).struct_size < condition_size_1_1 || (*record).negated > 1U ||
        (*record).reserved != 0U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, add_slot, "a condition record is malformed");
    }
    const gf_rule_condition_kind kind = (*record).kind;
    made.negated = (*record).negated == 1U;
    if (kind == GF_RULE_CONDITION_KEY_DOWN) {
        if ((*record).physical_key == 0U || (*record).object != 0U || (*record).modifiers != 0U) {
            return fail(GF_STATUS_INVALID_ARGUMENT, add_slot, "a key condition names a key and nothing else");
        }
        made.kind = RuleConditionKind::key_down;
        made.physical_key = (*record).physical_key;
        return GF_STATUS_OK;
    }
    if (kind == GF_RULE_CONDITION_MODIFIERS) {
        if (((*record).modifiers & ~all_modifiers) != 0U || (*record).object != 0U || (*record).physical_key != 0U) {
            return fail(GF_STATUS_INVALID_ARGUMENT, add_slot, "a modifiers condition names GF_RULE_MODIFIER_* only");
        }
        made.kind = RuleConditionKind::modifiers;
        made.modifiers = static_cast<Modifier>((*record).modifiers);
        return GF_STATUS_OK;
    }
    if (kind == GF_RULE_CONDITION_WINDOW_ACTIVE) {
        if ((*record).object != 0U || (*record).physical_key != 0U || (*record).modifiers != 0U) {
            return fail(GF_STATUS_INVALID_ARGUMENT, add_slot, "a window condition names nothing");
        }
        made.kind = RuleConditionKind::window_active;
        return GF_STATUS_OK;
    }
    if (kind >= GF_RULE_CONDITION_HOVERING && kind <= GF_RULE_CONDITION_VISIBLE) {
        if ((*record).physical_key != 0U || (*record).modifiers != 0U) {
            return fail(GF_STATUS_INVALID_ARGUMENT, add_slot, "a control condition names a control only");
        }
        Control::Ptr control;
        const gf_status found = find_live_control((*record).object, control);
        if (found != GF_STATUS_OK) {
            return found;
        }
        made.kind = static_cast<RuleConditionKind>(kind);
        made.control = control;
        return GF_STATUS_OK;
    }
    return fail(GF_STATUS_INVALID_ARGUMENT, add_slot, "the condition kind is not one of GF_RULE_CONDITION_*");
}

// The property of a SET or TOGGLE action, with its descriptor.
gf_status find_property(const gf_rule_action& record, const Control& control, std::string_view& property,
                        std::optional<PropertyDescriptor>& descriptor) {
    const gf_status read = read_text(record.property, add_slot, property);
    if (read != GF_STATUS_OK) {
        return read;
    }
    if (property.empty()) {
        return fail(GF_STATUS_INVALID_ARGUMENT, add_slot, "a property action names its property");
    }
    descriptor = control.property_descriptor(property);
    if (!descriptor) {
        return fail(GF_STATUS_NOT_FOUND, add_slot, "the target has no property of that name");
    }
    return GF_STATUS_OK;
}

gf_status build_action(const gf_rule_action* record, const Component* sender, RuleAction& made,
                       Building& building) {
    if (record == nullptr || (*record).struct_size < action_size_1_1) {
        return fail(GF_STATUS_INVALID_ARGUMENT, add_slot, "an action record is malformed");
    }
    const gf_rule_action_kind kind = (*record).kind;
    const bool names_property = kind == GF_RULE_ACTION_SET || kind == GF_RULE_ACTION_TOGGLE;
    if ((kind != GF_RULE_ACTION_CALLBACK && (*record).callback != nullptr) ||
        (kind != GF_RULE_ACTION_SET && (*record).value != nullptr) ||
        (!names_property && (*record).property.size != 0U) ||
        (kind == GF_RULE_ACTION_CALLBACK && (*record).target != 0U)) {
        return fail(GF_STATUS_INVALID_ARGUMENT, add_slot, "an action sets a field its kind does not read");
    }
    if (kind == GF_RULE_ACTION_CALLBACK) {
        gf_callback copy{};
        const gf_status read = read_callback((*record).callback, add_slot, copy);
        if (read != GF_STATUS_OK) {
            return read;
        }
        const std::shared_ptr<CallbackAction> context = std::make_shared<CallbackAction>();
        (*context).callback = std::make_unique<CallbackBinding>(copy);
        building.callbacks.push_back((*context).callback.get());
        (*context).source = sender;
        made = gui_forms::action(*context, &run_callback);
        made.retained = context;
        return GF_STATUS_OK;
    }
    if (kind == GF_RULE_ACTION_INVOKE) {
        std::shared_ptr<Command> command;
        const gf_status found = find_live_command((*record).target, command);
        if (found != GF_STATUS_OK) {
            return found;
        }
        made = builtin::invoke(*command);
        return GF_STATUS_OK;
    }
    if (kind == GF_RULE_ACTION_FOCUS) {
        Control::Ptr control;
        const gf_status found = find_live_control((*record).target, control);
        if (found != GF_STATUS_OK) {
            return found;
        }
        made = builtin::focus(*control);
        return GF_STATUS_OK;
    }
    if (names_property) {
        Control::Ptr control;
        const gf_status found = find_live_control((*record).target, control);
        if (found != GF_STATUS_OK) {
            return found;
        }
        std::string_view property;
        std::optional<PropertyDescriptor> descriptor;
        const gf_status located = find_property(*record, *control, property, descriptor);
        if (located != GF_STATUS_OK) {
            return located;
        }
        const std::shared_ptr<PropertyAction> context = std::make_shared<PropertyAction>();
        (*context).target = control;
        (*context).property = std::string(property);
        (*context).toggle = kind == GF_RULE_ACTION_TOGGLE;
        if ((*context).toggle) {
            if ((*descriptor).kind != BindingValueKind::boolean) {
                return fail(GF_STATUS_INVALID_ARGUMENT, add_slot, "a toggled property is boolean");
            }
        } else {
            const gf_value* value = (*record).value;
            if (value == nullptr || (*value).kind == GF_VALUE_KIND_COLLECTION) {
                return fail(GF_STATUS_INVALID_ARGUMENT, add_slot, "a set action has a value that is not a collection");
            }
            const gf_status converted = read_value(value, add_slot, (*context).value);
            if (converted != GF_STATUS_OK) {
                return converted;
            }
        }
        made = gui_forms::action(*context, &run_property);
        made.retained = context;
        Component& anchor = *control;
        made.anchor = &anchor;
        return GF_STATUS_OK;
    }
    return fail(GF_STATUS_INVALID_ARGUMENT, add_slot, "the action kind is not one of GF_RULE_ACTION_*");
}

// The status a refused C++ add gives.
gf_status refusal_status(RuleRefusal refusal) noexcept {
    switch (refusal) {
    case RuleRefusal::none:
        return GF_STATUS_OK;
    case RuleRefusal::too_many_conditions:
    case RuleRefusal::too_many_actions:
    case RuleRefusal::limit_reached:
        return fail(GF_STATUS_LIMIT_EXCEEDED, add_slot, "the rule or the window is at its limit");
    case RuleRefusal::disposed_reference:
        return fail(GF_STATUS_DISPOSED, add_slot, "an object the rule names has been disposed");
    case RuleRefusal::duplicate:
        return fail(GF_STATUS_INVALID_STATE, add_slot, "an armed rule has the same trigger and conditions");
    case RuleRefusal::malformed_trigger:
    case RuleRefusal::malformed_condition:
    case RuleRefusal::malformed_action:
    case RuleRefusal::no_actions:
    default:
        break;
    }
    return fail(GF_STATUS_INVALID_ARGUMENT, add_slot, "the rule is malformed");
}

gf_status add_rule(gf_object window, const gf_rule* record, gf_rule_token* token) {
    if (record == nullptr || token == nullptr || (*record).struct_size < rule_size_1_1) {
        return fail(GF_STATUS_INVALID_ARGUMENT, add_slot, "add needs a rule record and a token output");
    }
    if (((*record).flags & ~GF_RULE_FLAGS_CONSUME) != 0U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, add_slot, "the rule's flags are GF_RULE_FLAGS_* only");
    }
    if ((*record).condition_count > maximum_rule_conditions || (*record).action_count > maximum_rule_actions) {
        return fail(GF_STATUS_LIMIT_EXCEEDED, add_slot, "a rule has at most 8 conditions and 8 actions");
    }
    if ((*record).action_count == 0U || (*record).actions == nullptr ||
        ((*record).condition_count != 0U && (*record).conditions == nullptr)) {
        return fail(GF_STATUS_INVALID_ARGUMENT, add_slot, "a rule has one or more actions and its conditions");
    }
    const gf_status entered = registry().enter_ui(add_slot, false);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    Component::Ptr object;
    const gf_status found = registry().find_component(window, add_slot, object);
    if (found != GF_STATUS_OK) {
        return found;
    }
    const std::shared_ptr<WindowHandle> handle = std::dynamic_pointer_cast<WindowHandle>(object);
    if (handle == nullptr) {
        return fail(GF_STATUS_WRONG_HANDLE_KIND, add_slot, "add takes a window");
    }
    Window* const held = (*handle).window();
    if (!(*handle).is_alive() || held == nullptr) {
        return fail(GF_STATUS_DISPOSED, add_slot, "the window has been disposed or has closed");
    }
    if (tokens().occupied >= maximum_rule_tokens) {
        return fail(GF_STATUS_LIMIT_EXCEEDED, add_slot, "4096 rule tokens are not freed");
    }
    Rule rule{};
    rule.consume = ((*record).flags & GF_RULE_FLAGS_CONSUME) != 0U;
    const Component* sender = nullptr;
    const gf_status triggered = build_trigger(*record, rule.trigger, sender);
    if (triggered != GF_STATUS_OK) {
        return triggered;
    }
    const std::size_t condition_count = static_cast<std::size_t>((*record).condition_count);
    const std::size_t action_count = static_cast<std::size_t>((*record).action_count);
    rule.conditions.resize(condition_count);
    for (std::size_t index = 0U; index < condition_count; ++index) {
        const gf_status built = build_condition(&(*record).conditions[index], rule.conditions[index]);
        if (built != GF_STATUS_OK) {
            return built;
        }
    }
    Building building{};
    rule.actions.resize(action_count);
    for (std::size_t index = 0U; index < action_count; ++index) {
        const gf_status built = build_action(&(*record).actions[index], sender, rule.actions[index], building);
        if (built != GF_STATUS_OK) {
            building.disarm();
            return built;
        }
    }
    RuleAddResult added{};
    try {
        added = (*held).rules().add(rule);
    } catch (...) {
        building.disarm();
        throw;
    }
    if (added.refusal != RuleRefusal::none) {
        building.disarm();
        return refusal_status(added.refusal);
    }
    *token = issue_token(added.token);
    return GF_STATUS_OK;
}

gf_status GF_CALL rules_add(gf_object window, const gf_rule* rule, gf_rule_token* token) noexcept {
    try {
        const ShadowFrameScope shadow_frame(add_slot.table_id, add_slot.slot_index);
        const gf_status added = add_rule(window, rule, token);
        return added;
    } catch (...) {
        return fail_with_current_exception(add_slot);
    }
}

gf_status GF_CALL rules_remove(gf_rule_token token) noexcept {
    try {
        const ShadowFrameScope shadow_frame(remove_slot.table_id, remove_slot.slot_index);
        const gf_status entered = registry().enter_ui(remove_slot, false);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        TokenSlot* const slot = find_token(token);
        if (slot == nullptr) {
            return fail(GF_STATUS_STALE_HANDLE, remove_slot, "the token was freed or never given");
        }
        // The rule's callbacks are released when the rule set drops it.
        (*slot).token.revoke();
        free_token(*slot);
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(remove_slot);
    }
}

gf_status GF_CALL rules_state(gf_rule_token token, gf_rule_state* state) noexcept {
    try {
        const ShadowFrameScope shadow_frame(state_slot.table_id, state_slot.slot_index);
        if (state == nullptr) {
            return fail(GF_STATUS_INVALID_ARGUMENT, state_slot, "state needs an output");
        }
        const gf_status entered = registry().enter_ui(state_slot, false);
        if (entered != GF_STATUS_OK) {
            return entered;
        }
        const TokenSlot* const slot = find_token(token);
        if (slot == nullptr) {
            return fail(GF_STATUS_STALE_HANDLE, state_slot, "the token was freed or never given");
        }
        const RuleTokenState held = (*slot).token.state();
        *state = held == RuleTokenState::armed ? GF_RULE_STATE_ARMED : GF_RULE_STATE_STALE;
        return GF_STATUS_OK;
    } catch (...) {
        return fail_with_current_exception(state_slot);
    }
}

constexpr gf_rules_table table{
    .struct_size = sizeof(gf_rules_table),
    .abi_minor = GF_ABI_MINOR,
    .add = &rules_add,
    .remove = &rules_remove,
    .state = &rules_state,
};

} // namespace

const gf_rules_table& rules_table() noexcept {
    return table;
}

} // namespace gui_forms::abi1
