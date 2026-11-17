#include "macos_locale.hpp"

#include "../../../host/macos/locale/icu_date_pattern.hpp"

#include <CoreFoundation/CoreFoundation.h>

#include <array>
#include <cstddef>
#include <string>

namespace gui_forms::host {
namespace {

// One CoreFoundation object this unit created or copied (the Create and Copy
// rules), released on destruction. Objects obtained by a Get function are
// borrowed and never held here.
class OwnedCfObject final {
public:
    explicit OwnedCfObject(CFTypeRef object) noexcept : object_(object) {}
    OwnedCfObject(const OwnedCfObject&) = delete;
    OwnedCfObject& operator=(const OwnedCfObject&) = delete;
    ~OwnedCfObject() {
        if (object_ != nullptr) CFRelease(object_);
    }
    [[nodiscard]] CFTypeRef get() const noexcept { return object_; }

private:
    CFTypeRef object_;
};

// `text` as UTF-8; empty for a null or unconvertible string.
std::string utf8_text(CFStringRef text) {
    std::string result;
    if (text == nullptr) return result;
    const CFIndex length = CFStringGetLength(text);
    const CFIndex capacity = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8);
    if (capacity == kCFNotFound || capacity < 0) return result;
    // One byte more for the terminator CFStringGetCString writes.
    result.resize(static_cast<std::size_t>(capacity) + 1U);
    if (!CFStringGetCString(text, result.data(), capacity + 1, kCFStringEncodingUTF8)) {
        result.clear();
        return result;
    }
    result.resize(std::char_traits<char>::length(result.c_str()));
    return result;
}

// The formatter's pattern converted, or `fallback` when nothing converts.
std::string formatter_pattern(CFLocaleRef locale, CFDateFormatterStyle date_style, CFDateFormatterStyle time_style,
                              const std::string& fallback) {
    const OwnedCfObject formatter(CFDateFormatterCreate(kCFAllocatorDefault, locale, date_style, time_style));
    if (formatter.get() == nullptr) return fallback;
    // Get rule: the format belongs to the formatter.
    const CFStringRef format =
        CFDateFormatterGetFormat(static_cast<CFDateFormatterRef>(const_cast<void*>(formatter.get())));
    const std::string converted = picker_pattern_from_icu(utf8_text(format));
    return converted.empty() ? fallback : converted;
}

// Fills `names` from the formatter's array property `key` when it holds
// exactly as many non-empty strings; otherwise leaves them.
template <std::size_t Count>
void fill_symbols(CFDateFormatterRef formatter, CFStringRef key, std::array<std::string, Count>& names) {
    const OwnedCfObject property(CFDateFormatterCopyProperty(formatter, key));
    if (property.get() == nullptr || CFGetTypeID(property.get()) != CFArrayGetTypeID()) return;
    const CFArrayRef symbols = static_cast<CFArrayRef>(property.get());
    if (CFArrayGetCount(symbols) != static_cast<CFIndex>(Count)) return;
    std::array<std::string, Count> read{};
    for (std::size_t index = 0U; index < Count; ++index) {
        const void* value = CFArrayGetValueAtIndex(symbols, static_cast<CFIndex>(index));
        if (value == nullptr || CFGetTypeID(value) != CFStringGetTypeID()) return;
        read[index] = utf8_text(static_cast<CFStringRef>(value));
        if (read[index].empty()) return;
    }
    names = read;
}

void fill_symbol(CFDateFormatterRef formatter, CFStringRef key, std::string& symbol) {
    const OwnedCfObject property(CFDateFormatterCopyProperty(formatter, key));
    if (property.get() == nullptr || CFGetTypeID(property.get()) != CFStringGetTypeID()) return;
    symbol = utf8_text(static_cast<CFStringRef>(property.get()));
}

// A CFLocale string value (Get rule), or `fallback` when absent.
std::string locale_text(CFLocaleRef locale, CFStringRef key, const std::string& fallback) {
    const CFTypeRef value = CFLocaleGetValue(locale, key);
    if (value == nullptr || CFGetTypeID(value) != CFStringGetTypeID()) return fallback;
    const std::string text = utf8_text(static_cast<CFStringRef>(value));
    return text;
}

// "en_GB@rg=dezzzz" as "en-GB": the modifiers dropped, '_' as '-'.
std::string bcp47_name(const std::string& identifier) {
    std::string name = identifier.substr(0U, identifier.find('@'));
    for (char& character : name) {
        if (character == '_') character = '-';
    }
    return name;
}

} // namespace

std::optional<HostLocale> read_macos_locale() {
    const OwnedCfObject owned_locale(CFLocaleCopyCurrent());
    if (owned_locale.get() == nullptr) return std::nullopt;
    const CFLocaleRef locale = static_cast<CFLocaleRef>(owned_locale.get());
    HostLocale record = portable_host_locale();
    const std::string name = bcp47_name(utf8_text(CFLocaleGetIdentifier(locale)));
    if (!name.empty()) record.name = name;

    record.long_date_pattern =
        formatter_pattern(locale, kCFDateFormatterFullStyle, kCFDateFormatterNoStyle, record.long_date_pattern);
    record.short_date_pattern =
        formatter_pattern(locale, kCFDateFormatterShortStyle, kCFDateFormatterNoStyle, record.short_date_pattern);
    record.time_pattern =
        formatter_pattern(locale, kCFDateFormatterNoStyle, kCFDateFormatterShortStyle, record.time_pattern);

    const OwnedCfObject owned_formatter(
        CFDateFormatterCreate(kCFAllocatorDefault, locale, kCFDateFormatterNoStyle, kCFDateFormatterNoStyle));
    if (owned_formatter.get() != nullptr) {
        const CFDateFormatterRef formatter = static_cast<CFDateFormatterRef>(const_cast<void*>(owned_formatter.get()));
        fill_symbols(formatter, kCFDateFormatterMonthSymbols, record.month_names);
        fill_symbols(formatter, kCFDateFormatterShortMonthSymbols, record.abbreviated_month_names);
        fill_symbols(formatter, kCFDateFormatterWeekdaySymbols, record.day_names);
        fill_symbols(formatter, kCFDateFormatterShortWeekdaySymbols, record.abbreviated_day_names);
        fill_symbol(formatter, kCFDateFormatterAMSymbol, record.am_designator);
        fill_symbol(formatter, kCFDateFormatterPMSymbol, record.pm_designator);
    }

    record.decimal_separator = locale_text(locale, kCFLocaleDecimalSeparator, record.decimal_separator);
    record.group_separator = locale_text(locale, kCFLocaleGroupingSeparator, record.group_separator);

    const OwnedCfObject owned_calendar(CFCalendarCopyCurrent());
    if (owned_calendar.get() != nullptr) {
        // CFCalendarGetFirstWeekday counts Sunday as 1.
        const CFIndex weekday =
            CFCalendarGetFirstWeekday(static_cast<CFCalendarRef>(const_cast<void*>(owned_calendar.get())));
        if (weekday >= 1 && weekday <= 7) record.first_day_of_week = static_cast<HostWeekday>(weekday - 1);
    }

    if (!valid_host_locale(record)) return std::nullopt;
    return record;
}

} // namespace gui_forms::host
