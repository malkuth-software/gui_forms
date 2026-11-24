#include "automation_internal.hpp"

#include "../marks/automation_marks_internal.hpp"

#include "gui_forms/window.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <system_error>
#include <unordered_map>

namespace gui_forms::automation::detail {
namespace {

struct FlattenFrame final {
    const SemanticNode* node{};
    std::size_t depth{};
    std::size_t parent{no_parent};
    std::uint64_t control_runtime_id{};
};

bool contains_point(Rect bounds, Point point) noexcept {
    const bool inside = point.x >= bounds.x && point.y >= bounds.y && point.x < bounds.x + bounds.width &&
                        point.y < bounds.y + bounds.height;
    return inside;
}

FlatNode flat_node_of(const SemanticNode& node, std::size_t depth, std::size_t parent,
                      std::uint64_t control_runtime_id) {
    FlatNode flat{};
    flat.runtime_id = node.runtime_id;
    flat.is_virtual = is_virtual_semantic_runtime_id(node.runtime_id);
    flat.control_runtime_id = flat.is_virtual ? control_runtime_id : node.runtime_id;
    flat.stable_id = node.stable_id;
    flat.role = node.role;
    flat.name = node.name;
    flat.states = node.states;
    // Study 9.1: a password field's value is never read.
    if (!has_semantic_state(node.states, SemanticState::protected_content)) flat.value = node.value;
    if (const double* const numeric = node.numeric_value.value()) {
        flat.has_numeric = true;
        flat.numeric_value = *numeric;
    }
    const double* const minimum = node.minimum_value.value();
    const double* const maximum = node.maximum_value.value();
    if (minimum != nullptr && maximum != nullptr) {
        flat.has_range = true;
        flat.minimum_value = *minimum;
        flat.maximum_value = *maximum;
    }
    flat.modal = node.modal;
    flat.bounds = node.bounds;
    flat.actions = node.actions;
    flat.has_popup = node.has_popup;
    if (const std::size_t* const position = node.position_in_set.value()) flat.position_in_set = *position;
    if (const std::size_t* const size = node.set_size.value()) flat.set_size = *size;
    flat.opens_dialog = (!node.stable_id.empty() && opens_dialog_mark(node.stable_id, flat.dialog_tool)) ||
                        (!node.has_popup && names_an_opener(node.name));
    flat.depth = depth;
    flat.parent = parent;
    return flat;
}

// Marks, below the control `control_index` that the interaction names, the
// deepest node holding the pointer: a virtual child (a tab, an item) when
// one does, else the control itself.
void mark_deepest(std::vector<FlatNode>& nodes, std::size_t control_index, Point pointer, bool pointer_inside,
                  bool hovered) {
    std::size_t marked = control_index;
    if (pointer_inside) {
        const std::size_t end = control_index + nodes[control_index].subtree_size;
        for (std::size_t index = control_index + 1U; index < end; ++index) {
            if (!nodes[index].is_virtual) continue;
            if (!contains_point(nodes[index].bounds, pointer)) continue;
            if (nodes[index].depth > nodes[marked].depth || marked == control_index) marked = index;
        }
    }
    if (hovered) {
        nodes[marked].hovered = true;
    } else {
        nodes[marked].pressed = true;
    }
}

void append_number(double value, std::string& text) {
    std::array<char, 32> buffer{};
    const std::to_chars_result result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    if (result.ec != std::errc{}) {
        text += "?";
        return;
    }
    text.append(buffer.data(), static_cast<std::size_t>(result.ptr - buffer.data()));
}

// Text quoted for an outline line: quotes, backslashes and line breaks
// escaped, and cut at 200 bytes on a character boundary, saying so (the
// text tool reads the whole of it).
void append_quoted(std::string_view value, std::string& text) {
    constexpr std::size_t maximum_quoted_bytes = 200U;
    std::size_t length = value.size();
    bool cut = false;
    if (length > maximum_quoted_bytes) {
        length = maximum_quoted_bytes;
        // Back to the start of a UTF-8 sequence.
        while (length > 0U && (static_cast<unsigned char>(value[length]) & 0xC0U) == 0x80U) --length;
        cut = true;
    }
    text += '"';
    for (std::size_t index = 0U; index < length; ++index) {
        const char character = value[index];
        if (character == '"' || character == '\\') {
            text += '\\';
            text += character;
        } else if (character == '\n') {
            text += "\\n";
        } else if (character == '\r') {
            text += "\\r";
        } else if (character == '\t') {
            text += "\\t";
        } else {
            text += character;
        }
    }
    if (cut) text += "...";
    text += '"';
    if (cut) text += " (cut at 200 bytes: text reads all " + std::to_string(value.size()) + ")";
}

// The states an outline line names, without "enabled" and "visible" (the
// usual case): their absence is named instead.
struct StateName final {
    SemanticState state{SemanticState::none};
    const char* name{};
};
constexpr std::array<StateName, 11> listed_states{{
    {SemanticState::selected, "selected"},
    {SemanticState::checked, "checked"},
    {SemanticState::mixed, "mixed"},
    {SemanticState::read_only, "read_only"},
    {SemanticState::expanded, "expanded"},
    {SemanticState::busy, "busy"},
    {SemanticState::protected_content, "protected"},
    {SemanticState::invalid, "invalid"},
    {SemanticState::checkable, "checkable"},
    {SemanticState::multiselectable, "multiselectable"},
    {SemanticState::focusable, "focusable"},
}};

void append_states(const FlatNode& node, std::string& text) {
    if (!has_semantic_state(node.states, SemanticState::enabled)) text += " disabled";
    if (!has_semantic_state(node.states, SemanticState::visible)) text += " hidden";
    // Whether a password field has focus is never exposed (study 9.1).
    if (has_semantic_state(node.states, SemanticState::focused) && !node.protected_content()) text += " focused";
    for (const StateName& entry : listed_states) {
        if (!has_semantic_state(node.states, entry.state)) continue;
        text += ' ';
        text += entry.name;
    }
    if (node.modal) text += " modal";
    if (node.hovered) text += " hovered";
    if (node.pressed) text += " pressed";
    // An opener needs the user (MCP round 1): the dialog it opens is the
    // user's to answer.
    if (node.opens_dialog) text += " needs_user";
}

// The node's actions beyond focus (focusable says that), and what its
// press opens.
void append_actions(const FlatNode& node, std::string& text) {
    std::string listed{};
    for (const SemanticAction action : node.actions) {
        if (action == SemanticAction::focus) continue;
        if (!listed.empty()) listed += ',';
        listed += semantic_action_name(action);
    }
    if (!listed.empty()) text += " actions=" + listed;
    const bool presses = std::find(node.actions.begin(), node.actions.end(), SemanticAction::press) !=
                         node.actions.end();
    if (node.opens_dialog) {
        text += presses ? " press=opens_dialog" : " opens=dialog";
        if (!node.dialog_tool.empty()) text += " instead=" + node.dialog_tool;
    } else if (node.has_popup) {
        text += presses ? " press=opens_popup" : " opens=popup";
    }
}

char lower(char character) noexcept {
    if (character >= 'A' && character <= 'Z') return static_cast<char>(character - 'A' + 'a');
    return character;
}

bool contains_text(std::string_view haystack, std::string_view needle) noexcept {
    if (needle.empty()) return true;
    if (needle.size() > haystack.size()) return false;
    for (std::size_t start = 0U; start + needle.size() <= haystack.size(); ++start) {
        bool same = true;
        for (std::size_t index = 0U; index < needle.size(); ++index) {
            if (lower(haystack[start + index]) != lower(needle[index])) {
                same = false;
                break;
            }
        }
        if (same) return true;
    }
    return false;
}

bool node_matches(const FlatNode& node, std::string_view filter) noexcept {
    const bool matches = contains_text(semantic_role_name(node.role), filter) || contains_text(node.name, filter) ||
                         contains_text(node.stable_id, filter);
    return matches;
}

std::string node_key(const FlatNode& node) {
    if (!node.stable_id.empty()) return node.stable_id;
    std::string key = "#r" + std::to_string(node.runtime_id);
    return key;
}

using NodeIndex = std::unordered_map<std::string, std::size_t>;

void index_nodes(std::span<const FlatNode> nodes, NodeIndex& index) {
    index.clear();
    index.reserve(nodes.size());
    for (std::size_t position = 0U; position < nodes.size(); ++position) {
        // A repeated key keeps its first node; the diff then compares those.
        static_cast<void>(index.emplace(node_key(nodes[position]), position));
    }
}

std::size_t focused_node(std::span<const FlatNode> nodes) noexcept {
    for (std::size_t index = 0U; index < nodes.size(); ++index) {
        if (has_semantic_state(nodes[index].states, SemanticState::focused)) return index;
    }
    return no_parent;
}

// Appends the subtrees of `nodes` whose keys `other` lacks (and whose
// parents it has), as added or removed lines.
void append_subtree_changes(std::span<const FlatNode> nodes, const NodeIndex& other, const char* label,
                            std::size_t maximum_entries, bool terse, std::string& text) {
    std::size_t shown = 0U;
    std::size_t more = 0U;
    std::size_t subtrees = 0U;
    std::size_t total = 0U;
    std::size_t index = 0U;
    while (index < nodes.size()) {
        const FlatNode& node = nodes[index];
        if (other.find(node_key(node)) != other.end()) {
            ++index;
            continue;
        }
        ++subtrees;
        total += node.subtree_size;
        if (terse) {
            index += node.subtree_size;
            continue;
        }
        if (shown < maximum_entries) {
            text += label;
            append_node_line(node, false, text);
            text += " (" + std::to_string(node.subtree_size) + (node.subtree_size == 1U ? " node)\n" : " nodes)\n");
            ++shown;
        } else {
            ++more;
        }
        // The whole subtree is new or gone: its nodes are not listed again.
        index += node.subtree_size;
    }
    if (more != 0U) text += std::string(label) + "... and " + std::to_string(more) + " more\n";
    if (terse && subtrees != 0U) {
        text += std::string(label) + std::to_string(subtrees) + (subtrees == 1U ? " subtree, " : " subtrees, ") +
                std::to_string(total) + (total == 1U ? " node\n" : " nodes\n");
    }
}

} // namespace

void append_node_actions(const FlatNode& node, std::string& text) {
    append_actions(node, text);
}

bool contains_any_case(std::string_view haystack, std::string_view needle) noexcept {
    const bool found = contains_text(haystack, needle);
    return found;
}

bool known_role_name(std::string_view name) noexcept {
    for (std::size_t index = 0U; index < semantic_role_count; ++index) {
        if (name == semantic_role_name(static_cast<SemanticRole>(index))) return true;
    }
    return false;
}

std::string role_names() {
    std::string names{};
    for (std::size_t index = 0U; index < semantic_role_count; ++index) {
        if (!names.empty()) names += ", ";
        names += semantic_role_name(static_cast<SemanticRole>(index));
    }
    return names;
}

void flatten_snapshot(const SemanticSnapshot& snapshot, const InteractionSnapshot& interaction,
                      std::vector<FlatNode>& nodes) {
    nodes.clear();
    nodes.reserve(snapshot.node_count);
    std::vector<FlattenFrame> work{};
    for (std::size_t index = snapshot.roots.size(); index > 0U; --index) {
        work.push_back(FlattenFrame{&snapshot.roots[index - 1U], 0U, no_parent, 0U});
    }
    while (!work.empty()) {
        const FlattenFrame frame = work.back();
        work.pop_back();
        const std::size_t position = nodes.size();
        nodes.push_back(flat_node_of(*frame.node, frame.depth, frame.parent, frame.control_runtime_id));
        const std::uint64_t control = nodes[position].control_runtime_id;
        const std::vector<SemanticNode>& children = (*frame.node).children;
        for (std::size_t index = children.size(); index > 0U; --index) {
            work.push_back(FlattenFrame{&children[index - 1U], frame.depth + 1U, position, control});
        }
    }
    // Preorder: a parent comes before its children, so one backward pass
    // sums each subtree into its parent.
    for (std::size_t index = nodes.size(); index > 0U; --index) {
        const FlatNode& node = nodes[index - 1U];
        if (node.parent != no_parent) nodes[node.parent].subtree_size += node.subtree_size;
    }
    for (std::size_t index = 0U; index < nodes.size(); ++index) {
        if (nodes[index].is_virtual) continue;
        if (interaction.hovered.value != 0U && nodes[index].runtime_id == interaction.hovered.value) {
            mark_deepest(nodes, index, interaction.pointer_position, interaction.pointer_inside, true);
        }
        if (interaction.pressed.value != 0U && nodes[index].runtime_id == interaction.pressed.value) {
            mark_deepest(nodes, index, interaction.pointer_position, interaction.pointer_inside, false);
        }
    }
}

std::string node_reference(const FlatNode& node) {
    if (!node.stable_id.empty()) {
        std::string reference = "[" + node.stable_id + "]";
        return reference;
    }
    std::string reference = "[#r" + std::to_string(node.runtime_id) + "]";
    return reference;
}

void append_node_line(const FlatNode& node, bool with_bounds, std::string& text) {
    text += semantic_role_name(node.role);
    if (!node.name.empty()) {
        text += ' ';
        append_quoted(node.name, text);
    }
    text += ' ';
    text += node_reference(node);
    append_states(node, text);
    if (node.has_numeric) {
        text += " value=";
        append_number(node.numeric_value, text);
    } else if (!node.value.empty()) {
        text += " value=";
        append_quoted(node.value, text);
    }
    if (node.has_range) {
        text += " range=";
        append_number(node.minimum_value, text);
        text += "..";
        append_number(node.maximum_value, text);
    }
    if (with_bounds) {
        text += " bounds=";
        append_number(node.bounds.x, text);
        text += ',';
        append_number(node.bounds.y, text);
        text += ',';
        append_number(node.bounds.width, text);
        text += 'x';
        append_number(node.bounds.height, text);
    }
}

namespace {

// After a parent's line: the rows its children show of their whole set,
// when they are fewer (a virtualised list), and how to reach the rest.
void append_rows_shown(std::span<const FlatNode> nodes, std::size_t parent, std::string& text) {
    std::size_t first = 0U;
    std::size_t last = 0U;
    std::size_t total = 0U;
    std::size_t shown = 0U;
    std::string example{};
    const std::size_t end = parent + nodes[parent].subtree_size;
    for (std::size_t index = parent + 1U; index < end; index += nodes[index].subtree_size) {
        const FlatNode& child = nodes[index];
        if (child.position_in_set == 0U || child.set_size == 0U) continue;
        if (first == 0U || child.position_in_set < first) first = child.position_in_set;
        if (child.position_in_set > last) last = child.position_in_set;
        total = std::max(total, child.set_size);
        if (example.empty()) example = node_reference(child);
        ++shown;
    }
    if (shown == 0U || shown >= total) return;
    text += " rows=" + std::to_string(first) + ".." + std::to_string(last) + "/" + std::to_string(total) + " (" +
            std::to_string(total - shown) + " more rows not shown: act on a row by its ID, as " + example +
            " is named, even when it is not shown)";
}

} // namespace

void append_outline(std::span<const FlatNode> nodes, const OutlineOptions& options, std::string& text) {
    std::size_t first = 0U;
    std::size_t end = nodes.size();
    std::size_t base_depth = 0U;
    if (options.root != no_parent && options.root < nodes.size()) {
        first = options.root;
        end = options.root + nodes[options.root].subtree_size;
        base_depth = nodes[options.root].depth;
    }
    // With a filter, a node shows when it matches or holds a match.
    std::vector<bool> shown(end - first, options.filter.empty());
    if (!options.filter.empty()) {
        for (std::size_t index = first; index < end; ++index) {
            if (!node_matches(nodes[index], options.filter)) continue;
            std::size_t ancestor = index;
            while (ancestor != no_parent && ancestor >= first && !shown[ancestor - first]) {
                shown[ancestor - first] = true;
                ancestor = nodes[ancestor].parent;
            }
        }
    }
    std::size_t written = 0U;
    std::size_t left_out = 0U;
    std::size_t without_identity = 0U;
    for (std::size_t index = first; index < end; ++index) {
        if (!shown[index - first]) continue;
        if (written >= options.node_limit) {
            ++left_out;
            continue;
        }
        const FlatNode& node = nodes[index];
        text.append(2U * (node.depth - base_depth + 1U), ' ');
        append_node_line(node, options.with_bounds, text);
        if (options.with_actions) append_actions(node, text);
        append_rows_shown(nodes, index, text);
        text += '\n';
        if (node.stable_id.empty()) ++without_identity;
        ++written;
    }
    if (left_out != 0U) {
        text += "... " + std::to_string(left_out) + " more nodes not shown (limit " +
                std::to_string(options.node_limit) + "); narrow it with root or filter\n";
    }
    if (without_identity != 0U) {
        text += std::to_string(without_identity) +
                " nodes have no stable ID: their [#r...] runtime IDs hold for this run only\n";
    }
}

bool names_an_opener(std::string_view name) noexcept {
    constexpr std::string_view dots = "...";
    constexpr std::string_view ellipsis = "\xE2\x80\xA6";
    const bool dotted = name.size() > dots.size() && name.substr(name.size() - dots.size()) == dots;
    const bool ellipsed = name.size() > ellipsis.size() && name.substr(name.size() - ellipsis.size()) == ellipsis;
    return dotted || ellipsed;
}

std::string modal_scope_of(const Window& window) {
    const Control::Ptr scope = window.active_focus_scope_root();
    if (!scope || scope == window.root()) return std::string{};
    // A modal dialog at or above the scope's root (a menu's scope is not
    // modal, and leaves the tree whole).
    for (Control::Ptr ancestor = scope; ancestor != nullptr; ancestor = (*ancestor).parent()) {
        const SemanticDescriptor descriptor = (*ancestor).semantic_descriptor();
        if (descriptor.role != SemanticRole::dialog || !descriptor.modal) continue;
        const std::string_view id = (*ancestor).stable_id().value();
        return id.empty() ? std::string("dialog") : std::string(id);
    }
    return std::string{};
}

void append_diff(std::span<const FlatNode> before, std::span<const FlatNode> after, const DiffOptions& options,
                 std::string& text) {
    const std::size_t maximum_entries = options.maximum_entries;
    NodeIndex before_index{};
    NodeIndex after_index{};
    index_nodes(before, before_index);
    index_nodes(after, after_index);
    std::string changes{};
    std::size_t changed = 0U;
    std::size_t more = 0U;
    for (const FlatNode& node : after) {
        const NodeIndex::const_iterator found = before_index.find(node_key(node));
        if (found == before_index.end()) continue;
        const FlatNode& old = before[(*found).second];
        std::string entry{};
        const bool hidden = node.protected_content() || old.protected_content();
        for (const SemanticState state : semantic_states) {
            if (state == SemanticState::focused && hidden) continue;
            const bool was = has_semantic_state(old.states, state);
            const bool is = has_semantic_state(node.states, state);
            if (was == is) continue;
            entry += is ? " +" : " -";
            entry += state == SemanticState::protected_content ? "protected" : semantic_state_name(state);
        }
        if (old.hovered != node.hovered) entry += node.hovered ? " +hovered" : " -hovered";
        if (old.pressed != node.pressed) entry += node.pressed ? " +pressed" : " -pressed";
        if (old.name != node.name) {
            entry += " name ";
            append_quoted(old.name, entry);
            entry += " -> ";
            append_quoted(node.name, entry);
        }
        if (!hidden && (old.value != node.value || old.numeric_value != node.numeric_value)) {
            entry += " value ";
            if (node.has_numeric) {
                append_number(old.numeric_value, entry);
                entry += " -> ";
                append_number(node.numeric_value, entry);
            } else {
                append_quoted(old.value, entry);
                entry += " -> ";
                append_quoted(node.value, entry);
            }
        }
        if (entry.empty()) continue;
        if (options.terse) {
            ++more;
            continue;
        }
        if (changed < maximum_entries) {
            if (changed != 0U) changes += ';';
            changes += ' ';
            changes += node_reference(node);
            changes += entry;
            ++changed;
        } else {
            ++more;
        }
    }
    if (changed != 0U) {
        text += "changed:" + changes;
        if (more != 0U) text += "; ... and " + std::to_string(more) + " more";
        text += '\n';
    } else if (options.terse && more != 0U) {
        text += "changed: " + std::to_string(more) + (more == 1U ? " node\n" : " nodes\n");
    }
    // A modal that opened scopes the tree to itself: what it hides is
    // obscured, not removed; when it closes, the hidden nodes come back
    // (MCP round 1).
    const bool scoped = options.scope_before != options.scope_after;
    const char* removed_label = "removed: ";
    const char* added_label = "added: ";
    if (scoped && !options.scope_after.empty()) {
        text += "a modal now scopes the tree: [" + options.scope_after +
                "]; the nodes behind it are obscured, not removed\n";
        removed_label = "obscured: ";
    }
    if (scoped && !options.scope_before.empty()) {
        text += "the modal [" + options.scope_before + "] no longer scopes the tree\n";
        added_label = "revealed: ";
    }
    append_subtree_changes(after, before_index, added_label, maximum_entries, options.terse, text);
    append_subtree_changes(before, after_index, removed_label, maximum_entries, options.terse, text);
    const std::size_t focus_before = focused_node(before);
    const std::size_t focus_after = focused_node(after);
    const std::string key_before = focus_before == no_parent ? std::string{} : node_key(before[focus_before]);
    const std::string key_after = focus_after == no_parent ? std::string{} : node_key(after[focus_after]);
    const bool hidden_before = focus_before != no_parent && before[focus_before].protected_content();
    const bool hidden_after = focus_after != no_parent && after[focus_after].protected_content();
    // A move into or out of a password field is left out (study 9.1).
    if (key_before != key_after && !hidden_before && !hidden_after) {
        text += "focus: ";
        text += focus_before == no_parent ? std::string("(none)") : node_reference(before[focus_before]);
        text += " -> ";
        text += focus_after == no_parent ? std::string("(none)") : node_reference(after[focus_after]);
        text += '\n';
    }
}

ResolvedTarget resolve_target(std::span<const FlatNode> nodes, std::string_view target, std::string_view within) {
    ResolvedTarget resolved{};
    std::uint64_t runtime = 0U;
    const bool by_runtime = target.size() > 2U && target[0] == '#' && target[1] == 'r';
    if (by_runtime) {
        const char* const first = target.data() + 2;
        const char* const last = target.data() + target.size();
        const std::from_chars_result result = std::from_chars(first, last, runtime);
        if (result.ec != std::errc{} || result.ptr != last) runtime = 0U;
    }
    std::vector<std::size_t> matches{};
    for (std::size_t index = 0U; index < nodes.size(); ++index) {
        const bool same = by_runtime ? nodes[index].runtime_id == runtime && runtime != 0U
                                     : nodes[index].stable_id == target;
        if (!same) continue;
        if (!within.empty()) {
            bool inside = false;
            std::size_t ancestor = nodes[index].parent;
            while (ancestor != no_parent) {
                if (nodes[ancestor].stable_id == within) {
                    inside = true;
                    break;
                }
                ancestor = nodes[ancestor].parent;
            }
            if (!inside) continue;
        }
        matches.push_back(index);
    }
    if (matches.size() == 1U) {
        resolved.resolution = Resolution::found;
        resolved.index = matches[0];
        return resolved;
    }
    if (matches.size() > 1U) {
        resolved.resolution = Resolution::ambiguous;
        resolved.hint = "the ID is repeated; name the parent with `within`: under ";
        for (std::size_t index = 0U; index < matches.size() && index < 8U; ++index) {
            if (index != 0U) resolved.hint += ", ";
            const std::size_t parent = nodes[matches[index]].parent;
            resolved.hint += parent == no_parent ? std::string("(the window)") : node_reference(nodes[parent]);
        }
        return resolved;
    }
    // Not found: the closest IDs by name, using the target's last segment.
    resolved.resolution = Resolution::not_found;
    const std::size_t dot = target.rfind('.');
    const std::string_view segment = dot == std::string_view::npos ? target : target.substr(dot + 1U);
    std::string close{};
    std::size_t listed = 0U;
    for (const FlatNode& node : nodes) {
        if (listed >= 5U) break;
        if (node.stable_id.empty() || segment.empty()) continue;
        if (!contains_text(node.stable_id, segment) && !contains_text(node.name, segment)) continue;
        if (listed != 0U) close += ", ";
        close += node_reference(node);
        ++listed;
    }
    resolved.hint = listed == 0U ? std::string("take a snapshot to see the IDs") : "closest: " + close;
    return resolved;
}

bool subtree_protected(std::span<const FlatNode> nodes, std::size_t index) noexcept {
    if (index >= nodes.size()) return false;
    const std::size_t end = index + nodes[index].subtree_size;
    for (std::size_t position = index; position < end && position < nodes.size(); ++position) {
        if (nodes[position].protected_content()) return true;
    }
    return false;
}

} // namespace gui_forms::automation::detail
