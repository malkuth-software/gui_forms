#include "console_overlay.hpp"

#include "../../../core/application/application_state.hpp"

#include "gui_forms/controls/spinner.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/host/compositor/window_backdrop.hpp"
#include "gui_forms/host/windowing/host_windowing.hpp"
#include "gui_forms/semantics/types/semantic_types.hpp"
#include "gui_forms/allocation/library_memory/library_memory.hpp"
#include "gui_forms/typography/font_families/font_families.hpp"
#include "gui_forms/window.hpp"
#include "gui_forms/engine/motion_clock/motion_clock.hpp"
#include "gui_forms/paint_framebuffer.hpp"
#include "gui_forms/resources/image_atlas/image_atlas.hpp"

#include "console_screen.hpp"

#include <algorithm>
#include <chrono>
#include <deque>
#include <map>
#include <memory>
#include <span>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gui_forms::automation {

ConsoleOverlayListener::~ConsoleOverlayListener() = default;

namespace {

constexpr std::string_view button_id = "automation.console-button";
constexpr std::string_view console_id = "automation.console";
constexpr double console_padding = 12.0;
// The lines the console keeps for re-wrapping when its width changes.
constexpr std::size_t kept_lines = 400U;
// The console's layer: black at three quarters over the whole client area,
// so the program shows faintly through it (the ruling: "a slightly
// transparent darkened background"). The button's pill is the same black,
// nearly opaque, its caption the instruction's orange at full v.
constexpr Color console_background{0U, 0U, 0U, 192U};
constexpr Color button_background{0U, 0U, 0U, 224U};

Color button_caption() noexcept {
    const Color colour = console_colour(ConsoleShade{0.0, 0.8}, 0U);
    return colour;
}

} // namespace

FontSpec ConsoleOverlay::console_font() noexcept {
    FontSpec font{};
    // Portsmouth Mono draws every glyph it has. The role only names the
    // faces that draw what it lacks (FontSpec::family): the monospace
    // role's Cousine, so a fallback glyph keeps the column.
    font.family = portsmouth_mono_font_family;
    font.role = FontRole::monospace;
    font.size = console_font_size;
    return font;
}

// The console's glyph atlas: each glyph rasterised once, in Portsmouth
// Mono at the screen's pixel density, into an alpha mask one cell in size
// (after the owner's FastMatrixDashboard, whose mask bank holds an H x W
// float mask per glyph). Built once per face, size and density; a glyph
// first met later is rasterised then, with the others it arrives with.
class ConsoleGlyphs final {
public:
    // A new cell (pixels) and baseline: every mask goes.
    void reset(std::uint32_t cell_width, std::uint32_t cell_height, double baseline, double density) {
        cell_width_ = cell_width;
        cell_height_ = cell_height;
        baseline_ = baseline;
        density_ = density;
        masks_.clear();
    }

    [[nodiscard]] std::uint32_t cell_width() const noexcept { return cell_width_; }
    [[nodiscard]] std::uint32_t cell_height() const noexcept { return cell_height_; }
    [[nodiscard]] double density() const noexcept { return density_; }

    // The mask of `glyph` (cell_width x cell_height coverages, 0 to 1, row
    // by row), or null before prepare made it.
    [[nodiscard]] const std::vector<float>* mask(const std::string& glyph) const {
        const std::map<std::string, std::vector<float>>::const_iterator found = masks_.find(glyph);
        if (found == masks_.end()) return nullptr;
        return &(*found).second;
    }

    // Rasterises those of `wanted` not in the atlas yet, white on clear in
    // one offscreen frame of the host's renderer, and keeps each cell's
    // alpha. A glyph the frame cannot be made for gets an empty mask, so it
    // is not tried again.
    void prepare(SurfaceRoot& root, const std::vector<std::string>& wanted) {
        std::vector<std::string> missing{};
        for (const std::string& glyph : wanted) {
            if (masks_.find(glyph) != masks_.end()) continue;
            if (std::find(missing.begin(), missing.end(), glyph) != missing.end()) continue;
            missing.push_back(glyph);
        }
        if (missing.empty() || cell_width_ == 0U || cell_height_ == 0U) return;
        const std::size_t per_row = 32U;
        const std::size_t rows = (missing.size() + per_row - 1U) / per_row;
        const double cell_w = static_cast<double>(cell_width_) / density_;
        const double cell_h = static_cast<double>(cell_height_) / density_;
        const Size size{cell_w * static_cast<double>(per_row), cell_h * static_cast<double>(rows)};
        std::unique_ptr<PaintFramebuffer> frame{};
        try {
            frame = root.create_framebuffer(size, density_);
        } catch (...) {
            frame.reset();
        }
        const std::vector<float> empty(static_cast<std::size_t>(cell_width_) * cell_height_, 0.0F);
        if (!frame) {
            for (const std::string& glyph : missing) masks_[glyph] = empty;
            return;
        }
        BorrowedMutableSpan<std::byte> clearing = (*frame).pixels();
        for (std::size_t index = 0U; index < clearing.size(); ++index) clearing[index] = std::byte{0};
        if ((*frame).begin(root.image_resources(), Rect{0.0, 0.0, size.width, size.height})) {
            Painter& painter = (*frame).painter();
            const FontSpec font = ConsoleOverlay::console_font();
            for (std::size_t index = 0U; index < missing.size(); ++index) {
                const double x = cell_w * static_cast<double>(index % per_row);
                const double y = cell_h * static_cast<double>(index / per_row) + baseline_ / density_;
                painter.draw_text_utf8(Point{x, y}, missing[index], font, Color{255U, 255U, 255U, 255U});
            }
            (*frame).end();
        }
        const BorrowedMutableSpan<std::byte> pixels = (*frame).pixels();
        const std::size_t row_bytes = (*frame).row_bytes();
        const std::size_t frame_width = (*frame).width();
        const std::size_t frame_height = (*frame).height();
        for (std::size_t index = 0U; index < missing.size(); ++index) {
            std::vector<float> mask = empty;
            const std::size_t left = (index % per_row) * cell_width_;
            const std::size_t top = (index / per_row) * cell_height_;
            for (std::size_t y = 0U; y < cell_height_ && top + y < frame_height; ++y) {
                for (std::size_t x = 0U; x < cell_width_ && left + x < frame_width; ++x) {
                    const std::size_t at = (top + y) * row_bytes + (left + x) * 4U + 3U;
                    if (at >= pixels.size()) continue;
                    mask[y * cell_width_ + x] = static_cast<float>(std::to_integer<unsigned>(pixels[at])) / 255.0F;
                }
            }
            masks_[missing[index]] = std::move(mask);
        }
    }

private:
    std::uint32_t cell_width_{};
    std::uint32_t cell_height_{};
    double baseline_{};
    double density_{1.0};
    std::map<std::string, std::vector<float>> masks_{};
};

