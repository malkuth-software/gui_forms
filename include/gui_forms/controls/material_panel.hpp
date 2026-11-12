#pragma once

#include "gui_forms/basic_controls.hpp"
#include "gui_forms/surface_material.hpp"
#include "gui_forms/controls/material_panel_capabilities.hpp"

#include <span>

namespace gui_forms {

// General retained material container. It is deliberately content-agnostic.
class MaterialPanel : public Panel, public MaterialPanelCapabilities<MaterialPanel> {
public:
    explicit MaterialPanel(StableId stable_id);

    [[nodiscard]] Event<const SurfaceMaterial&>& material_changed() noexcept {
        return material_changed_;
    }

    void on_paint(Painter& painter, Rect local_damage) override;
    [[nodiscard]] Insets visual_outsets() const noexcept override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void on_attached_to_window() override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;

private:
    void validate_window_images(const SurfaceMaterial& recipe) const;

    Event<const SurfaceMaterial&> material_changed_;
    // property_codemod begin MaterialPanel (tools/property_families/material_panel.json; generated, do not edit)
private:
    friend struct MaterialPanelMaterialCapability<MaterialPanel>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(material)& cell, SurfaceMaterial& incoming);
    void written(const decltype(material)& cell);
    // property_codemod end MaterialPanel
};

} // namespace gui_forms
