// The Gallery's own semantics, read headless from Window::semantic_snapshot.
//
// The Windows MSAA and UI Automation Gallery clients
// (windows_accessibility_msaa_gallery_tests.cpp and
// windows_accessibility_uia_gallery_tests.cpp) run on the user's desktop
// under a machine-wide lock and take the foreground. They check two things:
// what the semantic tree says (semantics GUI.Forms owns) and how the Windows
// adapter maps it (what MSAA and UI Automation return). This test holds the
// first part, with no host, no client and no foreground, so it runs anywhere
// and in parallel:
// - registry coverage: every visual registry control publishes a node of
//   its own on some Gallery page or in the off-tree fixture, except the
//   presentational ones listed below, and that list names none that does;
// - the identities a host names nodes by: no node below the window has
//   runtime ID zero, and each (scope, node) pair is unique on a page;
// - a position in set is published with its set size, within it;
// - a tab's controlled panel, when it is in the tree, is a tab panel node;
// - one default action for every control type and semantic role pair,
//   chosen and judged by each client's rules (the MSAA client's and the UI
//   Automation client's, which differ slightly), performed through
//   Window::perform_semantic_action exactly as the Windows adapter performs
//   it (windows_semantic_source.cpp, SemanticSource::perform), and the change
//   it makes observed in the semantic tree; a reversible one is undone and
//   the undo observed too.
// The adapter's part (roles, states, verbs, patterns, locations, parents,
// runtime IDs, stale elements, and one pattern call per role, action and
// node kind) stays in those two tests.
//
// Execution context: one thread, the windows' UI thread; every window is
// headless (no host session) and owned by the function that made it.

#include "gallery.hpp"
#include "gallery_catalog.hpp"
#include "gallery_page_specs.hpp"
#include "gui_forms/gui_forms.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace gui_forms;

// The page whose buttons open the host's modal dialogs, write the system
// clipboard and play sounds: walked like every page, but no action is
// performed there (both clients skip it).
constexpr std::string_view host_services_page = "dialogs-and-host-services";

// The fixture window holding the registry controls the Gallery does not
// attach to its root tree, walked and acted on like a Gallery page.
constexpr std::string_view off_tree_page = "off-tree-controls";

// Registry entries that are not controls (nonvisual components and the
// LiveSurface service type), as the anatomy coverage test lists them.
constexpr std::array<std::string_view, 24> nonvisual_entries{
    "command", "gallery_command", "font_command", "color_command", "context_menu",
    "tool_tip", "ribbon_tool_tip", "timer", "background_worker", "notify_icon",
    "thread_exception_dialog", "image_list", "error_provider", "help_provider", "semantic_feedback", "binding",
    "binding_source", "binding_context", "live_surface", "dock_workspace", "open_file_dialog",
    "save_file_dialog",
    "color_dialog",
    "folder_browser_dialog",
};

// Registry controls that publish no node of their own: presentational
// containers whose children are published in their place (a scrollable
// control publishes only its scroll bars, as virtual children), the overlay
// layer that only hosts an overlay's content, and RangeControl, the value base
// whose subclasses (track bar, scroll bars) publish their own roles. The
// test fails when this list and the published tree disagree in either
// direction.
// A toast is listed because no page shows a transient toast during the walk.
constexpr std::array<std::string_view, 9> unexposed_entries{
    "scaled_panel", "splitter_panel", "tool_strip_panel", "property_grid",
    "scrollable_control", "anchored_overlay_layer", "visual_inspector_overlay",
    "range_control", "toast",
};

// The most overlays an action's clean-up closes with Escape (a menu inside a
// menu is two).
constexpr int maximum_overlay_closes = 4;

bool listed(std::span<const std::string_view> list, std::string_view id) noexcept {
    for (const std::string_view entry : list) {
        if (entry == id) return true;
    }
    return false;
}

bool has_action(const SemanticNode& node, SemanticAction action) noexcept {
    for (const SemanticAction published : node.actions) {
        if (published == action) return true;
    }
    return false;
}

bool is_button_role(SemanticRole role) noexcept {
    const bool button = role == SemanticRole::button || role == SemanticRole::split_button ||
                        role == SemanticRole::menu_button;
    return button;
}

bool is_check_role(SemanticRole role) noexcept {
    const bool check = role == SemanticRole::check_box || role == SemanticRole::check_list_item;
    return check;
}

// A node a control publishes about itself (not a virtual child).
bool control_backed(const SemanticNode& node) noexcept {
    const bool backed = node.runtime_id != 0U && !is_virtual_semantic_runtime_id(node.runtime_id);
    return backed;
}

// -- the clients' rules ------------------------------------------------------

// Whose rules choose and judge an action: the MSAA client's
// (accDoDefaultAction, or accSelect for focus) or the UI Automation
// client's (one pattern call).
enum class ClientRules : std::uint8_t { msaa, uia };

