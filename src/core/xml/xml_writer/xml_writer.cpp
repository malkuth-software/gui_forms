#include "gui_forms/xml/xml_writer/xml_writer.hpp"

#include "../xml_characters/xml_characters.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gui_forms {

namespace {

using detail::validate_xml_characters;
using detail::validate_xml_name;

// Character data escapes '>' as well as '<' and '&', so "]]>" can never
// appear, and CR as a reference, so the reader's line-end normalization
// gives it back unchanged.
void append_escaped_text(std::string& output, std::string_view text) {
    for (const char byte : text) {
        if (byte == '&') {
            output.append("&amp;");
        } else if (byte == '<') {
            output.append("&lt;");
        } else if (byte == '>') {
            output.append("&gt;");
        } else if (byte == '\r') {
            output.append("&#13;");
        } else {
            output.push_back(byte);
        }
    }
}

// Attribute values escape tab, LF and CR as references, which the reader's
// attribute-value normalization keeps; written literally they would read
// back as spaces.
void append_escaped_attribute(std::string& output, std::string_view value) {
    for (const char byte : value) {
        if (byte == '&') {
            output.append("&amp;");
        } else if (byte == '<') {
            output.append("&lt;");
        } else if (byte == '>') {
            output.append("&gt;");
        } else if (byte == '"') {
            output.append("&quot;");
        } else if (byte == '\t') {
            output.append("&#9;");
        } else if (byte == '\n') {
            output.append("&#10;");
        } else if (byte == '\r') {
            output.append("&#13;");
        } else {
            output.push_back(byte);
        }
    }
}

// One write. Validation happens element by element as the text is built;
// the caller receives the text only when every element passed, so a failed
// write leaves no partial document behind.
class XmlDocumentWriter final {
public:
    XmlDocumentWriter(const XmlWriteOptions& options, const XmlLimits& limits)
        : options_(options), limits_(limits) {}

    XmlWriteResult write(const XmlElement& root) {
        XmlWriteResult result{};
        if (limits_.maximum_depth == 0U || limits_.maximum_depth > xml_depth_ceiling) {
            result.error = XmlError::invalid_limits;
            return result;
        }
        if (options_.declaration) {
            output_.append("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
        }
        const XmlError error = write_tree(root);
        if (error != XmlError::none) {
            result.error = error;
            return result;
        }
        if (output_.size() > limits_.maximum_input_bytes) {
            result.error = XmlError::input_too_large;
            return result;
        }
        result.text = std::move(output_);
        return result;
    }

private:
    [[nodiscard]] XmlError validate_value(std::string_view value) const noexcept {
        if (value.size() > limits_.maximum_value_bytes) return XmlError::value_too_long;
        const XmlError characters = validate_xml_characters(value);
        return characters;
    }

    [[nodiscard]] XmlError validate_attributes(const XmlElement& element) const noexcept {
        if (element.attributes.size() > limits_.maximum_attributes_per_element) {
            return XmlError::attribute_limit_exceeded;
        }
        for (std::size_t index = 0; index < element.attributes.size(); ++index) {
            const XmlAttribute& attribute = element.attributes[index];
            const XmlError name = validate_xml_name(attribute.name, limits_.maximum_name_bytes);
            if (name != XmlError::none) return name;
            const XmlError value = validate_value(attribute.value);
            if (value != XmlError::none) return value;
            for (std::size_t earlier = 0; earlier < index; ++earlier) {
                if (element.attributes[earlier].name == attribute.name) {
                    return XmlError::duplicate_attribute;
                }
            }
        }
        return XmlError::none;
    }

    void indent(std::uint32_t depth) {
        const std::size_t levels = depth - 1U;
        const std::size_t spaces = levels * options_.indent_spaces;
        output_.append(spaces, ' ');
    }

    // One element whose children are being written: its depth (the root
    // is at depth 1) and the next child to write.
    struct WriteFrame final {
        const XmlElement* element{nullptr};
        std::size_t next_child{0U};
        std::uint32_t depth{0U};
    };

    // Writes the tree in explicit work storage, one frame per open element
    // with children, so no call nests per level (HOUSE_STYLE section 1).
    // The frames are at most maximum_depth (at most xml_depth_ceiling),
    // reserved before the walk.
    XmlError write_tree(const XmlElement& root) {
        std::vector<WriteFrame> open{};
        open.reserve(limits_.maximum_depth);
        const XmlError first = open_element(root, 1U);
        if (first != XmlError::none) return first;
        if (!root.children.empty()) open.push_back(WriteFrame{&root, 0U, 1U});
        while (!open.empty()) {
            WriteFrame& frame = open.back();
            const std::vector<XmlElement>& children = (*frame.element).children;
            if (frame.next_child == children.size()) {
                close_element(*frame.element, frame.depth);
                open.pop_back();
                continue;
            }
            const XmlElement& child = children[frame.next_child];
            ++frame.next_child;
            const std::uint32_t depth = frame.depth + 1U;
            const XmlError written = open_element(child, depth);
            if (written != XmlError::none) return written;
            if (!child.children.empty()) open.push_back(WriteFrame{&child, 0U, depth});
        }
        return XmlError::none;
    }

    // Validates `element` and writes its start tag. An element without
    // children is written whole (self-closed, or with its text and end
    // tag); one with children is left open for close_element. The depth is
    // checked first, so no element past maximum_depth is written.
    XmlError open_element(const XmlElement& element, std::uint32_t depth) {
        if (depth > limits_.maximum_depth) return XmlError::depth_exceeded;
        ++element_count_;
        if (element_count_ > limits_.maximum_elements) return XmlError::element_limit_exceeded;
        const XmlError name = validate_xml_name(element.name, limits_.maximum_name_bytes);
        if (name != XmlError::none) return name;
        const XmlError attributes = validate_attributes(element);
        if (attributes != XmlError::none) return attributes;
        if (!element.children.empty() && !element.text.empty()) return XmlError::mixed_content;
        const XmlError text = validate_value(element.text);
        if (text != XmlError::none) return text;

        indent(depth);
        output_.push_back('<');
        output_.append(element.name);
        for (const XmlAttribute& attribute : element.attributes) {
            output_.push_back(' ');
            output_.append(attribute.name);
            output_.append("=\"");
            append_escaped_attribute(output_, attribute.value);
            output_.push_back('"');
        }
        if (element.children.empty() && element.text.empty()) {
            output_.append("/>\n");
            return XmlError::none;
        }
        output_.push_back('>');
        if (element.children.empty()) {
            append_escaped_text(output_, element.text);
            output_.append("</");
            output_.append(element.name);
            output_.append(">\n");
            return XmlError::none;
        }
        output_.push_back('\n');
        return XmlError::none;
    }

    // The end tag of an element open_element left open.
    void close_element(const XmlElement& element, std::uint32_t depth) {
        indent(depth);
        output_.append("</");
        output_.append(element.name);
        output_.append(">\n");
    }

    const XmlWriteOptions& options_;
    const XmlLimits& limits_;
    std::uint32_t element_count_{};
    std::string output_{};
};

} // namespace

XmlWriteResult write_xml(const XmlElement& root, const XmlWriteOptions& options,
                         const XmlLimits& limits) {
    XmlDocumentWriter writer(options, limits);
    XmlWriteResult result = writer.write(root);
    return result;
}

} // namespace gui_forms
