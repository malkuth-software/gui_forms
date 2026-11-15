#include "gui_forms/xml/xml_reader/xml_reader.hpp"

#include "../xml_characters/xml_characters.hpp"
#include "xml_located_reader.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gui_forms {

namespace {

using detail::XmlDecodedCharacter;
using detail::append_xml_utf8;
using detail::decode_xml_utf8;
using detail::is_xml_character;
using detail::is_xml_name_character;
using detail::is_xml_name_start;

// The longest predefined entity names, "quot" and "apos", have four letters;
// a longer run of letters before ';' cannot name one.
constexpr std::size_t maximum_entity_name_bytes = 4;
// Leading zeros are legal in a character reference; this bounds the digits
// read for one so the scan stays short on hostile input.
constexpr std::size_t maximum_reference_digits = 16;

[[nodiscard]] bool is_space_byte(char byte) noexcept {
    const bool space = byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n';
    return space;
}

[[nodiscard]] bool is_whitespace_only(std::string_view text) noexcept {
    for (const char byte : text) {
        if (!is_space_byte(byte)) return false;
    }
    return true;
}

[[nodiscard]] char lower_ascii(char byte) noexcept {
    if (byte >= 'A' && byte <= 'Z') {
        const char lowered = static_cast<char>(byte - 'A' + 'a');
        return lowered;
    }
    return byte;
}

[[nodiscard]] bool equal_ignoring_ascii_case(std::string_view left,
                                             std::string_view right) noexcept {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (lower_ascii(left[index]) != lower_ascii(right[index])) return false;
    }
    return true;
}

// Lines and columns are computed only when a diagnostic is reported, by one
// pass over the bytes before it; a column counts the bytes that start a UTF-8
// sequence, which is the scalar count for the well-formed prefix.
[[nodiscard]] XmlSourcePosition position_of(std::string_view input, std::size_t offset) noexcept {
    std::uint32_t line = 1;
    std::uint32_t column = 1;
    std::size_t index = 0;
    while (index < offset && index < input.size()) {
        const unsigned char byte = static_cast<unsigned char>(input[index]);
        if (byte == '\n') {
            ++line;
            column = 1;
        } else if (byte == '\r') {
            ++line;
            column = 1;
            const bool pair = index + 1U < offset && input[index + 1U] == '\n';
            if (pair) ++index;
        } else if ((byte & 0xC0U) != 0x80U) {
            ++column;
        }
        ++index;
    }
    const XmlSourcePosition position{.byte_offset = offset, .line = line, .column = column};
    return position;
}

// One read of one document. The reader owns the elements it is building:
// open_ holds the element whose content is being read and its ancestors
// (back() innermost); an element is moved into its parent's children only
// when its end tag has been read, so no reference into a growing vector is
// ever retained.
class XmlDocumentReader final {
public:
    // `locations`, when given, receives where each element and attribute
    // was written (read_xml_located); it is borrowed for the read.
    XmlDocumentReader(std::string_view input, const XmlLimits& limits,
                      detail::XmlLocations* locations)
        : input_(input), limits_(limits), locations_(locations) {}

    XmlReadResult read() {
        const bool completed = read_document();
        XmlReadResult result{};
        if (!completed) {
            if (locations_ != nullptr) (*locations_).elements.clear();
            result.error = error_;
            result.position = position_of(input_, error_offset_);
            return result;
        }
        result.root = std::move(root_);
        return result;
    }

private:
    bool fail(XmlError error, std::size_t offset) {
        error_ = error;
        error_offset_ = offset;
        return false;
    }

    [[nodiscard]] bool at_end() const noexcept {
        const bool end = offset_ >= input_.size();
        return end;
    }

    [[nodiscard]] bool looking_at(std::string_view text) const noexcept {
        const std::string_view rest = input_.substr(offset_);
        const bool matches = rest.starts_with(text);
        return matches;
    }

