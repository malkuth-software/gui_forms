// Content scroll copies (Control::scroll_content): a control whose painted
// content moved declares it, and the window moves the retained pixels with
// a scroll copy and repaints only the band the move exposed and what is
// drawn over the area. RasterCanvas pans this way. After every pan the
// raster that applied the copies and painted only the damage equals a whole
// repaint of the same window, at scales 1 and 2, for both samplings, at
// zooms 1 and 2, with and without the transparency checker; an 8-unit
// pan damages at most the 8-unit band plus what is drawn over the canvas.
// Controls whose paint does not follow the view (an automatic checker cell
// size, a subclass that has not declared it) and views that single
// precision does not place exactly are repainted instead.

#include "gui_forms/canvas.hpp"
#include "gui_forms/basic_controls.hpp"
#include "gui_forms/window.hpp"
#include "skia_raster.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace gui_forms;
using gui_forms::render::SkiaRaster;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

constexpr Size window_size{400.0, 300.0};
constexpr Rect canvas_bounds{20.0, 30.0, 360.0, 240.0};
// A child of the canvas, drawn over it, and a later sibling crossing it.
constexpr Rect child_bounds{50.0, 50.0, 40.0, 30.0};
constexpr Rect sibling_bounds{300.0, 200.0, 60.0, 60.0};

std::shared_ptr<gui_drawing::Bitmap> make_bitmap() {
    constexpr int width = 900;
    constexpr int height = 700;
    const std::shared_ptr<gui_drawing::Bitmap> bitmap = std::make_shared<gui_drawing::Bitmap>(width, height);
    gui_drawing::BitmapEditView edit = (*bitmap).begin_edit({0, 0, width, height});
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            std::byte* pixel = edit.writable_data + y * edit.row_bytes + x * 4;
            const unsigned alpha = 90U + static_cast<unsigned>((x * 7 + y * 3) % 166);
            pixel[0] = std::byte(static_cast<unsigned>(x % 241) * alpha / 255U);
            pixel[1] = std::byte(static_cast<unsigned>(y % 239) * alpha / 255U);
            pixel[2] = std::byte(static_cast<unsigned>((x * 3 + y) % 233) * alpha / 255U);
            pixel[3] = std::byte(alpha);
        }
    }
    static_cast<void>((*bitmap).commit_edit(edit.token));
    return bitmap;
}

// A canvas that paints its own content and never declares that it follows
// the view: its pans must repaint it.
class UndeclaredCanvas final : public RasterCanvas {
public:
    explicit UndeclaredCanvas(StableId id) : RasterCanvas(std::move(id)) {}
    void on_paint(Painter& painter, Rect) override {
        const Rect bounds = client_rectangle();
        painter.fill_rect(bounds, Color::rgba(30, 60, 90));
        report_opaque_cover(painter, bounds);
        // Fixed in the client area: it does not move with the view.
        painter.fill_rect({10.0, 10.0, 30.0, 30.0}, Color::rgba(250, 200, 40));
    }
};

struct CheckerMode final {
    bool grid{};
    // Zero is the automatic size, which follows the visible area.
    double cell_size{};
};

struct Scene final {
    std::shared_ptr<Panel> root;
    std::shared_ptr<RasterCanvas> canvas;
    std::shared_ptr<gui_drawing::Bitmap> bitmap;
};

Scene make_scene(ImageSampling sampling, CheckerMode checker, bool undeclared) {
    Scene scene{};
    scene.root = make_control<Panel>(StableId("content-scroll.root"));
    (*scene.root).set_background(Color::rgba(240, 240, 240));
    scene.canvas = undeclared ? std::static_pointer_cast<RasterCanvas>(
                                    make_control<UndeclaredCanvas>(StableId("content-scroll.canvas")))
                              : make_control<RasterCanvas>(StableId("content-scroll.canvas"));
    scene.bitmap = make_bitmap();
    (*scene.canvas).bitmap = scene.bitmap;
    (*scene.canvas).transparency_grid = checker.grid;
    (*scene.canvas).transparency_cell_size = checker.cell_size;
    (*scene.canvas).canvas_background = Color::rgba(23, 37, 51);
    (*scene.canvas).sampling = sampling;
    (*scene.canvas).set_requested_bounds(canvas_bounds);
    const std::shared_ptr<Panel> child = make_control<Panel>(StableId("content-scroll.child"));
    (*child).set_background(Color::rgba(200, 40, 60));
    (*child).set_requested_bounds(child_bounds);
    (*scene.canvas).add_child(child);
    (*scene.root).add_child(scene.canvas);
    const std::shared_ptr<Panel> sibling = make_control<Panel>(StableId("content-scroll.sibling"));
    (*sibling).set_background(Color::rgba(40, 160, 90));
    (*sibling).set_requested_bounds(sibling_bounds);
    (*scene.root).add_child(sibling);
    return scene;
}

