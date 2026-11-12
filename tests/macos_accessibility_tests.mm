#import <AppKit/AppKit.h>

#include "macos_host.hpp"

#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/text_box.hpp"
#include "gui_forms/controls/tree_view.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/host/types/host_capability/host_capability.hpp"
#include "gui_forms/host/types/host_types.hpp"
#include "gui_forms/plots/decimation/decimation_kernels.hpp"
#include "gui_forms/rules/rule/rule.hpp"
#include "gui_forms/semantics/types/semantic_types.hpp"
#include "gui_forms/text/markdown/markdown.hpp"
#include "gui_forms/text/types/text_types.hpp"
#include "gui_forms/window/window.hpp"
#include "support/accessibility_actions.hpp"
#include "support/macos_accessibility_contract.hpp"
#include "support/stock_control_fixture.hpp"

#include <cstdint>
#include <cstdio>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <string_view>
#include <vector>

// Stage A, macOS in-process regression test. The stock-control fixture runs
// on a real AppKit window; the test walks the window's NSAccessibility tree
// through the NSAccessibility protocol, compares every element with the
// semantic node it projects, performs each control's default action through
// its element and observes the change, and checks that an element held past
// its control's removal refuses actions, and that items whose stable ids two
// trees repeat each reach their own tree, and reads and sets a text box's
// text attributes (A-T1). It runs in process, so it needs no
// Accessibility permission (CI); the out-of-process AXUIElement client is
// macos_accessibility_client_tests.

namespace {

using gui_forms::SemanticAction;
using gui_forms::SemanticNode;
using gui_forms::SemanticRole;
using gui_forms::SemanticSnapshot;
using gui_forms::SemanticState;
using gui_forms::has_semantic_state;
using gui_forms::test_support::StockControlEntry;
using gui_forms::test_support::StockControlFixture;

constexpr const char* fixture_title = "GUI.Forms accessibility fixture";

using Projection = gui_forms::test_support::AccessibilityProjection;
using gui_forms::test_support::default_action_node;
using gui_forms::test_support::default_action_value;
using gui_forms::test_support::expected_ax_projection;
using gui_forms::test_support::find_semantic_node;
using gui_forms::test_support::has_semantic_action;
using gui_forms::test_support::same_semantic_roots;
using gui_forms::test_support::semantic_number_text;
using gui_forms::test_support::semantic_subtree_signature;

// ---------------------------------------------------------------------------
// What an element publishes, read through the NSAccessibility protocol.

std::string utf8(NSString* text) {
    if (text == nil) return {};
    const char* bytes = text.UTF8String;
    return bytes == nullptr ? std::string{} : std::string(bytes);
}

std::string value_text(id value) {
    if (value == nil) return {};
    if ([value isKindOfClass:[NSNumber class]]) {
        NSNumber* number = (NSNumber*)value;
        return semantic_number_text(number.doubleValue);
    }
    if ([value isKindOfClass:[NSString class]]) return utf8((NSString*)value);
    return utf8([value description]);
}

bool selector_allowed(id element, SEL selector) {
    if (![element respondsToSelector:@selector(isAccessibilitySelectorAllowed:)]) return NO;
    return [element isAccessibilitySelectorAllowed:selector];
}

bool has_custom_action(id element, NSString* name) {
    NSArray<NSAccessibilityCustomAction*>* actions = [element accessibilityCustomActions];
    for (NSAccessibilityCustomAction* action in actions) {
        if ([action.name isEqualToString:name]) return true;
    }
    return false;
}

// A range as "location+length", or "" when its location is NSNotFound.
std::string range_text(NSRange range) {
    if (range.location == NSNotFound) return {};
    const std::string text = std::to_string(static_cast<unsigned long long>(range.location)) + "+" +
                             std::to_string(static_cast<unsigned long long>(range.length));
    return text;
}

Projection published_projection(id element) {
    Projection published;
    published.role = utf8([element accessibilityRole]);
    published.subrole = utf8([element accessibilitySubrole]);
    published.label = utf8([element accessibilityLabel]);
    published.identifier = utf8([element accessibilityIdentifier]);
    published.value = value_text([element accessibilityValue]);
    published.enabled = [element isAccessibilityEnabled];
    published.focused = [element isAccessibilityFocused];
    published.selected = [element isAccessibilitySelected];
    published.expanded = [element isAccessibilityExpanded];
    published.press = selector_allowed(element, @selector(accessibilityPerformPress));
    published.show_menu = selector_allowed(element, @selector(accessibilityPerformShowMenu));
    published.increment = selector_allowed(element, @selector(accessibilityPerformIncrement));
    published.decrement = selector_allowed(element, @selector(accessibilityPerformDecrement));
    published.focus_settable = selector_allowed(element, @selector(setAccessibilityFocused:));
    published.value_settable = selector_allowed(element, @selector(setAccessibilityValue:));
    published.selected_settable = selector_allowed(element, @selector(setAccessibilitySelected:));
    published.close_action = has_custom_action(element, @"Close");
    published.disclosure_level = static_cast<long long>([element accessibilityDisclosureLevel]);
    published.row_range = range_text([element accessibilityRowIndexRange]);
    published.column_range = range_text([element accessibilityColumnIndexRange]);
    published.row_count = static_cast<long long>([element accessibilityRowCount]);
    published.column_count = static_cast<long long>([element accessibilityColumnCount]);
    published.modal = [element isAccessibilityModal];
    if (selector_allowed(element, @selector(accessibilitySelectedTextRange))) {
        published.character_count = static_cast<long long>([element accessibilityNumberOfCharacters]);
        published.selected_text_range = range_text([element accessibilitySelectedTextRange]);
    }
    return published;
}

// ---------------------------------------------------------------------------
// Comparison and the per-control report.

struct Mismatch final {
    std::string phase;
    std::string owner;
    std::string stable_id;
    std::string field;
    std::string expected;
    std::string published;
};

struct ControlTally final {
    std::string manifest_id;
    std::size_t nodes{};
    std::size_t matched{};
    std::string default_action;
    bool action_observed{};
};

struct Report final {
    std::vector<ControlTally> controls;
    std::vector<Mismatch> mismatches;
    std::vector<std::string> failures;
};

ControlTally& tally_for(Report& report, std::string_view manifest_id) {
    for (ControlTally& tally : report.controls) {
        if (tally.manifest_id == manifest_id) return tally;
    }
    report.controls.push_back(ControlTally{std::string(manifest_id)});
    return report.controls.back();
}

// The placed control's manifest id when `stable_id` names one, else empty.
std::string entry_manifest_id(const StockControlFixture& fixture, std::string_view stable_id) {
    const StockControlEntry* entry = gui_forms::test_support::find_stock_control(fixture, stable_id);
    return entry == nullptr ? std::string{} : (*entry).manifest_id;
}

class TreeComparison final {
public:
    TreeComparison(const StockControlFixture& fixture, Report& report, std::string phase)
        : fixture_(fixture), report_(report), phase_(std::move(phase)) {}

