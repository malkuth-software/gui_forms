#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/borrow/borrowed_view/borrowed_view.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/controls/list_view_capabilities.hpp"
#include "gui_forms/controls/list_view_values.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/image_list.hpp"
#include "gui_forms/scheduler.hpp"
#include "gui_forms/text/localizable/localizable_property.hpp"
#include "gui_forms/theme/owner_draw/owner_draw.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace gui_forms {

class HScrollBar;
struct PartPaint;
class TextBox;
class VScrollBar;

namespace detail {
class CutOffTextRevealer;
} // namespace detail

// How a column's text sits in its cells. The first column's text always
// starts at the left, after the check box and icon, as in Windows Forms.
enum class ListViewAlignment : std::uint8_t { left, center, right };

// The order a sort glyph shows, or none.
enum class ListViewSortOrder : std::uint8_t { none, ascending, descending };

// One details-view column. `stable_id` is unique within the view and names
// the column in the published anatomy and semantics. `width` is in logical
// units at text scale 1.
struct GUI_FORMS_API ListViewColumn final {
    std::string stable_id{};
    GUI_FORMS_LOCALIZABLE(header) std::string text{};
    double width{120.0};
    ListViewAlignment alignment{ListViewAlignment::left};
};

// One item. `text` is shown in the first column (and as the label in the
// other views); `sub_items[n]` in column n + 1 of the details view. Sub-items
// past the last column are kept but not shown. `image_key` names an image
// in the small image list (details, list and small-icon views) or the large
// one (large-icon and tile views); an empty or unknown key shows no image.
// `checked` is shown while the view has check boxes. `group` names the
// ListViewGroup the item is shown in; an empty or unknown name puts it in
// the default group. A virtual view's items are never grouped.
struct GUI_FORMS_API ListViewItem final {
    std::string stable_id{};
    GUI_FORMS_LOCALIZABLE(text) std::string text{};
    std::vector<std::string> sub_items{};
    std::string image_key{};
    bool checked{};
    std::string group{};
};

// One group of a ListView (Windows Forms' ListViewGroup): a header over its
// items. `stable_id` is unique within the view; items name it in `group`.
struct GUI_FORMS_API ListViewGroup final {
    std::string stable_id{};
    GUI_FORMS_LOCALIZABLE(header) std::string header{};
    ListViewAlignment header_alignment{ListViewAlignment::left};
};

// The items of a virtual ListView (Windows Forms' VirtualMode with
// RetrieveVirtualItem). The application owns the data; the view asks for
// the count when the source is set or refresh_virtual_items() is called, and
// for single items only when it paints, lays out or describes the visible
// ones. Both functions are called on the view's UI thread from inside the
// view's own (const) work and must not change the view.
class GUI_FORMS_API ListViewItemSource {
public:
    virtual ~ListViewItemSource();
    [[nodiscard]] virtual std::size_t item_count() const = 0;
    // Fills `item` for `index` (below the last count the view read). `item`
    // is reused between calls: overwrite every field. An empty stable ID is
    // replaced by "#" and the index; text that is not valid UTF-8 is shown
    // empty.
    virtual void retrieve_item(std::size_t index, ListViewItem& item) const = 0;
};

// A change of the selected items, after it happened. The selected indices
// are read from the view (selected_indices()); a virtual view with a large
// selection does not copy them into every event.
struct GUI_FORMS_API ListViewSelectionChange final {
    std::string list_id{};
    std::size_t selected_count{};
    std::optional<std::size_t> focused_index{};
};

// An item activated by a double click, Enter, or the semantic press action
// on a view without check boxes.
struct GUI_FORMS_API ListViewItemActivation final {
    std::string list_id{};
    std::size_t index{};
    std::string item_id{};
};

// A check box toggled by the user. item_checking is raised first and may
// cancel the change or alter `new_value`; item_checked follows once the
// change is made. In a virtual view the application owns the checked state:
// it stores `new_value` when handling item_checking, and the view shows
// what the source then retrieves.
struct GUI_FORMS_API ListViewItemCheck final {
    std::string list_id{};
    std::size_t index{};
    std::string item_id{};
    bool current_value{};
    bool new_value{};
    bool cancel{};
};

// A click on a column header. The view does not sort: the application
// reorders its items (sort_items() is a helper for that) and shows the order
// with set_sort_indicator().
struct GUI_FORMS_API ListViewColumnClick final {
    std::string list_id{};
    std::size_t column{};
    std::string column_id{};
};

// A column's new width in logical units, after a divider drag ends or
// set_column_width() changes it.
struct GUI_FORMS_API ListViewColumnWidthChange final {
    std::string list_id{};
    std::size_t column{};
    double width{};
};

// Where a ListView's insertion mark stands (Windows Forms'
// ListViewInsertionMark Index and AppearsAfterItem): beside the item at
// model index `index`, before it or, with `after`, after it.
struct GUI_FORMS_API ListViewInsertionMark final {
    std::size_t index{};
    bool after{};
    friend constexpr bool operator==(const ListViewInsertionMark& left,
                                     const ListViewInsertionMark& right) noexcept {
        return left.index == right.index && left.after == right.after;
    }
};

