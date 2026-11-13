#include "gui_forms/docking/dock_layout/dock_layout.hpp"

#include "gui_forms/xml/xml_reader/xml_reader.hpp"
#include "gui_forms/xml/xml_writer/xml_writer.hpp"
#include "dock_layout_internal.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <ios>
#include <locale>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

// The saved form (format 2), one element per record:
//
//   <dock_layout format application_version next_id>
//     <main_site> [<window x y width height monitor maximized/>] NODE </main_site>
//     <floating_site id x y width height monitor maximized> NODE </floating_site>  ...
//     <auto_hidden edge extent neighbour side ratio [split split_ratio]> <tool_stack/> </auto_hidden>  ...
//     <panel_record id kind last> [<docked/>] [<floating/>] [<auto_hidden/>] </panel_record> ...
//   </dock_layout>
//
//   NODE: <split id axis ratio> NODE NODE </split>
//         <tool_stack id selected> <panel id/> ... </tool_stack>
//         <document_stack id selected> <panel id/> ... </document_stack>
//         <document_area id> [NODE] </document_area>
//
// Unknown elements and attributes (a newer format's additions) are reported
// and kept (DK decision 48): attached to the known element they were found
// on (DockLayoutState::unknown_fields) and written back there, the
// attributes after the known ones and each child element after as many known
// children as preceded it in the file. An unknown element that holds
// <panel> references is the exception: it is dropped, and its panels become
// placeholders, since the panels may later be placed where this format puts
// them and must not appear twice.