    void compare(const SemanticNode& node, id element, const std::string& inherited_owner) {
        std::string owner = inherited_owner;
        if (std::string placed = entry_manifest_id(fixture_, node.stable_id); !placed.empty()) {
            owner = std::move(placed);
        } else if (owner.empty()) {
            owner = owner_by_prefix(node.stable_id);
        }
        const bool attributed = !owner.empty();
        bool node_matches = true;
        if (element == nil) {
            add(owner, node.stable_id, "element", "present", "missing");
            node_matches = false;
        } else {
            const Projection expected = expected_ax_projection(node);
            const Projection published = published_projection(element);
            node_matches = compare_projection(owner, node.stable_id, expected, published);
        }
        NSArray* children = element == nil ? @[] : [element accessibilityChildren];
        const std::size_t published_count = children == nil ? 0U : children.count;
        if (published_count != node.children.size()) {
            add(owner, node.stable_id, "children", std::to_string(node.children.size()),
                std::to_string(published_count));
            node_matches = false;
        }
        if (attributed) {
            ControlTally& tally = tally_for(report_, owner);
            ++tally.nodes;
            if (node_matches) ++tally.matched;
        }
        for (std::size_t index = 0U; index < node.children.size(); ++index) {
            id child = index < published_count ? children[index] : nil;
            compare(node.children[index], child, owner);
        }
    }

private:
    // The manifest id of the placed control whose stable id prefixes this
    // one ("list-box" for "list-box.item.0"), for a node whose control
    // publishes no node of its own; the longest such prefix, or empty.
    std::string owner_by_prefix(const std::string& stable_id) const {
        std::string owner;
        std::size_t owner_prefix = 0U;
        for (const StockControlEntry& entry : fixture_.controls) {
            const std::string prefix = entry.stable_id + ".";
            if (stable_id.compare(0U, prefix.size(), prefix) == 0 && prefix.size() > owner_prefix) {
                owner = entry.manifest_id;
                owner_prefix = prefix.size();
            }
        }
        return owner;
    }

    void add(const std::string& owner, const std::string& stable_id, const char* field,
             const std::string& expected, const std::string& published) {
        report_.mismatches.push_back(Mismatch{phase_, owner, stable_id, field, expected, published});
    }

