// A resize repaints what it touched and nothing else, and what it leaves is
// what a full repaint draws (the owner's resize model, 2026-11-03). A window
// is resized through several sizes the way a host does it: the Skia raster
// carries its pixels where the old and new extents overlap
// (SkiaRaster::resize at an unchanged scale), the window is resized, and only
// the damage it reports is painted. The presented pixels at the final size
// must equal a fresh window's full paint at that size, byte for byte, at each
// scale.
//
// The scene: a header band docked top that keeps its pixels when resized, a
// footer docked bottom with a mark anchored to its right edge, a left column
// with a bordered box stretched to its height, and a size-dependent gauge
// filling the rest. Per axis (the owner's model): a height-only resize damages
// nothing over the header, which no vertical edge touches; a width-only
// resize damages nothing over the left column, which no horizontal edge
// touches.
#include "gui_forms/gui_forms.hpp"
#include "skia_raster.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace {

using namespace gui_forms;

constexpr double header_height = 30.0;
constexpr double footer_height = 24.0;
constexpr double column_width = 60.0;
constexpr std::array<double, 3> scales{1.0, 1.5, 2.0};
constexpr Size first_size{320.0, 200.0};
// Width only, height only, both smaller (a corner), mixed, both larger.
constexpr std::array<Size, 5> later_sizes{{{360.0, 200.0}, {360.0, 260.0}, {300.0, 230.0},
                                          {340.0, 180.0}, {310.0, 240.0}}};

// A solid band whose pixels do not depend on its size.
class Band final : public Control {
public:
    explicit Band(StableId stable_id) : Control(std::move(stable_id)) {}
    void on_paint(Painter& painter, Rect bounds) override { painter.fill_rect(bounds, Color::rgba(30, 60, 110, 255)); }
    [[nodiscard]] bool paint_depends_on_size() const noexcept override { return false; }
};

// A small fixed-size mark: two squares.
class Mark final : public Control {
public:
    explicit Mark(StableId stable_id) : Control(std::move(stable_id)) {}
    void on_paint(Painter& painter, Rect bounds) override {
        painter.fill_rect(bounds, Color::rgba(200, 80, 40, 255));
        painter.fill_rect(Rect{bounds.x + 4.0, bounds.y + 4.0, 8.0, 8.0}, Color::rgba(250, 240, 200, 255));
    }
};

// Paints across its whole extent: a gradient, a diagonal and a frame, all of
// which move when its size changes (resize_redraw, the default).
class Gauge final : public Control {
public:
    explicit Gauge(StableId stable_id) : Control(std::move(stable_id)) {}
    void on_paint(Painter& painter, Rect bounds) override {
        const std::array<GradientStop, 2> stops{{{0.0, Color::rgba(240, 240, 250, 255)},
                                                 {1.0, Color::rgba(90, 140, 200, 255)}}};
        painter.fill_linear_gradient(bounds, Point{bounds.x, bounds.y},
                                     Point{bounds.x + bounds.width, bounds.y + bounds.height}, stops);
        painter.draw_line(Point{bounds.x, bounds.y + bounds.height}, Point{bounds.x + bounds.width, bounds.y},
                          Color::rgba(20, 20, 20, 255), 1.5);
        painter.stroke_rect(Rect{bounds.x + 2.5, bounds.y + 2.5, bounds.width - 5.0, bounds.height - 5.0},
                            Color::rgba(160, 30, 30, 255), 1.0);
    }
};

// One window of the scene; owns its controls.
struct Scene final {
    std::shared_ptr<Panel> root;
    std::unique_ptr<Window> window;
};

std::unique_ptr<Scene> make_scene(Size size, double scale) {
    std::unique_ptr<Scene> held = std::make_unique<Scene>();
    Scene& scene = *held;
    // Flat panels, as the vsync lab's: a background colour without a border.
    scene.root = make_control<Panel>(StableId("root"));
    (*scene.root).set_background(Color::rgba(225, 231, 237, 255));
    const std::shared_ptr<Band> header = make_control<Band>(StableId("header"));
    (*header).set_requested_bounds(Rect{0.0, 0.0, size.width, header_height});
    (*header).dock = DockStyle::top;
    const std::shared_ptr<Panel> footer = make_control<Panel>(StableId("footer"));
    (*footer).set_requested_bounds(Rect{0.0, 0.0, size.width, footer_height});
    (*footer).dock = DockStyle::bottom;
    (*footer).set_background(Color::rgba(209, 218, 226, 255));
    const std::shared_ptr<Mark> corner_mark = make_control<Mark>(StableId("footer.mark"));
    (*corner_mark).set_requested_bounds(Rect{size.width - 24.0, 4.0, 16.0, 16.0});
    (*corner_mark).anchor = AnchorStyles::top | AnchorStyles::right;
    (*footer).add_child(corner_mark);
    const std::shared_ptr<Panel> column = make_control<Panel>(StableId("column"));
    (*column).set_requested_bounds(Rect{0.0, 0.0, column_width, size.height});
    (*column).dock = DockStyle::left;
    (*column).set_background(Color::rgba(240, 244, 247, 255));
    const std::shared_ptr<Mark> column_mark = make_control<Mark>(StableId("column.mark"));
    (*column_mark).set_requested_bounds(Rect{8.0, 8.0, 16.0, 16.0});
    (*column).add_child(column_mark);
    const std::shared_ptr<Panel> box = make_control<Panel>(StableId("column.box"));
    (*box).set_border_style(BorderStyle::line);
    const double box_height = size.height - header_height - footer_height - 40.0;
    (*box).set_requested_bounds(Rect{8.0, 32.0, column_width - 16.0, box_height});
    (*box).anchor = AnchorStyles::top | AnchorStyles::bottom | AnchorStyles::left;
    (*column).add_child(box);
    const std::shared_ptr<Gauge> gauge = make_control<Gauge>(StableId("gauge"));
    (*gauge).dock = DockStyle::fill;
    // Dock order: the edges claim their sides first, the fill takes the rest.
    (*scene.root).add_child(header);
    (*scene.root).add_child(footer);
    (*scene.root).add_child(column);
    (*scene.root).add_child(gauge);
    scene.window = std::make_unique<Window>(scene.root, size);
    (*scene.window).set_scale(scale);
    (*scene.window).perform_layout();
    return held;
}

