#pragma once

#include "gui_forms/basic_controls.hpp"
#include "gui_forms/controls/step_indicator_capabilities.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

// A step indicator (the superset's P1 rest; a stepper): a row of numbered
// steps joined by connectors, each with its name beneath. The steps before
// current_step are complete and show a check; the current step is
// highlighted; the steps after it are upcoming. A press on a completed step
// raises step_clicked with its index (a Wizard goes back to it); the
// indicator does not change current_step itself. It takes no focus: the
// keyboard path is the owner's (a Wizard's Back button).
//
// Theme: each step's `chip` is drawn in the selection family, selected for
// the complete and current steps and checked for the complete ones, with
// the `glyph` check or the step's number as its `label`; `divider`
// connectors join the chips (selected between complete steps); `caption`
// is a step's name, ordinary for the current step and secondary for the
// others. To assistive technology it is a group whose value reads "Step 2
// of 4: Details".
class StepIndicator final : public Control, public StepIndicatorCapabilities<StepIndicator> {
public:
    explicit StepIndicator(StableId stable_id);

    // The steps' names, in order; each must be valid UTF-8 (otherwise this
    // throws std::invalid_argument and keeps the old steps). A current step
    // past the new last step moves to the last (to zero when empty).
    void set_steps(std::vector<std::string> steps);
    [[nodiscard]] const std::vector<std::string>& steps() const noexcept { return steps_; }
    // The step whose chip or name contains a local point, if any.
    [[nodiscard]] std::optional<std::size_t> step_at(Point local_point) const;

    // Raised with a completed step's index when it is pressed.
    [[nodiscard]] Event<std::size_t>& step_clicked() noexcept { return step_clicked_; }
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    [[nodiscard]] Event<std::uint32_t>& current_step_changed() noexcept { return current_step_changed_; }

    [[nodiscard]] Size measure(Size available) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    void on_pointer(PointerEvent& event) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    // Stage T1 anatomy, as dot members (see Control::Parts): the chips and
    // connectors of every step, each part spanning all of its instances.
    struct Parts final {
        PartGeometry region;
        PartGeometry chip;
        PartGeometry glyph;
        PartGeometry divider;
        PartGeometry caption;
    };
    [[nodiscard]] Parts parts() const;

protected:
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;

private:
    // One step's chip and name, and the connector after it (empty for the
    // last step), in a control of the given width.
    struct StepPlacement final {
        Rect chip{};
        Rect caption{};
        Rect connector{};
    };
    [[nodiscard]] std::vector<StepPlacement> placements(double width) const;
    [[nodiscard]] FontSpec caption_font() const noexcept;
    [[nodiscard]] FontSpec number_font() const noexcept;

    std::vector<std::string> steps_{};
    Event<std::size_t> step_clicked_;
    Event<std::uint32_t> current_step_changed_;
    // property_codemod begin StepIndicator (tools/property_families/step_indicator.json; generated, do not edit)
public:
    // StepIndicator's property table: Control's entries, then its own.
    static constexpr std::size_t step_indicator_property_count = Control::control_property_count + 1U;
    [[nodiscard]] static const std::array<PropertyEntry, step_indicator_property_count>& step_indicator_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

private:
    friend struct StepIndicatorCurrentStepCapability<StepIndicator>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(current_step)& cell, std::uint32_t& incoming);
    void written(const decltype(current_step)& cell);
    // property_codemod end StepIndicator
};

} // namespace gui_forms
