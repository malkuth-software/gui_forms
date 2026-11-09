#pragma once

#include "gui_forms/event.hpp"
#include "gui_forms/value.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace gui_forms::gallery {

enum class StyleMode {
    classic_relief,
    quiet_relief,
};

struct GalleryState final {
    std::string text {"Edit this text"};
    // Gallery page id (demo/gallery_page_specs.hpp) currently shown; the shell
    // records it so page selection is an observable model mutation.
    std::string selected_page;
    std::string selected_collection_row {"gallery.collection.alpha"};
    bool precise_updates {true};
    bool diagnostics_visible {false};
    StyleMode style_mode {StyleMode::classic_relief};
    double slider_value {42.0};
    double progress_value {42.0};
    double instrument_value {0.42};
    std::uint64_t revision {0};
    std::uint64_t activations {0};
};

// The Gallery's model, which its window binds (demo/gallery.form, FD-6):
// the Values below are the form's binding sources, and state() reads them
// back as one record. A change of a bound Value, the user's through a
// two-way binding or the model's own, updates state() in the Value's
// notification. The Values are disposed with the model; a control bound to
// a disposed Value keeps its last value (FD-6), so the model may end before
// or after the window. UI thread only. Neither copied nor moved: the
// Values' subscriptions name this object.
class GalleryModel final {
public:
    GalleryModel();
    GalleryModel(const GalleryModel&) = delete;
    GalleryModel& operator=(const GalleryModel&) = delete;
    GalleryModel(GalleryModel&&) = delete;
    GalleryModel& operator=(GalleryModel&&) = delete;

    // The form's binding sources. Each is written through the members
    // below or by its bound control; write classic_relief and quiet_relief
    // true only (the model clears the other).
    Value<bool> precise_updates {true};
    Value<bool> classic_relief {true};
    Value<bool> quiet_relief {false};
    Value<double> slider {42.0};
    // Follows `slider`; the progress bar binds it one way.
    Value<double> progress {42.0};
    Value<bool> diagnostics_visible {false};
    // The status strip's live-animation switch (gallery.status.live).
    Value<bool> live_motion {true};

    [[nodiscard]] const GalleryState& state() const noexcept;

    bool activate(std::string_view stable_id);
    // Counts an activation whose change a bound Value already recorded (a
    // click on a bound check box or radio button).
    void note_activation() noexcept;
    bool replace_text(std::string text);
    bool set_slider_value(double value);
    bool select_page(std::string_view page_id);
    bool select_collection_row(std::string_view stable_id);
    void reset();

private:
    GalleryState state_ {};
    // The Values' notifications into state_; declared after the Values,
    // so they end first.
    SubscriptionToken precise_updates_changed_ {};
    SubscriptionToken classic_relief_changed_ {};
    SubscriptionToken quiet_relief_changed_ {};
    SubscriptionToken slider_changed_ {};
    SubscriptionToken diagnostics_visible_changed_ {};

    void on_precise_updates(bool checked);
    void on_classic_relief(bool checked);
    void on_quiet_relief(bool checked);
    void on_slider(double value);
    void on_diagnostics_visible(bool visible);
    void note_change() noexcept;
};

[[nodiscard]] std::string format_percent(double value);

}  // namespace gui_forms::gallery