// One label edit (Windows Forms' LabelEditEventArgs). In before_label_edit
// `label` is none, and a handler sets `cancel` to keep the item out of edit.
// In after_label_edit `label` is the edited text, or none when the edit was
// cancelled; a handler sets `cancel` to refuse the text, and the item keeps
// the text it had. A handler's change to `label` is ignored. `index` is the
// item's position when its edit began.
struct GUI_FORMS_API ListViewLabelEdit final {
    std::string list_id{};
    std::size_t index{};
    std::string item_id{};
    std::optional<std::string> label{};
    bool cancel{};
};

// A retained item list with details, list, small-icon and large-icon views
// (Windows Forms' ListView). Items are records, not child controls; only the
// visible ones are laid out, painted and described, so a virtual view of
// millions of items realizes a screenful. Every part is painted through the
// by-role paint path. Two child scroll bars scroll the view when its items
// or columns do not fit.
//
// Selection, focus and check boxes are kept by index. Replacing the items
// of an ordinary view carries the selection, the focused item and the anchor
// over by stable ID; a virtual view keeps them by index, clamped to the new
// count. Columns are resized by dragging a header's divider, items'
// labels are edited in place (label_edit), and an application may draw
// items' content itself (draw_mode), and items are shown in groups (groups,
// show_groups). Tooltips and hot-tracking activation are not supported yet.
// How a splice moves an index (ListView::splice_virtual_items): before
// `first` it stays, inside [first, removed_end) it is dropped, after it
// shifts by inserted - removed.
struct GUI_FORMS_API ListViewSpliceRemap final {
    std::size_t first{};
    std::size_t removed_end{};
    std::size_t inserted{};
    [[nodiscard]] std::optional<std::size_t> apply(std::optional<std::size_t> index) const noexcept;
};

class GUI_FORMS_API ListView final : public Control, public ListViewCapabilities<ListView> {
public:
    static constexpr bool initialize_tree_after_construction = true;
    // Development bounds, not measured capacity.
    static constexpr std::size_t maximum_items = 10'000'000U;
    static constexpr std::size_t maximum_columns = 64U;
    static constexpr double minimum_column_width = 20.0;
    static constexpr double maximum_column_width = 4096.0;
    static constexpr std::size_t maximum_groups = 4096U;

    explicit ListView(StableId stable_id);
    ~ListView() override;
    void initialize_control_tree();

    [[nodiscard]] std::span<const ListViewColumn> columns() const& noexcept {
        return std::span<const ListViewColumn>(columns_.data(), columns_.size());
    }
    std::span<const ListViewColumn> columns() const&& = delete;
    // Replaces the columns. Throws std::invalid_argument for an empty or
    // duplicate ID, invalid UTF-8, a width outside the minimum and maximum
    // column widths, or more than maximum_columns columns. Clears the sort
    // indicator when its column is gone.
    void set_columns(std::vector<ListViewColumn> columns);
    // Throws std::out_of_range for a column past the end and
    // std::invalid_argument for a width outside the bounds.
    void set_column_width(std::size_t column, double width);

    // The items of an ordinary view; empty in virtual mode.
    [[nodiscard]] BorrowedSpan<ListViewItem> items() const& noexcept { return detail::lend_view(items_); }
    BorrowedSpan<ListViewItem> items() const&& = delete;
    // Replaces the items. Throws std::invalid_argument for an empty or
    // duplicate ID or invalid UTF-8, and std::logic_error in virtual mode.
    void set_items(std::vector<ListViewItem> items);
    // The number of items, from the source in virtual mode.
    [[nodiscard]] std::size_t item_count() const noexcept;
    // The index of the item with this ID in an ordinary view; none in
    // virtual mode or when no item has it.
    [[nodiscard]] std::optional<std::size_t> index_of(std::string_view item_id) const;

    // Groups (Windows Forms' Groups, ShowGroups and DefaultGroup). While
    // show_groups is on, the view has groups, it is not virtual and not the
    // list view, it shows each group that holds items as a header line (the
    // header's text over a rule) followed by its items, in the groups'
    // order, after the default group of the items whose `group` is empty or
    // names no group. Items keep their model order within a group; keys,
    // ranges of selection and type-ahead follow the shown order. Groups do
    // not collapse. set_groups throws std::invalid_argument for an empty or
    // repeated ID, invalid UTF-8, an unknown alignment, or more than
    // maximum_groups groups.
    [[nodiscard]] std::span<const ListViewGroup> groups() const& noexcept {
        return std::span<const ListViewGroup>(groups_.data(), groups_.size());
    }
    std::span<const ListViewGroup> groups() const&& = delete;
    void set_groups(std::vector<ListViewGroup> groups);
    // Whether the items are shown in groups now.
    [[nodiscard]] bool grouped() const noexcept;

