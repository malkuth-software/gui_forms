#pragma once

#include "gui_forms/control.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/image_list.hpp"
#include "gui_forms/text/localizable/localizable_property.hpp"
#include "gui_forms/controls/panel/tree_list_view/tree_list_view_capabilities/tree_list_view_capabilities.hpp"

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
class VScrollBar;

// How a column's text sits in its cells. The tree column's text always
// starts after the indentation, the expander and the icon.
enum class TreeListAlignment : std::uint8_t { left, center, right };

// The order siblings are shown in, or none (the order of the nodes as
// given).
enum class TreeListSortOrder : std::uint8_t { none, ascending, descending };

// One column. `stable_id` is unique within the view and names the column in
// the published anatomy and semantics. `width` is in logical units at text
// scale 1. Column 0 is the tree column: it shows each node's `text` with the
// indentation and the expander, and it always stays first.
struct TreeListColumn final {
    std::string stable_id{};
    GUI_FORMS_LOCALIZABLE(header) std::string text{};
    double width{120.0};
    TreeListAlignment alignment{TreeListAlignment::left};
};

// One node of the tree. Nodes are given in preorder with their depth, as
// TreeView's items are: a node's children follow it directly, one level
// deeper. `text` is shown in the tree column and `cells[n]` in column n + 1;
// cells past the last column are kept but not shown. A node with children
// can always be expanded; `expandable` shows the expander on a node whose
// children are not loaded yet. `expanded` is ignored on a node that cannot
// be expanded. `image_key` names an image in the image list; an empty or
// unknown key shows none.
struct TreeListNode final {
    std::string stable_id{};
    std::string text{};
    std::vector<std::string> cells{};
    std::size_t depth{};
    bool expandable{};
    bool expanded{};
    bool enabled{true};
    std::string image_key{};
};

// A change of the selected node, after it happened.
struct TreeListSelectionChange final {
    std::optional<std::string> previous_id{};
    std::optional<std::string> current_id{};
};

// A node expanded or collapsed, by the user or the program.
struct TreeListExpansionChange final {
    std::string node_id{};
    bool expanded{};
};

// A node activated by a double click, Enter, or the semantic press action.
struct TreeListActivation final {
    std::string node_id{};
};

// The sibling order after a header click, the semantic press on a header,
// or set_sort().
struct TreeListSortChange final {
    std::optional<std::size_t> column{};
    TreeListSortOrder order{TreeListSortOrder::none};
};

// A column's new width in logical units, after a divider drag ends or
// set_column_width() changes it.
struct TreeListColumnWidthChange final {
    std::size_t column{};
    double width{};
};

// A column moved to a new display position, by a header drag or
// move_column(). Display position 0 always holds the tree column.
struct TreeListColumnMove final {
    std::size_t column{};
    std::size_t display_position{};
};

// A tree with tabular columns: TreeView's flat model of visible rows with
// the details columns of ListView and ObjectView. Nodes are records, not
// child controls, kept and found by stable ID; only the rows in view are
// laid out, painted and described, so a tree of a million nodes realizes a
// screenful. Every part is painted through the by-role paint path. Two child
// scroll bars scroll the view when its rows or columns do not fit.
//
// Selection (one node) and the keyboard's current node are kept by node,
// not by row: they stay on their node when rows move through expansion,
// collapse, sorting and edits, and set_nodes() carries them over by stable
// ID. A collapse that hides the selected or current node moves it to the
// collapsed node, as TreeView does. Sorting orders siblings by one column's
// text and keeps the tree structure. Columns are resized by dragging a
// header's divider and moved by dragging a header; the tree column stays
// first. Multiple selection, check boxes, label editing, owner drawing and a
// virtual node source are not supported yet.
class TreeListView final : public Control, public TreeListViewCapabilities<TreeListView> {
public:
    static constexpr bool initialize_tree_after_construction = true;
    // Development bounds, not measured capacity.
    static constexpr std::size_t maximum_nodes = 10'000'000U;
    static constexpr std::size_t maximum_columns = 64U;
    static constexpr double minimum_column_width = 20.0;
    static constexpr double maximum_column_width = 4096.0;
    static constexpr double minimum_indentation = 8.0;
    static constexpr double maximum_indentation = 96.0;

    explicit TreeListView(StableId stable_id);
    ~TreeListView() override;
    void initialize_control_tree();

