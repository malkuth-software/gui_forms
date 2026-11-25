#include "console_overlay.hpp"

#include "../../../core/application/application_state.hpp"

#include "gui_forms/controls/spinner.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/host/compositor/window_backdrop.hpp"
#include "gui_forms/host/windowing/host_windowing.hpp"
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

constexpr const char* button_id = "automation.console-button";
constexpr const char* console_id = "automation.console";
// The console's face and size: Portsmouth Mono, the owner's console face.
constexpr double console_font_size = 12.5;
constexpr double console_padding = 10.0;
// The lines the console keeps for re-wrapping when its width changes.
constexpr std::size_t kept_lines = 400U;
// Dark, slightly transparent: the panel and the button's pill.
constexpr Color console_background{10U, 10U, 12U, 214U};
constexpr Color button_background{10U, 10U, 12U, 232U};
constexpr Color button_text{230U, 230U, 230U, 255U};

FontSpec console_font() noexcept {
    FontSpec font{};
    font.role = FontRole::monospace;
    font.size = console_font_size;
    font.family = portsmouth_mono_font_family;
    return font;
}

// The number of code points in `text` (UTF-8 continuation bytes are not
// counted): the columns it takes in a monospace face.
std::size_t columns_of(std::string_view text) noexcept {
    std::size_t count = 0U;
    for (const char character : text) {
        if ((static_cast<unsigned char>(character) & 0xc0U) != 0x80U) ++count;
    }
    return count;
}

// The byte length of the first `columns` code points of `text`.
std::size_t bytes_for_columns(std::string_view text, std::size_t columns) noexcept {
    std::size_t seen = 0U;
    for (std::size_t index = 0U; index < text.size(); ++index) {
        if ((static_cast<unsigned char>(text[index]) & 0xc0U) != 0x80U) {
            if (seen == columns) return index;
            ++seen;
        }
    }
    return text.size();
}

} // namespace

// One row of the console: glyph runs at their columns, each in its colour.
// It takes no input (a press reaches the console surface).
class ConsoleRow final : public Control {
public:
    struct Run final {
        std::string text{};
        Color colour{};
        std::size_t column{};
    };

    ConsoleRow(StableId stable_id, double advance, double baseline, std::vector<Run> runs)
        : Control(std::move(stable_id)), advance_(advance), baseline_(baseline), runs_(std::move(runs)) {}

    void on_paint(Painter& painter, Rect) override {
        const FontSpec font = console_font();
        for (const Run& run : runs_) {
            painter.draw_text_utf8(Point{static_cast<double>(run.column) * advance_, baseline_}, run.text, font,
                                   run.colour);
        }
    }

    [[nodiscard]] bool hit_test_local(Point) const override { return false; }

private:
    double advance_{};
    double baseline_{};
    std::vector<Run> runs_{};
};

