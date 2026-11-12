// IndicatorLamp (skeudoindustrial surfaces, follow-up 4): its states and
// their names to assistive technology, its parts, the warm-up and cool-down
// (and their absence under reduced motion and outside a window), that it
// is never hit, and, drawn through the offscreen raster, that the lit lens
// differs from the dark one in the default theme and every familiar theme
// while not one pixel outside the lens changes. With GUI_FORMS_PREVIEW_DIR
// set, the test also writes preview shots of each state in each theme.

#include "gui_forms/capture/offscreen_raster/offscreen_raster.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/indicator_lamp.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/engine/light_planes/light_planes.hpp"
#include "gui_forms/semantics/types/semantic_types.hpp"
#include "gui_forms/surface_root/surface_root.hpp"
#include "gui_forms/theme/familiar_themes/familiar_themes.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/window/window.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace gui_forms;

int failures = 0;

void require(bool condition, const std::string& message) {
    if (condition) return;
    std::cerr << "gui_forms_indicator_lamp_tests: " << message << '\n';
    ++failures;
}

// Paints nothing; the timer only needs the owner's paint counted.
class SilentPainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override {}
    void fill_rounded_rect(Rect, double, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}
};

// A lamp of `size` at the origin of a panel in a window of its own.
struct LampWindow final {
    std::shared_ptr<Panel> root;
    std::shared_ptr<IndicatorLamp> lamp;
    std::unique_ptr<Window> window;
};

LampWindow lamp_window(Size size, std::string text) {
    LampWindow made;
    made.root = make_control<Panel>(StableId("root"));
    made.lamp = make_control<IndicatorLamp>(StableId("root.lamp"), std::move(text));
    (*made.lamp).set_requested_bounds({8.0, 8.0, size.width, size.height});
    (*made.root).add_child(made.lamp);
    made.window = std::make_unique<Window>(made.root, Size{size.width + 16.0, size.height + 16.0});
    (*made.window).set_active(true);
    (*made.window).perform_layout();
    return made;
}

void set_reduced_motion(Window& window, bool reduced) {
    PresentationSettings settings = window.presentation_base();
    settings.reduced_motion = reduced;
    window.set_presentation_settings(settings);
}

void test_states_and_semantics() {
    const std::shared_ptr<IndicatorLamp> lamp = make_control<IndicatorLamp>(StableId("lamp"), "Coolant");
    require((*lamp).state.read() == IndicatorLampState::off && (*lamp).power() == 0.0 && !(*lamp).changing(),
            "a new lamp is off and dark");
    require(!(*lamp).hit_test_local(Point{4.0, 4.0}), "a lamp is never hit");
    SemanticDescriptor described = (*lamp).semantic_descriptor();
    require(described.role == SemanticRole::static_text && described.name == "Coolant" &&
                described.value == "off" && described.exposed,
            "a lamp is static text named by its legend, its state the value");
    (*lamp).accessible_name = "Coolant pump";
    (*lamp).state = IndicatorLampState::error;
    described = (*lamp).semantic_descriptor();
    require(described.name == "Coolant pump" && described.value == "error",
            "the accessible name wins, and error is spoken, not only coloured");
    require(IndicatorLamp::state_name(IndicatorLampState::on) == "on", "on is named on");

    // The anatomy: three parts besides the region, two custom states.
    const ControlAnatomy& anatomy = (*lamp).anatomy();
    require(anatomy.control_id == "indicator_lamp" && anatomy.parts.size() == 4U &&
                anatomy.custom_states.size() == 2U && anatomy.custom_states[0].name == "on" &&
                anatomy.custom_states[1].name == "error",
            "the anatomy declares region, bezel, lens and label, and the states on and error");
    const AnatomyStateSet error_states = (*lamp).anatomy_states();
    require(error_states.contains(CustomStateIndex{1U}) && !error_states.contains(CustomStateIndex{0U}),
            "an error lamp takes the error state alone");
    (*lamp).state = IndicatorLampState::off;
    const AnatomyStateSet off_states = (*lamp).anatomy_states();
    require(!off_states.contains(CustomStateIndex{0U}) && !off_states.contains(CustomStateIndex{1U}),
            "an off lamp takes neither lit state");

    bool refused = false;
    try {
        (*lamp).state = static_cast<IndicatorLampState>(7);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*lamp).state.read() == IndicatorLampState::off,
            "a state outside the enumeration is refused, keeping the old one");
}

