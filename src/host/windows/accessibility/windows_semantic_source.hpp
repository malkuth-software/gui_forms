#pragma once

// One window's semantic tree as the Windows accessibility publishers see it:
// how a native object names its node, how the node is found again in a
// fresh snapshot, and how an action reaches the retained control. Shared by
// the MSAA objects (windows_accessibility.cpp) and the UI Automation
// providers (windows_automation.cpp). Win32 types stay inside
// src/host/windows/.
//
// Execution context: every member is used on the window's UI thread, the
// one owner of the semantic tree. MSAA calls from a client in another
// process arrive through WM_GETOBJECT and COM's apartment marshalling, which
// delivers them on this thread; in-process calls from this thread arrive
// directly. UI Automation calls arrive on this thread because every
// provider asks for COM threading (windows_automation.hpp).

#include "gui_forms/window.hpp"

#include <windows.h>

#include <oleauto.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace gui_forms::host {

// How a native object names its semantic node: the runtime ID of the
// nearest control-backed node at or above it (its scope) and its own
// runtime ID. A control's runtime ID is unique within the process and never
// reused, so the identity of a disposed control resolves to nothing, and a
// stale object is refused rather than reaching another control. A virtual
// child's runtime ID comes from its stable ID, which is unique within the
// control that publishes it but not within the window, so it is looked for
// only below its scope. The window's client object is {0, 0}; a virtual
// node with no control-backed node above it has scope 0.
struct SemanticElementId final {
    std::uint64_t scope{};
    std::uint64_t node{};
};

[[nodiscard]] bool operator==(SemanticElementId left, SemanticElementId right) noexcept;
[[nodiscard]] bool operator!=(SemanticElementId left, SemanticElementId right) noexcept;

// A node found in a snapshot. The pointers borrow the snapshot passed to
// the function that filled the location and are valid only while it lives
// unchanged. `parent` is null for the client node; `scope` is the nearest
// control-backed node at or above `node`, or null when there is none.
struct SemanticLocation final {
    const SemanticNode* node{};
    const SemanticNode* parent{};
    const SemanticNode* scope{};
    // The node's position among its parent's children; zero for the client.
    std::size_t index{};
    SemanticElementId id{};
};

// True for a node a control publishes about itself (not a virtual child
// and not the client node).
[[nodiscard]] bool control_backed(const SemanticNode& node) noexcept;

// The client node's location in `client`.
[[nodiscard]] SemanticLocation client_location(const SemanticNode& client) noexcept;
SemanticLocation client_location(SemanticNode&&) = delete;

// The location of child `index` of the node at `parent`; `index` must be
// below its child count.
[[nodiscard]] SemanticLocation child_location(const SemanticLocation& parent,
                                              std::size_t index) noexcept;

// Finds the node `id` names in `client`. Returns false, leaving `location`
// unchanged, when no node of the tree has that identity any more.
[[nodiscard]] bool locate_semantic_element(const SemanticNode& client, SemanticElementId id,
                                           SemanticLocation& location) noexcept;
bool locate_semantic_element(SemanticNode&&, SemanticElementId, SemanticLocation&) = delete;

// The location of `location`'s parent. Returns false for the client node.
[[nodiscard]] bool parent_semantic_location(const SemanticNode& client,
                                            const SemanticLocation& location,
                                            SemanticLocation& parent) noexcept;
bool parent_semantic_location(SemanticNode&&, const SemanticLocation&,
                              SemanticLocation&) = delete;

// The deepest visible node at (x, y), in logical window coordinates, at or
// below `at`; later children are above earlier ones. Returns false, leaving
// `hit` unchanged, when no visible node there contains the point.
[[nodiscard]] bool hit_semantic_location(const SemanticLocation& at, double x, double y,
                                         SemanticLocation& hit) noexcept;

// The first node at or below `at`, in tree order, with `state`. Returns
// false, leaving `found` unchanged, when there is none.
[[nodiscard]] bool find_state_location(const SemanticLocation& at, SemanticState state,
                                       SemanticLocation& found) noexcept;

// The control-backed node at or below `at` with stable ID `stable_id` (a
// tab's controlled panel, for one). Returns false, leaving `found`
// unchanged, when there is none.
[[nodiscard]] bool find_control_location(const SemanticLocation& at, std::string_view stable_id,
                                         SemanticLocation& found) noexcept;

// Whether `node` publishes `action`.
[[nodiscard]] bool semantic_has_action(const SemanticNode& node, SemanticAction action) noexcept;

// Whether `role` is checked and unchecked by a press: a check box or a
// checked list's item. (A toggle button is a button that publishes the
// checkable state.)
[[nodiscard]] bool checkable_role(SemanticRole role) noexcept;

// A new BSTR holding `value` as UTF-16, which the caller frees. Empty text
// leaves `*result` null and returns S_FALSE; text that is not valid UTF-8
// returns E_FAIL and memory exhaustion E_OUTOFMEMORY, both with `*result`
// null.
[[nodiscard]] HRESULT utf8_to_bstr(std::string_view value, BSTR* result) noexcept;

