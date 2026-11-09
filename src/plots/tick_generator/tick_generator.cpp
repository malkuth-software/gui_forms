#include "gui_forms/plots/tick_generator/tick_generator.hpp"

#include "gui_forms/binding/number_format/number_format.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <string>
#include <system_error>

namespace gui_forms::plots {

namespace {

// The fewest device units between minor ticks; finer minors are left out.
constexpr double minimum_minor_spacing = 4.0;
// The narrowest a log decade may be before its 2-9 minors are drawn.
constexpr double minimum_decade_for_minors = 40.0;
// Time spans shorter than this tick as linear seconds.
constexpr double shortest_calendar_span = 2.0;

// The label thinning multipliers' mantissas: 1, 2, 5, 10, 20, 50, ...
constexpr std::array<std::size_t, 3> thinning_mantissas{1U, 2U, 5U};

double power_of_ten(int exponent) noexcept {
    std::array<char, 16> text{};
    text[0] = '1';
    text[1] = 'e';
    const std::to_chars_result written = std::to_chars(text.data() + 2, text.data() + text.size(), exponent);
    double value = 1.0;
    const std::from_chars_result read = std::from_chars(text.data(), written.ptr, value);
    if (read.ec != std::errc{}) return std::numeric_limits<double>::infinity();
    return value;
}

// The exponent of `step` (= mantissa × 10^exponent, mantissa 1, 2 or 5).
int step_exponent(double step) noexcept {
    int exponent = static_cast<int>(std::floor(engine_log10(step)));
    if (power_of_ten(exponent) > step) exponent = exponent - 1;
    if (power_of_ten(exponent + 1) <= step) exponent = exponent + 1;
    return exponent;
}

void set_label(TickLabel& label, std::string_view text) noexcept {
    const std::size_t length = std::min(text.size(), maximum_tick_label);
    std::copy(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(length), label.bytes.begin());
    label.bytes[length] = '\0';
    label.length = static_cast<std::uint8_t>(length);
}

void label_number(TickLabel& label, double value, int decimals, std::string& scratch) {
    scratch.clear();
    NumberFormat format{};
    if (decimals >= 0 && decimals <= 9) {
        format.kind = NumberFormatKind::fixed;
        format.precision = static_cast<std::uint32_t>(decimals);
    } else {
        format.kind = NumberFormatKind::general;
        format.precision = 6U;
    }
    // -0 would print "-0": a tick at zero is zero.
    const double shown = value == 0.0 ? 0.0 : value;
    append_formatted_number(scratch, shown, format);
    set_label(label, scratch);
}

void append_two_digits(std::string& text, std::int64_t value) {
    const std::int64_t clamped = value < 0 ? 0 : value % 100;
    text.push_back(static_cast<char>('0' + clamped / 10));
    text.push_back(static_cast<char>('0' + clamped % 10));
}

void append_integer(std::string& text, std::int64_t value) {
    std::array<char, 24> digits{};
    const std::to_chars_result written = std::to_chars(digits.data(), digits.data() + digits.size(), value);
    text.append(digits.data(), written.ptr);
}

// How a time tick is labelled, by the step's unit.
enum class TimeLabel : std::uint8_t { seconds, minutes, days, months, years };

void label_time(TickLabel& label, std::int64_t seconds, TimeLabel form, std::string& scratch) {
    const CivilTime civil = civil_from_seconds(seconds);
    scratch.clear();
    switch (form) {
    case TimeLabel::seconds:
    case TimeLabel::minutes:
        append_two_digits(scratch, civil.hour);
        scratch.push_back(':');
        append_two_digits(scratch, civil.minute);
        if (form == TimeLabel::seconds) {
            scratch.push_back(':');
            append_two_digits(scratch, civil.second);
        }
        break;
    case TimeLabel::days:
    case TimeLabel::months:
        append_integer(scratch, civil.year);
        scratch.push_back('-');
        append_two_digits(scratch, civil.month);
        if (form == TimeLabel::days) {
            scratch.push_back('-');
            append_two_digits(scratch, civil.day);
        }
        break;
    case TimeLabel::years:
        append_integer(scratch, civil.year);
        break;
    }
    set_label(label, scratch);
}

// Adds one major tick unless the set is full; false when full.
bool add_major(TickSet& out, double value, double device) noexcept {
    if (out.major_count == maximum_major_ticks) return false;
    MajorTick& tick = out.major[out.major_count];
    tick = MajorTick{};
    tick.value = value;
    tick.device = device;
    tick.labelled = true;
    out.major_count = out.major_count + 1U;
    return true;
}

void add_minor(TickSet& out, double device) noexcept {
    if (out.minor_count == maximum_minor_ticks) return;
    out.minor[out.minor_count] = device;
    out.minor_count = out.minor_count + 1U;
}

// Labels every `label_every`-th major counted by `ordinals` (each major's
// integer position along the axis, so zero stays labelled), choosing the
// smallest multiplier from 1, 2, 5, 10, ... at which measured labels do not
// overlap.
void thin_labels(TickSet& out, const TickRequest& request, const TickLabelMetrics* metrics,
                 std::span<const std::int64_t> ordinals) {
    out.label_every = 1U;
    if (metrics == nullptr || out.major_count < 2U) return;
    double widest = 0.0;
    for (std::size_t index = 0U; index < out.major_count; ++index) {
        widest = std::max(widest, (*metrics).label_extent(out.major[index].label.text()));
    }
    // The closest two consecutive majors (calendar months differ in length).
    double spacing = std::numeric_limits<double>::infinity();
    for (std::size_t index = 1U; index < out.major_count; ++index) {
        spacing = std::min(spacing, std::fabs(out.major[index].device - out.major[index - 1U].device));
    }
    const double required = widest + request.label_gap;
    std::size_t every = 1U;
    std::size_t decade = 1U;
    bool found = false;
    while (!found && decade <= 1000000U) {
        for (const std::size_t mantissa : thinning_mantissas) {
            every = mantissa * decade;
            if (spacing * static_cast<double>(every) >= required) {
                found = true;
                break;
            }
        }
        decade = decade * 10U;
    }
    out.label_every = every;
    for (std::size_t index = 0U; index < out.major_count; ++index) {
        const std::int64_t ordinal = ordinals[index];
        const std::int64_t remainder = ordinal % static_cast<std::int64_t>(every);
        out.major[index].labelled = remainder == 0;
    }
}

// Linear ticks over [low, high] in the scale's transformed space, whose
// device position is `mapping.forward(mapping.untransform(t))`.
TickOutcome linear_ticks(const ScaleMapping& mapping, double low, double high, double extent,
                         const TickRequest& request, const TickLabelMetrics* metrics, TickSet& out,
                         std::string& scratch) {
    const double raw = (high - low) * request.minimum_spacing / extent;
    if (!(raw > 0.0) || !std::isfinite(raw)) return TickOutcome::too_small;
    const double step = nice_step(raw);
    const int exponent = step_exponent(step);
    out.step = step;
    const double first = std::ceil(low / step);
    const double last = std::floor(high / step);
    std::array<std::int64_t, maximum_major_ticks> ordinals{};
    TickOutcome outcome = TickOutcome::generated;
    // Integer positions times the step: never accumulated.
    for (double position = first; position <= last; position = position + 1.0) {
        const double transformed = position * step;
        const double value = mapping.untransform(transformed);
        const double device = mapping.forward(value);
        const std::size_t index = out.major_count;
        if (!add_major(out, value, device)) {
            outcome = TickOutcome::truncated;
            break;
        }
        ordinals[index] = static_cast<std::int64_t>(position);
        const int decimals = exponent >= 7 || exponent <= -7 ? -1 : std::max(0, -exponent);
        label_number(out.major[index].label, value, decimals, scratch);
    }
    thin_labels(out, request, metrics, std::span<const std::int64_t>(ordinals.data(), out.major_count));
    // Minors: a step of 2 divides by 4, others by 5.
    const double mantissa = step / power_of_ten(exponent);
    const double divisions = mantissa > 1.5 && mantissa < 2.5 ? 4.0 : 5.0;
    const double minor_step = step / divisions;
    const double minor_device = minor_step * extent / (high - low);
    if (minor_device < minimum_minor_spacing) return outcome;
    const double minor_first = std::ceil(low / minor_step);
    const double minor_last = std::floor(high / minor_step);
    for (double position = minor_first; position <= minor_last; position = position + 1.0) {
        if (std::fmod(position, divisions) == 0.0) continue;
        const double value = mapping.untransform(position * minor_step);
        add_minor(out, mapping.forward(value));
        if (out.minor_count == maximum_minor_ticks) break;
    }
    return outcome;
}

TickOutcome log_ticks(const ScaleMapping& mapping, double extent, const TickRequest& request,
                      const TickLabelMetrics* metrics, TickSet& out, std::string& scratch) {
    const PlotRange domain = mapping.scale().domain;
    const double low = engine_log10(domain.minimum);
    const double high = engine_log10(domain.maximum);
    // Under one decade the decades say too little: linear ticks in data
    // space, placed through the log mapping.
    if (high - low < 1.0) {
        const double raw = domain.extent() * request.minimum_spacing / extent;
        if (!(raw > 0.0)) return TickOutcome::too_small;
        const double step = nice_step(raw);
        const int exponent = step_exponent(step);
        out.step = step;
        std::array<std::int64_t, maximum_major_ticks> ordinals{};
        for (double position = std::ceil(domain.minimum / step); position * step <= domain.maximum;
             position = position + 1.0) {
            const double value = position * step;
            const std::size_t index = out.major_count;
            if (!add_major(out, value, mapping.forward(value))) return TickOutcome::truncated;
            ordinals[index] = static_cast<std::int64_t>(position);
            label_number(out.major[index].label, value, std::max(0, -exponent), scratch);
        }
        thin_labels(out, request, metrics, std::span<const std::int64_t>(ordinals.data(), out.major_count));
        return TickOutcome::generated;
    }
    const double decade_device = extent / (high - low);
    std::int64_t stride = 1;
    while (static_cast<double>(stride) * decade_device < request.minimum_spacing && stride < 1000) {
        stride = stride < 2 ? 2 : (stride < 3 ? 3 : (stride < 5 ? 5 : stride * 2));
    }
    out.step = static_cast<double>(stride);
    std::array<std::int64_t, maximum_major_ticks> ordinals{};
    TickOutcome outcome = TickOutcome::generated;
    const std::int64_t first = static_cast<std::int64_t>(std::ceil(low));
    const std::int64_t last = static_cast<std::int64_t>(std::floor(high));
    for (std::int64_t decade = first; decade <= last; ++decade) {
        if (decade % stride != 0) continue;
        const double value = engine_exp10(static_cast<double>(decade));
        const std::size_t index = out.major_count;
        if (!add_major(out, value, mapping.forward(value))) {
            outcome = TickOutcome::truncated;
            break;
        }
        ordinals[index] = decade / stride;
        if (decade >= -4 && decade <= 4) {
            label_number(out.major[index].label, value, decade < 0 ? static_cast<int>(-decade) : 0, scratch);
        } else {
            scratch.assign("1E");
            append_integer(scratch, decade);
            set_label(out.major[index].label, scratch);
        }
    }
    thin_labels(out, request, metrics, std::span<const std::int64_t>(ordinals.data(), out.major_count));
    if (stride != 1 || decade_device < minimum_decade_for_minors) return outcome;
    for (std::int64_t decade = first - 1; decade <= last; ++decade) {
        const double base = engine_exp10(static_cast<double>(decade));
        for (int multiple = 2; multiple <= 9; ++multiple) {
            const double value = base * static_cast<double>(multiple);
            if (value < domain.minimum || value > domain.maximum) continue;
            add_minor(out, mapping.forward(value));
        }
    }
    return outcome;
}

TickOutcome symlog_ticks(const ScaleMapping& mapping, const TickRequest& request,
                         const TickLabelMetrics* metrics, TickSet& out, std::string& scratch) {
    const PlotRange domain = mapping.scale().domain;
    const double width = mapping.scale().symlog_width;
    const int first = static_cast<int>(std::ceil(engine_log10(width)));
    // Candidates in device order: negative decades outwards-in, zero,
    // positive decades inwards-out; each kept only when far enough from the
    // last kept.
    std::array<double, 2U * 40U + 1U> candidates{};
    std::size_t count = 0U;
    for (int exponent = first + 39; exponent >= first; --exponent) {
        const double value = -engine_exp10(static_cast<double>(exponent));
        if (value < domain.minimum || value > domain.maximum) continue;
        candidates[count] = value;
        count = count + 1U;
    }
    if (domain.contains(0.0)) {
        candidates[count] = 0.0;
        count = count + 1U;
    }
    for (int exponent = first; exponent <= first + 39; ++exponent) {
        const double value = engine_exp10(static_cast<double>(exponent));
        if (value < domain.minimum || value > domain.maximum) continue;
        candidates[count] = value;
        count = count + 1U;
    }
    std::array<std::int64_t, maximum_major_ticks> ordinals{};
    double last_device = std::numeric_limits<double>::quiet_NaN();
    for (std::size_t index = 0U; index < count; ++index) {
        const double device = mapping.forward(candidates[index]);
        if (!std::isnan(last_device) && std::fabs(device - last_device) < request.minimum_spacing) continue;
        const std::size_t slot = out.major_count;
        if (!add_major(out, candidates[index], device)) return TickOutcome::truncated;
        ordinals[slot] = static_cast<std::int64_t>(slot);
        label_number(out.major[slot].label, candidates[index], first < 0 ? -first : 0, scratch);
        last_device = device;
    }
    out.step = 1.0;
    thin_labels(out, request, metrics, std::span<const std::int64_t>(ordinals.data(), out.major_count));
    return TickOutcome::generated;
}

// The calendar steps of fixed length, in seconds.
constexpr std::array<std::int64_t, 20> fixed_time_steps{
    1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600, 7200, 10800, 21600, 43200, 86400, 172800, 604800,
};
constexpr std::array<std::int64_t, 3> month_steps{1, 3, 6};
constexpr std::int64_t average_month_seconds = 2629746;  // 365.2425 days / 12
constexpr std::int64_t average_year_seconds = 31556952;
// Seconds a time axis may reach (about ±31,000 years): beyond it the
// calendar's integers are not trusted.
constexpr double time_limit = 1e12;

TickOutcome time_ticks(const ScaleMapping& mapping, double extent, const TickRequest& request,
                       const TickLabelMetrics* metrics, TickSet& out, std::string& scratch) {
    const PlotRange domain = mapping.scale().domain;
    if (domain.extent() < shortest_calendar_span || std::fabs(domain.minimum) > time_limit ||
        std::fabs(domain.maximum) > time_limit) {
        return linear_ticks(mapping, domain.minimum, domain.maximum, extent, request, metrics, out, scratch);
    }
    const double seconds_device = extent / domain.extent();
    const std::int64_t low = static_cast<std::int64_t>(std::ceil(domain.minimum));
    const std::int64_t high = static_cast<std::int64_t>(std::floor(domain.maximum));
    std::array<std::int64_t, maximum_major_ticks> ordinals{};
    TickOutcome outcome = TickOutcome::generated;
    for (const std::int64_t step : fixed_time_steps) {
        if (static_cast<double>(step) * seconds_device < request.minimum_spacing) continue;
        out.step = static_cast<double>(step);
        const TimeLabel form = step < 60 ? TimeLabel::seconds : (step < 86400 ? TimeLabel::minutes : TimeLabel::days);
        // Multiples of the step from the epoch: days at UTC midnight.
        const std::int64_t first = low >= 0 ? (low + step - 1) / step : -((-low) / step);
        for (std::int64_t position = first; position * step <= high; ++position) {
            const std::int64_t seconds = position * step;
            const std::size_t index = out.major_count;
            if (!add_major(out, static_cast<double>(seconds), mapping.forward(static_cast<double>(seconds)))) {
                outcome = TickOutcome::truncated;
                break;
            }
            ordinals[index] = position;
            label_time(out.major[index].label, seconds, form, scratch);
        }
        thin_labels(out, request, metrics, std::span<const std::int64_t>(ordinals.data(), out.major_count));
        return outcome;
    }
    const CivilTime start = civil_from_seconds(low);
    const CivilTime end = civil_from_seconds(high);
    for (const std::int64_t months : month_steps) {
        if (static_cast<double>(months * average_month_seconds) * seconds_device < request.minimum_spacing) continue;
        out.step = static_cast<double>(months * average_month_seconds);
        const std::int64_t first_index = start.year * 12 + (start.month - 1);
        const std::int64_t last_index = end.year * 12 + (end.month - 1);
        std::int64_t index = first_index - ((first_index % months) + months) % months;
        for (; index <= last_index; index = index + months) {
            CivilTime civil{};
            civil.year = index >= 0 ? index / 12 : -((-index + 11) / 12);
            civil.month = static_cast<std::int32_t>(index - civil.year * 12 + 1);
            const std::int64_t seconds = seconds_from_civil(civil);
            if (seconds < low || seconds > high) continue;
            const std::size_t slot = out.major_count;
            if (!add_major(out, static_cast<double>(seconds), mapping.forward(static_cast<double>(seconds)))) {
                outcome = TickOutcome::truncated;
                break;
            }
            ordinals[slot] = index / months;
            label_time(out.major[slot].label, seconds, TimeLabel::months, scratch);
        }
        thin_labels(out, request, metrics, std::span<const std::int64_t>(ordinals.data(), out.major_count));
        return outcome;
    }
    // Years: 1, 2, 5, 10, 20, 50, ... up to 100,000.
    std::int64_t years = 100000;
    std::int64_t decade = 1;
    bool chosen = false;
    while (!chosen && decade <= 10000) {
        for (const std::size_t mantissa : thinning_mantissas) {
            const std::int64_t candidate = static_cast<std::int64_t>(mantissa) * decade;
            if (static_cast<double>(candidate * average_year_seconds) * seconds_device >= request.minimum_spacing) {
                years = candidate;
                chosen = true;
                break;
            }
        }
        decade = decade * 10;
    }
    out.step = static_cast<double>(years * average_year_seconds);
    std::int64_t year = start.year - ((start.year % years) + years) % years;
    for (; year <= end.year; year = year + years) {
        CivilTime civil{};
        civil.year = year;
        const std::int64_t seconds = seconds_from_civil(civil);
        if (seconds < low || seconds > high) continue;
        const std::size_t slot = out.major_count;
        if (!add_major(out, static_cast<double>(seconds), mapping.forward(static_cast<double>(seconds)))) {
            outcome = TickOutcome::truncated;
            break;
        }
        ordinals[slot] = year / years;
        label_time(out.major[slot].label, seconds, TimeLabel::years, scratch);
    }
    thin_labels(out, request, metrics, std::span<const std::int64_t>(ordinals.data(), out.major_count));
    return outcome;
}

TickOutcome category_ticks(const ScaleMapping& mapping, const TickRequest& request,
                           const TickLabelMetrics* metrics, TickSet& out, std::string& scratch) {
    const PlotRange domain = mapping.scale().domain;
    std::array<std::int64_t, maximum_major_ticks> ordinals{};
    out.step = 1.0;
    const double first = std::max(0.0, std::ceil(domain.minimum));
    for (double position = first; position <= domain.maximum; position = position + 1.0) {
        const std::size_t index = out.major_count;
        if (!add_major(out, position, mapping.forward(position))) {
            thin_labels(out, request, metrics, std::span<const std::int64_t>(ordinals.data(), out.major_count));
            return TickOutcome::truncated;
        }
        const std::size_t category = static_cast<std::size_t>(position);
        ordinals[index] = static_cast<std::int64_t>(category);
        if (category < request.categories.size()) {
            set_label(out.major[index].label, request.categories[category]);
        } else {
            label_number(out.major[index].label, position, 0, scratch);
        }
    }
    thin_labels(out, request, metrics, std::span<const std::int64_t>(ordinals.data(), out.major_count));
    return TickOutcome::generated;
}

} // namespace

double EstimatedLabelMetrics::label_extent(std::string_view label) const {
    const double result = fixed_ + advance_ * static_cast<double>(label.size());
    return result;
}

double nice_step(double raw) noexcept {
    int exponent = static_cast<int>(std::floor(engine_log10(raw)));
    double power = power_of_ten(exponent);
    if (power > raw) {
        exponent = exponent - 1;
        power = power_of_ten(exponent);
    }
    if (power_of_ten(exponent + 1) <= raw) {
        exponent = exponent + 1;
        power = power_of_ten(exponent);
    }
    if (power >= raw) return power;
    if (2.0 * power >= raw) return 2.0 * power;
    if (5.0 * power >= raw) return 5.0 * power;
    const double result = power_of_ten(exponent + 1);
    return result;
}

TickOutcome generate_ticks(const ScaleMapping& mapping, const TickRequest& request,
                           const TickLabelMetrics* metrics, TickSet& out) {
    out.major_count = 0U;
    out.minor_count = 0U;
    out.step = 0.0;
    out.label_every = 1U;
    const Scale& scale = mapping.scale();
    const double extent = std::fabs(scale.device_end - scale.device_start);
    if (extent < 1.0 || !(request.minimum_spacing > 0.0)) return TickOutcome::too_small;
    std::string scratch;
    switch (scale.kind) {
    case ScaleKind::linear:
        return linear_ticks(mapping, scale.domain.minimum, scale.domain.maximum, extent, request, metrics, out,
                            scratch);
    case ScaleKind::log10:
        return log_ticks(mapping, extent, request, metrics, out, scratch);
    case ScaleKind::symlog:
        return symlog_ticks(mapping, request, metrics, out, scratch);
    case ScaleKind::time:
        return time_ticks(mapping, extent, request, metrics, out, scratch);
    case ScaleKind::category:
        return category_ticks(mapping, request, metrics, out, scratch);
    }
    return TickOutcome::too_small;
}

// Days from 1970-01-01 to a civil date and back: Howard Hinnant's
// algorithms (public domain), in 64-bit integers.
CivilTime civil_from_seconds(std::int64_t seconds) noexcept {
    const std::int64_t day_seconds = 86400;
    std::int64_t days = seconds / day_seconds;
    std::int64_t remainder = seconds % day_seconds;
    if (remainder < 0) {
        remainder = remainder + day_seconds;
        days = days - 1;
    }
    const std::int64_t shifted = days + 719468;
    const std::int64_t era = (shifted >= 0 ? shifted : shifted - 146096) / 146097;
    const std::int64_t day_of_era = shifted - era * 146097;
    const std::int64_t year_of_era = (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
    const std::int64_t day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    const std::int64_t month_index = (5 * day_of_year + 2) / 153;
    CivilTime civil{};
    civil.day = static_cast<std::int32_t>(day_of_year - (153 * month_index + 2) / 5 + 1);
    civil.month = static_cast<std::int32_t>(month_index < 10 ? month_index + 3 : month_index - 9);
    civil.year = year_of_era + era * 400 + (civil.month <= 2 ? 1 : 0);
    civil.hour = static_cast<std::int32_t>(remainder / 3600);
    civil.minute = static_cast<std::int32_t>((remainder % 3600) / 60);
    civil.second = static_cast<std::int32_t>(remainder % 60);
    return civil;
}

std::int64_t seconds_from_civil(const CivilTime& time) noexcept {
    const std::int64_t year = time.year - (time.month <= 2 ? 1 : 0);
    const std::int64_t era = (year >= 0 ? year : year - 399) / 400;
    const std::int64_t year_of_era = year - era * 400;
    const std::int64_t month = time.month;
    const std::int64_t day_of_year = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + time.day - 1;
    const std::int64_t day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    const std::int64_t days = era * 146097 + day_of_era - 719468;
    const std::int64_t seconds = days * 86400 + time.hour * 3600 + time.minute * 60 + time.second;
    return seconds;
}

} // namespace gui_forms::plots
