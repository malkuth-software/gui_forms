#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/borrow/borrowed_view/borrowed_view.hpp"
#include "gui_forms/layout/style/layout_style.hpp"
#include "gui_forms/layout/types/layout_types.hpp"
#include "gui_forms/types/geometry/geometry.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace gui_forms {

// The block flow solver (stage L, phase L5; PLAN section 2.4): a CSS block
// formatting context over named records, with no control, painter or host
// dependency. BlockPanel realizes it over child controls.
//
// Block-level items stack on the block axis with their vertical margins
// collapsed (CSS 2.1 section 8.3.1, without floats or clearance) and their
// horizontal margins resolved (10.3.3). A run of consecutive inline-level
// items, each an atomic inline box, is broken into line boxes (9.4.2) at
// the boundaries between items, aligned by `text-align`, and aligned on the
// block axis by `vertical-align` (10.8): every line box starts with the
// container's strut. An inline item's baseline is the one it gives (its
// last line box's), or else the bottom edge of its margin box, CSS's
// baseline of an inline-block without line boxes.
//
// The caller resolves what needs style or content knowledge: each item's
// used border-box width and height (the filled or given width of a block
// item, the shrink-to-fit width of an inline item, min and max sizes) and,
// for an item whose own content shares this container's margins, the
// margins that collapse through its edges. Lengths are logical units;
// rectangles are border boxes relative to the container's content box.

// The most items one container lays out (PLAN section 5).
inline constexpr std::size_t maximum_block_flow_items = 65'536U;

enum class BlockFlowLevel : std::uint8_t {
    // A block-level box: stacked, margins collapsing.
    block,
    // An atomic inline-level box (an `inline-block` child or a text
    // control): placed on line boxes.
    inline_level,
};

// A set of adjoining vertical margins (8.3.1): the largest positive margin
// and the most negative one; the collapsed margin is their sum.
struct GUI_FORMS_API CollapsedMargin final {
    double positive{};
    // Zero or less.
    double negative{};

    // Adjoins one more margin or set.
    void adjoin(double margin) noexcept;
    void adjoin(const CollapsedMargin& other) noexcept;
    [[nodiscard]] double collapsed() const noexcept { return positive + negative; }

    friend constexpr bool operator==(const CollapsedMargin& left, const CollapsedMargin& right) noexcept {
        return left.positive == right.positive && left.negative == right.negative;
    }
};

// The strut every line box starts with (10.8.1): the container's ascent
// and descent above and below the baseline, as its font and line height
// give them, and the x-height `vertical-align: middle` aligns to. A zero
// strut is a point on the baseline (CSS's strut of a zero-size font).
struct GUI_FORMS_API LineStrut final {
    double ascent{};
    double descent{};
    double x_height{};

    friend constexpr bool operator==(const LineStrut& left, const LineStrut& right) noexcept {
        return left.ascent == right.ascent && left.descent == right.descent && left.x_height == right.x_height;
    }
};

// One item's inputs.
struct GUI_FORMS_API BlockFlowItemInput final {
    BlockFlowLevel level{BlockFlowLevel::block};
    // The used border-box size.
    double width{};
    double height{};
    // Resolved margins. A block item's `auto` left or right margin is
    // flagged and its length ignored; vertical and inline-level `auto`
    // margins are zero, as CSS computes them.
    BoxSides<double> margin{};
    bool auto_margin_left{};
    bool auto_margin_right{};
    // A block item whose content shares this container's margins (a nested
    // block flow container that is not a formatting-context root): the
    // margins of its content collapsing through its top edge and through
    // its bottom edge, its own margins excluded.
    CollapsedMargin leading_inner{};
    CollapsedMargin trailing_inner{};
    // Whether the item's top and bottom margins are adjoining (8.3.1: an
    // empty block with zero height). Then leading_inner holds all of its
    // content's margins and trailing_inner is empty.
    bool collapses_through{};
    InlineVerticalAlignment vertical_align{InlineVerticalAlignment::baseline};
    // An inline item's baseline from the top of its border box (its last
    // line box's, Control::last_baseline); NaN for none, in which case the
    // solver synthesizes the bottom of its margin box. Block items ignore
    // it.
    double baseline{std::numeric_limits<double>::quiet_NaN()};
};

struct GUI_FORMS_API BlockFlowContainerInput final {
    // The content box's width the items are placed in.
    double width{};
    TextAlignment text_align{TextAlignment::start};
    // Left to right or right to left; `inherit` reads as left to right.
    LayoutDirection layout_direction{LayoutDirection::left_to_right};
    LineStrut strut{};
    // Whether the container's top margin adjoins its first item's (8.3.1:
    // it is not a formatting-context root and has no top padding). Then the
    // margins before the first line box or nonempty block collapse through
    // the container's top: they are published as leading_margin and the
    // items are placed without them.
    bool shares_leading_margin{};
};

