#pragma once

#include "gui_forms/gui_forms.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace gui_forms::test_support {

// Choosing and observing a control's default action for host accessibility
// tests (stage A). Portable: a host test performs the action through its
// own accessibility API.

[[nodiscard]] bool has_semantic_action(const SemanticNode& node, SemanticAction action);
// A number as accessibility tests compare it, %g.
[[nodiscard]] std::string semantic_number_text(double value);

// The node with this stable id in `roots` and their descendants, or
// nullptr. Returns a borrow into `roots`; a temporary list is refused.
[[nodiscard]] const SemanticNode* find_semantic_node(
    const std::vector<SemanticNode>& roots, std::string_view stable_id);
const SemanticNode* find_semantic_node(
    std::vector<SemanticNode>&& roots, std::string_view stable_id) = delete;

// The node whose default action stands for a control, and that action: the
// control's own node when it has one, else its first descendant that has
// one. The order is press, select, increment, set value, show menu, expand,
// collapse; focus alone is not a default action. A disabled node, a select
// on a node already selected and an increment at the maximum are skipped,
// since they change nothing. Returns a borrow into `control`, or nullptr.
[[nodiscard]] const SemanticNode* default_action_node(
    const SemanticNode& control, SemanticAction& action);
const SemanticNode* default_action_node(SemanticNode&& control, SemanticAction& action) = delete;

// A value the node accepts and that differs from its current one: the
// maximum (or one more) for a number, an ISO date for a calendar or date
// picker (whose published value is the long form, but which set an ISO
// date), another day for an ISO date, the text with a suffix for text, and
// digits for anything else (a masked field).
[[nodiscard]] std::string default_action_value(const SemanticNode& node);

// The published state of the subtree with this stable id in one string:
// each node's identity, states, value and children in order; empty when the
// node is absent. A default action shows its effect as a change here (or in
// an effect a test counts).
[[nodiscard]] std::string semantic_subtree_signature(
    const std::vector<SemanticNode>& roots, std::string_view stable_id);

// Whether two snapshots have the same roots in the same order. An open
// overlay replaces a window's roots.
[[nodiscard]] bool same_semantic_roots(const SemanticSnapshot& left, const SemanticSnapshot& right);

} // namespace gui_forms::test_support
