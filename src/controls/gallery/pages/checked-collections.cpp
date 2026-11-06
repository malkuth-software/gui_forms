#include "pages.hpp"

namespace gui_forms::gallery::pages {
namespace {

// CommandBar item ids; the bar names its retained buttons
// "gallery.checked.command.item.<id>".
constexpr int command_check_all = 1;
constexpr int command_clear_all = 2;
constexpr int command_set_mixed = 3;

class CheckedCollectionsPage final : public Component {
public:
    CheckedCollectionsPage(const std::shared_ptr<Surface>& body,
                           const std::shared_ptr<PageContext>& context) {
        static_cast<void>(context);
        (*body).add_at(label("gallery.checked.heading", "CHECKED COLLECTIONS", 22.0,
                             700, shell_blue_dark), {24.0, 18.0, 620.0, 34.0});
        (*body).add_at(label("gallery.checked.lead",
                             "Check state remains independent of selection, with cancellable pre-change events, keyboard control, and checkable virtual rows.",
                             12.0, 400, muted), {26.0, 52.0, 920.0, 24.0});
        build_deliberate(body);
        build_immediate(body);
        build_commands(body);
        build_list_view(body);
    }

private:
    void build_deliberate(const std::shared_ptr<Surface>& body) {
        std::shared_ptr<LayoutGroup> deliberate_group = group(
            "gallery.checked.deliberate", "CheckedListBox · deliberate two-click policy",
            {450.0, 400.0});
        (*body).add_at(deliberate_group, {24.0, 92.0, 450.0, 400.0});
        std::shared_ptr<CheckedListBox> deliberate = make_control<CheckedListBox>(
            StableId("gallery.checked.deliberate.list"));
        (*deliberate).accessible_name = "Deliberate feature selection";
        (*deliberate).add_item("Renderer-neutral core", CheckState::checked);
        (*deliberate).add_item("Window-owned resources", CheckState::checked);
        (*deliberate).add_item("Native accessibility", CheckState::indeterminate);
        (*deliberate).add_item("Stable semantic IDs", CheckState::checked);
        (*deliberate).add_item("Renderer-free build", CheckState::unchecked);
        (*deliberate).add_item("Sustained soak gate", CheckState::unchecked);
        (*deliberate_group).add_at(deliberate, {24.0, 40.0, 400.0, 282.0});
        on((*deliberate).item_check_state_changed(), *this,
           &CheckedCollectionsPage::on_item_checked, 0);
        std::shared_ptr<Label> deliberate_note = label(
            "gallery.checked.deliberate.note",
            "First click selects. Click the selected row again or press Space to toggle.",
            10.0, 400, muted);
        (*deliberate_note).set_text_wrapping(TextWrapping::word);
        (*deliberate_note).set_vertical_alignment(VerticalAlignment::near);
        (*deliberate_group).add_at(deliberate_note, {24.0, 332.0, 400.0, 48.0});
    }

    void build_immediate(const std::shared_ptr<Surface>& body) {
        std::shared_ptr<LayoutGroup> immediate_group = group(
            "gallery.checked.immediate", "CheckedListBox · CheckOnClick + extended selection",
            {460.0, 400.0});
        (*body).add_at(immediate_group, {494.0, 92.0, 460.0, 400.0});
        immediate_ = make_control<CheckedListBox>(StableId("gallery.checked.immediate.list"));
        (*immediate_).accessible_name = "Immediate capability selection";
        (*immediate_).set_check_on_click(true);
        (*immediate_).set_selection_mode(ListSelectionMode::multiple_extended);
        (*immediate_).add_item("Pointer activation", CheckState::checked);
        (*immediate_).add_item("Space-key toggle", CheckState::checked);
        (*immediate_).add_item("Cancelled transition", CheckState::unchecked);
        (*immediate_).add_item("Indeterminate state", CheckState::indeterminate);
        (*immediate_).add_item("Mutation remapping", CheckState::checked);
        (*immediate_).add_item("Semantic press", CheckState::unchecked);
        (*immediate_).select_index(1U);
        (*immediate_).select_index(4U, true, false);
        (*immediate_group).add_at(immediate_, {24.0, 40.0, 410.0, 282.0});
        on((*immediate_).item_check_state_changed(), *this,
           &CheckedCollectionsPage::on_item_checked, 1);
        std::shared_ptr<Label> immediate_note = label(
            "gallery.checked.immediate.note",
            "CheckOnClick toggles immediately; Shift/Ctrl selection remains a separate model.",
            10.0, 400, muted);
        (*immediate_note).set_text_wrapping(TextWrapping::word);
        (*immediate_note).set_vertical_alignment(VerticalAlignment::near);
        (*immediate_group).add_at(immediate_note, {24.0, 332.0, 410.0, 48.0});
    }

