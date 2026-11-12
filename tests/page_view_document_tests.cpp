// PageView's document preset and PageStrip's document features (stage DK,
// phase P4): the preset's settings, the close slot reserved on closable tabs
// with the close button on the selected and hot tabs, the modified dot and
// close replacing it while hot, one close request per cause (the close
// button, a middle click, Ctrl+F4, the semantic action) and none for Delete
// or a middle click released elsewhere, the view's close policy (removal
// when unhandled, request only, a handler cancelling), selection by most
// recent activation after a close, frozen widths during successive closes,
// pinned tabs (icon only, expanded while hot, the pin replacing close, the
// pin asking to unpin), the pinned boundary in every move path, shrinking to
// the minimum and then the count-and-arrow list, the list's italic rows,
// and a page chosen from the list taking the last visible position while
// the pages before it keep their places.

#include "gui_forms/anatomy/types/anatomy_types.hpp"
#include "gui_forms/binding/property_type/property_type.hpp"
#include "gui_forms/components/context_menu/context_menu.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/page_strip.hpp"
#include "gui_forms/controls/page_strip_values.hpp"
#include "gui_forms/controls/page_view.hpp"
#include "gui_forms/controls/page_view_values.hpp"
#include "gui_forms/controls/text_box.hpp"
#include "gui_forms/dirty.hpp"
#include "gui_forms/event/subscription_token/subscription_token.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/image_list/image_list/image_list.hpp"
#include "gui_forms/pages/page_set/page_set.hpp"
#include "gui_forms/pages/types/page_set_types.hpp"
#include "gui_forms/rules/rule/rule.hpp"
#include "gui_forms/semantics/types/semantic_types.hpp"
#include "gui_forms/text/markdown/markdown.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/window/window.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace gui_forms;

constexpr std::array<std::uint8_t, 70> one_pixel_png{
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
    0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00,
    0x0d, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0xf8, 0xcf, 0xc0, 0xf0,
    0x1f, 0x00, 0x05, 0x00, 0x01, 0xff, 0x56, 0xc7, 0x2f, 0x0d, 0x00, 0x00,
    0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

bool near(double left, double right) {
    const bool result = std::fabs(left - right) < 0.01;
    return result;
}

Point centre(Rect bounds) {
    const Point point{bounds.x + bounds.width * 0.5, bounds.y + bounds.height * 0.5};
    return point;
}

KeyEvent key(std::uint32_t physical, Modifier modifiers = Modifier::none) {
    KeyEvent event{};
    event.action = KeyAction::down;
    event.physical_key = physical;
    event.modifiers = modifiers;
    return event;
}

void click(Window& window, Point point) {
    static_cast<void>(window.dispatch_pointer({PointerAction::down, PointerButton::primary, point}));
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, point}));
}

void middle_click(Window& window, Point down, Point up) {
    static_cast<void>(window.dispatch_pointer({PointerAction::down, PointerButton::middle, down}));
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::middle, up}));
}

void hover(Window& window, Point point) {
    static_cast<void>(window.dispatch_pointer({PointerAction::move, PointerButton::none, point}));
}

PageRecord record(std::string_view id, std::string_view title, bool closable) {
    PageRecord page{};
    page.id = PageId{std::string(id)};
    page.title = std::string(title);
    page.closable = closable;
    return page;
}

PageViewOptions document_options(bool pinning) {
    PageViewOptions options{};
    options.preset = PageViewPreset::document;
    options.pinning_enabled = pinning;
    return options;
}

std::string selected_id(const PageSet& pages) {
    if (!pages.selected_page()) return {};
    return (*pages.selected_page()).value;
}

// The set's order as "a b c".
std::string order(const PageSet& pages) {
    std::string text;
    for (std::size_t position = 0; position < pages.count(); ++position) {
        if (position > 0U) text += ' ';
        text += pages.record_at(PagePosition{position}).id.value;
    }
    return text;
}

std::size_t position_of(const PageSet& pages, std::string_view id) {
    const std::optional<PagePosition> found = pages.find(PageId{std::string(id)});
    require(found.has_value(), "the page is in the set");
    return (*found).value;
}

void select(PageSet& pages, std::string_view id) {
    const PageEditStatus status = pages.select(PageId{std::string(id)}, PageSelectionCause::programmatic);
    require(page_edit_succeeded(status), "the fixture's selection applies");
}

