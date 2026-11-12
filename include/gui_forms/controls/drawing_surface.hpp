#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/basic_controls.hpp"
#include "gui_forms/controls/drawing_surface_capabilities.hpp"

#include <functional>
#include <span>

namespace gui_forms {

// Public owner-render surface. Applications configure drawing through the
// renderer-neutral Painter vocabulary without creating a Control subclass.
class GUI_FORMS_API DrawingSurface final : public Control, public DrawingSurfaceCapabilities<DrawingSurface> {
public:
    using PaintCallback = std::function<void(Painter&, Rect, Rect)>;

    explicit DrawingSurface(StableId stable_id);

    void set_paint_callback(PaintCallback callback);
    [[nodiscard]] bool has_paint_callback() const noexcept {
        return static_cast<bool>(paint_callback_);
    }

    void on_paint(Painter& painter, Rect local_damage) override;
    [[nodiscard]] bool hit_test_local(Point local_point) const override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    // Stage T1 anatomy, as dot members (see Control::Parts).
    struct GUI_FORMS_API Parts final {
        PartGeometry region;
        PartGeometry face;
        PartGeometry focus;
    };
    struct GUI_FORMS_API States final {
        bool normal{};
        bool enabled{};
        bool disabled{};
        bool focused{};
        bool focus_visible{};
    };
    [[nodiscard]] Parts parts() const;
    [[nodiscard]] States states() const;

protected:
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;

private:
    PaintCallback paint_callback_;
    // property_codemod begin DrawingSurface (tools/property_families/drawing_surface.json; generated, do not edit)
private:
    friend struct DrawingSurfaceHitTestVisibleCapability<DrawingSurface>;
    friend struct DrawingSurfaceSemanticRoleCapability<DrawingSurface>;
    friend struct DrawingSurfaceBackgroundCapability<DrawingSurface>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(hit_test_visible)& cell, bool& incoming);
    void written(const decltype(hit_test_visible)& cell);
    void prepare_write(const decltype(semantic_role)& cell, SemanticRole& incoming);
    void written(const decltype(semantic_role)& cell);
    void prepare_write(const decltype(background)& cell, Color& incoming);
    void written(const decltype(background)& cell);
    // property_codemod end DrawingSurface
};

} // namespace gui_forms
