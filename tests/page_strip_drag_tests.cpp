// PageStrip's drag hooks (stage DK, phase P6): the drag threshold and
// page_drag_started, a fake docking session claiming drags (the capture
// handed over, no reorder ever), live sliding reorder with the dragged tab
// following the pointer and the others moving aside, one move on release,
// Escape (and turning reordering off) restoring the order, the pinned
// boundary held by a
// drag, reorder off by default and on in the document preset, the insertion
// position table and the insertion mark, and the context-menu and
// double-click events on tabs.

#include "gui_forms/gui_forms.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace gui_forms;

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

void press(Window& window, Point point) {
    static_cast<void>(window.dispatch_pointer({PointerAction::down, PointerButton::primary, point}));
}

void move_to(Window& window, Point point) {
    static_cast<void>(window.dispatch_pointer({PointerAction::move, PointerButton::none, point}));
}

void release(Window& window, Point point) {
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, point}));
}

// Adds pages "p0", "p1", ..., the first `pinned` of them pinned.
void add_pages(PageSet& pages, std::size_t count, std::size_t pinned = 0U) {
    for (std::size_t index = 0; index < count; ++index) {
        PageRecord record{};
        record.id = PageId{"p" + std::to_string(index)};
        record.title = "Page " + std::to_string(index);
        record.closable = true;
        record.pinned = index < pinned;
        const PageEditStatus status = pages.insert(record, PagePlacement::end_of_group());
        require(status == PageEditStatus::applied, "the fixture's pages are inserted");
    }
}

std::string order(const PageSet& pages) {
    std::string text;
    for (std::size_t position = 0; position < pages.count(); ++position) {
        if (position > 0U) text += ' ';
        text += pages.record_at(PagePosition{position}).id.value;
    }
    return text;
}

// Four fixed 100-unit tabs at x = 2, 102, 202, 302 in a 600 by 26 strip.
std::shared_ptr<PageStrip> fixed_strip(PageSet& pages) {
    std::shared_ptr<PageStrip> strip = make_control<PageStrip>(StableId("strip"), pages);
    (*strip).item_sizing = PageItemSizing::fixed;
    PageItemWidths widths{};
    widths.fixed = 100.0;
    (*strip).item_widths = widths;
    return strip;
}

Rect tab_bounds(const PageStrip& strip, std::string_view id) {
    const std::vector<PageStrip::Item> items = strip.realized_items();
    for (const PageStrip::Item& item : items) {
        if (item.page.value == id) return item.parts.tab.bounds;
    }
    require(false, "the page's tab is laid out");
    return {};
}

bool captured(const PageStrip& strip, std::string_view id) {
    const std::vector<PageStrip::Item> items = strip.realized_items();
    for (const PageStrip::Item& item : items) {
        if (item.page.value == id) return item.states.captured;
    }
    return false;
}

struct CountPages final {
    std::size_t* count{};
    void operator()(const PageSetChange&) const { ++(*count); }
};

// A stand-in for a docking workspace: it records drag starts and, while
// `claiming`, claims them and captures the pointer for its own control, as
// docking's drag session will.
struct FakeDockingSession final {
    Window* window{};
    Control::Ptr capture{};
    bool claiming{true};
    std::vector<PageDragStart> starts{};
};

struct ClaimDrag final {
    FakeDockingSession* session{};
    void operator()(PageDragStart& start) const {
        (*session).starts.push_back(start);
        if (!(*session).claiming) return;
        start.claimed = true;
        (*(*session).window).capture_pointer((*session).capture);
    }
};

// A claimant that claims without capturing anything itself.
struct ClaimWithoutCapture final {
    FakeDockingSession* session{};
    void operator()(PageDragStart& start) const {
        (*session).starts.push_back(start);
        start.claimed = true;
    }
};

struct RecordStart final {
    std::vector<PageDragStart>* starts{};
    void operator()(PageDragStart& start) const { (*starts).push_back(start); }
};

struct TakeClose final {
    std::vector<std::string>* closes{};
    void operator()(PageCloseRequest& request) const {
        (*closes).push_back(request.page.value);
        request.handled = true;
    }
};

struct RecordContextMenu final {
    std::vector<PageContextMenuRequest>* requests{};
    void operator()(PageContextMenuRequest& request) const { (*requests).push_back(request); }
};