const char* rules_name(ClientRules rules) noexcept {
    return rules == ClientRules::msaa ? "MSAA" : "UI Automation";
}

// What a node's default action is: the first of press, show menu, select,
// then expand or collapse by the node's expanded state; none leaves focus.
enum class DefaultKind : std::uint8_t { none, press, show_menu, select, expand, collapse };

DefaultKind expected_default(const SemanticNode& node) noexcept {
    if (has_action(node, SemanticAction::press)) return DefaultKind::press;
    if (has_action(node, SemanticAction::show_menu)) return DefaultKind::show_menu;
    if (has_action(node, SemanticAction::select)) return DefaultKind::select;
    const bool expanded = has_semantic_state(node.states, SemanticState::expanded);
    if (expanded && has_action(node, SemanticAction::collapse)) return DefaultKind::collapse;
    if (!expanded && has_action(node, SemanticAction::expand)) return DefaultKind::expand;
    return DefaultKind::none;
}

// A toggle as the UI Automation client expects one: a check role, or a
// node that can be checked and is pressed.
bool expects_toggle(const SemanticNode& node) noexcept {
    if (is_check_role(node.role)) return true;
    if (node.role == SemanticRole::radio_button) return false;
    const bool can_check = has_semantic_state(node.states, SemanticState::checkable) ||
                           has_semantic_state(node.states, SemanticState::checked) ||
                           has_semantic_state(node.states, SemanticState::mixed);
    const bool toggles = can_check && has_action(node, SemanticAction::press);
    return toggles;
}

bool expected_selected(const SemanticNode& node) noexcept {
    if (node.role == SemanticRole::radio_button) {
        return has_semantic_state(node.states, SemanticState::checked);
    }
    return has_semantic_state(node.states, SemanticState::selected);
}

// The semantic action the Windows adapter performs for a node's default
// action, by the client's route. MSAA: windows_accessibility.cpp's default
// action (and accSelect's focus). UI Automation: the pattern method the
// client calls (windows_automation.cpp): Toggle and Invoke press, a radio
// button's Select presses, Expand expands or shows the menu. An empty
// result is a pattern call the adapter answers without an action (Expand
// of an expanded node, Collapse of a collapsed one).
std::optional<SemanticAction> adapter_action(const SemanticNode& node, DefaultKind kind, ClientRules rules) {
    const bool expanded = has_semantic_state(node.states, SemanticState::expanded);
    switch (kind) {
    case DefaultKind::none: return SemanticAction::focus;
    case DefaultKind::press: return SemanticAction::press;
    case DefaultKind::select:
        if (rules == ClientRules::uia && node.role == SemanticRole::radio_button) return SemanticAction::press;
        return SemanticAction::select;
    case DefaultKind::show_menu:
        if (rules == ClientRules::msaa) return SemanticAction::show_menu;
        if (expanded) return std::nullopt;
        return has_action(node, SemanticAction::expand) ? SemanticAction::expand : SemanticAction::show_menu;
    case DefaultKind::expand:
        if (rules == ClientRules::msaa) return SemanticAction::expand;
        if (expanded) return std::nullopt;
        return has_action(node, SemanticAction::expand) ? SemanticAction::expand : SemanticAction::show_menu;
    case DefaultKind::collapse:
        if (rules == ClientRules::msaa || expanded) return SemanticAction::collapse;
        return std::nullopt;
    }
    return std::nullopt;
}

// -- the walk ----------------------------------------------------------------

// A control that publishes virtual children, with the stable IDs of all of
// them and their virtual descendants, so a virtual node's control type can
// be named.
struct VirtualOwner final {
    Control::Ptr control;
    std::string control_type;
    std::vector<std::string> stable_ids;
};

void collect_virtual_ids(const std::vector<SemanticNode>& nodes, std::vector<std::string>& ids) {
    for (const SemanticNode& node : nodes) {
        ids.push_back(node.stable_id);
        collect_virtual_ids(node.children, ids);
    }
}

void collect_virtual_owners(const Control::Ptr& control, std::vector<VirtualOwner>& owners) {
    if (!(*control).effectively_visible()) return;
    const std::vector<SemanticNode> published = (*control).semantic_virtual_children();
    if (!published.empty()) {
        VirtualOwner owner;
        owner.control = control;
        owner.control_type = std::string((*control).anatomy().control_id);
        collect_virtual_ids(published, owner.stable_ids);
        owners.push_back(std::move(owner));
    }
    const std::span<const Control::Ptr> children = (*control).children();
    for (std::size_t index = 0; index < children.size(); ++index) {
        collect_virtual_owners(children[index], owners);
    }
}

bool contains_text(const std::vector<std::string>& texts, std::string_view wanted) noexcept {
    for (const std::string& text : texts) {
        if (text == wanted) return true;
    }
    return false;
}

