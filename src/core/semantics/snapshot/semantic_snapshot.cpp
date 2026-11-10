#include "gui_forms/semantics.hpp"

#include <algorithm>
#include <sstream>

namespace gui_forms {
namespace {

// Whether an offset lies past `end` (fill_semantic_text_boundaries).
struct OffsetPast final {
    Utf8Offset end;
    bool operator()(Utf8Offset offset) const noexcept { return end < offset; }
};

void append_json_string(std::ostringstream& stream, std::string_view value) {
    stream << '"';
    for (const unsigned char character : value) {
        switch (character) {
        case '"': stream << "\\\""; break;
        case '\\': stream << "\\\\"; break;
        case '\n': stream << "\\n"; break;
        case '\r': stream << "\\r"; break;
        case '\t': stream << "\\t"; break;
        default:
            if (character < 0x20U) stream << "?";
            else stream << static_cast<char>(character);
            break;
        }
    }
    stream << '"';
}

// Appends `,"name":value`, or `,"name":null` when the field is absent.
void append_count(std::ostringstream& stream, const char* name, const std::optional<std::size_t>& value) {
    stream << ",\"" << name << "\":";
    if (value) stream << *value;
    else stream << "null";
}
void append_number(std::ostringstream& stream, const char* name, const std::optional<double>& value) {
    stream << ",\"" << name << "\":";
    if (value) stream << *value;
    else stream << "null";
}

void append_node(std::ostringstream& stream, const SemanticNode& node) {
    stream << "{\"runtime_id\":" << node.runtime_id << ",\"stable_id\":";
    append_json_string(stream, node.stable_id);
    stream << ",\"role\":";
    append_json_string(stream, semantic_role_name(node.role));
    stream << ",\"name\":";
    append_json_string(stream, node.name);
    stream << ",\"value\":";
    append_json_string(stream, node.value);
    stream << ",\"description\":";
    append_json_string(stream, node.description);
    stream << ",\"numeric_value\":";
    if (node.numeric_value) stream << *node.numeric_value;
    else stream << "null";
    stream << ",\"minimum_value\":";
    if (node.minimum_value) stream << *node.minimum_value;
    else stream << "null";
    stream << ",\"maximum_value\":";
    if (node.maximum_value) stream << *node.maximum_value;
    else stream << "null";
    stream << ",\"position_in_set\":";
    if (node.position_in_set) stream << *node.position_in_set;
    else stream << "null";
    stream << ",\"set_size\":";
    if (node.set_size) stream << *node.set_size;
    else stream << "null";
    stream << ",\"has_popup\":" << (node.has_popup ? "true" : "false");
    stream << ",\"controlled_panel\":";
    append_json_string(stream, node.controlled_panel);
    stream << ",\"orientation\":";
    if (node.orientation) append_json_string(stream, semantic_orientation_name(*node.orientation));
    else stream << "null";
    append_count(stream, "level", node.level);
    stream << ",\"modal\":" << (node.modal ? "true" : "false");
    append_count(stream, "row_index", node.row_index);
    append_count(stream, "column_index", node.column_index);
    append_count(stream, "row_count", node.row_count);
    append_count(stream, "column_count", node.column_count);
    stream << ",\"access_key\":";
    append_json_string(stream, node.access_key);
    append_number(stream, "small_step", node.small_step);
    append_number(stream, "large_step", node.large_step);
    stream << ",\"text_selection\":";
    if (node.text_selection) {
        stream << "{\"anchor\":" << (*node.text_selection).anchor.value() << ",\"caret\":"
               << (*node.text_selection).caret.value() << '}';
    } else {
        stream << "null";
    }
    stream << ",\"multiline\":" << (node.multiline ? "true" : "false");
    stream << ",\"tool_tip\":";
    append_json_string(stream, node.tool_tip);
    stream << ",\"announces_on_appearance\":" << (node.announces_on_appearance ? "true" : "false");
    stream << ",\"bounds\":[" << node.bounds.x << ',' << node.bounds.y << ','
           << node.bounds.width << ',' << node.bounds.height << "],\"states\":"
           << static_cast<std::uint32_t>(node.states) << ",\"actions\":[";
    for (std::size_t index = 0; index < node.actions.size(); ++index) {
        if (index != 0U) stream << ',';
        append_json_string(stream, semantic_action_name(node.actions[index]));
    }
    stream << "],\"children\":[";
    for (std::size_t index = 0; index < node.children.size(); ++index) {
        if (index != 0U) stream << ',';
        append_node(stream, node.children[index]);
    }
    stream << "]}";
}

} // namespace

const char* semantic_role_name(SemanticRole role) noexcept {
    switch (role) {
    case SemanticRole::generic: return "generic";
    case SemanticRole::group: return "group";
    case SemanticRole::static_text: return "static_text";
    case SemanticRole::button: return "button";
    case SemanticRole::check_box: return "check_box";
    case SemanticRole::radio_button: return "radio_button";
    case SemanticRole::toolbar: return "toolbar";
    case SemanticRole::radio_group: return "radio_group";
    case SemanticRole::link: return "link";
    case SemanticRole::text_box: return "text_box";
    case SemanticRole::list: return "list";
    case SemanticRole::list_item: return "list_item";
    case SemanticRole::check_list_item: return "check_list_item";
    case SemanticRole::combo_box: return "combo_box";
    case SemanticRole::slider: return "slider";
    case SemanticRole::scroll_bar: return "scroll_bar";
    case SemanticRole::progress_bar: return "progress_bar";
    case SemanticRole::image: return "image";
    case SemanticRole::tab_group: return "tab_group";
    case SemanticRole::tab: return "tab";
    case SemanticRole::split_pane: return "split_pane";
    case SemanticRole::numeric_field: return "numeric_field";
    case SemanticRole::tool_tip: return "tool_tip";
    case SemanticRole::date_picker: return "date_picker";
    case SemanticRole::calendar: return "calendar";
    case SemanticRole::date_cell: return "date_cell";
    case SemanticRole::property_grid: return "property_grid";
    case SemanticRole::property_group: return "property_group";
    case SemanticRole::property_row: return "property_row";
    case SemanticRole::menu_bar: return "menu_bar";
    case SemanticRole::menu_bar_item: return "menu_bar_item";
    case SemanticRole::menu: return "menu";
    case SemanticRole::menu_item: return "menu_item";
    case SemanticRole::separator: return "separator";
    case SemanticRole::split_button: return "split_button";
    case SemanticRole::menu_button: return "menu_button";
    case SemanticRole::pane: return "pane";
    case SemanticRole::tab_panel: return "tab_panel";
    case SemanticRole::tree: return "tree";
    case SemanticRole::tree_item: return "tree_item";
    case SemanticRole::table: return "table";
    case SemanticRole::row: return "row";
    case SemanticRole::cell: return "cell";
    case SemanticRole::column_header: return "column_header";
    case SemanticRole::row_header: return "row_header";
    case SemanticRole::tree_grid: return "tree_grid";
    case SemanticRole::dialog: return "dialog";
    case SemanticRole::status_bar: return "status_bar";
    case SemanticRole::live_region: return "live_region";
    case SemanticRole::meter: return "meter";
    case SemanticRole::toggle_switch: return "toggle_switch";
    case SemanticRole::search_box: return "search_box";
    case SemanticRole::figure: return "figure";
    case SemanticRole::count: return "generic";
    }
    return "generic";
}

const char* semantic_action_name(SemanticAction action) noexcept {
    switch (action) {
    case SemanticAction::focus: return "focus";
    case SemanticAction::press: return "press";
    case SemanticAction::select: return "select";
    case SemanticAction::increment: return "increment";
    case SemanticAction::decrement: return "decrement";
    case SemanticAction::expand: return "expand";
    case SemanticAction::collapse: return "collapse";
    case SemanticAction::show_menu: return "show_menu";
    case SemanticAction::set_value: return "set_value";
    case SemanticAction::close: return "close";
    case SemanticAction::count: return "unknown";
    }
    return "unknown";
}

const char* semantic_orientation_name(SemanticOrientation orientation) noexcept {
    switch (orientation) {
    case SemanticOrientation::horizontal: return "horizontal";
    case SemanticOrientation::vertical: return "vertical";
    }
    return "horizontal";
}

const char* semantic_text_result_name(SemanticTextResult result) noexcept {
    switch (result) {
    case SemanticTextResult::done: return "done";
    case SemanticTextResult::stale_identity: return "stale_identity";
    case SemanticTextResult::during_paint: return "during_paint";
    case SemanticTextResult::outside_focus_scope: return "outside_focus_scope";
    case SemanticTextResult::no_text: return "no_text";
    case SemanticTextResult::off_boundary: return "off_boundary";
    }
    return "unknown";
}

const char* semantic_text_unit_name(SemanticTextUnit unit) noexcept {
    switch (unit) {
    case SemanticTextUnit::character: return "character";
    case SemanticTextUnit::word: return "word";
    case SemanticTextUnit::line: return "line";
    case SemanticTextUnit::paragraph: return "paragraph";
    case SemanticTextUnit::document: return "document";
    }
    return "unknown";
}

namespace {

// Appends the boundaries inside `value` that `unit` moves by, unsorted and
// possibly repeated; the caller adds the two ends and orders them.
void append_inner_boundaries(const SemanticTextLayout& layout, std::string_view value, SemanticTextUnit unit,
                             std::vector<Utf8Offset>& boundaries) {
    switch (unit) {
    case SemanticTextUnit::character:
        for (const SemanticTextGrapheme& grapheme : layout.graphemes) {
            boundaries.push_back(grapheme.range.start);
            boundaries.push_back(grapheme.range.end);
        }
        return;
    case SemanticTextUnit::word:
        boundaries.insert(boundaries.end(), layout.word_starts.begin(), layout.word_starts.end());
        return;
    case SemanticTextUnit::line:
        for (const SemanticTextLine& line : layout.lines) boundaries.push_back(line.range.start);
        return;
    case SemanticTextUnit::paragraph:
        for (std::size_t index = 0U; index < value.size(); ++index) {
            if (value[index] == '\n') boundaries.push_back(Utf8Offset{index + 1U});
        }
        return;
    case SemanticTextUnit::document:
        // The whole text is one unit: only its two ends.
        return;
    }
    // A unit outside the enumeration moves by the whole text, as document does.
    return;
}

} // namespace

void fill_semantic_text_boundaries(const SemanticTextLayout& layout, std::string_view value,
                                   SemanticTextUnit unit, std::vector<Utf8Offset>& boundaries) {
    boundaries.clear();
    const Utf8Offset start{0U};
    const Utf8Offset end{value.size()};
    boundaries.push_back(start);
    append_inner_boundaries(layout, value, unit, boundaries);
    boundaries.push_back(end);
    // A layout read for other text could hold offsets past this value's
    // end; they are no boundaries of it.
    const std::vector<Utf8Offset>::iterator past =
        std::remove_if(boundaries.begin(), boundaries.end(), OffsetPast{end});
    boundaries.erase(past, boundaries.end());
    std::sort(boundaries.begin(), boundaries.end());
    const std::vector<Utf8Offset>::iterator repeated = std::unique(boundaries.begin(), boundaries.end());
    boundaries.erase(repeated, boundaries.end());
}

const char* semantic_state_name(SemanticState state) noexcept {
    switch (state) {
    case SemanticState::none: return "none";
    case SemanticState::enabled: return "enabled";
    case SemanticState::visible: return "visible";
    case SemanticState::focusable: return "focusable";
    case SemanticState::focused: return "focused";
    case SemanticState::selected: return "selected";
    case SemanticState::checked: return "checked";
    case SemanticState::mixed: return "mixed";
    case SemanticState::read_only: return "read_only";
    case SemanticState::expanded: return "expanded";
    case SemanticState::busy: return "busy";
    case SemanticState::protected_content: return "protected_content";
    case SemanticState::invalid: return "invalid";
    case SemanticState::checkable: return "checkable";
    case SemanticState::multiselectable: return "multiselectable";
    }
    return "none";
}

namespace {
// The admitted node fields beyond the basic ones every node has: RB-22,
// DK-40, the stage A vocabulary (12.13), the plain tooltip (owner,
// 2026-10-28) and the announcement on appearance (owner, 2026-11-08), in
// that order.
constexpr const char* semantic_field_table[] = {
    "position_in_set", "set_size", "has_popup", "controlled_panel", "orientation",
    "level", "modal", "row_index", "column_index", "row_count", "column_count", "access_key",
    "small_step", "large_step", "text_selection", "multiline", "tool_tip",
    "announces_on_appearance",
};
} // namespace

std::span<const char* const> semantic_field_names() noexcept {
    const std::span<const char* const> names(semantic_field_table);
    return names;
}

std::string SemanticSnapshot::to_json() const {
    std::ostringstream stream;
    stream << "{\"generation\":" << generation << ",\"node_count\":"
           << node_count << ",\"roots\":[";
    for (std::size_t index = 0; index < roots.size(); ++index) {
        if (index != 0U) stream << ',';
        append_node(stream, roots[index]);
    }
    stream << "]}";
    return stream.str();
}

} // namespace gui_forms
