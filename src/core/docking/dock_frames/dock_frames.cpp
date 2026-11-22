#include "gui_forms/docking/dock_frames/dock_frames.hpp"

#include "../dock_layout/dock_layout_internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace gui_forms {

namespace {

// One frame of the area as the planners see it: its document, its memory
// (a normal frame at the back when the document has none) and its tree
// position.
struct PlannedFrame final {
    DockPanelId panel{};
    DockFrameMemory frame{};
    std::size_t position{};
};

// Front first; equal orders keep tree order.
struct FrontFirst final {
    [[nodiscard]] bool operator()(const PlannedFrame& left, const PlannedFrame& right) const noexcept {
        if (left.frame.order != right.frame.order) return left.frame.order > right.frame.order;
        const bool earlier = left.position < right.position;
        return earlier;
    }
};

// By icon slot; equal slots keep tree order.
struct SlotFirst final {
    [[nodiscard]] bool operator()(const PlannedFrame& left, const PlannedFrame& right) const noexcept {
        if (left.frame.icon_slot != right.frame.icon_slot) return left.frame.icon_slot < right.frame.icon_slot;
        const bool earlier = left.position < right.position;
        return earlier;
    }
};

void collect_frames(const DockLayout& layout, std::vector<PlannedFrame>& out) {
    std::vector<DockPanelId> documents{};
    detail::collect_area_documents(layout.state(), documents);
    out.reserve(documents.size());
    for (std::size_t position = 0; position < documents.size(); ++position) {
        PlannedFrame planned{};
        planned.panel = documents[position];
        planned.position = position;
        const DockPanelRecord* const record = detail::find_dock_record(layout.state(), documents[position]);
        const DockFrameMemory* const frame = record == nullptr ? nullptr : (*record).frame.value();
        if (frame != nullptr) {
            planned.frame = *frame;
        } else {
            planned.frame.bounds = Rect{0.0, 0.0, detail::dock_frame_default_width, detail::dock_frame_default_height};
        }
        out.push_back(std::move(planned));
    }
}

[[nodiscard]] std::size_t icons_per_row(Size area, const DockFrameMetrics& metrics) noexcept {
    if (!(metrics.icon_width > 0.0) || !(area.width > 0.0)) return 1U;
    const double fitting = std::floor(area.width / metrics.icon_width);
    const std::size_t count = fitting < 1.0 ? 1U : static_cast<std::size_t>(fitting);
    return count;
}

// The part of the area tiles and cascades use: the area less the rows of
// icons the minimized frames take along the anchored edge (never less than
// one caption tall).
[[nodiscard]] Rect working_region(Size area, const DockFrameMetrics& metrics, std::size_t minimized) noexcept {
    const std::size_t per_row = icons_per_row(area, metrics);
    const std::size_t rows = (minimized + per_row - 1U) / per_row;
    const double caption = std::max(metrics.caption_height, 1.0);
    const double reserved = std::min(static_cast<double>(rows) * caption, std::max(0.0, area.height - caption));
    Rect region{0.0, 0.0, area.width, area.height - reserved};
    if (!metrics.icons_at_bottom) region.y = reserved;
    return region;
}

void restore_and_place(DockTransaction& transaction, const PlannedFrame& planned, Rect bounds) {
    if (planned.frame.state == DockFrameState::maximized) {
        transaction.edits.push_back(DockEdit::set_frame_state(planned.panel, DockFrameState::normal));
    }
    if (!(planned.frame.bounds == bounds)) transaction.edits.push_back(DockEdit::set_frame_bounds(planned.panel, bounds));
}

void plan_cascade(DockTransaction& transaction, const std::vector<PlannedFrame>& front_first, Rect region,
                  const DockFrameMetrics& metrics) {
    const double step = std::max(metrics.caption_height, 1.0);
    const double width = std::max(region.width * 2.0 / 3.0, std::min(region.width, 160.0));
    const double height = std::max(region.height * 2.0 / 3.0, std::min(region.height, step * 3.0));
    const double room = std::min(region.width - width, region.height - height);
    const double fitting = room > 0.0 ? std::floor(room / step) + 1.0 : 1.0;
    const std::size_t wrap = static_cast<std::size_t>(std::max(fitting, 1.0));
    // Back to front: the frame at the back takes the corner, the front one
    // the farthest step, so every caption shows.
    const std::size_t count = front_first.size();
    for (std::size_t back = 0; back < count; ++back) {
        const PlannedFrame& planned = front_first[count - 1U - back];
        const double offset = step * static_cast<double>(back % wrap);
        const Rect bounds{region.x + offset, region.y + offset, width, height};
        restore_and_place(transaction, planned, bounds);
    }
}

// Lays the frames out, front first, in lines: rows one above another when
// `stacked`, else columns side by side. Up to three frames take a line
// each; past three the lines make a grid, the last line holding what is
// left in longer cells.
void plan_tiles(DockTransaction& transaction, const std::vector<PlannedFrame>& front_first, Rect region,
                bool stacked) {
    const std::size_t count = front_first.size();
    std::size_t lines = count <= 3U ? count : static_cast<std::size_t>(std::ceil(std::sqrt(static_cast<double>(count))));
    std::size_t per_line = (count + lines - 1U) / lines;
    while (lines > 1U && per_line * (lines - 1U) >= count) --lines;
    per_line = (count + lines - 1U) / lines;
    for (std::size_t index = 0; index < count; ++index) {
        const std::size_t line = index / per_line;
        const std::size_t cell = index % per_line;
        const std::size_t in_line = line + 1U == lines ? count - per_line * (lines - 1U) : per_line;
        Rect bounds{};
        if (stacked) {
            // Lines are rows, one above another.
            const double row_height = region.height / static_cast<double>(lines);
            const double cell_width = region.width / static_cast<double>(in_line);
            bounds = Rect{region.x + cell_width * static_cast<double>(cell),
                          region.y + row_height * static_cast<double>(line), cell_width, row_height};
        } else {
            // Lines are columns, side by side.
            const double column_width = region.width / static_cast<double>(lines);
            const double cell_height = region.height / static_cast<double>(in_line);
            bounds = Rect{region.x + column_width * static_cast<double>(line),
                          region.y + cell_height * static_cast<double>(cell), column_width, cell_height};
        }
        restore_and_place(transaction, front_first[index], bounds);
    }
}

} // namespace