// One node chosen to have its default action performed: the first enabled
// node of its control type and role on a page other than the host-services
// page, preferring one whose change can be seen.
struct ActionCandidate final {
    std::string page;
    // The stable ID of the nearest control-backed node at or above the
    // node, which with the node's stable ID names it: a virtual child's
    // stable ID is unique only within its owner.
    std::string scope_id;
    std::string stable_id;
    std::string control_type;
    SemanticRole role{SemanticRole::generic};
    bool observable{};
};

struct Failure final {
    std::string page;
    std::string stable_id;
    std::string what;
};

struct Report final {
    // Per registry control type, how many nodes it published of its own.
    std::map<std::string, std::size_t> own_nodes;
    std::map<std::string, ActionCandidate> msaa_candidates;
    std::map<std::string, ActionCandidate> uia_candidates;
    std::vector<Failure> failures;
    std::size_t pages{};
    std::size_t nodes{};
    std::size_t positioned{};
    std::size_t controller_relations{};
    std::size_t actions{};
};

void fail(Report& report, std::string_view page, std::string_view stable_id, std::string what) {
    Failure failure;
    failure.page = std::string(page);
    failure.stable_id = std::string(stable_id);
    failure.what = std::move(what);
    report.failures.push_back(std::move(failure));
}

struct PageWalk final {
    Window& window;
    Report& report;
    std::string_view page;
    std::vector<VirtualOwner> owners;
    // (runtime ID of the scope node, runtime ID of the node): what a host
    // names a node by.
    std::set<std::pair<std::uint64_t, std::uint64_t>> identities;
    // Stable IDs of the control-backed nodes on the page, and every node's
    // role by its stable ID, for the tab-to-panel relation.
    std::map<std::string, SemanticRole> control_roles;
    std::vector<const SemanticNode*> tabs;
};

std::string node_control_type(const PageWalk& walk, const SemanticNode& node, std::string_view scope_id) {
    if (control_backed(node)) {
        const Control::Ptr control = walk.window.find(node.stable_id);
        if (!control) return std::string("(control not found)");
        const std::string type((*control).anatomy().control_id);
        return type;
    }
    Control::Ptr scope;
    if (!scope_id.empty()) scope = walk.window.find(scope_id);
    for (const VirtualOwner& owner : walk.owners) {
        const bool within = !scope || owner.control == scope || (*scope).contains(*owner.control);
        if (within && contains_text(owner.stable_ids, node.stable_id)) return owner.control_type;
    }
    return std::string("(owner not found)");
}

// The MSAA client's choice: focus is the default for a focusable node and
// for one that publishes the focus action without being focusable (a
// composite whose own handler moves focus to an inner part). The UI
// Automation client's: focusable nodes only.
void consider_candidate(PageWalk& walk, const SemanticNode& node, const std::string& type,
                        std::string_view scope_id, ClientRules rules) {
    if (walk.page == host_services_page) return;
    if (!has_semantic_state(node.states, SemanticState::enabled)) return;
    const DefaultKind kind = expected_default(node);
    bool focus_only = kind == DefaultKind::none && has_semantic_state(node.states, SemanticState::focusable);
    if (rules == ClientRules::msaa && kind == DefaultKind::none && has_action(node, SemanticAction::focus)) {
        focus_only = true;
    }
    if (kind == DefaultKind::none && !focus_only) return;
    bool observable = true;
    if (kind == DefaultKind::select) observable = !has_semantic_state(node.states, SemanticState::selected);
    if (kind == DefaultKind::press && node.role == SemanticRole::radio_button) {
        observable = !has_semantic_state(node.states, SemanticState::checked);
    }
    if (kind == DefaultKind::none) observable = !has_semantic_state(node.states, SemanticState::focused);
    std::map<std::string, ActionCandidate>& candidates =
        rules == ClientRules::msaa ? walk.report.msaa_candidates : walk.report.uia_candidates;
    const std::string key = type + " / " + semantic_role_name(node.role);
    const std::map<std::string, ActionCandidate>::iterator existing = candidates.find(key);
    if (existing != candidates.end()) {
        if ((*existing).second.observable || !observable) return;
    }
    ActionCandidate candidate;
    candidate.page = std::string(walk.page);
    candidate.scope_id = std::string(scope_id);
    candidate.stable_id = node.stable_id;
    candidate.control_type = type;
    candidate.role = node.role;
    candidate.observable = observable;
    candidates[key] = std::move(candidate);
}

