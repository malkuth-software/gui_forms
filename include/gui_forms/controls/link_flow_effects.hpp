#pragma once

// The node canvas's link flow (NODE_GRAPH_SPEC gap 3; cp31): marks that
// travel along the links that carry data, an effect program on the
// canvas's link layer. The canvas draws each link statically
// (paint_canvas_link); the flow draws its marks over it in the mark colour
// (link_flow_mark_colour, the canvas's own colour), so the marks read as
// beads (`dots`) or moving breaks (`dashes`) in the line, in light, dark
// and forced high contrast alike.
//
// Motion: the marks advance on the motion clock only while the canvas
// enlists the effect, which it does only while link_flow_wants_frames is
// true; a still frame (reduced motion, an occluded window, the motion
// switch) draws them at phase 0, the static form, and advances nothing.
// With no flowing link there are no frames.

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/engine/effect/effect.hpp"
#include "gui_forms/engine/effect_canvas/effect_canvas.hpp"
#include "gui_forms/engine/theme_rules/theme_rules.hpp"
#include "gui_forms/theme/canvas_look/canvas_look.hpp"

#include <cstddef>
#include <string_view>

namespace gui_forms {

class EffectRegistry;
// The node canvas control (src/controls/node_canvas/), the flow's parent.
class NodeCanvas;

// The most links one frame of flow draws: the canvas fills the view with
// the flowing links it shows, nearest first, and leaves the rest still.
inline constexpr std::size_t maximum_flow_links = 64U;

// What the flow may read: a snapshot the canvas takes when the frame runs.
// Points are in the link layer's local logical units; each link is a
// polyline of up to maximum_effect_batch points (the canvas flattens a
// curve to it). `speed` is in view units a second (the look's flow speed,
// in world units a second, times the zoom), `spacing` the mark pitch and
// `width` the link's stroke width, both in view units.
template <>
struct EffectView<NodeCanvas> final {
    static constexpr std::string_view parent_name{"node_canvas"};

    EffectArray<EffectPoints, maximum_flow_links> links{};
    CanvasFlowStyle style{CanvasFlowStyle::none};
    double speed{48.0};
    double spacing{16.0};
    double width{2.0};
};

// The flow: one dashed stroke per link, its dash pattern moved along the
// link by the phase. `dots` are round beads half the link's width every
// `spacing`; `dashes` are breaks half a pitch long, a little wider than
// the link. The colour is the frame's primary colour.
class GUI_FORMS_API LinkFlow final {
public:
    using parent_type = NodeCanvas;
    static constexpr EffectDeclaration declaration{
        .name = "link_flow",
        .engine_version = 2U,
        .time_model = EffectTimeModel::ambient,
        .cost = {.fixed = 8000U, .per_device_pixel = 4U},
        .state_budget = 16U,
    };
    void run(const EffectView<NodeCanvas>& parent, const EffectFrame& frame, EffectCanvas& canvas);

private:
    // The marks' position along every link, as a fraction of one pitch in
    // [0, 1), so a change of pitch keeps the marks where they were.
    double phase_{};
};

// Whether the canvas should enlist the flow on the motion clock: some link
// flows, the style draws marks, the speed is above 0 and motion is not
// reduced. Anything else: no frames (a still frame draws the static form).
[[nodiscard]] GUI_FORMS_API bool link_flow_wants_frames(const EffectView<NodeCanvas>& view,
                                                        bool motion_reduced) noexcept;

// The colour the canvas gives the flow as its primary effect colour: the
// canvas's own colour from `look`, opaque.
[[nodiscard]] GUI_FORMS_API Color link_flow_mark_colour(const CanvasLook& look) noexcept;

// Adds the node canvas's effects to `registry`.
GUI_FORMS_API void register_link_flow_effects(EffectRegistry& registry);

} // namespace gui_forms
