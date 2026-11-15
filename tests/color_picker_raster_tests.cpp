// ColorPicker's colour areas drawn through theme parts (design language
// 8.5, owner decision W-7; the paint census admits no content exemption):
// the field, hue track, alpha track and swatch are well parts carrying
// content colours, and the content rules the default and familiar themes
// compile (content_parts_sheet) draw them as gradients over a
// checkerboard, with currentcolor naming each part's content colour. Drawn
// through the offscreen raster: the field runs white to the hue across and
// to black down and follows a new hue, the spectrum runs red to cyan at its
// middle, an opaque swatch shows the value and a transparent one the
// checkerboard. In forced high contrast (ruling 9) the content keeps its
// colours inside high contrast's frame.

#include "../src/core/theme/theme_compiler/content_parts_sheet.hpp"

#include "gui_forms/capture/offscreen_raster/offscreen_raster.hpp"
#include "gui_forms/controls/color_picker.hpp"
#include "gui_forms/resources/image_registry/image_registry.hpp"
#include "gui_forms/theme.hpp"
#include "gui_forms/theme/stock_anatomies/stock_anatomies.hpp"
#include "gui_forms/theme/theme_compiler/theme_compiler.hpp"
#include "gui_forms/window/window.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {
using namespace gui_forms;

int failures = 0;

void require(bool condition, const std::string& message) {
    if (condition) return;
    std::cerr << "gui_forms_color_picker_raster_tests: " << message << '\n';
    ++failures;
}

struct Pixel final {
    std::uint8_t red{};
    std::uint8_t green{};
    std::uint8_t blue{};
};

// The capture's pixel at logical (x, y), at scale 1: premultiplied BGRA8
// rows of pixel_width pixels; the window's backplane makes it opaque.
Pixel pixel_at(const WindowCapture& capture, double x, double y) {
    const std::size_t column = static_cast<std::size_t>(x);
    const std::size_t row = static_cast<std::size_t>(y);
    const std::size_t offset = (row * static_cast<std::size_t>(capture.pixel_width) + column) * 4U;
    Pixel pixel{};
    if (column >= capture.pixel_width || row >= capture.pixel_height || offset + 3U >= capture.bgra.size()) {
        return pixel;
    }
    pixel.blue = static_cast<std::uint8_t>(capture.bgra[offset]);
    pixel.green = static_cast<std::uint8_t>(capture.bgra[offset + 1U]);
    pixel.red = static_cast<std::uint8_t>(capture.bgra[offset + 2U]);
    return pixel;
}

bool high(std::uint8_t channel) {
    const bool result = channel >= 200U;
    return result;
}

bool low(std::uint8_t channel) {
    const bool result = channel <= 60U;
    return result;
}

std::optional<WindowCapture> capture(OffscreenAttachment& attached, bool high_contrast = false) {
    OffscreenCaptureOptions options{};
    options.scale = 1.0;
    options.high_contrast = high_contrast;
    std::optional<WindowCapture> picture = attached.capture_window(options);
    return picture;
}

// The content rules compiled as a theme package against the stock
// anatomies and the picker's own (the stock table gains it from the next
// Gallery schema).
std::shared_ptr<const Theme> content_theme(ImageRegistry& images, const ColorPicker& picker) {
    std::vector<ControlAnatomy> controls;
    for (const ControlAnatomy& anatomy : stock_control_anatomies()) controls.push_back(anatomy);
    controls.push_back(picker.anatomy());
    const ThemeLibrary library{};
    ThemeCompileOptions options{};
    options.controls = controls;
    const ThemePackage package = theme_sheet_package(detail::content_parts_sheet, "content-parts");
    const ThemeCompileResult compiled = compile_theme_package(package, library, images, options);
    require(compiled.succeeded(), "the content rules compile");
    if (!compiled.succeeded()) return nullptr;
    for (const ThemeCompileDiagnostic& diagnostic : compiled.diagnostics()) {
        // The content colours are literal on purpose (W-7): a derived
        // variant must not remap them (W201).
        if (diagnostic.code == "W201") continue;
        require(false, "the content rules compile without findings: " + format_theme_diagnostic(diagnostic));
    }
    return compiled.theme();
}

