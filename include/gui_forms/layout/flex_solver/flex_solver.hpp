#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/layout/style/layout_style.hpp"
#include "gui_forms/layout/types/layout_types.hpp"
#include "gui_forms/types/geometry/geometry.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace gui_forms {

// The flex solver (stage L, phase L3; PLAN sections 2.1 and 2.2): CSS
// Flexible Box Layout Level 1's layout algorithm (its section 9) over named
// records, with no control, painter or host dependency. FlexPanel
// realizes it over child controls; item systems may call it on their own
// records.
//
// One layout is two calls, because an item's cross size can depend on its
// final main size (wrapped text is taller when narrower):
// 1. resolve_flex_main_sizes: order, line breaking (9.3) and the flexible
//    lengths (9.7, the freeze loop, at most one round per item on a line).
//    Each item's main_size is then final.
// 2. The caller measures each item's hypothetical cross size at that main
//    size, then place_flex_items: line cross sizes (9.4), align-content,
//    stretch, main-axis alignment with auto margins (9.5), cross-axis and
//    baseline alignment (9.6), reversal and right-to-left mirroring.
//
// The caller resolves what needs style or content knowledge: the flex base
// size (9.2.3), the automatic minimum (4.5), percentages and the min and
// max sizes. All sizes are logical units of the item's border box (PLAN
// 2.5); the solver adds margins itself. Rectangles are relative to the
// container's content box.

// The most items one container lays out (PLAN section 5).
inline constexpr std::size_t maximum_flex_items = 65'536U;

// One margin as the solver reads it: a resolved length, or `auto`.
struct GUI_FORMS_API FlexMargin final {
    double length{};
    bool automatic{};

    friend constexpr bool operator==(const FlexMargin& left, const FlexMargin& right) noexcept {
        return left.length == right.length && left.automatic == right.automatic;
    }
};

// One item's inputs to both calls.
struct GUI_FORMS_API FlexItemInput final {
    FlexFactor grow{0.0};
    FlexFactor shrink{1.0};
    std::int32_t order{};
    // The flex base size (CSS Flexbox 9.2.3), resolved by the caller.
    double flex_base_size{};
    // The main size's resolved minimum, automatic minimum included, and its
    // maximum (infinity for `none`).
    double minimum_main{};
    double maximum_main{std::numeric_limits<double>::infinity()};
    double minimum_cross{};
    double maximum_cross{std::numeric_limits<double>::infinity()};
    // Physical sides.
    BoxSides<FlexMargin> margin{};
    // `auto` takes the container's align-items.
    LayoutAlignment align_self{LayoutAlignment::automatic};
    // Whether the item's cross size property is definite: such an item is
    // never stretched.
    bool definite_cross{};
};

// One item's measurement for the second call, at its resolved main size.
struct GUI_FORMS_API FlexItemCross final {
    // The cross size the item takes at its main size, before stretching.
    double hypothetical_cross{};
    // Its first baseline, from the top of its border box; NaN for none, in
    // which case the solver synthesizes one from the border box's bottom.
    double baseline{std::numeric_limits<double>::quiet_NaN()};
};

// One axis of the container's content box: a definite extent, or
// indefinite (sized from the items: the main axis never wraps then).
struct GUI_FORMS_API FlexAvailableExtent final {
    double extent{};
    bool definite{};
};

struct GUI_FORMS_API FlexContainerInput final {
    FlexDirection direction{FlexDirection::row};
    FlexWrap wrap{FlexWrap::nowrap};
    LayoutAlignment justify_content{LayoutAlignment::normal};
    LayoutAlignment align_content{LayoutAlignment::normal};
    LayoutAlignment align_items{LayoutAlignment::normal};
    // Resolved gaps: row_gap between lines of a row container and items of
    // a column one; column_gap the other way.
    double row_gap{};
    double column_gap{};
    FlexAvailableExtent width{};
    FlexAvailableExtent height{};
    // left_to_right or right_to_left (an `inherit` is resolved by the
    // caller): mirrors the inline axis at output (PLAN section 4).
    LayoutDirection layout_direction{LayoutDirection::left_to_right};
};

