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
// which call no theme resolution. All but the background allocate nothing;
// the background hands each texture layer to the theme engine, which
// bakes it into the window's part cache (paint_canvas_background).
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
#include <string>
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
// - a lamp: off (neither), on (`:checked`) or in error (`:invalid`);
// - the link being dragged (`link-preview`): seeking a port, over a port
//   that accepts it (`:state(drop-accepted)`) or over one that refuses it
//   (`:state(drop-rejected)`).
// A port's label ink follows its port's state.
enum class CanvasNodeState : std::uint8_t { normal, hot, selected, disabled, count };
enum class CanvasPortState : std::uint8_t { normal, hot, drop_accepted, drop_rejected, count };
enum class CanvasLinkState : std::uint8_t { normal, hot, selected, invalid, count };
enum class CanvasLampState : std::uint8_t { off, on, error, count };
enum class CanvasPreviewState : std::uint8_t { seeking, accepted, refused, count };

inline constexpr std::size_t canvas_node_state_count = static_cast<std::size_t>(CanvasNodeState::count);
inline constexpr std::size_t canvas_port_state_count = static_cast<std::size_t>(CanvasPortState::count);
inline constexpr std::size_t canvas_link_state_count = static_cast<std::size_t>(CanvasLinkState::count);
inline constexpr std::size_t canvas_lamp_state_count = static_cast<std::size_t>(CanvasLampState::count);
inline constexpr std::size_t canvas_preview_state_count = static_cast<std::size_t>(CanvasPreviewState::count);

// The keyboard focus cue (the `focus` part) is a shape, not only a colour:
// a ring at least minimum_canvas_focus_width units wide, drawn
// canvas_focus_gap units outside the focused node or port, so it shows
// where a hue difference alone would not (forced high contrast, colour
// blindness, a theme whose focus colour is near its line colour).
inline constexpr double minimum_canvas_focus_width = 2.0;
inline constexpr double canvas_focus_gap = 2.0;

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

// How the texture meets the canvas: multiplied (darkening, on a light
// canvas) or screened (lightening, on a dark one, where a multiplied grain
// would change a near-black well by less than one level). Either way at
// each layer's clamped contrast.
enum class CanvasTextureBlend : std::uint8_t { multiply, screen };

