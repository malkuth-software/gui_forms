#pragma once

#include "gui_forms/controls/scrollable_control/scrollable_control.hpp"
#include "gui_forms/controls/scrollable_control/container_control/container_control_capabilities/container_control_capabilities.hpp"

namespace gui_forms {

class ContainerControl : public ScrollableControl, public ContainerControlCapabilities<ContainerControl> {
public:
    explicit ContainerControl(StableId stable_id);

    [[nodiscard]] bool contains_descendant(const Control::Ptr& control) const noexcept;
    [[nodiscard]] Control::Ptr active_control() const noexcept;
    bool request_active_control(const Control::Ptr& control);
    bool clear_active_control();
    [[nodiscard]] AutoValidate effective_auto_validate() const noexcept;
    [[nodiscard]] Event<AutoValidate>& auto_validate_changed() noexcept {
        return auto_validate_changed_;
    }
    bool validate(bool check_auto_validate = false);
    bool validate_children(
        ValidationConstraints constraints = ValidationConstraints::selectable);
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;

private:
    [[nodiscard]] AutoValidate authored_auto_validate() const noexcept override {
        return auto_validate.read();
    }
    Event<AutoValidate> auto_validate_changed_;
    // property_codemod begin ContainerControl (tools/property_families/container_control.json; generated, do not edit)
private:
    friend struct ContainerControlAutoValidateCapability<ContainerControl>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(auto_validate)& cell, AutoValidate& incoming);
    void written(const decltype(auto_validate)& cell);
    // property_codemod end ContainerControl
};

} // namespace gui_forms