// Why an item's main size is what it is, for inspection (PLAN 3.4).
enum class FlexFreezeReason : std::uint8_t {
    // Its factor is zero, or its base size is already past its
    // hypothetical size in the direction of flexing.
    inflexible,
    // Clamped to its minimum or maximum by the violation loop.
    minimum_violation,
    maximum_violation,
    // Took its share of the free space.
    flexed,
};

// One item's results.
struct GUI_FORMS_API FlexItemResult final {
    double flex_base_size{};
    double hypothetical_main{};
    double main_size{};
    double cross_size{};
    // The item's border box in the container's content box.
    Rect bounds{};
    std::uint32_t line{};
    FlexFreezeReason freeze_reason{FlexFreezeReason::inflexible};
};

// One flex line, its items being order[first, first + count).
struct GUI_FORMS_API FlexLine final {
    std::uint32_t first{};
    std::uint32_t count{};
    // The sum of its items' outer main sizes and gaps.
    double main_extent{};
    double cross_size{};
    // Its cross-start edge, flow-relative (before reversal).
    double cross_position{};
};

enum class FlexSolveOutcome : std::uint8_t {
    solved,
    // More than maximum_flex_items: nothing was laid out.
    refused_too_many_items,
    // place_flex_items without a matching first call, or with a cross
    // record count other than the item count: nothing was placed.
    refused_mismatched_input,
};

// Caller-owned storage both calls fill, reused from layout to layout (house
// style section 1): `items` has one record per input item, in input order;
// `order` the input positions in (order, position) order; `lines` the lines
// in order. Its storage grows only when the item count does.
class GUI_FORMS_API FlexLayoutWorkspace final {
public:
    [[nodiscard]] std::span<const FlexItemResult> items() const& noexcept {
        return std::span<const FlexItemResult>(items_.data(), items_.size());
    }
    std::span<const FlexItemResult> items() const&& = delete;
    [[nodiscard]] std::span<const FlexLine> lines() const& noexcept {
        return std::span<const FlexLine>(lines_.data(), lines_.size());
    }
    std::span<const FlexLine> lines() const&& = delete;
    [[nodiscard]] std::span<const std::uint32_t> order() const& noexcept {
        return std::span<const std::uint32_t>(order_.data(), order_.size());
    }
    std::span<const std::uint32_t> order() const&& = delete;
    // The content's extent: the longest line's main extent, and the lines'
    // cross sizes with the gaps between them; filled by each call.
    [[nodiscard]] double content_main() const noexcept { return content_main_; }
    [[nodiscard]] double content_cross() const noexcept { return content_cross_; }
    // Reserves storage for `count` items, so a later layout of at most that
    // many allocates nothing.
    void reserve(std::size_t count);

private:
    friend GUI_FORMS_API FlexSolveOutcome resolve_flex_main_sizes(const FlexContainerInput& container,
                                                    std::span<const FlexItemInput> items,
                                                    FlexLayoutWorkspace& workspace);
    friend GUI_FORMS_API FlexSolveOutcome place_flex_items(const FlexContainerInput& container,
                                             std::span<const FlexItemInput> items,
                                             std::span<const FlexItemCross> crosses,
                                             FlexLayoutWorkspace& workspace);

    std::vector<FlexItemResult> items_{};
    std::vector<FlexLine> lines_{};
    std::vector<std::uint32_t> order_{};
    // Per item, for the freeze loop: whether it is frozen, and its target
    // main size before clamping.
    std::vector<std::uint8_t> frozen_{};
    std::vector<double> unclamped_{};
    double content_main_{};
    double content_cross_{};
};

// The first call. Fills each item's flex_base_size, hypothetical_main,
// main_size, line and freeze_reason, the order and the lines, and
// content_main. `items` is borrowed for the call.
GUI_FORMS_API FlexSolveOutcome resolve_flex_main_sizes(const FlexContainerInput& container, std::span<const FlexItemInput> items,
                                         FlexLayoutWorkspace& workspace);

// The second call, after the first with the same container and items;
// `crosses` has one record per item. Fills each item's cross_size and
// bounds, the lines' cross sizes and positions, and content_cross.
GUI_FORMS_API FlexSolveOutcome place_flex_items(const FlexContainerInput& container, std::span<const FlexItemInput> items,
                                  std::span<const FlexItemCross> crosses, FlexLayoutWorkspace& workspace);

} // namespace gui_forms
