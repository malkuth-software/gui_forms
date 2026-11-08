#pragma once

#include "gui_forms/engine/theme_rules/theme_rules.hpp"
#include "gui_forms/theme/types/theme_types.hpp"

namespace gui_forms::detail {

// Fills `recipes` (reset first; caller storage, a family is some 9 KB)
// with family `role`'s ordinary and selected recipes for every surface
// state, evaluated from a floor sheet's compiled `rules` (T4 floor sheet,
// owner option 1): each recipe is `base` (the structure: its material the
// empty floor the sheet's material fields are assembled onto) with the
// material fields and recipe fields of the rules matching the family in
// that state, as Theme::resolve applies them. The facts name no control
// type, ID, class, part or ancestor, so only rules of `:family()`, state
// and `*` compounds match. Assembled for a unit box: a floor sheet writes
// box-independent geometry (gradients without corners, absolute corners),
// plain colours and lengths (no currentcolor, inherit or em). The high
// contrast and dark records are left at their defaults, as Theme::create
// fills them.
void fill_family_recipes(const CompiledThemeRules& rules, ControlVisualRole role, const ControlVisualRecipe& base,
                         ControlRoleRecipes& recipes);

} // namespace gui_forms::detail
