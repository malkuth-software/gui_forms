// The UI Automation client test over the whole Gallery (stage A, step A-W3).
//
// Runs the Gallery in a real Win32 window and reads it through
// IUIAutomation, as a separate client does, from a thread of its own in the
// multithreaded apartment. On every page it compares each element with the
// semantic node it publishes: control type, name, AutomationId (the stable
// ID), help text, enabled, focusable and focused, bounding rectangle,
// position in set, the patterns it offers and their states (toggle,
// expand-collapse, selected, value, range, and a text box's TextPattern
// text and selection offsets), the tab-to-panel relation, its
// child order and its parent, and that runtime IDs are unique. A second
// window holds the registry controls the Gallery does not attach to its
// tree. Then it performs one default action for every semantic role,
// pattern call and placement (a control's own node, an item of a control,
// an item with no control above it) through the pattern that action belongs
// to (Invoke, Toggle, SelectionItem, ExpandCollapse, or SetFocus when focus
// is the only action) and observes the change in the semantic tree, in the
// window that compared the pages (one whose change is not seen runs again
// alone in a fresh window); the adapter's path depends only on those three.
// Every page but live_page is read from one cache request for its whole
// raw subtree; that page, TextPattern ranges, parent links, the
// tab-to-panel targets and the stale element are read live, a call each.
// It checks that an element of a page that was replaced answers
// UIA_E_ELEMENTNOTAVAILABLE. Which node every registry control publishes
// and each control type's default action are checked headless by
// semantic_gallery_tests.cpp. TextPattern's
// units, Select and events are covered by
// windows_accessibility_uia_text_tests.cpp.
//
// The expected UI Automation vocabulary (control types, the pattern each
// node offers, pattern states) is written out here, independently of the
// provider, as a client expects it. Every mismatch is printed; the test
// fails on any mismatch, refused action or unobserved change.
//
// Execution context: the window's UI thread runs the host's message loop
// and owns the semantic tree. UI Automation marshals every provider call to
// it (the providers ask for COM threading). The client thread reads and
// changes the model only through Window::invoke, whose work runs on the UI
// thread in posting order, after any work an Invoke posted before it.

#include "gallery.hpp"
#include "gallery_page_specs.hpp"
#include "gui_forms/gui_forms.hpp"
#include "gui_forms/platform/windows_host.hpp"
#include "support/windows_own_window.hpp"

#include <windows.h>
#include <ole2.h>
#include <uiautomation.h>

#include <algorithm>
#include <array>
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
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace gui_forms;

// The window title, followed by the process ID (test_support::process_title).
constexpr std::string_view fixture_title_utf8 = "GUI.Forms UI Automation Gallery client";
constexpr const wchar_t* framework_id = L"GUI.Forms";

// The page whose buttons open the host's modal dialogs, write the system
// clipboard and play sounds: compared like every page, but no action is
// performed there (a modal dialog would hold the UI thread; the clipboard is
// the machine's).
constexpr std::string_view host_services_page = "dialogs-and-host-services";

// The fixture window holding the registry controls the Gallery does not
// attach to its root tree, compared and acted on like a Gallery page.
constexpr std::string_view off_tree_page = "off-tree-controls";

// The page the client reads live, every property and pattern state its
// own call; every other page is read from one cache request. It holds
// items, nested scopes and most pattern kinds.
constexpr std::string_view live_page = "tree-and-object-views";

// The most popups the client closes with Escape after an action.
constexpr int maximum_popup_closes = 4;

// -- COM ownership -----------------------------------------------------------

// One owned COM interface reference. Move-only; releases on destruction.
template <typename Interface>
class ComReference final {
public:
    ComReference() noexcept = default;
    ~ComReference() { reset(); }
    ComReference(ComReference&& other) noexcept : value_(other.value_) { other.value_ = nullptr; }
    ComReference& operator=(ComReference&& other) noexcept {
        if (this != &other) {
            reset();
            value_ = other.value_;
            other.value_ = nullptr;
        }
        return *this;
    }
    ComReference(const ComReference&) = delete;
    ComReference& operator=(const ComReference&) = delete;

    [[nodiscard]] Interface* get() const noexcept { return value_; }
    // The address an out-parameter call fills; any held reference is
    // released first.
    [[nodiscard]] Interface** receive() noexcept {
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
    Interface* value_{};
};

using ElementReference = ComReference<IUIAutomationElement>;

// One owned BSTR.
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
    [[nodiscard]] std::wstring wide() const {
        if (value_ == nullptr) return {};
        const std::wstring text(value_, SysStringLen(value_));
        return text;
    }
    [[nodiscard]] std::string utf8() const;

private:
    BSTR value_{};
};

std::string BstrOwner::utf8() const {
    if (value_ == nullptr) return {};
    const UINT length = SysStringLen(value_);
    if (length == 0U) return {};
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, value_, static_cast<int>(length), nullptr, 0,
                                          nullptr, nullptr);
    if (bytes <= 0) throw std::runtime_error("a UI Automation string does not convert to UTF-8");
    std::string text(static_cast<std::size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value_, static_cast<int>(length), text.data(), bytes, nullptr,
                        nullptr);
    return text;
}

std::string hresult_text(HRESULT status) {
    const unsigned long bits = static_cast<unsigned long>(status);
    char text[16]{};
    std::snprintf(text, sizeof(text), "0x%08lX", bits);
    return std::string(text);
}

// -- the expected vocabulary -------------------------------------------------

bool has_action(const SemanticNode& node, SemanticAction action) noexcept {
    for (const SemanticAction published : node.actions) {
        if (published == action) return true;
    }
    return false;
}

bool is_check_role(SemanticRole role) noexcept {
    const bool check = role == SemanticRole::check_box || role == SemanticRole::check_list_item;
    return check;
}

// The control type a client expects for each semantic role.
CONTROLTYPEID expected_control_type(SemanticRole role) noexcept {
    switch (role) {
    case SemanticRole::generic: return UIA_GroupControlTypeId;
    case SemanticRole::group: return UIA_GroupControlTypeId;
    case SemanticRole::static_text: return UIA_TextControlTypeId;
    case SemanticRole::button: return UIA_ButtonControlTypeId;
    case SemanticRole::check_box: return UIA_CheckBoxControlTypeId;
    case SemanticRole::radio_button: return UIA_RadioButtonControlTypeId;
    case SemanticRole::link: return UIA_HyperlinkControlTypeId;
    case SemanticRole::text_box: return UIA_EditControlTypeId;
    case SemanticRole::list: return UIA_ListControlTypeId;
    case SemanticRole::list_item: return UIA_ListItemControlTypeId;
    case SemanticRole::check_list_item: return UIA_ListItemControlTypeId;
    case SemanticRole::combo_box: return UIA_ComboBoxControlTypeId;
    case SemanticRole::slider: return UIA_SliderControlTypeId;
    case SemanticRole::scroll_bar: return UIA_ScrollBarControlTypeId;
    case SemanticRole::progress_bar: return UIA_ProgressBarControlTypeId;
    case SemanticRole::image: return UIA_ImageControlTypeId;
    case SemanticRole::tab_group: return UIA_TabControlTypeId;
    case SemanticRole::tab: return UIA_TabItemControlTypeId;
    case SemanticRole::split_pane: return UIA_PaneControlTypeId;
    case SemanticRole::numeric_field: return UIA_SpinnerControlTypeId;
    case SemanticRole::tool_tip: return UIA_ToolTipControlTypeId;
    case SemanticRole::date_picker: return UIA_ComboBoxControlTypeId;
    case SemanticRole::calendar: return UIA_CalendarControlTypeId;
    case SemanticRole::date_cell: return UIA_DataItemControlTypeId;
    case SemanticRole::property_grid: return UIA_TableControlTypeId;
    case SemanticRole::property_group: return UIA_GroupControlTypeId;
    case SemanticRole::property_row: return UIA_DataItemControlTypeId;
    case SemanticRole::menu_bar: return UIA_MenuBarControlTypeId;
    case SemanticRole::menu_bar_item: return UIA_MenuItemControlTypeId;
    case SemanticRole::menu: return UIA_MenuControlTypeId;
    case SemanticRole::menu_item: return UIA_MenuItemControlTypeId;
    case SemanticRole::separator: return UIA_SeparatorControlTypeId;
    case SemanticRole::toolbar: return UIA_ToolBarControlTypeId;
    case SemanticRole::radio_group: return UIA_GroupControlTypeId;
    case SemanticRole::split_button: return UIA_SplitButtonControlTypeId;
    case SemanticRole::menu_button: return UIA_ButtonControlTypeId;
    case SemanticRole::pane: return UIA_PaneControlTypeId;
    case SemanticRole::tab_panel: return UIA_PaneControlTypeId;
    // The stage A vocabulary (12.13). A tree grid is a data grid whose rows
    // are tree items; a cell is a data item, as a grid's cells are; a dialog
    // inside the window is a pane with IsDialog; a live region is a group
    // with a polite LiveSetting.
    case SemanticRole::tree: return UIA_TreeControlTypeId;
    case SemanticRole::tree_item: return UIA_TreeItemControlTypeId;
    case SemanticRole::table: return UIA_TableControlTypeId;
    case SemanticRole::row: return UIA_DataItemControlTypeId;
    case SemanticRole::cell: return UIA_DataItemControlTypeId;
    case SemanticRole::column_header: return UIA_HeaderItemControlTypeId;
    case SemanticRole::row_header: return UIA_HeaderItemControlTypeId;
    case SemanticRole::tree_grid: return UIA_DataGridControlTypeId;
    case SemanticRole::dialog: return UIA_PaneControlTypeId;
    case SemanticRole::status_bar: return UIA_StatusBarControlTypeId;
    case SemanticRole::live_region: return UIA_GroupControlTypeId;
    case SemanticRole::meter: return UIA_ProgressBarControlTypeId;
    case SemanticRole::count: return UIA_GroupControlTypeId;
    }
    return UIA_GroupControlTypeId;
}

// The patterns a client expects a node to offer.
struct ExpectedPatterns final {
    bool invoke{};
    bool toggle{};
    bool selection_item{};
    bool selection{};
    bool range_value{};
    bool value{};
    bool expand_collapse{};
    // A table or tree grid publishes its size; a cell its row and column.
    bool grid{};
    bool grid_item{};
    // A control's own text box that publishes a text selection (not a
    // password box, which publishes none).
    bool text{};
};

bool expects_toggle(const SemanticNode& node) noexcept {
    if (is_check_role(node.role)) return true;
    if (node.role == SemanticRole::radio_button) return false;
    // A toggle button or a checkable menu item publishes checkable whether
    // or not it is on.
    const bool can_check = has_semantic_state(node.states, SemanticState::checkable) ||
                           has_semantic_state(node.states, SemanticState::checked) ||
                           has_semantic_state(node.states, SemanticState::mixed);
    const bool toggles = can_check && has_action(node, SemanticAction::press);
    return toggles;
}

bool expects_selection_item(const SemanticNode& node) noexcept {
    const bool item = node.role == SemanticRole::radio_button || has_action(node, SemanticAction::select);
    return item;
}

ExpectedPatterns expected_patterns(const SemanticNode& node) noexcept {
    ExpectedPatterns patterns;
    patterns.toggle = expects_toggle(node);
    patterns.invoke = has_action(node, SemanticAction::press) && !patterns.toggle &&
                      node.role != SemanticRole::radio_button;
    patterns.selection_item = expects_selection_item(node);
    for (const SemanticNode& child : node.children) {
        if (expects_selection_item(child)) patterns.selection = true;
    }
    patterns.range_value = node.numeric_value.has_value();
    patterns.value = !node.numeric_value.has_value() &&
                     (has_action(node, SemanticAction::set_value) || node.role == SemanticRole::text_box);
    patterns.grid = node.row_count.has_value() && node.column_count.has_value();
    patterns.grid_item = node.row_index.has_value() && node.column_index.has_value();
    patterns.text = node.role == SemanticRole::text_box && node.text_selection.has_value() &&
                    !has_semantic_state(node.states, SemanticState::protected_content);
    patterns.expand_collapse = has_action(node, SemanticAction::expand) ||
                               has_action(node, SemanticAction::collapse) ||
                               has_action(node, SemanticAction::show_menu);
    return patterns;
}

