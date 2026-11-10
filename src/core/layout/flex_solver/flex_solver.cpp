// The flex solver (stage L, phase L3): CSS Flexible Box Layout Level 1,
// section 9, over caller-resolved records. Both calls work in flow-relative
// coordinates (main-start to main-end, cross-start to cross-end) and map to
// physical ones once, at output.

#include "gui_forms/layout/flex_solver/flex_solver.hpp"

#include <algorithm>
#include <cmath>

namespace gui_forms {

namespace {

// An item that overflows its line by less than this still fits: sums of
// logical lengths such as 0.1 + 0.2 must not break a line that CSS's
// fixed-point arithmetic (1/64 px) keeps whole.
constexpr double line_break_tolerance = 1.0e-7;

bool horizontal_main(FlexDirection direction) noexcept {
    return direction == FlexDirection::row || direction == FlexDirection::row_reverse;
}

bool reversed_direction(FlexDirection direction) noexcept {
    return direction == FlexDirection::row_reverse || direction == FlexDirection::column_reverse;
}

bool right_to_left(const FlexContainerInput& container) noexcept {
    return container.layout_direction == LayoutDirection::right_to_left;
}

// Whether main-start is the physical right (a row) or bottom (a column).
bool main_physically_reversed(const FlexContainerInput& container) noexcept {
    if (horizontal_main(container.direction)) {
        return reversed_direction(container.direction) != right_to_left(container);
    }
    return reversed_direction(container.direction);
}

// Whether cross-start is the physical bottom (a row) or right (a column).
bool cross_physically_reversed(const FlexContainerInput& container) noexcept {
    const bool wrap_reverse = container.wrap == FlexWrap::wrap_reverse;
    if (horizontal_main(container.direction)) return wrap_reverse;
    return wrap_reverse != right_to_left(container);
}

double main_gap(const FlexContainerInput& container) noexcept {
    return horizontal_main(container.direction) ? container.column_gap : container.row_gap;
}

double cross_gap(const FlexContainerInput& container) noexcept {
    return horizontal_main(container.direction) ? container.row_gap : container.column_gap;
}

FlexAvailableExtent main_available(const FlexContainerInput& container) noexcept {
    return horizontal_main(container.direction) ? container.width : container.height;
}

FlexAvailableExtent cross_available(const FlexContainerInput& container) noexcept {
    return horizontal_main(container.direction) ? container.height : container.width;
}

// An item's margins in flow-relative terms.
struct FlowMargins final {
    FlexMargin main_start{};
    FlexMargin main_end{};
    FlexMargin cross_start{};
    FlexMargin cross_end{};
};

FlowMargins flow_margins(const FlexContainerInput& container, const BoxSides<FlexMargin>& margin) noexcept {
    FlowMargins flow{};
    const bool main_reversed = main_physically_reversed(container);
    const bool cross_reversed = cross_physically_reversed(container);
    if (horizontal_main(container.direction)) {
        flow.main_start = main_reversed ? margin.right : margin.left;
        flow.main_end = main_reversed ? margin.left : margin.right;
        flow.cross_start = cross_reversed ? margin.bottom : margin.top;
        flow.cross_end = cross_reversed ? margin.top : margin.bottom;
    } else {
        flow.main_start = main_reversed ? margin.bottom : margin.top;
        flow.main_end = main_reversed ? margin.top : margin.bottom;
        flow.cross_start = cross_reversed ? margin.right : margin.left;
        flow.cross_end = cross_reversed ? margin.left : margin.right;
    }
    return flow;
}

// A margin's length, `auto` counting as zero.
double fixed_length(const FlexMargin& margin) noexcept {
    return margin.automatic ? 0.0 : margin.length;
}

double main_margins(const FlowMargins& margins) noexcept {
    return fixed_length(margins.main_start) + fixed_length(margins.main_end);
}

double cross_margins(const FlowMargins& margins) noexcept {
    return fixed_length(margins.cross_start) + fixed_length(margins.cross_end);
}

// CSS's clamp for min and max: the minimum wins when they conflict.
double clamp_size(double value, double minimum, double maximum) noexcept {
    const double limited = std::min(value, maximum);
    return std::max(minimum, limited);
}

// How one alignment value packs its subjects, flow-relative.
enum class Packing : std::uint8_t {
    start,
    end,
    center,
    stretch,
    baseline,
    space_between,
    space_around,
    space_evenly,
};

// `start` and `end` follow the writing mode, which is flow-relative start
// unless the axis is reversed by the flex direction or wrap (right-to-left
// reverses the writing mode and the flow alike, so it cancels).
Packing writing_mode_packing(LayoutAlignment alignment, bool axis_reversed) noexcept {
    const bool start = alignment == LayoutAlignment::start;
    if (start != axis_reversed) return Packing::start;
    return Packing::end;
}

Packing justify_packing(LayoutAlignment alignment, bool main_reversed_by_direction) noexcept {
    switch (alignment) {
    case LayoutAlignment::start:
    case LayoutAlignment::end:
        return writing_mode_packing(alignment, main_reversed_by_direction);
    case LayoutAlignment::flex_end:
        return Packing::end;
    case LayoutAlignment::center:
        return Packing::center;
    case LayoutAlignment::space_between:
        return Packing::space_between;
    case LayoutAlignment::space_around:
        return Packing::space_around;
    case LayoutAlignment::space_evenly:
        return Packing::space_evenly;
    case LayoutAlignment::automatic:
    case LayoutAlignment::normal:
    case LayoutAlignment::stretch:
    case LayoutAlignment::flex_start:
    case LayoutAlignment::baseline:
        return Packing::start;
    }
    return Packing::start;
}

Packing align_content_packing(LayoutAlignment alignment, bool wrap_reverse) noexcept {
    switch (alignment) {
    case LayoutAlignment::normal:
    case LayoutAlignment::stretch:
        return Packing::stretch;
    case LayoutAlignment::start:
    case LayoutAlignment::end:
        return writing_mode_packing(alignment, wrap_reverse);
    case LayoutAlignment::flex_end:
        return Packing::end;
    case LayoutAlignment::center:
        return Packing::center;
    case LayoutAlignment::space_between:
        return Packing::space_between;
    case LayoutAlignment::space_around:
        return Packing::space_around;
    case LayoutAlignment::space_evenly:
        return Packing::space_evenly;
    case LayoutAlignment::automatic:
    case LayoutAlignment::flex_start:
    case LayoutAlignment::baseline:
        return Packing::start;
    }
    return Packing::start;
}

Packing self_packing(LayoutAlignment self, LayoutAlignment items, bool wrap_reverse, bool horizontal) noexcept {
    const LayoutAlignment alignment = self == LayoutAlignment::automatic ? items : self;
    switch (alignment) {
    case LayoutAlignment::automatic:
    case LayoutAlignment::normal:
    case LayoutAlignment::stretch:
        return Packing::stretch;
    case LayoutAlignment::start:
    case LayoutAlignment::end:
        return writing_mode_packing(alignment, wrap_reverse);
    case LayoutAlignment::flex_end:
        return Packing::end;
    case LayoutAlignment::center:
        return Packing::center;
    case LayoutAlignment::baseline:
        // Baselines run along the inline axis: a column's items have none
        // in its cross axis and fall back to flex-start.
        return horizontal ? Packing::baseline : Packing::start;
    case LayoutAlignment::flex_start:
    case LayoutAlignment::space_between:
    case LayoutAlignment::space_around:
    case LayoutAlignment::space_evenly:
        return Packing::start;
    }
    return Packing::start;
}

// Where the first subject starts and the extra space between subjects, for
// `count` subjects and `free` space (CSS Box Alignment's distribution and
// its fallbacks): space-between falls back to safe flex-start, and
// space-around and space-evenly to safe center. Safe alignment never
// overflows the start, so with negative space all three start at the
// start; a single subject with positive space is centred by the latter two
// (Chromium 155 agrees: fixture flex/justify-overflow-fallback).
struct Distribution final {
    double offset{};
    double spacing{};
};

Distribution distribute(Packing packing, double free, std::size_t count) noexcept {
    Distribution distribution{};
    const bool spreads = free > 0.0 && count > 1U;
    switch (packing) {
    case Packing::start:
    case Packing::stretch:
    case Packing::baseline:
        return distribution;
    case Packing::end:
        distribution.offset = free;
        return distribution;
    case Packing::center:
        distribution.offset = free * 0.5;
        return distribution;
    case Packing::space_between:
        if (spreads) distribution.spacing = free / static_cast<double>(count - 1U);
        return distribution;
    case Packing::space_around:
        if (free > 0.0 && count > 0U) {
            distribution.spacing = free / static_cast<double>(count);
            distribution.offset = distribution.spacing * 0.5;
        }
        return distribution;
    case Packing::space_evenly:
        if (free > 0.0 && count > 0U) {
            distribution.spacing = free / static_cast<double>(count + 1U);
            distribution.offset = distribution.spacing;
        }
        return distribution;
    }
    return distribution;
}

// Orders input positions by (order, position): a strict total order, so
// an unstable sort gives the stable result without a temporary buffer.
struct FlexOrderBefore final {
    std::span<const FlexItemInput> items;

