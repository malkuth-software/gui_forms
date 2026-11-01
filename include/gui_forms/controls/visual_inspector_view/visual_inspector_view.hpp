#pragma once

#include "gui_forms/animation/animation_timer/animation_timer.hpp"
#include "gui_forms/control.hpp"

#include <optional>
#include <span>
#include <string>

namespace gui_forms {

// A reusable, renderer-neutral presentation of Window visual inspection. The
// view consumes only the public snapshot and is suitable for demoboards and
// local application diagnostics; it is never required for ordinary rendering.
class VisualInspectorView final : public Control {
public:
    explicit VisualInspectorView(StableId stable_id,
                                 std::string target_stable_id = {});

    [[nodiscard]] const std::string& target_stable_id() const noexcept {
        return target_stable_id_;
    }
    void set_target_stable_id(std::string stable_id);
    // Repaints (and republishes semantics) unconditionally.
    void refresh();
    // Repaints only when the text the view would draw differs from what it
    // last drew, so polling an unchanged target costs one inspection snapshot
    // and no paint. The pending damage count is not compared: it describes
    // the paint that drew it, which always includes the view's own damage. Returns whether it repainted. Poll it from a UI timer: an
    // active surface repaints its control on every tick whatever on_frame does.
    bool refresh_if_changed();
    // Polls refresh_if_changed every `interval` on a paint-gated
    // AnimationTimer while the view is shown (ADR-016): an unchanged poll
    // damages nothing, and a hidden view or covered window holds no wake.
    // An interval below 1 ms throws std::invalid_argument.
    void start_polling(FrameInterval interval);
    void stop_polling() noexcept;

    void on_frame(FrameTime now) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    // Stage T1 anatomy, as dot members (see Control::Parts).
    struct Parts final {
        PartGeometry region;
        PartGeometry face;
        PartGeometry header;
        PartGeometry accent_strip;
        PartGeometry focus;
    };
    struct States final {
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
    // The poll timer's target. Observes the control; a destroyed one is
    // skipped.
    struct PollTick final {
        std::weak_ptr<VisualInspectorView> control;
        AnimationTickResult operator()(FrameTime now) const;
    };
    AnimationTimer poll_timer_;
    [[nodiscard]] std::string accessible_value() const;
    std::string target_stable_id_;
    // The rows on_paint last drew, as one key (empty before the first paint).
    std::string painted_content_;
};

// Transparent overlay companion which draws the target's arranged, visual,
// and effective clipped bounds in three distinct colors. It does not alter hit
// testing or the inspected control tree.
class VisualInspectorOverlay final : public Control {
public:
    explicit VisualInspectorOverlay(StableId stable_id,
                                    std::string target_stable_id = {});

    [[nodiscard]] const std::string& target_stable_id() const noexcept {
        return target_stable_id_;
    }
    void set_target_stable_id(std::string stable_id);
    // Repaints unconditionally.
    void refresh();
    // Repaints only when the outlines differ from the ones last drawn, and
    // returns whether it did. The overlay usually spans the window, so an
    // unconditional repaint repaints the whole window; poll this from a UI
    // timer, not an active surface (see VisualInspectorView).
    bool refresh_if_changed();
    // Polls refresh_if_changed every `interval` on a paint-gated
    // AnimationTimer while the overlay is shown (ADR-016): an unchanged poll
    // damages nothing, and a hidden overlay or covered window holds no wake.
    // An interval below 1 ms throws std::invalid_argument.
    void start_polling(FrameInterval interval);
    void stop_polling() noexcept;

    void on_frame(FrameTime now) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    [[nodiscard]] bool hit_test_local(Point local_point) const override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    // Stage T1 anatomy, as dot members (see Control::Parts).
    struct Parts final {
        PartGeometry region;
        PartGeometry highlight;
        PartGeometry focus;
    };
    struct States final {
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
    // The poll timer's target. Observes the control; a destroyed one is
    // skipped.
    struct PollTick final {
        std::weak_ptr<VisualInspectorOverlay> control;
        AnimationTickResult operator()(FrameTime now) const;
    };
    AnimationTimer poll_timer_;
    // The three outlines in this control's coordinates, or none when the
    // target is missing (or is this overlay).
    struct Outlines final {
        bool shown{};
        Rect visual{};
        Rect arranged{};
        Rect clip{};
        friend bool operator==(const Outlines& left, const Outlines& right) {
            const bool same = left.shown == right.shown && left.visual == right.visual &&
                              left.arranged == right.arranged && left.clip == right.clip;
            return same;
        }
    };
    [[nodiscard]] Outlines current_outlines() const;
    std::string target_stable_id_;
    // What on_paint last drew; nullopt before the first paint.
    std::optional<Outlines> painted_;
};

} // namespace gui_forms
