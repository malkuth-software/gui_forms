#include "seam_proximity_lab.hpp"

#include "gui_forms/gui_forms.hpp"

#include <cmath>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>

namespace {

using namespace gui_forms;
namespace lab = gui_forms::seam_proximity_lab;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool near(double left, double right) {
    return std::abs(left - right) < 0.001;
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

Control::Ptr find_control(const Control::Ptr& root, std::string_view id) {
    if (!root) return {};
    if ((*root).stable_id().value() == id) return root;
    for (const Control::Ptr& child : (*root).children()) {
        if (Control::Ptr found = find_control(child, id)) return found;
    }
    return {};
}

std::shared_ptr<SplitContainer> find_split(Window& window,
                                           std::string_view id) {
    return std::dynamic_pointer_cast<SplitContainer>(
        find_control(window.root(), id));
}

// The seam band's face: the grip's first fill. Since T4 the band is a panel
// face the theme's engine draws, a flat fill or a gradient as the theme's
// recipe says; the tab's arrow mark is drawn later with small fill_rects.
bool face_fill(VisualPaintOperation operation) noexcept {
    switch (operation) {
    case VisualPaintOperation::fill_rect:
    case VisualPaintOperation::fill_linear_gradient:
    case VisualPaintOperation::fill_linear_gradient_spread:
        return true;
    default:
        // Every other operation (state, strokes, text, images, groups) is
        // not a face fill.
        return false;
    }
}

const VisualPaintOperationSnapshot* first_fill(
    const VisualControlInspection& control) {
    for (const VisualPaintOperationSnapshot& operation :
         control.display_operations) {
        if (face_fill(operation.operation)) {
            return &operation;
        }
    }
    return nullptr;
}

void test_named_specimens_and_atomic_rejection() {
    const SplitSeamGeometry reference = lab::reference_seam_geometry();
    const SplitSeamGeometry hairline = lab::physical_hairline_geometry();
    require(reference.thickness_policy == SplitSeamThicknessPolicy::logical &&
                reference.visible_thickness == 3.0 &&
                reference.hit_before == 2.0 && reference.hit_after == 7.0 &&
                reference.minimum_hit_target == 12.0,
            "reference specimen must encode the accepted three-logical-pixel asymmetric seam");
    require(hairline.thickness_policy ==
                SplitSeamThicknessPolicy::device_pixel_hairline &&
                hairline.hit_before + hairline.hit_after >=
                    hairline.minimum_hit_target &&
                lab::invalid_geometry_is_rejected(),
            "hairline specimen must guarantee its hit target at every display scale and reject over-budget geometry");

    std::unique_ptr<Window> window = lab::make_seam_proximity_lab();
    std::shared_ptr<SplitContainer> vertical = find_split(*window, "seam-proximity.reference");
    std::shared_ptr<SplitContainer> physical = find_split(*window, "seam-proximity.hairline");
    std::shared_ptr<SplitContainer> horizontal = find_split(*window, "seam-proximity.horizontal");
    std::shared_ptr<SplitContainer> disabled = find_split(*window, "seam-proximity.disabled");
    std::shared_ptr<SplitContainer> collapsed = find_split(*window, "seam-proximity.collapsed");
    require(vertical && physical && horizontal && disabled && collapsed &&
                (*horizontal).orientation() == Orientation::horizontal &&
                (*disabled).splitter_seam_snapshot().state ==
                    SplitSeamState::disabled &&
                (*collapsed).splitter_seam_snapshot().state ==
                    SplitSeamState::collapsed,
            "lab must retain vertical, horizontal, disabled, and collapsed physical seam specimens");
}

void test_trace_scale_state_input_and_inspection() {
    std::unique_ptr<Window> window = lab::make_seam_proximity_lab();
    std::shared_ptr<SplitContainer> hairline = find_split(*window, "seam-proximity.hairline");
    require(hairline != nullptr,
            "live hairline specimen must be discoverable by stable identity");
    NullPainter painter;
    static_cast<void>((*window).paint(painter, {0.0, 0.0, 1360.0, 760.0}));
    VisualInspectionSnapshot snapshot = (*window).visual_inspection_snapshot();
    const VisualControlInspection* grip =
        snapshot.find("seam-proximity.hairline.splitter");
    const VisualPaintOperationSnapshot* fill = grip ? first_fill(*grip) : nullptr;
    // Bounded: the band's face (1; no edge below 2 px), the tab's clip, face, gradient,
    // restore and edge (5), and its arrow mark's four rows (4).
    require(grip && fill && near((*fill).rect.width, 1.0) &&
                (*grip).display_chunk_current &&
                (*grip).total_display_operations <= 11U,
            "one-times inspector trace must expose one logical hairline fill in a bounded current chunk");

    require((*window).perform_semantic_action(
                "seam-proximity.device-scale", SemanticAction::press) &&
                near((*window).scale(), 2.0),
            "device-scale selector must use the ordinary semantic command path");
    (*window).perform_layout();
    static_cast<void>((*window).paint(painter, {0.0, 0.0, 1360.0, 760.0}));
    snapshot = (*window).visual_inspection_snapshot();
    grip = snapshot.find("seam-proximity.hairline.splitter");
    fill = grip ? first_fill(*grip) : nullptr;
    const SplitSeamSnapshot physical = (*hairline).splitter_seam_snapshot();
    require(grip && fill && near((*fill).rect.width, 0.5) &&
                near(physical.visible_device_pixels, 1.0) &&
                physical.hit_bounds.width >=
                    physical.geometry.minimum_hit_target,
            "two-times trace must expose a half-logical, one-device-pixel seam without shrinking its hit target");

    const Control::Ptr scale_button = find_control(
        (*window).root(), "seam-proximity.device-scale");
    require(scale_button && (*window).request_focus(scale_button),
            "proximity focus fixture requires an independent command focus");
    const Rect hit = (*(*hairline).splitter_control()).absolute_bounds();
    PointerEvent near_pointer;
    near_pointer.action = PointerAction::move;
    near_pointer.position = {hit.x + 0.25, hit.y + 12.0};
    near_pointer.pointer_id = 4U;
    static_cast<void>((*window).dispatch_pointer(near_pointer));
    require((*hairline).splitter_seam_snapshot().state == SplitSeamState::near &&
                (*window).focused_control() == scale_button,
            "near state must not steal keyboard focus from adjacent commands");
    PointerEvent hot_pointer = near_pointer;
    hot_pointer.position = {hit.x + hit.width * 0.5,
                            hit.y + hit.height * 0.5};
    static_cast<void>((*window).dispatch_pointer(hot_pointer));
    require((*hairline).splitter_seam_snapshot().state == SplitSeamState::hot &&
                (*window).focused_control() == scale_button,
            "centered proximity must become hot without changing focus");

    PointerEvent down = near_pointer;
    down.action = PointerAction::down;
    down.button = PointerButton::primary;
    const double start = (*hairline).splitter_distance();
    require((*window).dispatch_pointer(down) &&
                (*window).captured_control() == (*hairline).splitter_control() &&
                (*hairline).splitter_seam_snapshot().state ==
                    SplitSeamState::dragging,
            "wide target press must enter captured drag state");
    PointerEvent move = down;
    move.action = PointerAction::move;
    move.button = PointerButton::none;
    move.position.x += 26.0;
    static_cast<void>((*window).dispatch_pointer(move));
    require((*window).dispatch_key({KeyAction::down, PhysicalKey::escape}) &&
                !(*window).captured_control() &&
                near((*hairline).splitter_distance(), start),
            "Escape must cancel and restore a live laboratory drag");

    require((*window).perform_semantic_action(
                "seam-proximity.hairline", SemanticAction::increment) &&
                (*hairline).splitter_distance() > start &&
                (*window).perform_semantic_action(
                    "seam-proximity.hairline", SemanticAction::set_value,
                    "180") && near((*hairline).splitter_distance(), 180.0),
            "lab seam must publish adjustable semantic value actions");
    require((*window).perform_semantic_action(
                "seam-proximity.text-scale", SemanticAction::press) &&
                near((*window).presentation_settings().text_scale, 1.25),
            "text-scale state must be exercisable through the lab command path");

    (*window).resize({1200.0, 720.0});
    (*window).perform_layout();
    snapshot = (*window).visual_inspection_snapshot();
    const VisualControlInspection* inspector =
        snapshot.find("seam-proximity.inspector");
    require(inspector && (*inspector).layout.arranged_bounds.x == 850.0 &&
                (*inspector).layout.arranged_bounds.width == 328.0 &&
                (*inspector).layout.arranged_bounds.height == 610.0 &&
                (*inspector).layout.arranged_bounds.x +
                    (*inspector).layout.arranged_bounds.width <= 1200.0,
            "minimum native resize must keep the inspector in bounds without moving the specimen geometry");
}

} // namespace

int main() {
    try {
        test_named_specimens_and_atomic_rejection();
        test_trace_scale_state_input_and_inspection();
        std::cout << "gui_forms_seam_proximity_lab_tests: all tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_seam_proximity_lab_tests: " << error.what()
                  << '\n';
        return EXIT_FAILURE;
    }
}