    // Virtual mode. Setting a source drops the ordinary items and reads the
    // source's count; null returns to an ordinary, empty view. Throws
    // std::invalid_argument when the count exceeds maximum_items.
    [[nodiscard]] bool virtual_mode() const noexcept { return source_ != nullptr; }
    [[nodiscard]] const std::shared_ptr<ListViewItemSource>& virtual_source() const noexcept {
        return source_;
    }
    void set_virtual_source(std::shared_ptr<ListViewItemSource> source);
    // Re-reads the source's count after the application changed its data,
    // keeps the selection, focus and anchor below the new count, and
    // repaints. Throws std::logic_error outside virtual mode.
    void refresh_virtual_items();
    // Tells the view that the source replaced `removed` items at `first`
    // with `inserted` new ones (audit N94): the bulk insertion or removal
    // of a virtual list without a refresh. The selection, focus, anchor and
    // top item stay on their items; those inside the removed range are
    // dropped. Throws std::logic_error outside virtual mode or when the
    // source's count is not the old count - removed + inserted, and
    // std::out_of_range for a range past the old items.
    void splice_virtual_items(std::size_t first, std::size_t removed, std::size_t inserted);

    // Throws std::out_of_range for an index past the end.
    [[nodiscard]] bool item_checked(std::size_t index) const;
    // Sets an ordinary item's checked state and raises item_checked when it
    // changes. Throws std::out_of_range for an index past the end and
    // std::logic_error in virtual mode, where the source owns it.
    void set_item_checked(std::size_t index, bool checked);

    [[nodiscard]] std::size_t selected_count() const noexcept { return selected_count_; }
    // The selected indices in ascending order.
    [[nodiscard]] std::vector<std::size_t> selected_indices() const;
    // Throws std::out_of_range for an index past the end.
    [[nodiscard]] bool is_selected(std::size_t index) const;
    // Selects or deselects one item, keeping the others (in single-selection
    // mode selecting an item deselects the rest). Throws std::out_of_range.
    void set_selected(std::size_t index, bool selected);
    void select_all();
    void clear_selection();
    // The item with the focus cue and the keyboard's position, if any.
    [[nodiscard]] std::optional<std::size_t> focused_index() const noexcept { return focused_index_; }
    // Throws std::out_of_range for an index past the end.
    void set_focused_index(std::optional<std::size_t> index);

    // The first item laid out in the visible area.
    [[nodiscard]] std::size_t top_index() const;
    // Scrolls so the item is fully visible. Throws std::out_of_range.
    void ensure_visible(std::size_t index);
    // The details view's sideways scroll, in logical units.
    [[nodiscard]] double horizontal_offset() const noexcept { return horizontal_offset_; }
    void set_horizontal_offset(double offset);

    // The sort glyph shown on one column header. The view never reorders
    // items for it. Throws std::out_of_range for a column past the end.
    [[nodiscard]] std::optional<std::size_t> sort_column() const noexcept { return sort_column_; }
    [[nodiscard]] ListViewSortOrder sort_order() const noexcept { return sort_order_; }
    void set_sort_indicator(std::optional<std::size_t> column, ListViewSortOrder order);
    // A helper the application may call from column_click: stable-sorts an
    // ordinary view's items by one column's text (ASCII case folded, then
    // byte order) and shows the sort glyph. The selection and focus follow
    // their items. Throws std::out_of_range for a column past the end and
    // std::logic_error in virtual mode.
    void sort_items(std::size_t column, ListViewSortOrder order);

    // The bounds of each side of a set tile_size, in logical units at text
    // scale 1.
    static constexpr double minimum_tile_side = 24.0;
    static constexpr double maximum_tile_side = 2048.0;

    // Raised for each owner-drawn content while draw_mode is
    // ItemDrawMode::owner_draw (see draw_mode).
    [[nodiscard]] Event<ItemContentPaint&>& draw_item_content() noexcept { return draw_item_content_; }

    // The item under a point in window coordinates, if any.
    [[nodiscard]] std::optional<std::size_t> item_index_at(Point window_point) const;

    // The insertion mark (Windows Forms' InsertionMark), for an application
    // that reorders items by dragging: a bar in the gap beside an item, the
    // `insertion_mark` part, painted by the drop role, until
    // clear_insertion_mark() or any change to the items or their order.
    // Before or after the item is above or below it in the details and list
    // views, and left or right of it in the icon views. False, with any mark
    // cleared, for an index past the items; true otherwise, also while the
    // item is scrolled out of view (the mark shows when it returns). The
    // mark's colour is the theme's: Windows Forms' InsertionMark.Color is
    // not admitted.
    bool show_insertion_mark(ListViewInsertionMark mark);
    void clear_insertion_mark() noexcept;
    [[nodiscard]] std::optional<ListViewInsertionMark> insertion_mark() const noexcept { return insertion_mark_; }
    // The mark nearest a point in window coordinates (Windows Forms'
    // InsertionMark.NearestIndex): beside the shown item under the point,
    // or nearest it once the point is clamped into the viewport, after the
    // item when the point is past the item's middle across the gap. None
    // when the view shows no items.
    [[nodiscard]] std::optional<ListViewInsertionMark> insertion_mark_at(Point window_point) const;

