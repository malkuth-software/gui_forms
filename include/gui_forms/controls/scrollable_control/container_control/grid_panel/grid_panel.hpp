#pragma once

#include "gui_forms/controls/scrollable_control/container_control/container_control.hpp"
#include "gui_forms/layout/grid_solver/grid_solver.hpp"
#include "gui_forms/layout/positioning/positioning.hpp"
#include "gui_forms/layout/style/layout_style.hpp"

#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace gui_forms {

// One child's grid item properties (CSS Grid's names; PLAN section 2.3).
// The panel keeps one per child, keyed by its runtime id and dropped when
// the child leaves. Margins are the child's own `margin`; `auto_margin`
// marks the sides that are `auto` instead. The child's minimum_size and
// maximum_size bound its content contributions, and its used size where
// its own minimum is a length or its own maximum a length or `none`.
struct GridItemLayout final {
    // grid-column-start / -end and grid-row-start / -end (`grid-area:
    // <name>` is the area's name on all four).
    GridItemStyle placement{};
    // `auto` takes the panel's justify-items or align-items.
    LayoutAlignment justify_self{LayoutAlignment::automatic};
    LayoutAlignment align_self{LayoutAlignment::automatic};
    // CSS width and height: `auto` sizes the item from its content (and
    // stretches it under `normal` and `stretch`); a length, or a
    // percentage of its grid area, is definite. Content keywords are not
    // admitted.
    SizeValue width{};
    SizeValue height{};
    // `auto` is CSS Grid's automatic minimum (its section 6.6); a length or
    // a percentage of the grid area sets it.
    SizeValue min_width{};
    SizeValue min_height{};
    SizeValue max_width{SizeKind::none, {}};
    SizeValue max_height{SizeKind::none, {}};
    BoxSides<bool> auto_margin{};
    // `position` and the insets (PLAN section 2.5): a relative item is
    // offset after layout without moving its neighbours; an absolute item
    // leaves the flow and is placed in its containing block after the
    // in-flow items. Either makes the child a positioned box, the
    // containing block of absolute children below it.
    ItemPositioning positioning{};

    friend bool operator==(const GridItemLayout& left, const GridItemLayout& right) {
        return left.placement == right.placement && left.justify_self == right.justify_self &&
               left.align_self == right.align_self && left.width == right.width && left.height == right.height &&
               left.min_width == right.min_width && left.min_height == right.min_height &&
               left.max_width == right.max_width && left.max_height == right.max_height &&
               left.auto_margin == right.auto_margin && left.positioning == right.positioning;
    }
};

// A container that lays its children out as a CSS grid container (stage L,
// phase L4): explicit and implicit tracks, line-based and automatic
// placement, named areas, intrinsic and flexible track sizing, gaps and box
// alignment, through the core grid solver.
class GridPanel final : public ContainerControl {
public:
    explicit GridPanel(StableId stable_id);

    // grid-template-columns and grid-template-rows: at most 10,000 tracks
    // each, `repeat()` counted. A list with `repeat(auto-fill | auto-fit,
    // ...)` holds only fixed tracks (CSS Grid 7.2.3). The getters borrow
    // the panel and last until the next set.
    [[nodiscard]] const GridTrackList& template_columns() const& noexcept { return style_.template_columns; }
    const GridTrackList& template_columns() const&& = delete;
    void set_template_columns(const GridTrackList& tracks);
    [[nodiscard]] const GridTrackList& template_rows() const& noexcept { return style_.template_rows; }
    const GridTrackList& template_rows() const&& = delete;
    void set_template_rows(const GridTrackList& tracks);
    // grid-template-areas: rectangular named areas within its cells.
    [[nodiscard]] const GridTemplateAreas& template_areas() const& noexcept { return style_.template_areas; }
    const GridTemplateAreas& template_areas() const&& = delete;
    void set_template_areas(const GridTemplateAreas& areas);
    // grid-auto-flow: row or column, sparse or dense.
    [[nodiscard]] GridAutoFlow auto_flow() const noexcept { return style_.auto_flow; }
    void set_auto_flow(GridAutoFlow flow);
    // grid-auto-columns and grid-auto-rows: one or more sizes, repeated
    // over the implicit tracks.
    [[nodiscard]] std::span<const GridTrackSize> auto_columns() const& noexcept { return style_.auto_columns; }
    std::span<const GridTrackSize> auto_columns() const&& = delete;
    void set_auto_columns(std::span<const GridTrackSize> sizes);
    [[nodiscard]] std::span<const GridTrackSize> auto_rows() const& noexcept { return style_.auto_rows; }
    std::span<const GridTrackSize> auto_rows() const&& = delete;
    void set_auto_rows(std::span<const GridTrackSize> sizes);
    // justify-content and align-content: normal, stretch, start, end,
    // flex-start, flex-end, center and the three space distributions.
    [[nodiscard]] LayoutAlignment justify_content() const noexcept { return justify_content_; }
    void set_justify_content(LayoutAlignment alignment);
    [[nodiscard]] LayoutAlignment align_content() const noexcept { return align_content_; }
    void set_align_content(LayoutAlignment alignment);
    // justify-items and align-items: normal and stretch stretch an
    // auto-sized item; start, end, flex-start, flex-end, center and
    // baseline (first-baseline alignment across a row; as start for
    // justify-items).
    [[nodiscard]] LayoutAlignment justify_items() const noexcept { return justify_items_; }
    void set_justify_items(LayoutAlignment alignment);
    [[nodiscard]] LayoutAlignment align_items() const noexcept { return align_items_; }
    void set_align_items(LayoutAlignment alignment);
    // row-gap and column-gap; a percentage is of the content box on the
    // gap's axis, and zero when that is indefinite.
    [[nodiscard]] LengthPercentage row_gap() const noexcept { return row_gap_; }
    void set_row_gap(LengthPercentage gap);
    [[nodiscard]] LengthPercentage column_gap() const noexcept { return column_gap_; }
    void set_column_gap(LengthPercentage gap);

