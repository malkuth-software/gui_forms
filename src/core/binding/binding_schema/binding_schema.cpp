// The binding model's schema, value kinds and key order. See
// binding_schema.hpp and binding_key_order.hpp.

#include "gui_forms/binding/binding_schema/binding_schema.hpp"

#include "binding_key_order.hpp"

#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

namespace gui_forms {
namespace {

// Whether name is blank: canonical_binding_name refuses those.
bool blank_name(std::string_view name) noexcept {
    const bool blank = name.find_first_not_of(" \t\r\n") == std::string_view::npos;
    return blank;
}

} // namespace

BindingSchema::BindingSchema(std::vector<BindingColumn> columns) {
    if (columns.size() > binding_maximum_columns) {
        throw std::invalid_argument("GUI.Forms binding schema has more than binding_maximum_columns columns");
    }
    columns_.reserve(columns.size());
    for (BindingColumn& column : columns) {
        static_cast<void>(append(std::move(column)));
    }
}

std::optional<std::size_t> BindingSchema::find(std::string_view name) const {
    if (blank_name(name)) {
        return std::nullopt;
    }
    const std::string canonical = canonical_binding_name(name);
    for (std::size_t index = 0U; index < columns_.size(); ++index) {
        if (columns_[index].name == canonical) {
            return index;
        }
    }
    return std::nullopt;
}

std::size_t BindingSchema::append(BindingColumn column) {
    if (blank_name(column.name)) {
        throw std::invalid_argument("GUI.Forms binding column name may not be empty");
    }
    if (columns_.size() >= binding_maximum_columns) {
        throw std::invalid_argument("GUI.Forms binding schema has more than binding_maximum_columns columns");
    }
    column.name = canonical_binding_name(column.name);
    if (find(column.name)) {
        throw std::invalid_argument("GUI.Forms binding schema repeats the column " + column.name);
    }
    columns_.push_back(std::move(column));
    const std::size_t index = columns_.size() - 1U;
    return index;
}

bool BindingSchema::admits(std::size_t column, const BindingValue& value) const noexcept {
    if (column >= columns_.size()) {
        return false;
    }
    const BindingValueKind kind = binding_value_kind(value);
    const BindingValueKind declared = columns_[column].kind;
    const bool admitted = kind == BindingValueKind::null || declared == BindingValueKind::null || kind == declared;
    return admitted;
}

int compare_binding_keys(const BindingValue& left, const BindingValue& right) noexcept {
    const detail::BindingKeyView left_view = detail::binding_key_view(left);
    const detail::BindingKeyView right_view = detail::binding_key_view(right);
    const int order = detail::compare_binding_key_views(left_view, right_view);
    return order;
}

bool binding_predicate_holds(const BindingPredicate& predicate, const BindingValue& value) noexcept {
    const BindingValueKind kind = binding_value_kind(value);
    switch (predicate.op) {
    case BindingPredicateOperator::is_null:
        return kind == BindingValueKind::null;
    case BindingPredicateOperator::is_not_null:
        return kind != BindingValueKind::null;
    case BindingPredicateOperator::contains:
    case BindingPredicateOperator::starts_with: {
        const std::string* text = std::get_if<std::string>(&value);
        const std::string* part = std::get_if<std::string>(&predicate.operand);
        if (text == nullptr || part == nullptr) {
            return false;
        }
        const bool holds = predicate.op == BindingPredicateOperator::contains
                               ? detail::text_contains_ignoring_case(*text, *part)
                               : detail::text_starts_with_ignoring_case(*text, *part);
        return holds;
    }
    default:
        break;
    }
    const int order = compare_binding_keys(value, predicate.operand);
    switch (predicate.op) {
    case BindingPredicateOperator::equal:
        return order == 0;
    case BindingPredicateOperator::not_equal:
        return order != 0;
    case BindingPredicateOperator::less:
        return order < 0;
    case BindingPredicateOperator::less_or_equal:
        return order <= 0;
    case BindingPredicateOperator::greater:
        return order > 0;
    case BindingPredicateOperator::greater_or_equal:
        return order >= 0;
    default:
        return false;
    }
}

namespace detail {
namespace {

// The families keys order by first: null, booleans, numbers, text, then
// every other kind by its BindingValueKind.
enum class KeyFamily : std::uint8_t {
    null,
    boolean,
    number,
    text,
    other,
};

KeyFamily family_of(BindingValueKind kind) noexcept {
    switch (kind) {
    case BindingValueKind::null:
        return KeyFamily::null;
    case BindingValueKind::boolean:
        return KeyFamily::boolean;
    case BindingValueKind::signed_integer:
    case BindingValueKind::unsigned_integer:
    case BindingValueKind::number:
        return KeyFamily::number;
    case BindingValueKind::text:
        return KeyFamily::text;
    default:
        return KeyFamily::other;
    }
}

template <typename Value>
int three_way(const Value& left, const Value& right) noexcept {
    if (left < right) {
        return -1;
    }
    if (right < left) {
        return 1;
    }
    return 0;
}

// A number's value as a long double: exact for every 64-bit integer where
// long double has a 64-bit significand (x87), and the nearest otherwise.
long double wide_value(const BindingKeyView& key) noexcept {
    if (key.kind == BindingValueKind::signed_integer) {
        return static_cast<long double>(key.signed_value);
    }
    if (key.kind == BindingValueKind::unsigned_integer) {
        return static_cast<long double>(key.unsigned_value);
    }
    return static_cast<long double>(key.number);
}

int compare_numbers(const BindingKeyView& left, const BindingKeyView& right) noexcept {
    const bool left_integer = left.kind != BindingValueKind::number;
    const bool right_integer = right.kind != BindingValueKind::number;
    if (left_integer && right_integer) {
        // Exact across signedness.
        const bool left_negative = left.kind == BindingValueKind::signed_integer && left.signed_value < 0;
        const bool right_negative = right.kind == BindingValueKind::signed_integer && right.signed_value < 0;
        if (left_negative != right_negative) {
            return left_negative ? -1 : 1;
        }
        if (left_negative) {
            return three_way(left.signed_value, right.signed_value);
        }
        const std::uint64_t left_magnitude = left.kind == BindingValueKind::signed_integer
                                                 ? static_cast<std::uint64_t>(left.signed_value)
                                                 : left.unsigned_value;
        const std::uint64_t right_magnitude = right.kind == BindingValueKind::signed_integer
                                                  ? static_cast<std::uint64_t>(right.signed_value)
                                                  : right.unsigned_value;
        return three_way(left_magnitude, right_magnitude);
    }
    // A NaN orders after every number, and equal to another NaN.
    const bool left_nan = !left_integer && left.number != left.number;
    const bool right_nan = !right_integer && right.number != right.number;
    if (left_nan || right_nan) {
        return three_way(left_nan, right_nan);
    }
    return three_way(wide_value(left), wide_value(right));
}

// A code point of text at offset, advancing offset. A byte that does not
// start a well-formed sequence is returned as 0x110000 plus its value.
char32_t next_code_point(std::string_view text, std::size_t& offset) noexcept {
    const unsigned char lead = static_cast<unsigned char>(text[offset]);
    const char32_t invalid = static_cast<char32_t>(0x110000U + lead);
    std::size_t length = 0U;
    char32_t value = 0U;
    if (lead < 0x80U) {
        offset = offset + 1U;
        return static_cast<char32_t>(lead);
    }
    if (lead >= 0xC2U && lead <= 0xDFU) {
        length = 2U;
        value = static_cast<char32_t>(lead & 0x1FU);
    } else if (lead >= 0xE0U && lead <= 0xEFU) {
        length = 3U;
        value = static_cast<char32_t>(lead & 0x0FU);
    } else if (lead >= 0xF0U && lead <= 0xF4U) {
        length = 4U;
        value = static_cast<char32_t>(lead & 0x07U);
    } else {
        offset = offset + 1U;
        return invalid;
    }
    if (text.size() - offset < length) {
        offset = offset + 1U;
        return invalid;
    }
    for (std::size_t index = 1U; index < length; ++index) {
        const unsigned char continuation = static_cast<unsigned char>(text[offset + index]);
        if ((continuation & 0xC0U) != 0x80U) {
            offset = offset + 1U;
            return invalid;
        }
        value = (value << 6U) | static_cast<char32_t>(continuation & 0x3FU);
    }
    // Overlong forms, surrogates and values past U+10FFFF are not well formed.
    const bool overlong = (length == 3U && value < 0x800U) || (length == 4U && value < 0x10000U);
    const bool surrogate = value >= 0xD800U && value <= 0xDFFFU;
    if (overlong || surrogate || value > 0x10FFFFU) {
        offset = offset + 1U;
        return invalid;
    }
    offset = offset + length;
    return value;
}

char32_t fold_code_point(char32_t value) noexcept {
    if (value >= U'A' && value <= U'Z') {
        return value + 0x20U;
    }
    if (value >= 0xC0U && value <= 0xDEU && value != 0xD7U) {
        return value + 0x20U;
    }
    if (value >= 0x391U && value <= 0x3A9U && value != 0x3A2U) {
        return value + 0x20U;
    }
    if (value >= 0x410U && value <= 0x42FU) {
        return value + 0x20U;
    }
    if (value >= 0x400U && value <= 0x40FU) {
        return value + 0x50U;
    }
    return value;
}

// Whether text from offset starts with part under folding.
bool folded_prefix_at(std::string_view text, std::size_t offset, std::string_view part) noexcept {
    std::size_t text_offset = offset;
    std::size_t part_offset = 0U;
    while (part_offset < part.size()) {
        if (text_offset >= text.size()) {
            return false;
        }
        const char32_t text_point = fold_code_point(next_code_point(text, text_offset));
        const char32_t part_point = fold_code_point(next_code_point(part, part_offset));
        if (text_point != part_point) {
            return false;
        }
    }
    return true;
}

} // namespace

BindingKeyView binding_key_view(const BindingValue& value) noexcept {
    BindingKeyView view;
    view.kind = binding_value_kind(value);
    if (const bool* boolean = std::get_if<bool>(&value)) {
        view.boolean = *boolean;
    } else if (const std::int64_t* signed_value = std::get_if<std::int64_t>(&value)) {
        view.signed_value = *signed_value;
    } else if (const std::uint64_t* unsigned_value = std::get_if<std::uint64_t>(&value)) {
        view.unsigned_value = *unsigned_value;
    } else if (const double* number = std::get_if<double>(&value)) {
        view.number = *number;
    } else if (const std::string* text = std::get_if<std::string>(&value)) {
        view.text = *text;
    }
    return view;
}

int compare_binding_key_views(const BindingKeyView& left, const BindingKeyView& right) noexcept {
    const KeyFamily left_family = family_of(left.kind);
    const KeyFamily right_family = family_of(right.kind);
    if (left_family != right_family) {
        return three_way(static_cast<int>(left_family), static_cast<int>(right_family));
    }
    switch (left_family) {
    case KeyFamily::null:
        return 0;
    case KeyFamily::boolean:
        return three_way(left.boolean, right.boolean);
    case KeyFamily::number:
        return compare_numbers(left, right);
    case KeyFamily::text:
        return compare_text_ignoring_case(left.text, right.text);
    default:
        return three_way(static_cast<int>(left.kind), static_cast<int>(right.kind));
    }
}

int compare_text_ignoring_case(std::string_view left, std::string_view right) noexcept {
    std::size_t left_offset = 0U;
    std::size_t right_offset = 0U;
    while (left_offset < left.size() && right_offset < right.size()) {
        const char32_t left_point = fold_code_point(next_code_point(left, left_offset));
        const char32_t right_point = fold_code_point(next_code_point(right, right_offset));
        if (left_point != right_point) {
            return left_point < right_point ? -1 : 1;
        }
    }
    const bool left_more = left_offset < left.size();
    const bool right_more = right_offset < right.size();
    return three_way(left_more, right_more);
}

bool text_starts_with_ignoring_case(std::string_view text, std::string_view part) noexcept {
    const bool holds = folded_prefix_at(text, 0U, part);
    return holds;
}

bool text_contains_ignoring_case(std::string_view text, std::string_view part) noexcept {
    if (part.empty()) {
        return true;
    }
    // Tries each code point boundary of text; O(text * part).
    std::size_t offset = 0U;
    while (offset < text.size()) {
        if (folded_prefix_at(text, offset, part)) {
            return true;
        }
        static_cast<void>(next_code_point(text, offset));
    }
    return false;
}

} // namespace detail
} // namespace gui_forms
