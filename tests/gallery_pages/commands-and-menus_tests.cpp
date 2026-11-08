#include "page_tests.hpp"

namespace gallery_pages_tests {

void test_commands_and_menus(gui_forms::Window& window) {
    select_page(window, "commands-and-menus");
    require(window.find("gallery.commands-and-menus.heading") != nullptr,
            "commands-and-menus page shows its heading");
    const std::shared_ptr<Label> status =
        std::dynamic_pointer_cast<Label>(window.find("gallery.commands.status"));
    const std::shared_ptr<Label> saves =
        std::dynamic_pointer_cast<Label>(window.find("gallery.commands.saves"));
    const Control::Ptr modified = window.find("gallery.commands.modified");
    const Control::Ptr save_button = window.find("gallery.commands.save.button");
    const Control::Ptr save_item = window.find("gallery.commands.bar.item.2");
    const Control::Ptr delete_button = window.find("gallery.commands.delete.button");
    require(status && saves && modified && save_button && save_item && delete_button,
            "commands page must publish its Command surfaces by stable id");

    // One Command, projected: a clean document disables Save on the button and
    // the bar item alike; Delete stays disabled by its availability reason.
    require(!(*save_button).effectively_enabled() && !(*save_item).effectively_enabled() &&
                !(*delete_button).effectively_enabled(),
            "Save and Delete Commands must project disabled state onto every bound button");
    click(window, modified);
    require((*save_button).effectively_enabled() && (*save_item).effectively_enabled(),
            "the modified Value must enable the Save Command on both buttons");
    click(window, save_item);
    require((*status).text.read().find("Save #1 invoked from gallery.commands.bar.item.2") == 0U &&
                (*saves).text.read() == "Saves: 1" && !(*save_button).effectively_enabled(),
            "a CommandBar item must invoke the shared Command and name itself as the source");

    // Ctrl+S is the bar item's shortcut; it reaches the same Command.
    click(window, modified);
    require((*save_item).effectively_enabled(), "re-modifying must re-enable Save");
    require(window.dispatch_key({KeyAction::down, PhysicalKey::s, Modifier::control}) &&
                (*saves).text.read() == "Saves: 2" && !(*save_item).effectively_enabled(),
            "the CommandBar shortcut must execute the shared Save Command");

    // MenuStrip: keyboard opening presents the File menu as a ContextMenu overlay
    // whose Save row executes the very same Command.
    click(window, modified);
    const Control::Ptr strip = window.find("gallery.commands.menu");
    require(strip && window.request_focus(strip) &&
                window.dispatch_key({KeyAction::down, PhysicalKey::down}) &&
                window.find("gallery.commands.menu.menu.overlay.panel.0") != nullptr &&
                window.focus_scope_depth() == 1U,
            "Down on the focused MenuStrip must open its first menu as an overlay root");
    window.perform_layout();
    const Control::Ptr menu_save = window.find("gallery.commands.menu.menu.overlay.row.file.save");
    require(menu_save && (*menu_save).effectively_enabled(),
            "the File menu must present the enabled Save Command as a row");
    click(window, menu_save);
    require((*saves).text.read() == "Saves: 3" &&
                (*status).text.read().find("Save #3 invoked from") == 0U &&
                (*status).text.read().find("menu gallery.commands.menu.file") != std::string::npos &&
                window.find("gallery.commands.menu.menu.overlay.panel.0") == nullptr &&
                window.focus_scope_depth() == 0U,
            "a MenuStrip row must execute the shared Command, report its menu and close");
    std::string semantics = window.semantic_snapshot().to_json();
    require(semantics.find("\"role\":\"menu_bar\"") != std::string::npos &&
                semantics.find("\"role\":\"toolbar\"") != std::string::npos,
            "the page must publish menu-bar and toolbar semantics");

    // ContextMenu: the Word wrap check row toggles the page's Value, which the
    // check box and the bar item (selected state) both reflect.
    const std::shared_ptr<CheckBox> wrap =
        std::dynamic_pointer_cast<CheckBox>(window.find("gallery.commands.wrap.check"));
    const std::shared_ptr<Button> wrap_item =
        std::dynamic_pointer_cast<Button>(window.find("gallery.commands.bar.item.4"));
    const Control::Ptr context_button = window.find("gallery.commands.context.open");
    require(wrap && wrap_item && context_button && !(*wrap).checked.read() && !(*wrap_item).selected(),
            "word wrap must start off on the check box and the bar item");
    click(window, context_button);
    require(window.find("gallery.commands.context.overlay.panel.0") != nullptr &&
                window.focus_scope_depth() == 1U,
            "the context button must open the ContextMenu as a focus-contained overlay");
    window.perform_layout();
    click(window, window.find("gallery.commands.context.overlay.row.wrap"));
    require((*wrap).checked.read() && (*wrap_item).selected() &&
                window.find("gallery.commands.context.overlay.panel.0") == nullptr,
            "the context-menu check row must toggle the shared state everywhere and close");
    click(window, wrap_item);
    require(!(*wrap).checked.read() && !(*wrap_item).selected(),
            "the bar's Word wrap item must toggle the same Value back");
    click(window, context_button);
    require(window.find("gallery.commands.context.overlay.panel.0") != nullptr,
            "the context menu must reopen");
    require(window.dispatch_key({KeyAction::down, PhysicalKey::escape}) &&
                window.find("gallery.commands.context.overlay.panel.0") == nullptr &&
                window.focus_scope_depth() == 0U && window.focused_control() == context_button,
            "Escape must revoke the context menu and restore the invoker's focus");

    // DropDownButton (menu mode): the whole button discloses an owned menu and
    // mirrors its open state; a row executes an Export Command.
    const std::shared_ptr<DropDownButton> export_button =
        std::dynamic_pointer_cast<DropDownButton>(window.find("gallery.commands.export"));
    require(export_button && !(*export_button).drop_down_open(),
            "the Export drop-down must start closed");
    click(window, export_button);
    require((*export_button).drop_down_open() &&
                window.find("gallery.commands.export.menu.overlay.panel.0") != nullptr,
            "clicking a menu-mode DropDownButton must open its menu and mark itself open");
    window.perform_layout();
    click(window, window.find("gallery.commands.export.menu.overlay.row.export.png"));
    require((*status).text.read().find("Export PNG") == 0U && !(*export_button).drop_down_open() &&
                window.find("gallery.commands.export.menu.overlay.panel.0") == nullptr,
            "an export row must execute its Command and close the drop-down");

    // DropDownButton (split mode): the primary region keeps the ordinary Click
    // path (bound Run Command); the trailing region opens the options menu.
    const std::shared_ptr<DropDownButton> run_button =
        std::dynamic_pointer_cast<DropDownButton>(window.find("gallery.commands.run"));
    require(run_button != nullptr, "the Run split button must resolve");
    const Rect run_bounds = (*run_button).absolute_bounds();
    const Point primary{run_bounds.x + 24.0, run_bounds.y + run_bounds.height * 0.5};
    require(window.dispatch_pointer({PointerAction::down, PointerButton::primary, primary}) &&
                window.dispatch_pointer({PointerAction::up, PointerButton::primary, primary}) &&
                (*status).text.read().find("Run · split button primary region") == 0U &&
                !(*run_button).drop_down_open(),
            "the split button's primary region must invoke the bound Run Command");
    const Point disclosure{run_bounds.x + run_bounds.width - 6.0,
                           run_bounds.y + run_bounds.height * 0.5};
    require(window.dispatch_pointer({PointerAction::down, PointerButton::primary, disclosure}) &&
                window.dispatch_pointer({PointerAction::up, PointerButton::primary, disclosure}) &&
                (*run_button).drop_down_open() &&
                window.find("gallery.commands.run.menu.overlay.row.run.watch") != nullptr,
            "the split button's trailing region must open the options menu");
    require(window.dispatch_key({KeyAction::down, PhysicalKey::escape}) &&
                !(*run_button).drop_down_open() && window.focus_scope_depth() == 0U,
            "Escape must close the split button's menu and clear its open state");
}

}  // namespace gallery_pages_tests