    // Skips XML whitespace and says whether any was skipped.
    bool skip_space() noexcept {
        const std::size_t start = offset_;
        while (!at_end() && is_space_byte(input_[offset_])) ++offset_;
        const bool skipped = offset_ != start;
        return skipped;
    }

    bool expect(char byte) {
        if (at_end()) return fail(XmlError::unexpected_end, offset_);
        if (input_[offset_] != byte) return fail(XmlError::syntax_error, offset_);
        ++offset_;
        return true;
    }

    bool read_document() {
        if (limits_.maximum_depth == 0U || limits_.maximum_depth > xml_depth_ceiling) {
            return fail(XmlError::invalid_limits, 0);
        }
        if (input_.size() > limits_.maximum_input_bytes) {
            return fail(XmlError::input_too_large, 0);
        }
        if (!read_encoding_signature()) return false;
        if (looking_at("<?xml") && input_.size() > offset_ + 5U &&
            is_space_byte(input_[offset_ + 5U])) {
            if (!read_declaration()) return false;
        }
        if (!read_prolog()) return false;
        if (!read_content()) return false;
        const bool epilogue = read_epilogue();
        return epilogue;
    }

    // A UTF-8 byte order mark is skipped. UTF-16 and UTF-32 marks, and the
    // zero bytes of BOM-less UTF-16 text, are refused as another encoding.
    bool read_encoding_signature() {
        if (looking_at("\xEF\xBB\xBF")) {
            offset_ = 3;
            return true;
        }
        const bool utf16_mark = looking_at("\xFE\xFF") || looking_at("\xFF\xFE");
        const bool zero_byte = input_.size() >= 2U && (input_[0] == '\0' || input_[1] == '\0');
        if (utf16_mark || zero_byte) return fail(XmlError::unsupported_encoding, 0);
        return true;
    }

    // One pseudo-attribute of the declaration: name, '=', quoted ASCII value.
    bool read_declaration_value(std::string_view name, std::string_view& value) {
        if (!looking_at(name)) return fail(XmlError::invalid_declaration, offset_);
        offset_ += name.size();
        skip_space();
        if (at_end() || input_[offset_] != '=') return fail(XmlError::invalid_declaration, offset_);
        ++offset_;
        skip_space();
        if (at_end()) return fail(XmlError::unexpected_end, offset_);
        const char quote = input_[offset_];
        if (quote != '"' && quote != '\'') return fail(XmlError::invalid_declaration, offset_);
        ++offset_;
        const std::size_t start = offset_;
        while (!at_end() && input_[offset_] != quote) {
            const unsigned char byte = static_cast<unsigned char>(input_[offset_]);
            if (byte < 0x21U || byte > 0x7EU) return fail(XmlError::invalid_declaration, offset_);
            ++offset_;
        }
        if (at_end()) return fail(XmlError::unexpected_end, offset_);
        value = input_.substr(start, offset_ - start);
        ++offset_;
        return true;
    }

    bool read_declaration() {
        offset_ += 5;
        skip_space();
        std::string_view version{};
        if (!read_declaration_value("version", version)) return false;
        if (version != "1.0") return fail(XmlError::invalid_declaration, offset_);
        bool spaced = skip_space();
        if (spaced && looking_at("encoding")) {
            std::string_view encoding{};
            const std::size_t encoding_offset = offset_;
            if (!read_declaration_value("encoding", encoding)) return false;
            if (!equal_ignoring_ascii_case(encoding, "UTF-8")) {
                return fail(XmlError::unsupported_encoding, encoding_offset);
            }
            spaced = skip_space();
        }
        if (spaced && looking_at("standalone")) {
            std::string_view standalone{};
            if (!read_declaration_value("standalone", standalone)) return false;
            if (standalone != "yes" && standalone != "no") {
                return fail(XmlError::invalid_declaration, offset_);
            }
            skip_space();
        }
        if (!looking_at("?>")) {
            if (at_end()) return fail(XmlError::unexpected_end, offset_);
            return fail(XmlError::invalid_declaration, offset_);
        }
        offset_ += 2;
        return true;
    }

