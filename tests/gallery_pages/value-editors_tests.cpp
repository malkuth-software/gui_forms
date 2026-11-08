#include "page_tests.hpp"

namespace gallery_pages_tests {

void test_value_editors(gui_forms::Window& window) {
    select_page(window, "value-editors");
    require(window.find("gallery.value-editors.heading") != nullptr,
            "value-editors page shows its heading");
    const std::shared_ptr<gui_forms::ColorValueEditor> color =
        std::dynamic_pointer_cast<ColorValueEditor>(window.find("gallery.value-editors.color"));
    const std::shared_ptr<gui_forms::FlagsValueEditor> flags =
        std::dynamic_pointer_cast<FlagsValueEditor>(window.find("gallery.value-editors.flags"));
    const std::shared_ptr<gui_forms::Panel> preview =
        std::dynamic_pointer_cast<Panel>(window.find("gallery.value-editors.preview"));
    const std::shared_ptr<gui_forms::PropertyList> properties =
        std::dynamic_pointer_cast<PropertyList>(window.find("gallery.value-editors.properties"));
    const std::shared_ptr<gui_forms::InstrumentRack> rack =
        std::dynamic_pointer_cast<InstrumentRack>(window.find("gallery.value-editors.rack"));
    const std::shared_ptr<gui_forms::CommandBar> binding_commands =
        std::dynamic_pointer_cast<CommandBar>(
            window.find("gallery.value-editors.binding-commands"));
    const std::shared_ptr<gui_forms::Label> binding_status =
        std::dynamic_pointer_cast<Label>(window.find("gallery.value-editors.binding-status"));
    require(color && flags && preview && properties && rack && binding_commands &&
                binding_status,
            "value-editors page must retain its public editor, list, rack and binding controls");
    const std::string semantics = window.semantic_snapshot().to_json();
    require(semantics.find("\"role\":\"property_grid\"") != std::string::npos &&
                semantics.find("\"role\":\"combo_box\"") != std::string::npos &&
                semantics.find("gallery.value-editors.properties.name") != std::string::npos,
            "value-editors page must publish PropertyList rows and the flags editor semantics");

    const std::shared_ptr<gui_forms::TextBox> rack_name = std::dynamic_pointer_cast<TextBox>(
        (*rack).field_editor("gallery.value-editors.rack.lamp", "name"));
    const std::shared_ptr<gui_forms::ComboBox> rack_channel = std::dynamic_pointer_cast<ComboBox>(
        (*rack).field_editor("gallery.value-editors.rack.lamp", "channel"));
    const std::shared_ptr<gui_forms::TextBox> rack_tint = std::dynamic_pointer_cast<TextBox>(
        (*rack).field_editor("gallery.value-editors.rack.lamp", "tint"));
    require(rack_name && rack_channel && rack_tint && (*rack_name).text.read() == "Signal lamp" &&
                (*rack_channel).selected_index() == std::optional<std::size_t>{1U} &&
                (*rack_tint).text.read() == "#2774B8FF" &&
                (*properties).value("gallery.value-editors.properties.tint") == "#2774B8FF",
            "the rack module and the PropertyList must start from the same record");

    // ColorValueEditor commit through its ordinary TextBox projects everywhere.
    require(window.request_focus((*color).editor()), "the color editor's TextBox must focus");
    (*(*color).editor()).select_all();
    require(window.dispatch_text({"#2A6FB4FF"}) &&
                window.dispatch_key({KeyAction::down, PhysicalKey::enter}) &&
                (*color).value() == Color::rgba(42, 111, 180, 255) &&
                (*preview).background() == Color::rgba(42, 111, 180, 255) &&
                (*properties).value("gallery.value-editors.properties.tint") == "#2A6FB4FF" &&
                (*rack_tint).text.read() == "#2A6FB4FF",
            "a typed color commit must reach the preview, the list row and the rack field");

    // FlagsValueEditor: the overlay is a real CheckedListBox; a check commits.
    (*flags).set_dropped_down(true);
    const std::shared_ptr<gui_forms::CheckedListBox> bits = std::dynamic_pointer_cast<CheckedListBox>(
        window.find("gallery.value-editors.flags.overlay.list"));
    require(bits && (*bits).items().size() == 5U && (*bits).item_checked(1U) &&
                (*bits).item_checked(2U) && !(*bits).item_checked(3U),
            "the flags overlay must expose every edge bit with synchronized checks");
    (*bits).set_item_checked(3U, true);
    require((*flags).value().name == "Top, Left, Right" && (*flags).value().value == 7 &&
                (*properties).value("gallery.value-editors.properties.edges") == "Top, Left, Right",
            "checking a flags bit must commit the typed value and project it into the list");
    (*flags).set_dropped_down(false);
    require(!window.find("gallery.value-editors.flags.overlay.list"),
            "closing the flags editor must unregister its overlay subtree");

    // PropertyList text commit reaches the rack; Reset restores the fixture.
    const std::shared_ptr<gui_forms::TextBox> name = std::dynamic_pointer_cast<TextBox>(
        (*properties).editor("gallery.value-editors.properties.name"));
    const std::shared_ptr<gui_forms::Button> reset =
        (*properties).reset_button("gallery.value-editors.properties.name");
    require(name && reset && !(*reset).enabled.read(), "the name row must own its editor and reset button");
    require(window.request_focus(name), "the PropertyList name editor must focus");
    (*name).select_all();
    require(window.dispatch_text({"Lamp B"}) &&
                window.dispatch_key({KeyAction::down, PhysicalKey::enter}) &&
                (*properties).value("gallery.value-editors.properties.name") == "Lamp B" &&
                (*rack_name).text.read() == "Lamp B" && (*reset).enabled.read(),
            "a PropertyList name commit must reach the rack field and enable Reset");
    click(window, reset);
    require((*properties).value("gallery.value-editors.properties.name") == "Signal lamp" &&
                (*rack_name).text.read() == "Signal lamp" && !(*reset).enabled.read(),
            "Reset must restore the fixture name in both views");

    // Rack commits reach the list; an unparseable tint is reported, not applied.
    (*rack_channel).set_selected_index(2U);
    require((*properties).value("gallery.value-editors.properties.channel") == "WFM",
            "a rack choice commit must project into the PropertyList row");
    require(window.request_focus(rack_tint), "the rack tint field must focus");
    (*rack_tint).select_all();
    require(window.dispatch_text({"teal"}) &&
                window.dispatch_key({KeyAction::down, PhysicalKey::enter}) &&
                (*rack).modules()[0].fields[2].validation_message == "Use #RRGGBB or #RRGGBBAA" &&
                (*color).value() == Color::rgba(42, 111, 180, 255),
            "an invalid rack tint must publish validation and leave the color unchanged");
    (*rack_tint).select_all();
    require(window.dispatch_text({"#102030FF"}) &&
                window.dispatch_key({KeyAction::down, PhysicalKey::enter}) &&
                (*color).value() == Color::rgba(16, 32, 48, 255) &&
                (*rack).modules()[0].fields[2].validation_message.empty() &&
                (*preview).background() == Color::rgba(16, 32, 48, 255),
            "a valid rack tint must re-enter through the ColorValueEditor and clear validation");

    // The enabled flag is one Value<bool>: the list's CheckBox binds it and
    // the rack module's enable switch follows it in both directions.
    const std::shared_ptr<gui_forms::CheckBox> enabled = std::dynamic_pointer_cast<CheckBox>(
        (*properties).editor("gallery.value-editors.properties.enabled"));
    require(enabled && (*enabled).checked() && (*rack).modules()[0].enabled,
            "the enabled row must be a checked CheckBox bound to the record");
    click(window, enabled);
    require(!(*enabled).checked() && !(*rack).modules()[0].enabled &&
                (*rack).modules()[0].state == InstrumentModuleState::staged,
            "unchecking the bound CheckBox must disable the rack module through the Value");
    require(window.perform_semantic_action("gallery.value-editors.rack.lamp.enable",
                                           SemanticAction::press) &&
                (*rack).modules()[0].enabled && (*enabled).checked() &&
                (*properties).value("gallery.value-editors.properties.enabled") == "True",
            "re-enabling from the rack must flow back into the bound CheckBox and its row");

    // Reset record: programmatic setters restore every view.
    click(window, window.find("gallery.value-editors.reset"));
    require((*color).value() == Color::rgba(39, 116, 184, 255) &&
                (*flags).value().name == "Top, Left" &&
                (*properties).value("gallery.value-editors.properties.channel") == "FM" &&
                (*rack_channel).selected_index() == std::optional<std::size_t>{1U},
            "the reset Command must restore the fixture in every view");

    // BindingContext membership through two Commands shown by one CommandBar.
    require((*binding_commands).items().size() == 2U && (*binding_commands).item(0).enabled.read() &&
                !(*binding_commands).item(1).enabled.read(),
            "register must start enabled and release disabled");
    click(window, window.find("gallery.value-editors.binding-commands.item.1"));
    require((*binding_status).text().find("1 source") != std::string::npos &&
                (*binding_status).text().find("lamp.profile") != std::string::npos &&
                (*binding_status).text().find("added") != std::string::npos &&
                !(*binding_commands).item(0).enabled.read() && (*binding_commands).item(1).enabled.read(),
            "registering must add the record's BindingSource to the Window's BindingContext");
    click(window, window.find("gallery.value-editors.binding-commands.item.2"));
    require((*binding_status).text().find("0 sources") != std::string::npos &&
                (*binding_status).text().find("removed") != std::string::npos &&
                (*binding_commands).item(0).enabled.read() && !(*binding_commands).item(1).enabled.read(),
            "releasing must remove the BindingSource and re-enable register");
}

}  // namespace gallery_pages_tests
