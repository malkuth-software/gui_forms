#include "automation_internal.hpp"

#include "../../../core/json/json_text/json_text.hpp"

#include <charconv>
#include <cmath>
#include <sstream>
#include <system_error>

namespace gui_forms::automation::detail {

gui_forms::detail::JsonLimits argument_limits() noexcept {
    gui_forms::detail::JsonLimits limits{};
    limits.maximum_input_bytes = 1U * 1024U * 1024U;
    limits.maximum_depth = 32U;
    limits.maximum_values = 65536U;
    limits.maximum_members = 1024U;
    limits.maximum_string_bytes = 256U * 1024U;
    return limits;
}

bool read_arguments(std::string_view json, JsonValue& object, std::string& error) {
    if (json.empty()) {
        object = JsonValue{};
        object.kind = JsonKind::object;
        return true;
    }
    gui_forms::detail::JsonReadResult read = gui_forms::detail::read_json(json, argument_limits());
    if (!read) {
        error = std::string("the arguments are not JSON: ") +
                std::string(gui_forms::detail::json_error_name(read.error)) + " at line " +
                std::to_string(read.position.line) + ", column " + std::to_string(read.position.column);
        return false;
    }
    if (read.root.kind != JsonKind::object) {
        error = "the arguments must be one JSON object";
        return false;
    }
    object = std::move(read.root);
    return true;
}

std::optional<std::string> text_member(const JsonValue& object, std::string_view name) {
    const JsonValue* const member = kind_member(object, name, JsonKind::string);
    if (member == nullptr) return std::nullopt;
    std::optional<std::string> text(std::in_place, (*member).text);
    return text;
}

bool number_value(const JsonValue& value, double& number) {
    if (value.kind != JsonKind::number) return false;
    const char* const first = value.text.data();
    const char* const last = first + value.text.size();
    double parsed = 0.0;
    const std::from_chars_result result = std::from_chars(first, last, parsed);
    if (result.ec != std::errc{} || result.ptr != last || !std::isfinite(parsed)) return false;
    number = parsed;
    return true;
}

bool point_value(const JsonValue& value, double& x, double& y) {
    if (value.kind == JsonKind::object) {
        const std::optional<double> given_x = number_member(value, "x");
        const std::optional<double> given_y = number_member(value, "y");
        if (!given_x.has_value() || !given_y.has_value()) return false;
        x = *given_x;
        y = *given_y;
        return true;
    }
    if (value.kind != JsonKind::array || value.items.size() != 2U) return false;
    double read_x = 0.0;
    double read_y = 0.0;
    if (!number_value(value.items[0], read_x) || !number_value(value.items[1], read_y)) return false;
    x = read_x;
    y = read_y;
    return true;
}

std::string point_schema(std::string_view description) {
    std::string schema = "{\"anyOf\":[{\"type\":\"object\",";
    if (!description.empty()) schema += "\"description\":\"" + std::string(description) + "\",";
    schema += "\"properties\":{\"x\":{\"type\":\"number\"},\"y\":{\"type\":\"number\"}},"
              "\"required\":[\"x\",\"y\"],\"additionalProperties\":false},"
              "{\"type\":\"array\",\"items\":{\"type\":\"number\"},\"minItems\":2,\"maxItems\":2}]}";
    return schema;
}

std::optional<double> number_member(const JsonValue& object, std::string_view name) {
    const JsonValue* const member = kind_member(object, name, JsonKind::number);
    if (member == nullptr) return std::nullopt;
    double number = 0.0;
    if (!number_value(*member, number)) return std::nullopt;
    std::optional<double> result(number);
    return result;
}

std::optional<bool> boolean_member(const JsonValue& object, std::string_view name) {
    const JsonValue* const member = kind_member(object, name, JsonKind::boolean);
    if (member == nullptr) return std::nullopt;
    std::optional<bool> result((*member).boolean);
    return result;
}

const JsonValue* kind_member(const JsonValue& object, std::string_view name, JsonKind kind) {
    const JsonValue* const member = object.find(name);
    if (member == nullptr || (*member).kind != kind) return nullptr;
    return member;
}

std::string unknown_members(const JsonValue& arguments, std::string_view schema, std::string& known) {
    known.clear();
    if (arguments.kind != JsonKind::object) return std::string{};
    gui_forms::detail::JsonReadResult read = gui_forms::detail::read_json(schema, argument_limits());
    if (!read || read.root.kind != JsonKind::object) return std::string{};
    // Only a schema that admits no other member is enforced.
    const JsonValue* const additional = kind_member(read.root, "additionalProperties", JsonKind::boolean);
    if (additional == nullptr || (*additional).boolean) return std::string{};
    const JsonValue* const properties = kind_member(read.root, "properties", JsonKind::object);
    std::string unknown{};
    for (const std::string& name : arguments.names) {
        const bool named = properties != nullptr && (*properties).find(name) != nullptr;
        if (named) continue;
        if (!unknown.empty()) unknown += ", ";
        unknown += name;
    }
    if (unknown.empty()) return unknown;
    if (properties != nullptr) {
        for (const std::string& name : (*properties).names) {
            if (!known.empty()) known += ", ";
            known += name;
        }
    }
    return unknown;
}

void append_json_string(std::string_view value, std::string& text) {
    std::ostringstream stream;
    gui_forms::detail::prepare_json_stream(stream);
    gui_forms::detail::append_json_string(stream, value);
    text += stream.str();
}

void append_json(const JsonValue& value, std::string& text) {
    switch (value.kind) {
    case JsonKind::null:
        text += "null";
        return;
    case JsonKind::boolean:
        text += value.boolean ? "true" : "false";
        return;
    case JsonKind::number:
        // Kept as written; the reader checked its grammar.
        text += value.text;
        return;
    case JsonKind::string:
        append_json_string(value.text, text);
        return;
    case JsonKind::array:
        text += '[';
        for (std::size_t index = 0U; index < value.items.size(); ++index) {
            if (index != 0U) text += ',';
            append_json(value.items[index], text);
        }
        text += ']';
        return;
    case JsonKind::object:
        text += '{';
        for (std::size_t index = 0U; index < value.items.size(); ++index) {
            if (index != 0U) text += ',';
            append_json_string(value.names[index], text);
            text += ':';
            append_json(value.items[index], text);
        }
        text += '}';
        return;
    }
    // A kind outside the enumeration is written as null.
    text += "null";
}

void object_without(const JsonValue& object, std::string_view skip, std::string& text) {
    text.clear();
    text += '{';
    bool first = true;
    for (std::size_t index = 0U; index < object.items.size(); ++index) {
        if (object.names[index] == skip) continue;
        if (!first) text += ',';
        first = false;
        append_json_string(object.names[index], text);
        text += ':';
        append_json(object.items[index], text);
    }
    text += '}';
}

} // namespace gui_forms::automation::detail
