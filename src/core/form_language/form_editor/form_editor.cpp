// A form document's editor (stage FD, phase FD4; XML since cp34). Each edit
// is one or two splices of the text, worked out from where the XML reader
// found each element and attribute, then the whole edited text is read
// again as a document; only a valid one replaces the caller's.

#include "form_editor.hpp"

#include "gui_forms/control/stable_id/stable_id.hpp"

#include <optional>
#include <utility>
#include <vector>

namespace gui_forms::form_language {

namespace {

using detail::XmlAttributeLocation;
using detail::XmlElementLocation;
using theme_language::ThemeDiagnosticList;
using theme_language::ThemeSource;

// The spaces a first child element is indented deeper than its parent.
constexpr std::string_view child_indent = "  ";

// One replacement of the text: bytes [begin, end) become `text`.
struct Splice final {
    std::size_t begin{};
    std::size_t end{};
    std::string text{};
};

[[nodiscard]] bool is_blank(char character) noexcept {
    return character == ' ' || character == '\t';
}

// Whether `name` is a kebab-case name: lowercase ASCII letters and digits
// in runs joined by single '-'.
[[nodiscard]] bool kebab_name(std::string_view name) noexcept {
    if (name.empty() || name.front() == '-' || name.back() == '-') return false;
    char previous = '\0';
    for (const char character : name) {
        const bool alphanumeric = (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9');
        if (character == '-' && previous == '-') return false;
        if (!alphanumeric && character != '-') return false;
        previous = character;
    }
    return true;
}

// Whether `name` is a property's spelling: kebab case, and neither of the
// two attributes a form reserves.
[[nodiscard]] bool property_name(std::string_view name) noexcept {
    if (name == form_identity_attribute || name == form_namespace_attribute) return false;
    const bool kebab = kebab_name(name);
    return kebab;
}

// `value` as the bytes of an attribute value quoted with `quote`: '&', '<'
// and the quote as entities, and tab, line feed and carriage return as
// character references, since XML normalizes them in an attribute.
[[nodiscard]] std::string attribute_text(std::string_view value, char quote) {
    std::string escaped{};
    escaped.reserve(value.size());
    for (const char character : value) {
        switch (character) {
        case '&': escaped.append("&amp;"); break;
        case '<': escaped.append("&lt;"); break;
        case '\t': escaped.append("&#9;"); break;
        case '\n': escaped.append("&#10;"); break;
        case '\r': escaped.append("&#13;"); break;
        case '"':
            if (quote == '"') {
                escaped.append("&quot;");
            } else {
                escaped.push_back(character);
            }
            break;
        case '\'':
            if (quote == '\'') {
                escaped.append("&apos;");
            } else {
                escaped.push_back(character);
            }
            break;
        default: escaped.push_back(character); break;
        }
    }
    return escaped;
}

// Reads a text into a document: its XML and, unless `syntax_only`, the
// model. Returns whether it is a valid document.
[[nodiscard]] bool read_document(FormDocument& document, const FormSchema& schema, bool syntax_only,
                                 ThemeDiagnosticList& diagnostics) {
    ThemeSource source{};
    source.file = document.file;
    source.text = document.text;
    if (read_form_syntax(source, document.syntax, diagnostics) != FormSyntaxOutcome::read) return false;
    if (syntax_only) return true;
    const FormModelOutcome built = build_form_model(source, document.syntax, schema, document.model, diagnostics);
    return built == FormModelOutcome::model_built;
}

// Reads the document's file with `text` and, when valid, makes it the
// document.
[[nodiscard]] FormEditOutcome commit(FormDocument& document, std::string text, const FormSchema& schema,
                                     ThemeDiagnosticList& diagnostics) {
    FormDocument scratch{};
    scratch.file = document.file;
    scratch.text = std::move(text);
    if (!read_document(scratch, schema, false, diagnostics)) return FormEditOutcome::refused;
    document = std::move(scratch);
    return FormEditOutcome::edited;
}

[[nodiscard]] std::string spliced_text(std::string_view text, const Splice& splice) {
    std::string edited{};
    edited.reserve(text.size() - (splice.end - splice.begin) + splice.text.size());
    edited.append(text.substr(0U, splice.begin));
    edited.append(splice.text);
    edited.append(text.substr(splice.end));
    return edited;
}

// One element of the read document: the element, its position in
// FormSyntax::locations, and its parent's position (none for the root).
struct DocumentElement final {
    const XmlElement* element{};
    std::uint32_t index{};
    std::optional<std::uint32_t> parent{};
};

// The document's elements in pre-order, the order of
// FormSyntax::locations. Walked in an explicit work list, children pushed
// last first so that they are taken in document order.
[[nodiscard]] std::vector<DocumentElement> document_elements(const FormDocument& document) {
    std::vector<DocumentElement> elements{};
    elements.reserve(document.syntax.locations.elements.size());
    std::vector<DocumentElement> pending{};
    DocumentElement root{};
    root.element = &document.syntax.root;
    pending.push_back(root);
    while (!pending.empty()) {
        DocumentElement current = pending.back();
        pending.pop_back();
        current.index = static_cast<std::uint32_t>(elements.size());
        elements.push_back(current);
        const std::vector<XmlElement>& children = (*current.element).children;
        for (std::size_t remaining = children.size(); remaining > 0U; --remaining) {
            DocumentElement child{};
            child.element = &children[remaining - 1U];
            child.parent = current.index;
            pending.push_back(child);
        }
    }
    return elements;
}

// Whether the element is an object of the document: neither a span (a
// region of its container's grid, study 10.9) nor a refused style block.
[[nodiscard]] bool object_element(const XmlElement& element) noexcept {
    const bool object = element.name != form_span_type_name && element.name != "style";
    return object;
}

// Byte positions, layout and the elements over one read document, which
// it borrows: it is declared after the document and used before any
// commit replaces it.
class TextLayout final {
public:
    explicit TextLayout(const FormDocument& document)
        : document_(document), elements_(document_elements(document)) {}

    [[nodiscard]] const XmlElementLocation& location(std::uint32_t index) const& noexcept {
        return document_.syntax.locations.elements[index];
    }
    const XmlElementLocation& location(std::uint32_t index) const&& = delete;
    [[nodiscard]] const XmlElement& element(std::uint32_t index) const& noexcept {
        return *elements_[index].element;
    }
    const XmlElement& element(std::uint32_t index) const&& = delete;

    // The object whose `id` is `stable_id`.
    [[nodiscard]] std::optional<std::uint32_t> find_object(std::string_view stable_id) const {
        for (const DocumentElement& candidate : elements_) {
            if (!object_element(*candidate.element)) continue;
            const XmlAttribute* identity = (*candidate.element).find_attribute(form_identity_attribute);
            if (identity != nullptr && (*identity).value == stable_id) return candidate.index;
        }
        return std::nullopt;
    }
    // The element's direct child elements, in order: the objects only when
    // `objects_only`.
    [[nodiscard]] std::vector<std::uint32_t> children(std::uint32_t parent, bool objects_only) const {
        std::vector<std::uint32_t> found{};
        for (const DocumentElement& candidate : elements_) {
            if (!candidate.parent || *candidate.parent != parent) continue;
            if (objects_only && !object_element(*candidate.element)) continue;
            found.push_back(candidate.index);
        }
        return found;
    }
    // The position of the element's attribute `name`.
    [[nodiscard]] std::optional<std::uint32_t> find_attribute(std::uint32_t index, std::string_view name) const {
        const std::vector<XmlAttribute>& attributes = element(index).attributes;
        for (std::uint32_t attribute = 0U; attribute < attributes.size(); ++attribute) {
            if (attributes[attribute].name == name) return attribute;
        }
        return std::nullopt;
    }
    // Whether the element `inner` is `outer` or lies inside it.
    [[nodiscard]] bool within(std::uint32_t inner, std::uint32_t outer) const noexcept {
        const XmlElementLocation& outside = location(outer);
        const std::size_t start = location(inner).start_offset;
        const bool inside = start >= outside.start_offset && start < outside.end_offset;
        return inside;
    }
    // The line ending the document writes.
    [[nodiscard]] std::string_view newline() const& noexcept {
        return document_.text.find("\r\n") != std::string::npos ? std::string_view("\r\n") : std::string_view("\n");
    }
    std::string_view newline() const&& = delete;
    [[nodiscard]] std::size_t line_start(std::size_t offset) const noexcept {
        const std::size_t previous = document_.text.rfind('\n', offset == 0U ? 0U : offset - 1U);
        if (offset == 0U || previous == std::string::npos) return 0U;
        return previous + 1U;
    }
    // The blanks before `offset` on its line, when nothing else is there.
    [[nodiscard]] std::optional<std::string_view> indentation(std::size_t offset) const noexcept {
        const std::size_t start = line_start(offset);
        for (std::size_t index = start; index < offset; ++index) {
            if (!is_blank(document_.text[index])) return std::nullopt;
        }
        return std::string_view(document_.text).substr(start, offset - start);
    }
    // The blanks that begin the line `offset` is on.
    [[nodiscard]] std::string_view leading_blanks(std::size_t offset) const noexcept {
        const std::size_t start = line_start(offset);
        std::size_t end = start;
        while (end < document_.text.size() && is_blank(document_.text[end])) ++end;
        return std::string_view(document_.text).substr(start, end - start);
    }
    // Where the line after `offset` begins, when only blanks and the line
    // ending follow it; the text's end when it ends there.
    [[nodiscard]] std::optional<std::size_t> blank_rest_of_line(std::size_t offset) const noexcept {
        std::size_t index = offset;
        while (index < document_.text.size() && is_blank(document_.text[index])) ++index;
        if (index == document_.text.size()) return index;
        if (document_.text[index] == '\n') return index + 1U;
        if (document_.text[index] == '\r' && index + 1U < document_.text.size() && document_.text[index + 1U] == '\n') {
            return index + 2U;
        }
        return std::nullopt;
    }
    [[nodiscard]] bool same_line(std::size_t first, std::size_t second) const noexcept {
        const std::string_view between = std::string_view(document_.text).substr(first, second - first);
        return between.find('\n') == std::string_view::npos;
    }

private:
    const FormDocument& document_;
    std::vector<DocumentElement> elements_;
};

// The byte after an attribute's closing quote.
[[nodiscard]] std::size_t attribute_end(const XmlAttributeLocation& attribute) noexcept {
    const std::size_t end = attribute.value_end + 1U;
    return end;
}

// Adds the attribute `item` (`name="value"`) after the element's last one.
// Every object writes `id`, so there is one.
[[nodiscard]] Splice append_attribute(const TextLayout& layout, std::uint32_t element, std::string_view item) {
    const XmlElementLocation& where = layout.location(element);
    const XmlAttributeLocation& last = where.attributes.back();
    Splice splice{};
    splice.begin = attribute_end(last);
    splice.end = splice.begin;
    if (layout.same_line(where.start_offset, last.name_offset)) {
        splice.text = " " + std::string(item);
        return splice;
    }
    splice.text = std::string(layout.newline()) + std::string(layout.leading_blanks(last.name_offset)) +
                  std::string(item);
    return splice;
}

// Removes the attribute `attribute` with the whitespace that separates it
// from the attribute before it (or, for the first, from the one after it,
// which `id` guarantees on an object's element).
[[nodiscard]] Splice remove_attribute(const TextLayout& layout, std::uint32_t element, std::uint32_t attribute) {
    const std::vector<XmlAttributeLocation>& attributes = layout.location(element).attributes;
    Splice splice{};
    if (attribute > 0U) {
        splice.begin = attribute_end(attributes[attribute - 1U]);
        splice.end = attribute_end(attributes[attribute]);
        return splice;
    }
    splice.begin = attributes[0].name_offset;
    splice.end = attributes.size() > 1U ? attributes[1].name_offset : attribute_end(attributes[0]);
    return splice;
}

// Places the element `item` before the sibling element `before`.
[[nodiscard]] Splice insert_before(const TextLayout& layout, std::uint32_t before, std::string_view item) {
    Splice splice{};
    splice.begin = layout.location(before).start_offset;
    splice.end = splice.begin;
    const std::optional<std::string_view> indent = layout.indentation(splice.begin);
    if (!indent) {
        splice.text = std::string(item);
        return splice;
    }
    splice.text = std::string(item) + std::string(layout.newline()) + std::string(*indent);
    return splice;
}

// Places the element `item` after the sibling element `after`.
[[nodiscard]] Splice insert_after(const TextLayout& layout, std::uint32_t after, std::string_view item) {
    const XmlElementLocation& sibling = layout.location(after);
    Splice splice{};
    splice.begin = sibling.end_offset;
    splice.end = splice.begin;
    const std::optional<std::string_view> indent = layout.indentation(sibling.start_offset);
    if (!indent) {
        splice.text = std::string(item);
        return splice;
    }
    splice.text = std::string(layout.newline()) + std::string(*indent) + std::string(item);
    return splice;
}

// Places the element `item` as the only child element of `parent`, which
// has none: an empty-element tag is opened and closed around it.
[[nodiscard]] Splice insert_first(const TextLayout& layout, std::uint32_t parent, std::string_view item) {
    const XmlElementLocation& where = layout.location(parent);
    const std::optional<std::string_view> parent_indent = layout.indentation(where.start_offset);
    const std::string newline(layout.newline());
    const std::string outer = parent_indent ? std::string(*parent_indent) : std::string();
    // On its own line two spaces deeper, when the parent stands on its own
    // line; otherwise directly after the start tag.
    const std::string opening = parent_indent ? newline + outer + std::string(child_indent) : std::string();
    const std::string closing = parent_indent ? newline + outer : std::string();
    Splice splice{};
    if (where.empty_element_tag) {
        // "/>" becomes ">", the child and the end tag.
        splice.begin = where.start_tag_end - 2U;
        splice.end = where.start_tag_end;
        splice.text = ">" + opening + std::string(item) + closing + "</" + layout.element(parent).name + ">";
        return splice;
    }
    splice.begin = where.start_tag_end;
    splice.end = splice.begin;
    const bool adjacent = where.start_tag_end == where.end_tag_offset;
    splice.text = opening + std::string(item) + (adjacent ? closing : std::string());
    return splice;
}

// Removes an element: with its line when it is alone on it, else with the
// blanks before it.
[[nodiscard]] Splice remove_element(const FormDocument& document, const TextLayout& layout, std::uint32_t element) {
    const XmlElementLocation& where = layout.location(element);
    Splice splice{};
    const std::optional<std::string_view> indent = layout.indentation(where.start_offset);
    const std::optional<std::size_t> rest = layout.blank_rest_of_line(where.end_offset);
    if (indent && rest) {
        splice.begin = layout.line_start(where.start_offset);
        splice.end = *rest;
        return splice;
    }
    std::size_t from = where.start_offset;
    while (from > 0U && is_blank(document.text[from - 1U])) --from;
    splice.begin = from;
    splice.end = where.end_offset;
    return splice;
}

// Where an element goes among `parent`'s child objects at `position`; none
// when the position is past them.
[[nodiscard]] std::optional<Splice> place_object(const TextLayout& layout, std::uint32_t parent,
                                                 std::size_t position, std::string_view item) {
    const std::vector<std::uint32_t> objects = layout.children(parent, true);
    if (position > objects.size()) return std::nullopt;
    if (position < objects.size()) return insert_before(layout, objects[position], item);
    const std::vector<std::uint32_t> children = layout.children(parent, false);
    if (children.empty()) return insert_first(layout, parent, item);
    return insert_after(layout, children.back(), item);
}

} // namespace

FormEditOutcome open_form_document(FormDocument& document, std::string file, std::string text,
                                   const FormSchema& schema, ThemeDiagnosticList& diagnostics) {
    FormDocument scratch{};
    scratch.file = std::move(file);
    scratch.text = std::move(text);
    if (!read_document(scratch, schema, false, diagnostics)) return FormEditOutcome::refused;
    document = std::move(scratch);
    return FormEditOutcome::edited;
}

FormEditOutcome set_form_property(FormDocument& document, std::string_view object_id, std::string_view property,
                                  std::string_view value, const FormSchema& schema, ThemeDiagnosticList& diagnostics) {
    if (!property_name(property)) return FormEditOutcome::not_admitted;
    const TextLayout layout(document);
    const std::optional<std::uint32_t> object = layout.find_object(object_id);
    if (!object) return FormEditOutcome::object_missing;
    const std::optional<std::uint32_t> attribute = layout.find_attribute(*object, property);
    Splice splice{};
    if (attribute) {
        // Only the value's bytes change, escaped for the quote it has.
        const XmlAttributeLocation& written = layout.location(*object).attributes[*attribute];
        const char quote = document.text[written.value_end];
        splice.begin = written.value_offset;
        splice.end = written.value_end;
        splice.text = attribute_text(value, quote);
    } else {
        const std::string item = std::string(property) + "=\"" + attribute_text(value, '"') + "\"";
        splice = append_attribute(layout, *object, item);
    }
    const FormEditOutcome outcome = commit(document, spliced_text(document.text, splice), schema, diagnostics);
    return outcome;
}

FormEditOutcome reset_form_property(FormDocument& document, std::string_view object_id, std::string_view property,
                                    const FormSchema& schema, ThemeDiagnosticList& diagnostics) {
    if (!property_name(property)) return FormEditOutcome::not_admitted;
    const TextLayout layout(document);
    const std::optional<std::uint32_t> object = layout.find_object(object_id);
    if (!object) return FormEditOutcome::object_missing;
    const std::optional<std::uint32_t> attribute = layout.find_attribute(*object, property);
    if (!attribute) return FormEditOutcome::unchanged;
    const Splice splice = remove_attribute(layout, *object, *attribute);
    const FormEditOutcome outcome = commit(document, spliced_text(document.text, splice), schema, diagnostics);
    return outcome;
}

FormEditOutcome add_form_object(FormDocument& document, std::string_view parent_id, std::size_t position,
                                std::string_view type, std::string_view stable_id, const FormSchema& schema,
                                ThemeDiagnosticList& diagnostics) {
    if (!kebab_name(type) || type == form_span_type_name || !valid_stable_id(stable_id)) {
        return FormEditOutcome::not_admitted;
    }
    const TextLayout layout(document);
    const std::optional<std::uint32_t> parent = layout.find_object(parent_id);
    if (!parent) return FormEditOutcome::object_missing;
    const std::string item = "<" + std::string(type) + " id=\"" + attribute_text(stable_id, '"') + "\"/>";
    const std::optional<Splice> splice = place_object(layout, *parent, position, item);
    if (!splice) return FormEditOutcome::not_admitted;
    const FormEditOutcome outcome = commit(document, spliced_text(document.text, *splice), schema, diagnostics);
    return outcome;
}

FormEditOutcome remove_form_object(FormDocument& document, std::string_view object_id, const FormSchema& schema,
                                   ThemeDiagnosticList& diagnostics) {
    const TextLayout layout(document);
    const std::optional<std::uint32_t> object = layout.find_object(object_id);
    if (!object) return FormEditOutcome::object_missing;
    if (*object == 0U) return FormEditOutcome::not_admitted;
    const Splice splice = remove_element(document, layout, *object);
    const FormEditOutcome outcome = commit(document, spliced_text(document.text, splice), schema, diagnostics);
    return outcome;
}

FormEditOutcome move_form_object(FormDocument& document, std::string_view object_id, std::string_view parent_id,
                                 std::size_t position, const FormSchema& schema, ThemeDiagnosticList& diagnostics) {
    const TextLayout layout(document);
    const std::optional<std::uint32_t> object = layout.find_object(object_id);
    const std::optional<std::uint32_t> parent = layout.find_object(parent_id);
    if (!object || !parent) return FormEditOutcome::object_missing;
    if (*object == 0U || layout.within(*parent, *object)) return FormEditOutcome::not_admitted;
    const XmlElementLocation& moved = layout.location(*object);
    const std::string item = document.text.substr(moved.start_offset, moved.end_offset - moved.start_offset);

    // First the element leaves its place; the parent is found again in the
    // text without it. That text need only be read as XML: a document may
    // name the moved object elsewhere.
    FormDocument without{};
    without.file = document.file;
    without.text = spliced_text(document.text, remove_element(document, layout, *object));
    if (!read_document(without, schema, true, diagnostics)) return FormEditOutcome::refused;
    const TextLayout without_layout(without);
    const std::optional<std::uint32_t> new_parent = without_layout.find_object(parent_id);
    if (!new_parent) return FormEditOutcome::object_missing;
    const std::optional<Splice> splice = place_object(without_layout, *new_parent, position, item);
    if (!splice) return FormEditOutcome::not_admitted;
    const FormEditOutcome outcome = commit(document, spliced_text(without.text, *splice), schema, diagnostics);
    return outcome;
}

} // namespace gui_forms::form_language