    // Markup that may appear outside the root element: comments are read;
    // a DOCTYPE or a processing instruction is refused.
    bool read_misc_markup(bool& was_markup) {
        was_markup = true;
        if (looking_at("<!--")) {
            const bool comment = read_comment();
            return comment;
        }
        if (looking_at("<!DOCTYPE")) return fail(XmlError::doctype_refused, offset_);
        if (looking_at("<?")) return fail(XmlError::processing_instruction_refused, offset_);
        was_markup = false;
        return true;
    }

    bool read_prolog() {
        while (true) {
            skip_space();
            if (at_end()) return fail(XmlError::missing_root_element, offset_);
            if (input_[offset_] != '<') {
                const bool character = check_character_at(offset_);
                if (!character) return false;
                return fail(XmlError::syntax_error, offset_);
            }
            bool was_markup = false;
            if (!read_misc_markup(was_markup)) return false;
            if (was_markup) continue;
            if (looking_at("<!")) return fail(XmlError::syntax_error, offset_);
            const bool start = read_start_tag();
            return start;
        }
    }

    bool read_epilogue() {
        while (true) {
            skip_space();
            if (at_end()) return true;
            if (input_[offset_] == '<') {
                bool was_markup = false;
                if (!read_misc_markup(was_markup)) return false;
                if (was_markup) continue;
            }
            return fail(XmlError::content_after_root, offset_);
        }
    }

    // Reports a UTF-8 or character error at offset, if there is one.
    bool check_character_at(std::size_t offset) {
        const XmlDecodedCharacter decoded = decode_xml_utf8(input_, offset);
        if (decoded.length == 0U) return fail(XmlError::invalid_utf8, offset);
        if (!is_xml_character(decoded.scalar)) return fail(XmlError::invalid_character, offset);
        return true;
    }

    bool read_comment() {
        const std::size_t start = offset_;
        offset_ += 4;
        const std::size_t content_start = offset_;
        while (true) {
            if (at_end()) return fail(XmlError::unexpected_end, offset_);
            if (offset_ - content_start > limits_.maximum_value_bytes) {
                return fail(XmlError::value_too_long, start);
            }
            if (looking_at("--")) {
                if (input_.size() > offset_ + 2U && input_[offset_ + 2U] == '>') {
                    offset_ += 3;
                    return true;
                }
                if (input_.size() <= offset_ + 2U) return fail(XmlError::unexpected_end, offset_);
                return fail(XmlError::invalid_comment, offset_);
            }
            const XmlDecodedCharacter decoded = decode_xml_utf8(input_, offset_);
            if (decoded.length == 0U) return fail(XmlError::invalid_utf8, offset_);
            if (!is_xml_character(decoded.scalar)) return fail(XmlError::invalid_character, offset_);
            offset_ += decoded.length;
        }
    }

    // Reads a Name at the current offset into name (a view of the input).
    bool read_name(std::string_view& name) {
        const std::size_t start = offset_;
        if (at_end()) return fail(XmlError::unexpected_end, offset_);
        bool first = true;
        while (!at_end()) {
            const XmlDecodedCharacter decoded = decode_xml_utf8(input_, offset_);
            if (decoded.length == 0U) return fail(XmlError::invalid_utf8, offset_);
            const bool admitted = first ? is_xml_name_start(decoded.scalar)
                                        : is_xml_name_character(decoded.scalar);
            if (!admitted) break;
            first = false;
            offset_ += decoded.length;
            if (offset_ - start > limits_.maximum_name_bytes) {
                return fail(XmlError::name_too_long, start);
            }
        }
        if (first) {
            const bool character = check_character_at(offset_);
            if (!character) return false;
            return fail(XmlError::invalid_name, offset_);
        }
        name = input_.substr(start, offset_ - start);
        return true;
    }

