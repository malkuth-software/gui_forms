#pragma once

// The automation core's private helpers, shared by its sources: arguments,
// outlines and diffs, resolution, and input building. Pure functions over
// their inputs: none keeps state or touches a window unless its contract
// says so.

#include "automation_core.hpp"

#include "../../../core/json/json_reader/json_reader.hpp"

#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/host/types/host_types.hpp"
#include "gui_forms/window/interaction/interaction_snapshot.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms::automation::detail {

using gui_forms::detail::JsonKind;
using gui_forms::detail::JsonValue;

// ---- arguments (automation_arguments.cpp) -----------------------------------

// The limits a call's arguments are read with: a call is small; a batch of
// at most maximum_batch_steps steps fits well inside them.
[[nodiscard]] gui_forms::detail::JsonLimits argument_limits() noexcept;
// Reads `json` as one object into `object`; an empty text is an empty
// object. False with `error` naming the fault otherwise.
[[nodiscard]] bool read_arguments(std::string_view json, JsonValue& object, std::string& error);
[[nodiscard]] std::optional<std::string> text_member(const JsonValue& object, std::string_view name);
[[nodiscard]] std::optional<double> number_member(const JsonValue& object, std::string_view name);
[[nodiscard]] std::optional<bool> boolean_member(const JsonValue& object, std::string_view name);
// The member if it is present and of `kind`, else null. Borrowed from
// `object`.
[[nodiscard]] const JsonValue* kind_member(const JsonValue& object, std::string_view name, JsonKind kind);
// A number's text as a double; false for text that is not one.
[[nodiscard]] bool number_value(const JsonValue& value, double& number);
// A point: {x, y}, the canonical form every tool documents, or the pair
// [x, y], which every tool also takes (1.0.5 review: capture took pairs and
// Plan Paint's tools objects). False for anything else.
[[nodiscard]] bool point_value(const JsonValue& value, double& x, double& y);
// The schema of a point as point_value reads it: the object form with
// `description`, or a pair.
[[nodiscard]] std::string point_schema(std::string_view description);
// Writes `value` as JSON text, appending to `text`. Recurses once per level:
// a value read with argument_limits() is at most 32 levels deep (the
// reader refuses deeper), about 200 bytes of stack a level.
void append_json(const JsonValue& value, std::string& text);
// Writes `object` without the member named `skip` (a batch step's "tool"),
// replacing `text`.
void object_without(const JsonValue& object, std::string_view skip, std::string& text);
// The members of `arguments` that the object schema `schema` does not name,
// as "a, b", with the members it names into `known`; empty when every
// member is named or the schema admits others (MCP round 1: unknown keys
// were silently ignored).
[[nodiscard]] std::string unknown_members(const JsonValue& arguments, std::string_view schema, std::string& known);
// Appends a JSON string (quoted, escaped) to `text`.
void append_json_string(std::string_view value, std::string& text);

// ---- outlines and diffs (automation_outline.cpp) ------------------------------

// Flattens `snapshot` in document order (an explicit work list: no
// recursion), marking the node under the pointer within the hovered and
// pressed controls (`interaction`). Replaces `nodes`.
void flatten_snapshot(const SemanticSnapshot& snapshot, const InteractionSnapshot& interaction,
                      std::vector<FlatNode>& nodes);
// "[stable.id]", or "[#r412]" for a node without one.
[[nodiscard]] std::string node_reference(const FlatNode& node);
// One outline line without indentation: role, quoted name, reference,
// states, value and range, and bounds when asked.
void append_node_line(const FlatNode& node, bool with_bounds, std::string& text);

struct OutlineOptions final {
    // The subtree to show: a node's index, or no_parent for the whole tree.
    std::size_t root{no_parent};
    // Shows nodes whose role, name or stable ID contains it (any case), with
    // their ancestors; empty shows every node.
    std::string filter{};
    std::size_t node_limit{400U};
    bool with_bounds{};
    // Each node's actions beyond focus, and what pressing it opens.
    bool with_actions{true};
};
// The node's actions beyond focus and what its press opens, as an outline
// line ends (" actions=press press=opens_dialog").
void append_node_actions(const FlatNode& node, std::string& text);
// Whether `haystack` contains `needle`, ASCII letters in any case.
[[nodiscard]] bool contains_any_case(std::string_view haystack, std::string_view needle) noexcept;
// Whether `name` is a semantic role's name, and every role's name.
[[nodiscard]] bool known_role_name(std::string_view name) noexcept;
[[nodiscard]] std::string role_names();
// Appends the outline of `nodes` (study 7.3) to `text`, indented two spaces
// a level below the window's line, with what it left out.
void append_outline(std::span<const FlatNode> nodes, const OutlineOptions& options, std::string& text);
// Appends what changed from `before` to `after` (study 7.4): changed
// states and values, added and removed subtrees, and the focus move, each
// kind bounded by `maximum_entries`. Protected content never shows its
// value, its focus or a focus move into or out of it (study 9.1). Appends
// nothing when nothing changed.
struct DiffOptions final {
    std::size_t maximum_entries{40U};
    // Counts in place of lists: changed nodes, added and removed subtrees.
    bool terse{};
    // The modal scope's root before and after (modal_scope_of), empty for
    // none: nodes a modal hides are obscured, not removed, and come back
    // when it closes.
    std::string scope_before{};
    std::string scope_after{};
};
void append_diff(std::span<const FlatNode> before, std::span<const FlatNode> after, const DiffOptions& options,
                 std::string& text);
// The stable ID of the control a modal scope (a dialog's focus scope) roots
// the window's semantic tree at, or empty while no modal scopes it.
[[nodiscard]] std::string modal_scope_of(const Window& window);
// Whether `name` ends in "..." or the ellipsis character (a command that
// asks for more, by convention).
[[nodiscard]] bool names_an_opener(std::string_view name) noexcept;

// How a target resolved.
enum class Resolution : std::uint8_t {
    found,
    not_found,
    ambiguous,
};
struct ResolvedTarget final {
    Resolution resolution{Resolution::not_found};
    std::size_t index{no_parent};
    // For not_found, the closest IDs by name; for ambiguous, the parents
    // of the matches. Ready to show as a hint.
    std::string hint{};
};
// Finds `target` ("stable.id", or "#r412" for a runtime ID) in `nodes`;
// `within`, when not empty, names an ancestor that must hold it.
[[nodiscard]] ResolvedTarget resolve_target(std::span<const FlatNode> nodes, std::string_view target,
                                            std::string_view within);

// The nodes under `index` (itself included) that are protected content.
[[nodiscard]] bool subtree_protected(std::span<const FlatNode> nodes, std::size_t index) noexcept;

// ---- input (automation_input.cpp) -----------------------------------------------

// A key with its modifiers, from text such as "Ctrl+Shift+S", "Enter",
// "Alt+F4" or "Cmd+Z".
struct KeyChord final {
    std::uint32_t physical_key{};
    Modifier modifiers{Modifier::none};
};
[[nodiscard]] bool parse_key_chord(std::string_view text, KeyChord& chord);
// Modifier names ("shift", "ctrl", "alt", "meta"), any case.
[[nodiscard]] bool parse_modifier(std::string_view text, Modifier& modifier);
[[nodiscard]] std::optional<SemanticAction> parse_semantic_action(std::string_view name);
[[nodiscard]] std::uint64_t nanoseconds_of(FrameTime time) noexcept;

} // namespace gui_forms::automation::detail
