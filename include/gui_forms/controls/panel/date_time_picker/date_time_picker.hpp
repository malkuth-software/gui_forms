#pragma once

#include "gui_forms/controls/panel/date_time_picker/date_time_format_provider/date_time_format_provider.hpp"
#include "gui_forms/controls/panel/panel.hpp"
#include "gui_forms/window.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace gui_forms {

enum class DateTimePickerFormat : std::uint8_t {
    long_date,
    short_date,
    time,
    custom,
};

enum class DateTimeDropDownAlignment : std::uint8_t {
    left,
    right,
};

class DateTimePicker final : public Panel {
public:
    explicit DateTimePicker(StableId stable_id);

    [[nodiscard]] DateTimeValue value() const noexcept { return value_; }
    void set_value(DateTimeValue value);
    [[nodiscard]] DateTimeValue minimum() const noexcept { return minimum_; }
    [[nodiscard]] DateTimeValue maximum() const noexcept { return maximum_; }
    void set_range(DateTimeValue minimum, DateTimeValue maximum);

    [[nodiscard]] DateTimePickerFormat format() const noexcept { return format_; }
    void set_format(DateTimePickerFormat format);
    [[nodiscard]] const std::string& custom_format() const noexcept {
        return custom_format_;
    }
    void set_custom_format(std::string format);
    [[nodiscard]] const DateTimeFormatProvider& format_provider() const noexcept {
        return format_provider_;
    }
    void set_format_provider(DateTimeFormatProvider provider);
    [[nodiscard]] std::string formatted_value() const;

    [[nodiscard]] bool show_check_box() const noexcept { return show_check_box_; }
    void set_show_check_box(bool show);
    [[nodiscard]] bool checked() const noexcept { return checked_; }
    void set_checked(bool checked);
    [[nodiscard]] bool show_up_down() const noexcept { return show_up_down_; }
    void set_show_up_down(bool show);
    [[nodiscard]] bool dropped_down() const noexcept { return dropped_down_; }
    void set_dropped_down(bool dropped_down);
    [[nodiscard]] DateTimeDropDownAlignment drop_down_alignment() const noexcept {
        return drop_down_alignment_;
    }
    void set_drop_down_alignment(DateTimeDropDownAlignment alignment);
    [[nodiscard]] FontSpec font() const noexcept { return font_; }
    void set_font(FontSpec font);
    [[nodiscard]] const BasicControlStyle& style() const noexcept { return style_; }
    void set_style(BasicControlStyle style);

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
                            std::string_view value) override;
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
    void commit_overlay_value(DateTimeValue value);
    void on_overlay_revoked();

    DateTimeValue minimum_{1753, 1U, 1U};
    DateTimeValue maximum_{9998, 12U, 31U, 23U, 59U, 59U, 999U};
    DateTimeValue value_{2000, 1U, 1U};
    DateTimePickerFormat format_{DateTimePickerFormat::long_date};
    DateTimeDropDownAlignment drop_down_alignment_{
        DateTimeDropDownAlignment::left};
    DateTimeFormatProvider format_provider_{
        DateTimeFormatProvider::english_united_states()};
    std::string custom_format_{"MMMM d, yyyy"};
    FontSpec font_{FontRole::content, 12.0, 400, false};
    BasicControlStyle style_;
    // Whether set_style supplied style_: its text then inks the field's text
    // and marks as authored colours (T4-1); otherwise the theme's stand.
    bool style_authored_{};
    HitPart pressed_part_{HitPart::none};
    HitPart activation_part_{HitPart::none};
    bool show_check_box_{};
    bool checked_{true};
    bool show_up_down_{};
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
};

} // namespace gui_forms
