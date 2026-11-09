#include "windows_accessibility.hpp"

#include "windows_automation.hpp"
#include "windows_semantic_source.hpp"

#include <oleacc.h>

#include <atomic>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// MSAA publication (stage A).
//
// Objects: one MsaaElement per object a client asks for. Each names its
// node by SemanticElementId and reads the window's current tree for every
// call (SemanticSource::snapshot reuses it only while the window is
// unchanged), so a client never sees a tree older than the call, and an
// object whose node has gone answers CO_E_OBJNOTCONNECTED. Children are full
// objects (get_accChild returns an IDispatch); a positive child ID in a
// VARIANT also reaches child `id` of the object, for clients that use
// simple-element addressing.
//
// Ownership: an element holds a shared reference to its window's
// SemanticSource and nothing else; COM reference counting (atomic, as COM
// requires) ends its life. The source observes the window and is cleared
// when the window goes.
//
// Errors: E_POINTER for a null out-parameter; E_INVALIDARG for a VARIANT
// that is not VT_I4 or names no child; CO_E_OBJNOTCONNECTED when the object's
// node is no longer in the tree; DISP_E_MEMBERNOTFOUND when the node has no
// default action or takes no value; E_FAIL when the control refuses an
// action; S_FALSE with an empty result for a property the node does not
// have.

