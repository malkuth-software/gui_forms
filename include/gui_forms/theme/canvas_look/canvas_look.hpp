#pragma once

// The node canvas's look, theme side (NODE_GRAPH_SPEC section 3 and gaps
// N2, 2 and 3; NODE_ENGINE_AND_VALUE_BAR_STUDY 2.10; cp31): the canvas's
// anatomy, the theme's resolution of every canvas part in every state into
// one flat record, and the painting of each part from that record, so that
// a graph canvas's colours, lines, grid, texture and flow live in themes,
// not in code.
//
// A canvas resolves its look once (resolve_canvas_look) and again only when
// its theme, colour scheme, contrast or classes change, never per frame; it
// then paints every frame from the record with the paint_canvas_ functions,
// which allocate nothing and call no theme.
//
// Coordinates: the paint functions draw in the painter's current
// coordinates (view units). The background reads a CanvasView, which
// places world (0, 0) and scales world units to view units, so the grid and
// the texture stay anchored to the canvas as it pans.
//
// Execution: the UI thread that paints the canvas.

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/anatomy/types/anatomy_types.hpp"
#include "gui_forms/engine/theme_rules/theme_rules.hpp"
#include "gui_forms/theme/types/control_visual_types.hpp"
#include "gui_forms/theme/types/theme_types.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_marks/paint_marks.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace gui_forms {

class Control;
class Painter;
class Theme;

// The node canvas's control id and anatomy (the part table in
// docs/user/theme-language.md, "node canvas"). The anatomy is the theme's,
// so the default theme's `node-canvas::part(...)` rules compile before a
// canvas control is registered: the theme compilers add it to the selector
// vocabulary when the controls they are given lack it. A canvas control
// returns it from Control::anatomy(). Static storage.
inline constexpr std::string_view node_canvas_control_id{"node_canvas"};
[[nodiscard]] GUI_FORMS_API const ControlAnatomy& node_canvas_anatomy() noexcept;

// The states a canvas part is resolved and painted in. Each is one record
// of CanvasLook; `count` is no state.
// - a node and its header: at rest, under the pointer (`:hover`), selected
//   (`:state(selected)`) or disabled (`:disabled`);
// - a port: at rest, under the pointer, or under a dragged link that may
//   (`:state(drop-accepted)`) or may not (`:state(drop-rejected)`) connect;
// - a link: at rest, under the pointer, selected or invalid (`:invalid`);
// - a lamp: off (neither), on (`:checked`) or in error (`:invalid`).
enum class CanvasNodeState : std::uint8_t { normal, hot, selected, disabled, count };
enum class CanvasPortState : std::uint8_t { normal, hot, drop_accepted, drop_rejected, count };
enum class CanvasLinkState : std::uint8_t { normal, hot, selected, invalid, count };
enum class CanvasLampState : std::uint8_t { off, on, error, count };

inline constexpr std::size_t canvas_node_state_count = static_cast<std::size_t>(CanvasNodeState::count);
inline constexpr std::size_t canvas_port_state_count = static_cast<std::size_t>(CanvasPortState::count);
inline constexpr std::size_t canvas_link_state_count = static_cast<std::size_t>(CanvasLinkState::count);
inline constexpr std::size_t canvas_lamp_state_count = static_cast<std::size_t>(CanvasLampState::count);

// Which link part a link is drawn as: `link`, or `dashed-link` (a link the
// consumer marks dashed, and the static form of a flowing link).
enum class CanvasLinkStyle : std::uint8_t { plain, dashed };

// A filled and outlined canvas part (the background, a node, its header,
// a port, a lamp, the marquee): its area, its line, the line's width and
// its corner radius, in view units. A transparent colour or a zero width
// draws nothing of that half.
struct GUI_FORMS_API CanvasArea final {
    Color fill{};
    Color stroke{};
    double stroke_width{};
    double corner_radius{};
    friend constexpr bool operator==(const CanvasArea& left, const CanvasArea& right) noexcept {
        return left.fill == right.fill && left.stroke == right.stroke && left.stroke_width == right.stroke_width &&
               left.corner_radius == right.corner_radius;
    }
};

