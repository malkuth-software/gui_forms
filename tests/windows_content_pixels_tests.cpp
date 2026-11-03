// What disabled means (owner, 2026-10-20: "disabled - usually- is a change in
// font color or control highlighting. greyed out, you could say."): a
// disabled control greys its chrome, the text colour of its labels and its
// highlighting (edge, well and plates), while its content stays exact (T2
// GIVEN E2-1 and H1-1). Through the Windows host's raster, the same control
// is painted enabled and disabled: its image part and its content colour
// write their given pixels both times, while its label, its well and its
// icon (chrome) change.
#include "gui_forms/control.hpp"
#include "gui_forms/theme.hpp"
#include "../src/host/windows/application/windows_host.cpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <vector>

using namespace gui_forms;

namespace {

int failures = 0;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    failures = failures + 1;
}

// One painted scene's pixels as 0xRRGGBB, row by row.
struct Scene final {
    std::vector<std::uint32_t> pixels;
    std::uint32_t width{};

    [[nodiscard]] std::uint32_t at(std::uint32_t x, std::uint32_t y) const {
        return pixels[static_cast<std::size_t>(y) * width + x];
    }
    // Whether any pixel of the rectangle differs from `other`'s.
    [[nodiscard]] bool differs(const Scene& other, std::uint32_t left, std::uint32_t top, std::uint32_t right,
                               std::uint32_t bottom) const {
        for (std::uint32_t y = top; y < bottom; ++y) {
            for (std::uint32_t x = left; x < right; ++x) {
                if (at(x, y) != other.at(x, y)) return true;
            }
        }
        return false;
    }
};

// A framebuffer's pixels as 0xRRGGBB, row by row.
Scene read_scene(PaintFramebuffer& framebuffer) {
    Scene scene{};
    const std::span<std::byte> bytes = framebuffer.pixels();
    scene.width = framebuffer.width();
    for (std::uint32_t y = 0U; y < framebuffer.height(); ++y) {
        for (std::uint32_t x = 0U; x < scene.width; ++x) {
            const std::size_t offset = static_cast<std::size_t>(y) * framebuffer.row_bytes() +
                                       static_cast<std::size_t>(x) * 4U;
            const std::uint32_t blue = static_cast<std::uint32_t>(bytes[offset]);
            const std::uint32_t green = static_cast<std::uint32_t>(bytes[offset + 1U]);
            const std::uint32_t red = static_cast<std::uint32_t>(bytes[offset + 2U]);
            scene.pixels.push_back((red << 16U) | (green << 8U) | blue);
        }
    }
    return scene;
}

// The parts of one control, in logical px on a 64 x 24 scene at scale 1:
// - an image part at (0, 0, 2, 2);
// - a content-colour swatch at (4, 0, 2, 2);
// - an icon at (8, 0, 2, 2), showing the same picture as the image;
// - a well, with its edge, at (12, 0, 20, 12);
// - a label at (34, 0, 30, 24).
Scene paint_control(const std::shared_ptr<gui_forms::detail::BundledFontSet>& fonts, const ImageRegistry& registry,
                    ImageId picture, bool disabled) {
    const std::shared_ptr<Control> control = make_control<Control>(StableId("content"));
    host::WindowsRaster host_painter;
    host_painter.set_bundled_fonts(fonts);
    const std::unique_ptr<PaintFramebuffer> framebuffer = host_painter.painter().create_framebuffer({64.0, 24.0}, 1.0);
    Scene scene{};
    if (framebuffer == nullptr || !(*framebuffer).begin(registry, {0.0, 0.0, 64.0, 24.0})) {
        require(false, "the framebuffer begins");
        return scene;
    }
    Painter& painter = (*framebuffer).painter();
    painter.fill_rect({0.0, 0.0, 64.0, 24.0}, Color::rgba(255, 255, 255, 255));
    AnatomyStateSet states{};
    if (disabled) states.insert(AnatomyState::disabled);

    PartPaint image{};
    image.role = PartRole::image;
    image.bounds = {0.0, 0.0, 2.0, 2.0};
    image.image = picture;
    image.states = states;
    paint_part(painter, *control, image);

    PartPaint swatch{};
    swatch.role = PartRole::fill;
    swatch.bounds = {4.0, 0.0, 2.0, 2.0};
    swatch.content_color = Color::rgba(200, 40, 90, 255);
    swatch.states = states;
    paint_part(painter, *control, swatch);

    PartPaint icon = image;
    icon.role = PartRole::icon;
    icon.bounds = {8.0, 0.0, 2.0, 2.0};
    paint_part(painter, *control, icon);

    PartPaint well{};
    well.role = PartRole::well;
    well.family = ControlVisualRole::editor;
    well.bounds = {12.0, 0.0, 20.0, 12.0};
    well.states = states;
    paint_part(painter, *control, well);

    PartPaint label{};
    label.role = PartRole::label;
    label.family = ControlVisualRole::button;
    label.bounds = {34.0, 0.0, 30.0, 24.0};
    label.text = "Hb";
    label.font = FontSpec{.role = FontRole::content, .size = 16.0};
    label.states = states;
    paint_part(painter, *control, label);
    (*framebuffer).end();
    scene = read_scene(*framebuffer);
    return scene;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "pass the fixture font directory\n";
        return 1;
    }
    const std::shared_ptr<gui_forms::detail::BundledFontSet> fonts = host::load_bundled_fonts(std::filesystem::path(argv[1]));
    require(fonts != nullptr, "the fixture fonts load");

    // A 2 x 2 opaque picture, premultiplied BGRA rows.
    const std::array<std::uint8_t, 16> picture{
        10, 20, 200, 255,  30, 160, 40, 255,
        220, 90, 15, 255,  5, 5, 5, 255,
    };
    const std::array<std::uint32_t, 4> picture_rgb{0xC8140AU, 0x28A01EU, 0x0F5ADCU, 0x050505U};
    ImageRegistry registry{};
    const ImageLoadResult loaded = registry.load_bgra32_premultiplied(
        2U, 2U, 8U, std::span<const std::byte>(reinterpret_cast<const std::byte*>(picture.data()), picture.size()));
    require(loaded.error == ImageResourceError::none, "the picture loads");

    const Scene enabled = paint_control(fonts, registry, loaded.image, false);
    const Scene disabled = paint_control(fonts, registry, loaded.image, true);
    if (enabled.pixels.empty() || disabled.pixels.empty()) return 1;

    // Content stays exact.
    for (std::uint32_t y = 0U; y < 2U; ++y) {
        for (std::uint32_t x = 0U; x < 2U; ++x) {
            require(disabled.at(x, y) == picture_rgb[y * 2U + x] && enabled.at(x, y) == picture_rgb[y * 2U + x],
                    "an image part writes its picture's pixels exactly, disabled or not");
            require(disabled.at(4U + x, y) == 0xC8285AU && enabled.at(4U + x, y) == 0xC8285AU,
                    "a content colour writes its given colour exactly, disabled or not");
        }
    }
    // Chrome greys out.
    require(disabled.differs(enabled, 34U, 0U, 64U, 24U), "the disabled control's label changes colour");
    require(disabled.differs(enabled, 12U, 0U, 32U, 12U), "the disabled control's well and edge change");
    require(disabled.differs(enabled, 8U, 0U, 10U, 2U), "the disabled control's icon keeps its dimming");

    if (failures != 0) return 1;
    std::cout << "windows content pixel tests passed\n";
    return 0;
}