    // A direct child's item properties; a child without its own record has
    // the default one (auto-placed into one cell).
    void set_item_layout(const Control& child, const GridItemLayout& layout);
    [[nodiscard]] GridItemLayout item_layout(const Control& child) const;
    // The child's `position` and insets, without copying its whole record;
    // static for a control that is not a live direct child.
    [[nodiscard]] ItemPositioning item_positioning(const Control& child) const noexcept;

    // The last arrange's resolution (PLAN 3.4): the tracks with their base
    // sizes, growth limits and positions, and per laid-out child, in the
    // order of `laid_out_children`, its area and bounds. Both borrow the
    // panel and last until its next arrange.
    [[nodiscard]] const GridLayoutWorkspace& layout_snapshot() const& noexcept { return arranged_; }
    const GridLayoutWorkspace& layout_snapshot() const&& = delete;
    [[nodiscard]] std::span<const RuntimeId> laid_out_children() const& noexcept { return arranged_ids_; }
    std::span<const RuntimeId> laid_out_children() const&& = delete;
    // How the last arrange ended: `solved`, or the limit that refused it
    // (more than 65,536 children, or an implicit grid of more than
    // 1,048,576 cells), in which case no child slot was published.
    [[nodiscard]] GridSolveOutcome layout_outcome() const noexcept { return arranged_outcome_; }

    [[nodiscard]] Size measure(Size available) override;
    void arrange(Rect final_bounds) override;
    // The arrange asks each item its content widths and its height at its
    // column width; the window's at-most measure would go unread.
    [[nodiscard]] bool measures_children_in_arrange() const noexcept override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;

protected:
    [[nodiscard]] Size measure_intrinsic(const LayoutConstraint& constraint) override;

private:
    struct ItemRecord final {
        GridItemLayout layout{};
        // The layout pass that last found the child, for reconciliation.
        std::uint64_t seen_pass{};
    };
    using ItemMap = std::unordered_map<std::uint64_t, ItemRecord>;

    // The content-box extents the solver lays out against; whether to stop
    // after the columns (for an intrinsic width); and whether to publish
    // child slots.
    struct SolveRequest final {
        GridAvailableExtent width{};
        GridAvailableExtent height{};
        bool columns_only{};
        bool assign{};
    };

    // The content box's used size and how the solver ended.
    struct SolveResult final {
        Size used{};
        GridSolveOutcome outcome{GridSolveOutcome::solved};
    };

    // One layout into `workspace`: gathers the children and their records,
    // places them, measures their width contributions, sizes the columns,
    // measures their heights at their column widths, sizes the rows,
    // arranges and, when `assign`, publishes their slots.
    SolveResult solve(const SolveRequest& request, GridLayoutWorkspace& workspace);
    void gather_children();
    void resolve_item_inputs();
    // Fills column_contributions_ for the items whose widths the solver
    // reads (an item stretched over fixed columns needs none).
    void measure_column_contributions(const SolveRequest& request, const GridLayoutWorkspace& workspace);
    void measure_row_contributions(const GridContainerInput& container, const GridLayoutWorkspace& workspace);
    // Assigns the in-flow children's slots, with relative offsets against
    // the content box.
    void publish_slots(const GridLayoutWorkspace& workspace, Size content_box);
    // Places the absolutely positioned children gathered for the pass in
    // their containing block (out_of_flow.hpp), registers the controls it
    // depends on with the window, and reports each child's resolution.
    void place_absolute_children(Size client);
    [[nodiscard]] GridContainerInput container_input(const SolveRequest& request) const noexcept;
    [[nodiscard]] const GridItemLayout& record_for(const Control& child) const noexcept;
    void forget_departed_children();

    ItemMap items_{};
    GridContainerStyle style_{};
    LayoutAlignment justify_content_{LayoutAlignment::normal};
    LayoutAlignment align_content_{LayoutAlignment::normal};
    LayoutAlignment justify_items_{LayoutAlignment::normal};
    LayoutAlignment align_items_{LayoutAlignment::normal};
    LengthPercentage row_gap_{};
    LengthPercentage column_gap_{};

    // The pass workspace, reserved when the child count grows and reused
    // (PLAN section 5): live length is the laid-out child count.
    // `placements_` holds each child's placement record for the pass, so
    // the solver's borrowed pointers stay valid whatever a measure callback
    // does to the item records.
    std::uint64_t pass_{};
    std::vector<Control::Ptr> children_{};
    std::vector<GridItemStyle> placements_{};
    std::vector<GridItemInput> inputs_{};
    std::vector<GridItemContribution> column_contributions_{};
    std::vector<GridItemContribution> row_contributions_{};
    // Measuring lays out into workspace_; arranging into arranged_, which
    // is kept for inspection with its children's runtime ids.
    GridLayoutWorkspace workspace_{};
    GridLayoutWorkspace arranged_{};
    std::vector<RuntimeId> arranged_ids_{};
    // The pass's absolutely positioned children, outside the solve, and
    // the controls their containing block depends on (registered with the
    // window while there are any).
    std::vector<Control::Ptr> absolute_children_{};
    std::vector<RuntimeId> dependency_path_{};
    bool registered_dependencies_{};
    GridSolveOutcome arranged_outcome_{GridSolveOutcome::solved};
};

} // namespace gui_forms
