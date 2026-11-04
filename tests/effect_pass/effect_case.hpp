#pragma once

// What every case of the effect pass's tests shares (CMakeLists.txt,
// cmake/check_effect_pass.cmake): a parent with a one-value view, so a case
// declares only its effect. A case registers its effect through
// make_effect_entry, which is what the pass looks for.

#include "gui_forms/engine/effect/effect.hpp"
#include "gui_forms/engine/effect_canvas/effect_canvas.hpp"
#include "gui_forms/engine/effect_registry/effect_registry.hpp"

#include <string_view>

namespace effect_case {

// The parent the cases' effects draw over.
struct CaseParent final {};

} // namespace effect_case

namespace gui_forms {

template <>
struct EffectView<effect_case::CaseParent> final {
    static constexpr std::string_view parent_name{"effect_case"};
    double fraction{};
};

} // namespace gui_forms

namespace effect_case {

using View = gui_forms::EffectView<CaseParent>;

} // namespace effect_case
