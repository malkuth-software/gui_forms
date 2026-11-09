#include "gui_forms/controls/panel/material_panel/material_panel.hpp"
#include "gui_forms/window.hpp"
#include "../panel_anatomy.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include <array>
#include <stdexcept>
#include <utility>
#include <vector>

namespace gui_forms {

MaterialPanel::MaterialPanel(StableId stable_id)
    : Panel(std::move(stable_id)) {
    set_background(Color::rgba(0, 0, 0, 0));
}

void MaterialPanel::on_attached_to_window() {
    Panel::on_attached_to_window();
    validate_window_images(material.read());
}

void MaterialPanel::validate_window_images(
    const SurfaceMaterial& recipe) const {
    if (window() == nullptr) return;
    // The fills, then an image mask (E9.6).
    std::vector<const MaterialFillLayer*> layers{};
    layers.reserve(recipe.fills.size() + 1U);
    for (const MaterialFillLayer& fill : recipe.fills) layers.push_back(&fill);
    if (recipe.mask) layers.push_back(&*recipe.mask);
    for (const MaterialFillLayer* layer : layers) {
        const MaterialFillLayer& fill = *layer;
        if (fill.kind != MaterialFillKind::image) continue;
        const std::optional<ImageResourceView> resource = (*window()).image_resources().find(fill.image);
        if (!resource ||
            static_cast<double>((*resource).metadata.width) !=
                fill.image_pixel_size.width ||
            static_cast<double>((*resource).metadata.height) !=
                fill.image_pixel_size.height) {
            throw std::invalid_argument(
                "material image is missing from the attached Window or its declared pixel size is stale");
        }
    }
}

void MaterialPanel::on_paint(Painter& painter, Rect) {
    const Rect arranged = committed_arranged_bounds();
    const Rect bounds{0.0, 0.0, arranged.width, arranged.height};
    if (bounds.empty()) return;

    paint_surface_material(painter, bounds, material.read());
}

Insets MaterialPanel::visual_outsets() const noexcept {
    return surface_material_visual_outsets(material.read());
}

namespace {

constexpr ControlAnatomy material_panel_anatomy{
    .control_id = "material_panel",
    .parts = panel_detail::parts,
    .states = scrollable_detail::states,
    .custom_states = {},
};

} // namespace

const ControlAnatomy& MaterialPanel::anatomy() const noexcept {
    return material_panel_anatomy;
}

void MaterialPanel::publish_part_geometry(std::span<PartGeometry> geometry) const {
    // The material is the whole face; Panel's border style is not painted.
    Parts record;
    record.scrollable = scrollable_parts();
    record.face = PartGeometry::from_bounds(client_rectangle());
    write_parts(anatomy(), scrollable_detail::part_fields, record.scrollable, geometry);
    write_parts(anatomy(), panel_detail::extra_fields, record, geometry);
}

namespace {

// The events MaterialPanel adds to those of Panel, in declaration order.
constexpr std::array<InteractionDeclaration, 1> material_panel_interactions{{
    {.name = "material_changed", .payload = "SurfaceMaterial", .mutable_payload = false},
}};

} // namespace

void MaterialPanel::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Panel::append_interactions(interactions);
    interactions.insert(interactions.end(), material_panel_interactions.begin(), material_panel_interactions.end());
}

// property_codemod begin MaterialPanel (tools/property_families/material_panel.json; generated, do not edit)

void MaterialPanel::prepare_write(const decltype(material)&, SurfaceMaterial& incoming) {
    require_mutable();
    if (!valid_surface_material(incoming)) throw std::invalid_argument("material recipe is invalid or unbounded");
    validate_window_images(incoming);
}

void MaterialPanel::written(const decltype(material)&) {
    invalidate(invalidation::paint_only);
    publish_change(material_changed_, material.read());
}

// property_codemod end MaterialPanel

} // namespace gui_forms
