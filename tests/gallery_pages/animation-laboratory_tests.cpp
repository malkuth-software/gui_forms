#include "support.hpp"
#include "gui_forms/controls/button.hpp"
#include "gui_forms/controls/check_box.hpp"
#include "gui_forms/controls/progress_bar.hpp"

namespace gallery_pages_tests {

void test_animation_laboratory(gui_forms::Window& window) {
    select_page(window, "animation-laboratory");
    const Control::Ptr easing = window.find("gallery.animation.easing");
    const std::shared_ptr<gui_forms::Button> pause_button = std::dynamic_pointer_cast<Button>(
        window.find("gallery.animation.pause"));
    const std::shared_ptr<gui_forms::ProgressBar> animated_progress = std::dynamic_pointer_cast<ProgressBar>(
        window.find("gallery.ranges.progress.2"));
    require(easing != nullptr && pause_button != nullptr && animated_progress != nullptr,
            "animation page must retain its public-frame proving surface");
    // The pause button binds the page's Command; the reduced-motion check box
    // binds the page's Value<bool>, so neither carries a caller-held handler.
    require((*pause_button).command_connected(),
            "animation pause button must bind the page-owned Command");
    const std::shared_ptr<gui_forms::CheckBox> reduced_box =
        std::dynamic_pointer_cast<CheckBox>(window.find("gallery.animation.reduced"));
    require(reduced_box != nullptr && !(*reduced_box).checked.read(),
            "reduced-motion check box must start from the page Value's false state");
    (*animated_progress).advance_animation(
        FrameClock::now() + std::chrono::milliseconds(410));
    const double retained_progress_phase = (*animated_progress).animation_phase();
    // The easing previews and progress bars animate on UI timers they stop
    // themselves (ADR-016).
    require(retained_progress_phase > 0.0,
            "gallery progress must advance its retained phase");
    require(window.next_wake().has_value(),
            "visible animation page must publish a retained frame deadline");
    click(window, window.find("gallery.animation.reduced"));
    require(window.next_wake().has_value() &&
                (*animated_progress).animation_phase() == retained_progress_phase &&
                has_semantic_state((*animated_progress).semantic_descriptor().states,
                                   SemanticState::busy),
            "reduced motion must retain every animated surface at a calm cadence");
    const std::vector<Rect> reduced_geometry_before = paint_fill_trace(easing);
    const double reduced_phase_before = (*animated_progress).animation_phase();
    // The reduced cadence's next tick, on the window's injected clock rather
    // than after a real wait (N104).
    const FrameClock::time_point reduced_tick = FrameClock::now() + std::chrono::milliseconds(110);
    const FramePollResult reduced_frame = window.poll_frame_schedule(reduced_tick);
    const std::vector<Rect> reduced_geometry = paint_fill_trace(easing);
    require(reduced_frame.ui_timer_ticks > 0U,
            "reduced motion must emit retained animation ticks");
    require(reduced_geometry != reduced_geometry_before,
            "reduced easing geometry must advance rather than present a frozen substitute");
    require((*animated_progress).animation_phase() == reduced_phase_before,
            "effectively hidden reduced progress must remain scheduler-suspended");
    const double reduced_progress_phase = reduced_phase_before;
    click(window, window.find("gallery.animation.pause"));
    // Focus moving to the pause button scrolls the page to reveal it, a
    // glide on the window's motion clock; let it finish first.
    static_cast<void>(window.poll_frame_schedule(FrameClock::now() + std::chrono::seconds(1)));
    require(!window.next_wake().has_value() &&
                (*pause_button).text.read() == "Resume motion",
            "pausing under reduced motion must quiesce its frame lease");
    const std::vector<Rect> paused_reduced_geometry = paint_fill_trace(easing);
    static_cast<void>(window.poll_frame_schedule(
        FrameClock::now() + std::chrono::seconds(1)));
    require(paint_fill_trace(easing) == paused_reduced_geometry,
            "paused reduced motion must retain stable geometry without wakeups");
    click(window, window.find("gallery.animation.pause"));
    require(window.next_wake().has_value() &&
                paint_fill_trace(easing) == paused_reduced_geometry &&
                (*animated_progress).animation_phase() == reduced_progress_phase &&
                (*pause_button).text.read() == "Pause motion",
            "one resume action under reduced motion must restore calm animation");
    click(window, window.find("gallery.animation.reduced"));
    require(window.next_wake().has_value() &&
                (*animated_progress).animation_phase() == reduced_progress_phase,
            "leaving reduced motion must preserve phase while restoring full cadence");
    click(window, window.find("gallery.status.live"));
    require(!window.next_wake().has_value() && !(*pause_button).enabled.read() &&
                (*pause_button).text.read() == "Motion disabled",
            "global live-off must quiesce motion and disable the page command");
    click(window, window.find("gallery.animation.reduced"));
    require(!window.next_wake().has_value(),
            "changing substitution while globally stopped must not restart motion");
    const std::vector<Rect> disabled_reduced_geometry = paint_fill_trace(easing);
    click(window, window.find("gallery.status.live"));
    require(window.next_wake().has_value() && (*pause_button).enabled.read() &&
                (*pause_button).text.read() == "Pause motion" &&
                paint_fill_trace(easing) == disabled_reduced_geometry,
            "one global live-on action must restore the retained reduced animation policy");

    // A connected deadline is not proof of animation. Repeatedly pause,
    // replace the animation timer, poll its first deadline, and require
    // the rendered marker geometry to advance each time.
    click(window, window.find("gallery.animation.reduced"));
    require(window.next_wake().has_value(),
            "leaving the reduced policy must arm exactly one continuous lease");
    for (std::size_t cycle = 0U; cycle < 8U; ++cycle) {
        click(window, window.find("gallery.animation.pause"));
        require(!window.next_wake().has_value() &&
                    (*pause_button).text.read() == "Resume motion",
                "every pause cycle must quiesce and publish one coherent command");
        const std::vector<Rect> before = paint_fill_trace(easing);
        click(window, window.find("gallery.animation.pause"));
        require(window.next_wake().has_value() &&
                    (*pause_button).text.read() == "Pause motion",
                "every resume cycle must replace the lease and publish one command");
        const FrameTime due = *window.next_wake();
        const FramePollResult frame = window.poll_frame_schedule(due);
        const std::vector<Rect> after = paint_fill_trace(easing);
        require(frame.ui_timer_ticks >= 1U && before != after,
                "a resumed deadline must advance visible easing geometry");
    }
    click(window, window.find("gallery.animation.pause"));
    require(!window.next_wake().has_value(),
            "the repeated-toggle fixture must reach a quiescent paused state");
    click(window, window.find("gallery.status.live"));
    require(!(*pause_button).enabled.read() && (*pause_button).text.read() == "Motion disabled",
            "the master gate must replace a paused command with one disabled state");
    click(window, window.find("gallery.status.live"));
    require(!window.next_wake().has_value() && (*pause_button).enabled.read() &&
                (*pause_button).text.read() == "Resume motion",
            "the master gate must preserve the orthogonal page-local pause latch");
    click(window, window.find("gallery.animation.pause"));
    require(window.next_wake().has_value() &&
                (*pause_button).text.read() == "Pause motion",
            "one explicit resume after a master-gate cycle must restore motion");
    click(window, window.find("gallery.animation.pause"));
    require(!window.next_wake().has_value(),
            "the repeated-toggle fixture must end in a quiescent paused state");
}

} // namespace gallery_pages_tests
