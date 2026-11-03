// Stage A, A-W2: the UI Automation provider, read through IUIAutomation.
//
// A fixture window (a button, a check box, a text box, a numeric field, a
// list, a tree and a button the test removes) runs in a real Win32 window
// through host::run_windows. A client thread in the multithreaded apartment
// reads it through IUIAutomation, as a separate client would: UI Automation
// marshals every provider call to the window's UI thread (the providers ask
// for COM threading), whose message loop keeps running. The client reads
// and changes the model only through Window::invoke, which runs on the UI
// thread. It checks:
// - the fragment root under the window and AutomationId from stable IDs,
//   for controls and for virtual children;
// - control types, names and the pattern each node offers;
// - Invoke (posted through Window::begin_invoke), Toggle, Value, RangeValue,
//   SelectionItem and ExpandCollapse, each observed in the model;
// - distinct runtime IDs;
// - an element whose control was removed answers UIA_E_ELEMENTNOTAVAILABLE.

#include "gui_forms/gui_forms.hpp"
#include "gui_forms/platform/windows_host.hpp"
#include "support/windows_own_window.hpp"

#include <windows.h>
#include <ole2.h>
#include <uiautomation.h>

#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

using namespace gui_forms;

constexpr const wchar_t* fixture_title = L"GUI.Forms UI Automation provider test";
constexpr std::string_view fixture_title_utf8 = "GUI.Forms UI Automation provider test";

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

// One COM interface reference, released when the owner goes.
template <typename Interface>
class ComReference final {
public:
    ComReference() noexcept = default;
    ~ComReference() { reset(); }
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

// A BSTR, freed when the owner goes.
class BstrText final {
public:
    BstrText() noexcept = default;
    ~BstrText() { SysFreeString(value_); }
    BstrText(const BstrText&) = delete;
    BstrText& operator=(const BstrText&) = delete;
    [[nodiscard]] BSTR* receive() noexcept {
        SysFreeString(value_);
        value_ = nullptr;
        return &value_;
    }
    [[nodiscard]] std::wstring text() const {
        if (value_ == nullptr) return {};
        const std::wstring result(value_, SysStringLen(value_));
        return result;
    }

private:
    BSTR value_{};
};

std::wstring wide(std::string_view text) {
    if (text.empty()) return {};
    const int size = static_cast<int>(text.size());
    const int count = MultiByteToWideChar(CP_UTF8, 0, text.data(), size, nullptr, 0);
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), size, result.data(), count);
    return result;
}

std::string narrow(std::wstring_view text) {
    if (text.empty()) return {};
    const int units = static_cast<int>(text.size());
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), units, nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), units, result.data(), size, nullptr, nullptr);
    return result;
}

// -- the fixture -------------------------------------------------------------

struct Fixture final {
    std::shared_ptr<Panel> root;
    std::shared_ptr<Button> button;
    std::shared_ptr<CheckBox> check;
    std::shared_ptr<TextBox> text;
    std::shared_ptr<NumericUpDown> number;
    std::shared_ptr<ListBox> list;
    std::shared_ptr<TreeView> tree;
    std::shared_ptr<Button> doomed;
    int clicks{};
};