    bool compare_text(const std::string& owner, const std::string& stable_id, const char* field,
                      const std::string& expected, const std::string& published) {
        if (expected == published) return true;
        add(owner, stable_id, field, expected, published);
        return false;
    }

    bool compare_flag(const std::string& owner, const std::string& stable_id, const char* field,
                      bool expected, bool published) {
        if (expected == published) return true;
        add(owner, stable_id, field, expected ? "true" : "false", published ? "true" : "false");
        return false;
    }

    bool compare_projection(const std::string& owner, const std::string& id,
                            const Projection& expected, const Projection& published) {
        bool same = true;
        same = compare_text(owner, id, "role", expected.role, published.role) && same;
        same = compare_text(owner, id, "subrole", expected.subrole, published.subrole) && same;
        same = compare_text(owner, id, "label", expected.label, published.label) && same;
        same = compare_text(owner, id, "identifier", expected.identifier, published.identifier) && same;
        same = compare_text(owner, id, "value", expected.value, published.value) && same;
        same = compare_flag(owner, id, "enabled", expected.enabled, published.enabled) && same;
        same = compare_flag(owner, id, "focused", expected.focused, published.focused) && same;
        same = compare_flag(owner, id, "selected", expected.selected, published.selected) && same;
        same = compare_flag(owner, id, "expanded", expected.expanded, published.expanded) && same;
        same = compare_flag(owner, id, "AXPress", expected.press, published.press) && same;
        same = compare_flag(owner, id, "AXShowMenu", expected.show_menu, published.show_menu) && same;
        same = compare_flag(owner, id, "AXIncrement", expected.increment, published.increment) && same;
        same = compare_flag(owner, id, "AXDecrement", expected.decrement, published.decrement) && same;
        same = compare_flag(owner, id, "AXFocused settable", expected.focus_settable, published.focus_settable) && same;
        same = compare_flag(owner, id, "AXValue settable", expected.value_settable, published.value_settable) && same;
        same = compare_flag(owner, id, "AXSelected settable", expected.selected_settable, published.selected_settable) && same;
        same = compare_flag(owner, id, "Close action", expected.close_action, published.close_action) && same;
        same = compare_text(owner, id, "AXDisclosureLevel", std::to_string(expected.disclosure_level),
                            std::to_string(published.disclosure_level)) && same;
        same = compare_text(owner, id, "AXRowIndexRange", expected.row_range, published.row_range) && same;
        same = compare_text(owner, id, "AXColumnIndexRange", expected.column_range, published.column_range) && same;
        same = compare_text(owner, id, "AXRowCount", std::to_string(expected.row_count),
                            std::to_string(published.row_count)) && same;
        same = compare_text(owner, id, "AXColumnCount", std::to_string(expected.column_count),
                            std::to_string(published.column_count)) && same;
        same = compare_flag(owner, id, "AXModal", expected.modal, published.modal) && same;
        same = compare_text(owner, id, "AXNumberOfCharacters", std::to_string(expected.character_count),
                            std::to_string(published.character_count)) && same;
        same = compare_text(owner, id, "AXSelectedTextRange", expected.selected_text_range,
                            published.selected_text_range) && same;
        return same;
    }

