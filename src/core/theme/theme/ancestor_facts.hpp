#pragma once

#include "gui_forms/control/control/control.hpp"
#include "gui_forms/engine/theme_rules/theme_rules.hpp"

#include <vector>

namespace gui_forms::detail {

// Fills `chain` (cleared first) with `control`'s ancestors, its parent
// first, at most maximum_ancestor_depth of them, and `bloom` with their
// keys, for rules with ancestor compounds (selector step 4). Each entry
// borrows its control's type, stable ID and classes: the chain is valid
// while the tree is unchanged, for one resolution.
void fill_ancestor_facts(const Control& control, std::vector<AncestorFacts>& chain, AncestorBloom& bloom);

// Sets `facts`' ancestors from `chain`, filled for `control`, when `rules`
// (a theme's, user theme's or inline style's) read them; otherwise leaves
// both empty, so that a theme without ancestor compounds pays nothing.
void attach_ancestor_facts(const Control& control, const CompiledThemeRules& rules,
                           std::vector<AncestorFacts>& chain, CompiledThemeRules::PartFacts& facts);

} // namespace gui_forms::detail
