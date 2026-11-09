#pragma once

#include "gui_forms/controls/panel/text_box/text_box.hpp"
#include "gui_forms/controls/panel/numeric_up_down/numeric_up_down_capabilities/numeric_up_down_capabilities.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace gui_forms {

class NumericUpDown final : public Panel, public NumericUpDownCapabilities<NumericUpDown> {
public:
    static constexpr bool initialize_tree_after_construction = true;
    explicit NumericUpDown(StableId stable_id);
    void initialize_control_tree();

    // Sets both bounds; they must be finite and ordered (std::invalid_argument).
    void set_range(double lowest, double highest);
    [[nodiscard]] std::shared_ptr<TextBox> editor() const noexcept { return editor_; }
    // Focus on the field goes to its edit part (null without one).
    [[nodiscard]] Ptr focus_target() const override { return editor_; }
    [[nodiscard]] Event<double>& value_changed() noexcept { return value_changed_; }

    void arrange(Rect final_bounds) override;
    // The scroll bars, then the spin column's halves as this control's
    // arrow and glyph parts.
    void on_paint_overlay(Painter& painter, Rect local_damage) override;
    void on_key_preview(KeyEvent& event) override;
    void on_pointer_preview(PointerEvent& event) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    bool on_semantic_action(SemanticAction action,
                            std::string_view value_text) override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    [[nodiscard]] AnatomyStateSet anatomy_states() const override;
    // Stage T1 anatomy, as dot members.
    struct Parts final {
        ScrollableControl::Parts scrollable;
        PartGeometry face;
        PartGeometry edge;
        PartGeometry field;
        PartGeometry increment_arrow;
        PartGeometry decrement_arrow;
        PartGeometry increment_glyph;
        PartGeometry decrement_glyph;
    };
    struct States final {
        bool normal{};
        bool pressed{};
        bool captured{};
        bool enabled{};
        bool disabled{};
        bool focused{};
        bool focus_visible{};
        bool invalid{};
        bool pending{};
    };
    [[nodiscard]] Parts parts() const;
    [[nodiscard]] States states() const;

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;

private:
    struct EditorChangeCallback final {
        std::weak_ptr<NumericUpDown> target;
        void operator()(const std::string&) const;
    };
    struct SpinnerStepCallback final {
        std::weak_ptr<NumericUpDown> target;
        void operator()(int direction) const;
    };

    void step(int direction);
    void commit_editor_text();
    void synchronize_editor();
    [[nodiscard]] std::string formatted_value() const;
    // Moves value into [minimum, maximum] when a bound has passed it.
    void clamp_value();

    std::shared_ptr<TextBox> editor_;
    Control::Ptr spinner_;
    SubscriptionToken editor_change_;
    SubscriptionToken spinner_step_;
    bool synchronizing_{};
    // Set while the editor's own text is committed to value, so the write
    // leaves the text as the user typed it.
    bool committing_{};
    Event<double> value_changed_;
    // property_codemod begin NumericUpDown (tools/property_families/numeric_up_down.json; generated, do not edit)
public:
    // NumericUpDown's property table: Panel's entries, then its own.
    static constexpr std::size_t numeric_up_down_property_count = Panel::panel_property_count + 5U;
    [[nodiscard]] static const std::array<PropertyEntry, numeric_up_down_property_count>& numeric_up_down_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

protected:
    // on_<property>_changed runs after a changed write's store and touch and
    // raises the change event; an override calls the base. Only the
    // properties with a Windows Forms On<Property>Changed have one.
    virtual void on_value_changed();

private:
    friend struct NumericUpDownValueCapability<NumericUpDown>;
    friend struct NumericUpDownMinimumCapability<NumericUpDown>;
    friend struct NumericUpDownMaximumCapability<NumericUpDown>;
    friend struct NumericUpDownIncrementCapability<NumericUpDown>;
    friend struct NumericUpDownDecimalPlacesCapability<NumericUpDown>;
    friend struct NumericUpDownHexadecimalCapability<NumericUpDown>;
    friend struct NumericUpDownButtonWidthCapability<NumericUpDown>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(value)& cell, double& incoming);
    void written(const decltype(value)& cell);
    void prepare_write(const decltype(minimum)& cell, double& incoming);
    void written(const decltype(minimum)& cell);
    void prepare_write(const decltype(maximum)& cell, double& incoming);
    void written(const decltype(maximum)& cell);
    void prepare_write(const decltype(increment)& cell, double& incoming);
    void written(const decltype(increment)& cell);
    void prepare_write(const decltype(decimal_places)& cell, std::uint8_t& incoming);
    void written(const decltype(decimal_places)& cell);
    void prepare_write(const decltype(hexadecimal)& cell, bool& incoming);
    void written(const decltype(hexadecimal)& cell);
    void prepare_write(const decltype(button_width)& cell, double& incoming);
    void written(const decltype(button_width)& cell);
    // property_codemod end NumericUpDown
};

} // namespace gui_forms