// Converts a client's UTF-16 text to UTF-8 in `text`. Returns false, with
// `text` empty, for text that is not valid UTF-16 or longer than INT_MAX
// code units. Throws std::bad_alloc when memory runs out.
[[nodiscard]] bool utf8_from_wide(std::wstring_view value, std::string& text);

// One window's client tree, read-only and shared. The source keeps the
// current tree and hands every reader a reference to it; a tree the source
// replaces lives until its last reader releases it, so the locations a call
// takes into its tree stay valid through that call.
using SemanticTreeRef = std::shared_ptr<const SemanticNode>;

// What the source's current tree was built from. The tree is reused while
// every field reads the same: the window's semantic generation (structure,
// state and text), its layout pass sequence (every change of arranged
// bounds, which the semantic generation does not count; a repaint that
// moves nothing no longer rebuilds the tree, audit N95), the focused,
// pressed and captured controls, activation, the client area and the title.
struct SemanticTreeKey final {
    std::uint64_t semantic_generation{};
    std::uint64_t layout_pass{};
    std::uint64_t focused{};
    std::uint64_t pressed{};
    std::uint64_t captured{};
    bool active{};
    // The client area in device pixels, and device pixels per logical unit.
    LONG client_width{};
    LONG client_height{};
    double scale{};
    std::string title;
    // The window's content rectangle in logical units: the client less a
    // declared outer band (Window::content_rect), which the client node
    // covers.
    Rect content{};
};

[[nodiscard]] bool operator==(const SemanticTreeKey& left, const SemanticTreeKey& right) noexcept;
[[nodiscard]] bool operator!=(const SemanticTreeKey& left, const SemanticTreeKey& right) noexcept;

// The publishers' link to one window. Shared by WindowsAccessibility and
// every native object it hands out; `model` and `hwnd` are cleared when the
// window goes, after which every object resolves to nothing and reports its
// disconnected error. Objects observe the window and never extend its life.
struct SemanticSource final {
    HWND hwnd{};
    Window* model{};
    // Device pixels per logical unit.
    double scale{1.0};
    // The semantic generation last announced to clients.
    std::uint64_t generation{};
    // The runtime ID of the focused node last announced, or zero.
    std::uint64_t focus{};
    // The runtime IDs of the tree last announced to MSAA, sorted: a
    // reorder event is raised only when this set changes (audit N97).
    std::vector<std::uint64_t> announced_ids{};
    // True once a client has asked for an MSAA object; MSAA events are
    // raised only after that.
    bool observed{};
    // True once a client has asked for the UI Automation root; UI
    // Automation events are raised only after that, and only while a
    // client listens.
    bool automation_observed{};
    // The client tree last announced to UI Automation, which the next
    // change is compared against; null until one is announced.
    SemanticTreeRef published{};
    // The tree the last build made and the key it was built under; null
    // until a snapshot succeeds, and released when the window goes.
    SemanticTreeRef current{};
    SemanticTreeKey current_key{};
    // How many trees snapshot built, and how many times it handed out the
    // current one instead (diagnostics; never reset).
    std::uint64_t trees_built{};
    std::uint64_t trees_reused{};
    // The newest text layout the TextPattern read (N109), the tree it was
    // read under and the runtime ID of its control. It is reused while
    // snapshot hands out that same tree for the same control: the tree's key
    // holds the semantic generation, content revision and display
    // generation, so the value and the layout pass are unchanged. A range
    // holds the layout it reads by its own reference, so a newer one
    // replacing it here frees nothing in use. Released with the trees;
    // counted as the trees are.
    SemanticTreeRef text_layout_tree{};
    std::uint64_t text_layout_control{};
    std::shared_ptr<const SemanticTextLayout> text_layout{};
    std::uint64_t text_layouts_built{};
    std::uint64_t text_layouts_reused{};

    // Sets `client` to the window's client node: the snapshot's roots under
    // one node named by the window title and covering the client area. The
    // current tree is reused while its key reads the same; otherwise a new
    // one is built and becomes current. Returns false, with `client` null,
    // when the window has gone or the build failed; the current tree is
    // then kept.
    [[nodiscard]] bool snapshot(SemanticTreeRef& client) noexcept;
    // Releases the current and announced trees (the window is going).
    void release_trees() noexcept;
    // Performs `action` on the located node through the window, named by
    // its scope's runtime ID (Window::perform_semantic_action(RuntimeId,
    // ...)), so a virtual child reaches its own owner and a disposed or
    // replaced control is refused. A virtual node with no scope is named by
    // its stable ID alone. Returns true when the control took it.
    [[nodiscard]] bool perform(const SemanticLocation& location, SemanticAction action,
                               std::string_view value) noexcept;
};

} // namespace gui_forms::host