// The console's root: the darkened layer over the program's client area and
// the screen on it, a character grid like a terminal's.
//
// The screen model (console_screen.hpp) says which glyphs show. Each row in
// view is composited into a pixel buffer at the cell's native size (the
// glyph's mask times the formula's colour, as FastMatrixDashboard's
// grid_pixels = masks * colours), only the glyphs newly shown, and written
// into its slots of an ImageAtlas; the paint draws the slots scaled up to
// the window with linear filtering. That soft scaling is the CRT-like
// dither the owner asked for: nothing is filtered afterwards.
//
// Frames run only while a line is typing or the screen is scrolling (an
// ambient effect on the window's motion clock); otherwise nothing wakes.
// The console is its own window, so neither its frames nor its repaints
// touch the program beneath, and the automation's idle test never sees it.
//
// A press on the button's place toggles the console (in case the host keeps
// the console above the button); a press anywhere else offers take-back.
class ConsoleSurface;

// A rebuild posted from arrange, which must not change the tree itself.
struct RewrapWork final {
    std::weak_ptr<ConsoleSurface*> surface{};
    void operator()() const noexcept;
};

class ConsoleSurface final : public Control {
public:
    // The buffer's pixels per logical unit at a window scale: half the
    // window's own, so the scaling softens it, and no less than 0.75.
    static double density_for(double scale) noexcept { return std::max(0.75, scale * 0.5); }

    ConsoleSurface(StableId stable_id, ConsoleOverlay* overlay)
        : Control(std::move(stable_id)), overlay_(overlay),
          self_(gui_forms::detail::make_library_shared<ConsoleSurface*>(this)) {}
    ~ConsoleSurface() override { self_.reset(); }

    // New lines from the record: queued to type.
    void take_lines(const std::vector<ConsoleLine>& lines) {
        for (const ConsoleLine& line : lines) {
            lines_.push_back(line);
            if (columns_ > 0U) {
                std::vector<ConsoleGlyphRow> rows{};
                console_wrap(line, columns_, rows);
                screen_.queue_line(std::move(rows), begins_entry(line));
            }
        }
        if (lines_.size() > kept_lines) {
            lines_.erase(lines_.begin(), lines_.begin() + static_cast<std::ptrdiff_t>(lines_.size() - kept_lines));
        }
        start_motion();
    }

    // Where the text begins (below a button inside the client area) and
    // the button's rectangle, in the surface's own units.
    void set_frame(double text_top, Rect button) {
        if (text_top != text_top_) invalidate(Dirty::arrange | Dirty::paint);
        text_top_ = text_top;
        button_ = button;
    }

    void arrange(Rect final_bounds) override {
        arrange_self(final_bounds);
        measure_face();
        const SurfaceRoot* const root = attached_surface_root();
        const double density = density_for(root != nullptr ? (*root).scale() : 1.0);
        const double usable = std::max(0.0, final_bounds.width - 2.0 * console_padding);
        const double text_height = std::max(0.0, final_bounds.height - text_top_ - console_padding);
        std::size_t columns = 0U;
        std::size_t rows = 0U;
        if (advance_ > 0.0 && line_height_ > 0.0) {
            // The rows are as tall as the paint draws them: a whole number
            // of buffer pixels at this density, scaled back.
            const double row_height = std::max(1.0, std::ceil(line_height_ * density)) / density;
            columns = std::max<std::size_t>(16U, static_cast<std::size_t>(usable / advance_));
            rows = std::max<std::size_t>(1U, static_cast<std::size_t>(text_height / row_height));
        }
        const bool changed = columns != columns_ || rows != screen_.visible_rows() || density != density_;
        if (changed && columns > 0U && !rebuild_posted_) {
            wanted_columns_ = columns;
            wanted_rows_ = rows;
            wanted_density_ = density;
            SurfaceRoot* const window = attached_surface_root();
            if (window != nullptr) {
                try {
                    static_cast<void>((*window).begin_invoke(RewrapWork{self_}));
                    rebuild_posted_ = true;
                } catch (...) {
                    // The window is closing: nothing to lay out for.
                }
            }
        }
    }

