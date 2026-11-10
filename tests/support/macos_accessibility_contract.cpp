#include "macos_accessibility_contract.hpp"

#include <algorithm>

namespace gui_forms::test_support {

std::size_t expected_utf16_units(std::string_view text, std::size_t bytes) {
    const std::size_t end = std::min(bytes, text.size());
    std::size_t units = 0U;
    for (std::size_t index = 0U; index < end; ++index) {
        const unsigned char byte = static_cast<unsigned char>(text[index]);
        // A continuation byte adds nothing; a four-byte sequence's lead
        // starts a surrogate pair.
        if ((byte & 0xC0U) == 0x80U) continue;
        units += byte >= 0xF0U ? 2U : 1U;
    }
    return units;
}

const char* expected_ax_role(SemanticRole role) {
    switch (role) {
    case SemanticRole::static_text: return "AXStaticText";
    case SemanticRole::button: return "AXButton";
    case SemanticRole::check_box: return "AXCheckBox";
    case SemanticRole::toggle_switch: return "AXCheckBox";
    case SemanticRole::radio_button: return "AXRadioButton";
    case SemanticRole::toolbar: return "AXToolbar";
    case SemanticRole::radio_group: return "AXRadioGroup";
    case SemanticRole::link: return "AXLink";
    case SemanticRole::text_box: return "AXTextField";
    case SemanticRole::search_box: return "AXTextField";
    case SemanticRole::numeric_field: return "AXTextField";
    case SemanticRole::list: return "AXList";
    case SemanticRole::list_item: return "AXRow";
    case SemanticRole::check_list_item: return "AXCheckBox";
    case SemanticRole::combo_box: return "AXComboBox";
    case SemanticRole::slider: return "AXSlider";
    case SemanticRole::scroll_bar: return "AXScrollBar";
    case SemanticRole::progress_bar: return "AXProgressIndicator";
    case SemanticRole::meter: return "AXLevelIndicator";
    case SemanticRole::image: return "AXImage";
    case SemanticRole::figure: return "AXImage";
    case SemanticRole::tab_group: return "AXTabGroup";
    case SemanticRole::tab: return "AXRadioButton";
    case SemanticRole::split_pane: return "AXSplitGroup";
    case SemanticRole::tool_tip: return "AXStaticText";
    case SemanticRole::date_picker: return "AXComboBox";
    case SemanticRole::date_cell: return "AXButton";
    case SemanticRole::property_row: return "AXStaticText";
    case SemanticRole::menu_bar: return "AXMenuBar";
    case SemanticRole::menu_bar_item: return "AXMenuBarItem";
    case SemanticRole::menu: return "AXMenu";
    case SemanticRole::menu_item: return "AXMenuItem";
    case SemanticRole::split_button: return "AXButton";
    case SemanticRole::menu_button: return "AXMenuButton";
    case SemanticRole::tree: return "AXOutline";
    case SemanticRole::tree_item: return "AXRow";
    case SemanticRole::table: return "AXTable";
    case SemanticRole::row: return "AXRow";
    case SemanticRole::cell: return "AXCell";
    case SemanticRole::column_header: return "AXCell";
    case SemanticRole::row_header: return "AXCell";
    case SemanticRole::tree_grid: return "AXOutline";
    case SemanticRole::generic:
    case SemanticRole::group:
    case SemanticRole::calendar:
    case SemanticRole::property_grid:
    case SemanticRole::property_group:
    case SemanticRole::separator:
    case SemanticRole::pane:
    case SemanticRole::tab_panel:
    case SemanticRole::dialog:
    case SemanticRole::status_bar:
    case SemanticRole::live_region:
    case SemanticRole::count:
        return "AXGroup";
    }
    return "AXGroup";
}

const char* expected_ax_subrole(SemanticRole role) {
    if (role == SemanticRole::toggle_switch) return "AXSwitch";
    if (role == SemanticRole::search_box) return "AXSearchField";
    if (role == SemanticRole::tab_panel) return "AXTabPanel";
    if (role == SemanticRole::tab) return "AXTabButton";
    if (role == SemanticRole::tree_item) return "AXOutlineRow";
    if (role == SemanticRole::dialog) return "AXApplicationDialog";
    if (role == SemanticRole::status_bar) return "AXApplicationStatus";
    if (role == SemanticRole::live_region) return "AXApplicationLog";
    return "";
}

std::string expected_ax_value(const SemanticNode& node) {
    const SemanticRole acting = semantic_role_fallback(node.role);
    if (acting == SemanticRole::check_box ||
        acting == SemanticRole::check_list_item ||
        acting == SemanticRole::radio_button) {
        if (has_semantic_state(node.states, SemanticState::mixed)) return "2";
        return has_semantic_state(node.states, SemanticState::checked) ? "1" : "0";
    }
    if (node.role == SemanticRole::tab) {
        return has_semantic_state(node.states, SemanticState::selected) ? "1" : "0";
    }
    if (node.numeric_value) return semantic_number_text(*node.numeric_value);
    return node.value;
}

std::string expected_ax_range(const std::optional<std::size_t>& index) {
    if (!index || *index == 0U) return {};
    const std::string range = std::to_string(*index - 1U) + "+1";
    return range;
}

AccessibilityProjection expected_ax_projection(const SemanticNode& node) {
    AccessibilityProjection expected;
    // A text box whose text runs over several lines is a text area.
    const bool text_area = node.role == SemanticRole::text_box && node.multiline;
    expected.role = text_area ? "AXTextArea" : expected_ax_role(node.role);
    expected.subrole = expected_ax_subrole(node.role);
    expected.label = node.name;
    expected.identifier = node.stable_id;
    expected.value = expected_ax_value(node);
    expected.enabled = has_semantic_state(node.states, SemanticState::enabled);
    expected.focused = has_semantic_state(node.states, SemanticState::focused);
    expected.selected = has_semantic_state(node.states, SemanticState::selected);
    expected.expanded = has_semantic_state(node.states, SemanticState::expanded);
    // AXPress performs press, else select, else collapse, else expand.
    expected.press = has_semantic_action(node, SemanticAction::press) ||
                     has_semantic_action(node, SemanticAction::select) ||
                     has_semantic_action(node, SemanticAction::expand) ||
                     has_semantic_action(node, SemanticAction::collapse);
    // A focusable element offers show-menu too: it is macOS's keyboard
    // context request (owner's ruling, 2026-11-08).
    expected.show_menu = has_semantic_action(node, SemanticAction::show_menu) ||
                         has_semantic_action(node, SemanticAction::expand) ||
                         has_semantic_action(node, SemanticAction::focus);
    expected.increment = has_semantic_action(node, SemanticAction::increment);
    expected.decrement = has_semantic_action(node, SemanticAction::decrement);
    expected.focus_settable = has_semantic_action(node, SemanticAction::focus);
    expected.value_settable = has_semantic_action(node, SemanticAction::set_value);
    expected.selected_settable = has_semantic_action(node, SemanticAction::select);
    expected.close_action = has_semantic_action(node, SemanticAction::close);
    expected.disclosure_level = node.level && *node.level != 0U ? static_cast<long long>(*node.level - 1U) : 0;
    expected.row_range = expected_ax_range(node.row_index);
    expected.column_range = expected_ax_range(node.column_index);
    expected.row_count = node.row_count ? static_cast<long long>(*node.row_count) : 0;
    expected.column_count = node.column_count ? static_cast<long long>(*node.column_count) : 0;
    expected.modal = node.modal;
    const bool text_ranges = semantic_role_fallback(node.role) == SemanticRole::text_box &&
                             node.text_selection.has_value() &&
                             !has_semantic_state(node.states, SemanticState::protected_content);
    if (text_ranges) {
        const SemanticTextSelection selection = *node.text_selection;
        const std::size_t low = std::min(selection.anchor.value(), selection.caret.value());
        const std::size_t high = std::max(selection.anchor.value(), selection.caret.value());
        const std::size_t start = expected_utf16_units(node.value, low);
        const std::size_t end = expected_utf16_units(node.value, high);
        expected.character_count = static_cast<long long>(expected_utf16_units(node.value, node.value.size()));
        expected.selected_text_range = std::to_string(start) + "+" + std::to_string(end - start);
    }
    return expected;
}

} // namespace gui_forms::test_support
