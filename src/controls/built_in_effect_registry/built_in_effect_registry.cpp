// The registry of the toolkit's own effects (effect_registry.hpp): every
// stock control package that hosts effect programs adds its own here.

#include "gui_forms/engine/effect_registry/effect_registry.hpp"

#include "gui_forms/controls/sweep_scope_effects.hpp"
#include "gui_forms/controls/progress_bar_effects.hpp"

namespace gui_forms {
namespace {

EffectRegistry make_built_in_effect_registry() {
    EffectRegistry registry;
    register_progress_bar_effects(registry);
    register_sweep_scope_effects(registry);
    return registry;
}

} // namespace

const EffectRegistry& built_in_effect_registry() {
    static const EffectRegistry registry = make_built_in_effect_registry();
    return registry;
}

void install_built_in_effect_registry() {
    const EffectRegistry& registry = built_in_effect_registry();
    install_effect_registry(&registry);
}

} // namespace gui_forms
