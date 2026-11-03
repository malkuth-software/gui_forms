#include "support.hpp"

namespace gallery_pages_tests {

void test_ranges_and_progress(gui_forms::Window& window) {
    select_page(window, "ranges-and-progress");
    require(window.semantic_snapshot().to_json().find("\"role\":\"scroll_bar\"") !=
                std::string::npos,
            "gallery range page must publish native-neutral scrollbar semantics");
    std::shared_ptr<gui_forms::TrackBar> slider = std::dynamic_pointer_cast<TrackBar>(
        window.find("gallery.ranges.slider.1"));
    std::shared_ptr<gui_forms::ProgressBar> progress = std::dynamic_pointer_cast<ProgressBar>(
        window.find("gallery.ranges.progress.1"));
    std::shared_ptr<gui_forms::HScrollBar> horizontal_scroll = std::dynamic_pointer_cast<HScrollBar>(
        window.find("gallery.ranges.scroll.horizontal"));
    std::shared_ptr<gui_forms::VScrollBar> vertical_scroll = std::dynamic_pointer_cast<VScrollBar>(
        window.find("gallery.ranges.scroll.vertical"));
    const std::shared_ptr<gui_forms::Label> shared = std::dynamic_pointer_cast<Label>(
        window.find("gallery.ranges.shared"));
    std::shared_ptr<gui_forms::SweepScope> scope = std::dynamic_pointer_cast<SweepScope>(
        window.find("gallery.ranges.scope"));
    require(slider && progress && horizontal_scroll && vertical_scroll && shared && scope,
            "range proving controls must retain public slider, progress, and scrollbar types");
    require((*slider).value() == 52.0 && (*progress).value() == 52.0 &&
                (*horizontal_scroll).value() == 52.0 && (*vertical_scroll).value() == 52.0 &&
                (*scope).value() == 52.0 &&
                (*shared).text().find("level = 52") != std::string::npos,
            "every range control must start from the one bound Value<double>");
    const Rect track = (*slider).absolute_bounds();
    const Point from{track.x + track.width * 0.2, track.y + track.height * 0.5};
    const Point to{track.x + track.width * 0.82, track.y + track.height * 0.5};
    require(window.dispatch_pointer({PointerAction::down, PointerButton::primary, from}) &&
                window.dispatch_pointer({PointerAction::move, PointerButton::none, to}) &&
                window.dispatch_pointer({PointerAction::up, PointerButton::primary, to}) &&
                (*slider).value() > 75.0 && (*progress).value() == (*slider).value() &&
                (*horizontal_scroll).value() == (*slider).value() &&
                (*vertical_scroll).value() == (*slider).value(),
            "gallery TrackBar drag must move every control bound to the shared Value");
    require((*shared).text().find("last input: Filled rail") != std::string::npos,
            "the input-only scroll event must name the control the user dragged");
    const Rect scroll_bounds = (*horizontal_scroll).absolute_bounds();
    const Rect scroll_thumb = (*horizontal_scroll).thumb_bounds();
    const Point scroll_from{scroll_bounds.x + scroll_thumb.x + scroll_thumb.width * 0.5,
                            scroll_bounds.y + scroll_thumb.height * 0.5};
    const Point scroll_to{scroll_bounds.x + (*horizontal_scroll).track_bounds().x +
                              (*horizontal_scroll).track_bounds().width - 2.0,
                          scroll_from.y};
    require(window.dispatch_pointer({PointerAction::down, PointerButton::primary,
                                     scroll_from}) &&
                window.dispatch_pointer({PointerAction::move, PointerButton::none,
                                         scroll_to}) &&
                window.dispatch_pointer({PointerAction::up, PointerButton::primary,
                                         scroll_to}) &&
                (*horizontal_scroll).value() > 95.0 &&
                (*vertical_scroll).value() == (*horizontal_scroll).value() &&
                (*slider).value() == (*horizontal_scroll).value(),
            "gallery ScrollBar drag must track continuously and move the bound slider");
    require((*shared).text().find("horizontal ScrollBar") != std::string::npos,
            "the status must report the scrollbar as the last input source");
    // A programmatic state change reaches every bound control and the state
    // observer, but synthesizes no input event.
    (*progress).set_value(40.0);
    require((*slider).value() == 40.0 && (*vertical_scroll).value() == 40.0 && (*scope).value() == 40.0 &&
                (*shared).text().find("level = 40") != std::string::npos &&
                (*shared).text().find("horizontal ScrollBar") != std::string::npos,
            "a programmatic set_value must propagate through the Value without a new input report");

    // Reproduce the reported interaction family without depending on wall
    // clock luck: mutate a captured slider continuously, release it, replace
    // the visible retained page immediately, service the new animation lease,
    // and paint. Repeating both directions exercises stale deadlines and page
    // visibility invalidation rather than merely proving that Animation opens.
    const std::uint64_t frame_faults_before =
        window.metrics_snapshot().frame_callback_faults;
    for (std::size_t cycle = 0U; cycle < 48U; ++cycle) {
        select_page(window, "ranges-and-progress");
        const Rect live_track = (*slider).absolute_bounds();
        const double start_ratio = cycle % 2U == 0U ? 0.18 : 0.82;
        const double finish_ratio = cycle % 2U == 0U ? 0.82 : 0.18;
        const Point drag_start{
            live_track.x + live_track.width * start_ratio,
            live_track.y + live_track.height * 0.5};
        require(window.dispatch_pointer(
                    {PointerAction::down, PointerButton::primary, drag_start}),
                "slider-to-animation stress must acquire the TrackBar");
        for (std::size_t step = 1U; step <= 9U; ++step) {
            const double ratio = start_ratio +
                (finish_ratio - start_ratio) *
                    static_cast<double>(step) / 9.0;
            require(window.dispatch_pointer(
                        {PointerAction::move, PointerButton::none,
                         {live_track.x + live_track.width * ratio,
                          drag_start.y}}),
                    "slider-to-animation stress must deliver every captured move");
        }
        const Point drag_finish{
            live_track.x + live_track.width * finish_ratio, drag_start.y};
        require(window.dispatch_pointer(
                    {PointerAction::up, PointerButton::primary, drag_finish}) &&
                    !window.captured_control(),
                "slider-to-animation stress must release capture before navigation");

        select_page(window, "animation-laboratory");
        // The page's animations run on paint-gated timers (ADR-016): a
        // control hidden on the other page waits for its next paint, so the
        // deadline follows the transition's paint.
        CountingPainter transition_painter;
        const DamageRegion transition_damage = window.take_damage();
        static_cast<void>(window.paint(
            transition_painter,
            transition_damage.empty()
                ? Rect{0.0, 0.0, 1280.0, 820.0}
                : transition_damage.bounds()));
        require(transition_painter.commands > 0U,
                "slider-to-animation transition must complete a retained paint");
        require(window.next_wake().has_value(),
                "opening Animation after a slider drag must retain a live deadline");
        static_cast<void>(window.poll_frame_schedule(*window.next_wake()));
    }
    require(window.metrics_snapshot().frame_callback_faults ==
                frame_faults_before,
            "slider-to-animation stress must not fault an animation timer callback");
}

} // namespace gallery_pages_tests