    const StockControlFixture& fixture_;
    Report& report_;
    std::string phase_;
};

void compare_window(const StockControlFixture& fixture, gui_forms::Window& window, NSView* view,
                    Report& report, const char* phase) {
    const SemanticSnapshot snapshot = window.semantic_snapshot();
    NSArray* roots = [view accessibilityChildren];
    const std::size_t published_count = roots == nil ? 0U : roots.count;
    if (published_count != snapshot.roots.size()) {
        report.mismatches.push_back(Mismatch{phase, "", "(window)", "roots",
                                             std::to_string(snapshot.roots.size()),
                                             std::to_string(published_count)});
    }
    TreeComparison comparison(fixture, report, phase);
    for (std::size_t index = 0U; index < snapshot.roots.size(); ++index) {
        id element = index < published_count ? roots[index] : nil;
        comparison.compare(snapshot.roots[index], element, "");
    }
}

// ---------------------------------------------------------------------------
// Default actions.

id find_element(NSArray* elements, NSString* identifier) {
    for (id element in elements) {
        if ([[element accessibilityIdentifier] isEqualToString:identifier]) return element;
        id found = find_element([element accessibilityChildren], identifier);
        if (found != nil) return found;
    }
    return nil;
}

// The first node, in tree order, whose stable id begins with the control's
// ("property-grid.row.0"), for a control that publishes no node of its own
// but publishes its parts. Returns a borrow into `nodes`.
const SemanticNode* first_prefixed_node(const std::vector<SemanticNode>& nodes, const std::string& control_id) {
    const std::string prefix = control_id + ".";
    for (const SemanticNode& node : nodes) {
        if (node.stable_id.compare(0U, prefix.size(), prefix) == 0) return &node;
        const SemanticNode* found = first_prefixed_node(node.children, control_id);
        if (found != nullptr) return found;
    }
    return nullptr;
}
const SemanticNode* first_prefixed_node(std::vector<SemanticNode>&& nodes, const std::string& control_id) = delete;

void append_prefixed_signatures(const std::vector<SemanticNode>& nodes, const std::string& prefix,
                                std::string& signature) {
    for (const SemanticNode& node : nodes) {
        if (node.stable_id.compare(0U, prefix.size(), prefix) == 0) {
            signature += semantic_subtree_signature(nodes, node.stable_id);
        } else {
            append_prefixed_signatures(node.children, prefix, signature);
        }
    }
}

// The control's published state: its own subtree's signature, or, for a
// control that publishes no node of its own, those of the top-most nodes
// under its stable id.
std::string control_signature(const std::vector<SemanticNode>& roots, const std::string& control_id) {
    std::string signature = semantic_subtree_signature(roots, control_id);
    if (signature.empty()) append_prefixed_signatures(roots, control_id + ".", signature);
    return signature;
}

bool perform_through_element(id element, SemanticAction action, const SemanticNode& node) {
    switch (action) {
    case SemanticAction::press:
    case SemanticAction::expand:
    case SemanticAction::collapse:
        return [element accessibilityPerformPress];
    case SemanticAction::select:
        [element setAccessibilitySelected:YES];
        return true;
    case SemanticAction::increment:
        return [element accessibilityPerformIncrement];
    case SemanticAction::set_value: {
        NSString* value = [NSString stringWithUTF8String:default_action_value(node).c_str()];
        [element setAccessibilityValue:value];
        return true;
    }
    case SemanticAction::show_menu:
        return [element accessibilityPerformShowMenu];
    case SemanticAction::focus:
    case SemanticAction::decrement:
    case SemanticAction::close:
    case SemanticAction::count:
        return false;
    }
    return false;
}

// Puts the window back after a default action that opened something: a node
// left expanded is collapsed through the model, and an open overlay, which
// replaces the window's semantic roots, is closed with Escape (at most three
// presses).
void restore_after_action(gui_forms::Window& window, const std::string& target_id,
                          const SemanticSnapshot& before) {
    const SemanticSnapshot after = window.semantic_snapshot();
    const SemanticNode* target = find_semantic_node(after.roots, target_id);
    if (target != nullptr && has_semantic_state((*target).states, SemanticState::expanded) &&
        has_semantic_action(*target, SemanticAction::collapse)) {
        static_cast<void>(window.perform_semantic_action(target_id, SemanticAction::collapse, ""));
    }
    for (int attempt = 0; attempt < 3; ++attempt) {
        const SemanticSnapshot current = window.semantic_snapshot();
        if (same_semantic_roots(current, before)) return;
        gui_forms::KeyEvent escape{};
        escape.action = gui_forms::KeyAction::down;
        escape.physical_key = gui_forms::PhysicalKey::escape;
        static_cast<void>(window.dispatch_key(escape));
    }
}

void perform_default_actions(StockControlFixture& fixture, gui_forms::Window& window, NSView* view,
                             Report& report) {
    for (const StockControlEntry& entry : fixture.controls) {
        const SemanticSnapshot before = window.semantic_snapshot();
        const SemanticNode* control_node = find_semantic_node(before.roots, entry.stable_id);
        if (control_node == nullptr) control_node = first_prefixed_node(before.roots, entry.stable_id);
        if (control_node == nullptr) continue;
        SemanticAction action = SemanticAction::focus;
        const SemanticNode* target = default_action_node(*control_node, action);
        ControlTally& tally = tally_for(report, entry.manifest_id);
        if (target == nullptr) {
            tally.default_action = "none";
            continue;
        }
        const SemanticNode target_before = *target;
        tally.default_action = gui_forms::semantic_action_name(action);
        NSString* identifier = [NSString stringWithUTF8String:target_before.stable_id.c_str()];
        id element = find_element([view accessibilityChildren], identifier);
        if (element == nil) {
            report.failures.push_back(entry.manifest_id + ": no element for " + target_before.stable_id);
            continue;
        }
        const std::uint64_t effects_before = (*fixture.effects).total();
        const std::string signature_before = control_signature(before.roots, entry.stable_id);
        const bool performed = perform_through_element(element, action, target_before);
        const SemanticSnapshot after = window.semantic_snapshot();
        const bool effect = (*fixture.effects).total() > effects_before;
        const bool changed = control_signature(after.roots, entry.stable_id) != signature_before;
        tally.action_observed = performed && (effect || changed);
        restore_after_action(window, target_before.stable_id, before);
        if (!tally.action_observed) {
            report.failures.push_back(entry.manifest_id + ": default action " + tally.default_action +
                                      " on " + target_before.stable_id + " showed no effect");
        }
    }
}

// ---------------------------------------------------------------------------
// A stale element: one kept past its control's removal from the window.

void check_stale_element(StockControlFixture& fixture, NSView* view, Report& report) {
    const StockControlEntry* entry = gui_forms::test_support::find_stock_control(fixture, "button");
    if (entry == nullptr) {
        report.failures.push_back("stale element: the fixture has no button");
        return;
    }
    id stale = find_element([view accessibilityChildren], @"button");
    if (stale == nil) {
        report.failures.push_back("stale element: no element for the button");
        return;
    }
    const std::uint64_t presses_before = (*fixture.effects).presses("button");
    const gui_forms::Control::Ptr removed = (*fixture.root).remove_child((*(*entry).control).runtime_id());
    if (!removed) {
        report.failures.push_back("stale element: the button could not be removed");
        return;
    }
    id replaced = find_element([view accessibilityChildren], @"button");
    if (replaced != nil) report.failures.push_back("stale element: the removed button is still published");
    const BOOL pressed = [stale accessibilityPerformPress];
    if (pressed || (*fixture.effects).presses("button") != presses_before) {
        report.failures.push_back("stale element: a press on the removed button's element was performed");
    }
    static_cast<void>([stale accessibilityRole]);
}

// ---------------------------------------------------------------------------
// Repeated item identities: two trees that show one model publish the same
// item stable ids. Each item's element must reach its own tree (the shared
// routing API names the publishing control by runtime identity), never the
// first tree that claims the id.

// The child of `element` whose identifier is `identifier`, or nil.
id direct_child(id element, NSString* identifier) {
    for (id child in [element accessibilityChildren]) {
        if ([[child accessibilityIdentifier] isEqualToString:identifier]) return child;
    }
    return nil;
}

void check_repeated_item_identities(StockControlFixture& fixture, gui_forms::Window& window, NSView* view,
                                    Report& report) {
    std::shared_ptr<gui_forms::TreeView> left =
        gui_forms::make_control<gui_forms::TreeView>(gui_forms::StableId("twins.left"));
    std::shared_ptr<gui_forms::TreeView> right =
        gui_forms::make_control<gui_forms::TreeView>(gui_forms::StableId("twins.right"));
    std::vector<gui_forms::TreeViewItem> items;
    items.push_back(gui_forms::TreeViewItem{"src", "src", 0U, false, false, true, {}});
    items.push_back(gui_forms::TreeViewItem{"docs", "docs", 0U, false, false, true, {}});
    (*left).set_items(items);
    (*right).set_items(items);
    (*left).set_requested_bounds({10.0, 10.0, 180.0, 120.0});
    (*right).set_requested_bounds({210.0, 10.0, 180.0, 120.0});
    (*fixture.root).add_child(left);
    (*fixture.root).add_child(right);
    window.perform_layout();
    id left_tree = find_element([view accessibilityChildren], @"twins.left");
    id right_tree = find_element([view accessibilityChildren], @"twins.right");
    id left_docs = left_tree == nil ? nil : direct_child(left_tree, @"docs");
    id right_docs = right_tree == nil ? nil : direct_child(right_tree, @"docs");
    if (left_docs == nil || right_docs == nil) {
        report.failures.push_back("repeated identities: both trees must publish an item docs");
    } else if (left_docs == right_docs) {
        report.failures.push_back("repeated identities: the two trees' docs items share one element");
    } else if ([right_docs accessibilityParent] != right_tree) {
        report.failures.push_back("repeated identities: the right tree's docs element has another parent");
    } else {
        [right_docs setAccessibilitySelected:YES];
        if ((*right).selected_id.read() != "docs" || (*left).selected_id.read() == "docs") {
            report.failures.push_back("repeated identities: selecting the right tree's docs element did not "
                                      "select the right tree's item alone");
        }
        [left_docs setAccessibilitySelected:YES];
        if ((*left).selected_id.read() != "docs") {
            report.failures.push_back("repeated identities: selecting the left tree's docs element did not "
                                      "select the left tree's item");
        }
        std::cout << "repeated identities: each tree's docs element selected its own tree's item\n";
    }
    static_cast<void>((*fixture.root).remove_child((*left).runtime_id()));
    static_cast<void>((*fixture.root).remove_child((*right).runtime_id()));
    window.perform_layout();
}

// ---------------------------------------------------------------------------
// Editable text (A-T1): a text box's text attributes and parameterized
// attributes, read and set through the NSAccessibility protocol, against
// the model. The single-line box's text holds a combining sequence: "Cafe",
// U+0301 and " au lait" are 13 UTF-16 code units and 14 bytes, and bytes 3
// to 6 (units 3 to 5) are one composed character.

constexpr const char* single_line_text = "Cafe\xCC\x81 au lait";
constexpr const char* two_line_text = "First line\nSecond line";

void expect_text(Report& report, bool condition, const std::string& what) {
    if (!condition) report.failures.push_back("text ranges: " + what);
}

bool same_range(NSRange range, NSUInteger location, NSUInteger length) {
    const bool same = range.location == location && range.length == length;
    return same;
}

void check_single_line_text(gui_forms::TextBox& box, id element, Report& report) {
    expect_text(report, [element accessibilityNumberOfCharacters] == 13,
                "AXNumberOfCharacters counts UTF-16 code units");
    box.select(gui_forms::Utf8Offset(1U), gui_forms::Utf8Offset(6U));
    expect_text(report, same_range([element accessibilitySelectedTextRange], 1U, 4U),
                "AXSelectedTextRange is the model's selection in UTF-16 code units, " +
                    range_text([element accessibilitySelectedTextRange]));
    NSString* const selected = [element accessibilitySelectedText];
    expect_text(report, selected != nil && [selected isEqualToString:[NSString stringWithUTF8String:"afe\xCC\x81"]],
                "AXSelectedText is the selected text");
    [element setAccessibilitySelectedTextRange:NSMakeRange(6U, 3U)];
    const gui_forms::TextSelection set = box.selection();
    expect_text(report, set.anchor.value() == 7U && set.caret.value() == 10U,
                "setting AXSelectedTextRange selects the model's bytes, anchored at the range's start");
    NSString* const word = [element accessibilityStringForRange:NSMakeRange(6U, 2U)];
    expect_text(report, word != nil && [word isEqualToString:@"au"], "AXStringForRange");
    expect_text(report, [element accessibilityStringForRange:NSMakeRange(14U, 1U)] == nil,
                "AXStringForRange refuses a range past the text's end");
    expect_text(report, same_range([element accessibilityRangeForIndex:4], 3U, 2U),
                "AXRangeForIndex gives the whole composed character");
    expect_text(report, same_range([element accessibilityStyleRangeForIndex:0], 0U, 13U),
                "AXStyleRangeForIndex gives the one style run");
    expect_text(report, [element accessibilityLineForIndex:5] == 0, "AXLineForIndex on a single line");
    expect_text(report, same_range([element accessibilityRangeForLine:0], 0U, 13U), "AXRangeForLine 0");
    expect_text(report, [element accessibilityInsertionPointLineNumber] == 0, "AXInsertionPointLineNumber");
    expect_text(report, same_range([element accessibilityVisibleCharacterRange], 0U, 13U),
                "AXVisibleCharacterRange covers the text it shows");
    const NSRect letter = [element accessibilityFrameForRange:NSMakeRange(1U, 1U)];
    expect_text(report, letter.size.width > 0.0 && letter.size.height > 0.0 &&
                            NSContainsRect([element accessibilityFrame], letter),
                "AXBoundsForRange gives a character's box within the element");
    const NSRect caret = [element accessibilityFrameForRange:NSMakeRange(1U, 0U)];
    expect_text(report, caret.size.width > 0.0 && caret.size.width <= 1.5 && caret.size.height > 0.0,
                "AXBoundsForRange of an empty range is the caret's box");
    const NSPoint middle = NSMakePoint(NSMidX(letter), NSMidY(letter));
    expect_text(report, same_range([element accessibilityRangeForPosition:middle], 1U, 1U),
                "AXRangeForPosition gives the character under the point");
}

void check_two_line_text(gui_forms::TextBox& box, id element, Report& report) {
    expect_text(report, [element accessibilityNumberOfCharacters] == 22, "a multiline box's AXNumberOfCharacters");
    expect_text(report, same_range([element accessibilityRangeForLine:0], 0U, 11U),
                "a line runs through its break");
    expect_text(report, same_range([element accessibilityRangeForLine:1], 11U, 11U), "AXRangeForLine 1");
    expect_text(report, [element accessibilityLineForIndex:10] == 0 && [element accessibilityLineForIndex:12] == 1,
                "AXLineForIndex: a break belongs to the line it ends");
    expect_text(report, same_range([element accessibilityRangeForIndex:10], 10U, 1U),
                "a line break is one composed character");
    box.select(gui_forms::Utf8Offset(15U), gui_forms::Utf8Offset(15U));
    expect_text(report, [element accessibilityInsertionPointLineNumber] == 1,
                "AXInsertionPointLineNumber follows the caret");
    const NSRect first = [element accessibilityFrameForRange:NSMakeRange(0U, 1U)];
    const NSRect second = [element accessibilityFrameForRange:NSMakeRange(11U, 1U)];
    // Screen coordinates rise upwards, so the second line lies lower.
    expect_text(report, first.size.height > 0.0 && second.size.height > 0.0 && NSMaxY(second) <= NSMinY(first) + 0.5,
                "the second line's box lies below the first's");
}

void check_text_ranges(StockControlFixture& fixture, gui_forms::Window& window, NSView* view, Report& report) {
    std::shared_ptr<gui_forms::TextBox> single =
        gui_forms::make_control<gui_forms::TextBox>(gui_forms::StableId("text.single"));
    std::shared_ptr<gui_forms::TextBox> multi =
        gui_forms::make_control<gui_forms::TextBox>(gui_forms::StableId("text.multi"));
    std::shared_ptr<gui_forms::TextBox> password =
        gui_forms::make_control<gui_forms::TextBox>(gui_forms::StableId("text.password"));
    (*single).set_requested_bounds({10.0, 10.0, 260.0, 26.0});
    (*single).text = single_line_text;
    (*multi).set_requested_bounds({10.0, 50.0, 260.0, 80.0});
    (*multi).multiline = true;
    (*multi).text = two_line_text;
    (*password).set_requested_bounds({10.0, 140.0, 260.0, 26.0});
    (*password).use_system_password_character = true;
    (*password).text = "secret";
    (*fixture.root).add_child(single);
    (*fixture.root).add_child(multi);
    (*fixture.root).add_child(password);
    window.perform_layout();
    id single_element = find_element([view accessibilityChildren], @"text.single");
    id multi_element = find_element([view accessibilityChildren], @"text.multi");
    id password_element = find_element([view accessibilityChildren], @"text.password");
    if (single_element == nil || multi_element == nil || password_element == nil) {
        report.failures.push_back("text ranges: the three text boxes must be published");
    } else {
        expect_text(report, selector_allowed(single_element, @selector(accessibilityStringForRange:)) &&
                                selector_allowed(single_element, @selector(setAccessibilitySelectedTextRange:)),
                    "a text box publishes the text attributes and their setter");
        expect_text(report, !selector_allowed(password_element, @selector(accessibilitySelectedTextRange)) &&
                                !selector_allowed(password_element, @selector(accessibilityStringForRange:)),
                    "a password box publishes no text attributes");
        check_single_line_text(*single, single_element, report);
        check_two_line_text(*multi, multi_element, report);
        static_cast<void>((*fixture.root).remove_child((*single).runtime_id()));
        window.perform_layout();
        static_cast<void>([view accessibilityChildren]);
        [single_element setAccessibilitySelectedTextRange:NSMakeRange(0U, 1U)];
        expect_text(report, [single_element accessibilityNumberOfCharacters] == 0 &&
                                [single_element accessibilityStringForRange:NSMakeRange(0U, 1U)] == nil,
                    "a removed text box's element reads no text");
        std::cout << "text ranges: single-line, multiline and password boxes checked\n";
    }
    static_cast<void>((*fixture.root).remove_child((*multi).runtime_id()));
    static_cast<void>((*fixture.root).remove_child((*password).runtime_id()));
    window.perform_layout();
}

// ---------------------------------------------------------------------------

void print_report(const StockControlFixture& fixture, const Report& report) {
    for (const StockControlEntry& entry : fixture.controls) {
        bool tallied = false;
        for (const ControlTally& tally : report.controls) {
            if (tally.manifest_id == entry.manifest_id) tallied = true;
        }
        if (!tallied) std::cout << "control " << entry.manifest_id << " nodes=0 (publishes no semantic node)\n";
    }
    for (const gui_forms::test_support::StockControlOmission& omission : fixture.omissions) {
        std::cout << "omitted " << omission.manifest_id << ": " << omission.reason << '\n';
    }
    std::size_t nodes = 0U;
    std::size_t matched = 0U;
    for (const ControlTally& tally : report.controls) {
        nodes += tally.nodes;
        matched += tally.matched;
        std::cout << "control " << tally.manifest_id << " nodes=" << tally.nodes
                  << " matched=" << tally.matched << " default_action=" << tally.default_action
                  << " observed=" << (tally.action_observed ? "yes" : "no") << '\n';
    }
    for (const Mismatch& mismatch : report.mismatches) {
        std::cout << "mismatch phase=" << mismatch.phase << " control=" << mismatch.owner
                  << " node=" << mismatch.stable_id << " field=" << mismatch.field
                  << " expected=\"" << mismatch.expected << "\" published=\""
                  << mismatch.published << "\"\n";
    }
    for (const std::string& failure : report.failures) std::cout << "failure " << failure << '\n';
    std::cout << "controls=" << report.controls.size() << " nodes=" << nodes << " matched=" << matched
              << " mismatches=" << report.mismatches.size() << " failures=" << report.failures.size()
              << '\n';
}

NSView* fixture_view() {
    NSString* title = [NSString stringWithUTF8String:fixture_title];
    for (NSWindow* window in NSApp.windows) {
        if ([window.title isEqualToString:title]) return window.contentView;
    }
    return nil;
}

// The run's state, owned by main for the whole run: the walk reads the
// fixture and the model, and closes the window when it is done.
struct RunState final {
    StockControlFixture* fixture{};
    gui_forms::Window* window{};
    std::function<void()> request_close;
    // Where to write the window's semantic snapshot (JSON) before the
    // actions, or empty; set by --dump-semantics <path>.
    std::string dump_path;
    Report report;
    bool ran{};
};

// Runs on the main queue once the window is up: the whole check, then close.
void walk_and_close(void* context) {
    RunState& state = *static_cast<RunState*>(context);
    gui_forms::Window& window = *state.window;
    window.perform_layout();
    NSView* view = fixture_view();
    if (view == nil) {
        state.report.failures.push_back("the fixture window has no content view");
    } else {
        if (!state.dump_path.empty()) {
            const std::string json = window.semantic_snapshot().to_json();
            std::FILE* file = std::fopen(state.dump_path.c_str(), "w");
            if (file != nullptr) {
                std::fwrite(json.data(), 1U, json.size(), file);
                std::fclose(file);
            }
        }
        compare_window(*state.fixture, window, view, state.report, "initial");
        perform_default_actions(*state.fixture, window, view, state.report);
        window.perform_layout();
        compare_window(*state.fixture, window, view, state.report, "after-actions");
        check_stale_element(*state.fixture, view, state.report);
        check_repeated_item_identities(*state.fixture, window, view, state.report);
        check_text_ranges(*state.fixture, window, view, state.report);
    }
    state.ran = true;
    state.request_close();
}

using VoidCallback = std::function<void()>;
using ShowDialogCallback = std::function<gui_forms::HostDialogResult(const gui_forms::HostDialogRequest&)>;
using ReadClipboardCallback = std::function<gui_forms::HostClipboardTextResult()>;
using WriteClipboardCallback = std::function<gui_forms::HostServiceStatus(std::string_view)>;

class ScheduleWalk final {
public:
    explicit ScheduleWalk(RunState& state) noexcept : state_(state) {}

