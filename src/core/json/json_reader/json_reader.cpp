#include "json_reader.hpp"

#include "../../algorithm/tree_teardown.hpp"

#include <cstddef>
#include <utility>
#include <vector>

namespace gui_forms::detail {

namespace {

// One decoded UTF-8 sequence; length zero when the bytes are ill-formed
// (overlong forms, surrogates, values above U+10FFFF, truncation).
struct DecodedScalar final {
    std::uint32_t scalar{};
    std::size_t length{};
};

bool continuation(std::uint8_t byte) noexcept {
    const bool follows = (byte & 0xC0U) == 0x80U;
    return follows;
}

DecodedScalar decode_utf8(std::string_view text, std::size_t offset) noexcept {
    const std::uint8_t lead = static_cast<std::uint8_t>(text[offset]);
    DecodedScalar decoded{};
    std::size_t length = 0U;
    std::uint32_t scalar = 0U;
    std::uint32_t minimum = 0U;
    if (lead < 0x80U) {
        decoded.scalar = lead;
        decoded.length = 1U;
        return decoded;
    }
    if ((lead & 0xE0U) == 0xC0U) {
        length = 2U;
        scalar = lead & 0x1FU;
        minimum = 0x80U;
    } else if ((lead & 0xF0U) == 0xE0U) {
        length = 3U;
        scalar = lead & 0x0FU;
        minimum = 0x800U;
    } else if ((lead & 0xF8U) == 0xF0U) {
        length = 4U;
        scalar = lead & 0x07U;
        minimum = 0x10000U;
    } else {
        return decoded;
    }
    if (text.size() - offset < length) return decoded;
    for (std::size_t index = 1U; index < length; ++index) {
        const std::uint8_t byte = static_cast<std::uint8_t>(text[offset + index]);
        if (!continuation(byte)) return decoded;
        scalar = (scalar << 6U) | (byte & 0x3FU);
    }
    const bool surrogate = scalar >= 0xD800U && scalar <= 0xDFFFU;
    if (scalar < minimum || scalar > 0x10FFFFU || surrogate) return decoded;
    decoded.scalar = scalar;
    decoded.length = length;
    return decoded;
}

void append_utf8(std::string& text, std::uint32_t scalar) {
    if (scalar < 0x80U) {
        text.push_back(static_cast<char>(scalar));
    } else if (scalar < 0x800U) {
        text.push_back(static_cast<char>(0xC0U | (scalar >> 6U)));
        text.push_back(static_cast<char>(0x80U | (scalar & 0x3FU)));
    } else if (scalar < 0x10000U) {
        text.push_back(static_cast<char>(0xE0U | (scalar >> 12U)));
        text.push_back(static_cast<char>(0x80U | ((scalar >> 6U) & 0x3FU)));
        text.push_back(static_cast<char>(0x80U | (scalar & 0x3FU)));
    } else {
        text.push_back(static_cast<char>(0xF0U | (scalar >> 18U)));
        text.push_back(static_cast<char>(0x80U | ((scalar >> 12U) & 0x3FU)));
        text.push_back(static_cast<char>(0x80U | ((scalar >> 6U) & 0x3FU)));
        text.push_back(static_cast<char>(0x80U | (scalar & 0x3FU)));
    }
}

bool json_whitespace(char character) noexcept {
    const bool space = character == ' ' || character == '\t' || character == '\n' || character == '\r';
    return space;
}

bool decimal_digit(char character) noexcept {
    const bool digit = character >= '0' && character <= '9';
    return digit;
}

int hex_value(char character) noexcept {
    if (character >= '0' && character <= '9') return character - '0';
    if (character >= 'a' && character <= 'f') return character - 'a' + 10;
    if (character >= 'A' && character <= 'F') return character - 'A' + 10;
    return -1;
}

bool limits_valid(const JsonLimits& limits) noexcept {
    const bool valid = limits.maximum_depth != 0U && limits.maximum_depth <= json_depth_ceiling &&
                       limits.maximum_values != 0U && limits.maximum_members != 0U &&
                       limits.maximum_number_bytes != 0U;
    return valid;
}

// The line and column of `offset`.
JsonSourcePosition position_of(std::string_view text, std::size_t offset) noexcept {
    JsonSourcePosition position{};
    position.byte_offset = offset;
    position.line = 1U;
    position.column = 1U;
    std::size_t index = 0U;
    while (index < offset && index < text.size()) {
        const char character = text[index];
        if (character == '\r' || character == '\n') {
            const bool pair = character == '\r' && index + 1U < text.size() && text[index + 1U] == '\n';
            index += pair ? 2U : 1U;
            position.line += 1U;
            position.column = 1U;
            continue;
        }
        const DecodedScalar decoded = decode_utf8(text, index);
        index += decoded.length == 0U ? 1U : decoded.length;
        position.column += 1U;
    }
    return position;
}

// One open container while it is read.
struct Frame final {
    JsonValue value{};
};

class Reader final {
public:
    Reader(std::string_view text, const JsonLimits& limits) noexcept : text_(text), limits_(limits) {}