// The realized tab of a page, by its id.
PageStrip::Item item_of(const PageStrip& strip, std::string_view id) {
    const std::vector<PageStrip::Item> items = strip.realized_items();
    for (const PageStrip::Item& item : items) {
        if (item.page.value == id) return item;
    }
    require(false, "the page's tab is laid out");
    return {};
}

const SemanticNode* find_node(const std::vector<SemanticNode>& nodes, std::string_view id) {
    for (const SemanticNode& node : nodes) {
        if (node.stable_id == id) return &node;
        const SemanticNode* const inner = find_node(node.children, id);
        if (inner != nullptr) return inner;
    }
    return nullptr;
}
// Borrows from `nodes`: the pointer is valid while that vector lives.
// semantic_snapshot() returns a new snapshot, so a pointer into the roots of
// a temporary would dangle after the statement; the deleted overload refuses
// one.
const SemanticNode* find_node(std::vector<SemanticNode>&& nodes, std::string_view id) = delete;

bool has_part(const ItemSystemDeclaration& system, std::string_view name, PartRole role) {
    for (const PartDeclaration& part : system.parts) {
        if (part.name == name && part.role == role) return true;
    }
    return false;
}

// Records every close request and, when `take` is set, handles it without
// removing anything.
struct CloseLog final {
    std::vector<PageCloseRequest> requests{};
    bool take{true};
};

struct RecordClose final {
    CloseLog* log{};
    void operator()(PageCloseRequest& request) const {
        (*log).requests.push_back(request);
        if ((*log).take) request.handled = true;
    }
};

struct RecordPin final {
    std::vector<PagePinRequest>* requests{};
    void operator()(PagePinRequest& request) const { (*requests).push_back(request); }
};

struct CancelSelecting final {
    bool* active{};
    void operator()(PageSelectingEvent& event) const {
        if (*active) event.cancel = true;
    }
};

struct CountPages final {
    std::size_t* count{};
    void operator()(const PageSetChange&) const { ++(*count); }
};

struct CountSelection final {
    std::size_t* count{};
    void operator()(const PageSelectionChange&) const { ++(*count); }
};

// -- tests ---------------------------------------------------------------------

void preset_and_anatomy() {
    std::shared_ptr<PageView> view = make_control<PageView>(StableId("docs"), document_options(true));
    const PageStrip& strip = *(*view).strip();
    require((*view).preset() == PageViewPreset::document &&
                (*view).close_policy.read() == PageViewClosePolicy::remove_when_unhandled,
            "the document preset starts by removing unhandled closes");
    require(strip.close_buttons.read() == PageCloseButtons::selected_and_hot &&
                strip.overflow.read() == PageStripOverflow::shrink_then_list && !strip.mnemonics_enabled.read(),
            "the document preset shows close buttons, shrinks then lists, and has no mnemonics");
    require((*view).pages().options().fallback == PageSelectionFallback::most_recent &&
                (*view).pages().options().pinning_enabled,
            "the document preset selects the most recent page after a close; pinning is opted into");
    std::shared_ptr<PageView> plain = make_control<PageView>(StableId("plain"));
    require((*plain).preset() == PageViewPreset::strip &&
                (*(*plain).strip()).close_buttons.read() == PageCloseButtons::none &&
                (*plain).pages().options().fallback == PageSelectionFallback::neighbour,
            "the strip preset is unchanged");
    const ControlAnatomy& anatomy = strip.anatomy();
    require(validate_control_anatomy(anatomy).defect == AnatomyDefect::none, "the strip's anatomy is valid");
    const ItemSystemDeclaration& tabs = anatomy.item_systems.front();
    require(has_part(tabs, "tab_close", PartRole::actuator) && has_part(tabs, "tab_close_glyph", PartRole::glyph) &&
                has_part(tabs, "tab_pin", PartRole::actuator) && has_part(tabs, "tab_pin_glyph", PartRole::glyph) &&
                has_part(tabs, "tab_modified", PartRole::glyph),
            "a tab declares its close button, pin and modified dot");
    bool count_part = false;
    for (const PartDeclaration& part : anatomy.parts) count_part = count_part || part.name == "list_count";
    require(count_part, "the strip declares the list button's count");
    bool invalid = false;
    try {
        PageViewOptions options{};
        options.preset = static_cast<PageViewPreset>(7);
        std::shared_ptr<PageView> refused = make_control<PageView>(StableId("bad"), options);
    } catch (const std::invalid_argument&) {
        invalid = true;
    }
    require(invalid, "an invalid preset is refused");
}

