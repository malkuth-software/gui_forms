#pragma once

#include "gui_forms/animation/animation_timer/animation_timer.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/controls/visual_inspector_view/visual_inspector_view_capabilities/visual_inspector_view_capabilities.hpp"
#include "gui_forms/controls/visual_inspector_view/visual_inspector_overlay_capabilities/visual_inspector_overlay_capabilities.hpp"

#include <optional>
#include <span>
#include <string>

namespace gui_forms {

// A reusable, renderer-neutral presentation of Window visual inspection. The
// view consumes only the public snapshot and is suitable for demoboards and
// local application diagnostics; it is never required for ordinary rendering.
class VisualInspectorView final : public Control, public VisualInspectorViewCapabilities<VisualInspectorView> {
public:
    explicit VisualInspectorView(StableId stable_id,
                                 std::string initial_target = {});

    // Repaints (and republishes semantics) unconditionally.
    void refresh();
    // Repaints only when the text the view would draw differs from what it
    // last drew, so polling an unchanged target costs one inspection snapshot
    // and no paint. The pending damage count is not compared: it describes
    // the paint that drew it, which always includes the view's own damage. Returns whether it repainted.
    bool refresh_if_changed();
    // Polls refresh_if_changed every `interval` on a paint-gated
    // AnimationTimer while the view is shown (ADR-016): an unchanged poll
    // damages nothing, and a hidden view or covered window holds no wake.
    // An interval below 1 ms throws std::invalid_argument.
    void start_polling(FrameInterval interval);
    void stop_polling() noexcept;

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
    // The window state a semantic value was computed in. Hover is not in it:
    // the window publishes no hovered control, and a hover change that
    // repaints nothing leaves the value as it was until the next repaint
    // (a polling view repaints itself when its rows change).
    struct SemanticValueKey final {
        std::uint64_t content_revision{};
        std::uint64_t display_generation{};
        RuntimeId focused{};
        RuntimeId pressed{};
        RuntimeId captured{};
        bool window_active{};
    };
    struct SemanticValue final {
        SemanticValueKey key;
        std::string value;
    };
    // A UI Automation client reads an element through many calls, each of
    // which takes a semantic snapshot, and the value needs a whole-window
    // visual inspection; the value is reused while its key holds. Written
    // only on the UI thread, from semantic_descriptor (hence mutable), and
    // dropped by refresh and a new target.
    [[nodiscard]] static SemanticValueKey semantic_value_key(const Window& owner);
    [[nodiscard]] static bool same_key(const SemanticValueKey& left,
                                       const SemanticValueKey& right) noexcept;
    mutable std::optional<SemanticValue> semantic_value_;
    // The rows on_paint last drew, as one key (empty before the first paint).
    std::string painted_content_;
    // property_codemod begin VisualInspectorView (tools/property_families/visual_inspector_view.json; generated, do not edit)
public:
    // VisualInspectorView's property table: Control's entries, then its own.
    static constexpr std::size_t visual_inspector_view_property_count = Control::control_property_count + 1U;
    [[nodiscard]] static const std::array<PropertyEntry, visual_inspector_view_property_count>& visual_inspector_view_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

private:
    friend struct VisualInspectorViewTargetStableIdCapability<VisualInspectorView>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(target_stable_id)& cell, std::string& incoming);
    void written(const decltype(target_stable_id)& cell);
    // property_codemod end VisualInspectorView
};

// Transparent overlay companion which draws the target's arranged, visual,
// and effective clipped bounds in three distinct colors. It does not alter hit
// testing or the inspected control tree.
class VisualInspectorOverlay final : public Control, public VisualInspectorOverlayCapabilities<VisualInspectorOverlay> {
public:
    explicit VisualInspectorOverlay(StableId stable_id,
                                    std::string initial_target = {});

    // Repaints unconditionally.
    void refresh();
    // Repaints only when the outlines differ from the ones last drawn, and
    // returns whether it did. The overlay usually spans the window, so an
    // unconditional repaint repaints the whole window; poll it with
    // start_polling.
    bool refresh_if_changed();
    // Polls refresh_if_changed every `interval` on a paint-gated
    // AnimationTimer while the overlay is shown (ADR-016): an unchanged poll
    // damages nothing, and a hidden overlay or covered window holds no wake.
    // An interval below 1 ms throws std::invalid_argument.
    void start_polling(FrameInterval interval);
    void stop_polling() noexcept;

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
    void paint_outline(Painter& painter, Rect bounds, Color colour, double width) const;
    // What on_paint last drew; nullopt before the first paint.
    std::optional<Outlines> painted_;
    // property_codemod begin VisualInspectorOverlay (tools/property_families/visual_inspector_overlay.json; generated, do not edit)
public:
    // VisualInspectorOverlay's property table: Control's entries, then its own.
    static constexpr std::size_t visual_inspector_overlay_property_count = Control::control_property_count + 1U;
    [[nodiscard]] static const std::array<PropertyEntry, visual_inspector_overlay_property_count>& visual_inspector_overlay_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

private:
    friend struct VisualInspectorOverlayTargetStableIdCapability<VisualInspectorOverlay>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(target_stable_id)& cell, std::string& incoming);
    void written(const decltype(target_stable_id)& cell);
    // property_codemod end VisualInspectorOverlay
};

} // namespace gui_forms
