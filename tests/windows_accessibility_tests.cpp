#include "gui_forms/application.hpp"
#include "gui_forms/basic_controls.hpp"
#include "gui_forms/input_controls.hpp"
#include "gui_forms/container_controls.hpp"
#include <windows.h>
#include <oleacc.h>
#include "support/windows_own_window.hpp"
#include <iostream>
#include <stdexcept>
namespace {
using namespace gui_forms;
void require(bool condition, const char* message) { if (!condition) { throw std::runtime_error(message); } }
struct AccessibleOwner {
    IAccessible* value = nullptr;
    ~AccessibleOwner() { if (value) { (*value).Release(); } }
};
VARIANT self() { VARIANT value{}; value.vt = VT_I4; value.lVal = CHILDID_SELF; return value; }
IAccessible* named(IAccessible& object, const wchar_t* wanted) {
    BSTR name = nullptr; object.get_accName(self(), &name);
    const bool match = name && wcscmp(name, wanted) == 0;
    SysFreeString(name);
    if (match) { object.AddRef(); return &object; }
    LONG count = 0; object.get_accChildCount(&count);
    for (LONG index = 1; index <= count; ++index) {
        VARIANT child{}; child.vt = VT_I4; child.lVal = index;
        IDispatch* dispatch = nullptr;
        if (FAILED(object.get_accChild(child, &dispatch)) || !dispatch) { continue; }
        AccessibleOwner next;
        (*dispatch).QueryInterface(IID_IAccessible, reinterpret_cast<void**>(&next.value)); (*dispatch).Release();
        if (next.value) { IAccessible* result = named(*next.value, wanted); if (result) { return result; } }
    }
    return nullptr;
}
struct Probe {
    int count = 0;
    AccessibleOwner retained;
    void clicked(ButtonBase&) { ++count; }
};
LONG role_of(IAccessible& object) {
    VARIANT role{};
    if (FAILED(object.get_accRole(self(), &role)) || role.vt != VT_I4) { return -1; }
    return role.lVal;
}
struct Ready {
    Probe* probe;
    std::shared_ptr<NumericUpDown> number;
    std::shared_ptr<TextBox> text;
    std::shared_ptr<TabControl> tabs;
    void operator()(gui_forms::Window& window, ApplicationWindowHandle handle) const {
        HWND hwnd = test_support::own_gui_forms_window(L"GUI.Forms accessibility fixture");
        require(hwnd != nullptr, "native window exists");
        AccessibleOwner root;
        require(SUCCEEDED(AccessibleObjectFromWindow(hwnd, OBJID_CLIENT, IID_IAccessible,
            reinterpret_cast<void**>(&root.value))) && root.value, "WM_GETOBJECT publishes MSAA root");
        (*probe).retained.value = named(*root.value, L"Draw mark");
        require((*probe).retained.value, "button named in native accessible tree");
        require(SUCCEEDED((*(*probe).retained.value).accDoDefaultAction(self())) && (*probe).count == 1,
            "native default action invokes retained button");
        AccessibleOwner coordinate; coordinate.value = named(*root.value, L"Canvas X");
        require(coordinate.value, "numeric control named in tree");
        BSTR value = SysAllocString(L"37"); HRESULT status = (*coordinate.value).put_accValue(self(), value); SysFreeString(value);
        require(status == S_OK && (*number).value() == 37, "MSAA numeric value changes retained control");
        AccessibleOwner textbox; textbox.value = named(*root.value, L"Artwork text");
        require(textbox.value, "text control named in tree");
        value = SysAllocString(L"مرحبا ABC"); status = (*textbox.value).put_accValue(self(), value); SysFreeString(value);
        require(status == S_OK && (*text).text.read() == "مرحبا ABC", "MSAA Unicode edit keeps original UTF-8 text");
        LONG x = 0, y = 0, width = 0, height = 0;
        require(SUCCEEDED((*coordinate.value).accLocation(&x, &y, &width, &height, self())) && width > 0 && height > 0,
            "native accessibility reports screen geometry");
        require(window.request_focus(window.find("button")), "button takes focus");
        VARIANT focused{};
        require((*root.value).get_accFocus(&focused) == S_OK && focused.vt == VT_DISPATCH,
            "root focus identifies a descendant accessible object");
        AccessibleOwner focused_child;
        require(SUCCEEDED((*focused.pdispVal).QueryInterface(IID_IAccessible,
            reinterpret_cast<void**>(&focused_child.value))), "focused descendant exposes IAccessible");
        VariantClear(&focused);
        require((*focused_child.value).get_accFocus(&focused) == S_OK && focused.vt == VT_I4 &&
            focused.lVal == CHILDID_SELF, "focus descent terminates with CHILDID_SELF rather than a new self object");
        VariantClear(&focused);
        VARIANT hit{};
        require((*coordinate.value).accHitTest(x + width / 2, y + height / 2, &hit) == S_OK &&
            hit.vt == VT_I4 && hit.lVal == CHILDID_SELF, "hit testing a leaf returns CHILDID_SELF");
        VariantClear(&hit);
        // Docking vocabulary (DK-35, DK-40): a tab page is a property page,
        // and a tab's default action, with press no longer published, is
        // select.
        AccessibleOwner page; page.value = named(*root.value, L"Overview page");
        require(page.value && role_of(*page.value) == ROLE_SYSTEM_PROPERTYPAGE, "a tab panel is an MSAA property page");
        AccessibleOwner tab; tab.value = named(*root.value, L"Details");
        require(tab.value && role_of(*tab.value) == ROLE_SYSTEM_PAGETAB, "a tab is an MSAA page tab");
        BSTR action = nullptr;
        require((*tab.value).get_accDefaultAction(self(), &action) == S_OK && action && wcscmp(action, L"Select") == 0,
            "a tab's default action is select");
        SysFreeString(action);
        require(SUCCEEDED((*tab.value).accDoDefaultAction(self())) && (*tabs).selected_index() == 1U,
            "the default action selects the tab");
        require(handle.request_close().accepted(), "fixture closes");
    }
};
}
int main() {
    try {
        std::shared_ptr<Panel> root = make_control<Panel>(StableId("root"));
        std::shared_ptr<Button> button = make_control<Button>(StableId("button"), "Draw mark");
        (*button).set_requested_bounds({10, 10, 160, 30}); (*root).add_child(button);
        std::shared_ptr<NumericUpDown> number = make_control<NumericUpDown>(StableId("number"));
        (*number).accessible_name = "Canvas X"; (*number).set_requested_bounds({10, 50, 160, 30}); (*number).set_range(0, 200); (*root).add_child(number);
        std::shared_ptr<TextBox> text = make_control<TextBox>(StableId("text"));
        (*text).accessible_name = "Artwork text"; (*text).set_requested_bounds({10, 90, 160, 30}); (*root).add_child(text);
        std::shared_ptr<TabControl> tabs = make_control<TabControl>(StableId("tabs"));
        (*tabs).set_requested_bounds({200, 10, 190, 150}); (*root).add_child(tabs);
        std::shared_ptr<TabPage> overview = make_control<TabPage>(StableId("tabs.overview"), "Overview");
        (*overview).accessible_name = "Overview page"; (*tabs).add_page(overview);
        std::shared_ptr<TabPage> details = make_control<TabPage>(StableId("tabs.details"), "Details");
        (*details).accessible_name = "Details page"; (*tabs).add_page(details);
        Probe probe; SubscriptionToken token = (*button).clicked().subscribe(Delegate<ButtonBase&>::bind<Probe, &Probe::clicked>(probe));
        ApplicationWindowOptions options; options.title = "GUI.Forms accessibility fixture"; options.ready = Ready{&probe, number, text, tabs};
        const ApplicationResult result = Application::run(std::make_unique<gui_forms::Window>(root, Size{400, 300}), options);
        if (result.callback_exception) { std::rethrow_exception(result.callback_exception); }
        require(result.accepted(), "native fixture completed");
        BSTR name = nullptr;
        require((*probe.retained.value).get_accName(self(), &name) == CO_E_OBJNOTCONNECTED && name == nullptr,
            "retained native objects become disconnected after window destruction");
        std::cout << "MSAA discovery, invocation, numeric/text editing, geometry and teardown passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
