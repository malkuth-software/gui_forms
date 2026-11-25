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

#include <algorithm>
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

// One row of the console: its glyphs at their columns, each in its own
// colour, and the grain over them: in every band of console_grain_band
// along the row's height, the glyphs whose parity matches the band's are
// drawn again in their grain colour, clipped to the band (an ordered dither
// inside each glyph's fill, its phase seeded per glyph). A row is drawn
// once when it is added; it takes no input (a press reaches the surface).
class ConsoleRow final : public Control {
public:
    ConsoleRow(StableId stable_id, double advance, double baseline, double height, ConsoleGlyphRow row)
        : Control(std::move(stable_id)), advance_(advance), baseline_(baseline), height_(height),
          row_(std::move(row)) {}

    void on_paint(Painter& painter, Rect) override {
        if (row_.glyphs.empty()) return;
        const FontSpec font = ConsoleOverlay::console_font();
        std::vector<TextRun> runs{};
        runs.reserve(row_.glyphs.size());
        for (const ConsoleGlyph& glyph : row_.glyphs) {
            runs.push_back(TextRun{Point{static_cast<double>(glyph.column) * advance_, baseline_}, glyph.text,
                                   glyph.colour});
        }
        painter.draw_text_runs(font, runs);
        const double width = arranged_bounds().width;
        std::size_t band = 0U;
        for (double top = 0.0; top < height_; top += console_grain_band) {
            const bool odd = (band % 2U) != 0U;
            ++band;
            runs.clear();
            for (const ConsoleGlyph& glyph : row_.glyphs) {
                if (glyph.odd != odd) continue;
                runs.push_back(TextRun{Point{static_cast<double>(glyph.column) * advance_, baseline_}, glyph.text,
                                       glyph.grain});
            }
            if (runs.empty()) continue;
            painter.save();
            painter.clip_rect(Rect{0.0, top, width, console_grain_band});
            painter.draw_text_runs(font, runs);
            painter.restore();
        }
    }

    [[nodiscard]] bool hit_test_local(Point) const override { return false; }

private:
    double advance_{};
    double baseline_{};
    double height_{};
    ConsoleGlyphRow row_{};
};

// The console's root: the darkened layer over the program's client area
// and its rows, filled from the top like a terminal; once full, the oldest
// row leaves at the top and the rest move up (their recorded drawing is
// moved, not repainted). A press on the button's place toggles the console
// (in case the host keeps the console above the button); a press anywhere
// else offers take-back.
class ConsoleSurface;

// A re-wrap posted from arrange, which must not change the tree itself.
struct RewrapWork final {
    std::weak_ptr<ConsoleSurface*> surface{};
    void operator()() const noexcept;
};

class ConsoleSurface final : public Control {
public:
    ConsoleSurface(StableId stable_id, ConsoleOverlay* overlay)
        : Control(std::move(stable_id)), overlay_(overlay),
          self_(gui_forms::detail::make_library_shared<ConsoleSurface*>(this)) {}
    ~ConsoleSurface() override { self_.reset(); }

    // New lines from the record.
    void take_lines(const std::vector<ConsoleLine>& lines) {
        for (const ConsoleLine& line : lines) {
            lines_.push_back(line);
            if (columns_ > 0U) wrap_line(line);
        }
        if (lines_.size() > kept_lines) {
            lines_.erase(lines_.begin(), lines_.begin() + static_cast<std::ptrdiff_t>(lines_.size() - kept_lines));
        }
        drop_overflow();
        invalidate(Dirty::arrange);
    }

    // Where the text begins (below a button inside the client area) and
    // the button's rectangle, in the surface's own units.
    void set_frame(double text_top, Rect button) {
        if (text_top != text_top_) invalidate(Dirty::arrange);
        text_top_ = text_top;
        button_ = button;
    }

