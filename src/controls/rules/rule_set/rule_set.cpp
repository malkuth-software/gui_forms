// A window's rule set (rule_set.hpp): validation, the shared subscriptions
// (taps) that bring control events, interactions, commands and Values to
// the rules, evaluation in declaration order with consume, the built-in
// actions, and disarming when a named component goes.
//
// Ownership. The rule set owns its entries (shared, so that a token can
// observe one) and its taps. Each entry owns an anchor, a Revocable that
// every component the rule names holds weakly (Component::own_revocable):
// disposing any of them disarms the entry. A tap is one subscription to one
// event, shared by every armed rule on it; it is disconnected when its last
// rule ends. Ended entries and unused taps are dropped only when no
// evaluation is under way, because an action may end the rule, or the tap,
// whose evaluation is running it.

#include "gui_forms/rules/rule_set/rule_set.hpp"

#include "gui_forms/surface_root/surface_root.hpp"

#include <array>
#include <utility>

namespace gui_forms {
namespace detail {

class RuleAnchor;

// One rule as the set keeps it: the copied record, the tap its trigger
// uses (null for a key rule), its state and its anchor.
struct RuleEntry final {
    RuleSet* rules{};
    std::uint64_t order{};
    Rule rule{};
    RuleTap* tap{};
    RuleTokenState state{RuleTokenState::armed};
    std::shared_ptr<RuleAnchor> anchor{};
};

// Held weakly by every component a rule names; its disconnect() is the
// component's disposal, which disarms the rule (stale). It observes the
// entry, which the rule set owns, so a disposal after the rule set has gone
// does nothing.
class RuleAnchor final : public Revocable {
public:
    explicit RuleAnchor(std::weak_ptr<RuleEntry> entry) noexcept : entry_(std::move(entry)) {}

    void disconnect() noexcept override {
        if (!connected_) return;
        connected_ = false;
        const std::shared_ptr<RuleEntry> entry = entry_.lock();
        if (!entry || (*entry).state != RuleTokenState::armed) return;
        RuleSet& rules = *(*entry).rules;
        rules.disarm(*entry, RuleTokenState::stale);
    }
    [[nodiscard]] bool connected() const noexcept override { return connected_; }
    // The rule ended another way: later disposals have nothing to do.
    void retire() noexcept { connected_ = false; }

private:
    std::weak_ptr<RuleEntry> entry_;
    bool connected_{true};
};

// One subscription shared by the rules on one trigger: a control event (the
// kind and the control), a command's execution, or a source (an interaction
// or a Value). Its members are the delegates' targets; each fills the
// firing's payload and offers it to the rule set. A member does not touch
// the tap after offering, since the tap may be dropped once the evaluation
// ends.
class RuleTap final {
public:
    RuleTap(RuleSet& rules, const RuleTrigger& trigger) noexcept
        : rules_(&rules), kind_(trigger.kind), control_(trigger.control), command_(trigger.command),
          source_(trigger.source) {}

    [[nodiscard]] bool serves(const RuleTrigger& trigger) const noexcept {
        if (trigger.kind != kind_) return false;
        if (source_ || trigger.source) {
            const bool same_source = source_ && trigger.source && (*source_).identity() == (*trigger.source).identity() &&
                                     (*source_).name() == (*trigger.source).name();
            return same_source;
        }
        if (command_ != nullptr || trigger.command != nullptr) return command_ == trigger.command;
        const bool same_control = !control_.owner_before(trigger.control) && !trigger.control.owner_before(control_);
        return same_control;
    }

    void connect() {
        if (source_) {
            subscription_ = (*source_).connect(*this);
            return;
        }
        if (command_ != nullptr) {
            const Delegate<const CommandInvocation&> forward =
                Delegate<const CommandInvocation&>::bind<RuleTap, &RuleTap::fire_invocation>(*this);
            subscription_ = (*command_).invoked().subscribe(forward);
            return;
        }
        const Control::Ptr control = control_.lock();
        if (!control) return;
        Control& target = *control;
        connect_control_event(target);
    }

    void disconnect() noexcept { subscription_.disconnect(); }

