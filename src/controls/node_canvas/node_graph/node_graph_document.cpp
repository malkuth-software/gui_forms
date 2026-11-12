#include "gui_forms/controls/node_graph.hpp"

#include "node_graph_text.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstring>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

// The node graph's document, version 1 (format in node_graph.hpp, above
// NodeGraph::save). save measures, then writes into the caller's storage.
// load runs in three phases:
//   1. read: one pass over the bytes, line by line, into a ParsedDocument
//      (node records in document order, links by key text), refusing the
//      first malformed byte; the parse is a loop, never recursive;
//   2. resolve: keys sorted once (duplicates), links looked up by key and
//      checked as connect checks them, port occupancy in one flat array;
//   3. install: storage reserved, then the graph's arrays replaced by moves
//      that cannot fail.
// Phases 1 and 2 touch only work storage, bounded by the document's own size
// and by maximum_document_nodes and maximum_document_links; a refusal at
// either leaves the graph and its undo log untouched.

namespace gui_forms {
namespace {

constexpr std::string_view document_header = "gui-forms-graph ";

std::string_view direction_word(PortDirection direction) noexcept {
    switch (direction) {
    case PortDirection::input: return "input";
    case PortDirection::output: return "output";
    }
    return "input";
}

std::string_view side_word(PortSide side) noexcept {
    switch (side) {
    case PortSide::left: return "left";
    case PortSide::right: return "right";
    case PortSide::top: return "top";
    case PortSide::bottom: return "bottom";
    case PortSide::automatic: return "automatic";
    }
    return "automatic";
}

// Whether a byte may appear outside a quoted text: printable ASCII and the
// line end.
bool format_byte(char byte) noexcept {
    const unsigned char value = static_cast<unsigned char>(byte);
    const bool printable = value >= 0x20U && value <= 0x7EU;
    return printable || byte == '\n';
}

// -- writing ----------------------------------------------------------------------

// Appends document bytes. Constructed without storage, it only counts; save
// runs it once that way, checks the count against the caller's storage, and
// runs it again writing.
class DocumentWriter final {
public:
    explicit DocumentWriter(char* storage) noexcept : storage_(storage) {}

    [[nodiscard]] std::size_t length() const noexcept { return length_; }

    void bytes(std::string_view text) noexcept {
        if (storage_ != nullptr && !text.empty()) std::memcpy(storage_ + length_, text.data(), text.size());
        length_ += text.size();
    }

    void byte(char value) noexcept {
        if (storage_ != nullptr) storage_[length_] = value;
        ++length_;
    }

    // The shortest decimal that reads back to the same double.
    void real(double value) noexcept {
        std::array<char, 32> digits{};
        const std::to_chars_result written = std::to_chars(digits.data(), digits.data() + digits.size(), value);
        const std::size_t count = static_cast<std::size_t>(written.ptr - digits.data());
        bytes(std::string_view(digits.data(), count));
    }

    void whole(std::uint32_t value) noexcept {
        std::array<char, 16> digits{};
        const std::to_chars_result written = std::to_chars(digits.data(), digits.data() + digits.size(), value);
        const std::size_t count = static_cast<std::size_t>(written.ptr - digits.data());
        bytes(std::string_view(digits.data(), count));
    }

