#include "gui_forms/host/types/host_locale/host_locale.hpp"

#include "gui_forms/text/types/text_types.hpp"

#include <sstream>
#include <string_view>

namespace gui_forms {
namespace {

bool valid_text(std::string_view text) {
    return validate_utf8(text).valid();
}

bool valid_named(std::string_view text) {
    return !text.empty() && valid_text(text);
}

template <std::size_t Count>
bool all_named(const std::array<std::string, Count>& names) {
    for (const std::string& name : names) {
        if (!valid_named(name)) return false;
    }
    return true;
}

// `text` as a JSON string body: '"' and '\\' escaped, control characters
// written as \u00XX.
void append_json_text(std::ostringstream& output, std::string_view text) {
    static constexpr char hexadecimal[] = "0123456789abcdef";
    output << '"';
    for (const char character : text) {
        const unsigned char byte = static_cast<unsigned char>(character);
        if (character == '"' || character == '\\') {
            output << '\\' << character;
        } else if (byte < 0x20U) {
            output << "\\u00" << hexadecimal[byte >> 4U] << hexadecimal[byte & 0x0FU];
        } else {
            output << character;
        }
    }
    output << '"';
}

template <std::size_t Count>
void append_json_names(std::ostringstream& output, const std::array<std::string, Count>& names) {
    output << '[';
    for (std::size_t index = 0U; index < Count; ++index) {
        if (index != 0U) output << ',';
        append_json_text(output, names[index]);
    }
    output << ']';
}

} // namespace

HostLocale portable_host_locale() {
    const HostLocale defaults{};
    return defaults;
}

bool valid_host_locale(const HostLocale& locale) {
    const bool names = valid_named(locale.name) && all_named(locale.month_names) &&
                       all_named(locale.abbreviated_month_names) && all_named(locale.day_names) &&
                       all_named(locale.abbreviated_day_names);
    const bool patterns = valid_named(locale.long_date_pattern) && valid_named(locale.short_date_pattern) &&
                          valid_named(locale.time_pattern) && valid_text(locale.am_designator) &&
                          valid_text(locale.pm_designator);
    const bool separators = valid_named(locale.decimal_separator) && valid_text(locale.group_separator) &&
                            locale.decimal_separator != locale.group_separator;
    const bool weekday = static_cast<std::uint8_t>(locale.first_day_of_week) <=
                         static_cast<std::uint8_t>(HostWeekday::saturday);
    return names && patterns && separators && weekday;
}

std::string host_locale_json(const HostLocale& locale) {
    std::ostringstream output;
    output << "{\"name\":";
    append_json_text(output, locale.name);
    output << ",\"month_names\":";
    append_json_names(output, locale.month_names);
    output << ",\"abbreviated_month_names\":";
    append_json_names(output, locale.abbreviated_month_names);
    output << ",\"day_names\":";
    append_json_names(output, locale.day_names);
    output << ",\"abbreviated_day_names\":";
    append_json_names(output, locale.abbreviated_day_names);
    output << ",\"long_date_pattern\":";
    append_json_text(output, locale.long_date_pattern);
    output << ",\"short_date_pattern\":";
    append_json_text(output, locale.short_date_pattern);
    output << ",\"time_pattern\":";
    append_json_text(output, locale.time_pattern);
    output << ",\"am_designator\":";
    append_json_text(output, locale.am_designator);
    output << ",\"pm_designator\":";
    append_json_text(output, locale.pm_designator);
    output << ",\"decimal_separator\":";
    append_json_text(output, locale.decimal_separator);
    output << ",\"group_separator\":";
    append_json_text(output, locale.group_separator);
    output << ",\"first_day_of_week\":" << static_cast<unsigned>(locale.first_day_of_week) << '}';
    return output.str();
}

} // namespace gui_forms
