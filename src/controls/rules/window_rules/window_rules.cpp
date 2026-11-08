// SurfaceRoot::rules (surface_root.hpp), defined with the rules: the rule
// set needs Command and the controls' events, which live in
// gui_forms_controls, so the core holds the set only through a pointer this
// library makes and enters it only through offer_key_rules_.

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

} // namespace

RuleSet& SurfaceRoot::rules() {
    require_root_thread("window rules");
    if (!rules_) {
        rules_ = std::make_shared<RuleSet>(*this);
        offer_key_rules_ = &offer_window_key_rules;
    }
    return *rules_;
}

} // namespace gui_forms