bool expected_selected(const SemanticNode& node) noexcept {
    if (node.role == SemanticRole::radio_button) {
        return has_semantic_state(node.states, SemanticState::checked);
    }
    return has_semantic_state(node.states, SemanticState::selected);
}

ToggleState expected_toggle_state(const SemanticNode& node) noexcept {
    if (has_semantic_state(node.states, SemanticState::mixed)) return ToggleState_Indeterminate;
    if (has_semantic_state(node.states, SemanticState::checked)) return ToggleState_On;
    return ToggleState_Off;
}

ExpandCollapseState expected_expand_state(const SemanticNode& node) noexcept {
    if (has_semantic_state(node.states, SemanticState::expanded)) return ExpandCollapseState_Expanded;
    return ExpandCollapseState_Collapsed;
}

// What one default action is, as this client performs it.
enum class DefaultKind : std::uint8_t { none, press, show_menu, select, expand, collapse };

// The first of press, show menu, select, then expand or collapse by the
// node's expanded state (the order A-W1's client uses).
DefaultKind expected_default(const SemanticNode& node) noexcept {
    if (has_action(node, SemanticAction::press)) return DefaultKind::press;
    if (has_action(node, SemanticAction::show_menu)) return DefaultKind::show_menu;
    if (has_action(node, SemanticAction::select)) return DefaultKind::select;
    const bool expanded = has_semantic_state(node.states, SemanticState::expanded);
    if (expanded && has_action(node, SemanticAction::collapse)) return DefaultKind::collapse;
    if (!expanded && has_action(node, SemanticAction::expand)) return DefaultKind::expand;
    return DefaultKind::none;
}

// The pattern method the client calls for a node's default action.
enum class PatternCall : std::uint8_t { set_focus, invoke, toggle, select, expand, collapse };

PatternCall pattern_call(const SemanticNode& node, DefaultKind kind) noexcept {
    switch (kind) {
    case DefaultKind::none: return PatternCall::set_focus;
    case DefaultKind::press:
        if (expects_toggle(node)) return PatternCall::toggle;
        if (node.role == SemanticRole::radio_button) return PatternCall::select;
        return PatternCall::invoke;
    case DefaultKind::show_menu: return PatternCall::expand;
    case DefaultKind::select: return PatternCall::select;
    case DefaultKind::expand: return PatternCall::expand;
    case DefaultKind::collapse: return PatternCall::collapse;
    }
    return PatternCall::set_focus;
}

const char* pattern_call_name(PatternCall call) noexcept {
    switch (call) {
    case PatternCall::set_focus: return "SetFocus";
    case PatternCall::invoke: return "Invoke.Invoke";
    case PatternCall::toggle: return "Toggle.Toggle";
    case PatternCall::select: return "SelectionItem.Select";
    case PatternCall::expand: return "ExpandCollapse.Expand";
    case PatternCall::collapse: return "ExpandCollapse.Collapse";
    }
    return "unknown";
}

// -- what the UI thread prepares ---------------------------------------------

// A published node with what the client needs to judge it: the node (its
// children cleared), the registry control type it belongs to, whether it is
// an item (a virtual child), the stable ID of its scope (the nearest
// control-backed node at or above it) and its children.
struct ExpectedNode final {
    SemanticNode node;
    std::string control_type;
    bool is_item{};
    std::string scope_id;
    ExpectedPatterns patterns;
    std::vector<ExpectedNode> children;
};

// One page as the UI thread saw it: the expected tree under the window's
// client node and the geometry that converts its bounds to the screen.
struct PageExpectation final {
    ExpectedNode client;
    double scale{1.0};
    POINT origin{};
    bool shown{};
};

// A control that publishes virtual children, with the stable IDs of all of
// them and their descendants.
struct VirtualOwner final {
    Control::Ptr control;
    std::string control_type;
    std::vector<std::string> stable_ids;
};

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

// The registry type a node belongs to: its control's anatomy id, or, for a
// virtual child, the id of the control at or below its scope publishing it.
std::string node_control_type(Window& window, const std::vector<VirtualOwner>& owners,
                              const SemanticNode& node, std::string_view scope_id, bool& is_item) {
    if (control_backed(node)) {
        is_item = false;
        const Control::Ptr control = window.find(node.stable_id);
        if (!control) return std::string("(control not found)");
        const std::string type((*control).anatomy().control_id);
        return type;
    }
    is_item = true;
    Control::Ptr scope;
    if (!scope_id.empty()) scope = window.find(scope_id);
    for (const VirtualOwner& owner : owners) {
        const bool within = !scope || owner.control == scope || (*scope).contains(*owner.control);
        if (within && contains_text(owner.stable_ids, node.stable_id)) return owner.control_type;
    }
    return std::string("(owner not found)");
}

void build_expected(Window& window, const std::vector<VirtualOwner>& owners,
                    const SemanticNode& node, std::string_view scope_id, ExpectedNode& expected) {
    expected.node = node;
    expected.node.children.clear();
    expected.patterns = expected_patterns(node);
    if (!control_backed(node)) expected.patterns.text = false;
    expected.control_type = node_control_type(window, owners, node, scope_id, expected.is_item);
    std::string_view own_scope = scope_id;
    if (control_backed(node)) own_scope = node.stable_id;
    expected.scope_id = std::string(own_scope);
    expected.children.resize(node.children.size());
    for (std::size_t index = 0; index < node.children.size(); ++index) {
        build_expected(window, owners, node.children[index], own_scope, expected.children[index]);
    }
}

bool show_page(Window& window, std::string_view page) {
    if (page == off_tree_page) return true;
    const bool selected = gallery::select_gallery_page(window, page);
    return selected;
}

// Shows a page and builds its expectation, on the UI thread. With
// `freeze`, it first takes the native window's input away and ends any
// pointer hover (below), then cancels the window's frame requests: the client reads
// the tree through many UI-thread calls while the message loop runs, and
// the timers and animations some pages start (the timing page's tick and
// progress, the easing preview, the motion target) would change it between
// the expectation and the reads. A-W1's client sees one instant because it
// runs on the UI thread; a frozen window gives this client the same. Only
// the comparison windows are frozen; actions run in windows of their own.
struct PreparePage final {
    Window* window{};
    HWND hwnd{};
    std::string_view page;
    PageExpectation* expectation{};
    bool freeze{};
    void operator()() const {
        PageExpectation& out = *expectation;
        out.shown = show_page(*window, page);
        if (!out.shown) return;
        (*window).perform_layout();
        if (freeze) {
            // The physical pointer may rest over the window: hovering shows
            // a tooltip, and a later move hides it while the client reads.
            // A disabled native window takes no pointer or key input (UI
            // Automation still reaches the providers), and a leave ends any
            // hover the pointer began before.
            EnableWindow(hwnd, FALSE);
            PointerEvent leave;
            leave.action = PointerAction::leave;
            leave.position = Point{-1.0, -1.0};
            static_cast<void>((*window).dispatch_pointer(leave));
            (*window).perform_layout();
            (*window).cancel_frame_requests();
        }
        const UINT dpi = GetDpiForWindow(hwnd);
        out.scale = dpi == 0U ? 1.0 : std::max(1.0, dpi / 96.0);
        out.origin = POINT{};
        ClientToScreen(hwnd, &out.origin);
        const SemanticSnapshot snapshot = (*window).semantic_snapshot();
        std::vector<VirtualOwner> owners;
        collect_virtual_owners((*window).root(), owners);
        out.client = ExpectedNode{};
        out.client.node.name = test_support::process_title(fixture_title_utf8);
        out.client.control_type = "(window client)";
        out.client.children.resize(snapshot.roots.size());
        for (std::size_t index = 0; index < snapshot.roots.size(); ++index) {
            build_expected(*window, owners, snapshot.roots[index], std::string_view(),
                           out.client.children[index]);
        }
    }
};

// -- the comparison ------------------------------------------------------------

struct Mismatch final {
    std::string page;
    std::string stable_id;
    std::string control_type;
    std::string field;
    std::string expected;
    std::string observed;
};

struct ControlTally final {
    std::size_t own_nodes{};
    std::size_t item_nodes{};
    std::size_t matched{};
    std::size_t mismatched{};
};

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

// One node chosen to have its default action performed (A-W1's choice: the
// first enabled node of its type and role, preferring one whose change can
// be seen).
struct ActionCandidate final {
    std::string page;
    std::string scope_id;
    std::string stable_id;
    std::string control_type;
    SemanticRole role{SemanticRole::generic};
    bool observable{};
};

struct ActionResult final {
    std::string key;
    std::string page;
    std::string stable_id;
    std::string call;
    std::string evidence;
    ActionOutcome outcome{ActionOutcome::unobserved};
};

struct Report final {
    std::map<std::string, ControlTally> tallies;
    std::vector<Mismatch> mismatches;
    std::map<std::string, ActionCandidate> candidates;
    std::vector<ActionResult> actions;
    std::size_t pages{};
    std::size_t elements{};
    std::size_t controller_relations{};
    std::string stale_check;
    std::string failure;
    // Actions run again alone in a fresh window after their change was not
    // seen in the shared one.
    std::size_t reruns{};
    bool completed{};
};

// The fixed parts of one page's comparison, on the client thread.
struct PageWalk final {
    IUIAutomation& automation;
    IUIAutomationTreeWalker& walker;
    Report& report;
    std::string_view page;
    const PageExpectation& expectation;
    std::set<std::vector<int>> runtime_ids;
    // Whether the page is read from its cache (one request) or live.
    bool cached{};
};

void add_mismatch(PageWalk& walk, const ExpectedNode& expected, std::string_view field,
                  std::string wanted, std::string observed, bool& matched) {
    Mismatch mismatch;
    mismatch.page = std::string(walk.page);
    mismatch.stable_id = expected.node.stable_id;
    mismatch.control_type = expected.control_type;
    mismatch.field = std::string(field);
    mismatch.expected = std::move(wanted);
    mismatch.observed = std::move(observed);
    walk.report.mismatches.push_back(std::move(mismatch));
    matched = false;
}

std::string bool_text(bool value) { return value ? "true" : "false"; }

std::vector<int> runtime_id(IUIAutomationElement& element) {
    std::vector<int> parts;
    SAFEARRAY* array = nullptr;
    if (FAILED(element.GetRuntimeId(&array)) || array == nullptr) return parts;
    LONG lower = 0;
    LONG upper = -1;
    SafeArrayGetLBound(array, 1, &lower);
    SafeArrayGetUBound(array, 1, &upper);
    for (LONG index = lower; index <= upper; ++index) {
        int part = 0;
        SafeArrayGetElement(array, &index, &part);
        parts.push_back(part);
    }
    SafeArrayDestroy(array);
    return parts;
}

// The element's children, in order: on the window element only the
// GUI.Forms ones (the host's non-client parts are the system's).
// With `trace`, which a mismatch report asks for by walking again, each raw
// child's AutomationId and framework (or the failing HRESULT) and how the
// walk ended are appended to it.
std::vector<ElementReference> element_children(PageWalk& walk, IUIAutomationElement& element,
                                               bool window_level, std::string* trace) {
    std::vector<ElementReference> children;
    ElementReference child;
    HRESULT status = walk.walker.GetFirstChildElement(&element, child.receive());
    while (SUCCEEDED(status) && child.get() != nullptr) {
        bool keep = true;
        if (window_level || trace != nullptr) {
            BstrOwner framework;
            const HRESULT read = (*child.get()).get_CurrentFrameworkId(framework.receive());
            if (window_level) keep = SUCCEEDED(read) && framework.wide() == framework_id;
            if (trace != nullptr) {
                BstrOwner id;
                static_cast<void>((*child.get()).get_CurrentAutomationId(id.receive()));
                const std::string seen = SUCCEEDED(read) ? framework.utf8() : hresult_text(read);
                *trace += " [" + id.utf8() + " " + seen + "]";
            }
        }
        ElementReference next;
        status = walk.walker.GetNextSiblingElement(child.get(), next.receive());
        if (keep) children.push_back(std::move(child));
        child = std::move(next);
    }
    if (trace != nullptr) *trace += " end " + hresult_text(status);
    return children;
}