void close_slot_and_modified_dot() {
    PageSet pages{};
    require(pages.insert(record("p0", "Same title", true), PagePlacement::end_of_group()) == PageEditStatus::applied &&
                pages.insert(record("p1", "Same title", true), PagePlacement::end_of_group()) == PageEditStatus::applied &&
                pages.insert(record("p2", "Same title", false), PagePlacement::end_of_group()) == PageEditStatus::applied,
            "the fixture's pages are inserted");
    std::shared_ptr<PageStrip> strip = make_control<PageStrip>(StableId("strip"), pages);
    (*strip).close_buttons = PageCloseButtons::selected_and_hot;
    Window window(strip, {600.0, 26.0});
    window.perform_layout();
    const PageStrip::Item first = item_of(*strip, "p0");
    const PageStrip::Item second = item_of(*strip, "p1");
    const PageStrip::Item third = item_of(*strip, "p2");
    require(first.parts.tab_close.present && first.parts.tab_close_glyph.present && !second.parts.tab_close.present,
            "the selected closable tab shows its close button; a closable tab at rest does not");
    require(near(first.parts.tab.bounds.width, second.parts.tab.bounds.width) &&
                near(first.parts.tab.bounds.width - third.parts.tab.bounds.width, 18.0),
            "space for the close button is reserved on every closable tab, and only there");
    require(near(first.parts.tab_label.bounds.width, second.parts.tab_label.bounds.width),
            "a closable tab's label keeps its width whether or not the button shows");
    hover(window, centre(second.parts.tab_label.bounds));
    const PageStrip::Item hot = item_of(*strip, "p1");
    require(hot.states.hot && hot.parts.tab_close.present && !hot.parts.tab_close.states.contains(AnatomyState::hot),
            "a hot closable tab shows its close button, not yet hot itself");
    hover(window, centre(hot.parts.tab_close.bounds));
    require(item_of(*strip, "p1").parts.tab_close.states.contains(AnatomyState::hot) &&
                item_of(*strip, "p1").states.hot,
            "the close button is hot (its plate) while the pointer is on it, and the tab stays hot");
    hover(window, centre(third.parts.tab_label.bounds));
    require(!item_of(*strip, "p2").parts.tab_close.present, "a tab that is not closable never shows the button");

    require(pages.set_modified(PageId{"p1"}, true) == PageEditStatus::applied &&
                pages.set_modified(PageId{"p0"}, true) == PageEditStatus::applied,
            "pages are marked modified");
    const PageStrip::Item modified = item_of(*strip, "p1");
    const PageStrip::Item selected_modified = item_of(*strip, "p0");
    require(modified.states.modified && modified.parts.tab_modified.present && !modified.parts.tab_close.present,
            "a modified tab at rest shows the dot in the close slot");
    require(selected_modified.parts.tab_modified.present && !selected_modified.parts.tab_close.present,
            "a selected modified tab shows the dot, not the close button, while not hot");
    hover(window, centre(modified.parts.tab_label.bounds));
    const PageStrip::Item replaced = item_of(*strip, "p1");
    require(replaced.parts.tab_close.present && !replaced.parts.tab_modified.present,
            "close replaces the dot while the tab is hot");
    require(near(replaced.parts.tab.bounds.width, second.parts.tab.bounds.width),
            "the dot and the button share the reserved slot: the width does not change");
    const SemanticSnapshot node_snapshot = window.semantic_snapshot();
    const SemanticNode* const node = find_node(node_snapshot.roots, "strip.tab.p1");
    require(node != nullptr && (*node).description == "modified", "a modified tab's description says so");
}

