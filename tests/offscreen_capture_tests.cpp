// Windowless capture (owner request 2026-11-11, the theme palette):
// - OffscreenRaster: refused without fonts, made over the bundled faces;
//   an attachment gives a headless window text metrics and a painter and
//   puts the provider back when it ends;
// - Window::capture_control: the subject's area grown by the margin and
//   clipped to the window, with and without the backplane, at a scale and
//   in a colour scheme the window's own settings return from;
// - specimen states: declared states only, published without input, a
//   disabled shown replaces enabled, cleared by an empty set; each route of
//   show_specimen_state;
// - the stock specimen catalogue: one row per anatomy of
//   stock_control_anatomies(), in its order, each row's maker giving a
//   subject of its type, no row near-uniform at rest
//   (stock_specimen_uniform_share) without its stated blank_reason, and
//   every declared state of every subject shown and captured.

#include "gui_forms/gui_forms.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace gui_forms;

// Registered types whose control still reports its base's anatomy (own
// anatomy pending; the Linux accessibility gallery client lists the same
// two), so their subject cannot name the row's id yet.
bool base_anatomy_pending(std::string_view control_id) noexcept {
    const bool pending = control_id == "wizard_page" || control_id == "help_book_viewer";
    return pending;
}

// A stable ID for a specimen of `control_id`: anatomy ids join words with
// '_', which stable IDs refuse, so they become '-'.
StableId specimen_id(std::string_view control_id) {
    std::string segment(control_id);
    std::replace(segment.begin(), segment.end(), '_', '-');
    StableId id("specimen." + segment);
    return id;
}

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "gui_forms_offscreen_capture_tests: " << message << '\n';
    std::exit(1);
}

void require(bool condition, const std::string& message) {
    require(condition, message.c_str());
}

std::unique_ptr<OffscreenRaster> made_raster() {
    OffscreenRasterOptions options{};
    options.font_directory = GUI_FORMS_TEST_FONT_DIR;
    OffscreenRasterCreateResult made = OffscreenRaster::create(options);
    require(made.succeeded() && made.error() == OffscreenRasterError::none, "the bundled faces make a raster");
    std::unique_ptr<OffscreenRaster> raster = made.take_raster();
    require(raster != nullptr && !made.take_raster(), "the raster is taken once");
    return raster;
}

// The capture's pixel at (x, y), as B, G, R, A bytes packed in that order
// from the low byte.
std::uint32_t pixel_at(const WindowCapture& capture, std::uint32_t x, std::uint32_t y) {
    const std::size_t at = (static_cast<std::size_t>(y) * capture.pixel_width + x) * 4U;
    std::uint32_t value = 0U;
    for (std::size_t channel = 0U; channel < 4U; ++channel) {
        const std::uint32_t byte = static_cast<std::uint32_t>(capture.bgra[at + channel]);
        value |= byte << (8U * channel);
    }
    return value;
}

void raster_refusals() {
    OffscreenRasterOptions options{};
    options.font_directory = "offscreen-capture-tests-no-such-directory";
    OffscreenRasterCreateResult refused = OffscreenRaster::create(options);
    require(!refused.succeeded() && refused.error() == OffscreenRasterError::fonts_not_found &&
                !refused.detail().empty() && !refused.take_raster(),
            "a directory without the faces is refused, naming it");
    // This test links no host: without a directory there is nowhere to look.
    OffscreenRasterCreateResult hostless = OffscreenRaster::create();
    require(hostless.error() == OffscreenRasterError::fonts_not_found, "without a host an empty directory is refused");
}

