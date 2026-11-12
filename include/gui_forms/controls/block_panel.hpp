#pragma once

#include "gui_forms/controls/container_control.hpp"
#include "gui_forms/layout/block_flow_solver/block_flow_solver.hpp"
#include "gui_forms/layout/positioning/positioning.hpp"
#include "gui_forms/layout/style/layout_style.hpp"
#include "gui_forms/controls/block_panel_capabilities.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace gui_forms {

// One child's block flow properties (CSS's names; PLAN section 2.4). The
// panel keeps one per child, keyed by its runtime id and dropped when the
// child leaves. Margins are the child's own `margin`; `auto_margin` marks
// the sides that are `auto` instead, which matters only for a block item's
// left and right (CSS 2.1 10.3.3; any other `auto` margin is zero). The
// child's minimum_size and maximum_size bound its used size as its own min
// and max sizes do.
struct BlockFlowItemLayout final {
    // `display: block` (the default) or an atomic inline box, CSS's
    // `inline-block`.
    BlockFlowLevel level{BlockFlowLevel::block};
    // CSS width: `auto` fills the panel less the margins for a block item
    // and shrinks to fit the content for an inline one; a length or a
    // percentage of the panel's content width is definite.
    SizeValue width{};
    // CSS height: `auto` is the content's height at the used width; a
    // length, or a percentage of a definite panel height (otherwise `auto`).
    SizeValue height{};
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
    // An inline item's alignment on its line.
    InlineVerticalAlignment vertical_align{InlineVerticalAlignment::baseline};

    friend bool operator==(const BlockFlowItemLayout& left, const BlockFlowItemLayout& right) noexcept {
        return left.level == right.level && left.width == right.width && left.height == right.height &&
               left.min_width == right.min_width && left.min_height == right.min_height &&
               left.max_width == right.max_width && left.max_height == right.max_height &&
               left.auto_margin == right.auto_margin && left.positioning == right.positioning && left.vertical_align == right.vertical_align;
    }
};

// The margins a nested BlockPanel's content shares with the block flow
// that lays it out (CSS 2.1 8.3.1), its own margins excluded.
struct BlockFlowSharedMargins final {
    // Collapsing through its top edge (empty with top padding).
    CollapsedMargin leading{};
    // Collapsing through its bottom edge (empty with bottom padding, or
    // when its own minimum or maximum changed its content's height).
    CollapsedMargin trailing{};
    // No line box or nonempty block closed the leading margins: at a zero
    // height the panel's own top and bottom margins adjoin.
    bool open{};

    friend bool operator==(const BlockFlowSharedMargins& left, const BlockFlowSharedMargins& right) noexcept {
        return left.leading == right.leading && left.trailing == right.trailing && left.open == right.open;
    }
};

// A container that lays its children out as a CSS block formatting context
// (stage L, phase L5): block items stacked with collapsing margins, and runs
// of inline items broken into line boxes, through the core block flow
// solver. A BlockPanel laid out as a block item of another, without
// auto_scroll, is not a formatting-context root: its first and last
// children's margins collapse with its own where no padding separates them,
// as nested CSS blocks do. Every other BlockPanel is a root.
class BlockPanel final : public ContainerControl, public BlockPanelCapabilities<BlockPanel> {
public:
    explicit BlockPanel(StableId stable_id);

    // The child property it provides: item_style, the item layout as one
    // CSS inline style (margin lengths stay the child's own margin).
    [[nodiscard]] std::span<const ChildPropertyEntry> child_property_table() const noexcept override;

    // text_align, line_strut and font are capabilities (block_panel_capabilities).
    // The strut the line boxes use: the set one, or the font's.
    [[nodiscard]] LineStrut used_line_strut() const;
    // The font the panel's inline content is set in, for the strut: the
    // panel's font, or the theme's field typography when none is set.
    [[nodiscard]] FontSpec resolved_font() const noexcept;

    // A live direct child's record; throws std::invalid_argument for any
    // other child or for a value the panel does not admit (content keywords,
    // a negative or non-finite length, `none` outside a maximum, `auto` as a
    // maximum).
    void set_item_layout(const Control& child, const BlockFlowItemLayout& layout);
    [[nodiscard]] BlockFlowItemLayout item_layout(const Control& child) const;
    // The child's `position` and insets, without copying its whole record;
    // static for a control that is not a live direct child.
    [[nodiscard]] ItemPositioning item_positioning(const Control& child) const noexcept;

