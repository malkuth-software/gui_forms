#include "pages.hpp"

namespace gui_forms::gallery::pages {

// One application-owned level drives every range control on the page: the
// three TrackBars, both ScrollBars, all six ProgressBars and the SweepScope
// (the instruments package's level scope) bind the same
// Value<double>, so any user drag moves them all without a copy chain. The
// owner observes the state event (every change) and the input-only `scroll`
// events (user gestures) separately, as the state/input table requires.
class RangesPage final : public gui_forms::Component {
public:
    RangesPage(const std::shared_ptr<Surface>& body,
               const std::shared_ptr<PageContext>& context) {
        (*body).add_at(label("gallery.ranges.heading", "RANGES AND PROGRESS", 22.0, 700,
                             shell_blue_dark), {24.0, 18.0, 500.0, 34.0});
        (*body).add_at(label("gallery.ranges.lead",
                             "Live captured dragging, keyboard control, vertical orientation, and retained animation.",
                             12.0, 400, muted), {26.0, 52.0, 850.0, 24.0});
        build_sliders(body);
        build_progress(body, context);
        build_scope(body);
        on(level_.changed(), *this, &RangesPage::on_level_changed);
        on_level_changed(level_.get());
    }

private:
    static constexpr std::array<std::string_view, 3> track_names{
        "Classic ticks", "Filled rail", "Compact rail"};
    static constexpr std::array<std::string_view, 6> progress_names{
        "Blocks", "Continuous", "Marquee", "Luminance pulse", "Marching stripes",
        "Laser etch"};
    static constexpr int horizontal_source = 3;
    static constexpr int vertical_source = 4;

    void build_sliders(const std::shared_ptr<Surface>& body) {
        std::shared_ptr<LayoutGroup> sliders = group(
            "gallery.ranges.sliders", "TrackBar · classic, filled, compact · ScrollBar",
            {930.0, 230.0});
        (*body).add_at(sliders, {24.0, 92.0, 930.0, 230.0});
        for (std::size_t index = 0; index < track_names.size(); ++index) {
            const double row = static_cast<double>(index) * 58.0;
            (*sliders).add_at(label("gallery.ranges.slider.caption." + std::to_string(index),
                                    std::string(track_names[index]), 11.0, 600, muted),
                              {24.0, 38.0 + row, 130.0, 24.0});
            std::shared_ptr<TrackBar> track = make_control<TrackBar>(
                StableId("gallery.ranges.slider." + std::to_string(index)));
            (*track).set_accessible_name(std::string(track_names[index]));
            (*track).set_visual_style(static_cast<TrackBarVisualStyle>(index));
            (*track).bind(level_);
            (*sliders).add_at(track, {155.0, 35.0 + row, 690.0, 32.0});
            on((*track).scroll(), *this, &RangesPage::on_scroll, static_cast<int>(index));
        }
        std::shared_ptr<HScrollBar> horizontal = make_control<HScrollBar>(
            StableId("gallery.ranges.scroll.horizontal"));
        (*horizontal).set_accessible_name("Horizontal viewport scrollbar");
        (*horizontal).set_range(0.0, 100.0);
        (*horizontal).set_large_change(18.0);
        (*horizontal).bind(level_);
        (*sliders).add_at(horizontal, {155.0, 200.0, 690.0, 18.0});
        (*sliders).add_at(label("gallery.ranges.scroll.caption", "Viewport", 11.0, 600, muted),
                          {24.0, 197.0, 120.0, 22.0});
        on((*horizontal).scroll(), *this, &RangesPage::on_scroll, horizontal_source);

        std::shared_ptr<VScrollBar> vertical = make_control<VScrollBar>(
            StableId("gallery.ranges.scroll.vertical"));
        (*vertical).set_accessible_name("Vertical viewport scrollbar");
        (*vertical).set_range(0.0, 100.0);
        (*vertical).set_large_change(18.0);
        (*vertical).bind(level_);
        (*sliders).add_at(vertical, {874.0, 38.0, 18.0, 160.0});
        on((*vertical).scroll(), *this, &RangesPage::on_scroll, vertical_source);
    }

