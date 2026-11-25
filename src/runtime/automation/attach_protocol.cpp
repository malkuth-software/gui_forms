// The attach endpoint's protocol pieces (attach_endpoint.hpp): reading a
// connection's first line (MCP or the console), the console's commands, a
// JSON-RPC error line for a refused initialize, the discovery entry's text,
// session tokens, and line reads and writes. Compiled into the automation
// and into the gui-forms-attach bridge.

#include "attach_endpoint.hpp"

#include <stxlib/entropy.hpp>
#include <stxlib/json.hpp>
#include <stxlib/result.hpp>
#include <stxlib/text.hpp>
#include <stxlib/text_view.hpp>
#include <stxlib/types.hpp>
#include <stxlib/view.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms::automation {

namespace {

constexpr std::size_t maximum_field = 256U;
constexpr std::size_t maximum_note = 512U;
constexpr std::size_t token_bytes = 16U;

// `text` with control bytes as spaces, cut to `limit` bytes at the start of
// a UTF-8 sequence.
std::string clean_field(std::string_view text, std::size_t limit) {
    std::string cleaned{};
    for (const char character : text) {
        const unsigned char byte = static_cast<unsigned char>(character);
        cleaned += byte < 0x20U || byte == 0x7fU ? ' ' : character;
    }
    if (cleaned.size() > limit) {
        std::size_t cut = limit;
        // Back over continuation bytes to a sequence's first byte.
        while (cut > 0U && (static_cast<unsigned char>(cleaned[cut]) & 0xc0U) == 0x80U) --cut;
        cleaned.resize(cut);
    }
    return cleaned;
}

bool is_space(char character) noexcept {
    return character == ' ' || character == '\t' || character == '\r' || character == '\n';
}

std::string_view trimmed(std::string_view text) noexcept {
    std::size_t start = 0U;
    while (start < text.size() && is_space(text[start])) ++start;
    std::size_t end = text.size();
    while (end > start && is_space(text[end - 1U])) --end;
    return text.substr(start, end - start);
}

// `text` as a view for STXlib; false when it is not UTF-8.
bool checked_view(std::string_view text, stxlib::TextView& view) noexcept {
    const stxlib::OperationResult<stxlib::TextView> checked = stxlib::validate_utf8(text.data(), text.size());
    if (checked.failed()) return false;
    view = *checked.value();
    return true;
}

std::string standard_text(stxlib::TextView text) {
    std::string made(text.data(), static_cast<std::size_t>(text.count()));
    return made;
}

// The member's text, or empty when it is missing or not a string.
std::string member_string(const stxlib::JsonDocument& document, stxlib::JsonRef object, stxlib::TextView name) {
    const stxlib::Optional<stxlib::TextView> text = document.member_text(object, name);
    if (!text.present()) return std::string{};
    return standard_text(*text.value());
}

// The first word of `text` from `start`, which moves past it and the
// spaces after it.
std::string_view next_word(std::string_view text, std::size_t& start) noexcept {
    while (start < text.size() && is_space(text[start])) ++start;
    const std::size_t begin = start;
    while (start < text.size() && !is_space(text[start])) ++start;
    const std::string_view word = text.substr(begin, start - begin);
    while (start < text.size() && is_space(text[start])) ++start;
    return word;
}

// Splits `text` at spaces outside double quotes and brackets: the
// key=value items of a console call. False when a quote or a bracket is
// left open.
bool split_items(std::string_view text, std::vector<std::string_view>& items) {
    std::size_t begin = 0U;
    std::size_t depth = 0U;
    bool quoted = false;
    bool escaped = false;
    for (std::size_t at = 0U; at <= text.size(); ++at) {
        const bool end = at == text.size();
        const char character = end ? ' ' : text[at];
        if (quoted) {
            if (end) return false;
            if (escaped) {
                escaped = false;
            } else if (character == '\\') {
                escaped = true;
            } else if (character == '"') {
                quoted = false;
            }
            continue;
        }
        if (character == '"') {
            quoted = true;
        } else if (character == '{' || character == '[') {
            ++depth;
        } else if ((character == '}' || character == ']') && depth > 0U) {
            --depth;
        } else if (is_space(character) && depth == 0U) {
            if (at > begin) items.push_back(text.substr(begin, at - begin));
            begin = at + 1U;
        }
    }
    return depth == 0U;
}

// A call's arguments from `text`: one JSON object as it is, or key=value
// items made into one. False, with `error` set, when they cannot be read.
bool call_arguments(std::string_view text, std::string& arguments, std::string& error) {
    if (text.empty()) {
        arguments = "{}";
        return true;
    }
    stxlib::TextView view{};
    if (!checked_view(text, view)) {
        error = "the arguments are not UTF-8";
        return false;
    }
    if (text.front() == '{') {
        stxlib::OperationResult<stxlib::JsonDocument> parsed = stxlib::JsonDocument::parse(view);
        if (parsed.failed() || !(*parsed.value()).is((*parsed.value()).root(), stxlib::JsonKind::object)) {
            error = "the arguments are not one JSON object";
            return false;
        }
        stxlib::Utf8String written{};
        stxlib::JsonWriter writer(written);
        writer.copy(*parsed.value(), (*parsed.value()).root());
        arguments = standard_text(written.view());
        return true;
    }
    std::vector<std::string_view> items{};
    if (!split_items(text, items)) {
        error = "a quote or a bracket in the arguments is not closed";
        return false;
    }
    stxlib::Utf8String written{};
    stxlib::JsonWriter writer(written);
    writer.begin_object();
    for (const std::string_view item : items) {
        const std::size_t equals = item.find('=');
        if (equals == std::string_view::npos || equals == 0U) {
            error = "expected key=value or one JSON object, not \"" + std::string(item) + "\"";
            return false;
        }
        stxlib::TextView key{};
        stxlib::TextView value{};
        static_cast<void>(checked_view(item.substr(0U, equals), key));
        static_cast<void>(checked_view(item.substr(equals + 1U), value));
        writer.key(key);
        stxlib::OperationResult<stxlib::JsonDocument> parsed = stxlib::JsonDocument::parse(value);
        if (parsed.failed()) {
            writer.string(value);
        } else {
            writer.copy(*parsed.value(), (*parsed.value()).root());
        }
    }
    writer.end_object();
    if (writer.failed()) {
        error = "the arguments could not be written as JSON";
        return false;
    }
    arguments = standard_text(written.view());
    return true;
}

} // namespace

