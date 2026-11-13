#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/controls/button_base.hpp"
#include "gui_forms/controls/radio_button_capabilities.hpp"

#include <string>

namespace gui_forms {

class GUI_FORMS_API RadioButton : public ButtonBase, public RadioButtonCapabilities<RadioButton> {
public:
    explicit RadioButton(StableId stable_id, std::string text = {});

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
    void set_checked_without_exclusion(bool checked);
    // Clears the checked sibling that shares this button's group_name.
    void clear_checked_peer();

    Event<bool> checked_changed_;
    // The checked view's storage.
    bool checked_{};
    // property_codemod begin RadioButton (tools/property_families/radio_button.json; generated, do not edit)
public:
    // RadioButton's property table: ButtonBase's entries, then its own.
    static constexpr std::size_t radio_button_property_count = ButtonBase::button_base_property_count + 3U;
    [[nodiscard]] static const std::array<PropertyEntry, radio_button_property_count>& radio_button_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

protected:
    // on_<property>_changed runs after a changed write's store and touch and
    // raises the change event; an override calls the base. Only the
    // properties with a Windows Forms On<Property>Changed have one.
    virtual void on_checked_changed();

private:
    friend struct RadioButtonCheckedCapability<RadioButton>;
    friend struct RadioButtonGroupNameCapability<RadioButton>;
    friend struct RadioButtonAutoCheckCapability<RadioButton>;
    friend struct RadioButtonIndicatorStyleCapability<RadioButton>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void write_view(const decltype(checked)& cell, bool incoming);
    [[nodiscard]] bool read_view(const decltype(checked)& cell) const;
    void prepare_write(const decltype(group_name)& cell, std::string& incoming);
    void written(const decltype(group_name)& cell);
    void prepare_write(const decltype(auto_check)& cell, bool& incoming);
    void written(const decltype(auto_check)& cell);
    void prepare_write(const decltype(indicator_style)& cell, ChoiceIndicatorStyle& incoming);
    void written(const decltype(indicator_style)& cell);
    // property_codemod end RadioButton
};

} // namespace gui_forms