namespace gui_forms {

namespace {

using detail::DockRegion;

// Numbers --------------------------------------------------------------

// The shortest form that reads back to the same double; locale-free.
[[nodiscard]] std::string format_double(double value) {
    const double normalized = value == 0.0 ? 0.0 : value;
    std::array<char, 64> buffer{};
    const std::to_chars_result written =
        std::to_chars(buffer.data(), buffer.data() + buffer.size(), normalized);
    const std::string text(buffer.data(), written.ptr);
    return text;
}

// Reads a whole finite double in the C locale.
[[nodiscard]] std::optional<double> parse_double(std::string_view text) {
    if (text.empty()) return std::nullopt;
    std::istringstream input{std::string(text)};
    input.imbue(std::locale::classic());
    double value = 0.0;
    input >> std::noskipws >> value;
    if (input.fail() || input.peek() != std::char_traits<char>::eof()) return std::nullopt;
    if (!std::isfinite(value)) return std::nullopt;
    const std::optional<double> parsed = value;
    return parsed;
}

[[nodiscard]] std::optional<std::uint32_t> parse_unsigned(std::string_view text) {
    std::uint32_t value = 0;
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    const std::from_chars_result parsed = std::from_chars(begin, end, value);
    if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != end) return std::nullopt;
    const std::optional<std::uint32_t> result = value;
    return result;
}

// Names ----------------------------------------------------------------

[[nodiscard]] std::string_view side_name(DockSide side) noexcept {
    switch (side) {
    case DockSide::left: return "left";
    case DockSide::top: return "top";
    case DockSide::right: return "right";
    case DockSide::bottom: return "bottom";
    }
    return "left";
}

[[nodiscard]] std::optional<DockSide> parse_side(std::string_view text) noexcept {
    if (text == "left") return DockSide::left;
    if (text == "top") return DockSide::top;
    if (text == "right") return DockSide::right;
    if (text == "bottom") return DockSide::bottom;
    return std::nullopt;
}

[[nodiscard]] std::string_view kind_name(DockPanelKind kind) noexcept {
    const std::string_view name = kind == DockPanelKind::document ? "document" : "tool";
    return name;
}

[[nodiscard]] std::optional<DockPanelKind> parse_kind(std::string_view text) noexcept {
    if (text == "tool") return DockPanelKind::tool;
    if (text == "document") return DockPanelKind::document;
    return std::nullopt;
}

[[nodiscard]] std::string_view placement_name(DockPlacementKind placement) noexcept {
    switch (placement) {
    case DockPlacementKind::floating: return "floating";
    case DockPlacementKind::auto_hidden: return "auto_hidden";
    case DockPlacementKind::docked:
    case DockPlacementKind::hidden:
    case DockPlacementKind::absent: return "docked";
    }
    return "docked";
}

[[nodiscard]] std::optional<DockPlacementKind> parse_placement(std::string_view text) noexcept {
    if (text == "docked") return DockPlacementKind::docked;
    if (text == "floating") return DockPlacementKind::floating;
    if (text == "auto_hidden") return DockPlacementKind::auto_hidden;
    return std::nullopt;
}

[[nodiscard]] std::string_view boolean_name(bool value) noexcept {
    const std::string_view name = value ? "true" : "false";
    return name;
}

// Writing --------------------------------------------------------------

void add_attribute(XmlElement& element, std::string_view name, std::string_view value) {
    XmlAttribute attribute{};
    attribute.name.assign(name);
    attribute.value.assign(value);
    element.attributes.push_back(std::move(attribute));
}

void add_number(XmlElement& element, std::string_view name, std::uint32_t value) {
    const std::string text = std::to_string(value);
    add_attribute(element, name, text);
}

void add_double(XmlElement& element, std::string_view name, double value) {
    const std::string text = format_double(value);
    add_attribute(element, name, text);
}

void add_frame(XmlElement& element, Rect frame) {
    add_double(element, "x", frame.x);
    add_double(element, "y", frame.y);
    add_double(element, "width", frame.width);
    add_double(element, "height", frame.height);
}

[[nodiscard]] XmlElement element_named(std::string_view name) {
    XmlElement element{};
    element.name.assign(name);
    return element;
}

[[nodiscard]] DockUnknownOwner owner_of(DockUnknownOwnerKind kind, std::uint32_t id = 0,
                                        const DockPanelId& panel = DockPanelId{}) {
    DockUnknownOwner owner{};
    owner.kind = kind;
    owner.id = id;
    owner.panel = panel;
    return owner;
}

// The element an owner names, in words, for a diagnostic.
[[nodiscard]] std::string unknown_owner_text(const DockUnknownOwner& owner) {
    switch (owner.kind) {
    case DockUnknownOwnerKind::layout: return "dock_layout";
    case DockUnknownOwnerKind::main_site: return "main_site";
    case DockUnknownOwnerKind::window: return "main_site/window";
    case DockUnknownOwnerKind::floating_site: return "floating_site " + std::to_string(owner.id);
    case DockUnknownOwnerKind::auto_hidden: return "auto_hidden " + std::to_string(owner.id);
    case DockUnknownOwnerKind::node: return "node " + std::to_string(owner.id);
    case DockUnknownOwnerKind::panel: return "panel " + owner.panel.value;
    case DockUnknownOwnerKind::panel_record: return "panel_record " + owner.panel.value;
    case DockUnknownOwnerKind::docked_memory: return "panel_record " + owner.panel.value + "/docked";
    case DockUnknownOwnerKind::floating_memory: return "panel_record " + owner.panel.value + "/floating";
    case DockUnknownOwnerKind::auto_hidden_memory: return "panel_record " + owner.panel.value + "/auto_hidden";
    }
    return "?";
}

// Writes an element's kept unknown fields back: its unknown attributes after
// the known ones (a name the writer now writes itself wins), and each unknown
// child after as many known children as preceded it in the file.
void add_unknown_fields(const DockLayoutState& state, const DockUnknownOwner& owner, XmlElement& element) {
    const DockUnknownFields* fields = detail::find_unknown_fields(state, owner);
    if (fields == nullptr) return;
    for (const XmlAttribute& attribute : (*fields).attributes) {
        if (element.find_attribute(attribute.name) != nullptr) continue;
        element.attributes.push_back(attribute);
    }
    if ((*fields).children.empty()) return;
    std::vector<XmlElement> known = std::move(element.children);
    element.children.clear();
    element.children.reserve(known.size() + (*fields).children.size());
    std::size_t next = 0U;
    for (std::size_t index = 0; index <= known.size(); ++index) {
        while (next < (*fields).children.size() && (*fields).children[next].after_known <= index) {
            element.children.push_back((*fields).children[next].element);
            ++next;
        }
        if (index < known.size()) element.children.push_back(std::move(known[index]));
    }
    while (next < (*fields).children.size()) {
        element.children.push_back((*fields).children[next].element);
        ++next;
    }
}

// Recurses once per node level: a committed tree is at most
// dock_maximum_depth deep (the invariant check).
[[nodiscard]] XmlElement node_element(const DockLayoutState& state, DockNodeId id) {
    const DockNode& node = *detail::find_dock_node(state, id);
    XmlElement element{};
    if (node.kind == DockNodeKind::split) {
        element.name = "split";
        add_number(element, "id", node.id.value);
        add_attribute(element, "axis", node.axis == DockAxis::horizontal ? "horizontal" : "vertical");
        add_double(element, "ratio", node.ratio);
        element.children.push_back(node_element(state, node.first));
        element.children.push_back(node_element(state, node.second));
        add_unknown_fields(state, owner_of(DockUnknownOwnerKind::node, node.id.value), element);
        return element;
    }
    if (node.kind == DockNodeKind::document_area) {
        element.name = "document_area";
        add_number(element, "id", node.id.value);
        if (!(node.first == DockNodeId{})) element.children.push_back(node_element(state, node.first));
        add_unknown_fields(state, owner_of(DockUnknownOwnerKind::node, node.id.value), element);
        return element;
    }
    element.name = node.kind == DockNodeKind::tool_stack ? "tool_stack" : "document_stack";
    add_number(element, "id", node.id.value);
    add_number(element, "selected", node.selected);
    for (const DockPanelId& panel : node.panels) {
        XmlElement panel_element = element_named("panel");
        add_attribute(panel_element, "id", panel.value);
        add_unknown_fields(state, owner_of(DockUnknownOwnerKind::panel, 0, panel), panel_element);
        element.children.push_back(std::move(panel_element));
    }
    add_unknown_fields(state, owner_of(DockUnknownOwnerKind::node, node.id.value), element);
    return element;
}

[[nodiscard]] XmlElement record_element(const DockLayoutState& state, const DockPanelRecord& record) {
    XmlElement element = element_named("panel_record");
    add_attribute(element, "id", record.id.value);
    add_attribute(element, "kind", kind_name(record.kind));
    add_attribute(element, "last", placement_name(record.last_placement));
    if (record.docked.has_value()) {
        const DockDockedMemory& memory = *record.docked;
        XmlElement docked = element_named("docked");
        add_number(docked, "stack", memory.stack.value);
        add_number(docked, "index", memory.index);
        add_number(docked, "neighbour", memory.neighbour.value);
        add_attribute(docked, "side", side_name(memory.side));
        add_double(docked, "ratio", memory.ratio);
        add_unknown_fields(state, owner_of(DockUnknownOwnerKind::docked_memory, 0, record.id), docked);
        element.children.push_back(std::move(docked));
    }
    if (record.floating.has_value()) {
        const DockFloatingMemory& memory = *record.floating;
        XmlElement floating = element_named("floating");
        add_number(floating, "site", memory.site.value);
        add_frame(floating, memory.frame);
        add_attribute(floating, "monitor", memory.monitor_id);
        add_unknown_fields(state, owner_of(DockUnknownOwnerKind::floating_memory, 0, record.id), floating);
        element.children.push_back(std::move(floating));
    }
    if (record.auto_hidden.has_value()) {
        const DockAutoHideMemory& memory = *record.auto_hidden;
        XmlElement hidden = element_named("auto_hidden");
        add_number(hidden, "stack", memory.stack.value);
        add_attribute(hidden, "edge", side_name(memory.edge));
        add_double(hidden, "extent", memory.extent);
        add_unknown_fields(state, owner_of(DockUnknownOwnerKind::auto_hidden_memory, 0, record.id), hidden);
        element.children.push_back(std::move(hidden));
    }
    add_unknown_fields(state, owner_of(DockUnknownOwnerKind::panel_record, 0, record.id), element);
    return element;
}

[[nodiscard]] XmlElement layout_element(const DockLayoutState& state) {
    XmlElement root = element_named("dock_layout");
    add_number(root, "format", dock_layout_format_version);
    add_number(root, "application_version", state.application_version);
    add_number(root, "next_id", state.next_id);

    XmlElement main_site = element_named("main_site");
    if (state.main_window.has_value()) {
        const DockWindowFrame& window = *state.main_window;
        XmlElement window_element = element_named("window");
        add_frame(window_element, window.frame);
        add_attribute(window_element, "monitor", window.monitor_id);
        add_attribute(window_element, "maximized", boolean_name(window.maximized));
        add_unknown_fields(state, owner_of(DockUnknownOwnerKind::window), window_element);
        main_site.children.push_back(std::move(window_element));
    }
    main_site.children.push_back(node_element(state, state.main_root));
    add_unknown_fields(state, owner_of(DockUnknownOwnerKind::main_site), main_site);
    root.children.push_back(std::move(main_site));

    for (const DockFloatingSite& site : state.floating_sites) {
        XmlElement element = element_named("floating_site");
        add_number(element, "id", site.id.value);
        add_frame(element, site.frame);
        add_attribute(element, "monitor", site.monitor_id);
        add_attribute(element, "maximized", boolean_name(site.maximized));
        element.children.push_back(node_element(state, site.root));
        add_unknown_fields(state, owner_of(DockUnknownOwnerKind::floating_site, site.id.value), element);
        root.children.push_back(std::move(element));
    }
    for (const DockAutoHideEntry& entry : state.auto_hidden) {
        XmlElement element = element_named("auto_hidden");
        add_attribute(element, "edge", side_name(entry.edge));
        add_double(element, "extent", entry.extent);
        add_number(element, "neighbour", entry.neighbour.value);
        add_attribute(element, "side", side_name(entry.side));
        add_double(element, "ratio", entry.ratio);
        // Written only when there is a split to recreate, so a layout without
        // one keeps the form written before these attributes existed.
        if (!(entry.split == DockNodeId{})) {
            add_number(element, "split", entry.split.value);
            add_double(element, "split_ratio", entry.split_ratio);
        }
        element.children.push_back(node_element(state, entry.stack));
        add_unknown_fields(state, owner_of(DockUnknownOwnerKind::auto_hidden, entry.stack.value), element);
        root.children.push_back(std::move(element));
    }
    for (const DockPanelRecord& record : state.panels) root.children.push_back(record_element(state, record));
    add_unknown_fields(state, owner_of(DockUnknownOwnerKind::layout), root);
    return root;
}

// Migrations -----------------------------------------------------------

// Format 1 spelled a stack as <stack kind="tool|document">; format 2 names
// the two kinds of stack. A stack with another kind is left as it is and is
// then dropped as an unknown element.
// Recurses once per element level: at most dock_layout_maximum_xml_depth,
// enforced by the read.
void migrate_format_1(XmlElement& element) {
    if (element.name == "stack") {
        const XmlAttribute* kind = element.find_attribute("kind");
        const std::string kind_text = kind == nullptr ? std::string{} : (*kind).value;
        if (kind_text == "tool" || kind_text == "document") {
            element.name = kind_text == "tool" ? "tool_stack" : "document_stack";
            std::vector<XmlAttribute> kept{};
            for (XmlAttribute& attribute : element.attributes) {
                if (attribute.name != "kind") kept.push_back(std::move(attribute));
            }
            element.attributes = std::move(kept);
        }
    }
    for (XmlElement& child : element.children) migrate_format_1(child);
}

// Reading --------------------------------------------------------------

void report(std::vector<DockRestoreDiagnostic>& diagnostics, DockRestoreDiagnosticKind kind, std::string detail) {
    DockRestoreDiagnostic diagnostic{};
    diagnostic.kind = kind;
    diagnostic.detail = std::move(detail);
    diagnostics.push_back(std::move(diagnostic));
}

// Builds a state from a parsed document. Required values that are missing
// or invalid fail the whole load (the layout then keeps what it held);
// unknown elements and attributes are reported and kept with the element
// they were found on (DK decision 48; see unknown_element for the one
// exception).
class DockLayoutLoader final {
public:
    DockLayoutLoader(const XmlElement& root, std::vector<DockRestoreDiagnostic>& diagnostics)
        : root_(root), diagnostics_(diagnostics) {}