namespace gui_forms::host {

namespace {

// The longest text a client may set through put_accValue, in UTF-16 code
// units.
constexpr UINT maximum_value_length = 1024U * 1024U;

// Converts a client's text to UTF-8. Returns false for text longer than
// maximum_value_length or not valid UTF-16, leaving `text` empty.
bool utf8_from_bstr(BSTR value, std::string& text) {
    text.clear();
    if (value == nullptr) return true;
    const UINT length = SysStringLen(value);
    if (length > maximum_value_length) return false;
    const std::wstring_view wide(value, length);
    const bool converted = utf8_from_wide(wide, text);
    return converted;
}

bool button_role(SemanticRole role) noexcept {
    const bool button = role == SemanticRole::button || role == SemanticRole::split_button ||
                        role == SemanticRole::menu_button;
    return button;
}

LONG msaa_role(SemanticRole role) noexcept {
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
    // A split container is a pane holding two panes.
    case SemanticRole::split_pane: return ROLE_SYSTEM_PANE;
    case SemanticRole::numeric_field: return ROLE_SYSTEM_SPINBUTTON;
    case SemanticRole::tool_tip: return ROLE_SYSTEM_TOOLTIP;
    // A date field with a drop-down calendar, as a Win32 date picker is.
    case SemanticRole::date_picker: return ROLE_SYSTEM_DROPLIST;
    // A month calendar is a table of day cells.
    case SemanticRole::calendar: return ROLE_SYSTEM_TABLE;
    case SemanticRole::date_cell: return ROLE_SYSTEM_CELL;
    // A property grid is a table of property rows grouped by category.
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
    // A tab's content is MSAA's property page, as a Win32 tab dialog's page
    // is. MSAA has no relation, orientation or second action: the
    // tab-to-panel relation, a tab list's orientation and the close action
    // are published through UI Automation.
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
    // MSAA has no meter role; a meter reads as a progress bar.
    case SemanticRole::meter: return ROLE_SYSTEM_PROGRESSBAR;
    // MSAA has neither: each reads as its fallback.
    case SemanticRole::toggle_switch: return ROLE_SYSTEM_CHECKBUTTON;
    case SemanticRole::search_box: return ROLE_SYSTEM_TEXT;
    case SemanticRole::count: return ROLE_SYSTEM_GROUPING;
    }
    return ROLE_SYSTEM_GROUPING;
}

LONG msaa_state(const SemanticNode& node) noexcept {
    const SemanticState states = node.states;
    LONG bits = 0;
    if (!has_semantic_state(states, SemanticState::enabled)) bits |= STATE_SYSTEM_UNAVAILABLE;
    if (!has_semantic_state(states, SemanticState::visible)) bits |= STATE_SYSTEM_INVISIBLE;
    if (has_semantic_state(states, SemanticState::focused)) bits |= STATE_SYSTEM_FOCUSED;
    if (has_semantic_state(states, SemanticState::focusable)) bits |= STATE_SYSTEM_FOCUSABLE;
    // A toggle button (a button with checked) is pressed, as a Win32
    // push-like check box is; every other checked node is checked.
    if (has_semantic_state(states, SemanticState::checked)) {
        bits |= button_role(node.role) ? STATE_SYSTEM_PRESSED : STATE_SYSTEM_CHECKED;
    }
    if (has_semantic_state(states, SemanticState::mixed)) bits |= STATE_SYSTEM_MIXED;
    if (has_semantic_state(states, SemanticState::selected)) bits |= STATE_SYSTEM_SELECTED;
    if (semantic_has_action(node, SemanticAction::select)) bits |= STATE_SYSTEM_SELECTABLE;
    if (has_semantic_state(states, SemanticState::read_only)) bits |= STATE_SYSTEM_READONLY;
    // A node that can expand and is not expanded is collapsed.
    if (has_semantic_state(states, SemanticState::expanded)) {
        bits |= STATE_SYSTEM_EXPANDED;
    } else if (semantic_has_action(node, SemanticAction::expand)) {
        bits |= STATE_SYSTEM_COLLAPSED;
    }
    if (has_semantic_state(states, SemanticState::protected_content)) bits |= STATE_SYSTEM_PROTECTED;
    if (has_semantic_state(states, SemanticState::busy)) bits |= STATE_SYSTEM_BUSY;
    // MSAA has a has-popup state but no position-in-set field; UI
    // Automation carries PositionInSet and SizeOfSet. It has no invalid
    // state either.
    if (node.has_popup) bits |= STATE_SYSTEM_HASPOPUP;
    // MSAA has no checkable or modal state (the role says it); it has a
    // multiple-selection state.
    if (has_semantic_state(states, SemanticState::multiselectable)) bits |= STATE_SYSTEM_MULTISELECTABLE;
    if (node.role == SemanticRole::link) bits |= STATE_SYSTEM_LINKED;
    return bits;
}

// A node's MSAA default action: the first of press, show menu and select it
// publishes, else expand or collapse by its expanded state. Focus is not a
// default action; a client reaches it through accSelect.
std::optional<SemanticAction> default_action(const SemanticNode& node) noexcept {
    if (semantic_has_action(node, SemanticAction::press)) return SemanticAction::press;
    if (semantic_has_action(node, SemanticAction::show_menu)) return SemanticAction::show_menu;
    if (semantic_has_action(node, SemanticAction::select)) return SemanticAction::select;
    const bool expanded = has_semantic_state(node.states, SemanticState::expanded);
    if (expanded && semantic_has_action(node, SemanticAction::collapse)) return SemanticAction::collapse;
    if (!expanded && semantic_has_action(node, SemanticAction::expand)) return SemanticAction::expand;
    return std::nullopt;
}

// The verb a client reads for the default action: the words the Win32
// control proxies use. Null when the node has none.
const wchar_t* default_action_verb(const SemanticNode& node) noexcept {
    const std::optional<SemanticAction> action = default_action(node);
    if (!action) return nullptr;
    switch (*action) {
    case SemanticAction::press: {
        const bool opens = node.has_popup &&
                           (node.role == SemanticRole::menu_item ||
                            node.role == SemanticRole::menu_bar_item ||
                            node.role == SemanticRole::menu_button);
        if (opens) return L"Open";
        if (checkable_role(node.role)) {
            const bool checked = has_semantic_state(node.states, SemanticState::checked);
            return checked ? L"Uncheck" : L"Check";
        }
        if (node.role == SemanticRole::radio_button) return L"Check";
        if (node.role == SemanticRole::link) return L"Jump";
        if (node.role == SemanticRole::menu_item) return L"Execute";
        return L"Press";
    }
    case SemanticAction::show_menu: return L"Open";
    case SemanticAction::select: return L"Select";
    case SemanticAction::expand: return L"Expand";
    case SemanticAction::collapse: return L"Collapse";
    case SemanticAction::focus:
    case SemanticAction::increment:
    case SemanticAction::decrement:
    case SemanticAction::set_value:
    case SemanticAction::close:
    case SemanticAction::count:
        return nullptr;
    }
    return nullptr;
}

// Takes the window's client tree and, when a tree was announced before and
// a UI Automation client listens, raises the events for the change from it;
// then keeps the new tree as the one announced. A failed snapshot keeps the
// old one, and a reused tree is the one announced, with nothing to raise.
void publish_automation_tree(const std::shared_ptr<SemanticSource>& source) noexcept {
    SemanticSource& state = *source;
    try {
        SemanticTreeRef current;
        if (!state.snapshot(current)) return;
        if (current == state.published) return;
        if (state.published && UiaClientsAreListening()) {
            raise_automation_events(source, *state.published, *current);
        }
        state.published = std::move(current);
    } catch (...) {
    }
}

// Which text property a text getter reads.
enum class TextProperty : std::uint8_t { name, value, description, keyboard_shortcut };

class MsaaElement final : public IAccessible {
public:
    MsaaElement(std::shared_ptr<SemanticSource> source, SemanticElementId id) noexcept
        : source_(std::move(source)), id_(id) {}
    MsaaElement(const MsaaElement&) = delete;
    MsaaElement& operator=(const MsaaElement&) = delete;

