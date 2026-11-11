#pragma once

#include "gui_forms/text/types/text_types.hpp"
#include "gui_forms/types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

// Semantic roles (design language 8.5). split_button (a button with a
// separate menu arrow) and menu_button (a button that opens a menu) were
// admitted with the ribbon plan (owner decision RB-22, 12.8); pane (a tool
// panel or another F6 region) and tab_panel (the content a tab shows) with
// the docking plan (owner decisions DK-35 and DK-40, 12.10). The stage A
// vocabulary (owner, 2026-10-21, 12.13) admitted tree and tree_item (a
// hierarchy of items), table, row, cell, column_header and row_header (a
// grid of cells), tree_grid (a table whose rows are tree items), dialog,
// status_bar, and live_region (a region whose changes are announced, owner
// decision DK-51). meter (W-5; the 0.5 closure's ruling 2 admits the
// instrument vocabulary only) is a read-only reading within a known range,
// such as a level, a gauge or a scope: unlike progress_bar it measures, it
// does not report a task's progress. toggle_switch (W-4; a ToggleSwitch,
// on or off at once, named so because `switch` is a C++ keyword) and
// search_box (W-4; a SearchField, a text box whose text is a query) are
// specialisations: a host without a mapping of its own publishes them as
// their fallbacks, check_box and text_box (semantic_role_fallback), and
// both act as their fallbacks do. figure (IMGUI_IMPLOT_STUDY B.4) is a
// plot or chart: an image whose description summarises the data it shows,
// with a keyboard point cursor and a table companion; its fallback is
// image. `count` is not a role; it sizes tables over the roles.
enum class SemanticRole : std::uint8_t {
    generic, group, static_text, button, check_box, radio_button, link,
    text_box, list, list_item, check_list_item, combo_box, slider,
    scroll_bar, progress_bar, image, tab_group, tab, split_pane,
    numeric_field, tool_tip, date_picker, calendar, date_cell, property_grid,
    property_group, property_row, menu_bar, menu_bar_item, menu, menu_item,
    separator, toolbar, radio_group, split_button, menu_button, pane,
    tab_panel, tree, tree_item, table, row, cell, column_header, row_header,
    tree_grid, dialog, status_bar, live_region, meter, toggle_switch, search_box,
    figure, count,
};

inline constexpr std::size_t semantic_role_count = static_cast<std::size_t>(SemanticRole::count);

// The role `role` specialises, which a host without a mapping for `role`
// publishes, and whose behaviour (patterns, interfaces, checked value and
// text) `role` shares: check_box for toggle_switch, text_box for
// search_box, image for figure. Every other role is its own fallback.
[[nodiscard]] constexpr SemanticRole semantic_role_fallback(SemanticRole role) noexcept {
    if (role == SemanticRole::toggle_switch) return SemanticRole::check_box;
    if (role == SemanticRole::search_box) return SemanticRole::text_box;
    if (role == SemanticRole::figure) return SemanticRole::image;
    // Every other role is its own fallback.
    return role;
}

enum class SemanticState : std::uint32_t {
    none = 0,
    enabled = 1U << 0U,
    visible = 1U << 1U,
    // The node's control can take focus, or a control the node hides (one
    // that publishes no node of its own, such as a composite's inner editor)
    // can; focused likewise covers focus on such a hidden control.
    focusable = 1U << 2U,
    focused = 1U << 3U,
    selected = 1U << 4U,
    checked = 1U << 5U,
    mixed = 1U << 6U,
    read_only = 1U << 7U,
    expanded = 1U << 8U,
    busy = 1U << 9U,
    protected_content = 1U << 10U,
    invalid = 1U << 11U,
    // Admitted with the stage A vocabulary (12.13): the node can be checked
    // (a check box, a toggle button, a checkable menu item, a docking pin),
    // whether or not it is now; it selects more than one of its items at a
    // time.
    checkable = 1U << 12U,
    multiselectable = 1U << 13U,
};

// Every semantic state, in bit order, for tables and the schema export.
inline constexpr SemanticState semantic_states[] = {
    SemanticState::enabled, SemanticState::visible, SemanticState::focusable,
    SemanticState::focused, SemanticState::selected, SemanticState::checked,
    SemanticState::mixed, SemanticState::read_only, SemanticState::expanded,
    SemanticState::busy, SemanticState::protected_content, SemanticState::invalid,
    SemanticState::checkable, SemanticState::multiselectable,
};