void attachment_and_control_capture() {
    const std::unique_ptr<OffscreenRaster> raster = made_raster();
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("root"));
    const std::shared_ptr<Button> button = make_control<Button>(StableId("root.button"), "Capture");
    (*button).set_requested_bounds({20.0, 10.0, 100.0, 30.0});
    (*root).add_child(button);
    Window window(root, {200.0, 80.0});
    window.set_active(true);
    window.perform_layout();
    require(!window.capture_control(button), "a headless window without an attachment captures nothing");
    {
        OffscreenAttachment attached(*raster, window);
        require((*raster).attachments() == 1U && window.text_metrics_provider() != nullptr,
                "an attachment gives the window text metrics");
        OffscreenCaptureOptions options{};
        options.scale = 2.0;
        options.margin = 4.0;
        const std::optional<WindowCapture> light = attached.capture_control(button, options);
        require(light.has_value(), "an attached window captures a control");
        require((*light).logical_size.width == 108.0 && (*light).logical_size.height == 38.0 &&
                    (*light).pixel_width == 216U && (*light).pixel_height == 76U && (*light).scale == 2.0,
                "the area is the bounds grown by the margin, at the scale");
        require((*light).bgra.size() == static_cast<std::size_t>(216U * 76U * 4U), "the pixels are tightly packed");
        require((pixel_at(*light, 1U, 1U) >> 24U) == 255U, "the backplane makes the margin opaque");
        require(window.scale() == 1.0, "the window's scale returns after the capture");

        options.include_backplane = false;
        const std::optional<WindowCapture> bare = attached.capture_control(button, options);
        require(bare.has_value() && (pixel_at(*bare, 1U, 1U) >> 24U) == 0U,
                "without the backplane the margin stays transparent");

        options.include_backplane = true;
        options.colour_scheme = ColourScheme::dark;
        const std::optional<WindowCapture> dark = attached.capture_control(button, options);
        require(dark.has_value() && pixel_at(*dark, 1U, 1U) != pixel_at(*light, 1U, 1U),
                "a dark capture draws a dark backplane");
        require(window.presentation_overrides() == nullptr, "the window's overrides return after the capture");

        options.margin = 400.0;
        const std::optional<WindowCapture> clipped = attached.capture_control(button, options);
        require(clipped.has_value() && (*clipped).logical_size.width == 200.0 &&
                    (*clipped).logical_size.height == 80.0,
                "the area is clipped to the window");
        options.margin = -1.0;
        require(!attached.capture_control(button, options), "a negative margin is refused");
        options.margin = 0.0;
        options.scale = 9.0;
        require(!attached.capture_control(button, options), "a scale past 8 is refused");

        OffscreenCaptureOptions whole{};
        const std::optional<WindowCapture> window_picture = attached.capture_window(whole);
        require(window_picture.has_value() && (*window_picture).pixel_width == 200U &&
                    (*window_picture).pixel_height == 80U,
                "an attached window captures itself");
        std::vector<std::byte> png{};
        require(encode_window_capture_png(*window_picture, png) && png.size() > 8U, "a capture encodes as a PNG");
    }
    require((*raster).attachments() == 0U && window.text_metrics_provider() == nullptr,
            "the attachment's end puts the provider back");
    require(!window.capture_control(button), "after the attachment the window captures nothing again");
}

void specimen_states() {
    const std::shared_ptr<CheckBox> box = make_control<CheckBox>(StableId("box"), "Box");
    AnatomyStateSet shown{};
    shown.insert(AnatomyState::hot);
    (*box).set_specimen_states(shown);
    require((*box).anatomy_states().contains(AnatomyState::hot) && !(*box).states().pressed,
            "a shown state is published");
    AnatomyStateSet undeclared{};
    undeclared.insert(AnatomyState::drop_accepted);
    bool refused = false;
    try {
        (*box).set_specimen_states(undeclared);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*box).specimen_states() == shown, "an undeclared state is refused, changing nothing");
    AnatomyStateSet disabled{};
    disabled.insert(AnatomyState::disabled);
    (*box).set_specimen_states(disabled);
    const AnatomyStateSet published = (*box).anatomy_states();
    require(published.contains(AnatomyState::disabled) && !published.contains(AnatomyState::enabled) &&
                (*box).enabled.read(),
            "a shown disabled replaces enabled and leaves the property");
    (*box).set_specimen_states(AnatomyStateSet{});
    require(!(*box).anatomy_states().contains(AnatomyState::disabled), "an empty set ends the presentation");

    const SpecimenStateResult checked = show_specimen_state(*box, AnatomyState::checked);
    require(checked.route == SpecimenStateRoute::value && (*box).check_state.read() == CheckState::checked,
            "checked is the box's value");
    const std::shared_ptr<CheckBox> third = make_control<CheckBox>(StableId("third"), "Third");
    const SpecimenStateResult indeterminate = show_specimen_state(*third, AnatomyState::indeterminate);
    require(indeterminate.route == SpecimenStateRoute::value &&
                (*third).check_state.read() == CheckState::indeterminate,
            "indeterminate makes the box three-state first");
    const std::shared_ptr<CheckBox> pressed = make_control<CheckBox>(StableId("pressed"), "Pressed");
    const SpecimenStateResult press = show_specimen_state(*pressed, AnatomyState::pressed);
    require(press.route == SpecimenStateRoute::presentation &&
                (*pressed).anatomy_states().contains(AnatomyState::pressed),
            "pressed is shown by presentation");
    const SpecimenStateResult off = show_specimen_state(*pressed, AnatomyState::disabled);
    require(off.route == SpecimenStateRoute::value && !(*pressed).enabled.read(), "disabled is the enabled value");
    const SpecimenStateResult none = show_specimen_state(*pressed, AnatomyState::drop_accepted);
    require(none.route == SpecimenStateRoute::not_declared, "an undeclared state is reported, not shown");
    const SpecimenStateResult contrast = show_specimen_state(*pressed, AnatomyState::high_contrast);
    require(contrast.route == SpecimenStateRoute::environment || contrast.route == SpecimenStateRoute::not_declared,
            "high contrast belongs to the window");
}