    // Reads the document into `root`; false with error_ and at_ set.
    bool read(JsonValue& root) {
        JsonValue completed{};
        while (true) {
            // A value is needed here.
            skip_whitespace();
            if (at_end()) return fail(JsonError::unexpected_end);
            const char character = text_[next_];
            if (character == '{' || character == '[') {
                if (!open_container(character == '{' ? JsonKind::object : JsonKind::array)) return false;
                skip_whitespace();
                if (at_end()) return fail(JsonError::unexpected_end);
                const char closer = (*top()).kind == JsonKind::object ? '}' : ']';
                if (text_[next_] != closer) {
                    if ((*top()).kind == JsonKind::object && !read_name()) return false;
                    continue;
                }
                next_ += 1U;
                completed = close_container();
            } else if (!read_scalar(completed)) {
                return false;
            }
            // A value is complete: give it to its container, then read on
            // until another value is needed.
            while (true) {
                if (stack_.empty()) {
                    skip_whitespace();
                    if (!at_end()) return fail(JsonError::content_after_value);
                    root = std::move(completed);
                    return true;
                }
                JsonValue& container = *top();
                container.items.push_back(std::move(completed));
                skip_whitespace();
                if (at_end()) return fail(JsonError::unexpected_end);
                const char after = text_[next_];
                const char closer = container.kind == JsonKind::object ? '}' : ']';
                if (after == ',') {
                    next_ += 1U;
                    if (container.items.size() >= limits_.maximum_members) {
                        return fail(JsonError::member_limit_exceeded);
                    }
                    if (container.kind == JsonKind::object) {
                        skip_whitespace();
                        if (!read_name()) return false;
                    }
                    break;
                }
                if (after != closer) return fail(JsonError::syntax_error);
                next_ += 1U;
                completed = close_container();
            }
        }
    }

    [[nodiscard]] JsonError error() const noexcept { return error_; }
    [[nodiscard]] std::size_t error_offset() const noexcept { return at_; }

private:
    [[nodiscard]] bool at_end() const noexcept { return next_ >= text_.size(); }

    JsonValue* top() noexcept { return &stack_.back().value; }

    void skip_whitespace() noexcept {
        while (!at_end() && json_whitespace(text_[next_])) next_ += 1U;
    }

    bool fail(JsonError error) noexcept { return fail_at(error, next_); }

    bool fail_at(JsonError error, std::size_t offset) noexcept {
        error_ = error;
        at_ = offset;
        return false;
    }

    bool count_value() noexcept {
        if (values_ >= limits_.maximum_values) return fail(JsonError::value_limit_exceeded);
        values_ += 1U;
        return true;
    }

    bool open_container(JsonKind kind) {
        if (stack_.size() + 1U > limits_.maximum_depth) return fail(JsonError::depth_exceeded);
        if (!count_value()) return false;
        stack_.emplace_back();
        (*top()).kind = kind;
        next_ += 1U;
        return true;
    }

    JsonValue close_container() {
        JsonValue closed = std::move(stack_.back().value);
        stack_.pop_back();
        return closed;
    }