std::unique_ptr<Window> make_fixture(Fixture& fixture) {
    fixture.root = make_control<Panel>(StableId("uia.root"));
    fixture.button = make_control<Button>(StableId("uia.button"), "Press me");
    fixture.check = make_control<CheckBox>(StableId("uia.check"), "Check me");
    fixture.text = make_control<TextBox>(StableId("uia.text"));
    fixture.number = make_control<NumericUpDown>(StableId("uia.number"));
    fixture.list = make_control<ListBox>(StableId("uia.list"));
    fixture.tree = make_control<TreeView>(StableId("uia.tree"));
    fixture.doomed = make_control<Button>(StableId("uia.doomed"), "Removed");
    (*fixture.button).set_requested_bounds({10.0, 10.0, 120.0, 28.0});
    (*fixture.check).set_requested_bounds({10.0, 50.0, 160.0, 24.0});
    (*fixture.text).set_requested_bounds({10.0, 90.0, 200.0, 26.0});
    (*fixture.number).set_requested_bounds({10.0, 130.0, 120.0, 26.0});
    (*fixture.number).set_range(0.0, 10.0);
    (*fixture.list).set_requested_bounds({240.0, 10.0, 160.0, 100.0});
    (*fixture.list).set_items({"Alpha", "Beta", "Gamma"});
    (*fixture.tree).set_requested_bounds({240.0, 120.0, 160.0, 100.0});
    std::vector<TreeViewItem> nodes;
    nodes.push_back(TreeViewItem{"uia.tree.folder", "Folder", 0U, true, false, true, {}});
    nodes.push_back(TreeViewItem{"uia.tree.file", "File", 1U, false, false, true, {}});
    (*fixture.tree).set_items(nodes);
    (*fixture.doomed).set_requested_bounds({10.0, 170.0, 120.0, 28.0});
    (*fixture.root).add_child(fixture.button);
    (*fixture.root).add_child(fixture.check);
    (*fixture.root).add_child(fixture.text);
    (*fixture.root).add_child(fixture.number);
    (*fixture.root).add_child(fixture.list);
    (*fixture.root).add_child(fixture.tree);
    (*fixture.root).add_child(fixture.doomed);
    std::unique_ptr<Window> window = std::make_unique<Window>(fixture.root, Size{420.0, 240.0});
    return window;
}

struct CountClick final {
    int* clicks{};
    void operator()(ButtonBase&) const { ++*clicks; }
};

// -- work the client runs on the UI thread ----------------------------------

// What the client reads from the model; filled on the UI thread.
struct ModelState final {
    int clicks{};
    bool checked{};
    std::string text;
    double number{};
    std::optional<std::size_t> list_selection;
    bool folder_expanded{};
    bool doomed_attached{};
};

struct ReadModel final {
    Fixture* fixture{};
    ModelState* state{};
    void operator()() const {
        (*state).clicks = (*fixture).clicks;
        (*state).checked = (*(*fixture).check).checked();
        (*state).text = std::string((*(*fixture).text).text());
        (*state).number = (*(*fixture).number).value();
        (*state).list_selection = (*(*fixture).list).selected_index();
        (*state).folder_expanded = (*(*fixture).tree).expanded("uia.tree.folder");
        (*state).doomed_attached = (*(*fixture).doomed).attached_window() != nullptr;
    }
};

struct RemoveDoomed final {
    Fixture* fixture{};
    void operator()() const {
        static_cast<void>((*(*fixture).root).remove_child((*(*fixture).doomed).runtime_id()));
    }
};

struct CloseWindow final {
    std::function<void()>* request_close{};
    void operator()() const { (*request_close)(); }
};

// -- the client ---------------------------------------------------------------

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

struct ClientContext final {
    // The client thread reaches the window only through this handle.
    WindowDispatcher dispatcher;
    Fixture* fixture{};
    HWND hwnd{};
    std::function<void()> request_close;
    std::vector<std::string> failures;
    int checks{};
    // How many windows with the fixture's class and title the desktop held
    // when the client started, in every process.
    std::size_t desktop_windows{};
    // The first window's element, which its client keeps (one reference)
    // past that window's end and the end of the UI thread's COM apartment,
    // as a long-lived client does; the second window's client checks it and
    // releases it. Both clients live in the multithreaded apartment, which
    // main keeps alive between them.
    IUIAutomationElement* kept{};
    // True for the second fixture window, whose client runs
    // run_second_window_checks.
    bool second_window{};
};

ModelState read_model(ClientContext& context) {
    ModelState state;
    on_ui_thread(context.dispatcher, std::function<void()>(ReadModel{context.fixture, &state}));
    return state;
}

void expect(ClientContext& context, bool condition, std::string_view what) {
    ++context.checks;
    if (!condition) context.failures.push_back(std::string(what));
}

// The first descendant of `root` with AutomationId `id`, or none.
bool find_element(IUIAutomation& automation, IUIAutomationElement& root, std::string_view id,
                  ComReference<IUIAutomationElement>& found) {
    VARIANT value;
    VariantInit(&value);
    value.vt = VT_BSTR;
    const std::wstring wanted = wide(id);
    value.bstrVal = SysAllocString(wanted.c_str());
    ComReference<IUIAutomationCondition> condition;
    const HRESULT created = automation.CreatePropertyCondition(UIA_AutomationIdPropertyId, value,
                                                               condition.receive());
    VariantClear(&value);
    if (FAILED(created)) return false;
    const HRESULT searched = root.FindFirst(TreeScope_Descendants, condition.get(), found.receive());
    const bool result = SUCCEEDED(searched) && found.get() != nullptr;
    return result;
}

