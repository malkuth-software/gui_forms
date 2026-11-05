// The MSAA client test over the whole Gallery (stage A, step A-W1).
//
// Runs the Gallery in a real Win32 window and reads it as an MSAA client
// does: AccessibleObjectFromWindow, then get_accChild down the tree. On every
// page it compares each accessible object with the semantic node it
// publishes: role, name, value, description, state, default action,
// location, child count and parent. A second window holds the registry
// controls the Gallery does not attach to its tree. Then it performs one
// default action for every semantic role, default action kind and
// placement (a control's own node, an item of a control, an item with no
// control above it) through accDoDefaultAction (or, for a node whose only
// action is focus, accSelect with SELFLAG_TAKEFOCUS) and observes the
// resulting change in the semantic tree, in the window that compared the
// pages (one whose change is not seen runs again alone in a fresh window);
// the adapter's path depends only on those three. Which node every registry
// control publishes and each control type's default action are checked
// headless by semantic_gallery_tests.cpp.
//
// The expected MSAA vocabulary (roles, state bits and default-action verbs)
// is written out here, independently of the provider, as a client expects
// it. Every mismatch is printed; the test fails on any mismatch, on any
// refused action and on any action whose effect it cannot observe.
//
// Execution context: everything runs on the UI thread inside the window's
// ready callback. In-process MSAA calls from the window's own thread reach
// the provider directly, so the semantic tree is read and changed on its one
// owner thread.

#include "gallery.hpp"
#include "gallery_page_specs.hpp"
#include "gui_forms/gui_forms.hpp"
#include "gui_forms/platform/windows_host.hpp"
#include "support/windows_own_window.hpp"

#include <windows.h>
#include <oleacc.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace gui_forms;

// The window title, followed by the process ID (test_support::process_title).
constexpr std::string_view fixture_title_utf8 = "GUI.Forms MSAA Gallery client";

// The page whose buttons open the host's modal dialogs, write the system
// clipboard and play sounds. Its controls are enumerated and compared like
// every other page's, but no action is performed there: a modal dialog
// would block this single-threaded client, and the clipboard is the
// machine's.
constexpr std::string_view host_services_page = "dialogs-and-host-services";

// The page name of the fixture window holding the registry controls the
// Gallery does not attach to its root tree: base types reached otherwise
// only through subclasses, presentational containers and dialogs that
// open on demand. It is enumerated and acted on like a Gallery page.
constexpr std::string_view off_tree_page = "off-tree-controls";

// The most popups the client closes with Escape after an action that opened
// some (a menu inside a menu is two).
constexpr int maximum_popup_closes = 4;

// One owned IAccessible reference. Move-only; releases on destruction.
class AccessibleReference final {
public:
    AccessibleReference() noexcept = default;
    ~AccessibleReference() { reset(); }
    AccessibleReference(AccessibleReference&& other) noexcept : value_(other.value_) {
        other.value_ = nullptr;
    }
    AccessibleReference& operator=(AccessibleReference&& other) noexcept {
        if (this != &other) {
            reset();
            value_ = other.value_;
            other.value_ = nullptr;
        }
        return *this;
    }
    AccessibleReference(const AccessibleReference&) = delete;
    AccessibleReference& operator=(const AccessibleReference&) = delete;

    [[nodiscard]] IAccessible* get() const noexcept { return value_; }
    // The address an out-parameter call fills; any held reference is
    // released first.
    [[nodiscard]] IAccessible** receive() noexcept {
        reset();
        return &value_;
    }
    void reset() noexcept {
        if (value_ != nullptr) {
            (*value_).Release();
            value_ = nullptr;
        }
    }

private:
    IAccessible* value_{};
};

// One owned BSTR. Move is not needed; the owner lives in one scope.
class BstrOwner final {
public:
    BstrOwner() noexcept = default;
    ~BstrOwner() { SysFreeString(value_); }
    BstrOwner(const BstrOwner&) = delete;
    BstrOwner& operator=(const BstrOwner&) = delete;

    [[nodiscard]] BSTR* receive() noexcept {
        SysFreeString(value_);
        value_ = nullptr;
        return &value_;
    }
    [[nodiscard]] bool empty() const noexcept { return value_ == nullptr; }
    [[nodiscard]] std::string utf8() const;

private:
    BSTR value_{};
};

std::string BstrOwner::utf8() const {
    if (value_ == nullptr) return {};
    const UINT length = SysStringLen(value_);
    if (length == 0U) return {};
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, value_, static_cast<int>(length),
                                          nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) throw std::runtime_error("an MSAA string does not convert to UTF-8");
    std::string text(static_cast<std::size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value_, static_cast<int>(length), text.data(), bytes,
                        nullptr, nullptr);
    return text;
}

VARIANT self_child() noexcept {
    VARIANT child;
    VariantInit(&child);
    child.vt = VT_I4;
    child.lVal = CHILDID_SELF;
    return child;
}