template <typename Pattern>
bool element_pattern(IUIAutomationElement& element, PATTERNID id, ComReference<Pattern>& found) {
    const HRESULT status = element.GetCurrentPatternAs(id, __uuidof(Pattern),
                                                       reinterpret_cast<void**>(found.receive()));
    const bool offered = SUCCEEDED(status) && found.get() != nullptr;
    return offered;
}

bool offers(IUIAutomationElement& element, PATTERNID id) {
    ComReference<IUnknown> pattern;
    const HRESULT status = element.GetCurrentPattern(id, pattern.receive());
    const bool offered = SUCCEEDED(status) && pattern.get() != nullptr;
    return offered;
}

// An integer property, or zero when the element reports none.
int integer_property(IUIAutomationElement& element, PROPERTYID property) {
    VARIANT value;
    VariantInit(&value);
    const HRESULT status = element.GetCurrentPropertyValue(property, &value);
    int result = 0;
    if (SUCCEEDED(status) && value.vt == VT_I4) result = value.lVal;
    VariantClear(&value);
    return result;
}

// A Boolean property, or false when the element reports none.
bool boolean_property(IUIAutomationElement& element, PROPERTYID property) {
    VARIANT value;
    VariantInit(&value);
    const HRESULT status = element.GetCurrentPropertyValue(property, &value);
    bool result = false;
    if (SUCCEEDED(status) && value.vt == VT_BOOL) result = value.boolVal == VARIANT_TRUE;
    VariantClear(&value);
    return result;
}

LONG device_coordinate(double logical, double scale) noexcept {
    const LONG device = static_cast<LONG>(std::lround(logical * scale));
    return device;
}

std::string rect_text(double left, double top, double width, double height) {
    const std::string text = std::to_string(std::lround(left)) + "," + std::to_string(std::lround(top)) +
                             " " + std::to_string(std::lround(width)) + "x" +
                             std::to_string(std::lround(height));
    return text;
}

// The length of `text` in UTF-16 code units.
std::size_t utf16_length(std::string_view text) {
    if (text.empty()) return 0U;
    const int units = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (units <= 0) throw std::runtime_error("a node's text does not convert to UTF-16");
    return static_cast<std::size_t>(units);
}

// Where `endpoint` of `range` lies in `document`'s text, in UTF-16 code
// units: the length of the text from the document's start to it. Returns
// false when a call fails.
bool range_offset(IUIAutomationTextRange& document, IUIAutomationTextRange& range,
                  TextPatternRangeEndpoint endpoint, std::size_t& offset) {
    ComReference<IUIAutomationTextRange> before;
    if (FAILED(document.Clone(before.receive())) || before.get() == nullptr) return false;
    if (FAILED((*before.get()).MoveEndpointByRange(TextPatternRangeEndpoint_End, &range, endpoint))) return false;
    BstrOwner text;
    if (FAILED((*before.get()).GetText(-1, text.receive()))) return false;
    offset = text.wide().size();
    return true;
}

// A text box's TextPattern against its node: the document range's text is
// the value, and the one selection range runs between the selection's ends.
void compare_text(PageWalk& walk, IUIAutomationTextPattern& text, const ExpectedNode& expected,
                  bool& matched) {
    const SemanticNode& node = expected.node;
    ComReference<IUIAutomationTextRange> document;
    BstrOwner whole;
    if (FAILED(text.get_DocumentRange(document.receive())) || document.get() == nullptr ||
        FAILED((*document.get()).GetText(-1, whole.receive()))) {
        add_mismatch(walk, expected, "TextPattern document range", "readable", "refused", matched);
        return;
    }
    if (whole.utf8() != node.value) {
        add_mismatch(walk, expected, "TextPattern text", node.value, whole.utf8(), matched);
    }
    ComReference<IUIAutomationTextRangeArray> ranges;
    int count = 0;
    ComReference<IUIAutomationTextRange> selection;
    if (FAILED(text.GetSelection(ranges.receive())) || ranges.get() == nullptr ||
        FAILED((*ranges.get()).get_Length(&count)) || count != 1 ||
        FAILED((*ranges.get()).GetElement(0, selection.receive())) || selection.get() == nullptr) {
        add_mismatch(walk, expected, "TextPattern selection", "one range", std::to_string(count) + " ranges",
                     matched);
        return;
    }
    const SemanticTextSelection wanted = *node.text_selection;
    const std::size_t low = std::min(wanted.anchor.value(), wanted.caret.value());
    const std::size_t high = std::max(wanted.anchor.value(), wanted.caret.value());
    const std::string_view value(node.value);
    const std::size_t wanted_start = utf16_length(value.substr(0U, low));
    const std::size_t wanted_end = utf16_length(value.substr(0U, high));
    std::size_t start = 0U;
    std::size_t end = 0U;
    const bool read = range_offset(*document.get(), *selection.get(), TextPatternRangeEndpoint_Start, start) &&
                      range_offset(*document.get(), *selection.get(), TextPatternRangeEndpoint_End, end);
    if (!read || start != wanted_start || end != wanted_end) {
        add_mismatch(walk, expected, "TextPattern selection",
                     std::to_string(wanted_start) + ".." + std::to_string(wanted_end),
                     read ? std::to_string(start) + ".." + std::to_string(end) : std::string("refused"),
                     matched);
    }
}

// The patterns the client looks for on every element, in this order: the
// pattern, the property that says whether it is offered (what a cached read
// uses) and its name in a mismatch.
struct PatternOffer final {
    PATTERNID pattern;
    PROPERTYID available;
    const char* name;
};

constexpr std::size_t offer_count = 10;
constexpr std::array<PatternOffer, offer_count> pattern_offers{{
    {UIA_InvokePatternId, UIA_IsInvokePatternAvailablePropertyId, "Invoke pattern"},
    {UIA_TogglePatternId, UIA_IsTogglePatternAvailablePropertyId, "Toggle pattern"},
    {UIA_SelectionItemPatternId, UIA_IsSelectionItemPatternAvailablePropertyId, "SelectionItem pattern"},
    {UIA_SelectionPatternId, UIA_IsSelectionPatternAvailablePropertyId, "Selection pattern"},
    {UIA_RangeValuePatternId, UIA_IsRangeValuePatternAvailablePropertyId, "RangeValue pattern"},
    {UIA_ValuePatternId, UIA_IsValuePatternAvailablePropertyId, "Value pattern"},
    {UIA_ExpandCollapsePatternId, UIA_IsExpandCollapsePatternAvailablePropertyId, "ExpandCollapse pattern"},
    {UIA_GridPatternId, UIA_IsGridPatternAvailablePropertyId, "Grid pattern"},
    {UIA_GridItemPatternId, UIA_IsGridItemPatternAvailablePropertyId, "GridItem pattern"},
    {UIA_TextPatternId, UIA_IsTextPatternAvailablePropertyId, "Text pattern"},
}};

// Indices into pattern_offers.
enum class Offered : std::uint8_t {
    invoke, toggle, selection_item, selection, range_value, value, expand_collapse, grid, grid_item, text
};

// Whether a node expects each of pattern_offers, in its order.
std::array<bool, offer_count> wanted_offers(const ExpectedPatterns& patterns) noexcept {
    const std::array<bool, offer_count> wanted{
        patterns.invoke, patterns.toggle, patterns.selection_item, patterns.selection, patterns.range_value,
        patterns.value, patterns.expand_collapse, patterns.grid, patterns.grid_item, patterns.text,
    };
    return wanted;
}

// The properties a cached page reads, all in one cache request. The base
// properties, then the pattern-state properties; the offers' are added from
// pattern_offers.
constexpr std::array<PROPERTYID, 32> cached_properties{
    UIA_ControlTypePropertyId, UIA_NamePropertyId, UIA_AutomationIdPropertyId, UIA_HelpTextPropertyId,
    UIA_FrameworkIdPropertyId, UIA_IsEnabledPropertyId, UIA_IsKeyboardFocusablePropertyId,
    UIA_HasKeyboardFocusPropertyId, UIA_IsOffscreenPropertyId, UIA_BoundingRectanglePropertyId,
    UIA_PositionInSetPropertyId, UIA_SizeOfSetPropertyId, UIA_LevelPropertyId, UIA_AccessKeyPropertyId,
    UIA_IsDialogPropertyId, UIA_LiveSettingPropertyId, UIA_RuntimeIdPropertyId, UIA_ControllerForPropertyId,
    UIA_ToggleToggleStatePropertyId, UIA_ExpandCollapseExpandCollapseStatePropertyId,
    UIA_SelectionItemIsSelectedPropertyId, UIA_ValueValuePropertyId, UIA_RangeValueValuePropertyId,
    UIA_RangeValueMinimumPropertyId, UIA_RangeValueMaximumPropertyId, UIA_RangeValueSmallChangePropertyId,
    UIA_RangeValueLargeChangePropertyId, UIA_SelectionCanSelectMultiplePropertyId, UIA_GridRowCountPropertyId,
    UIA_GridColumnCountPropertyId, UIA_GridItemRowPropertyId, UIA_GridItemColumnPropertyId,
};

// What the client reads from one element, live (Current* reads and pattern
// objects, one call each, carried to the UI thread) or from the page's
// cache (filled by one request per page). A pattern's state is read only
// when the node expects the pattern and the element offers it.
struct ElementReading final {
    CONTROLTYPEID control_type{};
    std::string name;
    std::string automation_id;
    std::string help_text;
    std::wstring framework;
    std::string framework_name;
    std::string access_key;
    bool enabled{};
    bool focusable{};
    bool focused{};
    bool offscreen{true};
    RECT bounds{};
    int position_in_set{};
    int size_of_set{};
    int level{};
    bool dialog{};
    int live_setting{};
    std::vector<int> runtime_id;
    std::array<bool, offer_count> offered{};
    ToggleState toggle_state{ToggleState_Indeterminate};
    ExpandCollapseState expand_state{ExpandCollapseState_LeafNode};
    bool selected{};
    std::string value;
    double range_value{};
    double range_minimum{};
    double range_maximum{};
    double small_change{};
    double large_change{};
    bool can_select_multiple{};
    int row_count{};
    int column_count{};
    int row{-1};
    int column{-1};
};

bool offered(const ElementReading& reading, Offered which) noexcept {
    const bool is_offered = reading.offered[static_cast<std::size_t>(which)];
    return is_offered;
}

// One VARIANT, cleared when the owner goes.
class VariantOwner final {
public:
    VariantOwner() noexcept { VariantInit(&value_); }
    ~VariantOwner() { VariantClear(&value_); }
    VariantOwner(const VariantOwner&) = delete;
    VariantOwner& operator=(const VariantOwner&) = delete;

    [[nodiscard]] VARIANT* receive() noexcept {
        VariantClear(&value_);
        return &value_;
    }
    [[nodiscard]] const VARIANT& value() const noexcept { return value_; }

private:
    VARIANT value_{};
};

// Reads one cached property into `value`; a failed read leaves it VT_EMPTY.
void cached_value(IUIAutomationElement& element, PROPERTYID property, VariantOwner& value) {
    const HRESULT status = element.GetCachedPropertyValue(property, value.receive());
    if (FAILED(status)) static_cast<void>(value.receive());
}

int cached_integer(IUIAutomationElement& element, PROPERTYID property) {
    VariantOwner value;
    cached_value(element, property, value);
    const int result = value.value().vt == VT_I4 ? value.value().lVal : 0;
    return result;
}

