#include "gui_forms/xml/types/xml_types.hpp"

#include "../../algorithm/tree_teardown.hpp"

#include <cstddef>
#include <utility>
#include <vector>

namespace gui_forms {

std::string_view xml_error_name(XmlError error) noexcept {
    switch (error) {
    case XmlError::none: return "none";
    case XmlError::invalid_limits: return "invalid_limits";
    case XmlError::input_too_large: return "input_too_large";
    case XmlError::unsupported_encoding: return "unsupported_encoding";
    case XmlError::invalid_utf8: return "invalid_utf8";
    case XmlError::invalid_character: return "invalid_character";
    case XmlError::unexpected_end: return "unexpected_end";
    case XmlError::syntax_error: return "syntax_error";
    case XmlError::invalid_name: return "invalid_name";
    case XmlError::name_too_long: return "name_too_long";
    case XmlError::value_too_long: return "value_too_long";
    case XmlError::depth_exceeded: return "depth_exceeded";
    case XmlError::element_limit_exceeded: return "element_limit_exceeded";
    case XmlError::attribute_limit_exceeded: return "attribute_limit_exceeded";
    case XmlError::duplicate_attribute: return "duplicate_attribute";
    case XmlError::mismatched_end_tag: return "mismatched_end_tag";
    case XmlError::unknown_entity: return "unknown_entity";
    case XmlError::invalid_character_reference: return "invalid_character_reference";
    case XmlError::doctype_refused: return "doctype_refused";
    case XmlError::processing_instruction_refused: return "processing_instruction_refused";
    case XmlError::cdata_refused: return "cdata_refused";
    case XmlError::invalid_declaration: return "invalid_declaration";
    case XmlError::invalid_comment: return "invalid_comment";
    case XmlError::missing_root_element: return "missing_root_element";
    case XmlError::content_after_root: return "content_after_root";
    case XmlError::mixed_content: return "mixed_content";
    }
    return "unknown";
}

const XmlAttribute* XmlElement::find_attribute(std::string_view attribute_name) const& noexcept {
    for (const XmlAttribute& attribute : attributes) {
        if (attribute.name == attribute_name) {
            const XmlAttribute* found = &attribute;
            return found;
        }
    }
    return nullptr;
}

namespace {

// A source element and the copy being filled from it.
struct XmlCopyStep final {
    const XmlElement* source{nullptr};
    XmlElement* destination{nullptr};
};

// Two elements whose children are still to be compared.
struct XmlComparePair final {
    const XmlElement* left{nullptr};
    const XmlElement* right{nullptr};
};

// One element's own fields, without its children.
[[nodiscard]] bool same_fields(const XmlElement& left, const XmlElement& right) {
    return left.name == right.name && left.attributes == right.attributes && left.text == right.text &&
           left.children.size() == right.children.size();
}

} // namespace

XmlElement::XmlElement(const XmlElement& other)
    : name(other.name), attributes(other.attributes), text(other.text) {
    // Each step fills one destination's children with copies of the
    // source's own fields, then queues each child pair. A destination's
    // child list is reserved before it is filled, so the queued pointers
    // into it stay valid.
    std::vector<XmlCopyStep> pending{};
    pending.push_back(XmlCopyStep{&other, this});
    while (!pending.empty()) {
        const XmlCopyStep step = pending.back();
        pending.pop_back();
        const std::vector<XmlElement>& source_children = (*step.source).children;
        std::vector<XmlElement>& destination_children = (*step.destination).children;
        destination_children.reserve(source_children.size());
        for (const XmlElement& source_child : source_children) {
            XmlElement shallow{};
            shallow.name = source_child.name;
            shallow.attributes = source_child.attributes;
            shallow.text = source_child.text;
            destination_children.push_back(std::move(shallow));
        }
        for (std::size_t index = 0U; index < source_children.size(); ++index) {
            if (source_children[index].children.empty()) continue;
            pending.push_back(XmlCopyStep{&source_children[index], &destination_children[index]});
        }
    }
}

XmlElement& XmlElement::operator=(const XmlElement& other) {
    XmlElement copy(other);
    *this = std::move(copy);
    return *this;
}

XmlElement::~XmlElement() {
    if (!children.empty()) {
        detail::release_children_iteratively<XmlElement, &XmlElement::children>(children);
    }
}

bool operator==(const XmlElement& left, const XmlElement& right) {
    if (!same_fields(left, right)) return false;
    std::vector<XmlComparePair> pending{};
    pending.push_back(XmlComparePair{&left, &right});
    while (!pending.empty()) {
        const XmlComparePair pair = pending.back();
        pending.pop_back();
        const std::vector<XmlElement>& left_children = (*pair.left).children;
        const std::vector<XmlElement>& right_children = (*pair.right).children;
        for (std::size_t index = 0U; index < left_children.size(); ++index) {
            if (!same_fields(left_children[index], right_children[index])) return false;
            if (left_children[index].children.empty()) continue;
            pending.push_back(XmlComparePair{&left_children[index], &right_children[index]});
        }
    }
    return true;
}

} // namespace gui_forms