void one_close_event_per_cause() {
    std::shared_ptr<PageView> view = make_control<PageView>(StableId("docs"), document_options(false));
    std::shared_ptr<TextBox> editor = make_control<TextBox>(StableId("docs.editor"));
    require((*view).add_page(record("d0", "Doc 0", true), editor) == PageEditStatus::applied &&
                (*view).add_page(record("d1", "Doc 1", true), Control::Ptr{}) == PageEditStatus::applied &&
                (*view).add_page(record("d2", "Doc 2", true), Control::Ptr{}) == PageEditStatus::applied &&
                (*view).add_page(record("d3", "Doc 3", true), Control::Ptr{}) == PageEditStatus::applied,
            "the fixture's documents are added");
    Window window(view, {600.0, 200.0});
    window.perform_layout();
    PageStrip& strip = *(*view).strip();
    CloseLog log{};
    SubscriptionToken closing = strip.page_close_requested().subscribe(RecordClose{&log});

    click(window, centre(item_of(strip, "d0").parts.tab_close.bounds));
    require(log.requests.size() == 1U && log.requests[0].page.value == "d0" &&
                log.requests[0].cause == PageCloseCause::pointer && selected_id((*view).pages()) == "d0",
            "a click on the close button asks once, with the pointer as cause, and selects nothing else");
    middle_click(window, centre(item_of(strip, "d2").parts.tab.bounds), centre(item_of(strip, "d2").parts.tab.bounds));
    require(log.requests.size() == 2U && log.requests[1].page.value == "d2" &&
                log.requests[1].cause == PageCloseCause::middle_button && selected_id((*view).pages()) == "d0",
            "a middle click released over the tab it pressed asks once and does not select");
    middle_click(window, centre(item_of(strip, "d1").parts.tab.bounds), centre(item_of(strip, "d3").parts.tab.bounds));
    require(log.requests.size() == 2U, "a middle press and release on different tabs ask nothing");
    require(window.request_focus((*view).strip()), "the strip takes focus");
    require(window.dispatch_key(key(PhysicalKey::f4, Modifier::control)), "Ctrl+F4 is handled");
    require(log.requests.size() == 3U && log.requests[2].page.value == "d0" &&
                log.requests[2].cause == PageCloseCause::keyboard,
            "Ctrl+F4 asks once for the selected page");
    static_cast<void>(window.dispatch_key(key(PhysicalKey::delete_forward)));
    require(log.requests.size() == 3U, "Delete closes nothing");
    require(window.perform_semantic_action("docs.strip.tab.d3", SemanticAction::close),
            "the semantic close action is taken");
    require(log.requests.size() == 4U && log.requests[3].page.value == "d3" &&
                log.requests[3].cause == PageCloseCause::semantic,
            "the semantic close action asks once");
    require(window.request_focus(editor), "the document's editor takes focus");
    require(window.dispatch_key(key(PhysicalKey::f4, Modifier::control)), "Ctrl+F4 is handled inside the content");
    require(log.requests.size() == 5U && log.requests[4].cause == PageCloseCause::keyboard,
            "Ctrl+F4 from inside the selected page asks once, through the view");
    require((*view).pages().count() == 4U, "handled requests removed nothing");
}

void close_policy_and_most_recent() {
    std::shared_ptr<PageView> view = make_control<PageView>(StableId("docs"), document_options(false));
    for (std::size_t index = 0; index < 4U; ++index) {
        const std::string id = "d" + std::to_string(index);
        require((*view).add_page(record(id, "Doc", true), Control::Ptr{}) == PageEditStatus::applied,
                "the fixture's documents are added");
    }
    require((*view).add_page(record("fixed", "Fixed", false), Control::Ptr{}) == PageEditStatus::applied,
            "a page that cannot be closed is added");
    Window window(view, {600.0, 200.0});
    window.perform_layout();
    PageSet& pages = (*view).pages();
    select(pages, "d1");
    select(pages, "d3");
    select(pages, "d2");
    require(window.request_focus((*view).strip()), "the strip takes focus");
    require(window.dispatch_key(key(PhysicalKey::f4, Modifier::control)) && !pages.find(PageId{"d2"}) &&
                selected_id(pages) == "d3",
            "an unhandled close removes the page and selects the most recently active one");
    (*view).close_policy = PageViewClosePolicy::request_only;
    const std::uint64_t revision = pages.revision();
    CloseLog log{};
    log.take = false;
    SubscriptionToken closing = (*(*view).strip()).page_close_requested().subscribe(RecordClose{&log});
    require(window.dispatch_key(key(PhysicalKey::f4, Modifier::control)) && log.requests.size() == 1U &&
                pages.count() == 4U && selected_id(pages) == "d3" && pages.revision() == revision,
            "under request_only the view asks and removes nothing");
    (*view).close_policy = PageViewClosePolicy::remove_when_unhandled;
    log.take = true;
    require(window.dispatch_key(key(PhysicalKey::f4, Modifier::control)) && log.requests.size() == 2U &&
                pages.count() == 4U && selected_id(pages) == "d3" && pages.revision() == revision,
            "a handler that takes the request (a cancelled prompt) leaves the set unchanged");
    log.take = false;
    require(window.perform_semantic_action("docs.strip.tab.d0", SemanticAction::close) && !pages.find(PageId{"d0"}),
            "an unhandled semantic close removes the page");
    select(pages, "fixed");
    const std::size_t asked = log.requests.size();
    static_cast<void>(window.dispatch_key(key(PhysicalKey::f4, Modifier::control)));
    require(log.requests.size() == asked && pages.find(PageId{"fixed"}), "a page that is not closable is not asked");
    require(!(*(*view).strip()).request_close(PageId{"missing"}, PageCloseCause::keyboard) &&
                log.requests.size() == asked,
            "a page that is not in the set is not asked");
}

