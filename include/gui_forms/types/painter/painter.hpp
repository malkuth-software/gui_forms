#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_marks/paint_marks.hpp"
#include "gui_forms/types/paint_path/paint_path.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/typography.hpp"
#include "gui_forms/typography/shaping_workspace/shaping_workspace.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace gui_forms {

struct PaintGroupEffect;

class LiveSurface;
class PaintFramebuffer;
class PartRaster;
class PreparedTextLayout;
struct LayoutAuthority;
struct PreparedTextPaintResult;
struct SurfaceMaterial;

// Renderer-derived data for one retained draw command, such as shaped text.
// The command's recording owns it: a Window creates it when a control
// records the command and releases it when that control's display chunk is
// rebuilt or retired. A renderer fills, validates and replaces the memo.
class GUI_FORMS_API RetainedDrawMemo {
public:
    virtual ~RetainedDrawMemo() = default;
};

struct GUI_FORMS_API RetainedDrawCache final {
    std::unique_ptr<RetainedDrawMemo> memo;
};

// How a paint group's image joins what lies beneath it, through the shared
// ADR-004 blend (detail::composite_part_raster):
// - source_over: premultiplied source-over;
// - plus (E7r): each premultiplied channel, alpha included, added to the
//   one beneath and clamped at 255. The engine's cross-fade draws a part's
//   old face at 1 - t and its new face in a plus group at t, inside one
//   isolated group, so that the two sum to one face.
// - multiply and screen (E9.6, CSS `mix-blend-mode`): CSS's separable
//   blend modes, composited as CSS composites them. With s and d the
//   premultiplied source and destination channels and αs and αd their
//   alphas, multiply gives s × (1 − αd) + d × (1 − αs) + s × d, screen
//   gives s + d − s × d, and both give alpha αs + αd − αs × αd. A theme
//   rule's `blend` property (blend_property) draws a part through one.
// - copy (stage T5): the group's image replaces what lies beneath it within
//   the group's clipped bounds, transparent pixels included, as Porter-Duff
//   "source" does; a partly covered edge pixel keeps the rest of what lay
//   beneath. A window granted a non-opaque backdrop paints its backplane in
//   a copy group, so that its frame starts clear. Theme data never selects
//   it.
// - overlay, darken, lighten, color_dodge, color_burn, hard_light,
//   soft_light, difference and exclusion (capability review, owner
//   2026-11-16; CSS `mix-blend-mode`): the rest of CSS's separable blend
//   modes, composited as multiply is: each colour channel
//   s × (1 − αd) + d × (1 − αs) + αs × αd × B(d / αd, s / αs), with B the
//   mode's function of the straight colours as Compositing and Blending
//   Level 1 defines it, and alpha αs + αd − αs × αd. They are appended
//   after copy so that recorded enumerators keep their values.
enum class PaintGroupComposite : std::uint8_t {
    source_over,
    plus,
    multiply,
    screen,
    copy,
    overlay,
    darken,
    lighten,
    color_dodge,
    color_burn,
    hard_light,
    soft_light,
    difference,
    exclusion,
};

// The last enumerator of PaintGroupComposite, for range checks of recorded
// values.
inline constexpr PaintGroupComposite paint_group_composite_last = PaintGroupComposite::exclusion;

// A paint group (T2 plan, E7; owner, 2026-10-21: "there are other things that
// group layer may be needed for"): commands drawn as one image, then
// composited once.
// - `bounds`, in the current coordinates when the group begins, limits the
//   group. It is given up front so that a painter sizes its storage once:
//   nothing the group draws outside `bounds` (or outside the clip in force
//   at its beginning) is shown.
// - `opacity` in [0, 1] multiplies the group's whole image. Below 1 the group
//   is isolated: its commands draw into a transparent premultiplied image the
//   size of its clipped bounds, which is composited once at `opacity` by
//   `composite`, so overlapping commands never show through one another. At
//   1 (or above) the commands draw directly under clip_rect(bounds), which is
//   exactly what isolation would give for source-over. At 0 (or NaN) nothing
//   is drawn.
// - A group whose `composite` is not source-over, or that sets `isolated`,
//   is isolated at any opacity: a group holding a plus group must be, so
//   that the sum is formed before it meets what lies beneath. A painter
//   that cannot isolate (no storage) draws the group directly.
// - `effect` (capability review decision 1, owner 2026-11-16), when set,
//   filters and masks the group's image before it is composited
//   (PaintGroupEffect, declared in surface_material/paint_group_effect,
//   which this header does not include). A group with an effect is
//   isolated at any opacity, and its image reaches past `bounds` by the
//   effect's reach (paint_group_effect_reach): its commands are still
//   limited to `bounds`, and the blur spreads into the margin. An invalid
//   effect (valid_paint_group_effect) is ignored and counted as a group
//   refusal. Shared read-only: a recording keeps it alive.
struct GUI_FORMS_API PaintGroup final {
    Rect bounds{};
    double opacity{1.0};
    PaintGroupComposite composite{PaintGroupComposite::source_over};
    bool isolated{false};
    std::shared_ptr<const PaintGroupEffect> effect{};
};

