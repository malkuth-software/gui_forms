#include "form_document_diff.hpp"

#include "gui_forms/xml/types/xml_types.hpp"
#include "gui_forms/xml/xml_reader/xml_reader.hpp"

#include <algorithm>
#include <utility>

namespace gui_forms::winforms_converter {

namespace {

// One object of a document, flattened: its own attributes (less `id`),
// its bindings by property, and its text content.
struct FlatObject final {
    std::string id{};
    std::string type{};
    std::string parent{};
    std::size_t position{};
    std::vector<XmlAttribute> attributes{};
    // `property` and the rest of the element's attributes, joined.
    std::vector<XmlAttribute> bindings{};
    std::string spans{};
};

struct FlatBelow final {
    [[nodiscard]] bool operator()(const FlatObject& left, const FlatObject& right) const noexcept {
        const bool below = left.id < right.id;
        return below;
    }
};

struct AttributeBelow final {
    [[nodiscard]] bool operator()(const XmlAttribute& left, const XmlAttribute& right) const noexcept {
        const bool below = left.name < right.name;
        return below;
    }
};

[[nodiscard]] std::string attribute_or_empty(const XmlElement& element, std::string_view name) {
    const XmlAttribute* const found = element.find_attribute(name);
    if (found == nullptr) return std::string();
    std::string value = (*found).value;
    return value;
}

// Flattens the tree under `root` in explicit storage (the reader already
// bounds depth and element count).
void flatten(const XmlElement& root, std::vector<FlatObject>& out) {
    struct Pending final {
        const XmlElement* element{};
        std::string parent{};
        std::size_t position{};
    };
    std::vector<Pending> pending{};
    pending.push_back(Pending{&root, std::string(), 0U});
    while (!pending.empty()) {
        const Pending current = pending.back();
        pending.pop_back();
        const XmlElement& element = *current.element;
        FlatObject flat{};
        flat.id = attribute_or_empty(element, "id");
        flat.type = element.name;
        flat.parent = current.parent;
        flat.position = current.position;
        for (const XmlAttribute& attribute : element.attributes) {
            if (attribute.name != "id" && attribute.name != "xmlns") flat.attributes.push_back(attribute);
        }
        std::sort(flat.attributes.begin(), flat.attributes.end(), AttributeBelow{});
        std::size_t position = 0U;
        for (const XmlElement& child : element.children) {
            if (child.name == "bind") {
                XmlAttribute binding{};
                binding.name = attribute_or_empty(child, "property");
                for (const XmlAttribute& attribute : child.attributes) {
                    if (attribute.name == "property") continue;
                    if (!binding.value.empty()) binding.value.push_back(' ');
                    binding.value.append(attribute.name + "=" + attribute.value);
                }
                flat.bindings.push_back(std::move(binding));
                continue;
            }
            if (child.name == "span" || child.find_attribute("id") == nullptr) {
                flat.spans.append("<" + child.name + ">" + child.text);
                continue;
            }
            pending.push_back(Pending{&child, flat.id, position});
            position = position + 1U;
        }
        std::sort(flat.bindings.begin(), flat.bindings.end(), AttributeBelow{});
        if (!element.text.empty()) flat.spans.insert(0U, element.text);
        out.push_back(std::move(flat));
    }
    std::sort(out.begin(), out.end(), FlatBelow{});
}

class DiffWriter final {
public:
    explicit DiffWriter(FormDiff& diff) : diff_(diff) {}

    void add(FormDiffKind kind, const std::string& id, std::string name, std::string before, std::string after) {
        if (diff_.entries.size() >= maximum_form_diff_entries) {
            diff_.dropped = diff_.dropped + 1U;
            return;
        }
        FormDiffEntry entry{};
        entry.kind = kind;
        entry.id = id;
        entry.name = std::move(name);
        entry.before = std::move(before);
        entry.after = std::move(after);
        diff_.entries.push_back(std::move(entry));
    }

