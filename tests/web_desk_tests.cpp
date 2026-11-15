// The web host's portable parts: the desk (src/host/web/desk/web_desk.hpp:
// stacking, frames, hit tests, modal blocking, activation, frame drags and
// placement), the computed input (src/host/web/input/web_input_rules.hpp:
// key codes, click counts, wheel deltas), the page queue
// (src/host/web/page/web_page_queue.hpp) and the present record's device
// rectangles (src/host/web/page/web_present.hpp). No Emscripten, so tested
// on every platform.

#include "../src/host/web/desk/web_desk.hpp"
#include "../src/host/web/input/web_input_rules.hpp"
#include "../src/host/web/page/web_page_queue.hpp"
#include "../src/host/web/page/web_present.hpp"

#include <array>
#include <chrono>
#include <cstdlib>
#include <thread>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace gui_forms;
using namespace gui_forms::host::web_detail;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

HostWindowId id(std::uint64_t value) {
    const HostWindowId window{value};
    return window;
}

WebDeskLayerInit layer(std::uint64_t window, std::uint64_t owner, HostWindowKind kind, Rect content) {
    WebDeskLayerInit init{};
    init.id = id(window);
    init.owner = id(owner);
    init.kind = kind;
    init.content = content;
    init.minimum = Size{100.0, 60.0};
    return init;
}

// A desk of 1000 by 700 with the primary window and two owned windows.
WebDesk three_windows() {
    WebDesk desk;
    desk.set_extent(Size{1000.0, 700.0});
    require(desk.add(layer(1U, 0U, HostWindowKind::primary, Rect{})), "the primary window is added");
    require(desk.add(layer(2U, 1U, HostWindowKind::standard, Rect{100.0, 100.0, 300.0, 200.0})), "an owned window");
    require(desk.add(layer(3U, 1U, HostWindowKind::tool, Rect{500.0, 300.0, 200.0, 150.0})), "a tool window");
    desk.set_visible(id(1U), true);
    desk.set_visible(id(2U), true);
    desk.set_visible(id(3U), true);
    return desk;
}

void test_primary_fills_and_stays_back() {
    WebDesk desk = three_windows();
    const std::optional<Rect> primary = desk.content(id(1U));
    require(primary.has_value() && *primary == (Rect{0.0, 0.0, 1000.0, 700.0}), "the primary window fills the canvas");
    require(!desk.framed(id(1U)), "the primary window has no frame");
    desk.raise(id(1U));
    require(desk.order().front() == id(1U), "raising the primary window leaves it at the back");
    desk.set_extent(Size{800.0, 500.0});
    require(*desk.content(id(1U)) == (Rect{0.0, 0.0, 800.0, 500.0}), "and it follows the canvas");
    require(!desk.add(layer(9U, 0U, HostWindowKind::primary, Rect{})), "a second primary window is refused");
    require(!desk.add(layer(2U, 1U, HostWindowKind::standard, Rect{})), "a duplicate identity is refused");
}

void test_raise_carries_owned_windows() {
    WebDesk desk = three_windows();
    require(desk.add(layer(4U, 2U, HostWindowKind::standard, Rect{150.0, 150.0, 200.0, 100.0})), "a grandchild");
    desk.set_visible(id(4U), true);
    desk.raise(id(3U));
    require(desk.order().back() == id(3U), "the tool window is at the front");
    desk.raise(id(2U));
    const std::span<const HostWindowId> order = desk.order();
    require(order.size() == 4U, "four layers");
    require(order[2] == id(2U) && order[3] == id(4U), "the owned window comes above with its owner");
}

void test_popups_above_windows() {
    WebDesk desk = three_windows();
    WebDeskLayerInit popup = layer(5U, 2U, HostWindowKind::popup, Rect{120.0, 120.0, 80.0, 40.0});
    require(desk.add(popup), "a popup");
    WebDeskLayerInit tip = layer(6U, 2U, HostWindowKind::popup, Rect{130.0, 130.0, 80.0, 20.0});
    tip.transient = HostTransientKind::tool_tip;
    require(desk.add(tip), "a tooltip");
    desk.set_visible(id(5U), true);
    desk.set_visible(id(6U), true);
    desk.raise(id(3U));
    const std::span<const HostWindowId> order = desk.order();
    require(order[order.size() - 1U] == id(6U), "the Splash is above everything");
    require(order[order.size() - 2U] == id(5U), "the popup is above the windows");
    const WebDeskHit through = desk.hit(Point{135.0, 135.0});
    require(through.window == id(5U), "a Splash takes no input: the point reaches the popup below it");
    const Rect popup_rect = *desk.content(id(5U));
    require(popup_rect == (Rect{120.0, 120.0, 80.0, 40.0}), "a popup is placed as given");
}