// The deepest groups nest (T2, coordinator decision Q2/Q3). A group begun
// deeper is refused by name: it draws without isolation (as clip_rect(bounds)
// under a save), and the refusal is counted.
inline constexpr std::size_t maximum_paint_group_depth = 4;

// A two-dimensional affine map (Painter::begin_transform): the point
// (x, y) goes to (xx * x + xy * y + dx, yx * x + yy * y + dy). The default
// is the identity.
struct GUI_FORMS_API PaintTransform final {
    double xx{1.0};
    double yx{0.0};
    double xy{0.0};
    double yy{1.0};
    double dx{0.0};
    double dy{0.0};

    [[nodiscard]] bool operator==(const PaintTransform& other) const noexcept {
        return xx == other.xx && yx == other.yx && xy == other.xy && yy == other.yy && dx == other.dx &&
               dy == other.dy;
    }
};

// The smallest |xx * yy - xy * yx| a transform group accepts: a map that
// flattens the plane, or nearly, has no usable inverse to clip by.
inline constexpr double minimum_paint_transform_determinant = 1.0e-6;
// The deepest transform groups nest. One begun deeper draws nothing, as
// does every group begun inside a group that draws nothing.
inline constexpr std::size_t maximum_paint_transform_depth = 8U;

// Whether `transform` is finite and its determinant at least
// minimum_paint_transform_determinant in size.
[[nodiscard]] GUI_FORMS_API bool valid_paint_transform(const PaintTransform& transform) noexcept;
// Whether `transform` only moves the plane (xx = yy = 1, xy = yx = 0).
[[nodiscard]] GUI_FORMS_API bool paint_transform_is_translation(const PaintTransform& transform) noexcept;
// Where `transform` takes `point`.
[[nodiscard]] GUI_FORMS_API Point transform_paint_point(const PaintTransform& transform, Point point) noexcept;
// The rectangle bounding `rect` mapped by `transform` (its four corners):
// the area a control damages for a transform group of bounds `rect`. An
// empty or non-finite rectangle, or a transform that is not valid, gives
// an empty rectangle.
[[nodiscard]] GUI_FORMS_API Rect transformed_paint_bounds(const PaintTransform& transform, Rect rect) noexcept;
// The turn of `degrees` about `centre`, positive anticlockwise as seen on
// the surface, as Painter::draw_rotated_text_utf8 turns: (1, 0) about the
// origin goes to (cos, -sin). A non-finite input gives the identity.
[[nodiscard]] GUI_FORMS_API PaintTransform paint_rotation_about(Point centre, double degrees) noexcept;

// A transform group (Painter::begin_transform; owner, 2026-11-16: a scoped
// transform, never a free concatenation): `bounds`, in the group's content
// coordinates, limits what it draws; `transform` maps content coordinates
// into those in force where the group begins.
struct GUI_FORMS_API PaintTransformGroup final {
    Rect bounds{};
    PaintTransform transform{};
};

// The most cells Painter::fill_checker draws one by one.
inline constexpr std::size_t maximum_checker_cells = 65536U;
// The most marked squares the base Painter::fill_hatch fills one by one.
inline constexpr std::size_t maximum_hatch_squares = 65536U;

