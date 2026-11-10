#include "pages.hpp"

#include "ranges-and-progress_form.hpp"

namespace gui_forms::gallery::pages {

// Owns the ranges page: the controls demo/pages/ranges-and-progress.form
// describes, and their behaviour. One application-owned level drives every range control on the page: the
// three TrackBars, both ScrollBars, all six ProgressBars and the SweepScope
// (the instruments package's level scope) bind the same
// Value<double>, so any user drag moves them all without a copy chain. The
// owner observes the state event (every change) and the input-only `scroll`
// events (user gestures) separately, as the state/input table requires.
class RangesPage final : public gui_forms::Component {
public:
    RangesPage(const std::shared_ptr<Surface>& body,
               const std::shared_ptr<PageContext>& context) {
        document_.initialize_component(*this);
        mount_document(body);
        connect_sliders();
        connect_progress(context);
        (*document_.gallery_ranges_scope).bind(level_);
        on(level_.changed(), *this, &RangesPage::on_level_changed);
        on_level_changed(level_.get());
    }

private:
    static constexpr std::array<std::string_view, 3> track_names{
        "Classic ticks", "Filled rail", "Compact rail"};
    static constexpr int horizontal_source = 3;
    static constexpr int vertical_source = 4;

    void mount_document(const std::shared_ptr<Surface>& body) {
        // The document's root is a declared boundary only: its children move
        // into the shell's page body, a design surface of the same 1000 x 660
        // size, where each is placed by its bounds in design coordinates. The
        // copy of the child list keeps the iteration valid while the root
        // shrinks.
        Control& root = *document_.gallery_ranges_document;
        const std::vector<Control::Ptr> children(root.children().begin(), root.children().end());
        for (const Control::Ptr& child : children) {
            Control::Ptr moved = root.remove_child((*child).runtime_id());
            (*body).add_child(std::move(moved));
        }
        shared_ = document_.gallery_ranges_shared;
    }

    void connect_sliders() {
        const std::array<std::shared_ptr<TrackBar>, 3> tracks{
            document_.gallery_ranges_slider_0, document_.gallery_ranges_slider_1,
            document_.gallery_ranges_slider_2};
        for (std::size_t index = 0; index < tracks.size(); ++index) {
            (*tracks[index]).bind(level_);
            on((*tracks[index]).scroll(), *this, &RangesPage::on_scroll, static_cast<int>(index));
        }
        HScrollBar& horizontal = *document_.gallery_ranges_scroll_horizontal;
        horizontal.bind(level_);
        on(horizontal.scroll(), *this, &RangesPage::on_scroll, horizontal_source);
        VScrollBar& vertical = *document_.gallery_ranges_scroll_vertical;
        vertical.bind(level_);
        on(vertical.scroll(), *this, &RangesPage::on_scroll, vertical_source);
    }

    void connect_progress(const std::shared_ptr<PageContext>& context) {
        const std::array<std::shared_ptr<ProgressBar>, 6> bars{
            document_.gallery_ranges_progress_0, document_.gallery_ranges_progress_1,
            document_.gallery_ranges_progress_2, document_.gallery_ranges_progress_3,
            document_.gallery_ranges_progress_4, document_.gallery_ranges_progress_5};
        (*bars[3]).animation_period = std::chrono::milliseconds(2400);
        (*bars[4]).animation_period = std::chrono::milliseconds(850);
        (*bars[5]).animation_period = std::chrono::milliseconds(1250);
        for (std::size_t index = 0; index < bars.size(); ++index) {
            (*bars[index]).bind(level_);
            // The shell's motion policy (pause, reduced motion) still reaches
            // the animated styles through the page context.
            if (index >= 2U) (*context).animated_progress.push_back(bars[index]);
        }
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
        if (shared_) (*shared_).text = level_text_ + input_text_;
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

    // Owns the document's controls; the shell's page body holds them too once
    // mount_document() has moved them there.
    RangesDocument<RangesPage> document_{};
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
