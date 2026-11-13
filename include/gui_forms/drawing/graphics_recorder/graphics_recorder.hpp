#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/drawing/bitmap/bitmap.hpp"
#include "gui_forms/drawing/brush/brush.hpp"
#include "gui_forms/drawing/font/font.hpp"
#include "gui_forms/drawing/graphics_path/graphics_path.hpp"
#include "gui_forms/drawing/image_attributes/image_attributes.hpp"
#include "gui_forms/drawing/image_reference/image_reference.hpp"
#include "gui_forms/drawing/pen/pen.hpp"
#include "gui_forms/drawing/string_format/string_format.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_drawing {

struct GUI_FORMS_API GraphicsState final {
    Matrix transform;
    std::optional<RectF> clip;
    SmoothingMode smoothing{SmoothingMode::default_mode};
    InterpolationMode interpolation{InterpolationMode::default_mode};
    PixelOffsetMode pixel_offset{PixelOffsetMode::default_mode};
    CompositingMode compositing{CompositingMode::source_over};
    CompositingQuality compositing_quality{CompositingQuality::default_mode};
};

struct GUI_FORMS_API GraphicsStateToken final {
    std::uint64_t value{};
    friend constexpr bool operator==(const GraphicsStateToken& left,
                                     const GraphicsStateToken& right) noexcept {
        return left.value == right.value;
    }
};

enum class CommandKind : std::uint8_t {
    save,
    restore,
    translate,
    set_transform,
    set_clip,
    reset_clip,
    set_quality,
    clear,
    fill_rectangle,
    draw_rectangle,
    draw_line,
    draw_string,
    draw_ellipse,
    fill_ellipse,
    fill_polygon,
    draw_path,
    fill_path,
    draw_image,
    // The batched commands (appended; their fields are in DrawingCommand).
    draw_lines,
    fill_rectangles,
    draw_line_segments,
    draw_markers,
    fill_grid_pattern,
    fill_proportion,
};

struct GUI_FORMS_API DrawingCommand final {
    CommandKind kind{};
    GraphicsState state;
    GraphicsStateToken token;
    PointF first{};
    PointF second{};
    RectF rect{};
    Color color;
    PenSnapshot pen;
    FontSnapshot font;
    StringFormatSnapshot format;
    PathSnapshot path;
    ImageSnapshot image;
    ImageAttributesSnapshot image_attributes;
    BrushSnapshot brush;
    std::vector<PointF> points;
    std::string text;
    // fill_rectangles: the rectangles.
    std::vector<RectF> rects;
    // draw_markers: the style (the centres are in `points`).
    MarkerStyle marker;
    // fill_grid_pattern: the grid (the area is `rect`).
    GridPatternStyle grid;
    // fill_proportion: the track's corner radii, the proportion in
    // `scalar` and the edge it grows from (the track is `rect`).
    CornerRadii radii;
    double scalar{};
    FillDirection direction{FillDirection::left_to_right};
};

class GUI_FORMS_API TextMetricsProvider {
public:
    virtual ~TextMetricsProvider() = default;
    [[nodiscard]] virtual SizeF measure(std::string_view utf8,
                                        const FontSnapshot& font,
                                        const StringFormatSnapshot& format) const = 0;
};

class GUI_FORMS_API GraphicsRecorder final : public DrawingObject {
public:
    static constexpr std::size_t maximum_commands = 1'000'000;
    static constexpr std::size_t maximum_state_depth = 256;
    static constexpr std::size_t maximum_text_bytes = 1U << 20U;

    GraphicsRecorder() = default;

    [[nodiscard]] GraphicsStateToken save();
    void restore(GraphicsStateToken token);
    void translate(double x, double y);
    void set_transform(Matrix transform);
    void set_clip(RectF clip);
    void reset_clip();
    void set_quality(SmoothingMode smoothing,
                     InterpolationMode interpolation,
                     PixelOffsetMode pixel_offset,
                     CompositingMode compositing,
                     CompositingQuality compositing_quality);