// Whether `focus` lies strictly inside the ellipse of `radii` (both
// positive) about `center`, as Painter::fill_focal_radial_gradient
// requires; false for radii that are not positive or a non-finite result.
[[nodiscard]] GUI_FORMS_API bool focus_inside_ellipse(Point center, Size radii, Point focus) noexcept;

// How draw_text_utf8_fitted shortens text that does not fit: cut it at
// the last code point that fits, or cut it so U+2026 fits after it.
enum class TextTrim : std::uint8_t { cut, ellipsis };

// One run of Painter::draw_text_runs: its baseline origin, its UTF-8 text
// (lent for the call) and its colour.
struct GUI_FORMS_API TextRun final {
    Point origin{};
    std::string_view text{};
    Color color{};
};
// Where a line of text turned about its baseline start lies
// (Painter::draw_rotated_text_utf8). The run's box is its advance `width`
// along the baseline and its line box `ascent` above and `descent` below
// it; turned `degrees` anticlockwise as seen about `anchor`, the box's
// corners are anchor + s * (cos, -sin) + t * (sin, cos) for s in
// [0, width] and t in [-ascent, descent].
struct GUI_FORMS_API RotatedTextBox final {
    double width{};
    double ascent{};
    double descent{};
    double degrees{};
};
// The rectangle bounding the turned box placed at `anchor`: the area a
// control keeps inside its bounds (or its visual outsets) for the run, so
// its damage covers every pixel the run inks. Negative or non-finite
// extents, or a non-finite angle or anchor, give an empty rectangle at the
// anchor.
[[nodiscard]] GUI_FORMS_API Rect rotated_text_bounds(Point anchor, const RotatedTextBox& box) noexcept;
// The anchor that centres the turned box on `centre` (a label placed by
// its middle, such as an axis title along its edge). Non-finite input
// gives `centre`.
[[nodiscard]] GUI_FORMS_API Point rotated_text_anchor(Point centre, const RotatedTextBox& box) noexcept;

class GUI_FORMS_API Painter : public TextMetricsProvider {
public:
    virtual ~Painter() = default;
    // Paint groups (see PaintGroup). begin_group and end_group pair like save
    // and restore and nest like them, up to maximum_paint_group_depth: the
    // state in force at end_group (transform, clips, saves) must be the one
    // at its begin_group. Within a retained display chunk every group ends in
    // the chunk that begins it; the recorder refuses a chunk left unbalanced.
    // The recording and Skia painters isolate; each keeps one
    // reusable scratch image per nesting level, grown to the largest clipped
    // bounds it has met and released with the painter. The base
    // implementation is a minimal painter's fallback without isolation:
    // save() and clip_rect(bounds), then restore().
    virtual void begin_group(const PaintGroup& group);
    virtual void end_group();
    // Transform groups (see PaintTransformGroup). Between begin_transform
    // and its end_transform, coordinates are the group's content
    // coordinates: a point p drawn there lands at transform(p) in the
    // coordinates in force at begin_transform. `bounds` limits the content
    // as clip_rect(bounds) under the map would, so everything the group
    // draws lies inside transformed_paint_bounds(transform, bounds) in the
    // enclosing coordinates: the rectangle a control damages for it. The
    // pair nests like save and restore, and inside and around paint groups
    // (each ends in the scope it began in), up to
    // maximum_paint_transform_depth; the saves inside must balance. A
    // transform that is not valid (valid_paint_transform), or a group begun
    // too deep, draws nothing until its end_transform.
    // Inside a group that does more than translate, text is shaped as
    // outside and its glyph outlines are rasterized under the map (no
    // picture of the upright run is turned); that rasterization is the seam
    // where glyphs from the STXlib face seat enter
    // (GlyphRunRasterizer::rasterize_transformed in the Skia painter). The
    // raster's own direct composites, which address device pixels (text
    // coverage, part rasters, isolated paint groups), give way to drawing
    // through the map; nothing new is blended.
    // The base implementation, a minimal painter's fallback, draws a
    // translation as save, translate and clip_rect(bounds), and any other
    // map as nothing (save and clip_rect of an empty rectangle);
    // end_transform restores. The recording and Skia painters draw every
    // valid map.
    virtual void begin_transform(const PaintTransformGroup& group);
    virtual void end_transform();
    // Native renderers preserve their fonts and drawing semantics offscreen.
    // Recording/headless painters explicitly report this facility unavailable.
    [[nodiscard]] virtual std::unique_ptr<PaintFramebuffer> create_framebuffer(
        Size logical_size, double scale);
    // Development surface: recording retains immutable storage; a compatible
    // backend stages pixels. Only the host's successful frame commit presents.
    [[nodiscard]] virtual PreparedTextPaintResult draw_prepared_text(
        const PreparedTextLayout& layout, const LayoutAuthority expected, const Point baseline, const Color color);