    void arrange(Rect final_bounds) override {
        arrange_self(final_bounds);
        measure_face();
        const double usable = std::max(0.0, final_bounds.width - 2.0 * console_padding);
        const std::size_t columns =
            advance_ > 0.0 ? std::max<std::size_t>(16U, static_cast<std::size_t>(usable / advance_)) : 0U;
        const double text_height = std::max(0.0, final_bounds.height - text_top_ - console_padding);
        const std::size_t rows =
            line_height_ > 0.0 ? std::max<std::size_t>(1U, static_cast<std::size_t>(text_height / line_height_)) : 0U;
        if ((columns != columns_ || rows != visible_rows_) && !rewrap_posted_) {
            // A new width wraps every kept line again and a new height keeps
            // the newest rows that fit: at the window's next turn, since
            // arrange must not change the tree.
            wanted_columns_ = columns;
            wanted_rows_ = rows;
            SurfaceRoot* const window = attached_surface_root();
            if (window != nullptr) {
                try {
                    static_cast<void>((*window).begin_invoke(RewrapWork{self_}));
                    rewrap_posted_ = true;
                } catch (...) {
                    // The window is closing: nothing to wrap for.
                }
            }
        }
        for (std::size_t index = 0U; index < rows_.size(); ++index) {
            const double top = text_top_ + static_cast<double>(index) * line_height_;
            set_child_layout(rows_[index], Rect{console_padding, top, usable, line_height_});
        }
    }

    void on_paint(Painter& painter, Rect) override {
        const Rect bounds{0.0, 0.0, arranged_bounds().width, arranged_bounds().height};
        painter.fill_rect(bounds, console_background);
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

    void forget_overlay() noexcept { overlay_ = nullptr; }

    // The posted re-wrap: every kept line again, at the size arrange found.
    void rewrap() {
        rewrap_posted_ = false;
        columns_ = wanted_columns_;
        visible_rows_ = wanted_rows_;
        clear_children();
        rows_.clear();
        if (columns_ > 0U) {
            for (const ConsoleLine& line : lines_) wrap_line(line);
        }
        drop_overflow();
        invalidate(Dirty::arrange | Dirty::paint);
    }

private:
    // The face's advance and line height, measured once it is attached.
    void measure_face() {
        if (advance_ > 0.0) return;
        const FontSpec font = ConsoleOverlay::console_font();
        const TextExtent extent = measure_text_extent_utf8("0000000000", font);
        if (extent.logical_size.width <= 0.0) return;
        advance_ = extent.logical_size.width / 10.0;
        const double height = extent.ascent + extent.descent + extent.line_gap;
        line_height_ = std::ceil(height > 0.0 ? height * 1.15 : ConsoleOverlay::console_font_size * 1.4);
        // The baseline sits where the line's spare height is shared above
        // and below the face.
        const double ascent = extent.ascent > 0.0 ? extent.ascent : ConsoleOverlay::console_font_size * 0.8;
        ascent_ = std::floor((line_height_ - std::max(height, 0.0)) / 2.0 + ascent);
    }

    // `line` as rows of glyphs (console_wrap), each row a child.
    void wrap_line(const ConsoleLine& line) {
        std::vector<ConsoleGlyphRow> rows{};
        console_wrap(line, columns_, rows);
        for (ConsoleGlyphRow& row : rows) push_row(std::move(row));
    }

    void push_row(ConsoleGlyphRow glyphs) {
        ++row_serial_;
        const std::shared_ptr<ConsoleRow> row = make_control<ConsoleRow>(
            StableId(std::string(console_id) + ".row." + std::to_string(row_serial_)), advance_, ascent_,
            line_height_, std::move(glyphs));
        add_child(row);
        rows_.push_back(row);
    }

    // The oldest rows leave until the rest fit.
    void drop_overflow() {
        while (visible_rows_ > 0U && rows_.size() > visible_rows_) {
            static_cast<void>(remove_child((*rows_.front()).runtime_id()));
            rows_.erase(rows_.begin());
        }
    }

    ConsoleOverlay* overlay_{};
    std::shared_ptr<ConsoleSurface*> self_{};
    bool rewrap_posted_{};
    std::size_t wanted_columns_{};
    std::size_t wanted_rows_{};
    std::vector<ConsoleLine> lines_{};
    std::vector<std::shared_ptr<ConsoleRow>> rows_{};
    std::uint64_t row_serial_{};
    double advance_{};
    double ascent_{};
    double line_height_{};
    std::size_t columns_{};
    std::size_t visible_rows_{};
    double text_top_{console_padding};
    Rect button_{};
};

void RewrapWork::operator()() const noexcept {
    const std::shared_ptr<ConsoleSurface*> live = surface.lock();
    if (!live) return;
    try {
        (**live).rewrap();
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
        if (event.action != PointerAction::up || event.button != PointerButton::primary) return;
        event.handled = true;
        if (overlay_ != nullptr) (*overlay_).request_toggle();
    }

    void forget_overlay() noexcept { overlay_ = nullptr; }

private:
    ConsoleOverlay* overlay_{};
    std::shared_ptr<Spinner> spinner_{};
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