void test_detached_changes_at_once() {
    const std::shared_ptr<IndicatorLamp> lamp = make_control<IndicatorLamp>(StableId("lamp"));
    (*lamp).state = IndicatorLampState::on;
    require((*lamp).power() == 1.0 && !(*lamp).changing(), "outside a window a lamp lights at once");
    (*lamp).state = IndicatorLampState::off;
    require((*lamp).power() == 0.0 && !(*lamp).changing(), "outside a window a lamp goes dark at once");
}

// Polls and paints until the change ends, at most `limit` ticks; returns
// the powers seen.
std::vector<double> run_change(LampWindow& made, std::size_t limit) {
    std::vector<double> powers;
    SilentPainter painter;
    for (std::size_t tick = 0U; tick < limit && (*made.lamp).changing(); ++tick) {
        const std::optional<FrameTime> wake = (*made.window).next_wake();
        if (!wake) break;
        static_cast<void>((*made.window).poll_frame_schedule(*wake));
        powers.push_back((*made.lamp).power());
        (*made.lamp).on_paint(painter, Rect{0.0, 0.0, 24.0, 24.0});
    }
    return powers;
}

void test_warm_up_and_cool_down() {
    LampWindow made = lamp_window(Size{24.0, 24.0}, {});
    (*made.lamp).state = IndicatorLampState::on;
    require((*made.lamp).changing() &&
                (*made.lamp).power() == engine_light::lamp_power(true, 0.0, false),
            "in a window a lamp strikes at the envelope's first power");
    const std::vector<double> warming = run_change(made, 64U);
    require(!(*made.lamp).changing() && (*made.lamp).power() == 1.0, "the warm-up ends fully lit");
    bool rising = true;
    for (std::size_t index = 1U; index < warming.size(); ++index) {
        if (warming[index] < warming[index - 1U]) rising = false;
    }
    require(rising, "the warm-up never dims");

    (*made.lamp).state = IndicatorLampState::off;
    require((*made.lamp).changing() && (*made.lamp).power() == 1.0, "switched off, a lit lamp starts to cool");
    const std::vector<double> cooling = run_change(made, 64U);
    require(!(*made.lamp).changing() && (*made.lamp).power() == 0.0 && cooling.size() >= 2U,
            "the cool-down takes frames and ends dark");
    bool falling = true;
    for (std::size_t index = 1U; index < cooling.size(); ++index) {
        if (cooling[index] > cooling[index - 1U]) falling = false;
    }
    require(falling, "the cool-down never brightens");

    // Under reduced motion both are instant.
    set_reduced_motion(*made.window, true);
    (*made.lamp).state = IndicatorLampState::error;
    require(!(*made.lamp).changing() && (*made.lamp).power() == 1.0, "under reduced motion a lamp lights at once");
    (*made.lamp).state = IndicatorLampState::off;
    require(!(*made.lamp).changing() && (*made.lamp).power() == 0.0, "under reduced motion a lamp goes dark at once");
}

void test_parts() {
    LampWindow made = lamp_window(Size{120.0, 24.0}, "Coolant");
    const IndicatorLamp::Parts parts = (*made.lamp).parts();
    require(parts.bezel.bounds == Rect{0.0, 4.0, 16.0, 16.0}, "beside a legend the bezel is 16 units, centred");
    require(parts.lens.bounds == Rect{2.0, 6.0, 12.0, 12.0}, "the lens sits two units inside the bezel");
    require(parts.label.bounds == Rect{22.0, 0.0, 98.0, 24.0}, "the legend follows the bezel after a gap");
    (*made.lamp).layout_direction = LayoutDirection::right_to_left;
    (*made.window).perform_layout();
    const IndicatorLamp::Parts mirrored = (*made.lamp).parts();
    require(mirrored.bezel.bounds == Rect{104.0, 4.0, 16.0, 16.0} && mirrored.label.bounds.x == 0.0,
            "right to left, the bezel moves to the right and the legend before it");

    LampWindow alone = lamp_window(Size{24.0, 24.0}, {});
    const IndicatorLamp::Parts lens_only = (*alone.lamp).parts();
    require(lens_only.bezel.bounds == Rect{0.0, 0.0, 24.0, 24.0} && !lens_only.label.present,
            "a lens alone fills the shorter side and publishes no legend");
}

std::optional<WindowCapture> capture_lamp(const OffscreenAttachment& attached, const LampWindow& made) {
    OffscreenCaptureOptions options{};
    options.scale = 1.0;
    options.margin = 4.0;
    std::optional<WindowCapture> picture = attached.capture_control(made.lamp, options);
    return picture;
}