std::string element_name(IUIAutomationElement& element) {
    BstrText name;
    if (FAILED(element.get_CurrentName(name.receive()))) return {};
    const std::string result = narrow(name.text());
    return result;
}

std::string automation_id(IUIAutomationElement& element) {
    BstrText id;
    if (FAILED(element.get_CurrentAutomationId(id.receive()))) return {};
    const std::string result = narrow(id.text());
    return result;
}

CONTROLTYPEID control_type(IUIAutomationElement& element) {
    CONTROLTYPEID type = 0;
    if (FAILED(element.get_CurrentControlType(&type))) return 0;
    return type;
}

template <typename Pattern>
bool pattern(IUIAutomationElement& element, PATTERNID id, ComReference<Pattern>& found) {
    const HRESULT status = element.GetCurrentPatternAs(id, __uuidof(Pattern),
                                                       reinterpret_cast<void**>(found.receive()));
    const bool result = SUCCEEDED(status) && found.get() != nullptr;
    return result;
}

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

// Waits for posted work (Invoke) to reach the model: reads it on the UI
// thread until `clicks` reaches `wanted` or five seconds pass.
bool wait_for_clicks(ClientContext& context, int wanted) {
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        const ModelState state = read_model(context);
        if (state.clicks >= wanted) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

void check_root_and_identity(ClientContext& context, IUIAutomation& automation,
                             IUIAutomationElement& window) {
    expect(context, control_type(window) == UIA_WindowControlTypeId,
           "the window element keeps the host's Window control type");
    ComReference<IUIAutomationElement> button;
    ComReference<IUIAutomationElement> check;
    expect(context, find_element(automation, window, "uia.button", button),
           "the button is found by its stable ID as AutomationId");
    expect(context, find_element(automation, window, "uia.check", check),
           "the check box is found by its stable ID as AutomationId");
    if (button.get() == nullptr || check.get() == nullptr) return;
    expect(context, control_type(*button.get()) == UIA_ButtonControlTypeId &&
                        element_name(*button.get()) == "Press me",
           "the button's control type and name");
    BstrText framework;
    const HRESULT read = (*button.get()).get_CurrentFrameworkId(framework.receive());
    expect(context, SUCCEEDED(read) && framework.text() == L"GUI.Forms", "FrameworkId is GUI.Forms");
    const std::vector<int> button_id = runtime_id(*button.get());
    const std::vector<int> check_id = runtime_id(*check.get());
    // UI Automation replaces UiaAppendRuntimeId with the window's own
    // prefix, so a client sees that prefix and the provider's four parts.
    expect(context, button_id.size() > 4U && check_id.size() > 4U && button_id != check_id,
           "elements carry distinct runtime IDs that end in the provider's four parts");
}

void check_invoke(ClientContext& context, IUIAutomation& automation, IUIAutomationElement& window) {
    ComReference<IUIAutomationElement> button;
    if (!find_element(automation, window, "uia.button", button)) {
        expect(context, false, "the button is found for Invoke");
        return;
    }
    ComReference<IUIAutomationInvokePattern> invoke;
    expect(context, pattern(*button.get(), UIA_InvokePatternId, invoke), "the button offers Invoke");
    ComReference<IUIAutomationTogglePattern> toggle;
    expect(context, !pattern(*button.get(), UIA_TogglePatternId, toggle),
           "a plain button does not offer Toggle");
    if (invoke.get() == nullptr) return;
    expect(context, SUCCEEDED((*invoke.get()).Invoke()), "Invoke succeeds");
    expect(context, wait_for_clicks(context, 1), "the posted Invoke clicks the button");
}

void check_toggle(ClientContext& context, IUIAutomation& automation, IUIAutomationElement& window) {
    ComReference<IUIAutomationElement> check;
    ComReference<IUIAutomationTogglePattern> toggle;
    if (!find_element(automation, window, "uia.check", check) ||
        !pattern(*check.get(), UIA_TogglePatternId, toggle)) {
        expect(context, false, "the check box offers Toggle");
        return;
    }
    expect(context, control_type(*check.get()) == UIA_CheckBoxControlTypeId,
           "the check box's control type");
    ToggleState before = ToggleState_Indeterminate;
    expect(context, SUCCEEDED((*toggle.get()).get_CurrentToggleState(&before)) &&
                        before == ToggleState_Off,
           "the check box starts off");
    expect(context, SUCCEEDED((*toggle.get()).Toggle()), "Toggle succeeds");
    ToggleState after = ToggleState_Off;
    expect(context, SUCCEEDED((*toggle.get()).get_CurrentToggleState(&after)) && after == ToggleState_On &&
                        read_model(context).checked,
           "Toggle checks the check box");
}

void check_value(ClientContext& context, IUIAutomation& automation, IUIAutomationElement& window) {
    ComReference<IUIAutomationElement> text;
    ComReference<IUIAutomationValuePattern> value;
    if (!find_element(automation, window, "uia.text", text) ||
        !pattern(*text.get(), UIA_ValuePatternId, value)) {
        expect(context, false, "the text box offers Value");
        return;
    }
    expect(context, control_type(*text.get()) == UIA_EditControlTypeId, "the text box's control type");
    BSTR typed = SysAllocString(L"Hello, UIA");
    const HRESULT set = (*value.get()).SetValue(typed);
    SysFreeString(typed);
    expect(context, SUCCEEDED(set) && read_model(context).text == "Hello, UIA",
           "SetValue sets the text box's text");
    BstrText read;
    expect(context, SUCCEEDED((*value.get()).get_CurrentValue(read.receive())) &&
                        read.text() == L"Hello, UIA",
           "Value reads the text back");
}

void check_range(ClientContext& context, IUIAutomation& automation, IUIAutomationElement& window) {
    ComReference<IUIAutomationElement> number;
    ComReference<IUIAutomationRangeValuePattern> range;
    if (!find_element(automation, window, "uia.number", number) ||
        !pattern(*number.get(), UIA_RangeValuePatternId, range)) {
        expect(context, false, "the numeric field offers RangeValue");
        return;
    }
    double minimum = -1.0;
    double maximum = -1.0;
    expect(context, SUCCEEDED((*range.get()).get_CurrentMinimum(&minimum)) &&
                        SUCCEEDED((*range.get()).get_CurrentMaximum(&maximum)) && minimum == 0.0 &&
                        maximum == 10.0,
           "RangeValue publishes the numeric field's range");
    expect(context, SUCCEEDED((*range.get()).SetValue(7.0)) && read_model(context).number == 7.0,
           "RangeValue SetValue sets the numeric field");
    expect(context, FAILED((*range.get()).SetValue(11.0)) && read_model(context).number == 7.0,
           "a value past the maximum is refused and changes nothing");
}

void check_selection(ClientContext& context, IUIAutomation& automation, IUIAutomationElement& window) {
    ComReference<IUIAutomationElement> list;
    if (!find_element(automation, window, "uia.list", list)) {
        expect(context, false, "the list is found");
        return;
    }
    ComReference<IUIAutomationTreeWalker> walker;
    if (FAILED(automation.get_RawViewWalker(walker.receive()))) {
        expect(context, false, "the raw view walker exists");
        return;
    }
    ComReference<IUIAutomationElement> first;
    ComReference<IUIAutomationElement> second;
    const HRESULT first_read = (*walker.get()).GetFirstChildElement(list.get(), first.receive());
    if (FAILED(first_read) || first.get() == nullptr) {
        expect(context, false, "the list publishes items");
        return;
    }
    const HRESULT second_read = (*walker.get()).GetNextSiblingElement(first.get(), second.receive());
    if (FAILED(second_read) || second.get() == nullptr) {
        expect(context, false, "the list publishes a second item");
        return;
    }
    expect(context, control_type(*second.get()) == UIA_ListItemControlTypeId &&
                        element_name(*second.get()) == "Beta" && !automation_id(*second.get()).empty(),
           "a list item's control type, name and AutomationId (its stable ID)");
    ComReference<IUIAutomationSelectionItemPattern> item;
    if (!pattern(*second.get(), UIA_SelectionItemPatternId, item)) {
        expect(context, false, "a list item offers SelectionItem");
        return;
    }
    expect(context, SUCCEEDED((*item.get()).Select()) && read_model(context).list_selection == 1U,
           "SelectionItem Select selects the item");
    BOOL selected = FALSE;
    expect(context, SUCCEEDED((*item.get()).get_CurrentIsSelected(&selected)) && selected == TRUE,
           "the selected item reports IsSelected");
    ComReference<IUIAutomationSelectionPattern> selection;
    ComReference<IUIAutomationElementArray> chosen;
    int count = 0;
    const bool listed = pattern(*list.get(), UIA_SelectionPatternId, selection) &&
                        SUCCEEDED((*selection.get()).GetCurrentSelection(chosen.receive())) &&
                        chosen.get() != nullptr && SUCCEEDED((*chosen.get()).get_Length(&count));
    expect(context, listed && count == 1, "the list's Selection holds the one selected item");
}

void check_expand(ClientContext& context, IUIAutomation& automation, IUIAutomationElement& window) {
    ComReference<IUIAutomationElement> folder;
    ComReference<IUIAutomationExpandCollapsePattern> expand;
    if (!find_element(automation, window, "uia.tree.folder", folder) ||
        !pattern(*folder.get(), UIA_ExpandCollapsePatternId, expand)) {
        expect(context, false, "the tree's folder offers ExpandCollapse");
        return;
    }
    ExpandCollapseState state = ExpandCollapseState_LeafNode;
    expect(context, SUCCEEDED((*expand.get()).get_CurrentExpandCollapseState(&state)) &&
                        state == ExpandCollapseState_Collapsed,
           "the folder starts collapsed");
    expect(context, SUCCEEDED((*expand.get()).Expand()) && read_model(context).folder_expanded,
           "Expand expands the folder");
    expect(context, SUCCEEDED((*expand.get()).Collapse()) && !read_model(context).folder_expanded,
           "Collapse collapses it again");
}

void check_stale_element(ClientContext& context, IUIAutomation& automation,
                         IUIAutomationElement& window) {
    ComReference<IUIAutomationElement> doomed;
    if (!find_element(automation, window, "uia.doomed", doomed)) {
        expect(context, false, "the button to remove is found");
        return;
    }
    ComReference<IUIAutomationInvokePattern> invoke;
    expect(context, pattern(*doomed.get(), UIA_InvokePatternId, invoke), "it offers Invoke");
    on_ui_thread(context.dispatcher, std::function<void()>(RemoveDoomed{context.fixture}));
    expect(context, !read_model(context).doomed_attached, "the button left the window");
    BstrText name;
    const HRESULT read = (*doomed.get()).get_CurrentName(name.receive());
    expect(context, read == static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE),
           "a removed control's element answers UIA_E_ELEMENTNOTAVAILABLE");
    if (invoke.get() != nullptr) {
        const HRESULT invoked = (*invoke.get()).Invoke();
        expect(context, invoked == static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE),
               "Invoke on a removed control's element answers UIA_E_ELEMENTNOTAVAILABLE");
    }
}

