#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/scheduler/types/scheduler_types.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/controls/spinner_capabilities.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace gui_forms {

// An indeterminate busy indicator (W-4): a ring of twelve dots, three lit,
// turning once a second while `active`. It shows that work is under way,
// not how much; ProgressRing shows an amount. The motion is an ambient
// control on the window's motion clock (MotionClock::enlist_control_ambient,
// as SearchField's busy strip): a frame per dot step while the spinner is
// drawn, dropped when it is not; with ambient motion suppressed (reduced
// motion, ambient motion off, an occluded window) the lit dots stand still
// at twelve o'clock. An inactive spinner draws nothing and is off the
// clock. It takes no input and no focus.
//
// Accessibility: a progress bar without a value, busy while active, named
// by its accessible name; an inactive spinner is not exposed.
//
// Theme: its dots are the progress family's `track` and, lit, its
// `activity` part with the selected state (src/controls/progress_ring/
// ring_dots.hpp), so the floor sheet's :family(progress) blocks style it.
class GUI_FORMS_API Spinner final : public Control, public SpinnerCapabilities<Spinner> {
public:
    explicit Spinner(StableId stable_id);

    // The dots in the ring and the lit run.
    static constexpr std::size_t dot_count = 12U;
    static constexpr std::size_t lit_count = 3U;
    // One turn of the ring.
    static constexpr FrameInterval turn_period = std::chrono::milliseconds(1200);

    // The first lit dot now, from twelve o'clock clockwise (0 while still).
    [[nodiscard]] std::size_t phase() const noexcept { return phase_; }
    // Whether the ring is turning: on the window's motion clock.
    [[nodiscard]] bool moving() const noexcept;

    [[nodiscard]] Size measure(Size available) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    [[nodiscard]] bool hit_test_local(Point local_point) const override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    [[nodiscard]] AnatomyStateSet anatomy_states() const override;
    // Stage T1 anatomy, as dot members (see Control::Parts). `activity` is
    // the ring's square, present while active.
    struct GUI_FORMS_API Parts final {
        PartGeometry region;
        PartGeometry activity;
    };
    [[nodiscard]] Parts parts() const;

protected:
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;
    void on_detached_from_window() noexcept override;
    // One ambient frame: the ring moves to the step `now` gives.
    bool on_ambient_frame(FrameTime now) override;

private:
    // Whether the ring turns now: active, in a window whose ambient motion is
    // not suppressed (reduced motion, ambient motion off, occluded).
    [[nodiscard]] bool wants_motion() const noexcept;
    // Enlists the control on the window's motion clock, or withdraws it,
    // for wants_motion(); `painting` is true from on_paint, the only place
    // that enlists it (a control not drawn is not kept on the clock). A
    // withdrawn ring returns to phase 0.
    void update_motion(bool painting);
    // The square the ring is drawn in: the client area's centred square.
    [[nodiscard]] Rect ring_square() const noexcept;

    // The clock time phase 0 began at, the paints counted, and the count
    // the last ambient frame saw (a frame with no paint since is dropped).
    FrameTime motion_start_{};
    std::uint64_t paints_{};
    std::uint64_t paints_seen_{};
    std::size_t phase_{};
    // property_codemod begin Spinner (tools/property_families/spinner.json; generated, do not edit)
public:
    // Spinner's property table: Control's entries, then its own.
    static constexpr std::size_t spinner_property_count = Control::control_property_count + 1U;
    [[nodiscard]] static const std::array<PropertyEntry, spinner_property_count>& spinner_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

private:
    friend struct SpinnerActiveCapability<Spinner>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(active)& cell, bool& incoming);
    void written(const decltype(active)& cell);
    // property_codemod end Spinner
};

} // namespace gui_forms
