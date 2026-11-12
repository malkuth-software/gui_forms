#pragma once

#include "gui_forms/controls/ribbon_color_picker.hpp"

namespace gui_forms {

// The content of a colour picker's palette overlay: Automatic, the palette and
// the recent colours on a panel that casts the overlay shadow. Private to the
// picker, which owns it and closes the overlay before it goes; `picker`
// observes the owner (house style §4) and is cleared by detach().
// Interaction state lives in the picker.
class RibbonColorPalette final : public Control {
public:
    RibbonColorPalette(StableId stable_id, RibbonColorPicker& picker);

    void detach() noexcept { picker_ = nullptr; }

    [[nodiscard]] Size measure(Size available) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    void on_pointer(PointerEvent& event) override;
    void on_key(KeyEvent& event) override;
    void on_focus_changed(bool focused) override;
    [[nodiscard]] Insets visual_outsets() const noexcept override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] std::vector<SemanticNode> semantic_virtual_children() const override;
    bool on_semantic_child_action(std::string_view stable_id, SemanticAction action,
                                  std::string_view value) override;

private:
    [[nodiscard]] bool picker_alive() const noexcept;

    RibbonColorPicker* picker_{};
};

} // namespace gui_forms