// The console's root: the dark panel and its rows, filled from the top;
// once full, the oldest row leaves at the top and the rest move up (their
// recorded drawing is moved, not repainted). A press anywhere offers
// take-back.
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

    void arrange(Rect final_bounds) override {
        arrange_self(final_bounds);
        measure_face();
        const double usable = std::max(0.0, final_bounds.width - 2.0 * console_padding);
        const std::size_t columns =
            advance_ > 0.0 ? std::max<std::size_t>(16U, static_cast<std::size_t>(usable / advance_)) : 0U;
        const double text_height = std::max(0.0, final_bounds.height - 2.0 * console_padding);
        const std::size_t rows =
            line_height_ > 0.0 ? std::max<std::size_t>(1U, static_cast<std::size_t>(text_height / line_height_)) : 0U;
        if ((columns != columns_ || rows != visible_rows_) && !rewrap_posted_) {
            // A new width wraps every kept line again and a new height keeps
            // the newest rows that fit: at the window's next turn, since
            // arrange must not change the tree.
            wanted_columns_ = columns;
            wanted_rows_ = rows;
            SurfaceRoot* const window = attached_surface_root().get();
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
            const double top = console_padding + static_cast<double>(index) * line_height_;
            set_child_layout(rows_[index], Rect{console_padding, top, usable, line_height_});
        }
    }

    void on_paint(Painter& painter, Rect) override {
        const Rect bounds{0.0, 0.0, arranged_bounds().width, arranged_bounds().height};
        painter.fill_rounded_rect(bounds, 8.0, console_background);
    }

    void on_pointer_preview(PointerEvent& event) override {
        if (event.action != PointerAction::down) return;
        event.handled = true;
        if (overlay_ != nullptr) (*overlay_).request_press();
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
        const FontSpec font = console_font();
        const TextExtent extent = measure_text_extent_utf8("0000000000", font);
        if (extent.logical_size.width <= 0.0) return;
        advance_ = extent.logical_size.width / 10.0;
        ascent_ = extent.ascent > 0.0 ? extent.ascent : console_font_size * 0.8;
        const double height = extent.ascent + extent.descent + extent.line_gap;
        line_height_ = std::ceil(height > 0.0 ? height * 1.15 : console_font_size * 1.4);
    }

    // `line` as rows of at most columns_ code points: each span's words are
    // glyph runs, each in its own shade of the span's tone; a row the line
    // wraps onto is all value (the ruling: values and wrapping are green).
    void wrap_line(const ConsoleLine& line) {
        std::vector<ConsoleRow::Run> runs{};
        std::size_t column = 0U;
        std::size_t run_index = 0U;
        bool wrapped = false;
        for (const ConsoleSpan& span : line.spans) {
            std::string_view rest(span.text);
            while (!rest.empty()) {
                // The next word with the spaces after it.
                std::size_t end = rest.find(' ');
                end = end == std::string_view::npos ? rest.size() : rest.find_first_not_of(' ', end);
                if (end == std::string_view::npos) end = rest.size();
                std::string_view word = rest.substr(0U, end);
                rest.remove_prefix(end);
                while (!word.empty()) {
                    if (column == columns_) {
                        push_row(std::move(runs));
                        runs.clear();
                        column = 0U;
                        wrapped = true;
                    }
                    const std::size_t room = columns_ - column;
                    const std::size_t take = bytes_for_columns(word, room);
                    const std::string_view piece = word.substr(0U, take);
                    word.remove_prefix(take);
                    const ConsoleTone tone = wrapped ? ConsoleTone::value : span.tone;
                    const ConsoleShade shade = console_shade(tone, line.serial, run_index);
                    ConsoleRow::Run run{};
                    run.text = std::string(piece);
                    run.colour = console_colour(shade, console_run_hash(line.serial, run_index) ^ 0x1dULL);
                    run.column = column;
                    runs.push_back(std::move(run));
                    column += columns_of(piece);
                    ++run_index;
                }
            }
        }
        push_row(std::move(runs));
    }

    void push_row(std::vector<ConsoleRow::Run> runs) {
        ++row_serial_;
        const std::shared_ptr<ConsoleRow> row = make_control<ConsoleRow>(
            StableId(std::string(console_id) + ".row." + std::to_string(row_serial_)), advance_, ascent_,
            std::move(runs));
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
        FontSpec font = console_font();
        font.size = 12.0;
        const TextExtent extent = measure_text_extent_utf8("thinking...", font);
        const double baseline = (bounds.height - extent.logical_size.height) / 2.0 + extent.ascent;
        painter.draw_text_utf8(Point{bounds.height + 6.0, baseline}, "thinking...", font, button_text);
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
            (**live).follow(event.frame.content);
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

Rect ConsoleOverlay::button_rect(Rect content) noexcept {
    const Rect rect{content.x + content.width - button_width - inset, content.y + inset, button_width, button_height};
    return rect;
}

Rect ConsoleOverlay::console_rect(Rect content) noexcept {
    const double top = content.y + inset + button_height + 6.0;
    const double height = std::max(80.0, content.y + content.height - inset - top);
    const double width = std::min(std::max(minimum_console_width, content.width * 0.55), content.width - 2.0 * inset);
    const Rect rect{content.x + content.width - inset - std::max(width, 120.0), top, std::max(width, 120.0), height};
    return rect;
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

bool ConsoleOverlay::show(const ApplicationWindowHandle& owner, std::string_view owner_id) {
    if (button_handle_.active()) return true;
    owner_ = owner;
    owner_id_ = std::string(owner_id);
    if (!owner_.active()) return false;
    const HostWindowFrameResult frame = owner_.frame();
    if (!frame.status.accepted()) return false;
    owner_content_ = frame.frame.content;
    button_ = make_control<ConsoleButton>(StableId(button_id), this);
    if (!open_popup(false, button_, button_rect(owner_content_), button_handle_, button_window_)) {
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
    if (!open_popup(true, console_, console_rect(owner_content_), console_handle_, console_window_)) {
        (*console_).forget_overlay();
        console_.reset();
        return;
    }
    shown_serial_ = 0U;
    refresh();
}

void ConsoleOverlay::refresh() {
    if (!console_ || log_ == nullptr) return;
    std::vector<ConsoleLine> lines{};
    (*log_).copy_since(shown_serial_, lines);
    if (lines.empty()) return;
    shown_serial_ = lines.back().serial;
    (*console_).take_lines(lines);
}

void ConsoleOverlay::follow(Rect content) {
    if (!button_handle_.active() || content == owner_content_) return;
    owner_content_ = content;
    static_cast<void>(button_handle_.request_frame(button_rect(owner_content_)));
    if (console_handle_.active()) static_cast<void>(console_handle_.request_frame(console_rect(owner_content_)));
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