    virtual void save() = 0;
    virtual void restore() = 0;
    virtual void translate(Point offset) = 0;
    virtual void clip_rect(Rect rect) = 0;
    // Rich geometry has deterministic renderer-neutral fallbacks so a minimal
    // host stays coherent.  Recording and production raster painters override
    // these operations to preserve the authored material exactly.
    virtual void clip_rounded_rect(Rect rect, double radius);
    // Narrows the clip to `rect` with each corner rounded by its own radius
    // (EP-6: an effect clipped to its part's shape). The fallback rounds
    // every corner by the largest of the four, so it never shows more
    // than the exact clip at a corner.
    virtual void clip_rounded_rect_corners(Rect rect, PaintCornerRadii radii);
    // Narrows the clip to `path`'s interior under `rule`: every contour,
    // open ones closed by a straight segment, as fill_path fills them. Any
    // fill drawn under it (a gradient, a pattern, an image, text) then shows
    // in an arbitrary shape: a radial gradient in a star is clip_path, then
    // fill_radial_gradient over the path's bounds. The clip lasts until the
    // matching restore, as clip_rect's does. An invalid path
    // (valid_paint_path) or a rule outside the enumeration clips everything
    // away until that restore, so a refused clip never shows what it was to
    // hide. The base implementation clips to paint_path_bounds(path): a
    // minimal painter shows the path's bounding box, never more.
    virtual void clip_path(PaintPathView path, PolygonFillRule rule);
    virtual void fill_rect(Rect rect, Color color) = 0;
    virtual void fill_rounded_rect(Rect rect, double radius, Color color);
    virtual void stroke_rect(Rect rect, Color color, double width) = 0;
    virtual void stroke_rounded_rect(Rect rect, double radius, Color color,
                                     double width);
    virtual void fill_linear_gradient(
        Rect rect, Point start, Point end,
        std::span<const GradientStop> stops);
    virtual void fill_linear_gradient_spread(
        Rect rect, Point start, Point end,
        std::span<const GradientStop> stops, GradientSpreadMode spread);
    virtual void fill_radial_gradient(
        Rect rect, Point center, Size radii,
        std::span<const GradientStop> stops);
    // fill_radial_gradient with `spread` outside the ellipse: `pad` keeps
    // the last stop's colour, `repeat` starts the stops again at each
    // further radius (concentric rings, CSS repeating-radial-gradient), and
    // `reflect` runs them back and forth so the rings join without a seam.
    // A spread outside the enumeration draws nothing. The base
    // implementation draws as fill_radial_gradient (padded), as
    // fill_linear_gradient_spread's does.
    virtual void fill_radial_gradient_spread(
        Rect rect, Point center, Size radii,
        std::span<const GradientStop> stops, GradientSpreadMode spread);
    // A radial gradient whose stops start at `focus` instead of the centre
    // (SVG's fx, fy; a highlight off the middle of a sphere): offset t lies
    // on the ellipse of radii t * `radii` centred at
    // focus + t * (center - focus), so 0 is the focus itself and 1 the
    // ellipse of `radii` about `center` (the HTML two-circle gradient from
    // a point to the ellipse), with `spread` beyond. `focus` must lie strictly inside
    // the ellipse; a focus on or outside it, an empty rectangle, radii that
    // are not positive, invalid stops or an unknown spread draw nothing.
    // The base implementation draws rings, each centred on its point along
    // the focus-to-centre line, padded as fill_radial_gradient's are.
    virtual void fill_focal_radial_gradient(
        Rect rect, Point center, Size radii, Point focus,
        std::span<const GradientStop> stops, GradientSpreadMode spread);
    // Drawn before the owning surface fill.  The shape body may therefore be
    // included by a fallback without changing the final composited result.
    virtual void draw_box_shadow(Rect rect, double corner_radius, Point offset,
                                 double blur_radius, double spread,
                                 Color color);
    // Replay of a retained draw_box_shadow. A renderer may keep the blurred
    // coverage in the cache; the default draws as draw_box_shadow.
    virtual void draw_retained_box_shadow(
        Rect rect, double corner_radius, Point offset, double blur_radius,
        double spread, Color color, const std::shared_ptr<RetainedDrawCache>& cache);
    // Drawn over the owning surface fill and clipped to its interior. The
    // source is the complement of a translated, spread-adjusted inner box, so
    // the recipe relaxes with live owner geometry rather than fixed traces.
    virtual void draw_inset_box_shadow(Rect rect, double corner_radius,
                                       Point offset, double blur_radius,
                                       double spread, Color color);
    virtual void draw_line(Point from, Point to, Color color, double width) = 0;
    // Batched marks (IMGUI_IMPLOT_STUDY P7). Each draws a whole caller-owned
    // span as one command: the painter reads the span during the call only
    // and allocates nothing per element; a recording copies the span once.
    // Invalid styles, and points or rectangles that are not finite where a
    // call says so, draw nothing. The base implementations are a minimal
    // painter's fallbacks built from draw_line, fill_rect and the other
    // primitives (solid strokes, square joins, no anti-aliasing of their
    // own); the recording and Skia painters draw them exactly.
    //
    // An open polyline through `points`, joined and capped by `style`. A
    // point whose coordinate is NaN breaks the line (a gap): each run of
    // finite points between breaks is stroked on its own, and a run of one
    // point draws nothing. An infinite coordinate is refused (the call draws
    // nothing).
    virtual void stroke_polyline(std::span<const Point> points, const StrokeStyle& style);
    // A closed polygon through `points` (at least 3, all finite), filled by
    // `rule`.
    virtual void fill_polygon(std::span<const Point> points, Color color, PolygonFillRule rule);
    // A curved path (paint_path.hpp, exhibit gap E7) filled by `rule`: every
    // contour, open ones closed by a straight segment. An invalid path
    // (valid_paint_path) draws nothing. The base implementation flattens
    // the path at paint_path_flattening_tolerance into fixed storage
    // (maximum_fallback_path_points points, maximum_fallback_path_contours
    // contours; a longer path is drawn from its start) and fills the
    // contours together by scan rows, as fill_polygon's fallback does.
    virtual void fill_path(PaintPathView path, Color color, PolygonFillRule rule);
    // A curved path stroked with `style`'s width, joins, caps and dashes;
    // closed contours join at their start instead of capping. An invalid
    // path or style draws nothing. The base implementation flattens as
    // fill_path's does and strokes each contour as stroke_polyline's
    // fallback does (solid, no joins of its own).
    virtual void stroke_path(PaintPathView path, const StrokeStyle& style);
    // The area between two polylines sampled at the same positions (a
    // plot's shaded band, area or stacked area): `upper` and `lower` hold
    // the same count, at least 2, all finite; the band's outline runs along
    // `upper` and back along `lower`, filled nonzero. A constant baseline
    // is a `lower` of equal points.
    virtual void fill_band(std::span<const Point> upper, std::span<const Point> lower, Color color);
    // Each rectangle filled in one colour (bars, histogram bins). Empty or
    // non-finite rectangles are skipped.
    virtual void fill_rects(std::span<const Rect> rects, Color color);
    // Each rectangle filled in its own colour: `colors` holds one per
    // rectangle (heatmap cells drawn as rectangles). Mismatched counts draw
    // nothing.
    virtual void fill_rects_each(std::span<const Rect> rects, std::span<const Color> colors);
    // Independent segments with `style`'s caps and dashes and no joins
    // (error bars, stems, ticks, grid lines). Segments with a non-finite end
    // are skipped.
    virtual void stroke_segments(std::span<const PaintSegment> segments, const StrokeStyle& style);
    // A marker centred on each point (scatter, line markers, decorators).
    // Points with a non-finite coordinate are skipped.
    virtual void draw_markers(std::span<const Point> centers, const MarkerStyle& style);
    // A conic (sweep) gradient over `rect` (piece C9: a hue wheel): the
    // colour at a point is the stops' colour at the angle about `center`,
    // measured clockwise (y down) from `start_degrees` (0 along +x), one
    // full turn from offset 0 to offset 1. Stops as valid_gradient_stops
    // says; the base implementation fills 90 wedges in the stops' colours.
    virtual void fill_conic_gradient(Rect rect, Point center, double start_degrees,
                                     std::span<const GradientStop> stops);
    // `rect` filled with a checkerboard (piece C7). A cell narrower than one
    // unit, or a rectangle holding more than maximum_checker_cells cells,
    // fills the rectangle with the average of the two colours instead.
    virtual void fill_checker(Rect rect, const CheckerPattern& pattern);
    // The grid lines of `pattern` that cross `rect`, clipped to it (piece
    // C17). Refused patterns (valid_grid_pattern) draw nothing.
    virtual void fill_grid_pattern(Rect rect, const GridPattern& pattern);
    // `rect` filled with a hatch (paint_marks.hpp HatchPattern): `back`,
    // then the marked squares in `fore`. Refused patterns
    // (valid_hatch_pattern) draw nothing. The base implementation fills
    // the squares as rectangles (every edge snapped as fill_rect's are);
    // past maximum_hatch_squares marked squares it fills the rectangle
    // with the average of the two colours, as fill_checker does.
    virtual void fill_hatch(Rect rect, const HatchPattern& pattern);
    // A rounded rectangle with each corner rounded by its own radius
    // (piece C1; joined segments round only a row's outer ends). The base
    // implementation fills under clip_rounded_rect_corners; the stroke's
    // draws stroke_rounded_rect with the largest radius.
    virtual void fill_rounded_rect_corners(Rect rect, PaintCornerRadii radii, Color color);
    virtual void stroke_rounded_rect_corners(Rect rect, PaintCornerRadii radii, Color color, double width);
    virtual void draw_text_utf8(Point origin,
                                std::string_view text,
                                FontSpec font,
                                Color color) = 0;
    // Replay of a retained draw_text_utf8. The cache lives as long as the
    // recorded command; a renderer may keep shaped text there. The default
    // draws as draw_text_utf8 and leaves the cache untouched.
    virtual void draw_retained_text_utf8(
        Point origin, std::string_view text, FontSpec font, Color color,
        const std::shared_ptr<RetainedDrawCache>& cache);
    // One line of text turned a quarter about `origin` (see TextOrientation).
    // `origin` is the start of the baseline, as for draw_text_utf8; the run
    // advances up the surface (bottom_to_top) or down it (top_to_bottom), so
    // the measured width of the text becomes its height. `horizontal` draws
    // as draw_text_utf8. The base implementation is a minimal painter's
    // fallback that draws the run horizontally from `origin`, unturned; the
    // recording and Skia painters turn it.
    virtual void draw_turned_text_utf8(Point origin, std::string_view text, FontSpec font,
                                       Color color, TextOrientation orientation);
    // One line of glyph runs a provider shaped earlier (GlyphRunsView, as a
    // paragraph layout fills them), drawn at `origin` (the start of the
    // baseline, as for draw_text_utf8) in `font`'s size and colour without
    // shaping again. A painter whose own engine shaped them, in its current
    // face table (view.source), draws exactly the pixels draw_text_utf8
    // draws for view.text; for any other source, or runs whose glyph
    // indices fall outside view.glyphs, it draws view.text with
    // draw_text_utf8. That fallback is the base implementation, which the
    // recording painter keeps (a recorded line is shaped again on replay);
    // the Skia painter draws the runs.
    virtual void draw_glyph_runs(Point origin, const GlyphRunsView& view, FontSpec font, Color color);
    // Many runs in one font (the flame graph demonstrator's F3), each at its
    // origin in its colour, in order. The base implementation draws each
    // with draw_text_utf8; a renderer may share the font's setup across
    // them. Each run is as draw_text_utf8 would draw it.
    virtual void draw_text_runs(FontSpec font, std::span<const TextRun> runs);
    // The longest prefix of `text`, in whole code points, whose
    // measure_text_utf8 width is at most `max_width` (the flame graph
    // demonstrator's F2): its byte count. A binary search over byte lengths,
    // so about log2(bytes) measures; 0 when nothing fits or `max_width` is
    // not a finite non-negative number.
    [[nodiscard]] std::size_t fit_text_utf8(std::string_view text, FontSpec font, double max_width);
    // `text` at `origin`, shortened to `max_width` when it does not fit:
    // `trim` cut draws the fitted prefix, ellipsis the prefix that leaves
    // room for U+2026 and then U+2026. Returns the width drawn (0 when
    // nothing was). Trailing spaces before the ellipsis are dropped. Two
    // draw_text_utf8 calls at most, no allocation.
    double draw_text_utf8_fitted(Point origin, std::string_view text, FontSpec font, Color color, double max_width,
                                 TextTrim trim);
    // One line of text turned `degrees` about `anchor` (D5 ruling of
    // 2026-11-13: text at any angle). `anchor` is the start of the
    // baseline, as for draw_text_utf8, and stays where it is; the run turns
    // about it, positive degrees anticlockwise as seen on the surface (90
    // reads up it as TextOrientation::bottom_to_top does, -90 down it). The
    // run is shaped as draw_text_utf8 shapes it, in the same face, and its
    // glyph runs are rasterized under the turn (no picture of the
    // horizontal run is turned). Its ink lies inside rotated_text_bounds of
    // its measured extent, which is what a control keeps inside the area it
    // damages. 0 draws as draw_text_utf8, the same pixels; a non-finite
    // angle draws nothing. The base implementation, a minimal painter's
    // fallback, draws the run horizontally from `anchor`, unturned; the
    // recording and Skia painters turn it.
    virtual void draw_rotated_text_utf8(Point anchor, std::string_view text, FontSpec font, Color color,
                                        double degrees);
    // draw_glyph_runs turned as draw_rotated_text_utf8 turns text: runs this
    // painter's engine shaped in its current face table (view.source) are
    // rasterized under the turn without shaping again, the pixels
    // draw_rotated_text_utf8 draws for view.text; any other runs are drawn
    // as view.text with draw_rotated_text_utf8. That fallback is the base
    // implementation and the recording painter's (a recorded line is
    // shaped again on replay); the Skia painter draws the runs.
    virtual void draw_rotated_glyph_runs(Point anchor, const GlyphRunsView& view, FontSpec font, Color color,
                                         double degrees);
    // Text controls need the same metrics used by the painter for caret hit
    // testing, selection geometry, and horizontal viewport maintenance.  The
    // renderer owns those metrics; controls must not guess a fixed glyph width.
    [[nodiscard]] virtual Size measure_text_utf8(std::string_view text,
                                                 FontSpec font) {
        std::size_t scalars{};
        for (const unsigned char byte : text) {
            if ((byte & 0xc0U) != 0x80U) ++scalars;
        }
        const double tracking = scalars > 1U
            ? static_cast<double>(scalars - 1U) * font.letter_spacing : 0.0;
        return {static_cast<double>(scalars) * font.size * 0.55 + tracking,
                font.size * 1.2};
    }
    [[nodiscard]] ResolvedTextLayout resolve_text_layout_utf8(
        std::string_view text, FontSpec font) override {
        ResolvedTextLayout result = estimate_text_layout_utf8(text, font);
        result.logical_size = measure_text_utf8(text, font);
        return result;
    }
    // Draws `image` over `destination`. Below 1x on either axis the CPU
    // renderer draws a copy resampled by pixel area (stx_conv) to the device
    // size, cached by image content and size; at or above 1x it draws the
    // image directly with a linear filter.
    virtual void draw_image(ImageId image,
                            Rect destination,
                            double opacity = 1.0) = 0;
    // A live raster is a retained renderer-neutral resource whose producer may
    // publish independently of the UI dispatcher. Display lists retain the
    // resource and terminal renderers sample only its newest complete frame.
    virtual void draw_live_surface(std::shared_ptr<LiveSurface> surface,
                                   Rect destination,
                                   double opacity = 1.0);
    // The part of `destination`, in the current coordinates, that
    // draw_live_surface(surface, destination, opacity) would overwrite with
    // opaque pixels if drawn now, whatever lay beneath: replay then need not
    // paint what an opaque live surface hides (a panned canvas's sheet).
    // Empty when the painter cannot promise it (the default): the newest
    // frame is not opaque, the opacity is below one, the transform is not a
    // scale and translation, or a clip has soft edges. Reads only.
    [[nodiscard]] virtual Rect opaque_live_surface_cover(const std::shared_ptr<LiveSurface>& surface,
                                                         Rect destination, double opacity);
    // Composites one of the theme engine's part rasters (T2 plan, E2;
    // GIVEN A2-2 and C1-1). `origin` is the raster's top-left corner in
    // logical units; the painter places the raster's first pixel on the
    // device pixel nearest to it, one raster pixel per device pixel, so the
    // part is device-aligned and never resampled. `opacity` in [0, 1] is
    // the part's opacity, one multiplier on its whole composite. The blend
    // is the shared premultiplied source-over (detail::composite_part_raster)
    // under the current clip, so every host writes the same bytes.
    //
    // A raster made for another display scale than the painter's is not
    // drawn: the engine rasterizes a part for the scale it is shown at. The
    // base implementation draws nothing; the recording and Skia
    // painters override it. A null raster draws nothing.
    virtual void draw_part_raster(const std::shared_ptr<const PartRaster>& raster, Point origin,
                                  double opacity = 1.0);
    // Composites a part raster shown at another size (E7r): the raster is
    // stretched over `destination`, in logical units, whose corners the
    // painter places on the device pixels nearest them. Each device pixel
    // samples the raster bilinearly through the same shared blend, so every
    // host writes the same bytes. The engine shows a part this way while its
    // presentational scale moves, and bakes it sharp at its size once the
    // scale rests. Otherwise as draw_part_raster; the base implementation
    // draws nothing.
    virtual void draw_part_raster_scaled(const std::shared_ptr<const PartRaster>& raster, Rect destination,
                                         double opacity = 1.0);
    // Draws a valid, unfinished `material` over `bounds`
    // (paint_surface_material calls it after its checks). Painters that can
    // composite part rasters draw it as one (stage T4, owner 2026-11-07: one
    // rasteriser): the theme engine's material kernel bakes it at the
    // painter's display scale and the raster goes on the device pixels
    // nearest the box grown by its outsets, so a material looks the same
    // whether a rule selects it as a part's face or a control paints it.
    // The base implementation, a minimal painter's fallback, draws its
    // layers as fills, strokes and shadows, in the kernel's order.
    virtual void draw_surface_material(Rect bounds, const SurfaceMaterial& material);
    // The replay of a material a recording baked: `raster`, made at the
    // recording's display scale, covers `bounds` grown by `outset` logical
    // units on every side. A painter at that scale composites it as
    // draw_part_raster does; one at another scale, or one that cannot
    // composite rasters, draws the material as draw_surface_material does.
    // The base implementation calls draw_surface_material.
    // `material` is shared read-only with the recording; null draws nothing.
    virtual void draw_baked_surface_material(Rect bounds, const std::shared_ptr<const SurfaceMaterial>& material,
                                             const std::shared_ptr<const PartRaster>& raster, double outset);
    // Source-region replay is the renderer-neutral primitive behind image
    // strips, texture fills, and nine-patch materials. `source` is expressed
    // in source-image pixels. Minimal painters may preserve coherence by
    // falling back to whole-image scaling; production painters override it.
    virtual void draw_image_region(ImageId image, Rect source,
                                   Rect destination,
                                   double opacity = 1.0);
    virtual void draw_image_region_sampled(
        ImageId image, Rect source, Rect destination, ImageSampling sampling,
        double opacity = 1.0);
    // One retained command regardless of repetition count. Production
    // painters realize the pattern exactly; the base implementation is a
    // bounded compatibility fallback for deliberately minimal hosts. The
    // first tile's top-left corner lies at `destination`'s; `wrap` says
    // what lies beyond it (ImagePatternWrap). The base implementation
    // draws `tile` (one image when the tiles exceed its bound), `clamp`
    // as the tile and its stretched edges, and `mirror` as `tile` (it
    // cannot flip an image: every tile shows unflipped).
    virtual void fill_image_pattern(ImageId image, Size source_pixel_size,
                                    Rect destination, Size logical_tile_size,
                                    ImagePatternWrap wrap = ImagePatternWrap::tile,
                                    double opacity = 1.0);
};

} // namespace gui_forms
