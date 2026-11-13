#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/controls/container_control.hpp"
#include "gui_forms/controls/splitter_panel_capabilities.hpp"

#include <span>

namespace gui_forms {

class GUI_FORMS_API SplitterPanel final : public ContainerControl, public SplitterPanelCapabilities<SplitterPanel> {
public:
    explicit SplitterPanel(StableId stable_id);

    void on_paint(Painter& painter, Rect local_damage) override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    // Stage T1 anatomy, as dot members: the scrollable parts and the face.
    struct GUI_FORMS_API Parts final {
        ScrollableControl::Parts scrollable;
        PartGeometry face;
    };
    [[nodiscard]] Parts parts() const;

protected:
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;

private:
    // property_codemod begin SplitterPanel (tools/property_families/splitter_panel.json; generated, do not edit)
private:
    friend struct SplitterPanelBackgroundCapability<SplitterPanel>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(background)& cell, Color& incoming);
    void written(const decltype(background)& cell);
    // property_codemod end SplitterPanel
};

} // namespace gui_forms
