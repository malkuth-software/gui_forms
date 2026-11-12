#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/scheduler.hpp"

#include <cstdint>
#include <functional>
#include <memory>

namespace gui_forms {

class Control;

// What one animation tick did: `damaged` when it invalidated what it
// changed (the timer then expects a paint before the next tick), and
// `unchanged` when nothing moved (a calm or finished state stays scheduled
// without being painted).
enum class AnimationTickResult : std::uint8_t {
    damaged,
    unchanged,
};

// The display interval an animation paces to when it needs every frame.
inline constexpr FrameInterval display_frame_interval = std::chrono::milliseconds(16);

// The interval at which something travelling `device_pixels_per_second`
// moves about one device pixel a tick, never shorter than `display_interval`.
// A speed that is not positive and finite gets `display_interval`.
[[nodiscard]] GUI_FORMS_API FrameInterval animation_interval_for_travel(
    double device_pixels_per_second,
    FrameInterval display_interval = display_frame_interval) noexcept;

// A paint-gated UI timer for one control's animation (ADR-016). It ticks
// only while the control is in a window and shown, and only as fast as the
// control is painted: a tick that finds the control hidden, or not painted
// since the last damaging tick, stops the timer, and the control's next
// paint (note_painted) starts it again. An interval of at least
// display_frame_interval ticks at the nearest whole number of display
// intervals, so that animations of different speeds share frames; a shorter
// one ticks as given. Ticks fall on multiples of that period in clock time
// and keep that phase; a late tick is one tick.
//
// The control owns the timer as a member and calls note_painted at the start
// of its on_paint. The tick target is called on the window's UI thread; it
// may call start or stop. The timer observes the control and never keeps it
// alive; destroying the timer stops it.
class GUI_FORMS_API AnimationTimer final {
public:
    using Tick = std::function<AnimationTickResult(FrameTime)>;

    AnimationTimer() noexcept;
    ~AnimationTimer();
    AnimationTimer(const AnimationTimer&) = delete;
    AnimationTimer& operator=(const AnimationTimer&) = delete;
    AnimationTimer(AnimationTimer&&) = delete;
    AnimationTimer& operator=(AnimationTimer&&) = delete;

    // Starts (or restarts) ticking `tick` every `interval` for `owner`.
    // When `owner` is not in a window or not shown, the timer waits for its
    // next paint. An interval shorter than minimum_ui_timer_interval throws
    // std::invalid_argument, as does an empty `tick`; either leaves the timer
    // as it was.
    void start(Control& owner, FrameInterval interval, Tick tick);
    // Stops the timer; note_painted no longer starts it.
    void stop() noexcept;
    // The owner was painted: counts the paint, and starts a timer that was
    // waiting for it.
    void note_painted();

    // Started and not stopped, whether ticking or waiting for a paint.
    [[nodiscard]] bool started() const noexcept;
    // A tick is scheduled on the owner's window.
    [[nodiscard]] bool ticking() const noexcept;
    // The interval given to start, before rounding to display intervals.
    [[nodiscard]] FrameInterval interval() const noexcept;

private:
    struct State;
    // The window's callback target: observes the state, so a destroyed
    // timer's tick is skipped.
    struct Callback final {
        std::weak_ptr<State> state;
        void operator()(FrameTime now) const;
    };

    static void schedule(const std::shared_ptr<State>& state);
    static void on_tick(const std::shared_ptr<State>& state, FrameTime now);
    // Window::cancel_frame_requests stops the timers of its window as it
    // ends their requests: a ticking timer loses its request, and one
    // waiting for a paint, which holds none, finds the window's count of
    // cancellations moved since it began waiting there. Either is stopped,
    // as by stop(), and no paint starts it again; start does.
    [[nodiscard]] static bool cancelled_by_window(const State& state) noexcept;

    std::shared_ptr<State> state_;
};

} // namespace gui_forms