    // A member's name and its ':' in the object on top, not yet named
    // twice.
    bool read_name() {
        if (at_end()) return fail(JsonError::unexpected_end);
        if (text_[next_] != '"') return fail(JsonError::syntax_error);
        const std::size_t start = next_;
        std::string name{};
        if (!read_string(name)) return false;
        JsonValue& object = *top();
        for (const std::string& held : object.names) {
            if (held == name) return fail_at(JsonError::duplicate_name, start);
        }
        skip_whitespace();
        if (at_end()) return fail(JsonError::unexpected_end);
        if (text_[next_] != ':') return fail(JsonError::syntax_error);
        next_ += 1U;
        object.names.push_back(std::move(name));
        return true;
    }

    bool read_scalar(JsonValue& value) {
        if (stack_.size() + 1U > limits_.maximum_depth) return fail(JsonError::depth_exceeded);
        if (!count_value()) return false;
        value = JsonValue{};
        const char character = text_[next_];
        if (character == '"') {
            value.kind = JsonKind::string;
            return read_string(value.text);
        }
        if (character == '-' || decimal_digit(character)) {
            value.kind = JsonKind::number;
            return read_number(value.text);
        }
        if (character == 't') {
            value.kind = JsonKind::boolean;
            value.boolean = true;
            return read_literal("true");
        }
        if (character == 'f') {
            value.kind = JsonKind::boolean;
            return read_literal("false");
        }
        if (character == 'n') return read_literal("null");
        return fail(JsonError::syntax_error);
    }

    bool read_literal(std::string_view literal) noexcept {
        if (text_.substr(next_, literal.size()) != literal) return fail(JsonError::invalid_literal);
        next_ += literal.size();
        return true;
    }

    bool read_digits() noexcept {
        if (at_end() || !decimal_digit(text_[next_])) return fail(JsonError::invalid_number);
        while (!at_end() && decimal_digit(text_[next_])) next_ += 1U;
        return true;
    }

    bool read_number(std::string& text) {
        const std::size_t start = next_;
        if (text_[next_] == '-') next_ += 1U;
        if (at_end()) return fail(JsonError::invalid_number);
        if (text_[next_] == '0') {
            next_ += 1U;
        } else if (!read_digits()) {
            return false;
        }
        if (!at_end() && text_[next_] == '.') {
            next_ += 1U;
            if (!read_digits()) return false;
        }
        if (!at_end() && (text_[next_] == 'e' || text_[next_] == 'E')) {
            next_ += 1U;
            if (!at_end() && (text_[next_] == '+' || text_[next_] == '-')) next_ += 1U;
            if (!read_digits()) return false;
        }
        if (next_ - start > limits_.maximum_number_bytes) return fail_at(JsonError::number_too_long, start);
        text.assign(text_.substr(start, next_ - start));
        return true;
    }

    // Four hexadecimal digits after `\u`; -1 with the error set when they
    // are not.
    std::int32_t read_hex4() noexcept {
        if (text_.size() - next_ < 4U) {
            fail_at(JsonError::invalid_escape, next_);
            return -1;
        }
        std::int32_t value = 0;
        for (std::size_t index = 0U; index < 4U; ++index) {
            const int digit = hex_value(text_[next_ + index]);
            if (digit < 0) {
                fail_at(JsonError::invalid_escape, next_ + index);
                return -1;
            }
            value = value * 16 + digit;
        }
        next_ += 4U;
        return value;
    }

