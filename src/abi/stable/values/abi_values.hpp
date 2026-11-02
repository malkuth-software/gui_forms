#pragma once

// gf_value conversions shared by the property and event tables: the record a
// BindingValue is described by, and the BindingValue a caller's record holds.

#include "gui_forms/c/gf_abi.h"

#include "../failure/abi_failure.hpp"

#include "gui_forms/binding.hpp"

#include <cstdint>
#include <optional>
#include <string_view>

namespace gui_forms::abi1 {

// The 1.0 size of gf_value: a caller passes at least this much.
inline constexpr std::uint32_t value_size_1_0 = 200U;
static_assert(sizeof(gf_value) == value_size_1_0);

// A colour as 0xRRGGBBAA, and back.
[[nodiscard]] std::uint32_t packed_color(Color color) noexcept;
[[nodiscard]] Color unpacked_color(std::uint32_t packed) noexcept;
[[nodiscard]] bool finite_all(double first, double second, double third, double fourth) noexcept;

// Sets record's kind and the field that kind selects from held; text
// receives held's text (borrowed from held, or the static name of an
// enumeration choice), which the caller places. A collection gives its
// count; its elements are read one at a time. False, with record unchanged,
// for a kind that has no gf_value form yet (an object value).
[[nodiscard]] bool record_binding_value(const BindingValue& held, gf_value& record, std::string_view& text);

// The BindingValue a caller's gf_value holds; text is copied. A COLLECTION
// becomes a collection whose item kind is its first element's (text when it
// has none) and whose item type name is "value"; a property gives it its own
// shape (shape_collection). Fails with the slot's status for a short record,
// a malformed value or a kind with no form.
[[nodiscard]] gf_status read_value(const gf_value* value, SlotIdentity slot, BindingValue& converted);

// converted, a collection read_value made, rebuilt with the item type and
// kind of shape when shape is a collection; otherwise unchanged. Throws
// std::invalid_argument when an item does not convert to shape's kind.
void shape_collection(BindingValue& converted, const std::optional<BindingValue>& shape);

// Checks a caller's output record and text buffer, as properties.get takes
// them: a gf_value of at least the 1.0 size, a text size output and a
// buffer unless its capacity is zero.
[[nodiscard]] gf_status check_value_output(const gf_value* value, const char* text_buffer,
                                           std::uint64_t text_capacity, const std::uint64_t* text_required,
                                           SlotIdentity slot);
// Writes held into the caller's record (its 1.0 fields after struct_size)
// and its text into text_buffer; text_required receives the text's size. A
// short buffer gives GF_STATUS_BUFFER_TOO_SMALL with the record unwritten.
[[nodiscard]] gf_status write_value_output(const BindingValue& held, gf_value* value, char* text_buffer,
                                           std::uint64_t text_capacity, std::uint64_t* text_required,
                                           SlotIdentity slot);

} // namespace gui_forms::abi1