    void build_progress(const std::shared_ptr<Surface>& body,
                        const std::shared_ptr<PageContext>& context) {
        std::shared_ptr<LayoutGroup> progress_group = group(
            "gallery.ranges.progress",
            "ProgressBar · static, indeterminate, luminance, marching, laser",
            {930.0, 325.0});
        (*body).add_at(progress_group, {24.0, 340.0, 930.0, 325.0});
        for (std::size_t index = 0; index < progress_names.size(); ++index) {
            const double row = static_cast<double>(index) * 45.0;
            (*progress_group).add_at(
                label("gallery.ranges.progress.caption." + std::to_string(index),
                      std::string(progress_names[index]), 11.0, 600, muted),
                {24.0, 36.0 + row, 122.0, 24.0});
            std::shared_ptr<ProgressBar> progress = make_control<ProgressBar>(
                StableId("gallery.ranges.progress." + std::to_string(index)));
            (*progress).set_accessible_name(std::string(progress_names[index]) + " progress");
            (*progress).set_visual_style(static_cast<ProgressBarVisualStyle>(index));
            if (index == 3U) {
                (*progress).set_animation_period(std::chrono::milliseconds(2400));
            } else if (index == 4U) {
                (*progress).set_animation_period(std::chrono::milliseconds(850));
            } else if (index == 5U) {
                (*progress).set_animation_period(std::chrono::milliseconds(1250));
            }
            (*progress).bind(level_);
            (*progress_group).add_at(progress, {160.0, 38.0 + row, 695.0, 24.0});
            // The shell's motion policy (pause, reduced motion) still reaches
            // the animated styles through the page context.
            if (index >= 2U) (*context).animated_progress.push_back(progress);
        }
        shared_ = label("gallery.ranges.shared", "", 10.5, 600, green);
        (*progress_group).add_at(shared_, {160.0, 292.0, 740.0, 24.0});
    }

    // The instruments package's level scope: the level as a trace, crossed
    // by its sweep (an effect program on the window's motion clock).
    void build_scope(const std::shared_ptr<Surface>& body) {
        std::shared_ptr<LayoutGroup> scope_group = group(
            "gallery.ranges.instruments", "SweepScope · instruments: level trace and effect-program sweep",
            {930.0, 170.0});
        (*body).add_at(scope_group, {24.0, 683.0, 930.0, 170.0});
        std::shared_ptr<SweepScope> scope = make_control<SweepScope>(StableId("gallery.ranges.scope"));
        (*scope).set_accessible_name("Level scope");
        (*scope).bind(level_);
        (*scope_group).add_at(scope, {160.0, 36.0, 220.0, 120.0});
    }

    // State: fires for every change of the shared level, user or programmatic.
    void on_level_changed(const double value) {
        level_text_ = "Value<double> level = " + std::to_string(static_cast<int>(value)) +
                      " · bound to 3 TrackBars, 2 ScrollBars, 6 ProgressBars, 1 SweepScope";
        publish_status();
    }

    // Input: fires only for the user's gesture on the originating control.
    void on_scroll(const int source, const RangeScrollEvent& event) {
        input_text_ = " · last input: " + source_name(source) + " (" +
                      std::string(range_action_name(event.action)) + ")";
        publish_status();
    }

    void publish_status() {
        if (shared_) (*shared_).set_text(level_text_ + input_text_);
    }

    [[nodiscard]] static std::string source_name(const int source) {
        if (source == horizontal_source) return "horizontal ScrollBar";
        if (source == vertical_source) return "vertical ScrollBar";
        if (source >= 0 && static_cast<std::size_t>(source) < track_names.size()) {
            return std::string(track_names[static_cast<std::size_t>(source)]);
        }
        return "range control";
    }

    [[nodiscard]] static std::string_view range_action_name(const RangeAction action) {
        switch (action) {
        case RangeAction::small_decrement: return "small decrement";
        case RangeAction::small_increment: return "small increment";
        case RangeAction::large_decrement: return "large decrement";
        case RangeAction::large_increment: return "large increment";
        case RangeAction::first: return "first";
        case RangeAction::last: return "last";
        case RangeAction::thumb_track: return "thumb track";
        case RangeAction::thumb_position: return "thumb position";
        }
        return "input";
    }

    Value<double> level_{52.0};
    std::shared_ptr<Label> shared_;
    std::string level_text_;
    std::string input_text_;
};

void add_ranges(const std::shared_ptr<Surface>& page,
                const std::shared_ptr<PageContext>& context) {
    (*context).components.add(std::make_shared<RangesPage>(page, context));
}

} // namespace gui_forms::gallery::pages