void test_content_parts() {
    OffscreenRasterOptions raster_options{};
    raster_options.font_directory = GUI_FORMS_TEST_FONT_DIR;
    OffscreenRasterCreateResult made_raster = OffscreenRaster::create(raster_options);
    require(made_raster.succeeded(), "the bundled faces make a raster");
    if (!made_raster.succeeded()) return;
    const std::unique_ptr<OffscreenRaster> raster = made_raster.take_raster();

    const std::shared_ptr<ColorPicker> picker =
        make_control<ColorPicker>(StableId("picker"), Color::rgba(255, 0, 0));
    Window window(picker, {220.0, 180.0});
    ImageRegistry images{};
    const std::shared_ptr<const Theme> theme = content_theme(images, *picker);
    if (theme == nullptr) return;
    window.set_theme(theme);
    window.perform_layout();
    OffscreenAttachment attached(*raster, window);
    const ColorPicker::Parts parts = (*picker).parts();
    const Rect field = parts.field.bounds;
    const Rect hue = parts.hue_track.bounds;
    const Rect swatch = parts.swatch.bounds;
    require(field.width > 20.0 && hue.height > 20.0 && swatch.width > 12.0, "the parts are laid out");

    // The thumbs are drawn over the colour areas where the value puts
    // them: the field's thumb (10 units square) centred on the field's top
    // right corner for a pure hue at full brightness, the hue thumb (6
    // units tall, across the track) centred on the spectrum's top for hue
    // 0. The samples near those ends sit clear of them.
    const double field_right_sample = field.x + field.width - 8.0;
    const double hue_top_sample = hue.y + 4.0;

    std::optional<WindowCapture> red = capture(attached);
    require(red.has_value(), "the picker is captured");
    if (!red) return;
    const Pixel field_corner = pixel_at(*red, field_right_sample, field.y + 2.0);
    require(high(field_corner.red) && low(field_corner.green) && low(field_corner.blue),
            "the field's top right is the hue's pure colour");
    const Pixel field_white = pixel_at(*red, field.x + 3.0, field.y + 2.0);
    require(high(field_white.red) && high(field_white.green) && high(field_white.blue), "the field's top left is white");
    const Pixel field_foot = pixel_at(*red, field.x + field.width * 0.5, field.y + field.height - 2.0);
    require(low(field_foot.red) && low(field_foot.green) && low(field_foot.blue), "the field's foot is black");
    const Pixel hue_top = pixel_at(*red, hue.x + hue.width * 0.5, hue_top_sample);
    require(high(hue_top.red) && low(hue_top.green) && low(hue_top.blue), "the spectrum starts red");
    const Pixel hue_middle = pixel_at(*red, hue.x + hue.width * 0.5, hue.y + hue.height * 0.5);
    require(low(hue_middle.red) && high(hue_middle.green) && high(hue_middle.blue), "the spectrum's middle is cyan");
    const Pixel swatch_red = pixel_at(*red, swatch.x + swatch.width * 0.5, swatch.y + swatch.height * 0.5);
    require(high(swatch_red.red) && low(swatch_red.green) && low(swatch_red.blue), "an opaque swatch shows the value");

    // A new hue re-assembles the field's material with the new colour.
    (*picker).value = Color::rgba(0, 0, 255);
    std::optional<WindowCapture> blue = capture(attached);
    require(blue.has_value(), "the picker is captured again");
    if (!blue) return;
    const Pixel field_blue = pixel_at(*blue, field_right_sample, field.y + 2.0);
    require(low(field_blue.red) && low(field_blue.green) && high(field_blue.blue), "the field follows the hue");

    // A transparent value shows the checkerboard's two squares. The
    // pattern is generated on an idle frame (E10) and the part shows its
    // ground until then, so the window's frames run between captures. An
    // idle frame runs only while no paint is owed (SurfaceRoot::
    // paint_pending), and a capture leaves the area it rebuilt owed to the
    // screen as damage, which a host takes when it presents: with no host
    // here, the test takes it, as a host's frame would.
    // The samples sit in the checker's lower row of squares (rows 5 to 9 of
    // its 10-unit tile, the dark square left of the light) and 12 units in:
    // clear of the editor well's inner top shade (`box-shadow: inset 0 1px
    // 2px #294b7730`, the default theme's), which tints the swatch's top
    // and left edges blue-grey (229, 230, 230 over the 232 ground).
    (*picker).value = Color::rgba(0, 0, 255, 0);
    bool checkered = false;
    bool grey = false;
    for (int attempt = 0; attempt < 8 && !checkered; ++attempt) {
        std::optional<WindowCapture> clear = capture(attached);
        if (!clear) break;
        const Pixel dark = pixel_at(*clear, swatch.x + 12.0, swatch.y + 7.0);
        const Pixel light = pixel_at(*clear, swatch.x + 17.0, swatch.y + 7.0);
        grey = light.red == light.green && light.green == light.blue && dark.red == dark.green &&
               dark.green == dark.blue;
        checkered = grey && light.red >= dark.red + 20U;
        for (int frame = 0; frame < 4 && window.next_wake(); ++frame) {
            static_cast<void>(window.take_damage());
            static_cast<void>(window.poll_frame_schedule(*window.next_wake()));
        }
    }
    require(grey, "a transparent swatch shows grey");
    require(checkered, "a transparent swatch shows the checkerboard's two squares");

    // Forced high contrast: content is data, not chrome (W-7, ruling 9).
    (*picker).value = Color::rgba(255, 0, 0);
    std::optional<WindowCapture> contrast = capture(attached, true);
    require(contrast.has_value(), "the high-contrast picker is captured");
    if (!contrast) return;
    const Pixel contrast_corner = pixel_at(*contrast, field_right_sample, field.y + 2.0);
    require(high(contrast_corner.red) && low(contrast_corner.green) && low(contrast_corner.blue),
            "in high contrast the field keeps the hue's pure colour");
    const Pixel contrast_middle = pixel_at(*contrast, hue.x + hue.width * 0.5, hue.y + hue.height * 0.5);
    require(low(contrast_middle.red) && high(contrast_middle.green) && high(contrast_middle.blue),
            "in high contrast the spectrum keeps its colours");
    const Pixel contrast_swatch = pixel_at(*contrast, swatch.x + swatch.width * 0.5, swatch.y + swatch.height * 0.5);
    require(high(contrast_swatch.red) && low(contrast_swatch.green) && low(contrast_swatch.blue),
            "in high contrast the swatch shows the value");
    const Pixel frame = pixel_at(*contrast, field_right_sample, field.y);
    require(!(high(frame.red) && low(frame.green) && low(frame.blue)),
            "in high contrast the field is framed by high contrast's edge, not its content");

}

} // namespace

int main() {
    test_content_parts();
    if (failures != 0) return EXIT_FAILURE;
    std::cout << "gui_forms_color_picker_raster_tests: all tests passed\n";
    return EXIT_SUCCESS;
}