    // IUnknown and IDispatch. The dispatch interface carries no type
    // information: clients call IAccessible directly.
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT* out) override;
    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT, LCID, ITypeInfo** out) override;
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID, LPOLESTR*, UINT, LCID, DISPID*) override;
    HRESULT STDMETHODCALLTYPE Invoke(DISPID, REFIID, LCID, WORD, DISPPARAMS*, VARIANT*,
                                     EXCEPINFO*, UINT*) override;

    // IAccessible.
    HRESULT STDMETHODCALLTYPE get_accParent(IDispatch** out) override;
    HRESULT STDMETHODCALLTYPE get_accChildCount(LONG* out) override;
    HRESULT STDMETHODCALLTYPE get_accChild(VARIANT child, IDispatch** out) override;
    HRESULT STDMETHODCALLTYPE get_accName(VARIANT child, BSTR* out) override;
    HRESULT STDMETHODCALLTYPE get_accValue(VARIANT child, BSTR* out) override;
    HRESULT STDMETHODCALLTYPE get_accDescription(VARIANT child, BSTR* out) override;
    HRESULT STDMETHODCALLTYPE get_accRole(VARIANT child, VARIANT* out) override;
    HRESULT STDMETHODCALLTYPE get_accState(VARIANT child, VARIANT* out) override;
    HRESULT STDMETHODCALLTYPE get_accHelp(VARIANT child, BSTR* out) override;
    HRESULT STDMETHODCALLTYPE get_accHelpTopic(BSTR* file, VARIANT child, LONG* topic) override;
    HRESULT STDMETHODCALLTYPE get_accKeyboardShortcut(VARIANT child, BSTR* out) override;
    HRESULT STDMETHODCALLTYPE get_accFocus(VARIANT* out) override;
    HRESULT STDMETHODCALLTYPE get_accSelection(VARIANT* out) override;
    HRESULT STDMETHODCALLTYPE get_accDefaultAction(VARIANT child, BSTR* out) override;
    HRESULT STDMETHODCALLTYPE accSelect(LONG flags, VARIANT child) override;
    HRESULT STDMETHODCALLTYPE accLocation(LONG* x, LONG* y, LONG* width, LONG* height,
                                          VARIANT child) override;
    HRESULT STDMETHODCALLTYPE accNavigate(LONG direction, VARIANT start, VARIANT* out) override;
    HRESULT STDMETHODCALLTYPE accHitTest(LONG x, LONG y, VARIANT* out) override;
    HRESULT STDMETHODCALLTYPE accDoDefaultAction(VARIANT child) override;
    HRESULT STDMETHODCALLTYPE put_accName(VARIANT child, BSTR value) override;
    HRESULT STDMETHODCALLTYPE put_accValue(VARIANT child, BSTR value) override;