    bool load() {
        const DockUnknownOwner layout = owner_of(DockUnknownOwnerKind::layout);
        check_attributes(root_, {"format", "application_version", "next_id"}, layout);
        const XmlAttribute* application = root_.find_attribute("application_version");
        if (application != nullptr) {
            const std::optional<std::uint32_t> version = parse_unsigned((*application).value);
            if (!version.has_value()) return invalid("dock_layout", "application_version", (*application).value);
            state_.application_version = *version;
        }
        std::uint32_t saved_next = 1;
        const XmlAttribute* next = root_.find_attribute("next_id");
        if (next != nullptr) {
            const std::optional<std::uint32_t> value = parse_unsigned((*next).value);
            // An exhausted layout saves next_id at the limit; beyond it, the
            // count would wrap to the null ID.
            if (!value.has_value() || *value > dock_identity_limit) {
                return invalid("dock_layout", "next_id", (*next).value);
            }
            saved_next = *value;
        }
        bool main_seen = false;
        std::size_t known = 0U;
        for (const XmlElement& child : root_.children) {
            bool read = true;
            if (child.name == "main_site") {
                if (main_seen) return structure("dock_layout has two main sites");
                main_seen = true;
                read = load_main_site(child);
            } else if (child.name == "floating_site") {
                read = load_floating_site(child);
            } else if (child.name == "auto_hidden") {
                read = load_auto_hidden(child);
            } else if (child.name == "panel_record") {
                read = load_record(child);
            } else {
                unknown_element("dock_layout", child, layout, known);
                continue;
            }
            if (!read) return false;
            ++known;
        }
        if (!main_seen) return structure("dock_layout has no main site");
        // read_id keeps every ID below dock_identity_limit, so this sum is at
        // most the limit and cannot wrap.
        const std::uint32_t after_largest = largest_id_ + 1U;
        state_.next_id = std::max(saved_next, after_largest);
        ensure_records();
        return true;
    }

    [[nodiscard]] DockLayoutState take_state() {
        DockLayoutState state = std::move(state_);
        return state;
    }

private:
    void report(DockRestoreDiagnosticKind kind, std::string detail) {
        ::gui_forms::report(diagnostics_, kind, std::move(detail));
    }

    bool invalid(std::string_view where, std::string_view attribute, std::string_view value) {
        std::string detail(where);
        detail.append(": ");
        detail.append(attribute);
        detail.append(" \"");
        detail.append(value);
        detail.append("\"");
        report(DockRestoreDiagnosticKind::invalid_value, std::move(detail));
        return false;
    }

    bool structure(std::string detail) {
        report(DockRestoreDiagnosticKind::structure_invalid, std::move(detail));
        return false;
    }

    // The kept unknown fields of an owner, added on first use.
    DockUnknownFields& fields_of(const DockUnknownOwner& owner) {
        for (DockUnknownFields& fields : state_.unknown_fields) {
            if (fields.owner == owner) return fields;
        }
        DockUnknownFields fields{};
        fields.owner = owner;
        state_.unknown_fields.push_back(std::move(fields));
        return state_.unknown_fields.back();
    }