[[nodiscard]] constexpr SemanticState operator|(
    SemanticState left, SemanticState right) noexcept {
    return static_cast<SemanticState>(static_cast<std::uint32_t>(left) |
                                      static_cast<std::uint32_t>(right));
}
constexpr SemanticState& operator|=(SemanticState& left,
                                    SemanticState right) noexcept {
    left = left | right;
    return left;
}
[[nodiscard]] constexpr bool has_semantic_state(
    SemanticState states, SemanticState requested) noexcept {
    return (static_cast<std::uint32_t>(states) &
            static_cast<std::uint32_t>(requested)) != 0U;
}

// Semantic actions. close (a document tab's or a panel's close, owner
// decision DK-35, 12.10) was admitted with the docking plan; tabs publish
// select and, where they can be closed, close, but not press. `count` is
// not an action; it sizes tables over the actions.
enum class SemanticAction : std::uint8_t {
    focus, press, select, increment, decrement, expand, collapse, show_menu,
    set_value, close,
    count,
};

inline constexpr std::size_t semantic_action_count = static_cast<std::size_t>(SemanticAction::count);

// Whether a tab list's tabs run in a row or a column (owner decision
// DK-40).
enum class SemanticOrientation : std::uint8_t { horizontal, vertical };

// The selection and caret of a node's text (owner decision A-T1, 12.13), as
// UTF-8 byte offsets into the node's value, on grapheme boundaries; with
// nothing selected the anchor and the caret are the same offset.
struct SemanticTextSelection final {
    Utf8Offset anchor{};
    Utf8Offset caret{};
    friend constexpr bool operator==(const SemanticTextSelection& left,
                                     const SemanticTextSelection& right) noexcept {
        const bool same = left.anchor == right.anchor && left.caret == right.caret;
        return same;
    }
};

// One visual line of a text node's layout: the UTF-8 range of the node's
// value it shows (without the line break that ends it) and its box. Boxes
// are in window client coordinates, as node bounds are, and may lie outside
// the node's bounds when the text is scrolled.
struct SemanticTextLine final {
    TextRange<Utf8Offset> range{};
    Rect bounds{};
};

// One grapheme of a text node's layout: its UTF-8 range in the node's value,
// the index of its visual line in SemanticTextLayout::lines, and its box. A
// line break is not a grapheme of any line.
struct SemanticTextGrapheme final {
    TextRange<Utf8Offset> range{};
    LineIndex line{};
    Rect bounds{};
};

// The layout of a text node (owner decision A-T1, 12.13), built only when a
// host asks through Window::semantic_text_layout. Lines and graphemes are in
// text order. `word_starts` are the offsets at which the control's own word
// rule stops (its Ctrl+Left and Ctrl+Right), ascending: 0, each word start,
// and the end of the text; empty for empty text. Sentences are not reported
// (A-T2).
struct SemanticTextLayout final {
    std::vector<SemanticTextLine> lines;
    std::vector<SemanticTextGrapheme> graphemes;
    std::vector<Utf8Offset> word_starts;
    // Empties the three lists and keeps their storage for the next query.
    void clear() noexcept {
        lines.clear();
        graphemes.clear();
        word_starts.clear();
    }
};

// What a text query or a selection change did (Window::semantic_text_layout
// and Window::set_semantic_text_selection). Every result except `done`
// leaves the control as it was and the caller's layout empty.
enum class SemanticTextResult : std::uint8_t {
    done,
    // No attached control has that runtime identity: it was disposed,
    // detached or replaced (interview GF017).
    stale_identity,
    // The query arrived while the window paints; the host asks again later.
    during_paint,
    // The control lies outside the active focus scope (a selection change
    // only; a layout may be read anywhere).
    outside_focus_scope,
    // The node publishes no text a host may read or select: a control
    // without editable text, or a password box (A-T2).
    no_text,
    // An offset past the end of the value or inside a grapheme, such as
    // within a combining sequence (A-T2).
    off_boundary,
};

// The units a host moves through a text node by (A-T2): a grapheme, a word
// by the control's own rule, a visual line, a paragraph (a logical line with
// its break) and the whole text. Sentences are not supported.
enum class SemanticTextUnit : std::uint8_t {
    character,
    word,
    line,
    paragraph,
    document,
};

