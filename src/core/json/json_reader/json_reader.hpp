#pragma once

// A bounded JSON reader (RFC 8259), in the pattern of the XML reader
// (stage T3, PLAN.md P10): it reads a theme package's manifest, which is
// untrusted input. The input is UTF-8 without a byte order mark. Every
// bound is stated in JsonLimits, and each, exceeded, gives its own
// JsonError. The reader is iterative: an explicit stack of at most
// maximum_depth containers, so no recursion depth follows the input.
//
// Narrower than RFC 8259, on purpose:
// - an object naming one member twice is refused (duplicate_name), so a
//   manifest has one meaning;
// - a string holding U+0000, written or escaped, is refused
//   (nul_character);
// - an escaped surrogate must be a high one followed by an escaped low
//   one (invalid_unicode_escape);
// - a number keeps its text as written; the reader checks its grammar and
//   length, and its reader converts it.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms::detail {

struct JsonLimits final {
    std::size_t maximum_input_bytes{1U * 1024U * 1024U};
    // The outermost value is at depth 1; a scalar inside one array is at
    // depth 2.
    std::uint32_t maximum_depth{32U};
    // Every value in the document, containers included.
    std::uint32_t maximum_values{65536U};
    // The members of one object or the items of one array.
    std::uint32_t maximum_members{1024U};
    // One string or member name, in bytes once decoded.
    std::size_t maximum_string_bytes{64U * 1024U};
    // One number's text.
    std::size_t maximum_number_bytes{64U};
};

// The largest maximum_depth accepted; a deeper limit is refused with
// JsonError::invalid_limits. Nothing recurses per level: the reader keeps
// one frame per open container in explicit storage, and copying,
// comparing and destroying a value walk it in work lists (JsonValue).
inline constexpr std::uint32_t json_depth_ceiling = 256U;

enum class JsonError : std::uint8_t {
    none,
    invalid_limits,
    input_too_large,
    byte_order_mark,
    invalid_utf8,
    unexpected_end,
    syntax_error,
    invalid_literal,
    invalid_number,
    number_too_long,
    invalid_escape,
    invalid_unicode_escape,
    control_character,
    nul_character,
    string_too_long,
    depth_exceeded,
    value_limit_exceeded,
    member_limit_exceeded,
    duplicate_name,
    content_after_value,
};

[[nodiscard]] std::string_view json_error_name(JsonError error) noexcept;

enum class JsonKind : std::uint8_t { null, boolean, number, string, array, object };

// One value and everything below it. An array's items are in `items`; an
// object's member values are in `items` and their names, at the same
// positions, in `names`, in document order.
//
// A value built in code has no depth limit, so copying, comparing and
// destroying walk it in explicit work storage instead of recursing per
// level (HOUSE_STYLE section 1). Moving moves the item list's storage.
struct JsonValue final {
    JsonKind kind{JsonKind::null};
    bool boolean{};
    // A string's decoded UTF-8, or a number's text as written.
    std::string text{};
    std::vector<JsonValue> items{};
    std::vector<std::string> names{};

    JsonValue() = default;
    // A deep copy, level by level in a work list. On failure (allocation)
    // the partial copy is destroyed and the exception propagates.
    JsonValue(const JsonValue& other);
    JsonValue(JsonValue&& other) noexcept = default;
    // Copies into a new value first, so on failure this value is unchanged.
    JsonValue& operator=(const JsonValue& other);
    JsonValue& operator=(JsonValue&& other) noexcept = default;
    // Tears the items down iteratively (src/core/algorithm/tree_teardown.hpp).
    ~JsonValue();

    // The member named `name` of an object, or null (also for any other
    // kind). The pointer borrows from this value; a temporary is refused.
    [[nodiscard]] const JsonValue* find(std::string_view name) const& noexcept;
    const JsonValue* find(std::string_view name) const&& = delete;
};

// Compares the values level by level in a work list, so it may throw
// std::bad_alloc.
[[nodiscard]] bool operator==(const JsonValue& left, const JsonValue& right);

// Where an error was found: lines and columns count from 1, a column in
// Unicode scalar values (an ill-formed byte counts as one); CR LF, CR and
// LF each end a line.
struct JsonSourcePosition final {
    std::size_t byte_offset{};
    std::uint32_t line{};
    std::uint32_t column{};
};

struct JsonReadResult final {
    JsonError error{JsonError::none};
    // Where the error was found; all zero on success.
    JsonSourcePosition position{};
    // The document's value on success; null on failure.
    JsonValue root{};

    [[nodiscard]] explicit operator bool() const noexcept { return error == JsonError::none; }
};

// Reads one document. The whole input is checked before a result is
// returned; on an error the result holds the error, where it was found and
// a null root. Expected faults never throw; allocation failure propagates
// as std::bad_alloc. What a read retains is bounded by the limits: at
// most maximum_values values, holding at most the input's bytes of text.
[[nodiscard]] JsonReadResult read_json(std::string_view utf8, const JsonLimits& limits = {});

} // namespace gui_forms::detail
