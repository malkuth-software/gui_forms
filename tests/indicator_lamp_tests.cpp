// IndicatorLamp (skeudoindustrial surfaces, follow-up 4): its states and
// their names to assistive technology, its parts, the warm-up and cool-down
// (and their absence under reduced motion and outside a window), that it
// is never hit, and, drawn through the offscreen raster, that the lit lens
// differs from the dark one in the default theme and every familiar theme
// while not one pixel outside the lens changes. The light-plane lens: the
// light_planes and lamp_color properties, the kept composite
// (engine_light::LightPlaneRaster) and the sRGB lamp colour, and through the
// offscreen raster a lens composited from a bake written here (it takes the
// bake's aspect, lights only inside the lens, never past the bake's
// coverage, and changes colour with lamp_color), and the theme's lens where
// the bake is missing, no directory is set, or high contrast is forced.
// With GUI_FORMS_PREVIEW_DIR set, the test also writes preview shots of
// each state in each theme.

#include "gui_forms/capture/offscreen_raster/offscreen_raster.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/indicator_lamp.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/resources/application_resources/application_resources.hpp"
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
#include <cstring>
#include <filesystem>
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

std::optional<WindowCapture> capture_lamp(OffscreenAttachment& attached, const LampWindow& made) {
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
void check_lights_only_the_lens(OffscreenAttachment& attached, LampWindow& made,
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
    OffscreenAttachment attached(*raster, *made.window);
    check_lights_only_the_lens(attached, made, nullptr, "gui_forms");
    for (std::size_t index = 0U; index < familiar_theme_count; ++index) {
        const FamiliarTheme theme = static_cast<FamiliarTheme>(index);
        check_lights_only_the_lens(attached, made, familiar_theme(theme), std::string(familiar_theme_id(theme)));
    }
}

// -- the light-plane lens ----------------------------------------------------------------

void test_lamp_colour_and_kept_composite() {
    const engine_light::LampColour white = engine_light::lamp_colour_from_srgb(Color::rgba(255, 255, 255));
    require(std::abs(white.red - 1.0) < 1e-9 && std::abs(white.green - 1.0) < 1e-9 && std::abs(white.blue - 1.0) < 1e-9,
            "sRGB white is unit light");
    const engine_light::LampColour black = engine_light::lamp_colour_from_srgb(Color::rgba(0, 0, 0));
    require(black.red == 0.0 && black.green == 0.0 && black.blue == 0.0, "sRGB black is no light");
    const engine_light::LampColour amber = engine_light::lamp_colour_from_srgb(Color::rgba(255, 188, 80));
    require(std::abs(amber.red - engine_light::lamp_amber.red) < 0.01 &&
                std::abs(amber.green - engine_light::lamp_amber.green) < 0.01 &&
                std::abs(amber.blue - engine_light::lamp_amber.blue) < 0.01,
            "the page's amber decodes to the study's lamp amber");

    std::shared_ptr<engine_light::LightPlanes> planes = std::make_shared<engine_light::LightPlanes>();
    (*planes).width = 2U;
    (*planes).height = 2U;
    (*planes).lamp_count = 1U;
    (*planes).room.assign(12U, 0.1F);
    (*planes).lamps.assign(12U, 0.5F);
    (*planes).coverage.assign(4U, 1.0F);
    const std::shared_ptr<const engine_light::LightPlanes> bake = planes;
    engine_light::LightPlaneRaster kept{};
    const engine_light::LampDrive lit{engine_light::lamp_green, 1.0};
    const engine_light::LightComposite settings{};
    const DeviceExtent extent{4U, 4U};
    const std::shared_ptr<const PartRaster> first = kept.composite(bake, lit, settings, extent, DeviceScale{1.0});
    const std::shared_ptr<const PartRaster> again = kept.composite(bake, lit, settings, extent, DeviceScale{1.0});
    require(first != nullptr && first == again && kept.composites() == 1U,
            "an unchanged repaint draws the kept composite again");
    const engine_light::LampDrive dimmer{engine_light::lamp_green, 0.5};
    const std::shared_ptr<const PartRaster> dimmed = kept.composite(bake, dimmer, settings, extent, DeviceScale{1.0});
    require(dimmed != nullptr && dimmed != first && kept.composites() == 2U, "a new power composites again");
    const std::shared_ptr<const PartRaster> scaled = kept.composite(bake, dimmer, settings, DeviceExtent{8U, 8U},
                                                                    DeviceScale{2.0});
    require(scaled != nullptr && kept.composites() == 3U, "a new extent and scale composite again");
    require(kept.composite(bake, dimmer, settings, DeviceExtent{0U, 4U}, DeviceScale{1.0}) == nullptr,
            "an empty extent composites nothing");
    require(kept.composite(nullptr, dimmer, settings, extent, DeviceScale{1.0}) == nullptr, "no bake composites nothing");
}

void test_light_plane_properties() {
    const std::shared_ptr<IndicatorLamp> lamp = make_control<IndicatorLamp>(StableId("lamp"));
    require((*lamp).light_planes.read().empty() && !(*lamp).lamp_color.read().has_value(),
            "a new lamp names no bake and takes the theme's colour");
    (*lamp).light_planes = std::string("indicator_clear_up");
    require((*lamp).light_planes.read() == "indicator_clear_up", "a bake is named");
    const std::array<std::string_view, 3> refused_names{{"Dialog_OK", "../dialog_ok_up", "dialog ok"}};
    for (const std::string_view name : refused_names) {
        bool refused = false;
        try {
            (*lamp).light_planes = std::string(name);
        } catch (const std::invalid_argument&) {
            refused = true;
        }
        require(refused && (*lamp).light_planes.read() == "indicator_clear_up",
                "a name that is not a bake's is refused, keeping the old one: " + std::string(name));
    }
    (*lamp).light_planes = std::string{};
    require((*lamp).light_planes.read().empty(), "an empty name draws the theme's lens");
    (*lamp).lamp_color = std::optional<Color>{Color::rgba(111, 255, 137)};
    require((*lamp).lamp_color.read() == std::optional<Color>{Color::rgba(111, 255, 137)}, "a lamp colour is kept");
    require(!(*lamp).light_planes_shown(), "outside a window no bake is shown");
}

void put_word(std::vector<std::byte>& bytes, std::uint32_t word) {
    for (std::uint32_t shift = 0U; shift < 32U; shift += 8U) {
        bytes.push_back(static_cast<std::byte>((word >> shift) & 0xffU));
    }
}

void put_float(std::vector<std::byte>& bytes, float value) {
    std::uint32_t word = 0U;
    std::memcpy(&word, &value, sizeof(word));
    put_word(bytes, word);
}

// A 6 x 4 bake with one lamp in the baker's layout (lamp_keys.cpp,
// write_buffers): covered inside, uncovered on its outer ring of pixels, so
// that the ring shows whether any light reaches past the coverage.
std::vector<std::byte> ringed_bake() {
    constexpr std::uint32_t width = 6U;
    constexpr std::uint32_t height = 4U;
    std::vector<float> coverage{};
    for (std::uint32_t y = 0U; y < height; ++y) {
        for (std::uint32_t x = 0U; x < width; ++x) {
            const bool edge = x == 0U || y == 0U || x == width - 1U || y == height - 1U;
            coverage.push_back(edge ? 0.0F : 1.0F);
        }
    }
    std::vector<std::byte> bytes{};
    put_word(bytes, width);
    put_word(bytes, height);
    put_word(bytes, 1U);
    // Room, then the lamp, each premultiplied by the coverage.
    for (const float cover : coverage) {
        for (std::size_t channel = 0U; channel < 3U; ++channel) put_float(bytes, 0.05F * cover);
    }
    for (const float cover : coverage) {
        for (std::size_t channel = 0U; channel < 3U; ++channel) put_float(bytes, 0.6F * cover);
    }
    for (const float cover : coverage) put_float(bytes, cover);
    return bytes;
}

std::filesystem::path write_test_bake() {
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "gui_forms_indicator_lamp_tests";
    std::filesystem::create_directories(directory);
    const std::vector<std::byte> bytes = ringed_bake();
    std::ofstream file(directory / "lamp_test_up.f32", std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    require(static_cast<bool>(file), "the test bake is written");
    return directory;
}

// Whether every pixel of `picture` in the outer `ring` pixels of `lens`
// (capture pixels) equals the capture's corner pixel, the window behind
// the lamp.
bool ring_untouched(const WindowCapture& picture, Rect lens, std::uint32_t ring) {
    const std::uint32_t left = static_cast<std::uint32_t>(lens.x);
    const std::uint32_t top = static_cast<std::uint32_t>(lens.y);
    const std::uint32_t right = left + static_cast<std::uint32_t>(lens.width);
    const std::uint32_t bottom = top + static_cast<std::uint32_t>(lens.height);
    if (right > picture.pixel_width || bottom > picture.pixel_height) return false;
    for (std::uint32_t y = top; y < bottom; ++y) {
        for (std::uint32_t x = left; x < right; ++x) {
            const bool inner = x >= left + ring && x < right - ring && y >= top + ring && y < bottom - ring;
            if (inner) continue;
            const std::size_t at = (static_cast<std::size_t>(y) * picture.pixel_width + x) * 4U;
            for (std::size_t channel = 0U; channel < 4U; ++channel) {
                if (picture.bgra[at + channel] != picture.bgra[channel]) return false;
            }
        }
    }
    return true;
}

bool same_pixels(const WindowCapture& a, const WindowCapture& b) {
    const bool same = a.pixel_width == b.pixel_width && a.pixel_height == b.pixel_height && a.bgra == b.bgra;
    return same;
}

void set_high_contrast(Window& window, bool forced) {
    PresentationSettings settings = window.presentation_base();
    settings.high_contrast = forced;
    window.set_presentation_settings(settings);
}

void test_baked_lens() {
    OffscreenRasterOptions options{};
    options.font_directory = GUI_FORMS_TEST_FONT_DIR;
    OffscreenRasterCreateResult made_raster = OffscreenRaster::create(options);
    require(made_raster.succeeded(), "the bundled faces make a raster");
    if (!made_raster.succeeded()) return;
    const std::unique_ptr<OffscreenRaster> raster = made_raster.take_raster();
    const std::filesystem::path directory = write_test_bake();

    // No directory: the name alone shows nothing.
    LampWindow made = lamp_window(Size{30.0, 20.0}, {});
    set_reduced_motion(*made.window, true);
    (*made.lamp).light_planes = std::string("lamp_test_up");
    (*made.window).perform_layout();
    require(!(*made.lamp).light_planes_shown(), "without a light-plane directory the theme's lens is drawn");

    (*(*made.window).resources()).set_light_plane_directory(directory);
    (*made.window).perform_layout();
    require((*made.lamp).light_planes_shown(), "with the bake present the lens is composited from it");
    const IndicatorLamp::Parts parts = (*made.lamp).parts();
    require(parts.lens.bounds == Rect{0.0, 0.0, 30.0, 20.0} && parts.bezel.bounds == parts.lens.bounds,
            "the baked lens takes the bake's aspect over the client area, and the bezel gives way");

    OffscreenAttachment attached(*raster, *made.window);
    // The lens in the capture's pixels: the margin, then the client area.
    const Rect lens{4.0, 4.0, 30.0, 20.0};
    const std::optional<WindowCapture> dark = capture_lamp(attached, made);
    (*made.lamp).lamp_color = std::optional<Color>{Color::rgba(111, 255, 137)};
    (*made.lamp).state = IndicatorLampState::on;
    const std::optional<WindowCapture> green = capture_lamp(attached, made);
    (*made.lamp).lamp_color = std::optional<Color>{Color::rgba(255, 97, 56)};
    const std::optional<WindowCapture> red = capture_lamp(attached, made);
    require(dark.has_value() && green.has_value() && red.has_value(), "the baked lamp is captured");
    if (!dark || !green || !red) return;
    write_preview(*dark, "indicator_lamp_baked_off");
    write_preview(*green, "indicator_lamp_baked_green");
    write_preview(*red, "indicator_lamp_baked_red");
    const Difference lit = compare(*dark, *green, lens);
    require(lit.any && !lit.outside, "the baked lamp lights inside its lens only");
    const Difference recoloured = compare(*green, *red, lens);
    require(recoloured.any && !recoloured.outside, "one bake lights in each lamp colour, inside the lens only");
    // Stretched five times, the bake's uncovered ring covers three device
    // pixels on each side, where nothing may be drawn, lit or not.
    require(ring_untouched(*dark, lens, 3U) && ring_untouched(*green, lens, 3U),
            "no light reaches past the bake's coverage");

    // A missing bake: the theme's lens, exactly as with none named.
    (*made.lamp).light_planes = std::string("missing_bake");
    (*made.window).perform_layout();
    require(!(*made.lamp).light_planes_shown(), "a missing bake shows the theme's lens");
    require((*made.lamp).parts().bezel.bounds == Rect{0.0, 0.0, 20.0, 20.0}, "and its round bezel's placement");
    const std::optional<WindowCapture> missing = capture_lamp(attached, made);
    (*made.lamp).light_planes = std::string{};
    const std::optional<WindowCapture> unnamed = capture_lamp(attached, made);
    require(missing.has_value() && unnamed.has_value() && same_pixels(*missing, *unnamed),
            "a lamp whose bake is missing draws as one that names none");

    // Forced high contrast: the role's colours stand, not the bake.
    (*made.lamp).light_planes = std::string("lamp_test_up");
    set_high_contrast(*made.window, true);
    (*made.window).perform_layout();
    require(!(*made.lamp).light_planes_shown(), "in forced high contrast the theme's lens is drawn");
    set_high_contrast(*made.window, false);
    (*made.window).perform_layout();
    require((*made.lamp).light_planes_shown(), "and the bake again after it");
}

} // namespace

int main() {
    test_states_and_semantics();
    test_detached_changes_at_once();
    test_warm_up_and_cool_down();
    test_parts();
    test_lights_only_the_lens();
    test_lamp_colour_and_kept_composite();
    test_light_plane_properties();
    test_baked_lens();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return EXIT_FAILURE;
    }
    std::cout << "indicator lamp tests passed\n";
    return EXIT_SUCCESS;
}