// Checks one node and descends. `scope` is the nearest control-backed node
// above `node`, or null; `scope_id` its stable ID, or empty.
void walk_node(PageWalk& walk, const SemanticNode& node, const SemanticNode* scope, std::string_view scope_id) {
    ++walk.report.nodes;
    const SemanticNode* own_scope = scope;
    std::string_view own_scope_id = scope_id;
    if (control_backed(node)) {
        own_scope = &node;
        own_scope_id = node.stable_id;
    }
    const std::string type = node_control_type(walk, node, scope_id);
    if (control_backed(node)) {
        ++walk.report.own_nodes[type];
        walk.control_roles[node.stable_id] = node.role;
    }
    // The identity a host names the node by. Hosts number nodes from their
    // runtime IDs (UI Automation's runtime ID, MSAA's child path), so none
    // is zero and none repeats within its scope.
    if (node.runtime_id == 0U) {
        fail(walk.report, walk.page, node.stable_id, "a node below the window has runtime ID zero");
    }
    const std::uint64_t scope_runtime = own_scope == &node || own_scope == nullptr ? 0U : (*own_scope).runtime_id;
    if (!walk.identities.insert(std::make_pair(scope_runtime, node.runtime_id)).second) {
        fail(walk.report, walk.page, node.stable_id, "two nodes share one runtime ID in one scope");
    }
    // A position is published with its set size or not at all (RB-22).
    if (node.position_in_set.has_value() != node.set_size.has_value()) {
        fail(walk.report, walk.page, node.stable_id, "a position in set without a set size, or the reverse");
    } else if (node.position_in_set.has_value()) {
        ++walk.report.positioned;
        if (*node.position_in_set < 1U || *node.position_in_set > *node.set_size) {
            fail(walk.report, walk.page, node.stable_id,
                 "position " + std::to_string(*node.position_in_set) + " outside a set of " +
                     std::to_string(*node.set_size));
        }
    }
    if (node.role == SemanticRole::tab) walk.tabs.push_back(&node);
    consider_candidate(walk, node, type, own_scope_id, ClientRules::msaa);
    consider_candidate(walk, node, type, own_scope_id, ClientRules::uia);
    for (const SemanticNode& child : node.children) walk_node(walk, child, own_scope, own_scope_id);
}

bool show_page(Window& window, std::string_view page) {
    if (page == off_tree_page) return true;
    const bool selected = gallery::select_gallery_page(window, page);
    return selected;
}

void walk_page(Window& window, Report& report, std::string_view page) {
    if (!show_page(window, page)) throw std::runtime_error("a Gallery page cannot be selected: " + std::string(page));
    window.perform_layout();
    const SemanticSnapshot snapshot = window.semantic_snapshot();
    PageWalk walk{window, report, page, {}, {}, {}, {}};
    collect_virtual_owners(window.root(), walk.owners);
    for (const SemanticNode& root : snapshot.roots) walk_node(walk, root, nullptr, std::string_view());
    // A tab's controlled panel, when it is in the tree, is a tab panel (the
    // UI Automation client reads it as the tab's ControllerFor).
    for (const SemanticNode* tab : walk.tabs) {
        const std::string& panel = (*tab).controlled_panel;
        if (panel.empty()) continue;
        const std::map<std::string, SemanticRole>::const_iterator found = walk.control_roles.find(panel);
        if (found == walk.control_roles.end()) continue;
        ++report.controller_relations;
        if ((*found).second != SemanticRole::tab_panel) {
            fail(report, page, (*tab).stable_id,
                 "the controlled panel " + panel + " is a " + semantic_role_name((*found).second));
        }
    }
    ++report.pages;
}

void place(Panel& root, const Control::Ptr& control, Rect bounds) {
    (*control).set_requested_bounds(bounds);
    root.add_child(control);
}

// The off-tree fixture, as both Windows clients build it: each registry
// control the Gallery does not attach to its root tree, in a cell of its
// own.
std::unique_ptr<Window> make_off_tree_fixture() {
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("off-tree"));
    const std::shared_ptr<ButtonBase> button_base =
        make_control<ButtonBase>(StableId("off-tree.button-base"), "Base command");
    place(*root, button_base, Rect{10.0, 10.0, 160.0, 30.0});
    place(*root, make_control<RangeControl>(StableId("off-tree.range-control")), Rect{10.0, 50.0, 160.0, 30.0});
    place(*root, make_control<ScrollBar>(StableId("off-tree.scroll-bar")), Rect{10.0, 90.0, 160.0, 18.0});
    const std::shared_ptr<ScaledPanel> scaled =
        make_control<ScaledPanel>(StableId("off-tree.scaled-panel"), Size{200.0, 100.0});
    place(*root, scaled, Rect{200.0, 10.0, 200.0, 100.0});
    const std::shared_ptr<Label> scaled_label =
        make_control<Label>(StableId("off-tree.scaled-panel.label"), "Scaled content");
    (*scaled_label).set_requested_bounds(Rect{4.0, 4.0, 150.0, 24.0});
    (*scaled).add_child(scaled_label);
    place(*root, make_control<SplitterPanel>(StableId("off-tree.splitter-panel")), Rect{420.0, 10.0, 160.0, 100.0});
    place(*root, make_control<ToolStripPanel>(StableId("off-tree.tool-strip-panel")),
          Rect{600.0, 10.0, 300.0, 40.0});
    place(*root, make_control<PropertyGrid>(StableId("off-tree.property-grid")), Rect{10.0, 130.0, 300.0, 240.0});
    place(*root, make_control<TaskDialog>(StableId("off-tree.task-dialog")), Rect{330.0, 130.0, 420.0, 240.0});
    place(*root, make_control<FontDialog>(StableId("off-tree.font-dialog")), Rect{10.0, 390.0, 640.0, 400.0});
    const Size size{1280.0, 820.0};
    std::unique_ptr<Window> window = std::make_unique<Window>(root, size);
    return window;
}

