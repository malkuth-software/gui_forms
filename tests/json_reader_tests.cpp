// The bounded JSON reader (stage T3, P10): RFC 8259 documents read into
// values, each limit and each defect refused with its own error and where
// it was found, and the narrower rules (duplicate names, U+0000, lone
// surrogates, a byte order mark) a manifest relies on.

#include "../src/core/json/json_reader/json_reader.hpp"

#include <array>
#include <cstddef>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>

namespace {

using gui_forms::detail::JsonError;
using gui_forms::detail::JsonKind;
using gui_forms::detail::JsonLimits;
using gui_forms::detail::JsonReadResult;
using gui_forms::detail::JsonValue;
using gui_forms::detail::json_depth_ceiling;
using gui_forms::detail::json_error_name;
using gui_forms::detail::read_json;

int failures = 0;

void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void test_documents() {
    const JsonReadResult read = read_json(
        "{\n  \"format\": \"gui-forms-theme/1\",\n  \"sheets\": [{\"path\": \"a.theme.css\", \"n\": -12.5e+3}],\n"
        "  \"flags\": [true, false, null], \"empty\": {}, \"none\": []\n}\n");
    require(static_cast<bool>(read) && read.root.kind == JsonKind::object, "an object reads");
    const JsonValue& root = read.root;
    const JsonValue* format = root.find("format");
    require(format != nullptr && (*format).kind == JsonKind::string && (*format).text == "gui-forms-theme/1",
            "a string member reads");
    const JsonValue* sheets = root.find("sheets");
    require(sheets != nullptr && (*sheets).kind == JsonKind::array && (*sheets).items.size() == 1U,
            "an array member reads");
    if (sheets != nullptr && (*sheets).items.size() == 1U) {
        const JsonValue* number = (*sheets).items[0].find("n");
        require(number != nullptr && (*number).kind == JsonKind::number && (*number).text == "-12.5e+3",
                "a number keeps its text");
    }
    const JsonValue* flags = root.find("flags");
    require(flags != nullptr && (*flags).items.size() == 3U && (*flags).items[0].boolean &&
                (*flags).items[1].kind == JsonKind::boolean && !(*flags).items[1].boolean &&
                (*flags).items[2].kind == JsonKind::null,
            "literals read");
    require(root.find("missing") == nullptr && (*flags).find("x") == nullptr, "find is null for no member");
    require(root.names.size() == 5U && root.names[0] == "format" && root.names[4] == "none",
            "members keep document order");

    const JsonReadResult scalar = read_json(" 0 ");
    require(static_cast<bool>(scalar) && scalar.root.kind == JsonKind::number && scalar.root.text == "0",
            "a scalar document reads");
    const JsonReadResult escapes =
        read_json("\"q\\\" b\\\\ s\\/ \\b\\f\\n\\r\\t \\u00e9 \\uD83D\\uDE00 caf\xC3\xA9\"");
    require(static_cast<bool>(escapes) &&
                escapes.root.text == "q\" b\\ s/ \b\f\n\r\t \xC3\xA9 \xF0\x9F\x98\x80 caf\xC3\xA9",
            "escapes decode to UTF-8");
    require(read_json("[1, [2, {\"a\": [3]}]]").root == read_json("[1,[2,{\"a\":[3]}]]").root,
            "whitespace does not change a value");
}

struct Refusal final {
    std::string_view text{};
    JsonError error{};
    std::size_t offset{};
};

void test_refusals() {
    const std::array<Refusal, 34> refusals{{
        {"", JsonError::unexpected_end, 0U},
        {"   ", JsonError::unexpected_end, 3U},
        {"\xEF\xBB\xBF{}", JsonError::byte_order_mark, 0U},
        {"{} x", JsonError::content_after_value, 3U},
        {"[1,]", JsonError::syntax_error, 3U},
        {"{\"a\":1,}", JsonError::syntax_error, 7U},
        {"{\"a\" 1}", JsonError::syntax_error, 5U},
        {"{a:1}", JsonError::syntax_error, 1U},
        {"[1 2]", JsonError::syntax_error, 3U},
        {"[1", JsonError::unexpected_end, 2U},
        {"{\"a\":", JsonError::unexpected_end, 5U},
        {"'a'", JsonError::syntax_error, 0U},
        {"tru", JsonError::invalid_literal, 0U},
        {"nul", JsonError::invalid_literal, 0U},
        {"True", JsonError::syntax_error, 0U},
        {"01", JsonError::content_after_value, 1U},
        {"-", JsonError::invalid_number, 1U},
        {"1.", JsonError::invalid_number, 2U},
        {".5", JsonError::syntax_error, 0U},
        {"1e", JsonError::invalid_number, 2U},
        {"+1", JsonError::syntax_error, 0U},
        {"\"a\\x\"", JsonError::invalid_escape, 2U},
        {"\"\\u12g4\"", JsonError::invalid_escape, 5U},
        {"\"\\uDE00\"", JsonError::invalid_unicode_escape, 1U},
        {"\"\\uD83D x\"", JsonError::invalid_unicode_escape, 1U},
        {"\"\\uD83D\\u0041\"", JsonError::invalid_unicode_escape, 1U},
        {"\"\\u0000\"", JsonError::nul_character, 1U},
        {std::string_view("\"a\0b\"", 5U), JsonError::nul_character, 2U},
        {"\"a\tb\"", JsonError::control_character, 2U},
        {"\"a\nb\"", JsonError::control_character, 2U},
        {"\"\xC3\x28\"", JsonError::invalid_utf8, 1U},
        {"\"\xED\xA0\x80\"", JsonError::invalid_utf8, 1U},
        {"\"\xC0\xAF\"", JsonError::invalid_utf8, 1U},
        {"{\"a\":1,\"a\":2}", JsonError::duplicate_name, 7U},
    }};
    for (const Refusal& entry : refusals) {
        const JsonReadResult read = read_json(entry.text);
        require(read.error == entry.error && read.position.byte_offset == entry.offset &&
                    read.root.kind == JsonKind::null,
                std::string("refused as ") + std::string(json_error_name(entry.error)) + " at " +
                    std::to_string(entry.offset) + ": " + std::string(entry.text) + " gave " +
                    std::string(json_error_name(read.error)) + " at " +
                    std::to_string(read.position.byte_offset));
    }
    const JsonReadResult placed = read_json("{\n  \"a\": 1,\r\n  \"\xC3\xA9\": tru }");
    require(placed.error == JsonError::invalid_literal && placed.position.line == 3U &&
                placed.position.column == 8U,
            "lines and columns count scalar values and each line ending once");
}

void test_limits() {
    JsonLimits limits{};
    limits.maximum_input_bytes = 4U;
    require(read_json("[1,2]", limits).error == JsonError::input_too_large &&
                static_cast<bool>(read_json("[12]", limits)),
            "the input bound");

    limits = JsonLimits{};
    limits.maximum_depth = 3U;
    require(static_cast<bool>(read_json("[[1]]", limits)) && static_cast<bool>(read_json("[[[]]]", limits)),
            "three levels read");
    require(read_json("[[[1]]]", limits).error == JsonError::depth_exceeded &&
                read_json("[[[[]]]]", limits).error == JsonError::depth_exceeded,
            "a fourth level is refused");
    const std::string deep(100000U, '[');
    require(read_json(deep).error == JsonError::depth_exceeded, "a hostile nesting stops at the depth bound");

    limits = JsonLimits{};
    limits.maximum_values = 3U;
    require(static_cast<bool>(read_json("[1,2]", limits)) &&
                read_json("[1,2,3]", limits).error == JsonError::value_limit_exceeded,
            "the value bound counts containers and scalars");

    limits = JsonLimits{};
    limits.maximum_members = 2U;
    require(static_cast<bool>(read_json("{\"a\":1,\"b\":2}", limits)) &&
                read_json("[1,2,3]", limits).error == JsonError::member_limit_exceeded &&
                read_json("{\"a\":1,\"b\":2,\"c\":3}", limits).error == JsonError::member_limit_exceeded,
            "the member bound");

    limits = JsonLimits{};
    limits.maximum_string_bytes = 3U;
    require(static_cast<bool>(read_json("\"\\u00e9a\"", limits)) &&
                read_json("\"\\u00e9ab\"", limits).error == JsonError::string_too_long &&
                read_json("{\"abcd\":1}", limits).error == JsonError::string_too_long,
            "the string bound counts decoded bytes, names included");

    limits = JsonLimits{};
    limits.maximum_number_bytes = 4U;
    require(static_cast<bool>(read_json("-1.5", limits)) &&
                read_json("-1.25", limits).error == JsonError::number_too_long,
            "the number bound");

    limits = JsonLimits{};
    limits.maximum_depth = json_depth_ceiling + 1U;
    require(read_json("1", limits).error == JsonError::invalid_limits, "a depth past the ceiling is refused");
    limits = JsonLimits{};
    limits.maximum_values = 0U;
    require(read_json("1", limits).error == JsonError::invalid_limits, "a zero bound is refused");
}

} // namespace