void run_client_checks(ClientContext& context) {
    ComReference<IUIAutomation> automation;
    const HRESULT created = CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                                             __uuidof(IUIAutomation),
                                             reinterpret_cast<void**>(automation.receive()));
    require(SUCCEEDED(created) && automation.get() != nullptr, "IUIAutomation is created");
    ComReference<IUIAutomationElement> window;
    const HRESULT found = (*automation.get()).ElementFromHandle(context.hwnd, window.receive());
    require(SUCCEEDED(found) && window.get() != nullptr, "the window has a UI Automation element");
    // The window's provider answers FrameworkId GUI.Forms; an element UI
    // Automation built from the system proxies answers "Win32".
    BstrText root_framework;
    const HRESULT framework_read = (*window.get()).get_CurrentFrameworkId(root_framework.receive());
    require(SUCCEEDED(framework_read) && root_framework.text() == L"GUI.Forms",
            "UI Automation uses the window's provider");
    // The decoy process shows a window with the same class and title, so a
    // desktop-wide lookup could have chosen either; the client must be
    // reading this process's window.
    expect(context, context.desktop_windows >= 2U,
           "a second process shows a window with the fixture's class and title");
    DWORD owner = 0;
    GetWindowThreadProcessId(context.hwnd, &owner);
    expect(context, owner == GetCurrentProcessId(), "the window the client reads belongs to this process");
    check_root_and_identity(context, *automation.get(), *window.get());
    check_invoke(context, *automation.get(), *window.get());
    check_toggle(context, *automation.get(), *window.get());
    check_value(context, *automation.get(), *window.get());
    check_range(context, *automation.get(), *window.get());
    check_selection(context, *automation.get(), *window.get());
    check_expand(context, *automation.get(), *window.get());
    check_stale_element(context, *automation.get(), *window.get());
    context.kept = window.get();
    (*context.kept).AddRef();
}