bool cached_boolean(IUIAutomationElement& element, PROPERTYID property) {
    VariantOwner value;
    cached_value(element, property, value);
    const bool result = value.value().vt == VT_BOOL && value.value().boolVal == VARIANT_TRUE;
    return result;
}

double cached_double(IUIAutomationElement& element, PROPERTYID property) {
    VariantOwner value;
    cached_value(element, property, value);
    const double result = value.value().vt == VT_R8 ? value.value().dblVal : 0.0;
    return result;
}

std::string cached_string(IUIAutomationElement& element, PROPERTYID property) {
    VariantOwner value;
    cached_value(element, property, value);
    if (value.value().vt != VT_BSTR || value.value().bstrVal == nullptr) return std::string();
    const BSTR text = value.value().bstrVal;
    const UINT length = SysStringLen(text);
    if (length == 0U) return std::string();
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, text, static_cast<int>(length), nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) throw std::runtime_error("a UI Automation string does not convert to UTF-8");
    std::string result(static_cast<std::size_t>(bytes), '\0');
    static_cast<void>(WideCharToMultiByte(CP_UTF8, 0, text, static_cast<int>(length), result.data(), bytes,
                                          nullptr, nullptr));
    return result;
}

// The cached runtime ID: a VT_ARRAY of VT_I4.
std::vector<int> cached_runtime_id(IUIAutomationElement& element) {
    std::vector<int> parts;
    VariantOwner value;
    cached_value(element, UIA_RuntimeIdPropertyId, value);
    if (value.value().vt != (VT_ARRAY | VT_I4) || value.value().parray == nullptr) return parts;
    SAFEARRAY* array = value.value().parray;
    LONG lower = 0;
    LONG upper = -1;
    SafeArrayGetLBound(array, 1, &lower);
    SafeArrayGetUBound(array, 1, &upper);
    for (LONG index = lower; index <= upper; ++index) {
        int part = 0;
        SafeArrayGetElement(array, &index, &part);
        parts.push_back(part);
    }
    return parts;
}

// Reads `element` live: each Current* property and pattern state is its
// own call.
void read_live(IUIAutomationElement& element, const ExpectedPatterns& patterns, ElementReading& reading) {
    static_cast<void>(element.get_CurrentControlType(&reading.control_type));
    BstrOwner name;
    static_cast<void>(element.get_CurrentName(name.receive()));
    reading.name = name.utf8();
    BstrOwner id;
    static_cast<void>(element.get_CurrentAutomationId(id.receive()));
    reading.automation_id = id.utf8();
    BstrOwner help;
    static_cast<void>(element.get_CurrentHelpText(help.receive()));
    reading.help_text = help.utf8();
    BstrOwner framework;
    static_cast<void>(element.get_CurrentFrameworkId(framework.receive()));
    reading.framework = framework.wide();
    reading.framework_name = framework.utf8();
    BOOL enabled = FALSE;
    BOOL focusable = FALSE;
    BOOL focused = FALSE;
    BOOL offscreen = TRUE;
    static_cast<void>(element.get_CurrentIsEnabled(&enabled));
    static_cast<void>(element.get_CurrentIsKeyboardFocusable(&focusable));
    static_cast<void>(element.get_CurrentHasKeyboardFocus(&focused));
    static_cast<void>(element.get_CurrentIsOffscreen(&offscreen));
    reading.enabled = enabled == TRUE;
    reading.focusable = focusable == TRUE;
    reading.focused = focused == TRUE;
    reading.offscreen = offscreen == TRUE;
    static_cast<void>(element.get_CurrentBoundingRectangle(&reading.bounds));
    reading.position_in_set = integer_property(element, UIA_PositionInSetPropertyId);
    reading.size_of_set = integer_property(element, UIA_SizeOfSetPropertyId);
    reading.level = integer_property(element, UIA_LevelPropertyId);
    BstrOwner key;
    static_cast<void>(element.get_CurrentAccessKey(key.receive()));
    reading.access_key = key.utf8();
    reading.dialog = boolean_property(element, UIA_IsDialogPropertyId);
    reading.live_setting = integer_property(element, UIA_LiveSettingPropertyId);
    reading.runtime_id = runtime_id(element);
    for (std::size_t index = 0; index < offer_count; ++index) {
        reading.offered[index] = offers(element, pattern_offers[index].pattern);
    }
    ComReference<IUIAutomationTogglePattern> toggle;
    if (patterns.toggle && element_pattern(element, UIA_TogglePatternId, toggle)) {
        static_cast<void>((*toggle.get()).get_CurrentToggleState(&reading.toggle_state));
    }
    ComReference<IUIAutomationExpandCollapsePattern> expand;
    if (patterns.expand_collapse && element_pattern(element, UIA_ExpandCollapsePatternId, expand)) {
        static_cast<void>((*expand.get()).get_CurrentExpandCollapseState(&reading.expand_state));
    }
    ComReference<IUIAutomationSelectionItemPattern> item;
    if (patterns.selection_item && element_pattern(element, UIA_SelectionItemPatternId, item)) {
        BOOL selected = FALSE;
        static_cast<void>((*item.get()).get_CurrentIsSelected(&selected));
        reading.selected = selected == TRUE;
    }
    ComReference<IUIAutomationValuePattern> value;
    if (patterns.value && element_pattern(element, UIA_ValuePatternId, value)) {
        BstrOwner text;
        static_cast<void>((*value.get()).get_CurrentValue(text.receive()));
        reading.value = text.utf8();
    }
    ComReference<IUIAutomationRangeValuePattern> range;
    if (patterns.range_value && element_pattern(element, UIA_RangeValuePatternId, range)) {
        static_cast<void>((*range.get()).get_CurrentValue(&reading.range_value));
        static_cast<void>((*range.get()).get_CurrentMinimum(&reading.range_minimum));
        static_cast<void>((*range.get()).get_CurrentMaximum(&reading.range_maximum));
        static_cast<void>((*range.get()).get_CurrentSmallChange(&reading.small_change));
        static_cast<void>((*range.get()).get_CurrentLargeChange(&reading.large_change));
    }
    ComReference<IUIAutomationSelectionPattern> selection;
    if (patterns.selection && element_pattern(element, UIA_SelectionPatternId, selection)) {
        BOOL multiple = FALSE;
        static_cast<void>((*selection.get()).get_CurrentCanSelectMultiple(&multiple));
        reading.can_select_multiple = multiple == TRUE;
    }
    ComReference<IUIAutomationGridPattern> grid;
    if (patterns.grid && element_pattern(element, UIA_GridPatternId, grid)) {
        static_cast<void>((*grid.get()).get_CurrentRowCount(&reading.row_count));
        static_cast<void>((*grid.get()).get_CurrentColumnCount(&reading.column_count));
    }
    ComReference<IUIAutomationGridItemPattern> grid_item;
    if (patterns.grid_item && element_pattern(element, UIA_GridItemPatternId, grid_item)) {
        static_cast<void>((*grid_item.get()).get_CurrentRow(&reading.row));
        static_cast<void>((*grid_item.get()).get_CurrentColumn(&reading.column));
    }
}

// Reads `element` from its cache, which the page's one request filled with
// cached_properties and the offers' properties.
void read_cached(IUIAutomationElement& element, const ExpectedPatterns& patterns, ElementReading& reading) {
    static_cast<void>(element.get_CachedControlType(&reading.control_type));
    BstrOwner name;
    static_cast<void>(element.get_CachedName(name.receive()));
    reading.name = name.utf8();
    BstrOwner id;
    static_cast<void>(element.get_CachedAutomationId(id.receive()));
    reading.automation_id = id.utf8();
    BstrOwner help;
    static_cast<void>(element.get_CachedHelpText(help.receive()));
    reading.help_text = help.utf8();
    BstrOwner framework;
    static_cast<void>(element.get_CachedFrameworkId(framework.receive()));
    reading.framework = framework.wide();
    reading.framework_name = framework.utf8();
    BOOL enabled = FALSE;
    BOOL focusable = FALSE;
    BOOL focused = FALSE;
    BOOL offscreen = TRUE;
    static_cast<void>(element.get_CachedIsEnabled(&enabled));
    static_cast<void>(element.get_CachedIsKeyboardFocusable(&focusable));
    static_cast<void>(element.get_CachedHasKeyboardFocus(&focused));
    static_cast<void>(element.get_CachedIsOffscreen(&offscreen));
    reading.enabled = enabled == TRUE;
    reading.focusable = focusable == TRUE;
    reading.focused = focused == TRUE;
    reading.offscreen = offscreen == TRUE;
    static_cast<void>(element.get_CachedBoundingRectangle(&reading.bounds));
    reading.position_in_set = cached_integer(element, UIA_PositionInSetPropertyId);
    reading.size_of_set = cached_integer(element, UIA_SizeOfSetPropertyId);
    reading.level = cached_integer(element, UIA_LevelPropertyId);
    BstrOwner key;
    static_cast<void>(element.get_CachedAccessKey(key.receive()));
    reading.access_key = key.utf8();
    reading.dialog = cached_boolean(element, UIA_IsDialogPropertyId);
    reading.live_setting = cached_integer(element, UIA_LiveSettingPropertyId);
    reading.runtime_id = cached_runtime_id(element);
    for (std::size_t index = 0; index < offer_count; ++index) {
        reading.offered[index] = cached_boolean(element, pattern_offers[index].available);
    }
    if (patterns.toggle && offered(reading, Offered::toggle)) {
        reading.toggle_state = static_cast<ToggleState>(cached_integer(element, UIA_ToggleToggleStatePropertyId));
    }
    if (patterns.expand_collapse && offered(reading, Offered::expand_collapse)) {
        const int state = cached_integer(element, UIA_ExpandCollapseExpandCollapseStatePropertyId);
        reading.expand_state = static_cast<ExpandCollapseState>(state);
    }
    if (patterns.selection_item && offered(reading, Offered::selection_item)) {
        reading.selected = cached_boolean(element, UIA_SelectionItemIsSelectedPropertyId);
    }
    if (patterns.value && offered(reading, Offered::value)) {
        reading.value = cached_string(element, UIA_ValueValuePropertyId);
    }
    if (patterns.range_value && offered(reading, Offered::range_value)) {
        reading.range_value = cached_double(element, UIA_RangeValueValuePropertyId);
        reading.range_minimum = cached_double(element, UIA_RangeValueMinimumPropertyId);
        reading.range_maximum = cached_double(element, UIA_RangeValueMaximumPropertyId);
        reading.small_change = cached_double(element, UIA_RangeValueSmallChangePropertyId);
        reading.large_change = cached_double(element, UIA_RangeValueLargeChangePropertyId);
    }
    if (patterns.selection && offered(reading, Offered::selection)) {
        reading.can_select_multiple = cached_boolean(element, UIA_SelectionCanSelectMultiplePropertyId);
    }
    if (patterns.grid && offered(reading, Offered::grid)) {
        reading.row_count = cached_integer(element, UIA_GridRowCountPropertyId);
        reading.column_count = cached_integer(element, UIA_GridColumnCountPropertyId);
    }
    if (patterns.grid_item && offered(reading, Offered::grid_item)) {
        reading.row = cached_integer(element, UIA_GridItemRowPropertyId);
        reading.column = cached_integer(element, UIA_GridItemColumnPropertyId);
    }
}

// The cache request a cached page is read with: every property above, the
// raw view, the whole subtree.
ComReference<IUIAutomationCacheRequest> make_cache_request(IUIAutomation& automation) {
    ComReference<IUIAutomationCacheRequest> request;
    if (FAILED(automation.CreateCacheRequest(request.receive())) || request.get() == nullptr) {
        throw std::runtime_error("UI Automation creates no cache request");
    }
    IUIAutomationCacheRequest& cache = *request.get();
    for (const PROPERTYID property : cached_properties) static_cast<void>(cache.AddProperty(property));
    for (const PatternOffer& offer : pattern_offers) static_cast<void>(cache.AddProperty(offer.available));
    ComReference<IUIAutomationCondition> raw;
    if (FAILED(automation.get_RawViewCondition(raw.receive())) || raw.get() == nullptr) {
        throw std::runtime_error("UI Automation has no raw view condition");
    }
    if (FAILED(cache.put_TreeFilter(raw.get())) || FAILED(cache.put_TreeScope(TreeScope_Subtree))) {
        throw std::runtime_error("the cache request does not take the raw view's subtree");
    }
    return request;
}

