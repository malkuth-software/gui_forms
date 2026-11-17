#include "support.hpp"
#include "gui_forms/binding/value/binding_value.hpp"
#include "gui_forms/composites/composite_control/composite_control.hpp"
#include "gui_forms/controls/label.hpp"

#include <optional>
#include <string>
#include <variant>

namespace gallery_pages_tests {

namespace {

bool checked_of(const CompositeControl& pill) {
    const std::optional<BindingValue> value = pill.property_value("checked");
    const bool* const checked = value ? std::get_if<bool>(&*value) : nullptr;
    const bool on = checked != nullptr && *checked;
    return on;
}

} // namespace

// The composite-controls page (cp34): the generated TogglePill and
// CardWithAction placed by a form; the form's values reach the
// composites' registered properties, the card's slot holds the form's
// children, a pill's toggle raises checked-changed to the page, and the
// card's action raises action-invoked.
void test_composite_controls(gui_forms::Window& window) {
    select_page(window, "composite-controls");
    require(window.find("gallery.composite-controls.heading") != nullptr,
            "composite-controls page shows its heading");
    const std::shared_ptr<CompositeControl> wifi =
        std::dynamic_pointer_cast<CompositeControl>(window.find("gallery.composites.wifi"));
    const std::shared_ptr<CompositeControl> bluetooth =
        std::dynamic_pointer_cast<CompositeControl>(window.find("gallery.composites.bluetooth"));
    const std::shared_ptr<CompositeControl> card =
        std::dynamic_pointer_cast<CompositeControl>(window.find("gallery.composites.storage"));
    const std::shared_ptr<Label> status =
        std::dynamic_pointer_cast<Label>(window.find("gallery.composites.pills.status"));
    const std::shared_ptr<Label> invoked =
        std::dynamic_pointer_cast<Label>(window.find("gallery.composites.cards.invoked"));
    require(wifi && bluetooth && card && status && invoked, "the page shows its composites and status lines");
    if (!wifi || !bluetooth || !card || !status || !invoked) return;
    require((*wifi).type_name() == "toggle_pill" && (*card).type_name() == "card_with_action",
            "the composites are the generated types");
    require(checked_of(*wifi) && !checked_of(*bluetooth), "the form's checked values reach the pills");
    require((*status).text.read() == "Wi-Fi on · Bluetooth off · Airplane mode off",
            "the status line starts from the pills' values");

    const Control::Ptr content = (*card).slot();
    const Control::Ptr used = window.find("gallery.composites.storage.used");
    const Control::Ptr offload = window.find("gallery.composites.storage.offload");
    require(content && used && offload && (*used).parent() == content && (*offload).parent() == content,
            "the form's children are in the card's slot");

    require((*bluetooth).perform("toggle") && checked_of(*bluetooth), "the toggle command checks Bluetooth");
    require((*status).text.read() == "Wi-Fi on · Bluetooth on · Airplane mode off",
            "checked-changed reaches the page");
    const std::shared_ptr<Label> last =
        std::dynamic_pointer_cast<Label>(window.find("gallery.composites.pills.last"));
    require(last && (*last).text.read() == "Last event: checked-changed (true)", "the payload reaches the page");

    window.perform_layout();
    const Control::Ptr action = (*card).child("action");
    require(action != nullptr, "the card has its action button");
    if (action) click(window, action);
    require((*invoked).text.read() == "Manage invoked 1 time.", "the card's action raises action-invoked");
    static_cast<void>((*bluetooth).perform("toggle"));
}

} // namespace gallery_pages_tests