    // A quoted text with the format's escapes.
    void quoted(std::string_view text) noexcept {
        constexpr std::string_view hex = "0123456789abcdef";
        byte('"');
        for (const char value : text) {
            const unsigned char code = static_cast<unsigned char>(value);
            if (value == '\\') {
                bytes("\\\\");
            } else if (value == '"') {
                bytes("\\\"");
            } else if (value == '\n') {
                bytes("\\n");
            } else if (value == '\t') {
                bytes("\\t");
            } else if (value == '\r') {
                bytes("\\r");
            } else if (code < 0x20U || code == 0x7FU) {
                bytes("\\x");
                byte(hex[code >> 4U]);
                byte(hex[code & 0x0FU]);
            } else {
                byte(value);
            }
        }
        byte('"');
    }

private:
    char* storage_{};
    std::size_t length_{};
};

void write_node(const NodeRecord& node, DocumentWriter& writer) noexcept {
    writer.bytes("node ");
    writer.bytes(node.key);
    writer.bytes(" {\n    kind: ");
    writer.quoted(node.kind);
    writer.bytes(";\n    title: ");
    writer.quoted(node.title);
    writer.bytes(";\n    bounds: ");
    writer.real(node.bounds.x);
    writer.byte(' ');
    writer.real(node.bounds.y);
    writer.byte(' ');
    writer.real(node.bounds.width);
    writer.byte(' ');
    writer.real(node.bounds.height);
    writer.bytes(";\n");
    for (const PortRecord& port : node.ports) {
        writer.bytes("    port: ");
        writer.bytes(direction_word(port.direction));
        writer.byte(' ');
        writer.bytes(side_word(port.side));
        writer.byte(' ');
        writer.real(port.offset);
        writer.byte(' ');
        writer.whole(port.socket_type);
        writer.byte(' ');
        writer.quoted(port.name);
        writer.bytes(";\n");
    }
    for (const NodeProperty& property : node.properties) {
        writer.bytes("    ");
        writer.bytes(property.name);
        writer.bytes(": ");
        writer.quoted(property.value);
        writer.bytes(";\n");
    }
    writer.bytes("}\n");
}

void write_document(std::span<const NodeRecord> nodes, std::span<const LinkRecord> links,
                    DocumentWriter& writer) noexcept {
    writer.bytes(document_header);
    writer.whole(graph_document_version);
    writer.bytes(";\n");
    for (const NodeRecord& node : nodes) {
        if (node.live) write_node(node, writer);
    }
    for (const LinkRecord& link : links) {
        if (!link.live) continue;
        writer.bytes("link ");
        writer.bytes(nodes[link.from.node.index].key);
        writer.byte(':');
        writer.whole(link.from.port.value);
        writer.bytes(" -> ");
        writer.bytes(nodes[link.to.node.index].key);
        writer.byte(':');
        writer.whole(link.to.port.value);
        writer.bytes(";\n");
    }
    writer.bytes("end;\n");
}

// -- reading ----------------------------------------------------------------------

// The range a number field admits.
enum class NumberRange : std::uint8_t { any, non_negative, unit };

// A link as read: its ends by key text (views of the document) and port
// index, with the byte offsets errors name; resolve fills the node slots.
struct ParsedLink final {
    std::string_view from_key{};
    std::string_view to_key{};
    std::uint32_t from_port{};
    std::uint32_t to_port{};
    std::size_t from_key_offset{};
    std::size_t from_port_offset{};
    std::size_t to_key_offset{};
    std::size_t to_port_offset{};
    std::uint32_t from_node{};
    std::uint32_t to_node{};
};

// The intermediate form between reading and installing. Declared after the
// document it views and destroyed before load returns.
struct ParsedDocument final {
    std::vector<NodeRecord> nodes{};
    // Per node: the byte offset of its key, for duplicate_key.
    std::vector<std::size_t> key_offsets{};
    std::vector<ParsedLink> links{};
    // Node indices ordered by key (resolve fills it; install keeps it).
    std::vector<std::uint32_t> order{};
};

// A cursor over the document. Each read either advances past what it read
// and returns ok, or records the failing byte's offset and returns the error.
class DocumentReader final {
public:
    explicit DocumentReader(std::string_view text) noexcept : text_(text) {}

    [[nodiscard]] std::size_t offset() const noexcept { return offset_; }
    [[nodiscard]] std::size_t failure() const noexcept { return failure_; }
    [[nodiscard]] bool at_end() const noexcept { return offset_ >= text_.size(); }
    [[nodiscard]] char peek() const noexcept { return text_[offset_]; }

    [[nodiscard]] bool starts_with(std::string_view expected) const noexcept {
        const std::string_view rest = text_.substr(offset_);
        const bool starts = rest.size() >= expected.size() && rest.substr(0U, expected.size()) == expected;
        return starts;
    }

    // Whether the rest of the input is a proper prefix of `expected`: the
    // document was cut inside it.
    [[nodiscard]] bool cut_inside(std::string_view expected) const noexcept {
        const std::string_view rest = text_.substr(offset_);
        const bool cut = rest.size() < expected.size() && expected.substr(0U, rest.size()) == rest;
        return cut;
    }

    GraphLoadError fail(GraphLoadError error, std::size_t at) noexcept {
        failure_ = at;
        return error;
    }

    // `mismatch` names a wrong byte the format uses; a byte it never uses
    // outside texts is unexpected_character.
    GraphLoadError mismatch_at(std::size_t at, GraphLoadError mismatch) noexcept {
        const GraphLoadError error = format_byte(text_[at]) ? mismatch : GraphLoadError::unexpected_character;
        return fail(error, at);
    }

    GraphLoadError literal(std::string_view expected, GraphLoadError mismatch) noexcept {
        for (std::size_t index = 0U; index < expected.size(); ++index) {
            const std::size_t at = offset_ + index;
            if (at >= text_.size()) return fail(GraphLoadError::truncated, text_.size());
            if (text_[at] != expected[index]) return mismatch_at(at, mismatch);
        }
        offset_ += expected.size();
        return GraphLoadError::ok;
    }

