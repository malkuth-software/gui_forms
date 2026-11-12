#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/binding/child_property_view/child_property_view.hpp"
#include "gui_forms/controls/container_control.hpp"
#include "gui_forms/controls/table_layout_panel_compat_values.hpp"
#include "gui_forms/layout/grid_span/grid_span.hpp"
#include "gui_forms/controls/table_layout_panel_compat_capabilities.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

namespace gui_forms {

enum class TableSizeMode : std::uint8_t {
    absolute,
    percent,
    auto_size,
};

struct GUI_FORMS_API TableLayoutStyle final {
    TableSizeMode size_mode{TableSizeMode::auto_size};
    double size{};
    friend constexpr bool operator==(const TableLayoutStyle& left,
                                     const TableLayoutStyle& right) noexcept {
        return left.size_mode == right.size_mode && left.size == right.size;
    }
};

struct GUI_FORMS_API TableLayoutCellPosition final {
    std::size_t column{};
    std::size_t row{};
    friend constexpr bool operator==(
        const TableLayoutCellPosition& left,
        const TableLayoutCellPosition& right) noexcept {
        return left.column == right.column && left.row == right.row;
    }
};

// The placement a table provides to its children (Column, Row, ColumnSpan
// and RowSpan; child properties, study sections 10, 10.9 section 5 and
// 15.4). `cell` is the child's explicit cell, row then column; a child
// without one is placed automatically. `column_span` and `row_span` size the
// anonymous span the table holds anchored at the child's cell, so a child
// needs a cell before it can span: `(*table).cell[*header] = GridCell{.row =
// 0U, .column = 0U};`, then `(*table).column_span[*header] = 4U;`.
template <typename Declaring>
struct TableCellCapability {
    ChildPropertyView<GridCell, TableCellCapability<Declaring>> cell;

    static void assign(TableCellCapability& capability, const Control& child, GridCell value) {
        Declaring& container = static_cast<Declaring&>(capability);
        container.prepare_write(child);
        if (!container.replace_cell(child, value)) return;
        container.written(child);
    }

    static GridCell read(const TableCellCapability& capability, const Control& child) {
        const Declaring& container = static_cast<const Declaring&>(capability);
        const GridCell value = container.stored_cell(child);
        return value;
    }
};

// The axis a span view sizes.
enum class TableSpanAxis : std::uint8_t {
    columns,
    rows,
};

template <typename Declaring>
struct TableColumnSpanCapability {
    ChildPropertyView<std::uint32_t, TableColumnSpanCapability<Declaring>> column_span;

    static void assign(TableColumnSpanCapability& capability, const Control& child, std::uint32_t value) {
        Declaring& container = static_cast<Declaring&>(capability);
        container.prepare_write(child);
        if (!container.replace_span(child, TableSpanAxis::columns, value)) return;
        container.written(child);
    }

    static std::uint32_t read(const TableColumnSpanCapability& capability, const Control& child) {
        const Declaring& container = static_cast<const Declaring&>(capability);
        const std::uint32_t value = container.stored_span(child, TableSpanAxis::columns);
        return value;
    }
};

template <typename Declaring>
struct TableRowSpanCapability {
    ChildPropertyView<std::uint32_t, TableRowSpanCapability<Declaring>> row_span;

    static void assign(TableRowSpanCapability& capability, const Control& child, std::uint32_t value) {
        Declaring& container = static_cast<Declaring&>(capability);
        container.prepare_write(child);
        if (!container.replace_span(child, TableSpanAxis::rows, value)) return;
        container.written(child);
    }

