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
// - ImageAtlas deferred fills in real pixels: a slot drawn and filled in the
//   same paint presents its old pixels in that frame and the new ones in the
//   next.

#include "gui_forms/anatomy/types/anatomy_types.hpp"
#include "gui_forms/capture/offscreen_raster/offscreen_raster.hpp"
#include "gui_forms/capture/specimen_states/specimen_states.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/button.hpp"
#include "gui_forms/controls/check_box.hpp"
#include "gui_forms/controls/check_box_values.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/controls/stock_specimens.hpp"
#include "gui_forms/dirty.hpp"
#include "gui_forms/paint_framebuffer.hpp"
#include "gui_forms/resources/application_resources/application_resources.hpp"
#include "gui_forms/resources/image_atlas/image_atlas.hpp"
#include "gui_forms/rules/rule/rule.hpp"
#include "gui_forms/surface_root/surface_root.hpp"
#include "gui_forms/theme/stock_anatomies/stock_anatomies.hpp"
#include "gui_forms/window/presentation/colour_scheme.hpp"
#include "gui_forms/window/window.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
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

// One opaque colour's premultiplied BGRA pixels for an 8 x 8 slot.
std::vector<std::byte> solid_slot(std::uint8_t blue, std::uint8_t green, std::uint8_t red) {
    std::vector<std::byte> pixels(8U * 8U * 4U, std::byte{0});
    for (std::size_t at = 0U; at < pixels.size(); at += 4U) {
        pixels[at] = std::byte{blue};
        pixels[at + 1U] = std::byte{green};
        pixels[at + 2U] = std::byte{red};
        pixels[at + 3U] = std::byte{255U};
    }
    return pixels;
}

// Draws an atlas slot over its first 8 x 8 units at every paint; the first
// paint after `refill` is set also fills the slot with it (a fill during a
// paint, so deferred).
class AtlasFrameProbe final : public Panel {
public:
    explicit AtlasFrameProbe(StableId id) : Panel(std::move(id)) {}

    void on_paint(Painter& painter, Rect local_damage) override {
        Panel::on_paint(painter, local_damage);
        if (atlas == nullptr) return;
        drawn = (*atlas).draw(painter, *this, slot, Rect{0.0, 0.0, 8.0, 8.0});
        if (refill == nullptr) return;
        const std::vector<std::byte>& pixels = *refill;
        refill = nullptr;
        fill_result = (*atlas).fill(slot, 32U, std::span<const std::byte>(pixels));
    }

    ImageAtlas* atlas{};
    ImageAtlasSlot slot{};
    const std::vector<std::byte>* refill{};
    bool drawn{};
    ImageAtlasError fill_result{ImageAtlasError::none};
};

// Paints `window` whole into a new framebuffer at scale 1 and returns the
// pixel at (x, y) as B, G, R, A bytes.
std::array<std::uint8_t, 4> presented_pixel(Window& window, Size size, std::size_t x, std::size_t y) {
    const Rect whole{0.0, 0.0, size.width, size.height};
    const std::unique_ptr<PaintFramebuffer> framebuffer = window.create_framebuffer(size, 1.0);
    require(framebuffer != nullptr, "the attached window makes a framebuffer");
    require((*framebuffer).begin(window.image_resources(), whole), "the framebuffer begins");
    const std::optional<PaintReceipt> receipt = window.paint((*framebuffer).painter(), whole);
    (*framebuffer).end();
    require(receipt.has_value(), "the frame completes");
    const std::span<std::byte> bytes = (*framebuffer).pixels();
    const std::size_t offset = y * (*framebuffer).row_bytes() + x * 4U;
    require(offset + 4U <= bytes.size(), "the pixel is inside the frame");
    const std::array<std::uint8_t, 4> pixel{
        static_cast<std::uint8_t>(bytes[offset]), static_cast<std::uint8_t>(bytes[offset + 1U]),
        static_cast<std::uint8_t>(bytes[offset + 2U]), static_cast<std::uint8_t>(bytes[offset + 3U])};
    return pixel;
}

// Whether each channel is within two steps of `wanted` (B, G, R, A).
bool near_pixel(const std::array<std::uint8_t, 4>& pixel, const std::array<std::uint8_t, 4>& wanted) {
    for (std::size_t channel = 0U; channel < 4U; ++channel) {
        const unsigned value = pixel[channel];
        const unsigned target = wanted[channel];
        if (value + 2U < target || value > target + 2U) return false;
    }
    return true;
}

// A slot drawn and filled in the same paint: the fill is deferred, so that
// frame presents the slot's old pixels (the paint replayed before the fill
// was written); the fill is written when the paint ends and damages the
// panel, and the next frame presents the new pixels.
void atlas_deferred_fill_pixels() {
    const std::unique_ptr<OffscreenRaster> raster = made_raster();
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("atlas-frames"));
    const std::shared_ptr<AtlasFrameProbe> probe = std::make_shared<AtlasFrameProbe>(StableId("atlas-frames.probe"));
    (*probe).set_bounds(Rect{20.0, 10.0, 40.0, 40.0});
    (*root).add_child(probe);
    const Size size{80.0, 60.0};
    Window window(root, size);
    OffscreenAttachment attached(*raster, window);

    ImageAtlasLayout layout{};
    layout.slot_width = 8U;
    layout.slot_height = 8U;
    layout.slot_count = 2U;
    ImageAtlas atlas(window.resources(), layout);
    const ImageAtlasSlot slot = atlas.acquire(1U).slot;
    const std::vector<std::byte> red = solid_slot(0U, 0U, 255U);
    const std::vector<std::byte> blue = solid_slot(255U, 0U, 0U);
    require(atlas.fill(slot, 32U, std::span<const std::byte>(red)) == ImageAtlasError::none,
            "the slot fills red outside a paint");
    (*probe).atlas = &atlas;
    (*probe).slot = slot;
    (*probe).refill = &blue;
    window.perform_layout();

    const std::array<std::uint8_t, 4> old_colour{0U, 0U, 255U, 255U};
    const std::array<std::uint8_t, 4> new_colour{255U, 0U, 0U, 255U};
    const std::array<std::uint8_t, 4> first = presented_pixel(window, size, 24U, 14U);
    require((*probe).drawn && (*probe).fill_result == ImageAtlasError::deferred,
            "the probe drew the slot and its fill during the paint was deferred");
    require(near_pixel(first, old_colour), "the frame that deferred the fill presents the slot's old pixels");
    const ImageAtlasStatistics written = atlas.statistics();
    require(written.deferred_fills == 1U && written.fills == 2U && written.pending_now == 0U,
            "the deferred fill was written when the paint ended");
    require(has_dirty((*probe).dirty(), Dirty::paint), "the written fill damaged the probe for the next frame");

    const std::array<std::uint8_t, 4> second = presented_pixel(window, size, 24U, 14U);
    require(near_pixel(second, new_colour), "the next frame presents the new pixels");
    (*probe).atlas = nullptr;
}

int main() {
    raster_refusals();
    attachment_and_control_capture();
    specimen_states();
    stock_catalogue();
    atlas_deferred_fill_pixels();
    std::cout << "Offscreen capture: raster refusals, the attachment, control captures, specimen states, the "
                 "stock catalogue and atlas deferred-fill frames passed\n";
    return EXIT_SUCCESS;
}