void test_hit_parts() {
    WebDesk desk = three_windows();
    desk.raise(id(2U));
    // Window 2's client area is (100, 100, 300, 200); its frame starts 28
    // above and 1 to each side.
    const WebDeskHit client = desk.hit(Point{150.0, 150.0});
    require(client.window == id(2U) && client.part == WebDeskPart::client, "the client area");
    require(client.local.x == 50.0 && client.local.y == 50.0, "in client coordinates");
    const WebDeskHit title = desk.hit(Point{200.0, 85.0});
    require(title.part == WebDeskPart::title_bar, "the title bar");
    const WebDeskHit close = desk.hit(Point{390.0, 85.0});
    require(close.part == WebDeskPart::close_box, "the close box at the title bar's right end");
    const WebDeskHit right_edge = desk.hit(Point{403.0, 200.0});
    require(right_edge.window == id(2U) && right_edge.part == WebDeskPart::edge_right,
            "the grip just beyond the right border");
    const WebDeskHit corner = desk.hit(Point{402.0, 302.0});
    require(corner.part == WebDeskPart::corner_bottom_right, "the bottom right corner");
    const WebDeskHit primary = desk.hit(Point{900.0, 50.0});
    require(primary.window == id(1U) && primary.part == WebDeskPart::client, "elsewhere, the primary window");
    desk.set_visible(id(2U), false);
    require(desk.hit(Point{150.0, 150.0}).window == id(1U), "a hidden layer takes no input");
}

void test_modal_blocking() {
    WebDesk desk = three_windows();
    require(desk.add(layer(4U, 2U, HostWindowKind::standard, Rect{150.0, 150.0, 200.0, 100.0})), "the dialog");
    desk.set_visible(id(4U), true);
    require(desk.activate(id(4U)) == id(4U), "the dialog is active");
    desk.begin_modal(id(4U));
    require(desk.blocked(id(1U)) && desk.blocked(id(2U)) && desk.blocked(id(3U)), "the others are blocked");
    require(!desk.blocked(id(4U)), "the modal window is not");
    require(desk.add(layer(5U, 4U, HostWindowKind::popup, Rect{160.0, 160.0, 50.0, 50.0})), "the dialog's popup");
    require(desk.add(layer(6U, 1U, HostWindowKind::standard, Rect{600.0, 100.0, 200.0, 100.0})),
            "a window opened during the session");
    desk.set_visible(id(6U), true);
    require(!desk.blocked(id(5U)) && !desk.blocked(id(6U)), "windows opened during the session are not blocked");
    require(desk.activate(id(2U)) == id(4U), "activating a blocked window activates the dialog");
    require(!desk.begin_frame_drag(id(2U), WebDeskPart::title_bar, Point{200.0, 85.0}),
            "a blocked window is not dragged");
    desk.end_modal(id(4U));
    require(!desk.blocked(id(2U)), "the session's end unblocks");
    require(desk.modal_depth() == 0U, "no session is under way");
}

void test_activation_after_removal() {
    WebDesk desk = three_windows();
    require(desk.activate(id(3U)) == id(3U), "the tool window is active");
    desk.remove(id(3U));
    require(desk.active() == id(2U), "the topmost remaining window takes the keyboard");
    WebDeskLayerInit popup = layer(5U, 2U, HostWindowKind::popup, Rect{120.0, 120.0, 80.0, 40.0});
    require(desk.add(popup), "a passive popup");
    desk.set_visible(id(5U), true);
    require(desk.activate(id(5U), false) == id(2U), "a passive popup is never activated");
    require(desk.activate(id(5U), true) == id(5U), "a focus-taking popup is");
}

void test_frame_drags() {
    WebDesk desk = three_windows();
    require(desk.begin_frame_drag(id(2U), WebDeskPart::title_bar, Point{200.0, 85.0}), "a move begins");
    const std::optional<Rect> moved = desk.frame_drag_to(Point{250.0, 135.0});
    require(moved.has_value() && (*moved).x == 150.0 && (*moved).y == 150.0, "the window moves with the pointer");
    const std::optional<Rect> above = desk.frame_drag_to(Point{250.0, -500.0});
    require(above.has_value() && (*above).y == WebDesk::title_bar_height, "the title bar stays inside the canvas");
    desk.end_frame_drag();
    require(desk.begin_frame_drag(id(2U), WebDeskPart::edge_left, Point{99.0, 150.0}), "a resize begins");
    const Rect before = *desk.content(id(2U));
    const std::optional<Rect> narrowed = desk.frame_drag_to(Point{1000.0, 150.0});
    require(narrowed.has_value() && (*narrowed).width == 100.0, "the width stops at the minimum");
    require((*narrowed).right() == before.right(), "the right edge stays while the left edge moves");
    desk.end_frame_drag();
    require(!desk.begin_frame_drag(id(1U), WebDeskPart::title_bar, Point{10.0, 10.0}),
            "the primary window has no frame to drag");
    require(!desk.begin_frame_drag(id(2U), WebDeskPart::client, Point{150.0, 150.0}), "nor does a client press");
}