std::unique_ptr<Window> make_window(std::string_view page) {
    if (page == off_tree_page) return make_off_tree_fixture();
    std::unique_ptr<Window> window = gallery::make_gallery();
    return window;
}

// Every visual registry control publishes a node of its own, except those
// unexposed_entries lists, which publish none. Returns the disagreement, or
// an empty string.
std::string registry_coverage(const Report& report) {
    std::size_t controls = 0;
    std::size_t exposed = 0;
    std::string missing;
    std::string stale;
    for (const gallery::CatalogEntry& entry : gallery::control_catalog) {
        if (listed(nonvisual_entries, entry.id)) continue;
        ++controls;
        const std::map<std::string, std::size_t>::const_iterator found = report.own_nodes.find(std::string(entry.id));
        const bool own_node = found != report.own_nodes.end() && (*found).second != 0U;
        if (own_node) ++exposed;
        const bool unexposed = listed(unexposed_entries, entry.id);
        if (!own_node && !unexposed) missing += " " + std::string(entry.id);
        if (own_node && unexposed) stale += " " + std::string(entry.id);
    }
    std::cout << "registry coverage: " << exposed << " of " << controls
              << " visual registry controls publish their own node; " << unexposed_entries.size()
              << " are presentational (" << nonvisual_entries.size() << " nonvisual entries excluded)\n";
    std::string disagreement;
    if (!missing.empty()) disagreement += "registry controls that publish no node:" + missing;
    if (!stale.empty()) disagreement += " unexposed_entries lists exposed controls:" + stale;
    return disagreement;
}

// -- the actions -------------------------------------------------------------

enum class ActionOutcome : std::uint8_t { changed, refused, unobserved, lost };

const char* outcome_name(ActionOutcome outcome) noexcept {
    switch (outcome) {
    case ActionOutcome::changed: return "changed";
    case ActionOutcome::refused: return "refused";
    case ActionOutcome::unobserved: return "no observable change";
    case ActionOutcome::lost: return "node lost";
    }
    return "unknown";
}

struct ActionResult final {
    std::string key;
    std::string evidence;
    ActionOutcome outcome{ActionOutcome::unobserved};
};

// The node a candidate names in `nodes` and its scope: the nearest
// control-backed node at or above it, or null.
struct NamedNode final {
    const SemanticNode* node{};
    const SemanticNode* scope{};
};

NamedNode find_named(const std::vector<SemanticNode>& nodes, std::string_view scope_id, std::string_view stable_id,
                     const SemanticNode* current_scope) {
    for (const SemanticNode& node : nodes) {
        const SemanticNode* node_scope = current_scope;
        if (control_backed(node)) node_scope = &node;
        const std::string_view node_scope_id =
            node_scope == nullptr ? std::string_view() : std::string_view((*node_scope).stable_id);
        if (node.stable_id == stable_id && node_scope_id == scope_id) {
            const NamedNode named{&node, node_scope};
            return named;
        }
        const NamedNode found = find_named(node.children, scope_id, stable_id, node_scope);
        if (found.node != nullptr) return found;
    }
    return NamedNode{};
}
NamedNode find_named(std::vector<SemanticNode>&&, std::string_view, std::string_view, const SemanticNode*) = delete;

// Performs `action` on a named node as the Windows adapter does
// (SemanticSource::perform): a node with no control-backed node above it by
// its stable ID, otherwise through its scope's runtime ID and, for a
// virtual child, the child's stable ID.
bool perform_as_adapter(Window& window, const NamedNode& named, SemanticAction action) {
    const SemanticNode& node = *named.node;
    if (named.scope == nullptr) {
        const bool handled = window.perform_semantic_action(node.stable_id, action);
        return handled;
    }
    const RuntimeId scope{(*named.scope).runtime_id};
    std::string_view virtual_child;
    if (named.scope != named.node) virtual_child = node.stable_id;
    const bool handled = window.perform_semantic_action(scope, virtual_child, action);
    return handled;
}

void press_escape(Window& window) {
    KeyEvent down;
    down.action = KeyAction::down;
    down.physical_key = PhysicalKey::escape;
    static_cast<void>(window.dispatch_key(down));
    KeyEvent up;
    up.action = KeyAction::up;
    up.physical_key = PhysicalKey::escape;
    static_cast<void>(window.dispatch_key(up));
}

