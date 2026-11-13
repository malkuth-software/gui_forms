// The block flow solver (stage L, phase L5): one pass over the items in
// order. Block items are placed with their collapsed margins; each run of
// inline-level items is broken into line boxes as it is met. The pass keeps
// three values: the block position where the content so far ends, the
// margins pending after it, and whether the leading margins are still open
// (no line box or nonempty block yet, with a leading margin shared with the
// container's parent).

#include "gui_forms/layout/block_flow_solver/block_flow_solver.hpp"

#include <algorithm>
#include <cmath>

namespace gui_forms {

namespace {

// A line fits an item when the line's width is exceeded by at most this:
// half of Chromium's LayoutUnit (1/64 px), which absorbs the rounding of
// fractional widths that add up to the line exactly (three items of
// 33.333... in 100).
constexpr double line_fit_tolerance = 1.0 / 128.0;

struct FlowState final {
    double cursor{};
    CollapsedMargin pending{};
    CollapsedMargin leading{};
    bool open{};
};

double used_left_margin(const BlockFlowItemInput& item) noexcept {
    return item.auto_margin_left ? 0.0 : item.margin.left;
}

double used_right_margin(const BlockFlowItemInput& item) noexcept {
    return item.auto_margin_right ? 0.0 : item.margin.right;
}

double outer_width(const BlockFlowItemInput& item) noexcept {
    return item.width + used_left_margin(item) + used_right_margin(item);
}

double outer_height(const BlockFlowItemInput& item) noexcept {
    return item.margin.top + item.height + item.margin.bottom;
}

// CSS 2.1 10.3.3: a block item's border-box left edge. Auto margins share
// what is left; when nothing is left they are zero, and an over-constrained
// item keeps its start margin.
double block_item_x(const BlockFlowItemInput& item, double available, bool right_to_left) noexcept {
    const double left = used_left_margin(item);
    const double right = used_right_margin(item);
    const double free = available - item.width - left - right;
    const double from_left = left;
    const double from_right = available - right - item.width;
    if (free >= 0.0) {
        if (item.auto_margin_left && item.auto_margin_right) return free / 2.0;
        if (item.auto_margin_left) return from_right;
        if (item.auto_margin_right) return from_left;
    }
    return right_to_left ? from_right : from_left;
}

void place_block_item(const BlockFlowContainerInput& container, const BlockFlowItemInput& item,
                      BlockFlowItemResult& result, FlowState& state) {
    CollapsedMargin top{};
    top.adjoin(item.margin.top);
    top.adjoin(item.leading_inner);
    double y = 0.0;
    if (state.open) {
        // The margins so far collapse through the container's top: the
        // item sits at the content's start.
        state.leading.adjoin(top);
        y = state.cursor;
        if (item.collapses_through) {
            state.leading.adjoin(item.margin.bottom);
        } else {
            state.open = false;
            state.cursor = y + item.height;
            state.pending = item.trailing_inner;
            state.pending.adjoin(item.margin.bottom);
        }
    } else {
        CollapsedMargin before = state.pending;
        before.adjoin(top);
        y = state.cursor + before.collapsed();
        if (item.collapses_through) {
            // Placed as if it had a bottom border (8.3.1); its margins join
            // the pending ones and the content's end does not move.
            state.pending = before;
            state.pending.adjoin(item.margin.bottom);
        } else {
            state.cursor = y + item.height;
            state.pending = item.trailing_inner;
            state.pending.adjoin(item.margin.bottom);
        }
    }
    const bool right_to_left = container.layout_direction == LayoutDirection::right_to_left;
    const double x = block_item_x(item, container.width, right_to_left);
    result.bounds = Rect{x, y, item.width, item.height};
    result.line = no_block_flow_line;
}

// The inline offset of a line's items from its start edge (CSS Text 3,
// `text-align`): content wider than the line is start-aligned.
double line_offset(TextAlignment alignment, double free, bool right_to_left) noexcept {
    const double room = std::max(0.0, free);
    switch (alignment) {
    case TextAlignment::start:
        return 0.0;
    case TextAlignment::end:
        return room;
    case TextAlignment::left:
        return right_to_left ? room : 0.0;
    case TextAlignment::right:
        return right_to_left ? 0.0 : room;
    case TextAlignment::center:
        return room / 2.0;
    }
    return 0.0;
}

// The line's ascent and descent above and below its baseline (10.8.1):
// the strut's, widened by each baseline- or middle-aligned item's margin
// box, then by the tallest top- or bottom-aligned item where it is taller
// than the line (Chromium's order: the tallest decides, the first of
// equals, a top item extending the descent and a bottom item the ascent).
struct LineMetrics final {
    double ascent{};
    double descent{};
};

// How far a baseline-aligned item's margin box reaches above its baseline:
// its top margin and its own baseline, or its whole margin box when it has
// none.
double item_ascent(const BlockFlowItemInput& item) noexcept {
    if (std::isnan(item.baseline)) return outer_height(item);
    const double ascent = item.margin.top + item.baseline;
    return ascent;
}

LineMetrics line_metrics(const LineStrut& strut, std::span<const BlockFlowItemInput> line) noexcept {
    LineMetrics metrics{strut.ascent, strut.descent};
    double tallest = 0.0;
    bool tallest_top = false;
    bool any_aligned_to_line = false;
    for (const BlockFlowItemInput& item : line) {
        const double height = outer_height(item);
        switch (item.vertical_align) {
        case InlineVerticalAlignment::baseline:
        default: { // Not an InlineVerticalAlignment: the item sits on the baseline, the initial value.
            const double ascent = item_ascent(item);
            metrics.ascent = std::max(metrics.ascent, ascent);
            metrics.descent = std::max(metrics.descent, height - ascent);
            break;
        }
        case InlineVerticalAlignment::middle: {
            const double half = height / 2.0;
            metrics.ascent = std::max(metrics.ascent, half + strut.x_height / 2.0);
            metrics.descent = std::max(metrics.descent, half - strut.x_height / 2.0);
            break;
        }
        case InlineVerticalAlignment::top:
        case InlineVerticalAlignment::bottom:
            if (!any_aligned_to_line || height > tallest) {
                tallest = height;
                tallest_top = item.vertical_align == InlineVerticalAlignment::top;
                any_aligned_to_line = true;
            }
            break;
        }
    }
    if (any_aligned_to_line && tallest > metrics.ascent + metrics.descent) {
        if (tallest_top) {
            metrics.descent = tallest - metrics.ascent;
        } else {
            metrics.ascent = tallest - metrics.descent;
        }
    }
    return metrics;
}

// The top of an item's border box from its line's top.
double inline_item_top(const BlockFlowItemInput& item, const LineMetrics& metrics, double x_height) noexcept {
    const double height = outer_height(item);
    double margin_top = 0.0;
    switch (item.vertical_align) {
    case InlineVerticalAlignment::baseline:
    default: // Not an InlineVerticalAlignment: the item sits on the baseline, the initial value.
        margin_top = metrics.ascent - item_ascent(item);
        break;
    case InlineVerticalAlignment::middle:
        margin_top = metrics.ascent - x_height / 2.0 - height / 2.0;
        break;
    case InlineVerticalAlignment::top:
        margin_top = 0.0;
        break;
    case InlineVerticalAlignment::bottom:
        margin_top = metrics.ascent + metrics.descent - height;
        break;
    }
    return margin_top + item.margin.top;
}

// Breaks items[first, end), all inline-level, into line boxes from the
// content's current end.
void place_inline_run(const BlockFlowContainerInput& container, std::span<const BlockFlowItemInput> items,
                      std::size_t first, std::size_t end, std::vector<BlockFlowItemResult>& results,
                      std::vector<BlockFlowLine>& lines, FlowState& state) {
    double top = state.cursor;
    if (state.open) {
        state.open = false;
    } else {
        top += state.pending.collapsed();
    }
    state.pending = CollapsedMargin{};
    const bool right_to_left = container.layout_direction == LayoutDirection::right_to_left;
    const double available = container.width;
    std::size_t start = first;
    while (start < end) {
        double used = 0.0;
        std::size_t stop = start;
        while (stop < end) {
            const double outer = outer_width(items[stop]);
            if (stop > start && used + outer > available + line_fit_tolerance) break;
            used += outer;
            ++stop;
        }
        const std::span<const BlockFlowItemInput> line_items = items.subspan(start, stop - start);
        const LineMetrics metrics = line_metrics(container.strut, line_items);
        const double offset = line_offset(container.text_align, available - used, right_to_left);
        const std::uint32_t line_index = static_cast<std::uint32_t>(lines.size());
        double advance = offset;
        for (std::size_t index = start; index < stop; ++index) {
            const BlockFlowItemInput& item = items[index];
            const double x = right_to_left ? available - advance - item.margin.right - item.width
                                           : advance + item.margin.left;
            const double y = top + inline_item_top(item, metrics, container.strut.x_height);
            results[index].bounds = Rect{x, y, item.width, item.height};
            results[index].line = line_index;
            advance += outer_width(item);
        }
        BlockFlowLine line{};
        line.first = static_cast<std::uint32_t>(start);
        line.count = static_cast<std::uint32_t>(stop - start);
        line.top = top;
        line.height = metrics.ascent + metrics.descent;
        line.baseline = metrics.ascent;
        line.used_width = used;
        line.offset = offset;
        lines.push_back(line);
        top += line.height;
        start = stop;
    }
    state.cursor = top;
}

} // namespace

void CollapsedMargin::adjoin(double margin) noexcept {
    positive = std::max(positive, margin);
    negative = std::min(negative, margin);
}

void CollapsedMargin::adjoin(const CollapsedMargin& other) noexcept {
    positive = std::max(positive, other.positive);
    negative = std::min(negative, other.negative);
}

void BlockFlowLayoutWorkspace::reserve(std::size_t count) {
    solve_lender_.invalidate();
    items_.reserve(count);
    lines_.reserve(count);
}

BlockFlowSolveOutcome solve_block_flow(const BlockFlowContainerInput& container,
                                       std::span<const BlockFlowItemInput> items,
                                       BlockFlowLayoutWorkspace& workspace) {
    // A new solve: the views of the last one end (they may relocate below).
    workspace.solve_lender_.invalidate();
    workspace.lines_.clear();
    workspace.content_height_ = 0.0;
    workspace.leading_margin_ = CollapsedMargin{};
    workspace.trailing_margin_ = CollapsedMargin{};
    workspace.leading_open_ = false;
    if (items.size() > maximum_block_flow_items) {
        workspace.items_.clear();
        return BlockFlowSolveOutcome::refused_too_many_items;
    }
    const std::size_t count = items.size();
    workspace.items_.resize(count);
    FlowState state{};
    state.open = container.shares_leading_margin;
    std::size_t index = 0U;
    while (index < count) {
        if (items[index].level == BlockFlowLevel::inline_level) {
            std::size_t end = index;
            while (end < count && items[end].level == BlockFlowLevel::inline_level) ++end;
            place_inline_run(container, items, index, end, workspace.items_, workspace.lines_, state);
            index = end;
            continue;
        }
        place_block_item(container, items[index], workspace.items_[index], state);
        ++index;
    }
    workspace.content_height_ = state.cursor;
    workspace.leading_margin_ = state.leading;
    workspace.trailing_margin_ = state.open ? CollapsedMargin{} : state.pending;
    workspace.leading_open_ = state.open;
    return BlockFlowSolveOutcome::solved;
}

double block_flow_intrinsic_width(std::span<const BlockFlowItemInput> items, BlockFlowIntrinsicKind kind) noexcept {
    double widest = 0.0;
    double run = 0.0;
    for (const BlockFlowItemInput& item : items) {
        const double outer = outer_width(item);
        if (item.level == BlockFlowLevel::block) {
            widest = std::max(widest, run);
            run = 0.0;
            widest = std::max(widest, outer);
        } else if (kind == BlockFlowIntrinsicKind::min_content) {
            run = std::max(run, outer);
        } else {
            run += outer;
        }
    }
    widest = std::max(widest, run);
    return widest;
}

} // namespace gui_forms