    void on_paint(Painter& painter, Rect) override {
        const Rect bounds{0.0, 0.0, arranged_bounds().width, arranged_bounds().height};
        // Where the host grants no transparency (Reduce Transparency, no
        // compositor), the layer is opaque black, a terminal's screen,
        // rather than three quarters of black over the window's backplane.
        const SurfaceRoot* const root = attached_surface_root();
        const bool clear = root != nullptr && (*root).backdrop_grant().granted != WindowBackdrop::opaque;
        painter.fill_rect(bounds, clear ? console_background : Color{0U, 0U, 0U, 255U});
        if (screen_.busy()) keep_moving();
        if (!atlas_ || glyphs_.cell_height() == 0U) return;
        const double density = glyphs_.density();
        const double row_height = static_cast<double>(glyphs_.cell_height()) / density;
        const double slot_width = static_cast<double>((*atlas_).layout().slot_width) / density;
        const double first = static_cast<double>(screen_.first_visible());
        painter.save();
        painter.clip_rect(Rect{0.0, text_top_, bounds.width, std::max(0.0, bounds.height - text_top_)});
        const std::deque<ConsoleScreenRow>& rows = screen_.rows();
        for (std::size_t index = 0U; index < rows.size(); ++index) {
            const double y = text_top_ + (static_cast<double>(index) - first + screen_.scroll()) * row_height;
            if (y + row_height <= text_top_ || y >= bounds.height) continue;
            for (std::size_t segment = 0U; segment < segments_; ++segment) {
                const ImageAtlasSlot slot = (*atlas_).find(slot_key(rows[index].key, segment));
                if (!slot) continue;
                const Rect destination{console_padding + slot_width * static_cast<double>(segment), y, slot_width,
                                       row_height};
                static_cast<void>((*atlas_).draw(painter, *this, slot, destination));
            }
        }
        painter.restore();
    }

    bool on_ambient_frame(FrameTime now) override {
        const double seconds = std::chrono::duration<double>(now - last_frame_).count();
        last_frame_ = now;
        if (screen_.advance(seconds)) {
            sync();
            invalidate(Dirty::paint);
        }
        if (screen_.busy()) return true;
        // Settled: no more frames until a line arrives.
        Window* const host = attached_window();
        if (host != nullptr) (*host).motion_clock().withdraw_control_ambient(*this);
        return false;
    }

    void on_pointer_preview(PointerEvent& event) override {
        if (event.action != PointerAction::down) return;
        event.handled = true;
        if (overlay_ == nullptr) return;
        const Point at = event.position;
        const bool on_button = at.x >= button_.x && at.x < button_.x + button_.width && at.y >= button_.y &&
                               at.y < button_.y + button_.height;
        if (on_button) {
            (*overlay_).request_console_toggle();
        } else {
            (*overlay_).request_press();
        }
    }

    void on_detached_from_window() noexcept override {
        Window* const host = attached_window();
        if (host != nullptr) (*host).motion_clock().withdraw_control_ambient(*this);
        Control::on_detached_from_window();
    }

    void forget_overlay() noexcept { overlay_ = nullptr; }

    // The posted rebuild: every kept line wrapped again at the new size and
    // shown at once (a resize does not replay the typing).
    void rebuild() {
        rebuild_posted_ = false;
        columns_ = wanted_columns_;
        density_ = wanted_density_;
        screen_.clear();
        screen_.set_visible_rows(wanted_rows_);
        rasters_.clear();
        const std::uint32_t cell_width = static_cast<std::uint32_t>(std::max(1.0, std::round(advance_ * density_)));
        const std::uint32_t cell_height =
            static_cast<std::uint32_t>(std::max(1.0, std::ceil(line_height_ * density_)));
        if (cell_width != glyphs_.cell_width() || cell_height != glyphs_.cell_height() ||
            density_ != glyphs_.density()) {
            glyphs_.reset(cell_width, cell_height, std::round(baseline_ * density_), density_);
        }
        make_atlas();
        for (const ConsoleLine& line : lines_) {
            std::vector<ConsoleGlyphRow> rows{};
            console_wrap(line, columns_, rows);
            screen_.queue_line(std::move(rows), false);
        }
        screen_.show_all();
        sync();
        invalidate(Dirty::paint);
    }

private:
    struct RowRaster final {
        std::vector<std::byte> pixels{};
        std::size_t painted{};
    };

    // Whether `line` begins an entry (a call, a typed line, a note), not a
    // reply's line ("‹ ..." or an indented continuation).
    static bool begins_entry(const ConsoleLine& line) {
        if (line.spans.empty()) return true;
        const std::string& first = line.spans.front().text;
        const bool reply = first.rfind("\u2039", 0U) == 0U || first.rfind("  ", 0U) == 0U;
        return !reply;
    }