    // -- columns -------------------------------------------------------------
    [[nodiscard]] std::span<const TreeListColumn> columns() const noexcept { return columns_; }
    // Replaces the columns and resets the display order. Throws
    // std::invalid_argument for an empty or duplicate ID, invalid UTF-8, a
    // width outside the column width bounds, or more than maximum_columns
    // columns. Clears the sort when its column is gone. The view shows no
    // rows without columns.
    void set_columns(std::vector<TreeListColumn> columns);
    // Throws std::out_of_range for a column past the end and
    // std::invalid_argument for a width outside the bounds.
    void set_column_width(std::size_t column, double width);
    // The columns in display order: column_order()[position] is the column
    // shown at that position.
    [[nodiscard]] std::span<const std::size_t> column_order() const noexcept { return column_order_; }
    // Moves a column to a display position and raises column_moved. Throws
    // std::out_of_range for a column or position past the end and
    // std::invalid_argument for the tree column or position 0.
    void move_column(std::size_t column, std::size_t display_position);

    // -- nodes ---------------------------------------------------------------
    // The nodes in the order given (model order), whatever the sort.
    [[nodiscard]] std::span<const TreeListNode> nodes() const noexcept { return nodes_; }
    // Replaces the nodes. Throws std::invalid_argument for an empty or
    // duplicate ID, invalid UTF-8, a depth sequence that is not a preorder
    // (the first node at depth 0, each next node at most one level deeper)
    // or more than maximum_nodes nodes; the old nodes then stay. The
    // selection and the current node stay on their IDs when those survive.
    void set_nodes(std::vector<TreeListNode> nodes);
    // The model index of the node with this ID, or none.
    [[nodiscard]] std::optional<std::size_t> index_of(std::string_view node_id) const;
    // Edits one cell: column 0 is the node's text. Siblings are sorted again
    // when the view is sorted by that column. Throws std::out_of_range for an
    // unknown node or a column past the end and std::invalid_argument for
    // invalid UTF-8.
    void set_cell_text(std::string_view node_id, std::size_t column, std::string text);

    // -- expansion -------------------------------------------------------------
    // Throws std::out_of_range for an unknown node.
    [[nodiscard]] bool is_expanded(std::string_view node_id) const;
    // Expands or collapses one node and raises expansion_changed when it
    // changes. A node that cannot be expanded is left alone. Throws
    // std::out_of_range for an unknown node.
    void set_expanded(std::string_view node_id, bool expanded);
    // Expands the node and every expandable node under it (the asterisk
    // key), raising expansion_changed for each in preorder.
    void expand_subtree(std::string_view node_id);

    // -- rows ------------------------------------------------------------------
    // The rows: the nodes not hidden under a collapsed node, in sorted order.
    [[nodiscard]] std::size_t row_count() const noexcept { return visible_.size(); }
    // The row showing a node, or none when it is hidden.
    [[nodiscard]] std::optional<std::size_t> row_of(std::string_view node_id) const;
    // The ID of the node a row shows. Throws std::out_of_range for a row
    // past the end. The view is borrowed until its nodes change.
    [[nodiscard]] std::string_view row_node_id(std::size_t row) const;
    // The first row in view.
    [[nodiscard]] std::size_t top_row() const;
    // Scrolls a row to the top, clamped so the last page stays full.
    void set_top_row(std::size_t row);
    // Expands the node's ancestors and scrolls it fully into view. Throws
    // std::out_of_range for an unknown node.
    void ensure_visible(std::string_view node_id);
    // The sideways scroll, in logical units.
    [[nodiscard]] double horizontal_offset() const noexcept { return horizontal_offset_; }
    void set_horizontal_offset(double offset);

    // The node with the keyboard's position and the focus cue, or none.
    [[nodiscard]] std::optional<std::string_view> current_id() const noexcept;

    // -- sorting -----------------------------------------------------------------
    [[nodiscard]] std::optional<std::size_t> sort_column() const noexcept { return sort_column_; }
    [[nodiscard]] TreeListSortOrder sort_order() const noexcept { return sort_order_; }
    // Orders every node's children (and the roots) by one column's text,
    // ASCII case folded, then byte order; equal texts keep the model order.
    // No column or TreeListSortOrder::none returns to the model order. The
    // header shows the order with a sort glyph. Raises sort_changed when the
    // sort changes. Throws std::out_of_range for a column past the end.
    void set_sort(std::optional<std::size_t> column, TreeListSortOrder order);

    // The node under a point in window coordinates, if any.
    [[nodiscard]] std::optional<std::string> node_id_at(Point window_point) const;

