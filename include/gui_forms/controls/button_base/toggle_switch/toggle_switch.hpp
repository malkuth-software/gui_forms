#pragma once

#include "gui_forms/controls/button_base/button_base.hpp"
#include "gui_forms/controls/button_base/toggle_switch/toggle_switch_capabilities/toggle_switch_capabilities.hpp"

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace gui_forms {

// A switch that is on or off at once (W-4): a rounded track with a thumb
// at its end, the label after it and, when given, the state text after the
// label (on_text or off_text). Unlike a CheckBox it has no third state and
// stands for a setting that takes effect as it is turned.
//
// Activation (a click, Space or the press action) turns `checked` over,
// which raises checked_changed, and then raises clicked and runs the bound
// command, as a CheckBox does. A write to `checked` raises checked_changed
// only.
//
// To assistive technology it is a toggle_switch (hosts without a mapping
// publish a check box), named by its label, checked while on. The thumb
// moves at once; its motion waits for the frame scheduler's reduced-motion
// gate.
class ToggleSwitch final : public ButtonBase, public ToggleSwitchCapabilities<ToggleSwitch> {
public:
    explicit ToggleSwitch(StableId stable_id, std::string initial_text = {});

    // Raised after `checked` changes, with its new value.
    [[nodiscard]] Event<bool>& checked_changed() noexcept { return checked_changed_; }
    // The state text that shows now: on_text while on, off_text while off.
    [[nodiscard]] const std::string& state_text() const noexcept;

    [[nodiscard]] Size measure(Size available) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    [[nodiscard]] Insets visual_outsets() const noexcept override;
    void on_activate() override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    [[nodiscard]] AnatomyStateSet anatomy_states() const override;
    // Stage T1 anatomy, as dot members (see Control::Parts). `track` is the
    // rounded ground, `thumb` the knob at its start (off) or end (on),
    // `label` the text, `state` the state text (absent when empty) and
    // `focus` the cue around the whole control.
    struct Parts final {
        PartGeometry region;
        PartGeometry track;
        PartGeometry thumb;
        PartGeometry label;
        PartGeometry state;
        PartGeometry focus;
    };
    [[nodiscard]] Parts parts() const;

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;

private:
    // Where each part sits in a control of `size`; label and state are
    // empty rectangles when their text is.
    struct Placement final {
        Rect track{};
        Rect thumb{};
        Rect label{};
        Rect state{};
    };
    [[nodiscard]] Placement placement(Size size) const;
    [[nodiscard]] FontSpec label_font() const noexcept;

    Event<bool> checked_changed_;
    // property_codemod begin ToggleSwitch (tools/property_families/toggle_switch.json; generated, do not edit)
public:
    // ToggleSwitch's property table: ButtonBase's entries, then its own.
    static constexpr std::size_t toggle_switch_property_count = ButtonBase::button_base_property_count + 3U;
    [[nodiscard]] static const std::array<PropertyEntry, toggle_switch_property_count>& toggle_switch_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

private:
    friend struct ToggleSwitchCheckedCapability<ToggleSwitch>;
    friend struct ToggleSwitchOnTextCapability<ToggleSwitch>;
    friend struct ToggleSwitchOffTextCapability<ToggleSwitch>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(checked)& cell, bool& incoming);
    void written(const decltype(checked)& cell);
    void prepare_write(const decltype(on_text)& cell, std::string& incoming);
    void written(const decltype(on_text)& cell);
    void prepare_write(const decltype(off_text)& cell, std::string& incoming);
    void written(const decltype(off_text)& cell);
    // property_codemod end ToggleSwitch
};

} // namespace gui_forms
