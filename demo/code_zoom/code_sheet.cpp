#include "code_sheet.hpp"

#include "cpp_language.hpp"

#include "gui_forms/dirty.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/metrics/types/metrics_types.hpp"
#include "gui_forms/window/window.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <utility>

namespace gui_forms::code_zoom {
namespace {

// -- the program's visual choices ---------------------------------------------------------

constexpr Color sheet_background = Color::rgba(24, 26, 31);
constexpr Color readout_background = Color::rgba(8, 9, 12, 214);
constexpr Color readout_ink = Color::rgba(232, 236, 242);
constexpr double readout_font_size = 12.0;
constexpr double readout_margin = 10.0;
constexpr double readout_padding = 7.0;

// In SheetColour's order.
constexpr std::array<Color, sheet_colour_count> palette{
    Color::rgba(205, 211, 222), // text
    Color::rgba(137, 148, 166), // punctuation
    Color::rgba(112, 130, 114), // comment
    Color::rgba(152, 195, 121), // string
    Color::rgba(86, 182, 194),  // character
    Color::rgba(209, 154, 102), // number
    Color::rgba(198, 120, 221), // keyword
    Color::rgba(229, 192, 123), // type
    Color::rgba(224, 108, 117), // preprocessor
    Color::rgba(97, 175, 239),  // attribute
    Color::rgba(255, 85, 85),   // invalid
    Color::rgba(78, 86, 102),   // gutter
};

// HID usages PhysicalKey does not name: the main row's 0 and the keypad's.
constexpr std::uint32_t digit_zero_key = 0x27U;
constexpr std::uint32_t keypad_zero_key = 0x62U;

// The glide's timer interval. A stopgap until the library has a
// display-paced frame source (hosts reporting the refresh period and each
// frame's presentation time to the window): AnimationTimer ticks only once
// the sheet has been painted since its last damaging tick, so at 1 ms the
// glide advances once per paint, at the host's paint cadence (the display's
// on macOS, where AppKit coalesces drawing to the refresh), rather than on
// display_frame_interval's fixed 16 ms. The easing is evaluated at the
// tick's time, not the frame's presentation time.
constexpr FrameInterval glide_tick_interval = std::chrono::milliseconds(1);

constexpr std::uint32_t maximum_source_bytes = std::numeric_limits<std::uint32_t>::max() / 2U;

// -- loading ------------------------------------------------------------------------------

// Builds one line's runs from its pieces (tokens and the plain gaps between
// them), in order: a glyph extends the open run when it has the run's
// colour (across spaces), else starts a run; a tab ends the open run and
// moves to the next tab stop.
class RunBuilder final {
public:
    RunBuilder(std::vector<SheetRun>& runs, std::uint32_t line_offset) noexcept
        : runs_(runs), line_offset_(line_offset) {}

