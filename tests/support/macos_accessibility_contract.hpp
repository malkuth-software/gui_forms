#pragma once

#include "accessibility_actions.hpp"

#include "gui_forms/dirty.hpp"
#include "gui_forms/rules/rule/rule.hpp"
#include "gui_forms/semantics/types/semantic_types.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace gui_forms::test_support {

// What a semantic node must publish through macOS accessibility (stage A):
// the contract both macOS accessibility tests check, the in-process one
// through the NSAccessibility protocol and the out-of-process one through
// AXUIElement. It is written apart from the host, so a host change that
// alters the projection fails the tests. Roles, subroles and action names
// are the AX attribute and action strings.
struct AccessibilityProjection final {
    std::string role;
    std::string subrole;
    // AXDescription, which NSAccessibility's label sets.
    std::string label;
    std::string identifier;
    // The value in one text form (see expected_ax_value).
    std::string value;
    bool enabled{};
    bool focused{};
    bool selected{};
    bool expanded{};
    // AXPress, AXShowMenu, AXIncrement and AXDecrement.
    bool press{};
    bool show_menu{};
    bool increment{};
    bool decrement{};
    // Whether AXFocused, AXValue and AXSelected are settable.
    bool focus_settable{};
    bool value_settable{};
    bool selected_settable{};
    // A custom action named "Close".
    bool close_action{};
    // The stage A fields (12.13): AXDisclosureLevel (a tree item's depth from
    // zero; 0 without one), AXRowIndexRange and AXColumnIndexRange as
    // "location+length" counting from zero ("" without one), AXRowCount and
    // AXColumnCount (0 without one), and AXModal.
    long long disclosure_level{};
    std::string row_range;
    std::string column_range;
    long long row_count{};
    long long column_count{};
    bool modal{};
    // A text box's text ranges (A-T1): AXNumberOfCharacters, the text's
    // length in UTF-16 code units, and AXSelectedTextRange as
    // "location+length" in UTF-16 code units, the selection's low end
    // first. 0 and "" for a node that publishes none: anything but a text
    // box with a text selection, which the core publishes only on a
    // control's own node and never for a password box (A-T2).
    long long character_count{};
    std::string selected_text_range;
};

// The UTF-16 code units of text[0, bytes) (the text is valid UTF-8).
[[nodiscard]] std::size_t expected_utf16_units(std::string_view text, std::size_t bytes);

// A one-row or one-column range from an index counting from one, as
// AXRowIndexRange publishes it: "location+length", or "" without one.
[[nodiscard]] std::string expected_ax_range(const std::optional<std::size_t>& index);

[[nodiscard]] const char* expected_ax_role(SemanticRole role);
// The subrole, or "" for none.
[[nodiscard]] const char* expected_ax_subrole(SemanticRole role);
// A check box, a check list item and a radio button publish 0, 1 or 2
// (mixed); a tab publishes 1 when selected and 0 otherwise, as AppKit's tab
// buttons do; a node with a numeric value publishes the number (in
// semantic_number_text's form); any other node its value text.
[[nodiscard]] std::string expected_ax_value(const SemanticNode& node);
[[nodiscard]] AccessibilityProjection expected_ax_projection(const SemanticNode& node);

} // namespace gui_forms::test_support