struct RecordDoubleClick final {
    std::vector<std::string>* pages{};
    void operator()(const PageDoubleClick& event) const { (*pages).push_back(event.page.value); }
};

// -- tests ---------------------------------------------------------------------

void fake_docking_session_claims_drags() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("root"));
    PageSet pages{};
    add_pages(pages, 4U);
    std::shared_ptr<PageStrip> strip = fixed_strip(pages);
    (*strip).reorder_enabled = true;
    (*strip).set_requested_bounds({0.0, 0.0, 600.0, 26.0});
    (*root).add_child(strip);
    std::shared_ptr<Panel> site = make_control<Panel>(StableId("site"));
    (*site).set_requested_bounds({0.0, 30.0, 600.0, 170.0});
    (*root).add_child(site);
    Window window(root, {600.0, 200.0});
    window.perform_layout();
    FakeDockingSession session{&window, site, true, {}};
    SubscriptionToken claiming = (*strip).page_drag_started().subscribe(ClaimDrag{&session});
    std::size_t changes = 0U;
    SubscriptionToken counting = pages.pages_changed().subscribe(CountPages{&changes});

    const Point start = centre(tab_bounds(*strip, "p1"));
    press(window, start);
    require(window.captured_control() == strip, "a press on a tab captures the pointer for the strip");
    move_to(window, {start.x + 3.0, start.y + 3.0});
    require(session.starts.empty(), "a move within the threshold starts no drag");
    move_to(window, {start.x + 40.0, start.y});
    require(session.starts.size() == 1U && session.starts[0].page.value == "p1" &&
                session.starts[0].press_point == start && near(session.starts[0].pointer.x, start.x + 40.0),
            "past the threshold the strip raises one drag start with the page and both points");
    require(window.captured_control() == site, "the claimant holds the capture; the strip let it go");
    require(!(*strip).dragged_page() && !captured(*strip, "p1"), "a claimed drag is not the strip's to slide");
    move_to(window, {start.x + 250.0, start.y});
    release(window, {start.x + 250.0, start.y});
    require(order(pages) == "p0 p1 p2 p3" && changes == 0U && session.starts.size() == 1U,
            "a claimed drag never reorders and starts only once");

    // The same with the claimant capturing nothing: the strip still hands
    // the capture over and stays out of the drag.
    session.capture = strip;
    session.claiming = true;
    claiming.disconnect();
    SubscriptionToken plain = (*strip).page_drag_started().subscribe(ClaimWithoutCapture{&session});
    const Point other = centre(tab_bounds(*strip, "p2"));
    press(window, other);
    move_to(window, {other.x - 150.0, other.y});
    require(session.starts.size() == 2U && !window.captured_control(),
            "an unclaimed capture is released when the drag is claimed");
    move_to(window, {other.x - 180.0, other.y});
    release(window, {other.x - 180.0, other.y});
    require(order(pages) == "p0 p1 p2 p3" && changes == 0U, "the claimed drag moved nothing");
}

void live_slide_and_commit() {
    PageSet pages{};
    add_pages(pages, 4U);
    std::shared_ptr<PageStrip> strip = fixed_strip(pages);
    (*strip).reorder_enabled = true;
    Window window(strip, {600.0, 26.0});
    window.perform_layout();
    std::size_t changes = 0U;
    SubscriptionToken counting = pages.pages_changed().subscribe(CountPages{&changes});
    std::vector<PageDragStart> starts;
    SubscriptionToken recording = (*strip).page_drag_started().subscribe(RecordStart{&starts});
    // Grab p0 at its middle (x 52) and carry it to x 202: drawn from 152,
    // its middle (202) past p1's middle (152) and short of p2's (252).
    press(window, {52.0, 13.0});
    move_to(window, {120.0, 13.0});
    move_to(window, {202.0, 13.0});
    require(starts.size() == 1U && !starts[0].claimed, "an unclaimed drag start is raised once");
    require((*strip).dragged_page() && (*(*strip).dragged_page()).value == "p0" && captured(*strip, "p0"),
            "the strip slides the dragged tab, which is captured");
    require(near(tab_bounds(*strip, "p0").x, 152.0) && near(tab_bounds(*strip, "p1").x, 2.0) &&
                near(tab_bounds(*strip, "p2").x, 202.0),
            "the dragged tab follows the pointer and the tab it passed slides into its place");
    require(order(pages) == "p0 p1 p2 p3" && changes == 0U, "the order changes only on release");
    const std::optional<PageId> hit = (*strip).page_at({160.0, 13.0});
    require(hit && (*hit).value == "p0", "the dragged tab lies over the others for hit-testing");
    move_to(window, {900.0, 13.0});
    require(near(tab_bounds(*strip, "p0").x, 302.0), "the dragged tab stops at the end of the run");
    move_to(window, {202.0, 13.0});
    release(window, {202.0, 13.0});
    require(order(pages) == "p1 p0 p2 p3" && changes == 1U && !(*strip).dragged_page() &&
                !captured(*strip, "p0"),
            "the release moves the page once, where it was shown");
    require(near(tab_bounds(*strip, "p1").x, 2.0) && near(tab_bounds(*strip, "p0").x, 102.0),
            "after the move the tabs rest in the new order");
}

