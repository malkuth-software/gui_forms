// The browser host's plain rules: the DOM values it turns into the window
// manager's records (src/host/web/input/web_input_rules.hpp), the present
// record (src/host/web/page/web_present.hpp) and the dialogs' accept lists
// and save names (src/host/web/application/web_dialog_rules.cpp). No
// Emscripten, so tested on every platform.

#include "../src/host/web/application/web_surface_host.hpp"
#include "../src/host/web/input/web_input_rules.hpp"
#include "../src/host/web/page/web_present.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace gui_forms;
using namespace gui_forms::host;
using namespace gui_forms::host::web_detail;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void test_key_codes_and_modifiers() {
    require(web_physical_key("KeyA") == PhysicalKey::a, "KeyA");
    require(web_physical_key("Enter") == PhysicalKey::enter, "Enter");
    require(web_physical_key("ArrowLeft") == PhysicalKey::left, "ArrowLeft");
    require(web_physical_key("ContextMenu") == PhysicalKey::context_menu, "ContextMenu");
    require(web_physical_key("MetaLeft") == PhysicalKey::left_meta && web_physical_key("OSLeft") == PhysicalKey::left_meta,
            "both names of the left meta key");
    require(web_physical_key("Unidentified") == 0U, "an unknown code maps to nothing");
    require(web_modifiers(1U | 2U) == (Modifier::shift | Modifier::control), "shift and control");
    require(web_modifiers(0xF0U) == Modifier::none, "unknown bits are dropped");
    require(web_pointer_button(2) == PointerButton::secondary && web_pointer_button(3) == PointerButton::none,
            "DOM buttons");
}

void test_actions_wheels_and_outcomes() {
    require(web_pointer_action(1) == std::optional<surface_detail::SurfacePointerAction>(
                                         surface_detail::SurfacePointerAction::down),
            "1 is a press");
    require(!web_pointer_action(6).has_value() && !web_pointer_action(-1).has_value(), "unknown actions drop out");
    const Point delta = web_wheel_delta(-1.0, 120.0);
    require(delta.x == 1.0 && delta.y == -120.0, "the DOM's wheel deltas are negated");
    require(web_wheel_delta(std::numeric_limits<double>::infinity(), 1.0).x == 0.0, "a non-finite delta is 0");
    require(web_wheel_unit(1) == surface_detail::SurfaceWheelUnit::lines &&
                web_wheel_unit(2) == surface_detail::SurfaceWheelUnit::pages &&
                web_wheel_unit(9) == surface_detail::SurfaceWheelUnit::pixels,
            "deltaMode");
    require(web_dialog_outcome(1) == surface_detail::SurfaceDialogOutcome::accepted &&
                web_dialog_outcome(0) == surface_detail::SurfaceDialogOutcome::cancelled &&
                web_dialog_outcome(7) == surface_detail::SurfaceDialogOutcome::failed,
            "dialog outcomes");
}

void test_present_rects() {
    WebPresentFrame frame{};
    std::vector<surface_detail::SurfaceDeviceRect> damage(WebPresentFrame::maximum_rects + 3U);
    damage[0] = surface_detail::SurfaceDeviceRect{1, 2, 3, 4};
    require(fill_web_present_rects(frame, damage) == WebPresentFrame::maximum_rects, "at most the record's room");
    require(frame.rect_count == WebPresentFrame::maximum_rects, "the count is written");
    require(frame.rects[0] == 1 && frame.rects[1] == 2 && frame.rects[2] == 3 && frame.rects[3] == 4,
            "left, top, right, bottom");
}

void test_dialog_rules() {
    std::vector<HostFileDialogFilter> filters(1U);
    filters[0].extensions = {"*.png", ".jpg", "gif"};
    require(web_accept_list(filters) == ".png,.jpg,.gif", "an accept list");
    filters[0].extensions.push_back("*.*");
    require(web_accept_list(filters).empty(), "every file");
    HostSaveFileDialogRequest save{};
    save.suggested_name = "../notes/plan";
    save.default_extension = "txt";
    require(web_save_name(save) == "plan.txt", "the base name with the default extension");
    save.suggested_name = "";
    require(web_save_name(save) == "untitled.txt", "untitled when empty");
}

} // namespace

int main() {
    try {
        test_key_codes_and_modifiers();
        test_actions_wheels_and_outcomes();
        test_present_rects();
        test_dialog_rules();
    } catch (const std::exception& error) {
        std::cerr << "web host rules tests failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "web host rules tests passed\n";
    return EXIT_SUCCESS;
}