    void operator()(VoidCallback, VoidCallback request_close, ShowDialogCallback,
                    ReadClipboardCallback, WriteClipboardCallback) const {
        state_.request_close = std::move(request_close);
        const dispatch_time_t when = dispatch_time(DISPATCH_TIME_NOW, 300 * NSEC_PER_MSEC);
        dispatch_after_f(when, dispatch_get_main_queue(), &state_, &walk_and_close);
    }

private:
    RunState& state_;
};

} // namespace

int main(int argc, char** argv) {
    try {
        if (!gui_forms::host::macos_capabilities().supports(gui_forms::HostCapability::accessibility)) {
            std::cerr << "the macOS host does not advertise accessibility\n";
            return 1;
        }
        StockControlFixture fixture = gui_forms::test_support::make_stock_control_fixture();
        std::unique_ptr<gui_forms::Window> window = std::make_unique<gui_forms::Window>(
            fixture.root, gui_forms::test_support::stock_control_fixture_size);
        RunState state;
        state.fixture = &fixture;
        state.window = window.get();
        for (int index = 1; index + 1 < argc; ++index) {
            if (std::string_view(argv[index]) == "--dump-semantics") state.dump_path = argv[index + 1];
        }
        gui_forms::host::MacHostOptions options;
        options.title = fixture_title;
        options.initial_size = gui_forms::test_support::stock_control_fixture_size;
        options.print_metrics_on_close = false;
        options.host_ready = ScheduleWalk(state);
        const int result = gui_forms::host::run_macos(std::move(window), std::move(options));
        if (!state.ran) {
            std::cerr << "the fixture window never became ready (host result " << result << ")\n";
            return 2;
        }
        print_report(fixture, state.report);
        const bool clean = state.report.mismatches.empty() && state.report.failures.empty();
        return clean && result == 0 ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "macos accessibility test failed: " << error.what() << '\n';
        return 1;
    }
}
