#pragma once

// A control's kept cascade results (audit N90 and N1): the recipes
// Theme::resolve gave it and the plans ThemeEngine::plan made for its parts,
// so that a part painted, hit-tested and laid out again in the same facts
// resolves once.
//
// What a result depends on, and how a kept one is known to be current:
// - the control's own facts (type, stable id, classes, inline style), its
//   ancestors' (type, id, classes, and the text colour and font they pass
//   down), the theme and user rules and the window's presentation. Every
//   change to these raises Dirty::style on some control, or moves a control
//   in the tree; both advance the process's style epoch (style_epoch), and a
//   table filled under another epoch is cleared before it is read;
// - the window, its media facts, its text scale and the control's effective
//   theme, which each entry also holds, so that a control moved between
//   windows, or a window whose scale changes, never reads another's result;
// - an ancestor's live anatomy states, which a part reads only where its
//   text colour or font inherits: such an entry holds the states of every
//   ancestor, and is current only while they are the same;
// - the call's own arguments (role, visual context, states, part, control
//   type; for a plan the part's role, family and size), held per entry.
//
// Ownership: the control owns its table (Control::kept_resolutions_), which
// ends with it. The table is read and written on the control's root thread
// only, from const resolution calls; the epoch is atomic because windows on
// different root threads share it.

#include "gui_forms/control/control/control.hpp"
#include "gui_forms/engine/theme_engine/theme_engine.hpp"
#include "gui_forms/theme/types/theme_types.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {
class SurfaceRoot;
class Theme;
} // namespace gui_forms

namespace gui_forms::detail {

// The process's style epoch: advanced on every Dirty::style mark and every
// change of a control's parent. Never zero, so a new table (epoch 0) is
// always stale.
[[nodiscard]] std::uint64_t style_epoch() noexcept;
void advance_style_epoch() noexcept;

// The facts both kinds of entry share: where the control is shown.
struct KeptPlacement final {
    const Theme* theme{};
    const SurfaceRoot* window{};
    MediaFacts media{};
    double text_scale{};
    friend bool operator==(const KeptPlacement& left, const KeptPlacement& right) noexcept {
        return left.theme == right.theme && left.window == right.window && left.media == right.media &&
               left.text_scale == right.text_scale;
    }
};

// The anatomy states of `control`'s ancestors, parent first, into `states`
// (cleared first).
void fill_ancestor_states(const Control& control, std::vector<AnatomyStateSet>& states);
// Whether `control`'s ancestors' states are `states`, parent first.
[[nodiscard]] bool ancestor_states_match(const Control& control, const std::vector<AnatomyStateSet>& states);

struct KeptRecipe final {
    // The theme whose resolve made the entry.
    const Theme* resolver{};
    KeptPlacement placement{};
    ControlVisualRole role{};
    ControlVisualContext context{};
    AnatomyStateSet states{};
    std::string part;
    std::string control_type;
    // Whether the cascade read the ancestors' states (an inherited text
    // colour or font), which ancestor_states then holds.
    bool reads_ancestor_states{};
    std::vector<AnatomyStateSet> ancestor_states;
    ResolvedControlRecipe recipe;
};

struct KeptPlan final {
    // The engine whose plan made the entry.
    const ThemeEngine* resolver{};
    KeptPlacement placement{};
    std::string control_type;
    std::string part;
    PartRole role{};
    AnatomyStateSet states{};
    ControlVisualRole family{};
    ControlVisualContext context{};
    // The part's size: a plan reads its bounds only for the raster extent
    // and the animation budget, and paintable bounds were checked first.
    double width{};
    double height{};
    bool reads_ancestor_states{};
    std::vector<AnatomyStateSet> ancestor_states;
    ThemeEngine::PartPlan plan{};
};

// At most this many entries of each kind; a full table is cleared, not
// evicted, before the next insertion.
inline constexpr std::size_t maximum_kept_recipes = 32U;
inline constexpr std::size_t maximum_kept_plans = 32U;

class KeptResolutions final {
public:
    // The epoch the entries were made under; 0 for none yet.
    std::uint64_t epoch{};
    std::vector<KeptRecipe> recipes;
    std::vector<KeptPlan> plans;
};

// `control`'s table, made on first use, cleared when the style epoch has
// moved since it was filled.
[[nodiscard]] KeptResolutions& kept_resolutions_of(const Control& control);

// Where `control` is shown, for an entry's placement.
[[nodiscard]] KeptPlacement kept_placement_of(const Control& control) noexcept;

} // namespace gui_forms::detail
