#include "gallery_model.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <utility>

namespace gui_forms::gallery {
namespace {

[[nodiscard]] bool is_collection_row(std::string_view id) noexcept
{
    return id == "gallery.collection.alpha" || id == "gallery.collection.beta" ||
           id == "gallery.collection.gamma" || id == "gallery.collection.delta";
}

}  // namespace

GalleryModel::GalleryModel()
{
    using BoolChanged = Delegate<bool>;
    using NumberChanged = Delegate<double>;
    precise_updates_changed_ = precise_updates.changed().subscribe(
        BoolChanged::bind<GalleryModel, &GalleryModel::on_precise_updates>(*this));
    classic_relief_changed_ = classic_relief.changed().subscribe(
        BoolChanged::bind<GalleryModel, &GalleryModel::on_classic_relief>(*this));
    quiet_relief_changed_ = quiet_relief.changed().subscribe(
        BoolChanged::bind<GalleryModel, &GalleryModel::on_quiet_relief>(*this));
    slider_changed_ = slider.changed().subscribe(NumberChanged::bind<GalleryModel, &GalleryModel::on_slider>(*this));
    diagnostics_visible_changed_ = diagnostics_visible.changed().subscribe(
        BoolChanged::bind<GalleryModel, &GalleryModel::on_diagnostics_visible>(*this));
}

const GalleryState& GalleryModel::state() const noexcept
{
    return state_;
}

bool GalleryModel::activate(std::string_view stable_id)
{
    // An ID the Gallery does not act on (unknown, or a disabled control
    // such as gallery.disabled-button) is no activation. A bound Value's
    // notification records its own change (note_change).
    if (stable_id == "gallery.command.reset") {
        reset();
        ++state_.activations;
        return true;
    }
    if (stable_id == "gallery.command.diagnostics") {
        diagnostics_visible.set(!state_.diagnostics_visible);
    } else if (stable_id == "gallery.default-button") {
        note_change();
    } else if (stable_id == "gallery.checkbox") {
        precise_updates.set(!state_.precise_updates);
    } else if (stable_id == "gallery.radio.classic") {
        classic_relief.set(true);
    } else if (stable_id == "gallery.radio.quiet") {
        quiet_relief.set(true);
    } else if (is_collection_row(stable_id)) {
        static_cast<void>(select_collection_row(stable_id));
    } else {
        return false;
    }
    ++state_.activations;
    return true;
}

bool GalleryModel::replace_text(std::string text)
{
    if (state_.text == text) {
        return false;
    }
    state_.text = std::move(text);
    note_change();
    return true;
}

bool GalleryModel::set_slider_value(double value)
{
    if (!std::isfinite(value)) {
        return false;
    }
    const double bounded = std::clamp(value, 0.0, 100.0);
    if (bounded == state_.slider_value) {
        return false;
    }
    slider.set(bounded);
    return true;
}

bool GalleryModel::select_page(std::string_view page_id)
{
    if (page_id.empty() || state_.selected_page == page_id) {
        return false;
    }
    state_.selected_page.assign(page_id);
    note_change();
    return true;
}

bool GalleryModel::select_collection_row(std::string_view stable_id)
{
    if (!is_collection_row(stable_id) || state_.selected_collection_row == stable_id) {
        return false;
    }
    state_.selected_collection_row.assign(stable_id);
    note_change();
    return true;
}

void GalleryModel::reset()
{
    // The bound Values first, each recording its change; then the record
    // returns to its defaults, keeping the counters and the page shown.
    precise_updates.set(true);
    classic_relief.set(true);
    slider.set(42.0);
    diagnostics_visible.set(false);
    const std::uint64_t next_revision = state_.revision + 1U;
    const std::uint64_t activation_count = state_.activations;
    std::string selected_page = std::move(state_.selected_page);
    state_ = GalleryState {};
    state_.revision = next_revision;
    state_.activations = activation_count;
    state_.selected_page = std::move(selected_page);
}

void GalleryModel::on_precise_updates(bool checked)
{
    state_.precise_updates = checked;
    note_change();
}

// The two relief Values are one choice: a Value written true clears the
// other; a Value written false (the radio group clearing it) changes
// nothing more.
void GalleryModel::on_classic_relief(bool checked)
{
    if (!checked) return;
    state_.style_mode = StyleMode::classic_relief;
    quiet_relief.set(false);
    note_change();
}

void GalleryModel::on_quiet_relief(bool checked)
{
    if (!checked) return;
    state_.style_mode = StyleMode::quiet_relief;
    classic_relief.set(false);
    note_change();
}

// The slider's value, the user's (its track bar keeps it within 0..100) or
// set_slider_value's, drives the progress and the instrument.
void GalleryModel::on_slider(double value)
{
    state_.slider_value = value;
    state_.progress_value = value;
    state_.instrument_value = value / 100.0;
    progress.set(value);
    note_change();
}

void GalleryModel::on_diagnostics_visible(bool visible)
{
    state_.diagnostics_visible = visible;
    note_change();
}

void GalleryModel::note_activation() noexcept
{
    ++state_.activations;
}

void GalleryModel::note_change() noexcept
{
    ++state_.revision;
}

std::string format_percent(double value)
{
    std::ostringstream output;
    output << std::fixed << std::setprecision(0) << std::clamp(value, 0.0, 100.0) << '%';
    return output.str();
}

}  // namespace gui_forms::gallery