// A link's stroke: its colour and width, its dash and gap (a dash of 0 is
// solid), and its halo, a wider stroke `halo_width` beyond each side drawn
// under it (0 draws none). Lengths in view units.
struct GUI_FORMS_API CanvasLine final {
    Color colour{};
    double width{1.0};
    double dash{};
    double gap{};
    Color halo{};
    double halo_width{};
    friend constexpr bool operator==(const CanvasLine& left, const CanvasLine& right) noexcept {
        return left.colour == right.colour && left.width == right.width && left.dash == right.dash &&
               left.gap == right.gap && left.halo == right.halo && left.halo_width == right.halo_width;
    }
};

// The world grid: minor and major colours, one line width (view units),
// the minor spacing in world units at zoom 1, and the band of view spacings
// the shown level keeps to (select_grid_level; its maximum is exactly
// grid_level_ratio times its minimum). `minor_shown` is false in forced
// high contrast, which draws the major lines alone, with no fade.
struct GUI_FORMS_API CanvasGrid final {
    Color minor{};
    Color major{};
    double line_width{1.0};
    double spacing{16.0};
    GridLevelBand band{};
    bool minor_shown{true};
    friend constexpr bool operator==(const CanvasGrid& left, const CanvasGrid& right) noexcept {
        return left.minor == right.minor && left.major == right.major && left.line_width == right.line_width &&
               left.spacing == right.spacing && left.band.minimum_spacing == right.band.minimum_spacing &&
               left.band.maximum_spacing == right.band.maximum_spacing && left.minor_shown == right.minor_shown;
    }
};

// What moves along a flowing link (the link part at rest): the mark, its
// speed in world units a second and its pitch in view units.
struct GUI_FORMS_API CanvasFlow final {
    CanvasFlowStyle style{CanvasFlowStyle::none};
    double speed{48.0};
    double spacing{16.0};
    friend constexpr bool operator==(const CanvasFlow& left, const CanvasFlow& right) noexcept {
        return left.style == right.style && left.speed == right.speed && left.spacing == right.spacing;
    }
};

// Every canvas part in every state, as one theme draws them in one colour
// scheme and contrast. `texture` is none where the theme sets none, and
// always in forced high contrast. `titles` are the header's title ink by
// node state. The marquee is its area (`marquee`) and its dashed outline
// (`marquee_outline`; no halo). A plain value: copy it, keep it, compare
// it.
struct GUI_FORMS_API CanvasLook final {
    CanvasArea background{};
    CanvasGrid grid{};
    std::optional<ControlCanvasTexture> texture{};
    std::array<CanvasArea, canvas_node_state_count> nodes{};
    std::array<CanvasArea, canvas_node_state_count> headers{};
    std::array<Color, canvas_node_state_count> titles{};
    CanvasArea selection{};
    std::array<CanvasArea, canvas_port_state_count> ports{};
    std::array<CanvasLine, canvas_link_state_count> links{};
    std::array<CanvasLine, canvas_link_state_count> dashed_links{};
    std::array<CanvasArea, canvas_lamp_state_count> lamps{};
    CanvasArea marquee{};
    CanvasLine marquee_outline{};
    CanvasFlow flow{};
    friend bool operator==(const CanvasLook& left, const CanvasLook& right) noexcept {
        return left.background == right.background && left.grid == right.grid && left.texture == right.texture &&
               left.nodes == right.nodes && left.headers == right.headers && left.titles == right.titles &&
               left.selection == right.selection && left.ports == right.ports && left.links == right.links &&
               left.dashed_links == right.dashed_links && left.lamps == right.lamps &&
               left.marquee == right.marquee && left.marquee_outline == right.marquee_outline &&
               left.flow == right.flow;
    }
};