    static std::uint64_t slot_key(std::uint64_t row, std::size_t segment) noexcept {
        return row * 16U + static_cast<std::uint64_t>(segment);
    }

    // The face's advance, line height and baseline, measured once.
    void measure_face() {
        if (advance_ > 0.0) return;
        const FontSpec font = ConsoleOverlay::console_font();
        const TextExtent extent = measure_text_extent_utf8("0000000000", font);
        if (extent.logical_size.width <= 0.0) return;
        advance_ = extent.logical_size.width / 10.0;
        const double height = extent.ascent + extent.descent + extent.line_gap;
        line_height_ = std::ceil(height > 0.0 ? height * 1.15 : ConsoleOverlay::console_font_size * 1.4);
        const double ascent = extent.ascent > 0.0 ? extent.ascent : ConsoleOverlay::console_font_size * 0.8;
        baseline_ = std::floor((line_height_ - std::max(height, 0.0)) / 2.0 + ascent);
    }

    // The atlas of row slots: a row's buffer split into slots no wider than
    // the atlas allows, enough for the rows in view, the one above them and
    // two to spare.
    void make_atlas() {
        atlas_.reset();
        SurfaceRoot* const root = attached_surface_root();
        if (root == nullptr || columns_ == 0U) return;
        const std::uint32_t row_pixels = static_cast<std::uint32_t>(columns_) * glyphs_.cell_width();
        segments_ = (row_pixels + 1023U) / 1024U;
        if (segments_ == 0U || segments_ > 16U) return;
        ImageAtlasLayout layout{};
        layout.slot_width = (row_pixels + static_cast<std::uint32_t>(segments_) - 1U) /
                            static_cast<std::uint32_t>(segments_);
        layout.slot_height = std::min<std::uint32_t>(glyphs_.cell_height(), 1024U);
        layout.slot_count = static_cast<std::uint32_t>((screen_.visible_rows() + 3U) * segments_);
        layout.gutter = 1U;
        layout.users_per_slot = 1U;
        layout.replacement = ImageAtlasReplacement::least_recently_used;
        layout.pending_fills = image_atlas_pending_every_slot;
        try {
            atlas_ = std::make_unique<ImageAtlas>((*root).resources(), layout);
        } catch (...) {
            // The registry's quota or limits: the console shows its layer
            // alone.
            atlas_.reset();
        }
    }

    // Brings each row's buffer up to what the screen shows: the glyphs newly
    // shown are composited and their slots written. Buffers of rows that
    // left go.
    void sync() {
        if (!atlas_) return;
        SurfaceRoot* const root = attached_surface_root();
        if (root == nullptr) return;
        const std::deque<ConsoleScreenRow>& rows = screen_.rows();
        std::vector<std::string> wanted{};
        for (const ConsoleScreenRow& row : rows) {
            for (std::size_t index = 0U; index < row.shown; ++index) wanted.push_back(row.row.glyphs[index].text);
        }
        glyphs_.prepare(*root, wanted);
        const std::uint32_t slot_width = (*atlas_).layout().slot_width;
        const std::uint32_t cell_height = std::min<std::uint32_t>(glyphs_.cell_height(), 1024U);
        const std::size_t row_bytes = static_cast<std::size_t>(slot_width) * segments_ * 4U;
        std::map<std::uint64_t, RowRaster> kept{};
        for (const ConsoleScreenRow& row : rows) {
            std::map<std::uint64_t, RowRaster>::iterator found = rasters_.find(row.key);
            RowRaster raster{};
            if (found != rasters_.end()) raster = std::move((*found).second);
            if (raster.pixels.empty()) raster.pixels.assign(row_bytes * cell_height, std::byte{0});
            // The glyphs newly shown, and those whose glow changed.
            std::size_t first_segment = segments_;
            std::size_t last_segment = 0U;
            const std::size_t painted = std::min(raster.painted, row.shown);
            composite_range(raster, row, painted, row.shown, first_segment, last_segment);
            raster.painted = row.shown;
            for (const ConsoleFade& fade : screen_.fades()) {
                if (fade.key == row.key) {
                    composite_range(raster, row, fade.first, std::min(fade.last, painted), first_segment,
                                    last_segment);
                }
            }
            for (const ConsoleFade& fade : screen_.settled()) {
                if (fade.key == row.key) {
                    composite_range(raster, row, fade.first, std::min(fade.last, row.shown), first_segment,
                                    last_segment);
                }
            }
            for (std::size_t segment = first_segment; segment <= last_segment && segment < segments_; ++segment) {
                write_slot(row.key, segment, raster.pixels, row_bytes);
            }
            kept[row.key] = std::move(raster);
        }
        rasters_ = std::move(kept);
    }