void escape_restores_the_order() {
    PageSet pages{};
    add_pages(pages, 4U);
    std::shared_ptr<PageStrip> strip = fixed_strip(pages);
    (*strip).reorder_enabled = true;
    Window window(strip, {600.0, 26.0});
    window.perform_layout();
    std::size_t changes = 0U;
    SubscriptionToken counting = pages.pages_changed().subscribe(CountPages{&changes});
    press(window, {352.0, 13.0});
    move_to(window, {40.0, 13.0});
    require((*strip).dragged_page() && near(tab_bounds(*strip, "p3").x, 2.0) &&
                near(tab_bounds(*strip, "p0").x, 102.0),
            "p3 dragged to the front pushes the others along");
    require(window.dispatch_key(key(PhysicalKey::escape)), "Escape is taken while dragging");
    require(!(*strip).dragged_page() && near(tab_bounds(*strip, "p3").x, 302.0) &&
                near(tab_bounds(*strip, "p0").x, 2.0),
            "Escape puts every tab back");
    move_to(window, {20.0, 13.0});
    release(window, {20.0, 13.0});
    require(order(pages) == "p0 p1 p2 p3" && changes == 0U, "after Escape the release moves nothing");
    require(!window.dispatch_key(key(PhysicalKey::escape)), "Escape without a drag is not the strip's");

    press(window, {52.0, 13.0});
    move_to(window, {300.0, 13.0});
    require((*strip).dragged_page().has_value(), "a second drag slides");
    (*strip).reorder_enabled = false;
    require(!(*strip).dragged_page(), "turning reordering off ends the drag");
    release(window, {300.0, 13.0});
    require(order(pages) == "p0 p1 p2 p3" && changes == 0U, "nothing moved");
}

void pinned_boundary_holds_while_dragging() {
    PageSet pages(PageSetOptions{PageSelectionFallback::neighbour, true, PageSetLimits::default_capacity});
    add_pages(pages, 4U, 2U);
    std::shared_ptr<PageStrip> strip = fixed_strip(pages);
    (*strip).reorder_enabled = true;
    Window window(strip, {600.0, 26.0});
    window.perform_layout();
    require(order(pages) == "p0 p1 p2 p3" && pages.pinned_count() == 2U, "two pages are pinned");
    // p2, the first ordinary page, dragged to the very start.
    const Rect first_ordinary = tab_bounds(*strip, "p2");
    press(window, centre(first_ordinary));
    move_to(window, {10.0, 13.0});
    require(near(tab_bounds(*strip, "p2").x, first_ordinary.x) && near(tab_bounds(*strip, "p0").x, 2.0),
            "an ordinary tab cannot be drawn into the pinned group");
    release(window, {10.0, 13.0});
    require(order(pages) == "p0 p1 p2 p3", "nor dropped there");
    // p0, a pinned page, dragged to the far end.
    press(window, centre(tab_bounds(*strip, "p0")));
    move_to(window, {590.0, 13.0});
    release(window, {590.0, 13.0});
    require(order(pages) == "p1 p0 p2 p3" && pages.pinned_count() == 2U,
            "a pinned tab moves only to the end of the pinned group");
}

