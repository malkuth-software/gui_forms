// Stage A, A-W3: UI Automation's TextPattern, read through IUIAutomation.
//
// A fixture window (a single-line text box whose text holds a combining
// sequence, a multiline text box, a password box and a text box the test
// removes) runs in a real Win32 window through host::run_windows. A client
// thread in the multithreaded apartment reads it through IUIAutomation, as a
// separate client would; it reads and changes the model only through
// Window::invoke, which runs on the UI thread. It checks:
// - which nodes offer TextPattern and TextPattern2 (not a password box);
// - the document range's text, a cut GetText, FindText and IsReadOnly;
// - the units: a character is a grapheme (a combining sequence is one
//   character), words, lines and paragraphs, each against the model's text;
// - GetSelection and the caret range against the model's selection, and
//   Select setting the model's selection;
// - bounding rectangles (one per line) and RangeFromPoint;
// - TextSelectionChanged and TextChanged reach a client's handler;
// - a range of a removed control answers UIA_E_ELEMENTNOTAVAILABLE.

#include "gui_forms/gui_forms.hpp"
#include "gui_forms/platform/windows_host.hpp"
#include "support/windows_own_window.hpp"

#include <windows.h>
#include <ole2.h>
#include <uiautomation.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

using namespace gui_forms;

constexpr const wchar_t* fixture_title = L"GUI.Forms UI Automation text test";
constexpr std::string_view fixture_title_utf8 = "GUI.Forms UI Automation text test";

// "Café au lait" with the é spelled e and U+0301 COMBINING ACUTE ACCENT:
// bytes 3 to 6 are one grapheme.
constexpr std::string_view single_text = "Cafe\xCC\x81 au lait";
constexpr std::string_view multi_text = "First line\nSecond line";

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
    [[nodiscard]] bool null() const noexcept { return value_ == nullptr; }
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

// -- the fixture -------------------------------------------------------------

struct Fixture final {
    std::shared_ptr<Panel> root;
    std::shared_ptr<TextBox> single;
    std::shared_ptr<TextBox> multi;
    std::shared_ptr<TextBox> password;
    std::shared_ptr<TextBox> doomed;
};

std::unique_ptr<Window> make_fixture(Fixture& fixture) {
    fixture.root = make_control<Panel>(StableId("uiatext.root"));
    fixture.single = make_control<TextBox>(StableId("uiatext.single"));
    fixture.multi = make_control<TextBox>(StableId("uiatext.multi"));
    fixture.password = make_control<TextBox>(StableId("uiatext.password"));
    fixture.doomed = make_control<TextBox>(StableId("uiatext.doomed"));
    (*fixture.single).set_requested_bounds({10.0, 10.0, 260.0, 26.0});
    (*fixture.single).set_text(std::string(single_text));
    (*fixture.multi).set_requested_bounds({10.0, 50.0, 260.0, 80.0});
    (*fixture.multi).set_multiline(true);
    (*fixture.multi).set_text(std::string(multi_text));
    (*fixture.password).set_requested_bounds({10.0, 140.0, 260.0, 26.0});
    (*fixture.password).set_use_system_password_character(true);
    (*fixture.password).set_text("secret");
    (*fixture.doomed).set_requested_bounds({10.0, 180.0, 260.0, 26.0});
    (*fixture.doomed).set_text("Removed");
    (*fixture.root).add_child(fixture.single);
    (*fixture.root).add_child(fixture.multi);
    (*fixture.root).add_child(fixture.password);
    (*fixture.root).add_child(fixture.doomed);
    std::unique_ptr<Window> window = std::make_unique<Window>(fixture.root, Size{300.0, 220.0});
    return window;
}

// -- work the client runs on the UI thread ----------------------------------

struct ModelState final {
    TextSelection single_selection{};
    std::string single_text;
    std::string multi_text;
};

struct ReadModel final {
    Fixture* fixture{};
    ModelState* state{};
    void operator()() const {
        (*state).single_selection = (*(*fixture).single).selection();
        (*state).single_text = std::string((*(*fixture).single).text());
        (*state).multi_text = std::string((*(*fixture).multi).text());
    }
};

