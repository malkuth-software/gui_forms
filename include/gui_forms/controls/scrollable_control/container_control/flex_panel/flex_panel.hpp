#pragma once

#include "gui_forms/controls/scrollable_control/container_control/container_control.hpp"
#include "gui_forms/layout/flex_solver/flex_solver.hpp"
#include "gui_forms/layout/positioning/positioning.hpp"
#include "gui_forms/layout/style/layout_style.hpp"
#include "gui_forms/controls/scrollable_control/container_control/flex_panel/flex_panel_capabilities/flex_panel_capabilities.hpp"

#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace gui_forms {

// One child's flex item properties (CSS Flexbox's names; PLAN section 2.2).
// The panel keeps one per child, keyed by its runtime id and dropped when
// the child leaves. Margins are the child's own `margin`; `auto_margin`
// marks the sides that are `auto` instead. The child's minimum_size and
// maximum_size still bound it after these sizes are applied.
struct FlexItemLayout final {
    // flex-grow, flex-shrink, flex-basis and order.
    FlexItemStyle flex{};
    // `auto` takes the panel's align_items.
    LayoutAlignment align_self{LayoutAlignment::automatic};
    // CSS width and height: `auto` sizes the item from its content; a
    // length or a percentage of the panel's content box is definite.
    SizeValue width{};
    SizeValue height{};
    // `auto` is CSS's automatic minimum, the content's minimum size (owner
    // decision L-Q3); a length sets it.
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

    friend bool operator==(const FlexItemLayout& left, const FlexItemLayout& right) noexcept {
        return left.flex == right.flex && left.align_self == right.align_self && left.width == right.width &&
               left.height == right.height && left.min_width == right.min_width &&
               left.min_height == right.min_height && left.max_width == right.max_width &&
               left.max_height == right.max_height && left.auto_margin == right.auto_margin && left.positioning == right.positioning;
    }
};

// A container that lays its children out as a CSS flex container (stage L,
// phase L3): one or more lines along a main axis, with growing, shrinking,
// gaps and box alignment, through the core flex solver.
class FlexPanel final : public ContainerControl, public FlexPanelCapabilities<FlexPanel> {
public:
    explicit FlexPanel(StableId stable_id);

    // The child property it provides: item_style, the item layout as one
    // CSS inline style (margin lengths stay the child's own margin).
    [[nodiscard]] std::span<const ChildPropertyEntry> child_property_table() const noexcept override;

    // A direct child's item properties; a child without its own record has
    // the default one.
    void set_item_layout(const Control& child, const FlexItemLayout& layout);
    [[nodiscard]] FlexItemLayout item_layout(const Control& child) const;
    // The child's `position` and insets, without copying its whole record;
    // static for a control that is not a live direct child.
    [[nodiscard]] ItemPositioning item_positioning(const Control& child) const noexcept;

    // The last arrange's resolution (PLAN 3.4): per laid-out child, in the
    // order of `laid_out_children`, its base, hypothetical and final sizes,
    // line and freeze reason; and the lines. Both borrow the panel and
    // last until its next arrange.
    [[nodiscard]] const FlexLayoutWorkspace& layout_snapshot() const& noexcept { return arranged_; }
    const FlexLayoutWorkspace& layout_snapshot() const&& = delete;
    [[nodiscard]] std::span<const RuntimeId> laid_out_children() const& noexcept { return arranged_ids_; }
    std::span<const RuntimeId> laid_out_children() const&& = delete;

    [[nodiscard]] Size measure(Size available) override;
    void arrange(Rect final_bounds) override;
    // The arrange asks each item its max-content size and its height at
    // the flexed width; the window's at-most measure would go unread.
    [[nodiscard]] bool measures_children_in_arrange() const noexcept override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;

protected:
    [[nodiscard]] Size measure_intrinsic(const LayoutConstraint& constraint) override;

private:
    [[nodiscard]] BindingValue style_property() const;
    void set_style_property(const BindingValue& value);

    struct ItemRecord final {
        FlexItemLayout layout{};
        // The layout pass that last found the child, for reconciliation.
        std::uint64_t seen_pass{};
    };
    using ItemMap = std::unordered_map<std::uint64_t, ItemRecord>;

    // What one child measured as for the pass: its max-content size, and
    // its min-content main size when its automatic minimum needs it.
    struct ItemContent final {
        Size max_content{};
        double min_content_main{};
    };

    // The content-box extents the solver lays out against, and whether to
    // publish child slots.
    struct SolveRequest final {
        FlexAvailableExtent width{};
        FlexAvailableExtent height{};
        bool assign{};
    };