    // Composites glyphs [first, last) of `row` into its raster, each in its
    // colour at its glow now, and widens the segments touched.
    void composite_range(RowRaster& raster, const ConsoleScreenRow& row, std::size_t first, std::size_t last,
                         std::size_t& first_segment, std::size_t& last_segment) {
        const std::uint32_t slot_width = (*atlas_).layout().slot_width;
        const std::uint32_t cell_width = glyphs_.cell_width();
        const std::uint32_t cell_height = std::min<std::uint32_t>(glyphs_.cell_height(), 1024U);
        const std::size_t row_bytes = static_cast<std::size_t>(slot_width) * segments_ * 4U;
        for (std::size_t index = first; index < last && index < row.row.glyphs.size(); ++index) {
            const ConsoleGlyph& glyph = row.row.glyphs[index];
            const std::vector<float>* const mask = glyphs_.mask(glyph.text);
            const std::size_t left = glyph.column * cell_width;
            if (mask == nullptr || left + cell_width > row_bytes / 4U) continue;
            const ConsoleRgb rgb = console_glow(glyph.shade, screen_.glow(row.key, index));
            composite(raster.pixels, row_bytes, left, *mask, cell_width, cell_height, rgb);
            first_segment = std::min<std::size_t>(first_segment, left / slot_width);
            last_segment = std::max<std::size_t>(last_segment, (left + cell_width - 1U) / slot_width);
        }
    }

    // One glyph's cell: its mask times its colour, premultiplied BGRA, over
    // clear (the layer beneath is the surface's own fill).
    static void composite(std::vector<std::byte>& pixels, std::size_t row_bytes, std::size_t left,
                          const std::vector<float>& mask, std::uint32_t cell_width, std::uint32_t cell_height,
                          ConsoleRgb rgb) {
        for (std::uint32_t y = 0U; y < cell_height; ++y) {
            for (std::uint32_t x = 0U; x < cell_width; ++x) {
                const float coverage = mask[static_cast<std::size_t>(y) * cell_width + x];
                const std::size_t at = static_cast<std::size_t>(y) * row_bytes + (left + x) * 4U;
                pixels[at] = std::byte{static_cast<unsigned char>(std::lround(rgb.b * coverage * 255.0))};
                pixels[at + 1U] = std::byte{static_cast<unsigned char>(std::lround(rgb.g * coverage * 255.0))};
                pixels[at + 2U] = std::byte{static_cast<unsigned char>(std::lround(rgb.r * coverage * 255.0))};
                pixels[at + 3U] = std::byte{static_cast<unsigned char>(std::lround(coverage * 255.0))};
            }
        }
    }

    void write_slot(std::uint64_t row, std::size_t segment, const std::vector<std::byte>& pixels,
                    std::size_t row_bytes) {
        const ImageAtlasAcquire acquired = (*atlas_).acquire(slot_key(row, segment));
        if (!acquired.slot) return;
        const std::size_t offset = segment * (*atlas_).layout().slot_width * 4U;
        const std::span<const std::byte> view(pixels.data() + offset, pixels.size() - offset);
        static_cast<void>((*atlas_).fill(acquired.slot, row_bytes, view));
    }

    // Starts the frames for typing, or shows everything at once where
    // motion is off (reduced motion, an occluded window).
    void start_motion() {
        Window* const host = attached_window();
        if (host == nullptr || (*host).motion_clock().ambient_suppressed()) {
            screen_.show_all();
            sync();
            invalidate(Dirty::paint);
            return;
        }
        keep_moving();
        invalidate(Dirty::paint);
    }

    // Enlists the frames when they are not running (a paint restarts frames
    // the clock dropped while the console was not drawn).
    void keep_moving() {
        Window* const host = attached_window();
        if (host == nullptr) return;
        MotionClock& clock = (*host).motion_clock();
        if (clock.control_ambient(*this)) return;
        try {
            if (clock.enlist_control_ambient(*this, frame_interval)) last_frame_ = clock.now();
        } catch (...) {
            // No room for the effect: the lines show at once.
            screen_.show_all();
            sync();
        }
    }

    static constexpr FrameInterval frame_interval = std::chrono::milliseconds(16);

    ConsoleOverlay* overlay_{};
    std::shared_ptr<ConsoleSurface*> self_{};
    bool rebuild_posted_{};
    std::size_t wanted_columns_{};
    std::size_t wanted_rows_{};
    double wanted_density_{1.0};
    std::vector<ConsoleLine> lines_{};
    ConsoleScreen screen_{};
    ConsoleGlyphs glyphs_{};
    std::unique_ptr<ImageAtlas> atlas_{};
    std::size_t segments_{};
    std::map<std::uint64_t, RowRaster> rasters_{};
    FrameTime last_frame_{};
    double advance_{};
    double baseline_{};
    double line_height_{};
    double density_{};
    std::size_t columns_{};
    double text_top_{console_padding};
    Rect button_{};
};

void RewrapWork::operator()() const noexcept {
    const std::shared_ptr<ConsoleSurface*> live = surface.lock();
    if (!live) return;
    try {
        (**live).rebuild();
    } catch (...) {
        // No memory for the rows: the console shows what it had.
    }
}

// The button: a dark pill with a turning spinner and "thinking...". A click
// opens or closes the console.
class ConsoleButton final : public Control {
public:
    static constexpr bool initialize_tree_after_construction = true;

    ConsoleButton(StableId stable_id, ConsoleOverlay* overlay) : Control(std::move(stable_id)), overlay_(overlay) {}

    void initialize_control_tree() {
        spinner_ = make_control<Spinner>(StableId(std::string(button_id) + ".spinner"));
        add_child(spinner_);
    }

    void arrange(Rect final_bounds) override {
        arrange_self(final_bounds);
        const double side = std::max(0.0, final_bounds.height - 10.0);
        set_child_layout(spinner_, Rect{8.0, 5.0, side, side});
    }

