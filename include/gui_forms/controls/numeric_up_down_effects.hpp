#pragma once

// The numeric field's scrub glow (roadmap row 2.6, from the Dear ImGui
// study's section A.3): an effect program on the field while its value is
// scrubbed. It is a transition: the field drives EffectFrame::transition
// (EffectInstance::set_transition) from 0 to 1 as a scrub starts and back
// as it ends, on its own timeline, and jumps to the end under reduced
// motion. At transition 0 it draws nothing. It takes no input and draws
// inside its part (outsets 0). Its source is shared with the drop pulse
// (src/controls/interaction_cues/).

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/engine/effect/effect.hpp"
#include "gui_forms/engine/effect_canvas/effect_canvas.hpp"

#include <string_view>

namespace gui_forms {

class EffectRegistry;
// The numeric field (src/controls/panel/numeric_up_down/), the glow's parent.
class NumericUpDown;

// What the glow may read. The part is the field, in the numeric field's
// logical units; `edge` is the glowing edge's width.
template <>
struct EffectView<NumericUpDown> final {
    static constexpr std::string_view parent_name{"numeric_up_down"};

    double edge{2.0};
};

// The scrub glow: an edge inside the field in the frame's primary colour
// and a faint tint in its secondary, both at an opacity that follows the
// transition (smoothed). Nothing at transition 0. It keeps no state.
class GUI_FORMS_API ScrubGlow final {
public:
    using parent_type = NumericUpDown;
    static constexpr EffectDeclaration declaration{
        .name = "scrub_glow",
        .engine_version = 2U,
        .time_model = EffectTimeModel::transition,
        .cost = {.fixed = 1200U, .per_device_pixel = 2U},
        .state_budget = 8U,
    };
    void run(const EffectView<NumericUpDown>& parent, const EffectFrame& frame, EffectCanvas& canvas);
};

// Adds the scrub glow to `registry`.
GUI_FORMS_API void register_numeric_up_down_effects(EffectRegistry& registry);

} // namespace gui_forms