// A second window on the same UI thread, after the first window's run
// ended its COM apartment while the first client still held an element.
// UI Automation then refused the new window's provider
// (UiaReturnRawElementProvider answered E_INVALIDARG) and served the window
// from the system proxies, until the host released its providers before the
// apartment ended.
void run_second_window_checks(ClientContext& context) {
    ComReference<IUIAutomation> automation;
    const HRESULT created = CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                                             __uuidof(IUIAutomation),
                                             reinterpret_cast<void**>(automation.receive()));
    require(SUCCEEDED(created) && automation.get() != nullptr, "IUIAutomation is created again");
    ComReference<IUIAutomationElement> window;
    const HRESULT found = (*automation.get()).ElementFromHandle(context.hwnd, window.receive());
    require(SUCCEEDED(found) && window.get() != nullptr, "the second window has a UI Automation element");
    BstrText framework;
    const HRESULT read = (*window.get()).get_CurrentFrameworkId(framework.receive());
    expect(context, SUCCEEDED(read) && framework.text() == L"GUI.Forms",
           "UI Automation uses the second window's provider");
    ComReference<IUIAutomationElement> button;
    expect(context, find_element(*automation.get(), *window.get(), "uia.button", button) &&
                        element_name(*button.get()) == "Press me",
           "the second window's button is found by its stable ID");
    if (context.kept != nullptr) {
        BstrText name;
        const HRESULT stale = (*context.kept).get_CurrentName(name.receive());
        expect(context, FAILED(stale), "the first window's kept element answers an error");
        (*context.kept).Release();
        context.kept = nullptr;
    }
}

