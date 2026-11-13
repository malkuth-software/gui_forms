#include "gui_forms/controls/gauge_base.hpp"

#include "gui_forms/text.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <ios>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace gui_forms {
namespace {

// The most scale marks a gauge draws.
constexpr std::size_t maximum_ticks = 101U;

} // namespace

GaugeBase::GaugeBase(StableId stable_id) : RangeControl(std::move(stable_id)) {}

bool GaugeBase::in_alarm() const noexcept {
    const double current = value.read();
    const std::optional<double> low = low_alarm.read();
    const std::optional<double> high = high_alarm.read();
    const bool below = low.has_value() && current < *low;
    const bool above = high.has_value() && current > *high;
    return below || above;
}

std::string GaugeBase::value_text() const {
    std::ostringstream text;
    text.imbue(std::locale::classic());
    text << std::fixed << std::setprecision(static_cast<int>(decimal_places.read())) << value.read();
    const std::string& unit = units.read();
    if (!unit.empty()) text << ' ' << unit;
    return text.str();
}

double GaugeBase::ratio_of(double candidate) const noexcept {
    const double span = maximum.read() - minimum.read();
    if (!(span > 0.0)) return 0.0;
    const double ratio = (candidate - minimum.read()) / span;
    const double clamped = std::clamp(ratio, 0.0, 1.0);
    return clamped;
}

std::vector<double> GaugeBase::tick_ratios() const {
    std::vector<double> ratios;
    const double frequency = tick_frequency.read();
    const double span = maximum.read() - minimum.read();
    if (!(frequency > 0.0) || !(span > 0.0)) return ratios;
    const double requested = std::floor(span / frequency);
    // A frequency finer than the most marks allow is drawn as that many
    // marks, evenly; the comparison is made in doubles, before any
    // conversion, so a huge quotient is never converted to an integer.
    const double most_steps = static_cast<double>(maximum_ticks - 1U);
    const bool exact = requested <= most_steps;
    const std::size_t steps = static_cast<std::size_t>(exact ? requested : most_steps);
    const double step_ratio = exact ? frequency / span : 1.0 / most_steps;
    ratios.reserve(steps + 2U);
    for (std::size_t index = 0U; index <= steps; ++index) {
        ratios.push_back(std::min(1.0, step_ratio * static_cast<double>(index)));
    }
    // The maximum always carries a mark, even off the frequency's grid.
    // A mark within a millionth of the maximum is the maximum's.
    if (ratios.back() < 1.0 - 1e-6) {
        ratios.push_back(1.0);
    } else {
        ratios.back() = 1.0;
    }
    return ratios;
}

FontSpec GaugeBase::value_font(double size) const noexcept {
    const FontSpec own{FontRole::content, size, 600, false};
    const FontSpec font = effective_font(own);
    return font;
}

AnatomyStateSet GaugeBase::anatomy_states() const {
    AnatomyStateSet states = base_anatomy_states();
    if (in_alarm()) states.insert(AnatomyState::invalid);
    return states;
}

AnatomyStateSet GaugeBase::reading_states() const {
    AnatomyStateSet states = anatomy_states();
    states.insert(AnatomyState::selected);
    return states;
}

SemanticDescriptor GaugeBase::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.role = SemanticRole::meter;
    descriptor.name = accessible_name.read();
    descriptor.description = accessible_description.read();
    const double current = std::clamp(value.read(), minimum.read(), maximum.read());
    descriptor.numeric_value = current;
    descriptor.minimum_value = minimum.read();
    descriptor.maximum_value = maximum.read();
    std::string shown = value_text();
    if (in_alarm()) {
        const std::optional<double> low = low_alarm.read();
        const bool below = low.has_value() && value.read() < *low;
        shown += below ? " (low alarm)" : " (high alarm)";
        descriptor.states |= SemanticState::invalid;
    }
    descriptor.value = shown;
    descriptor.states |= SemanticState::read_only;
    descriptor.exposed = true;
    return descriptor;
}

// property_codemod begin GaugeBase (tools/property_families/gauge_base.json; generated, do not edit)

