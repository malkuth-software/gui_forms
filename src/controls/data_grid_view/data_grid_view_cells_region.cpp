// DataGridView's cell region on a threaded sub-surface (stage S7, threaded
// sub-surfaces design note section 2; coordinator ruling 2026-11-05,
// option 1). With cells_on_sub_surface the grid keeps its data, events,
// editor, headers and scroll bars on the UI thread; each paint resolves the
// visible cells into a DataGridViewCellsSnapshot and sends it with a new
// content epoch (SubSurfaceHost::redraw_all), so the cells shown at that
// epoch agree with the headers painted with them. Owner drawing keeps the
// cells on the UI thread: its handlers run there.

#include "gui_forms/controls/data_grid_view/data_grid_view.hpp"
#include "gui_forms/surface_root/sub_surface/sub_surface_host.hpp"

#include <algorithm>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace gui_forms {

namespace {

[[nodiscard]] PartGeometry region_part(PartGeometry part, Point offset) noexcept {
    part.bounds.x += offset.x;
    part.bounds.y += offset.y;
    return part;
}

} // namespace

void DataGridView::prepare_write(const decltype(cells_on_sub_surface)&, bool&) {
    require_mutable();
}

// On: the region is made under the editor and the bars, transparent to hit
// testing, no tab stop and absent from the semantic tree, and its content starts once it is arranged in a
// window. Off: the region leaves the grid, which stops and joins its thread.
void DataGridView::written(const decltype(cells_on_sub_surface)&) {
    const bool wanted = cells_on_sub_surface.read();
    if (wanted && !cells_host_) {
        const std::string prefix(stable_id().value());
        cells_host_ = make_control<SubSurfaceHost>(StableId(prefix + ".cells-region"));
        (*cells_host_).set_focusable(false);
        (*cells_host_).hit_test_transparent = true;
        // The grid publishes its cells as its own virtual children; the
        // region publishes no "Sub-surface" pane and no nodes of its own.
        (*cells_host_).set_publishes_semantics(false);
        add_child(cells_host_);
        // The bottom of the paint order (set_child_index counts from the top).
        static_cast<void>(set_child_index((*cells_host_).runtime_id(), children().size()));
        set_child_layout_collapsed(cells_host_, true);
        (*cells_host_).set_content(std::make_unique<DataGridViewCellsContent>(stable_id()), true);
    } else if (!wanted && cells_host_) {
        const Control::Ptr removed = remove_child((*cells_host_).runtime_id());
        static_cast<void>(removed);
        cells_host_.reset();
    }
    invalidate(Dirty::arrange | Dirty::paint);
}

bool DataGridView::cells_on_region() const noexcept {
    const bool on_region = cells_host_ && draw_mode.read() != ItemDrawMode::owner_draw;
    return on_region;
}

bool DataGridView::send_cells_snapshot(const GridLayout& layout, const Parts& grid) {
    const Point offset{-layout.data.x, -layout.data.y};
    DataGridViewCellsSnapshot snapshot{};
    const std::span<const std::string> own_classes = classes();
    snapshot.look.classes.assign(own_classes.begin(), own_classes.end());
    snapshot.look.inline_style = std::string(inline_style());
    snapshot.look.back_color = ambient_back_color();
    snapshot.look.fore_color = ambient_fore_color();
    snapshot.well = region_part(grid.well, offset);
    const std::size_t row_end = visible_row_end(layout);
    const std::size_t first_row = std::min(first_row_, row_end);
    const std::size_t first_display = first_visible_display(layout);
    const std::size_t display_end = visible_display_end(layout);
    if (first_row < row_end && first_display < display_end) {
        // The cells' own extent, where an authored background leaves the
        // theme's ground (as on_paint paints it on the UI thread).
        const Rect last =
            cell_bounds(DataGridViewRowPosition{row_end - 1U}, display_order_[display_end - 1U], layout);
        const Rect extent{layout.data.x, layout.data.y, last.x + last.width - layout.data.x,
                          last.y + last.height - layout.data.y};
        const Rect inside = Rect::intersection(extent, layout.data);
        snapshot.cells_extent = Rect{inside.x + offset.x, inside.y + offset.y, inside.width, inside.height};
        snapshot.cells.reserve((row_end - first_row) * (display_end - first_display));
    }
    for (std::size_t position = first_row; position < row_end; ++position) {
        const DataGridViewRowPosition row{position};
        for (std::size_t display = first_display; display < display_end; ++display) {
            DataGridViewCellsCell cell{};
            if (!resolve_cell(row, display_order_[display], layout, offset, snapshot, cell)) return false;
            snapshot.cells.push_back(cell);
        }
    }
    static_cast<void>((*cells_host_).redraw_all(std::make_unique<DataGridViewCellsUpdate>(std::move(snapshot))));
    return true;
}

} // namespace gui_forms