void test_placement() {
    WebDesk desk = three_windows();
    const Rect centred = desk.placement(HostWindowKind::standard, id(1U), Size{200.0, 100.0});
    require(centred.x == 400.0 && centred.y == 300.0, "centred over the owner");
    require(desk.add(layer(7U, 1U, HostWindowKind::standard, centred)), "placed there");
    const Rect next = desk.placement(HostWindowKind::standard, id(1U), Size{200.0, 100.0});
    require(next.x == 400.0 + WebDesk::cascade_step && next.y == 300.0 + WebDesk::cascade_step,
            "the next one cascades");
    const Rect large = desk.placement(HostWindowKind::standard, id(1U), Size{5000.0, 5000.0});
    require(large.width <= 1000.0 && large.height <= 700.0, "a placement fits the canvas");
}

void test_scene() {
    WebDesk desk = three_windows();
    static_cast<void>(desk.activate(id(2U)));
    std::vector<WebDeskQuad> quads;
    desk.scene(quads);
    require(quads.size() == 3U, "three visible layers");
    require(quads.front().window == id(1U) && !quads.front().framed, "the primary window first, unframed");
    require(quads.back().window == id(2U) && quads.back().active, "the active window last");
    require(quads.back().outer.y == quads.back().content.y - WebDesk::title_bar_height, "a framed layer's frame");
}

void test_key_codes_and_modifiers() {
    require(web_physical_key("KeyA") == PhysicalKey::a, "KeyA");
    require(web_physical_key("Enter") == PhysicalKey::enter, "Enter");
    require(web_physical_key("ArrowLeft") == PhysicalKey::left, "ArrowLeft");
    require(web_physical_key("ContextMenu") == PhysicalKey::context_menu, "ContextMenu");
    require(web_physical_key("MetaLeft") == PhysicalKey::left_meta && web_physical_key("OSLeft") == PhysicalKey::left_meta,
            "both names of the left meta key");
    require(web_physical_key("Unidentified") == 0U, "an unknown code maps to nothing");
    require(web_modifiers(1U | 2U) == (Modifier::shift | Modifier::control), "shift and control");
    require(web_modifiers(0xF0U) == Modifier::none, "unknown bits are dropped");
    require(web_pointer_button(2) == PointerButton::secondary && web_pointer_button(3) == PointerButton::none,
            "DOM buttons");
}

void test_click_counts() {
    WebClickHistory history{};
    const double interval = default_web_double_click_milliseconds;
    const double distance = default_web_double_click_distance;
    const PointerButton primary = PointerButton::primary;
    require(next_web_click_count(history, primary, 1000.0, {10.0, 10.0}, interval, distance) == 1U, "a first press");
    require(next_web_click_count(history, primary, 1200.0, {11.0, 9.0}, interval, distance) == 2U, "a double click");
    require(next_web_click_count(history, primary, 1300.0, {11.0, 9.0}, interval, distance) == 3U, "a triple click");
    require(next_web_click_count(history, primary, 1400.0, {11.0, 9.0}, interval, distance) == 1U, "then one again");
    require(next_web_click_count(history, primary, 2000.0, {11.0, 9.0}, interval, distance) == 1U, "too late");
    require(next_web_click_count(history, PointerButton::secondary, 2100.0, {11.0, 9.0}, interval, distance) == 1U,
            "another button");
    require(next_web_click_count(history, PointerButton::secondary, 2150.0, {20.0, 9.0}, interval, distance) == 1U,
            "too far");
}