    // Begins editing the label of the item at `index`, after committing any
    // other edit in progress: raises before_label_edit, scrolls the item into
    // view and focuses the editor. True when the item is in edit (also when
    // it already was); false when label_edit is off, the view does not show
    // the item (a details view without columns), or a handler cancelled,
    // changed the items or disposed the view. Throws std::out_of_range for an
    // index past the end.
    bool begin_label_edit(std::size_t index);
    // Commits the edit in progress, if any.
    void end_label_edit();
    // Cancels the edit in progress, if any.
    void cancel_label_edit();
    // The item in edit, if any.
    [[nodiscard]] std::optional<std::size_t> label_edit_index() const noexcept { return label_edit_index_; }
    // The editor; null until the view's first edit.
    [[nodiscard]] std::shared_ptr<TextBox> label_editor() const noexcept { return label_editor_; }

    [[nodiscard]] Event<const ListViewSelectionChange&>& selection_changed() noexcept {
        return selection_changed_;
    }
    [[nodiscard]] Event<const ListViewItemActivation&>& item_activated() noexcept {
        return item_activated_;
    }
    [[nodiscard]] Event<ListViewItemCheck&>& item_checking() noexcept { return item_checking_; }
    [[nodiscard]] Event<const ListViewItemCheck&>& item_checked() noexcept { return item_checked_; }
    [[nodiscard]] Event<const ListViewColumnClick&>& column_click() noexcept { return column_click_; }
    [[nodiscard]] Event<const ListViewColumnWidthChange&>& column_width_changed() noexcept {
        return column_width_changed_;
    }
    [[nodiscard]] Event<ListViewLabelEdit&>& before_label_edit() noexcept { return before_label_edit_; }
    [[nodiscard]] Event<ListViewLabelEdit&>& after_label_edit() noexcept { return after_label_edit_; }

    void arrange(Rect final_bounds) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    void on_key_preview(KeyEvent& event) override;
    void on_text_input(TextInputEvent& event) override;
    void on_focus_changed(bool focused) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] std::vector<SemanticNode> semantic_virtual_children() const override;
    bool on_semantic_child_action(std::string_view stable_id, SemanticAction action,
                                  std::string_view value) override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    [[nodiscard]] std::size_t realized_item_count() const override;
    [[nodiscard]] std::string realized_item_stable_id(std::size_t item) const override;
    [[nodiscard]] AnatomyStateSet realized_item_anatomy_states(std::size_t item) const override;

    // Stage T1 anatomy, as dot members; its states are Control's.
    struct GUI_FORMS_API Parts final {
        PartGeometry region;
        PartGeometry well;
        PartGeometry header;
        PartGeometry vertical_scroll_bar;
        PartGeometry horizontal_scroll_bar;
        PartGeometry scroll_corner;
        PartGeometry focus;
        // The bar of the insertion mark; absent without a shown mark.
        PartGeometry insertion_mark;
        // The TextBox of a label edit; absent while no item is in edit.
        PartGeometry label_editor;
    };
    [[nodiscard]] Parts parts() const;

    // The "item" item system: one realized item (a details row, or a cell of
    // the other views).
    struct GUI_FORMS_API ItemParts final {
        PartGeometry cell;
        PartGeometry item_face;
        PartGeometry check_well;
        PartGeometry check;
        PartGeometry icon;
        PartGeometry text;
        PartGeometry cell_focus;
    };
    struct GUI_FORMS_API ItemStates final {
        bool selected{};
        bool hot{};
        bool current{};
        bool checked{};
        bool disabled{};
        bool focus_visible{};
    };
    struct GUI_FORMS_API Item final {
        // The item's position in the model.
        std::size_t index{};
        std::string stable_id;
        ItemParts parts;
        ItemStates states;
    };
    // The realized items in paint order.
    [[nodiscard]] std::vector<Item> realized_items() const;

    // The "sub_item" item system: one visible details-view cell past the
    // first column. Its identity is the item's ID, ".sub.", and the column's.
    struct GUI_FORMS_API SubItemParts final {
        PartGeometry sub_item_cell;
        PartGeometry sub_item_text;
    };
    struct GUI_FORMS_API SubItemStates final {
        bool selected{};
        bool disabled{};
    };
    struct GUI_FORMS_API SubItem final {
        std::size_t index{};
        std::size_t column{};
        std::string stable_id;
        SubItemParts parts;
        SubItemStates states;
    };
    [[nodiscard]] std::vector<SubItem> realized_sub_items() const;

    // The "column" item system, named as ObjectView's: one visible column
    // header of the details view. Its identity is the view's stable ID,
    // ".column.", and the column's ID.
    struct GUI_FORMS_API ColumnParts final {
        PartGeometry column_header;
        PartGeometry column_label;
        PartGeometry sort_glyph;
        PartGeometry column_divider;
        PartGeometry column_grip;
    };
    struct GUI_FORMS_API ColumnStates final {
        bool hot{};
        bool pressed{};
        bool captured{};
        // Application-defined column states.
        bool sorted_ascending{};
        bool sorted_descending{};
    };
    struct GUI_FORMS_API Column final {
        std::size_t index{};
        std::string stable_id;
        ColumnParts parts;
        ColumnStates states;
    };
    [[nodiscard]] std::vector<Column> realized_columns() const;

    // The "group" item system: one shown group header line. Its identity is
    // the view's stable ID, ".group.", and the group's ID; the default
    // group's is the view's stable ID and ".default-group". `index` is the
    // group's position in groups(), or groups().size() for the default
    // group.
    struct GUI_FORMS_API GroupParts final {
        PartGeometry group_band;
        PartGeometry group_caption;
        PartGeometry group_divider;
    };
    struct GUI_FORMS_API GroupStates final {
        bool disabled{};
    };
    struct GUI_FORMS_API Group final {
        std::size_t index{};
        std::string stable_id;
        GroupParts parts;
        GroupStates states;
    };
    [[nodiscard]] std::vector<Group> realized_groups() const;