    // A run of key bytes, 1 to maximum_graph_key_bytes long.
    GraphLoadError key(std::string_view& out) noexcept {
        const std::size_t start = offset_;
        std::size_t at = start;
        while (at < text_.size() && node_graph_text::key_byte(text_[at])) ++at;
        if (at >= text_.size()) return fail(GraphLoadError::truncated, text_.size());
        const std::size_t length = at - start;
        if (length == 0U) return mismatch_at(start, GraphLoadError::bad_key);
        if (length > maximum_graph_key_bytes) return fail(GraphLoadError::bad_key, start);
        out = text_.substr(start, length);
        offset_ = at;
        return GraphLoadError::ok;
    }

    // The bytes up to the next space, semicolon or line end.
    GraphLoadError token(std::string_view& out, GraphLoadError empty) noexcept {
        const std::size_t start = offset_;
        std::size_t at = start;
        while (at < text_.size()) {
            const char byte = text_[at];
            if (byte == ' ' || byte == ';' || byte == '\n') break;
            if (!format_byte(byte)) return fail(GraphLoadError::unexpected_character, at);
            ++at;
        }
        if (at >= text_.size()) return fail(GraphLoadError::truncated, text_.size());
        if (at == start) return fail(empty, start);
        out = text_.substr(start, at - start);
        offset_ = at;
        return GraphLoadError::ok;
    }

    GraphLoadError real(double& out, NumberRange range) noexcept {
        const std::size_t start = offset_;
        std::string_view text{};
        const GraphLoadError read = token(text, GraphLoadError::bad_number);
        if (read != GraphLoadError::ok) return read;
        double value = 0.0;
        const char* const last = text.data() + text.size();
        const std::from_chars_result parsed = std::from_chars(text.data(), last, value, std::chars_format::general);
        if (parsed.ec != std::errc{} || parsed.ptr != last || !std::isfinite(value)) {
            return fail(GraphLoadError::bad_number, start);
        }
        const bool in_range = range == NumberRange::any || (range == NumberRange::non_negative && value >= 0.0) ||
                              (range == NumberRange::unit && value >= 0.0 && value <= 1.0);
        if (!in_range) return fail(GraphLoadError::bad_number, start);
        std::array<char, 32> canonical{};
        const std::to_chars_result written =
            std::to_chars(canonical.data(), canonical.data() + canonical.size(), value);
        const std::size_t count = static_cast<std::size_t>(written.ptr - canonical.data());
        if (std::string_view(canonical.data(), count) != text) return fail(GraphLoadError::non_canonical, start);
        out = value;
        return GraphLoadError::ok;
    }

    GraphLoadError whole(std::uint32_t& out, std::uint32_t maximum) noexcept {
        const std::size_t start = offset_;
        std::string_view text{};
        const GraphLoadError read = token(text, GraphLoadError::bad_number);
        if (read != GraphLoadError::ok) return read;
        std::uint32_t value = 0U;
        const char* const last = text.data() + text.size();
        const std::from_chars_result parsed = std::from_chars(text.data(), last, value, 10);
        if (parsed.ec != std::errc{} || parsed.ptr != last || value > maximum) {
            return fail(GraphLoadError::bad_number, start);
        }
        std::array<char, 16> canonical{};
        const std::to_chars_result written =
            std::to_chars(canonical.data(), canonical.data() + canonical.size(), value);
        const std::size_t count = static_cast<std::size_t>(written.ptr - canonical.data());
        if (std::string_view(canonical.data(), count) != text) return fail(GraphLoadError::non_canonical, start);
        out = value;
        return GraphLoadError::ok;
    }