private:
    ~MsaaElement() = default;

    // Takes a snapshot into `client` and finds this object's node.
    HRESULT resolve_self(SemanticTreeRef& client, SemanticLocation& self) noexcept;
    // As resolve_self, then the node `child` names: this object's node for
    // CHILDID_SELF, else its child `child.lVal` (counting from one).
    HRESULT resolve(const VARIANT& child, SemanticTreeRef& client, SemanticLocation& target) noexcept;
    // A new object for the located node, returned as IDispatch.
    HRESULT object_for(const SemanticLocation& location, IDispatch** out) noexcept;
    // The VARIANT a navigation result takes: VT_I4 CHILDID_SELF for this
    // object's own node (a new object for it would make focus and hit-test
    // descent recurse forever), VT_DISPATCH for another node.
    HRESULT variant_for(const SemanticLocation& found, VARIANT* out) noexcept;
    HRESULT text_property(const VARIANT& child, BSTR* out, TextProperty property) noexcept;

    std::atomic<ULONG> references_{1U};
    std::shared_ptr<SemanticSource> source_;
    SemanticElementId id_;
};

HRESULT MsaaElement::resolve_self(SemanticTreeRef& client, SemanticLocation& self) noexcept {
    if (!(*source_).snapshot(client)) return CO_E_OBJNOTCONNECTED;
    if (!locate_semantic_element(*client, id_, self)) return CO_E_OBJNOTCONNECTED;
    return S_OK;
}

HRESULT MsaaElement::resolve(const VARIANT& child, SemanticTreeRef& client,
                             SemanticLocation& target) noexcept {
    if (child.vt != VT_I4) return E_INVALIDARG;
    SemanticLocation self;
    const HRESULT status = resolve_self(client, self);
    if (FAILED(status)) return status;
    if (child.lVal == CHILDID_SELF) {
        target = self;
        return S_OK;
    }
    const std::size_t count = (*self.node).children.size();
    if (child.lVal < 0 || static_cast<std::size_t>(child.lVal) > count) return E_INVALIDARG;
    target = child_location(self, static_cast<std::size_t>(child.lVal) - 1U);
    return S_OK;
}

HRESULT MsaaElement::object_for(const SemanticLocation& location, IDispatch** out) noexcept {
    MsaaElement* element = new (std::nothrow) MsaaElement(source_, location.id);
    if (element == nullptr) return E_OUTOFMEMORY;
    *out = static_cast<IDispatch*>(element);
    return S_OK;
}

HRESULT MsaaElement::variant_for(const SemanticLocation& found, VARIANT* out) noexcept {
    if (found.id == id_) {
        (*out).vt = VT_I4;
        (*out).lVal = CHILDID_SELF;
        return S_OK;
    }
    IDispatch* dispatch = nullptr;
    const HRESULT status = object_for(found, &dispatch);
    if (FAILED(status)) return status;
    (*out).vt = VT_DISPATCH;
    (*out).pdispVal = dispatch;
    return S_OK;
}

HRESULT MsaaElement::text_property(const VARIANT& child, BSTR* out,
                                   TextProperty property) noexcept {
    if (out == nullptr) return E_POINTER;
    *out = nullptr;
    SemanticTreeRef client;
    SemanticLocation target;
    const HRESULT status = resolve(child, client, target);
    if (FAILED(status)) return status;
    const SemanticNode& node = *target.node;
    switch (property) {
    case TextProperty::name: return utf8_to_bstr(node.name, out);
    case TextProperty::value: return utf8_to_bstr(node.value, out);
    case TextProperty::description: return utf8_to_bstr(node.description, out);
    case TextProperty::keyboard_shortcut: {
        // A mnemonic is reached with Alt, as Win32 menus and dialogs publish it.
        if (node.access_key.empty()) return S_FALSE;
        const std::string shortcut = "Alt+" + node.access_key;
        return utf8_to_bstr(shortcut, out);
    }
    }
    return S_FALSE;
}

HRESULT STDMETHODCALLTYPE MsaaElement::QueryInterface(REFIID iid, void** out) {
    if (out == nullptr) return E_POINTER;
    *out = nullptr;
    const bool known = iid == IID_IUnknown || iid == IID_IDispatch || iid == IID_IAccessible;
    if (!known) return E_NOINTERFACE;
    *out = static_cast<IAccessible*>(this);
    AddRef();
    return S_OK;
}

