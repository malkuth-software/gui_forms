// The XSETTINGS property parser behind the X11 host's double-click interval
// (src/host/linux/settings/xsettings_parse.hpp). The parser takes plain
// bytes, so it is tested on every platform with properties built here in
// both byte orders.
#include "../src/host/linux/settings/xsettings_parse.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

using gui_forms::host::linux_detail::XSettingsInteger;
using gui_forms::host::linux_detail::XSettingsLookup;
using gui_forms::host::linux_detail::find_xsettings_integer;
using gui_forms::host::linux_detail::xsettings_caret;
using gui_forms::host::linux_detail::resource_database_dpi;
using gui_forms::host::linux_detail::x11_desktop_scale;
using gui_forms::host::linux_detail::x11_scale_for_dpi;
using gui_forms::host::linux_detail::xsettings_dpi;
using gui_forms::CaretSettings;
using gui_forms::SettingOrigin;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

constexpr std::string_view double_click_name = "Net/DoubleClickTime";

// Builds a property in one byte order.
struct PropertyWriter final {
    std::vector<std::uint8_t> bytes;
    bool most_significant{};
};

void put8(PropertyWriter& writer, std::uint8_t value) {
    writer.bytes.push_back(value);
}

void put16(PropertyWriter& writer, std::uint16_t value) {
    const std::uint8_t high = static_cast<std::uint8_t>(value >> 8U);
    const std::uint8_t low = static_cast<std::uint8_t>(value & 0xffU);
    put8(writer, writer.most_significant ? high : low);
    put8(writer, writer.most_significant ? low : high);
}

void put32(PropertyWriter& writer, std::uint32_t value) {
    for (std::size_t index = 0U; index < 4U; ++index) {
        const std::size_t shift = writer.most_significant ? (3U - index) * 8U : index * 8U;
        put8(writer, static_cast<std::uint8_t>((value >> shift) & 0xffU));
    }
}

void put_padded(PropertyWriter& writer, std::string_view text) {
    for (const char character : text) put8(writer, static_cast<std::uint8_t>(character));
    const std::size_t padding = (4U - text.size() % 4U) % 4U;
    for (std::size_t index = 0U; index < padding; ++index) put8(writer, 0U);
}

void put_header(PropertyWriter& writer, std::uint32_t count) {
    put8(writer, writer.most_significant ? 1U : 0U);
    put8(writer, 0U);
    put8(writer, 0U);
    put8(writer, 0U);
    put32(writer, 7U);
    put32(writer, count);
}

void put_setting_head(PropertyWriter& writer, std::uint8_t type, std::string_view name) {
    put8(writer, type);
    put8(writer, 0U);
    put16(writer, static_cast<std::uint16_t>(name.size()));
    put_padded(writer, name);
    put32(writer, 3U);
}

void put_integer(PropertyWriter& writer, std::string_view name, std::int32_t value) {
    put_setting_head(writer, 0U, name);
    put32(writer, static_cast<std::uint32_t>(value));
}

void put_string(PropertyWriter& writer, std::string_view name, std::string_view value) {
    put_setting_head(writer, 1U, name);
    put32(writer, static_cast<std::uint32_t>(value.size()));
    put_padded(writer, value);
}

void put_colour(PropertyWriter& writer, std::string_view name) {
    put_setting_head(writer, 2U, name);
    put16(writer, 0x1111U);
    put16(writer, 0x2222U);
    put16(writer, 0x3333U);
    put16(writer, 0xffffU);
}

// A property with a string, a colour and the interval, in that order.
std::vector<std::uint8_t> typical_property(bool most_significant, std::int32_t interval) {
    PropertyWriter writer{};
    writer.most_significant = most_significant;
    put_header(writer, 3U);
    put_string(writer, "Net/ThemeName", "Adwaita");
    put_colour(writer, "Gtk/Colour");
    put_integer(writer, double_click_name, interval);
    return writer.bytes;
}

