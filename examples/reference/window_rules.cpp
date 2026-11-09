// A window's rules (APP_WINDOW_RULES_DESIGN Part C): behaviour attached
// from program code. A form names no program code, so what a form's
// handler reference once did is a rule here: one trigger, conditions that
// must all hold, and ordered actions, a named function or a built-in.
#include "window_rules_form.hpp"   // generated from forms/window_rules.form

#include <gui_forms/application.hpp>
#include <gui_forms/rules.hpp>
#include <gui_forms/value.hpp>

namespace gf = gui_forms;

// The context of the program action. It is no Component, so it must
// outlive the window's rules: it is declared before the window.
struct Settings final {
    int applied{};
};

void apply_settings(Settings& settings, const gf::RuleFiring& firing) {
    static_cast<void>(firing);
    ++settings.applied;
}

int main() {
    // The program's application first: the window joins it.
    gf::ApplicationCreateResult created = gf::Application::create(gf::ApplicationConfig{});
    if (!created) return 1;
    gf::Application& application = *created.application;
    Settings settings{};
    gf::Value<bool> unlocked{true};
    gf::Value<bool> dirty{true};
    reference::RulesWindow window(application);   // binds nothing, so it takes no model
    if (application.window_count() != 1U) return 1;
    gf::Button& apply = *window.example_apply;

    // Apply runs the program action and clears `dirty`, while `unlocked`
    // holds.
    gf::Rule rule{};
    rule.trigger = gf::trigger::interaction(apply, apply.clicked());
    rule.conditions = {gf::condition::is_true(unlocked)};
    rule.actions = {gf::action(settings, &apply_settings), gf::builtin::set(dirty, false)};
    const gf::RuleAddResult added = window.rules().add(rule);
    if (added.refusal != gf::RuleRefusal::none) return 1;

    apply.perform_click();
    if (settings.applied != 1 || dirty.get()) return 1;
    unlocked.set(false);
    apply.perform_click();
    if (settings.applied != 1) return 1;

    // The token revokes early; a revoked rule fires no more.
    unlocked.set(true);
    gf::RuleToken token = added.token;
    token.revoke();
    apply.perform_click();
    if (settings.applied != 1 || token.state() != gf::RuleTokenState::revoked) return 1;
    return 0;
}
