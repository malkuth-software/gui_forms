#pragma once

#include "gui_forms/controls/tool_strip.hpp"

namespace gui_forms {

// A status bar (Windows Forms' StatusStrip): a ToolStrip laid out for the
// bottom of a window. Its items never overflow; labels marked `spring` share
// the width the other items leave; a sizing grip shows in its corner. The
// grip is drawn and published but does not resize the window yet: a control
// has no native window request (ApplicationWindowHandle::request_client_size
// belongs to the application).
class StatusStrip final : public ToolStrip {
public:
    explicit StatusStrip(StableId stable_id);

    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    // Stage T1 anatomy, as dot members; its states are Control's, and its
    // items are ToolStrip's "item" system.
    struct Parts final {
        PartGeometry region;
        PartGeometry face;
        PartGeometry sizing_grip;
        PartGeometry focus;
    };
    [[nodiscard]] Parts parts() const;

protected:
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;
};

} // namespace gui_forms