    void add(std::string_view line, std::size_t start, std::size_t end, SheetColour colour);
    void close();
    [[nodiscard]] std::uint32_t column() const noexcept { return column_; }

private:
    std::vector<SheetRun>& runs_;
    std::uint32_t line_offset_{};
    std::uint32_t column_{gutter_columns};
    bool open_{};
    SheetRun run_{};
};

void RunBuilder::add(const std::string_view line, const std::size_t start, const std::size_t end,
                     const SheetColour colour) {
    for (std::size_t index = start; index < end; ++index) {
        const unsigned char byte = static_cast<unsigned char>(line[index]);
        const std::uint32_t offset = line_offset_ + static_cast<std::uint32_t>(index);
        if (byte == '\t') {
            close();
            const std::uint32_t text_column = column_ - gutter_columns;
            column_ = gutter_columns + (text_column / tab_columns + 1U) * tab_columns;
            continue;
        }
        if (byte == ' ') {
            column_ += 1U;
            continue;
        }
        if ((byte & 0xC0U) == 0x80U) {
            // A continuation byte belongs to the code point the run holds.
            if (open_) run_.length = offset + 1U - run_.offset;
            continue;
        }
        if (open_ && run_.colour == colour) {
            run_.length = offset + 1U - run_.offset;
        } else {
            close();
            open_ = true;
            run_ = SheetRun{offset, 1U, column_, 0U, 0U, colour};
        }
        column_ += 1U;
        run_.glyphs += 1U;
        run_.columns = column_ - run_.column;
    }
}

void RunBuilder::close() {
    if (open_) runs_.push_back(run_);
    open_ = false;
}

// The decimal digits of `number` appended to `digits`; their count.
std::uint32_t append_digits(std::string& digits, std::size_t number) {
    std::array<char, 24> buffer{};
    const int written = std::snprintf(buffer.data(), buffer.size(), "%zu", number);
    const std::size_t count = written > 0 ? static_cast<std::size_t>(written) : 0U;
    digits.append(buffer.data(), count);
    return static_cast<std::uint32_t>(count);
}

// -- culling ------------------------------------------------------------------------------

// A line's runs are in increasing column order without overlap, so both
// predicates partition them.
struct RunEndsBefore final {
    double column{};
    bool operator()(const SheetRun& run) const noexcept {
        return static_cast<double>(run.column + run.columns) <= column;
    }
};

struct RunStartsBefore final {
    double column{};
    bool operator()(const SheetRun& run) const noexcept { return static_cast<double>(run.column) < column; }
};

[[nodiscard]] double seconds_of(const FrameTime time) noexcept {
    const double seconds = std::chrono::duration<double>(time.time_since_epoch()).count();
    return seconds;
}

} // namespace

SheetColour colour_of(const code_highlight::TokenKind kind) noexcept {
    switch (kind) {
    case code_highlight::TokenKind::comment: return SheetColour::comment;
    case code_highlight::TokenKind::string: return SheetColour::string;
    case code_highlight::TokenKind::character: return SheetColour::character;
    case code_highlight::TokenKind::number: return SheetColour::number;
    case code_highlight::TokenKind::keyword: return SheetColour::keyword;
    case code_highlight::TokenKind::type: return SheetColour::type;
    case code_highlight::TokenKind::preprocessor: return SheetColour::preprocessor;
    case code_highlight::TokenKind::attribute: return SheetColour::attribute;
    case code_highlight::TokenKind::punctuation: return SheetColour::punctuation;
    case code_highlight::TokenKind::operator_symbol: return SheetColour::punctuation;
    case code_highlight::TokenKind::invalid: return SheetColour::invalid;
    default: return SheetColour::text;
    }
}

CodeSheet::CodeSheet(StableId stable_id) : Control(std::move(stable_id)) {}

bool CodeSheet::load_source(const std::string_view name, const std::string_view source) {
    if (source.size() > maximum_source_bytes) return false;
    const code_highlight::CppLanguage& language = code_highlight::cpp_language();
    std::vector<SheetLine> lines{};
    std::vector<SheetRun> runs{};
    std::string digits{};
    std::vector<code_highlight::LineToken> tokens{};
    runs.reserve(source.size() / 4U + 16U);
    code_highlight::LexState state = language.initial_state();
    std::uint32_t widest = 0U;
    std::size_t line_start = 0U;
    while (line_start <= source.size()) {
        const std::size_t newline = source.find('\n', line_start);
        const std::size_t line_end = newline == std::string_view::npos ? source.size() : newline;
        std::string_view line = source.substr(line_start, line_end - line_start);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1U);
        // A final newline ends the last line; it starts none.
        if (newline == std::string_view::npos && line_start == source.size() && line_start != 0U) break;
        tokens.clear();
        state = language.lex_line(line, state, tokens);
        SheetLine sheet_line{};
        sheet_line.first_run = static_cast<std::uint32_t>(runs.size());
        // The line number, right-aligned before the gutter's space.
        const std::uint32_t digits_offset = static_cast<std::uint32_t>(source.size() + digits.size());
        const std::uint32_t digit_count = append_digits(digits, lines.size() + 1U);
        const std::uint32_t number_column = digit_count + 2U > gutter_columns ? 0U : gutter_columns - 2U - digit_count;
        const SheetRun number{digits_offset, digit_count, number_column, digit_count, digit_count, SheetColour::gutter};
        runs.push_back(number);
        RunBuilder builder(runs, static_cast<std::uint32_t>(line_start));
        std::size_t covered = 0U;
        for (const code_highlight::LineToken& token : tokens) {
            builder.add(line, covered, token.column, SheetColour::text);
            builder.add(line, token.column, token.column + token.length, colour_of(token.kind));
            covered = token.column + token.length;
        }
        builder.add(line, covered, line.size(), SheetColour::text);
        builder.close();
        widest = std::max(widest, builder.column());
        sheet_line.run_count = static_cast<std::uint32_t>(runs.size()) - sheet_line.first_run;
        lines.push_back(sheet_line);
        if (newline == std::string_view::npos) break;
        line_start = newline + 1U;
    }
    std::string text(source);
    text.append(digits);
    name_ = std::string(name);
    text_ = std::move(text);
    lines_ = std::move(lines);
    runs_ = std::move(runs);
    widest_columns_ = widest;
    sync_extent();
    invalidate(Dirty::paint);
    return true;
}