    // Both lists sorted by name.
    void compare(const std::string& id, const std::vector<XmlAttribute>& before,
                 const std::vector<XmlAttribute>& after, FormDiffKind added, FormDiffKind removed,
                 FormDiffKind changed) {
        std::size_t left = 0U;
        std::size_t right = 0U;
        while (left < before.size() || right < after.size()) {
            const bool take_left = right == after.size() ||
                                   (left < before.size() && before[left].name < after[right].name);
            const bool take_right = left == before.size() ||
                                    (right < after.size() && after[right].name < before[left].name);
            if (take_left) {
                add(removed, id, before[left].name, before[left].value, std::string());
                left = left + 1U;
            } else if (take_right) {
                add(added, id, after[right].name, std::string(), after[right].value);
                right = right + 1U;
            } else {
                if (before[left].value != after[right].value) {
                    add(changed, id, before[left].name, before[left].value, after[right].value);
                }
                left = left + 1U;
                right = right + 1U;
            }
        }
    }

private:
    FormDiff& diff_;
};

[[nodiscard]] std::string place_of(const FlatObject& object) {
    std::string place = object.parent + "#" + std::to_string(object.position);
    return place;
}

} // namespace

std::string_view form_diff_kind_name(FormDiffKind kind) noexcept {
    switch (kind) {
    case FormDiffKind::object_added: return "object_added";
    case FormDiffKind::object_removed: return "object_removed";
    case FormDiffKind::object_moved: return "object_moved";
    case FormDiffKind::object_retyped: return "object_retyped";
    case FormDiffKind::attribute_added: return "attribute_added";
    case FormDiffKind::attribute_removed: return "attribute_removed";
    case FormDiffKind::attribute_changed: return "attribute_changed";
    case FormDiffKind::binding_added: return "binding_added";
    case FormDiffKind::binding_removed: return "binding_removed";
    case FormDiffKind::binding_changed: return "binding_changed";
    }
    return "unknown";
}

FormDiffOutcome diff_form_documents(std::string_view before, std::string_view after, FormDiff& diff) {
    const XmlReadResult read_before = read_xml(before);
    const XmlReadResult read_after = read_xml(after);
    FormDiff made{};
    if (!read_before || !read_after) {
        const XmlReadResult& failed = !read_before ? read_before : read_after;
        made.problem = std::string(!read_before ? "the first" : "the second") + " document is not XML (" +
                       std::string(xml_error_name(failed.error)) + " at line " +
                       std::to_string(failed.position.line) + ")";
        diff = std::move(made);
        return FormDiffOutcome::refused;
    }
    std::vector<FlatObject> left{};
    std::vector<FlatObject> right{};
    flatten(read_before.root, left);
    flatten(read_after.root, right);
    DiffWriter writer(made);
    std::size_t at_left = 0U;
    std::size_t at_right = 0U;
    while (at_left < left.size() || at_right < right.size()) {
        const bool take_left = at_right == right.size() ||
                               (at_left < left.size() && left[at_left].id < right[at_right].id);
        const bool take_right = at_left == left.size() ||
                                (at_right < right.size() && right[at_right].id < left[at_left].id);
        if (take_left) {
            writer.add(FormDiffKind::object_removed, left[at_left].id, std::string(), left[at_left].type,
                       std::string());
            at_left = at_left + 1U;
            continue;
        }
        if (take_right) {
            writer.add(FormDiffKind::object_added, right[at_right].id, std::string(), std::string(),
                       right[at_right].type);
            at_right = at_right + 1U;
            continue;
        }
        const FlatObject& was = left[at_left];
        const FlatObject& now = right[at_right];
        if (was.type != now.type) writer.add(FormDiffKind::object_retyped, was.id, std::string(), was.type, now.type);
        if (was.parent != now.parent || was.position != now.position) {
            writer.add(FormDiffKind::object_moved, was.id, std::string(), place_of(was), place_of(now));
        }
        writer.compare(was.id, was.attributes, now.attributes, FormDiffKind::attribute_added,
                       FormDiffKind::attribute_removed, FormDiffKind::attribute_changed);
        writer.compare(was.id, was.bindings, now.bindings, FormDiffKind::binding_added,
                       FormDiffKind::binding_removed, FormDiffKind::binding_changed);
        if (was.spans != now.spans) {
            writer.add(FormDiffKind::attribute_changed, was.id, "#content", was.spans, now.spans);
        }
        at_left = at_left + 1U;
        at_right = at_right + 1U;
    }
    diff = std::move(made);
    return FormDiffOutcome::compared;
}

std::string form_diff_text(const FormDiff& diff) {
    std::string text{};
    for (const FormDiffEntry& entry : diff.entries) {
        text.append(form_diff_kind_name(entry.kind));
        text.push_back(' ');
        text.append(entry.id.empty() ? std::string("(no id)") : entry.id);
        if (!entry.name.empty()) {
            text.push_back(' ');
            text.append(entry.name);
        }
        text.append(": ");
        text.append(entry.before);
        text.append(" -> ");
        text.append(entry.after);
        text.push_back('\n');
    }
    if (diff.dropped != 0U) text.append("... and " + std::to_string(diff.dropped) + " more\n");
    return text;
}

} // namespace gui_forms::winforms_converter
