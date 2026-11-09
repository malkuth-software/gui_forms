#include "gui_forms/plots/plot_legend/plot_legend.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace gui_forms::plots {

namespace {

// The column (0 left, 1 centre, 2 right) and row of a place.
std::size_t place_column(LegendPlace place) noexcept {
    const std::size_t result = static_cast<std::size_t>(place) % 3U;
    return result;
}

std::size_t place_row(LegendPlace place) noexcept {
    const std::size_t result = static_cast<std::size_t>(place) / 3U;
    return result;
}

bool overlaps(Rect left, Rect right) noexcept {
    const bool result = left.x < right.right() && right.x < left.right() && left.y < right.bottom() &&
                        right.y < left.bottom();
    return result;
}

Rect clamp_inside(Rect rect, Rect area) noexcept {
    Rect result = rect;
    result.x = std::clamp(rect.x, area.x, std::max(area.x, area.right() - rect.width));
    result.y = std::clamp(rect.y, area.y, std::max(area.y, area.bottom() - rect.height));
    return result;
}

} // namespace

LegendLayout layout_legend(const LegendStyle& style, Rect plot, std::span<const double> label_widths,
                           std::span<LegendEntryLayout> entries) noexcept {
    LegendLayout layout{};
    const std::size_t count = std::min(label_widths.size(), entries.size());
    if (count == 0U) return layout;
    // The frame's size from the entries.
    double width = 0.0;
    double height = 0.0;
    for (std::size_t index = 0U; index < count; ++index) {
        const double entry_width = style.swatch + style.swatch_gap + std::max(0.0, label_widths[index]);
        if (style.flow == LegendFlow::vertical) {
            width = std::max(width, entry_width);
            height = height + style.line_height + (index == 0U ? 0.0 : style.entry_gap);
        } else {
            width = width + entry_width + (index == 0U ? 0.0 : style.entry_gap * 2.0);
            height = style.line_height;
        }
    }
    width = width + style.padding * 2.0;
    height = height + style.padding * 2.0;
    const std::size_t column = place_column(style.place);
    const std::size_t row = place_row(style.place);
    double x = 0.0;
    double y = 0.0;
    if (style.inside || style.place == LegendPlace::center) {
        x = column == 0U ? plot.x + style.margin
                         : (column == 1U ? plot.x + (plot.width - width) * 0.5 : plot.right() - style.margin - width);
        y = row == 0U ? plot.y + style.margin
                      : (row == 1U ? plot.y + (plot.height - height) * 0.5 : plot.bottom() - style.margin - height);
    } else {
        // Beside the plot: a corner or an edge place puts the legend
        // outside on the left or right when its column is an edge, else
        // above or below.
        if (column == 0U) {
            x = plot.x - style.margin - width;
        } else if (column == 2U) {
            x = plot.right() + style.margin;
        } else {
            x = plot.x + (plot.width - width) * 0.5;
        }
        if (column != 1U) {
            y = row == 0U ? plot.y : (row == 1U ? plot.y + (plot.height - height) * 0.5 : plot.bottom() - height);
        } else {
            y = row == 0U ? plot.y - style.margin - height : plot.bottom() + style.margin;
        }
    }
    layout.frame = Rect{x, y, width, height};
    double cursor_x = x + style.padding;
    double cursor_y = y + style.padding;
    for (std::size_t index = 0U; index < count; ++index) {
        const double label_width = std::max(0.0, label_widths[index]);
        const double entry_width = style.swatch + style.swatch_gap + label_width;
        LegendEntryLayout& entry = entries[index];
        entry.bounds = Rect{cursor_x, cursor_y, entry_width, style.line_height};
        const double swatch_top = cursor_y + (style.line_height - style.swatch) * 0.5;
        entry.swatch = Rect{cursor_x, swatch_top, style.swatch, style.swatch};
        entry.label = Rect{cursor_x + style.swatch + style.swatch_gap, cursor_y, label_width, style.line_height};
        if (style.flow == LegendFlow::vertical) {
            cursor_y = cursor_y + style.line_height + style.entry_gap;
        } else {
            cursor_x = cursor_x + entry_width + style.entry_gap * 2.0;
        }
    }
    layout.entry_count = count;
    return layout;
}

std::size_t legend_entry_at(std::span<const LegendEntryLayout> entries, Point point) noexcept {
    for (std::size_t index = 0U; index < entries.size(); ++index) {
        if (entries[index].bounds.contains(point)) return index;
    }
    return entries.size();
}

std::size_t layout_annotations(Rect plot, std::span<const AnnotationRequest> requests,
                               std::span<Rect> placed) noexcept {
    const std::size_t count = std::min({requests.size(), placed.size(), maximum_annotations});
    // Offsets from the anchor: above right, above left, below right, below
    // left, each clear of the anchor by a small gap.
    constexpr double gap = 4.0;
    for (std::size_t index = 0U; index < count; ++index) {
        const AnnotationRequest& request = requests[index];
        const Size size = request.size;
        const std::array<Rect, 4> candidates{{
            {request.anchor.x + gap, request.anchor.y - gap - size.height, size.width, size.height},
            {request.anchor.x - gap - size.width, request.anchor.y - gap - size.height, size.width, size.height},
            {request.anchor.x + gap, request.anchor.y + gap, size.width, size.height},
            {request.anchor.x - gap - size.width, request.anchor.y + gap, size.width, size.height},
        }};
        Rect chosen = clamp_inside(candidates[0], plot);
        for (const Rect candidate : candidates) {
            const Rect clamped = clamp_inside(candidate, plot);
            bool clear = true;
            for (std::size_t earlier = 0U; earlier < index; ++earlier) {
                if (overlaps(clamped, placed[earlier])) {
                    clear = false;
                    break;
                }
            }
            if (!clear) continue;
            chosen = clamped;
            break;
        }
        placed[index] = chosen;
    }
    return count;
}

} // namespace gui_forms::plots
