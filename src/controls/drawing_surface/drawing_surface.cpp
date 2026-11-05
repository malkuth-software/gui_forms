#include "gui_forms/controls/drawing_surface/drawing_surface.hpp"

#include <array>
#include <utility>

namespace gui_forms {

DrawingSurface::DrawingSurface(StableId stable_id)
    : Control(std::move(stable_id)) {}

void DrawingSurface::set_paint_callback(PaintCallback callback) {
    require_mutable();
    paint_callback_ = std::move(callback);
    invalidate(Dirty::paint | Dirty::semantics);
}

void DrawingSurface::set_hit_test_visible(bool visible) {
    require_mutable();
    if (hit_test_visible_ == visible) return;
    hit_test_visible_ = visible;
    invalidate(Dirty::hit_test | Dirty::semantics);
}

void DrawingSurface::set_semantic_role(SemanticRole role) {
    require_mutable();
    if (semantic_role_ == role) return;
    semantic_role_ = role;
    invalidate(Dirty::semantics);
}

void DrawingSurface::set_background(Color color) {
    require_mutable();
    if (background_ == color) return;
    background_ = color;
    invalidate(Dirty::paint);
}

void DrawingSurface::on_paint(Painter& painter, Rect local_damage) {
    const Rect bounds{0.0, 0.0, committed_arranged_bounds().width,
                      committed_arranged_bounds().height};
    painter.fill_rect(bounds, background_);
    if (paint_callback_) paint_callback_(painter, bounds, local_damage);
}

bool DrawingSurface::hit_test_local(Point local_point) const {
    return hit_test_visible_ && Control::hit_test_local(local_point);
}

SemanticDescriptor DrawingSurface::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.role = semantic_role_;
    descriptor.name = accessible_name.read();
    descriptor.description = accessible_description.read();
    descriptor.exposed = !descriptor.name.empty() || !descriptor.description.empty();
    return descriptor;
}


namespace {

constexpr std::array<PartField<DrawingSurface::Parts>, 3> drawing_surface_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &DrawingSurface::Parts::region},
    {.declaration = {.name = "face", .role = PartRole::face, .composed_from = {}},
     .member = &DrawingSurface::Parts::face},
    {.declaration = {.name = "focus", .role = PartRole::focus, .composed_from = {}},
     .member = &DrawingSurface::Parts::focus},
}};

constexpr std::array<StateField<DrawingSurface::States>, 5> drawing_surface_state_fields{{
    {.state = AnatomyState::normal, .member = &DrawingSurface::States::normal},
    {.state = AnatomyState::enabled, .member = &DrawingSurface::States::enabled},
    {.state = AnatomyState::disabled, .member = &DrawingSurface::States::disabled},
    {.state = AnatomyState::focused, .member = &DrawingSurface::States::focused},
    {.state = AnatomyState::focus_visible, .member = &DrawingSurface::States::focus_visible},
}};

constexpr std::array<PartDeclaration, 3> drawing_surface_parts =
    part_declarations(drawing_surface_part_fields);
constexpr std::array<AnatomyState, 5> drawing_surface_states =
    state_declarations(drawing_surface_state_fields);

constexpr ControlAnatomy drawing_surface_anatomy{
    .control_id = "drawing_surface",
    .parts = drawing_surface_parts,
    .states = drawing_surface_states,
    .custom_states = {},
};

} // namespace

const ControlAnatomy& DrawingSurface::anatomy() const noexcept {
    return drawing_surface_anatomy;
}

void DrawingSurface::publish_part_geometry(std::span<PartGeometry> geometry) const {

    const Rect bounds = client_rectangle();
    Parts record;
    record.region = PartGeometry::from_bounds(bounds);
    // The background fill; the application's drawing lies over it.
    if (background_.alpha != 0U) {
        record.face = PartGeometry::from_bounds(bounds);
    }
    record.focus = PartGeometry::from_bounds(bounds);
    write_parts(anatomy(), drawing_surface_part_fields, record, geometry);
}

DrawingSurface::Parts DrawingSurface::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), drawing_surface_part_fields, published.parts(), record);
    return record;
}

DrawingSurface::States DrawingSurface::states() const {
    States record;
    read_states(drawing_surface_state_fields, anatomy_states(), record);
    return record;
}

} // namespace gui_forms