// Closes overlays an action opened, so the page's root set is restored.
void close_overlays(Window& window, std::size_t baseline_roots) {
    for (int attempt = 0; attempt < maximum_overlay_closes; ++attempt) {
        const SemanticSnapshot snapshot = window.semantic_snapshot();
        const bool open = snapshot.roots.size() > baseline_roots || window.focus_scope_depth() != 0U;
        if (!open) return;
        press_escape(window);
    }
}

struct ClickCounter final {
    int count{};
    void clicked(ButtonBase&) { ++count; }
};

struct InvocationCounter final {
    int count{};
    void invoked(const CommandInvocation&) { ++count; }
};

// True when an action opened an overlay: the snapshot gained roots or nodes,
// or a focus scope began.
bool overlay_opened(const SemanticSnapshot& before, const SemanticSnapshot& after, std::size_t scopes_before,
                  std::size_t scopes_after) noexcept {
    const bool opened = after.roots.size() > before.roots.size() || after.node_count > before.node_count ||
                        scopes_after > scopes_before;
    return opened;
}

// Undoes a reversible action as the client does and reports whether the
// node's expanded and checked states are back to `before`'s. MSAA performs
// the node's default action again (as it reads now); UI Automation calls
// the reverse pattern method (Toggle again, or the opposite expansion).
bool undo_action(Window& window, const ActionCandidate& candidate, const SemanticNode& before,
                 DefaultKind kind, ClientRules rules) {
    const SemanticSnapshot current = window.semantic_snapshot();
    const NamedNode named = find_named(current.roots, candidate.scope_id, candidate.stable_id, nullptr);
    if (named.node == nullptr) return false;
    DefaultKind undo = kind;
    if (rules == ClientRules::msaa) {
        undo = expected_default(*named.node);
    } else if (kind == DefaultKind::expand || kind == DefaultKind::show_menu) {
        undo = DefaultKind::collapse;
    } else if (kind == DefaultKind::collapse) {
        undo = DefaultKind::expand;
    }
    const std::optional<SemanticAction> action = adapter_action(*named.node, undo, rules);
    if (action && !perform_as_adapter(window, named, *action)) return false;
    const SemanticSnapshot restored = window.semantic_snapshot();
    const NamedNode back = find_named(restored.roots, candidate.scope_id, candidate.stable_id, nullptr);
    if (back.node == nullptr) return false;
    const bool expanded = has_semantic_state((*back.node).states, SemanticState::expanded) ==
                          has_semantic_state(before.states, SemanticState::expanded);
    const bool checked = has_semantic_state((*back.node).states, SemanticState::checked) ==
                         has_semantic_state(before.states, SemanticState::checked);
    const bool undone = expanded && checked;
    return undone;
}

