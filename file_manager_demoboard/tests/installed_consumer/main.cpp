#include "gui_forms/gui_forms.hpp"

#include <memory>
#include <string>
#include <vector>

namespace {

struct AcceleratorInvocationCounter final {
    std::size_t* invocations;

    void operator()(const gui_forms::CommandInvocation&) const {
        ++(*invocations);
    }
};

struct InstalledAcceleratorExecution final {
    std::shared_ptr<gui_forms::Command> command;

    bool operator()() const {
        const bool executed = (*command).execute("installed.accelerator");
        return executed;
    }
};

struct CorrespondenceActivationRecorder final {
    std::string* activation;

    void operator()(const std::string& stable_id) const {
        *activation = stable_id;
    }
};

} // namespace

int main() {
    std::shared_ptr<gui_forms::Panel> root =
        std::make_shared<gui_forms::Panel>(
            gui_forms::StableId("installed.consumer.root"));
    std::shared_ptr<gui_forms::Label> label =
        std::make_shared<gui_forms::Label>(
            gui_forms::StableId("installed.consumer.label"),
            "GUI.Forms installed package consumer");
    (*label).set_requested_bounds({8, 8, 260, 24});
    (*root).add_child(label);
    std::shared_ptr<gui_forms::TreeView> tree =
        std::make_shared<gui_forms::TreeView>(
            gui_forms::StableId("installed.consumer.tree"));
    (*tree).set_requested_bounds({8, 36, 140, 80});
    (*tree).set_items({{"installed.node.root", "Root", 0, true, true},
                       {"installed.node.child", "Child", 1}});
    (*tree).set_selected_id("installed.node.child");
    (*root).add_child(tree);
    std::shared_ptr<gui_forms::ObjectView> objects =
        std::make_shared<gui_forms::ObjectView>(
            gui_forms::StableId("installed.consumer.objects"));
    (*objects).set_requested_bounds({156, 36, 156, 80});
    (*objects).set_items({{"installed.object", "Fixture", "1 KB",
                           "Installed consumer fixture",
                           gui_forms::ObjectGlyph::document},
                          {"installed.object.second", "Second fixture", "2 KB",
                           "Installed consumer second fixture",
                           gui_forms::ObjectGlyph::folder}});
    (*objects).set_selected_ids({"installed.object", "installed.object.second"},
                                "installed.object");
    (*root).add_child(objects);
    std::shared_ptr<gui_forms::Button> button =
        std::make_shared<gui_forms::Button>(
            gui_forms::StableId("installed.consumer.command"), "stale");
    std::shared_ptr<gui_forms::Command> command =
        std::make_shared<gui_forms::Command>("installed.command",
                                             "Installed command");
    (*command).set_description("Installed public command metadata");
    gui_forms::CommandBinding binding(command, button);
    (*button).set_requested_bounds({8, 116, 180, 28});
    (*root).add_child(button);
    std::shared_ptr<gui_forms::Button> overlay_owner =
        std::make_shared<gui_forms::Button>(
            gui_forms::StableId("installed.overlay.owner"), "Open overlay");
    (*overlay_owner).set_requested_bounds({190, 116, 120, 28});
    (*overlay_owner).set_expanded_state(false);
    (*root).add_child(overlay_owner);
    std::shared_ptr<gui_forms::ListBox> stable_list =
        std::make_shared<gui_forms::ListBox>(
            gui_forms::StableId("installed.stable-list"));
    (*stable_list).set_requested_bounds({320, 244, 192, 82});
    (*stable_list).set_items({"Alpha", "Beta"});
    (*stable_list).set_item_stable_ids(
        {"installed.stable-list.alpha", "installed.stable-list.beta"});
    (*root).add_child(stable_list);
    gui_forms::ContextMenu menu("installed.menu");
    menu.set_items({{"installed.menu.command",
                     gui_forms::MenuItemKind::command, command}});
    std::shared_ptr<gui_forms::MenuStrip> menu_strip =
        std::make_shared<gui_forms::MenuStrip>(
            gui_forms::StableId("installed.menu-strip"));
    (*menu_strip).set_requested_bounds({0, 152, 320, 24});
    (*menu_strip).set_items({{"installed.menu.file", "File", {
        {"installed.menu.file.command", gui_forms::MenuItemKind::command,
         command},
    }}});
    (*root).add_child(menu_strip);
    std::shared_ptr<gui_forms::PropertyList> properties =
        std::make_shared<gui_forms::PropertyList>(
            gui_forms::StableId("installed.properties"));
    (*properties).set_requested_bounds({320, 36, 192, 200});
    (*properties).set_groups({
        {"installed.properties.identity", "IDENTITY", {
            {"installed.properties.kind", "Kind", "Fixture", "Object kind"},
        }},
        {"installed.properties.editable", "EDITABLE", {
            {"installed.properties.name", "Name", "Fixture",
             "Session name", gui_forms::PropertyEditorKind::text},
            {"installed.properties.handler", "Opens with", "Preview",
             "Session handler", gui_forms::PropertyEditorKind::choice,
             {"Preview", "Inspector"}},
        }},
    });
    (*root).add_child(properties);
    std::shared_ptr<gui_forms::SplitContainer> split =
        std::make_shared<gui_forms::SplitContainer>(
            gui_forms::StableId("installed.split"));
    (*split).initialize_control_tree();
    (*split).set_requested_bounds({8, 184, 304, 150});
    (*split).set_splitter_distance(190);
    (*split).set_collapse_panel(gui_forms::SplitFixedPanel::second);
    (*root).add_child(split);
    std::shared_ptr<gui_forms::CorrespondenceView> correspondence =
        std::make_shared<gui_forms::CorrespondenceView>(
            gui_forms::StableId("installed.correspondence"));
    (*correspondence).set_requested_bounds({8, 340, 504, 170});
    std::vector<gui_forms::CorrespondenceItem> correspondence_items{
        {"installed.correspondence.first", "First evidence", "Fixture / first",
         "Exact installed-package reason", "Matched public evidence text", "98%",
         "Provider information", {"Installed index"}, "Generation 1",
         gui_forms::ObjectGlyph::document},
        {"installed.correspondence.second", "Second evidence", "Fixture / second",
         "Second installed-package reason", "Another matched excerpt", "82%",
         "Provider information", {"Installed index"}, "Generation 1",
         gui_forms::ObjectGlyph::image},
    };
    correspondence_items.front().emphasis_terms = {"evidence"};
    (*correspondence).set_items(std::move(correspondence_items));
    (*correspondence).set_selected_id("installed.correspondence.first");
    (*correspondence).set_pinned_id("installed.correspondence.first");
    (*root).add_child(correspondence);
    std::shared_ptr<gui_forms::InstrumentRack> rack =
        std::make_shared<gui_forms::InstrumentRack>(
            gui_forms::StableId("installed.instrument-rack"));
    (*rack).set_requested_bounds({8, 516, 504, 100});
    (*rack).set_modules({
        {"installed.criterion.kind", "Kind criterion",
         {{"property", "Property", "Kind",
           gui_forms::InstrumentFieldEditor::choice, {"Kind", "Name"}},
          {"value", "Value", "Images",
           gui_forms::InstrumentFieldEditor::choice,
           {"Images", "Documents"}}},
         "live · installed consumer", gui_forms::InstrumentModuleState::live,
         10, true, true},
        {"installed.criterion.content", "Content criterion",
         {{"value", "Value", "Facade",
           gui_forms::InstrumentFieldEditor::text}},
         "staged · installed consumer",
         gui_forms::InstrumentModuleState::staged, 5, true, true},
    });
    (*root).add_child(rack);
    gui_forms::Window window(root, {520, 624});
    window.perform_layout();
    gui_forms::AnchoredOverlayPlacement overlay_placement;
    overlay_placement.preferred_size = {160, 80};
    overlay_placement.horizontal_alignment =
        gui_forms::OverlayHorizontalAlignment::far;
    overlay_placement.gap = 2;
    std::shared_ptr<gui_forms::Panel> overlay_content =
        std::make_shared<gui_forms::Panel>(
            gui_forms::StableId("installed.overlay.content"));
    std::shared_ptr<gui_forms::AnchoredOverlayLayer> overlay_layer =
        std::make_shared<gui_forms::AnchoredOverlayLayer>(
            gui_forms::StableId("installed.overlay.layer"), overlay_owner,
            overlay_placement);
    (*overlay_layer).set_content(overlay_content);
    (*overlay_layer).set_requested_bounds({0, 0, 520, 350});
    gui_forms::OverlayToken overlay = window.open_overlay(overlay_owner, overlay_layer);
    window.perform_layout();
    const bool overlay_resolved = overlay.connected() &&
        (*overlay_layer).resolved_placement().bounds ==
            gui_forms::Rect{150, 146, 160, 80};
    overlay.disconnect();
    std::size_t accelerator_invocations{};
    gui_forms::SubscriptionToken invoked = (*command).invoked().subscribe(
        AcceleratorInvocationCounter{&accelerator_invocations});
    gui_forms::AcceleratorToken accelerator = window.register_accelerator(
        *command, {gui_forms::PhysicalKey::left, gui_forms::Modifier::alt},
        InstalledAcceleratorExecution{command}, {true});
    const bool accelerated = window.dispatch_key(
        {gui_forms::KeyAction::down, gui_forms::PhysicalKey::left,
         gui_forms::Modifier::alt});
    menu.show(button, {8, 8});
    const bool menu_visible = window.find("installed.menu.overlay.panel.0") != nullptr;
    menu.close();
    const bool strip_open = (*menu_strip).open(0U) &&
        window.find("installed.menu-strip.menu.overlay.panel.0") != nullptr;
    (*menu_strip).close();
    const bool property_edit = (*properties).set_validation(
        "installed.properties.name", "Installed validation") &&
        (*properties).set_value("installed.properties.name", "Renamed fixture") &&
        (*properties).value("installed.properties.name") == "Renamed fixture";
    const double remembered_split = (*split).splitter_distance();
    const bool collapsed = window.perform_semantic_action(
        "installed.split", gui_forms::SemanticAction::collapse);
    window.perform_layout();
    const bool restored = window.perform_semantic_action(
        "installed.split", gui_forms::SemanticAction::expand);
    window.perform_layout();
    std::string correspondence_activation;
    gui_forms::SubscriptionToken correspondence_invoked =
        (*correspondence).item_activated().subscribe(
            CorrespondenceActivationRecorder{&correspondence_activation});
    const bool correspondence_activated = window.perform_semantic_action(
        "installed.correspondence.first.activate",
        gui_forms::SemanticAction::press);
    return window.find("installed.consumer.label") == label &&
        (*tree).selected_id() == "installed.node.child" &&
        (*objects).selected_id() == "installed.object" &&
        (*objects).selected_ids().size() == 2U && menu_visible && strip_open &&
        accelerated && accelerator.connected() && accelerator_invocations == 1U &&
        property_edit && collapsed && restored && !(*split).second_collapsed() &&
        (*split).splitter_distance() == remembered_split &&
        overlay_resolved &&
        (*correspondence).items().size() == 2U &&
        (*correspondence).children().empty() &&
        (*correspondence).expanded("installed.correspondence.first") &&
        correspondence_activated &&
        correspondence_activation == "installed.correspondence.first" &&
        (*rack).modules().size() == 2U &&
        std::dynamic_pointer_cast<gui_forms::ComboBox>(
            (*rack).field_editor("installed.criterion.kind", "property")) &&
        std::dynamic_pointer_cast<gui_forms::TextBox>(
            (*rack).field_editor("installed.criterion.content", "value")) &&
        (*stable_list).item_stable_id(1U) == "installed.stable-list.beta" &&
        (*overlay_owner).expanded_state() == false &&
        (*button).text() == "Installed command" &&
        (*button).accessible_description() ==
            "Installed public command metadata" ? 0 : 1;
}