    void build_commands(const std::shared_ptr<Surface>& body) {
        std::shared_ptr<LayoutGroup> commands = group(
            "gallery.checked.commands", "Check-state commands and ordered telemetry",
            {930.0, 112.0});
        (*body).add_at(commands, {24.0, 514.0, 930.0, 112.0});
        // One CommandBar replaces the hand-rolled button row: one group
        // notification, a single roving tab stop, and Arrow-key movement.
        std::shared_ptr<CommandBar> bar = make_control<CommandBar>(
            StableId("gallery.checked.command"));
        (*bar).set_style(page_style());
        (*bar).accessible_name = "Check-state commands";
        (*bar).set_items({
            {.id = command_check_all, .label = "Check all"},
            {.id = command_clear_all, .label = "Clear all"},
            {.id = command_set_mixed, .label = "Set mixed"},
        });
        for (int index = 0; index < static_cast<int>((*bar).items().size()); ++index) {
            (*bar).item(index).set_style(page_style());
        }
        (*bar).item(0).set_visual_style(ButtonVisualStyle::accent);
        (*commands).add_at(bar, {22.0, 42.0, 418.0, 34.0});
        on((*bar).invoked(), *this, &CheckedCollectionsPage::on_command);
        status_ = label("gallery.checked.status",
                        "Ready · pre-change event precedes committed state", 10.0, 600, green);
        (*status_).set_text_wrapping(TextWrapping::word);
        (*status_).set_vertical_alignment(VerticalAlignment::near);
        (*commands).add_at(status_, {470.0, 38.0, 420.0, 48.0});
    }

    // A details ListView with check boxes. A header click sorts through
    // the sort_items helper; the view itself never reorders.
    void build_list_view(const std::shared_ptr<Surface>& body) {
        std::shared_ptr<LayoutGroup> list_group = group(
            "gallery.checked.list-view", "ListView · details, check boxes, sortable columns, by-role painted parts",
            {930.0, 250.0});
        (*body).add_at(list_group, {24.0, 640.0, 930.0, 250.0});
        list_view_ = make_control<ListView>(StableId("gallery.checked.list-view.list"));
        (*list_view_).accessible_name = "Release checklist";
        (*list_view_).set_check_boxes(true);
        (*list_view_).set_full_row_select(true);
        (*list_view_).set_columns({
            {.stable_id = "task", .text = "Task", .width = 260.0},
            {.stable_id = "owner", .text = "Owner", .width = 140.0},
            {.stable_id = "estimate", .text = "Estimate", .width = 100.0, .alignment = ListViewAlignment::right},
            {.stable_id = "stage", .text = "Stage", .width = 120.0},
        });
        (*list_view_).set_items({
            {.stable_id = "task.anatomy", .text = "Publish item anatomy", .sub_items = {"Controls", "2 d", "T1"},
             .checked = true},
            {.stable_id = "task.paint", .text = "Paint parts by role", .sub_items = {"Controls", "3 d", "W"},
             .checked = true},
            {.stable_id = "task.virtual", .text = "Virtual rows", .sub_items = {"Core", "1 d", "W"}},
            {.stable_id = "task.engine", .text = "Theme engine", .sub_items = {"Engine", "10 d", "T2"}},
            {.stable_id = "task.theme", .text = "Default theme", .sub_items = {"Design", "8 d", "T4"}},
            {.stable_id = "task.abi", .text = "Stable C ABI", .sub_items = {"Core", "6 d", "C"}},
        });
        (*list_group).add_at(list_view_, {22.0, 36.0, 640.0, 196.0});
        on((*list_view_).column_click(), *this, &CheckedCollectionsPage::on_column_click);
        on((*list_view_).item_checked(), *this, &CheckedCollectionsPage::on_list_view_checked);
        (*list_view_).set_label_edit(true);
        on((*list_view_).after_label_edit(), *this, &CheckedCollectionsPage::on_list_view_renamed);
        std::shared_ptr<Label> note = label(
            "gallery.checked.list-view.note",
            "Click a header to sort through the sort_items helper; drag a divider to resize. Space or a click on the box toggles a check; F2 renames the focused task.",
            10.0, 400, muted);
        (*note).set_text_wrapping(TextWrapping::word);
        (*note).set_vertical_alignment(VerticalAlignment::near);
        (*list_group).add_at(note, {686.0, 36.0, 220.0, 120.0});
        mark_toggle_ = make_control<Button>(StableId("gallery.checked.list-view.mark"), "Show insertion mark");
        (*mark_toggle_).set_style(page_style());
        (*list_group).add_at(mark_toggle_, {686.0, 166.0, 200.0, 30.0});
        on((*mark_toggle_).clicked(), *this, &CheckedCollectionsPage::on_mark_toggled);
    }