// The position of an element among its siblings of one set (a list's items,
// a strip's buttons) and the set's size, both counting from one, and whether
// the element opens an overlay (a menu, a list or a dialog) when activated.
// These node fields were admitted with the ribbon plan (owner decision
// RB-22); a position is published with its set size or not at all.
//
// `controlled_panel` and `orientation` were admitted with the docking plan
// (owner decision DK-40). On a tab, `controlled_panel` is the stable
// identity of the tab_panel node the tab shows (the tab-to-panel
// relation), or empty; the panel need not be in the same snapshot, since a
// hidden page's panel is not. On a tab list (tab_group), `orientation` says
// whether its tabs run in a row or a column; other nodes leave it empty.
//
// The stage A vocabulary (owner, 2026-10-21, 12.13) admitted these fields:
// - `level`: a tree item's depth, counting from one at the top level;
// - `modal`: a dialog blocks the rest of its window while it is open;
// - `row_index` and `column_index`: a row's, cell's or header's place in its
//   table, counting from one; `row_count` and `column_count`: a table's size
//   (all rows and columns, realized or not);
// - `access_key`: the character of the control's mnemonic, as UTF-8 (the
//   host adds its modifier, Alt on Windows and Linux); empty without one;
// - `small_step` and `large_step`: how far a range node's value moves for a
//   small change (an arrow key) and a large one (a page key);
// - `text_selection` and `multiline`: a text node's selection and caret
//   (absent for protected content) and whether its text runs over several
//   lines.
//
// `announces_on_appearance` (owner ruling, 2026-11-08: a toast is a polite
// live region): a live region whose name is spoken when its node first
// appears, not only when its name changes later (semantic_announcement_due).
// A live region present from the start (a dock site's announcements, a
// banner) leaves it false, so a page that shows one announces nothing.
struct SemanticDescriptor final {
    SemanticRole role{SemanticRole::generic};
    std::string name;
    std::string value;
    std::string description;
    std::optional<double> numeric_value;
    std::optional<double> minimum_value;
    std::optional<double> maximum_value;
    std::optional<std::size_t> position_in_set;
    std::optional<std::size_t> set_size;
    bool has_popup{};
    std::string controlled_panel;
    std::optional<SemanticOrientation> orientation;
    std::optional<std::size_t> level;
    bool modal{};
    std::optional<std::size_t> row_index;
    std::optional<std::size_t> column_index;
    std::optional<std::size_t> row_count;
    std::optional<std::size_t> column_count;
    std::string access_key;
    std::optional<double> small_step;
    std::optional<double> large_step;
    std::optional<SemanticTextSelection> text_selection;
    bool multiline{};
    bool announces_on_appearance{};
    SemanticState states{SemanticState::none};
    std::vector<SemanticAction> actions;
    bool exposed{};
    bool include_descendants{true};
};

// The deepest a control's virtual semantic children may nest below the
// control's own node: a child the control publishes directly is at level
// one. The window refuses a control's virtual children that nest deeper
// (std::invalid_argument naming the control, as it refuses an invalid
// virtual stable ID) before they join a snapshot. The stock controls nest
// at most four levels (a ribbon's tab, group and items; a grid's row, cell
// and error glyph).
//
// With the control nesting limit (64 levels), every snapshot is at most
// 64 + 16 levels deep, so the walks over it (the snapshot's JSON, the
// tool-tip and sub-surface lookups, the hosts' accessibility trees) may
// recurse once per level and a SemanticNode tree is destroyed recursively:
// at a few hundred bytes per level, at most about 40 KiB of stack.
inline constexpr std::size_t maximum_virtual_semantic_depth = 16U;

