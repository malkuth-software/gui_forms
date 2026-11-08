#pragma once

#include "gui_forms/animation/animation_timer/animation_timer.hpp"
#include "gui_forms/animation.hpp"
#include "gui_forms/basic_controls.hpp"
#include "gui_forms/controls/easing_preview/easing_preview_capabilities/easing_preview_capabilities.hpp"

#include <memory>
#include <span>
#include <string>
#include <vector>

namespace gui_forms {

struct PresentationSettings;

struct EasingPreviewTrack final {
    EasingCurve curve{EasingCurve::linear};
    std::string label;
    Color color{};

    friend bool operator==(const EasingPreviewTrack& left,
                           const EasingPreviewTrack& right) noexcept(noexcept(
        left.curve == right.curve && left.label == right.label &&
        left.color == right.color)) {
        return left.curve == right.curve && left.label == right.label &&
               left.color == right.color;
    }
};

// A retained animation/easing conformance surface.
class EasingPreview final : public Control, public EasingPreviewCapabilities<EasingPreview> {
public:
    explicit EasingPreview(StableId stable_id);

    [[nodiscard]] const BasicControlStyle& style() const noexcept {
        return style_;
    }
    void set_style(BasicControlStyle style);
    [[nodiscard]] const std::vector<EasingPreviewTrack>& tracks() const noexcept {
        return tracks_;
    }
    void set_tracks(std::vector<EasingPreviewTrack> tracks);
    [[nodiscard]] MotionPolicy effective_motion_policy() const noexcept;
    [[nodiscard]] double phase() const noexcept { return phase_; }

    // Samples the timeline at `now` into the phase; the frame timer calls it
    // on each tick, and tests call it to step the preview without a clock.
    void advance_animation(FrameTime now);
    void on_paint(Painter& painter, Rect local_damage) override;
    [[nodiscard]] bool hit_test_local(Point local_point) const override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    // Stage T1 anatomy, as dot members (see Control::Parts).
    struct Parts final {
        PartGeometry region;
        PartGeometry face;
        PartGeometry edge;
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
    void on_attached_to_window() override;
    void on_detached_from_window() noexcept override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;

private:
    // The animation timer's named target. Observes the preview; a destroyed
    // one is skipped.
    struct FrameTick final {
        std::weak_ptr<EasingPreview> preview;
        AnimationTickResult operator()(FrameTime now) const;
    };

    void register_frames();
    // One timer tick: samples the timeline and damages each marker's old and
    // new place and the readout.
    AnimationTickResult on_frame_tick(FrameTime now);
    // The places that move with the phase, used by on_paint and the tick's
    // damage: track `index`'s marker at `presented_phase`, and the readout.
    [[nodiscard]] Rect marker_bounds(std::size_t index, double presented_phase) const noexcept;
    [[nodiscard]] Rect readout_bounds() const noexcept;
    // The vertical origin of track `index`'s row.
    [[nodiscard]] double track_row_top(std::size_t index) const noexcept;
    [[nodiscard]] double track_left() const noexcept;
    [[nodiscard]] double track_width() const noexcept;
    void on_presentation_changed(const PresentationSettings& settings);
    [[nodiscard]] std::string motion_readout(double presented_phase) const;

    // ADR-016: a paint-gated UI timer the preview stops.
    AnimationTimer frames_;
    SubscriptionToken presentation_subscription_;
    AnimationTimeline timeline_;
    std::vector<EasingPreviewTrack> tracks_;
    BasicControlStyle style_;
    MotionPolicy motion_policy_{};
    double phase_{};
    bool timeline_started_{};
    // property_codemod begin EasingPreview (tools/property_families/easing_preview.json; generated, do not edit)
public:
    // EasingPreview's property table: Control's entries, then its own.
    static constexpr std::size_t easing_preview_property_count = Control::control_property_count + 1U;
    [[nodiscard]] static const std::array<PropertyEntry, easing_preview_property_count>& easing_preview_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

private:
    friend struct EasingPreviewTitleCapability<EasingPreview>;
    friend struct EasingPreviewSpecificationCapability<EasingPreview>;
    friend struct EasingPreviewMotionPolicyCapability<EasingPreview>;
    friend struct EasingPreviewMarkerSizeCapability<EasingPreview>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(title)& cell, std::string& incoming);
    void written(const decltype(title)& cell);
    void write_view(const decltype(specification)& cell, AnimationSpec incoming);
    [[nodiscard]] AnimationSpec read_view(const decltype(specification)& cell) const;
    void write_view(const decltype(motion_policy)& cell, MotionPolicy incoming);
    [[nodiscard]] MotionPolicy read_view(const decltype(motion_policy)& cell) const;
    void prepare_write(const decltype(marker_size)& cell, double& incoming);
    void written(const decltype(marker_size)& cell);
    // property_codemod end EasingPreview
};

} // namespace gui_forms