void frozen_widths_during_successive_closes() {
    std::shared_ptr<PageView> view = make_control<PageView>(StableId("docs"), document_options(false));
    for (std::size_t index = 0; index < 8U; ++index) {
        const std::string id = "d" + std::to_string(index);
        require((*view).add_page(record(id, "Document", true), Control::Ptr{}) == PageEditStatus::applied,
                "the fixture's documents are added");
    }
    Window window(view, {600.0, 200.0});
    window.perform_layout();
    PageStrip& strip = *(*view).strip();
    const double shrunk = item_of(strip, "d0").parts.tab.bounds.width;
    require(!strip.overflowing() && near(item_of(strip, "d7").parts.tab.bounds.width, shrunk),
            "eight equal documents shrink to one width and all fit");
    const Rect first_close = item_of(strip, "d0").parts.tab_close.bounds;
    const Point under = centre(first_close);
    hover(window, under);
    click(window, under);
    require(!(*view).pages().find(PageId{"d0"}) && strip.widths_frozen() && selected_id((*view).pages()) == "d1",
            "the first close removes the page and freezes the widths");
    const PageStrip::Item next = item_of(strip, "d1");
    require(near(next.parts.tab.bounds.width, shrunk) && near(item_of(strip, "d7").parts.tab.bounds.width, shrunk),
            "the remaining tabs keep their widths");
    require(next.parts.tab_close.present && next.parts.tab_close.bounds == first_close,
            "the next tab's close button arrives under the pointer");
    click(window, under);
    require(!(*view).pages().find(PageId{"d1"}) && (*view).pages().count() == 6U && strip.widths_frozen() &&
                near(item_of(strip, "d2").parts.tab.bounds.width, shrunk),
            "a second click at the same place closes the next tab, widths still frozen");
    hover(window, {300.0, 150.0});
    require(!strip.widths_frozen() && item_of(strip, "d2").parts.tab.bounds.width > shrunk + 1.0,
            "when the pointer leaves the strip the widths thaw and the tabs widen");
}

