// SurfaceRoot::rules (surface_root.hpp), defined with the rules: the rule
// set needs Command and the controls' events, which live in
// gui_forms_controls, so the core holds the set only through a pointer this
// library makes and enters it only through offer_key_rules_ and
// offer_mouse_rules_.

#include "gui_forms/rules/rule_set/rule_set.hpp"
#include "gui_forms/surface_root/surface_root.hpp"

#include <memory>

namespace gui_forms {
namespace {

// The key route's entry (SurfaceRoot::offer_key_rules_).
bool offer_window_key_rules(RuleSet& rules, KeyEvent& event) {
    const bool consumed = rules.offer_key(event);
    return consumed;
}

// raise_mouse_events' entry (SurfaceRoot::offer_mouse_rules_): the event's
// trigger kind, then the rules on that control's tap for it.
bool offer_window_mouse_rules(RuleSet& rules, detail::MouseRuleEvent event, Control& control,
                              const MouseEventArgs* arguments) {
    RuleTriggerKind kind = RuleTriggerKind::none;
    switch (event) {
    case detail::MouseRuleEvent::mouse_down:
        kind = RuleTriggerKind::mouse_down;
        break;
    case detail::MouseRuleEvent::mouse_up:
        kind = RuleTriggerKind::mouse_up;
        break;
    case detail::MouseRuleEvent::click:
        kind = RuleTriggerKind::click;
        break;
    case detail::MouseRuleEvent::mouse_click:
        kind = RuleTriggerKind::mouse_click;
        break;
    case detail::MouseRuleEvent::double_click:
        kind = RuleTriggerKind::double_click;
        break;
    case detail::MouseRuleEvent::mouse_double_click:
        kind = RuleTriggerKind::mouse_double_click;
        break;
    default:
        // An event this library does not know offers nothing.
        return false;
    }
    const bool consumed = rules.offer_mouse(kind, control, arguments);
    return consumed;
}

} // namespace

RuleSet& SurfaceRoot::rules() {
    require_root_thread("window rules");
    if (!rules_) {
        rules_ = std::make_shared<RuleSet>(*this);
        offer_key_rules_ = &offer_window_key_rules;
        offer_mouse_rules_ = &offer_window_mouse_rules;
    }
    return *rules_;
}

} // namespace gui_forms