void compare_patterns(PageWalk& walk, IUIAutomationElement& element, const ElementReading& reading,
                      const ExpectedNode& expected, bool& matched) {
    const ExpectedPatterns& patterns = expected.patterns;
    const SemanticNode& node = expected.node;
    const std::array<bool, offer_count> wanted_offer = wanted_offers(patterns);
    for (std::size_t index = 0; index < offer_count; ++index) {
        if (reading.offered[index] != wanted_offer[index]) {
            add_mismatch(walk, expected, pattern_offers[index].name, bool_text(wanted_offer[index]),
                         bool_text(reading.offered[index]), matched);
        }
    }
    if (patterns.toggle && offered(reading, Offered::toggle) &&
        reading.toggle_state != expected_toggle_state(node)) {
        add_mismatch(walk, expected, "ToggleState", std::to_string(static_cast<int>(expected_toggle_state(node))),
                     std::to_string(static_cast<int>(reading.toggle_state)), matched);
    }
    if (patterns.expand_collapse && offered(reading, Offered::expand_collapse) &&
        reading.expand_state != expected_expand_state(node)) {
        add_mismatch(walk, expected, "ExpandCollapseState",
                     std::to_string(static_cast<int>(expected_expand_state(node))),
                     std::to_string(static_cast<int>(reading.expand_state)), matched);
    }
    if (patterns.selection_item && offered(reading, Offered::selection_item) &&
        reading.selected != expected_selected(node)) {
        add_mismatch(walk, expected, "IsSelected", bool_text(expected_selected(node)), bool_text(reading.selected),
                     matched);
    }
    if (patterns.value && offered(reading, Offered::value) && reading.value != node.value) {
        add_mismatch(walk, expected, "Value", node.value, reading.value, matched);
    }
    // TextPattern's ranges are always read live: they are objects, not
    // properties a cache holds.
    ComReference<IUIAutomationTextPattern> text;
    if (patterns.text && offered(reading, Offered::text) && element_pattern(element, UIA_TextPatternId, text)) {
        compare_text(walk, *text.get(), expected, matched);
    }
    if (patterns.range_value && offered(reading, Offered::range_value)) {
        const double wanted = *node.numeric_value;
        const double wanted_minimum = node.minimum_value ? *node.minimum_value : wanted;
        const double wanted_maximum = node.maximum_value ? *node.maximum_value : wanted;
        if (reading.range_value != wanted || reading.range_minimum != wanted_minimum ||
            reading.range_maximum != wanted_maximum) {
            add_mismatch(walk, expected, "RangeValue",
                         std::to_string(wanted) + " in [" + std::to_string(wanted_minimum) + ", " +
                             std::to_string(wanted_maximum) + "]",
                         std::to_string(reading.range_value) + " in [" + std::to_string(reading.range_minimum) +
                             ", " + std::to_string(reading.range_maximum) + "]",
                         matched);
        }
        // The steps (12.13); zero for a node that publishes none.
        const double wanted_small = node.small_step.value_or(0.0);
        const double wanted_large = node.large_step.value_or(0.0);
        if (reading.small_change != wanted_small || reading.large_change != wanted_large) {
            add_mismatch(walk, expected, "SmallChange/LargeChange",
                         std::to_string(wanted_small) + "/" + std::to_string(wanted_large),
                         std::to_string(reading.small_change) + "/" + std::to_string(reading.large_change),
                         matched);
        }
    }
    if (patterns.selection && offered(reading, Offered::selection)) {
        const bool wanted = has_semantic_state(node.states, SemanticState::multiselectable);
        if (reading.can_select_multiple != wanted) {
            add_mismatch(walk, expected, "CanSelectMultiple", bool_text(wanted),
                         bool_text(reading.can_select_multiple), matched);
        }
    }
    if (patterns.grid && offered(reading, Offered::grid)) {
        const int wanted_rows = static_cast<int>(*node.row_count);
        const int wanted_columns = static_cast<int>(*node.column_count);
        if (reading.row_count != wanted_rows || reading.column_count != wanted_columns) {
            add_mismatch(walk, expected, "RowCount/ColumnCount",
                         std::to_string(wanted_rows) + "/" + std::to_string(wanted_columns),
                         std::to_string(reading.row_count) + "/" + std::to_string(reading.column_count), matched);
        }
    }
    if (patterns.grid_item && offered(reading, Offered::grid_item)) {
        // UI Automation counts from zero; the tree from one.
        const int wanted_row = static_cast<int>(*node.row_index) - 1;
        const int wanted_column = static_cast<int>(*node.column_index) - 1;
        if (reading.row != wanted_row || reading.column != wanted_column) {
            add_mismatch(walk, expected, "Row/Column", std::to_string(wanted_row) + "/" + std::to_string(wanted_column),
                         std::to_string(reading.row) + "/" + std::to_string(reading.column), matched);
        }
    }
}

// The stage A properties (12.13): Level, AccessKey ("Alt+" and the
// mnemonic), IsDialog and LiveSetting (polite for a live region).
void compare_vocabulary_properties(PageWalk& walk, const ElementReading& reading, const ExpectedNode& expected,
                                   bool& matched) {
    const SemanticNode& node = expected.node;
    const int wanted_level = node.level ? static_cast<int>(*node.level) : 0;
    if (reading.level != wanted_level) {
        add_mismatch(walk, expected, "Level", std::to_string(wanted_level), std::to_string(reading.level), matched);
    }
    const std::string wanted_key = node.access_key.empty() ? std::string{} : "Alt+" + node.access_key;
    if (reading.access_key != wanted_key) {
        add_mismatch(walk, expected, "AccessKey", wanted_key, reading.access_key, matched);
    }
    const bool wanted_dialog = node.role == SemanticRole::dialog;
    if (reading.dialog != wanted_dialog) {
        add_mismatch(walk, expected, "IsDialog", bool_text(wanted_dialog), bool_text(reading.dialog), matched);
    }
    const int wanted_live = node.role == SemanticRole::live_region ? static_cast<int>(Polite) : 0;
    if (reading.live_setting != wanted_live) {
        add_mismatch(walk, expected, "LiveSetting", std::to_string(wanted_live),
                     std::to_string(reading.live_setting), matched);
    }
}

// Whether the expectation holds a control-backed node with `stable_id`.
bool holds_control(const ExpectedNode& at, std::string_view stable_id) noexcept {
    for (const ExpectedNode& child : at.children) {
        if (!control_backed(child.node)) continue;
        if (child.node.stable_id == stable_id) return true;
        if (holds_control(child, stable_id)) return true;
    }
    return false;
}

void compare_controller(PageWalk& walk, IUIAutomationElement& element, const ExpectedNode& expected,
                        bool& matched) {
    const std::string& panel = expected.node.controlled_panel;
    // A hidden page's panel is not in the tree, and no relation is published.
    const bool present = !panel.empty() && holds_control(walk.expectation.client, panel);
    ComReference<IUIAutomationElementArray> controlled;
    HRESULT status = S_OK;
    if (walk.cached) {
        status = element.get_CachedControllerFor(controlled.receive());
    } else {
        status = element.get_CurrentControllerFor(controlled.receive());
    }
    int count = 0;
    if (SUCCEEDED(status) && controlled.get() != nullptr) {
        static_cast<void>((*controlled.get()).get_Length(&count));
    }
    const int wanted = present ? 1 : 0;
    if (count != wanted) {
        add_mismatch(walk, expected, "ControllerFor count", std::to_string(wanted),
                     std::to_string(count), matched);
        return;
    }
    if (!present) return;
    ElementReference target;
    static_cast<void>((*controlled.get()).GetElement(0, target.receive()));
    BstrOwner target_id;
    if (target.get() != nullptr) static_cast<void>((*target.get()).get_CurrentAutomationId(target_id.receive()));
    if (target_id.utf8() != panel) {
        add_mismatch(walk, expected, "ControllerFor", panel, target_id.utf8(), matched);
        return;
    }
    ++walk.report.controller_relations;
}

// The key of a node's action: its role, the pattern call and its placement.
// The provider routes an action by exactly these, so one action per key
// covers the mapping.
std::string action_key(const ExpectedNode& expected, DefaultKind kind) {
    const char* placement = "own node";
    if (expected.is_item && expected.scope_id.empty()) placement = "unscoped item";
    if (expected.is_item && !expected.scope_id.empty()) placement = "item";
    const PatternCall call = pattern_call(expected.node, kind);
    std::string key = semantic_role_name(expected.node.role);
    key += " / ";
    key += pattern_call_name(call);
    key += " / ";
    key += placement;
    return key;
}

void consider_candidate(PageWalk& walk, const ExpectedNode& expected) {
    const SemanticNode& node = expected.node;
    if (walk.page == host_services_page) return;
    if (!has_semantic_state(node.states, SemanticState::enabled)) return;
    const DefaultKind kind = expected_default(node);
    const bool focus_only = kind == DefaultKind::none &&
                            has_semantic_state(node.states, SemanticState::focusable);
    if (kind == DefaultKind::none && !focus_only) return;
    const std::string key = action_key(expected, kind);
    bool observable = true;
    if (kind == DefaultKind::select) observable = !has_semantic_state(node.states, SemanticState::selected);
    if (kind == DefaultKind::press && node.role == SemanticRole::radio_button) {
        observable = !has_semantic_state(node.states, SemanticState::checked);
    }
    if (kind == DefaultKind::none) observable = !has_semantic_state(node.states, SemanticState::focused);
    std::map<std::string, ActionCandidate>::iterator existing = walk.report.candidates.find(key);
    if (existing != walk.report.candidates.end()) {
        if ((*existing).second.observable || !observable) return;
    }
    ActionCandidate candidate;
    candidate.page = std::string(walk.page);
    candidate.scope_id = expected.scope_id;
    candidate.stable_id = node.stable_id;
    candidate.control_type = expected.control_type;
    candidate.role = node.role;
    candidate.observable = observable;
    walk.report.candidates[key] = std::move(candidate);
}

// The cached children of `element`, in order: on the window element only
// the GUI.Forms ones (the host's non-client parts are the system's).
std::vector<ElementReference> cached_children(IUIAutomationElement& element, bool window_level) {
    std::vector<ElementReference> children;
    ComReference<IUIAutomationElementArray> array;
    if (FAILED(element.GetCachedChildren(array.receive())) || array.get() == nullptr) return children;
    int length = 0;
    static_cast<void>((*array.get()).get_Length(&length));
    for (int index = 0; index < length; ++index) {
        ElementReference child;
        if (FAILED((*array.get()).GetElement(index, child.receive())) || child.get() == nullptr) continue;
        if (window_level) {
            BstrOwner framework;
            const HRESULT read = (*child.get()).get_CachedFrameworkId(framework.receive());
            if (FAILED(read) || framework.wide() != framework_id) continue;
        }
        children.push_back(std::move(child));
    }
    return children;
}