struct SemanticNode final {
    std::uint64_t runtime_id{};
    std::string stable_id;
    SemanticRole role{SemanticRole::generic};
    std::string name;
    std::string value;
    std::string description;
    std::optional<double> numeric_value;
    std::optional<double> minimum_value;
    std::optional<double> maximum_value;
    std::optional<std::size_t> position_in_set;
    std::optional<std::size_t> set_size;
    bool has_popup{};
    std::string controlled_panel;
    std::optional<SemanticOrientation> orientation;
    std::optional<std::size_t> level;
    bool modal{};
    std::optional<std::size_t> row_index;
    std::optional<std::size_t> column_index;
    std::optional<std::size_t> row_count;
    std::optional<std::size_t> column_count;
    std::string access_key;
    std::optional<double> small_step;
    std::optional<double> large_step;
    std::optional<SemanticTextSelection> text_selection;
    bool multiline{};
    bool announces_on_appearance{};
    // The thing's plain tooltip text (owner, 2026-10-28): a control's
    // Control::tool_tip_text, or an authored part's, such as a strip item's
    // or a tab header's. Data items never carry one. Empty for none.
    std::string tool_tip;
    Rect bounds{};
    SemanticState states{SemanticState::none};
    std::vector<SemanticAction> actions;
    std::vector<SemanticNode> children;
};

struct SemanticSnapshot final {
    std::uint64_t generation{};
    std::size_t node_count{};
    std::vector<SemanticNode> roots;
    [[nodiscard]] std::string to_json() const;
};

[[nodiscard]] const char* semantic_role_name(SemanticRole role) noexcept;
[[nodiscard]] const char* semantic_action_name(SemanticAction action) noexcept;
[[nodiscard]] const char* semantic_orientation_name(SemanticOrientation orientation) noexcept;
[[nodiscard]] const char* semantic_text_result_name(SemanticTextResult result) noexcept;
[[nodiscard]] const char* semantic_text_unit_name(SemanticTextUnit unit) noexcept;

// Fills `boundaries` with the offsets in `value` at which `unit` starts or
// ends, ascending and distinct, from `layout` (Window::semantic_text_layout
// of the node whose value is `value`): always 0 and the end of the text,
// which coincide for empty text (one boundary). Characters are bounded by
// each grapheme's start and end, so a line break, which is no grapheme,
// counts as one character; words by the layout's word starts; lines by each
// visual line's start (a line runs to the next line's start, so it holds
// its break); paragraphs after each line feed. The text between two
// adjacent boundaries is one unit. Replaces what `boundaries` held, reusing
// its storage. Throws std::bad_alloc when memory runs out, leaving
// `boundaries` unspecified.
void fill_semantic_text_boundaries(const SemanticTextLayout& layout, std::string_view value,
                                   SemanticTextUnit unit, std::vector<Utf8Offset>& boundaries);
// The name of one state bit (`state` holds exactly one of semantic_states).
[[nodiscard]] const char* semantic_state_name(SemanticState state) noexcept;
// The node fields the vocabulary admits, by name, for the schema export.
[[nodiscard]] std::span<const char* const> semantic_field_names() noexcept;

// Whether a host asks assistive technology to speak `current`'s name now, at
// polite priority (DK-51; the toast, owner ruling 2026-11-08). `current` is a
// node the host is publishing and `previous` the same node (same control,
// same virtual child) at the host's last publication, or null when the node
// is new. Due when `current` is a live region with a non-empty name and
// either `previous` had another name, or the node is new and announces on
// appearance. Every host decides through this one rule: UI Automation raises
// the live-region event, AppKit posts an announcement request, AT-SPI emits
// object:announcement; the text spoken is `current.name`.
[[nodiscard]] bool semantic_announcement_due(const SemanticNode* previous, const SemanticNode& current) noexcept;

// The runtime ID of a virtual semantic child (a tab, an item, a caption
// button: a SemanticNode with no control of its own), from its stable ID: a
// 64-bit FNV-1a-style hash of the ID's bytes with the top bit set, so it
// never meets a control's own runtime ID (those count up from 1). The same
// stable ID always gives the same runtime ID, on every host.
// A virtual child the control publishes with runtime ID zero is given this
// one when the window assembles its snapshot, so no node below the window
// has runtime ID zero.
[[nodiscard]] std::uint64_t virtual_semantic_runtime_id(std::string_view stable_id) noexcept;

// True for a runtime ID virtual_semantic_runtime_id gives (its top bit is
// set), false for a control's own runtime ID and for zero. A host uses it to
// tell a control's own node, which it names to
// Window::perform_semantic_action by that runtime ID, from a virtual child,
// which it names by the runtime ID of the nearest control's node above it
// and the child's stable ID.
[[nodiscard]] bool is_virtual_semantic_runtime_id(std::uint64_t runtime_id) noexcept;

} // namespace gui_forms