    void on_paint(Painter& painter, Rect) override {
        const Rect bounds{0.0, 0.0, arranged_bounds().width, arranged_bounds().height};
        painter.fill_rounded_rect(bounds, bounds.height / 2.0, button_background);
        FontSpec font = ConsoleOverlay::console_font();
        font.size = 12.0;
        const TextExtent extent = measure_text_extent_utf8("thinking...", font);
        const double baseline = (bounds.height - extent.logical_size.height) / 2.0 + extent.ascent;
        painter.draw_text_utf8(Point{bounds.height + 6.0, baseline}, "thinking...", font, button_caption());
    }

    void on_pointer_preview(PointerEvent& event) override {
        if (event.button != PointerButton::primary) return;
        if (event.action == PointerAction::down) {
            pressed_ = true;
            event.handled = true;
            return;
        }
        if (event.action != PointerAction::up) return;
        event.handled = true;
        // Only a click that began on the button toggles: a release left
        // over from a press elsewhere (one that opened the button under
        // the pointer) does nothing.
        const bool clicked = pressed_;
        pressed_ = false;
        if (clicked && overlay_ != nullptr) (*overlay_).request_toggle();
    }

    void forget_overlay() noexcept { overlay_ = nullptr; }

private:
    ConsoleOverlay* overlay_{};
    std::shared_ptr<Spinner> spinner_{};
    bool pressed_{};
};

namespace {

// A popup window's closed callback.
struct OverlayClosed final {
    std::weak_ptr<ConsoleOverlay*> overlay{};
    bool console{};
    void operator()() const noexcept {
        const std::shared_ptr<ConsoleOverlay*> live = overlay.lock();
        if (live) (**live).window_closed(console);
    }
};

// The run's frame reports, for the program window only.
struct OwnerFrameReport final {
    std::weak_ptr<ConsoleOverlay*> overlay{};
    HostWindowId owner{};
    void operator()(const HostWindowFrameEvent& event) const noexcept {
        if (!(event.window == owner)) return;
        const std::shared_ptr<ConsoleOverlay*> live = overlay.lock();
        if (!live) return;
        try {
            (**live).follow(event.frame);
        } catch (...) {
            // A window refused its move: the next report moves it again.
        }
    }
};

// The button's toggle runs at the button window's next turn, not inside
// its own pointer dispatch (opening a window there would reenter it).
struct DeferredToggle final {
    std::weak_ptr<ConsoleOverlay*> overlay{};
    void operator()() const noexcept {
        const std::shared_ptr<ConsoleOverlay*> live = overlay.lock();
        if (!live) return;
        try {
            (**live).toggle_console();
        } catch (...) {
            // The console could not open: the button stays.
        }
    }
};

// A press on the console reaches the listener at the console window's next
// turn, as the gate's held press does.
struct DeferredPress final {
    std::weak_ptr<ConsoleOverlay*> overlay{};
    void operator()() const noexcept {
        const std::shared_ptr<ConsoleOverlay*> live = overlay.lock();
        if (!live) return;
        try {
            (**live).console_pressed();
        } catch (...) {
            // The take-back dialog could not open: the next press tries.
        }
    }
};

} // namespace

ConsoleOverlay::ConsoleOverlay(ConsoleOverlayListener* listener)
    : listener_(listener), self_(gui_forms::detail::make_library_shared<ConsoleOverlay*>(this)) {}

ConsoleOverlay::~ConsoleOverlay() {
    self_.reset();
    hide();
}

ConsoleButtonProbe::~ConsoleButtonProbe() = default;

namespace {

// The roles of a window's background: a hit on one covers nothing a user
// reads or uses, unless the control takes an action.
bool background_role(SemanticRole role) noexcept {
    return role == SemanticRole::generic || role == SemanticRole::group || role == SemanticRole::pane ||
           role == SemanticRole::tab_panel || role == SemanticRole::toolbar || role == SemanticRole::menu_bar ||
           role == SemanticRole::status_bar || role == SemanticRole::separator || role == SemanticRole::tab_group;
}

// The probe over the program window's model: a rectangle covers content
// when a hit test at its corners or centre lands on a control that takes
// an action or is not background.
class ModelProbe final : public ConsoleButtonProbe {
public:
    explicit ModelProbe(Window* window) noexcept : window_(window) {}
    [[nodiscard]] bool covers(Rect local) const override {
        const Point points[5] = {{local.x + 1.0, local.y + 1.0},
                                 {local.x + local.width - 1.0, local.y + 1.0},
                                 {local.x + 1.0, local.y + local.height - 1.0},
                                 {local.x + local.width - 1.0, local.y + local.height - 1.0},
                                 {local.x + local.width / 2.0, local.y + local.height / 2.0}};
        for (const Point point : points) {
            const Control::Ptr hit = (*window_).hit_test(point);
            if (!hit) continue;
            const SemanticDescriptor descriptor = (*hit).semantic_descriptor();
            if (!descriptor.actions.empty() || !background_role(descriptor.role)) return true;
        }
        return false;
    }

private:
    Window* window_{};
};

// The least title area the button sits in, and the step of the search
// along the client area.
constexpr double least_title_button = 20.0;
constexpr double search_step = 16.0;
constexpr int search_rows = 6;

} // namespace

