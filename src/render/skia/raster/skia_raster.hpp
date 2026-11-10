#pragma once

#include "gui_forms/resources.hpp"
#include "gui_forms/live_surface/frame/live_surface_frame.hpp"
#if defined(GUI_FORMS_PREPARED_TEXT)
#include "gui_forms/prepared_text.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/window/presentation/presentation_types.hpp"
#endif

#include "../../../core/resources/fonts/bundled_font_set.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace gui_forms::render {

// Ends stx_skia for the process (SkGraphics::Shutdown): frees its caches and
// singletons. Application::shutdown calls it once, after every SkiaRaster and
// every image or font registered through one is destroyed. Any later stx_skia
// use stops the program.
void shutdown_skia_library();

class SkiaRaster final : public Painter {
public:
    SkiaRaster();
    std::unique_ptr<PaintFramebuffer> create_framebuffer(Size logical_size, double scale) override;
    ~SkiaRaster() override;
    SkiaRaster(const SkiaRaster&) = delete;
    SkiaRaster& operator=(const SkiaRaster&) = delete;

    // Sizes the surface for `logical_size` at `scale`; true when it was
    // replaced. At an unchanged scale the replacement keeps the previous
    // surface's pixels in their top-left overlap and is clear elsewhere; at
    // a new scale it is clear (carried_on_resize says which).
    bool resize(Size logical_size, double scale);
    // Whether the current surface kept its predecessor's pixels when resize
    // replaced it: a host then repaints only what the resize exposed.
    [[nodiscard]] bool carried_on_resize() const noexcept;
    void begin_frame(const DamageRegion& damage);
    void end_frame();
    // Intersects the current clip with damage's rectangles, rounded outward
    // to device pixels, so drawing touches those rectangles and not the span
    // between them. Bracket with save() and restore().
    void clip_damage(const DamageRegion& damage);
    // Applies a scroll copy (ScrollCopy) to the frame begun: moves the pixels
    // inside `clip` by `offset`, both logical and whole device pixels; pixels
    // that would come from outside the clip are left as they were. Call after
    // begin_frame or begin_prepared_frame and before painting the frame's
    // damage. False, changing nothing, when no frame surface is addressable
    // or the copy is not device-aligned; the caller then repaints the clip.
    [[nodiscard]] bool scroll_copy(Rect clip, Point offset);
#if defined(GUI_FORMS_PREPARED_TEXT)
    // Guarded host transaction. begin expands damage when a full repaint is
    // required; caller paints that updated region. Null receipts and exceptions
    // must abort. end_frame only restores canvas state, never publishes pixels.
    [[nodiscard]] PreparedTextStatus begin_prepared_frame(const Size logical_size,
        const double scale, DamageRegion& damage);
    [[nodiscard]] PreparedTextStatus commit_prepared_frame(const PaintReceipt receipt);
    void abort_prepared_frame() noexcept;
    [[nodiscard]] PaintReceipt prepared_front_receipt() const noexcept;
    [[nodiscard]] double prepared_front_scale() const noexcept;
    [[nodiscard]] bool prepared_front_matches(const Size logical_size, const double scale) const noexcept;
    [[nodiscard]] PreparedTextPaintResult draw_prepared_text(const PreparedTextLayout& layout,
        const LayoutAuthority expected, const Point baseline, const Color color) override;
#endif

    [[nodiscard]] bool register_typeface(FontRole role,
                                         std::uint16_t weight,
                                         bool italic,
                                         std::span<const std::byte> encoded);
    [[nodiscard]] bool register_fallback_typeface(
        std::uint16_t weight, bool italic,
        std::span<const std::byte> encoded);
    // Registers the bundled faces in gui_forms::detail::bundled_face_uses() order from `set`, borrowing its
    // sealed bytes (no copy), and keeps the set for as long as this raster lives. A fallback
    // with a coverage table is read from the set only when text first needs it. Returns false
    // if a required face is unavailable; optional faces that fail are left out, and faces
    // registered before a failure stay registered.
    [[nodiscard]] bool register_bundled_fonts(std::shared_ptr<gui_forms::detail::BundledFontSet> set);
    // Fonts an application supplies by path. Reads the whole file into a private copy shared
    // with the text shaper; false when the path is null, the file cannot be read, is empty or
    // is larger than 64 MiB.
    [[nodiscard]] bool register_typeface_file(
        FontRole role, std::uint16_t weight, bool italic, const char* path);
    [[nodiscard]] bool register_fallback_typeface_file(
        std::uint16_t weight, bool italic, const char* path);
    [[nodiscard]] bool synchronize_images(const ImageRegistry& registry);
    [[nodiscard]] const void* pixels() const noexcept;
    [[nodiscard]] std::size_t row_bytes() const noexcept;
    [[nodiscard]] std::uint32_t pixel_width() const noexcept;
    [[nodiscard]] std::uint32_t pixel_height() const noexcept;
    [[nodiscard]] std::size_t byte_size() const noexcept;
    // Encodes the presented (front) raster as a straight-alpha PNG.
    // Returns an empty vector when no front surface exists or encoding fails.
    // Private adapter seam for host frame capture; no Skia type escapes.
    [[nodiscard]] std::vector<std::byte> encode_png() const;