XSettingsInteger look_up(const std::vector<std::uint8_t>& property) {
    const XSettingsInteger found =
        find_xsettings_integer(std::span<const std::uint8_t>(property.data(), property.size()), double_click_name);
    return found;
}

void test_the_interval_is_found_in_both_byte_orders() {
    const XSettingsInteger least = look_up(typical_property(false, 250));
    require(least.lookup == XSettingsLookup::found && least.value == 250,
            "the interval is read least significant byte first");
    const XSettingsInteger most = look_up(typical_property(true, 250));
    require(most.lookup == XSettingsLookup::found && most.value == 250,
            "the interval is read most significant byte first");
    const XSettingsInteger large = look_up(typical_property(true, 0x01020304));
    require(large.lookup == XSettingsLookup::found && large.value == 0x01020304,
            "every byte of the value lands in place");
}

void test_absent_and_wrong_type() {
    PropertyWriter empty{};
    put_header(empty, 0U);
    require(look_up(empty.bytes).lookup == XSettingsLookup::absent, "an empty property has no interval");

    PropertyWriter others{};
    put_header(others, 2U);
    put_integer(others, "Net/CursorBlinkTime", 1200);
    put_string(others, "Net/DoubleClick", "Time");
    require(look_up(others.bytes).lookup == XSettingsLookup::absent,
            "other settings, a name prefix among them, are not the interval");

    PropertyWriter as_string{};
    put_header(as_string, 1U);
    put_string(as_string, double_click_name, "400");
    require(look_up(as_string.bytes).lookup == XSettingsLookup::wrong_type,
            "an interval given as a string is the wrong type");
}

void test_malformed_properties_are_refused() {
    const std::vector<std::uint8_t> whole = typical_property(false, 250);
    for (std::size_t length = 0U; length < whole.size(); ++length) {
        const std::vector<std::uint8_t> cut(whole.begin(), whole.begin() + static_cast<std::ptrdiff_t>(length));
        require(look_up(cut).lookup == XSettingsLookup::malformed, "every truncation is malformed");
    }

    std::vector<std::uint8_t> bad_order = whole;
    bad_order[0] = 2U;
    require(look_up(bad_order).lookup == XSettingsLookup::malformed, "an unknown byte order is malformed");

    PropertyWriter too_many{};
    put_header(too_many, 0xffffffffU);
    put_integer(too_many, double_click_name, 250);
    require(look_up(too_many.bytes).lookup == XSettingsLookup::malformed,
            "a count the bytes cannot hold is malformed, even with the interval present");

    PropertyWriter unknown_type{};
    put_header(unknown_type, 2U);
    put_integer(unknown_type, double_click_name, 250);
    put_setting_head(unknown_type, 3U, "Net/Unknown");
    put32(unknown_type, 0U);
    require(look_up(unknown_type.bytes).lookup == XSettingsLookup::malformed,
            "an unknown type ends the walk as malformed");

    PropertyWriter long_name{};
    put_header(long_name, 1U);
    put8(long_name, 0U);
    put8(long_name, 0U);
    put16(long_name, 0xffffU);
    put_padded(long_name, "Net/DoubleClickTime");
    put32(long_name, 3U);
    put32(long_name, 250U);
    require(look_up(long_name.bytes).lookup == XSettingsLookup::malformed,
            "a name longer than the property is malformed");

    PropertyWriter long_string{};
    put_header(long_string, 2U);
    put_setting_head(long_string, 1U, "Net/ThemeName");
    put32(long_string, 0x7fffffffU);
    put_padded(long_string, "Adwaita");
    put_integer(long_string, double_click_name, 250);
    require(look_up(long_string.bytes).lookup == XSettingsLookup::malformed,
            "a string longer than the property is malformed");
}

void test_the_first_of_two_is_used() {
    PropertyWriter twice{};
    put_header(twice, 2U);
    put_integer(twice, double_click_name, 300);
    put_integer(twice, double_click_name, 900);
    const XSettingsInteger found = look_up(twice.bytes);
    require(found.lookup == XSettingsLookup::found && found.value == 300, "a repeated setting gives its first value");
}

