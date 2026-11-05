#pragma once

#include "gui_forms/controls/scrollable_control/container_control/container_control.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace gui_forms {

enum class TableSizeMode : std::uint8_t {
    absolute,
    percent,
    auto_size,
};

struct TableLayoutStyle final {
    TableSizeMode size_mode{TableSizeMode::auto_size};
    double size{};
    friend constexpr bool operator==(const TableLayoutStyle& left,
                                     const TableLayoutStyle& right) noexcept {
        return left.size_mode == right.size_mode && left.size == right.size;
    }
};

struct TableLayoutCellPosition final {
    std::size_t column{};
    std::size_t row{};
    friend constexpr bool operator==(
        const TableLayoutCellPosition& left,
        const TableLayoutCellPosition& right) noexcept {
        return left.column == right.column && left.row == right.row;
    }
};

enum class TableLayoutGrowStyle : std::uint8_t {
    fixed_size,
    add_rows,
    add_columns,
};

enum class TableCellBorderStyle : std::uint8_t {
    none,
    single,
    inset,
    outset,
};

// The Windows Forms table container: children in cells of column and row
// tracks sized `Absolute`, `Percent` (weights over the space the other
// tracks leave) or `AutoSize`. Its algorithm is its own, not the CSS grid
// solver's (notes:planning/stages/L-layout/README.md, "Decisions in force"). In
// a right-to-left effective layout direction the layout is computed left
// to right and mirrored about the panel, padding, margins and the
// placement in cells included: the first column is at the right edge.
class TableLayoutPanelCompat final : public ContainerControl {
public:
    explicit TableLayoutPanelCompat(StableId stable_id);

    [[nodiscard]] std::size_t column_count() const noexcept {
        return column_count_;
    }
    void set_column_count(std::size_t count);
    [[nodiscard]] std::size_t row_count() const noexcept { return row_count_; }
    void set_row_count(std::size_t count);
    [[nodiscard]] TableLayoutGrowStyle grow_style() const noexcept {
        return grow_style_;
    }
    void set_grow_style(TableLayoutGrowStyle style);
    [[nodiscard]] TableCellBorderStyle cell_border_style() const noexcept {
        return cell_border_style_;
    }
    void set_cell_border_style(TableCellBorderStyle style);
    [[nodiscard]] Size track_spacing() const noexcept { return track_spacing_; }
    void set_track_spacing(Size spacing);

    [[nodiscard]] std::span<const TableLayoutStyle> column_styles() const noexcept {
        return column_styles_;
    }
    [[nodiscard]] std::span<const TableLayoutStyle> row_styles() const noexcept {
        return row_styles_;
    }
    void set_column_style(std::size_t column, TableLayoutStyle style);
    void set_row_style(std::size_t row, TableLayoutStyle style);

    void set_cell_position(const Control& child,
                           TableLayoutCellPosition position);
    void clear_cell_position(const Control& child);
    [[nodiscard]] std::optional<TableLayoutCellPosition> cell_position(
        const Control& child) const;
    void set_column_span(const Control& child, std::size_t span);
    [[nodiscard]] std::size_t column_span(const Control& child) const;
    void set_row_span(const Control& child, std::size_t span);
    [[nodiscard]] std::size_t row_span(const Control& child) const;
    [[nodiscard]] Control::Ptr control_from_position(std::size_t column,
                                                     std::size_t row) const;

    [[nodiscard]] std::span<const double> column_widths() const noexcept {
        return column_widths_;
    }
    [[nodiscard]] std::span<const double> row_heights() const noexcept {
        return row_heights_;
    }
    [[nodiscard]] bool layout_overflowed() const noexcept {
        return layout_overflowed_;
    }

    [[nodiscard]] Size measure(Size available) override;
    void arrange(Rect final_bounds) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    // Stage T1 anatomy, as dot members: the scrollable parts and the cell grid.
    struct Parts final {
        ScrollableControl::Parts scrollable;
        PartGeometry cell_grid;
    };
    [[nodiscard]] Parts parts() const;

protected:
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;

private:
    // `seen_pass` is the layout pass that last found the child, for
    // reconciliation without a per-pass set.
    struct CellMetadata final {
        std::optional<TableLayoutCellPosition> position;
        std::size_t column_span{1U};
        std::size_t row_span{1U};
        std::uint64_t seen_pass{};
    };
    using MetadataMap = std::unordered_map<std::uint64_t, CellMetadata>;
    // Each laid-out child's cell, stamped with the pass that resolved it.
    // A pass erases what it did not resolve, so the map holds exactly the
    // last pass's cells, and a steady layout inserts nothing.
    struct ResolvedCell final {
        TableLayoutCellPosition position{};
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

    [[nodiscard]] CellMetadata& metadata_for(const Control& child);
    [[nodiscard]] const CellMetadata* metadata_for(const Control& child) const;
    void reconcile_metadata(std::uint64_t pass);
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

    MetadataMap metadata_;
    ResolvedCellMap resolved_cells_;
    std::vector<TableLayoutStyle> column_styles_{TableLayoutStyle{}};
    std::vector<TableLayoutStyle> row_styles_{TableLayoutStyle{}};
    std::vector<double> column_widths_;
    std::vector<double> row_heights_;
    std::size_t column_count_{1U};
    std::size_t row_count_{1U};
    TableLayoutGrowStyle grow_style_{TableLayoutGrowStyle::add_rows};
    TableCellBorderStyle cell_border_style_{TableCellBorderStyle::none};
    Size track_spacing_{};
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
};

} // namespace gui_forms