// Copying, comparing and destroying do not recurse per level (HOUSE_STYLE
// section 1): a value far deeper than the reader's ceiling, built in code,
// whose recursive teardown would exhaust an 8 MiB stack. The reader takes a
// document at the ceiling and refuses one level more.
void test_deep_values() {
    constexpr std::size_t deep = 200000U;
    {
        JsonValue current{};
        current.kind = JsonKind::number;
        current.text = "1";
        for (std::size_t level = 1U; level < deep; ++level) {
            JsonValue parent{};
            parent.kind = JsonKind::array;
            parent.items.push_back(std::move(current));
            current = std::move(parent);
        }
        const JsonValue copy = current;
        require(copy == current, "a deep copy compares equal");
        JsonValue assigned{};
        assigned = current;
        assigned.items.front().items.front().boolean = true;
        require(!(assigned == current), "a change two levels down compares unequal");
    }
    JsonLimits limits{};
    limits.maximum_depth = json_depth_ceiling;
    const std::string at_ceiling = std::string(json_depth_ceiling - 1U, '[') + "1" +
                                   std::string(json_depth_ceiling - 1U, ']');
    require(static_cast<bool>(read_json(at_ceiling, limits)), "a document at the ceiling reads");
    const std::string past_ceiling = std::string(json_depth_ceiling, '[') + "1" +
                                     std::string(json_depth_ceiling, ']');
    require(read_json(past_ceiling, limits).error == JsonError::depth_exceeded,
            "a document one level past the ceiling is refused");
}

int main() {
    test_documents();
    test_refusals();
    test_limits();
    test_deep_values();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "json reader tests passed\n";
    return 0;
}