// The client thread: the multithreaded apartment, the checks, then the
// window closes through the UI thread whatever happened.
struct ClientThread final {
    ClientContext* context{};
    void operator()() const {
        const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        try {
            if (FAILED(initialized)) throw std::runtime_error("the client thread has no COM apartment");
            if ((*context).second_window) {
                run_second_window_checks(*context);
            } else {
                run_client_checks(*context);
            }
        } catch (const std::exception& error) {
            (*context).failures.push_back(error.what());
        }
        if (SUCCEEDED(initialized)) CoUninitialize();
        try {
            // A window that closed already needs no close.
            static_cast<void>((*context).dispatcher.invoke(std::function<void()>(CloseWindow{&(*context).request_close})));
        } catch (const std::exception& error) {
            (*context).failures.push_back(std::string("closing the window: ") + error.what());
        }
    }
};

// Called on the UI thread once the window is attached: starts the client
// and returns, so the UI thread's message loop serves the client's calls.
struct HostReady final {
    ClientContext* context{};
    std::thread* client{};
    void operator()(std::function<void()>, std::function<void()> request_close,
                    std::function<HostDialogResult(const HostDialogRequest&)>,
                    std::function<HostServiceStatus(const HostTooltipRequest&)>,
                    std::function<void()>, std::function<HostClipboardTextResult()>,
                    std::function<HostServiceStatus(std::string_view)>) const {
        (*context).request_close = std::move(request_close);
        (*context).hwnd = test_support::own_gui_forms_window(fixture_title);
        (*context).desktop_windows = test_support::desktop_gui_forms_windows(fixture_title);
        if ((*context).hwnd == nullptr) {
            (*context).failures.push_back("the fixture's native window does not exist");
            (*context).request_close();
            return;
        }
        *client = std::thread(ClientThread{context});
    }
};

// -- the decoy process -----------------------------------------------------------
//
// Two copies of a test can run at once (other workspaces run the same
// tests). The test starts a copy of itself with --decoy, which shows a
// window with the same class and title until the test is done, so the
// client's window lookup meets that collision on every run.