struct SelectSingle final {
    Fixture* fixture{};
    std::size_t anchor{};
    std::size_t caret{};
    void operator()() const { (*(*fixture).single).select(Utf8Offset{anchor}, Utf8Offset{caret}); }
};

struct SetSingleText final {
    Fixture* fixture{};
    std::string text;
    void operator()() const { (*(*fixture).single).set_text(text); }
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

// -- an event handler -----------------------------------------------------------

// Counts the automation events UI Automation delivers to it, on UI
// Automation's own threads (hence the atomic counter).
class EventCounter final : public IUIAutomationEventHandler {
public:
    EventCounter() noexcept = default;
    EventCounter(const EventCounter&) = delete;
    EventCounter& operator=(const EventCounter&) = delete;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (out == nullptr) return E_POINTER;
        *out = nullptr;
        if (iid != IID_IUnknown && iid != __uuidof(IUIAutomationEventHandler)) return E_NOINTERFACE;
        *out = static_cast<IUIAutomationEventHandler*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {
        const ULONG count = ++references_;
        return count;
    }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG count = --references_;
        if (count == 0U) delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE HandleAutomationEvent(IUIAutomationElement*, EVENTID) override {
        ++events_;
        return S_OK;
    }
    [[nodiscard]] int events() const noexcept { return events_.load(); }

private:
    ~EventCounter() = default;
    std::atomic<ULONG> references_{1U};
    std::atomic<int> events_{0};
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

template <typename Pattern>
bool pattern(IUIAutomationElement& element, PATTERNID id, ComReference<Pattern>& found) {
    const HRESULT status = element.GetCurrentPatternAs(id, __uuidof(Pattern),
                                                       reinterpret_cast<void**>(found.receive()));
    const bool result = SUCCEEDED(status) && found.get() != nullptr;
    return result;
}

// The range's whole text, or "<error>" when GetText fails.
std::wstring range_text(IUIAutomationTextRange& range) {
    BstrText text;
    if (FAILED(range.GetText(-1, text.receive()))) return L"<error>";
    const std::wstring result = text.text();
    return result;
}

// A copy of `range` collapsed to its start.
bool collapsed_start(IUIAutomationTextRange& range, ComReference<IUIAutomationTextRange>& copy) {
    if (FAILED(range.Clone(copy.receive())) || copy.get() == nullptr) return false;
    const HRESULT moved = (*copy.get()).MoveEndpointByRange(TextPatternRangeEndpoint_End, copy.get(),
                                                            TextPatternRangeEndpoint_Start);
    return SUCCEEDED(moved);
}

// The rectangles GetBoundingRectangles returns: four doubles each.
std::vector<double> bounding_values(IUIAutomationTextRange& range) {
    std::vector<double> values;
    SAFEARRAY* array = nullptr;
    if (FAILED(range.GetBoundingRectangles(&array)) || array == nullptr) return values;
    LONG lower = 0;
    LONG upper = -1;
    SafeArrayGetLBound(array, 1, &lower);
    SafeArrayGetUBound(array, 1, &upper);
    for (LONG index = lower; index <= upper; ++index) {
        double value = 0.0;
        SafeArrayGetElement(array, &index, &value);
        values.push_back(value);
    }
    SafeArrayDestroy(array);
    return values;
}

// The text of the element's first selection range, or "<none>".
std::wstring selection_text(IUIAutomationTextPattern& text) {
    ComReference<IUIAutomationTextRangeArray> ranges;
    if (FAILED(text.GetSelection(ranges.receive())) || ranges.get() == nullptr) return L"<none>";
    int length = 0;
    if (FAILED((*ranges.get()).get_Length(&length)) || length != 1) return L"<none>";
    ComReference<IUIAutomationTextRange> range;
    if (FAILED((*ranges.get()).GetElement(0, range.receive())) || range.get() == nullptr) return L"<none>";
    const std::wstring result = range_text(*range.get());
    return result;
}

bool text_pattern_of(ClientContext& context, IUIAutomation& automation, IUIAutomationElement& window,
                     std::string_view id, ComReference<IUIAutomationElement>& element,
                     ComReference<IUIAutomationTextPattern>& text) {
    if (!find_element(automation, window, id, element)) {
        expect(context, false, std::string(id) + " is found by its stable ID");
        return false;
    }
    const bool offered = pattern(*element.get(), UIA_TextPatternId, text);
    expect(context, offered, std::string(id) + " offers TextPattern");
    return offered;
}

void check_offered(ClientContext& context, IUIAutomation& automation, IUIAutomationElement& window) {
    ComReference<IUIAutomationElement> single;
    ComReference<IUIAutomationTextPattern2> text2;
    expect(context, find_element(automation, window, "uiatext.single", single) &&
                        pattern(*single.get(), UIA_TextPattern2Id, text2),
           "a text box offers TextPattern2");
    ComReference<IUIAutomationElement> password;
    ComReference<IUIAutomationTextPattern> text;
    expect(context, find_element(automation, window, "uiatext.password", password) &&
                        !pattern(*password.get(), UIA_TextPatternId, text),
           "a password box offers no TextPattern");
    if (text2.get() == nullptr) return;
    SupportedTextSelection supported = SupportedTextSelection_None;
    expect(context, SUCCEEDED((*text2.get()).get_SupportedTextSelection(&supported)) &&
                        supported == SupportedTextSelection_Single,
           "a text box supports a single selection");
}

void check_document(ClientContext& context, IUIAutomation& automation, IUIAutomationElement& window) {
    ComReference<IUIAutomationElement> element;
    ComReference<IUIAutomationTextPattern> text;
    if (!text_pattern_of(context, automation, window, "uiatext.single", element, text)) return;
    ComReference<IUIAutomationTextRange> document;
    require(SUCCEEDED((*text.get()).get_DocumentRange(document.receive())) && document.get() != nullptr,
            "the single-line box has a document range");
    expect(context, range_text(*document.get()) == wide(single_text),
           "the document range holds the text box's text");
    BstrText cut;
    expect(context, SUCCEEDED((*document.get()).GetText(3, cut.receive())) && cut.text() == L"Caf",
           "GetText keeps the first maxLength UTF-16 units");
    ComReference<IUIAutomationTextRange> found;
    BSTR wanted = SysAllocString(L"LAIT");
    const HRESULT searched = (*document.get()).FindText(wanted, FALSE, TRUE, found.receive());
    SysFreeString(wanted);
    expect(context, SUCCEEDED(searched) && found.get() != nullptr && range_text(*found.get()) == L"lait",
           "FindText ignoring case finds a word");
    VARIANT read_only;
    VariantInit(&read_only);
    expect(context, SUCCEEDED((*document.get()).GetAttributeValue(UIA_IsReadOnlyAttributeId, &read_only)) &&
                        read_only.vt == VT_BOOL && read_only.boolVal == VARIANT_FALSE,
           "IsReadOnly is false for an editable box");
    VariantClear(&read_only);
    ComReference<IUIAutomationElement> enclosing;
    BstrText id;
    expect(context, SUCCEEDED((*document.get()).GetEnclosingElement(enclosing.receive())) &&
                        enclosing.get() != nullptr &&
                        SUCCEEDED((*enclosing.get()).get_CurrentAutomationId(id.receive())) &&
                        id.text() == L"uiatext.single",
           "a range's enclosing element is its text box");
}

void check_units(ClientContext& context, IUIAutomation& automation, IUIAutomationElement& window) {
    ComReference<IUIAutomationElement> element;
    ComReference<IUIAutomationTextPattern> text;
    if (!text_pattern_of(context, automation, window, "uiatext.single", element, text)) return;
    ComReference<IUIAutomationTextRange> document;
    require(SUCCEEDED((*text.get()).get_DocumentRange(document.receive())), "a document range");
    ComReference<IUIAutomationTextRange> range;
    require(collapsed_start(*document.get(), range), "a degenerate range at the start");
    int moved = 0;
    expect(context, SUCCEEDED((*range.get()).Move(TextUnit_Character, 3, &moved)) && moved == 3 &&
                        range_text(*range.get()).empty(),
           "a degenerate range moves three characters and stays degenerate");
    expect(context, SUCCEEDED((*range.get()).ExpandToEnclosingUnit(TextUnit_Character)) &&
                        range_text(*range.get()) == L"é",
           "a combining sequence is one character");
    expect(context, SUCCEEDED((*range.get()).Move(TextUnit_Character, 1, &moved)) && moved == 1 &&
                        range_text(*range.get()) == L" ",
           "a character range moves past the whole combining sequence");
    require(collapsed_start(*document.get(), range), "a degenerate range at the start");
    expect(context, SUCCEEDED((*range.get()).ExpandToEnclosingUnit(TextUnit_Word)) &&
                        range_text(*range.get()) == L"Café ",
           "the first word runs to the next word start");
    expect(context, SUCCEEDED((*range.get()).Move(TextUnit_Word, 1, &moved)) && moved == 1 &&
                        range_text(*range.get()) == L"au ",
           "Move by a word reaches the second word");
    expect(context, SUCCEEDED((*range.get()).Move(TextUnit_Word, 5, &moved)) && moved == 1 &&
                        range_text(*range.get()) == L"lait",
           "Move by words stops at the last word");
    int crossed = 0;
    expect(context, SUCCEEDED((*range.get()).MoveEndpointByUnit(TextPatternRangeEndpoint_Start,
                                                                TextUnit_Character, -2, &crossed)) &&
                        crossed == -2 && range_text(*range.get()) == L"u lait",
           "MoveEndpointByUnit moves one endpoint");
    expect(context, SUCCEEDED((*range.get()).ExpandToEnclosingUnit(TextUnit_Document)) &&
                        range_text(*range.get()) == wide(single_text),
           "the Document unit is the whole text");

    ComReference<IUIAutomationElement> multi_element;
    ComReference<IUIAutomationTextPattern> multi;
    if (!text_pattern_of(context, automation, window, "uiatext.multi", multi_element, multi)) return;
    const std::wstring whole = wide(read_model(context).multi_text);
    const std::size_t split = whole.find(L'\n');
    require(split != std::wstring::npos, "the multiline box holds a line feed");
    const std::wstring first_paragraph = whole.substr(0U, split + 1U);
    const std::wstring second_paragraph = whole.substr(split + 1U);
    ComReference<IUIAutomationTextRange> multi_document;
    require(SUCCEEDED((*multi.get()).get_DocumentRange(multi_document.receive())), "a document range");
    expect(context, range_text(*multi_document.get()) == whole, "the multiline document range");
    ComReference<IUIAutomationTextRange> line;
    require(collapsed_start(*multi_document.get(), line), "a degenerate range at the start");
    expect(context, SUCCEEDED((*line.get()).ExpandToEnclosingUnit(TextUnit_Line)) &&
                        range_text(*line.get()) == first_paragraph,
           "the first line runs through its break");
    expect(context, SUCCEEDED((*line.get()).Move(TextUnit_Line, 1, &moved)) && moved == 1 &&
                        range_text(*line.get()) == second_paragraph,
           "Move by a line reaches the second line");
    ComReference<IUIAutomationTextRange> paragraph;
    require(collapsed_start(*multi_document.get(), paragraph), "a degenerate range at the start");
    expect(context, SUCCEEDED((*paragraph.get()).ExpandToEnclosingUnit(TextUnit_Paragraph)) &&
                        range_text(*paragraph.get()) == first_paragraph,
           "a paragraph holds its line feed");
    const std::vector<double> boxes = bounding_values(*multi_document.get());
    expect(context, boxes.size() == 8U && boxes[2] > 0.0 && boxes[3] > 0.0 && boxes[5] > boxes[1],
           "the multiline document range has one rectangle per line, the second below the first");
}

void check_selection(ClientContext& context, IUIAutomation& automation, IUIAutomationElement& window) {
    ComReference<IUIAutomationElement> element;
    ComReference<IUIAutomationTextPattern> text;
    if (!text_pattern_of(context, automation, window, "uiatext.single", element, text)) return;
    on_ui_thread(context.dispatcher, std::function<void()>(SelectSingle{context.fixture, 1U, 6U}));
    expect(context, selection_text(*text.get()) == L"afé",
           "GetSelection returns the model's selection");
    ComReference<IUIAutomationTextPattern2> text2;
    if (pattern(*element.get(), UIA_TextPattern2Id, text2)) {
        BOOL active = TRUE;
        ComReference<IUIAutomationTextRange> caret;
        const HRESULT read = (*text2.get()).GetCaretRange(&active, caret.receive());
        int moved = 0;
        expect(context, SUCCEEDED(read) && caret.get() != nullptr && range_text(*caret.get()).empty() &&
                            SUCCEEDED((*caret.get()).MoveEndpointByUnit(TextPatternRangeEndpoint_Start,
                                                                        TextUnit_Character, -1, &moved)) &&
                            range_text(*caret.get()) == L"é",
               "the caret range sits at the model's caret");
    }
    ComReference<IUIAutomationTextRange> document;
    require(SUCCEEDED((*text.get()).get_DocumentRange(document.receive())), "a document range");
    ComReference<IUIAutomationTextRange> word;
    require(collapsed_start(*document.get(), word), "a degenerate range at the start");
    int moved = 0;
    require(SUCCEEDED((*word.get()).Move(TextUnit_Word, 1, &moved)) &&
                SUCCEEDED((*word.get()).ExpandToEnclosingUnit(TextUnit_Word)),
            "the second word");
    expect(context, SUCCEEDED((*word.get()).Select()), "Select succeeds");
    const ModelState state = read_model(context);
    expect(context, state.single_selection.anchor.value() == 7U && state.single_selection.caret.value() == 10U,
           "Select sets the model's selection, anchored at the range's start");
    expect(context, selection_text(*text.get()) == L"au ", "GetSelection follows Select");
    expect(context, (*word.get()).AddToSelection() == static_cast<HRESULT>(UIA_E_INVALIDOPERATION),
           "a text box holds one selection");
}

void check_point(ClientContext& context, IUIAutomation& automation, IUIAutomationElement& window) {
    ComReference<IUIAutomationElement> element;
    ComReference<IUIAutomationTextPattern> text;
    if (!text_pattern_of(context, automation, window, "uiatext.single", element, text)) return;
    ComReference<IUIAutomationTextRange> document;
    require(SUCCEEDED((*text.get()).get_DocumentRange(document.receive())), "a document range");
    ComReference<IUIAutomationTextRange> letter;
    require(collapsed_start(*document.get(), letter), "a degenerate range at the start");
    int moved = 0;
    require(SUCCEEDED((*letter.get()).Move(TextUnit_Character, 1, &moved)) &&
                SUCCEEDED((*letter.get()).ExpandToEnclosingUnit(TextUnit_Character)) &&
                range_text(*letter.get()) == L"a",
            "the second character");
    const std::vector<double> box = bounding_values(*letter.get());
    expect(context, box.size() == 4U && box[2] > 0.0 && box[3] > 0.0, "a character has one rectangle");
    ComReference<IUIAutomationTextRange> degenerate;
    require(collapsed_start(*letter.get(), degenerate), "a degenerate range before the character");
    const std::vector<double> caret = bounding_values(*degenerate.get());
    expect(context, caret.size() == 4U && caret[2] > 0.0 && caret[2] <= 2.0,
           "a degenerate range's rectangle is the caret's, about one pixel wide");
    if (box.size() != 4U) return;
    // A quarter of the way in from the character's left edge: nearer its
    // start than its end.
    POINT point{static_cast<LONG>(box[0] + box[2] / 4.0), static_cast<LONG>(box[1] + box[3] / 2.0)};
    ComReference<IUIAutomationTextRange> hit;
    const HRESULT located = (*text.get()).RangeFromPoint(point, hit.receive());
    expect(context, SUCCEEDED(located) && hit.get() != nullptr && range_text(*hit.get()).empty() &&
                        SUCCEEDED((*hit.get()).ExpandToEnclosingUnit(TextUnit_Character)) &&
                        range_text(*hit.get()) == L"a",
           "RangeFromPoint finds the edge nearest the point");
}

bool wait_for_events(const EventCounter& counter) {
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        if (counter.events() > 0) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

void check_events(ClientContext& context, IUIAutomation& automation, IUIAutomationElement& window) {
    ComReference<IUIAutomationElement> element;
    if (!find_element(automation, window, "uiatext.single", element)) {
        expect(context, false, "the single-line box is found for events");
        return;
    }
    EventCounter* selection = new EventCounter();
    EventCounter* changed = new EventCounter();
    const HRESULT added_selection = automation.AddAutomationEventHandler(
        UIA_Text_TextSelectionChangedEventId, element.get(), TreeScope_Element, nullptr, selection);
    const HRESULT added_changed = automation.AddAutomationEventHandler(
        UIA_Text_TextChangedEventId, element.get(), TreeScope_Element, nullptr, changed);
    expect(context, SUCCEEDED(added_selection) && SUCCEEDED(added_changed), "the event handlers are added");
    on_ui_thread(context.dispatcher, std::function<void()>(SelectSingle{context.fixture, 0U, 2U}));
    expect(context, wait_for_events(*selection), "a selection change raises TextSelectionChanged");
    on_ui_thread(context.dispatcher, std::function<void()>(SetSingleText{context.fixture, "Changed"}));
    expect(context, wait_for_events(*changed), "a text change raises TextChanged");
    static_cast<void>(automation.RemoveAllEventHandlers());
    (*selection).Release();
    (*changed).Release();
}

void check_stale(ClientContext& context, IUIAutomation& automation, IUIAutomationElement& window) {
    ComReference<IUIAutomationElement> element;
    ComReference<IUIAutomationTextPattern> text;
    if (!text_pattern_of(context, automation, window, "uiatext.doomed", element, text)) return;
    ComReference<IUIAutomationTextRange> document;
    require(SUCCEEDED((*text.get()).get_DocumentRange(document.receive())), "a document range");
    on_ui_thread(context.dispatcher, std::function<void()>(RemoveDoomed{context.fixture}));
    BstrText read;
    expect(context, (*document.get()).GetText(-1, read.receive()) ==
                        static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE),
           "a removed text box's range answers UIA_E_ELEMENTNOTAVAILABLE");
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
    check_offered(context, *automation.get(), *window.get());
    check_document(context, *automation.get(), *window.get());
    check_units(context, *automation.get(), *window.get());
    check_selection(context, *automation.get(), *window.get());
    check_point(context, *automation.get(), *window.get());
    check_events(context, *automation.get(), *window.get());
    check_stale(context, *automation.get(), *window.get());
}

// The client thread: the multithreaded apartment, the checks, then the
// window closes through the UI thread whatever happened.
struct ClientThread final {
    ClientContext* context{};
    void operator()() const {
        const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        try {
            if (FAILED(initialized)) throw std::runtime_error("the client thread has no COM apartment");
            run_client_checks(*context);
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
        if ((*context).hwnd == nullptr) {
            (*context).failures.push_back("the fixture's native window does not exist");
            (*context).request_close();
            return;
        }
        *client = std::thread(ClientThread{context});
    }
};

} // namespace

int main() {
    try {
        Fixture fixture;
        std::unique_ptr<Window> window = make_fixture(fixture);
        ClientContext context;
        context.dispatcher = (*window).dispatcher();
        context.fixture = &fixture;
        std::thread client;
        host::WindowsHostOptions options;
        options.title = std::string(fixture_title_utf8);
        options.initial_size = Size{300.0, 220.0};
        options.print_metrics_on_close = false;
        options.host_ready = HostReady{&context, &client};
        const int exit_code = host::run_windows(std::move(window), std::move(options));
        if (client.joinable()) client.join();
        require(exit_code == 0, "the fixture window did not run");
        if (!context.failures.empty()) {
            for (const std::string& failure : context.failures) {
                std::cerr << "gui_forms_windows_accessibility_uia_text_tests: " << failure << '\n';
            }
            return EXIT_FAILURE;
        }
        std::cout << "gui_forms_windows_accessibility_uia_text_tests: " << context.checks
                  << " checks passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_windows_accessibility_uia_text_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