    static std::uint32_t read(const TableRowSpanCapability& capability, const Control& child) {
        const Declaring& container = static_cast<const Declaring&>(capability);
        const std::uint32_t value = container.stored_span(child, TableSpanAxis::rows);
        return value;
    }
};

// The Windows Forms table container: children in cells of column and row
// tracks sized `Absolute`, `Percent` (weights over the space the other
// tracks leave) or `AutoSize`. Its algorithm is its own, not the CSS grid
// solver's (notes:planning/stages/L-layout/README.md, "Decisions in force"). In
// a right-to-left effective layout direction the layout is computed left
// to right and mirrored about the panel, padding, margins and the
// placement in cells included: the first column is at the right edge.
class GUI_FORMS_API TableLayoutPanelCompat final : public ContainerControl,
                                     public TableCellCapability<TableLayoutPanelCompat>,
                                     public TableColumnSpanCapability<TableLayoutPanelCompat>,
                                     public TableRowSpanCapability<TableLayoutPanelCompat>, public TableLayoutPanelCompatCapabilities<TableLayoutPanelCompat> {
public:
    explicit TableLayoutPanelCompat(StableId stable_id);

    // The child properties it provides, the cell first (a span needs it).
    static constexpr std::size_t table_child_property_count = 3U;
    [[nodiscard]] static const std::array<ChildPropertyEntry, table_child_property_count>&
    table_child_property_entries() noexcept;
    [[nodiscard]] std::span<const ChildPropertyEntry> child_property_table() const noexcept override;

    // The anonymous spans, in the order they were made; each is anchored at
    // a placed child's cell.
    [[nodiscard]] std::span<const GridSpan> spans() const& noexcept { return spans_; }
    std::span<const GridSpan> spans() const&& = delete;

    [[nodiscard]] std::span<const TableLayoutStyle> column_styles() const& noexcept {
        return column_styles_;
    }
    std::span<const TableLayoutStyle> column_styles() const&& = delete;
    [[nodiscard]] std::span<const TableLayoutStyle> row_styles() const& noexcept {
        return row_styles_;
    }
    std::span<const TableLayoutStyle> row_styles() const&& = delete;
    void set_column_style(std::size_t column, TableLayoutStyle style);
    void set_row_style(std::size_t row, TableLayoutStyle style);
    // Replaces every column's style; the count becomes the number of
    // styles (1 through 64, std::out_of_range). Each style is validated
    // before anything changes (std::invalid_argument).
    void set_column_styles(std::span<const TableLayoutStyle> styles);
    // The same for the rows.
    void set_row_styles(std::span<const TableLayoutStyle> styles);

    // The cell a child occupies after layout: its explicit cell, or the one
    // automatic placement found; none when it overflowed or is not a live
    // direct child. Lays the panel out first when it is in a window.
    [[nodiscard]] std::optional<TableLayoutCellPosition> cell_position(const Control& child) const;
    // Makes `child` automatic again (the cell's reset); refuses while it is
    // the only child anchored at a span, which must go back to 1 by 1 first.
    void clear_cell(const Control& child);
    // Whether `child` has an explicit cell (false: placed automatically).
    [[nodiscard]] bool has_cell(const Control& child) const;
    [[nodiscard]] Control::Ptr control_from_position(std::size_t column,
                                                     std::size_t row) const;

    [[nodiscard]] std::span<const double> column_widths() const& noexcept {
        return column_widths_;
    }
    std::span<const double> column_widths() const&& = delete;
    [[nodiscard]] std::span<const double> row_heights() const& noexcept {
        return row_heights_;
    }
    std::span<const double> row_heights() const&& = delete;
    [[nodiscard]] bool layout_overflowed() const noexcept {
        return layout_overflowed_;
    }

    [[nodiscard]] Size measure(Size available) override;
    void arrange(Rect final_bounds) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    // Stage T1 anatomy, as dot members: the scrollable parts and the cell grid.
    struct GUI_FORMS_API Parts final {
        ScrollableControl::Parts scrollable;
        PartGeometry cell_grid;
    };
    [[nodiscard]] Parts parts() const;

protected:
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;
    void child_leaving(const Control& child) noexcept override;

private:
    friend struct TableCellCapability<TableLayoutPanelCompat>;
    friend struct TableColumnSpanCapability<TableLayoutPanelCompat>;
    friend struct TableRowSpanCapability<TableLayoutPanelCompat>;

    // The placement views' hooks: the container's thread and a live direct
    // child before the compare; relayout after a change.
    void prepare_write(const Control& child) const;
    void written(const Control& child);
    // Moves `child` to `cell`, carrying the span anchored at its old cell
    // when no other child is anchored there; refuses a cell inside another
    // span, past the track limit, or a carried span that would no longer
    // fit. False when the child was already there.
    bool replace_cell(const Control& child, GridCell cell);
    // Drops `child`'s cell; false when it had none.
    bool remove_cell(const Control& child);
    // Sizes the span anchored at `child`'s cell on `axis` (1 through 64; 1 by
    // 1 removes it); refuses an automatic child, a span past the track
    // limit, one overlapping another span or covering another placed
    // child's cell. False when it already had that extent.
    bool replace_span(const Control& child, TableSpanAxis axis, std::uint32_t extent);
    [[nodiscard]] GridCell stored_cell(const Control& child) const;
    [[nodiscard]] std::uint32_t stored_span(const Control& child, TableSpanAxis axis) const;
    // The span anchored at `anchor`; null when there is none.
    [[nodiscard]] const GridSpan* anchored_span(GridCell anchor) const noexcept;
    // Whether `candidate` (replacing the span at `replaced`, or none) fits:
    // inside the track limit, overlapping no other span and covering no
    // placed child's cell but its anchor. Throws the refusal.
    void check_span(const GridSpan& candidate, const GridSpan* replaced) const;
    // How many children have their explicit cell at `anchor`.
    [[nodiscard]] std::size_t children_anchored_at(GridCell anchor) const noexcept;
    // Replaces, adds or (at 1 by 1) removes the span anchored at `anchor`.
    void store_span(GridCell anchor, const GridSpan& span);

    // The explicit cells, keyed by the child's runtime id; dropped when the
    // child leaves.
    using CellMap = std::unordered_map<std::uint64_t, GridCell>;
    // Each laid-out child's cell, stamped with the pass that resolved it.
    // A pass erases what it did not resolve, so the map holds exactly the
    // last pass's cells, and a steady layout inserts nothing.
    struct ResolvedCell final {
        TableLayoutCellPosition position{};
        std::size_t column_span{1U};
        std::size_t row_span{1U};
        std::uint64_t seen_pass{};
    };
    using ResolvedCellMap = std::unordered_map<std::uint64_t, ResolvedCell>;

    // One child as a pass lays it out.
    struct TableItem final {
        Control::Ptr control{};
        TableLayoutCellPosition position{};
        std::size_t column_span{1U};
        std::size_t row_span{1U};
        Size desired{};
        Insets margin{};
    };
    // A spanning item's demand on the tracks [start, start + span).
    struct TrackDemand final {
        std::size_t start{};
        std::size_t span{1U};
        double required{};
    };
    // One pass's storage, reused from pass to pass; live lengths are the
    // pass's counts. `occupied` is a 64 by 64 cell bitmap with a fixed row
    // stride (the track limit), `columns` by `rows` of it live. The track
    // vectors hold one entry per track; the offset vectors one more, the
    // far edge.
    struct Workspace final {
        std::vector<Control::Ptr> retained{};
        std::vector<Control::Ptr> automatic{};
        std::vector<Control::Ptr> overflow{};
        std::vector<TableItem> items{};
        std::vector<std::uint8_t> occupied{};
        std::size_t columns{};
        std::size_t rows{};
        std::vector<TableLayoutStyle> column_styles{};
        std::vector<TableLayoutStyle> row_styles{};
        std::vector<double> column_minimum{};
        std::vector<double> row_minimum{};
        std::vector<double> column_actual{};
        std::vector<double> row_actual{};
        std::vector<double> column_offsets{};
        std::vector<double> row_offsets{};
        std::vector<TrackDemand> column_spans{};
        std::vector<TrackDemand> row_spans{};
    };

    // Lays the children out in the panel's workspace, or in a fresh one
    // when a child's measure lays the panel out again inside a pass.
    [[nodiscard]] Size layout_children(Size available, bool assign);
    [[nodiscard]] Size layout_pass(Size available, bool assign, Workspace& workspace);
    // Places the children with a cell position, then the others in the
    // first free cells, growing the grid as grow_style allows, into
    // `items`; the rest go to `overflow`. Sets the pass's track counts.
    void place_items(Workspace& workspace);
    // One axis's tracks: raises `minimum` (one entry per track) by the
    // absolute sizes and the spanning demands, fills `actual` (as long)
    // with the used sizes in `available`, and returns the desired extent.
    [[nodiscard]] static double resolve_tracks(std::span<const TableLayoutStyle> styles,
                                               std::span<double> minimum,
                                               std::span<const TrackDemand> spans, double available,
                                               std::span<double> actual) noexcept;
    static void validate_style(TableLayoutStyle style);
    [[nodiscard]] BindingValue column_count_property() const;
    void set_column_count_property(const BindingValue& value);
    [[nodiscard]] BindingValue row_count_property() const;
    void set_row_count_property(const BindingValue& value);
    [[nodiscard]] BindingValue column_styles_property() const;
    void set_column_styles_property(const BindingValue& value);
    [[nodiscard]] BindingValue row_styles_property() const;
    void set_row_styles_property(const BindingValue& value);

    CellMap cells_;
    std::vector<GridSpan> spans_;
    ResolvedCellMap resolved_cells_;
    std::vector<TableLayoutStyle> column_styles_{TableLayoutStyle{}};
    std::vector<TableLayoutStyle> row_styles_{TableLayoutStyle{}};
    std::vector<double> column_widths_;
    std::vector<double> row_heights_;
    std::size_t column_count_{1U};
    std::size_t row_count_{1U};
    bool layout_overflowed_{};

    // The layout pass number, for reconciliation stamps.
    std::uint64_t pass_{};
    Workspace workspace_{};
    bool workspace_busy_{};
    // The arranged columns in painted order (left to right on screen), and
    // the grid's left edge in the panel: right to left puts the first
    // column at the right, so the grid ends at the right padding.
    std::vector<double> painted_column_widths_{};
    double grid_left_{};
    // property_codemod begin TableLayoutPanelCompat (tools/property_families/table_layout_panel_compat.json; generated, do not edit)
public:
    // TableLayoutPanelCompat's property table: Control's entries, then its own.
    static constexpr std::size_t table_layout_panel_compat_property_count = Control::control_property_count + 7U;
    [[nodiscard]] static const std::array<PropertyEntry, table_layout_panel_compat_property_count>& table_layout_panel_compat_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

private:
    friend struct TableLayoutGrowCapability<TableLayoutPanelCompat>;
    friend struct TableLayoutCellBorderCapability<TableLayoutPanelCompat>;
    friend struct TableLayoutTrackSpacingCapability<TableLayoutPanelCompat>;
    friend struct TableLayoutColumnCountCapability<TableLayoutPanelCompat>;
    friend struct TableLayoutRowCountCapability<TableLayoutPanelCompat>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(grow_style)& cell, TableLayoutGrowStyle& incoming);
    void written(const decltype(grow_style)& cell);
    void prepare_write(const decltype(cell_border_style)& cell, TableCellBorderStyle& incoming);
    void written(const decltype(cell_border_style)& cell);
    void prepare_write(const decltype(track_spacing)& cell, Size& incoming);
    void written(const decltype(track_spacing)& cell);
    void write_view(const decltype(column_count)& cell, std::size_t incoming);
    [[nodiscard]] std::size_t read_view(const decltype(column_count)& cell) const;
    void write_view(const decltype(row_count)& cell, std::size_t incoming);
    [[nodiscard]] std::size_t read_view(const decltype(row_count)& cell) const;
    // property_codemod end TableLayoutPanelCompat
};

} // namespace gui_forms