// Compares one element with its expected node, then descends. `window`
// says that `element` is the window element, whose node is the client.
void compare_element(PageWalk& walk, IUIAutomationElement& element, const ExpectedNode& expected,
                     bool window) {
    ++walk.report.elements;
    bool matched = true;
    const SemanticNode& node = expected.node;
    if (!window) {
        ElementReading reading;
        if (walk.cached) {
            read_cached(element, expected.patterns, reading);
        } else {
            read_live(element, expected.patterns, reading);
        }
        const CONTROLTYPEID wanted = expected_control_type(node.role);
        if (reading.control_type != wanted) {
            add_mismatch(walk, expected, "ControlType", std::to_string(static_cast<int>(wanted)),
                         std::to_string(static_cast<int>(reading.control_type)), matched);
        }
        if (reading.name != node.name) add_mismatch(walk, expected, "Name", node.name, reading.name, matched);
        if (reading.automation_id != node.stable_id) {
            add_mismatch(walk, expected, "AutomationId", node.stable_id, reading.automation_id, matched);
        }
        if (reading.help_text != node.description) {
            add_mismatch(walk, expected, "HelpText", node.description, reading.help_text, matched);
        }
        if (reading.framework != framework_id) {
            add_mismatch(walk, expected, "FrameworkId", "GUI.Forms", reading.framework_name, matched);
        }
        const bool wanted_enabled = has_semantic_state(node.states, SemanticState::enabled);
        const bool wanted_focusable = has_semantic_state(node.states, SemanticState::focusable);
        const bool wanted_focused = has_semantic_state(node.states, SemanticState::focused);
        const bool wanted_offscreen = !has_semantic_state(node.states, SemanticState::visible);
        if (reading.enabled != wanted_enabled) {
            add_mismatch(walk, expected, "IsEnabled", bool_text(wanted_enabled), bool_text(reading.enabled), matched);
        }
        if (reading.focusable != wanted_focusable) {
            add_mismatch(walk, expected, "IsKeyboardFocusable", bool_text(wanted_focusable),
                         bool_text(reading.focusable), matched);
        }
        if (reading.focused != wanted_focused) {
            add_mismatch(walk, expected, "HasKeyboardFocus", bool_text(wanted_focused), bool_text(reading.focused),
                         matched);
        }
        if (reading.offscreen != wanted_offscreen) {
            add_mismatch(walk, expected, "IsOffscreen", bool_text(wanted_offscreen), bool_text(reading.offscreen),
                         matched);
        }
        const RECT& bounds = reading.bounds;
        const double scale = walk.expectation.scale;
        LONG left = walk.expectation.origin.x + device_coordinate(node.bounds.x, scale);
        LONG top = walk.expectation.origin.y + device_coordinate(node.bounds.y, scale);
        LONG width = device_coordinate(node.bounds.width, scale);
        LONG height = device_coordinate(node.bounds.height, scale);
        // A node with no area (an overflowed tab, a gallery item scrolled
        // out, a label squeezed to no width) reads as the empty rectangle.
        if (width == 0 || height == 0) {
            left = 0;
            top = 0;
            width = 0;
            height = 0;
        }
        const bool located = bounds.left == left && bounds.top == top &&
                             bounds.right - bounds.left == width && bounds.bottom - bounds.top == height;
        if (!located) {
            add_mismatch(walk, expected, "BoundingRectangle", rect_text(left, top, width, height),
                         rect_text(bounds.left, bounds.top, bounds.right - bounds.left,
                                   bounds.bottom - bounds.top),
                         matched);
        }
        const int position = reading.position_in_set;
        const int size = reading.size_of_set;
        const bool positioned = node.position_in_set.has_value() && node.set_size.has_value();
        const int wanted_position = positioned ? static_cast<int>(*node.position_in_set) : 0;
        const int wanted_size = positioned ? static_cast<int>(*node.set_size) : 0;
        // UI Automation reports a missing position as zero or -1.
        const bool position_matches = positioned ? position == wanted_position && size == wanted_size
                                                 : position <= 0 && size <= 0;
        if (!position_matches) {
            add_mismatch(walk, expected, "PositionInSet/SizeOfSet",
                         std::to_string(wanted_position) + "/" + std::to_string(wanted_size),
                         std::to_string(position) + "/" + std::to_string(size), matched);
        }
        compare_patterns(walk, element, reading, expected, matched);
        compare_vocabulary_properties(walk, reading, expected, matched);
        if (node.role == SemanticRole::tab) compare_controller(walk, element, expected, matched);
        const std::vector<int>& identity = reading.runtime_id;
        if (identity.empty() || !walk.runtime_ids.insert(identity).second) {
            add_mismatch(walk, expected, "RuntimeId", "unique", identity.empty() ? "none" : "repeated",
                         matched);
        }
        ControlTally& tally = walk.report.tallies[expected.control_type];
        if (expected.is_item) {
            ++tally.item_nodes;
        } else {
            ++tally.own_nodes;
        }
        if (matched) {
            ++tally.matched;
        } else {
            ++tally.mismatched;
        }
        consider_candidate(walk, expected);
    }
    std::vector<ElementReference> children;
    if (walk.cached) {
        children = cached_children(element, window);
    } else {
        children = element_children(walk, element, window, nullptr);
    }
    if (children.size() != expected.children.size()) {
        // The trace walks again live, whichever way the page was read.
        std::string trace;
        static_cast<void>(element_children(walk, element, window, &trace));
        std::string wanted = std::to_string(expected.children.size()) + ":";
        for (const ExpectedNode& child : expected.children) wanted += " " + child.node.stable_id;
        bool counted = true;
        add_mismatch(walk, expected, "child count", wanted,
                     std::to_string(children.size()) + ":" + trace, counted);
    }
    const std::size_t walked = std::min(children.size(), expected.children.size());
    for (std::size_t index = 0; index < walked; ++index) {
        IUIAutomationElement& child = *children[index].get();
        // The parent link leads back to this element. It is read live on
        // every page: a cache holds the walk's own structure, not the
        // provider's parent navigation.
        ElementReference parent;
        BOOL same = FALSE;
        const HRESULT read = walk.walker.GetParentElement(&child, parent.receive());
        if (SUCCEEDED(read) && parent.get() != nullptr) {
            static_cast<void>(walk.automation.CompareElements(parent.get(), &element, &same));
        }
        if (same != TRUE) {
            bool linked = true;
            add_mismatch(walk, expected.children[index], "parent", "this element's parent",
                         "another element", linked);
        }
        compare_element(walk, child, expected.children[index], false);
    }
}

// -- actions ----------------------------------------------------------------------

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

struct ClickCounter final {
    int count{};
    void clicked(ButtonBase&) { ++count; }
};

struct InvocationCounter final {
    int count{};
    void invoked(const CommandInvocation&) { ++count; }
};

// The node a candidate names (its scope and stable ID) in `nodes`, or null.
const SemanticNode* find_named(const std::vector<SemanticNode>& nodes, std::string_view scope_id,
                               std::string_view stable_id, std::string_view current_scope) {
    for (const SemanticNode& node : nodes) {
        std::string_view node_scope = current_scope;
        if (control_backed(node)) node_scope = node.stable_id;
        if (node.stable_id == stable_id && node_scope == scope_id) return &node;
        const SemanticNode* found = find_named(node.children, scope_id, stable_id, node_scope);
        if (found != nullptr) return found;
    }
    return nullptr;
}
const SemanticNode* find_named(std::vector<SemanticNode>&&, std::string_view, std::string_view,
                               std::string_view) = delete;

// The state one action starts from, captured on the UI thread, and the
// counters that observe its effects.
struct ActionState final {
    SemanticSnapshot before;
    std::optional<SemanticNode> node;
    std::uint64_t generation_before{};
    std::size_t scopes_before{};
    ClickCounter clicks;
    InvocationCounter invocations;
    SubscriptionToken click_subscription;
    SubscriptionToken invocation_subscription;
};

// Ends an action's observation subscriptions on the UI thread, which owns
// the events they connect to.
struct ReleaseSubscriptions final {
    ActionState* state{};
    void operator()() const {
        (*state).click_subscription = SubscriptionToken{};
        (*state).invocation_subscription = SubscriptionToken{};
    }
};

struct BeginAction final {
    Window* window{};
    const ActionCandidate* candidate{};
    ActionState* state{};
    void operator()() const {
        ActionState& out = *state;
        out.before = (*window).semantic_snapshot();
        const SemanticNode* found = find_named(out.before.roots, (*candidate).scope_id,
                                               (*candidate).stable_id, std::string_view());
        out.node.reset();
        if (found == nullptr) return;
        out.node = *found;
        out.generation_before = (*window).semantic_generation();
        out.scopes_before = (*window).focus_scope_depth();
        const Control::Ptr control = (*window).find((*found).stable_id);
        const std::shared_ptr<ButtonBase> button = std::dynamic_pointer_cast<ButtonBase>(control);
        if (button) {
            out.click_subscription = (*button).clicked().subscribe(
                Delegate<ButtonBase&>::bind<ClickCounter, &ClickCounter::clicked>(out.clicks));
        }
        const std::shared_ptr<RibbonColorPicker> picker = std::dynamic_pointer_cast<RibbonColorPicker>(control);
        if (picker && (*picker).command()) {
            const std::shared_ptr<Command>& command = (*(*picker).command()).command();
            out.invocation_subscription = (*command).invoked().subscribe(
                Delegate<const CommandInvocation&>::bind<InvocationCounter, &InvocationCounter::invoked>(
                    out.invocations));
        }
    }
};

bool popup_opened(const SemanticSnapshot& before, const SemanticSnapshot& after, std::size_t scopes_before,
                  std::size_t scopes_after) noexcept {
    const bool opened = after.roots.size() > before.roots.size() || after.node_count > before.node_count ||
                        scopes_after > scopes_before;
    return opened;
}

// Judges an action's effect from the state it started from, on the UI
// thread (A-W1's rules).
struct EndAction final {
    Window* window{};
    const ActionCandidate* candidate{};
    ActionState* state{};
    ActionResult* result{};
    // Filled for a reversible action: whether it changed and which states
    // the undo must restore.
    bool* reversible{};
    void operator()() const {
        ActionState& from = *state;
        ActionResult& out = *result;
        const SemanticNode& node = *from.node;
        const DefaultKind kind = expected_default(node);
        const SemanticSnapshot after = (*window).semantic_snapshot();
        const std::size_t scopes_after = (*window).focus_scope_depth();
        const SemanticNode* changed = find_named(after.roots, (*candidate).scope_id, (*candidate).stable_id,
                                                 std::string_view());
        const bool checked_before = has_semantic_state(node.states, SemanticState::checked);
        const bool mixed_before = has_semantic_state(node.states, SemanticState::mixed);
        const bool expanded_before = has_semantic_state(node.states, SemanticState::expanded);
        const bool opened = popup_opened(from.before, after, from.scopes_before, scopes_after);
        out.outcome = ActionOutcome::unobserved;
        if (kind == DefaultKind::none) {
            if (changed != nullptr && has_semantic_state((*changed).states, SemanticState::focused)) {
                out.outcome = ActionOutcome::changed;
                out.evidence = "focused";
            }
        } else if (kind == DefaultKind::select ||
                   (kind == DefaultKind::press && node.role == SemanticRole::radio_button)) {
            if (changed != nullptr && expected_selected(*changed)) {
                out.outcome = ActionOutcome::changed;
                out.evidence = node.role == SemanticRole::radio_button ? "checked" : "selected";
            }
        } else if (kind == DefaultKind::expand || kind == DefaultKind::collapse) {
            if (changed != nullptr &&
                has_semantic_state((*changed).states, SemanticState::expanded) != expanded_before) {
                out.outcome = ActionOutcome::changed;
                out.evidence = expanded_before ? "collapsed" : "expanded";
            } else if (kind == DefaultKind::expand && opened) {
                out.outcome = ActionOutcome::changed;
                out.evidence = "a popup opened";
            }
        } else if (kind == DefaultKind::show_menu) {
            if (opened) {
                out.outcome = ActionOutcome::changed;
                out.evidence = "a popup opened";
            } else if (changed != nullptr && has_semantic_state((*changed).states, SemanticState::expanded)) {
                out.outcome = ActionOutcome::changed;
                out.evidence = "expanded";
            }
        } else if (changed != nullptr &&
                   (has_semantic_state((*changed).states, SemanticState::checked) != checked_before ||
                    has_semantic_state((*changed).states, SemanticState::mixed) != mixed_before)) {
            out.outcome = ActionOutcome::changed;
            out.evidence = has_semantic_state((*changed).states, SemanticState::checked) ? "checked" : "unchecked";
        } else if (from.clicks.count == 1) {
            out.outcome = ActionOutcome::changed;
            out.evidence = "clicked raised once";
        } else if (from.invocations.count == 1) {
            out.outcome = ActionOutcome::changed;
            out.evidence = "its command invoked once";
        } else if (opened) {
            out.outcome = ActionOutcome::changed;
            out.evidence = "a popup opened";
        } else if ((*window).semantic_generation() != from.generation_before) {
            out.outcome = ActionOutcome::changed;
            out.evidence = "the semantic tree changed";
        }
        if (from.clicks.count > 1) {
            out.outcome = ActionOutcome::unobserved;
            out.evidence = "clicked raised " + std::to_string(from.clicks.count) + " times";
        }
        // An expansion or a toggle is undone through the same pattern,
        // which proves the reverse too; before any popup closes.
        *reversible = out.outcome == ActionOutcome::changed &&
                      (kind == DefaultKind::expand || kind == DefaultKind::collapse ||
                       (kind == DefaultKind::press && expects_toggle(node))) &&
                      changed != nullptr && out.evidence != "a popup opened";
        ReleaseSubscriptions{state}();
    }
};

