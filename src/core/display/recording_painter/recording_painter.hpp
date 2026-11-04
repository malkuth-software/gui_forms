#pragma once

#include "../chunk/display_chunk.hpp"

#include <memory>
#include <optional>
#include <vector>

namespace gui_forms::detail {

class RecordingPainter final : public Painter {
public:
    explicit RecordingPainter(TextMetricsProvider* text_metrics = nullptr)
        : text_metrics_(text_metrics) {}
    void save() override;
    void restore() override;
    void translate(Point offset) override;
    void clip_rect(Rect rect) override;
    void clip_rounded_rect(Rect rect, double radius) override;
    void fill_rect(Rect rect, Color color) override;
    void fill_rounded_rect(Rect rect, double radius, Color color) override;
    void stroke_rect(Rect rect, Color color, double width) override;
    void stroke_rounded_rect(Rect rect, double radius, Color color,
                             double width) override;
    void fill_linear_gradient(Rect rect, Point start, Point end,
                              std::span<const GradientStop> stops) override;
    void fill_linear_gradient_spread(
        Rect rect, Point start, Point end,
        std::span<const GradientStop> stops,
        GradientSpreadMode spread) override;
    void fill_radial_gradient(Rect rect, Point center, Size radii,
                              std::span<const GradientStop> stops) override;
    void draw_box_shadow(Rect rect, double corner_radius, Point offset,
                         double blur_radius, double spread,
                         Color color) override;
    void draw_retained_box_shadow(Rect rect, double corner_radius, Point offset,
                                  double blur_radius, double spread, Color color,
                                  const std::shared_ptr<RetainedDrawCache>& cache) override;
    void draw_inset_box_shadow(Rect rect, double corner_radius, Point offset,
                               double blur_radius, double spread,
                               Color color) override;
    void draw_line(Point from, Point to, Color color, double width) override;
    void draw_text_utf8(Point origin, std::string_view text,
                        FontSpec font, Color color) override;
    // Re-recording a retained command keeps its cache with the new copy.
    void draw_retained_text_utf8(Point origin, std::string_view text, FontSpec font,
                                 Color color, const std::shared_ptr<RetainedDrawCache>& cache) override;
    void draw_turned_text_utf8(Point origin, std::string_view text, FontSpec font, Color color,
                               TextOrientation orientation) override;
    [[nodiscard]] PreparedTextPaintResult draw_prepared_text(const PreparedTextLayout& layout,
        const LayoutAuthority expected, const Point baseline, const Color color) override;
    [[nodiscard]] Size measure_text_utf8(std::string_view text,
                                         FontSpec font) override;
    [[nodiscard]] TextResolutionStatus fill_line_break_opportunities_utf8(
        std::string_view text, std::span<LineBreakOpportunity> opportunities) override;
    [[nodiscard]] ResolvedTextLayout resolve_text_layout_utf8(
        std::string_view text, FontSpec font) override;
    void draw_image(ImageId image, Rect destination, double opacity) override;
    void draw_live_surface(std::shared_ptr<LiveSurface> surface,
                           Rect destination, double opacity) override;
    void draw_part_raster(const std::shared_ptr<const PartRaster>& raster, Point origin,
                          double opacity) override;
    void draw_part_raster_scaled(const std::shared_ptr<const PartRaster>& raster, Rect destination,
                                 double opacity) override;
    // Records the group. One begun deeper than maximum_paint_group_depth is
    // refused by name: it is recorded as save() and clip_rect(bounds), drawn
    // without isolation, and counted in group_refusals(). end_group throws
    // std::logic_error without a group to end or when the saves inside the
    // group are unbalanced; finish() throws for a group left open.
    void begin_group(const PaintGroup& group) override;
    void end_group() override;
    [[nodiscard]] std::uint64_t group_refusals() const noexcept { return group_refusals_; }
    void draw_image_region(ImageId image, Rect source, Rect destination,
                           double opacity) override;
    void draw_image_region_sampled(ImageId image, Rect source,
                                   Rect destination, ImageSampling sampling,
                                   double opacity) override;
    void fill_image_pattern(ImageId image, Size source_pixel_size,
                            Rect destination, Size logical_tile_size,
                            ImagePatternWrap wrap, double opacity) override;

    [[nodiscard]] std::shared_ptr<const DisplayChunk> finish(
        std::uint64_t generation, PaintPlane plane, Rect logical_bounds);

    // Where the current coordinate origin lies in the recording control's
    // own coordinates: the sum of the translations in force (T2 E6, so that
    // the engine knows where a part it draws lands).
    [[nodiscard]] Point origin() const noexcept { return origin_; }

    // Records that the commands so far leave `rect` opaque, whatever lay
    // beneath: an opaque solid fill of it was just drawn. The chunk keeps
    // it (DisplayChunk::opaque_cover) only if the fill was drawn untranslated
    // and unclipped, outside any save and group, and no later group replaces
    // pixels (PaintGroupComposite::copy); anything else drawn after it
    // composites over it and leaves it opaque.
    void mark_opaque_cover(Rect rect) noexcept;

    // Lets this recording keep the renderer caches of `previous`, the chunk
    // it replaces: the k-th horizontal text command recorded takes the cache
    // of the previous chunk's k-th horizontal text command when both draw
    // the same text in the same font, and the k-th box shadow takes the
    // previous k-th box shadow's cache when every parameter matches. A
    // control repainted every frame (an animation) then shapes and
    // rasterizes its unchanged text, and blurs its unchanged shadows, once
    // rather than every frame. Called before the first command; the
    // recording holds `previous` until it is destroyed. The two chunks share
    // a cache only on the window's UI thread, and the old chunk is retired
    // when this one replaces it.
    void inherit_caches(std::shared_ptr<const DisplayChunk> previous);

private:
    // The cache the command `recorded` may take from the previous chunk
    // (inherit_caches): the one of the previous command at `ordinal` in
    // `previous_indices` when it matches, else null. Advances `ordinal`.
    [[nodiscard]] std::shared_ptr<RetainedDrawCache> inherited_cache(
        const DisplayCommand& recorded, const std::vector<std::size_t>& previous_indices,
        std::size_t& ordinal) const;

    std::vector<DisplayCommand> commands_;
    // inherit_caches: the replaced chunk, the indices of its horizontal text
    // and box-shadow commands in recording order, and how many of each this
    // recording has recorded so far.
    std::shared_ptr<const DisplayChunk> previous_;
    std::vector<std::size_t> previous_text_;
    std::vector<std::size_t> previous_shadows_;
    std::size_t text_ordinal_{};
    std::size_t shadow_ordinal_{};
    std::uint64_t save_depth_{};
    Point origin_{};
    // A clip outside any save, which stays in force to the end.
    bool clipped_{};
    std::optional<Rect> opaque_cover_;
    // Each open group's kind (isolated or refused) and the save depth at
    // its beginning, innermost last; `group_depth_` counts the recorded
    // (not refused) ones.
    struct OpenGroup final {
        bool refused{};
        std::uint64_t save_depth{};
    };
    std::vector<OpenGroup> open_groups_;
    std::size_t group_depth_{};
    std::uint64_t group_refusals_{};
    // The origin at each save in force, restored by its restore.
    std::vector<Point> saved_origins_;
    TextMetricsProvider* text_metrics_{};
    std::optional<PreparedTextStatus> prepared_failure_{};
};

} // namespace gui_forms::detail