    bool read_unicode_escape(std::string& text, std::size_t escape) {
        const std::int32_t first = read_hex4();
        if (first < 0) return false;
        std::uint32_t scalar = static_cast<std::uint32_t>(first);
        if (scalar >= 0xDC00U && scalar <= 0xDFFFU) return fail_at(JsonError::invalid_unicode_escape, escape);
        if (scalar >= 0xD800U && scalar <= 0xDBFFU) {
            if (text_.substr(next_, 2U) != "\\u") return fail_at(JsonError::invalid_unicode_escape, escape);
            next_ += 2U;
            const std::int32_t second = read_hex4();
            if (second < 0) return false;
            const std::uint32_t low = static_cast<std::uint32_t>(second);
            if (low < 0xDC00U || low > 0xDFFFU) return fail_at(JsonError::invalid_unicode_escape, escape);
            scalar = 0x10000U + ((scalar - 0xD800U) << 10U) + (low - 0xDC00U);
        }
        if (scalar == 0U) return fail_at(JsonError::nul_character, escape);
        append_utf8(text, scalar);
        return true;
    }

    bool read_escape(std::string& text) {
        const std::size_t escape = next_;
        next_ += 1U;
        if (at_end()) return fail(JsonError::unexpected_end);
        const char kind = text_[next_];
        next_ += 1U;
        switch (kind) {
        case '"': text.push_back('"'); return true;
        case '\\': text.push_back('\\'); return true;
        case '/': text.push_back('/'); return true;
        case 'b': text.push_back('\b'); return true;
        case 'f': text.push_back('\f'); return true;
        case 'n': text.push_back('\n'); return true;
        case 'r': text.push_back('\r'); return true;
        case 't': text.push_back('\t'); return true;
        case 'u': return read_unicode_escape(text, escape);
        default: return fail_at(JsonError::invalid_escape, escape);
        }
    }

    // A string from its opening quote, decoded into `text`.
    bool read_string(std::string& text) {
        text.clear();
        next_ += 1U;
        while (true) {
            if (at_end()) return fail(JsonError::unexpected_end);
            const std::size_t at = next_;
            const std::uint8_t byte = static_cast<std::uint8_t>(text_[next_]);
            if (byte == '"') {
                next_ += 1U;
                return true;
            }
            if (byte == 0U) return fail(JsonError::nul_character);
            if (byte < 0x20U) return fail(JsonError::control_character);
            if (byte == '\\') {
                if (!read_escape(text)) return false;
            } else if (byte < 0x80U) {
                text.push_back(static_cast<char>(byte));
                next_ += 1U;
            } else {
                const DecodedScalar decoded = decode_utf8(text_, next_);
                if (decoded.length == 0U) return fail(JsonError::invalid_utf8);
                text.append(text_.substr(next_, decoded.length));
                next_ += decoded.length;
            }
            if (text.size() > limits_.maximum_string_bytes) return fail_at(JsonError::string_too_long, at);
        }
    }

