// A mockup's fixture data: read, checked and located (form_fixture.hpp).

#include "form_fixture.hpp"

#include "../form_model/form_model.hpp"
#include "../form_xml/form_xml.hpp"

#include "gui_forms/binding/value/binding_value.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace gui_forms::form_language {

namespace {

using theme_language::ByteOffset;
using theme_language::DiagnosticCode;
using theme_language::SourcePosition;
using theme_language::ThemeDiagnostic;

constexpr std::string_view fixture_root_name = "fixture";
constexpr std::string_view value_element = "value";
constexpr std::string_view records_element = "records";
constexpr std::string_view row_element = "row";
constexpr std::string_view tree_element = "tree";
constexpr std::string_view item_element = "item";

[[nodiscard]] bool xml_space(char byte) noexcept {
    const bool space = byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n';
    return space;
}

[[nodiscard]] bool xml_space_only(std::string_view text) noexcept {
    for (const char byte : text) {
        if (!xml_space(byte)) return false;
    }
    return true;
}

// Whether `name` is one C++ identifier: a source's name (FormSources).
[[nodiscard]] bool identifier(std::string_view name) noexcept {
    if (name.empty()) return false;
    for (std::size_t index = 0U; index < name.size(); ++index) {
        const char character = name[index];
        const bool letter = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                            character == '_';
        const bool digit = character >= '0' && character <= '9';
        if (!letter && !(digit && index != 0U)) return false;
    }
    return true;
}

// A list attribute's items (form_model.hpp, Strings): whitespace between
// items, a quoted item read as a form string. `valid` false with
// `escape` set for a refused escape, with it empty for an unclosed or
// unseparated quote.
struct ListReading final {
    std::vector<std::string> items{};
    std::string escape{};
    bool valid{};
};

[[nodiscard]] ListReading read_list(std::string_view text) {
    ListReading reading{};
    std::size_t position = 0U;
    while (position < text.size()) {
        if (xml_space(text[position])) {
            ++position;
            continue;
        }
        const std::size_t item_start = position;
        const char quote = text[position];
        if (quote != '"' && quote != '\'') {
            while (position < text.size() && !xml_space(text[position])) ++position;
            reading.items.emplace_back(text.substr(item_start, position - item_start));
            continue;
        }
        ++position;
        while (position < text.size() && text[position] != quote) {
            if (text[position] == '\\') ++position;
            ++position;
        }
        const bool closed = position < text.size();
        if (closed) ++position;
        const bool separated = position >= text.size() || xml_space(text[position]);
        if (!closed || !separated) return reading;
        FormStringReading item = read_form_string(text.substr(item_start, position - item_start));
        if (!item.valid) {
            reading.escape = std::move(item.escape);
            return reading;
        }
        reading.items.push_back(std::move(item.text));
    }
    reading.valid = true;
    return reading;
}

// One column spelled `name` or `name:kind`; none for an unknown kind.
[[nodiscard]] std::optional<BindingColumn> column_from(std::string_view written) {
    const std::size_t colon = written.find(':');
    BindingColumn column{};
    column.name = std::string(written.substr(0U, colon));
    if (column.name.empty()) return std::nullopt;
    if (colon == std::string_view::npos) {
        column.kind = BindingValueKind::text;
        return column;
    }
    const std::string_view kind = written.substr(colon + 1U);
    if (kind == "text") {
        column.kind = BindingValueKind::text;
    } else if (kind == "boolean") {
        column.kind = BindingValueKind::boolean;
    } else if (kind == "integer") {
        column.kind = BindingValueKind::signed_integer;
    } else if (kind == "number") {
        column.kind = BindingValueKind::number;
    } else {
        return std::nullopt;
    }
    return column;
}

// `text` as a cell of `kind`; none when it cannot be one.
[[nodiscard]] std::optional<BindingValue> cell_from(const std::string& text, BindingValueKind kind) {
    const BindingValue written{text};
    if (kind == BindingValueKind::text) return written;
    if (kind == BindingValueKind::boolean) {
        const stxlib::Optional<bool> read = binding_value_to_bool(written);
        if (read.absent()) return std::nullopt;
        const BindingValue value{*read.value()};
        return value;
    }
    if (kind == BindingValueKind::signed_integer) {
        const stxlib::Optional<std::int64_t> read = binding_value_to_signed(written);
        if (read.absent()) return std::nullopt;
        const BindingValue value{*read.value()};
        return value;
    }
    // The last kind column_from gives: a number.
    const stxlib::Optional<double> read = binding_value_to_number(written);
    if (read.absent()) return std::nullopt;
    const BindingValue value{*read.value()};
    return value;
}

// Whether `records` has a column named `name`, ASCII letters folded as
// BindingSchema folds them.
[[nodiscard]] bool column_name_repeated(const FixtureRecords& records, std::string_view name) noexcept {
    for (const BindingColumn& column : records.columns) {
        if (column.name.size() != name.size()) continue;
        bool same = true;
        for (std::size_t index = 0U; same && index < name.size(); ++index) {
            const char left = column.name[index];
            const char right = name[index];
            const char folded_left = left >= 'A' && left <= 'Z' ? static_cast<char>(left - 'A' + 'a') : left;
            const char folded_right = right >= 'A' && right <= 'Z' ? static_cast<char>(right - 'A' + 'a') : right;
            same = folded_left == folded_right;
        }
        if (same) return true;
    }
    return false;
}

// The admitted attributes of one element, each one's position among the
// element's attributes, none when not written.
struct ElementAttributes final {
    std::optional<std::uint32_t> name{};
    std::optional<std::uint32_t> value{};
    std::optional<std::uint32_t> columns{};
    std::optional<std::uint32_t> values{};
    std::optional<std::uint32_t> shown_in{};
    std::optional<std::uint32_t> id{};
    std::optional<std::uint32_t> text{};
    std::optional<std::uint32_t> expanded{};
};

class FixtureReader final {
public:
    FixtureReader(const theme_language::ThemeSource& source, const FormSyntax& syntax,
                  theme_language::ThemeDiagnosticList& diagnostics) noexcept
        : source_(source), syntax_(syntax), diagnostics_(diagnostics), cursor_(syntax.lines, source.text) {}

