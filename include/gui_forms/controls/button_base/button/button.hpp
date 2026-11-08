#pragma once

#include "gui_forms/controls/button_base/button_base.hpp"
#include "gui_forms/controls/button_base/button/button_values/button_values.hpp"
#include "gui_forms/controls/button_base/button/button_capabilities/button_capabilities.hpp"

#include <cstdint>

namespace gui_forms {

class Button : public ButtonBase, public ButtonCapabilities<Button> {
public:
    explicit Button(StableId stable_id, std::string text = {});

    [[nodiscard]] bool default_button() const noexcept { return default_button_; }
    void set_default_button(bool is_default);
    [[nodiscard]] DialogResult dialog_result() const noexcept {
        return dialog_result_;
    }
    void set_dialog_result(DialogResult result);
    [[nodiscard]] Event<DialogResult>& dialog_result_changed() noexcept {
        return dialog_result_changed_;
    }
    [[nodiscard]] bool selected() const noexcept { return selected_; }
    void set_selected(bool selected);
    [[nodiscard]] Event<bool>& selected_changed() noexcept { return selected_changed_; }
    void on_paint(Painter& painter, Rect local_damage) override;
    [[nodiscard]] Insets visual_outsets() const noexcept override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    // Stage T1 anatomy, as dot members (see Control::Parts).
    struct Parts final {
        PartGeometry region;
        PartGeometry face;
        PartGeometry icon;
        PartGeometry label;
        PartGeometry preselect;
        PartGeometry focus;
    };
    struct States final {
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

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;
    // The button's parts computed for its current style.
    [[nodiscard]] Parts button_parts() const;

private:
    Event<bool> selected_changed_{};
    void notify_default(bool value) override;
    void on_activate() override;
    [[nodiscard]] DialogResult command_dialog_result() const noexcept override {
        return dialog_result_;
    }
    void assign_cancel_dialog_result() override;
    bool default_button_{};
    DialogResult dialog_result_{DialogResult::none};
    Event<DialogResult> dialog_result_changed_;
    bool selected_{};
    // property_codemod begin Button (tools/property_families/button.json; generated, do not edit)
public:
    // Button's property table: ButtonBase's entries, then its own.
    static constexpr std::size_t button_property_count = ButtonBase::button_base_property_count + 2U;
    [[nodiscard]] static const std::array<PropertyEntry, button_property_count>& button_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

private:
    friend struct ButtonVisualStyleChoiceCapability<Button>;
    friend struct ButtonFlatBorderWidthCapability<Button>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(visual_style)& cell, ButtonVisualStyle& incoming);
    void written(const decltype(visual_style)& cell);
    void prepare_write(const decltype(flat_border_width)& cell, double& incoming);
    void written(const decltype(flat_border_width)& cell);
    // property_codemod end Button
};

} // namespace gui_forms