    // Reports and keeps the attributes a known element has beyond `known`.
    void check_attributes(const XmlElement& element, std::initializer_list<std::string_view> known,
                          const DockUnknownOwner& owner) {
        for (const XmlAttribute& attribute : element.attributes) {
            const bool listed = std::find(known.begin(), known.end(), attribute.name) != known.end();
            if (listed) continue;
            report(DockRestoreDiagnosticKind::unknown_attribute, element.name + ": " + attribute.name);
            fields_of(owner).attributes.push_back(attribute);
        }
    }

    // Recurses once per element level: at most dock_layout_maximum_xml_depth,
    // enforced by the read.
    [[nodiscard]] static bool holds_panels(const XmlElement& element) {
        if (element.name == "panel") return true;
        for (const XmlElement& child : element.children) {
            if (holds_panels(child)) return true;
        }
        return false;
    }

    // Reports an unknown child element of `owner`'s element, found after
    // `known_before` known children, and keeps it whole; one holding panel
    // references is dropped instead and its panels become placeholders.
    void unknown_element(std::string_view where, const XmlElement& element, const DockUnknownOwner& owner,
                         std::size_t known_before) {
        report(DockRestoreDiagnosticKind::unknown_element, std::string(where) + "/" + element.name);
        if (holds_panels(element)) {
            harvest_panels(element);
            return;
        }
        DockUnknownChild child{};
        child.after_known = known_before;
        child.element = element;
        fields_of(owner).children.push_back(std::move(child));
    }

    // Recurses once per element level: at most dock_layout_maximum_xml_depth,
    // enforced by the read.
    void harvest_panels(const XmlElement& element) {
        if (element.name == "panel") {
            const XmlAttribute* id = element.find_attribute("id");
            if (id != nullptr && !(*id).value.empty()) {
                const DockPanelId panel((*id).value);
                if (!seen(panel)) {
                    seen_panels_.push_back(panel);
                    orphans_.push_back(panel);
                    report(DockRestoreDiagnosticKind::panel_moved_to_placeholder, panel.value);
                }
            }
        }
        for (const XmlElement& child : element.children) harvest_panels(child);
    }

    [[nodiscard]] bool seen(const DockPanelId& panel) const {
        const bool found = std::find(seen_panels_.begin(), seen_panels_.end(), panel) != seen_panels_.end();
        return found;
    }

    bool read_id(const XmlElement& element, std::uint32_t& id) {
        const XmlAttribute* attribute = element.find_attribute("id");
        if (attribute == nullptr) return invalid(element.name, "id", "");
        const std::optional<std::uint32_t> value = parse_unsigned((*attribute).value);
        if (!value.has_value() || *value == 0U || *value >= dock_identity_limit) {
            return invalid(element.name, "id", (*attribute).value);
        }
        const bool repeated = std::find(used_ids_.begin(), used_ids_.end(), *value) != used_ids_.end();
        if (repeated) return invalid(element.name, "id (repeated)", (*attribute).value);
        used_ids_.push_back(*value);
        largest_id_ = std::max(largest_id_, *value);
        id = *value;
        return true;
    }

    bool read_double(const XmlElement& element, std::string_view name, double& value) {
        const XmlAttribute* attribute = element.find_attribute(name);
        if (attribute == nullptr) return invalid(element.name, name, "");
        const std::optional<double> parsed = parse_double((*attribute).value);
        if (!parsed.has_value()) return invalid(element.name, name, (*attribute).value);
        value = *parsed;
        return true;
    }

    bool read_side(const XmlElement& element, std::string_view name, DockSide& side) {
        const XmlAttribute* attribute = element.find_attribute(name);
        if (attribute == nullptr) return invalid(element.name, name, "");
        const std::optional<DockSide> parsed = parse_side((*attribute).value);
        if (!parsed.has_value()) return invalid(element.name, name, (*attribute).value);
        side = *parsed;
        return true;
    }

    bool read_node_reference(const XmlElement& element, std::string_view name, DockNodeId& node) {
        const XmlAttribute* attribute = element.find_attribute(name);
        if (attribute == nullptr) return invalid(element.name, name, "");
        const std::optional<std::uint32_t> parsed = parse_unsigned((*attribute).value);
        if (!parsed.has_value()) return invalid(element.name, name, (*attribute).value);
        node = DockNodeId{*parsed};
        return true;
    }

    bool read_frame(const XmlElement& element, Rect& frame) {
        if (!read_double(element, "x", frame.x)) return false;
        if (!read_double(element, "y", frame.y)) return false;
        if (!read_double(element, "width", frame.width)) return false;
        if (!read_double(element, "height", frame.height)) return false;
        if (frame.width <= 0.0 || frame.height <= 0.0) return invalid(element.name, "width or height", "");
        return true;
    }

    bool read_boolean(const XmlElement& element, std::string_view name, bool& value) {
        const XmlAttribute* attribute = element.find_attribute(name);
        if (attribute == nullptr) return true;
        if ((*attribute).value == "true") {
            value = true;
        } else if ((*attribute).value == "false") {
            value = false;
        } else {
            return invalid(element.name, name, (*attribute).value);
        }
        return true;
    }

    [[nodiscard]] static std::string monitor_of(const XmlElement& element) {
        const XmlAttribute* attribute = element.find_attribute("monitor");
        std::string monitor = attribute == nullptr ? std::string{} : (*attribute).value;
        return monitor;
    }

    [[nodiscard]] static bool is_node_name(std::string_view name) noexcept {
        const bool node = name == "split" || name == "tool_stack" || name == "document_stack" ||
                          name == "document_area";
        return node;
    }

    // Reads one node and its subtree (element names a node: is_node_name);
    // node is none when it was dropped (an empty stack, or a split left with
    // nothing). A split left with one child is replaced by it.
    // load_node, load_split, load_document_area and load_children recurse
    // once per element level: at most dock_layout_maximum_xml_depth,
    // enforced by the read, about 1 KiB of stack per level.
    bool load_node(const XmlElement& element, DockNodeId& node) {
        node = DockNodeId{};
        if (element.name == "split") {
            const bool split = load_split(element, node);
            return split;
        }
        if (element.name == "document_area") {
            const bool area = load_document_area(element, node);
            return area;
        }
        const bool stack = load_stack(element, node);
        return stack;
    }

