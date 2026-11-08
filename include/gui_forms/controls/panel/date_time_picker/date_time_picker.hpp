#pragma once

#include "gui_forms/controls/panel/date_time_picker/date_time_format_provider/date_time_format_provider.hpp"
#include "gui_forms/controls/panel/date_time_picker/date_time_picker_values/date_time_picker_values.hpp"
#include "gui_forms/controls/panel/panel.hpp"
#include "gui_forms/window.hpp"
#include "gui_forms/controls/panel/date_time_picker/date_time_picker_capabilities/date_time_picker_capabilities.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace gui_forms {

class DateTimePicker final : public Panel, public DateTimePickerCapabilities<DateTimePicker> {
public:
    // The picker's style property hides Panel::style(), as the method it
    // replaces did; without this the name is ambiguous between the bases.
    using DateTimePickerCapabilities<DateTimePicker>::style;

    explicit DateTimePicker(StableId stable_id);

    void set_range(DateTimeValue earliest, DateTimeValue latest);

    [[nodiscard]] std::string formatted_value() const;

    [[nodiscard]] Event<DateTimeValue>& value_changed() noexcept {
        return value_changed_;
    }
    [[nodiscard]] Event<bool>& checked_changed() noexcept {
        return checked_changed_;
    }
    [[nodiscard]] Event<bool>& drop_down_changed() noexcept {
        return drop_down_changed_;
    }

    void on_paint(Painter& painter, Rect local_damage) override;
    void on_pointer(PointerEvent& event) override;
    void on_key(KeyEvent& event) override;
    void on_focus_changed(bool focused) override;
    void on_activate() override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    bool on_semantic_action(SemanticAction action,
                            std::string_view action_value) override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    [[nodiscard]] AnatomyStateSet anatomy_states() const override;
    // Stage T1 anatomy, as dot members.
    struct Parts final {
        ScrollableControl::Parts scrollable;
        PartGeometry face;
        PartGeometry edge;
        PartGeometry content;
        PartGeometry drop_down;
        PartGeometry disclosure;
        PartGeometry increment_arrow;
        PartGeometry decrement_arrow;
        PartGeometry well;
        PartGeometry indicator;
    };
    struct States final {
        bool normal{};
        bool pressed{};
        bool captured{};
        bool enabled{};
        bool disabled{};
        bool focused{};
        bool focus_visible{};
        bool open{};
        bool closed{};
        bool checked{};
    };
    [[nodiscard]] Parts parts() const;
    [[nodiscard]] States states() const;

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void on_detached_from_window() noexcept override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;

private:
    enum class HitPart : std::uint8_t { none, check, body, arrow_up, arrow_down };

    [[nodiscard]] HitPart part_at(Point absolute) const noexcept;
    void step_days(int days);
    void open_drop_down();
    void close_drop_down();
    void commit_overlay_value(DateTimeValue chosen);
    void apply_range();
    void on_overlay_revoked();

    BasicControlStyle style_;
    // Whether set_style supplied style_: its text then inks the field's text
    // and marks as authored colours (T4-1); otherwise the theme's stand.
    bool style_authored_{};
    HitPart pressed_part_{HitPart::none};
    HitPart activation_part_{HitPart::none};
    bool dropped_down_{};
    bool focused_{};
    bool closing_overlay_{};
    std::shared_ptr<Panel> overlay_layer_;
    Control::Ptr overlay_calendar_;
    OverlayToken overlay_token_;
    std::uint64_t overlay_scope_{};
    SubscriptionToken overlay_commit_;
    SubscriptionToken overlay_cancel_;
    SubscriptionToken overlay_dismiss_;
    SubscriptionToken overlay_revocation_;
    Event<DateTimeValue> value_changed_;
    Event<bool> checked_changed_;
    Event<bool> drop_down_changed_;
    // property_codemod begin DateTimePicker (tools/property_families/date_time_picker.json; generated, do not edit)
private:
    friend struct DateTimePickerValueCapability<DateTimePicker>;
    friend struct DateTimePickerMinimumCapability<DateTimePicker>;
    friend struct DateTimePickerMaximumCapability<DateTimePicker>;
    friend struct DateTimePickerFormatPropertyCapability<DateTimePicker>;
    friend struct DateTimePickerCustomFormatCapability<DateTimePicker>;
    friend struct DateTimePickerFormatProviderCapability<DateTimePicker>;
    friend struct DateTimePickerShowCheckBoxCapability<DateTimePicker>;
    friend struct DateTimePickerCheckedCapability<DateTimePicker>;
    friend struct DateTimePickerShowUpDownCapability<DateTimePicker>;
    friend struct DateTimePickerDroppedDownCapability<DateTimePicker>;
    friend struct DateTimePickerDropDownAlignmentCapability<DateTimePicker>;
    friend struct FontCapability<DateTimePicker>;
    friend struct DateTimePickerStyleCapability<DateTimePicker>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(value)& cell, DateTimeValue& incoming);
    void written(const decltype(value)& cell);
    void prepare_write(const decltype(minimum)& cell, DateTimeValue& incoming);
    void written(const decltype(minimum)& cell);
    void prepare_write(const decltype(maximum)& cell, DateTimeValue& incoming);
    void written(const decltype(maximum)& cell);
    void prepare_write(const decltype(format)& cell, DateTimePickerFormat& incoming);
    void written(const decltype(format)& cell);
    void prepare_write(const decltype(custom_format)& cell, std::string& incoming);
    void written(const decltype(custom_format)& cell);
    void prepare_write(const decltype(format_provider)& cell, DateTimeFormatProvider& incoming);
    void written(const decltype(format_provider)& cell);
    void prepare_write(const decltype(show_check_box)& cell, bool& incoming);
    void written(const decltype(show_check_box)& cell);
    void prepare_write(const decltype(checked)& cell, bool& incoming);
    void written(const decltype(checked)& cell);
    void prepare_write(const decltype(show_up_down)& cell, bool& incoming);
    void written(const decltype(show_up_down)& cell);
    void write_view(const decltype(dropped_down)& cell, bool incoming);
    [[nodiscard]] bool read_view(const decltype(dropped_down)& cell) const;
    void prepare_write(const decltype(drop_down_alignment)& cell, DateTimeDropDownAlignment& incoming);
    void written(const decltype(drop_down_alignment)& cell);
    void prepare_write(const decltype(font)& cell, FontSpec& incoming);
    void written(const decltype(font)& cell);
    void write_view(const decltype(style)& cell, BasicControlStyle incoming);
    [[nodiscard]] BasicControlStyle read_view(const decltype(style)& cell) const;
    // property_codemod end DateTimePicker
};

} // namespace gui_forms