    // A quoted text, decoded into `out` (at most maximum_graph_text_bytes).
    // May throw std::bad_alloc while `out` grows.
    GraphLoadError quoted(std::string& out) {
        const std::size_t start = offset_;
        const GraphLoadError opened = literal("\"", GraphLoadError::syntax);
        if (opened != GraphLoadError::ok) return opened;
        out.clear();
        while (true) {
            if (offset_ >= text_.size()) return fail(GraphLoadError::truncated, text_.size());
            const char byte = text_[offset_];
            const unsigned char code = static_cast<unsigned char>(byte);
            char decoded = byte;
            if (byte == '"') {
                ++offset_;
                return GraphLoadError::ok;
            }
            if (byte == '\\') {
                const GraphLoadError escaped = escape(decoded);
                if (escaped != GraphLoadError::ok) return escaped;
            } else if (code < 0x20U || code == 0x7FU) {
                return fail(GraphLoadError::bad_text, offset_);
            } else {
                ++offset_;
            }
            if (out.size() >= maximum_graph_text_bytes) return fail(GraphLoadError::limit, start);
            out.push_back(decoded);
        }
    }

private:
    // One escape at offset_ (a backslash), in its canonical form only.
    GraphLoadError escape(char& decoded) noexcept {
        const std::size_t start = offset_;
        if (start + 1U >= text_.size()) return fail(GraphLoadError::truncated, text_.size());
        const char kind = text_[start + 1U];
        switch (kind) {
        case '\\': decoded = '\\'; break;
        case '"': decoded = '"'; break;
        case 'n': decoded = '\n'; break;
        case 't': decoded = '\t'; break;
        case 'r': decoded = '\r'; break;
        case 'x': {
            std::uint32_t code = 0U;
            for (std::size_t digit = 2U; digit < 4U; ++digit) {
                if (start + digit >= text_.size()) return fail(GraphLoadError::truncated, text_.size());
                const char hex = text_[start + digit];
                std::uint32_t value = 0U;
                if (hex >= '0' && hex <= '9') {
                    value = static_cast<std::uint32_t>(hex - '0');
                } else if (hex >= 'a' && hex <= 'f') {
                    value = static_cast<std::uint32_t>(hex - 'a') + 10U;
                } else if (hex >= 'A' && hex <= 'F') {
                    return fail(GraphLoadError::non_canonical, start);
                } else {
                    return fail(GraphLoadError::bad_text, start);
                }
                code = code * 16U + value;
            }
            // save writes \xHH only for control bytes without a short escape.
            const bool short_escape = code == 0x0AU || code == 0x09U || code == 0x0DU;
            const bool control = code < 0x20U || code == 0x7FU;
            if (!control || short_escape) return fail(GraphLoadError::non_canonical, start);
            decoded = static_cast<char>(code);
            offset_ = start + 4U;
            return GraphLoadError::ok;
        }
        default: return fail(GraphLoadError::bad_text, start);
        }
        offset_ = start + 2U;
        return GraphLoadError::ok;
    }