std::vector<std::uint32_t> pixels_of(const SkiaRaster& raster) {
    const std::uint32_t width = raster.pixel_width();
    const std::uint32_t height = raster.pixel_height();
    const std::uint8_t* const source = static_cast<const std::uint8_t*>(raster.pixels());
    require(source != nullptr && width != 0U && height != 0U, "the raster has pixels");
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(width) * height);
    for (std::uint32_t row = 0U; row < height; ++row) {
        std::memcpy(&pixels[static_cast<std::size_t>(row) * width], source + row * raster.row_bytes(),
                    static_cast<std::size_t>(width) * 4U);
    }
    return pixels;
}

void paint_into(SkiaRaster& raster, Window& window, const DamageRegion& damage) {
    require(raster.synchronize_images(window.image_resources()), "the images synchronize");
    raster.begin_frame(damage);
    require(window.paint(raster, damage.rectangles()).has_value(), "the window paints");
    raster.end_frame();
}

DamageRegion whole_window() {
    DamageRegion damage;
    damage.add({0.0, 0.0, window_size.width, window_size.height});
    return damage;
}

// The window painted whole into a fresh raster.
std::vector<std::uint32_t> reference_pixels(Window& window, double scale) {
    SkiaRaster reference;
    require(reference.resize(window_size, scale), "the reference raster is allocated");
    paint_into(reference, window, whole_window());
    return pixels_of(reference);
}

// The count of differing pixels; the first few are printed (index, then
// the two values) for diagnosis.
std::size_t differing_pixels(const std::vector<std::uint32_t>& left, const std::vector<std::uint32_t>& right,
                             std::uint32_t width) {
    require(left.size() == right.size(), "the rasters differ in extent");
    std::size_t differing = 0U;
    for (std::size_t index = 0U; index < left.size(); ++index) {
        if (left[index] == right[index]) continue;
        if (differing < 8U) {
            std::cerr << "  pixel (" << index % width << ", " << index / width << "): " << std::hex << left[index]
                      << " != " << right[index] << std::dec << "\n";
        }
        ++differing;
    }
    return differing;
}

struct PanStep final {
    // The canvas content's move in logical units: the origin moves the
    // other way by this over the zoom.
    Point moved;
    bool edit_first{};
};

struct CaseName final {
    std::string text;
};

// Pans the canvas step by step; after each, applies the copies, paints the
// damage and compares with a whole repaint. `copies_expected`: whether the
// pans must be copied (else repainted).
void run_case(ImageSampling sampling, double zoom, double scale, CheckerMode checker, bool undeclared,
              bool copies_expected) {
    const CaseName name{std::string(sampling == ImageSampling::linear ? "linear" : "nearest") + " zoom " +
                        std::to_string(zoom) + " scale " + std::to_string(scale) +
                        (checker.grid ? (checker.cell_size == 0.0 ? " auto checker" : " checker") : "") +
                        (undeclared ? " undeclared" : "")};
    Scene scene = make_scene(sampling, checker, undeclared);
    gui_drawing::PointF origin{140.25, 95.5};
    (*scene.canvas).set_view(zoom, origin);
    Window window(scene.root, window_size);
    window.set_scale(scale);
    window.set_scroll_copies_supported(true);
    window.perform_layout();
    SkiaRaster raster;
    require(raster.resize(window_size, scale), name.text + ": the raster is allocated");
    paint_into(raster, window, whole_window());
    static_cast<void>(window.take_damage());
    std::vector<ScrollCopy> copies;
    window.take_scroll_copies(copies);
    copies.clear();

    // Device-pixel steps: 8 units down, right, both ways, back, half a unit
    // (one device pixel at scale 2), and a pan after an edit not yet painted.
    const PanStep steps[] = {
        {{0.0, -8.0}, false}, {{-8.0, 0.0}, false}, {{8.0, -8.0}, false}, {{0.0, 8.0}, false},
        {{0.0, scale == 2.0 ? -0.5 : -1.0}, false}, {{-8.0, 0.0}, true}, {{0.0, -8.0}, false},
    };
    const double width = canvas_bounds.width;
    const double height = canvas_bounds.height;
    // What is drawn over the canvas, repainted where it is and where the
    // copy moved it.
    const Rect sibling_over = Rect::intersection(sibling_bounds, canvas_bounds);
    const double overlays = 2.0 * child_bounds.width * child_bounds.height + 2.0 * sibling_over.area();
    std::size_t step_index = 0U;
    for (const PanStep& step : steps) {
        if (step.edit_first) {
            const int x = static_cast<int>(origin.x) + 20;
            const int y = static_cast<int>(origin.y) + 30;
            (*scene.bitmap).set_pixel(x, y, gui_drawing::Color::from_name("red"));
            (*scene.bitmap).set_pixel(x + 1, y + 1, gui_drawing::Color::from_name("blue"));
            require((*scene.canvas).synchronize_bitmap(), name.text + ": the edit is published");
        }
        origin = {origin.x - step.moved.x / zoom, origin.y - step.moved.y / zoom};
        (*scene.canvas).set_view(zoom, origin);
        window.perform_layout();
        const DamageRegion damage = window.take_damage();
        copies.clear();
        window.take_scroll_copies(copies);
        if (copies_expected) {
            require(copies.size() == 1U, name.text + ": a pan records one copy");
            const double band = std::abs(step.moved.y) * width + std::abs(step.moved.x) * height;
            // An edit marked before the pan is damaged too (a few pixels,
            // grown by the linear sampling's reach).
            const double edit_reach = step.edit_first ? 4.0 * 4.0 * zoom * zoom + 64.0 : 0.0;
            require(damage.area() <= band + overlays + edit_reach + 1.0e-6,
                    name.text + ": a pan damages only the exposed band and what is drawn over the canvas (" +
                        std::to_string(damage.area()) + " > " + std::to_string(band + overlays) + ")");
        } else {
            require(copies.empty(), name.text + ": a pan the paint does not follow records no copy");
            require(damage.area() >= width * height - 1.0e-6, name.text + ": that pan repaints the canvas");
        }
        for (const ScrollCopy& copy : copies) {
            require(raster.scroll_copy(copy.clip, copy.offset), name.text + ": the raster applies the copy");
        }
        paint_into(raster, window, damage);
        const std::size_t differing =
            differing_pixels(pixels_of(raster), reference_pixels(window, scale), raster.pixel_width());
        require(differing == 0U, name.text + ", step " + std::to_string(step_index) + ": " +
                                     std::to_string(differing) + " pixels differ from a whole repaint after a pan");
        ++step_index;
    }
}