    [[nodiscard]] Event<const TreeListSelectionChange&>& selection_changed() noexcept {
        return selection_changed_;
    }
    [[nodiscard]] Event<const TreeListExpansionChange&>& expansion_changed() noexcept {
        return expansion_changed_;
    }
    [[nodiscard]] Event<const TreeListActivation&>& item_activated() noexcept { return item_activated_; }
    [[nodiscard]] Event<const TreeListSortChange&>& sort_changed() noexcept { return sort_changed_; }
    [[nodiscard]] Event<const TreeListColumnWidthChange&>& column_width_changed() noexcept {
        return column_width_changed_;
    }
    [[nodiscard]] Event<const TreeListColumnMove&>& column_moved() noexcept { return column_moved_; }

    void arrange(Rect final_bounds) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    void on_pointer(PointerEvent& event) override;
    void on_key(KeyEvent& event) override;
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
    struct Parts final {
        PartGeometry region;
        PartGeometry well;
        PartGeometry header;
        PartGeometry vertical_scroll_bar;
        PartGeometry horizontal_scroll_bar;
        PartGeometry scroll_corner;
        PartGeometry focus;
    };
    [[nodiscard]] Parts parts() const;

    // The "row" item system: one realized row. Its identity is the node's.
    struct RowParts final {
        PartGeometry row;
        PartGeometry row_face;
        PartGeometry expander;
        PartGeometry icon;
        PartGeometry text;
        PartGeometry row_focus;
    };
    struct RowStates final {
        bool selected{};
        bool hot{};
        bool current{};
        bool open{};
        bool closed{};
        bool disabled{};
        bool focus_visible{};
    };
    struct Row final {
        // The row's position among the rows, and the node's model index.
        std::size_t row{};
        std::size_t index{};
        std::string stable_id;
        RowParts parts;
        RowStates states;
    };
    // The realized rows in paint order.
    [[nodiscard]] std::vector<Row> realized_rows() const;

    // The "cell" item system: one visible cell past the tree column. Its
    // identity is the node's ID, ".cell.", and the column's.
    struct CellParts final {
        PartGeometry cell;
        PartGeometry cell_text;
    };
    struct CellStates final {
        bool selected{};
        bool disabled{};
    };
    struct Cell final {
        std::size_t row{};
        std::size_t column{};
        std::string stable_id;
        CellParts parts;
        CellStates states;
    };
    [[nodiscard]] std::vector<Cell> realized_cells() const;

    // The "column" item system, named as ObjectView's and ListView's: one
    // visible column header. Its identity is the view's stable ID,
    // ".column.", and the column's ID.
    struct ColumnParts final {
        PartGeometry column_header;
        PartGeometry column_label;
        PartGeometry sort_glyph;
        PartGeometry column_divider;
        PartGeometry column_grip;
    };
    struct ColumnStates final {
        bool hot{};
        bool pressed{};
        bool captured{};
        // Application-defined column states.
        bool sorted_ascending{};
        bool sorted_descending{};
    };
    struct Column final {
        std::size_t index{};
        std::size_t display_position{};
        std::string stable_id;
        ColumnParts parts;
        ColumnStates states;
    };
    [[nodiscard]] std::vector<Column> realized_columns() const;

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void on_attached_to_window() override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;
    [[nodiscard]] ItemSystemIndex publish_realized_item_system(std::size_t item) const override;
    void publish_realized_item_geometry(std::size_t item, std::span<PartGeometry> geometry) const override;

private:
    // Where everything sits for the current bounds and model, from one pass
    // (see layout_for()).
    struct Layout final {
        Rect well{};
        Rect header{};          // the header band; empty without columns
        Rect viewport{};        // where rows are shown
        Rect vertical_bar{};    // empty when not shown
        Rect horizontal_bar{};
        Rect corner{};          // between two shown bars
        double scale{1.0};
        double row_height{};
        double columns_width{};     // the columns' total scaled width
        double horizontal_offset{}; // clamped, logical units
        std::size_t shown_rows{};   // rows shown: none without columns
        std::size_t page_rows{1U};  // fully visible rows
        std::size_t visible_rows{}; // rows at least partly visible
        std::size_t top_row{};      // clamped
    };
    // Which part of the view a point is on.
    enum class HitZone : std::uint8_t { none, row, expander, header, divider };
    struct Hit final {
        HitZone zone{HitZone::none};
        // A row, or a column for the header and divider zones.
        std::size_t index{};
    };
    using NodeIndices = std::unordered_map<std::string, std::size_t>;