std::string_view CodeSheet::run_text(const SheetRun& run) const& noexcept {
    const std::string_view text = std::string_view(text_).substr(run.offset, run.length);
    return text;
}

void CodeSheet::set_readout_shown(const bool shown) {
    if (readout_shown_ == shown) return;
    readout_shown_ = shown;
    invalidate(Dirty::paint);
}

Size CodeSheet::content_size() const noexcept {
    const Size size{static_cast<double>(widest_columns_ + 1U) * base_advance_,
                    static_cast<double>(lines_.size() + 1U) * base_line_height};
    return size;
}

Point CodeSheet::centre() const noexcept {
    const Rect client = client_rectangle();
    const Point point{client.width * 0.5, client.height * 0.5};
    return point;
}

Point CodeSheet::content_point(const std::size_t line, const std::uint32_t column) const noexcept {
    const Point point{static_cast<double>(gutter_columns + column) * base_advance_,
                      static_cast<double>(line) * base_line_height};
    return point;
}

void CodeSheet::sync_extent() {
    const Rect client = client_rectangle();
    model_.set_extent(content_size(), Size{client.width, client.height});
}

void CodeSheet::show_at(const double zoom, const Point content, const Point screen) {
    glide_timer_.stop();
    sync_extent();
    model_.show(zoom, content, screen);
    invalidate(Dirty::paint);
}

void CodeSheet::glide_by(const double factor, const Point anchor) {
    sync_extent();
    model_.glide_by(factor, anchor);
    begin_glide();
}

void CodeSheet::glide_to(const double zoom, const Point anchor) {
    sync_extent();
    model_.glide_to(zoom, anchor);
    begin_glide();
}

void CodeSheet::begin_glide() {
    invalidate(Dirty::paint);
    if (reduced_motion() || attached_window() == nullptr) {
        glide_timer_.stop();
        model_.finish();
        return;
    }
    if (glide_timer_.started()) return;
    const std::shared_ptr<CodeSheet> self = std::dynamic_pointer_cast<CodeSheet>(shared_from_this());
    if (!self) return;
    glide_timer_.start(*this, glide_tick_interval, GlideTick{self});
}

void CodeSheet::scroll_screen(const Point screen_delta) {
    sync_extent();
    const double zoom = model_.zoom();
    model_.scroll_by(Point{screen_delta.x / zoom, screen_delta.y / zoom});
    invalidate(Dirty::paint);
}

AnimationTickResult CodeSheet::advance(const FrameTime now) {
    const bool going = model_.advance(seconds_of(now));
    invalidate(Dirty::paint);
    if (!going) glide_timer_.stop();
    return AnimationTickResult::damaged;
}

AnimationTickResult CodeSheet::GlideTick::operator()(const FrameTime now) const {
    const std::shared_ptr<CodeSheet> live = sheet.lock();
    if (!live) return AnimationTickResult::unchanged;
    const AnimationTickResult result = (*live).advance(now);
    return result;
}

bool CodeSheet::reduced_motion() const noexcept {
    const Window* const owner = attached_window();
    const bool reduced = owner != nullptr && (*owner).presentation_settings().reduced_motion;
    return reduced;
}

void CodeSheet::on_detached_from_window() noexcept {
    Control::on_detached_from_window();
    glide_timer_.stop();
    model_.finish();
}