// One item's result.
struct GUI_FORMS_API BlockFlowItemResult final {
    // The border box in the container's content box.
    Rect bounds{};
    // Its line, for an inline-level item; no_block_flow_line for a block.
    std::uint32_t line{};
};

inline constexpr std::uint32_t no_block_flow_line = 0xFFFF'FFFFU;

// One line box, its items being items[first, first + count).
struct GUI_FORMS_API BlockFlowLine final {
    std::uint32_t first{};
    std::uint32_t count{};
    double top{};
    double height{};
    // The baseline, from the line's top.
    double baseline{};
    // The items' margin boxes' summed width, and the inline offset
    // text-align gave them, from the line's start edge.
    double used_width{};
    double offset{};
};

enum class BlockFlowSolveOutcome : std::uint8_t {
    solved,
    // More than maximum_block_flow_items: nothing was laid out.
    refused_too_many_items,
};

// Caller-owned storage solve_block_flow fills, reused from layout to layout
// (house style section 1): `items` has one record per input item, in input
// order; `lines` the line boxes in order. Its storage grows only when the
// item or line count does.
class GUI_FORMS_API BlockFlowLayoutWorkspace final {
public:
    [[nodiscard]] BorrowedSpan<BlockFlowItemResult> items() const& noexcept {
        return detail::lend_view(items_.data(), items_.size(), solve_lender_);
    }
    BorrowedSpan<BlockFlowItemResult> items() const&& = delete;
    [[nodiscard]] BorrowedSpan<BlockFlowLine> lines() const& noexcept {
        return detail::lend_view(lines_.data(), lines_.size(), solve_lender_);
    }
    BorrowedSpan<BlockFlowLine> lines() const&& = delete;
    // The block end of the last line box or nonempty block, where the
    // content ends before its trailing margins.
    [[nodiscard]] double content_height() const noexcept { return content_height_; }
    // The margins that collapsed through the container's top (empty unless
    // the container shares its leading margin).
    [[nodiscard]] CollapsedMargin leading_margin() const noexcept { return leading_margin_; }
    // The margins after the content's end: the last block's bottom margin
    // with whatever collapses with it. A container whose bottom margin
    // adjoins them passes them on; any other adds their collapsed sum to
    // its height.
    [[nodiscard]] CollapsedMargin trailing_margin() const noexcept { return trailing_margin_; }
    // Whether no line box or nonempty block closed the leading margins: with
    // a shared leading margin, every item's margins are then in
    // leading_margin and the container can collapse through.
    [[nodiscard]] bool leading_open() const noexcept { return leading_open_; }
    // Reserves storage for `count` items and as many lines, so a later
    // layout of at most that many allocates nothing.
    void reserve(std::size_t count);

private:
    friend GUI_FORMS_API BlockFlowSolveOutcome solve_block_flow(const BlockFlowContainerInput& container,
                                                  std::span<const BlockFlowItemInput> items,
                                                  BlockFlowLayoutWorkspace& workspace);

    std::vector<BlockFlowItemResult> items_{};
    std::vector<BlockFlowLine> lines_{};
    double content_height_{};
    CollapsedMargin leading_margin_{};
    CollapsedMargin trailing_margin_{};
    bool leading_open_{};
    // Stamps the views above per solve (borrow sanitizer, stage 3): the
    // next solve or reserve ends the views lent so far, and the sanitizer
    // tree reports a later use. Empty in the ordinary build.
    [[no_unique_address]] detail::BorrowLender solve_lender_{};
};

// Lays the items out. `items` is borrowed for the call. On refusal the
// workspace is cleared.
GUI_FORMS_API BlockFlowSolveOutcome solve_block_flow(const BlockFlowContainerInput& container,
                                       std::span<const BlockFlowItemInput> items,
                                       BlockFlowLayoutWorkspace& workspace);

enum class BlockFlowIntrinsicKind : std::uint8_t { min_content, max_content };

// The container's min-content or max-content width (CSS Sizing 3): the
// widest block item's margin box, and of each inline run the widest item
// (min-content, every item boundary being a break) or the items' sum
// (max-content). Each item's `width` is its contribution for that kind.
[[nodiscard]] GUI_FORMS_API double block_flow_intrinsic_width(std::span<const BlockFlowItemInput> items,
                                                BlockFlowIntrinsicKind kind) noexcept;

} // namespace gui_forms