// Judges a performed action from the tree before and after it, by the
// client's rules (the two Windows clients' judges, which differ for a radio
// button's press, a composite's focus and which presses are reversed).
ActionResult judge(Window& window, const ActionCandidate& candidate, const SemanticNode& node,
                   const SemanticSnapshot& before, std::size_t scopes_before, std::uint64_t generation_before,
                   const ClickCounter& clicks, const InvocationCounter& invocations, ClientRules rules,
                   bool& reversible) {
    ActionResult result;
    const DefaultKind kind = expected_default(node);
    const SemanticSnapshot after = window.semantic_snapshot();
    const std::size_t scopes_after = window.focus_scope_depth();
    const NamedNode named = find_named(after.roots, candidate.scope_id, candidate.stable_id, nullptr);
    const SemanticNode* changed = named.node;
    const bool checked_before = has_semantic_state(node.states, SemanticState::checked);
    const bool mixed_before = has_semantic_state(node.states, SemanticState::mixed);
    const bool expanded_before = has_semantic_state(node.states, SemanticState::expanded);
    const bool opened = overlay_opened(before, after, scopes_before, scopes_after);
    const bool radio_press = rules == ClientRules::uia && kind == DefaultKind::press &&
                             node.role == SemanticRole::radio_button;
    result.outcome = ActionOutcome::unobserved;
    if (kind == DefaultKind::none) {
        const Control::Ptr control = window.find(node.stable_id);
        const Control::Ptr focused = window.focused_control();
        if (changed != nullptr && has_semantic_state((*changed).states, SemanticState::focused)) {
            result.outcome = ActionOutcome::changed;
            result.evidence = "focused";
        } else if (rules == ClientRules::msaa && control && focused && focused != control &&
                   (*control).contains(*focused)) {
            // A composite that is not focusable moved focus to an inner
            // control the semantic tree does not publish.
            result.outcome = ActionOutcome::changed;
            result.evidence = "focus moved inside, to " + std::string((*focused).stable_id().value());
        }
    } else if (kind == DefaultKind::select || radio_press) {
        const bool selected = rules == ClientRules::uia
                                  ? changed != nullptr && expected_selected(*changed)
                                  : changed != nullptr && has_semantic_state((*changed).states, SemanticState::selected);
        if (selected) {
            result.outcome = ActionOutcome::changed;
            result.evidence = node.role == SemanticRole::radio_button ? "checked" : "selected";
        }
    } else if (kind == DefaultKind::expand || kind == DefaultKind::collapse) {
        if (changed != nullptr && has_semantic_state((*changed).states, SemanticState::expanded) != expanded_before) {
            result.outcome = ActionOutcome::changed;
            result.evidence = expanded_before ? "collapsed" : "expanded";
        } else if (kind == DefaultKind::expand && opened) {
            result.outcome = ActionOutcome::changed;
            result.evidence = "an overlay opened";
        }
    } else if (kind == DefaultKind::show_menu) {
        if (opened) {
            result.outcome = ActionOutcome::changed;
            result.evidence = "an overlay opened";
        } else if (changed != nullptr && has_semantic_state((*changed).states, SemanticState::expanded)) {
            result.outcome = ActionOutcome::changed;
            result.evidence = "expanded";
        }
    } else if (changed != nullptr &&
               (has_semantic_state((*changed).states, SemanticState::checked) != checked_before ||
                has_semantic_state((*changed).states, SemanticState::mixed) != mixed_before)) {
        result.outcome = ActionOutcome::changed;
        result.evidence = has_semantic_state((*changed).states, SemanticState::checked) ? "checked" : "unchecked";
    } else if (clicks.count == 1) {
        result.outcome = ActionOutcome::changed;
        result.evidence = "clicked raised once";
    } else if (invocations.count == 1) {
        result.outcome = ActionOutcome::changed;
        result.evidence = "its command invoked once";
    } else if (opened) {
        result.outcome = ActionOutcome::changed;
        result.evidence = "an overlay opened";
    } else if (window.semantic_generation() != generation_before) {
        result.outcome = ActionOutcome::changed;
        result.evidence = "the semantic tree changed";
    }
    if (clicks.count > 1) {
        result.outcome = ActionOutcome::unobserved;
        result.evidence = "clicked raised " + std::to_string(clicks.count) + " times";
    }
    const bool reversed_press = rules == ClientRules::msaa ? is_check_role(node.role) : expects_toggle(node);
    reversible = result.outcome == ActionOutcome::changed &&
                 (kind == DefaultKind::expand || kind == DefaultKind::collapse ||
                  (kind == DefaultKind::press && reversed_press)) &&
                 changed != nullptr && result.evidence != "an overlay opened";
    return result;
}

// Performs one candidate's default action in `window`, whose page is shown,
// and judges it.
ActionResult perform_candidate(Window& window, const std::string& key, const ActionCandidate& candidate,
                               ClientRules rules) {
    const SemanticSnapshot before = window.semantic_snapshot();
    const std::size_t baseline_roots = before.roots.size();
    const NamedNode named = find_named(before.roots, candidate.scope_id, candidate.stable_id, nullptr);
    ActionResult result;
    result.key = key;
    if (named.node == nullptr) {
        result.outcome = ActionOutcome::lost;
        result.evidence = "the node is not in the tree when its page is shown again";
        return result;
    }
    const SemanticNode node = *named.node;
    const DefaultKind kind = expected_default(node);
    ClickCounter clicks;
    InvocationCounter invocations;
    SubscriptionToken click_subscription;
    SubscriptionToken invocation_subscription;
    const Control::Ptr control = window.find(node.stable_id);
    const std::shared_ptr<ButtonBase> button = std::dynamic_pointer_cast<ButtonBase>(control);
    if (button) {
        click_subscription =
            (*button).clicked().subscribe(Delegate<ButtonBase&>::bind<ClickCounter, &ClickCounter::clicked>(clicks));
    }
    // A colour picker's press commits its colour again through its command,
    // which changes no state when the colour is the same.
    const std::shared_ptr<RibbonColorPicker> picker = std::dynamic_pointer_cast<RibbonColorPicker>(control);
    if (picker && (*picker).command()) {
        const std::shared_ptr<Command>& command = (*(*picker).command()).command();
        invocation_subscription = (*command).invoked().subscribe(
            Delegate<const CommandInvocation&>::bind<InvocationCounter, &InvocationCounter::invoked>(invocations));
    }
    const std::uint64_t generation_before = window.semantic_generation();
    const std::size_t scopes_before = window.focus_scope_depth();
    const std::optional<SemanticAction> action = adapter_action(node, kind, rules);
    if (action && !perform_as_adapter(window, named, *action)) {
        result.outcome = ActionOutcome::refused;
        result.evidence = std::string("Window::perform_semantic_action refused ") + semantic_action_name(*action);
        close_overlays(window, baseline_roots);
        return result;
    }
    bool reversible = false;
    const ActionResult judged = judge(window, candidate, node, before, scopes_before, generation_before, clicks,
                                      invocations, rules, reversible);
    result.outcome = judged.outcome;
    result.evidence = judged.evidence;
    click_subscription = SubscriptionToken{};
    invocation_subscription = SubscriptionToken{};
    if (reversible) {
        const bool undone = undo_action(window, candidate, node, kind, rules);
        result.evidence += undone ? ", then restored" : ", but not restored";
        if (!undone) result.outcome = ActionOutcome::unobserved;
    }
    close_overlays(window, baseline_roots);
    return result;
}