void CodeSheet::window_key_down(KeyEventArgs& arguments) {
    if (arguments.handled || arguments.released_by_deactivation) return;
    const std::uint32_t key = arguments.physical_key;
    const Rect client = client_rectangle();
    const double line_step = base_line_height * model_.zoom() * 3.0;
    if (key == PhysicalKey::equal || key == PhysicalKey::keypad_plus) {
        glide_by(zoom_step, centre());
    } else if (key == PhysicalKey::minus || key == PhysicalKey::keypad_minus) {
        glide_by(1.0 / zoom_step, centre());
    } else if (key == digit_zero_key || key == keypad_zero_key) {
        glide_to(1.0, centre());
    } else if (key == PhysicalKey::f && arguments.modifiers == Modifier::none) {
        set_readout_shown(!readout_shown_);
    } else if (key == PhysicalKey::up) {
        scroll_screen(Point{0.0, -line_step});
    } else if (key == PhysicalKey::down) {
        scroll_screen(Point{0.0, line_step});
    } else if (key == PhysicalKey::page_up) {
        scroll_screen(Point{0.0, -client.height * 0.9});
    } else if (key == PhysicalKey::page_down) {
        scroll_screen(Point{0.0, client.height * 0.9});
    } else if (key == PhysicalKey::home) {
        scroll_screen(Point{0.0, -content_size().height * model_.zoom()});
    } else if (key == PhysicalKey::end) {
        scroll_screen(Point{0.0, content_size().height * model_.zoom()});
    } else {
        return;
    }
    arguments.handled = true;
}

void CodeSheet::on_mouse_wheel(MouseEventArgs& arguments) {
    const bool zooming =
        has_modifier(arguments.modifiers, Modifier::control) || has_modifier(arguments.modifiers, Modifier::meta);
    const bool precise = arguments.wheel_unit == WheelDeltaUnit::pixels;
    if (zooming && precise) {
        glide_timer_.stop();
        sync_extent();
        const double factor = std::exp(arguments.wheel_delta.y * pixel_zoom_rate);
        model_.zoom_at(model_.zoom() * factor, arguments.location);
        invalidate(Dirty::paint);
    } else if (zooming) {
        glide_by(std::pow(zoom_step, arguments.wheel_delta.y), arguments.location);
    } else {
        // A positive delta scrolls toward the top (and the left).
        const double unit = precise ? 1.0 : wheel_line_pixels;
        scroll_screen(Point{-arguments.wheel_delta.x * unit, -arguments.wheel_delta.y * unit});
    }
    arguments.handled = true;
    Control::on_mouse_wheel(arguments);
}

double CodeSheet::advance_at(Painter& painter, const FontSpec font) {
    // Ten advances, so the measure's rounding is a tenth of a column's.
    constexpr std::string_view sample = "0000000000";
    if (!base_measured_) {
        FontSpec base = font;
        base.size = base_font_size;
        const Size measured = painter.measure_text_utf8(sample, base);
        if (measured.width > 0.0) base_advance_ = measured.width / 10.0;
        base_measured_ = true;
        sync_extent();
    }
    if (measured_size_ != font.size) {
        const Size measured = painter.measure_text_utf8(sample, font);
        measured_size_ = font.size;
        measured_advance_ = measured.width > 0.0 ? measured.width / 10.0 : base_advance_ * font.size / base_font_size;
    }
    return measured_advance_;
}

void CodeSheet::on_paint(Painter& painter, const Rect) {
    glide_timer_.note_painted();
    note_paint(FrameClock::now());
    const Rect client = client_rectangle();
    painter.fill_rect(client, sheet_background);
    FontSpec font{};
    font.role = FontRole::monospace;
    font.size = base_font_size * model_.zoom();
    const double advance = advance_at(painter, font);
    sync_extent();
    const double zoom = model_.zoom();
    const double line_height = base_line_height * zoom;
    const Point origin = model_.origin();
    // The view in columns and lines: the first visible column and line may
    // be fractional or negative (content smaller than the sheet).
    const double left_column = origin.x / base_advance_;
    const double top_line = origin.y / base_line_height;
    const double right_column = left_column + client.width / advance + 1.0;
    const double line_total = static_cast<double>(lines_.size());
    const std::size_t first_line = static_cast<std::size_t>(std::clamp(std::floor(top_line), 0.0, line_total));
    const std::size_t end_line =
        static_cast<std::size_t>(std::clamp(std::ceil(top_line + client.height / line_height) + 1.0, 0.0, line_total));
    text_runs_.clear();
    std::uint64_t glyphs = 0U;
    for (std::size_t line = first_line; line < end_line; ++line) {
        const SheetLine& sheet_line = lines_[line];
        const std::vector<SheetRun>::const_iterator line_begin = runs_.begin() + sheet_line.first_run;
        const std::vector<SheetRun>::const_iterator line_end = line_begin + sheet_line.run_count;
        const std::vector<SheetRun>::const_iterator shown_begin =
            std::partition_point(line_begin, line_end, RunEndsBefore{left_column});
        const std::vector<SheetRun>::const_iterator shown_end =
            std::partition_point(shown_begin, line_end, RunStartsBefore{right_column});
        const double baseline = (static_cast<double>(line) - top_line + baseline_share) * line_height;
        for (std::vector<SheetRun>::const_iterator run = shown_begin; run != shown_end; ++run) {
            const double x = (static_cast<double>((*run).column) - left_column) * advance;
            text_runs_.push_back(
                TextRun{Point{x, baseline}, run_text(*run), palette[static_cast<std::size_t>((*run).colour)]});
            glyphs += (*run).glyphs;
        }
    }
    painter.draw_text_runs(font, text_runs_);
    glyphs_drawn_ = glyphs;
    if (readout_shown_) paint_readout(painter, client);
}