// Paints the window's damage into `raster` as a host does: the reported
// rectangles under their own clips, then the receipt released. Returns the
// damage painted.
DamageRegion paint_damage(Window& window, render::SkiaRaster& raster) {
    DamageRegion damage = window.take_damage();
    if (damage.empty()) return damage;
    raster.begin_frame(damage);
    raster.save();
    raster.clip_damage(damage);
    const std::optional<PaintReceipt> receipt = window.paint(raster, damage.rectangles());
    raster.restore();
    raster.end_frame();
    if (receipt) static_cast<void>(window.notify_presented(*receipt));
    return damage;
}

bool check(bool condition, const char* message) {
    if (!condition) std::cerr << message << '\n';
    return condition;
}

bool damage_meets(const DamageRegion& damage, Rect area) {
    const std::span<const Rect> rectangles = damage.rectangles();
    for (std::size_t index = 0; index < rectangles.size(); ++index) {
        if (!Rect::intersection(rectangles[index], area).empty()) return true;
    }
    return false;
}

// The bytes, over the visible row width, in which two rasters of one extent differ.
std::size_t differing_bytes(const render::SkiaRaster& left, const render::SkiaRaster& right) {
    if (left.pixel_width() != right.pixel_width() || left.pixel_height() != right.pixel_height()) {
        return static_cast<std::size_t>(-1);
    }
    const std::uint8_t* const left_bytes = static_cast<const std::uint8_t*>(left.pixels());
    const std::uint8_t* const right_bytes = static_cast<const std::uint8_t*>(right.pixels());
    const std::size_t width_bytes = static_cast<std::size_t>(left.pixel_width()) * 4U;
    std::size_t count = 0U;
    for (std::uint32_t row = 0U; row < left.pixel_height(); ++row) {
        const std::uint8_t* const a = left_bytes + static_cast<std::size_t>(row) * left.row_bytes();
        const std::uint8_t* const b = right_bytes + static_cast<std::size_t>(row) * right.row_bytes();
        for (std::size_t column = 0U; column < width_bytes; ++column) {
            if (a[column] != b[column]) ++count;
        }
    }
    return count;
}

} // namespace

int main() {
    bool passed = true;
    std::size_t carried_steps = 0U;
    for (const double scale : scales) {
        render::SkiaRaster raster{};
        if (!check(raster.resize(first_size, scale), "the raster is allocated")) return 1;
        const std::unique_ptr<Scene> scene = make_scene(first_size, scale);
        Window& window = *(*scene).window;
        static_cast<void>(paint_damage(window, raster));
        Size previous = first_size;
        for (const Size size : later_sizes) {
            if (!check(raster.resize(size, scale), "the raster is resized")) return 1;
            if (raster.carried_on_resize()) ++carried_steps;
            window.resize(size);
            const DamageRegion damage = paint_damage(window, raster);
            const bool height_only = size.width == previous.width && size.height != previous.height;
            const bool width_only = size.height == previous.height && size.width != previous.width;
            if (height_only) {
                passed = check(!damage_meets(damage, Rect{0.0, 0.0, size.width, header_height}),
                               "a height-only resize damages the header, which no vertical edge touches") && passed;
            }
            if (width_only) {
                const Rect column{0.0, header_height, column_width, size.height - header_height - footer_height};
                passed = check(!damage_meets(damage, column),
                               "a width-only resize damages the left column, which no horizontal edge touches") &&
                         passed;
            }
            const double area = damage.bounds().area();
            passed = check(area > 0.0, "a resize damages what it exposed") && passed;
            previous = size;
        }
        render::SkiaRaster reference{};
        if (!check(reference.resize(previous, scale), "the reference raster is allocated")) return 1;
        const std::unique_ptr<Scene> fresh = make_scene(previous, scale);
        static_cast<void>(paint_damage(*(*fresh).window, reference));
        const std::size_t differing = differing_bytes(raster, reference);
        if (differing != 0U) {
            std::cerr << "scale " << scale << ": " << differing
                      << " bytes differ from a full repaint at the final size\n";
            passed = false;
        }
    }
    passed = check(carried_steps == scales.size() * later_sizes.size(), "every resize carries the raster's pixels") &&
             passed;
    if (!passed) return 1;
    std::cout << "gui_forms_resize_damage_equivalence_tests passed\n";
    return 0;
}
