#pragma once
// A window's rules (notes:planning APP_WINDOW_RULES_DESIGN Part C, owner
// rulings Q-C1 to Q-C5): the RuleSet a SurfaceRoot owns and makes on first
// use (SurfaceRoot::rules). See rule.hpp for the record and its vocabulary.
//
// Evaluation. For one occurrence of a trigger the rules on it are evaluated
// in the order they were added; each whose conditions all hold fires, its
// actions running in order. A firing rule with `consume` stops the rules
// added after it and the occurrence's default route. A rule added while an
// occurrence is being evaluated is first evaluated by the next one; a rule
// revoked or disarmed meanwhile is not evaluated again, even later in that
// occurrence. An action that throws stops the later actions of its firing
// and the occurrence's later rules; the exception reaches the code that
// raised the trigger (the window's input dispatch, the control's event, the
// command), as an event handler's does.
//
// Where rules sit in the input order (Q-C5). Key rules see a key before the
// control route (WinForms' ProcessCmdKey position): before the preemptive
// accelerators, the focused control's preview, the control and the key
// commands; a consuming key rule ends the key there. Rules on MouseDown,
// MouseUp, Click, MouseClick, DoubleClick and MouseDoubleClick see the event
// before the control's subscribers (SurfaceRoot::raise_mouse_events offers
// it through offer_mouse); a consuming one keeps the event from them, and a
// consumed Click also from on_activate (a button's clicked). The other
// control-event rules run when the control raises the event, as a
// subscriber connected when the first rule on that control's event was
// added; a consuming context_menu_requested rule also marks the request
// handled, so it does not reach the control's ancestors.
//
// Lifetime. The window owns its rules. A rule naming a component (rule.hpp)
// is disarmed when that component is disposed or destroyed, and its token
// reads stale; a window's rules end with it. The rule set and its tokens are
// used on the window's thread only.

#include "gui_forms/rules/rule/rule.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace gui_forms {

class SurfaceRoot;

namespace detail {
struct RuleEntry;
class RuleAnchor;
class RuleTap;
} // namespace detail

// Why add() refused a rule. A refusal changes nothing.
enum class RuleRefusal : std::uint8_t {
    none,
    // The trigger kind is none or outside the vocabulary, or it lacks what
    // its kind names (a key, a control, a command, a source).
    malformed_trigger,
    // A condition's kind is none or outside the vocabulary, or it lacks what
    // its kind reads.
    malformed_condition,
    // An action's kind is none or outside the vocabulary, or it lacks what
    // its kind runs (a function, a command, a Value, a control).
    malformed_action,
    no_actions,
    too_many_conditions,
    too_many_actions,
    // A component the rule names is disposed (or a control has gone).
    disposed_reference,
    // A rule with the same trigger and the same set of conditions is armed.
    duplicate,
    // The window holds maximum_rules armed rules.
    limit_reached,
};

[[nodiscard]] std::string_view rule_refusal_name(RuleRefusal refusal) noexcept;

// Where a rule is: armed (it fires), revoked (through its token), stale (a
// component it names was disposed or destroyed, or its window has gone), or
// empty (a token from no rule, or a refused add).
enum class RuleTokenState : std::uint8_t {
    empty,
    armed,
    revoked,
    stale,
};

// A handle to one rule. It observes the rule and does not own it: dropping
// the token leaves the rule armed. revoke() ends the rule early.
class RuleToken final {
public:
    RuleToken() = default;

    // Ends the rule if it is armed; afterwards the token reads revoked. A
    // stale, revoked or empty token is left as it is.
    void revoke() noexcept;
    [[nodiscard]] RuleTokenState state() const noexcept;

private:
    friend class RuleSet;
    explicit RuleToken(std::weak_ptr<detail::RuleEntry> entry) noexcept : entry_(std::move(entry)) {}
    std::weak_ptr<detail::RuleEntry> entry_{};
    // Set when this token's revoke() ended the rule: the rule set then frees
    // the entry, so the expired observer alone would read stale.
    bool revoked_{};
};

// add()'s explicit result: the token of the armed rule, or the refusal (and
// an empty token).
struct RuleAddResult final {
    RuleToken token{};
    RuleRefusal refusal{RuleRefusal::none};
};

// What a window's rules have done, for tests and diagnostics.
//   occurrences: trigger occurrences offered to at least one armed rule.
//   evaluated: rules whose trigger matched an occurrence.
//   fired: rules whose conditions held and whose actions ran.
//   consumed: occurrences a consuming rule ended.
//   refused: add() calls refused.
struct RuleSetStatistics final {
    std::uint64_t occurrences{};
    std::uint64_t evaluated{};
    std::uint64_t fired{};
    std::uint64_t consumed{};
    std::uint64_t refused{};
};