// The caret rule (audit N75): off is a host 0, a cycle is halved and
// bounded, and anything unusable keeps the default.
void test_the_caret_rule() {
    const XSettingsInteger absent{};
    const XSettingsInteger off{XSettingsLookup::found, 0};
    const XSettingsInteger on{XSettingsLookup::found, 1};
    const XSettingsInteger cycle{XSettingsLookup::found, 1200};
    const CaretSettings none = xsettings_caret(absent, absent);
    require(none.blink_milliseconds == 530U && none.origin == SettingOrigin::default_value,
            "without either setting the default stays");
    const CaretSettings stopped = xsettings_caret(off, cycle);
    require(stopped.blink_milliseconds == 0U && stopped.origin == SettingOrigin::host,
            "blinking off is a host phase of 0, whatever the cycle");
    const CaretSettings halved = xsettings_caret(on, cycle);
    require(halved.blink_milliseconds == 600U && halved.origin == SettingOrigin::host, "the cycle is halved");
    const CaretSettings shortest = xsettings_caret(absent, XSettingsInteger{XSettingsLookup::found, 1});
    require(shortest.blink_milliseconds == 1U, "a 1 ms cycle is a 1 ms phase");
    const CaretSettings longest = xsettings_caret(on, XSettingsInteger{XSettingsLookup::found, 100000});
    require(longest.blink_milliseconds == 10000U, "a long cycle is bounded");
    const CaretSettings negative = xsettings_caret(on, XSettingsInteger{XSettingsLookup::found, -5});
    require(negative.origin == SettingOrigin::default_value && negative.blink_milliseconds == 530U,
            "a negative cycle keeps the default");
    const CaretSettings wrong = xsettings_caret(XSettingsInteger{XSettingsLookup::wrong_type, 0}, absent);
    require(wrong.origin == SettingOrigin::default_value, "a mistyped switch is not an off switch");
}

// Xft/DPI (audit N66, option a): the manager's density in 1024ths of a dot
// per inch, read from a property in both byte orders.
void test_the_manager_density() {
    const std::array<bool, 2> orders{false, true};
    for (const bool most_significant : orders) {
        PropertyWriter writer{};
        writer.most_significant = most_significant;
        put_header(writer, 2U);
        put_string(writer, "Gtk/FontName", "Cantarell 11");
        put_integer(writer, "Xft/DPI", 192 * 1024);
        const std::span<const std::uint8_t> bytes(writer.bytes.data(), writer.bytes.size());
        const XSettingsInteger found = find_xsettings_integer(bytes, "Xft/DPI");
        const std::optional<double> dpi = xsettings_dpi(found);
        require(dpi.has_value() && *dpi == 192.0, "Xft/DPI 196608 is 192 dots per inch");
        require(x11_scale_for_dpi(dpi) == 2.0, "192 dots per inch is scale 2");
    }
    require(*xsettings_dpi(XSettingsInteger{XSettingsLookup::found, 98304}) == 96.0, "98304 is 96 dots per inch");
    require(*xsettings_dpi(XSettingsInteger{XSettingsLookup::found, 147456}) == 144.0, "147456 is 144 dots per inch");
    require(*xsettings_dpi(XSettingsInteger{XSettingsLookup::found, 98816}) == 96.5, "a fractional density is kept");
    require(!xsettings_dpi(XSettingsInteger{XSettingsLookup::found, -1}).has_value(), "-1 asks for the default");
    require(!xsettings_dpi(XSettingsInteger{XSettingsLookup::found, 0}).has_value(), "0 is no density");
    require(!xsettings_dpi(XSettingsInteger{XSettingsLookup::found, 9601 * 1024}).has_value(),
            "a density above the bound is no density");
    require(!xsettings_dpi(XSettingsInteger{XSettingsLookup::wrong_type, 196608}).has_value(),
            "Xft/DPI as another type is no density");
    require(!xsettings_dpi(XSettingsInteger{}).has_value(), "an absent Xft/DPI is no density");
}