    void save() override;
    void restore() override;
    void translate(Point offset) override;
    void clip_rect(Rect rect) override;
    void clip_rounded_rect(Rect rect, double radius) override;
    void clip_rounded_rect_corners(Rect rect, PaintCornerRadii radii) override;
    void fill_rect(Rect rect, Color color) override;
    void fill_rounded_rect(Rect rect, double radius, Color color) override;
    void stroke_rect(Rect rect, Color color, double width) override;
    void stroke_rounded_rect(Rect rect, double radius, Color color,
                             double width) override;
    void fill_linear_gradient(
        Rect rect, Point start, Point end,
        std::span<const GradientStop> stops) override;
    void fill_linear_gradient_spread(
        Rect rect, Point start, Point end,
        std::span<const GradientStop> stops,
        GradientSpreadMode spread) override;
    void fill_radial_gradient(
        Rect rect, Point center, Size radii,
        std::span<const GradientStop> stops) override;
    void draw_box_shadow(Rect rect, double corner_radius, Point offset,
                         double blur_radius, double spread,
                         Color color) override;
    // Blurs a retained shadow's coverage once per device scale and subpixel
    // phase, then composites that mask with the shadow color.
    void draw_retained_box_shadow(Rect rect, double corner_radius, Point offset,
                                  double blur_radius, double spread, Color color,
                                  const std::shared_ptr<RetainedDrawCache>& cache) override;
    void draw_inset_box_shadow(Rect rect, double corner_radius, Point offset,
                               double blur_radius, double spread,
                               Color color) override;
    void draw_line(Point from, Point to, Color color, double width) override;
    // The batched marks draw as one Skia path per call (markers: one path,
    // built once, placed at each centre), antialiased; strokes take the
    // style's joins, caps and dashes. fill_rects, fill_rects_each,
    // fill_checker and fill_grid_pattern keep the base implementations,
    // which draw through fill_rect and so snap every edge to device pixels.
    void stroke_polyline(std::span<const Point> points, const StrokeStyle& style) override;
    void fill_polygon(std::span<const Point> points, Color color, PolygonFillRule rule) override;
    void fill_band(std::span<const Point> upper, std::span<const Point> lower, Color color) override;
    void stroke_segments(std::span<const PaintSegment> segments, const StrokeStyle& style) override;
    void draw_markers(std::span<const Point> centers, const MarkerStyle& style) override;
    // A sweep shader from the start angle through one full turn, over the
    // snapped rectangle.
    void fill_conic_gradient(Rect rect, Point center, double start_degrees,
                             std::span<const GradientStop> stops) override;
    // As fill_rounded_rect and stroke_rounded_rect, with each corner's
    // radius; a corner of radius 0 stays square on both sides of a stroke.
    void fill_rounded_rect_corners(Rect rect, PaintCornerRadii radii, Color color) override;
    void stroke_rounded_rect_corners(Rect rect, PaintCornerRadii radii, Color color, double width) override;
    void draw_text_utf8(Point origin,
                        std::string_view text,
                        FontSpec font,
                        Color color) override;
    // Shapes a retained command once and keeps the result in its cache until
    // this raster's registered faces change.
    void draw_retained_text_utf8(Point origin, std::string_view text, FontSpec font,
                                 Color color, const std::shared_ptr<RetainedDrawCache>& cache) override;
    // Turns the canvas a quarter about `origin` and draws the run through
    // the engine's general (unhinted) placement. No coverage is retained.
    void draw_turned_text_utf8(Point origin, std::string_view text, FontSpec font, Color color,
                               TextOrientation orientation) override;
    [[nodiscard]] Size measure_text_utf8(std::string_view text,
                                         FontSpec font) override;
    [[nodiscard]] ResolvedTextLayout resolve_text_layout_utf8(
        std::string_view text, FontSpec font) override;
    [[nodiscard]] TextResolutionStatus fill_line_break_opportunities_utf8(
        std::string_view text, std::span<LineBreakOpportunity> opportunities) override;
    void draw_image(ImageId image, Rect destination, double opacity) override;
    void draw_live_surface(const std::shared_ptr<LiveSurface> surface,
                           const Rect destination, const double opacity) override;
    // The device pixels whose centres lie inside `destination`, when the
    // newest frame is opaque premultiplied BGRA, the opacity is one, the
    // transform is a scale and translation and no soft (rounded) clip is in
    // force: draw_live_surface_frame then writes each of them from the frame
    // (a copy, or a source-mode draw without antialiasing). A frame
    // published between this answer and the draw is drawn the same way
    // unless the producer reconfigured the surface as not opaque meanwhile;
    // its publication repaints the surface's area.
    [[nodiscard]] Rect opaque_live_surface_cover(const std::shared_ptr<LiveSurface>& surface,
                                                 Rect destination, double opacity) override;
    // Composites the raster into the canvas pixels with the shared blend
    // (detail::composite_part_raster) under the recorded clips, as text is
    // composited. Draws nothing when the canvas is not a plain scale and
    // translation at the raster's own scale, or its pixels cannot be
    // addressed.
    void draw_part_raster(const std::shared_ptr<const PartRaster>& raster, Point origin,
                          double opacity) override;
    // As draw_part_raster, stretched over `destination` (E7r scale motion).
    void draw_part_raster_scaled(const std::shared_ptr<const PartRaster>& raster, Rect destination,
                                 double opacity) override;
    // The material kernel's raster of `material` (material_raster.hpp),
    // baked at the canvas's scale for this draw and composited as a part
    // raster; the layers where the canvas is rotated or skewed, its two
    // scales differ, or the kernel does not bake the material.
    void draw_surface_material(Rect bounds, const SurfaceMaterial& material) override;
    // The recorded raster where it was made at the canvas's scale, else as
    // draw_surface_material.
    void draw_baked_surface_material(Rect bounds, const std::shared_ptr<const SurfaceMaterial>& material,
                                     const std::shared_ptr<const PartRaster>& raster, double outset) override;
    // Internal host seam: keep the coverage promise and sampled pixels in the
    // same read lease through rasterization, even during producer reconfigure.
    [[nodiscard]] bool draw_live_surface_frame(const LiveSurfaceFrame& frame,
                                               const Rect destination, const double opacity);
    void draw_image_region(ImageId image, Rect source, Rect destination,
                           double opacity) override;
    void draw_image_region_sampled(ImageId image, Rect source,
                                   Rect destination, ImageSampling sampling,
                                   double opacity) override;
    void fill_image_pattern(ImageId image, Size source_pixel_size,
                            Rect destination, Size logical_tile_size,
                            ImagePatternWrap wrap, double opacity) override;
    // Paint groups (Painter::begin_group). An isolated group draws into its
    // level's scratch surface (premultiplied BGRA, grown to the largest
    // clipped bounds met and reused), then composites it once into the
    // parent canvas through the shared blend at the group's opacity, under
    // the clip in force when it began, as part rasters are composited.
    void begin_group(const PaintGroup& group) override;
    void end_group() override;
    // Paint groups begun deeper than maximum_paint_group_depth.
    [[nodiscard]] std::uint64_t group_refusals() const noexcept;

private:
    friend struct SkiaLiveSurfaceTestAccess;
    friend struct SkiaCanvasTestAccess;
    class Impl;
    std::unique_ptr<Impl> impl_;
    bool fail_next_live_image_{};
    // Tests only: fill_image_pattern always samples directly, so the kept
    // device tile can be compared with it; and the covers
    // opaque_live_surface_cover has promised.
    bool direct_patterns_{};
    std::uint64_t covers_promised_{};
};

} // namespace gui_forms::render