AttachOpening read_attach_opening(std::string_view line) {
    AttachOpening opening{};
    const std::string_view text = trimmed(line);
    if (text.empty() || text.front() != '{') return opening;
    // A line that opens an object is MCP's, even one that does not parse:
    // MCP's server answers it with a parse error.
    opening.json = true;
    stxlib::TextView view{};
    if (!checked_view(text, view)) return opening;
    stxlib::OperationResult<stxlib::JsonDocument> parsed = stxlib::JsonDocument::parse(view);
    if (parsed.failed()) return opening;
    const stxlib::JsonDocument& document = *parsed.value();
    const stxlib::JsonRef root = document.root();
    if (!document.is(root, stxlib::JsonKind::object)) return opening;
    opening.method = clean_field(member_string(document, root, "method"), maximum_field);
    const stxlib::Optional<stxlib::JsonRef> id = document.member(root, "id");
    if (id.present()) {
        stxlib::Utf8String written{};
        stxlib::JsonWriter writer(written);
        writer.copy(document, *id.value());
        if (!writer.failed()) opening.id_json = standard_text(written.view());
    }
    const stxlib::Optional<stxlib::JsonRef> params = document.member(root, "params");
    if (!params.present() || !document.is(*params.value(), stxlib::JsonKind::object)) return opening;
    const stxlib::Optional<stxlib::JsonRef> client = document.member(*params.value(), "clientInfo");
    if (client.present() && document.is(*client.value(), stxlib::JsonKind::object)) {
        opening.client_name = clean_field(member_string(document, *client.value(), "name"), maximum_field);
        opening.client_version = clean_field(member_string(document, *client.value(), "version"), maximum_field);
    }
    opening.resume = clean_field(member_string(document, *params.value(), "resume"), maximum_field);
    if (opening.resume.empty()) {
        const stxlib::Optional<stxlib::JsonRef> meta = document.member(*params.value(), "_meta");
        if (meta.present() && document.is(*meta.value(), stxlib::JsonKind::object)) {
            opening.resume = clean_field(member_string(document, *meta.value(), "gui-forms/resume"), maximum_field);
        }
    }
    return opening;
}

ConsoleCommand parse_console_command(std::string_view line) {
    ConsoleCommand command{};
    const std::string_view text = trimmed(line);
    if (text.empty()) return command;
    std::size_t start = 0U;
    const std::string_view verb = next_word(text, start);
    const std::string_view rest = text.substr(start);
    if (verb == "help" || verb == "?") {
        command.verb = ConsoleVerb::help;
    } else if (verb == "tools") {
        command.verb = ConsoleVerb::tools;
    } else if (verb == "transcript") {
        command.verb = ConsoleVerb::transcript;
    } else if (verb == "quit" || verb == "exit") {
        command.verb = ConsoleVerb::quit;
    } else if (verb == "resume") {
        command.verb = ConsoleVerb::resume;
        command.token = clean_field(trimmed(rest), maximum_field);
        if (command.token.empty()) {
            command.verb = ConsoleVerb::unknown;
            command.error = "resume needs the token an earlier session printed";
        }
    } else if (verb == "call") {
        std::size_t after = 0U;
        command.tool = std::string(next_word(rest, after));
        if (command.tool.empty()) {
            command.verb = ConsoleVerb::unknown;
            command.error = "call needs a tool's name: call <tool> [<json object> | key=value ...]";
            return command;
        }
        command.verb = ConsoleVerb::call;
        if (!call_arguments(trimmed(rest.substr(after)), command.arguments_json, command.error)) {
            command.verb = ConsoleVerb::unknown;
        }
    } else {
        command.verb = ConsoleVerb::unknown;
        command.error = "unknown command \"" + clean_field(verb, maximum_field) +
                        "\"; the commands are help, tools, call, transcript, resume and quit";
    }
    return command;
}

