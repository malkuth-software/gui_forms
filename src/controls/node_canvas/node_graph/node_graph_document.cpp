#include "gui_forms/controls/node_graph.hpp"

#include "node_graph_room.hpp"
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

// The node graph's document, version 1 (format, tolerated variants and the
// round-trip guarantee in node_graph.hpp, above NodeGraph::save). save
// measures, then writes the canonical form into the caller's storage. load
// runs in three phases:
//   1. read: one pass over the bytes, line by line, into a ParsedDocument
//      (node records in document order, links by key text), refusing the
//      first wrong byte; spaces, tabs, blank lines, comments and CRLF are
//      skipped between tokens; the parse is a loop, never recursive;
//   2. resolve: node keys and link IDs sorted once (duplicates), links
//      looked up by key and checked as connect checks them, port occupancy
//      in one flat array;
//   3. install: storage reserved, then the graph's arrays replaced by moves
//      that cannot fail.
// Phases 1 and 2 touch only work storage, bounded by the document's size and
// by maximum_document_nodes and maximum_document_links; a refusal at either
// leaves the graph and its undo log untouched.

namespace gui_forms {
namespace {

using node_graph_room::require_room;

constexpr std::string_view header_word = "gui-forms-graph";

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

    // A quoted text with the canonical escapes.
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
    writer.bytes(header_word);
    writer.byte(' ');
    writer.whole(graph_document_version);
    writer.bytes(";\n");
    for (const NodeRecord& node : nodes) {
        if (node.live) write_node(node, writer);
    }
    for (const LinkRecord& link : links) {
        if (!link.live) continue;
        writer.bytes("link ");
        writer.bytes(link.key);
        writer.byte(' ');
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

// A link as read: its ID (owned, moved into the graph at install), its ends
// by key text (views of the document) and port index, and the byte offsets
// errors name; resolve fills the node slots.
struct ParsedLink final {
    std::string id{};
    std::string_view from_key{};
    std::string_view to_key{};
    std::uint32_t from_port{};
    std::uint32_t to_port{};
    std::size_t id_offset{};
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
    // Node indices ordered by key and link indices ordered by ID (resolve
    // fills them; install keeps them).
    std::vector<std::uint32_t> order{};
    std::vector<std::uint32_t> link_order{};
};

bool space_byte(char byte) noexcept { return byte == ' ' || byte == '\t'; }

// A cursor over the document. Each read skips the spaces before its token,
// then either advances past what it read and returns ok, or records the
// failing byte's offset and returns the error.
class DocumentReader final {
public:
    explicit DocumentReader(std::string_view text) noexcept : text_(text) {}

    [[nodiscard]] std::size_t offset() const noexcept { return offset_; }
    [[nodiscard]] std::size_t failure() const noexcept { return failure_; }
    [[nodiscard]] bool at_end() const noexcept { return offset_ >= text_.size(); }
    [[nodiscard]] char peek() const noexcept { return text_[offset_]; }

    void skip_space() noexcept {
        while (offset_ < text_.size() && space_byte(text_[offset_])) ++offset_;
    }

    GraphLoadError fail(GraphLoadError error, std::size_t at) noexcept {
        failure_ = at;
        return error;
    }

    // `mismatch` names a wrong byte the format uses (a line end included);
    // a byte it never uses outside texts and comments is
    // unexpected_character, and a carriage return ending the input is a cut
    // line end (truncated).
    GraphLoadError mismatch_at(std::size_t at, GraphLoadError mismatch) noexcept {
        const char byte = text_[at];
        if (byte == '\r' && at + 1U == text_.size()) return fail(GraphLoadError::truncated, text_.size());
        const unsigned char code = static_cast<unsigned char>(byte);
        const bool printable = code >= 0x20U && code <= 0x7EU;
        const bool line_end = byte == '\n' || (byte == '\r' && at + 1U < text_.size() && text_[at + 1U] == '\n');
        const bool known = printable || byte == '\t' || line_end;
        return fail(known ? mismatch : GraphLoadError::unexpected_character, at);
    }

    // Spaces, an optional comment, and one line end ("\n" or "\r\n").
    GraphLoadError line_end() noexcept {
        skip_space();
        if (offset_ < text_.size() && text_[offset_] == '#') {
            while (offset_ < text_.size() && text_[offset_] != '\n') ++offset_;
            // A comment's "\r\n" ends it: step back onto the carriage return.
            if (offset_ < text_.size() && text_[offset_ - 1U] == '\r') --offset_;
        }
        if (offset_ >= text_.size()) return fail(GraphLoadError::truncated, text_.size());
        if (text_[offset_] == '\n') {
            ++offset_;
            return GraphLoadError::ok;
        }
        if (text_[offset_] == '\r' && offset_ + 1U < text_.size() && text_[offset_ + 1U] == '\n') {
            offset_ += 2U;
            return GraphLoadError::ok;
        }
        return mismatch_at(offset_, GraphLoadError::syntax);
    }

    // Blank and comment-only lines, then the spaces starting the next line.
    // Stops at the input's end without an error.
    GraphLoadError blank_lines() noexcept {
        while (true) {
            skip_space();
            if (offset_ >= text_.size()) return GraphLoadError::ok;
            const char byte = text_[offset_];
            if (byte != '#' && byte != '\n' && byte != '\r') return GraphLoadError::ok;
            const GraphLoadError ended = line_end();
            if (ended == GraphLoadError::truncated) {
                // A comment or line end cut by the input's end.
                offset_ = text_.size();
                return GraphLoadError::ok;
            }
            if (ended != GraphLoadError::ok) return ended;
        }
    }

    GraphLoadError punctuation(std::string_view expected, GraphLoadError mismatch) noexcept {
        skip_space();
        for (std::size_t index = 0U; index < expected.size(); ++index) {
            const std::size_t at = offset_ + index;
            if (at >= text_.size()) return fail(GraphLoadError::truncated, text_.size());
            if (text_[at] != expected[index]) return mismatch_at(at, mismatch);
        }
        offset_ += expected.size();
        return GraphLoadError::ok;
    }

    // A run of key bytes (keywords, keys, IDs, names, words); `empty` names
    // a missing run.
    GraphLoadError word(std::string_view& out, GraphLoadError empty) noexcept {
        skip_space();
        const std::size_t start = offset_;
        std::size_t at = start;
        while (at < text_.size() && node_graph_text::key_byte(text_[at])) ++at;
        if (at >= text_.size()) return fail(GraphLoadError::truncated, text_.size());
        if (at == start) return mismatch_at(start, empty);
        out = text_.substr(start, at - start);
        offset_ = at;
        return GraphLoadError::ok;
    }

    // A node key or link ID: a word of at most maximum_graph_key_bytes.
    GraphLoadError key(std::string_view& out) noexcept {
        skip_space();
        const std::size_t start = offset_;
        const GraphLoadError read = word(out, GraphLoadError::bad_key);
        if (read != GraphLoadError::ok) return read;
        if (out.size() > maximum_graph_key_bytes) return fail(GraphLoadError::bad_key, start);
        return GraphLoadError::ok;
    }

    // The bytes up to the next space, semicolon, comment or line end.
    GraphLoadError token(std::string_view& out, GraphLoadError empty) noexcept {
        skip_space();
        const std::size_t start = offset_;
        std::size_t at = start;
        while (at < text_.size()) {
            const char byte = text_[at];
            if (space_byte(byte) || byte == ';' || byte == '#' || byte == '\n' || byte == '\r') break;
            const unsigned char code = static_cast<unsigned char>(byte);
            if (code < 0x20U || code > 0x7EU) return fail(GraphLoadError::unexpected_character, at);
            ++at;
        }
        if (at >= text_.size()) return fail(GraphLoadError::truncated, text_.size());
        if (at == start) return mismatch_at(start, empty);
        out = text_.substr(start, at - start);
        offset_ = at;
        return GraphLoadError::ok;
    }

    // Any std::from_chars spelling of a finite double in `range`.
    GraphLoadError real(double& out, NumberRange range) noexcept {
        skip_space();
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
        out = value;
        return GraphLoadError::ok;
    }

    // A decimal integer up to `maximum` (leading zeros allowed).
    GraphLoadError whole(std::uint32_t& out, std::uint32_t maximum) noexcept {
        skip_space();
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
        out = value;
        return GraphLoadError::ok;
    }

    // A quoted text, decoded into `out` (at most maximum_graph_text_bytes).
    // May throw std::bad_alloc while `out` grows.
    GraphLoadError quoted(std::string& out) {
        skip_space();
        const std::size_t start = offset_;
        const GraphLoadError opened = punctuation("\"", GraphLoadError::syntax);
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
    // One escape at offset_ (a backslash): \\, \", \n, \t, \r, or \xHH in
    // either case for any byte.
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
                    value = static_cast<std::uint32_t>(hex - 'A') + 10U;
                } else {
                    return fail(GraphLoadError::bad_text, start);
                }
                code = code * 16U + value;
            }
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
GraphLoadError read_choice(DocumentReader& reader, std::span<const std::string_view> words, std::size_t& out) {
    reader.skip_space();
    const std::size_t start = reader.offset();
    std::string_view text{};
    const GraphLoadError read = reader.word(text, GraphLoadError::bad_word);
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

// The end of a statement or field line: its semicolon and line end.
GraphLoadError field_end(DocumentReader& reader) {
    GraphLoadError error = reader.punctuation(";", GraphLoadError::syntax);
    if (error == GraphLoadError::ok) error = reader.line_end();
    return error;
}

// "port" has been read.
GraphLoadError read_port(DocumentReader& reader, PortRecord& port) {
    GraphLoadError error = reader.punctuation(":", GraphLoadError::syntax);
    std::size_t direction = 0U;
    std::size_t side = 0U;
    std::uint32_t socket = 0U;
    if (error == GraphLoadError::ok) error = read_choice(reader, direction_words, direction);
    if (error == GraphLoadError::ok) error = read_choice(reader, side_words, side);
    if (error == GraphLoadError::ok) error = reader.real(port.offset, NumberRange::unit);
    if (error == GraphLoadError::ok) error = reader.whole(socket, 65535U);
    if (error == GraphLoadError::ok) error = reader.quoted(port.name);
    if (error == GraphLoadError::ok) error = field_end(reader);
    if (error != GraphLoadError::ok) return error;
    port.direction = directions[direction];
    port.side = sides[side];
    port.socket_type = static_cast<std::uint16_t>(socket);
    return GraphLoadError::ok;
}

// A property's name (starting at `start`) has been read.
GraphLoadError read_property(DocumentReader& reader, std::string_view name, std::size_t start,
                             std::vector<NodeProperty>& properties) {
    if (!node_graph_text::valid_property_name(name)) return reader.fail(GraphLoadError::bad_property, start);
    for (const NodeProperty& earlier : properties) {
        if (std::string_view(earlier.name) == name) return reader.fail(GraphLoadError::bad_property, start);
    }
    NodeProperty property{};
    property.name.assign(name);
    GraphLoadError error = reader.punctuation(":", GraphLoadError::syntax);
    if (error == GraphLoadError::ok) error = reader.quoted(property.value);
    if (error == GraphLoadError::ok) error = field_end(reader);
    if (error != GraphLoadError::ok) return error;
    properties.push_back(std::move(property));
    return GraphLoadError::ok;
}

// The fields a node block expects next: kind, title and bounds in order,
// then ports and properties.
enum class NodeStage : std::uint8_t { kind, title, bounds, rest };

// "node" has been read; `key_offset` receives the key's offset.
GraphLoadError read_node(DocumentReader& reader, NodeRecord& node, std::size_t& key_offset) {
    reader.skip_space();
    key_offset = reader.offset();
    std::string_view key{};
    GraphLoadError error = reader.key(key);
    if (error != GraphLoadError::ok) return error;
    node.key.assign(key);
    error = reader.punctuation("{", GraphLoadError::syntax);
    if (error == GraphLoadError::ok) error = reader.line_end();
    if (error != GraphLoadError::ok) return error;
    NodeStage stage = NodeStage::kind;
    // One field per line until the closing brace. Each line is bounded, and
    // the counts are checked before each line is read. `line` is the
    // line's first token, where its structural errors are reported.
    while (true) {
        error = reader.blank_lines();
        if (error != GraphLoadError::ok) return error;
        if (reader.at_end()) return reader.fail(GraphLoadError::truncated, reader.offset());
        const std::size_t line = reader.offset();
        if (reader.peek() == '}') {
            if (stage != NodeStage::rest) return reader.fail(GraphLoadError::syntax, line);
            error = reader.punctuation("}", GraphLoadError::syntax);
            if (error == GraphLoadError::ok) error = reader.line_end();
            return error;
        }
        std::string_view name{};
        error = reader.word(name, GraphLoadError::syntax);
        if (error != GraphLoadError::ok) return error;
        switch (stage) {
        case NodeStage::kind:
            if (name != "kind") return reader.fail(GraphLoadError::syntax, line);
            error = reader.punctuation(":", GraphLoadError::syntax);
            if (error == GraphLoadError::ok) error = reader.quoted(node.kind);
            if (error == GraphLoadError::ok) error = field_end(reader);
            stage = NodeStage::title;
            break;
        case NodeStage::title:
            if (name != "title") return reader.fail(GraphLoadError::syntax, line);
            error = reader.punctuation(":", GraphLoadError::syntax);
            if (error == GraphLoadError::ok) error = reader.quoted(node.title);
            if (error == GraphLoadError::ok) error = field_end(reader);
            stage = NodeStage::bounds;
            break;
        case NodeStage::bounds:
            if (name != "bounds") return reader.fail(GraphLoadError::syntax, line);
            error = reader.punctuation(":", GraphLoadError::syntax);
            if (error == GraphLoadError::ok) error = reader.real(node.bounds.x, NumberRange::any);
            if (error == GraphLoadError::ok) error = reader.real(node.bounds.y, NumberRange::any);
            if (error == GraphLoadError::ok) error = reader.real(node.bounds.width, NumberRange::non_negative);
            if (error == GraphLoadError::ok) error = reader.real(node.bounds.height, NumberRange::non_negative);
            if (error == GraphLoadError::ok) error = field_end(reader);
            stage = NodeStage::rest;
            break;
        case NodeStage::rest:
        default:
            if (name == "port") {
                if (!node.properties.empty()) return reader.fail(GraphLoadError::syntax, line);
                if (node.ports.size() >= maximum_node_ports) return reader.fail(GraphLoadError::limit, line);
                PortRecord port{};
                error = read_port(reader, port);
                if (error == GraphLoadError::ok) node.ports.push_back(std::move(port));
                break;
            }
            if (node.properties.size() >= maximum_node_properties) return reader.fail(GraphLoadError::limit, line);
            error = read_property(reader, name, line, node.properties);
            break;
        }
        if (error != GraphLoadError::ok) return error;
    }
}

// One link end, "<key>:<port>", recording each part's offset.
GraphLoadError read_end(DocumentReader& reader, std::string_view& key, std::size_t& key_offset,
                        std::uint32_t& port, std::size_t& port_offset) {
    reader.skip_space();
    key_offset = reader.offset();
    GraphLoadError error = reader.key(key);
    if (error == GraphLoadError::ok) error = reader.punctuation(":", GraphLoadError::syntax);
    reader.skip_space();
    port_offset = reader.offset();
    if (error == GraphLoadError::ok) error = reader.whole(port, 65535U);
    return error;
}

// "link" has been read. May throw std::bad_alloc copying the ID.
GraphLoadError read_link(DocumentReader& reader, ParsedLink& link) {
    reader.skip_space();
    link.id_offset = reader.offset();
    std::string_view id{};
    GraphLoadError error = reader.key(id);
    if (error != GraphLoadError::ok) return error;
    link.id.assign(id);
    error = read_end(reader, link.from_key, link.from_key_offset, link.from_port, link.from_port_offset);
    if (error == GraphLoadError::ok) error = reader.punctuation("->", GraphLoadError::syntax);
    if (error == GraphLoadError::ok) {
        error = read_end(reader, link.to_key, link.to_key_offset, link.to_port, link.to_port_offset);
    }
    if (error == GraphLoadError::ok) error = field_end(reader);
    return error;
}

// How often `word` occurs: an upper bound on its statements, for one
// reserve (at most a quarter of the document's bytes).
std::size_t count_occurrences(std::string_view text, std::string_view word, std::size_t cap) noexcept {
    std::size_t count = 0U;
    std::size_t at = 0U;
    while (count < cap) {
        const std::size_t found = text.find(word, at);
        if (found == std::string_view::npos) break;
        ++count;
        at = found + word.size();
    }
    return count;
}

// After "end": its semicolon, then only spaces, comments and blank lines;
// the input may end without a line end.
GraphLoadError read_end_statement(DocumentReader& reader) {
    GraphLoadError error = reader.punctuation(";", GraphLoadError::syntax);
    if (error != GraphLoadError::ok) return error;
    reader.skip_space();
    if (reader.at_end()) return GraphLoadError::ok;
    if (reader.peek() == '#') {
        error = reader.blank_lines();
    } else {
        error = reader.line_end();
        if (error == GraphLoadError::truncated) return GraphLoadError::ok;
        if (error == GraphLoadError::ok) error = reader.blank_lines();
    }
    if (error != GraphLoadError::ok) return error;
    if (!reader.at_end()) return reader.fail(GraphLoadError::syntax, reader.offset());
    return GraphLoadError::ok;
}

GraphLoadError read_document(std::string_view text, DocumentReader& reader, ParsedDocument& parsed) {
    GraphLoadError error = reader.blank_lines();
    if (error != GraphLoadError::ok) return error;
    const std::size_t header_offset = reader.offset();
    std::string_view header{};
    error = reader.word(header, GraphLoadError::bad_header);
    if (error != GraphLoadError::ok) return error;
    if (header != header_word) return reader.fail(GraphLoadError::bad_header, header_offset);
    reader.skip_space();
    const std::size_t version_offset = reader.offset();
    std::uint32_t version = 0U;
    error = reader.whole(version, 0xFFFFFFFFU);
    if (error != GraphLoadError::ok) return error;
    if (version != graph_document_version) return reader.fail(GraphLoadError::unsupported_version, version_offset);
    error = field_end(reader);
    if (error != GraphLoadError::ok) return error;
    // One reserve for the whole parse.
    const std::size_t nodes = count_occurrences(text, "node", maximum_document_nodes);
    const std::size_t links = count_occurrences(text, "link", maximum_document_links);
    parsed.nodes.reserve(nodes);
    parsed.key_offsets.reserve(nodes);
    parsed.links.reserve(links);
    while (true) {
        error = reader.blank_lines();
        if (error != GraphLoadError::ok) return error;
        if (reader.at_end()) return reader.fail(GraphLoadError::truncated, reader.offset());
        const std::size_t start = reader.offset();
        std::string_view statement{};
        error = reader.word(statement, GraphLoadError::unknown_statement);
        if (error != GraphLoadError::ok) return error;
        if (statement == "node") {
            if (!parsed.links.empty()) return reader.fail(GraphLoadError::unknown_statement, start);
            if (parsed.nodes.size() >= maximum_document_nodes) return reader.fail(GraphLoadError::limit, start);
            parsed.nodes.emplace_back();
            parsed.key_offsets.push_back(0U);
            error = read_node(reader, parsed.nodes.back(), parsed.key_offsets.back());
            if (error != GraphLoadError::ok) return error;
            continue;
        }
        if (statement == "link") {
            if (parsed.links.size() >= maximum_document_links) return reader.fail(GraphLoadError::limit, start);
            parsed.links.emplace_back();
            error = read_link(reader, parsed.links.back());
            if (error != GraphLoadError::ok) return error;
            continue;
        }
        if (statement == "end") return read_end_statement(reader);
        return reader.fail(GraphLoadError::unknown_statement, start);
    }
}

// Orders parsed node indices by key (and finds a key).
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

// Orders parsed link indices by ID.
struct ParsedIdBefore final {
    const std::vector<ParsedLink>* links{};
    bool operator()(std::uint32_t left, std::uint32_t right) const noexcept {
        const bool before = (*links)[left].id < (*links)[right].id;
        return before;
    }
};

// The node with this key, or the node count when there is none.
std::uint32_t parsed_node(const ParsedDocument& parsed, std::string_view key) noexcept {
    const ParsedKeyBefore before{&parsed.nodes};
    const std::vector<std::uint32_t>::const_iterator found =
        std::lower_bound(parsed.order.begin(), parsed.order.end(), key, before);
    const std::uint32_t none = static_cast<std::uint32_t>(parsed.nodes.size());
    if (found == parsed.order.end()) return none;
    if (std::string_view(parsed.nodes[*found].key) != key) return none;
    return *found;
}

// The smallest offset among repeats in `order` (indices stably sorted by
// text, so the second of each equal pair is the later one in the document),
// or `none`. `text` and `offset` give each index's text and byte offset.
template <typename Text, typename Offset>
std::size_t earliest_repeat(const std::vector<std::uint32_t>& order, Text text, Offset offset,
                            std::size_t none) noexcept {
    std::size_t repeat = none;
    for (std::size_t index = 1U; index < order.size(); ++index) {
        if (text(order[index - 1U]) != text(order[index])) continue;
        const std::size_t at = offset(order[index]);
        if (repeat == none || at < repeat) repeat = at;
    }
    return repeat;
}

struct NodeKeyText final {
    const ParsedDocument* parsed{};
    std::string_view operator()(std::uint32_t node) const noexcept { return (*parsed).nodes[node].key; }
};

struct NodeKeyOffset final {
    const ParsedDocument* parsed{};
    std::size_t operator()(std::uint32_t node) const noexcept { return (*parsed).key_offsets[node]; }
};

struct LinkIdText final {
    const ParsedDocument* parsed{};
    std::string_view operator()(std::uint32_t link) const noexcept { return (*parsed).links[link].id; }
};

struct LinkIdOffset final {
    const ParsedDocument* parsed{};
    std::size_t operator()(std::uint32_t link) const noexcept { return (*parsed).links[link].id_offset; }
};

// Keys, IDs and links, checked as add_node and connect check them. The
// earliest repeated node key is reported first, then the earliest repeated
// link ID, then link errors in document order.
GraphLoadError resolve_document(DocumentReader& reader, ParsedDocument& parsed) {
    const std::size_t count = parsed.nodes.size();
    const std::size_t none = static_cast<std::size_t>(-1);
    parsed.order.resize(count);
    for (std::size_t index = 0U; index < count; ++index) parsed.order[index] = static_cast<std::uint32_t>(index);
    const ParsedKeyBefore key_before{&parsed.nodes};
    std::stable_sort(parsed.order.begin(), parsed.order.end(), key_before);
    const std::size_t node_repeat = earliest_repeat(parsed.order, NodeKeyText{&parsed}, NodeKeyOffset{&parsed}, none);
    if (node_repeat != none) return reader.fail(GraphLoadError::duplicate_key, node_repeat);

    const std::size_t link_count = parsed.links.size();
    parsed.link_order.resize(link_count);
    for (std::size_t index = 0U; index < link_count; ++index) {
        parsed.link_order[index] = static_cast<std::uint32_t>(index);
    }
    const ParsedIdBefore id_before{&parsed.links};
    std::stable_sort(parsed.link_order.begin(), parsed.link_order.end(), id_before);
    const std::size_t link_repeat =
        earliest_repeat(parsed.link_order, LinkIdText{&parsed}, LinkIdOffset{&parsed}, none);
    if (link_repeat != none) return reader.fail(GraphLoadError::duplicate_key, link_repeat);

    // One occupancy flag per port, flat: port_base[node] is the node's first.
    std::vector<std::size_t> port_base(count + 1U, 0U);
    for (std::size_t index = 0U; index < count; ++index) {
        port_base[index + 1U] = port_base[index] + parsed.nodes[index].ports.size();
    }
    std::vector<std::uint8_t> occupied(port_base[count], 0U);
    const std::uint32_t missing = static_cast<std::uint32_t>(count);
    for (ParsedLink& link : parsed.links) {
        link.from_node = parsed_node(parsed, link.from_key);
        if (link.from_node == missing) return reader.fail(GraphLoadError::unknown_node, link.from_key_offset);
        link.to_node = parsed_node(parsed, link.to_key);
        if (link.to_node == missing) return reader.fail(GraphLoadError::unknown_node, link.to_key_offset);
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

// One past the largest n of the IDs "l.<n>" among `links` (n in decimal,
// leading zeros allowed, so "l.007" counts as 7), so the automatic counter
// never hands out a loaded ID again. Order and gaps do not matter.
std::uint64_t next_automatic_number(const std::vector<ParsedLink>& links) noexcept {
    std::uint64_t next = 1U;
    for (const ParsedLink& link : links) {
        const std::string_view id(link.id);
        if (id.size() < 3U || id.substr(0U, 2U) != "l.") continue;
        std::uint64_t value = 0U;
        const char* const last = id.data() + id.size();
        const std::from_chars_result parsed = std::from_chars(id.data() + 2U, last, value, 10);
        if (parsed.ec != std::errc{} || parsed.ptr != last || value == 0xFFFFFFFFFFFFFFFFULL) continue;
        next = std::max(next, value + 1U);
    }
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

    // Phase 3. Reserve sizes: the node slots, their free list, move marks and
    // key order each to the larger of the old slot count and the document's
    // node count (slots kept from before stay, freed, so their generations
    // keep counting); the link slots, their free list and ID order likewise.
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
    link_key_order_.reserve(link_total);

    // Nothing below allocates or fails; each push checks its reservation.
    require_room(nodes_, node_total - nodes_.size(), "node_graph.load");
    require_room(links_, link_total - links_.size(), "node_graph.load");
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
    require_room(free_nodes_, nodes_.size() - node_count, "node_graph.load");
    for (std::size_t index = nodes_.size(); index > node_count; --index) {
        free_nodes_.push_back(static_cast<std::uint32_t>(index - 1U));
    }
    move_marks_.clear();
    require_room(move_marks_, nodes_.size(), "node_graph.load");
    move_marks_.assign(nodes_.size(), 0U);
    key_order_.clear();
    require_room(key_order_, parsed.order.size(), "node_graph.load");
    key_order_.assign(parsed.order.begin(), parsed.order.end());

    // Read before the loop below moves the IDs out of `parsed.links`.
    const std::uint64_t loaded_next_number = next_automatic_number(parsed.links);
    for (std::size_t index = 0U; index < link_count; ++index) {
        ParsedLink& parsed_link = parsed.links[index];
        LinkRecord link{};
        link.key = std::move(parsed_link.id);
        link.from = PortRef{NodeId{parsed_link.from_node, nodes_[parsed_link.from_node].generation},
                            PortIndex{static_cast<std::uint16_t>(parsed_link.from_port)}};
        link.to = PortRef{NodeId{parsed_link.to_node, nodes_[parsed_link.to_node].generation},
                          PortIndex{static_cast<std::uint16_t>(parsed_link.to_port)}};
        link.live = true;
        link.generation = 1U;
        if (index < links_.size()) {
            link.generation = newer_generation(links_[index].generation);
            links_[index] = std::move(link);
        } else {
            links_.push_back(std::move(link));
        }
    }
    for (std::size_t index = link_count; index < links_.size(); ++index) {
        const std::uint32_t generation = links_[index].generation;
        links_[index] = LinkRecord{};
        links_[index].generation = generation;
    }
    free_links_.clear();
    require_room(free_links_, links_.size() - link_count, "node_graph.load");
    for (std::size_t index = links_.size(); index > link_count; --index) {
        free_links_.push_back(static_cast<std::uint32_t>(index - 1U));
    }
    link_key_order_.clear();
    require_room(link_key_order_, parsed.link_order.size(), "node_graph.load");
    link_key_order_.assign(parsed.link_order.begin(), parsed.link_order.end());
    next_link_number_ = std::max(next_link_number_, loaded_next_number);
    live_nodes_ = node_count;
    live_links_ = link_count;
    advance();
    return result;
}

} // namespace gui_forms