Rect dock_frame_icon_bounds(std::uint32_t slot, Size area, const DockFrameMetrics& metrics) noexcept {
    const std::size_t per_row = icons_per_row(area, metrics);
    const std::size_t row = static_cast<std::size_t>(slot) / per_row;
    const std::size_t column = static_cast<std::size_t>(slot) % per_row;
    const double height = std::max(metrics.caption_height, 1.0);
    const double width = metrics.icon_width > 0.0 ? metrics.icon_width : area.width;
    const double y = metrics.icons_at_bottom ? area.height - height * static_cast<double>(row + 1U)
                                             : height * static_cast<double>(row);
    const Rect bounds{width * static_cast<double>(column), y, width, height};
    return bounds;
}

Rect dock_frame_shown_bounds(const DockFrameMemory& frame, Size area, const DockFrameMetrics& metrics) noexcept {
    if (frame.state == DockFrameState::maximized) {
        const Rect whole{0.0, 0.0, area.width, area.height};
        return whole;
    }
    if (frame.state == DockFrameState::minimized) {
        const Rect icon = dock_frame_icon_bounds(frame.icon_slot, area, metrics);
        return icon;
    }
    return frame.bounds;
}

DockTransaction plan_frame_arrangement(const DockLayout& layout, DockFrameArrangement arrangement, Size area,
                                       const DockFrameMetrics& metrics) {
    DockTransaction transaction{};
    transaction.origin = DockEditOrigin::program;
    const bool usable = std::isfinite(area.width) && std::isfinite(area.height) && area.width > 0.0 &&
                        area.height > 0.0;
    if (!usable) return transaction;
    std::vector<PlannedFrame> frames{};
    collect_frames(layout, frames);
    std::vector<PlannedFrame> minimized{};
    std::vector<PlannedFrame> shown{};
    for (PlannedFrame& planned : frames) {
        if (planned.frame.state == DockFrameState::minimized) {
            minimized.push_back(std::move(planned));
        } else {
            shown.push_back(std::move(planned));
        }
    }
    if (arrangement == DockFrameArrangement::arrange_icons) {
        std::stable_sort(minimized.begin(), minimized.end(), SlotFirst{});
        for (std::size_t slot = 0; slot < minimized.size(); ++slot) {
            const std::uint32_t wanted = static_cast<std::uint32_t>(slot);
            if (minimized[slot].frame.icon_slot == wanted) continue;
            transaction.edits.push_back(DockEdit::set_frame_icon_slot(minimized[slot].panel, wanted));
        }
        return transaction;
    }
    if (shown.empty()) return transaction;
    std::stable_sort(shown.begin(), shown.end(), FrontFirst{});
    const Rect region = working_region(area, metrics, minimized.size());
    if (arrangement == DockFrameArrangement::cascade) {
        plan_cascade(transaction, shown, region, metrics);
    } else {
        plan_tiles(transaction, shown, region, arrangement == DockFrameArrangement::tile_horizontal);
    }
    return transaction;
}

} // namespace gui_forms
