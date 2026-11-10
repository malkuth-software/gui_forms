#include "xsettings_parse.hpp"

#include <cstddef>

namespace gui_forms::host::linux_detail {
namespace {

constexpr std::uint8_t least_significant_first = 0U;
constexpr std::uint8_t most_significant_first = 1U;
constexpr std::uint8_t integer_type = 0U;
constexpr std::uint8_t string_type = 1U;
constexpr std::uint8_t colour_type = 2U;
// The header (byte order, 3 unused, serial, count), and the least a setting
// can hold: its 4-byte head, an empty name, the serial and a 4-byte value.
constexpr std::size_t header_bytes = 12U;
constexpr std::size_t smallest_setting_bytes = 12U;
constexpr std::size_t colour_bytes = 8U;

// A bounds-checked cursor over the property. A read past the end fails and
// leaves the cursor where it was.
struct PropertyCursor final {
  std::span<const std::uint8_t> bytes;
  std::size_t offset{};
  bool most_significant{};
};

std::size_t remaining(const PropertyCursor &cursor) noexcept {
  const std::size_t left = cursor.bytes.size() - cursor.offset;
  return left;
}

bool skip(PropertyCursor &cursor, std::size_t count) noexcept {
  if (count > remaining(cursor))
    return false;
  cursor.offset += count;
  return true;
}

bool read_card16(PropertyCursor &cursor, std::uint16_t &value) noexcept {
  if (remaining(cursor) < 2U)
    return false;
  const std::uint16_t first = cursor.bytes[cursor.offset];
  const std::uint16_t second = cursor.bytes[cursor.offset + 1U];
  value = cursor.most_significant ? static_cast<std::uint16_t>((first << 8U) | second)
                                  : static_cast<std::uint16_t>((second << 8U) | first);
  cursor.offset += 2U;
  return true;
}

bool read_card32(PropertyCursor &cursor, std::uint32_t &value) noexcept {
  if (remaining(cursor) < 4U)
    return false;
  std::uint32_t assembled = 0U;
  for (std::size_t index = 0U; index < 4U; ++index) {
    const std::size_t lane = cursor.most_significant ? index : 3U - index;
    assembled = (assembled << 8U) | cursor.bytes[cursor.offset + lane];
  }
  value = assembled;
  cursor.offset += 4U;
  return true;
}

// The padding that brings `length` to a multiple of 4.
std::size_t padding_of(std::size_t length) noexcept {
  const std::size_t padding = (4U - length % 4U) % 4U;
  return padding;
}

} // namespace

XSettingsInteger find_xsettings_integer(std::span<const std::uint8_t> property,
                                        std::string_view name) noexcept {
  XSettingsInteger malformed{};
  malformed.lookup = XSettingsLookup::malformed;
  if (property.size() < header_bytes)
    return malformed;
  const std::uint8_t order = property[0];
  if (order != least_significant_first && order != most_significant_first)
    return malformed;
  PropertyCursor cursor{};
  cursor.bytes = property;
  cursor.most_significant = order == most_significant_first;
  std::uint32_t serial = 0U;
  std::uint32_t count = 0U;
  if (!skip(cursor, 4U) || !read_card32(cursor, serial) || !read_card32(cursor, count))
    return malformed;
  // A count the remaining bytes cannot hold is a fault, found before the
  // walk (no setting is smaller than smallest_setting_bytes).
  if (static_cast<std::size_t>(count) > remaining(cursor) / smallest_setting_bytes)
    return malformed;
  XSettingsInteger result{};
  for (std::uint32_t setting = 0U; setting < count; ++setting) {
    if (remaining(cursor) < 4U)
      return malformed;
    const std::uint8_t type = cursor.bytes[cursor.offset];
    std::uint16_t name_length = 0U;
    if (!skip(cursor, 2U) || !read_card16(cursor, name_length))
      return malformed;
    const std::size_t name_bytes = name_length;
    if (name_bytes > remaining(cursor))
      return malformed;
    const char *const name_start =
        reinterpret_cast<const char *>(cursor.bytes.data() + cursor.offset);
    const std::string_view setting_name(name_start, name_bytes);
    std::uint32_t last_change = 0U;
    if (!skip(cursor, name_bytes + padding_of(name_bytes)) || !read_card32(cursor, last_change))
      return malformed;
    const bool wanted = setting_name == name;
    switch (type) {
    case integer_type: {
      std::uint32_t raw = 0U;
      if (!read_card32(cursor, raw))
        return malformed;
      if (wanted && result.lookup == XSettingsLookup::absent) {
        result.lookup = XSettingsLookup::found;
        result.value = static_cast<std::int32_t>(raw);
      }
      break;
    }
    case string_type: {
      std::uint32_t length = 0U;
      if (!read_card32(cursor, length))
        return malformed;
      const std::size_t value_bytes = length;
      if (value_bytes > remaining(cursor) || !skip(cursor, value_bytes + padding_of(value_bytes)))
        return malformed;
      if (wanted && result.lookup == XSettingsLookup::absent)
        result.lookup = XSettingsLookup::wrong_type;
      break;
    }
    case colour_type:
      if (!skip(cursor, colour_bytes))
        return malformed;
      if (wanted && result.lookup == XSettingsLookup::absent)
        result.lookup = XSettingsLookup::wrong_type;
      break;
    default:
      // An unknown type has an unknown length: nothing after it can be read.
      return malformed;
    }
  }
  return result;
}

CaretSettings xsettings_caret(XSettingsInteger blink, XSettingsInteger cycle) noexcept {
  CaretSettings caret{};
  if (blink.lookup == XSettingsLookup::found && blink.value == 0) {
    caret.blink_milliseconds = 0U;
    caret.origin = SettingOrigin::host;
    return caret;
  }
  if (cycle.lookup != XSettingsLookup::found || cycle.value <= 0)
    return caret;
  std::uint32_t phase = static_cast<std::uint32_t>(cycle.value) / 2U;
  if (phase == 0U)
    phase = 1U;
  if (phase > maximum_caret_blink_milliseconds)
    phase = maximum_caret_blink_milliseconds;
  caret.blink_milliseconds = phase;
  caret.origin = SettingOrigin::host;
  return caret;
}

} // namespace gui_forms::host::linux_detail
