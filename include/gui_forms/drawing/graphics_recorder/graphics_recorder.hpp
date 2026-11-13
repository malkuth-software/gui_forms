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
    // draw_string: true when the text is laid out in `rect`
    // (draw_string_in_rect) rather than placed at `first`.
    bool laid_out{};
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
    // The clip rules (the flame graph demonstrator's D5). set_clip replaces
    // the clip in force; it does not intersect with it. The clip is the
    // axis-aligned bounds of `clip` under the transform in force when it is
    // set, in target coordinates, so a later transform does not move it.
    // reset_clip removes it. save records it with the rest of the state and
    // restore brings back the one in force at that save. Every recorded
    // command carries the clip in force (DrawingCommand::state), and an
    // executor draws it within that rectangle only. A program that wants an
    // intersection computes it.
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
    // The string laid out in `layout` (gd_* ABI 0.5, the flame graph
    // demonstrator's D2). Lines break at newlines only. Each line is placed
    // across the box by the format's alignment (near, center, far) and the
    // block of lines down it by its line alignment, with the same side
    // bearing an origin-placed string has. A line wider than the box is
    // trimmed by the format's trimming: character and word cut it at the
    // last code point or word that fits, the ellipsis forms add U+2026 after
    // the cut, and ellipsis_path is taken as ellipsis_character; with none
    // it runs on. The text is clipped to the box (within the clip in force)
    // unless the format has the no-clip flag (0x4000). `layout` is finite
    // with a non-negative width and height. Records one draw_string.
    void draw_string_in_rect(std::string_view utf8, const Font& font, const SolidBrush& brush, RectF layout,
                             const StringFormat& format);
    // One string of draw_strings: its text (lent for the call), its origin,
    // and its colour.
    struct GUI_FORMS_API TextItem final {
        std::string_view text;
        PointF origin{};
        Color color;
    };
    static constexpr std::size_t maximum_text_items = 65536;
    // Strings in one font and format, each in its own colour at its own
    // origin (gd_* ABI 0.5, the flame graph demonstrator's D3): one
    // draw_string per item, in order. 1 through maximum_text_items items,
    // each bounded valid UTF-8 at a finite origin; the whole list is
    // validated, and that its commands fit maximum_commands, before
    // anything is recorded.
    void draw_strings(const Font& font, const StringFormat& format, std::span<const TextItem> items);
    // The string as draw_string draws it at `origin`, turned about `origin`
    // by `degrees` (finite; positive turns anticlockwise as seen). One
    // draw_string command whose recorded transform is the turn followed by
    // the transform in force, which stays in force; 0 degrees records what
    // draw_string records. Throws, recording nothing, as draw_string does or
    // for a non-finite angle.
    void draw_string_rotated(std::string_view utf8, const Font& font,
                             const SolidBrush& brush, PointF origin, double degrees,
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

    // Gapped and per-element marks (gd_* ABI 0.4, the plot demonstrator's
    // gaps G2 to G4). Each records commands of the kinds above, so replay
    // and the trace need no new command kind. Each validates its whole
    // input, and that the commands it records fit maximum_commands, before
    // it records anything; a refusal records nothing.
    //
    // A break is a point with a NaN coordinate. An infinite coordinate is
    // refused, as for the batched commands.
    //
    // The most samples fill_band takes: its polygon holds two points per
    // sample, within GraphicsPath::maximum_elements.
    static constexpr std::size_t maximum_band_samples = maximum_batch_elements / 2U;
    // The area between two polylines sampled at the same positions (the
    // Painter's fill_band): `upper` and `lower` hold the same count, 2
    // through maximum_band_samples. A break at index i in either span
    // breaks the band there. Each run of 2 or more unbroken indices records
    // one fill_polygon (winding, along `upper` and back along `lower`); a
    // run of one index records nothing.
    void fill_band(const Brush& brush, std::span<const PointF> upper, std::span<const PointF> lower);
    // An open polyline through `points` (2 or more) that breaks at each
    // break point (the Painter's stroke_polyline): each run of 2 or more
    // unbroken points records one draw_lines, and a run of one records
    // nothing.
    void draw_polyline_runs(const Pen& pen, std::span<const PointF> points);
    // Each of 1 or more finite rectangles filled in its own colour (the
    // Painter's fill_rects_each): `colors` holds one per rectangle. Each
    // run of consecutive equal colours records one fill_rectangles with a
    // solid brush of that colour.
    void fill_rectangles_each(std::span<const RectF> rects, std::span<const Color> colors);

    // Ends recording: every recording call afterwards throws
    // std::logic_error (the C ABI returns an error and records nothing),
    // until reset.
    void close();
    [[nodiscard]] bool closed() const;
    // Empties the recording for reuse (gd_* ABI 0.5, the flame graph
    // demonstrator's D4): the commands, the saved states (their tokens are
    // no longer valid) and the state in force (no transform, no clip,
    // default quality) are cleared and the recorder reopens. The command
    // storage is kept for the next frame. Throws on a disposed recorder.
    void reset();
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