VARIANT indexed_child(LONG index) noexcept {
    VARIANT child;
    VariantInit(&child);
    child.vt = VT_I4;
    child.lVal = index;
    return child;
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

// The MSAA role a client expects for each semantic role.
LONG expected_role(SemanticRole role) noexcept {
    switch (role) {
    case SemanticRole::generic: return ROLE_SYSTEM_GROUPING;
    case SemanticRole::group: return ROLE_SYSTEM_GROUPING;
    case SemanticRole::static_text: return ROLE_SYSTEM_STATICTEXT;
    case SemanticRole::button: return ROLE_SYSTEM_PUSHBUTTON;
    case SemanticRole::check_box: return ROLE_SYSTEM_CHECKBUTTON;
    case SemanticRole::radio_button: return ROLE_SYSTEM_RADIOBUTTON;
    case SemanticRole::link: return ROLE_SYSTEM_LINK;
    case SemanticRole::text_box: return ROLE_SYSTEM_TEXT;
    case SemanticRole::list: return ROLE_SYSTEM_LIST;
    case SemanticRole::list_item: return ROLE_SYSTEM_LISTITEM;
    case SemanticRole::check_list_item: return ROLE_SYSTEM_CHECKBUTTON;
    case SemanticRole::combo_box: return ROLE_SYSTEM_COMBOBOX;
    case SemanticRole::slider: return ROLE_SYSTEM_SLIDER;
    case SemanticRole::scroll_bar: return ROLE_SYSTEM_SCROLLBAR;
    case SemanticRole::progress_bar: return ROLE_SYSTEM_PROGRESSBAR;
    case SemanticRole::image: return ROLE_SYSTEM_GRAPHIC;
    case SemanticRole::tab_group: return ROLE_SYSTEM_PAGETABLIST;
    case SemanticRole::tab: return ROLE_SYSTEM_PAGETAB;
    case SemanticRole::split_pane: return ROLE_SYSTEM_PANE;
    case SemanticRole::numeric_field: return ROLE_SYSTEM_SPINBUTTON;
    case SemanticRole::tool_tip: return ROLE_SYSTEM_TOOLTIP;
    case SemanticRole::date_picker: return ROLE_SYSTEM_DROPLIST;
    case SemanticRole::calendar: return ROLE_SYSTEM_TABLE;
    case SemanticRole::date_cell: return ROLE_SYSTEM_CELL;
    case SemanticRole::property_grid: return ROLE_SYSTEM_TABLE;
    case SemanticRole::property_group: return ROLE_SYSTEM_GROUPING;
    case SemanticRole::property_row: return ROLE_SYSTEM_ROW;
    case SemanticRole::menu_bar: return ROLE_SYSTEM_MENUBAR;
    case SemanticRole::menu_bar_item: return ROLE_SYSTEM_MENUITEM;
    case SemanticRole::menu: return ROLE_SYSTEM_MENUPOPUP;
    case SemanticRole::menu_item: return ROLE_SYSTEM_MENUITEM;
    case SemanticRole::separator: return ROLE_SYSTEM_SEPARATOR;
    case SemanticRole::toolbar: return ROLE_SYSTEM_TOOLBAR;
    case SemanticRole::radio_group: return ROLE_SYSTEM_GROUPING;
    case SemanticRole::split_button: return ROLE_SYSTEM_SPLITBUTTON;
    case SemanticRole::menu_button: return ROLE_SYSTEM_BUTTONMENU;
    case SemanticRole::pane: return ROLE_SYSTEM_PANE;
    case SemanticRole::tab_panel: return ROLE_SYSTEM_PROPERTYPAGE;
    // The stage A vocabulary (12.13). MSAA has no tree-grid or live-region
    // role: a tree grid is an outline, as Chromium publishes one, and a live
    // region a grouping (UI Automation carries its live setting).
    case SemanticRole::tree: return ROLE_SYSTEM_OUTLINE;
    case SemanticRole::tree_item: return ROLE_SYSTEM_OUTLINEITEM;
    case SemanticRole::table: return ROLE_SYSTEM_TABLE;
    case SemanticRole::row: return ROLE_SYSTEM_ROW;
    case SemanticRole::cell: return ROLE_SYSTEM_CELL;
    case SemanticRole::column_header: return ROLE_SYSTEM_COLUMNHEADER;
    case SemanticRole::row_header: return ROLE_SYSTEM_ROWHEADER;
    case SemanticRole::tree_grid: return ROLE_SYSTEM_OUTLINE;
    case SemanticRole::dialog: return ROLE_SYSTEM_DIALOG;
    case SemanticRole::status_bar: return ROLE_SYSTEM_STATUSBAR;
    case SemanticRole::live_region: return ROLE_SYSTEM_GROUPING;
    case SemanticRole::meter: return ROLE_SYSTEM_PROGRESSBAR;
    case SemanticRole::count: return ROLE_SYSTEM_GROUPING;
    }
    return ROLE_SYSTEM_GROUPING;
}

// The MSAA state bits a client expects for a node.
LONG expected_state(const SemanticNode& node) noexcept {
    const SemanticState states = node.states;
    LONG bits = 0;
    if (!has_semantic_state(states, SemanticState::enabled)) bits |= STATE_SYSTEM_UNAVAILABLE;
    if (!has_semantic_state(states, SemanticState::visible)) bits |= STATE_SYSTEM_INVISIBLE;
    if (has_semantic_state(states, SemanticState::focused)) bits |= STATE_SYSTEM_FOCUSED;
    if (has_semantic_state(states, SemanticState::focusable)) bits |= STATE_SYSTEM_FOCUSABLE;
    // A toggle button (a button with checked) is pressed, as a Win32
    // push-like check box is; every other checked node is checked.
    if (has_semantic_state(states, SemanticState::checked)) {
        bits |= is_button_role(node.role) ? STATE_SYSTEM_PRESSED : STATE_SYSTEM_CHECKED;
    }
    if (has_semantic_state(states, SemanticState::mixed)) bits |= STATE_SYSTEM_MIXED;
    if (has_semantic_state(states, SemanticState::selected)) bits |= STATE_SYSTEM_SELECTED;
    if (has_action(node, SemanticAction::select)) bits |= STATE_SYSTEM_SELECTABLE;
    if (has_semantic_state(states, SemanticState::read_only)) bits |= STATE_SYSTEM_READONLY;
    if (has_semantic_state(states, SemanticState::expanded)) {
        bits |= STATE_SYSTEM_EXPANDED;
    } else if (has_action(node, SemanticAction::expand)) {
        bits |= STATE_SYSTEM_COLLAPSED;
    }
    if (has_semantic_state(states, SemanticState::protected_content)) bits |= STATE_SYSTEM_PROTECTED;
    if (has_semantic_state(states, SemanticState::busy)) bits |= STATE_SYSTEM_BUSY;
    if (node.has_popup) bits |= STATE_SYSTEM_HASPOPUP;
    if (has_semantic_state(states, SemanticState::multiselectable)) bits |= STATE_SYSTEM_MULTISELECTABLE;
    if (node.role == SemanticRole::link) bits |= STATE_SYSTEM_LINKED;
    return bits;
}

// What a node's MSAA default action does, as this client expects it.
enum class DefaultKind : std::uint8_t { none, press, show_menu, select, expand, collapse };

// The default action a client expects: the first of press, show menu,
// select, then expand or collapse by the node's expanded state. Focus is
// not a default action (MSAA reaches it through accSelect).
DefaultKind expected_default(const SemanticNode& node) noexcept {
    if (has_action(node, SemanticAction::press)) return DefaultKind::press;
    if (has_action(node, SemanticAction::show_menu)) return DefaultKind::show_menu;
    if (has_action(node, SemanticAction::select)) return DefaultKind::select;
    const bool expanded = has_semantic_state(node.states, SemanticState::expanded);
    if (expanded && has_action(node, SemanticAction::collapse)) return DefaultKind::collapse;
    if (!expanded && has_action(node, SemanticAction::expand)) return DefaultKind::expand;
    return DefaultKind::none;
}

// The verb a client reads for that default action: the words the Win32
// control proxies use.
std::string expected_verb(const SemanticNode& node) {
    const DefaultKind kind = expected_default(node);
    switch (kind) {
    case DefaultKind::none: return {};
    case DefaultKind::press: {
        const bool opens = node.has_popup &&
                           (node.role == SemanticRole::menu_item ||
                            node.role == SemanticRole::menu_bar_item ||
                            node.role == SemanticRole::menu_button);
        if (opens) return "Open";
        if (is_check_role(node.role)) {
            const bool checked = has_semantic_state(node.states, SemanticState::checked);
            return checked ? "Uncheck" : "Check";
        }
        if (node.role == SemanticRole::radio_button) return "Check";
        if (node.role == SemanticRole::link) return "Jump";
        if (node.role == SemanticRole::menu_item) return "Execute";
        return "Press";
    }
    case DefaultKind::show_menu: return "Open";
    case DefaultKind::select: return "Select";
    case DefaultKind::expand: return "Expand";
    case DefaultKind::collapse: return "Collapse";
    }
    return {};
}

// What one MSAA object reports about itself.
struct MsaaReading final {
    LONG role{-1};
    LONG state{-1};
    std::string name;
    std::string value;
    std::string description;
    std::string default_action;
    // The access key as MSAA's keyboard shortcut ("Alt+" and the mnemonic).
    std::string keyboard_shortcut;
    LONG x{};
    LONG y{};
    LONG width{};
    LONG height{};
    LONG child_count{-1};
    // A failure HRESULT from any property read, or S_OK.
    HRESULT failure{S_OK};
};

void note_failure(MsaaReading& reading, HRESULT status) noexcept {
    if (FAILED(status) && SUCCEEDED(reading.failure)) reading.failure = status;
}

MsaaReading read_object(IAccessible& object) {
    MsaaReading reading;
    VARIANT self = self_child();
    VARIANT role;
    VariantInit(&role);
    note_failure(reading, object.get_accRole(self, &role));
    if (role.vt == VT_I4) reading.role = role.lVal;
    VariantClear(&role);
    VARIANT state;
    VariantInit(&state);
    note_failure(reading, object.get_accState(self, &state));
    if (state.vt == VT_I4) reading.state = state.lVal;
    VariantClear(&state);
    BstrOwner name;
    note_failure(reading, object.get_accName(self, name.receive()));
    reading.name = name.utf8();
    BstrOwner value;
    note_failure(reading, object.get_accValue(self, value.receive()));
    reading.value = value.utf8();
    BstrOwner description;
    note_failure(reading, object.get_accDescription(self, description.receive()));
    reading.description = description.utf8();
    BstrOwner action;
    note_failure(reading, object.get_accDefaultAction(self, action.receive()));
    reading.default_action = action.utf8();
    BstrOwner shortcut;
    note_failure(reading, object.get_accKeyboardShortcut(self, shortcut.receive()));
    reading.keyboard_shortcut = shortcut.utf8();
    note_failure(reading, object.accLocation(&reading.x, &reading.y, &reading.width,
                                             &reading.height, self));
    note_failure(reading, object.get_accChildCount(&reading.child_count));
    return reading;
}

// The child object at a one-based MSAA child index, or empty.
AccessibleReference child_object(IAccessible& parent, LONG index) {
    AccessibleReference child;
    IDispatch* dispatch = nullptr;
    const HRESULT status = parent.get_accChild(indexed_child(index), &dispatch);
    if (FAILED(status) || dispatch == nullptr) return child;
    (*dispatch).QueryInterface(IID_IAccessible, reinterpret_cast<void**>(child.receive()));
    (*dispatch).Release();
    return child;
}

AccessibleReference parent_object(IAccessible& child) {
    AccessibleReference parent;
    IDispatch* dispatch = nullptr;
    const HRESULT status = child.get_accParent(&dispatch);
    if (FAILED(status) || dispatch == nullptr) return parent;
    (*dispatch).QueryInterface(IID_IAccessible, reinterpret_cast<void**>(parent.receive()));
    (*dispatch).Release();
    return parent;
}

AccessibleReference window_root(HWND hwnd) {
    AccessibleReference root;
    const HRESULT status = AccessibleObjectFromWindow(
        hwnd, static_cast<DWORD>(OBJID_CLIENT), IID_IAccessible,
        reinterpret_cast<void**>(root.receive()));
    if (FAILED(status)) root.reset();
    return root;
}

// One difference between an MSAA object and its semantic node.
struct Mismatch final {
    std::string page;
    std::string stable_id;
    std::string control_type;
    std::string field;
    std::string expected;
    std::string observed;
};

// Per registry control type: how many nodes the Gallery published for it
// (its own nodes, and the virtual children it publishes as items), and how
// many matched in every field.
struct ControlTally final {
    std::size_t own_nodes{};
    std::size_t item_nodes{};
    std::size_t matched{};
    std::size_t mismatched{};
};

// What happened when a default action was performed.
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

// One node chosen to have its default action performed: the first enabled
// node of its control type and role on a page other than the host-services
// page, preferring one whose change can be seen (an unchecked radio button,
// an unselected item).
struct ActionCandidate final {
    std::string page;
    // The stable ID of the nearest control-backed node at or above the
    // node (itself for a control's own node), which with the node's stable
    // ID names it: a virtual child's stable ID is unique only within its
    // owner.
    std::string scope_id;
    std::string stable_id;
    std::string control_type;
    SemanticRole role{SemanticRole::generic};
    // True when the change the action makes is visible from the node's
    // state before it (false for a radio button already checked or an
    // item already selected).
    bool observable{};
};

struct ActionResult final {
    std::string key;
    std::string page;
    std::string stable_id;
    std::string verb;
    std::string evidence;
    ActionOutcome outcome{ActionOutcome::unobserved};
};

struct Report final {
    std::map<std::string, ControlTally> tallies;
    std::vector<Mismatch> mismatches;
    std::map<std::string, ActionCandidate> candidates;
    std::vector<ActionResult> actions;
    std::size_t pages{};
    std::size_t nodes{};
    // A retained object of a control that is gone once the window closes.
    AccessibleReference retained;
    std::string failure;
    // Actions run again alone in a fresh window after their change was not
    // seen in the shared one.
    std::size_t reruns{};
    bool completed{};
};

// A control that publishes virtual children, with the stable IDs of all of
// them and their virtual descendants, so the client can name the control
// type a virtual node belongs to.
struct VirtualOwner final {
    Control::Ptr control;
    std::string control_type;
    std::vector<std::string> stable_ids;
};

// The fixed parts of one page's comparison.
struct PageWalk final {
    Window& window;
    Report& report;
    std::string_view page;
    double scale{1.0};
    POINT origin{};
    std::vector<VirtualOwner> owners;
};

// A node a control publishes about itself (not a virtual child, not the
// client node).
bool control_backed(const SemanticNode& node) noexcept {
    const bool backed = node.runtime_id != 0U && !is_virtual_semantic_runtime_id(node.runtime_id);
    return backed;
}

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

std::string hresult_text(HRESULT status) {
    const unsigned long bits = static_cast<unsigned long>(status);
    char text[16]{};
    std::snprintf(text, sizeof(text), "0x%08lX", bits);
    return std::string(text);
}

std::string number_text(LONG value) { return std::to_string(value); }

std::string location_text(LONG x, LONG y, LONG width, LONG height) {
    const std::string text = number_text(x) + "," + number_text(y) + " " +
                             number_text(width) + "x" + number_text(height);
    return text;
}

LONG device_coordinate(double logical, double scale) noexcept {
    const LONG device = static_cast<LONG>(std::lround(logical * scale));
    return device;
}

void add_mismatch(PageWalk& walk, const SemanticNode& node, std::string_view type,
                  std::string_view field, std::string expected, std::string observed,
                  bool& matched) {
    Mismatch mismatch;
    mismatch.page = std::string(walk.page);
    mismatch.stable_id = node.stable_id;
    mismatch.control_type = std::string(type);
    mismatch.field = std::string(field);
    mismatch.expected = std::move(expected);
    mismatch.observed = std::move(observed);
    walk.report.mismatches.push_back(std::move(mismatch));
    matched = false;
}

// The registry type a node belongs to: its control's anatomy id, or, for a
// virtual child, the id of the control at or below its scope that publishes
// it.
std::string node_control_type(const PageWalk& walk, const SemanticNode& node,
                              std::string_view scope_id, bool& is_item) {
    if (control_backed(node)) {
        is_item = false;
        const Control::Ptr control = walk.window.find(node.stable_id);
        if (!control) return std::string("(control not found)");
        const std::string type((*control).anatomy().control_id);
        return type;
    }
    is_item = true;
    Control::Ptr scope;
    if (!scope_id.empty()) scope = walk.window.find(scope_id);
    for (const VirtualOwner& owner : walk.owners) {
        const bool within = !scope || owner.control == scope || (*scope).contains(*owner.control);
        if (within && contains_text(owner.stable_ids, node.stable_id)) return owner.control_type;
    }
    return std::string("(owner not found)");
}

bool candidate_observable(const SemanticNode& node, DefaultKind kind) noexcept {
    if (kind == DefaultKind::select) {
        return !has_semantic_state(node.states, SemanticState::selected);
    }
    if (kind == DefaultKind::press && node.role == SemanticRole::radio_button) {
        return !has_semantic_state(node.states, SemanticState::checked);
    }
    if (kind == DefaultKind::none) {
        return !has_semantic_state(node.states, SemanticState::focused);
    }
    return true;
}

const char* default_kind_name(DefaultKind kind) noexcept {
    switch (kind) {
    case DefaultKind::none: return "focus";
    case DefaultKind::press: return "press";
    case DefaultKind::show_menu: return "show menu";
    case DefaultKind::select: return "select";
    case DefaultKind::expand: return "expand";
    case DefaultKind::collapse: return "collapse";
    }
    return "focus";
}

// The key of a node's action: its role, the default action kind and its
// placement. The provider routes an action by exactly these, so one action
// per key covers the mapping.
std::string action_key(const SemanticNode& node, DefaultKind kind, bool is_item, std::string_view scope_id) {
    const char* placement = "own node";
    if (is_item && scope_id.empty()) placement = "unscoped item";
    if (is_item && !scope_id.empty()) placement = "item";
    std::string key = semantic_role_name(node.role);
    key += " / ";
    key += default_kind_name(kind);
    key += " / ";
    key += placement;
    return key;
}

void consider_candidate(PageWalk& walk, const SemanticNode& node, std::string_view type, bool is_item,
                        std::string_view scope_id) {
    if (walk.page == host_services_page) return;
    if (!has_semantic_state(node.states, SemanticState::enabled)) return;
    const DefaultKind kind = expected_default(node);
    // Focus is the default for a focusable node, and for one that publishes
    // the focus action although it is not focusable itself: a composite whose
    // own handler moves focus to an inner editor or part.
    const bool focus_only = kind == DefaultKind::none &&
                            (has_semantic_state(node.states, SemanticState::focusable) ||
                             has_action(node, SemanticAction::focus));
    if (kind == DefaultKind::none && !focus_only) return;
    const std::string key = action_key(node, kind, is_item, scope_id);
    const bool observable = candidate_observable(node, kind);
    std::map<std::string, ActionCandidate>::iterator existing = walk.report.candidates.find(key);
    if (existing != walk.report.candidates.end()) {
        if ((*existing).second.observable || !observable) return;
    }
    ActionCandidate candidate;
    candidate.page = std::string(walk.page);
    candidate.scope_id = std::string(scope_id);
    candidate.stable_id = node.stable_id;
    candidate.control_type = std::string(type);
    candidate.role = node.role;
    candidate.observable = observable;
    walk.report.candidates[key] = std::move(candidate);
}

// Compares one MSAA object with its semantic node, then descends. `root` is
// the window's client object, whose node the client builds from the window.
// `scope_id` is the stable ID of the nearest control-backed node above
// `node`, or empty.
void compare_tree(PageWalk& walk, IAccessible& object, const SemanticNode& node,
                  bool root, std::string_view scope_id) {
    bool is_item = false;
    const std::string type = root ? std::string("(window client)")
                                  : node_control_type(walk, node, scope_id, is_item);
    std::string_view own_scope = scope_id;
    if (control_backed(node)) own_scope = node.stable_id;
    const MsaaReading reading = read_object(object);
    ++walk.report.nodes;
    bool matched = true;
    if (FAILED(reading.failure)) {
        add_mismatch(walk, node, type, "status", "S_OK", hresult_text(reading.failure), matched);
    }
    const LONG role = root ? ROLE_SYSTEM_CLIENT : expected_role(node.role);
    if (reading.role != role) {
        add_mismatch(walk, node, type, "role", number_text(role), number_text(reading.role),
                     matched);
    }
    if (reading.name != node.name) {
        add_mismatch(walk, node, type, "name", node.name, reading.name, matched);
    }
    if (reading.value != node.value) {
        add_mismatch(walk, node, type, "value", node.value, reading.value, matched);
    }
    if (reading.description != node.description) {
        add_mismatch(walk, node, type, "description", node.description, reading.description,
                     matched);
    }
    const std::string shortcut = node.access_key.empty() ? std::string{} : "Alt+" + node.access_key;
    if (reading.keyboard_shortcut != shortcut) {
        add_mismatch(walk, node, type, "keyboard shortcut", shortcut, reading.keyboard_shortcut, matched);
    }
    const LONG state = expected_state(node);
    if (reading.state != state) {
        add_mismatch(walk, node, type, "state", hresult_text(state), hresult_text(reading.state),
                     matched);
    }
    const std::string verb = root ? std::string() : expected_verb(node);
    if (reading.default_action != verb) {
        add_mismatch(walk, node, type, "default action", verb, reading.default_action, matched);
    }
    const LONG x = walk.origin.x + device_coordinate(node.bounds.x, walk.scale);
    const LONG y = walk.origin.y + device_coordinate(node.bounds.y, walk.scale);
    const LONG width = device_coordinate(node.bounds.width, walk.scale);
    const LONG height = device_coordinate(node.bounds.height, walk.scale);
    const bool located = reading.x == x && reading.y == y && reading.width == width &&
                         reading.height == height;
    if (!located) {
        add_mismatch(walk, node, type, "location", location_text(x, y, width, height),
                     location_text(reading.x, reading.y, reading.width, reading.height),
                     matched);
    }
    const LONG children = static_cast<LONG>(node.children.size());
    if (reading.child_count != children) {
        add_mismatch(walk, node, type, "child count", number_text(children),
                     number_text(reading.child_count), matched);
    }
    if (!root) {
        ControlTally& tally = walk.report.tallies[type];
        if (is_item) {
            ++tally.item_nodes;
        } else {
            ++tally.own_nodes;
        }
        if (matched) {
            ++tally.matched;
        } else {
            ++tally.mismatched;
        }
        consider_candidate(walk, node, type, is_item, own_scope);
    }
    const LONG walked = reading.child_count < children ? reading.child_count : children;
    for (LONG index = 1; index <= walked; ++index) {
        const SemanticNode& child_node = node.children[static_cast<std::size_t>(index - 1)];
        AccessibleReference child = child_object(object, index);
        if (child.get() == nullptr) {
            bool child_matched = true;
            add_mismatch(walk, child_node, type, "get_accChild", "an object", "none",
                         child_matched);
            continue;
        }
        // The parent link must lead back to this object's node.
        AccessibleReference parent = parent_object(*child.get());
        const std::string parent_name =
            parent.get() == nullptr ? std::string("(none)") : read_object(*parent.get()).name;
        if (parent_name != node.name) {
            bool child_matched = true;
            add_mismatch(walk, child_node, type, "get_accParent name", node.name, parent_name,
                         child_matched);
        }
        compare_tree(walk, *child.get(), child_node, false, own_scope);
    }
}

// The client's model of the window client object: the snapshot's roots
// under one node named by the window title, covering the client area.
SemanticNode client_node(HWND hwnd, SemanticSnapshot snapshot, double scale) {
    SemanticNode node;
    node.role = SemanticRole::group;
    node.name = test_support::process_title(fixture_title_utf8);
    node.states = SemanticState::enabled | SemanticState::visible;
    RECT client{};
    GetClientRect(hwnd, &client);
    node.bounds = Rect{0.0, 0.0, client.right / scale, client.bottom / scale};
    node.children = std::move(snapshot.roots);
    return node;
}

// Finds the node a candidate names (its scope and stable ID) below the
// client and records the one-based child indices that lead to it.
// `current_scope` is the scope of `nodes`' parent.
bool find_path(const std::vector<SemanticNode>& nodes, std::string_view scope_id,
               std::string_view stable_id, std::string_view current_scope,
               std::vector<LONG>& path) {
    for (std::size_t index = 0; index < nodes.size(); ++index) {
        const SemanticNode& node = nodes[index];
        std::string_view node_scope = current_scope;
        if (control_backed(node)) node_scope = node.stable_id;
        path.push_back(static_cast<LONG>(index + 1U));
        if (node.stable_id == stable_id && node_scope == scope_id) return true;
        if (find_path(node.children, scope_id, stable_id, node_scope, path)) return true;
        path.pop_back();
    }
    return false;
}

// The node at a path find_path recorded, or null.
const SemanticNode* node_at(const std::vector<SemanticNode>& roots,
                            const std::vector<LONG>& path) {
    const std::vector<SemanticNode>* level = &roots;
    const SemanticNode* node = nullptr;
    for (const LONG index : path) {
        if (index < 1) return nullptr;
        const std::size_t position = static_cast<std::size_t>(index - 1);
        if (position >= (*level).size()) return nullptr;
        node = &(*level)[position];
        level = &(*node).children;
    }
    return node;
}

const SemanticNode* node_at(std::vector<SemanticNode>&&, const std::vector<LONG>&) = delete;

// The node a candidate names in `roots`, or null; fills `path`.
const SemanticNode* find_candidate(const std::vector<SemanticNode>& roots,
                                   const ActionCandidate& candidate, std::vector<LONG>& path) {
    path.clear();
    if (!find_path(roots, candidate.scope_id, candidate.stable_id, std::string_view(), path)) {
        return nullptr;
    }
    const SemanticNode* found = node_at(roots, path);
    return found;
}
const SemanticNode* find_candidate(std::vector<SemanticNode>&&, const ActionCandidate&,
                                   std::vector<LONG>&) = delete;

AccessibleReference object_at(IAccessible& root, const std::vector<LONG>& path) {
    AccessibleReference current;
    IAccessible* parent = &root;
    for (const LONG index : path) {
        AccessibleReference next = child_object(*parent, index);
        if (next.get() == nullptr) return next;
        current = std::move(next);
        parent = current.get();
    }
    return current;
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

// Closes popups an action opened, so the page's root set is restored.
void close_popups(Window& window, std::size_t baseline_roots) {
    for (int attempt = 0; attempt < maximum_popup_closes; ++attempt) {
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

// True when an action opened a popup: the snapshot gained roots or nodes,
// or a focus scope began (a modal popup, while it is open, is the
// snapshot's only root).
bool popup_opened(const SemanticSnapshot& before, const SemanticSnapshot& after,
                  std::size_t scopes_before, std::size_t scopes_after) noexcept {
    const bool opened = after.roots.size() > before.roots.size() ||
                        after.node_count > before.node_count || scopes_after > scopes_before;
    return opened;
}

// Performs the candidate's default action again, through a new client
// object, and reports whether its expanded and checked states are back to
// the given ones.
bool undo_by_default_action(Window& window, HWND hwnd, const ActionCandidate& candidate,
                            bool expanded_before, bool checked_before) {
    AccessibleReference root = window_root(hwnd);
    if (root.get() == nullptr) return false;
    std::vector<LONG> path;
    const SemanticSnapshot current = window.semantic_snapshot();
    if (find_candidate(current.roots, candidate, path) == nullptr) return false;
    AccessibleReference again = object_at(*root.get(), path);
    if (again.get() == nullptr) return false;
    const HRESULT undo = (*again.get()).accDoDefaultAction(self_child());
    if (undo != S_OK) return false;
    const SemanticSnapshot restored = window.semantic_snapshot();
    std::vector<LONG> back_path;
    const SemanticNode* back = find_candidate(restored.roots, candidate, back_path);
    if (back == nullptr) return false;
    const bool expanded = has_semantic_state((*back).states, SemanticState::expanded);
    const bool checked = has_semantic_state((*back).states, SemanticState::checked);
    const bool undone = expanded == expanded_before && checked == checked_before;
    return undone;
}

// Performs one candidate's default action through MSAA and records what the
// semantic tree shows afterwards.
ActionResult perform_candidate(Window& window, HWND hwnd, const std::string& key,
                               const ActionCandidate& candidate) {
    ActionResult result;
    result.key = key;
    result.page = candidate.page;
    result.stable_id = candidate.stable_id;
    const SemanticSnapshot before = window.semantic_snapshot();
    const std::size_t baseline_roots = before.roots.size();
    std::vector<LONG> path;
    const SemanticNode* found = find_candidate(before.roots, candidate, path);
    if (found == nullptr) {
        result.outcome = ActionOutcome::lost;
        result.evidence = "the node is not in the tree when its page is shown again";
        return result;
    }
    const SemanticNode node = *found;
    AccessibleReference root = window_root(hwnd);
    if (root.get() == nullptr) {
        result.outcome = ActionOutcome::lost;
        result.evidence = "WM_GETOBJECT returned no client object";
        return result;
    }
    AccessibleReference object = object_at(*root.get(), path);
    if (object.get() == nullptr) {
        result.outcome = ActionOutcome::lost;
        result.evidence = "the MSAA path to the node ends early";
        return result;
    }
    const DefaultKind kind = expected_default(node);
    result.verb = kind == DefaultKind::none ? std::string("accSelect(TAKEFOCUS)")
                                            : expected_verb(node);
    ClickCounter clicks;
    SubscriptionToken subscription;
    const Control::Ptr control = window.find(node.stable_id);
    const std::shared_ptr<ButtonBase> button = std::dynamic_pointer_cast<ButtonBase>(control);
    if (button) {
        subscription = (*button).clicked().subscribe(
            Delegate<ButtonBase&>::bind<ClickCounter, &ClickCounter::clicked>(clicks));
    }
    // A colour picker's press commits its colour again through its command,
    // which changes no state when the colour is the same.
    InvocationCounter invocations;
    SubscriptionToken invocation_subscription;
    const std::shared_ptr<RibbonColorPicker> picker =
        std::dynamic_pointer_cast<RibbonColorPicker>(control);
    if (picker && (*picker).command()) {
        const std::shared_ptr<Command>& command = (*(*picker).command()).command();
        invocation_subscription = (*command).invoked().subscribe(
            Delegate<const CommandInvocation&>::bind<InvocationCounter,
                                                     &InvocationCounter::invoked>(invocations));
    }
    const std::uint64_t generation_before = window.semantic_generation();
    const std::size_t scopes_before = window.focus_scope_depth();
    HRESULT status = S_OK;
    if (kind == DefaultKind::none) {
        status = (*object.get()).accSelect(SELFLAG_TAKEFOCUS, self_child());
    } else {
        status = (*object.get()).accDoDefaultAction(self_child());
    }
    if (status != S_OK) {
        result.outcome = ActionOutcome::refused;
        result.evidence = "HRESULT " + hresult_text(status);
        close_popups(window, baseline_roots);
        return result;
    }
    const SemanticSnapshot after = window.semantic_snapshot();
    const std::size_t scopes_after = window.focus_scope_depth();
    std::vector<LONG> after_path;
    const SemanticNode* changed = find_candidate(after.roots, candidate, after_path);
    const bool checked_before = has_semantic_state(node.states, SemanticState::checked);
    const bool mixed_before = has_semantic_state(node.states, SemanticState::mixed);
    const bool expanded_before = has_semantic_state(node.states, SemanticState::expanded);
    result.outcome = ActionOutcome::unobserved;
    if (kind == DefaultKind::none) {
        const Control::Ptr focused = window.focused_control();
        if (changed != nullptr && has_semantic_state((*changed).states, SemanticState::focused)) {
            result.outcome = ActionOutcome::changed;
            result.evidence = "focused";
        } else if (control && focused && focused != control && (*control).contains(*focused)) {
            // A composite that is not focusable moved focus to an inner
            // control the semantic tree does not publish.
            result.outcome = ActionOutcome::changed;
            result.evidence = "focus moved inside, to " + std::string((*focused).stable_id().value());
        }
    } else if (kind == DefaultKind::select) {
        if (changed != nullptr && has_semantic_state((*changed).states, SemanticState::selected)) {
            result.outcome = ActionOutcome::changed;
            result.evidence = "selected";
        }
    } else if (kind == DefaultKind::expand || kind == DefaultKind::collapse) {
        if (changed != nullptr &&
            has_semantic_state((*changed).states, SemanticState::expanded) != expanded_before) {
            result.outcome = ActionOutcome::changed;
            result.evidence = expanded_before ? "collapsed" : "expanded";
        } else if (kind == DefaultKind::expand &&
                   popup_opened(before, after, scopes_before, scopes_after)) {
            result.outcome = ActionOutcome::changed;
            result.evidence = "a popup opened";
        }
    } else if (kind == DefaultKind::show_menu) {
        if (popup_opened(before, after, scopes_before, scopes_after)) {
            result.outcome = ActionOutcome::changed;
            result.evidence = "a popup opened";
        } else if (changed != nullptr &&
                   has_semantic_state((*changed).states, SemanticState::expanded)) {
            result.outcome = ActionOutcome::changed;
            result.evidence = "expanded";
        }
    } else if (changed != nullptr &&
               (has_semantic_state((*changed).states, SemanticState::checked) != checked_before ||
                has_semantic_state((*changed).states, SemanticState::mixed) != mixed_before)) {
        result.outcome = ActionOutcome::changed;
        result.evidence = has_semantic_state((*changed).states, SemanticState::checked)
                              ? "checked" : "unchecked";
    } else if (clicks.count == 1) {
        result.outcome = ActionOutcome::changed;
        result.evidence = "clicked raised once";
    } else if (invocations.count == 1) {
        result.outcome = ActionOutcome::changed;
        result.evidence = "its command invoked once";
    } else if (popup_opened(before, after, scopes_before, scopes_after)) {
        result.outcome = ActionOutcome::changed;
        result.evidence = "a popup opened";
    } else if (window.semantic_generation() != generation_before) {
        result.outcome = ActionOutcome::changed;
        result.evidence = "the semantic tree changed";
    }
    if (clicks.count > 1) {
        result.outcome = ActionOutcome::unobserved;
        result.evidence = "clicked raised " + std::to_string(clicks.count) + " times";
    }
    // An expansion or a check box toggle is undone through the same
    // default action, which proves the reverse as well; before any popup
    // is closed, since closing a drop-down collapses it.
    const bool reversible = result.outcome == ActionOutcome::changed &&
                            (kind == DefaultKind::expand || kind == DefaultKind::collapse ||
                             (kind == DefaultKind::press && is_check_role(node.role))) &&
                            changed != nullptr && result.evidence != "a popup opened";
    if (reversible) {
        const bool undone = undo_by_default_action(window, hwnd, candidate, expanded_before,
                                                   checked_before);
        result.evidence += undone ? ", then restored" : ", but not restored";
        if (!undone) result.outcome = ActionOutcome::unobserved;
    }
    close_popups(window, baseline_roots);
    return result;
}

void print_report(const Report& report) {
    std::cout << "MSAA Gallery client: " << report.pages << " pages, " << report.nodes
              << " accessible objects compared\n";
    std::cout << "| control type | own nodes | item nodes | matched | mismatched |\n";
    for (const std::pair<const std::string, ControlTally>& entry : report.tallies) {
        const ControlTally& tally = entry.second;
        std::cout << "| " << entry.first << " | " << tally.own_nodes << " | " << tally.item_nodes
                  << " | " << tally.matched << " | " << tally.mismatched << " |\n";
    }
    std::cout << "default actions (" << report.actions.size() << ", " << report.reruns
              << " run again alone in a fresh window):\n";
    for (const ActionResult& action : report.actions) {
        std::cout << "  " << action.key << " [" << action.page << " " << action.stable_id
                  << "] " << action.verb << ": " << outcome_name(action.outcome);
        if (!action.evidence.empty()) std::cout << " (" << action.evidence << ")";
        std::cout << '\n';
    }
    std::cout << "mismatches (" << report.mismatches.size() << "):\n";
    for (const Mismatch& mismatch : report.mismatches) {
        std::cout << "  " << mismatch.page << " " << mismatch.stable_id << " ["
                  << mismatch.control_type << "] " << mismatch.field << ": expected \""
                  << mismatch.expected << "\", observed \"" << mismatch.observed << "\"\n";
    }
}

// Shows a Gallery page; the off-tree fixture has one page, always shown.
bool show_page(Window& window, std::string_view page) {
    if (page == off_tree_page) return true;
    const bool selected = gallery::select_gallery_page(window, page);
    return selected;
}

void place(Panel& root, const Control::Ptr& control, Rect bounds) {
    (*control).set_requested_bounds(bounds);
    root.add_child(control);
}

// The off-tree fixture: each registry control the Gallery does not attach
// to its root tree, in a cell of its own.
std::unique_ptr<Window> make_off_tree_fixture() {
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("off-tree"));
    const std::shared_ptr<ButtonBase> button_base =
        make_control<ButtonBase>(StableId("off-tree.button-base"), "Base command");
    place(*root, button_base, Rect{10.0, 10.0, 160.0, 30.0});
    place(*root, make_control<RangeControl>(StableId("off-tree.range-control")),
          Rect{10.0, 50.0, 160.0, 30.0});
    place(*root, make_control<ScrollBar>(StableId("off-tree.scroll-bar")),
          Rect{10.0, 90.0, 160.0, 18.0});
    const std::shared_ptr<ScaledPanel> scaled =
        make_control<ScaledPanel>(StableId("off-tree.scaled-panel"), Size{200.0, 100.0});
    place(*root, scaled, Rect{200.0, 10.0, 200.0, 100.0});
    const std::shared_ptr<Label> scaled_label =
        make_control<Label>(StableId("off-tree.scaled-panel.label"), "Scaled content");
    (*scaled_label).set_requested_bounds(Rect{4.0, 4.0, 150.0, 24.0});
    (*scaled).add_child(scaled_label);
    place(*root, make_control<SplitterPanel>(StableId("off-tree.splitter-panel")),
          Rect{420.0, 10.0, 160.0, 100.0});
    place(*root, make_control<ToolStripPanel>(StableId("off-tree.tool-strip-panel")),
          Rect{600.0, 10.0, 300.0, 40.0});
    place(*root, make_control<PropertyGrid>(StableId("off-tree.property-grid")),
          Rect{10.0, 130.0, 300.0, 240.0});
    place(*root, make_control<TaskDialog>(StableId("off-tree.task-dialog")),
          Rect{330.0, 130.0, 420.0, 240.0});
    place(*root, make_control<FontDialog>(StableId("off-tree.font-dialog")),
          Rect{10.0, 390.0, 640.0, 400.0});
    const Size size{1280.0, 820.0};
    std::unique_ptr<Window> window = std::make_unique<Window>(root, size);
    return window;
}

// One action to perform: a candidate and its key in Report::candidates.
struct ActionJob final {
    const std::string* key;
    const ActionCandidate* candidate;
};

// The candidates acted on in one window: the Gallery's in Gallery page
// order (`window_kind` empty), or the off-tree fixture's. Within a page, in
// key order.
void append_page_jobs(const Report& report, std::string_view page, std::vector<ActionJob>& jobs) {
    for (const std::pair<const std::string, ActionCandidate>& entry : report.candidates) {
        if (entry.second.page == page) jobs.push_back(ActionJob{&entry.first, &entry.second});
    }
}

std::vector<ActionJob> window_jobs(const Report& report, std::string_view window_kind) {
    std::vector<ActionJob> jobs;
    if (window_kind == off_tree_page) {
        append_page_jobs(report, off_tree_page, jobs);
        return jobs;
    }
    for (const gallery::GalleryPageSpec& page : gallery::gallery_pages) {
        append_page_jobs(report, page.id, jobs);
    }
    return jobs;
}

// Runs the client once the host has attached the Gallery and its
// accessibility publisher, on the UI thread, then asks the host to close.
// Without jobs it compares every page, then performs the candidates the
// comparison found; with them, it performs only each job's action in turn
// in this fresh window. Either way it appends one result per action.
struct HostReady final {
    Report* report;
    Window* window;
    // The Gallery, or off_tree_page for the off-tree fixture.
    std::string_view window_kind;
    const std::vector<ActionJob>* jobs;

    void run(Window& window) const;
    void compare_pages(Window& window, HWND hwnd, double scale, POINT origin) const;
    void retain_object(Window& window, HWND hwnd) const;
    void compare_page(Window& window, HWND hwnd, double scale, POINT origin,
                      std::string_view page) const;

    void operator()(std::function<void()>, std::function<void()> request_close,
                    std::function<HostDialogResult(const HostDialogRequest&)>,
                    std::function<HostServiceStatus(const HostTooltipRequest&)>,
                    std::function<void()>, std::function<HostClipboardTextResult()>,
                    std::function<HostServiceStatus(std::string_view)>) const {
        try {
            run(*window);
            (*report).completed = true;
        } catch (const std::exception& error) {
            (*report).failure = error.what();
        }
        request_close();
    }
};

void HostReady::run(Window& window) const {
    HWND hwnd = test_support::own_gui_forms_window(test_support::process_title_wide(fixture_title_utf8).c_str());
    if (hwnd == nullptr) throw std::runtime_error("the Gallery's native window does not exist");
    const UINT dpi = GetDpiForWindow(hwnd);
    const double scale = dpi == 0U ? 1.0 : std::max(1.0, dpi / 96.0);
    POINT origin{};
    ClientToScreen(hwnd, &origin);
    const std::vector<ActionJob>* acted = jobs;
    std::vector<ActionJob> found;
    if (jobs == nullptr) {
        compare_pages(window, hwnd, scale, origin);
        // The comparison found the candidates; this window performs them.
        found = window_jobs(*report, window_kind);
        acted = &found;
    }
    for (const ActionJob& job : *acted) {
        const ActionCandidate& candidate = *job.candidate;
        if (!show_page(window, candidate.page)) {
            throw std::runtime_error("a Gallery page cannot be selected: " + candidate.page);
        }
        window.perform_layout();
        (*report).actions.push_back(perform_candidate(window, hwnd, *job.key, candidate));
    }
    if (jobs == nullptr && window_kind != off_tree_page) retain_object(window, hwnd);
}

void HostReady::compare_page(Window& window, HWND hwnd, double scale, POINT origin,
                             std::string_view page) const {
    if (!show_page(window, page)) {
        throw std::runtime_error("a Gallery page cannot be selected: " + std::string(page));
    }
    window.perform_layout();
    AccessibleReference root = window_root(hwnd);
    if (root.get() == nullptr) throw std::runtime_error("WM_GETOBJECT publishes no MSAA root");
    const SemanticNode client = client_node(hwnd, window.semantic_snapshot(), scale);
    PageWalk walk{window, *report, page, scale, origin, {}};
    collect_virtual_owners(window.root(), walk.owners);
    compare_tree(walk, *root.get(), client, true, std::string_view());
    ++(*report).pages;
}

void HostReady::compare_pages(Window& window, HWND hwnd, double scale, POINT origin) const {
    if (window_kind == off_tree_page) {
        compare_page(window, hwnd, scale, origin, off_tree_page);
        return;
    }
    for (const gallery::GalleryPageSpec& page : gallery::gallery_pages) {
        compare_page(window, hwnd, scale, origin, page.id);
    }
}

// Keeps one object whose control dies with the window.
void HostReady::retain_object(Window& window, HWND hwnd) const {
    gallery::select_gallery_page(window, gallery::gallery_pages[0].id);
    window.perform_layout();
    AccessibleReference root = window_root(hwnd);
    if (root.get() != nullptr) {
        (*report).retained = child_object(*root.get(), 1);
    }
}

// Runs one window with the client in it: the Gallery, or the off-tree
// fixture when `window_kind` is off_tree_page. Throws when the window does
// not run or the client fails.
void run_gallery(Report& report, std::string_view window_kind,
                 const std::vector<ActionJob>* jobs) {
    report.completed = false;
    std::unique_ptr<Window> gallery_window;
    if (window_kind == off_tree_page) {
        gallery_window = make_off_tree_fixture();
    } else {
        gallery_window = gallery::make_gallery();
    }
    Window* window = gallery_window.get();
    host::WindowsHostOptions options;
    options.title = test_support::process_title(fixture_title_utf8);
    options.initial_size = gallery::gallery_default_size;
    options.minimum_size = gallery::gallery_minimum_size;
    options.print_metrics_on_close = false;
    options.host_ready = HostReady{&report, window, window_kind, jobs};
    const int exit_code = host::run_windows(std::move(gallery_window), std::move(options));
    if (exit_code != 0) throw std::runtime_error("the Gallery window did not run");
    if (!report.failure.empty()) throw std::runtime_error(report.failure);
    if (!report.completed) throw std::runtime_error("the client did not complete");
}

// Runs one window of `window_kind`, which compares its pages and then
// performs its candidates page by page. An action whose change was not seen
// may have met an earlier action's effects (a closed panel, a popup still
// open); it runs again alone in a fresh window, and that result stands.
void run_window_kind(Report& report, std::string_view window_kind) {
    const std::size_t first = report.actions.size();
    run_gallery(report, window_kind, nullptr);
    const std::vector<ActionJob> jobs = window_jobs(report, window_kind);
    for (std::size_t index = 0; index < jobs.size(); ++index) {
        if (report.actions[first + index].outcome == ActionOutcome::changed) continue;
        const std::vector<ActionJob> alone{jobs[index]};
        ++report.reruns;
        run_gallery(report, window_kind, &alone);
        ActionResult isolated = std::move(report.actions.back());
        report.actions.pop_back();
        isolated.evidence += isolated.evidence.empty() ? "alone in a fresh window"
                                                       : "; alone in a fresh window";
        report.actions[first + index] = std::move(isolated);
    }
}

}  // namespace

int main() {
    try {
        Report report;
        run_window_kind(report, std::string_view());
        run_window_kind(report, off_tree_page);
        print_report(report);
        // After the window is gone a retained object is disconnected.
        if (report.retained.get() == nullptr) throw std::runtime_error("no object was retained");
        BstrOwner name;
        const HRESULT stale = (*report.retained.get()).get_accName(self_child(), name.receive());
        if (stale != CO_E_OBJNOTCONNECTED || !name.empty()) {
            throw std::runtime_error("a retained object is not disconnected after the window closes");
        }
        const HRESULT stale_action = (*report.retained.get()).accDoDefaultAction(self_child());
        if (stale_action != CO_E_OBJNOTCONNECTED) {
            throw std::runtime_error("a disconnected object's default action does not report "
                                     "CO_E_OBJNOTCONNECTED");
        }
        std::size_t failed_actions = 0;
        for (const ActionResult& action : report.actions) {
            if (action.outcome != ActionOutcome::changed) ++failed_actions;
        }
        if (!report.mismatches.empty() || failed_actions != 0U) {
            std::cerr << report.mismatches.size() << " mismatches, " << failed_actions
                      << " actions without an observed change\n";
            return 1;
        }
        std::cout << "MSAA Gallery client passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
