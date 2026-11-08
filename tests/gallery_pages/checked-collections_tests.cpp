#include "support.hpp"

namespace gallery_pages_tests {

void test_checked_collections(gui_forms::Window& window) {
    select_page(window, "checked-collections");
    std::shared_ptr<gui_forms::CheckedListBox> checked = std::dynamic_pointer_cast<CheckedListBox>(
        window.find("gallery.checked.immediate.list"));
    require(checked && (*checked).check_on_click() &&
                (*checked).item_check_state(3U) == CheckState::indeterminate,
            "gallery checked page must retain a public check-state collection");
    const Rect checked_bounds = (*checked).absolute_bounds();
    const Point last_row{checked_bounds.x + 14.0,
                         checked_bounds.y + 2.0 + 5.5 * (*checked).item_height.read()};
    require(window.dispatch_pointer({PointerAction::down, PointerButton::primary,
                                     last_row}) &&
                window.dispatch_pointer({PointerAction::up, PointerButton::primary,
                                         last_row}) &&
                (*checked).item_checked(5U),
            "gallery CheckOnClick row must select and toggle immediately");
    std::shared_ptr<gui_forms::Label> status = std::dynamic_pointer_cast<Label>(
        window.find("gallery.checked.status"));
    require(status && (*status).text().find("Immediate item 6 committed checked") == 0U,
            "item_check_state_changed must reach the owner's bound-list handler");
    const std::string checked_semantics = window.semantic_snapshot().to_json();
    require(checked_semantics.find("\"role\":\"check_list_item\"") !=
                std::string::npos,
            "gallery checked rows must publish checkable virtual semantics");
    // The command row is one CommandBar: its buttons are the bar's retained
    // item parts and dispatch through the bar's single group notification.
    require(checked_semantics.find("\"role\":\"toolbar\"") != std::string::npos,
            "gallery check-state commands must publish toolbar semantics");
    click(window, window.find("gallery.checked.command.item.2"));
    bool all_clear = true;
    for (std::size_t index = 0U; index < (*checked).items.read().size(); ++index) {
        all_clear = all_clear && !(*checked).item_checked(index);
    }
    require(all_clear, "Clear all must uncheck every row through the public check model");
    click(window, window.find("gallery.checked.command.item.1"));
    bool all_checked = true;
    for (std::size_t index = 0U; index < (*checked).items.read().size(); ++index) {
        all_checked = all_checked && (*checked).item_checked(index);
    }
    require(all_checked, "Check all must check every row through the public check model");
    click(window, window.find("gallery.checked.command.item.3"));
    require((*checked).item_check_state(3U) == CheckState::indeterminate,
            "Set mixed must restore the indeterminate row");

    // The toggle sits below the fold, so it is pressed through its public
    // activation rather than a hit-tested click.
    std::shared_ptr<gui_forms::ListView> list_view =
        std::dynamic_pointer_cast<ListView>(window.find("gallery.checked.list-view.list"));
    std::shared_ptr<gui_forms::Button> mark =
        std::dynamic_pointer_cast<Button>(window.find("gallery.checked.list-view.mark"));
    require(list_view && mark && !(*list_view).insertion_mark().has_value(),
            "the gallery ListView starts without an insertion mark");
    (*mark).perform_click();
    require((*list_view).insertion_mark() == std::optional<ListViewInsertionMark>(ListViewInsertionMark{2U, false}),
            "the gallery toggle shows the insertion mark before the third row");
    (*mark).perform_click();
    require(!(*list_view).insertion_mark().has_value(), "the gallery toggle clears the insertion mark");

    // F2's label edit, opened through its public entry point for the same
    // reason. The edits leave the task's text as it was.
    require((*list_view).label_edit.read() && (*list_view).begin_label_edit(0U) &&
                window.dispatch_text({"Discarded"}) &&
                window.dispatch_key({KeyAction::down, PhysicalKey::escape}) &&
                (*status).text() == "ListView rename of task.anatomy cancelled",
            "Escape cancels the gallery ListView's rename and the page reports it");
    require((*list_view).begin_label_edit(0U) && window.dispatch_key({KeyAction::down, PhysicalKey::enter}) &&
                (*status).text() == "ListView item task.anatomy renamed to Publish item anatomy" &&
                (*list_view).items()[0].text == "Publish item anatomy",
            "Enter commits the gallery ListView's label and the page reports it");
}

} // namespace gallery_pages_tests