void pinned_tabs() {
    std::shared_ptr<PageView> view = make_control<PageView>(StableId("docs"), document_options(true));
    PageRecord pinned_icon = record("pin0", "Pinned with icon", true);
    pinned_icon.pinned = true;
    pinned_icon.image_key = "doc";
    PageRecord pinned_plain = record("pin1", "Pinned", true);
    pinned_plain.pinned = true;
    require((*view).add_page(pinned_icon, Control::Ptr{}) == PageEditStatus::applied &&
                (*view).add_page(pinned_plain, Control::Ptr{}) == PageEditStatus::applied,
            "pinned pages are added");
    for (std::size_t index = 2; index < 5U; ++index) {
        PageRecord ordinary = record("d" + std::to_string(index), "Doc", true);
        ordinary.image_key = "doc";
        require((*view).add_page(ordinary, Control::Ptr{}) == PageEditStatus::applied, "documents are added");
    }
    Window window(view, {800.0, 200.0});
    std::shared_ptr<ImageList> images = std::make_shared<ImageList>(window, Size{16.0, 16.0});
    const std::span<const std::byte, 70U> png = std::as_bytes(std::span{one_pixel_png});
    require((*images).add_png("doc", png, 1.0).image.value != 0U, "the fixture's image is added");
    PageStrip& strip = *(*view).strip();
    strip.image_list = images;
    PageSet& pages = (*view).pages();
    select(pages, "d2");
    window.perform_layout();
    require(order(pages) == "pin0 pin1 d2 d3 d4" && pages.pinned_count() == 2U, "pinned pages lead the order");
    const PageStrip::Item collapsed = item_of(strip, "pin0");
    require(collapsed.states.pinned && near(collapsed.parts.tab.bounds.width, 32.0) &&
                collapsed.parts.tab_icon.present && !collapsed.parts.tab_label.present &&
                !collapsed.parts.tab_pin.present && !collapsed.parts.tab_close.present,
            "a pinned tab at rest shows its icon alone");
    const PageStrip::Item plain = item_of(strip, "pin1");
    require(plain.parts.tab_label.present && plain.parts.tab_pin.present &&
                plain.parts.tab_pin.states.contains(AnatomyState::checked) && !plain.parts.tab_close.present,
            "a pinned tab without an icon keeps its label, and its pin replaces close");
    const double d2_before = item_of(strip, "d2").parts.tab.bounds.x;
    hover(window, centre(collapsed.parts.tab_icon.bounds));
    const PageStrip::Item expanded = item_of(strip, "pin0");
    require(expanded.states.hot && expanded.parts.tab.bounds.width > 32.0 && expanded.parts.tab_label.present &&
                expanded.parts.tab_pin.present && !expanded.parts.tab_close.present,
            "a hot pinned tab expands to its full width, its pin replacing close");
    require(item_of(strip, "d2").parts.tab.bounds.x > d2_before, "the tabs after an expanded pinned tab move along");
    const SemanticSnapshot node_snapshot = window.semantic_snapshot();
    const SemanticNode* const node = find_node(node_snapshot.roots, "docs.strip.tab.pin0");
    require(node != nullptr && (*node).description == "pinned", "a pinned tab's description says so");

    std::vector<PagePinRequest> requests;
    SubscriptionToken pinning = strip.page_pin_requested().subscribe(RecordPin{&requests});
    click(window, centre(expanded.parts.tab_pin.bounds));
    require(requests.size() == 1U && requests[0].page.value == "pin0" && !requests[0].pinned,
            "the pin asks to unpin its page");
    require(order(pages) == "pin1 pin0 d2 d3 d4" && pages.pinned_count() == 1U && selected_id(pages) == "d2",
            "an unhandled unpin request is applied: the page leads the ordinary pages and nothing is selected");

    // The pinned boundary holds for the keyboard moves.
    require(pages.set_pinned(PageId{"pin0"}, true) == PageEditStatus::applied &&
                order(pages) == "pin1 pin0 d2 d3 d4",
            "pinning again puts the page at the end of the pinned group");
    require(window.request_focus((*view).strip()), "the strip takes focus");
    select(pages, "pin0");
    static_cast<void>(window.dispatch_key(key(PhysicalKey::page_down, Modifier::control | Modifier::shift)));
    require(order(pages) == "pin1 pin0 d2 d3 d4", "the last pinned page does not move into the ordinary pages");
    static_cast<void>(window.dispatch_key(key(PhysicalKey::page_up, Modifier::control | Modifier::shift)));
    require(order(pages) == "pin0 pin1 d2 d3 d4", "a pinned page moves within the pinned group");
    select(pages, "d2");
    static_cast<void>(window.dispatch_key(key(PhysicalKey::page_up, Modifier::control | Modifier::shift)));
    require(order(pages) == "pin0 pin1 d2 d3 d4", "the first ordinary page does not move into the pinned group");
    static_cast<void>(window.dispatch_key(key(PhysicalKey::page_down, Modifier::control | Modifier::shift)));
    require(order(pages) == "pin0 pin1 d3 d2 d4" && selected_id(pages) == "d2",
            "Ctrl+Shift+PageDown moves the selected page one place later, keeping it selected");
    static_cast<void>(window.dispatch_key(key(PhysicalKey::page_down, Modifier::control | Modifier::shift)));
    static_cast<void>(window.dispatch_key(key(PhysicalKey::page_down, Modifier::control | Modifier::shift)));
    require(order(pages) == "pin0 pin1 d3 d4 d2", "the last page does not move past the end");
    require(pages.set_modified(PageId{"pin1"}, true) == PageEditStatus::applied, "a pinned page is modified");
    const SemanticSnapshot both_snapshot = window.semantic_snapshot();
    const SemanticNode* const both = find_node(both_snapshot.roots, "docs.strip.tab.pin1");
    require(both != nullptr && (*both).description == "pinned, modified", "the description names both");
}