// Resolves every canvas part of `canvas` from `theme` (Theme::resolve with
// the part's name and node_canvas_control_id as the control type, so the
// rules match the canvas's classes and stable ID: the grid variants
// `paper`, `blue`, `green` and `grey` are classes). `context` carries the
// canvas's colour scheme and contrast (Control::visual_context); each part
// is resolved in its own states over it.
// - A field no rule sets takes the stock value the part table states,
//   coloured from the theme's basic style.
// - Dark: where the theme derives its dark variant (ThemeDarkVariant::
//   derived), every colour is mapped by the dark colour rule by what it
//   paints (the canvas as a well, nodes and ports as stock faces, headers
//   and lamps as chrome, lines as lines, selection cues as cues, titles as
//   ink); a theme with an authored dark variant keeps its rules' colours
//   and maps only the stock values.
// - Forced high contrast: no rule applies (the engine's pass); areas take
//   the plate, lines the state's edge, a selected link a two-unit halo in
//   the focus colour; minor grid lines and the marquee's fill are clear and
//   there is no texture.
// Reads the theme only; the result owns everything it holds.
[[nodiscard]] GUI_FORMS_API CanvasLook resolve_canvas_look(const Theme& theme, const Control& canvas,
                                                           ControlVisualContext context);

// The contrast a canvas texture is multiplied at: `contrast` clamped to
// [0, maximum_canvas_texture_contrast] (the design language's sub-3 %
// texture rule; the compiler already refuses more), 0 when not finite.
[[nodiscard]] GUI_FORMS_API double clamp_canvas_texture_contrast(double contrast) noexcept;

// Where the canvas lies in the painter's coordinates: `origin` is world
// (0, 0) and `zoom` view units per world unit (finite and positive; the
// background draws no grid otherwise).
struct GUI_FORMS_API CanvasView final {
    Point origin{};
    double zoom{1.0};
    friend constexpr bool operator==(const CanvasView& left, const CanvasView& right) noexcept {
        return left.origin == right.origin && left.zoom == right.zoom;
    }
};

// The canvas background over `area`: the region's colour, then the texture
// (one multiply group at the clamped contrast holding one tiled pattern,
// its tiles anchored at world (0, 0) and drawn at the image's own size at
// every zoom, so the grain stays sharp), then the grid (the level
// select_grid_level chooses for `view.zoom`, one fill_grid_pattern pass
// with the level's fade). Draws a bounded number of commands whatever the
// area: three at most, besides the group.
GUI_FORMS_API void paint_canvas_background(Painter& painter, const CanvasLook& look, Rect area, CanvasView view);

// A node's body `body` with its header band, the top `header_height` view
// units (0 for none, clamped to the body), in `state`: the body's area, the
// header's area clipped to the band and a line under it, then the body's
// outline over both.
GUI_FORMS_API void paint_canvas_node(Painter& painter, const CanvasLook& look, Rect body, double header_height,
                                     CanvasNodeState state);

// The selection ring around a node's `body`: the selection part's line,
// two view units outside the body plus half its width, with its corners.
GUI_FORMS_API void paint_canvas_node_selection(Painter& painter, const CanvasLook& look, Rect body);

// A port: a disc of `radius` at `centre` in `state`'s area.
GUI_FORMS_API void paint_canvas_port(Painter& painter, const CanvasLook& look, Point centre, double radius,
                                     CanvasPortState state);

// A link through `points` (2 or more, finite; a NaN breaks it as
// Painter::stroke_polyline does), as `style`'s part in `state`: its halo
// first, solid, then its stroke with round joins and caps and its dashes.
// Fewer than 2 points draw nothing.
GUI_FORMS_API void paint_canvas_link(Painter& painter, const CanvasLook& look, std::span<const Point> points,
                                     CanvasLinkState state, CanvasLinkStyle style);

// A lamp: a disc of `radius` at `centre` in `state`'s area.
GUI_FORMS_API void paint_canvas_lamp(Painter& painter, const CanvasLook& look, Point centre, double radius,
                                     CanvasLampState state);

// The marquee over `rect`: its area, then its outline with its dashes.
GUI_FORMS_API void paint_canvas_marquee(Painter& painter, const CanvasLook& look, Rect rect);

} // namespace gui_forms