    // One layout into `workspace`: gathers the children and their records,
    // measures them, runs both solver calls and, when `assign`, publishes
    // their slots. Returns the content box's used size.
    Size solve(const SolveRequest& request, FlexLayoutWorkspace& workspace);
    // The content's max-content (or min-content) width, for the panel's own
    // intrinsic width: its items' contributions, side by side in a row.
    double intrinsic_content_width(bool minimum);
    void gather_children();
    void resolve_item_inputs(const SolveRequest& request);
    void measure_item_crosses(const SolveRequest& request, const FlexLayoutWorkspace& workspace);
    // Assigns the in-flow children's slots, with relative offsets against
    // the content box, moved by overflow_shift_.
    void publish_slots(const FlexLayoutWorkspace& workspace, Size content_box);
    // The smallest rectangle holding every laid-out item's margin box, in
    // the content box's coordinates (it may start left of or above the
    // box, where a reversed axis overflows); empty with no items.
    [[nodiscard]] Rect content_margin_bounds(const FlexLayoutWorkspace& workspace) const noexcept;
    // Places the absolutely positioned children gathered for the pass in
    // their containing block (out_of_flow.hpp), registers the controls it
    // depends on with the window, and reports each child's resolution.
    void place_absolute_children(Size client);
    [[nodiscard]] FlexContainerInput container_input(const SolveRequest& request) const noexcept;
    [[nodiscard]] const FlexItemLayout& record_for(const Control& child) const noexcept;
    void forget_departed_children();

    ItemMap items_{};

    // The pass workspace, reserved when the child count grows and reused
    // (PLAN section 5): live length is the laid-out child count.
    std::uint64_t pass_{};
    std::vector<Control::Ptr> children_{};
    std::vector<FlexItemInput> inputs_{};
    std::vector<ItemContent> contents_{};
    std::vector<FlexItemCross> crosses_{};
    // Measuring lays out into workspace_; arranging into arranged_, which
    // is kept for inspection with its children's runtime ids.
    FlexLayoutWorkspace workspace_{};
    FlexLayoutWorkspace arranged_{};
    std::vector<RuntimeId> arranged_ids_{};
    // The pass's absolutely positioned children, outside the solve, and
    // the controls their containing block depends on (registered with the
    // window while there are any).
    std::vector<Control::Ptr> absolute_children_{};
    std::vector<RuntimeId> dependency_path_{};
    bool registered_dependencies_{};
    // With auto_scroll, how far the items move right and down so content
    // that overflows the content box's start (a reversed direction or
    // wrap-reverse) lies inside the scroll extent; zero otherwise.
    Point overflow_shift_{};
    // property_codemod begin FlexPanel (tools/property_families/flex_panel.json; generated, do not edit)
public:
    // FlexPanel's property table: Control's entries, then its own.
    static constexpr std::size_t flex_panel_property_count = Control::control_property_count + 1U;
    [[nodiscard]] static const std::array<PropertyEntry, flex_panel_property_count>& flex_panel_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

private:
    friend struct FlexPanelDirectionCapability<FlexPanel>;
    friend struct FlexPanelWrapCapability<FlexPanel>;
    friend struct FlexPanelJustifyContentCapability<FlexPanel>;
    friend struct FlexPanelAlignContentCapability<FlexPanel>;
    friend struct FlexPanelAlignItemsCapability<FlexPanel>;
    friend struct FlexPanelRowGapCapability<FlexPanel>;
    friend struct FlexPanelColumnGapCapability<FlexPanel>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(flex_direction)& cell, FlexDirection& incoming);
    void written(const decltype(flex_direction)& cell);
    void prepare_write(const decltype(flex_wrap)& cell, FlexWrap& incoming);
    void written(const decltype(flex_wrap)& cell);
    void prepare_write(const decltype(justify_content)& cell, LayoutAlignment& incoming);
    void written(const decltype(justify_content)& cell);
    void prepare_write(const decltype(align_content)& cell, LayoutAlignment& incoming);
    void written(const decltype(align_content)& cell);
    void prepare_write(const decltype(align_items)& cell, LayoutAlignment& incoming);
    void written(const decltype(align_items)& cell);
    void prepare_write(const decltype(row_gap)& cell, LengthPercentage& incoming);
    void written(const decltype(row_gap)& cell);
    void prepare_write(const decltype(column_gap)& cell, LengthPercentage& incoming);
    void written(const decltype(column_gap)& cell);
    // property_codemod end FlexPanel
};

} // namespace gui_forms