namespace {

PropertyDescriptor gauge_base_tick_frequency_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "tick_frequency";
    descriptor.kind = BindingValueKind::number;
    descriptor.category = "Appearance";
    descriptor.description = "Distance in value units between scale marks.";
    descriptor.default_value = BindingValue{10.0};
    descriptor.invalidation_effects = Dirty::paint;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_gauge_base_tick_frequency() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        gauge_base_tick_frequency_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor gauge_base_units_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "units";
    descriptor.kind = BindingValueKind::text;
    descriptor.category = "Appearance";
    descriptor.description = "The unit shown after the value.";
    descriptor.default_value = BindingValue{std::string{}};
    descriptor.invalidation_effects = Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_gauge_base_units() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        gauge_base_units_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor gauge_base_decimal_places_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "decimal_places";
    descriptor.kind = BindingValueKind::unsigned_integer;
    descriptor.category = "Appearance";
    descriptor.description = "Digits after the decimal point in the shown value.";
    descriptor.default_value = BindingValue{std::uint64_t{0U}};
    descriptor.invalidation_effects = Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::semantics;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_gauge_base_decimal_places() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        gauge_base_decimal_places_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

PropertyDescriptor gauge_base_show_value_descriptor() {
    PropertyDescriptor descriptor;
    descriptor.name = "show_value";
    descriptor.kind = BindingValueKind::boolean;
    descriptor.category = "Appearance";
    descriptor.description = "Whether the value is shown as text.";
    descriptor.default_value = BindingValue{true};
    descriptor.invalidation_effects = Dirty::measure | Dirty::arrange | Dirty::paint;
    descriptor.bindable = false;
    descriptor.assignable_member = true;
    return descriptor;
}

const PropertyDescriptor& describe_gauge_base_show_value() {
    static const PropertyDescriptor descriptor = property_table_descriptor(
        gauge_base_show_value_descriptor(), PropertyOperations{true, true, false, false});
    return descriptor;
}

} // namespace

const std::array<PropertyEntry, GaugeBase::gauge_base_property_count>& GaugeBase::gauge_base_property_entries() noexcept {
    static constexpr std::array<PropertyEntry, 4U> own{{
        {"tick_frequency", &describe_gauge_base_tick_frequency,
         &read_cell_entry<GaugeBase, decltype(&GaugeBase::tick_frequency), &GaugeBase::tick_frequency>,
         &write_cell_entry<GaugeBase, decltype(&GaugeBase::tick_frequency), &GaugeBase::tick_frequency>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"units", &describe_gauge_base_units,
         &read_cell_entry<GaugeBase, decltype(&GaugeBase::units), &GaugeBase::units>,
         &write_cell_entry<GaugeBase, decltype(&GaugeBase::units), &GaugeBase::units>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"decimal_places", &describe_gauge_base_decimal_places,
         &read_cell_entry<GaugeBase, decltype(&GaugeBase::decimal_places), &GaugeBase::decimal_places>,
         &write_cell_entry<GaugeBase, decltype(&GaugeBase::decimal_places), &GaugeBase::decimal_places>,
         nullptr, nullptr, nullptr,
         nullptr},
        {"show_value", &describe_gauge_base_show_value,
         &read_cell_entry<GaugeBase, decltype(&GaugeBase::show_value), &GaugeBase::show_value>,
         &write_cell_entry<GaugeBase, decltype(&GaugeBase::show_value), &GaugeBase::show_value>,
         nullptr, nullptr, nullptr,
         nullptr},
    }};
    static_assert(property_names_distinct(own), "GaugeBase's property table names a property twice");
    static const std::array<PropertyEntry, gauge_base_property_count> entries =
        join_property_tables(RangeControl::range_control_property_entries(), own);
    return entries;
}

std::span<const PropertyEntry> GaugeBase::property_table() const noexcept {
    const std::array<PropertyEntry, gauge_base_property_count>& entries = gauge_base_property_entries();
    return std::span<const PropertyEntry>(entries);
}

void GaugeBase::prepare_write(const decltype(tick_frequency)&, double& incoming) {
    require_mutable();
    if (!std::isfinite(incoming) || incoming < 0.0) throw std::invalid_argument("gauge tick frequency must be finite and not negative");
}

void GaugeBase::written(const decltype(tick_frequency)&) {
    invalidate(Dirty::paint);
}

void GaugeBase::prepare_write(const decltype(units)&, std::string& incoming) {
    require_mutable();
    if (!validate_utf8(incoming).valid()) throw std::invalid_argument("gauge units must be valid UTF-8");
}

void GaugeBase::written(const decltype(units)&) {
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::semantics);
}

void GaugeBase::prepare_write(const decltype(decimal_places)&, std::uint32_t& incoming) {
    require_mutable();
    if (incoming > 6U) throw std::invalid_argument("gauge decimal places must be at most 6");
}

void GaugeBase::written(const decltype(decimal_places)&) {
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::semantics);
}

void GaugeBase::prepare_write(const decltype(show_value)&, bool&) {
    require_mutable();
}

void GaugeBase::written(const decltype(show_value)&) {
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint);
}

void GaugeBase::prepare_write(const decltype(low_alarm)&, std::optional<double>& incoming) {
    require_mutable();
    if (incoming && !std::isfinite(*incoming)) throw std::invalid_argument("gauge alarm must be finite");
}

void GaugeBase::written(const decltype(low_alarm)&) {
    invalidate(Dirty::paint | Dirty::semantics);
}

void GaugeBase::prepare_write(const decltype(high_alarm)&, std::optional<double>& incoming) {
    require_mutable();
    if (incoming && !std::isfinite(*incoming)) throw std::invalid_argument("gauge alarm must be finite");
}

void GaugeBase::written(const decltype(high_alarm)&) {
    invalidate(Dirty::paint | Dirty::semantics);
}

// property_codemod end GaugeBase

} // namespace gui_forms