void CodeSheet::note_paint(const FrameTime now) {
    statistics_.paints[statistics_.next_paint] = now;
    statistics_.next_paint = (statistics_.next_paint + 1U) % kept_paint_times;
    statistics_.paint_count = std::min(statistics_.paint_count + 1U, kept_paint_times);
    const Window* const owner = attached_window();
    if (owner == nullptr) return;
    const MetricsSnapshot figures = (*owner).metrics_snapshot();
    const std::uint64_t frames = figures.frames_presented - statistics_.frames_presented;
    if (figures.frames_presented > statistics_.frames_presented) {
        const std::uint64_t nanoseconds = figures.present_duration_nanoseconds - statistics_.present_nanoseconds;
        statistics_.milliseconds = static_cast<double>(nanoseconds) / static_cast<double>(frames) / 1'000'000.0;
        statistics_.measured = true;
    }
    statistics_.frames_presented = figures.frames_presented;
    statistics_.present_nanoseconds = figures.present_duration_nanoseconds;
}

// Paints in the last second, from the kept paint times.
double CodeSheet::paints_per_second() const noexcept {
    if (statistics_.paint_count < 2U) return 0.0;
    const std::size_t newest_index = (statistics_.next_paint + kept_paint_times - 1U) % kept_paint_times;
    const FrameTime newest = statistics_.paints[newest_index];
    std::size_t counted = 1U;
    FrameTime oldest = newest;
    for (std::size_t back = 1U; back < statistics_.paint_count; ++back) {
        const std::size_t index = (newest_index + kept_paint_times - back) % kept_paint_times;
        const FrameTime time = statistics_.paints[index];
        if (newest - time > std::chrono::seconds(1)) break;
        oldest = time;
        counted += 1U;
    }
    const double span = std::chrono::duration<double>(newest - oldest).count();
    const double rate = span > 0.0 ? static_cast<double>(counted - 1U) / span : 0.0;
    return rate;
}

void CodeSheet::paint_readout(Painter& painter, const Rect client) {
    const double rate = paints_per_second();
    int written = 0;
    if (statistics_.measured) {
        written = std::snprintf(readout_text_.data(), readout_text_.size(), "%.2f ms  %.0f fps  %llu glyphs  %.2fx",
                                statistics_.milliseconds, rate, static_cast<unsigned long long>(glyphs_drawn_),
                                model_.zoom());
    } else {
        written = std::snprintf(readout_text_.data(), readout_text_.size(), "-- ms  %.0f fps  %llu glyphs  %.2fx",
                                rate, static_cast<unsigned long long>(glyphs_drawn_), model_.zoom());
    }
    if (written <= 0) return;
    const std::size_t length = std::min(static_cast<std::size_t>(written), readout_text_.size() - 1U);
    const std::string_view text(readout_text_.data(), length);
    FontSpec font{};
    font.role = FontRole::monospace;
    font.size = readout_font_size;
    const Size measured = painter.measure_text_utf8(text, font);
    const double width = measured.width + readout_padding * 2.0;
    const double height = measured.height + readout_padding * 2.0;
    const Rect box{client.width - readout_margin - width, readout_margin, width, height};
    painter.fill_rounded_rect(box, 5.0, readout_background);
    const Point origin{box.x + readout_padding, box.y + readout_padding + measured.height * 0.78};
    painter.draw_text_utf8(origin, text, font, readout_ink);
}

} // namespace gui_forms::code_zoom