// One action to perform: a candidate and its key in its candidate map.
struct ActionJob final {
    const std::string* key;
    const ActionCandidate* candidate;
};

// Performs each job in turn in one fresh window, appending one result per
// job.
void run_jobs(std::string_view page, const std::vector<ActionJob>& jobs, ClientRules rules,
              std::vector<ActionResult>& results) {
    std::unique_ptr<Window> window = make_window(page);
    for (const ActionJob& job : jobs) {
        if (!show_page(*window, page)) throw std::runtime_error("a Gallery page cannot be selected: " + std::string(page));
        (*window).perform_layout();
        results.push_back(perform_candidate(*window, *job.key, *job.candidate, rules));
    }
}

// Performs every candidate of `page` in one fresh window. An action whose
// change was not seen may have met an earlier action's effects; it runs
// again alone in a fresh window, and that result stands (both clients'
// rule).
void perform_page_actions(Report& report, const std::map<std::string, ActionCandidate>& candidates,
                          std::string_view page, ClientRules rules) {
    std::vector<ActionJob> jobs;
    for (const std::pair<const std::string, ActionCandidate>& entry : candidates) {
        if (entry.second.page != page) continue;
        jobs.push_back(ActionJob{&entry.first, &entry.second});
    }
    if (jobs.empty()) return;
    std::vector<ActionResult> results;
    run_jobs(page, jobs, rules, results);
    for (std::size_t index = 0; index < jobs.size(); ++index) {
        ++report.actions;
        if (results[index].outcome == ActionOutcome::changed) continue;
        const std::vector<ActionJob> alone{jobs[index]};
        std::vector<ActionResult> isolated;
        run_jobs(page, alone, rules, isolated);
        if (isolated[0].outcome == ActionOutcome::changed) continue;
        const ActionCandidate& candidate = *jobs[index].candidate;
        std::string what = std::string(rules_name(rules)) + " default action of " + *jobs[index].key + ": " +
                           outcome_name(isolated[0].outcome);
        if (!isolated[0].evidence.empty()) what += " (" + isolated[0].evidence + ")";
        what += ", alone in a fresh window, after \"" + std::string(outcome_name(results[index].outcome));
        if (!results[index].evidence.empty()) what += ", " + results[index].evidence;
        what += "\" in the page's window";
        fail(report, page, candidate.stable_id, std::move(what));
    }
}

void perform_all_actions(Report& report, const std::map<std::string, ActionCandidate>& candidates,
                         ClientRules rules) {
    for (const gallery::GalleryPageSpec& page : gallery::gallery_pages) {
        perform_page_actions(report, candidates, page.id, rules);
    }
    perform_page_actions(report, candidates, off_tree_page, rules);
}

} // namespace

int main() {
    try {
        Report report;
        {
            std::unique_ptr<Window> gallery_window = gallery::make_gallery();
            for (const gallery::GalleryPageSpec& page : gallery::gallery_pages) {
                walk_page(*gallery_window, report, page.id);
            }
        }
        {
            std::unique_ptr<Window> off_tree = make_off_tree_fixture();
            walk_page(*off_tree, report, off_tree_page);
        }
        const std::string coverage = registry_coverage(report);
        perform_all_actions(report, report.msaa_candidates, ClientRules::msaa);
        perform_all_actions(report, report.uia_candidates, ClientRules::uia);
        std::cout << "semantic Gallery: " << report.pages << " pages, " << report.nodes << " nodes, "
                  << report.positioned << " positioned, " << report.controller_relations
                  << " tab-to-panel relations; default actions " << report.msaa_candidates.size()
                  << " (MSAA rules) + " << report.uia_candidates.size() << " (UI Automation rules) = "
                  << report.actions << " performed\n";
        if (!coverage.empty()) fail(report, "", "", coverage);
        if (!report.failures.empty()) {
            for (const Failure& failure : report.failures) {
                std::cerr << "gui_forms_semantic_gallery_tests: " << failure.page << " " << failure.stable_id << ": "
                          << failure.what << '\n';
            }
            std::cerr << report.failures.size() << " failures\n";
            return 1;
        }
        std::cout << "gui_forms_semantic_gallery_tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_semantic_gallery_tests: " << error.what() << '\n';
        return 1;
    }
}