// A copy outside a host frame, before the first paint, or under a fraction
// of a device pixel is refused and the area repainted.
void check_refusals() {
    Scene scene = make_scene(ImageSampling::nearest, CheckerMode{}, false);
    (*scene.canvas).set_view(1.0, {10.0, 10.0});
    Window window(scene.root, window_size);
    window.set_scale(2.0);
    window.set_scroll_copies_supported(true);
    window.perform_layout();
    std::vector<ScrollCopy> copies;
    // Not painted yet: the canvas has not declared that its paint follows
    // the view.
    (*scene.canvas).set_view(1.0, {10.0, 18.0});
    window.perform_layout();
    window.take_scroll_copies(copies);
    require(copies.empty(), "a pan before the first paint records no copy");
    SkiaRaster raster;
    require(raster.resize(window_size, 2.0), "the raster is allocated");
    paint_into(raster, window, whole_window());
    static_cast<void>(window.take_damage());
    // A quarter unit at scale 2 is half a device pixel.
    (*scene.canvas).set_view(1.0, {10.0, 18.25});
    window.perform_layout();
    const DamageRegion damage = window.take_damage();
    window.take_scroll_copies(copies);
    require(copies.empty() && damage.area() >= canvas_bounds.width * canvas_bounds.height - 1.0e-6,
            "a pan under a device pixel repaints the canvas");
    // Copies off: the area is repainted.
    paint_into(raster, window, damage);
    window.set_scroll_copies_supported(false);
    require(!(*scene.canvas).scroll_content((*scene.canvas).client_rectangle(), {0.0, 8.0}),
            "scroll_content refuses when the window cannot copy");
    window.take_scroll_copies(copies);
    require(copies.empty() && window.take_damage().area() >= canvas_bounds.width * canvas_bounds.height - 1.0e-6,
            "a refused content scroll repaints the area");
}

} // namespace

int main() {
    try {
        const ImageSampling samplings[] = {ImageSampling::nearest, ImageSampling::linear};
        const double zooms[] = {1.0, 2.0};
        const double scales[] = {1.0, 2.0};
        for (const ImageSampling sampling : samplings) {
            for (const double zoom : zooms) {
                for (const double scale : scales) {
                    run_case(sampling, zoom, scale, CheckerMode{false, 0.0}, false, true);
                    run_case(sampling, zoom, scale, CheckerMode{true, 12.0}, false, true);
                }
            }
        }
        // Views single precision does not place exactly: a zoom or a scale
        // that is no power of two.
        run_case(ImageSampling::linear, 1.25, 1.0, CheckerMode{}, false, false);
        run_case(ImageSampling::nearest, 0.8, 2.0, CheckerMode{}, false, false);
        run_case(ImageSampling::nearest, 1.0, 1.5, CheckerMode{}, false, false);
        run_case(ImageSampling::nearest, 1.0, 2.0, CheckerMode{true, 0.0}, false, false);
        run_case(ImageSampling::nearest, 1.0, 2.0, CheckerMode{}, true, false);
        check_refusals();
    } catch (const std::exception& error) {
        std::cerr << "content scroll failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "content scroll raster tests passed\n";
    return EXIT_SUCCESS;
}