// The names of the two events the processes share, made unique by the
// test process's ID: the decoy sets "ready" once its window is shown, and
// waits for "done".
std::wstring decoy_event_name(DWORD test_process, const wchar_t* which) {
    const std::wstring name =
        L"GUIForms.UiaProviderTest." + std::to_wstring(test_process) + L"." + which;
    return name;
}

// An owned Win32 handle, closed when the owner goes.
class HandleOwner final {
public:
    explicit HandleOwner(HANDLE value) noexcept : value_(value) {}
    ~HandleOwner() {
        if (value_ != nullptr) CloseHandle(value_);
    }
    HandleOwner(const HandleOwner&) = delete;
    HandleOwner& operator=(const HandleOwner&) = delete;
    [[nodiscard]] HANDLE get() const noexcept { return value_; }

private:
    HANDLE value_{};
};

struct DecoyClose final {
    std::function<void()>* request_close{};
    void operator()() const { (*request_close)(); }
};

// Waits on its own thread for the test to finish (two minutes at most),
// then closes the decoy's window through the UI thread.
struct DecoyWait final {
    HANDLE done{};
    WindowDispatcher dispatcher;
    std::function<void()>* request_close{};
    void operator()() const {
        static_cast<void>(WaitForSingleObject(done, 120000U));
        // A decoy window that closed already needs no close.
        static_cast<void>(dispatcher.invoke(std::function<void()>(DecoyClose{request_close})));
    }
};

struct DecoyReady final {
    HANDLE ready{};
    HANDLE done{};
    Window* window{};
    std::function<void()>* request_close{};
    std::thread* waiter{};
    void operator()(std::function<void()>, std::function<void()> close,
                    std::function<HostDialogResult(const HostDialogRequest&)>,
                    std::function<HostServiceStatus(const HostTooltipRequest&)>,
                    std::function<void()>, std::function<HostClipboardTextResult()>,
                    std::function<HostServiceStatus(std::string_view)>) const {
        *request_close = std::move(close);
        *waiter = std::thread(DecoyWait{done, (*window).dispatcher(), request_close});
        SetEvent(ready);
    }
};

int run_decoy(const char* test_process_text) {
    const DWORD test_process = static_cast<DWORD>(std::strtoul(test_process_text, nullptr, 10));
    const std::wstring ready_name = decoy_event_name(test_process, L"ready");
    const std::wstring done_name = decoy_event_name(test_process, L"done");
    const HandleOwner ready(OpenEventW(EVENT_MODIFY_STATE, FALSE, ready_name.c_str()));
    const HandleOwner done(OpenEventW(SYNCHRONIZE, FALSE, done_name.c_str()));
    if (ready.get() == nullptr || done.get() == nullptr) return EXIT_FAILURE;
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("uia.decoy.root"));
    std::shared_ptr<Button> button = make_control<Button>(StableId("uia.decoy.button"), "Decoy");
    (*button).set_requested_bounds({10.0, 10.0, 120.0, 28.0});
    (*root).add_child(button);
    std::unique_ptr<Window> window = std::make_unique<Window>(root, Size{420.0, 240.0});
    Window* model = window.get();
    std::function<void()> request_close;
    std::thread waiter;
    host::WindowsHostOptions options;
    options.title = std::string(fixture_title_utf8);
    options.initial_size = Size{420.0, 240.0};
    options.print_metrics_on_close = false;
    options.host_ready = DecoyReady{ready.get(), done.get(), model, &request_close, &waiter};
    const int exit_code = host::run_windows(std::move(window), std::move(options));
    if (waiter.joinable()) waiter.join();
    return exit_code;
}

// The decoy process: started (and shown) by the constructor, stopped by
// stop() or, on an early exit, by the destructor.
class Decoy final {
public:
    Decoy();
    ~Decoy();
    Decoy(const Decoy&) = delete;
    Decoy& operator=(const Decoy&) = delete;
    // Tells the decoy to close and waits for it. Returns its exit code, or
    // -1 when it did not exit within a minute.
    int stop();

private:
    HandleOwner ready_;
    HandleOwner done_;
    PROCESS_INFORMATION process_{};
    bool stopped_{};
};

