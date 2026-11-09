#pragma once

#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_marks/paint_marks.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/typography.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace gui_forms {

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
class RetainedDrawMemo {
public:
    virtual ~RetainedDrawMemo() = default;
};

struct RetainedDrawCache final {
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
enum class PaintGroupComposite : std::uint8_t { source_over, plus, multiply, screen, copy };

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
struct PaintGroup final {
    Rect bounds{};
    double opacity{1.0};
    PaintGroupComposite composite{PaintGroupComposite::source_over};
    bool isolated{false};
};

// The deepest groups nest (T2, coordinator decision Q2/Q3). A group begun
// deeper is refused by name: it draws without isolation (as clip_rect(bounds)
// under a save), and the refusal is counted.
inline constexpr std::size_t maximum_paint_group_depth = 4;

// The most cells Painter::fill_checker draws one by one.
inline constexpr std::size_t maximum_checker_cells = 65536U;

class Painter : public TextMetricsProvider {
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
    virtual void draw_image(ImageId image,
                            Rect destination,
                            double opacity = 1.0) = 0;
    // A live raster is a retained renderer-neutral resource whose producer may
    // publish independently of the UI dispatcher. Display lists retain the
    // resource and terminal renderers sample only its newest complete frame.
    virtual void draw_live_surface(std::shared_ptr<LiveSurface> surface,
                                   Rect destination,
                                   double opacity = 1.0);
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
    // bounded compatibility fallback for deliberately minimal hosts.
    virtual void fill_image_pattern(ImageId image, Size source_pixel_size,
                                    Rect destination, Size logical_tile_size,
                                    ImagePatternWrap wrap = ImagePatternWrap::tile,
                                    double opacity = 1.0);
};

} // namespace gui_forms
