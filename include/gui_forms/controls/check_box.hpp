#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/detail/scalar_binding.hpp"

#include "gui_forms/controls/button_base.hpp"
#include "gui_forms/controls/check_box_values.hpp"
#include "gui_forms/controls/check_box_capabilities.hpp"

#include <cstdint>

namespace gui_forms {

class GUI_FORMS_API CheckBox : public ButtonBase, public CheckBoxCapabilities<CheckBox> {
public:
    explicit CheckBox(StableId stable_id, std::string text = {});
    void bind(Command& command) override;
    void bind(Value<bool>& model);
    void unbind() noexcept;

    [[nodiscard]] Event<CheckState>& check_state_changed() noexcept {
        return check_state_changed_;
    }
    [[nodiscard]] Event<bool>& checked_changed() noexcept {
        return checked_changed_;
    }

    void on_paint(Painter& painter, Rect local_damage) override;
    [[nodiscard]] Insets visual_outsets() const noexcept override;
    void on_activate() override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    // Stage T1 anatomy, as dot members (see Control::Parts).
    struct GUI_FORMS_API Parts final {
        PartGeometry region;
        PartGeometry face;
        PartGeometry well;
        PartGeometry indicator;
        PartGeometry icon;
        PartGeometry label;
        PartGeometry focus;
    };
    struct GUI_FORMS_API States final {
        bool normal{};
        bool hot{};
        bool pressed{};
        bool checked{};
        bool indeterminate{};
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

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;

private:
    // The Value<bool> binding's getter and setter: the checked view's read
    // and write.
    [[nodiscard]] bool bound_checked() const noexcept;
    void write_bound_checked(bool value);

    std::unique_ptr<detail::ScalarBinding<CheckBox, bool>> value_binding_{};
    // The check_state and checked views' storage.
    CheckState check_state_{CheckState::unchecked};
    Event<CheckState> check_state_changed_;
    Event<bool> checked_changed_;
    // property_codemod begin CheckBox (tools/property_families/check_box.json; generated, do not edit)
public:
    // CheckBox's property table: ButtonBase's entries, then its own.
    static constexpr std::size_t check_box_property_count = ButtonBase::button_base_property_count + 4U;
    [[nodiscard]] static const std::array<PropertyEntry, check_box_property_count>& check_box_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

protected:
    // on_<property>_changed runs after a changed write's store and touch and
    // raises the change event; an override calls the base. Only the
    // properties with a Windows Forms On<Property>Changed have one.
    virtual void on_check_state_changed();
    virtual void on_checked_changed();
    virtual void on_appearance_changed();

private:
    friend struct CheckBoxCheckStateCapability<CheckBox>;
    friend struct CheckBoxCheckedCapability<CheckBox>;
    friend struct CheckBoxThreeStateCapability<CheckBox>;
    friend struct CheckBoxAutoCheckCapability<CheckBox>;
    friend struct CheckBoxAppearanceStyleCapability<CheckBox>;
    friend struct CheckBoxIndicatorStyleCapability<CheckBox>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void write_view(const decltype(check_state)& cell, CheckState incoming);
    [[nodiscard]] CheckState read_view(const decltype(check_state)& cell) const;
    void write_view(const decltype(checked)& cell, bool incoming);
    [[nodiscard]] bool read_view(const decltype(checked)& cell) const;
    void prepare_write(const decltype(three_state)& cell, bool& incoming);
    void written(const decltype(three_state)& cell);
    void prepare_write(const decltype(auto_check)& cell, bool& incoming);
    void written(const decltype(auto_check)& cell);
    void prepare_write(const decltype(appearance)& cell, CheckBoxAppearance& incoming);
    void written(const decltype(appearance)& cell);
    void prepare_write(const decltype(indicator_style)& cell, ChoiceIndicatorStyle& incoming);
    void written(const decltype(indicator_style)& cell);
    // property_codemod end CheckBox
};

} // namespace gui_forms
