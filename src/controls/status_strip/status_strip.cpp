#include "gui_forms/controls/status_strip/status_strip.hpp"

#include <array>
#include <utility>

namespace gui_forms {

StatusStrip::StatusStrip(StableId stable_id) : ToolStrip(std::move(stable_id), StatusLayout{}) {}

SemanticDescriptor StatusStrip::semantic_descriptor() const {
    // A status bar (12.13).
    SemanticDescriptor descriptor = ToolStrip::semantic_descriptor();
    descriptor.role = SemanticRole::status_bar;
    descriptor.name = accessible_name.type().empty() ? "Status" : accessible_name.type();
    return descriptor;
}

namespace {

constexpr std::array<PartField<StatusStrip::Parts>, 4> status_strip_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &StatusStrip::Parts::region},
    {.declaration = {.name = "face", .role = PartRole::face, .composed_from = {}},
     .member = &StatusStrip::Parts::face},
    {.declaration = {.name = "sizing_grip", .role = PartRole::grip, .composed_from = {}},
     .member = &StatusStrip::Parts::sizing_grip},
    {.declaration = {.name = "focus", .role = PartRole::focus, .composed_from = {}},
     .member = &StatusStrip::Parts::focus},
}};

constexpr std::array<PartDeclaration, 4> status_strip_parts = part_declarations(status_strip_part_fields);
constexpr std::array<AnatomyState, 5> status_strip_states{
    AnatomyState::normal, AnatomyState::enabled, AnatomyState::disabled,
    AnatomyState::focused, AnatomyState::focus_visible,
};

} // namespace

const ControlAnatomy& StatusStrip::anatomy() const noexcept {
    // The item system is ToolStrip's: the same parts and states per item.
    static const ControlAnatomy status_strip_anatomy{
        .control_id = "status_strip",
        .parts = status_strip_parts,
        .states = status_strip_states,
        .custom_states = {},
        .item_systems = ToolStrip::anatomy().item_systems,
    };
    return status_strip_anatomy;
}

void StatusStrip::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const ToolStrip::Parts strip = strip_parts();
    Parts record;
    record.region = strip.region;
    record.face = strip.face;
    record.sizing_grip = PartGeometry::from_bounds(layout().sizing_grip);
    record.focus = strip.focus;
    write_parts(anatomy(), status_strip_part_fields, record, geometry);
}

StatusStrip::Parts StatusStrip::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), status_strip_part_fields, published.parts(), record);
    return record;
}

} // namespace gui_forms