    std::string_view text_{};
    std::size_t offset_{};
    std::size_t failure_{};
};

// A word from `words`, written as its index.
GraphLoadError read_word(DocumentReader& reader, std::span<const std::string_view> words, std::size_t& out) {
    const std::size_t start = reader.offset();
    std::string_view text{};
    const GraphLoadError read = reader.token(text, GraphLoadError::bad_word);
    if (read != GraphLoadError::ok) return read;
    for (std::size_t index = 0U; index < words.size(); ++index) {
        if (words[index] != text) continue;
        out = index;
        return GraphLoadError::ok;
    }
    return reader.fail(GraphLoadError::bad_word, start);
}

constexpr std::array<std::string_view, 2> direction_words{"input", "output"};
constexpr std::array<PortDirection, 2> directions{PortDirection::input, PortDirection::output};
constexpr std::array<std::string_view, 5> side_words{"left", "right", "top", "bottom", "automatic"};
constexpr std::array<PortSide, 5> sides{PortSide::left, PortSide::right, PortSide::top, PortSide::bottom,
                                        PortSide::automatic};

// "port: " has been read.
GraphLoadError read_port(DocumentReader& reader, PortRecord& port) {
    GraphLoadError error = GraphLoadError::ok;
    std::size_t direction = 0U;
    std::size_t side = 0U;
    std::uint32_t socket = 0U;
    error = read_word(reader, direction_words, direction);
    if (error == GraphLoadError::ok) error = reader.literal(" ", GraphLoadError::syntax);
    if (error == GraphLoadError::ok) error = read_word(reader, side_words, side);
    if (error == GraphLoadError::ok) error = reader.literal(" ", GraphLoadError::syntax);
    if (error == GraphLoadError::ok) error = reader.real(port.offset, NumberRange::unit);
    if (error == GraphLoadError::ok) error = reader.literal(" ", GraphLoadError::syntax);
    if (error == GraphLoadError::ok) error = reader.whole(socket, 65535U);
    if (error == GraphLoadError::ok) error = reader.literal(" ", GraphLoadError::syntax);
    if (error == GraphLoadError::ok) error = reader.quoted(port.name);
    if (error == GraphLoadError::ok) error = reader.literal(";\n", GraphLoadError::syntax);
    if (error != GraphLoadError::ok) return error;
    port.direction = directions[direction];
    port.side = sides[side];
    port.socket_type = static_cast<std::uint16_t>(socket);
    return GraphLoadError::ok;
}

// "    " has been read; a property line follows.
GraphLoadError read_property(DocumentReader& reader, std::vector<NodeProperty>& properties) {
    const std::size_t start = reader.offset();
    std::string_view name{};
    GraphLoadError error = reader.key(name);
    if (error == GraphLoadError::bad_key) return reader.fail(GraphLoadError::bad_property, start);
    if (error != GraphLoadError::ok) return error;
    if (!node_graph_text::valid_property_name(name)) return reader.fail(GraphLoadError::bad_property, start);
    for (const NodeProperty& earlier : properties) {
        if (std::string_view(earlier.name) == name) return reader.fail(GraphLoadError::bad_property, start);
    }
    NodeProperty property{};
    property.name.assign(name);
    error = reader.literal(": ", GraphLoadError::syntax);
    if (error == GraphLoadError::ok) error = reader.quoted(property.value);
    if (error == GraphLoadError::ok) error = reader.literal(";\n", GraphLoadError::syntax);
    if (error != GraphLoadError::ok) return error;
    properties.push_back(std::move(property));
    return GraphLoadError::ok;
}

// "node " has been read.
GraphLoadError read_node(DocumentReader& reader, NodeRecord& node) {
    GraphLoadError error = GraphLoadError::ok;
    std::string_view key{};
    error = reader.key(key);
    if (error != GraphLoadError::ok) return error;
    node.key.assign(key);
    error = reader.literal(" {\n    kind: ", GraphLoadError::syntax);
    if (error == GraphLoadError::ok) error = reader.quoted(node.kind);
    if (error == GraphLoadError::ok) error = reader.literal(";\n    title: ", GraphLoadError::syntax);
    if (error == GraphLoadError::ok) error = reader.quoted(node.title);
    if (error == GraphLoadError::ok) error = reader.literal(";\n    bounds: ", GraphLoadError::syntax);
    if (error == GraphLoadError::ok) error = reader.real(node.bounds.x, NumberRange::any);
    if (error == GraphLoadError::ok) error = reader.literal(" ", GraphLoadError::syntax);
    if (error == GraphLoadError::ok) error = reader.real(node.bounds.y, NumberRange::any);
    if (error == GraphLoadError::ok) error = reader.literal(" ", GraphLoadError::syntax);
    if (error == GraphLoadError::ok) error = reader.real(node.bounds.width, NumberRange::non_negative);
    if (error == GraphLoadError::ok) error = reader.literal(" ", GraphLoadError::syntax);
    if (error == GraphLoadError::ok) error = reader.real(node.bounds.height, NumberRange::non_negative);
    if (error == GraphLoadError::ok) error = reader.literal(";\n", GraphLoadError::syntax);
    if (error != GraphLoadError::ok) return error;
    // Ports, then properties, then the closing brace. Each line is bounded,
    // and the counts are checked before each line is read.
    while (true) {
        if (reader.at_end()) return reader.fail(GraphLoadError::truncated, reader.offset());
        if (reader.peek() == '}') return reader.literal("}\n", GraphLoadError::syntax);
        const std::size_t line = reader.offset();
        error = reader.literal("    ", GraphLoadError::syntax);
        if (error != GraphLoadError::ok) return error;
        // A line cut inside "port: " is truncated, not a property named
        // "port".
        if (reader.cut_inside("port: ")) return reader.literal("port: ", GraphLoadError::syntax);
        if (reader.starts_with("port: ")) {
            if (!node.properties.empty()) return reader.fail(GraphLoadError::syntax, reader.offset());
            if (node.ports.size() >= maximum_node_ports) return reader.fail(GraphLoadError::limit, line);
            error = reader.literal("port: ", GraphLoadError::syntax);
            PortRecord port{};
            if (error == GraphLoadError::ok) error = read_port(reader, port);
            if (error != GraphLoadError::ok) return error;
            node.ports.push_back(std::move(port));
            continue;
        }
        if (node.properties.size() >= maximum_node_properties) return reader.fail(GraphLoadError::limit, line);
        error = read_property(reader, node.properties);
        if (error != GraphLoadError::ok) return error;
    }
}

// "link " has been read.
GraphLoadError read_link(DocumentReader& reader, ParsedLink& link) {
    GraphLoadError error = GraphLoadError::ok;
    link.from_key_offset = reader.offset();
    error = reader.key(link.from_key);
    if (error == GraphLoadError::ok) error = reader.literal(":", GraphLoadError::syntax);
    link.from_port_offset = reader.offset();
    if (error == GraphLoadError::ok) error = reader.whole(link.from_port, 65535U);
    if (error == GraphLoadError::ok) error = reader.literal(" -> ", GraphLoadError::syntax);
    link.to_key_offset = reader.offset();
    if (error == GraphLoadError::ok) error = reader.key(link.to_key);
    if (error == GraphLoadError::ok) error = reader.literal(":", GraphLoadError::syntax);
    link.to_port_offset = reader.offset();
    if (error == GraphLoadError::ok) error = reader.whole(link.to_port, 65535U);
    if (error == GraphLoadError::ok) error = reader.literal(";\n", GraphLoadError::syntax);
    return error;
}

// How many lines start with `statement` (an upper bound for one reserve).
std::size_t count_statements(std::string_view text, std::string_view statement, std::size_t cap) noexcept {
    std::size_t count = 0U;
    std::size_t at = 0U;
    while (count < cap) {
        const std::size_t found = text.find(statement, at);
        if (found == std::string_view::npos) break;
        if (found == 0U || text[found - 1U] == '\n') ++count;
        at = found + 1U;
    }
    return count;
}

GraphLoadError read_document(std::string_view text, DocumentReader& reader, ParsedDocument& parsed) {
    GraphLoadError error = reader.literal(document_header, GraphLoadError::bad_header);
    if (error != GraphLoadError::ok) return error;
    const std::size_t version_offset = reader.offset();
    std::uint32_t version = 0U;
    error = reader.whole(version, 0xFFFFFFFFU);
    if (error != GraphLoadError::ok) return error;
    if (version != graph_document_version) return reader.fail(GraphLoadError::unsupported_version, version_offset);
    error = reader.literal(";\n", GraphLoadError::syntax);
    if (error != GraphLoadError::ok) return error;
    // One reserve for the whole parse (the counts are upper bounds: a
    // "node " inside a text at a line start is counted too).
    const std::size_t nodes = count_statements(text, "node ", maximum_document_nodes);
    const std::size_t links = count_statements(text, "link ", maximum_document_links);
    parsed.nodes.reserve(nodes);
    parsed.key_offsets.reserve(nodes);
    parsed.links.reserve(links);
    while (true) {
        const std::size_t start = reader.offset();
        if (reader.starts_with("node ")) {
            if (!parsed.links.empty()) return reader.fail(GraphLoadError::unknown_statement, start);
            if (parsed.nodes.size() >= maximum_document_nodes) return reader.fail(GraphLoadError::limit, start);
            error = reader.literal("node ", GraphLoadError::syntax);
            parsed.nodes.emplace_back();
            parsed.key_offsets.push_back(reader.offset());
            if (error == GraphLoadError::ok) error = read_node(reader, parsed.nodes.back());
            if (error != GraphLoadError::ok) return error;
            continue;
        }
        if (reader.starts_with("link ")) {
            if (parsed.links.size() >= maximum_document_links) return reader.fail(GraphLoadError::limit, start);
            error = reader.literal("link ", GraphLoadError::syntax);
            ParsedLink link{};
            if (error == GraphLoadError::ok) error = read_link(reader, link);
            if (error != GraphLoadError::ok) return error;
            parsed.links.push_back(link);
            continue;
        }
        if (reader.starts_with("end")) {
            // A wrong byte after "end" is named where it is ("end;\r\n" at
            // the carriage return).
            error = reader.literal("end;\n", GraphLoadError::syntax);
            if (error != GraphLoadError::ok) return error;
            break;
        }
        const bool cut = reader.cut_inside("node ") || reader.cut_inside("link ") || reader.cut_inside("end;\n");
        if (cut) return reader.fail(GraphLoadError::truncated, text.size());
        return reader.mismatch_at(start, GraphLoadError::unknown_statement);
    }
    if (!reader.at_end()) return reader.fail(GraphLoadError::syntax, reader.offset());
    return error;
}

// Orders node indices of a parsed document by key.
struct ParsedKeyBefore final {
    const std::vector<NodeRecord>* nodes{};
    bool operator()(std::uint32_t left, std::uint32_t right) const noexcept {
        const bool before = (*nodes)[left].key < (*nodes)[right].key;
        return before;
    }
    bool operator()(std::uint32_t node, std::string_view key) const noexcept {
        const bool before = std::string_view((*nodes)[node].key) < key;
        return before;
    }
};

// The node with this key, or `count` when there is none.
std::uint32_t parsed_node(const ParsedDocument& parsed, std::string_view key) noexcept {
    const ParsedKeyBefore before{&parsed.nodes};
    const std::vector<std::uint32_t>::const_iterator found =
        std::lower_bound(parsed.order.begin(), parsed.order.end(), key, before);
    const std::uint32_t none = static_cast<std::uint32_t>(parsed.nodes.size());
    if (found == parsed.order.end()) return none;
    if (std::string_view(parsed.nodes[*found].key) != key) return none;
    return *found;
}

// Keys and links, checked as add_node and connect check them. The first
// duplicate key in document order is reported before any link error; links
// are then checked in document order.
GraphLoadError resolve_document(DocumentReader& reader, ParsedDocument& parsed) {
    const std::size_t count = parsed.nodes.size();
    parsed.order.resize(count);
    for (std::size_t index = 0U; index < count; ++index) parsed.order[index] = static_cast<std::uint32_t>(index);
    const ParsedKeyBefore before{&parsed.nodes};
    std::stable_sort(parsed.order.begin(), parsed.order.end(), before);
    // Equal keys keep document order, so the second of each equal pair is a
    // repeat; the earliest repeat is reported.
    std::size_t repeat = 0U;
    bool repeated = false;
    for (std::size_t index = 1U; index < count; ++index) {
        const NodeRecord& earlier = parsed.nodes[parsed.order[index - 1U]];
        const NodeRecord& later = parsed.nodes[parsed.order[index]];
        if (earlier.key != later.key) continue;
        const std::size_t offset = parsed.key_offsets[parsed.order[index]];
        if (!repeated || offset < repeat) repeat = offset;
        repeated = true;
    }
    if (repeated) return reader.fail(GraphLoadError::duplicate_key, repeat);

    // One occupancy flag per input port, flat: port_base[node] is the node's
    // first flag.
    std::vector<std::size_t> port_base(count + 1U, 0U);
    for (std::size_t index = 0U; index < count; ++index) {
        port_base[index + 1U] = port_base[index] + parsed.nodes[index].ports.size();
    }
    std::vector<std::uint8_t> occupied(port_base[count], 0U);
    const std::uint32_t none = static_cast<std::uint32_t>(count);
    for (ParsedLink& link : parsed.links) {
        link.from_node = parsed_node(parsed, link.from_key);
        if (link.from_node == none) return reader.fail(GraphLoadError::unknown_node, link.from_key_offset);
        link.to_node = parsed_node(parsed, link.to_key);
        if (link.to_node == none) return reader.fail(GraphLoadError::unknown_node, link.to_key_offset);
        const NodeRecord& source = parsed.nodes[link.from_node];
        const NodeRecord& target = parsed.nodes[link.to_node];
        if (link.from_port >= source.ports.size()) return reader.fail(GraphLoadError::bad_port, link.from_port_offset);
        if (link.to_port >= target.ports.size()) return reader.fail(GraphLoadError::bad_port, link.to_port_offset);
        if (link.from_node == link.to_node) return reader.fail(GraphLoadError::same_node, link.to_key_offset);
        if (source.ports[link.from_port].direction != PortDirection::output) {
            return reader.fail(GraphLoadError::wrong_direction, link.from_key_offset);
        }
        if (target.ports[link.to_port].direction != PortDirection::input) {
            return reader.fail(GraphLoadError::wrong_direction, link.to_key_offset);
        }
        std::uint8_t& taken = occupied[port_base[link.to_node] + link.to_port];
        if (taken != 0U) return reader.fail(GraphLoadError::occupied, link.to_key_offset);
        taken = 1U;
    }
    return GraphLoadError::ok;
}

// The line and column (both from 1) of a byte offset.
void locate(std::string_view text, GraphLoadResult& result) noexcept {
    const std::size_t end = std::min(result.offset, text.size());
    std::size_t line = 1U;
    std::size_t line_start = 0U;
    for (std::size_t index = 0U; index < end; ++index) {
        if (text[index] != '\n') continue;
        ++line;
        line_start = index + 1U;
    }
    result.line = line;
    result.column = result.offset - line_start + 1U;
}

std::uint32_t newer_generation(std::uint32_t generation) noexcept {
    std::uint32_t next = generation + 1U;
    if (next == 0U) next = 1U;
    return next;
}

} // namespace

std::string_view graph_load_error_name(GraphLoadError error) noexcept {
    switch (error) {
    case GraphLoadError::ok: return "ok";
    case GraphLoadError::undo_group_open: return "undo_group_open";
    case GraphLoadError::bad_header: return "bad_header";
    case GraphLoadError::unsupported_version: return "unsupported_version";
    case GraphLoadError::truncated: return "truncated";
    case GraphLoadError::unexpected_character: return "unexpected_character";
    case GraphLoadError::syntax: return "syntax";
    case GraphLoadError::unknown_statement: return "unknown_statement";
    case GraphLoadError::bad_key: return "bad_key";
    case GraphLoadError::bad_number: return "bad_number";
    case GraphLoadError::non_canonical: return "non_canonical";
    case GraphLoadError::bad_text: return "bad_text";
    case GraphLoadError::bad_word: return "bad_word";
    case GraphLoadError::bad_property: return "bad_property";
    case GraphLoadError::duplicate_key: return "duplicate_key";
    case GraphLoadError::unknown_node: return "unknown_node";
    case GraphLoadError::bad_port: return "bad_port";
    case GraphLoadError::wrong_direction: return "wrong_direction";
    case GraphLoadError::same_node: return "same_node";
    case GraphLoadError::occupied: return "occupied";
    case GraphLoadError::limit: return "limit";
    }
    return "none";
}

GraphSaveResult NodeGraph::save(std::span<char> storage) const {
    GraphSaveResult result{};
    const std::span<const NodeRecord> nodes = node_slots();
    const std::span<const LinkRecord> links = link_slots();
    DocumentWriter measure(nullptr);
    write_document(nodes, links, measure);
    result.required = measure.length();
    if (storage.size() < result.required) {
        result.status = GraphSaveStatus::storage_too_small;
        return result;
    }
    DocumentWriter writer(storage.data());
    write_document(nodes, links, writer);
    result.written = writer.length();
    return result;
}

GraphLoadResult NodeGraph::load(std::string_view document) {
    GraphLoadResult result{};
    if (group_depth_ > 0U) {
        result.error = GraphLoadError::undo_group_open;
        return result;
    }
    // Phases 1 and 2: work storage only.
    DocumentReader reader(document);
    ParsedDocument parsed{};
    GraphLoadError error = read_document(document, reader, parsed);
    if (error == GraphLoadError::ok) error = resolve_document(reader, parsed);
    if (error != GraphLoadError::ok) {
        result.error = error;
        result.offset = reader.failure();
        locate(document, result);
        return result;
    }

    // Phase 3: reserve every array to its final size (slots kept from before
    // stay, freed, so their generations keep counting), then replace.
    const std::size_t node_count = parsed.nodes.size();
    const std::size_t link_count = parsed.links.size();
    const std::size_t node_total = std::max(nodes_.size(), node_count);
    const std::size_t link_total = std::max(links_.size(), link_count);
    nodes_.reserve(node_total);
    free_nodes_.reserve(node_total);
    move_marks_.reserve(node_total);
    key_order_.reserve(node_total);
    links_.reserve(link_total);
    free_links_.reserve(link_total);

    // Nothing below allocates or fails.
    clear_log();
    for (std::size_t index = 0U; index < node_count; ++index) {
        std::uint32_t generation = 1U;
        if (index < nodes_.size()) {
            generation = newer_generation(nodes_[index].generation);
            nodes_[index] = std::move(parsed.nodes[index]);
        } else {
            nodes_.push_back(std::move(parsed.nodes[index]));
        }
        nodes_[index].generation = generation;
        nodes_[index].live = true;
    }
    for (std::size_t index = node_count; index < nodes_.size(); ++index) {
        const std::uint32_t generation = nodes_[index].generation;
        nodes_[index] = NodeRecord{};
        nodes_[index].generation = generation;
    }
    // Free slots reused lowest first: the free list's newest entry is the
    // lowest free slot.
    free_nodes_.clear();
    for (std::size_t index = nodes_.size(); index > node_count; --index) {
        free_nodes_.push_back(static_cast<std::uint32_t>(index - 1U));
    }
    move_marks_.assign(nodes_.size(), 0U);
    key_order_.assign(parsed.order.begin(), parsed.order.end());

    for (std::size_t index = 0U; index < link_count; ++index) {
        const ParsedLink& parsed_link = parsed.links[index];
        LinkRecord link{};
        link.from = PortRef{NodeId{parsed_link.from_node, nodes_[parsed_link.from_node].generation},
                            PortIndex{static_cast<std::uint16_t>(parsed_link.from_port)}};
        link.to = PortRef{NodeId{parsed_link.to_node, nodes_[parsed_link.to_node].generation},
                          PortIndex{static_cast<std::uint16_t>(parsed_link.to_port)}};
        link.live = true;
        link.generation = 1U;
        if (index < links_.size()) {
            link.generation = newer_generation(links_[index].generation);
            links_[index] = link;
        } else {
            links_.push_back(link);
        }
    }
    for (std::size_t index = link_count; index < links_.size(); ++index) links_[index].live = false;
    free_links_.clear();
    for (std::size_t index = links_.size(); index > link_count; --index) {
        free_links_.push_back(static_cast<std::uint32_t>(index - 1U));
    }
    live_nodes_ = node_count;
    live_links_ = link_count;
    advance();
    return result;
}

} // namespace gui_forms
