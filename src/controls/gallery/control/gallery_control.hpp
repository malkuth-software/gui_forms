#pragma once

#include "../context/gallery_context.hpp"
#include "gallery_dml.hpp"
#include "gui_forms/gui_forms.hpp"

#include <memory>
#include <string>

namespace gui_forms::gallery {

class GalleryControl final : public Control {
public:
    GalleryControl(StableId stable_id,
                   const dml::NodeSpec& specification,
                   std::shared_ptr<GalleryContext> context);

    [[nodiscard]] Size measure(Size available) override;
    void arrange(Rect final_bounds) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    [[nodiscard]] bool hit_test_local(Point local_point) const override;
    void on_pointer(PointerEvent& event) override;
    void on_text_input(TextInputEvent& event) override;
    void on_drag(DragEvent& event) override;
    void on_focus_changed(bool focused) override;
    void on_activate() override;

    [[nodiscard]] dml::NodeKind kind() const noexcept;
    [[nodiscard]] bool selected() const;
    [[nodiscard]] double value() const;
    [[nodiscard]] std::string display_text() const;

protected:
    void on_attached_to_window() override;
    void on_detached_from_window() noexcept override;

private:
    // The instrument's sweep timer target. Observes the control; a
    // destroyed one is skipped.
    struct SweepTick final {
        std::weak_ptr<GalleryControl> control;
        AnimationTickResult operator()(FrameTime now) const;
    };

    // One sweep tick: advances the sweep by the time since the last one and
    // damages the sweep line's old and new place.
    AnimationTickResult on_sweep_tick(FrameTime now);
    // The sweep line's bounds at `phase` (0 to 1 across the trace).
    [[nodiscard]] Rect sweep_bounds(double phase) const noexcept;

    const dml::NodeSpec* specification_{};
    std::shared_ptr<GalleryContext> context_;
    bool pressed_{};
    bool focused_{};
    bool drag_hovered_{};
    // The instrument's sweep (ADR-016: a paint-gated UI timer at the
    // instrument's 30 Hz, not an active surface). The phase advances with
    // time, not with paints.
    AnimationTimer sweep_timer_;
    FrameTime sweep_last_tick_{};
    double sweep_phase_{};

    void arrange_children(Size size);
};

} // namespace gui_forms::gallery