Decoy::Decoy()
    : ready_(CreateEventW(nullptr, TRUE, FALSE, decoy_event_name(GetCurrentProcessId(), L"ready").c_str())),
      done_(CreateEventW(nullptr, TRUE, FALSE, decoy_event_name(GetCurrentProcessId(), L"done").c_str())) {
    require(ready_.get() != nullptr && done_.get() != nullptr, "the decoy's events are created");
    wchar_t path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    require(length > 0U && length < MAX_PATH, "the test knows its own executable");
    std::wstring command =
        L"\"" + std::wstring(path) + L"\" --decoy " + std::to_wstring(GetCurrentProcessId());
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    const BOOL started = CreateProcessW(path, command.data(), nullptr, nullptr, FALSE, 0, nullptr,
                                        nullptr, &startup, &process_);
    require(started != FALSE, "the decoy process starts");
    const HANDLE waits[2] = {ready_.get(), process_.hProcess};
    const DWORD signalled = WaitForMultipleObjects(2U, waits, FALSE, 60000U);
    require(signalled == WAIT_OBJECT_0, "the decoy shows its window within a minute");
}

int Decoy::stop() {
    stopped_ = true;
    SetEvent(done_.get());
    if (WaitForSingleObject(process_.hProcess, 60000U) != WAIT_OBJECT_0) return -1;
    DWORD code = 0;
    GetExitCodeProcess(process_.hProcess, &code);
    return static_cast<int>(code);
}

Decoy::~Decoy() {
    if (process_.hProcess == nullptr) return;
    if (!stopped_) {
        SetEvent(done_.get());
        if (WaitForSingleObject(process_.hProcess, 60000U) != WAIT_OBJECT_0) {
            TerminateProcess(process_.hProcess, 1U);
        }
    }
    CloseHandle(process_.hThread);
    CloseHandle(process_.hProcess);
}

// Runs one fixture window through host::run_windows, which begins and ends
// the UI thread's COM apartment, with its client.
void run_fixture_window(ClientContext& context) {
    Fixture fixture;
    std::unique_ptr<Window> window = make_fixture(fixture);
    SubscriptionToken clicked = (*fixture.button).clicked().subscribe(CountClick{&fixture.clicks});
    context.dispatcher = (*window).dispatcher();
    context.fixture = &fixture;
    std::thread client;
    host::WindowsHostOptions options;
    options.title = std::string(fixture_title_utf8);
    options.initial_size = Size{420.0, 240.0};
    options.print_metrics_on_close = false;
    options.host_ready = HostReady{&context, &client};
    const int exit_code = host::run_windows(std::move(window), std::move(options));
    if (client.joinable()) client.join();
    context.dispatcher = WindowDispatcher();
    context.fixture = nullptr;
    require(exit_code == 0, "the fixture window did not run");
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::string_view(argv[1]) == "--decoy") return run_decoy(argv[2]);
    try {
        // The multithreaded apartment outlives both clients, so the element
        // the first keeps stays valid for the second to check and release.
        require(host::windows_capabilities().supports(HostCapability::accessibility),
                "the Windows host advertises accessibility");
        CO_MTA_USAGE_COOKIE mta{};
        require(SUCCEEDED(CoIncrementMTAUsage(&mta)), "the multithreaded apartment is kept");
        Decoy decoy;
        ClientContext context;
        run_fixture_window(context);
        ClientContext second;
        second.second_window = true;
        second.kept = context.kept;
        context.kept = nullptr;
        run_fixture_window(second);
        if (second.kept != nullptr) (*second.kept).Release();
        require(decoy.stop() == 0, "the decoy process closes its window and exits cleanly");
        static_cast<void>(CoDecrementMTAUsage(mta));
        std::vector<std::string> failures = context.failures;
        failures.insert(failures.end(), second.failures.begin(), second.failures.end());
        if (!failures.empty()) {
            for (const std::string& failure : failures) {
                std::cerr << "gui_forms_windows_accessibility_uia_provider_tests: " << failure << '\n';
            }
            return EXIT_FAILURE;
        }
        std::cout << "gui_forms_windows_accessibility_uia_provider_tests: " << context.checks + second.checks
                  << " checks passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_windows_accessibility_uia_provider_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
