#include "gui_forms/controls/scrollable_control/container_control/split_container/splitter_panel/splitter_panel.hpp"
#include "../../../scrollable_anatomy.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include <array>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>

namespace gui_forms {

SplitterPanel::SplitterPanel(StableId stable_id)
    : ContainerControl(std::move(stable_id)) {}

void SplitterPanel::on_paint(Painter& painter, Rect) {
    if (background.read().alpha == 0U) return;
    const Rect bounds = committed_arranged_bounds();
    // The authored background is a flat ground on a fill (T4-1).
    PartPaint ground;
    ground.family = ControlVisualRole::panel;
    ground.role = PartRole::fill;
    ground.bounds = {0.0, 0.0, bounds.width, bounds.height};
    ground.corner_radius = 0.0;
    ground.authored_background = background.read();
    paint_part(painter, *this, ground);
}

namespace {

constexpr std::array<PartField<SplitterPanel::Parts>, 1> splitter_panel_extra_fields{{
    {.declaration = {.name = "face", .role = PartRole::face, .composed_from = {}},
     .member = &SplitterPanel::Parts::face},
}};

constexpr std::array<PartDeclaration, scrollable_detail::part_count + 1>
    splitter_panel_parts = joined_declarations(
        scrollable_detail::parts, part_declarations(splitter_panel_extra_fields));

constexpr ControlAnatomy splitter_panel_anatomy{
    .control_id = "splitter_panel",
    .parts = splitter_panel_parts,
    .states = scrollable_detail::states,
    .custom_states = {},
};

} // namespace

const ControlAnatomy& SplitterPanel::anatomy() const noexcept {
    return splitter_panel_anatomy;
}

void SplitterPanel::publish_part_geometry(std::span<PartGeometry> geometry) const {
    Parts record;
    record.scrollable = scrollable_parts();
    // The face exists only while the panel has an opaque background.
    if (background.read().alpha != 0U) {
        record.face = PartGeometry::from_bounds(client_rectangle());
    }
    write_parts(anatomy(), scrollable_detail::part_fields, record.scrollable, geometry);
    write_parts(anatomy(), splitter_panel_extra_fields, record, geometry);
}

SplitterPanel::Parts SplitterPanel::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), scrollable_detail::part_fields, published.parts(),
               record.scrollable);
    read_parts(anatomy(), splitter_panel_extra_fields, published.parts(), record);
    return record;
}

// property_codemod begin SplitterPanel (tools/property_families/splitter_panel.json; generated, do not edit)

void SplitterPanel::prepare_write(const decltype(background)&, Color&) {
    require_mutable();
}

void SplitterPanel::written(const decltype(background)&) {
    invalidate(invalidation::style_only);
}

// property_codemod end SplitterPanel

} // namespace gui_forms