void test_wheel_and_context_keys() {
    const WebWheel pixels = web_wheel(0.0, 120.0, WebWheelMode::pixels, 700.0);
    require(pixels.unit == WheelDeltaUnit::pixels && pixels.delta.y == -120.0, "pixels, negated");
    const WebWheel lines = web_wheel(-1.0, 3.0, WebWheelMode::lines, 700.0);
    require(lines.unit == WheelDeltaUnit::lines && lines.delta.x == 1.0 && lines.delta.y == -3.0, "lines");
    const WebWheel pages = web_wheel(0.0, 1.0, WebWheelMode::pages, 700.0);
    require(pages.unit == WheelDeltaUnit::pixels && pages.delta.y == -700.0, "a page is the page height");
    require(web_requests_keyboard_context(PhysicalKey::context_menu, Modifier::none), "the Menu key asks");
    require(web_requests_keyboard_context(PhysicalKey::f10, Modifier::shift), "Shift+F10 asks");
    require(!web_requests_keyboard_context(PhysicalKey::f10, Modifier::none), "F10 alone does not");
}

// The other thread's half of the wait test.
void post_presented(PageQueue* queue) noexcept {
    PageRecord late{};
    late.kind = PageRecordKind::presented;
    static_cast<void>((*queue).post(late));
}

void test_page_queue_order_and_waits() {
    PageQueue queue;
    std::deque<PageRecord> taken;
    const std::chrono::steady_clock::time_point past = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    require(!queue.take(taken, past), "an empty queue with a past deadline returns at once with nothing");
    PageRecord first{};
    first.kind = PageRecordKind::key;
    first.text = "KeyA";
    PageRecord second{};
    second.kind = PageRecordKind::text;
    second.text = "a";
    require(queue.post(first) && queue.post(second), "records are posted");
    require(queue.take(taken, std::nullopt), "posted records are taken without waiting");
    require(taken.size() == 2U && taken[0].kind == PageRecordKind::key && taken[1].text == "a",
            "in the order they were posted");
    taken.clear();
    queue.wake();
    require(queue.take(taken, std::nullopt) && taken.empty(), "a wake is taken without a record");
    require(!queue.take(taken, past), "and only once");
    // A post from another thread ends a wait without a deadline.
    std::thread poster(&post_presented, &queue);
    require(queue.take(taken, std::nullopt), "a post from another thread wakes the wait");
    poster.join();
    require(taken.size() == 1U && taken[0].kind == PageRecordKind::presented, "with its record");
}

void test_page_queue_bound() {
    PageQueue queue;
    for (std::size_t index = 0U; index < PageQueue::maximum_records; ++index) {
        require(queue.post(PageRecord{}), "records up to the bound are kept");
    }
    require(!queue.post(PageRecord{}), "a record beyond the bound is dropped");
    require(queue.dropped() == 1U, "and counted");
}

void test_device_rects() {
    std::array<WebDeviceRect, 4> out{};
    const std::array<Rect, 2> damage{Rect{0.25, 0.25, 10.0, 10.0}, Rect{90.0, 90.0, 50.0, 50.0}};
    const std::size_t count = web_device_rects(damage, 2.0, 200U, 200U, out);
    require(count == 2U, "two rectangles");
    require(out[0] == (WebDeviceRect{0, 0, 21, 21}), "scaled and aligned outward");
    require(out[1] == (WebDeviceRect{180, 180, 200, 200}), "clipped to the raster");
    const std::array<Rect, 3> empty{Rect{300.0, 300.0, 10.0, 10.0}, Rect{1.0, 1.0, 0.0, 5.0},
                                    Rect{0.0, 0.0, std::numeric_limits<double>::infinity(), 1.0}};
    require(web_device_rects(empty, 1.0, 200U, 200U, out) == 0U, "outside, empty and infinite damage drop out");
    std::array<WebDeviceRect, 2> small{};
    const std::array<Rect, 3> many{Rect{0.0, 0.0, 1.0, 1.0}, Rect{10.0, 10.0, 1.0, 1.0}, Rect{5.0, 20.0, 1.0, 1.0}};
    require(web_device_rects(many, 1.0, 100U, 100U, small) == 1U, "beyond the capacity, one rectangle");
    require(small[0] == (WebDeviceRect{0, 0, 11, 21}), "their bounds");
}

} // namespace

int main() {
    try {
        test_primary_fills_and_stays_back();
        test_raise_carries_owned_windows();
        test_popups_above_windows();
        test_hit_parts();
        test_modal_blocking();
        test_activation_after_removal();
        test_frame_drags();
        test_placement();
        test_scene();
        test_key_codes_and_modifiers();
        test_click_counts();
        test_wheel_and_context_keys();
        test_page_queue_order_and_waits();
        test_page_queue_bound();
        test_device_rects();
    } catch (const std::exception& error) {
        std::cerr << "web desk tests failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "web desk tests passed\n";
    return EXIT_SUCCESS;
}