protected:
    void on_key_down(KeyEventArgs& arguments) override;

    void on_mouse_down(MouseEventArgs& arguments) override;
    void on_mouse_up(MouseEventArgs& arguments) override;
    void on_mouse_move(MouseEventArgs& arguments) override;
    void on_mouse_wheel(MouseEventArgs& arguments) override;
    void on_mouse_leave() override;

    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void on_attached_to_window() override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;
    [[nodiscard]] ItemSystemIndex publish_realized_item_system(std::size_t item) const override;
    void publish_realized_item_geometry(std::size_t item, std::span<PartGeometry> geometry) const override;

private:
    // One group shown by a grouped view, in display order: a header line
    // and then the lines of its items.
    struct GroupBand final {
        std::size_t group{};      // its position in groups_, or groups_.size() for the default group
        std::size_t first{};      // its first item's display position
        std::size_t count{};      // its items
        std::size_t first_line{}; // its header line
    };
    // Where everything sits for the current bounds, view and model, from one
    // pass (see layout()). A "line" is the unit the view scrolls by: a row
    // of the details and icon views, a column of items in the list view.
    struct Layout final {
        Rect well{};
        Rect header{};          // the details view's header band; empty otherwise
        Rect viewport{};        // where items are shown
        Rect vertical_bar{};    // empty when not shown
        Rect horizontal_bar{};
        Rect corner{};          // between two shown bars
        // Right to left: every rectangle here is in reading order and is
        // mirrored about `well` where it is drawn, published or placed.
        bool mirrored{};
        double scale{1.0};
        double row_height{};    // details, list and small-icon rows
        double text_line{};     // one line of the font's text
        std::size_t tile_lines{1U}; // the lines of text a tile has room for
        Size cell{};            // one item's cell; a details row spans the columns
        double columns_width{}; // the details columns' total scaled width
        double horizontal_offset{}; // clamped, logical units
        std::size_t shown_count{};  // items shown: none in a details view without columns
        std::size_t per_line{1U};   // items in one line
        std::size_t line_count{};
        std::size_t page_lines{1U}; // fully visible lines
        std::size_t visible_lines{}; // lines at least partly visible
        std::size_t top_line{};     // clamped
        // A grouped view's groups and header line extent; a grouped view's
        // lines differ in extent, so its offsets come from line_offset().
        bool grouped{};
        double header_extent{};
        std::vector<GroupBand> bands{};
    };
    // Which part of the view a point is on.
    enum class HitZone : std::uint8_t { none, item, check, header, divider };
    struct Hit final {
        HitZone zone{HitZone::none};
        std::size_t index{};
    };
    using ItemIndices = std::unordered_map<std::string, std::size_t>;

    // The layout at the current key, kept (S5): made again only when the
    // key (Control::LayoutKey, and the model revision where one is read)
    // has moved. The reference is valid until the next layout().
    [[nodiscard]] const Layout& layout() const;
    [[nodiscard]] Layout layout_for(Rect client) const;
    mutable Layout kept_layout_{};
    mutable LayoutKey kept_layout_key_{};
    mutable std::uint64_t kept_layout_model_{};
    mutable bool kept_layout_valid_{};
    [[nodiscard]] double text_line_height() const;
    [[nodiscard]] Size icon_size(bool large) const noexcept;
    [[nodiscard]] std::size_t maximum_top_line(const Layout& placed) const noexcept;
    // Display positions: the order the items are shown in. Ungrouped, a
    // position is the item's index.
    void refresh_display_order() const;
    [[nodiscard]] std::size_t index_at(const Layout& placed, std::size_t position) const noexcept;
    [[nodiscard]] std::size_t position_of(const Layout& placed, std::size_t index) const noexcept;
    // A grouped view's lines.
    void place_groups(Layout& placed) const;
    [[nodiscard]] std::size_t band_of_line(const Layout& placed, std::size_t line) const noexcept;
    [[nodiscard]] std::size_t band_of_position(const Layout& placed, std::size_t position) const noexcept;
    [[nodiscard]] bool header_line(const Layout& placed, std::size_t line) const noexcept;
    // The first display position on an item line, and its item count.
    [[nodiscard]] std::size_t line_first(const Layout& placed, std::size_t line) const noexcept;
    [[nodiscard]] std::size_t line_items(const Layout& placed, std::size_t line) const noexcept;
    [[nodiscard]] std::size_t position_line(const Layout& placed, std::size_t position) const noexcept;
    // The extent of the lines before `line`, along the scroll.
    [[nodiscard]] double line_offset(const Layout& placed, std::size_t line) const noexcept;
    // The line holding an offset along the scroll, the last when past it.
    [[nodiscard]] std::size_t line_at_offset(const Layout& placed, double offset) const noexcept;
    // The item line after (forward) or before `line`, skipping headers;
    // `line` itself at an end.
    [[nodiscard]] std::size_t next_item_line(const Layout& placed, std::size_t line, bool forward) const noexcept;
    // Scrolls so `line` is the top line.
    void store_top_line(const Layout& placed, std::size_t line);
    [[nodiscard]] std::optional<std::size_t> grouped_navigation_target(std::uint32_t key, std::size_t from,
                                                                       const Layout& placed) const;
    [[nodiscard]] std::size_t realized_group_count(const Layout& placed) const noexcept;
    // The band of the realized group at `position` among the shown headers.
    [[nodiscard]] std::size_t realized_group_band(const Layout& placed, std::size_t position) const noexcept;
    [[nodiscard]] GroupParts group_parts(const Layout& placed, std::size_t band, Rect clip) const;
    [[nodiscard]] std::string group_identity(std::size_t group) const;
    [[nodiscard]] const std::string& group_header(std::size_t group) const noexcept;
    void paint_group(Painter& painter, const Layout& placed, std::size_t band);
    // The realized items: display positions [first, end).
    [[nodiscard]] std::size_t first_realized(const Layout& placed) const noexcept;
    [[nodiscard]] std::size_t end_realized(const Layout& placed) const noexcept;
    [[nodiscard]] Rect item_bounds(const Layout& placed, std::size_t index) const noexcept;
    [[nodiscard]] Rect column_cell(const Layout& placed, std::size_t column) const noexcept;
    [[nodiscard]] bool column_visible(const Layout& placed, std::size_t column) const noexcept;
    void append_row_cells(const Layout& placed, std::size_t index, const ListViewItem& item,
                          SemanticNode& row_node) const;
    [[nodiscard]] std::size_t visible_column_count(const Layout& placed) const noexcept;
    [[nodiscard]] std::size_t visible_column(const Layout& placed, std::size_t position) const noexcept;
    [[nodiscard]] ItemParts item_parts(const Layout& placed, std::size_t index, Rect clip) const;
    [[nodiscard]] SubItemParts sub_item_parts(const Layout& placed, std::size_t index,
                                              std::size_t column, Rect clip) const;
    [[nodiscard]] ColumnParts column_parts(const Layout& placed, std::size_t column, Rect clip) const;
    [[nodiscard]] AnatomyStateSet item_states(std::size_t index, bool checked) const;
    [[nodiscard]] AnatomyStateSet column_states(std::size_t column) const;
    // The item at `index`: a reference into the ordinary items, or `scratch`
    // filled by the source in virtual mode. Valid until `scratch` is reused
    // or the model changes.
    [[nodiscard]] const ListViewItem& item_at(std::size_t index, ListViewItem& scratch) const;
    [[nodiscard]] Hit hit_test(const Layout& placed, Point local) const;
    // Reveals the item text under `local` in place when its cell cuts it
    // off (the details and list views, default drawing only), else
    // conceals any reveal (detail::CutOffTextRevealer).
    void reveal_cut_off_text(const Layout& placed, const Hit& hit, Point local);
    void conceal_cut_off_text() noexcept;
    // What one realized-item position names. The realized items are the
    // visible rows (item system "item"), then each row's visible sub-item
    // cells, row by row (item system "sub_item"), then the visible column
    // headers (item system "column"). `index` is the model index of a row or
    // sub-item's item; `column` the column of a sub-item or header.
    struct RealizedSlot final {
        ItemSystemIndex system{};
        std::size_t index{};
        std::size_t column{};
    };
    [[nodiscard]] RealizedSlot realized_slot(const Layout& placed, std::size_t item) const;
    // The counts of the first two runs of realized items, and the column of
    // the sub-item at `position` within one row's run.
    [[nodiscard]] std::size_t realized_row_count(const Layout& placed) const noexcept;
    [[nodiscard]] std::size_t realized_sub_item_count(const Layout& placed) const noexcept;
    [[nodiscard]] std::size_t realized_sub_item_column(const Layout& placed, std::size_t position) const noexcept;

    // Paints one item; false when an owner-draw handler changed the model
    // or disposed the view, so that the paint stops.
    [[nodiscard]] bool paint_item(Painter& painter, const Layout& placed, std::size_t index);
    [[nodiscard]] bool paint_tile_lines(Painter& painter, const Layout& placed, std::size_t index,
                                        const ItemParts& parts, AnatomyStateSet states);
    // Whether the view is alive and its model is the one `revision` named
    // before an owner-draw handler ran.
    [[nodiscard]] bool model_unchanged(std::uint64_t revision) const noexcept;
    void paint_column(Painter& painter, const Layout& placed, std::size_t column);
    void paint_shown(Painter& painter, const Layout& placed, PartPaint part);

    void set_top_line(std::size_t line);
    void scroll_lines(std::ptrdiff_t lines);
    void sync_scroll_bars(const Layout& placed);
    void on_vertical_scroll(double value);
    void on_horizontal_scroll(double value);
    void image_list_changed(const ImageListChange& change);
    // Throws std::invalid_argument unless `images` is null or a live list of
    // the view's window (any window before the view is in one).
    void require_window_image_list(const std::shared_ptr<ImageList>& images) const;
    // Replaces `subscription` with one to `images`' changes (none for null)
    // and invalidates the view.
    void watch_image_list(const std::shared_ptr<ImageList>& images, SubscriptionToken& subscription);

    // Selection operations. Each returns whether the selected set changed.
    [[nodiscard]] bool select_only(std::size_t index);
    [[nodiscard]] bool select_none();
    [[nodiscard]] bool toggle_selected(std::size_t index);
    [[nodiscard]] bool select_range(std::size_t anchor, std::size_t index, bool keep_existing);
    [[nodiscard]] bool assign_selected(std::size_t index, bool selected);
    void publish_selection();
    // A pointer press or a navigation key on `index`, with its modifiers.
    void choose(std::size_t index, Modifier modifiers, bool move_only);
    void move_focus(std::size_t index);
    void toggle_check(std::size_t index);
    void activate(std::size_t index);
    void request_column_click(std::size_t column);
    void finish_column_resize();
    [[nodiscard]] Rect insertion_mark_bounds(const Layout& placed) const noexcept;
    // Label editing. label_editor_bounds is the edited item's text part,
    // empty while the item is scrolled out of view.
    [[nodiscard]] Rect label_editor_bounds(const Layout& placed) const;
    void place_label_editor(const Layout& placed);
    void create_label_editor();
    [[nodiscard]] ListViewLabelEdit label_edit_event(std::optional<std::string> label) const;
    // Hides the editor and leaves edit, returning focus to the view when the
    // editor held it. Raises no label event.
    void close_label_editor();
    void on_label_editor_focus(bool focused);
    // The slow second click (Windows' list view): a single press on the text
    // of the item that was already focused and selected begins its label
    // edit once the double-click interval passes without a second press.
    struct LabelClickElapsed;
    struct PendingLabelClick final {
        std::size_t index{};
        // The model revision when pressed: a changed model drops the click.
        std::uint64_t revision{};
    };
    void schedule_label_click(std::size_t index);
    void cancel_label_click() noexcept;
    void label_click_elapsed();
    [[nodiscard]] std::optional<std::size_t> navigation_target(std::uint32_t key, std::size_t from,
                                                               const Layout& placed) const;
    void type_select(std::string_view text);
    [[nodiscard]] std::optional<std::size_t> realized_index_of(std::string_view item_id) const;
    void reset_after_model_change(std::size_t count);

    std::vector<ListViewColumn> columns_{};
    detail::LendingVector<ListViewItem> items_{};
    ItemIndices item_indices_{};
    std::shared_ptr<ListViewItemSource> source_{};
    std::size_t virtual_count_{};
    // One flag per item: nonzero when selected.
    std::vector<std::uint8_t> selected_{};
    std::size_t selected_count_{};
    std::optional<std::size_t> focused_index_{};
    std::optional<std::size_t> anchor_index_{};
    std::optional<std::size_t> hot_index_{};
    std::optional<std::size_t> hot_column_{};
    // The in-place reveal of the hot item's cut-off text; owned, created
    // with the view.
    std::unique_ptr<detail::CutOffTextRevealer> reveal_;
    std::optional<ListViewInsertionMark> insertion_mark_{};
    // Set while close_label_editor moves focus, so that the editor's focus
    // loss does not end the edit a second time.
    bool closing_label_editor_{};
    std::optional<std::size_t> label_edit_index_{};
    std::string label_edit_item_id_{};
    std::shared_ptr<TextBox> label_editor_{};
    SubscriptionToken label_editor_focus_{};
    std::optional<PendingLabelClick> label_click_{};
    FrameRequestToken label_click_timer_{};
    std::optional<std::size_t> pressed_column_{};
    std::optional<std::size_t> resizing_column_{};
    double resize_start_x_{};
    double resize_start_width_{};
    // The first visible item's display position; the layout shows the line
    // holding it, or with top_header_ its group's header line above it.
    std::size_t top_item_{};
    bool top_header_{};
    std::vector<ListViewGroup> groups_{};
    // The grouped display order, rebuilt when model_revision_ moves:
    // display_order_[position] is an index, display_position_[index] a
    // position, and display_counts_[group] the items of each group in
    // groups_ (the default group's last).
    mutable std::vector<std::size_t> display_order_{};
    mutable std::vector<std::size_t> display_position_{};
    mutable std::vector<std::size_t> display_counts_{};
    mutable std::uint64_t display_revision_{};
    mutable bool display_valid_{};
    double horizontal_offset_{};
    std::optional<std::size_t> sort_column_{};
    ListViewSortOrder sort_order_{ListViewSortOrder::none};
    // Moves with every change to the items, their order, the columns or the
    // view, so that a paint can tell an owner-draw handler changed them.
    std::uint64_t model_revision_{};
    bool focused_{};
    bool synchronizing_scroll_{};
    SubscriptionToken small_images_changed_{};
    SubscriptionToken large_images_changed_{};
    std::shared_ptr<VScrollBar> vertical_bar_{};
    std::shared_ptr<HScrollBar> horizontal_bar_{};
    SubscriptionToken vertical_scrolled_{};
    SubscriptionToken horizontal_scrolled_{};
    // Paint scratch, reused for every item of a paint pass.
    ListViewItem paint_item_{};
    std::string paint_text_{};
    // The painted item's identity while owner-draw handlers run.
    std::string paint_stable_id_{};
    // Type-ahead.
    std::string type_prefix_{};
    std::chrono::steady_clock::time_point last_type_time_{};
    Event<const ListViewSelectionChange&> selection_changed_{};
    Event<const ListViewItemActivation&> item_activated_{};
    Event<ListViewItemCheck&> item_checking_{};
    Event<const ListViewItemCheck&> item_checked_{};
    Event<const ListViewColumnClick&> column_click_{};
    Event<const ListViewColumnWidthChange&> column_width_changed_{};
    Event<ListViewLabelEdit&> before_label_edit_{};
    Event<ListViewLabelEdit&> after_label_edit_{};
    Event<ItemContentPaint&> draw_item_content_{};
    // property_codemod begin ListView (tools/property_families/list_view.json; generated, do not edit)