    bool load_split(const XmlElement& element, DockNodeId& node) {
        std::uint32_t id = 0;
        if (!read_id(element, id)) return false;
        const DockUnknownOwner owner = owner_of(DockUnknownOwnerKind::node, id);
        check_attributes(element, {"id", "axis", "ratio"}, owner);
        const XmlAttribute* axis = element.find_attribute("axis");
        const std::string axis_text = axis == nullptr ? std::string{} : (*axis).value;
        if (axis_text != "horizontal" && axis_text != "vertical") return invalid("split", "axis", axis_text);
        double ratio = 0.5;
        if (!read_double(element, "ratio", ratio)) return false;
        if (!(ratio > 0.0 && ratio < 1.0)) return invalid("split", "ratio", format_double(ratio));
        std::vector<DockNodeId> children{};
        if (!load_children(element, children, owner)) return false;
        if (children.size() > 2U) return structure("split " + std::to_string(id) + " has more than two children");
        if (children.size() < 2U) {
            if (children.size() == 1U) node = children[0];
            return true;
        }
        DockNode split{};
        split.id = DockNodeId{id};
        split.kind = DockNodeKind::split;
        split.axis = axis_text == "horizontal" ? DockAxis::horizontal : DockAxis::vertical;
        split.ratio = ratio;
        split.first = children[0];
        split.second = children[1];
        set_parent(children[0], split.id);
        set_parent(children[1], split.id);
        detail::insert_dock_node(state_, std::move(split));
        node = DockNodeId{id};
        return true;
    }

    bool load_document_area(const XmlElement& element, DockNodeId& node) {
        std::uint32_t id = 0;
        if (!read_id(element, id)) return false;
        const DockUnknownOwner owner = owner_of(DockUnknownOwnerKind::node, id);
        check_attributes(element, {"id"}, owner);
        std::vector<DockNodeId> children{};
        if (!load_children(element, children, owner)) return false;
        if (children.size() > 1U) return structure("the document area has more than one child");
        DockNode area{};
        area.id = DockNodeId{id};
        area.kind = DockNodeKind::document_area;
        if (children.size() == 1U) {
            area.first = children[0];
            set_parent(children[0], area.id);
        }
        detail::insert_dock_node(state_, std::move(area));
        node = DockNodeId{id};
        return true;
    }

    // A <tool_stack> or <document_stack>: its panels in order, a panel seen
    // before in the file reported and skipped; an empty stack is dropped.
    bool load_stack(const XmlElement& element, DockNodeId& node) {
        std::uint32_t id = 0;
        if (!read_id(element, id)) return false;
        const DockUnknownOwner owner = owner_of(DockUnknownOwnerKind::node, id);
        check_attributes(element, {"id", "selected"}, owner);
        std::uint32_t selected = 0;
        const XmlAttribute* selected_attribute = element.find_attribute("selected");
        if (selected_attribute != nullptr) {
            const std::optional<std::uint32_t> value = parse_unsigned((*selected_attribute).value);
            if (!value.has_value()) return invalid(element.name, "selected", (*selected_attribute).value);
            selected = *value;
        }
        DockNode stack{};
        stack.id = DockNodeId{id};
        stack.kind = element.name == "tool_stack" ? DockNodeKind::tool_stack : DockNodeKind::document_stack;
        std::size_t known = 0U;
        for (const XmlElement& child : element.children) {
            if (child.name != "panel") {
                unknown_element(element.name, child, owner, known);
                continue;
            }
            const XmlAttribute* panel_id = child.find_attribute("id");
            if (panel_id == nullptr || (*panel_id).value.empty()) return invalid("panel", "id", "");
            const DockPanelId panel((*panel_id).value);
            ++known;
            if (seen(panel)) {
                report(DockRestoreDiagnosticKind::duplicate_panel, panel.value);
                continue;
            }
            const DockUnknownOwner panel_owner = owner_of(DockUnknownOwnerKind::panel, 0, panel);
            check_attributes(child, {"id"}, panel_owner);
            // A <panel> has no known children: any it has are kept whole.
            for (const XmlElement& nested : child.children) unknown_element("panel", nested, panel_owner, 0U);
            seen_panels_.push_back(panel);
            stack.panels.push_back(panel);
        }
        if (stack.panels.empty()) return true;
        if (selected >= stack.panels.size()) {
            report(DockRestoreDiagnosticKind::invalid_value,
                   element.name + " " + std::to_string(id) + ": selected past its end");
            selected = 0;
        }
        stack.selected = selected;
        detail::insert_dock_node(state_, std::move(stack));
        node = DockNodeId{id};
        return true;
    }

    bool load_children(const XmlElement& element, std::vector<DockNodeId>& children, const DockUnknownOwner& owner) {
        std::size_t known = 0U;
        for (const XmlElement& child : element.children) {
            if (!is_node_name(child.name)) {
                unknown_element(element.name, child, owner, known);
                continue;
            }
            DockNodeId node{};
            if (!load_node(child, node)) return false;
            if (!(node == DockNodeId{})) children.push_back(node);
            ++known;
        }
        return true;
    }

    void set_parent(DockNodeId child, DockNodeId parent) {
        DockNode* node = detail::find_dock_node(state_, child);
        if (node != nullptr) (*node).parent = parent;
    }

    bool load_main_site(const XmlElement& element) {
        const DockUnknownOwner owner = owner_of(DockUnknownOwnerKind::main_site);
        check_attributes(element, {}, owner);
        std::vector<DockNodeId> roots{};
        std::size_t known = 0U;
        for (const XmlElement& child : element.children) {
            if (child.name == "window") {
                const DockUnknownOwner window_owner = owner_of(DockUnknownOwnerKind::window);
                check_attributes(child, {"x", "y", "width", "height", "monitor", "maximized"}, window_owner);
                for (const XmlElement& nested : child.children) unknown_element("window", nested, window_owner, 0U);
                DockWindowFrame window{};
                if (!read_frame(child, window.frame)) return false;
                window.monitor_id = monitor_of(child);
                if (!read_boolean(child, "maximized", window.maximized)) return false;
                state_.main_window = std::move(window);
                ++known;
                continue;
            }
            if (!is_node_name(child.name)) {
                unknown_element("main_site", child, owner, known);
                continue;
            }
            DockNodeId node{};
            if (!load_node(child, node)) return false;
            if (!(node == DockNodeId{})) roots.push_back(node);
            ++known;
        }
        if (roots.size() != 1U) return structure("the main site does not hold exactly one root");
        state_.main_root = roots[0];
        return true;
    }

