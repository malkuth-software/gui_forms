#pragma once

// The DataGridView cell region's root control on its sub-surface (stage S7).
// Private: applications reach it only through DataGridViewCellsContent, so it
// is not a registry type (it is not made by id, has no schema entry and
// borrows the grid's anatomy ID for its theme rules).

#include "gui_forms/controls/data_grid_view_cells.hpp"

#include <cstdint>

namespace gui_forms {

// The sub-surface's root: paints the newest snapshot it was given, nothing
// else. It takes no input and is not exposed to accessibility: the grid on
// the UI thread keeps the cells' events, focus and semantic nodes. It
// reports the grid's anatomy ID ("data_grid_view"), so the theme's rules
// for the grid's parts select its parts too. Sub-surface thread.
class DataGridViewCells final : public Control {
public:
    explicit DataGridViewCells(StableId stable_id);

    // Shows `snapshot` from the next frame: repaints the region whole and
    // takes over its look. An invalid snapshot is refused, false, and the
    // previous one stays.
    bool set_snapshot(DataGridViewCellsSnapshot snapshot);
    [[nodiscard]] const DataGridViewCellsSnapshot& snapshot() const noexcept { return snapshot_; }
    // Snapshots shown and refused.
    [[nodiscard]] std::uint64_t snapshots_taken() const noexcept { return snapshots_taken_; }
    [[nodiscard]] std::uint64_t snapshots_refused() const noexcept { return snapshots_refused_; }

    void on_paint(Painter& painter, Rect local_damage) override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;

private:
    void take_look(const DataGridViewCellsLook& look);

    DataGridViewCellsSnapshot snapshot_{};
    std::uint64_t snapshots_taken_{};
    std::uint64_t snapshots_refused_{};
};

} // namespace gui_forms