    // Model structure, rebuilt when the nodes, the sort or the expansion
    // change. See rebuild_structure(), rebuild_order() and rebuild_rows().
    void rebuild_structure();
    void rebuild_order();
    void rebuild_rows();
    [[nodiscard]] bool has_children(std::size_t index) const noexcept;
    [[nodiscard]] bool can_expand(std::size_t index) const noexcept;
    [[nodiscard]] std::size_t require_index(std::string_view node_id, const char* message) const;
    // Expands the collapsed ancestors of a node and appends one change per
    // ancestor to `changes`, outermost first. Rebuilds the rows when any
    // changed. The changes are published afterwards (publish_expansions), so
    // a handler sees the finished state.
    void expand_ancestors(std::size_t index, std::vector<TreeListExpansionChange>& changes);
    void publish_expansions(std::span<const TreeListExpansionChange> changes);
    // The node itself when it has a row, otherwise its nearest ancestor
    // with one (a root always has a row).
    [[nodiscard]] std::size_t visible_ancestor(std::size_t index) const noexcept;
    // Moves the selection and the current node off rows a collapse hid,
    // onto their nearest visible ancestor. Returns whether the selection
    // moved.
    [[nodiscard]] bool lift_hidden_selection();
    // The selected node's ID as an owned string, or none.
    [[nodiscard]] std::optional<std::string> selected_copy() const;
    void publish_selection(std::optional<std::string> previous);

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
    [[nodiscard]] Size icon_size() const noexcept;
    [[nodiscard]] std::size_t maximum_top_row(const Layout& placed) const noexcept;
    [[nodiscard]] std::size_t end_realized(const Layout& placed) const noexcept;
    [[nodiscard]] Rect row_bounds(const Layout& placed, std::size_t row) const noexcept;
    [[nodiscard]] Rect column_cell(const Layout& placed, std::size_t column) const noexcept;
    void append_row_cells(const Layout& placed, std::size_t row, SemanticNode& row_node) const;
    [[nodiscard]] bool column_visible(const Layout& placed, std::size_t column) const noexcept;
    [[nodiscard]] std::size_t visible_column_count(const Layout& placed) const noexcept;
    // The `position`th visible column, counted in display order.
    [[nodiscard]] std::size_t visible_column(const Layout& placed, std::size_t position) const noexcept;
    // The one geometry function of each item system's parts.
    [[nodiscard]] RowParts row_parts(const Layout& placed, std::size_t row, Rect clip) const;
    [[nodiscard]] CellParts cell_parts(const Layout& placed, std::size_t row, std::size_t column,
                                       Rect clip) const;
    [[nodiscard]] ColumnParts column_parts(const Layout& placed, std::size_t column, Rect clip) const;
    [[nodiscard]] AnatomyStateSet row_states(std::size_t row) const;
    [[nodiscard]] AnatomyStateSet cell_states(std::size_t row) const;
    [[nodiscard]] AnatomyStateSet column_states(std::size_t column) const;
    [[nodiscard]] Hit hit_test(const Layout& placed, Point local) const;
    // What one realized-item position names. The realized items are the
    // rows in view (item system "row"), then each row's visible cells past
    // the tree column, row by row (item system "cell"), then the visible
    // column headers (item system "column"). `row` is the row of a row or
    // cell; `column` the column of a cell or header.
    struct RealizedSlot final {
        ItemSystemIndex system{};
        std::size_t row{};
        std::size_t column{};
    };
    [[nodiscard]] RealizedSlot realized_slot(const Layout& placed, std::size_t item) const;
    [[nodiscard]] std::size_t realized_row_count(const Layout& placed) const noexcept;
    [[nodiscard]] std::size_t realized_cell_count(const Layout& placed) const noexcept;
    [[nodiscard]] std::size_t realized_cell_column(const Layout& placed, std::size_t position) const noexcept;

    void paint_row(Painter& painter, const Layout& placed, std::size_t row);
    void paint_column(Painter& painter, const Layout& placed, std::size_t column);

    void scroll_rows(std::ptrdiff_t rows);
    void ensure_row_visible(std::size_t row);
    void sync_scroll_bars(const Layout& placed);
    void on_vertical_scroll(double value);
    void on_horizontal_scroll(double value);
    void image_list_changed(const ImageListChange& change);