// Every canvas part in every state, as one theme draws them in one colour
// scheme and contrast. `texture` holds the theme's procedural and finish layers, bottom
// first, each with its contrast clamped (layers of contrast 0 are dropped);
// none where the theme sets none, and always in forced high contrast. `titles` are the header's title ink by
// node state. The marquee is its area (`marquee`) and its dashed outline
// (`marquee_outline`; no halo). A plain value: copy it, keep it, compare
// it.
struct GUI_FORMS_API CanvasLook final {
    CanvasArea background{};
    CanvasGrid grid{};
    CanvasTextureLayers texture{};
    CanvasTextureBlend texture_blend{CanvasTextureBlend::multiply};
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
    // A port's label ink, by port state.
    std::array<Color, canvas_port_state_count> port_labels{};
    // The focus ring: its line colour, width (at least
    // minimum_canvas_focus_width) and the radius it adds to the item's
    // corners; its fill is clear.
    CanvasArea focus{};
    // The knife stroke that cuts links.
    CanvasLine knife{};
    // The link being dragged, by preview state.
    std::array<CanvasLine, canvas_preview_state_count> link_previews{};
    friend bool operator==(const CanvasLook& left, const CanvasLook& right) noexcept {
        return left.background == right.background && left.grid == right.grid && left.texture == right.texture &&
               left.texture_blend == right.texture_blend &&
               left.nodes == right.nodes && left.headers == right.headers && left.titles == right.titles &&
               left.selection == right.selection && left.ports == right.ports && left.links == right.links &&
               left.dashed_links == right.dashed_links && left.lamps == right.lamps &&
               left.marquee == right.marquee && left.marquee_outline == right.marquee_outline &&
               left.flow == right.flow && left.port_labels == right.port_labels && left.focus == right.focus &&
               left.knife == right.knife && left.link_previews == right.link_previews;
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
//   as chrome, lines as lines, selection cues as cues, titles as ink, and
//   the colours that carry state as signals, which keep their hue: a lamp
//   on stays green, in error red, off neutral; an accepted drop green, a
//   rejected one red; an invalid link red); a theme with an authored dark
//   variant keeps its rules' colours and maps only the stock values. The
//   texture screens instead of multiplying (CanvasTextureBlend).
// - Forced high contrast: no rule applies (the engine's pass); areas take
//   the plate, lines the state's edge, a selected link a two-unit halo in
//   the focus colour; minor grid lines and the marquee's fill are clear and
//   there is no texture.
// Reads the theme only; the result owns everything it holds.
[[nodiscard]] GUI_FORMS_API CanvasLook resolve_canvas_look(const Theme& theme, const Control& canvas,
                                                           ControlVisualContext context);

// One item of the canvas (a node, a port or a link) as theme rules address
// it with `:item()`: its stable ID (`:item(#n42)`) and classes
// (`:item(.warning)`). A socket's data type is an item class on its port
// and on the links it carries, conventionally `socket-<type>`
// (`node-canvas::part(port):item(.socket-float)`), so a theme colours by
// data type. Both views are borrowed for the call.
struct GUI_FORMS_API CanvasItem final {
    std::string_view id{};
    std::span<const std::string> classes{};
};

// The look of one item: as resolve_canvas_look above, with `item`'s ID and
// classes matched by `:item()` rules, so every part an item draws (node,
// header, selection, port, port label, link, dashed link, link preview,
// lamp and focus ring) takes the rules written for it. A canvas paints the
// item's parts from this look and the canvas-wide parts (region, grid,
// marquee, knife, texture) from its own; it resolves one per distinct ID
// and class set it shows, on the same triggers, and keeps them. With no ID
// and no classes it is the canvas's look. Reads the theme only; runs the
// cascade on every call (Theme::resolve's item form keeps nothing).
[[nodiscard]] GUI_FORMS_API CanvasLook resolve_canvas_look(const Theme& theme, const Control& canvas,
                                                           ControlVisualContext context, const CanvasItem& item);

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

// The procedural fill a texture layer bakes (canvas_texture_fill): the
// layer's pattern, seed, tile and strength, over the ground that leaves the
// canvas unchanged under the blend (white under multiply, black under
// screen), its figure the other extreme. So a pattern not yet generated
// (its ground drawn meanwhile, E10) changes nothing, and a generated one
// darkens or lightens by at most its strength times the layer's contrast.
[[nodiscard]] GUI_FORMS_API ProceduralFill canvas_texture_fill(const CanvasTextureLayer& layer,
                                                               CanvasTextureBlend blend) noexcept;

// The canvas background of `canvas` over `area`: the region's colour, then
// each texture layer, then the grid (the level select_grid_level chooses for
// `view.zoom`, one fill_grid_pattern pass with the level's fade).
// - A procedural layer is a baked pattern, never an image: a material of
//   the one procedural fill canvas_texture_fill makes, baked by the theme
//   engine (ThemeEngine::paint_material_blocks, record `texture.<index>`).
//   Its tiles are anchored at world (0, 0) and stay at the tile's logical
//   size at every zoom, so the grain stays sharp.
// - A finish layer is the layer's finish, in its theme's colours, as a
//   property of the world: evaluated at world device pixels at the
//   window's scale times the zoom, from the window's world finish tiles
//   (ThemeEngine::paint_finish_tiles, record `texture.<index>`), so it is
//   continuous everywhere, with no period and no seam between tiles, and
//   pans and zooms with the canvas.
// - Each layer is drawn in one multiply or screen group, as the look's
//   texture_blend says, at the layer's clamped contrast.
// - Cost: a procedural layer is baked once per change of the look or the
//   window's scale, its pattern generated in an idle frame; a finish tile
//   is painted in an idle frame, once per tile, scale and zoom, and kept
//   while the window's finish tile budget allows (panning reuses them, a
//   new zoom paints new ones; until then the place shows no finish). Each
//   paint draws, per layer, one raster per block or tile meeting the area.
// A view that is not placed (a non-finite origin or zoom, or a zoom not
// above 0) draws the colour alone.
GUI_FORMS_API void paint_canvas_background(Painter& painter, const Control& canvas, const CanvasLook& look,
                                           Rect area, CanvasView view);

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

// The keyboard focus ring around a focused node's body or port's disc
// `target` whose corners are `target_radius` (a port's radius for a
// disc): the focus part's line, canvas_focus_gap outside the target plus
// half its width, its corners the target's grown by the same reach plus
// the part's corner radius. Draws nothing for an empty target or a clear
// line; the width is never below minimum_canvas_focus_width.
GUI_FORMS_API void paint_canvas_focus(Painter& painter, const CanvasLook& look, Rect target, double target_radius);

// Node silhouettes at the overview level of detail: each of `bodies`
// filled flat in the node part's fill for `state` (no header, outline or
// corners at that distance). Draws nothing for no bodies or a clear fill.
GUI_FORMS_API void paint_canvas_overview(Painter& painter, const CanvasLook& look, std::span<const Rect> bodies,
                                         CanvasNodeState state);

// A node's title `text` at `baseline` in `font`, in the header part's ink
// (CanvasLook::titles) for `state`, clipped to `band` (the header band less
// its corners). Draws nothing for empty text, an empty band or a clear ink.
GUI_FORMS_API void paint_canvas_node_title(Painter& painter, const CanvasLook& look, Rect band, Point baseline,
                                           std::string_view text, FontSpec font, CanvasNodeState state);

// A port's label `text` at `baseline` in `font`, in the label ink of the
// port's `state`. Draws nothing for empty text or a clear ink.
GUI_FORMS_API void paint_canvas_port_label(Painter& painter, const CanvasLook& look, Point baseline,
                                           std::string_view text, FontSpec font, CanvasPortState state);

// The knife stroke through `points` (as paint_canvas_link's), with its
// dashes. Fewer than 2 points draw nothing.
GUI_FORMS_API void paint_canvas_knife(Painter& painter, const CanvasLook& look, std::span<const Point> points);

// The link being dragged through `points`, in `state`: its halo, then its
// stroke with its dashes, as paint_canvas_link. Fewer than 2 points draw
// nothing.
GUI_FORMS_API void paint_canvas_link_preview(Painter& painter, const CanvasLook& look, std::span<const Point> points,
                                             CanvasPreviewState state);

} // namespace gui_forms
