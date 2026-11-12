#pragma once

#include "gui_forms/export/application_export.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

// The bounded XML subset read and written by GUI.Forms (DK decision 43):
// UTF-8 only; elements, attributes, character data, the five predefined
// entities and numeric character references; comments are read and dropped.
// No DTD, no processing instruction other than the optional leading
// <?xml version="1.0" encoding="UTF-8"?> declaration, no CDATA section, no
// external entity and no namespace processing (':' is a name character).
// An element holds either child elements or character data, never both
// ("mixed content"); whitespace between child elements is indentation and is
// not kept.

// Where a reader diagnostic was found. Lines and columns count from 1. A
// column counts Unicode scalar values from the start of the line, not bytes;
// CR LF, CR and LF each end one line. byte_offset counts bytes from the start
// of the input, including any byte order mark.
struct GUI_FORMS_API XmlSourcePosition final {
    std::size_t byte_offset{};
    std::uint32_t line{};
    std::uint32_t column{};

    friend bool operator==(const XmlSourcePosition& left,
                           const XmlSourcePosition& right) noexcept {
        return left.byte_offset == right.byte_offset && left.line == right.line &&
               left.column == right.column;
    }
};

// Every bound the reader and writer enforce. Each one, when exceeded, gives
// its own XmlError. Lengths are in bytes of UTF-8 after entity decoding.
struct GUI_FORMS_API XmlLimits final {
    std::size_t maximum_input_bytes{4U * 1024U * 1024U};
    // The root element is at depth 1.
    std::uint32_t maximum_depth{64};
    std::uint32_t maximum_elements{65536};
    std::uint32_t maximum_attributes_per_element{64};
    std::size_t maximum_name_bytes{256};
    // Bounds one attribute value, one element's character data and one
    // comment.
    std::size_t maximum_value_bytes{64U * 1024U};
};

// The largest maximum_depth accepted; a deeper limit is refused with
// XmlError::invalid_limits. Nothing recurses per level: the reader and the
// writer keep one entry per open element in explicit storage, at most
// maximum_depth of them, and copying, comparing and destroying an element
// walk it in work lists (XmlElement), whatever its depth.
inline constexpr std::uint32_t xml_depth_ceiling = 1024;

enum class XmlError : std::uint8_t {
    none,
    invalid_limits,
    input_too_large,
    unsupported_encoding,
    invalid_utf8,
    invalid_character,
    unexpected_end,
    syntax_error,
    invalid_name,
    name_too_long,
    value_too_long,
    depth_exceeded,
    element_limit_exceeded,
    attribute_limit_exceeded,
    duplicate_attribute,
    mismatched_end_tag,
    unknown_entity,
    invalid_character_reference,
    doctype_refused,
    processing_instruction_refused,
    cdata_refused,
    invalid_declaration,
    invalid_comment,
    missing_root_element,
    content_after_root,
    mixed_content,
};

[[nodiscard]] GUI_FORMS_API std::string_view xml_error_name(XmlError error) noexcept;

struct GUI_FORMS_API XmlAttribute final {
    std::string name;
    std::string value;

    friend bool operator==(const XmlAttribute& left,
                           const XmlAttribute& right) noexcept {
        return left.name == right.name && left.value == right.value;
    }
};

// One element and everything below it. attributes keep document order;
// children keep document order; text is the decoded character data of an
// element without children.
//
// A tree built in code has no depth limit, so copying, comparing and
// destroying walk it in explicit work storage instead of recursing per
// level (HOUSE_STYLE section 1). Moving moves the child list's storage.
struct GUI_FORMS_API XmlElement final {
    std::string name;
    std::vector<XmlAttribute> attributes;
    std::vector<XmlElement> children;
    std::string text;

    XmlElement() = default;
    // A deep copy, level by level in a work list. On failure (allocation)
    // the partial copy is destroyed and the exception propagates.
    XmlElement(const XmlElement& other);
    XmlElement(XmlElement&& other) noexcept = default;
    // Copies into a new tree first, so on failure this element is unchanged.
    XmlElement& operator=(const XmlElement& other);
    XmlElement& operator=(XmlElement&& other) noexcept = default;
    // Tears the children down iteratively (src/core/algorithm/tree_teardown.hpp).
    ~XmlElement();

    // The attribute with this name, or null. The pointer borrows from this
    // element and is invalidated by any change to attributes. A temporary
    // element is refused (house style section 1): its attributes end with
    // the statement.
    [[nodiscard]] const XmlAttribute* find_attribute(std::string_view attribute_name) const& noexcept;
    const XmlAttribute* find_attribute(std::string_view attribute_name) const&& = delete;

    // Compares the trees level by level in a work list, so it may throw
    // std::bad_alloc.
    friend GUI_FORMS_API bool operator==(const XmlElement& left, const XmlElement& right);
};

struct GUI_FORMS_API XmlReadResult final {
    XmlError error{XmlError::none};
    // Where the error was found; all zero on success.
    XmlSourcePosition position{};
    // The document's root element on success; empty on failure.
    XmlElement root{};

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == XmlError::none;
    }
};

struct GUI_FORMS_API XmlWriteOptions final {
    // Spaces per nesting level; zero writes each element on its own line
    // without indentation.
    std::uint32_t indent_spaces{2};
    bool declaration{true};
};

struct GUI_FORMS_API XmlWriteResult final {
    XmlError error{XmlError::none};
    // The document on success; empty on failure.
    std::string text{};

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == XmlError::none;
    }
};

} // namespace gui_forms