// One row of the shrink table: the strip's width, the tab width expected
// when everything fits (0 when it overflows).
struct ShrinkRow final {
    double width{};
    double tab{};
};

void shrink_then_overflow() {
    PageSet pages{};
    for (std::size_t index = 0; index < 6U; ++index) {
        const std::string id = "p" + std::to_string(index);
        require(pages.insert(record(id, "Page", false), PagePlacement::end_of_group()) == PageEditStatus::applied,
                "the fixture's pages are inserted");
    }
    std::shared_ptr<PageStrip> strip = make_control<PageStrip>(StableId("strip"), pages);
    (*strip).overflow = PageStripOverflow::shrink_then_list;
    (*strip).item_sizing = PageItemSizing::fixed;
    PageItemWidths widths{};
    widths.minimum = 60.0;
    widths.fixed = 120.0;
    (*strip).item_widths = widths;
    Window window(strip, {724.0, 26.0});
    // Six tabs of 120 after a 2-unit inset at each end: they fit at 724,
    // shrink to an equal share down to 60 at 364, and overflow below it.
    const std::vector<ShrinkRow> table{{724.0, 120.0}, {604.0, 100.0}, {484.0, 80.0}, {364.0, 60.0}};
    for (const ShrinkRow& row : table) {
        window.resize({row.width, 26.0});
        window.perform_layout();
        const std::vector<PageStrip::Item> items = (*strip).realized_items();
        require(!(*strip).overflowing() && items.size() == 6U && (*strip).hidden_count() == 0U &&
                    !(*strip).parts().list_button.present,
                "every tab shows while the minimum still fits");
        for (const PageStrip::Item& item : items) {
            require(near(item.parts.tab.bounds.width, row.tab), "each tab takes its share of the row");
        }
    }
    const std::vector<double> narrow{363.0, 300.0, 200.0, 120.0};
    for (const double width : narrow) {
        window.resize({width, 26.0});
        window.perform_layout();
        const std::vector<PageStrip::Item> items = (*strip).realized_items();
        const PageStrip::Parts parts = (*strip).parts();
        require((*strip).overflowing() && parts.list_button.present && parts.list_count.present &&
                    parts.list_glyph.present && !parts.scroll_back.present && !parts.scroll_forward.present,
                "below the minimum the list button shows, with its count and arrow and no scroll buttons");
        require(near(parts.list_button.bounds.x + parts.list_button.bounds.width, width - 2.0),
                "the list button sits at the strip's end");
        require((*strip).hidden_count() == 6U - items.size(), "the hidden count is the pages without a tab");
        double end = 2.0;
        for (std::size_t index = 0; index < items.size(); ++index) {
            require(items[index].page.value == "p" + std::to_string(index), "the front of the order shows");
            require(items[index].parts.tab.bounds.width >= 60.0 - 0.01, "no tab is narrower than the minimum");
            require(near(items[index].parts.tab.bounds.x, end), "tabs are contiguous from the start");
            end += items[index].parts.tab.bounds.width;
        }
        require(near(end, parts.list_button.bounds.x), "the visible tabs fill the row up to the list button");
        select(pages, "p5");
        const std::vector<PageId> shown = (*strip).visible_pages();
        require(!shown.empty() && shown.back().value == "p5" && order(pages) == "p0 p1 p2 p3 p4 p5",
                "a hidden selected page shows in the last place without moving in the order");
        select(pages, "p0");
    }
}