void reorder_by_view_preset() {
    PageSet pages{};
    add_pages(pages, 3U);
    std::shared_ptr<PageStrip> strip = fixed_strip(pages);
    require(!(*strip).reorder_enabled.read(), "reordering is off by default");
    Window window(strip, {600.0, 26.0});
    window.perform_layout();
    std::vector<PageDragStart> starts;
    SubscriptionToken recording = (*strip).page_drag_started().subscribe(RecordStart{&starts});
    press(window, {52.0, 13.0});
    move_to(window, {260.0, 13.0});
    require(starts.size() == 1U && !(*strip).dragged_page(), "a drag still starts, but nothing slides");
    release(window, {260.0, 13.0});
    require(order(pages) == "p0 p1 p2", "without reordering the order stays");
    std::shared_ptr<PageView> plain = make_control<PageView>(StableId("plain"));
    PageViewOptions options{};
    options.preset = PageViewPreset::document;
    std::shared_ptr<PageView> documents = make_control<PageView>(StableId("documents"), options);
    require(!(*(*plain).strip()).reorder_enabled.read() && (*(*documents).strip()).reorder_enabled.read(),
            "the strip preset does not reorder; the document preset does");
}

// One row of the insertion table: a point along the strip and the gap.
struct InsertionRow final {
    double x{};
    std::size_t gap{};
};

void insertion_tables() {
    PageSet pages{};
    add_pages(pages, 4U);
    std::shared_ptr<PageStrip> strip = fixed_strip(pages);
    Window window(strip, {600.0, 26.0});
    window.perform_layout();
    // Tabs at 2, 102, 202, 302, each 100 wide; middles at 52, 152, 252, 352.
    const std::vector<InsertionRow> table{
        {0.5, 0U}, {10.0, 0U}, {51.0, 0U}, {53.0, 1U}, {150.0, 1U}, {160.0, 2U},
        {251.0, 2U}, {300.0, 3U}, {351.0, 3U}, {353.0, 4U}, {402.0, 4U}, {590.0, 4U},
    };
    for (const InsertionRow& row : table) {
        const std::optional<PagePosition> gap = (*strip).insertion_position_at({row.x, 13.0});
        require(gap && (*gap).value == row.gap, "the insertion table's gap matches");
    }
    require(!(*strip).insertion_position_at({100.0, 40.0}), "a point outside the strip has no gap");
    require((*strip).show_insertion_mark(PagePosition{2U}), "the mark shows at a gap between tabs");
    const PageStrip::Parts parts = (*strip).parts();
    require(parts.insertion_mark.present && near(parts.insertion_mark.bounds.x + 1.0, 202.0) &&
                near(parts.insertion_mark.bounds.width, 2.0) &&
                parts.insertion_mark.states.contains(AnatomyState::drop_accepted),
            "the insertion mark is a bar centred on the gap, drop-accepted");
    require((*strip).show_insertion_mark(PagePosition{4U}) &&
                near((*strip).parts().insertion_mark.bounds.x + 1.0, 402.0),
            "the gap after the last tab marks its end");
    require(!(*strip).show_insertion_mark(PagePosition{5U}) && !(*strip).insertion_mark(),
            "a gap beyond the set shows nothing");
    require((*strip).show_insertion_mark(PagePosition{1U}), "the mark shows again");
    (*strip).clear_insertion_mark();
    require(!(*strip).parts().insertion_mark.present, "clearing removes the mark");

    PageSet pinned_pages(PageSetOptions{PageSelectionFallback::neighbour, true, PageSetLimits::default_capacity});
    add_pages(pinned_pages, 4U, 1U);
    std::shared_ptr<PageStrip> pinned = fixed_strip(pinned_pages);
    Window pinned_window(pinned, {600.0, 26.0});
    pinned_window.perform_layout();
    const std::optional<PagePosition> front = (*pinned).insertion_position_at({10.0, 13.0});
    const std::optional<PagePosition> later = (*pinned).insertion_position_at({160.0, 13.0});
    require(front && (*front).value == 1U && later && (*later).value == 2U,
            "a gap never lies inside the pinned group");
}