    void fire_plain() {
        RuleFiring firing{};
        offer(firing);
    }
    void fire_mouse(const MouseEventArgs& arguments) {
        RuleFiring firing{};
        firing.mouse = &arguments;
        offer(firing);
    }
    void fire_context(ContextMenuRequest& request) {
        RuleFiring firing{};
        firing.context_request = &request;
        const bool consumed = offer(firing);
        // A consuming rule keeps the request from the control's ancestors.
        if (consumed) request.handled = true;
    }
    void fire_focus(bool focused) {
        const bool gained_kind = kind_ == RuleTriggerKind::focus_gained;
        if (focused != gained_kind) return;
        RuleFiring firing{};
        offer(firing);
    }
    void fire_invocation(const CommandInvocation& invocation) {
        RuleFiring firing{};
        firing.invocation = &invocation;
        offer(firing);
    }

    // Fills the trigger kind and control and offers the occurrence.
    bool offer(RuleFiring& firing) {
        firing.trigger = kind_;
        const Control::Ptr control = control_.lock();
        firing.control = control.get();
        RuleSet& rules = *rules_;
        const bool consumed = rules.offer(this, firing);
        return consumed;
    }

    std::size_t users{};

private:
    // The control's event for the kind (the WinForms mouse events and
    // focus_observed). A kind outside them connects nothing.
    void connect_control_event(Control& control) {
        switch (kind_) {
        case RuleTriggerKind::mouse_down:
            subscription_ = control.mouse_down().subscribe(mouse_delegate());
            return;
        case RuleTriggerKind::mouse_up:
            subscription_ = control.mouse_up().subscribe(mouse_delegate());
            return;
        case RuleTriggerKind::mouse_click:
            subscription_ = control.mouse_click().subscribe(mouse_delegate());
            return;
        case RuleTriggerKind::mouse_double_click:
            subscription_ = control.mouse_double_click().subscribe(mouse_delegate());
            return;
        case RuleTriggerKind::click:
            subscription_ = control.click().subscribe(plain_delegate());
            return;
        case RuleTriggerKind::double_click:
            subscription_ = control.double_click().subscribe(plain_delegate());
            return;
        case RuleTriggerKind::mouse_enter:
            subscription_ = control.mouse_enter().subscribe(plain_delegate());
            return;
        case RuleTriggerKind::mouse_hover:
            subscription_ = control.mouse_hover().subscribe(plain_delegate());
            return;
        case RuleTriggerKind::mouse_leave:
            subscription_ = control.mouse_leave().subscribe(plain_delegate());
            return;
        case RuleTriggerKind::mouse_capture_changed:
            subscription_ = control.mouse_capture_changed().subscribe(plain_delegate());
            return;
        case RuleTriggerKind::context_menu_requested: {
            const Delegate<ContextMenuRequest&> forward =
                Delegate<ContextMenuRequest&>::bind<RuleTap, &RuleTap::fire_context>(*this);
            subscription_ = control.context_menu_requested().subscribe(forward);
            return;
        }
        case RuleTriggerKind::focus_gained:
        case RuleTriggerKind::focus_lost: {
            const Delegate<bool> forward = Delegate<bool>::bind<RuleTap, &RuleTap::fire_focus>(*this);
            subscription_ = control.focus_observed().subscribe(forward);
            return;
        }
        default:
            break;
        }
    }

    [[nodiscard]] Delegate<const MouseEventArgs&> mouse_delegate() noexcept {
        const Delegate<const MouseEventArgs&> forward =
            Delegate<const MouseEventArgs&>::bind<RuleTap, &RuleTap::fire_mouse>(*this);
        return forward;
    }
    [[nodiscard]] Delegate<> plain_delegate() noexcept {
        const Delegate<> forward = Delegate<>::bind<RuleTap, &RuleTap::fire_plain>(*this);
        return forward;
    }