// Whether the candidate's expanded and checked states are back to the ones
// the action started from, on the UI thread.
struct CheckRestored final {
    Window* window{};
    const ActionCandidate* candidate{};
    const SemanticNode* before{};
    bool* restored{};
    void operator()() const {
        const SemanticSnapshot now = (*window).semantic_snapshot();
        const SemanticNode* node = find_named(now.roots, (*candidate).scope_id, (*candidate).stable_id,
                                              std::string_view());
        *restored = false;
        if (node == nullptr) return;
        const bool expanded = has_semantic_state((*node).states, SemanticState::expanded) ==
                              has_semantic_state((*before).states, SemanticState::expanded);
        const bool checked = has_semantic_state((*node).states, SemanticState::checked) ==
                             has_semantic_state((*before).states, SemanticState::checked);
        *restored = expanded && checked;
    }
};

// Closes popups an action opened, so the page's root set is restored, on
// the UI thread.
struct ClosePopups final {
    Window* window{};
    std::size_t baseline_roots{};
    void operator()() const {
        for (int attempt = 0; attempt < maximum_popup_closes; ++attempt) {
            const SemanticSnapshot snapshot = (*window).semantic_snapshot();
            const bool open = snapshot.roots.size() > baseline_roots || (*window).focus_scope_depth() != 0U;
            if (!open) return;
            press_escape(*window);
        }
    }
};

struct DoNothing final {
    void operator()() const {}
};


// The element a candidate names: the path of child positions to its node
// in the window's tree, then the same path through UI Automation.
bool find_path(const std::vector<SemanticNode>& nodes, std::string_view scope_id, std::string_view stable_id,
               std::string_view current_scope, std::vector<std::size_t>& path) {
    for (std::size_t index = 0; index < nodes.size(); ++index) {
        const SemanticNode& node = nodes[index];
        std::string_view node_scope = current_scope;
        if (control_backed(node)) node_scope = node.stable_id;
        path.push_back(index);
        if (node.stable_id == stable_id && node_scope == scope_id) return true;
        if (find_path(node.children, scope_id, stable_id, node_scope, path)) return true;
        path.pop_back();
    }
    return false;
}

struct FindCandidatePath final {
    Window* window{};
    const ActionCandidate* candidate{};
    std::vector<std::size_t>* path{};
    bool* found{};
    void operator()() const {
        const SemanticSnapshot snapshot = (*window).semantic_snapshot();
        (*path).clear();
        *found = find_path(snapshot.roots, (*candidate).scope_id, (*candidate).stable_id, std::string_view(),
                           *path);
    }
};

// Calls one pattern method on `element`. Returns its HRESULT, or
// E_NOINTERFACE when the element does not offer the pattern.
HRESULT call_pattern(IUIAutomationElement& element, PatternCall call) {
    switch (call) {
    case PatternCall::set_focus: return element.SetFocus();
    case PatternCall::invoke: {
        ComReference<IUIAutomationInvokePattern> invoke;
        if (!element_pattern(element, UIA_InvokePatternId, invoke)) return E_NOINTERFACE;
        return (*invoke.get()).Invoke();
    }
    case PatternCall::toggle: {
        ComReference<IUIAutomationTogglePattern> toggle;
        if (!element_pattern(element, UIA_TogglePatternId, toggle)) return E_NOINTERFACE;
        return (*toggle.get()).Toggle();
    }
    case PatternCall::select: {
        ComReference<IUIAutomationSelectionItemPattern> item;
        if (!element_pattern(element, UIA_SelectionItemPatternId, item)) return E_NOINTERFACE;
        return (*item.get()).Select();
    }
    case PatternCall::expand: {
        ComReference<IUIAutomationExpandCollapsePattern> expand;
        if (!element_pattern(element, UIA_ExpandCollapsePatternId, expand)) return E_NOINTERFACE;
        return (*expand.get()).Expand();
    }
    case PatternCall::collapse: {
        ComReference<IUIAutomationExpandCollapsePattern> expand;
        if (!element_pattern(element, UIA_ExpandCollapsePatternId, expand)) return E_NOINTERFACE;
        return (*expand.get()).Collapse();
    }
    }
    return E_NOTIMPL;
}

// The reverse of a reversible call: Toggle again, or the opposite of an
// expansion.
PatternCall undo_call(PatternCall call) noexcept {
    if (call == PatternCall::expand) return PatternCall::collapse;
    if (call == PatternCall::collapse) return PatternCall::expand;
    return call;
}

// -- the client thread ------------------------------------------------------------

// One action to perform: a candidate and its key in Report::candidates.
struct ActionJob final {
    const std::string* key;
    const ActionCandidate* candidate;
};

struct ClientRun final {
    Report* report{};
    // The window, for the work the client runs on the UI thread; the client
    // thread itself reaches the window only through `dispatcher`.
    Window* window{};
    WindowDispatcher dispatcher;
    HWND hwnd{};
    // The Gallery, or off_tree_page for the off-tree fixture.
    std::string_view window_kind;
    const std::vector<ActionJob>* jobs{};
    std::function<void()> request_close;
};

// Runs `work` on the UI thread through the window's dispatcher handle. The
// window may close under the client (its host destroys it); the handle then
// reports window_gone and the client stops with that failure instead of
// touching the destroyed window. The work itself runs only while the window
// lives, so it may use the window.
void on_ui_thread(const WindowDispatcher& dispatcher, std::function<void()> work) {
    if (dispatcher.invoke(std::move(work)) == DispatchInvokeResult::window_gone) {
        throw std::runtime_error("the window closed before the client finished");
    }
}

// The element at a path of child positions below the window element (only
// GUI.Forms children at the window level).
ElementReference element_at(IUIAutomationTreeWalker& walker, IUIAutomationElement& window,
                            const std::vector<std::size_t>& path) {
    ElementReference current;
    IUIAutomationElement* parent = &window;
    for (std::size_t depth = 0; depth < path.size(); ++depth) {
        ElementReference child;
        HRESULT status = walker.GetFirstChildElement(parent, child.receive());
        std::size_t position = 0;
        bool reached = false;
        while (SUCCEEDED(status) && child.get() != nullptr) {
            bool ours = true;
            if (depth == 0U) {
                BstrOwner framework;
                const HRESULT read = (*child.get()).get_CurrentFrameworkId(framework.receive());
                ours = SUCCEEDED(read) && framework.wide() == framework_id;
            }
            if (ours) {
                if (position == path[depth]) {
                    reached = true;
                    break;
                }
                ++position;
            }
            ElementReference next;
            status = walker.GetNextSiblingElement(child.get(), next.receive());
            child = std::move(next);
        }
        if (!reached) return ElementReference{};
        current = std::move(child);
        parent = current.get();
    }
    return current;
}

void perform_job(ClientRun& run, IUIAutomationTreeWalker& walker, IUIAutomationElement& window_element,
                 const ActionJob& job) {
    const ActionCandidate& candidate = *job.candidate;
    ActionResult result;
    result.key = *job.key;
    result.page = candidate.page;
    result.stable_id = candidate.stable_id;
    PageExpectation shown;
    on_ui_thread(run.dispatcher, std::function<void()>(PreparePage{run.window, run.hwnd, candidate.page, &shown}));
    if (!shown.shown) throw std::runtime_error("a Gallery page cannot be selected: " + candidate.page);
    ActionState state;
    on_ui_thread(run.dispatcher, std::function<void()>(BeginAction{run.window, &candidate, &state}));
    if (!state.node) {
        result.outcome = ActionOutcome::lost;
        result.evidence = "the node is not in the tree when its page is shown again";
        (*run.report).actions.push_back(std::move(result));
        return;
    }
    const std::size_t baseline_roots = state.before.roots.size();
    std::vector<std::size_t> path;
    bool found = false;
    on_ui_thread(run.dispatcher, std::function<void()>(FindCandidatePath{run.window, &candidate, &path, &found}));
    ElementReference element;
    if (found) element = element_at(walker, window_element, path);
    if (element.get() == nullptr) {
        result.outcome = ActionOutcome::lost;
        result.evidence = "the UI Automation path to the node ends early";
        (*run.report).actions.push_back(std::move(result));
        return;
    }
    const DefaultKind kind = expected_default(*state.node);
    const PatternCall call = pattern_call(*state.node, kind);
    result.call = pattern_call_name(call);
    const HRESULT status = call_pattern(*element.get(), call);
    // Work an Invoke posted runs on the UI thread before this.
    on_ui_thread(run.dispatcher, std::function<void()>(DoNothing{}));
    if (status != S_OK) {
        result.outcome = ActionOutcome::refused;
        result.evidence = "HRESULT " + hresult_text(status);
        on_ui_thread(run.dispatcher, std::function<void()>(ReleaseSubscriptions{&state}));
        on_ui_thread(run.dispatcher, std::function<void()>(ClosePopups{run.window, baseline_roots}));
        (*run.report).actions.push_back(std::move(result));
        return;
    }
    bool reversible = false;
    on_ui_thread(run.dispatcher, std::function<void()>(EndAction{run.window, &candidate, &state, &result, &reversible}));
    if (reversible) {
        // The element is found again: the action may have rebuilt the tree.
        std::vector<std::size_t> again_path;
        bool again_found = false;
        on_ui_thread(run.dispatcher, 
            std::function<void()>(FindCandidatePath{run.window, &candidate, &again_path, &again_found}));
        ElementReference again;
        if (again_found) again = element_at(walker, window_element, again_path);
        bool restored = false;
        if (again.get() != nullptr && call_pattern(*again.get(), undo_call(call)) == S_OK) {
            const SemanticNode before = *state.node;
            on_ui_thread(run.dispatcher, std::function<void()>(CheckRestored{run.window, &candidate, &before, &restored}));
        }
        result.evidence += restored ? ", then restored" : ", but not restored";
        if (!restored) result.outcome = ActionOutcome::unobserved;
    }
    on_ui_thread(run.dispatcher, std::function<void()>(ClosePopups{run.window, baseline_roots}));
    (*run.report).actions.push_back(std::move(result));
}