    std::string_view text_;
    const JsonLimits& limits_;
    std::vector<Frame> stack_{};
    std::size_t next_{};
    std::uint32_t values_{};
    JsonError error_{JsonError::none};
    std::size_t at_{};
};

} // namespace

std::string_view json_error_name(JsonError error) noexcept {
    switch (error) {
    case JsonError::none: return "none";
    case JsonError::invalid_limits: return "invalid_limits";
    case JsonError::input_too_large: return "input_too_large";
    case JsonError::byte_order_mark: return "byte_order_mark";
    case JsonError::invalid_utf8: return "invalid_utf8";
    case JsonError::unexpected_end: return "unexpected_end";
    case JsonError::syntax_error: return "syntax_error";
    case JsonError::invalid_literal: return "invalid_literal";
    case JsonError::invalid_number: return "invalid_number";
    case JsonError::number_too_long: return "number_too_long";
    case JsonError::invalid_escape: return "invalid_escape";
    case JsonError::invalid_unicode_escape: return "invalid_unicode_escape";
    case JsonError::control_character: return "control_character";
    case JsonError::nul_character: return "nul_character";
    case JsonError::string_too_long: return "string_too_long";
    case JsonError::depth_exceeded: return "depth_exceeded";
    case JsonError::value_limit_exceeded: return "value_limit_exceeded";
    case JsonError::member_limit_exceeded: return "member_limit_exceeded";
    case JsonError::duplicate_name: return "duplicate_name";
    case JsonError::content_after_value: return "content_after_value";
    default: return "unknown";
    }
}

const JsonValue* JsonValue::find(std::string_view name) const& noexcept {
    if (kind != JsonKind::object) return nullptr;
    for (std::size_t index = 0U; index < names.size() && index < items.size(); ++index) {
        if (names[index] == name) return &items[index];
    }
    return nullptr;
}

namespace {

// A source value and the copy being filled from it.
struct JsonCopyStep final {
    const JsonValue* source{nullptr};
    JsonValue* destination{nullptr};
};

// Two values whose items are still to be compared.
struct JsonComparePair final {
    const JsonValue* left{nullptr};
    const JsonValue* right{nullptr};
};

// One value's own fields, without its items' contents.
[[nodiscard]] bool same_fields(const JsonValue& left, const JsonValue& right) {
    return left.kind == right.kind && left.boolean == right.boolean && left.text == right.text &&
           left.names == right.names && left.items.size() == right.items.size();
}

} // namespace

JsonValue::JsonValue(const JsonValue& other)
    : kind(other.kind), boolean(other.boolean), text(other.text), names(other.names) {
    // Each step fills one destination's items with copies of the source's
    // own fields, then queues each item pair. A destination's item list is
    // reserved before it is filled, so the queued pointers stay valid.
    std::vector<JsonCopyStep> pending{};
    pending.push_back(JsonCopyStep{&other, this});
    while (!pending.empty()) {
        const JsonCopyStep step = pending.back();
        pending.pop_back();
        const std::vector<JsonValue>& source_items = (*step.source).items;
        std::vector<JsonValue>& destination_items = (*step.destination).items;
        destination_items.reserve(source_items.size());
        for (const JsonValue& source_item : source_items) {
            JsonValue shallow{};
            shallow.kind = source_item.kind;
            shallow.boolean = source_item.boolean;
            shallow.text = source_item.text;
            shallow.names = source_item.names;
            destination_items.push_back(std::move(shallow));
        }
        for (std::size_t index = 0U; index < source_items.size(); ++index) {
            if (source_items[index].items.empty()) continue;
            pending.push_back(JsonCopyStep{&source_items[index], &destination_items[index]});
        }
    }
}

JsonValue& JsonValue::operator=(const JsonValue& other) {
    JsonValue copy(other);
    *this = std::move(copy);
    return *this;
}

JsonValue::~JsonValue() {
    if (!items.empty()) release_children_iteratively<JsonValue, &JsonValue::items>(items);
}

bool operator==(const JsonValue& left, const JsonValue& right) {
    if (!same_fields(left, right)) return false;
    std::vector<JsonComparePair> pending{};
    pending.push_back(JsonComparePair{&left, &right});
    while (!pending.empty()) {
        const JsonComparePair pair = pending.back();
        pending.pop_back();
        const std::vector<JsonValue>& left_items = (*pair.left).items;
        const std::vector<JsonValue>& right_items = (*pair.right).items;
        for (std::size_t index = 0U; index < left_items.size(); ++index) {
            if (!same_fields(left_items[index], right_items[index])) return false;
            if (left_items[index].items.empty()) continue;
            pending.push_back(JsonComparePair{&left_items[index], &right_items[index]});
        }
    }
    return true;
}

JsonReadResult read_json(std::string_view utf8, const JsonLimits& limits) {
    JsonReadResult result{};
    if (!limits_valid(limits)) {
        result.error = JsonError::invalid_limits;
        return result;
    }
    if (utf8.size() > limits.maximum_input_bytes) {
        result.error = JsonError::input_too_large;
        result.position = position_of(utf8, 0U);
        result.position.byte_offset = limits.maximum_input_bytes;
        return result;
    }
    if (utf8.substr(0U, 3U) == "\xEF\xBB\xBF") {
        result.error = JsonError::byte_order_mark;
        result.position = position_of(utf8, 0U);
        return result;
    }
    Reader reader(utf8, limits);
    JsonValue root{};
    if (!reader.read(root)) {
        result.error = reader.error();
        result.position = position_of(utf8, reader.error_offset());
        return result;
    }
    result.root = std::move(root);
    return result;
}

} // namespace gui_forms::detail