    bool load_floating_site(const XmlElement& element) {
        std::uint32_t id = 0;
        if (!read_id(element, id)) return false;
        const DockUnknownOwner owner = owner_of(DockUnknownOwnerKind::floating_site, id);
        check_attributes(element, {"id", "x", "y", "width", "height", "monitor", "maximized"}, owner);
        DockFloatingSite site{};
        site.id = DockSiteId{id};
        if (!read_frame(element, site.frame)) return false;
        site.monitor_id = monitor_of(element);
        if (!read_boolean(element, "maximized", site.maximized)) return false;
        if (state_.floating_sites.size() >= dock_maximum_floating_sites) {
            report(DockRestoreDiagnosticKind::floating_site_limit, "floating_site " + std::to_string(id));
            harvest_panels(element);
            return true;
        }
        std::vector<DockNodeId> roots{};
        if (!load_children(element, roots, owner)) return false;
        if (roots.size() > 1U) return structure("floating site " + std::to_string(id) + " has more than one root");
        if (roots.empty()) return true;
        const DockNode& root = *detail::find_dock_node(state_, roots[0]);
        if (root.kind == DockNodeKind::document_area) return structure("a document area in a floating site");
        site.root = roots[0];
        state_.floating_sites.push_back(std::move(site));
        return true;
    }

    bool load_auto_hidden(const XmlElement& element) {
        // The entry is known by its stack: the ID of its stack element, read
        // ahead (an entry left without a stack is dropped, and its unknown
        // fields with it).
        std::uint32_t stack_id = 0;
        for (const XmlElement& child : element.children) {
            if (!is_node_name(child.name)) continue;
            const XmlAttribute* id = child.find_attribute("id");
            const std::optional<std::uint32_t> parsed = id == nullptr ? std::nullopt : parse_unsigned((*id).value);
            if (parsed.has_value()) stack_id = *parsed;
            break;
        }
        const DockUnknownOwner owner = owner_of(DockUnknownOwnerKind::auto_hidden, stack_id);
        check_attributes(element, {"edge", "extent", "neighbour", "side", "ratio", "split", "split_ratio"}, owner);
        DockAutoHideEntry entry{};
        if (!read_side(element, "edge", entry.edge)) return false;
        if (!read_double(element, "extent", entry.extent)) return false;
        if (entry.extent <= 0.0) return invalid("auto_hidden", "extent", format_double(entry.extent));
        if (!read_node_reference(element, "neighbour", entry.neighbour)) return false;
        if (!read_side(element, "side", entry.side)) return false;
        if (!read_double(element, "ratio", entry.ratio)) return false;
        // Optional: the split to recreate on unpinning.
        if (element.find_attribute("split") != nullptr) {
            if (!read_node_reference(element, "split", entry.split)) return false;
            if (!read_double(element, "split_ratio", entry.split_ratio)) return false;
            if (!(entry.split_ratio > 0.0 && entry.split_ratio < 1.0)) {
                return invalid("auto_hidden", "split_ratio", format_double(entry.split_ratio));
            }
        }
        std::vector<DockNodeId> stacks{};
        if (!load_children(element, stacks, owner)) return false;
        if (stacks.size() > 1U) return structure("an auto-hidden entry holds more than one stack");
        if (stacks.empty()) return true;
        const DockNode& stack = *detail::find_dock_node(state_, stacks[0]);
        if (stack.kind != DockNodeKind::tool_stack) return structure("an auto-hidden entry is not a tool stack");
        entry.stack = stacks[0];
        state_.auto_hidden.push_back(entry);
        return true;
    }

    bool load_record(const XmlElement& element) {
        const XmlAttribute* id = element.find_attribute("id");
        if (id == nullptr || (*id).value.empty()) return invalid("panel_record", "id", "");
        DockPanelRecord record{};
        record.id = DockPanelId((*id).value);
        if (detail::find_dock_record(state_, record.id) != nullptr) {
            report(DockRestoreDiagnosticKind::duplicate_panel, "panel_record " + record.id.value);
            return true;
        }
        const DockUnknownOwner owner = owner_of(DockUnknownOwnerKind::panel_record, 0, record.id);
        check_attributes(element, {"id", "kind", "last"}, owner);
        const XmlAttribute* kind = element.find_attribute("kind");
        const std::optional<DockPanelKind> parsed_kind =
            kind == nullptr ? std::nullopt : parse_kind((*kind).value);
        if (!parsed_kind.has_value()) return invalid("panel_record", "kind", kind == nullptr ? "" : (*kind).value);
        record.kind = *parsed_kind;
        const XmlAttribute* last = element.find_attribute("last");
        const std::optional<DockPlacementKind> parsed_last =
            last == nullptr ? std::nullopt : parse_placement((*last).value);
        if (!parsed_last.has_value()) return invalid("panel_record", "last", last == nullptr ? "" : (*last).value);
        record.last_placement = *parsed_last;
        std::size_t known = 0U;
        for (const XmlElement& child : element.children) {
            const bool memory_element = child.name == "docked" || child.name == "floating" ||
                                        child.name == "auto_hidden";
            if (!memory_element) {
                unknown_element("panel_record", child, owner, known);
                continue;
            }
            ++known;
            DockUnknownOwnerKind memory_kind = DockUnknownOwnerKind::docked_memory;
            if (child.name == "floating") memory_kind = DockUnknownOwnerKind::floating_memory;
            if (child.name == "auto_hidden") memory_kind = DockUnknownOwnerKind::auto_hidden_memory;
            const DockUnknownOwner memory_owner = owner_of(memory_kind, 0, record.id);
            for (const XmlElement& nested : child.children) unknown_element(child.name, nested, memory_owner, 0U);
            if (child.name == "docked") {
                check_attributes(child, {"stack", "index", "neighbour", "side", "ratio"}, memory_owner);
                DockDockedMemory memory{};
                if (!read_node_reference(child, "stack", memory.stack)) return false;
                const XmlAttribute* index = child.find_attribute("index");
                const std::optional<std::uint32_t> parsed_index =
                    index == nullptr ? std::nullopt : parse_unsigned((*index).value);
                if (!parsed_index.has_value()) return invalid("docked", "index", index == nullptr ? "" : (*index).value);
                memory.index = *parsed_index;
                if (!read_node_reference(child, "neighbour", memory.neighbour)) return false;
                if (!read_side(child, "side", memory.side)) return false;
                if (!read_double(child, "ratio", memory.ratio)) return false;
                record.docked = memory;
            } else if (child.name == "floating") {
                check_attributes(child, {"site", "x", "y", "width", "height", "monitor"}, memory_owner);
                DockFloatingMemory memory{};
                DockNodeId site{};
                if (!read_node_reference(child, "site", site)) return false;
                memory.site = DockSiteId{site.value};
                if (!read_frame(child, memory.frame)) return false;
                memory.monitor_id = monitor_of(child);
                record.floating = std::move(memory);
            } else {
                check_attributes(child, {"stack", "edge", "extent"}, memory_owner);
                DockAutoHideMemory memory{};
                if (!read_node_reference(child, "stack", memory.stack)) return false;
                if (!read_side(child, "edge", memory.edge)) return false;
                if (!read_double(child, "extent", memory.extent)) return false;
                record.auto_hidden = memory;
            }
        }
        detail::insert_dock_record(state_, std::move(record));
        return true;
    }

