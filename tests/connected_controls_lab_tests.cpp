#include "connected_controls_lab.hpp"

#include "gui_forms/display/types/display_types.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/visual_inspection/types/visual_inspection_types.hpp"
#include "gui_forms/window/window.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using namespace gui_forms;

void require(const bool condition, const char* message) {
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

} // namespace

int main() {
    try {
        std::unique_ptr<Window> window =
            gui_forms::connected_controls_lab::make_connected_controls_lab();
        NullPainter painter;
        static_cast<void>((*window).paint(painter, {0.0, 0.0, 1400.0, 760.0}));
        const VisualInspectionSnapshot snapshot =
            (*window).visual_inspection_snapshot();
        const VisualControlInspection* isolated =
            snapshot.find("connected-controls.isolated");
        const VisualControlInspection* first =
            snapshot.find("connected-controls.three.default");
        const VisualControlInspection* middle =
            snapshot.find("connected-controls.three.checked");
        const VisualControlInspection* disabled =
            snapshot.find("connected-controls.three.disabled");
        const VisualControlInspection* disabled_interior =
            snapshot.find("connected-controls.five.2");
        const VisualControlInspection* split =
            snapshot.find("connected-controls.two.split");
        const VisualControlInspection* vertical =
            snapshot.find("connected-controls.vertical.1");
        const VisualControlInspection* overlay =
            snapshot.find("connected-controls.overlay");
        require(isolated && first && middle && disabled && disabled_interior &&
                    split && vertical && overlay,
                "connected lab must retain every required specimen and inspector overlay");
        require((*first).layout.absolute_bounds.right() ==
                    (*middle).layout.absolute_bounds.left() &&
                    (*middle).layout.absolute_bounds.right() ==
                    (*disabled).layout.absolute_bounds.left(),
                "three-segment specimen must have exact crack-free logical boundaries");
        require((*first).state.focused && (*middle).state.enabled &&
                    !(*disabled).state.effectively_enabled &&
                    !(*disabled_interior).state.effectively_enabled,
                "lab must expose focused/default, checked-capable, disabled trailing, and disabled interior states");
        require((*isolated).total_display_operations <
                    (*middle).total_display_operations &&
                    (*middle).display_chunk_current && (*split).display_chunk_current &&
                    (*vertical).display_chunk_current,
                "connected specimens must retain committed clipped/seam paint operations");
        require((*overlay).paint_plane == PaintPlane::overlay &&
                    (*overlay).state.hit_test_transparent,
                "connected lab inspector overlay must remain pointer-transparent");
        const std::string semantics = (*window).semantic_snapshot().to_json();
        require(semantics.find("connected-controls.three.default") !=
                    std::string::npos &&
                    semantics.find("connected-controls.three.checked") !=
                    std::string::npos &&
                    semantics.find("\"role\":\"check_box\"") !=
                    std::string::npos &&
                    semantics.find("connected-controls.two.split") !=
                    std::string::npos,
                "lab must preserve separate semantic nodes and check/disclosure roles");
        std::cout << "gui_forms_connected_controls_lab_tests: all tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_connected_controls_lab_tests: " << error.what()
                  << '\n';
        return EXIT_FAILURE;
    }
}