    // The last arrange's resolution: one record per laid-out child, in
    // laid_out_children's order, and the line boxes. Both borrow this
    // panel's storage until its next arrange.
    [[nodiscard]] const BlockFlowLayoutWorkspace& layout_snapshot() const& noexcept { return arranged_; }
    const BlockFlowLayoutWorkspace& layout_snapshot() const&& = delete;
    [[nodiscard]] std::span<const RuntimeId> laid_out_children() const& noexcept { return arranged_ids_; }
    std::span<const RuntimeId> laid_out_children() const&& = delete;
    [[nodiscard]] BlockFlowSolveOutcome layout_outcome() const noexcept { return arranged_outcome_; }

    // The margins this panel's content shares with its parent block flow
    // when laid out at the border-box `width`, for that parent to collapse
    // with its own; all empty when it shares none. It answers from the
    // panel's last measure at that width while nothing changed since, and
    // otherwise lays the content out again (children's measures answer
    // from their caches).
    [[nodiscard]] BlockFlowSharedMargins shared_margins(double width);

    [[nodiscard]] Size measure(Size available) override;
    void arrange(Rect final_bounds) override;
    [[nodiscard]] bool measures_children_in_arrange() const noexcept override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;

protected:
    [[nodiscard]] Size measure_intrinsic(const LayoutConstraint& constraint) override;

private:
    [[nodiscard]] BindingValue style_property() const;
    void set_style_property(const BindingValue& value);
    struct ItemRecord final {
        BlockFlowItemLayout layout{};
        std::uint64_t seen_pass{};
    };
    using ItemMap = std::unordered_map<std::uint64_t, ItemRecord>;

    struct SolveRequest final {
        // The content box's width.
        double width{};
        // The content box's height, read by percentage heights when
        // height_definite.
        double height{};
        bool height_definite{};
        bool assign{};
    };

    struct SolveResult final {
        // The content's height before the panel's own minimum and maximum:
        // with the trailing margins when it does not share them.
        double content_height{};
        BlockFlowSharedMargins shared{};
        BlockFlowSolveOutcome outcome{BlockFlowSolveOutcome::solved};
    };

    // One answer of shared_margins, valid while the panel's measure
    // generation is unchanged.
    struct SharedMarginsEntry final {
        double width{};
        std::uint64_t generation{};
        BlockFlowSharedMargins margins{};
        bool valid{};
    };

    SolveResult solve(const SolveRequest& request, BlockFlowLayoutWorkspace& workspace);
    double intrinsic_content_width(BlockFlowIntrinsicKind kind);
    void gather_children();
    void resolve_item_inputs(const SolveRequest& request);
    // Assigns the in-flow children's slots, with relative offsets against
    // the content box.
    void publish_slots(const BlockFlowLayoutWorkspace& workspace, Size content_box);
    // Places the absolutely positioned children gathered for the pass in
    // their containing block (out_of_flow.hpp), registers the controls it
    // depends on with the window, and reports each child's resolution.
    void place_absolute_children(Size client);
    [[nodiscard]] bool shares_parent_flow() const noexcept;
    [[nodiscard]] bool percentage_heights_definite() const noexcept;
    [[nodiscard]] const BlockFlowItemLayout& record_for(const Control& child) const noexcept;
    void forget_departed_children();

    ItemMap items_{};

    std::uint64_t pass_{};
    std::vector<Control::Ptr> children_{};
    std::vector<BlockFlowItemInput> inputs_{};
    BlockFlowLayoutWorkspace workspace_{};
    BlockFlowLayoutWorkspace arranged_{};
    std::vector<RuntimeId> arranged_ids_{};
    // The pass's absolutely positioned children, outside the solve, and
    // the controls their containing block depends on (registered with the
    // window while there are any).
    std::vector<Control::Ptr> absolute_children_{};
    std::vector<RuntimeId> dependency_path_{};
    bool registered_dependencies_{};
    BlockFlowSolveOutcome arranged_outcome_{BlockFlowSolveOutcome::solved};
    SharedMarginsEntry shared_entry_{};
    // property_codemod begin BlockPanel (tools/property_families/block_panel.json; generated, do not edit)
public:
    // BlockPanel's property table: ScrollableControl's entries, then its own.
    static constexpr std::size_t block_panel_property_count = ScrollableControl::scrollable_control_property_count + 1U;
    [[nodiscard]] static const std::array<PropertyEntry, block_panel_property_count>& block_panel_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

private:
    friend struct BlockPanelTextAlignCapability<BlockPanel>;
    friend struct BlockPanelLineStrutCapability<BlockPanel>;
    friend struct BlockPanelFontCapability<BlockPanel>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(text_align)& cell, TextAlignment& incoming);
    void written(const decltype(text_align)& cell);
    void prepare_write(const decltype(line_strut)& cell, std::optional<LineStrut>& incoming);
    void written(const decltype(line_strut)& cell);
    void prepare_write(const decltype(font)& cell, std::optional<FontSpec>& incoming);
    void written(const decltype(font)& cell);
    // property_codemod end BlockPanel
};

} // namespace gui_forms
