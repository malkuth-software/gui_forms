#include "custom_chrome_lab.hpp"

#include "gui_forms/control/control/control.hpp"
#include "gui_forms/controls/label.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/host/types/host_types.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/visual_inspection/types/visual_inspection_types.hpp"
#include "gui_forms/window/window.hpp"
#include "headless_host.hpp"

#include <array>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

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

} // namespace

int main() {
    try {
        std::unique_ptr<Window> window =
            gui_forms::custom_chrome_lab::make_custom_chrome_lab();
        const std::shared_ptr<Control> initial_title =
            (*window).find("custom-chrome.title-drag");
        require(initial_title && (*initial_title).authored_surface_material() &&
                    (*(*initial_title).authored_surface_material()).fills.front().spread ==
                        GradientSpreadMode::pad,
                "title material must retain its authored pad spread before paint");
        NullPainter painter;
        static_cast<void>((*window).paint(
            painter, {0.0, 0.0, 1120.0, 580.0}));

        VisualInspectionSnapshot snapshot =
            (*window).visual_inspection_snapshot();
        const VisualControlInspection* title =
            snapshot.find("custom-chrome.title-drag");
        require(title && (*title).layout.absolute_bounds ==
                             Rect{0.0, 0.0, 1120.0, 40.0},
                "title identity must begin at client y=0 and span the window");
        require((*title).authored_material &&
                    (*(*title).authored_material).fills.size() == 4U &&
                    (*(*title).authored_material).keylines.size() == 2U &&
                    (*title).display_chunk_current,
                "active title must expose the complete retained material chunk");

        const std::array<std::string, 1> drag_regions{
            "custom-chrome.title-drag"};
        const WindowChromeHit open_title = resolve_window_chrome_hit(
            *window, {700.0, 20.0}, drag_regions);
        require(open_title.begins_native_drag() &&
                    open_title.target_stable_id ==
                        "custom-chrome.title-drag",
                "open title backdrop must resolve to native drag");
        const WindowChromeHit transparent_mark = resolve_window_chrome_hit(
            *window, {100.0, 20.0}, drag_regions);
        require(transparent_mark.begins_native_drag() &&
                    transparent_mark.target_stable_id ==
                        "custom-chrome.title-drag",
                "transparent decorative child must expose the drag backdrop");
        const WindowChromeHit interactive = resolve_window_chrome_hit(
            *window, {1000.0, 20.0}, drag_regions);
        require(!interactive.begins_native_drag() &&
                    interactive.target_stable_id ==
                        "custom-chrome.interactive-exclusion",
                "interactive title descendant must stay client input");
        const WindowChromeHit body = resolve_window_chrome_hit(
            *window, {100.0, 120.0}, drag_regions);
        require(!body.begins_native_drag(),
                "ordinary client content must never resolve as caption drag");
        require(validate_window_chrome_drag_regions(*window, drag_regions)
                    .accepted(),
                "authored resolved drag region must pass conformance");
        const std::array<std::string, 2> resolved_regions{
            "custom-chrome.title-drag", "custom-chrome.paper"};
        require(validate_window_chrome_drag_regions(*window, resolved_regions)
                    .accepted(),
                "multiple distinct resolved drag-region IDs must be admitted");
        const std::array<std::string, 1> empty_region{""};
        const std::array<std::string, 2> duplicate_regions{
            "custom-chrome.title-drag", "custom-chrome.title-drag"};
        const std::array<std::string, 1> unresolved_region{
            "custom-chrome.missing"};
        require(validate_window_chrome_drag_regions(*window, empty_region).error ==
                    WindowChromeRegionError::empty_id &&
                    validate_window_chrome_drag_regions(*window, duplicate_regions)
                            .error == WindowChromeRegionError::duplicate_id &&
                    validate_window_chrome_drag_regions(*window, unresolved_region)
                            .error == WindowChromeRegionError::unresolved_id,
                "empty, duplicate, and unresolved drag IDs must fail distinctly");

        host::HeadlessHost host(*window);
        require(host.dispatch(HostAttachEvent{{1120.0, 580.0}, 1.0}, 1)
                    .accepted() &&
                    host.dispatch(HostActivationEvent{false}, 2).accepted() &&
                    !(*window).active(),
                "host activation must update portable Window visual state");
        snapshot = (*window).visual_inspection_snapshot();
        title = snapshot.find("custom-chrome.title-drag");
        require(title && (*title).authored_material &&
                    (*(*title).authored_material).fills.size() == 2U,
                "inactive title must retain a distinct restrained material");
        require((*(*window).find("custom-chrome.interactive-exclusion"))
                    .effectively_enabled(),
                "window deactivation must not disable retained content");
        require(host.dispatch(HostActivationEvent{true}, 3).accepted() &&
                    (*window).active(),
                "host reactivation must restore the active visual state");

        const HostDispatchResult pointer_down = host.dispatch(
            PointerEvent{PointerAction::down, PointerButton::primary,
                         {1000.0, 20.0}},
            4);
        const HostDispatchResult pointer_up = host.dispatch(
            PointerEvent{PointerAction::up, PointerButton::primary,
                         {1000.0, 20.0}},
            5);
        const std::shared_ptr<Label> click_readout =
            std::dynamic_pointer_cast<Label>(
                (*window).find("custom-chrome.click-readout"));
        require(pointer_down.accepted() && pointer_up.accepted() &&
                    click_readout &&
                    (*click_readout).text.read() ==
                        "Interactive exclusion received 1 click",
                "interactive title descendant must receive ordinary pointer dispatch");

        (*window).set_scale(2.0);
        (*window).perform_layout();
        require((*(*window).find("custom-chrome.title-drag"))
                    .absolute_bounds() == Rect{0.0, 0.0, 1120.0, 40.0},
                "device scale must not perturb logical title geometry");
        require((*window).semantic_snapshot().to_json().find(
                    "custom-chrome.interactive-exclusion") !=
                    std::string::npos,
                "interactive exclusion must remain in the semantic tree");

        std::cout << "gui_forms_custom_chrome_lab_tests: all tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_custom_chrome_lab_tests: " << error.what()
                  << '\n';
        return EXIT_FAILURE;
    }
}
