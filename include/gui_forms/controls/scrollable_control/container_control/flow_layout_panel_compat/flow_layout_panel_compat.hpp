#pragma once

#include "gui_forms/binding/child_property_view/child_property_view.hpp"
#include "gui_forms/controls/scrollable_control/container_control/container_control.hpp"
#include "gui_forms/controls/scrollable_control/container_control/flow_layout_panel_compat/flow_layout_panel_compat_values/flow_layout_panel_compat_values.hpp"
#include "gui_forms/controls/scrollable_control/container_control/flow_layout_panel_compat/flow_layout_panel_compat_capabilities/flow_layout_panel_compat_capabilities.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

namespace gui_forms {

// The flow break a flow container provides to its children (FlowBreak; a
// child property, study sections 10 and 15.4): whether the flow continues
// on a new line after the child. `(*flow).flow_break[*child] = true;`
template <typename Declaring>
struct FlowBreakCapability {
    ChildPropertyView<bool, FlowBreakCapability<Declaring>> flow_break;

    static void assign(FlowBreakCapability& capability, const Control& child, bool value) {
        Declaring& container = static_cast<Declaring&>(capability);
        container.prepare_write(std::as_const(capability.flow_break), child, value);
        if (!container.replace_flow_break(child, value)) return;
        container.written(std::as_const(capability.flow_break), child);
    }

    static bool read(const FlowBreakCapability& capability, const Control& child) {
        const Declaring& container = static_cast<const Declaring&>(capability);
        const bool value = container.stored_flow_break(child);
        return value;
    }
};

// The Windows Forms flow container: its children in lines along a flow
// direction, wrapped, broken and aligned. Its algorithm is its own, not the
// CSS flex solver's (notes:planning/stages/L-layout/README.md, "Decisions in
// force"). In a right-to-left effective layout direction the layout is
// computed left to right and mirrored about the viewport, padding and
// margins included: a `left_to_right` flow starts at the right edge.
class FlowLayoutPanelCompat final : public ContainerControl, public FlowBreakCapability<FlowLayoutPanelCompat>, public FlowLayoutPanelCompatCapabilities<FlowLayoutPanelCompat> {
public:
    explicit FlowLayoutPanelCompat(StableId stable_id);

    // The child properties it provides: flow_break.
    static constexpr std::size_t flow_child_property_count = 1U;
    [[nodiscard]] static const std::array<ChildPropertyEntry, flow_child_property_count>&
    flow_child_property_entries() noexcept;
    [[nodiscard]] std::span<const ChildPropertyEntry> child_property_table() const noexcept override;

    void set_flex_grow(const Control& child, double grow);
    [[nodiscard]] double flex_grow(const Control& child) const;

    [[nodiscard]] Size measure(Size available) override;
    void arrange(Rect final_bounds) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;

protected:
    void child_leaving(const Control& child) noexcept override;

private:
    friend struct FlowBreakCapability<FlowLayoutPanelCompat>;

    // The flow break's hooks: the container's thread and a live direct
    // child before the compare; the container's relayout after a change.
    void prepare_write(const ChildPropertyView<bool, FlowBreakCapability<FlowLayoutPanelCompat>>& property,
                       const Control& child, bool& incoming) const;
    void written(const ChildPropertyView<bool, FlowBreakCapability<FlowLayoutPanelCompat>>& property,
                 const Control& child);
    // Stores `child`'s flow break; false when it was already `value`.
    bool replace_flow_break(const Control& child, bool value);
    // `child`'s flow break; refuses a child that is not a live direct child.
    [[nodiscard]] bool stored_flow_break(const Control& child) const;
    // The flow break recorded under a child's runtime id; false when none.
    [[nodiscard]] bool recorded_flow_break(std::uint64_t id) const noexcept;

    // One child's records, its flow break and grow factor, kept while
    // either differs from the default; `seen_pass` is the layout pass that
    // last found the child, for reconciliation.
    struct ItemRecord final {
        bool flow_break{};
        double grow{};
        std::uint64_t seen_pass{};
    };
    using ItemMap = std::unordered_map<std::uint64_t, ItemRecord>;

    // One child as a pass lays it out: its desired size and margins.
    struct FlowItem final {
        Control::Ptr control{};
        Size desired{};
        Insets margin{};
        double grow{};
        bool break_after{};
    };
    // One line: the pass's items [first, first + count), the sum of their
    // outer main extents and spacing, and the largest outer cross extent.
    struct FlowLine final {
        std::size_t first{};
        std::size_t count{};
        double main{};
        double cross{};
    };

    // One pass's storage, reused from pass to pass: live lengths are the
    // pass's child, item and line counts.
    struct Workspace final {
        std::vector<Control::Ptr> retained{};
        std::vector<FlowItem> items{};
        std::vector<FlowLine> lines{};
    };

    // Lays the children out in the panel's workspace, or in a fresh one
    // when a child's measure lays the panel out again inside a pass.
    [[nodiscard]] Size layout_children(Size available, bool assign);
    [[nodiscard]] Size layout_pass(Size available, bool assign, Workspace& workspace);
    void reconcile_records(std::uint64_t pass);
    void erase_default_record(std::uint64_t id);

    ItemMap records_{};
    // The layout pass number, for reconciliation stamps.
    std::uint64_t pass_{};
    Workspace workspace_{};
    bool workspace_busy_{};
    // property_codemod begin FlowLayoutPanelCompat (tools/property_families/flow_layout_panel_compat.json; generated, do not edit)
public:
    // FlowLayoutPanelCompat's property table: Control's entries, then its own.
    static constexpr std::size_t flow_layout_panel_compat_property_count = Control::control_property_count + 5U;
    [[nodiscard]] static const std::array<PropertyEntry, flow_layout_panel_compat_property_count>& flow_layout_panel_compat_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

private:
    friend struct FlowLayoutDirectionCapability<FlowLayoutPanelCompat>;
    friend struct FlowLayoutWrapContentsCapability<FlowLayoutPanelCompat>;
    friend struct FlowLayoutItemSpacingCapability<FlowLayoutPanelCompat>;
    friend struct FlowLayoutMainAlignmentCapability<FlowLayoutPanelCompat>;
    friend struct FlowLayoutCrossAlignmentCapability<FlowLayoutPanelCompat>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(flow_direction)& cell, FlowDirection& incoming);
    void written(const decltype(flow_direction)& cell);
    void prepare_write(const decltype(wrap_contents)& cell, bool& incoming);
    void written(const decltype(wrap_contents)& cell);
    void prepare_write(const decltype(item_spacing)& cell, Size& incoming);
    void written(const decltype(item_spacing)& cell);
    void prepare_write(const decltype(main_alignment)& cell, FlowMainAlignment& incoming);
    void written(const decltype(main_alignment)& cell);
    void prepare_write(const decltype(cross_alignment)& cell, FlowCrossAlignment& incoming);
    void written(const decltype(cross_alignment)& cell);
    // property_codemod end FlowLayoutPanelCompat
};

} // namespace gui_forms
