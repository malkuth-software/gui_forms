#include "support.hpp"

namespace gallery_pages_tests {

void test_values_and_spinners(gui_forms::Window& window) {
    select_page(window, "values-and-spinners");
    std::shared_ptr<gui_forms::NumericUpDown> numeric = std::dynamic_pointer_cast<NumericUpDown>(
        window.find("gallery.values.numeric.1"));
    require(numeric && window.request_focus((*numeric).editor()),
            "gallery NumericUpDown must expose a focusable public editor");
    KeyEvent up{KeyAction::down, PhysicalKey::up};
    require(window.dispatch_key(up) && (*numeric).value() == 4.0 &&
                (*(*numeric).editor()).text.read() == "4.00",
            "gallery NumericUpDown key step must synchronize value and editor");
}

void test_values_and_spinners_binding(gui_forms::Window& window) {
    select_page(window, "values-and-spinners");
    const std::shared_ptr<gui_forms::TextBox> editor = std::dynamic_pointer_cast<TextBox>(
        window.find("gallery.values.binding.name"));
    const std::shared_ptr<gui_forms::CheckBox> enabled = std::dynamic_pointer_cast<CheckBox>(
        window.find("gallery.values.binding.enabled"));
    const std::shared_ptr<gui_forms::TrackBar> gain = std::dynamic_pointer_cast<TrackBar>(
        window.find("gallery.values.binding.gain"));
    const std::shared_ptr<gui_forms::Label> status = std::dynamic_pointer_cast<Label>(
        window.find("gallery.values.binding.status"));
    const std::shared_ptr<gui_forms::PropertyGrid> property_grid = std::dynamic_pointer_cast<PropertyGrid>(
        window.find("gallery.values.property-grid"));
    const std::shared_ptr<gui_forms::NumericUpDown> inspected_numeric = std::dynamic_pointer_cast<NumericUpDown>(
        window.find("gallery.values.numeric.1"));
    const std::shared_ptr<NumericUpDown> value_editor = property_grid
        ? std::dynamic_pointer_cast<NumericUpDown>((*property_grid).editor("Value"))
        : std::shared_ptr<NumericUpDown>{};
    const std::shared_ptr<NumericUpDown> bounds_x_editor = property_grid
        ? std::dynamic_pointer_cast<NumericUpDown>(
              (*property_grid).editor("Bounds.X"))
        : std::shared_ptr<NumericUpDown>{};
    require(property_grid && inspected_numeric && value_editor &&
                bounds_x_editor && (*bounds_x_editor).visible.read() &&
                (*property_grid).selected_object() == inspected_numeric &&
                (*property_grid).selected_origin("Value") ==
                    PropertyValueOrigin::local,
            "gallery must dogfood the public metadata-driven PropertyGrid against a stock compound control");
    require(window.request_focus((*value_editor).editor()),
            "gallery PropertyGrid numeric editor must expose its ordinary retained text focus target");
    (*value_editor).set_value(4.5);
    require((*inspected_numeric).value() == 4.5,
            "gallery PropertyGrid numeric factory must commit through the inspected NumericUpDown's registered Value property");
    const std::shared_ptr<gui_forms::ComboBox> items_target = std::dynamic_pointer_cast<ComboBox>(
        window.find("gallery.values.items-target"));
    const std::shared_ptr<gui_forms::CommandBar> inspector_commands =
        std::dynamic_pointer_cast<CommandBar>(
            window.find("gallery.values.inspector-commands"));
    require(inspector_commands && (*inspector_commands).items().size() == 4U &&
                (*inspector_commands).items()[1].command != nullptr &&
                (*inspector_commands).item(1).enabled.read(),
            "gallery inspector actions must be application Commands shown through one CommandBar");
    click(window, window.find("gallery.values.inspector-commands.item.2"));
    require(items_target && (*property_grid).selected_object() == items_target &&
                (*property_grid).property_expanded("Items") == true &&
                (*std::dynamic_pointer_cast<TextBox>(
                    (*property_grid).editor("Items[1]"))).text.read() == "FM",
            "gallery must expose the stock ComboBox.Items collection through the same recursive PropertyGrid");
    click(window, window.find("gallery.values.inspector-commands.item.4"));
    require((*items_target).items().size() == 4U &&
                (*items_target).items().back() == "CW" &&
                (*std::dynamic_pointer_cast<TextBox>(
                    (*property_grid).editor("Items[3]"))).text.read() == "CW",
            "gallery collection command must mutate the real stock Items property rather than a local display model");
    click(window, window.find("gallery.values.inspector-commands.item.1"));
    require((*property_grid).selected_object() == inspected_numeric,
            "gallery inspector target switching must restore the stock numeric specimen without stale collection rows");
    const std::shared_ptr<gui_forms::Label> inspection_status = std::dynamic_pointer_cast<Label>(
        window.find("gallery.values.status"));
    click(window, window.find("gallery.values.inspector-commands.item.3"));
    const std::shared_ptr<gui_forms::ColorValueEditor> color_editor = std::dynamic_pointer_cast<ColorValueEditor>(
        (*property_grid).editor("ForeColor"));
    const std::shared_ptr<gui_forms::FlagsValueEditor> flags_editor = std::dynamic_pointer_cast<FlagsValueEditor>(
        (*property_grid).editor("Anchor"));
    require(inspection_status &&
                (*property_grid).selected_object() == inspection_status &&
                color_editor && (*color_editor).editor() && flags_editor,
            "gallery Style inspection must dogfood the reusable color and flags editor services");
    (*(*color_editor).editor()).text = "#245A92FF";
    require(window.request_focus((*color_editor).editor()) &&
                window.dispatch_key({KeyAction::down, PhysicalKey::enter}) &&
                (*inspection_status).foreground() == Color::rgba(36, 90, 146),
            "gallery color dogfood must commit through the stock Label ForeColor descriptor");
    require(editor && enabled && gain && status &&
                (*editor).text.read() == "Local index" && (*enabled).checked() &&
                (*gain).value() == 28.0 &&
                (*status).text().find("profile.local") != std::string::npos,
            "gallery binding specimen must project the initial retained record");
    require(window.request_focus(editor),
            "gallery bound editor must focus before its retained edit");
    (*editor).text = "Local index edited";
    click(window, window.find("gallery.values.binding.next"));
    require((*editor).text.read() == "Archive review" && !(*enabled).checked() &&
                (*gain).value() == 61.0 &&
                (*status).text().find("profile.archive") != std::string::npos,
            "gallery binding currency command must update all three stock control families");
    click(window, window.find("gallery.values.binding.previous"));
    require((*editor).text.read() == "Local index edited",
            "gallery OnValidation binding must preserve the committed edit across currency movement");
    require(window.request_focus(editor),
            "gallery bound editor must accept focus for invalid-input dogfood");
    (*editor).text = std::string{};
    require(!window.request_focus(window.find("gallery.values.binding.next")) &&
                window.focused_control() == editor &&
                window.semantic_snapshot().to_json().find(
                    "Profile name may not be empty") != std::string::npos,
            "gallery validation must retain focus and project a binding-aware ErrorProvider error");
    (*editor).text = "Local index repaired";
    require(window.request_focus(window.find("gallery.values.binding.next")) &&
                window.semantic_snapshot().to_json().find(
                    "Profile name may not be empty") == std::string::npos,
            "corrected gallery input must commit and revoke its retained error adornment");
}

} // namespace gallery_pages_tests
