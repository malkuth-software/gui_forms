#include "resx_reader.hpp"

#include "csharp_lexer.hpp"

#include "gui_forms/xml/types/xml_types.hpp"
#include "gui_forms/xml/xml_reader/xml_reader.hpp"

#include <algorithm>
#include <utility>

namespace gui_forms::winforms_converter {

namespace {

[[nodiscard]] bool entry_name_less(const ResxEntry& left, const ResxEntry& right) noexcept {
    const bool less = left.name < right.name;
    return less;
}

struct EntryNameBelow final {
    [[nodiscard]] bool operator()(const ResxEntry& entry, std::string_view name) const noexcept {
        const bool below = std::string_view(entry.name) < name;
        return below;
    }
};

[[nodiscard]] std::string attribute_value(const XmlElement& element, std::string_view name) {
    const XmlAttribute* const found = element.find_attribute(name);
    if (found == nullptr) return std::string();
    std::string value = (*found).value;
    return value;
}

} // namespace

const ResxEntry* ResxDocument::find(std::string_view name) const& noexcept {
    const std::vector<ResxEntry>::const_iterator found =
        std::lower_bound(entries.begin(), entries.end(), name, EntryNameBelow{});
    if (found == entries.end() || (*found).name != name) return nullptr;
    return &*found;
}

ResxReadOutcome read_resx(std::string_view file, std::string_view text, ResxDocument& document,
                          ConversionDiagnosticList& diagnostics) {
    if (text.size() > maximum_source_bytes) {
        ConversionDiagnostic diagnostic{};
        diagnostic.code = ConversionCode::file_too_large;
        diagnostic.file = std::string(file);
        diagnostic.detail = std::to_string(text.size()) + " bytes; the converter reads at most " +
                            std::to_string(maximum_source_bytes);
        diagnostics.add(std::move(diagnostic));
        return ResxReadOutcome::refused;
    }
    XmlLimits limits{};
    limits.maximum_input_bytes = maximum_source_bytes;
    limits.maximum_value_bytes = maximum_resx_value_bytes;
    limits.maximum_elements = static_cast<std::uint32_t>(maximum_resx_entries * 4U);
    const XmlReadResult read = read_xml(text, limits);
    if (!read) {
        ConversionDiagnostic diagnostic{};
        diagnostic.code = ConversionCode::resx_invalid;
        diagnostic.file = std::string(file);
        diagnostic.line = read.position.line;
        diagnostic.column = read.position.column;
        diagnostic.detail = "not XML the reader admits: " + std::string(xml_error_name(read.error));
        diagnostics.add(std::move(diagnostic));
        return ResxReadOutcome::refused;
    }
    ResxDocument made{};
    made.file = std::string(file);
    // The entries are the root's `data` children; `resheader`, `metadata`
    // and the schema are not resources.
    for (const XmlElement& child : read.root.children) {
        if (child.name != "data") continue;
        if (made.entries.size() >= maximum_resx_entries) {
            ConversionDiagnostic diagnostic{};
            diagnostic.code = ConversionCode::limit_reached;
            diagnostic.file = std::string(file);
            diagnostic.detail = "more entries than the converter reads; the rest are left out";
            diagnostics.add(std::move(diagnostic));
            break;
        }
        ResxEntry entry{};
        entry.name = attribute_value(child, "name");
        entry.type = attribute_value(child, "type");
        entry.mime_type = attribute_value(child, "mimetype");
        for (const XmlElement& part : child.children) {
            if (part.name == "value") entry.value = part.text;
        }
        if (!entry.name.empty()) made.entries.push_back(std::move(entry));
    }
    // A name written twice keeps its last entry, as ResXResourceReader does.
    std::stable_sort(made.entries.begin(), made.entries.end(), entry_name_less);
    std::vector<ResxEntry> unique{};
    unique.reserve(made.entries.size());
    for (ResxEntry& entry : made.entries) {
        if (!unique.empty() && unique.back().name == entry.name) {
            unique.back() = std::move(entry);
        } else {
            unique.push_back(std::move(entry));
        }
    }
    made.entries = std::move(unique);
    document = std::move(made);
    return ResxReadOutcome::read;
}

} // namespace gui_forms::winforms_converter
