#include "gui_forms/gui_forms.hpp"

#include <cmath>
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

bool close(double left, double right) {
    return std::abs(left - right) < 0.0001;
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

class ClippingPanel final : public Panel {
public:
    explicit ClippingPanel(StableId id) : Panel(std::move(id)) {}

protected:
    [[nodiscard]] Rect child_viewport_rectangle() const noexcept override {
        return {12.0, 8.0, 96.0, 64.0};
    }
};

SurfaceMaterial inspected_material() {
    SurfaceMaterial material;
    material.fills = {
        MaterialFillLayer::linear_css_angle(90.0, {
            {0.0, Color::rgba(20, 80, 150)},
            {1.0, Color::rgba(220, 90, 150)},
        }),
        MaterialFillLayer::radial({0.2, 0.2}, {0.5, 0.8}, {
            {0.0, Color::rgba(255, 255, 255, 90)},
            {1.0, Color::rgba(255, 255, 255, 0)},
        }),
    };
    material.shadows = {
        {{0.0, 3.0}, 6.0, 1.0, Color::rgba(0, 0, 0, 80), false},
    };
    material.border = MaterialBorder{Color::rgba(10, 30, 60), 1.0};
    material.corner_radius = 8.0;
    return material;
}

struct Fixture final {
    Fixture() {
        (*root).set_background(Color::rgba(235, 239, 242));
        (*clip).set_requested_bounds({30.0, 20.0, 120.0, 80.0});
        (*clip).set_authored_surface_material(inspected_material());
        (*root).add_child(clip);

        (*text).set_requested_bounds({-10.0, 12.0, 110.0, 32.0});
        (*text).font = FontSpec{FontRole::content, 11.0, 600, true, 0.25};
        (*clip).add_child(text);

        (*button).set_requested_bounds({8.0, 46.0, 90.0, 26.0});
        (*button).set_visual_style(ButtonVisualStyle::accent);
        (*clip).add_child(button);

        window = std::make_unique<Window>(root, Size{240.0, 160.0});
        (*window).set_text_scale(1.25);
        (*window).set_presentation_settings(
            {1.25, true, true, false});
        (*window).perform_layout();
        static_cast<void>((*window).paint(painter, {0.0, 0.0, 240.0, 160.0}));
    }

    std::shared_ptr<Panel> root =
        make_control<Panel>(StableId("inspection.root"));
    std::shared_ptr<ClippingPanel> clip =
        make_control<ClippingPanel>(StableId("inspection.clip"));
    std::shared_ptr<Label> text =
        make_control<Label>(StableId("inspection.text"), "Private witness text");
    std::shared_ptr<Button> button =
        make_control<Button>(StableId("inspection.button"), "Press");
    std::unique_ptr<Window> window;
    NullPainter painter;
};

void test_geometry_clip_material_font_and_redaction() {
    Fixture fixture;
    VisualInspectionSnapshot snapshot =
        (*fixture.window).visual_inspection_snapshot();
    require(snapshot.total_controls == 4U && snapshot.controls.size() == 4U &&
                !snapshot.controls_truncated,
            "visual snapshot must retain every control in deterministic tree order");
    require(close(snapshot.text_scale, 1.25) && snapshot.high_contrast &&
                snapshot.reduced_motion && !snapshot.sound_enabled,
            "visual snapshot must expose every presentation input that can change appearance");

    const VisualControlInspection* clip = snapshot.find("inspection.clip");
    const VisualControlInspection* text = snapshot.find("inspection.text");
    require(clip != nullptr && text != nullptr,
            "visual snapshot must resolve controls by stable identity");
    require((*clip).layout.absolute_bounds == Rect{30.0, 20.0, 120.0, 80.0} &&
                (*clip).layout.child_viewport == Rect{12.0, 8.0, 96.0, 64.0},
            "inspection must expose committed absolute bounds and child viewport");
    require((*text).layout.absolute_bounds == Rect{20.0, 32.0, 110.0, 32.0} &&
                (*text).layout.effective_clip == Rect{42.0, 32.0, 88.0, 32.0} &&
                (*text).layout.clipped_by_ancestor &&
                !(*text).layout.fully_clipped &&
                (*text).layout.ancestor_clip_count == 2U,
            "inspection must calculate the exact effective ancestor clip");
    require((*clip).authored_material &&
                (*(*clip).authored_material).fills.size() == 2U &&
                (*(*clip).authored_material).shadows.size() == 1U,
            "inspection must retain exact authored material layer order");
    require((*clip).display_chunk_current && (*text).display_chunk_current,
            "painted clean controls must report current committed chunks");

    bool found_resolved_font = false;
    for (const VisualPaintOperationSnapshot& operation : (*text).display_operations) {
        if (operation.operation != VisualPaintOperation::draw_text) continue;
        found_resolved_font = close(operation.font.size, 13.75) &&
            operation.font.role == FontRole::content &&
            operation.font.weight == 600U && operation.font.italic &&
            close(operation.font.letter_spacing, 0.3125) &&
            operation.text.empty() && operation.text_byte_count == 20U;
    }
    require(found_resolved_font,
            "inspection must expose the committed effective font while redacting text by default");

    VisualInspectionOptions unredacted;
    unredacted.include_text = true;
    const VisualInspectionSnapshot unredacted_snapshot =
        (*fixture.window).visual_inspection_snapshot(unredacted);
    const VisualControlInspection* unredacted_text =
        unredacted_snapshot.find("inspection.text");
    require(unredacted_text != nullptr,
            "unredacted visual snapshot must retain the requested control");
    bool found_text = false;
    for (const VisualPaintOperationSnapshot& operation :
         (*unredacted_text).display_operations) {
        found_text = found_text || operation.text == "Private witness text";
    }
    require(found_text,
            "explicit include_text must disclose committed draw text for local design QA");
}

void test_state_damage_staleness_bounds_and_json() {
    Fixture fixture;
    require((*fixture.window).request_focus(fixture.button),
            "fixture button must accept focus");
    PointerEvent move;
    move.action = PointerAction::move;
    move.position = {62.0, 76.0};
    move.pointer_id = 7U;
    static_cast<void>((*fixture.window).dispatch_pointer(move));
    PointerEvent down;
    down.action = PointerAction::down;
    down.button = PointerButton::primary;
    down.position = {62.0, 76.0};
    down.pointer_id = 7U;
    static_cast<void>((*fixture.window).dispatch_pointer(down));

    VisualInspectionSnapshot active =
        (*fixture.window).visual_inspection_snapshot();
    const VisualControlInspection* button = active.find("inspection.button");
    require(button && (*button).state.focused && (*button).state.hovered &&
                (*button).state.pressed && (*button).state.pointer_captured,
            "inspection must expose focused, hovered, pressed, and capture state");

    (*fixture.text).font = FontSpec{FontRole::content, 16.0, 400, false};
    const VisualInspectionSnapshot stale =
        (*fixture.window).visual_inspection_snapshot();
    const VisualControlInspection* stale_text = stale.find("inspection.text");
    require(stale_text && (*stale_text).display_chunk &&
                !(*stale_text).display_chunk_current &&
                !stale.pending_damage[paint_plane_index(PaintPlane::control)].empty(),
            "inspection must report a stale chunk and non-destructively retain pending damage");
    require((*fixture.window).needs_frame(),
            "taking a visual snapshot must not consume pending paint work");

    VisualInspectionOptions bounded;
    bounded.maximum_controls = 2U;
    bounded.maximum_operations_per_control = 1U;
    const VisualInspectionSnapshot truncated =
        (*fixture.window).visual_inspection_snapshot(bounded);
    require(truncated.total_controls == 4U && truncated.controls.size() == 2U &&
                truncated.controls_truncated,
            "control capture must count the full tree while bounding retained records");
    bool operation_bound_observed = false;
    for (const VisualControlInspection& control : truncated.controls) {
        operation_bound_observed = operation_bound_observed ||
            (control.total_display_operations > 1U &&
             control.display_operations.size() == 1U &&
             control.display_operations_truncated);
    }
    require(operation_bound_observed,
            "display operation capture must expose exact truncation");

    const std::string json = active.to_json();
    require(json.find("\"stable_id\":\"inspection.text\"") !=
                std::string::npos &&
                json.find("Private witness text") == std::string::npos &&
                json.find("\"effective_clip\":[42,32,88,32]") !=
                    std::string::npos &&
                json.find("\"high_contrast\":true") != std::string::npos &&
                json.find("\"image_sampling\":") != std::string::npos &&
                json.find("\"display_chunk_current\":true") !=
                    std::string::npos,
            "visual inspection JSON must be deterministic, useful, and redacted by default");

    VisualInspectionOptions invalid;
    invalid.maximum_controls = 0U;
    bool rejected = false;
    try {
        static_cast<void>((*fixture.window).visual_inspection_snapshot(invalid));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "visual inspection must reject unbounded/empty capture limits");
}

} // namespace

int main() {
    try {
        test_geometry_clip_material_font_and_redaction();
        test_state_damage_staleness_bounds_and_json();
        std::cout << "gui_forms_visual_inspection_tests: all tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_visual_inspection_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