void overflow_choice_keeps_the_front_order() {
    std::shared_ptr<PageView> view = make_control<PageView>(StableId("docs"), document_options(true));
    PageRecord pinned = record("pin", "Pinned", true);
    pinned.pinned = true;
    require((*view).add_page(pinned, Control::Ptr{}) == PageEditStatus::applied, "a pinned page is added");
    for (std::size_t index = 1; index < 10U; ++index) {
        const std::string id = "d" + std::to_string(index);
        require((*view).add_page(record(id, "Document " + std::to_string(index), true), Control::Ptr{}) ==
                    PageEditStatus::applied,
                "the fixture's documents are added");
    }
    Window window(view, {420.0, 200.0});
    window.perform_layout();
    PageStrip& strip = *(*view).strip();
    PageSet& pages = (*view).pages();
    require(strip.overflowing() && strip.hidden_count() > 0U, "the documents overflow into the list");
    const std::vector<PageId> shown = strip.visible_pages();
    require(shown.front().value == "pin", "the pinned page is never hidden");
    const std::string last_visible = shown.back().value;
    const std::size_t destination = position_of(pages, last_visible);
    std::string front;
    for (std::size_t position = 0; position < destination; ++position) {
        front += pages.record_at(PagePosition{position}).id.value + " ";
    }
    require(strip.show_page_list(MenuOpenMode::keyboard), "the list opens");
    const std::vector<MenuItemSpec> rows = strip.page_list_items();
    require(rows.size() == 10U, "the list has a row for every page");
    for (std::size_t index = 0; index < rows.size(); ++index) {
        const bool visible = index <= destination;
        require(rows[index].italic == !visible, "hidden pages are italic in the list, visible ones are not");
    }
    std::size_t page_changes = 0U;
    std::size_t selection_changes = 0U;
    SubscriptionToken counting_pages = pages.pages_changed().subscribe(CountPages{&page_changes});
    SubscriptionToken counting_selection = pages.selected_page_changed().subscribe(CountSelection{&selection_changes});
    // The keyboard-opened list starts on its first row; Down moves.
    for (int step = 0; step < 9; ++step) {
        require(window.dispatch_key(key(PhysicalKey::down)), "Down moves in the list");
    }
    require(window.dispatch_key(key(PhysicalKey::enter)), "Enter chooses the last row");
    require(selected_id(pages) == "d9" && position_of(pages, "d9") == destination &&
                pages.record_at(PagePosition{destination + 1U}).id.value == last_visible,
            "the chosen page takes the last visible position and the page there moves after it");
    std::string kept;
    for (std::size_t position = 0; position < destination; ++position) {
        kept += pages.record_at(PagePosition{position}).id.value + " ";
    }
    require(kept == front && pages.pinned_count() == 1U, "the pages before it keep their places, the pinned first");
    require(page_changes == 1U && selection_changes == 1U, "the choice is one batch: one change of each kind");
    bool shown_now = false;
    for (const PageId& page : strip.visible_pages()) shown_now = shown_now || page.value == "d9";
    require(shown_now, "the chosen page is visible");

    bool cancelling = true;
    SubscriptionToken cancel = pages.selecting().subscribe(CancelSelecting{&cancelling});
    const std::uint64_t revision = pages.revision();
    const std::string before = order(pages);
    require(strip.show_page_list(MenuOpenMode::keyboard), "the list opens again");
    for (int step = 0; step < 9; ++step) static_cast<void>(window.dispatch_key(key(PhysicalKey::down)));
    static_cast<void>(window.dispatch_key(key(PhysicalKey::enter)));
    require(order(pages) == before && pages.revision() == revision && selected_id(pages) == "d9",
            "a cancelled choice moves nothing and selects nothing");
}

void mnemonics_off_in_documents() {
    std::shared_ptr<PageView> view = make_control<PageView>(StableId("docs"), document_options(false));
    require((*view).add_page(record("save", "&Save notes.txt", true), Control::Ptr{}) == PageEditStatus::applied &&
                (*view).add_page(record("other", "Other", true), Control::Ptr{}) == PageEditStatus::applied,
            "documents are added");
    Window window(view, {600.0, 200.0});
    window.perform_layout();
    const SemanticSnapshot node_snapshot = window.semantic_snapshot();
    const SemanticNode* const node = find_node(node_snapshot.roots, "docs.strip.tab.save");
    require(node != nullptr && (*node).name == "&Save notes.txt", "a document's title is shown as written");
    select((*view).pages(), "other");
    static_cast<void>(window.dispatch_key(key(PhysicalKey::s, Modifier::alt)));
    require(selected_id((*view).pages()) == "other", "an ampersand in a document title is no mnemonic");
}

} // namespace

int main() {
    preset_and_anatomy();
    close_slot_and_modified_dot();
    one_close_event_per_cause();
    close_policy_and_most_recent();
    frozen_widths_during_successive_closes();
    pinned_tabs();
    shrink_then_overflow();
    overflow_choice_keeps_the_front_order();
    mnemonics_off_in_documents();
    std::cout << "PageView document preset tests passed\n";
    return 0;
}
