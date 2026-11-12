#include "pages.hpp"
#include "gui_forms/controls/checked_list_box.hpp"
#include "gui_forms/controls/command_bar.hpp"
#include "gui_forms/controls/list_view.hpp"

#include "checked-collections_form.hpp"

#include <array>
#include <vector>

namespace gui_forms::gallery::pages {
namespace {

// CommandBar item ids; the bar names its retained buttons
// "gallery.checked.command.item.<id>".
constexpr int command_check_all = 1;
constexpr int command_clear_all = 2;
constexpr int command_set_mixed = 3;

// Owns the checked-collections page: the controls
// demo/pages/checked-collections.form describes, and their behaviour.
class CheckedCollectionsPage final : public gui_forms::Component {
public:
    CheckedCollectionsPage(const std::shared_ptr<Surface>& body,
                           const std::shared_ptr<PageContext>& context) {
        static_cast<void>(context);
        document_.initialize_component(*this);
        mount_document(body);
        build_deliberate();
        build_immediate();
        build_commands();
        build_list_view();
    }

private:
    void mount_document(const std::shared_ptr<Surface>& body) {
        // The document's root is a declared boundary only: its children move
        // into the shell's page body, where each is placed by its bounds in
        // design coordinates. The copy of the child list keeps the iteration
        // valid while the root shrinks.
        Control& root = *document_.gallery_checked_document;
        const std::vector<Control::Ptr> children(root.children().begin(), root.children().end());
        for (const Control::Ptr& child : children) {
            Control::Ptr moved = root.remove_child((*child).runtime_id());
            (*body).add_child(std::move(moved));
        }
        immediate_ = document_.gallery_checked_immediate_list;
        list_view_ = document_.gallery_checked_list_view_list;
        mark_toggle_ = document_.gallery_checked_list_view_mark;
        status_ = document_.gallery_checked_status;
    }

    // The deliberate list's check states; the first click selects, the
    // second toggles (the CheckedListBox default).
    void build_deliberate() {
        CheckedListBox& deliberate = *document_.gallery_checked_deliberate_list;
        constexpr std::array<CheckState, 6> states{CheckState::checked,       CheckState::checked,
                                                   CheckState::indeterminate, CheckState::checked,
                                                   CheckState::unchecked,     CheckState::unchecked};
        for (std::size_t index = 0U; index < states.size(); ++index) {
            deliberate.set_item_check_state(index, states[index]);
        }
        on(deliberate.item_check_state_changed(), *this, &CheckedCollectionsPage::on_item_checked, 0);
    }

    void build_immediate() {
        (*immediate_).check_on_click = true;
        constexpr std::array<CheckState, 6> states{CheckState::checked,   CheckState::checked,
                                                   CheckState::unchecked, CheckState::indeterminate,
                                                   CheckState::checked,   CheckState::unchecked};
        for (std::size_t index = 0U; index < states.size(); ++index) {
            (*immediate_).set_item_check_state(index, states[index]);
        }
        (*immediate_).select_index(1U);
        (*immediate_).select_index(4U, true, false);
        on((*immediate_).item_check_state_changed(), *this, &CheckedCollectionsPage::on_item_checked, 1);
    }

    void build_commands() {
        CommandBar& bar = *document_.gallery_checked_command;
        bar.set_items({
            {.id = command_check_all, .label = "Check all"},
            {.id = command_clear_all, .label = "Clear all"},
            {.id = command_set_mixed, .label = "Set mixed"},
        });
        bar.item(0).visual_style = ButtonVisualStyle::accent;
        on(bar.invoked(), *this, &CheckedCollectionsPage::on_command);
    }

    // A details ListView with check boxes. A header click sorts through
    // the sort_items helper; the view itself never reorders.
    void build_list_view() {
        (*list_view_).check_boxes = true;
        (*list_view_).full_row_select = true;
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
        on((*list_view_).column_click(), *this, &CheckedCollectionsPage::on_column_click);
        on((*list_view_).item_checked(), *this, &CheckedCollectionsPage::on_list_view_checked);
        (*list_view_).label_edit = true;
        on((*list_view_).after_label_edit(), *this, &CheckedCollectionsPage::on_list_view_renamed);
        on((*mark_toggle_).clicked(), *this, &CheckedCollectionsPage::on_mark_toggled);
    }

    // The insertion mark an application shows while an item is dragged:
    // here before the third row, until the toggle clears it.
    void on_mark_toggled(ButtonBase&) {
        if ((*list_view_).insertion_mark().has_value()) {
            (*list_view_).clear_insertion_mark();
            (*mark_toggle_).text = "Show insertion mark";
            (*status_).text = "ListView insertion mark cleared";
            return;
        }
        const bool shown = (*list_view_).show_insertion_mark({.index = 2U, .after = false});
        (*mark_toggle_).text = shown ? "Clear insertion mark" : "Show insertion mark";
        (*status_).text = shown ? "ListView insertion mark before the third row · drop role"
                                  : "ListView has no third row to mark";
    }

    void on_column_click(const ListViewColumnClick& click) {
        const bool ascending = (*list_view_).sort_column() == click.column &&
                               (*list_view_).sort_order() == ListViewSortOrder::ascending;
        (*list_view_).sort_items(click.column, ascending ? ListViewSortOrder::descending
                                                         : ListViewSortOrder::ascending);
        (*status_).text = "ListView sorted by " + click.column_id +
                            (ascending ? " descending" : " ascending") + " · the application chose the order";
    }

    void on_list_view_checked(const ListViewItemCheck& check) {
        (*status_).text = "ListView item " + check.item_id + (check.new_value ? " checked" : " unchecked");
    }

    void on_list_view_renamed(ListViewLabelEdit& edit) {
        if (!edit.label) {
            (*status_).text = "ListView rename of " + edit.item_id + " cancelled";
            return;
        }
        (*status_).text = "ListView item " + edit.item_id + " renamed to " + *edit.label;
    }

    // Bound value 0/1 names the list; both lists share one handler.
    void on_item_checked(const int list, const std::size_t index, const CheckState state) {
        (*status_).text = std::string(list == 0 ? "Deliberate" : "Immediate") + " item " +
                            std::to_string(index + 1U) + " committed " +
                            (state == CheckState::checked ? "checked"
                             : state == CheckState::indeterminate ? "mixed" : "unchecked") +
                            " · selection unchanged";
    }

    void on_command(const CommandItem& item) {
        if (item.id == command_set_mixed) {
            if (!(*immediate_).items.read().empty()) {
                (*immediate_).set_item_check_state(3U, CheckState::indeterminate);
            }
            return;
        }
        const bool checked = item.id == command_check_all;
        for (std::size_t index = 0U; index < (*immediate_).items.read().size(); ++index) {
            (*immediate_).set_item_checked(index, checked);
        }
    }

    CheckedCollectionsDocument<CheckedCollectionsPage> document_{};
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
