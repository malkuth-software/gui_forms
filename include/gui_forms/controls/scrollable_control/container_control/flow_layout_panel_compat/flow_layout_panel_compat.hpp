#pragma once

#include "gui_forms/controls/scrollable_control/container_control/container_control.hpp"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace gui_forms {

enum class FlowDirection : std::uint8_t {
    left_to_right,
    right_to_left,
    top_down,
    bottom_up,
};

enum class FlowMainAlignment : std::uint8_t {
    start,
    center,
    end,
    space_between,
    space_around,
    space_evenly,
};

enum class FlowCrossAlignment : std::uint8_t {
    start,
    center,
    end,
    stretch,
};

// The Windows Forms flow container: its children in lines along a flow
// direction, wrapped, broken and aligned. Its algorithm is its own, not the
// CSS flex solver's (notes:planning/stages/L-layout/README.md, "Decisions in
// force"). In a right-to-left effective layout direction the layout is
// computed left to right and mirrored about the viewport, padding and
// margins included: a `left_to_right` flow starts at the right edge.
class FlowLayoutPanelCompat final : public ContainerControl {
public:
    explicit FlowLayoutPanelCompat(StableId stable_id);

    [[nodiscard]] FlowDirection flow_direction() const noexcept {
        return flow_direction_;
    }
    void set_flow_direction(FlowDirection direction);
    [[nodiscard]] bool wrap_contents() const noexcept { return wrap_contents_; }
    void set_wrap_contents(bool wrap);
    [[nodiscard]] Size item_spacing() const noexcept { return item_spacing_; }
    void set_item_spacing(Size spacing);
    [[nodiscard]] FlowMainAlignment main_alignment() const noexcept {
        return main_alignment_;
    }
    void set_main_alignment(FlowMainAlignment alignment);
    [[nodiscard]] FlowCrossAlignment cross_alignment() const noexcept {
        return cross_alignment_;
    }
    void set_cross_alignment(FlowCrossAlignment alignment);
    void set_flow_break(const Control& child, bool flow_break);
    [[nodiscard]] bool flow_break(const Control& child) const;
    void set_flex_grow(const Control& child, double grow);
    [[nodiscard]] double flex_grow(const Control& child) const;

    [[nodiscard]] Size measure(Size available) override;
    void arrange(Rect final_bounds) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;

private:
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
    FlowDirection flow_direction_{FlowDirection::left_to_right};
    FlowMainAlignment main_alignment_{FlowMainAlignment::start};
    FlowCrossAlignment cross_alignment_{FlowCrossAlignment::start};
    Size item_spacing_{};
    bool wrap_contents_{true};
};

} // namespace gui_forms