    // Fills `scratch`; returns whether nothing was refused.
    [[nodiscard]] bool run(FormFixture& scratch);

private:
    // Each reader is given the element and its position in
    // FormSyntax::locations, and leaves `next_` past its subtree.
    void read_value(const XmlElement& element, std::uint32_t index, FormFixture& scratch);
    void read_records(const XmlElement& element, std::uint32_t index, FormFixture& scratch);
    void read_row(const XmlElement& element, std::uint32_t index, FixtureRecords& records);
    void read_tree(const XmlElement& element, std::uint32_t index, FormFixture& scratch);
    // An item and, recursively, its items. Depth is bounded by the XML
    // reader's maximum depth.
    void read_item(const XmlElement& element, std::uint32_t index, std::size_t depth, FixtureTree& tree);
    // Skips the element `index` and its subtree.
    void skip(const XmlElement& element);
    // The attributes of `element` (position `index`) that `admitted` names,
    // in the order ElementAttributes lists; any other is reported (E804).
    [[nodiscard]] ElementAttributes attributes_of(const XmlElement& element, std::uint32_t index,
                                                  std::string_view admitted);
    // Reports text inside `element` (E821).
    void check_text(const XmlElement& element, std::uint32_t index);
    // The value of a required attribute, reporting its absence (E807).
    [[nodiscard]] std::optional<std::string_view> required(const XmlElement& element, std::uint32_t index,
                                                           std::optional<std::uint32_t> attribute,
                                                           std::string_view name);
    // A list attribute's items, reporting a refused escape (E815) or a
    // malformed quote (E807).
    [[nodiscard]] std::optional<std::vector<std::string>> list_of(const XmlElement& element, std::uint32_t index,
                                                                  std::uint32_t attribute);
    // Reads a name, reporting one that is no identifier (E807) or one used
    // before (E803).
    [[nodiscard]] bool admit_name(std::string_view name, std::uint32_t index, std::uint32_t attribute);
    [[nodiscard]] SourcePosition element_position(std::uint32_t index) noexcept;
    [[nodiscard]] SourcePosition attribute_position(std::uint32_t index, std::uint32_t attribute) noexcept;
    void report(DiagnosticCode code, SourcePosition position, std::string_view rule, std::string_view property,
                std::string detail);