protected:
    // on_<property>_changed runs after a changed write's store and touch and
    // raises the change event; an override calls the base. Only the
    // properties with a Windows Forms On<Property>Changed have one.
    virtual void on_font_changed();

private:
    // The routed form of a key the raisers left unhandled.
    void handle_key(KeyEvent& event);
    // The target phase in the routed form, from the mouse raisers.
    void handle_pointer(PointerEvent& event);
    friend struct ListViewViewCapability<ListView>;
    friend struct ListViewCheckBoxesCapability<ListView>;
    friend struct ListViewMultiSelectCapability<ListView>;
    friend struct ListViewFullRowSelectCapability<ListView>;
    friend struct SmallImageListCapability<ListView>;
    friend struct LargeImageListCapability<ListView>;
    friend struct FontCapability<ListView>;
    friend struct TileSizeCapability<ListView>;
    friend struct ListViewDrawModeCapability<ListView>;
    friend struct ShowGroupsCapability<ListView>;
    friend struct DefaultGroupHeaderCapability<ListView>;
    friend struct ListViewLabelEditCapability<ListView>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(view)& cell, ListViewView& incoming);
    void written(const decltype(view)& cell);
    void prepare_write(const decltype(check_boxes)& cell, bool& incoming);
    void written(const decltype(check_boxes)& cell);
    void prepare_write(const decltype(multi_select)& cell, bool& incoming);
    void written(const decltype(multi_select)& cell);
    void prepare_write(const decltype(full_row_select)& cell, bool& incoming);
    void written(const decltype(full_row_select)& cell);
    void prepare_write(const decltype(small_image_list)& cell, std::shared_ptr<ImageList>& incoming);
    void written(const decltype(small_image_list)& cell);
    void prepare_write(const decltype(large_image_list)& cell, std::shared_ptr<ImageList>& incoming);
    void written(const decltype(large_image_list)& cell);
    void prepare_write(const decltype(font)& cell, FontSpec& incoming);
    void written(const decltype(font)& cell);
    void prepare_write(const decltype(tile_size)& cell, Size& incoming);
    void written(const decltype(tile_size)& cell);
    void prepare_write(const decltype(draw_mode)& cell, ItemDrawMode& incoming);
    void written(const decltype(draw_mode)& cell);
    void prepare_write(const decltype(show_groups)& cell, bool& incoming);
    void written(const decltype(show_groups)& cell);
    void prepare_write(const decltype(default_group_header)& cell, std::string& incoming);
    void written(const decltype(default_group_header)& cell);
    void prepare_write(const decltype(label_edit)& cell, bool& incoming);
    void written(const decltype(label_edit)& cell);
    // property_codemod end ListView
};

} // namespace gui_forms
