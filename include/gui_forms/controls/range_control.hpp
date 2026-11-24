#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/detail/scalar_binding.hpp"

#include "gui_forms/animation.hpp"
#include "gui_forms/basic_controls.hpp"
#include "gui_forms/controls/range_control_values.hpp"
#include "gui_forms/controls/range_control_capabilities.hpp"

#include <cstdint>
#include <span>
#include <string_view>

namespace gui_forms {

// ic: gui_forms::RangeControl: generated from include/gui_forms/controls/range_control.ic.toml by
// ic: tools/ic_declarations.py; edit the declaration, not these lines. Thread: ui, unless a call says otherwise.
class GUI_FORMS_API RangeControl : public Control, public RangeControlCapabilities<RangeControl> {
public:
    explicit RangeControl(StableId stable_id);
    void bind(Value<double>& model);
    void unbind() noexcept;

    [[nodiscard]] double normalized_value() const noexcept;
    [[nodiscard]] const BasicControlStyle& style() const noexcept { return style_; }

    // Sets both bounds at once (minimum and maximum are views onto them),
    // clamping the value into the new range.
    void set_range(double minimum_value, double maximum_value);
    void set_style(BasicControlStyle style);
    void increment(double delta);

    [[nodiscard]] Event<double, double>& range_changed() noexcept {
        return range_changed_;
    }
    [[nodiscard]] Event<double>& value_changed() noexcept {
        return value_changed_;
    }
    [[nodiscard]] Event<const RangeScrollEvent&>& scroll() noexcept {
        return scroll_;
    }
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    // Stage T1 anatomy, as dot members (see Control::Parts).
    struct GUI_FORMS_API Parts final {
        PartGeometry region;
        PartGeometry focus;
    };
    struct GUI_FORMS_API States final {
        bool normal{};
        bool enabled{};
        bool disabled{};
        bool focused{};
        bool focus_visible{};
    };
    [[nodiscard]] Parts parts() const;
    [[nodiscard]] States states() const;

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    [[nodiscard]] Rect local_bounds() const noexcept;
    // Whether the value axis runs right to left: a horizontal bar whose
    // resolved direction is right to left (Control::right_to_left), which
    // puts its minimum at the right edge.
    [[nodiscard]] bool mirrored_axis() const noexcept;
    bool set_value_from_input(double requested, RangeAction action);
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;

private:
    friend struct detail::StateFlipAccess;
    void validate_bound_value(double candidate) const;
    // The value binding's getter and setter (ScalarBinding names members).
    [[nodiscard]] double bound_value() const noexcept { return checked_value(); }
    // The stored range read back through its rule (bit-flip run 7, F5).
    // Bounds that break it (not finite, or the minimum not below the
    // maximum) are no write's result and no repair knows which bound is
    // wrong: a defect, which fails fast. A value outside sound bounds (or
    // not a number) is clamped into them, as set_range clamps it, and the
    // next write of that value stores it.
    void require_sound_bounds() const noexcept;
    [[nodiscard]] double checked_value() const noexcept;
    void write_bound_value(double incoming);
    std::unique_ptr<detail::ScalarBinding<RangeControl, double>> value_binding_{};
    double minimum_{};
    double maximum_{100.0};
    double value_{};
    BasicControlStyle style_;
    Event<double, double> range_changed_;
    Event<double> value_changed_;
    Event<const RangeScrollEvent&> scroll_;
    // property_codemod begin RangeControl (tools/property_families/range_control.json; generated, do not edit)
public:
    // RangeControl's property table: Control's entries, then its own.
    static constexpr std::size_t range_control_property_count = Control::control_property_count + 3U;
    [[nodiscard]] static const std::array<PropertyEntry, range_control_property_count>& range_control_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

protected:
    // on_<property>_changed runs after a changed write's store and touch and
    // raises the change event; an override calls the base. Only the
    // properties with a Windows Forms On<Property>Changed have one.
    virtual void on_value_changed();

private:
    friend struct RangeControlValueCapability<RangeControl>;
    friend struct RangeControlMinimumCapability<RangeControl>;
    friend struct RangeControlMaximumCapability<RangeControl>;
    friend struct RangeControlSmallChangeCapability<RangeControl>;
    friend struct RangeControlLargeChangeCapability<RangeControl>;
    friend struct RangeControlOrientationCapability<RangeControl>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void write_view(const decltype(value)& cell, double incoming);
    [[nodiscard]] double read_view(const decltype(value)& cell) const;
    void write_view(const decltype(minimum)& cell, double incoming);
    [[nodiscard]] double read_view(const decltype(minimum)& cell) const;
    void write_view(const decltype(maximum)& cell, double incoming);
    [[nodiscard]] double read_view(const decltype(maximum)& cell) const;
    // ic: Requires finite(incoming) and incoming > 0, else failure invalid_argument, unchanged.
    void prepare_write(const decltype(small_change)& cell, double& incoming);
    void written(const decltype(small_change)& cell);
    // ic: Requires finite(incoming) and incoming > 0, else failure invalid_argument, unchanged.
    void prepare_write(const decltype(large_change)& cell, double& incoming);
    void written(const decltype(large_change)& cell);
    void prepare_write(const decltype(orientation)& cell, Orientation& incoming);
    void written(const decltype(orientation)& cell);
    // property_codemod end RangeControl
};

} // namespace gui_forms