    // A user's choice of a row: selects its node when enabled, moves the
    // current node, scrolls it into view.
    void choose_row(std::size_t row);
    void activate_row(std::size_t row);
    void toggle_sort(std::size_t column);
    void apply_column_move(std::size_t column, std::size_t display_position);
    // The display position a dragged column lands at for a release at
    // `local_x`: the number of other columns whose middle lies left of it,
    // at least 1.
    [[nodiscard]] std::size_t drop_position(const Layout& placed, std::size_t column,
                                            double local_x) const noexcept;
    void finish_column_resize();
    void end_header_press();
    // The enabled row nearest `row` going in `step` (+1 or -1), `row` itself
    // when enabled; none when there is no enabled row that way.
    [[nodiscard]] std::optional<std::size_t> enabled_row_from(std::size_t row, std::ptrdiff_t step) const;
    // The row Up, Down, Home, End, Page Up or Page Down moves to from
    // `from`, skipping disabled rows; none for other keys or no move.
    [[nodiscard]] std::optional<std::size_t> navigation_target(std::uint32_t key, std::size_t from,
                                                               const Layout& placed) const;
    // Right and Left: expand or enter, collapse or go to the parent.
    void step_into(std::size_t row);
    void step_out(std::size_t row);
    void type_select(std::string_view text);

    std::vector<TreeListColumn> columns_{};
    // column_order_[display position] = column; column_position_[column] =
    // display position. Both are permutations of the columns.
    std::vector<std::size_t> column_order_{};
    std::vector<std::size_t> column_position_{};
    std::vector<TreeListNode> nodes_{};
    NodeIndices node_indices_{};
    // Structure, indexed by model index. parent_ is npos for a root.
    // children_ lists every node's children in shown order:
    // children_[child_begin_[slot] .. child_begin_[slot + 1]) where slot 0
    // holds the roots and slot index + 1 a node's children.
    // sibling_position_ is a node's position among its shown siblings.
    std::vector<std::size_t> parent_{};
    std::vector<std::size_t> child_begin_{};
    std::vector<std::size_t> children_{};
    std::vector<std::size_t> sibling_position_{};
    // Every node in shown preorder (siblings sorted); then the rows, the
    // nodes not under a collapsed node; row_of_node_[index] is a node's row
    // or npos when hidden.
    std::vector<std::size_t> order_{};
    std::vector<std::size_t> visible_{};
    std::vector<std::size_t> row_of_node_{};
    // Model indices.
    std::optional<std::size_t> selected_node_{};
    std::optional<std::size_t> current_node_{};
    // Rows and columns.
    std::optional<std::size_t> hot_row_{};
    std::optional<std::size_t> hot_column_{};
    std::optional<std::size_t> pressed_column_{};
    std::optional<std::size_t> resizing_column_{};
    bool dragging_column_{};
    double press_x_{};
    double resize_start_x_{};
    double resize_start_width_{};
    std::size_t top_row_{};
    double horizontal_offset_{};
    std::optional<std::size_t> sort_column_{};
    TreeListSortOrder sort_order_{TreeListSortOrder::none};
    bool focused_{};
    bool synchronizing_scroll_{};
    SubscriptionToken images_changed_{};
    std::shared_ptr<VScrollBar> vertical_bar_{};
    std::shared_ptr<HScrollBar> horizontal_bar_{};
    SubscriptionToken vertical_scrolled_{};
    SubscriptionToken horizontal_scrolled_{};
    // Paint scratch, reused for every cell of a paint pass.
    std::string paint_text_{};
    // Type-ahead.
    std::string type_prefix_{};
    std::chrono::steady_clock::time_point last_type_time_{};
    Event<const TreeListSelectionChange&> selection_changed_{};
    Event<const TreeListExpansionChange&> expansion_changed_{};
    Event<const TreeListActivation&> item_activated_{};
    Event<const TreeListSortChange&> sort_changed_{};
    Event<const TreeListColumnWidthChange&> column_width_changed_{};
    Event<const TreeListColumnMove&> column_moved_{};
    // property_codemod begin TreeListView (tools/property_families/tree_list_view.json; generated, do not edit)
protected:
    // on_<property>_changed runs after a changed write's store and touch and
    // raises the change event; an override calls the base. Only the
    // properties with a Windows Forms On<Property>Changed have one.
    virtual void on_font_changed();

private:
    friend struct TreeListViewIndentationCapability<TreeListView>;
    friend struct TreeListViewImageListCapability<TreeListView>;
    friend struct FontCapability<TreeListView>;
    friend struct TreeListViewSelectedIdCapability<TreeListView>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(indentation)& cell, double& incoming);
    void written(const decltype(indentation)& cell);
    void prepare_write(const decltype(image_list)& cell, std::shared_ptr<ImageList>& incoming);
    void written(const decltype(image_list)& cell);
    void prepare_write(const decltype(font)& cell, FontSpec& incoming);
    void written(const decltype(font)& cell);
    void write_view(const decltype(selected_id)& cell, std::optional<std::string> incoming);
    [[nodiscard]] std::optional<std::string> read_view(const decltype(selected_id)& cell) const;
    // property_codemod end TreeListView
};

} // namespace gui_forms
