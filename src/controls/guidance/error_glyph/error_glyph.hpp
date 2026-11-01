#pragma once

#include "gui_forms/components/error_provider/error_provider.hpp"
#include "gui_forms/animation/animation_timer/animation_timer.hpp"
#include "gui_forms/control.hpp"

namespace gui_forms {

class ErrorGlyph final : public Control {
public:
    explicit ErrorGlyph(StableId stable_id);

    void set_error(std::string error);
    void set_icon(std::optional<ImageId> icon);
    void configure_blink(ErrorBlinkStyle style,
                         std::chrono::milliseconds rate,
                         bool restart);
    void refresh_motion_policy();
    [[nodiscard]] bool blink_active() const noexcept;
    [[nodiscard]] bool phase_visible() const noexcept;

    void on_paint(Painter& painter, Rect local_damage) override;
    [[nodiscard]] bool hit_test_local(Point point) const override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    void on_frame(FrameTime now) override;

protected:
    void on_attachment_committed() noexcept override;
    void on_detached_from_window() noexcept override;

private:
    // The blink timer's target. Observes the glyph; a destroyed one is
    // skipped.
    struct BlinkTick final {
        std::weak_ptr<ErrorGlyph> glyph;
        AnimationTickResult operator()(FrameTime now) const;
    };

    void refresh_schedule();

    std::string error_;
    std::optional<ImageId> icon_;
    // ADR-016: a paint-gated UI timer the glyph stops, not an active surface.
    AnimationTimer blink_timer_;
    std::chrono::milliseconds rate_{250};
    ErrorBlinkStyle style_{ErrorBlinkStyle::blink_if_different_error};
    std::size_t transitions_remaining_{6U};
    bool phase_visible_{true};
};

} // namespace gui_forms