    bool operator()(std::uint32_t left, std::uint32_t right) const noexcept {
        const std::int32_t left_order = items[left].order;
        const std::int32_t right_order = items[right].order;
        if (left_order != right_order) return left_order < right_order;
        return left < right;
    }
};

bool has_nonzero_order(std::span<const FlexItemInput> items) noexcept {
    for (const FlexItemInput& item : items) {
        if (item.order != 0) return true;
    }
    return false;
}

void clear_results(std::vector<FlexItemResult>& items, std::vector<FlexLine>& lines,
                   std::vector<std::uint32_t>& order) noexcept {
    items.clear();
    lines.clear();
    order.clear();
}

} // namespace

void FlexLayoutWorkspace::reserve(std::size_t count) {
    items_.reserve(count);
    lines_.reserve(count);
    order_.reserve(count);
    frozen_.reserve(count);
    unclamped_.reserve(count);
}

FlexSolveOutcome resolve_flex_main_sizes(const FlexContainerInput& container, std::span<const FlexItemInput> items,
                                         FlexLayoutWorkspace& workspace) {
    const std::size_t count = items.size();
    workspace.content_main_ = 0.0;
    workspace.content_cross_ = 0.0;
    if (count > maximum_flex_items) {
        clear_results(workspace.items_, workspace.lines_, workspace.order_);
        return FlexSolveOutcome::refused_too_many_items;
    }
    workspace.reserve(count);
    workspace.items_.assign(count, FlexItemResult{});
    workspace.order_.resize(count);
    workspace.frozen_.assign(count, 0U);
    workspace.unclamped_.assign(count, 0.0);
    workspace.lines_.clear();
    for (std::size_t index = 0U; index < count; ++index) {
        workspace.order_[index] = static_cast<std::uint32_t>(index);
    }
    if (has_nonzero_order(items)) {
        std::sort(workspace.order_.begin(), workspace.order_.end(), FlexOrderBefore{items});
    }

    // Base and hypothetical main sizes (9.2.3).
    for (std::size_t index = 0U; index < count; ++index) {
        const FlexItemInput& item = items[index];
        FlexItemResult& result = workspace.items_[index];
        // A non-finite base (a child's own measure) counts as zero (audit
        // N41): the freeze loop below compares against it.
        result.flex_base_size = std::isfinite(item.flex_base_size) ? std::max(0.0, item.flex_base_size) : 0.0;
        result.hypothetical_main = clamp_size(result.flex_base_size, item.minimum_main, item.maximum_main);
        result.main_size = result.hypothetical_main;
    }

    // Lines (9.3): one when the container does not wrap or its main size is
    // indefinite; otherwise each line takes items while their outer
    // hypothetical sizes and gaps fit, and at least one item.
    const FlexAvailableExtent available = main_available(container);
    const double gap = main_gap(container);
    const bool single_line = container.wrap == FlexWrap::nowrap || !available.definite;
    FlexLine line{};
    for (std::size_t position = 0U; position < count; ++position) {
        const std::uint32_t index = workspace.order_[position];
        const FlowMargins margins = flow_margins(container, items[index].margin);
        const double outer = workspace.items_[index].hypothetical_main + main_margins(margins);
        const double extended = line.count == 0U ? outer : line.main_extent + gap + outer;
        if (!single_line && line.count != 0U && extended > available.extent + line_break_tolerance) {
            workspace.lines_.push_back(line);
            line = FlexLine{static_cast<std::uint32_t>(position), 0U, 0.0, 0.0, 0.0};
        }
        line.main_extent = line.count == 0U ? outer : line.main_extent + gap + outer;
        ++line.count;
    }
    if (line.count != 0U) workspace.lines_.push_back(line);

    // Flexible lengths per line (9.7).
    for (std::size_t line_index = 0U; line_index < workspace.lines_.size(); ++line_index) {
        FlexLine& current = workspace.lines_[line_index];
        const std::size_t first = current.first;
        const std::size_t last = first + current.count;
        double fixed_extent = gap * static_cast<double>(current.count - 1U);
        double hypothetical_extent = fixed_extent;
        for (std::size_t position = first; position < last; ++position) {
            const std::uint32_t index = workspace.order_[position];
            workspace.items_[index].line = static_cast<std::uint32_t>(line_index);
            const double margins = main_margins(flow_margins(container, items[index].margin));
            fixed_extent += margins;
            hypothetical_extent += workspace.items_[index].hypothetical_main + margins;
        }
        if (!available.definite) {
            current.main_extent = hypothetical_extent;
            continue;
        }
        const bool growing = hypothetical_extent < available.extent;
        // Size inflexible items.
        std::size_t unfrozen = 0U;
        for (std::size_t position = first; position < last; ++position) {
            const std::uint32_t index = workspace.order_[position];
            const FlexItemInput& item = items[index];
            FlexItemResult& result = workspace.items_[index];
            const double factor = growing ? item.grow.value() : item.shrink.value();
            const bool inflexible = factor <= 0.0 ||
                                    (growing && result.flex_base_size > result.hypothetical_main) ||
                                    (!growing && result.flex_base_size < result.hypothetical_main);
            workspace.frozen_[index] = inflexible ? 1U : 0U;
            result.main_size = result.hypothetical_main;
            result.freeze_reason = inflexible ? FlexFreezeReason::inflexible : FlexFreezeReason::flexed;
            if (!inflexible) ++unfrozen;
        }
        // The space left with frozen items at their sizes and the others at
        // their base sizes.
        double initial_free = available.extent - fixed_extent;
        for (std::size_t position = first; position < last; ++position) {
            const std::uint32_t index = workspace.order_[position];
            const FlexItemResult& result = workspace.items_[index];
            initial_free -= workspace.frozen_[index] != 0U ? result.main_size : result.flex_base_size;
        }
        // The freeze loop: each round freezes at least one item.
        while (unfrozen != 0U) {
            double remaining = available.extent - fixed_extent;
            double factors = 0.0;
            double scaled_shrink = 0.0;
            for (std::size_t position = first; position < last; ++position) {
                const std::uint32_t index = workspace.order_[position];
                const FlexItemResult& result = workspace.items_[index];
                if (workspace.frozen_[index] != 0U) {
                    remaining -= result.main_size;
                    continue;
                }
                remaining -= result.flex_base_size;
                factors += growing ? items[index].grow.value() : items[index].shrink.value();
                scaled_shrink += items[index].shrink.value() * result.flex_base_size;
            }
            if (factors < 1.0) {
                const double limited = initial_free * factors;
                if (std::abs(limited) < std::abs(remaining)) remaining = limited;
            }
            double violation = 0.0;
            for (std::size_t position = first; position < last; ++position) {
                const std::uint32_t index = workspace.order_[position];
                if (workspace.frozen_[index] != 0U) continue;
                const FlexItemInput& item = items[index];
                const FlexItemResult& result = workspace.items_[index];
                double target = result.flex_base_size;
                if (remaining != 0.0 && growing) {
                    target = result.flex_base_size + remaining * (item.grow.value() / factors);
                } else if (remaining != 0.0 && scaled_shrink > 0.0) {
                    const double ratio = item.shrink.value() * result.flex_base_size / scaled_shrink;
                    target = result.flex_base_size - std::abs(remaining) * ratio;
                }
                workspace.unclamped_[index] = target;
                const double clamped = std::max(0.0, clamp_size(target, item.minimum_main, item.maximum_main));
                violation += clamped - target;
                workspace.items_[index].main_size = clamped;
            }
            // Freeze all when there is no violation, else the items that
            // violated in the total's direction.
            const std::size_t unfrozen_before = unfrozen;
            for (std::size_t position = first; position < last; ++position) {
                const std::uint32_t index = workspace.order_[position];
                if (workspace.frozen_[index] != 0U) continue;
                FlexItemResult& result = workspace.items_[index];
                const double unclamped = workspace.unclamped_[index];
                const bool minimum = result.main_size > unclamped;
                const bool maximum = result.main_size < unclamped;
                bool freeze = violation == 0.0;
                if (violation > 0.0 && minimum) freeze = true;
                if (violation < 0.0 && maximum) freeze = true;
                if (!freeze) continue;
                workspace.frozen_[index] = 1U;
                --unfrozen;
                if (minimum) result.freeze_reason = FlexFreezeReason::minimum_violation;
                if (maximum) result.freeze_reason = FlexFreezeReason::maximum_violation;
            }
            // Audit N41: a round that froze nothing (a NaN violation, from a
            // non-finite extent, gap, margin or factor) freezes the rest at
            // their clamped sizes, so the loop always ends.
            if (unfrozen == unfrozen_before) {
                for (std::size_t position = first; position < last; ++position) {
                    const std::uint32_t index = workspace.order_[position];
                    if (workspace.frozen_[index] != 0U) continue;
                    workspace.frozen_[index] = 1U;
                    --unfrozen;
                }
            }
        }
        double extent = fixed_extent;
        for (std::size_t position = first; position < last; ++position) {
            extent += workspace.items_[workspace.order_[position]].main_size;
        }
        current.main_extent = extent;
    }
    for (const FlexLine& current : workspace.lines_) {
        workspace.content_main_ = std::max(workspace.content_main_, current.main_extent);
    }
    return FlexSolveOutcome::solved;
}

FlexSolveOutcome place_flex_items(const FlexContainerInput& container, std::span<const FlexItemInput> items,
                                  std::span<const FlexItemCross> crosses, FlexLayoutWorkspace& workspace) {
    const std::size_t count = items.size();
    if (count > maximum_flex_items) return FlexSolveOutcome::refused_too_many_items;
    if (crosses.size() != count || workspace.items_.size() != count || workspace.order_.size() != count) {
        return FlexSolveOutcome::refused_mismatched_input;
    }
    const bool horizontal = horizontal_main(container.direction);
    const bool wrap_reverse = container.wrap == FlexWrap::wrap_reverse;
    const FlexAvailableExtent main_space = main_available(container);
    const FlexAvailableExtent cross_space = cross_available(container);
    const double gap = main_gap(container);
    const double line_gap = cross_gap(container);
    const bool single_line = container.wrap == FlexWrap::nowrap;
    const std::size_t line_count = workspace.lines_.size();

    // Each item's hypothetical cross size, and each line's cross size
    // (9.4.7, 9.4.8): its largest outer cross size, or for baseline-aligned
    // items the largest ascent plus the largest descent.
    for (std::size_t index = 0U; index < count; ++index) {
        const FlexItemInput& item = items[index];
        workspace.items_[index].cross_size =
            std::max(0.0, clamp_size(crosses[index].hypothetical_cross, item.minimum_cross, item.maximum_cross));
    }
    for (std::size_t line_index = 0U; line_index < line_count; ++line_index) {
        FlexLine& line = workspace.lines_[line_index];
        double largest = 0.0;
        double ascent = 0.0;
        double descent = 0.0;
        for (std::size_t position = line.first; position < line.first + line.count; ++position) {
            const std::uint32_t index = workspace.order_[position];
            const FlexItemInput& item = items[index];
            const FlowMargins margins = flow_margins(container, item.margin);
            const double cross = workspace.items_[index].cross_size;
            const double outer = cross + cross_margins(margins);
            const Packing packing = self_packing(item.align_self, container.align_items, wrap_reverse, horizontal);
            const bool auto_margin = margins.cross_start.automatic || margins.cross_end.automatic;
            if (packing == Packing::baseline && !auto_margin) {
                const double baseline = std::isnan(crosses[index].baseline) ? cross : crosses[index].baseline;
                const double item_ascent = baseline + fixed_length(margins.cross_start);
                ascent = std::max(ascent, item_ascent);
                descent = std::max(descent, outer - item_ascent);
            } else {
                largest = std::max(largest, outer);
            }
        }
        line.cross_size = std::max(largest, ascent + descent);
    }
    if (single_line && cross_space.definite && line_count == 1U) {
        workspace.lines_[0].cross_size = cross_space.extent;
    }

    // align-content (9.4.15, 9.6.17): a multi-line container stretches its
    // lines into positive free space, then places them.
    double lines_extent = line_count == 0U ? 0.0 : line_gap * static_cast<double>(line_count - 1U);
    for (const FlexLine& line : workspace.lines_) lines_extent += line.cross_size;
    const double cross_extent = cross_space.definite ? cross_space.extent : lines_extent;
    double free_cross = cross_extent - lines_extent;
    const Packing content_packing = single_line ? Packing::start
                                                : align_content_packing(container.align_content, wrap_reverse);
    if (content_packing == Packing::stretch && free_cross > 0.0 && line_count != 0U) {
        const double share = free_cross / static_cast<double>(line_count);
        for (FlexLine& line : workspace.lines_) line.cross_size += share;
        free_cross = 0.0;
    }
    const Distribution line_distribution = distribute(content_packing, free_cross, line_count);
    double line_cursor = line_distribution.offset;
    for (FlexLine& line : workspace.lines_) {
        line.cross_position = line_cursor;
        line_cursor += line.cross_size + line_gap + line_distribution.spacing;
    }
    workspace.content_cross_ = lines_extent;

    // Main-axis alignment (9.5) and cross-axis alignment (9.6) per item,
    // then the physical rectangle.
    const double main_extent = main_space.definite ? main_space.extent : workspace.content_main_;
    const bool main_reversed = main_physically_reversed(container);
    const bool cross_reversed = cross_physically_reversed(container);
    const Packing main_packing = justify_packing(container.justify_content, reversed_direction(container.direction));
    for (const FlexLine& line : workspace.lines_) {
        const std::size_t first = line.first;
        const std::size_t last = first + line.count;
        double free_main = main_space.definite ? main_space.extent - line.main_extent : 0.0;
        std::size_t auto_margins = 0U;
        for (std::size_t position = first; position < last; ++position) {
            const FlowMargins margins = flow_margins(container, items[workspace.order_[position]].margin);
            if (margins.main_start.automatic) ++auto_margins;
            if (margins.main_end.automatic) ++auto_margins;
        }
        double auto_share = 0.0;
        if (free_main > 0.0 && auto_margins != 0U) {
            auto_share = free_main / static_cast<double>(auto_margins);
            free_main = 0.0;
        }
        const Distribution distribution =
            auto_margins != 0U ? Distribution{} : distribute(main_packing, free_main, line.count);
        // Baseline-aligned items' shared ascent on this line.
        double line_ascent = 0.0;
        for (std::size_t position = first; position < last; ++position) {
            const std::uint32_t index = workspace.order_[position];
            const FlexItemInput& item = items[index];
            const FlowMargins margins = flow_margins(container, item.margin);
            const Packing packing = self_packing(item.align_self, container.align_items, wrap_reverse, horizontal);
            if (packing != Packing::baseline || margins.cross_start.automatic || margins.cross_end.automatic) continue;
            const double cross = workspace.items_[index].cross_size;
            const double baseline = std::isnan(crosses[index].baseline) ? cross : crosses[index].baseline;
            line_ascent = std::max(line_ascent, baseline + fixed_length(margins.cross_start));
        }
        double main_cursor = distribution.offset;
        for (std::size_t position = first; position < last; ++position) {
            const std::uint32_t index = workspace.order_[position];
            const FlexItemInput& item = items[index];
            FlexItemResult& result = workspace.items_[index];
            const FlowMargins margins = flow_margins(container, item.margin);
            const double start_margin = margins.main_start.automatic ? auto_share : margins.main_start.length;
            const double end_margin = margins.main_end.automatic ? auto_share : margins.main_end.length;
            const double main_position = main_cursor + start_margin;
            main_cursor = main_position + result.main_size + end_margin + gap + distribution.spacing;

            // Cross size: stretched to the line (9.4.11) when the item's
            // alignment is stretch, its cross size is not definite and
            // neither cross margin is auto.
            const Packing packing = self_packing(item.align_self, container.align_items, wrap_reverse, horizontal);
            const bool auto_cross = margins.cross_start.automatic || margins.cross_end.automatic;
            const double fixed_cross_margins = cross_margins(margins);
            if (packing == Packing::stretch && !item.definite_cross && !auto_cross) {
                const double stretched = line.cross_size - fixed_cross_margins;
                result.cross_size = std::max(0.0, clamp_size(stretched, item.minimum_cross, item.maximum_cross));
            }
            const double free_item = line.cross_size - result.cross_size - fixed_cross_margins;
            double cross_offset = 0.0;
            if (auto_cross) {
                // Auto cross margins absorb positive free space (9.6.13);
                // otherwise the item sits at cross-start.
                if (free_item > 0.0 && margins.cross_start.automatic && margins.cross_end.automatic) {
                    cross_offset = free_item * 0.5;
                } else if (free_item > 0.0 && margins.cross_start.automatic) {
                    cross_offset = free_item;
                }
            } else if (packing == Packing::end) {
                cross_offset = free_item;
            } else if (packing == Packing::center) {
                cross_offset = free_item * 0.5;
            } else if (packing == Packing::baseline) {
                const double baseline = std::isnan(crosses[index].baseline) ? result.cross_size : crosses[index].baseline;
                cross_offset = line_ascent - (baseline + fixed_length(margins.cross_start));
            }
            const double cross_position = line.cross_position + fixed_length(margins.cross_start) + cross_offset;

            const double physical_main =
                main_reversed ? main_extent - main_position - result.main_size : main_position;
            const double physical_cross =
                cross_reversed ? cross_extent - cross_position - result.cross_size : cross_position;
            if (horizontal) {
                result.bounds = Rect{physical_main, physical_cross, result.main_size, result.cross_size};
            } else {
                result.bounds = Rect{physical_cross, physical_main, result.cross_size, result.main_size};
            }
        }
    }
    return FlexSolveOutcome::solved;
}

} // namespace gui_forms
