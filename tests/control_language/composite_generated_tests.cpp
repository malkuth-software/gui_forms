// Composite controls generated at build time (cp34, phase 2;
// gui_forms_add_control): the worked examples' headers, written by the form
// compiler's control mode, give classes over CompositeControl that are made
// like any control, share one definition, and read and write their
// properties through typed functions.

#include "card_with_action_control.hpp"
#include "toggle_pill_control.hpp"

#include "gui_forms/window/window.hpp"

#include <iostream>
#include <memory>
#include <string>
#include <string_view>

namespace {

using namespace gui_forms;

int failures = 0;

void require(const bool condition, const std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void generated_toggle_pill() {
    const std::shared_ptr<composite_generated::TogglePill> pill =
        make_control<composite_generated::TogglePill>(StableId(std::string("pill")));
    const std::shared_ptr<composite_generated::TogglePill> other =
        make_control<composite_generated::TogglePill>(StableId(std::string("other")));
    require(&(*pill).definition() == &(*other).definition(), "the instances share one definition");
    require((*pill).type_name() == "toggle_pill", "the type is toggle_pill");
    require(composite_generated::TogglePill::child_types.empty(), "the pill names no child types");
    require(!(*pill).read_checked(), "checked starts false");
    (*pill).write_checked(true);
    require((*pill).read_checked(), "write_checked writes the registered property");
    (*pill).write_text("Wi-Fi");
    require((*pill).read_text() == "Wi-Fi", "text reads back");
    require((*pill).perform("toggle") && !(*pill).read_checked(), "the toggle command runs");
}

void generated_card() {
    const std::shared_ptr<composite_generated::CardWithAction> card =
        make_control<composite_generated::CardWithAction>(StableId(std::string("card")));
    (*card).set_requested_bounds({0.0, 0.0, 240.0, 160.0});
    Window window(card, {240.0, 160.0});
    window.perform_layout();
    require(composite_generated::CardWithAction::child_types.size() == 3U,
            "the card names its three child types (flex_panel, label, button)");
    require((*card).child("title") != nullptr && (*card).slot() != nullptr, "the children are made");
    (*card).write_title("Storage");
    require((*card).read_title() == "Storage", "the title reads back");
}

} // namespace

int main() {
    generated_toggle_pill();
    generated_card();
    if (failures != 0) {
        std::cerr << failures << " generated composite check(s) failed\n";
        return 1;
    }
    std::cout << "gui_forms_composite_generated_tests: all checks passed\n";
    return 0;
}
