#pragma once

// The code-zoom demonstrator's sheet (owner, 2026-11-22): a dark editor
// page showing one C++ source, highlighted by the C++ language
// (cpp_language.hpp), zoomed by the ZoomModel (zoom_model.hpp).
//
// Drawing. Every paint lays the visible text out at the zoom's true size:
// one monospace FontSpec of base_font_size * zoom, its advance measured
// at that size, and the visible runs handed to Painter::draw_text_runs,
// which shapes and rasterizes each through GUI.Drawing's text engine over
// STXlib's glyphs at that size and the device scale (never a stretched
// bitmap). What the sheet keeps between paints is what does not depend on
// the size: the source lexed once at load, and its runs (load_source):
// adjacent tokens of one colour merged across spaces, each with its byte
// range, its first column (tabs expanded to tab_columns) and its glyph
// count. Per paint it only culls: the visible lines by arithmetic, and in
// each line the visible runs by two binary searches over their columns.
// The renderer keeps what is cacheable per size (shaped runs, glyph
// coverage).
//
// Zooming. +, = and the keypad's + glide in by zoom_step, - out, 0 back to
// 1 (about the sheet's centre); Ctrl+wheel or Cmd+wheel glides by a step a
// notch about the pointer, or, from a precise device (a trackpad, a
// precision touchpad's pinch, which Windows reports as Ctrl+wheel), zooms
// at once by exp(delta * pixel_zoom_rate). A glide runs on an
// AnimationTimer only while it lasts; reduced motion lands at once. The
// wheel alone, the arrows, Page Up/Down, Home and End scroll.
//
// The readout (F toggles it): in the top-right corner, the milliseconds the
// host spent on each frame (WindowMetrics' present durations, paint and
// raster, since the readout last updated), the paint rate over the last
// second, the glyphs the last paint drew, and the zoom.

#include "code_highlight.hpp"
#include "zoom_model.hpp"

#include "gui_forms/animation/animation_timer/animation_timer.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/events/key_events/key_events.hpp"
#include "gui_forms/events/mouse_events/mouse_events.hpp"
#include "gui_forms/types/painter/painter.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms::code_zoom {

inline constexpr double base_font_size = 13.0;
inline constexpr double base_line_height = 18.0;
// The baseline's depth in a line, as a share of the line's height.
inline constexpr double baseline_share = 0.74;
inline constexpr std::uint32_t tab_columns = 4U;
// The line-number gutter: five digits and a space before the text.
inline constexpr std::uint32_t gutter_columns = 7U;
// Logical pixels a wheel line scrolls, and the zoom factor's exponent per
// logical pixel of a precise zoom.
inline constexpr double wheel_line_pixels = 54.0;
inline constexpr double pixel_zoom_rate = 0.005;

// The colours runs take (code_sheet.cpp's palette gives each one).
enum class SheetColour : std::uint8_t {
    text,
    punctuation,
    comment,
    string,
    character,
    number,
    keyword,
    type,
    preprocessor,
    attribute,
    invalid,
    gutter,
};
inline constexpr std::size_t sheet_colour_count = 12U;

// The colour a token kind draws in.
[[nodiscard]] SheetColour colour_of(code_highlight::TokenKind kind) noexcept;

// One run: the sheet text's bytes [offset, offset + length) drawn in one
// colour from `column` (gutter included), covering `columns` columns, with
// `glyphs` code points that are not spaces.
struct SheetRun final {
    std::uint32_t offset{};
    std::uint32_t length{};
    std::uint32_t column{};
    std::uint32_t columns{};
    std::uint32_t glyphs{};
    SheetColour colour{SheetColour::text};
};

// A line's runs: [first_run, first_run + run_count) of the sheet's runs, in
// increasing column order without overlap. The line number is the first.
struct SheetLine final {
    std::uint32_t first_run{};
    std::uint32_t run_count{};
};

class CodeSheet final : public Control {
public:
    explicit CodeSheet(StableId stable_id);

    // Replaces the sheet's text with `source` (copied), named `name` (shown
    // nowhere yet but kept for the bench), lexed as C++. Lines end at LF; a
    // CR before it is dropped. False, leaving the sheet as it was, when the
    // source is larger than a run's 32-bit offsets can address.
    bool load_source(std::string_view name, std::string_view source);

