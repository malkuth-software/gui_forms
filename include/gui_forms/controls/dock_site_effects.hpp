#pragma once

// The dock site's drop-cue pulse (roadmap row 2.6, from the Dear ImGui
// study's section A.3): an effect program on the drop part while a dragged
// panel hovers a target that accepts it. It is ambient: the site's drag
// layer enlists it on the motion clock only while a drop target is shown,
// and a still frame (reduced motion, an occluded window, the motion switch)
// draws its still pose, a steady edge, and advances nothing. It takes no
// input and draws inside its part (outsets 0). Its source is shared with
// the scrub glow (src/controls/interaction_cues/).

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/engine/effect/effect.hpp"
#include "gui_forms/engine/effect_canvas/effect_canvas.hpp"

#include <string_view>

namespace gui_forms {

class EffectRegistry;
// The docking site (src/controls/docking/dock_site/), the pulse's parent.
class DockSite;

// What the pulse may read. The part is the drop target's highlight, in the
// site's logical units; `edge` is the pulsing edge's width.
template <>
struct EffectView<DockSite> final {
    static constexpr std::string_view parent_name{"dock_site"};

    double edge{2.0};
};

// The drop-cue pulse: an edge inside the drop part and a faint tint over
// it, their opacity breathing between a low and a high level once every
// `drop_pulse_period_seconds`. The edge is the frame's primary colour and
// the tint its secondary. Still: the edge at its middle level, no tint.
class GUI_FORMS_API DropPulse final {
public:
    using parent_type = DockSite;
    static constexpr EffectDeclaration declaration{
        .name = "drop_pulse",
        .engine_version = 2U,
        .time_model = EffectTimeModel::ambient,
        .cost = {.fixed = 2000U, .per_device_pixel = 2U},
        .state_budget = 16U,
    };
    void run(const EffectView<DockSite>& parent, const EffectFrame& frame, EffectCanvas& canvas);

private:
    // Where the pulse is in its period, a fraction in [0, 1).
    double phase_{};
};

// One breath of the pulse.
inline constexpr double drop_pulse_period_seconds = 1.2;

// Adds the drop pulse to `registry`.
GUI_FORMS_API void register_dock_site_effects(EffectRegistry& registry);

} // namespace gui_forms
