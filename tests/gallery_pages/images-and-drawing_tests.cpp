#include "support.hpp"
#include "gui_forms/controls/picture_box.hpp"
#include "gui_forms/controls/picture_box_values.hpp"

namespace gallery_pages_tests {

void test_images_and_drawing(gui_forms::Window& window) {
    select_page(window, "images-and-drawing");
    std::shared_ptr<gui_forms::PictureBox> picture = std::dynamic_pointer_cast<PictureBox>(
        window.find("gallery.images.picture.4"));
    require(picture && (*picture).has_valid_image() &&
                (*picture).size_mode.read() == PictureBoxSizeMode::zoom &&
                (*picture).image_size() == Size{128.0, 80.0},
            "gallery image page must use the public PictureBox and image registry");
    const std::string image_semantics = window.semantic_snapshot().to_json();
    require(image_semantics.find("\"role\":\"image\"") != std::string::npos &&
                image_semantics.find("128 x 80") != std::string::npos,
            "gallery image page must publish image semantics and intrinsic size");
    CountingPainter image_painter;
    window.paint(image_painter, {0.0, 0.0, 1280.0, 820.0});
    // Eight PictureBox consumers on the page plus the shell's status badge.
    require(image_painter.images == 9U,
            "gallery image page must render all eight public PictureBox consumers");
    require(image_painter.image_regions >= 9U,
            "gallery image page must retain source-cropped nine-patch material");
    require(image_painter.image_patterns == 1U,
            "gallery image page must retain one bounded exact-period tile command");
}

} // namespace gallery_pages_tests
