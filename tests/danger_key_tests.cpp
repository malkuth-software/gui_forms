// The danger key (skeudoindustrial surfaces, follow-up 5): Button::danger
// adds the custom state danger and a leading warning mark (measured and
// laid out), pulses the lit face between danger_glow_floor and full while
// it can, and holds it steady under reduced motion, while disabled and
// outside a window. Drawn through the offscreen raster in the default
// theme, the danger key differs from a plain one and not one pixel outside
// the button changes. With GUI_FORMS_PREVIEW_DIR set, the test also writes
// preview shots of a plain and a danger key.

#include "gui_forms/capture/offscreen_raster/offscreen_raster.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/button.hpp"
#include "gui_forms/controls/drop_down_button.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/surface_root/surface_root.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/window/window.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace gui_forms;

int failures = 0;

void require(bool condition, const std::string& message) {
    if (condition) return;
    std::cerr << "gui_forms_danger_key_tests: " << message << '\n';
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

struct ButtonWindow final {
    std::shared_ptr<Panel> root;
    std::shared_ptr<Button> button;
    std::unique_ptr<Window> window;
};

ButtonWindow button_window() {
    ButtonWindow made;
    made.root = make_control<Panel>(StableId("root"));
    made.button = make_control<Button>(StableId("root.erase"), "Erase all");
    (*made.button).set_requested_bounds({8.0, 8.0, 100.0, 30.0});
    (*made.root).add_child(made.button);
    made.window = std::make_unique<Window>(made.root, Size{116.0, 46.0});
    (*made.window).set_active(true);
    (*made.window).perform_layout();
    return made;
}

void set_reduced_motion(Window& window, bool reduced) {
    PresentationSettings settings = window.presentation_base();
    settings.reduced_motion = reduced;
    window.set_presentation_settings(settings);
}

void test_state_and_mark() {
    const std::shared_ptr<Button> button = make_control<Button>(StableId("erase"), "Erase all");
    require(!(*button).danger.read(), "a button is not a danger key by default");
    const ControlAnatomy& anatomy = (*button).anatomy();
    require(anatomy.custom_states.size() == 1U && anatomy.custom_states[0].name == "danger",
            "the button anatomy declares the custom state danger");
    require(!(*button).anatomy_states().contains(CustomStateIndex{0U}), "a plain button is not in the danger state");
    const Size plain = (*button).measure(Size{1000.0, 1000.0});
    (*button).danger = true;
    require((*button).anatomy_states().contains(CustomStateIndex{0U}), "a danger key takes the danger state");
    const Size marked = (*button).measure(Size{1000.0, 1000.0});
    // The column's width is the button's own layout detail; the public
    // measure shows only that the mark takes room inline.
    require(marked.width > plain.width && marked.height == plain.height,
            "the warning mark's column widens the button and keeps its height");
    require((*button).danger_glow() == 1.0 && !(*button).danger_glowing(),
            "outside a window the glow is steady and full");

    // A drop-down button inherits the state and declares it at the same index.
    const std::shared_ptr<DropDownButton> drop_down = make_control<DropDownButton>(StableId("more"));
    (*drop_down).danger = true;
    require((*drop_down).anatomy().custom_states.size() == 1U &&
                (*drop_down).anatomy().custom_states[0].name == "danger" &&
                (*drop_down).anatomy_states().contains(CustomStateIndex{0U}),
            "a drop-down danger key names the state it takes");
}

void test_glow_level() {
    const FrameInterval half = Button::danger_glow_period / 2;
    require(Button::danger_glow_level(FrameInterval{}) == 1.0, "the pulse starts full");
    require(std::fabs(Button::danger_glow_level(half) - Button::danger_glow_floor) < 1e-12,
            "half a period in, the pulse is at its floor");
    require(std::fabs(Button::danger_glow_level(Button::danger_glow_period) - 1.0) < 1e-12,
            "a whole period in, the pulse is full again");
    bool bounded = true;
    for (int step = 0; step <= 100; ++step) {
        const FrameInterval at = Button::danger_glow_period * step / 37;
        const double level = Button::danger_glow_level(at);
        if (level < Button::danger_glow_floor - 1e-12 || level > 1.0 + 1e-12) bounded = false;
    }
    require(bounded, "the pulse stays between its floor and full: a low amplitude");
}

void test_pulse_runs_and_rests() {
    ButtonWindow made = button_window();
    (*made.button).danger = true;
    require((*made.button).danger_glowing(), "in a window a danger key pulses");
    SilentPainter painter;
    double lowest = 1.0;
    for (int tick = 0; tick < 24 && (*made.window).next_wake(); ++tick) {
        static_cast<void>((*made.window).poll_frame_schedule(*(*made.window).next_wake()));
        lowest = std::min(lowest, (*made.button).danger_glow());
        (*made.button).on_paint(painter, Rect{0.0, 0.0, 100.0, 30.0});
    }
    require(lowest < 1.0 && lowest >= Button::danger_glow_floor, "the pulse dims, never below its floor");

    // Reduced motion: the next tick holds it full and stops the timer.
    set_reduced_motion(*made.window, true);
    if ((*made.window).next_wake()) {
        static_cast<void>((*made.window).poll_frame_schedule(*(*made.window).next_wake()));
    }
    require(!(*made.button).danger_glowing() && (*made.button).danger_glow() == 1.0,
            "under reduced motion the glow is steady and full");
    (*made.button).danger = false;
    (*made.button).danger = true;
    require(!(*made.button).danger_glowing(), "under reduced motion a new danger key does not pulse");
    set_reduced_motion(*made.window, false);

    // Disabled, the glow rests; enabled again, it pulses.
    (*made.button).danger = false;
    (*made.button).danger = true;
    require((*made.button).danger_glowing(), "with motion a danger key pulses again");
    (*made.button).enabled = false;
    require(!(*made.button).danger_glowing() && (*made.button).danger_glow() == 1.0,
            "a disabled danger key holds its glow steady");
    (*made.button).enabled = true;
    require((*made.button).danger_glowing(), "enabled again, it pulses");
    (*made.button).danger = false;
    require(!(*made.button).danger_glowing() && (*made.button).danger_glow() == 1.0,
            "a button that is no longer a danger key stops pulsing");

    // Out of the window, the pulse stops.
    (*made.button).danger = true;
    const Control::Ptr removed = (*made.root).remove_child((*made.button).runtime_id());
    require(removed != nullptr && !(*made.button).danger_glowing() && (*made.button).danger_glow() == 1.0,
            "a danger key taken out of its window stops pulsing");
}

std::optional<WindowCapture> capture_button(OffscreenAttachment& attached, const ButtonWindow& made) {
    OffscreenCaptureOptions options{};
    options.scale = 1.0;
    options.margin = 6.0;
    std::optional<WindowCapture> picture = attached.capture_control(made.button, options);
    return picture;
}

void write_preview(const WindowCapture& picture, const std::string& name) {
    const char* const directory = std::getenv("GUI_FORMS_PREVIEW_DIR");
    if (directory == nullptr || *directory == '\0') return;
    std::vector<std::byte> png;
    if (!encode_window_capture_png(picture, png)) return;
    std::ofstream out(std::string(directory) + "/" + name + ".png", std::ios::binary);
    out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
}

void test_no_spill() {
    OffscreenRasterOptions options{};
    options.font_directory = GUI_FORMS_TEST_FONT_DIR;
    OffscreenRasterCreateResult made_raster = OffscreenRaster::create(options);
    require(made_raster.succeeded(), "the bundled faces make a raster");
    if (!made_raster.succeeded()) return;
    const std::unique_ptr<OffscreenRaster> raster = made_raster.take_raster();
    ButtonWindow made = button_window();
    // The steady glow: the comparison is of the full lit face.
    set_reduced_motion(*made.window, true);
    OffscreenAttachment attached(*raster, *made.window);
    const std::optional<WindowCapture> plain = capture_button(attached, made);
    (*made.button).danger = true;
    const std::optional<WindowCapture> danger = capture_button(attached, made);
    require(plain.has_value() && danger.has_value(), "both keys are captured");
    if (!plain || !danger) return;
    write_preview(*plain, "danger_key_gui_forms_plain");
    write_preview(*danger, "danger_key_gui_forms_danger");
    require(plain.value().bgra.size() == danger.value().bgra.size(), "the captures have one size");
    if (plain.value().bgra.size() != danger.value().bgra.size()) return;
    // The button in the capture's pixels: the margin, then its bounds.
    const Rect face{6.0, 6.0, 100.0, 30.0};
    bool any = false;
    bool outside = false;
    const WindowCapture& before = *plain;
    const WindowCapture& after = *danger;
    for (std::uint32_t y = 0U; y < before.pixel_height; ++y) {
        for (std::uint32_t x = 0U; x < before.pixel_width; ++x) {
            const std::size_t at = (static_cast<std::size_t>(y) * before.pixel_width + x) * 4U;
            bool same = true;
            for (std::size_t channel = 0U; channel < 4U; ++channel) {
                if (before.bgra[at + channel] != after.bgra[at + channel]) same = false;
            }
            if (same) continue;
            any = true;
            const Point centre{static_cast<double>(x) + 0.5, static_cast<double>(y) + 0.5};
            if (!face.contains(centre)) outside = true;
        }
    }
    require(any, "the danger key looks different from a plain key");
    require(!outside, "the danger glow reaches nothing outside the button");
}

} // namespace

int main() {
    test_state_and_mark();
    test_glow_level();
    test_pulse_runs_and_rests();
    test_no_spill();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return EXIT_FAILURE;
    }
    std::cout << "danger key tests passed\n";
    return EXIT_SUCCESS;
}
