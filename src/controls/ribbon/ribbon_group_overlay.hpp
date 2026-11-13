#pragma once

#include "gui_forms/controls/ribbon.hpp"

namespace gui_forms {

// The content of a collapsed group's overlay (stage RB, phase 2b; owner
// decision 27): the group at its largest layout on a panel that casts the
// overlay shadow (decision 37). Private to the ribbon, which owns it, makes
// it on the first overlay and disposes it with itself; `ribbon` observes the
// owner (house style §4: the back link does not extend its lifetime, and the
// ribbon closes the overlay before it goes).
//
// An AnchoredOverlayLayer places the content below the ribbon, its left edge
// at the ribbon's (near alignment), as wide as the ribbon's left edge to the
// panel's right edge; the panel sits at the group button's left within it,
// moved left only to stay inside the window. Outside the panel the content
// is transparent to input, so a click there reaches the layer and dismisses
// the overlay. Interaction state lives in the ribbon: a command shows in the
// plane or in the overlay, never both.
class RibbonGroupOverlay final : public Control {
public:
    RibbonGroupOverlay(StableId stable_id, Ribbon& ribbon);

    // The panel's left edge in window coordinates, as the ribbon asks.
    void set_anchor_x(double x);
    // The ribbon is going: stop forwarding to it.
    void detach() noexcept { ribbon_ = nullptr; }
    // The group's rectangle in local coordinates, from the last arrange.
    [[nodiscard]] Rect panel() const noexcept { return panel_; }

    [[nodiscard]] Size measure(Size available) override;
    void arrange(Rect final_bounds) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    void on_focus_changed(bool focused) override;
    [[nodiscard]] bool hit_test_local(Point local_point) const override;
    [[nodiscard]] Insets visual_outsets() const noexcept override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] std::vector<SemanticNode> semantic_virtual_children() const override;
    bool on_semantic_child_action(std::string_view stable_id, SemanticAction action,
                                  std::string_view value) override;

protected:
    void on_mouse_down(MouseEventArgs& arguments) override;
    void on_mouse_up(MouseEventArgs& arguments) override;
    void on_mouse_move(MouseEventArgs& arguments) override;
    void on_mouse_wheel(MouseEventArgs& arguments) override;
    void on_mouse_leave() override;
    void on_key_down(KeyEventArgs& arguments) override;
    void on_key_up(KeyEventArgs& arguments) override;

private:
    // The ribbon's handling of the overlay's mouse and keys.
    void forward_mouse(MouseEventArgs& arguments);
    void forward_key(KeyEventArgs& arguments, KeyAction action);
    [[nodiscard]] bool ribbon_alive() const noexcept;

    Ribbon* ribbon_{};
    double anchor_x_{};
    Rect panel_{};
};

} // namespace gui_forms