std::string json_rpc_error_line(std::string_view id_json, int code, std::string_view message) {
    stxlib::Utf8String written{};
    stxlib::JsonWriter writer(written);
    writer.begin_object();
    writer.member_string("jsonrpc", "2.0");
    writer.key("id");
    // The id as the opening read it; null when it does not parse.
    stxlib::TextView id_text{};
    if (!checked_view(id_json, id_text)) id_text = "null";
    stxlib::OperationResult<stxlib::JsonDocument> parsed = stxlib::JsonDocument::parse(id_text);
    if (parsed.failed()) {
        writer.null_value();
    } else {
        writer.copy(*parsed.value(), (*parsed.value()).root());
    }
    writer.key("error");
    writer.begin_object();
    writer.member_integer("code", static_cast<stxlib::i64>(code));
    const std::string cleaned = clean_field(message, maximum_console_line);
    stxlib::TextView text{};
    static_cast<void>(checked_view(cleaned, text));
    writer.member_string("message", text);
    writer.end_object();
    writer.end_object();
    std::string line = standard_text(written.view());
    line += '\n';
    return line;
}

std::string attach_info_text(const AttachListing& listing) {
    std::string text{};
    text += "name: " + clean_field(listing.name, maximum_field) + "\n";
    text += "identity: " + clean_field(listing.identity, maximum_field) + "\n";
    text += "pid: " + std::to_string(listing.process) + "\n";
    text += "executable: " + clean_field(listing.executable, 4096U) + "\n";
    text += "socket: " + clean_field(listing.address, 4096U) + "\n";
    text += "tools: " + std::to_string(listing.tools) + "\n";
    text += "note: " + clean_field(listing.note, maximum_note) + "\n";
    return text;
}

bool parse_attach_info(std::string_view text, AttachListing& listing) {
    bool has_process = false;
    std::size_t start = 0U;
    while (start < text.size()) {
        const std::size_t newline = text.find('\n', start);
        const std::size_t end = newline == std::string_view::npos ? text.size() : newline;
        const std::string_view line = text.substr(start, end - start);
        start = newline == std::string_view::npos ? text.size() : newline + 1U;
        const std::size_t colon = line.find(':');
        if (colon == std::string_view::npos) continue;
        const std::string_view key = line.substr(0U, colon);
        const std::string value(trimmed(line.substr(colon + 1U)));
        if (key == "name") {
            listing.name = value;
        } else if (key == "identity") {
            listing.identity = value;
        } else if (key == "executable") {
            listing.executable = value;
        } else if (key == "socket") {
            listing.address = value;
        } else if (key == "note") {
            listing.note = value;
        } else if (key == "pid" || key == "tools") {
            std::uint64_t number = 0U;
            if (value.empty() || value.size() > 18U) return false;
            for (const char digit : value) {
                if (digit < '0' || digit > '9') return false;
                number = number * 10U + static_cast<std::uint64_t>(digit - '0');
            }
            if (key == "pid") {
                listing.process = static_cast<std::int64_t>(number);
                has_process = number != 0U;
            } else {
                listing.tools = static_cast<std::size_t>(number);
            }
        }
    }
    return has_process && !listing.address.empty();
}

std::string make_session_token() {
    std::array<stxlib::Byte, token_bytes> bytes{};
    const stxlib::OperationStatus filled =
        stxlib::fill_entropy(stxlib::MutableView<stxlib::Byte>(bytes.data(), bytes.size()));
    if (filled.failed()) return std::string{};
    constexpr std::string_view digits = "0123456789abcdef";
    std::string token{};
    for (const stxlib::Byte byte : bytes) {
        token += digits[static_cast<std::size_t>(byte >> 4U)];
        token += digits[static_cast<std::size_t>(byte & 0x0fU)];
    }
    return token;
}

bool read_attach_line(stxlib::PipeReader& input, std::string& line, std::size_t maximum) {
    line.clear();
    while (line.size() <= maximum) {
        stxlib::Byte byte = 0U;
        const stxlib::OperationResult<stxlib::PipeRead> read = input.read(stxlib::MutableView<stxlib::Byte>(&byte, 1U));
        if (read.failed()) return false;
        if ((*read.value()).is_end()) return false;
        if (byte == static_cast<stxlib::Byte>('\n')) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            return true;
        }
        line += static_cast<char>(byte);
    }
    return false;
}

bool write_attach_line(stxlib::PipeWriter& output, std::string_view text) {
    const stxlib::View<stxlib::Byte> bytes(reinterpret_cast<const stxlib::Byte*>(text.data()), text.size());
    const stxlib::OperationStatus written = output.write_all(bytes);
    return written.succeeded();
}

} // namespace gui_forms::automation
