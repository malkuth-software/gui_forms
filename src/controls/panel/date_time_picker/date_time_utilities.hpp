#pragma once

#include "gui_forms/controls/date_time_format_provider.hpp"
#include "gui_forms/events.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace gui_forms::date_time_detail {

[[nodiscard]] std::int64_t days_from_civil(
    std::int64_t year, unsigned month, unsigned day) noexcept;
[[nodiscard]] DateTimeValue civil_from_days(
    std::int64_t serial, DateTimeValue time) noexcept;
[[nodiscard]] bool includes(Modifier value, Modifier requested) noexcept;
[[nodiscard]] bool civil_in_range(DateTimeValue value,
                                  DateTimeValue minimum,
                                  DateTimeValue maximum) noexcept;
[[nodiscard]] std::string padded(unsigned value, unsigned digits);
[[nodiscard]] std::string format_date_time(
    DateTimeValue value, std::string_view pattern,
    const DateTimeFormatProvider& provider);
void validate_provider(const DateTimeFormatProvider& provider);
[[nodiscard]] bool parse_iso_date(
    std::string_view text, DateTimeValue& value);

} // namespace gui_forms::date_time_detail
