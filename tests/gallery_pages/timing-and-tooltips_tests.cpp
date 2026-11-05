#include "support.hpp"

namespace gallery_pages_tests {

void test_timing_and_tooltips(gui_forms::Window& window) {
    select_page(window, "timing-and-tooltips");
    std::shared_ptr<gui_forms::Label> status = std::dynamic_pointer_cast<Label>(
        window.find("gallery.timing.tick.status"));
    std::shared_ptr<gui_forms::ProgressBar> progress = std::dynamic_pointer_cast<ProgressBar>(
        window.find("gallery.timing.progress"));
    require(status && progress && window.next_wake().has_value(),
            "timing page must activate its public UI Timer without a hidden control");
    const std::string before = (*status).text();
    const FrameTime timer_deadline = *window.next_wake();
    const FramePollResult timer_tick = window.poll_frame_schedule(timer_deadline);
    require(timer_tick.ui_timer_ticks == 1U && (*status).text() != before &&
                (*progress).value() > 0.0,
            "gallery Timer tick must update retained public controls on the UI queue");
    click(window, window.find("gallery.timing.stop"));
    require(!window.next_wake().has_value() &&
                (*status).text().find("zero idle wakeups") != std::string::npos,
            "gallery Timer stop must make the window scheduler quiescent");

    // Start/stop bind page-owned Commands; the interval TrackBar binds a
    // Value<double>, so a programmatic state change (no scroll input) reaches
    // the Timer and the next tick reports the new interval.
    const std::shared_ptr<gui_forms::Button> start_button =
        std::dynamic_pointer_cast<Button>(window.find("gallery.timing.start"));
    const std::shared_ptr<gui_forms::Button> stop_button =
        std::dynamic_pointer_cast<Button>(window.find("gallery.timing.stop"));
    const std::shared_ptr<gui_forms::TrackBar> interval =
        std::dynamic_pointer_cast<TrackBar>(window.find("gallery.timing.interval"));
    require(start_button && stop_button && interval &&
                (*start_button).command_connected() &&
                (*stop_button).command_connected(),
            "timer buttons must bind page-owned Commands");
    (*interval).set_value(400.0);
    click(window, start_button);
    require(window.next_wake().has_value() &&
                (*status).text().find("deadline armed") != std::string::npos,
            "the start Command must arm the UI Timer deadline");
    static_cast<void>(window.poll_frame_schedule(
        FrameClock::now() + std::chrono::seconds(1)));
    require((*status).text().find("400 ms") != std::string::npos,
            "the bound interval Value must reach the Timer before its next tick");
    click(window, stop_button);
    require(!window.next_wake().has_value(),
            "the stop Command must make the window scheduler quiescent again");

    const Control::Ptr hover_target = window.find("gallery.timing.tooltip.hover");
    const Rect hover_bounds = (*hover_target).absolute_bounds();
    const Point hover_point{hover_bounds.x + 12.0, hover_bounds.y + 12.0};
    static_cast<void>(window.dispatch_pointer(
        {PointerAction::move, PointerButton::none, hover_point}));
    require(window.next_wake().has_value(),
            "gallery hover target must arm the public ToolTip delay");
    static_cast<void>(window.poll_frame_schedule(*window.next_wake()));
    window.perform_layout();
    require(window.semantic_snapshot().to_json().find("\"role\":\"tool_tip\"") !=
                std::string::npos,
            "gallery ToolTip must materialize as a retained semantic overlay");
    static_cast<void>(window.dispatch_pointer(
        {PointerAction::move, PointerButton::none, {1000.0, 700.0}}));
    require(window.semantic_snapshot().to_json().find("\"role\":\"tool_tip\"") ==
                std::string::npos,
            "gallery ToolTip must cancel immediately when hover leaves its owner");
    click(window, window.find("gallery.timing.tooltip.show-disabled"));
    require(window.semantic_snapshot().to_json().find(
                "This action is disabled because no compatible device is selected") !=
                std::string::npos,
            "gallery must explain a disabled target through passive overlay ownership");
}

} // namespace gallery_pages_tests
