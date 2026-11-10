#pragma once

#include "gui_forms/controls/panel/border_style/border_style.hpp"
#include "gui_forms/controls/panel/panel_capabilities/panel_capabilities.hpp"
#include "gui_forms/controls/scrollable_control/scrollable_control.hpp"
#include "gui_forms/theme.hpp"

#include <cstdint>
#include <optional>
#include <span>

namespace gui_forms {

class Panel : public ScrollableControl, public PanelCapabilities<Panel> {
public:
    explicit Panel(StableId stable_id);

    // The face's ground: the background or back_color override, else the
    // material a theme rule's fields assemble for the face (G3), else the
    // theme recipe's material; a gradient or image reports its first colour.
    [[nodiscard]] Color background() const;
    [[nodiscard]] bool has_background_override() const noexcept {
        return background_override_.has_value();
    }
    void set_background(Color color);
    void clear_background();
    [[nodiscard]] const BasicControlStyle& style() const noexcept;
    [[nodiscard]] bool has_style_override() const noexcept {
        return style_override_.has_value();
    }
    void set_style(BasicControlStyle style);
    void clear_style();

    void on_paint(Painter& painter, Rect local_damage) override;
    // A panel paints on the backplane (under the window's controls) while
    // every ancestor below the root does too; under a control that paints
    // on the control plane (a custom control hosting it, as a docking
    // pane hosts its content) it paints on the control plane, so it shows
    // over that ancestor as a WinForms child does. Decided at attach.
    void on_attached_to_window() override;
    [[nodiscard]] Insets visual_outsets() const noexcept override;
    // False for a borderless panel without scroll bars whose face is a
    // solid colour or a size-invariant material (surface_material_size_invariant).
    [[nodiscard]] bool paint_depends_on_size() const noexcept override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    // Stage T1 anatomy, as dot members: the scrollable parts, a face and an edge.
    struct Parts final {
        ScrollableControl::Parts scrollable;
        PartGeometry face;
        PartGeometry edge;
    };
    [[nodiscard]] Parts parts() const;

protected:
    [[nodiscard]] Rect local_bounds() const noexcept;
    void paint_panel(Painter& painter, Rect bounds) const;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;
    // The scrollable parts, the face, and the edge when a border style is set.
    [[nodiscard]] Parts panel_parts() const;
    // Whether a theme rule selects a primitive for the face (G3): then the
    // rule, not the recipe, decides what the face draws.
    [[nodiscard]] bool face_selected_by_rule() const noexcept;

private:
    // The colour that replaces the themed material on the panel's face: the
    // panel background (set_background), else the control's own back_color
    // outside high contrast (Control::paint_back_color's rule); none keeps
    // the material.
    [[nodiscard]] std::optional<Color> face_colour() const noexcept;
    // The face part paint_panel draws over `bounds`, keyed "face" so that a
    // theme rule can select it (G3).
    [[nodiscard]] PartPaint face_part(Rect bounds) const noexcept;
    // The face's material as the selecting rule's material fields assemble
    // it over the recipe's, into `material`; false where no rule's material
    // fields select the face. Assembling allocates.
    [[nodiscard]] bool rule_face_material(SurfaceMaterial& material) const;

    std::optional<BasicControlStyle> style_override_;
    std::optional<Color> background_override_;
    // property_codemod begin Panel (tools/property_families/panel.json; generated, do not edit)
public:
    // Panel's property table: ScrollableControl's entries, then its own.
    static constexpr std::size_t panel_property_count = ScrollableControl::scrollable_control_property_count + 1U;
    [[nodiscard]] static const std::array<PropertyEntry, panel_property_count>& panel_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

private:
    friend struct BorderStyleCapability<Panel>;
    friend struct VisualRoleCapability<Panel>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(border_style)& cell, BorderStyle& incoming);
    void written(const decltype(border_style)& cell);
    void prepare_write(const decltype(visual_role)& cell, ControlVisualRole& incoming);
    void written(const decltype(visual_role)& cell);
    // property_codemod end Panel
};

} // namespace gui_forms
