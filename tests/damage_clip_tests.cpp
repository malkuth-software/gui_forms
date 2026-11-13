// Damage recorded outside the client area must not keep a plane pending.
// A control whose visual outsets reach past the window edge used to leave a
// rectangle the paint could never contain, so every later paint was a full
// window paint and localized mutations never produced partial paints.
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/label.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/metrics/types/metrics_types.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/window/window.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <string_view>

namespace {

using namespace gui_forms;

class NullPainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void clip_rounded_rect(Rect, double) override {}
    void fill_rect(Rect, Color) override {}
    void fill_rounded_rect(Rect, double, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void stroke_rounded_rect(Rect, double, Color, double) override {}
    void fill_linear_gradient(Rect, Point, Point, std::span<const GradientStop>) override {}
    void fill_radial_gradient(Rect, Point, Size, std::span<const GradientStop>) override {}
    void draw_box_shadow(Rect, double, Point, double, double, Color) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}
};

// Sits in the window's top-left corner with outsets that reach past the edge.
class EdgeControl final : public Control {
public:
    explicit EdgeControl(StableId id) : Control(std::move(id)) {}
    [[nodiscard]] Insets visual_outsets() const noexcept override { return {10.5, 7.0, 10.5, 7.0}; }
};

void expect(const bool condition, const std::string_view message) {
    if (condition) return;
    std::cerr << "damage clip failure: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void outset_damage_beyond_the_window_does_not_pin_full_paints() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("clip.root"));
    std::shared_ptr<EdgeControl> edge = make_control<EdgeControl>(StableId("clip.edge"));
    std::shared_ptr<Label> label = make_control<Label>(StableId("clip.label"), "Ready");
    (*edge).set_bounds({0.0, 0.0, 9.0, 4.0});
    (*label).set_bounds({100.0, 60.0, 60.0, 16.0});
    (*root).add_child(edge);
    (*root).add_child(label);
    Window window(root, Size{200.0, 120.0});
    window.perform_layout();

    NullPainter painter;
    window.paint(painter, {0.0, 0.0, 200.0, 120.0});
    window.reset_activity_metrics();

    (*label).text = "Changed";
    window.paint(painter);
    const MetricsSnapshot metrics = window.metrics_snapshot();
    expect(metrics.paint_passes == 1U, "one paint follows the label mutation");
    expect(metrics.partial_paints == 1U && metrics.full_window_paints == 0U,
           "a localized mutation after an edge-outset control paints partially");
    expect(metrics.painted_damage_area < 200.0 * 120.0,
           "the partial paint covers less than the window");
}

}  // namespace

int main() {
    try {
        outset_damage_beyond_the_window_does_not_pin_full_paints();
    } catch (const std::exception& error) {
        std::cerr << "damage clip exception: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "damage clip tests passed\n";
    return EXIT_SUCCESS;
}