// Xft.dpi in the resource database, as xrdb writes RESOURCE_MANAGER.
void test_the_resource_database_density() {
    require(*resource_database_dpi("Xft.dpi:\t192\n") == 192.0, "xrdb's tab-separated entry is read");
    require(*resource_database_dpi("Xcursor.size: 48\nXft.antialias: 1\nXft.dpi: 144\nXft.hinting: 1\n") == 144.0,
            "the entry is found among others");
    require(*resource_database_dpi("  Xft.dpi :  120  ") == 120.0, "blanks around the name and value are allowed");
    require(*resource_database_dpi("Xft.dpi: 96.5") == 96.5, "a fraction is read");
    require(*resource_database_dpi("Xft.dpi: 96\nXft.dpi: 192\n") == 192.0, "the last entry counts");
    require(!resource_database_dpi("Xft.dpi: 192\nXft.dpi: big\n").has_value(),
            "an unusable last entry replaces an earlier usable one");
    require(!resource_database_dpi("").has_value(), "an empty database has no density");
    require(!resource_database_dpi("! Xft.dpi: 192\n").has_value(), "a comment line is not an entry");
    require(!resource_database_dpi("Xft.dpiX: 192\n*dpi: 192\nXft*dpi: 192\n").has_value(),
            "only the exact name Xft.dpi is read");
    require(!resource_database_dpi("Xft.dpi: -96\n").has_value(), "a signed value is refused");
    require(!resource_database_dpi("Xft.dpi: 0\n").has_value(), "0 is no density");
    require(!resource_database_dpi("Xft.dpi: 192dpi\n").has_value(), "trailing text is refused");
    require(*resource_database_dpi("Xft.dpi: 100.\n") == 100.0, "a point with no fraction reads as its whole part");
    require(!resource_database_dpi("Xft.dpi: 9601\n").has_value(), "a density above the bound is refused");
    require(!resource_database_dpi("Xft.dpi: 1234567890\n").has_value(), "more than nine digits are refused");
    require(!resource_database_dpi("Xft.dpi 192\n").has_value(), "a line without a colon is not an entry");
}

// The scale rule and the sources' order: the manager first, then the
// resource database, else 1; dpi over 96 within [1, 4].
void test_the_scale_rule() {
    require(x11_scale_for_dpi(std::nullopt) == 1.0, "no density is scale 1");
    require(x11_scale_for_dpi(96.0) == 1.0, "96 dots per inch is scale 1");
    require(x11_scale_for_dpi(120.0) == 1.25, "120 dots per inch is scale 1.25");
    require(x11_scale_for_dpi(144.0) == 1.5, "144 dots per inch is scale 1.5");
    require(x11_scale_for_dpi(192.0) == 2.0, "192 dots per inch is scale 2");
    require(x11_scale_for_dpi(72.0) == 1.0, "a density below 96 is scale 1");
    require(x11_scale_for_dpi(960.0) == 4.0, "a dense desktop is bounded at scale 4");
    require(x11_desktop_scale(192.0, 144.0) == 2.0, "the manager's density comes first");
    require(x11_desktop_scale(std::nullopt, 144.0) == 1.5, "the resource database answers without the manager");
    require(x11_desktop_scale(std::nullopt, std::nullopt) == 1.0, "neither source is scale 1");
}

} // namespace

int main() {
    try {
        test_the_interval_is_found_in_both_byte_orders();
        test_absent_and_wrong_type();
        test_malformed_properties_are_refused();
        test_the_first_of_two_is_used();
        test_the_caret_rule();
        test_the_manager_density();
        test_the_resource_database_density();
        test_the_scale_rule();
        std::cout << "gui_forms_xsettings_parse_tests: all tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_xsettings_parse_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