    // The insertion mark an application shows while an item is dragged:
    // here before the third row, until the toggle clears it.
    void on_mark_toggled(ButtonBase&) {
        if ((*list_view_).insertion_mark().has_value()) {
            (*list_view_).clear_insertion_mark();
            (*mark_toggle_).text = "Show insertion mark";
            (*status_).set_text("ListView insertion mark cleared");
            return;
        }
        const bool shown = (*list_view_).show_insertion_mark({.index = 2U, .after = false});
        (*mark_toggle_).text = shown ? "Clear insertion mark" : "Show insertion mark";
        (*status_).set_text(shown ? "ListView insertion mark before the third row · drop role"
                                  : "ListView has no third row to mark");
    }

    void on_column_click(const ListViewColumnClick& click) {
        const bool ascending = (*list_view_).sort_column() == click.column &&
                               (*list_view_).sort_order() == ListViewSortOrder::ascending;
        (*list_view_).sort_items(click.column, ascending ? ListViewSortOrder::descending
                                                         : ListViewSortOrder::ascending);
        (*status_).set_text("ListView sorted by " + click.column_id +
                            (ascending ? " descending" : " ascending") + " · the application chose the order");
    }

    void on_list_view_checked(const ListViewItemCheck& check) {
        (*status_).set_text("ListView item " + check.item_id + (check.new_value ? " checked" : " unchecked"));
    }

    void on_list_view_renamed(ListViewLabelEdit& edit) {
        if (!edit.label) {
            (*status_).set_text("ListView rename of " + edit.item_id + " cancelled");
            return;
        }
        (*status_).set_text("ListView item " + edit.item_id + " renamed to " + *edit.label);
    }

    // Bound value 0/1 names the list; both lists share one handler.
    void on_item_checked(const int list, const std::size_t index, const CheckState state) {
        (*status_).set_text(std::string(list == 0 ? "Deliberate" : "Immediate") + " item " +
                            std::to_string(index + 1U) + " committed " +
                            (state == CheckState::checked ? "checked"
                             : state == CheckState::indeterminate ? "mixed" : "unchecked") +
                            " · selection unchanged");
    }

    void on_command(const CommandItem& item) {
        if (item.id == command_set_mixed) {
            if (!(*immediate_).items().empty()) {
                (*immediate_).set_item_check_state(3U, CheckState::indeterminate);
            }
            return;
        }
        const bool checked = item.id == command_check_all;
        for (std::size_t index = 0U; index < (*immediate_).items().size(); ++index) {
            (*immediate_).set_item_checked(index, checked);
        }
    }

    std::shared_ptr<CheckedListBox> immediate_;
    std::shared_ptr<ListView> list_view_;
    std::shared_ptr<Button> mark_toggle_;
    std::shared_ptr<Label> status_;
};

} // namespace

void add_checked_collections(const std::shared_ptr<Surface>& page,
                             const std::shared_ptr<PageContext>& context) {
    (*context).components.add(std::make_shared<CheckedCollectionsPage>(page, context));
}

} // namespace gui_forms::gallery::pages