    // Reads an entity or character reference at '&' and appends its value.
    bool read_reference(std::string& output) {
        const std::size_t start = offset_;
        ++offset_;
        if (at_end()) return fail(XmlError::unexpected_end, offset_);
        if (input_[offset_] == '#') {
            const bool character = read_character_reference(start, output);
            return character;
        }
        const std::size_t name_start = offset_;
        while (!at_end() && offset_ - name_start <= maximum_entity_name_bytes) {
            const char byte = input_[offset_];
            const bool letter = (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z');
            if (!letter) break;
            ++offset_;
        }
        const std::string_view name = input_.substr(name_start, offset_ - name_start);
        if (at_end()) return fail(XmlError::unexpected_end, offset_);
        if (input_[offset_] != ';') {
            if (name.empty()) return fail(XmlError::syntax_error, start);
            return fail(XmlError::unknown_entity, start);
        }
        ++offset_;
        if (name == "amp") {
            output.push_back('&');
        } else if (name == "lt") {
            output.push_back('<');
        } else if (name == "gt") {
            output.push_back('>');
        } else if (name == "apos") {
            output.push_back('\'');
        } else if (name == "quot") {
            output.push_back('"');
        } else if (name.empty()) {
            return fail(XmlError::syntax_error, start);
        } else {
            return fail(XmlError::unknown_entity, start);
        }
        return true;
    }

    bool read_character_reference(std::size_t start, std::string& output) {
        ++offset_;
        std::uint32_t base = 10;
        if (!at_end() && input_[offset_] == 'x') {
            base = 16;
            ++offset_;
        }
        std::uint32_t value = 0;
        std::size_t digits = 0;
        bool overflow = false;
        while (!at_end() && digits < maximum_reference_digits) {
            const char byte = input_[offset_];
            std::uint32_t digit = base;
            if (byte >= '0' && byte <= '9') {
                digit = static_cast<std::uint32_t>(byte - '0');
            } else if (base == 16U && byte >= 'a' && byte <= 'f') {
                digit = static_cast<std::uint32_t>(byte - 'a' + 10);
            } else if (base == 16U && byte >= 'A' && byte <= 'F') {
                digit = static_cast<std::uint32_t>(byte - 'A' + 10);
            }
            if (digit >= base) break;
            // Anything above U+10FFFF is refused, so stop accumulating there.
            if (value > 0x10FFFFU) {
                overflow = true;
            } else {
                value = value * base + digit;
            }
            ++digits;
            ++offset_;
        }
        if (at_end()) return fail(XmlError::unexpected_end, offset_);
        if (digits == 0U || input_[offset_] != ';') {
            return fail(XmlError::invalid_character_reference, start);
        }
        ++offset_;
        if (overflow || !is_xml_character(value)) {
            return fail(XmlError::invalid_character_reference, start);
        }
        append_xml_utf8(output, value);
        return true;
    }

    // Reads a quoted attribute value, applying XML 1.0 attribute-value
    // normalization: a literal tab, CR, LF or CR LF becomes one space;
    // the same characters written as references are kept.
    bool read_attribute_value(std::string& value) {
        if (at_end()) return fail(XmlError::unexpected_end, offset_);
        const char quote = input_[offset_];
        if (quote != '"' && quote != '\'') return fail(XmlError::syntax_error, offset_);
        const std::size_t start = offset_;
        ++offset_;
        while (true) {
            if (at_end()) return fail(XmlError::unexpected_end, offset_);
            if (value.size() > limits_.maximum_value_bytes) {
                return fail(XmlError::value_too_long, start);
            }
            const char byte = input_[offset_];
            if (byte == quote) {
                ++offset_;
                break;
            }
            if (byte == '<') return fail(XmlError::syntax_error, offset_);
            if (byte == '&') {
                if (!read_reference(value)) return false;
                continue;
            }
            if (byte == '\r') {
                value.push_back(' ');
                ++offset_;
                if (!at_end() && input_[offset_] == '\n') ++offset_;
                continue;
            }
            if (byte == '\n' || byte == '\t') {
                value.push_back(' ');
                ++offset_;
                continue;
            }
            const XmlDecodedCharacter decoded = decode_xml_utf8(input_, offset_);
            if (decoded.length == 0U) return fail(XmlError::invalid_utf8, offset_);
            if (!is_xml_character(decoded.scalar)) return fail(XmlError::invalid_character, offset_);
            value.append(input_.substr(offset_, decoded.length));
            offset_ += decoded.length;
        }
        if (value.size() > limits_.maximum_value_bytes) return fail(XmlError::value_too_long, start);
        return true;
    }

    // An element with child elements may hold only whitespace between them;
    // that whitespace is indentation and is dropped.
    bool settle_text_beside_children(XmlElement& element, std::size_t offset) {
        if (element.text.empty()) return true;
        if (!is_whitespace_only(element.text)) return fail(XmlError::mixed_content, offset);
        element.text.clear();
        return true;
    }

    void attach(XmlElement&& element) {
        if (open_.empty()) {
            root_ = std::move(element);
            return;
        }
        XmlElement& parent = open_.back();
        parent.children.push_back(std::move(element));
    }

    bool read_start_tag() {
        const std::size_t start = offset_;
        ++offset_;
        std::string_view name{};
        if (!read_name(name)) return false;
        if (open_.size() + 1U > limits_.maximum_depth) return fail(XmlError::depth_exceeded, start);
        if (element_count_ + 1U > limits_.maximum_elements) {
            return fail(XmlError::element_limit_exceeded, start);
        }
        if (!open_.empty()) {
            if (!settle_text_beside_children(open_.back(), start)) return false;
        }
        ++element_count_;
        XmlElement element{};
        element.name.assign(name);
        // The location's index, which stays valid as later elements append.
        const std::size_t location = locations_ == nullptr ? 0U : (*locations_).elements.size();
        if (locations_ != nullptr) {
            detail::XmlElementLocation record{};
            record.start_offset = start;
            (*locations_).elements.push_back(std::move(record));
        }
        while (true) {
            const bool spaced = skip_space();
            if (at_end()) return fail(XmlError::unexpected_end, offset_);
            const char byte = input_[offset_];
            if (byte == '/') {
                ++offset_;
                if (!expect('>')) return false;
                if (locations_ != nullptr) {
                    detail::XmlElementLocation& record = (*locations_).elements[location];
                    record.start_tag_end = offset_;
                    record.end_tag_offset = offset_;
                    record.end_offset = offset_;
                    record.empty_element_tag = true;
                }
                attach(std::move(element));
                return true;
            }
            if (byte == '>') {
                ++offset_;
                if (locations_ != nullptr) (*locations_).elements[location].start_tag_end = offset_;
                open_.push_back(std::move(element));
                open_locations_.push_back(location);
                return true;
            }
            if (!spaced) return fail(XmlError::syntax_error, offset_);
            const std::size_t attribute_start = offset_;
            std::string_view attribute_name{};
            if (!read_name(attribute_name)) return false;
            if (element.find_attribute(attribute_name) != nullptr) {
                return fail(XmlError::duplicate_attribute, attribute_start);
            }
            if (element.attributes.size() + 1U > limits_.maximum_attributes_per_element) {
                return fail(XmlError::attribute_limit_exceeded, attribute_start);
            }
            skip_space();
            if (!expect('=')) return false;
            skip_space();
            XmlAttribute attribute{};
            attribute.name.assign(attribute_name);
            const std::size_t value_quote = offset_;
            if (!read_attribute_value(attribute.value)) return false;
            if (locations_ != nullptr) {
                detail::XmlAttributeLocation where{};
                where.name_offset = attribute_start;
                where.value_offset = value_quote + 1U;
                where.value_end = offset_ - 1U;
                (*locations_).elements[location].attributes.push_back(where);
            }
            element.attributes.push_back(std::move(attribute));
        }
    }

    bool read_end_tag() {
        const std::size_t start = offset_;
        offset_ += 2;
        std::string_view name{};
        if (!read_name(name)) return false;
        skip_space();
        if (!expect('>')) return false;
        XmlElement& current = open_.back();
        if (name != current.name) return fail(XmlError::mismatched_end_tag, start);
        if (!current.children.empty()) {
            if (!settle_text_beside_children(current, start)) return false;
        }
        if (locations_ != nullptr) {
            detail::XmlElementLocation& record = (*locations_).elements[open_locations_.back()];
            record.end_tag_offset = start;
            record.end_offset = offset_;
        }
        if (!open_locations_.empty()) open_locations_.pop_back();
        XmlElement element = std::move(current);
        open_.pop_back();
        attach(std::move(element));
        return true;
    }

    // Character data, with line ends normalized to LF (XML 1.0 section 2.11).
    bool read_character_data() {
        XmlElement& current = open_.back();
        while (!at_end()) {
            const char byte = input_[offset_];
            if (byte == '<' || byte == '&') break;
            const std::size_t character_offset = offset_;
            if (byte == '\r') {
                current.text.push_back('\n');
                ++offset_;
                if (!at_end() && input_[offset_] == '\n') ++offset_;
            } else {
                if (byte == ']' && looking_at("]]>")) return fail(XmlError::syntax_error, offset_);
                const XmlDecodedCharacter decoded = decode_xml_utf8(input_, offset_);
                if (decoded.length == 0U) return fail(XmlError::invalid_utf8, offset_);
                if (!is_xml_character(decoded.scalar)) {
                    return fail(XmlError::invalid_character, offset_);
                }
                current.text.append(input_.substr(offset_, decoded.length));
                offset_ += decoded.length;
            }
            if (current.text.size() > limits_.maximum_value_bytes) {
                return fail(XmlError::value_too_long, character_offset);
            }
        }
        return true;
    }

    bool read_content() {
        while (!open_.empty()) {
            if (at_end()) return fail(XmlError::unexpected_end, offset_);
            const char byte = input_[offset_];
            bool read = false;
            if (byte == '&') {
                XmlElement& current = open_.back();
                read = read_reference(current.text);
                if (read && current.text.size() > limits_.maximum_value_bytes) {
                    return fail(XmlError::value_too_long, offset_);
                }
            } else if (byte != '<') {
                read = read_character_data();
            } else if (looking_at("</")) {
                read = read_end_tag();
            } else if (looking_at("<!--")) {
                read = read_comment();
            } else if (looking_at("<![CDATA[")) {
                return fail(XmlError::cdata_refused, offset_);
            } else if (looking_at("<!DOCTYPE")) {
                return fail(XmlError::doctype_refused, offset_);
            } else if (looking_at("<!")) {
                return fail(XmlError::syntax_error, offset_);
            } else if (looking_at("<?")) {
                return fail(XmlError::processing_instruction_refused, offset_);
            } else {
                read = read_start_tag();
            }
            if (!read) return false;
        }
        return true;
    }

    std::string_view input_{};
    const XmlLimits& limits_;
    std::size_t offset_{};
    XmlError error_{XmlError::none};
    std::size_t error_offset_{};
    std::uint32_t element_count_{};
    std::vector<XmlElement> open_{};
    // The location index of each element of open_, in step with it.
    std::vector<std::size_t> open_locations_{};
    XmlElement root_{};
    detail::XmlLocations* locations_{};
};

} // namespace

XmlReadResult read_xml(std::string_view utf8, const XmlLimits& limits) {
    XmlDocumentReader reader(utf8, limits, nullptr);
    XmlReadResult result = reader.read();
    return result;
}

namespace detail {

XmlReadResult read_xml_located(std::string_view utf8, const XmlLimits& limits, XmlLocations& locations) {
    locations.elements.clear();
    XmlDocumentReader reader(utf8, limits, &locations);
    XmlReadResult result = reader.read();
    return result;
}

} // namespace detail

} // namespace gui_forms