double ConsoleOverlay::caption_reserve() noexcept {
#if defined(__APPLE__)
    return 0.0;
#elif defined(_WIN32)
    // Minimize, maximize and close, about 46 logical units each.
    return 146.0;
#else
    return 110.0;
#endif
}

Rect ConsoleOverlay::button_place(const HostWindowFrame& frame, double reserve,
                                  const ConsoleButtonProbe* probe) noexcept {
    const Rect content = frame.content;
    const double title = content.y - frame.outer.y;
    if (frame.outer_measured && title >= least_title_button + 4.0) {
        const double height = std::min(button_height, title - 4.0);
        const double x = frame.outer.x + frame.outer.width - reserve - inset - button_width;
        // Clear of the title text in the middle of the bar.
        if (x >= frame.outer.x + frame.outer.width / 2.0 + 40.0) {
            const Rect rect{x, frame.outer.y + (title - height) / 2.0, button_width, height};
            return rect;
        }
    }
    const double right = content.x + content.width - inset - button_width;
    for (int row = 0; row < search_rows; ++row) {
        const double y = content.y + inset + static_cast<double>(row) * (button_height + inset);
        if (y + button_height > content.y + content.height) break;
        for (double x = right; x >= content.x + inset; x -= search_step) {
            const Rect local{x - content.x, y - content.y, button_width, button_height};
            if (probe == nullptr || !(*probe).covers(local)) {
                const Rect rect{x, y, button_width, button_height};
                return rect;
            }
        }
    }
    const Rect rect{right, content.y + inset, button_width, button_height};
    return rect;
}

Rect ConsoleOverlay::console_place(Rect content, Rect) noexcept {
    return content;
}

double ConsoleOverlay::console_text_top(Rect content, Rect button) noexcept {
    const bool inside = button.y >= content.y && button.y < content.y + content.height;
    const double top = inside ? button.y + button.height - content.y + 6.0 : 12.0;
    return top;
}

Rect ConsoleOverlay::button_rect() const {
    Window* const model = owner_model_.get();
    if (model == nullptr) return button_place(owner_frame_, caption_reserve(), nullptr);
    const ModelProbe probe(model);
    return button_place(owner_frame_, caption_reserve(), &probe);
}

bool ConsoleOverlay::open_popup(bool console, const Control::Ptr& root, Rect bounds, ApplicationWindowHandle& handle,
                                WindowRef& reference) {
    const ApplicationWindows windowing = owner_.windows();
    if (!windowing.active()) return false;
    std::unique_ptr<Window> model = std::make_unique<Window>(root, Size{bounds.width, bounds.height});
    // The content draws every surface it shows: the backplane is clear over
    // a non-opaque grant, so the panel's own transparency shows.
    static_cast<void>((*model).declare_content_surface());
    static_cast<void>((*model).request_backdrop(WindowBackdropRequest::preferring(WindowBackdrop::transparent)));
    const WindowRef made = (*model).window_ref();
    ApplicationWindow entry{};
    entry.stable_id = console ? console_id : button_id;
    entry.owner_id = owner_id_;
    // The console answers whatever modal state the model opened, as the
    // take-back dialog does.
    entry.outside_modality = true;
    entry.model = std::move(model);
    entry.options.title = console ? "MCP console" : "MCP console button";
    entry.options.initial_size = Size{bounds.width, bounds.height};
    entry.options.minimizable = false;
    entry.options.non_opaque = true;
    entry.options.closed = OverlayClosed{self_, console};
    HostPopupPlacement placement{};
    placement.kind = HostTransientKind::popup;
    placement.focus = HostPopupFocus::passive;
    placement.origin = DesktopLogicalPoint{bounds.x, bounds.y};
    entry.popup = stxlib::Optional<HostPopupPlacement>::of(placement);
    // Set before opening: the gate asks presenter_window for the window's
    // first input, which may come during open_window.
    reference = made;
    const ApplicationWindowOpenResult opened = windowing.open_window(std::move(entry));
    if (!opened.status.accepted()) {
        reference = WindowRef{};
        return false;
    }
    handle = opened.window;
    return true;
}

bool ConsoleOverlay::show(const ApplicationWindowHandle& owner, std::string_view owner_id,
                          const WindowRef& owner_model) {
    if (button_handle_.active()) return true;
    owner_ = owner;
    owner_id_ = std::string(owner_id);
    owner_model_ = owner_model;
    if (!owner_.active()) return false;
    const HostWindowFrameResult frame = owner_.frame();
    if (!frame.status.accepted()) return false;
    owner_frame_ = frame.frame;
    button_bounds_ = button_rect();
    button_ = make_control<ConsoleButton>(StableId(button_id), this);
    if (!open_popup(false, button_, button_bounds_, button_handle_, button_window_)) {
        (*button_).forget_overlay();
        button_.reset();
        return false;
    }
    subscribe_follow();
    return true;
}

void ConsoleOverlay::close_console() noexcept {
    if (console_) (*console_).forget_overlay();
    const ApplicationWindowHandle closing = console_handle_;
    console_handle_ = ApplicationWindowHandle{};
    console_window_ = WindowRef{};
    console_.reset();
    shown_serial_ = 0U;
    if (!closing.active()) return;
    try {
        static_cast<void>(owner_.windows().destroy_window(closing));
    } catch (...) {
        // The run is ending: the window goes with it.
    }
}