// A window's rules: armed by add(), evaluated as their triggers occur, in
// the order added. Evaluation, input order and lifetime are described at
// the top of rule_set.hpp.
class RuleSet final {
public:
    explicit RuleSet(SurfaceRoot& window);
    ~RuleSet();
    RuleSet(const RuleSet&) = delete;
    RuleSet& operator=(const RuleSet&) = delete;

    // Arms `rule`, copying it, after validating it whole (RuleRefusal). The
    // window's thread (std::logic_error elsewhere). May allocate and throw;
    // a throw leaves the rule set as it was.
    [[nodiscard]] RuleAddResult add(const Rule& rule);
    // The armed rules.
    [[nodiscard]] std::size_t armed_count() const noexcept;
    [[nodiscard]] RuleSetStatistics statistics() const noexcept { return statistics_; }

    // The window's key route, before the control route: offers `event` to
    // the key rules and marks it handled when one consumed it. Returns
    // whether one did.
    bool offer_key(KeyEvent& event);
    // The window's mouse route, before the control's subscribers: offers
    // `kind` (mouse_down, mouse_up, click, mouse_click, double_click or
    // mouse_double_click) on `control` to the rules for it. `arguments` is
    // null for click and double_click. Returns whether a rule consumed it,
    // in which case the caller raises the event to no subscriber.
    bool offer_mouse(RuleTriggerKind kind, Control& control, const MouseEventArgs* arguments);

private:
    friend class detail::RuleAnchor;
    friend class detail::RuleTap;
    friend class RuleToken;

    // Marks an evaluation under way, so that removals wait for its end.
    class EvaluationScope final {
    public:
        explicit EvaluationScope(RuleSet& rules) noexcept;
        ~EvaluationScope();
        EvaluationScope(const EvaluationScope&) = delete;
        EvaluationScope& operator=(const EvaluationScope&) = delete;

    private:
        RuleSet& rules_;
    };

    // The input state, copied once for the occurrence the first time a
    // condition or a firing needs it.
    struct OccurrenceState final {
        std::optional<InteractionSnapshot> snapshot{};
    };

    [[nodiscard]] RuleRefusal validate(const Rule& rule) const;
    [[nodiscard]] bool duplicates(const Rule& rule) const noexcept;
    // The tap (shared subscription) for `trigger`, made and connected when
    // no armed rule uses it yet.
    [[nodiscard]] detail::RuleTap* acquire_tap(const RuleTrigger& trigger);
    void release_tap(detail::RuleTap& tap) noexcept;
    // Offers one occurrence to the rules on `tap` (null: the key rules of
    // `firing.key`), in order. Returns whether a rule consumed it.
    bool offer(const detail::RuleTap* tap, RuleFiring& firing);
    [[nodiscard]] bool matches(const detail::RuleEntry& entry, const detail::RuleTap* tap,
                               const RuleFiring& firing) const noexcept;
    [[nodiscard]] bool holds(const RuleCondition& condition, OccurrenceState& state);
    [[nodiscard]] const InteractionSnapshot& snapshot(OccurrenceState& state);
    void run(const RuleAction& action, const RuleFiring& firing);
    // Ends an armed rule (revoked or stale), releasing its tap.
    void disarm(detail::RuleEntry& entry, RuleTokenState reason) noexcept;
    // Drops ended rules and unused taps when no evaluation is under way.
    void compact() noexcept;

    SurfaceRoot* window_;
    // In the order the rules were added; ended entries stay until compact().
    std::vector<std::shared_ptr<detail::RuleEntry>> entries_{};
    std::vector<std::unique_ptr<detail::RuleTap>> taps_{};
    std::size_t armed_{};
    std::size_t key_rules_{};
    std::size_t evaluation_depth_{};
    std::uint64_t next_order_{1U};
    RuleSetStatistics statistics_{};
};

// The one-condition spellings (Q-C3): one trigger, no conditions and one
// action, armed in one call. gui_forms::on(event, owner, &Owner::member)
// stays the spelling for events outside the rule vocabulary.
// Owner rulings FD-6 Q2 and Q3 (a), 2026-11-08: the triggers on() and rules
// take are one namespace, `trigger`, holding the control's WinForms mouse
// events and its semantic events (trigger::interaction with the control's
// own typed event: value_changed, checked_changed, selection_changed and the
// rest). One action per interaction: a second on() for the same trigger on
// the same control is refused (RuleRefusal::duplicate, nothing changes);
// revoking the first token frees the interaction for a replacement.
template <typename Context>
RuleAddResult on(RuleSet& rules, const RuleTrigger& trigger, Context& context,
                 void (*function)(Context&, const RuleFiring&)) {
    Rule rule{};
    rule.trigger = trigger;
    rule.actions.push_back(action(context, function));
    RuleAddResult added = rules.add(rule);
    return added;
}

RuleAddResult on(RuleSet& rules, const RuleTrigger& trigger, const RuleAction& built);

} // namespace gui_forms
