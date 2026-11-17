#pragma once

// A located read of the bounded XML subset (xml_types.hpp), for readers that
// report against the text and edit it in place: the form documents' front
// end and editor (src/core/form_language/form_xml). Internal. The subset and
// every limit are read_xml's; this only records where each element and
// attribute was written.

#include "gui_forms/xml/types/xml_types.hpp"

#include <cstddef>
#include <string_view>
#include <vector>

namespace gui_forms::detail {

// Where one attribute was written, as byte offsets into the input.
struct XmlAttributeLocation final {
    // The first byte of its name.
    std::size_t name_offset{};
    // The first byte after its opening quote, and the byte of its closing
    // quote: [value_offset, value_end) is the value as written, before
    // entity decoding and normalization.
    std::size_t value_offset{};
    std::size_t value_end{};
};

// Where one element was written, as byte offsets into the input.
struct XmlElementLocation final {
    // The '<' of its start tag.
    std::size_t start_offset{};
    // The byte after the start tag's '>' (after "/>" for an empty-element
    // tag).
    std::size_t start_tag_end{};
    // The '<' of its end tag; equal to start_tag_end for an empty-element
    // tag.
    std::size_t end_tag_offset{};
    // The byte after its end tag's '>' (start_tag_end for an empty-element
    // tag).
    std::size_t end_offset{};
    bool empty_element_tag{};
    // In the element's attribute order.
    std::vector<XmlAttributeLocation> attributes{};
};

// The elements of one read in document order of their start tags (the
// root first, then each element before its children): the order a pre-order
// walk of XmlReadResult::root visits them. Caller-owned; replaced whole by
// read_xml_located, empty after a failed read.
struct XmlLocations final {
    std::vector<XmlElementLocation> elements{};
};

// read_xml, also filling `locations`.
[[nodiscard]] XmlReadResult read_xml_located(std::string_view utf8, const XmlLimits& limits,
                                             XmlLocations& locations);

} // namespace gui_forms::detail