    const theme_language::ThemeSource& source_;
    const FormSyntax& syntax_;
    theme_language::ThemeDiagnosticList& diagnostics_;
    theme_language::SourcePositionCursor cursor_;
    std::uint32_t next_{};
    std::unordered_set<std::string> names_{};
    std::unordered_set<std::string> item_ids_{};
    std::size_t item_count_{};
    bool defective_{};
};

bool FixtureReader::run(FormFixture& scratch) {
    const XmlElement& root = syntax_.root;
    next_ = 1U;
    if (root.name != fixture_root_name) {
        report(DiagnosticCode::form_root_invalid, element_position(0U), root.name, {},
               "a fixture's root is <fixture>");
        return false;
    }
    const XmlAttribute* declared = root.find_attribute(form_namespace_attribute);
    if (declared == nullptr || (*declared).value != fixture_namespace) {
        report(DiagnosticCode::form_xml_invalid, element_position(0U), root.name, form_namespace_attribute,
               "the root declares xmlns=\"" + std::string(fixture_namespace) + "\"");
    }
    for (std::uint32_t attribute = 0U; attribute < root.attributes.size(); ++attribute) {
        if (root.attributes[attribute].name == form_namespace_attribute) continue;
        report(DiagnosticCode::form_property_unknown, attribute_position(0U, attribute), root.name,
               root.attributes[attribute].name, "a fixture's root has only xmlns");
    }
    check_text(root, 0U);
    for (const XmlElement& child : root.children) {
        const std::uint32_t index = next_;
        if (child.name == value_element) {
            read_value(child, index, scratch);
        } else if (child.name == records_element) {
            read_records(child, index, scratch);
        } else if (child.name == tree_element) {
            read_tree(child, index, scratch);
        } else {
            report(DiagnosticCode::form_type_unknown, element_position(index), child.name, {},
                   "a fixture holds value, records and tree");
            skip(child);
        }
    }
    return !defective_;
}

void FixtureReader::read_value(const XmlElement& element, std::uint32_t index, FormFixture& scratch) {
    const ElementAttributes attributes = attributes_of(element, index, "name value");
    check_text(element, index);
    skip(element);
    const std::optional<std::string_view> name = required(element, index, attributes.name, "name");
    const std::optional<std::string_view> value = required(element, index, attributes.value, "value");
    if (!name || !value) return;
    if (!admit_name(*name, index, *attributes.name)) return;
    FixtureValue read{};
    read.name = std::string(*name);
    read.text = std::string(*value);
    read.position = element_position(index);
    scratch.values.push_back(std::move(read));
}

void FixtureReader::read_records(const XmlElement& element, std::uint32_t index, FormFixture& scratch) {
    const ElementAttributes attributes = attributes_of(element, index, "name columns shown-in");
    check_text(element, index);
    ++next_;
    FixtureRecords records{};
    records.position = element_position(index);
    const std::optional<std::string_view> name = required(element, index, attributes.name, "name");
    const std::optional<std::string_view> columns = required(element, index, attributes.columns, "columns");
    bool usable = name && columns && admit_name(*name, index, *attributes.name);
    if (name) records.name = std::string(*name);
    if (columns) {
        const std::optional<std::vector<std::string>> written = list_of(element, index, *attributes.columns);
        usable = usable && written.has_value();
        if (written) {
            for (const std::string& spelled : *written) {
                const std::optional<BindingColumn> column = column_from(spelled);
                if (!column || column_name_repeated(records, (*column).name)) {
                    report(DiagnosticCode::form_value_invalid, attribute_position(index, *attributes.columns),
                           element.name, "columns",
                           "a column is name or name:text, name:boolean, name:integer or name:number, once: " +
                               spelled);
                    usable = false;
                    continue;
                }
                records.columns.push_back(*column);
            }
        }
    }
    if (attributes.shown_in) {
        const std::optional<std::vector<std::string>> shown = list_of(element, index, *attributes.shown_in);
        if (shown) records.shown_in = *shown;
    }
    for (const XmlElement& child : element.children) {
        const std::uint32_t child_index = next_;
        if (child.name != row_element) {
            report(DiagnosticCode::form_type_unknown, element_position(child_index), child.name, {},
                   "records hold rows");
            skip(child);
            continue;
        }
        if (usable) {
            read_row(child, child_index, records);
        } else {
            static_cast<void>(attributes_of(child, child_index, "id values"));
            skip(child);
        }
    }
    if (usable) scratch.records.push_back(std::move(records));
}

void FixtureReader::read_row(const XmlElement& element, std::uint32_t index, FixtureRecords& records) {
    const ElementAttributes attributes = attributes_of(element, index, "id values");
    check_text(element, index);
    skip(element);
    BindingRow row{};
    row.stable_id = "row-" + std::to_string(records.rows.size() + 1U);
    if (attributes.id) row.stable_id = element.attributes[*attributes.id].value;
    for (const BindingRow& earlier : records.rows) {
        if (earlier.stable_id != row.stable_id) continue;
        report(DiagnosticCode::form_identity_repeated, element_position(index), element.name, "id",
               "a row's stable ID is used once in its records: " + row.stable_id);
        return;
    }
    if (attributes.values) {
        const std::optional<std::vector<std::string>> written = list_of(element, index, *attributes.values);
        if (!written) return;
        if ((*written).size() > records.columns.size()) {
            report(DiagnosticCode::form_value_invalid, attribute_position(index, *attributes.values), element.name,
                   "values", "more values than the records' columns");
            return;
        }
        for (std::size_t column = 0U; column < (*written).size(); ++column) {
            const std::optional<BindingValue> cell = cell_from((*written)[column], records.columns[column].kind);
            if (!cell) {
                report(DiagnosticCode::form_value_invalid, attribute_position(index, *attributes.values),
                       element.name, "values",
                       "column " + records.columns[column].name + " cannot take " + (*written)[column]);
                return;
            }
            row.values.push_back(*cell);
        }
    }
    records.rows.push_back(std::move(row));
}

void FixtureReader::read_tree(const XmlElement& element, std::uint32_t index, FormFixture& scratch) {
    const ElementAttributes attributes = attributes_of(element, index, "shown-in");
    check_text(element, index);
    ++next_;
    FixtureTree tree{};
    tree.position = element_position(index);
    const std::optional<std::string_view> shown = required(element, index, attributes.shown_in, "shown-in");
    if (shown) {
        const std::optional<std::vector<std::string>> named = list_of(element, index, *attributes.shown_in);
        if (named) tree.shown_in = *named;
    }
    for (const XmlElement& child : element.children) {
        const std::uint32_t child_index = next_;
        if (child.name != item_element) {
            report(DiagnosticCode::form_type_unknown, element_position(child_index), child.name, {},
                   "a tree holds items");
            skip(child);
            continue;
        }
        read_item(child, child_index, 0U, tree);
    }
    scratch.trees.push_back(std::move(tree));
}

void FixtureReader::read_item(const XmlElement& element, std::uint32_t index, std::size_t depth, FixtureTree& tree) {
    const ElementAttributes attributes = attributes_of(element, index, "id text expanded");
    check_text(element, index);
    ++next_;
    ++item_count_;
    FixtureTreeItem item{};
    item.depth = depth;
    item.stable_id = "item-" + std::to_string(item_count_);
    if (attributes.id) item.stable_id = element.attributes[*attributes.id].value;
    if (!item_ids_.insert(item.stable_id).second) {
        report(DiagnosticCode::form_identity_repeated, element_position(index), element.name, "id",
               "an item's stable ID is used once in the fixture: " + item.stable_id);
    }
    const std::optional<std::string_view> text = required(element, index, attributes.text, "text");
    if (text) item.text = std::string(*text);
    if (attributes.expanded) {
        const std::string& written = element.attributes[*attributes.expanded].value;
        item.expanded = written == "true";
        if (written != "true" && written != "false") {
            report(DiagnosticCode::form_value_invalid, attribute_position(index, *attributes.expanded),
                   element.name, "expanded", "expanded is true or false");
        }
    }
    item.expandable = false;
    for (const XmlElement& child : element.children) {
        if (child.name == item_element) item.expandable = true;
    }
    tree.items.push_back(std::move(item));
    for (const XmlElement& child : element.children) {
        const std::uint32_t child_index = next_;
        if (child.name != item_element) {
            report(DiagnosticCode::form_type_unknown, element_position(child_index), child.name, {},
                   "an item holds items");
            skip(child);
            continue;
        }
        read_item(child, child_index, depth + 1U, tree);
    }
}

void FixtureReader::skip(const XmlElement& element) {
    // The element itself, then every element below it, in a work list.
    std::vector<const XmlElement*> pending{};
    pending.push_back(&element);
    while (!pending.empty()) {
        const XmlElement* current = pending.back();
        pending.pop_back();
        ++next_;
        for (const XmlElement& child : (*current).children) pending.push_back(&child);
    }
}

ElementAttributes FixtureReader::attributes_of(const XmlElement& element, std::uint32_t index,
                                               std::string_view admitted) {
    ElementAttributes found{};
    for (std::uint32_t attribute = 0U; attribute < element.attributes.size(); ++attribute) {
        const std::string& name = element.attributes[attribute].name;
        // `admitted` lists names separated by single spaces; a name is
        // admitted when it is one of them whole.
        bool listed = false;
        std::size_t start = 0U;
        while (start <= admitted.size()) {
            const std::size_t end = admitted.find(' ', start);
            const std::string_view candidate =
                admitted.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
            if (candidate == name) listed = true;
            if (end == std::string_view::npos) break;
            start = end + 1U;
        }
        if (!listed) {
            report(DiagnosticCode::form_property_unknown, attribute_position(index, attribute), element.name, name,
                   std::string(element.name) + " takes " + std::string(admitted));
            continue;
        }
        if (name == "name") {
            found.name = attribute;
        } else if (name == "value") {
            found.value = attribute;
        } else if (name == "columns") {
            found.columns = attribute;
        } else if (name == "values") {
            found.values = attribute;
        } else if (name == "shown-in") {
            found.shown_in = attribute;
        } else if (name == "id") {
            found.id = attribute;
        } else if (name == "text") {
            found.text = attribute;
        } else {
            // The last name any element admits.
            found.expanded = attribute;
        }
    }
    return found;
}

void FixtureReader::check_text(const XmlElement& element, std::uint32_t index) {
    if (xml_space_only(element.text)) return;
    report(DiagnosticCode::form_xml_invalid, element_position(index), element.name, {},
           "a fixture's elements hold no text: write values in attributes");
}

std::optional<std::string_view> FixtureReader::required(const XmlElement& element, std::uint32_t index,
                                                        std::optional<std::uint32_t> attribute,
                                                        std::string_view name) {
    if (attribute) {
        const std::string_view value = element.attributes[*attribute].value;
        return value;
    }
    report(DiagnosticCode::form_value_invalid, element_position(index), element.name, name,
           std::string(element.name) + " needs " + std::string(name));
    return std::nullopt;
}

std::optional<std::vector<std::string>> FixtureReader::list_of(const XmlElement& element, std::uint32_t index,
                                                               std::uint32_t attribute) {
    const XmlAttribute& written = element.attributes[attribute];
    ListReading reading = read_list(written.value);
    if (reading.valid) return std::move(reading.items);
    if (!reading.escape.empty()) {
        report(DiagnosticCode::form_escape_invalid, attribute_position(index, attribute), element.name, written.name,
               reading.escape);
    } else {
        report(DiagnosticCode::form_value_invalid, attribute_position(index, attribute), element.name, written.name,
               "a quoted item ends with its quote and is followed by whitespace");
    }
    return std::nullopt;
}

bool FixtureReader::admit_name(std::string_view name, std::uint32_t index, std::uint32_t attribute) {
    if (!identifier(name)) {
        report(DiagnosticCode::form_value_invalid, attribute_position(index, attribute), syntax_.root.name, "name",
               "a name is one C++ identifier, as a binding's `to` writes it: " + std::string(name));
        return false;
    }
    if (!names_.insert(std::string(name)).second) {
        report(DiagnosticCode::form_identity_repeated, attribute_position(index, attribute), syntax_.root.name,
               "name", "a name is used once across the values and records: " + std::string(name));
        return false;
    }
    return true;
}

SourcePosition FixtureReader::element_position(std::uint32_t index) noexcept {
    if (index >= syntax_.locations.elements.size()) return SourcePosition{};
    const std::size_t offset = syntax_.locations.elements[index].start_offset;
    const SourcePosition position = cursor_.locate(ByteOffset{static_cast<std::uint32_t>(offset)});
    return position;
}

SourcePosition FixtureReader::attribute_position(std::uint32_t index, std::uint32_t attribute) noexcept {
    if (index >= syntax_.locations.elements.size()) return SourcePosition{};
    const detail::XmlElementLocation& element = syntax_.locations.elements[index];
    if (attribute >= element.attributes.size()) return element_position(index);
    const std::size_t offset = element.attributes[attribute].name_offset;
    const SourcePosition position = cursor_.locate(ByteOffset{static_cast<std::uint32_t>(offset)});
    return position;
}

void FixtureReader::report(DiagnosticCode code, SourcePosition position, std::string_view rule,
                           std::string_view property, std::string detail) {
    ThemeDiagnostic diagnostic{};
    diagnostic.code = code;
    diagnostic.file = std::string(source_.file);
    diagnostic.position = position;
    diagnostic.rule = std::string(rule);
    diagnostic.property = std::string(property);
    diagnostic.detail = std::move(detail);
    diagnostics_.add(std::move(diagnostic));
    defective_ = true;
}

} // namespace

FormFixtureOutcome read_form_fixture(const theme_language::ThemeSource& source, FormFixture& fixture,
                                     theme_language::ThemeDiagnosticList& diagnostics) {
    if (xml_space_only(source.text)) {
        fixture = FormFixture{};
        return FormFixtureOutcome::read;
    }
    FormSyntax syntax{};
    if (read_form_syntax(source, syntax, diagnostics) != FormSyntaxOutcome::read) return FormFixtureOutcome::refused;
    FormFixture scratch{};
    FixtureReader reader(source, syntax, diagnostics);
    if (!reader.run(scratch)) return FormFixtureOutcome::refused;
    fixture = std::move(scratch);
    return FormFixtureOutcome::read;
}

} // namespace gui_forms::form_language