    [[nodiscard]] std::string_view source_name() const& noexcept { return name_; }
    std::string_view source_name() const&& = delete;
    [[nodiscard]] std::size_t line_count() const noexcept { return lines_.size(); }
    [[nodiscard]] std::span<const SheetLine> lines() const& noexcept { return lines_; }
    std::span<const SheetLine> lines() const&& = delete;
    [[nodiscard]] std::span<const SheetRun> runs() const& noexcept { return runs_; }
    std::span<const SheetRun> runs() const&& = delete;
    // A run's text.
    [[nodiscard]] std::string_view run_text(const SheetRun& run) const& noexcept;
    std::string_view run_text(const SheetRun& run) const&& = delete;
    [[nodiscard]] const ZoomModel& zoom_model() const noexcept { return model_; }
    // The glyphs the last paint drew (the line numbers' included).
    [[nodiscard]] std::uint64_t glyphs_drawn() const noexcept { return glyphs_drawn_; }
    [[nodiscard]] bool readout_shown() const noexcept { return readout_shown_; }
    void set_readout_shown(bool shown);

    // Shows content point `content` (logical pixels at zoom 1) at the
    // sheet's point `screen` at `zoom`, at once (staging, the bench).
    void show_at(double zoom, Point content, Point screen);
    // The content point of the start of `line`'s `column` (zero-based).
    [[nodiscard]] Point content_point(std::size_t line, std::uint32_t column) const noexcept;

    // The window's key_down (make_code_zoom subscribes it): the zoom,
    // readout and scrolling keys, whatever has the focus.
    void window_key_down(KeyEventArgs& arguments);

    void on_paint(Painter& painter, Rect local_damage) override;
    [[nodiscard]] bool paint_depends_on_size() const noexcept override { return true; }
    void on_mouse_wheel(MouseEventArgs& arguments) override;

protected:
    void on_detached_from_window() noexcept override;

private:
    // The glide's frames: observes the sheet, so a destroyed sheet's tick
    // is skipped.
    struct GlideTick final {
        std::weak_ptr<CodeSheet> sheet;
        AnimationTickResult operator()(FrameTime now) const;
    };

    // Paint rates and present durations for the readout.
    static constexpr std::size_t kept_paint_times = 64U;
    struct FrameStatistics final {
        std::array<FrameTime, kept_paint_times> paints{};
        std::size_t paint_count{};
        std::size_t next_paint{};
        std::uint64_t frames_presented{};
        std::uint64_t present_nanoseconds{};
        double milliseconds{};
        bool measured{};
    };

    // Gives the model the content's and the sheet's sizes.
    void sync_extent();
    [[nodiscard]] Size content_size() const noexcept;
    [[nodiscard]] Point centre() const noexcept;
    void glide_by(double factor, Point anchor);
    void glide_to(double zoom, Point anchor);
    void begin_glide();
    void scroll_screen(Point screen_delta);
    AnimationTickResult advance(FrameTime now);
    [[nodiscard]] bool reduced_motion() const noexcept;
    // The monospace advance at `font`'s size, measured once per size.
    [[nodiscard]] double advance_at(Painter& painter, FontSpec font);
    void note_paint(FrameTime now);
    [[nodiscard]] double paints_per_second() const noexcept;
    void paint_readout(Painter& painter, Rect client);

    std::string name_{};
    // The source, then the line numbers' digits.
    std::string text_{};
    std::vector<SheetLine> lines_{};
    std::vector<SheetRun> runs_{};
    std::uint32_t widest_columns_{};
    ZoomModel model_{};
    AnimationTimer glide_timer_;
    // The advance at base_font_size (content widths) and at the size last
    // measured.
    double base_advance_{base_font_size * 0.6};
    bool base_measured_{};
    double measured_size_{};
    double measured_advance_{};
    // The paint's runs, refilled every paint (their capacity is kept).
    std::vector<TextRun> text_runs_{};
    std::uint64_t glyphs_drawn_{};
    bool readout_shown_{true};
    FrameStatistics statistics_{};
    std::array<char, 128> readout_text_{};
};

} // namespace gui_forms::code_zoom