    [[nodiscard]] GraphicsState current_state() const;
    [[nodiscard]] bool is_visible(PointF point) const;
    [[nodiscard]] SizeF measure_string(std::string_view utf8, const Font& font,
                                       const StringFormat& format,
                                       const TextMetricsProvider& provider) const;

    void clear(Color color);
    void fill_rectangle(const Brush& brush, RectF rect);
    void draw_rectangle(const Pen& pen, RectF rect);
    void draw_line(const Pen& pen, PointF from, PointF to);
    void draw_string(std::string_view utf8, const Font& font,
                     const SolidBrush& brush, PointF origin,
                     const StringFormat& format);
    void draw_ellipse(const Pen& pen, RectF bounds);
    void fill_ellipse(const Brush& brush, RectF bounds);
    void fill_polygon(const Brush& brush, std::span<const PointF> points,
                      FillMode fill_mode = FillMode::alternate);
    void draw_path(const Pen& pen, const GraphicsPath& path);
    void fill_path(const Brush& brush, const GraphicsPath& path);
    void draw_image(const ImageReference& image, RectF destination,
                    RectF source, const ImageAttributes& attributes);
    void draw_image(const Bitmap& image, RectF destination,
                    RectF source, const ImageAttributes& attributes);

    // Batched commands (the GUI.Forms Painter's batch calls; one command
    // per call, the span copied once). Every point and rectangle must be
    // finite, and each span holds at most maximum_batch_elements.
    static constexpr std::size_t maximum_batch_elements = 1'000'000;
    // An open polyline through 2 or more points, with the pen's joins, caps
    // and dashes.
    void draw_lines(const Pen& pen, std::span<const PointF> points);
    // Each of 1 or more rectangles filled with the brush.
    void fill_rectangles(const Brush& brush, std::span<const RectF> rects);
    // Independent segments, each a pair of points (an even count, 2 or
    // more), with the pen's caps and dashes and no joins.
    void draw_line_segments(const Pen& pen, std::span<const PointF> points);
    // A marker centred on each of 1 or more points. The style's size is
    // finite and positive, its edge width finite and non-negative.
    void draw_markers(std::span<const PointF> centers, const MarkerStyle& style);
    // The grid lines that cross `rect`, clipped to it (the blueprint
    // background, piece C17). The spacing is at least 2 units, majors fall
    // every 1 or more lines, the width is finite and positive, and neither
    // axis may cross more than 65536 lines.
    void fill_grid_pattern(RectF rect, const GridPatternStyle& style);
    // Piece C3: the part of `track` a fill of `proportion` (clamped to
    // [0, 1]) covers from `direction`'s edge, filled with the brush and
    // clipped to the track's rounded shape (`radii`, finite and
    // non-negative).
    void fill_proportion(const Brush& brush, RectF track, CornerRadii radii, double proportion,
                         FillDirection direction);
    // Piece C4: the string drawn twice under complementary clips, with
    // `on_fill_brush` where it lies over `fill` and `brush` elsewhere. It
    // records up to five draw_string commands, each with its own clip
    // within the current one: the fill's part, and the bands above, below,
    // left and right of it, which share its edges exactly. The transform
    // must be a scale and translation (the clips are rectangles).
    void draw_string_over_fill(std::string_view utf8, const Font& font, const SolidBrush& brush,
                               const SolidBrush& on_fill_brush, PointF origin, const StringFormat& format,
                               RectF fill);

    void close();
    [[nodiscard]] bool closed() const;
    [[nodiscard]] std::span<const DrawingCommand> commands() const;
    [[nodiscard]] std::string deterministic_trace() const;

protected:
    void on_dispose() noexcept override;

private:
    struct SavedState final {
        GraphicsStateToken token;
        GraphicsState state;
    };

    void require_recordable() const;
    void append(DrawingCommand command);

    GraphicsState state_;
    std::vector<SavedState> saved_;
    std::vector<DrawingCommand> commands_;
    std::uint64_t next_token_{1};
    bool closed_{};
};

constexpr FontStyle operator|(FontStyle left, FontStyle right) noexcept {
    return static_cast<FontStyle>(static_cast<std::uint32_t>(left) |
                                  static_cast<std::uint32_t>(right));
}

} // namespace gui_drawing
