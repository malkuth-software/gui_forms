#pragma once

// The MCP console overlay (MCP 1.0.5; the owner's console ruling,
// DECISIONS 2026-11-24 "MCP console overlay and attach", with its
// amendments): what the user sees of a model's traffic while it holds the
// program.
//
// - The button: in AI mode (the model holds control), a small dark pill in
//   the upper right of the program's window with a turning spinner and
//   "thinking..."; a click opens the console, and another closes it. Hidden
//   by default: the console shows only when the user asks.
// - The console: a slightly transparent dark panel under the button,
//   filling the window's height like a terminal, in Portsmouth Mono (the
//   owner's console face; it is for surfaces made to look like a terminal,
//   never for interface text). Every MCP call and its reply, in the owner's
//   colours (console_log.hpp: the instruction orange, values and wrapping
//   green, each glyph run's shade varied slightly and dithered). It fills
//   from the top and, once full, scrolls a line at a time.
// - A press on the console (anywhere but the button) offers the take-back
//   dialog, as a press in the held program does: while open it is part of
//   the control surface.
//
// Both are their own native popup windows (passive, owned by the program's
// window, outside application modality), opened through the run's
// windowing, so:
// - adding a line repaints only the console's window; nothing of the
//   program beneath is repainted (the owner's concern);
// - they are never in a capture: the automation's capture draws the
//   program's own window and its menus' popup slots, and these are neither;
// - the automation never attaches, reads or drives them (presenter_window).
// They follow the program's window: a quarter-second timer on the button
// compares the window's frame and moves both when it moved or was resized.
//
// Ownership: the presenter owns the overlay; the overlay owns its two
// windows and destroys them when AI mode ends or it is destroyed. Thread:
// the UI thread of the automation's windows.

#include "console_log.hpp"

#include "gui_forms/application/application.hpp"
#include "gui_forms/scheduler/frame_request_token/frame_request_token.hpp"
#include "gui_forms/surface_root/surface_root.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace gui_forms::automation {

// Told when the user presses the console (the presenter offers take-back).
class ConsoleOverlayListener {
public:
    virtual ~ConsoleOverlayListener();
    virtual void console_pressed() = 0;
};

class ConsoleSurface;
class ConsoleButton;

class ConsoleOverlay final {
public:
    // The button's size and its inset from the window's upper right corner,
    // and the console's least width (logical units).
    static constexpr double button_width = 128.0;
    static constexpr double button_height = 28.0;
    static constexpr double inset = 10.0;
    static constexpr double minimum_console_width = 420.0;
    // How often the overlay compares the program window's frame.
    static constexpr std::chrono::milliseconds follow_interval{250};

    // `listener` must outlive the overlay.
    explicit ConsoleOverlay(ConsoleOverlayListener* listener);
    ~ConsoleOverlay();
    ConsoleOverlay(const ConsoleOverlay&) = delete;
    ConsoleOverlay& operator=(const ConsoleOverlay&) = delete;

    // The record the console shows; null shows nothing. Borrowed.
    void set_log(const ConsoleLog* log) noexcept { log_ = log; }

    // AI mode began on the program window `owner` (`owner_id`, its stable
    // ID): shows the button (the console stays as the user left it, closed
    // by default). False when the run cannot open it (no windowing, a host
    // without popup windows).
    bool show(const ApplicationWindowHandle& owner, std::string_view owner_id);
    // AI mode ended: the button and the console close.
    void hide() noexcept;
    // The button's click: opens the console, or closes it.
    void toggle_console();
    // The click and the console's press, from inside their windows' pointer
    // dispatch: each runs at that window's next turn.
    void request_toggle() noexcept;
    void request_press() noexcept;
    // New lines may be in the record: the open console takes them.
    void refresh();
    // The follow timer: moves both windows to the program window's frame.
    void follow();
    // A press on the console.
    void console_pressed();
    // A window of the overlay closed (the host closed it).
    void window_closed(bool console) noexcept;

    // Whether `window` is one of the overlay's.
    [[nodiscard]] bool owns(const SurfaceRoot& window) const noexcept;
    [[nodiscard]] bool button_shown() const noexcept;
    [[nodiscard]] bool console_open() const noexcept;

private:
    // Where the button and the console go for the program window's client
    // rectangle `content` (desktop logical units).
    [[nodiscard]] static Rect button_rect(Rect content) noexcept;
    [[nodiscard]] static Rect console_rect(Rect content) noexcept;
    // Opens one popup window at `bounds` with `root`; false when refused.
    bool open_popup(bool console, const Control::Ptr& root, Rect bounds, ApplicationWindowHandle& handle,
                    WindowRef& reference);
    void close_console() noexcept;
    void arm_follow() noexcept;

    ConsoleOverlayListener* listener_{};
    const ConsoleLog* log_{};
    // Posted work and timers hold this weakly; reset first in the
    // destructor.
    std::shared_ptr<ConsoleOverlay*> self_{};
    ApplicationWindowHandle owner_{};
    std::string owner_id_{};
    Rect owner_content_{};
    ApplicationWindowHandle button_handle_{};
    WindowRef button_window_{};
    std::shared_ptr<ConsoleButton> button_{};
    ApplicationWindowHandle console_handle_{};
    WindowRef console_window_{};
    std::shared_ptr<ConsoleSurface> console_{};
    // The last record line the console has taken.
    std::uint64_t shown_serial_{};
    FrameRequestToken follow_tick_{};
};

} // namespace gui_forms::automation