// Compares one page: the UI thread shows it and builds the expectation,
// then the client walks the window element, live on live_page and from one
// cache request (the whole raw subtree) on every other.
void compare_page(ClientRun& run, IUIAutomation& automation, IUIAutomationTreeWalker& walker,
                  IUIAutomationCacheRequest& cache_request, IUIAutomationElement& window_element,
                  std::string_view page) {
    PageExpectation expectation;
    on_ui_thread(run.dispatcher, std::function<void()>(PreparePage{run.window, run.hwnd, page, &expectation, true}));
    if (!expectation.shown) throw std::runtime_error("a Gallery page cannot be selected: " + std::string(page));
    const bool cached = page != live_page;
    ElementReference cached_window;
    if (cached) {
        const HRESULT built = window_element.BuildUpdatedCache(&cache_request, cached_window.receive());
        if (FAILED(built) || cached_window.get() == nullptr) {
            throw std::runtime_error("the page " + std::string(page) + " is not cached: " + hresult_text(built));
        }
    }
    IUIAutomationElement& root = cached ? *cached_window.get() : window_element;
    PageWalk walk{automation, walker, *run.report, page, expectation, {}, cached};
    compare_element(walk, root, expectation.client, true);
    ++(*run.report).pages;
}

// The child positions to the first control-backed node at or below `at`
// that `other` does not hold. Returns false when there is none.
bool path_to_unshared_control(const ExpectedNode& at, const ExpectedNode& other,
                              std::vector<std::size_t>& path, std::string& stable_id) {
    for (std::size_t index = 0; index < at.children.size(); ++index) {
        const ExpectedNode& child = at.children[index];
        path.push_back(index);
        const bool candidate = control_backed(child.node) && !holds_control(other, child.node.stable_id);
        if (candidate) {
            stable_id = child.node.stable_id;
            return true;
        }
        if (path_to_unshared_control(child, other, path, stable_id)) return true;
        path.pop_back();
    }
    return false;
}

// An element of a control on the first page, and not on the second,
// answers UIA_E_ELEMENTNOTAVAILABLE once the second page replaced the
// first.
void check_stale_element(ClientRun& run, IUIAutomationTreeWalker& walker,
                         IUIAutomationElement& window_element) {
    const std::string_view first = gallery::gallery_pages[0].id;
    const std::string_view second = gallery::gallery_pages[1].id;
    PageExpectation later;
    on_ui_thread(run.dispatcher, std::function<void()>(PreparePage{run.window, run.hwnd, second, &later}));
    PageExpectation shown;
    on_ui_thread(run.dispatcher, std::function<void()>(PreparePage{run.window, run.hwnd, first, &shown}));
    std::vector<std::size_t> path;
    std::string stable_id;
    if (!path_to_unshared_control(shown.client, later.client, path, stable_id)) {
        (*run.report).stale_check = "page " + std::string(first) + " has no control page " +
                                    std::string(second) + " lacks";
        return;
    }
    ElementReference kept = element_at(walker, window_element, path);
    if (kept.get() == nullptr) {
        (*run.report).stale_check = "no element was found for " + stable_id;
        return;
    }
    BstrOwner before;
    const HRESULT live = (*kept.get()).get_CurrentName(before.receive());
    PageExpectation replaced;
    on_ui_thread(run.dispatcher, std::function<void()>(PreparePage{run.window, run.hwnd, second, &replaced}));
    BstrOwner after;
    const HRESULT stale = (*kept.get()).get_CurrentName(after.receive());
    if (FAILED(live) || stale != static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE)) {
        (*run.report).stale_check = "the element of " + stable_id + " answered " + hresult_text(stale) +
                                    " after its page was replaced (before: " + hresult_text(live) + ")";
    }
}

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

void run_client(ClientRun& run) {
    ComReference<IUIAutomation> automation;
    const HRESULT created = CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                                             __uuidof(IUIAutomation),
                                             reinterpret_cast<void**>(automation.receive()));
    if (FAILED(created) || automation.get() == nullptr) throw std::runtime_error("IUIAutomation is not created");
    ComReference<IUIAutomationTreeWalker> walker;
    if (FAILED((*automation.get()).get_RawViewWalker(walker.receive())) || walker.get() == nullptr) {
        throw std::runtime_error("the raw view walker is not available");
    }
    ElementReference window_element;
    const HRESULT found = (*automation.get()).ElementFromHandle(run.hwnd, window_element.receive());
    if (FAILED(found) || window_element.get() == nullptr) {
        throw std::runtime_error("the window has no UI Automation element");
    }
    // The window's provider answers FrameworkId GUI.Forms; an element built
    // from the system proxies answers "Win32".
    BstrOwner framework;
    const HRESULT read = (*window_element.get()).get_CurrentFrameworkId(framework.receive());
    if (FAILED(read) || framework.wide() != framework_id) {
        throw std::runtime_error("UI Automation did not use the window's provider (FrameworkId \"" +
                                 framework.utf8() + "\", " + hresult_text(read) + ")");
    }
    if (run.jobs != nullptr) {
        for (const ActionJob& job : *run.jobs) perform_job(run, *walker.get(), *window_element.get(), job);
        return;
    }
    const ComReference<IUIAutomationCacheRequest> cache_request = make_cache_request(*automation.get());
    IUIAutomationCacheRequest& cache = *cache_request.get();
    if (run.window_kind == off_tree_page) {
        compare_page(run, *automation.get(), *walker.get(), cache, *window_element.get(), off_tree_page);
    } else {
        for (const gallery::GalleryPageSpec& page : gallery::gallery_pages) {
            compare_page(run, *automation.get(), *walker.get(), cache, *window_element.get(), page.id);
        }
        check_stale_element(run, *walker.get(), *window_element.get());
    }
    // The comparison found the candidates; this window performs them.
    const std::vector<ActionJob> jobs = window_jobs(*run.report, run.window_kind);
    for (const ActionJob& job : jobs) perform_job(run, *walker.get(), *window_element.get(), job);
}

struct CloseWindow final {
    std::function<void()>* request_close{};
    void operator()() const { (*request_close)(); }
};

// The client thread: the multithreaded apartment, the run, then the window
// closes through the UI thread whatever happened.
struct ClientThread final {
    ClientRun* run{};
    void operator()() const {
        ClientRun& state = *run;
        const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        try {
            if (FAILED(initialized)) throw std::runtime_error("the client thread has no COM apartment");
            run_client(state);
            (*state.report).completed = true;
        } catch (const std::exception& error) {
            (*state.report).failure = error.what();
        }
        if (SUCCEEDED(initialized)) CoUninitialize();
        try {
            // A window that closed already needs no close.
            static_cast<void>(state.dispatcher.invoke(std::function<void()>(CloseWindow{&state.request_close})));
        } catch (const std::exception& error) {
            (*state.report).failure += std::string(" closing the window: ") + error.what();
        }
    }
};

// Starts the client once the host has attached the window and its
// publishers, and returns so the UI thread's message loop serves it.
struct HostReady final {
    ClientRun* run{};
    std::thread* client{};
    void operator()(std::function<void()>, std::function<void()> request_close,
                    std::function<HostDialogResult(const HostDialogRequest&)>,
                    std::function<HostServiceStatus(const HostTooltipRequest&)>,
                    std::function<void()>, std::function<HostClipboardTextResult()>,
                    std::function<HostServiceStatus(std::string_view)>) const {
        (*run).request_close = std::move(request_close);
        (*run).dispatcher = (*(*run).window).dispatcher();
        (*run).hwnd = test_support::own_gui_forms_window(test_support::process_title_wide(fixture_title_utf8).c_str());
        if ((*run).hwnd == nullptr) {
            (*(*run).report).failure = "the window's native handle does not exist";
            (*run).request_close();
            return;
        }
        *client = std::thread(ClientThread{run});
    }
};

void place(Panel& root, const Control::Ptr& control, Rect bounds) {
    (*control).set_requested_bounds(bounds);
    root.add_child(control);
}

// The off-tree fixture, as A-W1 builds it.
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

// Runs one window with the client in it. Throws when the window does not
// run or the client fails.
void run_window(Report& report, std::string_view window_kind, const std::vector<ActionJob>* jobs) {
    report.completed = false;
    std::unique_ptr<Window> model;
    if (window_kind == off_tree_page) {
        model = make_off_tree_fixture();
    } else {
        model = gallery::make_gallery();
    }
    ClientRun run;
    run.report = &report;
    run.window = model.get();
    run.window_kind = window_kind;
    run.jobs = jobs;
    std::thread client;
    host::WindowsHostOptions options;
    options.title = test_support::process_title(fixture_title_utf8);
    options.initial_size = gallery::gallery_default_size;
    options.minimum_size = gallery::gallery_minimum_size;
    options.print_metrics_on_close = false;
    options.host_ready = HostReady{&run, &client};
    const int exit_code = host::run_windows(std::move(model), std::move(options));
    if (client.joinable()) client.join();
    if (exit_code != 0) throw std::runtime_error("the Gallery window did not run");
    if (!report.failure.empty()) throw std::runtime_error(report.failure);
    if (!report.completed) throw std::runtime_error("the client did not complete");
}

// Runs one window of `window_kind`, which compares its pages and then
// performs its candidates page by page; one whose change was not seen runs
// again alone in a fresh window, and that result stands (A-W1's rule: an
// earlier action's effects can hide a later one's).
void run_window_kind(Report& report, std::string_view window_kind) {
    const std::size_t first = report.actions.size();
    run_window(report, window_kind, nullptr);
    const std::vector<ActionJob> jobs = window_jobs(report, window_kind);
    for (std::size_t index = 0; index < jobs.size(); ++index) {
        if (report.actions[first + index].outcome == ActionOutcome::changed) continue;
        const ActionResult& batched = report.actions[first + index];
        std::string first_try = std::string(outcome_name(batched.outcome));
        if (!batched.evidence.empty()) first_try += ", " + batched.evidence;
        const std::vector<ActionJob> alone{jobs[index]};
        ++report.reruns;
        run_window(report, window_kind, &alone);
        ActionResult isolated = std::move(report.actions.back());
        report.actions.pop_back();
        if (!isolated.evidence.empty()) isolated.evidence += "; ";
        isolated.evidence += "alone in a fresh window, after \"" + first_try + "\" in the shared window";
        report.actions[first + index] = std::move(isolated);
    }
}

void print_report(const Report& report) {
    std::cout << "UI Automation Gallery client: " << report.pages << " pages, " << report.elements
              << " elements compared, " << report.controller_relations << " tab-to-panel relations\n";
    std::cout << "| control type | own nodes | item nodes | matched | mismatched |\n";
    for (const std::pair<const std::string, ControlTally>& entry : report.tallies) {
        const ControlTally& tally = entry.second;
        std::cout << "| " << entry.first << " | " << tally.own_nodes << " | " << tally.item_nodes << " | "
                  << tally.matched << " | " << tally.mismatched << " |\n";
    }
    std::cout << "default actions (" << report.actions.size() << ", " << report.reruns
              << " run again alone in a fresh window):\n";
    for (const ActionResult& action : report.actions) {
        std::cout << "  " << action.key << " [" << action.page << " " << action.stable_id << "] " << action.call
                  << ": " << outcome_name(action.outcome);
        if (!action.evidence.empty()) std::cout << " (" << action.evidence << ")";
        std::cout << '\n';
    }
    std::cout << "mismatches (" << report.mismatches.size() << "):\n";
    for (const Mismatch& mismatch : report.mismatches) {
        std::cout << "  " << mismatch.page << " " << mismatch.stable_id << " [" << mismatch.control_type << "] "
                  << mismatch.field << ": expected \"" << mismatch.expected << "\", observed \""
                  << mismatch.observed << "\"\n";
    }
}

} // namespace

int main() {
    try {
        Report report;
        run_window_kind(report, std::string_view());
        run_window_kind(report, off_tree_page);
        print_report(report);
        std::size_t failed_actions = 0;
        for (const ActionResult& action : report.actions) {
            if (action.outcome != ActionOutcome::changed) ++failed_actions;
        }
        if (!report.stale_check.empty()) {
            std::cerr << report.stale_check << '\n';
            return 1;
        }
        std::cout << "stale element: UIA_E_ELEMENTNOTAVAILABLE after its page was replaced\n";
        if (!report.mismatches.empty() || failed_actions != 0U) {
            std::cerr << report.mismatches.size() << " mismatches, " << failed_actions
                      << " actions without an observed change\n";
            return 1;
        }
        std::cout << "UI Automation Gallery client passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