// Whether every pixel that differs between `dark` and `lit` lies inside
// `lens` (capture pixels), and whether any differs at all.
struct Difference final {
    bool any{};
    bool outside{};
};

Difference compare(const WindowCapture& dark, const WindowCapture& lit, Rect lens) {
    Difference found;
    if (dark.bgra.size() != lit.bgra.size()) {
        found.outside = true;
        return found;
    }
    for (std::uint32_t y = 0U; y < dark.pixel_height; ++y) {
        for (std::uint32_t x = 0U; x < dark.pixel_width; ++x) {
            const std::size_t at = (static_cast<std::size_t>(y) * dark.pixel_width + x) * 4U;
            bool same = true;
            for (std::size_t channel = 0U; channel < 4U; ++channel) {
                if (dark.bgra[at + channel] != lit.bgra[at + channel]) same = false;
            }
            if (same) continue;
            found.any = true;
            const Point centre{static_cast<double>(x) + 0.5, static_cast<double>(y) + 0.5};
            if (!lens.contains(centre)) found.outside = true;
        }
    }
    return found;
}

// Writes `picture` as <GUI_FORMS_PREVIEW_DIR>/<name>.png when the variable
// is set.
void write_preview(const WindowCapture& picture, const std::string& name) {
    const char* const directory = std::getenv("GUI_FORMS_PREVIEW_DIR");
    if (directory == nullptr || *directory == '\0') return;
    std::vector<std::byte> png;
    if (!encode_window_capture_png(picture, png)) return;
    std::ofstream out(std::string(directory) + "/" + name + ".png", std::ios::binary);
    out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
}

// In `theme` (none: the default theme), the lit lens differs from the dark
// one and nothing outside the lens changes, for on and for error.
void check_lights_only_the_lens(const OffscreenAttachment& attached, LampWindow& made,
                                const std::shared_ptr<const Theme>& theme, const std::string& name) {
    (*made.window).set_theme_override(theme);
    (*made.lamp).state = IndicatorLampState::off;
    const std::optional<WindowCapture> dark = capture_lamp(attached, made);
    require(dark.has_value(), name + ": the dark lamp is captured");
    if (!dark) return;
    write_preview(*dark, "indicator_lamp_" + name + "_off");
    // The lens in the capture's pixels: the margin, then two units in.
    const Rect lens{4.0 + 2.0, 4.0 + 2.0, 20.0, 20.0};
    const std::array<IndicatorLampState, 2> lit_states{IndicatorLampState::on, IndicatorLampState::error};
    for (const IndicatorLampState state : lit_states) {
        (*made.lamp).state = state;
        const std::optional<WindowCapture> lit = capture_lamp(attached, made);
        const std::string label = name + " " + std::string(IndicatorLamp::state_name(state));
        require(lit.has_value(), label + ": the lit lamp is captured");
        if (!lit) continue;
        write_preview(*lit, "indicator_lamp_" + name + "_" + std::string(IndicatorLamp::state_name(state)));
        const Difference found = compare(*dark, *lit, lens);
        require(found.any, label + ": the lit lens differs from the dark one");
        require(!found.outside, label + ": no pixel outside the lens changes");
    }
}

void test_lights_only_the_lens() {
    OffscreenRasterOptions options{};
    options.font_directory = GUI_FORMS_TEST_FONT_DIR;
    OffscreenRasterCreateResult made_raster = OffscreenRaster::create(options);
    require(made_raster.succeeded(), "the bundled faces make a raster");
    if (!made_raster.succeeded()) return;
    const std::unique_ptr<OffscreenRaster> raster = made_raster.take_raster();
    LampWindow made = lamp_window(Size{24.0, 24.0}, {});
    // Settled lamps only: the comparison is of end states.
    set_reduced_motion(*made.window, true);
    const OffscreenAttachment attached(*raster, *made.window);
    check_lights_only_the_lens(attached, made, nullptr, "gui_forms");
    for (std::size_t index = 0U; index < familiar_theme_count; ++index) {
        const FamiliarTheme theme = static_cast<FamiliarTheme>(index);
        check_lights_only_the_lens(attached, made, familiar_theme(theme), std::string(familiar_theme_id(theme)));
    }
}

} // namespace

int main() {
    test_states_and_semantics();
    test_detached_changes_at_once();
    test_warm_up_and_cool_down();
    test_parts();
    test_lights_only_the_lens();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return EXIT_FAILURE;
    }
    std::cout << "indicator lamp tests passed\n";
    return EXIT_SUCCESS;
}