void context_menu_and_double_click() {
    PageSet pages{};
    add_pages(pages, 4U);
    std::shared_ptr<PageStrip> strip = fixed_strip(pages);
    (*strip).close_buttons = PageCloseButtons::selected_and_hot;
    Window window(strip, {600.0, 26.0});
    window.perform_layout();
    std::vector<PageContextMenuRequest> menus;
    SubscriptionToken menu_token = (*strip).page_context_menu_requested().subscribe(RecordContextMenu{&menus});
    std::vector<std::string> doubles;
    SubscriptionToken double_token = (*strip).page_double_clicked().subscribe(RecordDoubleClick{&doubles});
    const Point second{152.0, 13.0};
    static_cast<void>(window.dispatch_pointer({PointerAction::down, PointerButton::secondary, second}));
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::secondary, second}));
    require(menus.size() == 1U && menus[0].page.value == "p1" && menus[0].cause == PageContextMenuCause::pointer &&
                menus[0].anchor == second && (*pages.selected_page()).value == "p0",
            "a secondary click asks once for its tab's menu, at the pointer, selecting nothing");
    static_cast<void>(window.dispatch_pointer({PointerAction::down, PointerButton::secondary, second}));
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::secondary, {252.0, 13.0}}));
    require(menus.size() == 1U, "a secondary press and release on different tabs ask nothing");
    require(window.request_focus(strip), "the strip takes focus");
    require(window.dispatch_key(key(PhysicalKey::f10, Modifier::shift)), "Shift+F10 is taken");
    require(menus.size() == 2U && menus[1].page.value == "p0" && menus[1].cause == PageContextMenuCause::keyboard &&
                near(menus[1].anchor.x, 2.0) && near(menus[1].anchor.y, 26.0),
            "Shift+F10 asks for the keyboard's tab's menu at its corner by the content");

    PointerEvent first_press{PointerAction::down, PointerButton::primary, {252.0, 13.0}};
    static_cast<void>(window.dispatch_pointer(first_press));
    release(window, {252.0, 13.0});
    PointerEvent second_press{PointerAction::down, PointerButton::primary, {252.0, 13.0}};
    second_press.click_count = 2U;
    static_cast<void>(window.dispatch_pointer(second_press));
    release(window, {252.0, 13.0});
    require(doubles.size() == 1U && doubles[0] == "p2" && (*pages.selected_page()).value == "p2",
            "the second press of a double click on a tab raises one double click, after selecting");
    const std::vector<PageStrip::Item> items = (*strip).realized_items();
    Rect close{};
    for (const PageStrip::Item& item : items) {
        if (item.page.value == "p2") close = item.parts.tab_close.bounds;
    }
    require(!close.empty(), "the selected tab shows its close button");
    std::vector<std::string> closes;
    SubscriptionToken close_token = (*strip).page_close_requested().subscribe(TakeClose{&closes});
    PointerEvent on_close{PointerAction::down, PointerButton::primary, centre(close)};
    on_close.click_count = 2U;
    static_cast<void>(window.dispatch_pointer(on_close));
    release(window, centre(close));
    require(doubles.size() == 1U && closes.size() == 1U,
            "a double click on the close button is a close, not a double click on the tab");
}

// Shift+F10 on a tab the scroll buttons moved out of view scrolls it back
// and anchors its menu at its corner by the content.
void keyboard_menu_on_a_scrolled_out_tab() {
    PageSet pages{};
    add_pages(pages, 8U);
    std::shared_ptr<PageStrip> strip = fixed_strip(pages);
    Window window(strip, {400.0, 26.0});
    window.perform_layout();
    std::vector<PageContextMenuRequest> menus;
    SubscriptionToken menu_token = (*strip).page_context_menu_requested().subscribe(RecordContextMenu{&menus});
    require(window.request_focus(strip), "the strip takes focus");
    require((*strip).scroll_forward() && (*strip).scroll_forward(), "the strip scrolls forward twice");
    window.perform_layout();
    const std::vector<PageId> before = (*strip).visible_pages();
    require(!before.empty() && before.front().value != "p0", "the keyboard's tab p0 is scrolled out of view");
    require(window.dispatch_key(key(PhysicalKey::f10, Modifier::shift)), "Shift+F10 is taken");
    window.perform_layout();
    const std::vector<PageId> after = (*strip).visible_pages();
    require(!after.empty() && after.front().value == "p0", "the tab is scrolled back into view");
    const Rect tab = tab_bounds(*strip, "p0");
    require(menus.size() == 1U && menus[0].page.value == "p0" && near(menus[0].anchor.x, tab.x) &&
                near(menus[0].anchor.y, tab.y + tab.height),
            "the menu is anchored at the tab's corner by the content");
}

} // namespace

int main() {
    fake_docking_session_claims_drags();
    live_slide_and_commit();
    escape_restores_the_order();
    pinned_boundary_holds_while_dragging();
    reorder_by_view_preset();
    insertion_tables();
    context_menu_and_double_click();
    keyboard_menu_on_a_scrolled_out_tab();
    std::cout << "PageStrip drag hook tests passed\n";
    return 0;
}