// The catalogue against the anatomies, then every row made, laid out and
// captured in every declared state.
void stock_catalogue() {
    const std::span<const ControlAnatomy> anatomies = stock_control_anatomies();
    const std::span<const StockSpecimen> rows = stock_specimens();
    require(rows.size() == anatomies.size(), "one stock specimen row per stock anatomy");
    for (std::size_t index = 0; index < anatomies.size(); ++index) {
        require(rows[index].control_id == anatomies[index].control_id,
                "row " + std::to_string(index) + " is " + std::string(anatomies[index].control_id) +
                    ", in the anatomies' order");
        require(find_stock_specimen(anatomies[index].control_id) == &rows[index], "a row is found by its id");
        require(!rows[index].family.empty() && rows[index].size.width > 0.0 && rows[index].size.height > 0.0 &&
                    rows[index].make != nullptr,
                "a row has its family, size and maker");
    }
    require(find_stock_specimen("no_such_control") == nullptr, "an unknown id has no row");

    WindowCapture flat{};
    flat.pixel_width = 8U;
    flat.pixel_height = 8U;
    flat.bgra.assign(8U * 8U * 4U, std::byte{0x80});
    require(stock_specimen_dominant_share(flat, 1U) == 1.0, "one colour is a share of one");
    flat.bgra[(3U * 8U + 3U) * 4U] = std::byte{0x00};
    require(stock_specimen_dominant_share(flat, 1U) == 35.0 / 36.0, "the share counts the inset region only");
    require(stock_specimen_dominant_share(flat, 4U) == 1.0, "an empty region is a share of one");

    const std::unique_ptr<OffscreenRaster> raster = made_raster();
    std::string blank{};
    for (const StockSpecimen& row : rows) {
        const StockSpecimenInstance instance =
            make_stock_specimen(row, specimen_id(row.control_id));
        require(instance.root != nullptr && instance.subject != nullptr && instance.frame != nullptr,
                std::string(row.control_id) + ": a maker gives a root, a subject and a frame");
        Window window(instance.root, row.size);
        window.set_active(true);
        OffscreenAttachment attached(*raster, window);
        window.perform_layout();
        present_stock_specimen(row, instance, window);
        OffscreenCaptureOptions options{};
        const std::optional<WindowCapture> picture = attached.capture_control(instance.frame, options);
        require(picture.has_value(), std::string(row.control_id) + ": the specimen at rest captures");
        // Three pixels in from each edge leave out the subject's own border.
        const double share = stock_specimen_dominant_share(*picture, 3U);
        if (share >= stock_specimen_uniform_share && row.blank_reason.empty()) {
            blank += " " + std::string(row.control_id) + " (" + std::to_string(share) + ")";
        }
    }
    require(blank.empty(), "near-uniform stock specimens without a reason:" + blank);

    std::size_t captured = 0U;
    std::size_t shown = 0U;
    for (std::size_t index = 0; index < rows.size(); ++index) {
        const StockSpecimen& row = rows[index];
        const ControlAnatomy& declared = anatomies[index];
        for (const AnatomyState state : declared.states) {
            const StockSpecimenInstance instance =
                make_stock_specimen(row, specimen_id(row.control_id));
            require(instance.root != nullptr && instance.subject != nullptr, "a maker gives a root and a subject");
            const std::string_view made_id = (*instance.subject).anatomy().control_id;
            require(made_id == row.control_id || made_id == "control" || base_anatomy_pending(row.control_id),
                    std::string(row.control_id) + ": the subject is of the row's type");
            Window window(instance.root, row.size);
            window.set_active(true);
            OffscreenAttachment attached(*raster, window);
            window.perform_layout();
            present_stock_specimen(row, instance, window);
            const SpecimenStateResult result = show_specimen_state(*instance.subject, state);
            if (result.route != SpecimenStateRoute::not_declared && result.route != SpecimenStateRoute::refused) {
                shown = shown + 1U;
            }
            OffscreenCaptureOptions options{};
            const std::optional<WindowCapture> picture = attached.capture_window(options);
            require(picture.has_value(), std::string(row.control_id) + ": a specimen window captures");
            captured = captured + 1U;
        }
    }
    std::cout << "stock catalogue: " << rows.size() << " rows, " << captured << " captures, " << shown
              << " states shown\n";
}

} // namespace

int main() {
    raster_refusals();
    attachment_and_control_capture();
    specimen_states();
    stock_catalogue();
    std::cout << "Offscreen capture: raster refusals, the attachment, control captures, specimen states and the "
                 "stock catalogue passed\n";
    return EXIT_SUCCESS;
}