ULONG STDMETHODCALLTYPE MsaaElement::AddRef() {
    const ULONG count = ++references_;
    return count;
}

ULONG STDMETHODCALLTYPE MsaaElement::Release() {
    const ULONG count = --references_;
    if (count == 0U) delete this;
    return count;
}

HRESULT STDMETHODCALLTYPE MsaaElement::GetTypeInfoCount(UINT* out) {
    if (out == nullptr) return E_POINTER;
    *out = 0U;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE MsaaElement::GetTypeInfo(UINT, LCID, ITypeInfo** out) {
    if (out != nullptr) *out = nullptr;
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE MsaaElement::GetIDsOfNames(REFIID, LPOLESTR*, UINT, LCID, DISPID*) {
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE MsaaElement::Invoke(DISPID, REFIID, LCID, WORD, DISPPARAMS*, VARIANT*,
                                             EXCEPINFO*, UINT*) {
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE MsaaElement::get_accParent(IDispatch** out) {
    if (out == nullptr) return E_POINTER;
    *out = nullptr;
    SemanticTreeRef client;
    SemanticLocation self;
    const HRESULT status = resolve_self(client, self);
    if (FAILED(status)) return status;
    if (self.parent == nullptr) {
        // The client object's parent is the window's standard object.
        return CreateStdAccessibleObject((*source_).hwnd, OBJID_WINDOW, IID_IDispatch,
                                         reinterpret_cast<void**>(out));
    }
    SemanticLocation parent;
    if (!parent_semantic_location(*client, self, parent)) return CO_E_OBJNOTCONNECTED;
    return object_for(parent, out);
}

HRESULT STDMETHODCALLTYPE MsaaElement::get_accChildCount(LONG* out) {
    if (out == nullptr) return E_POINTER;
    *out = 0;
    SemanticTreeRef client;
    SemanticLocation self;
    const HRESULT status = resolve_self(client, self);
    if (FAILED(status)) return status;
    *out = static_cast<LONG>((*self.node).children.size());
    return S_OK;
}

HRESULT STDMETHODCALLTYPE MsaaElement::get_accChild(VARIANT child, IDispatch** out) {
    if (out == nullptr) return E_POINTER;
    *out = nullptr;
    if (child.vt == VT_I4 && child.lVal == CHILDID_SELF) return E_INVALIDARG;
    SemanticTreeRef client;
    SemanticLocation target;
    const HRESULT status = resolve(child, client, target);
    if (FAILED(status)) return status;
    return object_for(target, out);
}

HRESULT STDMETHODCALLTYPE MsaaElement::get_accName(VARIANT child, BSTR* out) {
    return text_property(child, out, TextProperty::name);
}

HRESULT STDMETHODCALLTYPE MsaaElement::get_accValue(VARIANT child, BSTR* out) {
    return text_property(child, out, TextProperty::value);
}

HRESULT STDMETHODCALLTYPE MsaaElement::get_accDescription(VARIANT child, BSTR* out) {
    return text_property(child, out, TextProperty::description);
}

HRESULT STDMETHODCALLTYPE MsaaElement::get_accRole(VARIANT child, VARIANT* out) {
    if (out == nullptr) return E_POINTER;
    VariantInit(out);
    SemanticTreeRef client;
    SemanticLocation target;
    const HRESULT status = resolve(child, client, target);
    if (FAILED(status)) return status;
    const bool client_object = target.parent == nullptr;
    (*out).vt = VT_I4;
    (*out).lVal = client_object ? ROLE_SYSTEM_CLIENT : msaa_role((*target.node).role);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE MsaaElement::get_accState(VARIANT child, VARIANT* out) {
    if (out == nullptr) return E_POINTER;
    VariantInit(out);
    SemanticTreeRef client;
    SemanticLocation target;
    const HRESULT status = resolve(child, client, target);
    if (FAILED(status)) return status;
    (*out).vt = VT_I4;
    (*out).lVal = msaa_state(*target.node);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE MsaaElement::get_accHelp(VARIANT child, BSTR* out) {
    return text_property(child, out, TextProperty::description);
}

HRESULT STDMETHODCALLTYPE MsaaElement::get_accHelpTopic(BSTR* file, VARIANT, LONG* topic) {
    if (file != nullptr) *file = nullptr;
    if (topic != nullptr) *topic = 0;
    return S_FALSE;
}

HRESULT STDMETHODCALLTYPE MsaaElement::get_accKeyboardShortcut(VARIANT child, BSTR* out) {
    return text_property(child, out, TextProperty::keyboard_shortcut);
}

HRESULT STDMETHODCALLTYPE MsaaElement::get_accFocus(VARIANT* out) {
    if (out == nullptr) return E_POINTER;
    VariantInit(out);
    SemanticTreeRef client;
    SemanticLocation self;
    const HRESULT status = resolve_self(client, self);
    if (FAILED(status)) return status;
    SemanticLocation found;
    if (!find_state_location(self, SemanticState::focused, found)) return S_FALSE;
    return variant_for(found, out);
}

HRESULT STDMETHODCALLTYPE MsaaElement::get_accSelection(VARIANT* out) {
    if (out == nullptr) return E_POINTER;
    VariantInit(out);
    SemanticTreeRef client;
    SemanticLocation self;
    const HRESULT status = resolve_self(client, self);
    if (FAILED(status)) return status;
    SemanticLocation found;
    if (!find_state_location(self, SemanticState::selected, found)) return S_FALSE;
    return variant_for(found, out);
}

HRESULT STDMETHODCALLTYPE MsaaElement::get_accDefaultAction(VARIANT child, BSTR* out) {
    if (out == nullptr) return E_POINTER;
    *out = nullptr;
    SemanticTreeRef client;
    SemanticLocation target;
    const HRESULT status = resolve(child, client, target);
    if (FAILED(status)) return status;
    if (target.parent == nullptr) return S_FALSE;
    const wchar_t* verb = default_action_verb(*target.node);
    if (verb == nullptr) return S_FALSE;
    *out = SysAllocString(verb);
    if (*out == nullptr) return E_OUTOFMEMORY;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE MsaaElement::accSelect(LONG flags, VARIANT child) {
    const LONG supported = SELFLAG_TAKEFOCUS | SELFLAG_TAKESELECTION;
    if (flags == 0 || (flags & ~supported) != 0) return E_INVALIDARG;
    SemanticTreeRef client;
    SemanticLocation target;
    const HRESULT status = resolve(child, client, target);
    if (FAILED(status)) return status;
    bool done = true;
    if ((flags & SELFLAG_TAKEFOCUS) != 0) {
        done = (*source_).perform(target, SemanticAction::focus, {});
    }
    if ((flags & SELFLAG_TAKESELECTION) != 0) {
        // The focus action may have changed the tree; select reads it again.
        SemanticTreeRef current;
        SemanticLocation selected;
        const HRESULT again = resolve(child, current, selected);
        if (FAILED(again)) return again;
        const bool taken = (*source_).perform(selected, SemanticAction::select, {});
        done = done && taken;
    }
    return done ? S_OK : E_FAIL;
}

HRESULT STDMETHODCALLTYPE MsaaElement::accLocation(LONG* x, LONG* y, LONG* width, LONG* height,
                                                  VARIANT child) {
    if (x == nullptr || y == nullptr || width == nullptr || height == nullptr) return E_POINTER;
    *x = 0;
    *y = 0;
    *width = 0;
    *height = 0;
    SemanticTreeRef client;
    SemanticLocation target;
    const HRESULT status = resolve(child, client, target);
    if (FAILED(status)) return status;
    const Rect bounds = (*target.node).bounds;
    const double scale = (*source_).scale;
    POINT origin{};
    ClientToScreen((*source_).hwnd, &origin);
    *x = origin.x + static_cast<LONG>(std::lround(bounds.x * scale));
    *y = origin.y + static_cast<LONG>(std::lround(bounds.y * scale));
    *width = static_cast<LONG>(std::lround(bounds.width * scale));
    *height = static_cast<LONG>(std::lround(bounds.height * scale));
    return S_OK;
}

HRESULT STDMETHODCALLTYPE MsaaElement::accNavigate(LONG direction, VARIANT start, VARIANT* out) {
    if (out == nullptr) return E_POINTER;
    VariantInit(out);
    SemanticTreeRef client;
    SemanticLocation from;
    const HRESULT status = resolve(start, client, from);
    if (FAILED(status)) return status;
    const std::size_t count = (*from.node).children.size();
    if (direction == NAVDIR_FIRSTCHILD || direction == NAVDIR_LASTCHILD) {
        if (count == 0U) return S_FALSE;
        const std::size_t index = direction == NAVDIR_FIRSTCHILD ? 0U : count - 1U;
        return variant_for(child_location(from, index), out);
    }
    if (direction != NAVDIR_NEXT && direction != NAVDIR_PREVIOUS) return S_FALSE;
    SemanticLocation parent;
    if (!parent_semantic_location(*client, from, parent)) return S_FALSE;
    const std::size_t siblings = (*parent.node).children.size();
    if (direction == NAVDIR_NEXT && from.index + 1U < siblings) {
        return variant_for(child_location(parent, from.index + 1U), out);
    }
    if (direction == NAVDIR_PREVIOUS && from.index > 0U) {
        return variant_for(child_location(parent, from.index - 1U), out);
    }
    return S_FALSE;
}

HRESULT STDMETHODCALLTYPE MsaaElement::accHitTest(LONG x, LONG y, VARIANT* out) {
    if (out == nullptr) return E_POINTER;
    VariantInit(out);
    SemanticTreeRef client;
    SemanticLocation self;
    const HRESULT status = resolve_self(client, self);
    if (FAILED(status)) return status;
    POINT origin{};
    ClientToScreen((*source_).hwnd, &origin);
    const double scale = (*source_).scale;
    const double logical_x = (x - origin.x) / scale;
    const double logical_y = (y - origin.y) / scale;
    SemanticLocation hit;
    if (!hit_semantic_location(self, logical_x, logical_y, hit)) return S_FALSE;
    return variant_for(hit, out);
}

HRESULT STDMETHODCALLTYPE MsaaElement::accDoDefaultAction(VARIANT child) {
    SemanticTreeRef client;
    SemanticLocation target;
    const HRESULT status = resolve(child, client, target);
    if (FAILED(status)) return status;
    if (target.parent == nullptr) return DISP_E_MEMBERNOTFOUND;
    const std::optional<SemanticAction> action = default_action(*target.node);
    if (!action) return DISP_E_MEMBERNOTFOUND;
    const bool handled = (*source_).perform(target, *action, {});
    return handled ? S_OK : E_FAIL;
}

HRESULT STDMETHODCALLTYPE MsaaElement::put_accName(VARIANT, BSTR) {
    return E_NOTIMPL;
}

HRESULT STDMETHODCALLTYPE MsaaElement::put_accValue(VARIANT child, BSTR value) {
    SemanticTreeRef client;
    SemanticLocation target;
    const HRESULT status = resolve(child, client, target);
    if (FAILED(status)) return status;
    if (!semantic_has_action(*target.node, SemanticAction::set_value)) return DISP_E_MEMBERNOTFOUND;
    std::string text;
    try {
        if (!utf8_from_bstr(value, text)) return E_INVALIDARG;
    } catch (...) {
        return E_OUTOFMEMORY;
    }
    const bool handled = (*source_).perform(target, SemanticAction::set_value, text);
    return handled ? S_OK : E_FAIL;
}

} // namespace

WindowsAccessibility::WindowsAccessibility() : source_(std::make_shared<SemanticSource>()) {}

WindowsAccessibility::~WindowsAccessibility() { detach(); }

void WindowsAccessibility::attach(HWND hwnd, Window& model, double scale) {
    (*source_).hwnd = hwnd;
    (*source_).model = &model;
    (*source_).scale = scale;
}

void WindowsAccessibility::detach() noexcept {
    SemanticSource& source = *source_;
    // UI Automation holds the providers it was handed for this window until
    // told the window is going.
    if (source.automation_observed && source.hwnd != nullptr) {
        UiaReturnRawElementProvider(source.hwnd, 0, 0, nullptr);
    }
    source.model = nullptr;
    source.hwnd = nullptr;
    source.release_trees();
}

LRESULT WindowsAccessibility::object(WPARAM parameter) noexcept {
    if ((*source_).model == nullptr) return 0;
    (*source_).observed = true;
    MsaaElement* root = new (std::nothrow) MsaaElement(source_, SemanticElementId{});
    if (root == nullptr) return 0;
    const LRESULT result = LresultFromObject(IID_IAccessible, parameter, root);
    (*root).Release();
    return result;
}

namespace {

// Whether this thread's COM apartment is pinned for UI Automation.
thread_local bool automation_apartment_pinned = false;

} // namespace

// UI Automation does not accept a thread's providers after that thread's COM
// apartment ended and began again while a client still held an element:
// when a run ended the UI thread's apartment (the host's last
// CoUninitialize) and the next run began a new one on the same thread, the
// new window's UiaReturnRawElementProvider answered E_INVALIDARG and UI
// Automation served that window from the system proxies (measured: every
// time with an element kept across the runs, about half the time under
// load without). Releasing every provider (UiaDisconnectAllProviders)
// before the apartment ended did not help. So the first provider a thread
// serves takes one more reference on the thread's apartment and never
// releases it: the apartment ends when the thread does.
void WindowsAccessibility::pin_automation_apartment() noexcept {
    if (automation_apartment_pinned) return;
    const HRESULT pinned = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    automation_apartment_pinned = SUCCEEDED(pinned);
}

bool WindowsAccessibility::automation_request(LPARAM lparam) noexcept {
    // WM_GETOBJECT carries the object ID in the low 32 bits of lparam.
    const bool requested = static_cast<LONG>(lparam) == static_cast<LONG>(UiaRootObjectId);
    return requested;
}

LRESULT WindowsAccessibility::automation_object(WPARAM wparam, LPARAM lparam) noexcept {
    SemanticSource& source = *source_;
    if (source.model == nullptr || source.hwnd == nullptr) return 0;
    pin_automation_apartment();
    if (!source.automation_observed) {
        // The tree the first client sees is the one later changes are
        // compared against.
        source.automation_observed = true;
        publish_automation_tree(source_);
    }
    IRawElementProviderSimple* root = make_automation_element(source_, SemanticElementId{});
    if (root == nullptr) return 0;
    const LRESULT result = UiaReturnRawElementProvider(source.hwnd, wparam, lparam, root);
    (*root).Release();
    return result;
}

void WindowsAccessibility::notify(double scale) noexcept {
    SemanticSource& source = *source_;
    source.scale = scale;
    if (source.model == nullptr || (!source.observed && !source.automation_observed)) return;
    if ((*source.model).semantic_generation() == source.generation) return;
    try {
        // The tree is the source's current one, which the UI Automation
        // publication below reuses.
        SemanticTreeRef tree;
        if (!source.snapshot(tree)) return;
        const std::uint64_t generation = source.current_key.semantic_generation;
        if (generation == source.generation) return;
        source.generation = generation;
        if (source.automation_observed) publish_automation_tree(source_);
        if (!source.observed) return;
        NotifyWinEvent(EVENT_OBJECT_REORDER, source.hwnd, OBJID_CLIENT, CHILDID_SELF);
        NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, source.hwnd, OBJID_CLIENT, CHILDID_SELF);
        NotifyWinEvent(EVENT_OBJECT_STATECHANGE, source.hwnd, OBJID_CLIENT, CHILDID_SELF);
        std::uint64_t focused = 0U;
        for (const SemanticNode& root : (*tree).children) {
            const SemanticLocation root_at = client_location(root);
            SemanticLocation found;
            if (find_state_location(root_at, SemanticState::focused, found)) {
                focused = (*found.node).runtime_id;
                break;
            }
        }
        if (focused != source.focus) {
            source.focus = focused;
            NotifyWinEvent(EVENT_OBJECT_FOCUS, source.hwnd, OBJID_CLIENT, CHILDID_SELF);
        }
    } catch (...) {
    }
}

} // namespace gui_forms::host