    RuleSet* rules_;
    RuleTriggerKind kind_;
    Control::WeakPtr control_;
    Command* command_;
    std::shared_ptr<RuleSource> source_;
    SubscriptionToken subscription_{};
};

void fire_rule_tap(RuleTap& tap, RuleFiring& firing) {
    static_cast<void>(tap.offer(firing));
}

} // namespace detail

namespace {

[[nodiscard]] bool is_key_trigger(RuleTriggerKind kind) noexcept {
    const bool key = kind == RuleTriggerKind::key_pressed || kind == RuleTriggerKind::key_released ||
                     kind == RuleTriggerKind::key_repeat;
    return key;
}

[[nodiscard]] bool is_control_trigger(RuleTriggerKind kind) noexcept {
    const std::uint8_t value = static_cast<std::uint8_t>(kind);
    const bool control = value >= static_cast<std::uint8_t>(RuleTriggerKind::mouse_down) &&
                         value <= static_cast<std::uint8_t>(RuleTriggerKind::focus_lost);
    return control;
}

[[nodiscard]] bool live_control(const Control::WeakPtr& control) noexcept {
    const Control::Ptr held = control.lock();
    const bool live = held && (*held).is_alive();
    return live;
}

[[nodiscard]] bool live_component(const Component* component) noexcept {
    const bool live = component != nullptr && (*component).is_alive();
    return live;
}

[[nodiscard]] bool same_control(const Control::WeakPtr& left, const Control::WeakPtr& right) noexcept {
    const bool same = !left.owner_before(right) && !right.owner_before(left);
    return same;
}

[[nodiscard]] bool same_condition(const RuleCondition& left, const RuleCondition& right) noexcept {
    const bool same = left.kind == right.kind && left.negated == right.negated &&
                      left.physical_key == right.physical_key && left.modifiers == right.modifiers &&
                      same_control(left.control, right.control) && left.flag == right.flag &&
                      left.number == right.number && left.expected == right.expected;
    return same;
}

[[nodiscard]] bool same_trigger(const RuleTrigger& left, const RuleTrigger& right) noexcept {
    if (left.kind != right.kind || left.physical_key != right.physical_key || left.command != right.command) {
        return false;
    }
    if (!same_control(left.control, right.control)) return false;
    if (static_cast<bool>(left.source) != static_cast<bool>(right.source)) return false;
    if (!left.source) return true;
    const bool same = (*left.source).identity() == (*right.source).identity() &&
                      (*left.source).name() == (*right.source).name();
    return same;
}

// Whether every condition of `left` has an equal condition in `right` (both
// sets have the same size; a condition repeated counts once per copy).
[[nodiscard]] bool same_condition_set(const std::vector<RuleCondition>& left,
                                      const std::vector<RuleCondition>& right) noexcept {
    if (left.size() != right.size()) return false;
    std::array<bool, maximum_rule_conditions> matched{};
    for (const RuleCondition& wanted : left) {
        bool found = false;
        for (std::size_t index = 0U; index < right.size(); ++index) {
            if (matched[index] || !same_condition(wanted, right[index])) continue;
            matched[index] = true;
            found = true;
            break;
        }
        if (!found) return false;
    }
    return true;
}

[[nodiscard]] RuleRefusal validate_trigger(const RuleTrigger& trigger) noexcept {
    const std::uint8_t kind = static_cast<std::uint8_t>(trigger.kind);
    if (trigger.kind == RuleTriggerKind::none || kind > static_cast<std::uint8_t>(RuleTriggerKind::value_changed)) {
        return RuleRefusal::malformed_trigger;
    }
    if (is_key_trigger(trigger.kind)) {
        return trigger.physical_key == 0U ? RuleRefusal::malformed_trigger : RuleRefusal::none;
    }
    if (is_control_trigger(trigger.kind)) {
        if (trigger.control.expired() && trigger.anchor == nullptr) return RuleRefusal::malformed_trigger;
        return live_control(trigger.control) ? RuleRefusal::none : RuleRefusal::disposed_reference;
    }
    if (trigger.kind == RuleTriggerKind::command_executed) {
        if (trigger.command == nullptr) return RuleRefusal::malformed_trigger;
        return live_component(trigger.command) ? RuleRefusal::none : RuleRefusal::disposed_reference;
    }
    // interaction, value_changed: a source, and the component it belongs to
    // (an interaction's control in C++; any component through the C ABI).
    if (!trigger.source || trigger.anchor == nullptr) return RuleRefusal::malformed_trigger;
    return live_component(trigger.anchor) ? RuleRefusal::none : RuleRefusal::disposed_reference;
}

[[nodiscard]] RuleRefusal validate_condition(const RuleCondition& condition) noexcept {
    switch (condition.kind) {
    case RuleConditionKind::key_down:
        return condition.physical_key == 0U ? RuleRefusal::malformed_condition : RuleRefusal::none;
    case RuleConditionKind::modifiers:
    case RuleConditionKind::window_active:
        return RuleRefusal::none;
    case RuleConditionKind::hovering:
    case RuleConditionKind::pressed:
    case RuleConditionKind::focused:
    case RuleConditionKind::focus_within:
    case RuleConditionKind::enabled:
    case RuleConditionKind::visible:
        return live_control(condition.control) ? RuleRefusal::none : RuleRefusal::disposed_reference;
    case RuleConditionKind::is_true:
        if (condition.flag == nullptr || condition.anchor == nullptr) return RuleRefusal::malformed_condition;
        return live_component(condition.anchor) ? RuleRefusal::none : RuleRefusal::disposed_reference;
    case RuleConditionKind::equals:
        if (condition.number == nullptr || condition.anchor == nullptr) return RuleRefusal::malformed_condition;
        return live_component(condition.anchor) ? RuleRefusal::none : RuleRefusal::disposed_reference;
    case RuleConditionKind::none:
    default:
        break;
    }
    return RuleRefusal::malformed_condition;
}

[[nodiscard]] RuleRefusal validate_action(const RuleAction& action) noexcept {
    switch (action.kind) {
    case RuleActionKind::program:
        if (action.context == nullptr || action.function == nullptr || action.call == nullptr) {
            return RuleRefusal::malformed_action;
        }
        if (action.anchor != nullptr && !(*action.anchor).is_alive()) return RuleRefusal::disposed_reference;
        return RuleRefusal::none;
    case RuleActionKind::invoke:
        if (action.command == nullptr) return RuleRefusal::malformed_action;
        return live_component(action.command) ? RuleRefusal::none : RuleRefusal::disposed_reference;
    case RuleActionKind::set:
    case RuleActionKind::toggle:
        if (action.value == nullptr || action.write == nullptr || action.anchor == nullptr) {
            return RuleRefusal::malformed_action;
        }
        return live_component(action.anchor) ? RuleRefusal::none : RuleRefusal::disposed_reference;
    case RuleActionKind::focus:
        return live_control(action.control) ? RuleRefusal::none : RuleRefusal::disposed_reference;
    case RuleActionKind::none:
    default:
        break;
    }
    return RuleRefusal::malformed_action;
}

// Registers `anchor` with `component` (a disposal disarms the rule).
void anchor_to(Component* component, const std::shared_ptr<detail::RuleAnchor>& anchor) {
    if (component == nullptr) return;
    const std::weak_ptr<detail::Revocable> observed = anchor;
    (*component).own_revocable(observed);
}

void anchor_to(const Control::WeakPtr& control, const std::shared_ptr<detail::RuleAnchor>& anchor) {
    const Control::Ptr held = control.lock();
    if (!held) return;
    Component& component = *held;
    anchor_to(&component, anchor);
}

} // namespace

std::string_view rule_refusal_name(RuleRefusal refusal) noexcept {
    switch (refusal) {
    case RuleRefusal::none: return "none";
    case RuleRefusal::malformed_trigger: return "malformed_trigger";
    case RuleRefusal::malformed_condition: return "malformed_condition";
    case RuleRefusal::malformed_action: return "malformed_action";
    case RuleRefusal::no_actions: return "no_actions";
    case RuleRefusal::too_many_conditions: return "too_many_conditions";
    case RuleRefusal::too_many_actions: return "too_many_actions";
    case RuleRefusal::disposed_reference: return "disposed_reference";
    case RuleRefusal::duplicate: return "duplicate";
    case RuleRefusal::limit_reached: return "limit_reached";
    default:
        break;
    }
    return "none";
}

// ---------------------------------------------------------------------------
// Tokens.

void RuleToken::revoke() noexcept {
    const std::shared_ptr<detail::RuleEntry> entry = entry_.lock();
    if (!entry || (*entry).state != RuleTokenState::armed) return;
    RuleSet& rules = *(*entry).rules;
    rules.disarm(*entry, RuleTokenState::revoked);
}

RuleTokenState RuleToken::state() const noexcept {
    // A token from no rule shares no ownership with anything (empty); an
    // expired one still names its rule's control block, so its rule set has
    // gone with its window (stale).
    const std::weak_ptr<detail::RuleEntry> none{};
    const bool from_rule = entry_.owner_before(none) || none.owner_before(entry_);
    if (!from_rule) return RuleTokenState::empty;
    const std::shared_ptr<detail::RuleEntry> entry = entry_.lock();
    if (!entry) return RuleTokenState::stale;
    return (*entry).state;
}

// ---------------------------------------------------------------------------
// The rule set.

RuleSet::EvaluationScope::EvaluationScope(RuleSet& rules) noexcept : rules_(rules) {
    ++rules_.evaluation_depth_;
}

RuleSet::EvaluationScope::~EvaluationScope() {
    --rules_.evaluation_depth_;
    rules_.compact();
}

RuleSet::RuleSet(SurfaceRoot& window) : window_(&window) {}

RuleSet::~RuleSet() {
    // Taps first: their subscriptions end before the entries (and the
    // sources the taps share) go. Anchors observe entries weakly, so a
    // later disposal of a named component finds nothing.
    for (const std::unique_ptr<detail::RuleTap>& tap : taps_) {
        (*tap).disconnect();
    }
    taps_.clear();
    for (const std::shared_ptr<detail::RuleEntry>& entry : entries_) {
        if ((*entry).anchor) (*(*entry).anchor).retire();
    }
    entries_.clear();
}

std::size_t RuleSet::armed_count() const noexcept { return armed_; }

RuleRefusal RuleSet::validate(const Rule& rule) const {
    const RuleRefusal trigger = validate_trigger(rule.trigger);
    if (trigger != RuleRefusal::none) return trigger;
    if (rule.conditions.size() > maximum_rule_conditions) return RuleRefusal::too_many_conditions;
    for (const RuleCondition& condition : rule.conditions) {
        const RuleRefusal refused = validate_condition(condition);
        if (refused != RuleRefusal::none) return refused;
    }
    if (rule.actions.empty()) return RuleRefusal::no_actions;
    if (rule.actions.size() > maximum_rule_actions) return RuleRefusal::too_many_actions;
    for (const RuleAction& action : rule.actions) {
        const RuleRefusal refused = validate_action(action);
        if (refused != RuleRefusal::none) return refused;
    }
    if (armed_ >= maximum_rules) return RuleRefusal::limit_reached;
    if (duplicates(rule)) return RuleRefusal::duplicate;
    return RuleRefusal::none;
}

bool RuleSet::duplicates(const Rule& rule) const noexcept {
    for (const std::shared_ptr<detail::RuleEntry>& entry : entries_) {
        const detail::RuleEntry& existing = *entry;
        if (existing.state != RuleTokenState::armed) continue;
        if (same_trigger(existing.rule.trigger, rule.trigger) &&
            same_condition_set(existing.rule.conditions, rule.conditions)) {
            return true;
        }
    }
    return false;
}

RuleAddResult RuleSet::add(const Rule& rule) {
    (*window_).verify_access("rule registration");
    RuleAddResult result{};
    const RuleRefusal refused = validate(rule);
    if (refused != RuleRefusal::none) {
        ++statistics_.refused;
        result.refusal = refused;
        return result;
    }
    if (evaluation_depth_ == 0U) compact();
    // Everything that can throw comes before the entry is published: the
    // slot is reserved, the record copied, the anchor registered (a weak
    // registration left behind by a throw observes a dead anchor) and the
    // tap connected.
    entries_.reserve(entries_.size() + 1U);
    std::shared_ptr<detail::RuleEntry> entry = std::make_shared<detail::RuleEntry>();
    (*entry).rules = this;
    (*entry).order = next_order_;
    (*entry).rule = rule;
    (*entry).anchor = std::make_shared<detail::RuleAnchor>(entry);
    const std::shared_ptr<detail::RuleAnchor>& anchor = (*entry).anchor;
    // A trigger's anchor is the component it names (for the control
    // triggers and interaction, its control).
    anchor_to((*entry).rule.trigger.anchor, anchor);
    for (const RuleCondition& condition : (*entry).rule.conditions) {
        anchor_to(condition.control, anchor);
        anchor_to(condition.anchor, anchor);
    }
    for (const RuleAction& action : (*entry).rule.actions) {
        anchor_to(action.anchor, anchor);
        anchor_to(action.control, anchor);
    }
    const bool key = is_key_trigger(rule.trigger.kind);
    if (!key) {
        (*entry).tap = acquire_tap((*entry).rule.trigger);
    }
    entries_.push_back(entry);
    ++next_order_;
    ++armed_;
    if (key) ++key_rules_;
    result.token = RuleToken(entry);
    return result;
}

detail::RuleTap* RuleSet::acquire_tap(const RuleTrigger& trigger) {
    for (const std::unique_ptr<detail::RuleTap>& tap : taps_) {
        if ((*tap).users == 0U || !(*tap).serves(trigger)) continue;
        ++(*tap).users;
        return tap.get();
    }
    taps_.reserve(taps_.size() + 1U);
    std::unique_ptr<detail::RuleTap> made = std::make_unique<detail::RuleTap>(*this, trigger);
    (*made).connect();
    (*made).users = 1U;
    detail::RuleTap* const tap = made.get();
    taps_.push_back(std::move(made));
    return tap;
}

void RuleSet::release_tap(detail::RuleTap& tap) noexcept {
    if (tap.users == 0U) return;
    --tap.users;
    if (tap.users == 0U) tap.disconnect();
}

void RuleSet::disarm(detail::RuleEntry& entry, RuleTokenState reason) noexcept {
    if (entry.state != RuleTokenState::armed) return;
    entry.state = reason;
    if (entry.anchor) (*entry.anchor).retire();
    --armed_;
    if (entry.tap != nullptr) {
        release_tap(*entry.tap);
    } else if (is_key_trigger(entry.rule.trigger.kind)) {
        --key_rules_;
    }
    if (evaluation_depth_ == 0U) compact();
}

void RuleSet::compact() noexcept {
    if (evaluation_depth_ != 0U) return;
    std::size_t kept = 0U;
    for (std::size_t index = 0U; index < entries_.size(); ++index) {
        if ((*entries_[index]).state != RuleTokenState::armed) continue;
        if (kept != index) entries_[kept] = std::move(entries_[index]);
        ++kept;
    }
    entries_.resize(kept);
    std::size_t kept_taps = 0U;
    for (std::size_t index = 0U; index < taps_.size(); ++index) {
        if ((*taps_[index]).users == 0U) continue;
        if (kept_taps != index) taps_[kept_taps] = std::move(taps_[index]);
        ++kept_taps;
    }
    taps_.resize(kept_taps);
}

bool RuleSet::offer_key(KeyEvent& event) {
    if (key_rules_ == 0U) return false;
    RuleFiring firing{};
    if (event.action == KeyAction::up) {
        firing.trigger = RuleTriggerKind::key_released;
    } else if (event.repeat) {
        firing.trigger = RuleTriggerKind::key_repeat;
    } else {
        firing.trigger = RuleTriggerKind::key_pressed;
    }
    firing.key = &event;
    const bool consumed = offer(nullptr, firing);
    if (consumed) event.handled = true;
    return consumed;
}

bool RuleSet::matches(const detail::RuleEntry& entry, const detail::RuleTap* tap,
                      const RuleFiring& firing) const noexcept {
    if (entry.state != RuleTokenState::armed) return false;
    if (tap != nullptr) return entry.tap == tap;
    const bool key = entry.rule.trigger.kind == firing.trigger && firing.key != nullptr &&
                     entry.rule.trigger.physical_key == (*firing.key).physical_key;
    return key;
}

const InteractionSnapshot& RuleSet::snapshot(OccurrenceState& state) {
    if (!state.snapshot) state.snapshot = (*window_).interaction_snapshot();
    return *state.snapshot;
}

bool RuleSet::holds(const RuleCondition& condition, OccurrenceState& state) {
    bool level = false;
    const Control::Ptr control = condition.control.lock();
    switch (condition.kind) {
    case RuleConditionKind::key_down: {
        const InteractionSnapshot& input = snapshot(state);
        for (const HeldKey& held : input.held_keys) {
            if (held.physical_key == condition.physical_key) level = true;
        }
        break;
    }
    case RuleConditionKind::modifiers: {
        const InteractionSnapshot& input = snapshot(state);
        level = input.modifiers == condition.modifiers;
        break;
    }
    case RuleConditionKind::hovering: {
        const Control::Ptr hovered = (*window_).hovered_control();
        level = control && hovered && (hovered == control || (*control).contains(*hovered));
        break;
    }
    case RuleConditionKind::pressed: {
        const Control::Ptr pressed = (*window_).pressed_control();
        level = control && pressed == control;
        break;
    }
    case RuleConditionKind::focused: {
        const Control::Ptr focused = (*window_).focused_control();
        level = control && focused == control;
        break;
    }
    case RuleConditionKind::focus_within: {
        const Control::Ptr focused = (*window_).focused_control();
        level = control && focused && (focused == control || (*control).contains(*focused));
        break;
    }
    case RuleConditionKind::enabled:
        level = control && (*control).effectively_enabled();
        break;
    case RuleConditionKind::visible:
        level = control && (*control).effectively_visible();
        break;
    case RuleConditionKind::window_active:
        level = (*window_).active();
        break;
    case RuleConditionKind::is_true:
        level = condition.flag != nullptr && (*condition.flag).get();
        break;
    case RuleConditionKind::equals:
        level = condition.number != nullptr && (*condition.number).get() == condition.expected;
        break;
    case RuleConditionKind::none:
    default:
        // validate() admits no other kind; an unknown one never holds.
        return false;
    }
    const bool holds_now = level != condition.negated;
    return holds_now;
}

void RuleSet::run(const RuleAction& action, const RuleFiring& firing) {
    switch (action.kind) {
    case RuleActionKind::program:
        action.call(action.context, action.function, firing);
        return;
    case RuleActionKind::invoke:
        static_cast<void>((*action.command).execute_routed(*window_, "rule"));
        return;
    case RuleActionKind::set:
    case RuleActionKind::toggle:
        action.write(action.value, action.argument);
        return;
    case RuleActionKind::focus: {
        const Control::Ptr control = action.control.lock();
        if (control) static_cast<void>((*window_).request_focus(control));
        return;
    }
    case RuleActionKind::none:
    default:
        // validate() admits no other kind.
        return;
    }
}

bool RuleSet::offer(const detail::RuleTap* tap, RuleFiring& firing) {
    if (armed_ == 0U) return false;
    const EvaluationScope evaluation(*this);
    firing.window = window_;
    OccurrenceState state{};
    bool offered = false;
    bool consumed = false;
    // The boundary: a rule added by an action waits for the next occurrence.
    const std::size_t boundary = entries_.size();
    for (std::size_t index = 0U; index < boundary; ++index) {
        // Held for the firing: an action may end this rule or add others.
        const std::shared_ptr<detail::RuleEntry> entry = entries_[index];
        if (!matches(*entry, tap, firing)) continue;
        offered = true;
        ++statistics_.evaluated;
        bool all_hold = true;
        for (const RuleCondition& condition : (*entry).rule.conditions) {
            if (!holds(condition, state)) {
                all_hold = false;
                break;
            }
        }
        if (!all_hold) continue;
        firing.state = &snapshot(state);
        ++statistics_.fired;
        for (const RuleAction& action : (*entry).rule.actions) {
            // An action may have disarmed the rule (its context disposed).
            if ((*entry).state != RuleTokenState::armed) break;
            run(action, firing);
        }
        if ((*entry).rule.consume) {
            consumed = true;
            break;
        }
    }
    if (offered) ++statistics_.occurrences;
    if (consumed) ++statistics_.consumed;
    return consumed;
}

RuleAddResult on(RuleSet& rules, const RuleTrigger& trigger, const RuleAction& built) {
    Rule rule{};
    rule.trigger = trigger;
    rule.actions.push_back(built);
    RuleAddResult added = rules.add(rule);
    return added;
}

} // namespace gui_forms
