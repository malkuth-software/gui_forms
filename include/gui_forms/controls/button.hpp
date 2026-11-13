#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/animation/animation_timer/animation_timer.hpp"
#include "gui_forms/controls/button_base.hpp"
#include "gui_forms/controls/button_values.hpp"
#include "gui_forms/controls/button_capabilities.hpp"

#include <cstdint>
#include <memory>
#include <optional>

namespace gui_forms {

// A push button. A button that marks a dangerous or irreversible action
// sets `danger` (skeudoindustrial surfaces, follow-up 5): its parts take the
// custom state `danger`, a warning mark leads its content (the cue that does
// not rest on colour), and a theme that styles the state, such as the
// default theme's `button::part(face):state(danger)`, lights the face from
// inside. The lit face pulses slowly and gently over the plain face
// (danger_glow_floor to full, over danger_glow_period, at
// danger_glow_interval), only inside the face, and holds full and steady
// under reduced motion, while disabled and outside a window.
class GUI_FORMS_API Button : public ButtonBase, public ButtonCapabilities<Button> {
public:
    // The danger glow's pulse: the share of the lit face shown at its
    // lowest, the period of one rise and fall, and the frame interval (the
    // glow's whole frame budget: some 12 frames a second).
    static constexpr double danger_glow_floor = 0.72;
    static constexpr FrameInterval danger_glow_period = std::chrono::milliseconds(2600);
    static constexpr FrameInterval danger_glow_interval = std::chrono::milliseconds(80);
    // The lit face's share at `since` into the pulse: full at the start,
    // danger_glow_floor half a period later.
    [[nodiscard]] static double danger_glow_level(FrameInterval since) noexcept;

    explicit Button(StableId stable_id, std::string text = {});

    [[nodiscard]] Event<DialogResult>& dialog_result_changed() noexcept {
        return dialog_result_changed_;
    }
    [[nodiscard]] Event<bool>& selected_changed() noexcept { return selected_changed_; }
    void on_paint(Painter& painter, Rect local_damage) override;
    [[nodiscard]] Insets visual_outsets() const noexcept override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    // Stage T1 anatomy, as dot members (see Control::Parts).
    struct GUI_FORMS_API Parts final {
        PartGeometry region;
        PartGeometry face;
        PartGeometry icon;
        PartGeometry label;
        PartGeometry preselect;
        PartGeometry focus;
    };
    struct GUI_FORMS_API States final {
        bool normal{};
        bool hot{};
        bool pressed{};
        bool preselect{};
        bool selected{};
        bool enabled{};
        bool disabled{};
        bool focused{};
        bool focus_visible{};
        bool open{};
        bool closed{};
    };
    [[nodiscard]] Parts parts() const;
    [[nodiscard]] States states() const;
    [[nodiscard]] AnatomyStateSet anatomy_states() const override;
    // The lit face's current share of the danger glow (1 when steady), and
    // whether the pulse is running.
    [[nodiscard]] double danger_glow() const noexcept {
        return danger_level_;
    }
    [[nodiscard]] bool danger_glowing() const noexcept;

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;
    // The button's parts computed for its current style.
    [[nodiscard]] Parts button_parts() const;
    [[nodiscard]] FaceLayer face_layer() const noexcept override;
    void on_attached_to_window() override;
    void on_detached_from_window() noexcept override;
    void on_enabled_changed() override;

private:
    // The danger glow's frames: observes the button, so a destroyed
    // button's tick is skipped.
    struct DangerTick final {
        std::weak_ptr<Button> button;
        AnimationTickResult operator()(FrameTime now) const;
    };
    // Starts the pulse where it can run (danger set, in a window, enabled,
    // motion not reduced), else stops it and holds the glow steady.
    void update_danger_glow();
    AnimationTickResult advance_danger_glow(FrameTime now);
    double danger_level_{1.0};
    std::optional<FrameTime> danger_since_{};
    AnimationTimer danger_timer_;

    Event<bool> selected_changed_{};
    void notify_default(bool value) override;
    void on_activate() override;
    [[nodiscard]] DialogResult command_dialog_result() const noexcept override {
        return dialog_result.read();
    }
    void assign_cancel_dialog_result() override;
    Event<DialogResult> dialog_result_changed_;
    // property_codemod begin Button (tools/property_families/button.json; generated, do not edit)
public:
    // Button's property table: ButtonBase's entries, then its own.
    static constexpr std::size_t button_property_count = ButtonBase::button_base_property_count + 3U;
    [[nodiscard]] static const std::array<PropertyEntry, button_property_count>& button_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

private:
    friend struct ButtonVisualStyleChoiceCapability<Button>;
    friend struct ButtonFlatBorderWidthCapability<Button>;
    friend struct ButtonDefaultButtonCapability<Button>;
    friend struct ButtonDialogResultCapability<Button>;
    friend struct ButtonSelectedCapability<Button>;
    friend struct ButtonDangerCapability<Button>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(visual_style)& cell, ButtonVisualStyle& incoming);
    void written(const decltype(visual_style)& cell);
    void prepare_write(const decltype(flat_border_width)& cell, double& incoming);
    void written(const decltype(flat_border_width)& cell);
    void prepare_write(const decltype(default_button)& cell, bool& incoming);
    void written(const decltype(default_button)& cell);
    void prepare_write(const decltype(dialog_result)& cell, DialogResult& incoming);
    void written(const decltype(dialog_result)& cell);
    void prepare_write(const decltype(selected)& cell, bool& incoming);
    void written(const decltype(selected)& cell);
    void prepare_write(const decltype(danger)& cell, bool& incoming);
    void written(const decltype(danger)& cell);
    // property_codemod end Button
};

} // namespace gui_forms
