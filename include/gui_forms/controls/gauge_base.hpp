#pragma once

#include "gui_forms/controls/range_control.hpp"
#include "gui_forms/controls/gauge_base_capabilities.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace gui_forms {

// What the read-only gauges share (W-5b; the instruments package): a value
// in a range (RangeControl's value, minimum and maximum), scale marks every
// tick_frequency, the value shown as text with its units and a fixed number
// of decimals, and optional low and high alarm limits. A gauge whose value
// is below low_alarm or above high_alarm is in alarm: its reading takes the
// invalid state, which every stock theme draws in its warning colours, and
// assistive technology hears the alarm with the value.
//
// A gauge takes no focus, and a press on it does nothing (its tooltip
// still shows). To assistive technology it is a meter
// (a level indicator, a progress bar on hosts without one) with its value,
// range and value text. LinearGauge and RadialGauge draw it.
class GaugeBase : public RangeControl, public GaugeBaseCapabilities<GaugeBase> {
public:
    // Whether the value lies outside the alarm limits that are set.
    [[nodiscard]] bool in_alarm() const noexcept;
    // The value as shown: decimal_places digits after the point, then the
    // units after a space when there are any.
    [[nodiscard]] std::string value_text() const;
    // Where `value` lies in the range, from 0 at the minimum to 1 at the
    // maximum, clamped; 0 for an empty range.
    [[nodiscard]] double ratio_of(double value) const noexcept;
    // Where the scale marks lie, as ratios from 0 to 1: one every
    // tick_frequency from the minimum, and the maximum; none when
    // tick_frequency is zero. At most 101 marks: a finer frequency is drawn
    // as 101 marks across the range.
    [[nodiscard]] std::vector<double> tick_ratios() const;

    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] AnatomyStateSet anatomy_states() const override;

protected:
    explicit GaugeBase(StableId stable_id);

    // The font of the value text: the control's text font, scaled.
    [[nodiscard]] FontSpec value_font(double size) const noexcept;
    // The states a gauge's reading (its trace and indicator) is drawn in:
    // the control's, with invalid while in alarm.
    [[nodiscard]] AnatomyStateSet reading_states() const;
    // property_codemod begin GaugeBase (tools/property_families/gauge_base.json; generated, do not edit)
public:
    // GaugeBase's property table: RangeControl's entries, then its own.
    static constexpr std::size_t gauge_base_property_count = RangeControl::range_control_property_count + 4U;
    [[nodiscard]] static const std::array<PropertyEntry, gauge_base_property_count>& gauge_base_property_entries() noexcept;
    [[nodiscard]] std::span<const PropertyEntry> property_table() const noexcept override;

private:
    friend struct GaugeBaseTickFrequencyCapability<GaugeBase>;
    friend struct GaugeBaseUnitsCapability<GaugeBase>;
    friend struct GaugeBaseDecimalPlacesCapability<GaugeBase>;
    friend struct GaugeBaseShowValueCapability<GaugeBase>;
    friend struct GaugeBaseLowAlarmCapability<GaugeBase>;
    friend struct GaugeBaseHighAlarmCapability<GaugeBase>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(tick_frequency)& cell, double& incoming);
    void written(const decltype(tick_frequency)& cell);
    void prepare_write(const decltype(units)& cell, std::string& incoming);
    void written(const decltype(units)& cell);
    void prepare_write(const decltype(decimal_places)& cell, std::uint32_t& incoming);
    void written(const decltype(decimal_places)& cell);
    void prepare_write(const decltype(show_value)& cell, bool& incoming);
    void written(const decltype(show_value)& cell);
    void prepare_write(const decltype(low_alarm)& cell, std::optional<double>& incoming);
    void written(const decltype(low_alarm)& cell);
    void prepare_write(const decltype(high_alarm)& cell, std::optional<double>& incoming);
    void written(const decltype(high_alarm)& cell);
    // property_codemod end GaugeBase
};

} // namespace gui_forms
