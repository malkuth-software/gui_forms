// Composite controls generated at build time (cp34, phase 2;
// gui_forms_add_control): the worked examples' headers, written by the form
// compiler's control mode, give classes over CompositeControl that are made
// like any control, share one definition, and read and write their
// properties through typed functions; and a form naming them
// (gui_forms_add_form COMPOSITES) whose children fill the card's slot.

#include "card_with_action_control.hpp"
#include "composite_panel_form.hpp"
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

void generated_form() {
    const std::shared_ptr<composite_generated::CompositePanel> panel = composite_generated::CompositePanel::make();
    require((*panel).children().size() == 2U, "the form's root holds the card and the pill");
    const std::shared_ptr<composite_generated::CardWithAction> card = (*panel).panel_card;
    const std::shared_ptr<composite_generated::TogglePill> pill = (*panel).panel_pill;
    require(card != nullptr && pill != nullptr, "the composites are the form's members, typed");
    if (card == nullptr || pill == nullptr) return;
    require((*card).read_title() == "Storage", "the form sets the card's title");
    require((*pill).read_checked() && (*pill).read_text() == "Wi-Fi", "the form sets the pill's properties");
    const Control::Ptr content = (*card).slot();
    require(content != nullptr && (*content).children().size() == 1U &&
                (*content).children()[0] == (*panel).panel_note,
            "the form's child goes into the card's slot");
    require((*(*panel).panel_note).parent() == content, "the label's parent is the slot");
}

} // namespace

int main() {
    generated_toggle_pill();
    generated_card();
    generated_form();
    if (failures != 0) {
        std::cerr << failures << " generated composite check(s) failed\n";
        return 1;
    }
    std::cout << "gui_forms_composite_generated_tests: all checks passed\n";
    return 0;
}