    // Every panel in a stack, and every panel kept from a dropped element,
    // needs a record; one the file did not have takes its stack's kind.
    void ensure_records() {
        for (const DockNode& node : state_.nodes) {
            if (!detail::is_dock_stack(node.kind)) continue;
            const DockPanelKind kind = node.kind == DockNodeKind::tool_stack ? DockPanelKind::tool : DockPanelKind::document;
            for (const DockPanelId& panel : node.panels) {
                if (detail::find_dock_record(state_, panel) != nullptr) continue;
                DockPanelRecord record{};
                record.id = panel;
                record.kind = kind;
                pending_records_.push_back(std::move(record));
            }
        }
        for (const DockPanelId& panel : orphans_) {
            if (detail::find_dock_record(state_, panel) != nullptr) continue;
            DockPanelRecord record{};
            record.id = panel;
            pending_records_.push_back(std::move(record));
        }
        for (DockPanelRecord& record : pending_records_) {
            if (detail::find_dock_record(state_, record.id) != nullptr) continue;
            detail::insert_dock_record(state_, std::move(record));
        }
        pending_records_.clear();
    }

    const XmlElement& root_;
    std::vector<DockRestoreDiagnostic>& diagnostics_;
    DockLayoutState state_{};
    std::vector<std::uint32_t> used_ids_{};
    std::uint32_t largest_id_{0};
    std::vector<DockPanelId> seen_panels_{};
    std::vector<DockPanelId> orphans_{};
    std::vector<DockPanelRecord> pending_records_{};
};

[[nodiscard]] double overlap_area(Rect left, Rect right) noexcept {
    const Rect shared = Rect::intersection(left, right);
    const double area = shared.area();
    return area;
}

// Applies the monitor fallback to one saved frame and reports what moved.
void place_frame(Rect& frame, std::string& monitor_id, std::string_view what, const DockRestoreOptions& options,
                 std::vector<DockRestoreDiagnostic>& diagnostics) {
    if (options.monitors.empty()) return;
    const DockMonitorPlacement placement = place_on_monitor(frame, monitor_id, options.monitors, options.reach);
    if (placement.match != DockMonitorMatch::same_id) {
        report(diagnostics, DockRestoreDiagnosticKind::monitor_fallback,
               std::string(what) + ": monitor \"" + monitor_id + "\" is not connected; placed on \"" +
                   placement.monitor_id + "\"");
    }
    if (placement.clamped) {
        report(diagnostics, DockRestoreDiagnosticKind::frame_clamped, std::string(what));
    }
    frame = placement.frame;
    monitor_id = placement.monitor_id;
}

} // namespace

XmlLimits dock_layout_xml_limits() noexcept {
    XmlLimits limits{};
    limits.maximum_depth = dock_layout_maximum_xml_depth;
    return limits;
}

DockMonitorPlacement place_on_monitor(Rect frame, std::string_view monitor_id,
                                      std::span<const DockMonitor> monitors, const DockCaptionReach& reach) {
    DockMonitorPlacement placement{};
    placement.frame = frame;
    placement.monitor_id.assign(monitor_id);
    if (monitors.empty()) return placement;
    const DockMonitor* chosen = nullptr;
    for (const DockMonitor& monitor : monitors) {
        if (monitor.id == monitor_id) {
            chosen = &monitor;
            placement.match = DockMonitorMatch::same_id;
            break;
        }
    }
    if (chosen == nullptr) {
        double best = 0.0;
        for (const DockMonitor& monitor : monitors) {
            const double area = overlap_area(frame, monitor.work_area);
            if (area > best) {
                best = area;
                chosen = &monitor;
            }
        }
        if (chosen != nullptr) placement.match = DockMonitorMatch::most_overlap;
    }
    if (chosen == nullptr) {
        chosen = &monitors[0];
        for (const DockMonitor& monitor : monitors) {
            if (monitor.primary) {
                chosen = &monitor;
                break;
            }
        }
        placement.match = DockMonitorMatch::primary;
    }
    const Rect area = (*chosen).work_area;
    // Keep the caption's full height and visible_width of its width inside
    // the work area; a frame smaller than that only needs to be inside.
    const double reach_width = std::min(reach.visible_width, frame.width);
    const double reach_height = std::min(reach.caption_height, frame.height);
    const double lowest_x = area.x - (frame.width - reach_width);
    const double highest_x = std::max(lowest_x, area.right() - reach_width);
    const double lowest_y = area.y;
    const double highest_y = std::max(lowest_y, area.bottom() - reach_height);
    Rect placed = frame;
    placed.x = std::clamp(frame.x, lowest_x, highest_x);
    placed.y = std::clamp(frame.y, lowest_y, highest_y);
    placement.clamped = !(placed == frame);
    placement.frame = placed;
    placement.monitor_id = (*chosen).id;
    return placement;
}

bool rename_dock_panel(DockLayoutState& state, const DockPanelId& from, const DockPanelId& to) {
    if (to.value.empty() || from == to) return false;
    const DockPanelLocation from_location = detail::locate_dock_panel(state, from);
    const DockPanelLocation to_location = detail::locate_dock_panel(state, to);
    if (from_location.placement == DockPlacementKind::absent) return false;
    if (to_location.placement != DockPlacementKind::absent) return false;
    for (DockNode& node : state.nodes) {
        for (DockPanelId& panel : node.panels) {
            if (panel == from) panel = to;
        }
    }
    const DockPanelRecord* record = detail::find_dock_record(state, from);
    if (record != nullptr) {
        DockPanelRecord renamed = *record;
        renamed.id = to;
        detail::erase_dock_record(state, from);
        detail::insert_dock_record(state, std::move(renamed));
    }
    // Kept unknown fields follow the panel.
    for (DockUnknownFields& fields : state.unknown_fields) {
        if (fields.owner.panel == from) fields.owner.panel = to;
    }
    return true;
}