void ConsoleOverlay::hide() noexcept {
    follow_reports_.disconnect();
    close_console();
    if (button_) (*button_).forget_overlay();
    const ApplicationWindowHandle closing = button_handle_;
    button_handle_ = ApplicationWindowHandle{};
    button_window_ = WindowRef{};
    button_.reset();
    if (!closing.active()) return;
    try {
        static_cast<void>(owner_.windows().destroy_window(closing));
    } catch (...) {
        // The run is ending: the window goes with it.
    }
}

void ConsoleOverlay::toggle_console() {
    if (console_handle_.active()) {
        close_console();
        return;
    }
    if (!button_handle_.active()) return;
    console_ = make_control<ConsoleSurface>(StableId(console_id), this);
    if (!open_popup(true, console_, console_place(owner_frame_.content, button_bounds_), console_handle_,
                    console_window_)) {
        (*console_).forget_overlay();
        console_.reset();
        return;
    }
    frame_console();
    // The button stays above the console's layer.
    static_cast<void>(button_handle_.bring_to_front());
    shown_serial_ = 0U;
    refresh();
}

void ConsoleOverlay::frame_console() {
    if (!console_) return;
    const Rect content = owner_frame_.content;
    const Rect local_button{button_bounds_.x - content.x, button_bounds_.y - content.y, button_bounds_.width,
                            button_bounds_.height};
    (*console_).set_frame(console_text_top(content, button_bounds_), local_button);
}

void ConsoleOverlay::refresh() {
    if (!console_ || log_ == nullptr) return;
    std::vector<ConsoleLine> lines{};
    (*log_).copy_since(shown_serial_, lines);
    if (lines.empty()) return;
    shown_serial_ = lines.back().serial;
    (*console_).take_lines(lines);
}

void ConsoleOverlay::follow(const HostWindowFrame& frame) {
    if (!button_handle_.active() || frame == owner_frame_) return;
    owner_frame_ = frame;
    button_bounds_ = button_rect();
    static_cast<void>(button_handle_.request_frame(button_bounds_));
    if (console_handle_.active()) {
        static_cast<void>(console_handle_.request_frame(console_place(owner_frame_.content, button_bounds_)));
        frame_console();
    }
}

void ConsoleOverlay::subscribe_follow() noexcept {
    follow_reports_.disconnect();
    const std::shared_ptr<gui_forms::detail::ApplicationWindowState> state =
        gui_forms::detail::ApplicationHandleAccess::state(owner_);
    const std::shared_ptr<gui_forms::detail::ApplicationRunState> run =
        state ? (*state).run.lock() : std::shared_ptr<gui_forms::detail::ApplicationRunState>{};
    if (!run || (*run).closed || (*run).windowing == nullptr) return;
    try {
        follow_reports_ = (*(*run).windowing).frame_changed().subscribe(OwnerFrameReport{self_, owner_.id()});
    } catch (...) {
        // No memory for the subscription: the overlay stays where it opened.
    }
}

void ConsoleOverlay::request_toggle() noexcept {
    Window* const window = button_window_.get();
    if (window == nullptr) return;
    try {
        static_cast<void>((*window).begin_invoke(DeferredToggle{self_}));
    } catch (...) {
        // The button's window stopped taking work: it is closing.
    }
}

void ConsoleOverlay::request_press() noexcept {
    Window* const window = console_window_.get();
    if (window == nullptr) return;
    try {
        static_cast<void>((*window).begin_invoke(DeferredPress{self_}));
    } catch (...) {
        // The console's window stopped taking work: it is closing.
    }
}

void ConsoleOverlay::request_console_toggle() noexcept {
    Window* const window = console_window_.get();
    if (window == nullptr) return;
    try {
        static_cast<void>((*window).begin_invoke(DeferredToggle{self_}));
    } catch (...) {
        // The console's window stopped taking work: it is closing.
    }
}

void ConsoleOverlay::console_pressed() {
    if (listener_ != nullptr) (*listener_).console_pressed();
}

void ConsoleOverlay::window_closed(bool console) noexcept {
    if (console) {
        if (console_) (*console_).forget_overlay();
        console_handle_ = ApplicationWindowHandle{};
        console_window_ = WindowRef{};
        console_.reset();
        shown_serial_ = 0U;
        return;
    }
    follow_reports_.disconnect();
    if (button_) (*button_).forget_overlay();
    button_handle_ = ApplicationWindowHandle{};
    button_window_ = WindowRef{};
    button_.reset();
    // The console goes with its button.
    close_console();
}

bool ConsoleOverlay::owns(const SurfaceRoot& window) const noexcept {
    const SurfaceRoot* const button = button_window_.get();
    const SurfaceRoot* const console = console_window_.get();
    return (button != nullptr && button == &window) || (console != nullptr && console == &window);
}

bool ConsoleOverlay::button_shown() const noexcept {
    return button_handle_.active();
}

bool ConsoleOverlay::console_open() const noexcept {
    return console_handle_.active();
}

} // namespace gui_forms::automation
