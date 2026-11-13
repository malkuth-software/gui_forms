#include "visual_inspector_lab.hpp"

#include "gui_forms/control/control/control.hpp"
#include "gui_forms/dirty.hpp"
#include "gui_forms/display/types/display_types.hpp"
#include "gui_forms/scheduler/types/scheduler_types.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/visual_inspection/types/visual_inspection_types.hpp"
#include "gui_forms/window/window.hpp"
#include "gui_forms_demo/visual_inspector_view.hpp"

#include <chrono>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using namespace gui_forms;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

class NullPainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}
};

// The readout's semantic value is reused while the window is unchanged and
// recomputed when the target changes: its bounds, its enabled state, or the
// target itself.
std::string readout_value(Window& window) {
    const Control::Ptr readout = window.find("visual-inspector.readout");
    require(readout != nullptr, "the lab has its visual state readout");
    const std::string value = (*readout).semantic_descriptor().value;
    return value;
}

void test_readout_value_follows_its_target(Window& window) {
    window.perform_layout();
    const std::string first = readout_value(window);
    const std::string again = readout_value(window);
    require(first == again && first.find("visual-inspector.specimen-card; ") == 0U,
            "an unchanged window gives the readout the same value");

    const Control::Ptr card = window.find("visual-inspector.specimen-card");
    require(card != nullptr, "the lab has its specimen card");
    const Rect bounds = (*card).requested_bounds();
    (*card).set_requested_bounds(Rect{bounds.x + 7.0, bounds.y, bounds.width, bounds.height});
    window.perform_layout();
    const std::string moved = readout_value(window);
    require(moved != first, "moving the target changes the readout's value");

    (*card).enabled = false;
    const std::string disabled = readout_value(window);
    require(disabled.find("disabled") != std::string::npos,
            "disabling the target shows in the readout's value");
    (*card).enabled = true;
    (*card).set_requested_bounds(bounds);
    window.perform_layout();
    require(readout_value(window) == first, "restoring the target restores the readout's value");

    const std::shared_ptr<VisualInspectorView> readout =
        std::dynamic_pointer_cast<VisualInspectorView>(window.find("visual-inspector.readout"));
    require(readout != nullptr, "the readout is a VisualInspectorView");
    (*readout).target_stable_id = "visual-inspector.specimen-button";
    require(readout_value(window).find("visual-inspector.specimen-button; ") == 0U,
            "a new target gives the readout a new value");
    (*readout).target_stable_id = "visual-inspector.specimen-card";
}

} // namespace

int main() {
    try {
        std::unique_ptr<Window> window =
            gui_forms::visual_inspector_lab::make_visual_inspector_lab();
        NullPainter painter;
        static_cast<void>((*window).paint(
            painter, {0.0, 0.0, 1160.0, 700.0}));
        const VisualInspectionSnapshot snapshot =
            (*window).visual_inspection_snapshot();
        const VisualControlInspection* card =
            snapshot.find("visual-inspector.specimen-card");
        const VisualControlInspection* text =
            snapshot.find("visual-inspector.specimen-title");
        const VisualControlInspection* clipped =
            snapshot.find("visual-inspector.clipped-child");
        const VisualControlInspection* overlay =
            snapshot.find("visual-inspector.overlay");
        require(card && (*card).authored_material && (*card).display_chunk_current &&
                    (*(*card).authored_material).fills.size() == 3U,
                "lab must dogfood ordered authored material inspection");
        require(text && (*text).display_chunk_current,
                "lab must dogfood committed resolved text inspection");
        require(clipped && (*clipped).layout.clipped_by_ancestor,
                "lab must dogfood ancestor clip inspection");
        require(overlay && (*overlay).paint_plane == PaintPlane::overlay &&
                    (*overlay).state.hit_test_transparent,
                "lab overlay must remain pointer-transparent and ordered last");
        require(snapshot.total_controls >= 20U,
                "lab must contain a representative retained inspection surface");
        const std::string semantics = (*window).semantic_snapshot().to_json();
        require(semantics.find("Visual state inspector") != std::string::npos &&
                    semantics.find("visual-inspector.specimen-card") !=
                        std::string::npos,
                "lab inspector must expose its target and current visual summary to accessibility");
        require(semantics.find("visual-inspector.overlay") == std::string::npos,
                "pointer-transparent visual geometry overlay must remain absent from accessibility");
        // The inspector polls on a UI timer and repaints only what changed:
        // an idle poll leaves nothing to paint (it used to repaint the whole
        // window through the overlay four times a second); it polls once a second.
        static_cast<void>((*window).take_damage());
        const FrameTime first_poll = FrameClock::now() + std::chrono::milliseconds(1100);
        static_cast<void>((*window).poll_frame_schedule(first_poll));
        require(!(*window).needs_frame(), "an idle inspector poll must not repaint");
        const Control::Ptr card_control = (*window).find("visual-inspector.specimen-card");
        require(card_control != nullptr, "lab must keep its inspected card");
        (*card_control).enabled = false;
        static_cast<void>((*window).paint(painter, {0.0, 0.0, 1160.0, 700.0}));
        static_cast<void>((*window).take_damage());
        const std::shared_ptr<VisualInspectorView> view =
            std::dynamic_pointer_cast<VisualInspectorView>((*window).find("visual-inspector.readout"));
        require(view != nullptr, "lab must keep its inspector view");
        // The inspector may paint before its target's new display chunk is
        // committed, so one poll can repaint it; after that it is current.
        static_cast<void>((*view).refresh_if_changed());
        static_cast<void>((*window).paint(painter, {0.0, 0.0, 1160.0, 700.0}));
        static_cast<void>((*window).take_damage());
        require(!(*view).refresh_if_changed(), "a repainted inspector must settle after one poll");
        // Repaint only the card: the inspector now shows a stale state, and
        // only its poll can notice.
        (*card_control).enabled = true;
        static_cast<void>((*window).paint(painter, (*card_control).absolute_bounds()));
        static_cast<void>((*window).take_damage());
        require(!(*window).needs_frame(), "repainting the card must leave nothing pending");
        const FrameTime second_poll = first_poll + std::chrono::milliseconds(1100);
        static_cast<void>((*window).poll_frame_schedule(second_poll));
        require((*window).needs_frame(), "a poll after the target changed must repaint the inspector");
        test_readout_value_follows_its_target(*window);
        std::cout << "gui_forms_visual_inspector_lab_tests: all tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_visual_inspector_lab_tests: " << error.what()
                  << '\n';
        return EXIT_FAILURE;
    }
}