DockLayoutWriteResult DockLayout::write_xml() const {
    const XmlElement root = layout_element(state_);
    const XmlWriteOptions options{};
    XmlWriteResult written = ::gui_forms::write_xml(root, options, dock_layout_xml_limits());
    DockLayoutWriteResult result{};
    result.error = written.error;
    result.text = std::move(written.text);
    return result;
}

DockRestoreResult DockLayout::restore_xml(std::string_view xml, const DockRestoreOptions& options) {
    DockRestoreResult result{};
    std::vector<DockRestoreDiagnostic>& diagnostics = result.diagnostics;

    // 1. Read and validate the whole record before changing anything. The
    // loader below recurses once per element level, so the depth is capped
    // before the read (HOUSE_STYLE section 1).
    XmlLimits limits = options.limits;
    limits.maximum_depth = std::min(options.limits.maximum_depth, dock_layout_maximum_xml_depth);
    XmlReadResult read = read_xml(xml, limits);
    if (!read) {
        report(diagnostics, DockRestoreDiagnosticKind::unreadable_xml,
               std::string(xml_error_name(read.error)) + " at line " + std::to_string(read.position.line) +
                   ", column " + std::to_string(read.position.column));
        return result;
    }
    XmlElement root = std::move(read.root);
    if (root.name != "dock_layout") {
        report(diagnostics, DockRestoreDiagnosticKind::not_a_layout, root.name);
        return result;
    }

    // 2. The framework's format version: migrate an older one step by step,
    // read a newer one best-effort.
    const XmlAttribute* format_attribute = root.find_attribute("format");
    const std::optional<std::uint32_t> format =
        format_attribute == nullptr ? std::nullopt : parse_unsigned((*format_attribute).value);
    if (!format.has_value() || *format == 0U) {
        report(diagnostics, DockRestoreDiagnosticKind::unsupported_format_version,
               format_attribute == nullptr ? std::string("missing") : (*format_attribute).value);
        return result;
    }
    if (*format == 1U) {
        migrate_format_1(root);
        report(diagnostics, DockRestoreDiagnosticKind::older_format_migrated, "format 1 to 2");
    }
    if (*format > dock_layout_format_version) {
        report(diagnostics, DockRestoreDiagnosticKind::newer_format_read, "format " + std::to_string(*format));
    }
    DockLayoutLoader loader(root, diagnostics);
    if (!loader.load()) return result;
    DockLayoutState state = loader.take_state();

    // 3. The application's layout version and its upgrade hook.
    if (state.application_version != options.application_version) {
        if (options.upgrade.upgrade != nullptr) {
            const DockUpgradeDecision decision =
                options.upgrade.upgrade(options.upgrade.context, state.application_version, state);
            if (decision == DockUpgradeDecision::reject) {
                report(diagnostics, DockRestoreDiagnosticKind::application_rejected,
                       "application version " + std::to_string(state.application_version));
                return result;
            }
        }
        report(diagnostics, DockRestoreDiagnosticKind::application_version_upgraded,
               std::to_string(state.application_version) + " to " + std::to_string(options.application_version));
        state.application_version = options.application_version;
    }

    // 4. Panel identities: an unregistered panel, or one whose registered
    // kind no longer fits its stack, becomes a placeholder.
    std::vector<DockPanelId> placed{};
    for (const DockNode& node : state.nodes) {
        if (!detail::is_dock_stack(node.kind)) continue;
        for (const DockPanelId& panel : node.panels) placed.push_back(panel);
    }
    for (DockPanelRecord& record : state.panels) {
        const DockPanelDescriptor* descriptor = find_descriptor(record.id);
        if (descriptor != nullptr) record.kind = (*descriptor).kind;
    }
    for (const DockPanelId& panel : placed) {
        const DockPanelDescriptor* descriptor = find_descriptor(panel);
        if (descriptor == nullptr) {
            detail::detach_dock_panel(state, panel);
            report(diagnostics, DockRestoreDiagnosticKind::unknown_panel_kept_as_placeholder, panel.value);
            continue;
        }
        const DockPanelLocation location = detail::locate_dock_panel(state, panel);
        const DockNode& stack = *detail::find_dock_node(state, location.stack);
        const bool tool = (*descriptor).kind == DockPanelKind::tool;
        const bool fits = stack.kind == DockNodeKind::tool_stack ? tool : (!tool || (*descriptor).dock_as_document);
        if (!fits) {
            detail::detach_dock_panel(state, panel);
            report(diagnostics, DockRestoreDiagnosticKind::panel_moved_to_placeholder, panel.value);
        }
    }

    // 5. Monitors: the same ID, else the most overlap, else the primary;
    // clamped so the caption is reachable.
    if (state.main_window.has_value()) {
        DockWindowFrame& window = *state.main_window;
        place_frame(window.frame, window.monitor_id, "main window", options, diagnostics);
    }
    for (DockFloatingSite& site : state.floating_sites) {
        place_frame(site.frame, site.monitor_id, "floating site " + std::to_string(site.id.value), options,
                    diagnostics);
    }
    for (DockPanelRecord& record : state.panels) {
        if (!record.floating.has_value()) continue;
        DockFloatingMemory& memory = *record.floating;
        place_frame(memory.frame, memory.monitor_id, "remembered floating frame of " + record.id.value, options,
                    diagnostics);
    }

    const DockInvariantResult invariants = detail::check_dock_invariants(state);
    if (!invariants) {
        report(diagnostics, DockRestoreDiagnosticKind::structure_invalid, invariants.problem);
        return result;
    }
    // 6. Unknown fields whose element the restore removed (a placeholder, a
    // pruned stack) are not kept.
    std::vector<DockUnknownOwner> dropped{};
    detail::prune_unknown_fields(state, dropped);
    for (const DockUnknownOwner& owner : dropped) {
        report(diagnostics, DockRestoreDiagnosticKind::unknown_fields_dropped, unknown_owner_text(owner));
    }
    // 7. Apply as one change.
    commit(std::move(state), DockEditOrigin::program, 0, true, false);
    result.status = DockRestoreStatus::restored;
    return result;
}

} // namespace gui_forms
