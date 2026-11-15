// The single-surface window manager's plain parts: the desk
// (src/host/surface/desk/surface_desk.hpp: stacking, frames and their theme,
// hit tests, window states and tabs, modal blocking, activation, frame drags
// and placement), the input queue and rules
// (src/host/surface/input/surface_input.hpp: order, bound, waits, click
// counts, wheel deltas, context keys) and the compositing
// (src/host/surface/compose/surface_compose.hpp: device rectangles, the
// compose kernel and its tint, cursor sprites). The desk's capsule parts,
// half-surface tiling and capsule moves are here too; the capsule's
// gestures are tests/window_capsule_tests.cpp. Tested on every platform.

#include "../src/host/surface/compose/surface_compose.hpp"
#include "../src/host/surface/desk/surface_desk.hpp"
#include "../src/host/surface/input/surface_input.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <thread>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace gui_forms;
using namespace gui_forms::host::surface_detail;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

HostWindowId id(std::uint64_t value) {
    const HostWindowId window{value};
    return window;
}

SurfaceDeskLayerInit layer(std::uint64_t window, std::uint64_t owner, HostWindowKind kind, Rect content) {
    SurfaceDeskLayerInit init{};
    init.id = id(window);
    init.owner = id(owner);
    init.kind = kind;
    init.content = content;
    init.minimum = Size{100.0, 60.0};
    return init;
}

// A desk of 1000 by 700 with the primary window and two owned windows.
SurfaceDesk three_windows() {
    SurfaceDesk desk;
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
    SurfaceDesk desk = three_windows();
    const std::optional<Rect> primary = desk.content(id(1U));
    require(primary.has_value() && *primary == (Rect{0.0, 0.0, 1000.0, 700.0}), "the primary window fills the surface");
    require(!desk.framed(id(1U)), "the primary window has no frame");
    desk.raise(id(1U));
    require(desk.order().front() == id(1U), "raising the primary window leaves it at the back");
    desk.set_extent(Size{800.0, 500.0});
    require(*desk.content(id(1U)) == (Rect{0.0, 0.0, 800.0, 500.0}), "and it follows the surface");
    require(!desk.add(layer(9U, 0U, HostWindowKind::primary, Rect{})), "a second primary window is refused");
    require(!desk.add(layer(2U, 1U, HostWindowKind::standard, Rect{})), "a duplicate identity is refused");
}

void test_raise_carries_owned_windows() {
    SurfaceDesk desk = three_windows();
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
    SurfaceDesk desk = three_windows();
    SurfaceDeskLayerInit popup = layer(5U, 2U, HostWindowKind::popup, Rect{120.0, 120.0, 80.0, 40.0});
    require(desk.add(popup), "a popup");
    SurfaceDeskLayerInit tip = layer(6U, 2U, HostWindowKind::popup, Rect{130.0, 130.0, 80.0, 20.0});
    tip.transient = HostTransientKind::tool_tip;
    require(desk.add(tip), "a tooltip");
    desk.set_visible(id(5U), true);
    desk.set_visible(id(6U), true);
    desk.raise(id(3U));
    const std::span<const HostWindowId> order = desk.order();
    require(order[order.size() - 1U] == id(6U), "the Splash is above everything");
    require(order[order.size() - 2U] == id(5U), "the popup is above the windows");
    const SurfaceDeskHit through = desk.hit(Point{135.0, 135.0});
    require(through.window == id(5U), "a Splash takes no input: the point reaches the popup below it");
    const Rect popup_rect = *desk.content(id(5U));
    require(popup_rect == (Rect{120.0, 120.0, 80.0, 40.0}), "a popup is placed as given");
}

// The classic title bar boxes instead of the default capsule.
void use_boxes(SurfaceDesk& desk) {
    SurfaceFrameTheme theme = desk.theme();
    theme.window_controls = SurfaceWindowControls::boxes;
    desk.set_theme(theme);
}

void test_hit_parts() {
    SurfaceDesk desk = three_windows();
    use_boxes(desk);
    desk.raise(id(2U));
    // Window 2's client area is (100, 100, 300, 200); its frame starts 28
    // above and 1 to each side.
    const SurfaceDeskHit client = desk.hit(Point{150.0, 150.0});
    require(client.window == id(2U) && client.part == SurfaceDeskPart::client, "the client area");
    require(client.local.x == 50.0 && client.local.y == 50.0, "in client coordinates");
    const SurfaceDeskHit title = desk.hit(Point{200.0, 85.0});
    require(title.part == SurfaceDeskPart::title_bar, "the title bar");
    const SurfaceDeskHit close = desk.hit(Point{390.0, 85.0});
    require(close.part == SurfaceDeskPart::close_box, "the close box at the title bar's right end");
    const SurfaceDeskHit maximize = desk.hit(Point{350.0, 85.0});
    require(maximize.part == SurfaceDeskPart::maximize_box, "the maximise box left of it");
    const SurfaceDeskHit minimize = desk.hit(Point{310.0, 85.0});
    require(minimize.part == SurfaceDeskPart::minimize_box, "the minimise box left of that");
    require(desk.box(id(2U), SurfaceDeskPart::close_box) == (Rect{360.0, 73.0, 40.0, 27.0}), "the close box's rectangle");
    const SurfaceDeskHit right_edge = desk.hit(Point{403.0, 200.0});
    require(right_edge.window == id(2U) && right_edge.part == SurfaceDeskPart::edge_right,
            "the grip just beyond the right border");
    const SurfaceDeskHit corner = desk.hit(Point{402.0, 302.0});
    require(corner.part == SurfaceDeskPart::corner_bottom_right, "the bottom right corner");
    const SurfaceDeskHit primary = desk.hit(Point{900.0, 50.0});
    require(primary.window == id(1U) && primary.part == SurfaceDeskPart::client, "elsewhere, the primary window");
    desk.set_visible(id(2U), false);
    require(desk.hit(Point{150.0, 150.0}).window == id(1U), "a hidden layer takes no input");
}

void test_modal_blocking() {
    SurfaceDesk desk = three_windows();
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
    require(!desk.begin_frame_drag(id(2U), SurfaceDeskPart::title_bar, Point{200.0, 85.0}),
            "a blocked window is not dragged");
    desk.end_modal(id(4U));
    require(!desk.blocked(id(2U)), "the session's end unblocks");
    require(desk.modal_depth() == 0U, "no session is under way");
}

void test_activation_after_removal() {
    SurfaceDesk desk = three_windows();
    require(desk.activate(id(3U)) == id(3U), "the tool window is active");
    desk.remove(id(3U));
    require(desk.active() == id(2U), "the topmost remaining window takes the keyboard");
    SurfaceDeskLayerInit popup = layer(5U, 2U, HostWindowKind::popup, Rect{120.0, 120.0, 80.0, 40.0});
    require(desk.add(popup), "a passive popup");
    desk.set_visible(id(5U), true);
    require(desk.activate(id(5U), false) == id(2U), "a passive popup is never activated");
    require(desk.activate(id(5U), true) == id(5U), "a focus-taking popup is");
}

void test_frame_drags() {
    SurfaceDesk desk = three_windows();
    require(desk.begin_frame_drag(id(2U), SurfaceDeskPart::title_bar, Point{200.0, 85.0}), "a move begins");
    const std::optional<Rect> moved = desk.frame_drag_to(Point{250.0, 135.0});
    require(moved.has_value() && (*moved).x == 150.0 && (*moved).y == 150.0, "the window moves with the pointer");
    const std::optional<Rect> above = desk.frame_drag_to(Point{250.0, -500.0});
    require(above.has_value() && (*above).y == desk.theme().title_bar_height, "the title bar stays inside the surface");
    desk.end_frame_drag();
    require(desk.begin_frame_drag(id(2U), SurfaceDeskPart::edge_left, Point{99.0, 150.0}), "a resize begins");
    const Rect before = *desk.content(id(2U));
    const std::optional<Rect> narrowed = desk.frame_drag_to(Point{1000.0, 150.0});
    require(narrowed.has_value() && (*narrowed).width == 100.0, "the width stops at the minimum");
    require((*narrowed).right() == before.right(), "the right edge stays while the left edge moves");
    desk.end_frame_drag();
    require(!desk.begin_frame_drag(id(1U), SurfaceDeskPart::title_bar, Point{10.0, 10.0}),
            "the primary window has no frame to drag");
    require(!desk.begin_frame_drag(id(2U), SurfaceDeskPart::client, Point{150.0, 150.0}), "nor does a client press");
}

void test_placement() {
    SurfaceDesk desk = three_windows();
    const Rect centred = desk.placement(HostWindowKind::standard, id(1U), Size{200.0, 100.0});
    require(centred.x == 400.0 && centred.y == 300.0, "centred over the owner");
    require(desk.add(layer(7U, 1U, HostWindowKind::standard, centred)), "placed there");
    const Rect next = desk.placement(HostWindowKind::standard, id(1U), Size{200.0, 100.0});
    require(next.x == 400.0 + desk.theme().cascade_step && next.y == 300.0 + desk.theme().cascade_step,
            "the next one cascades");
    const Rect large = desk.placement(HostWindowKind::standard, id(1U), Size{5000.0, 5000.0});
    require(large.width <= 1000.0 && large.height <= 700.0, "a placement fits the surface");
}

void test_scene() {
    SurfaceDesk desk = three_windows();
    static_cast<void>(desk.activate(id(2U)));
    std::vector<SurfaceDeskQuad> quads;
    desk.scene(quads);
    require(quads.size() == 3U, "three visible layers");
    require(quads.front().window == id(1U) && !quads.front().framed, "the primary window first, unframed");
    require(quads.back().window == id(2U) && quads.back().active, "the active window last");
    require(quads.back().outer.y == quads.back().content.y - desk.theme().title_bar_height, "a framed layer's frame");
}

void test_click_counts() {
    SurfaceClickHistory history{};
    // The surface manager's own default (gui_forms has one of the same name).
    const double interval = host::surface_detail::default_double_click_milliseconds;
    const double distance = default_double_click_distance;
    const PointerButton primary = PointerButton::primary;
    require(next_surface_click_count(history, primary, 1000.0, {10.0, 10.0}, interval, distance) == 1U, "a first press");
    require(next_surface_click_count(history, primary, 1200.0, {11.0, 9.0}, interval, distance) == 2U, "a double click");
    require(next_surface_click_count(history, primary, 1300.0, {11.0, 9.0}, interval, distance) == 3U, "a triple click");
    require(next_surface_click_count(history, primary, 1400.0, {11.0, 9.0}, interval, distance) == 1U, "then one again");
    require(next_surface_click_count(history, primary, 2000.0, {11.0, 9.0}, interval, distance) == 1U, "too late");
    require(next_surface_click_count(history, PointerButton::secondary, 2100.0, {11.0, 9.0}, interval, distance) == 1U,
            "another button");
    require(next_surface_click_count(history, PointerButton::secondary, 2150.0, {20.0, 9.0}, interval, distance) == 1U,
            "too far");
}

void test_wheel_and_context_keys() {
    const SurfaceWheel pixels = surface_wheel(Point{0.0, -120.0}, SurfaceWheelUnit::pixels, 700.0);
    require(pixels.unit == WheelDeltaUnit::pixels && pixels.delta.y == -120.0, "pixels");
    const SurfaceWheel lines = surface_wheel(Point{1.0, -3.0}, SurfaceWheelUnit::lines, 700.0);
    require(lines.unit == WheelDeltaUnit::lines && lines.delta.x == 1.0 && lines.delta.y == -3.0, "lines");
    const SurfaceWheel pages = surface_wheel(Point{0.0, -1.0}, SurfaceWheelUnit::pages, 700.0);
    require(pages.unit == WheelDeltaUnit::pixels && pages.delta.y == -700.0, "a page is the page height");
    const SurfaceWheel broken =
        surface_wheel(Point{std::numeric_limits<double>::quiet_NaN(), 2.0}, SurfaceWheelUnit::pixels, 700.0);
    require(broken.delta.x == 0.0 && broken.delta.y == 2.0, "a non-finite delta is 0");
    require(surface_requests_keyboard_context(PhysicalKey::context_menu, Modifier::none), "the Menu key asks");
    require(surface_requests_keyboard_context(PhysicalKey::f10, Modifier::shift), "Shift+F10 asks");
    require(!surface_requests_keyboard_context(PhysicalKey::f10, Modifier::none), "F10 alone does not");
}

// The other thread's half of the wait test.
void post_presented(SurfaceInputQueue* queue) noexcept {
    SurfaceInputRecord late{};
    late.kind = SurfaceInputKind::presented;
    static_cast<void>((*queue).post(late));
}

void test_input_queue_order_and_waits() {
    SurfaceInputQueue queue;
    std::deque<SurfaceInputRecord> taken;
    const std::chrono::steady_clock::time_point past = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    require(!queue.take(taken, past), "an empty queue with a past deadline returns at once with nothing");
    SurfaceInputRecord first{};
    first.kind = SurfaceInputKind::key;
    first.physical_key = PhysicalKey::a;
    SurfaceInputRecord second{};
    second.kind = SurfaceInputKind::text;
    second.text = "a";
    require(queue.post(first) && queue.post(second), "records are posted");
    require(queue.take(taken, std::nullopt), "posted records are taken without waiting");
    require(taken.size() == 2U && taken[0].kind == SurfaceInputKind::key && taken[1].text == "a",
            "in the order they were posted");
    taken.clear();
    queue.wake();
    require(queue.take(taken, std::nullopt) && taken.empty(), "a wake is taken without a record");
    require(!queue.take(taken, past), "and only once");
    // A post from another thread ends a wait without a deadline.
    std::thread poster(&post_presented, &queue);
    require(queue.take(taken, std::nullopt), "a post from another thread wakes the wait");
    poster.join();
    require(taken.size() == 1U && taken[0].kind == SurfaceInputKind::presented, "with its record");
}

void test_input_queue_bound() {
    SurfaceInputQueue queue;
    for (std::size_t index = 0U; index < SurfaceInputQueue::maximum_records; ++index) {
        require(queue.post(SurfaceInputRecord{}), "records up to the bound are kept");
    }
    require(!queue.post(SurfaceInputRecord{}), "a record beyond the bound is dropped");
    require(queue.dropped() == 1U, "and counted");
}

void test_device_rects() {
    std::array<SurfaceDeviceRect, 4> out{};
    const std::array<Rect, 2> damage{Rect{0.25, 0.25, 10.0, 10.0}, Rect{90.0, 90.0, 50.0, 50.0}};
    const std::size_t count = surface_device_rects(damage, 2.0, 200U, 200U, out);
    require(count == 2U, "two rectangles");
    require(out[0] == (SurfaceDeviceRect{0, 0, 21, 21}), "scaled and aligned outward");
    require(out[1] == (SurfaceDeviceRect{180, 180, 200, 200}), "clipped to the surface");
    const std::array<Rect, 3> empty{Rect{300.0, 300.0, 10.0, 10.0}, Rect{1.0, 1.0, 0.0, 5.0},
                                    Rect{0.0, 0.0, std::numeric_limits<double>::infinity(), 1.0}};
    require(surface_device_rects(empty, 1.0, 200U, 200U, out) == 0U, "outside, empty and infinite damage drop out");
    std::array<SurfaceDeviceRect, 2> small{};
    const std::array<Rect, 3> many{Rect{0.0, 0.0, 1.0, 1.0}, Rect{10.0, 10.0, 1.0, 1.0}, Rect{5.0, 20.0, 1.0, 1.0}};
    require(surface_device_rects(many, 1.0, 100U, 100U, small) == 1U, "beyond the capacity, one rectangle");
    require(small[0] == (SurfaceDeviceRect{0, 0, 11, 21}), "their bounds");
}

void test_window_states() {
    SurfaceDesk desk = three_windows();
    static_cast<void>(desk.activate(id(2U)));
    const Rect normal = *desk.content(id(2U));
    require(desk.maximize(id(2U)), "a resizable window maximises");
    require(desk.state(id(2U)) == SurfaceWindowState::maximized, "and is maximised");
    require(*desk.content(id(2U)) == (Rect{1.0, 28.0, 998.0, 671.0}), "filling the surface inside its frame");
    require(!desk.maximize(id(2U)), "maximising twice changes nothing");
    require(!desk.begin_frame_drag(id(2U), SurfaceDeskPart::title_bar, Point{200.0, 10.0}),
            "a maximised window is not dragged");
    require(desk.hit(Point{0.5, 300.0}).part != SurfaceDeskPart::edge_left, "nor has it grips");
    desk.set_extent(Size{800.0, 600.0});
    require(*desk.content(id(2U)) == (Rect{1.0, 28.0, 798.0, 571.0}), "it follows the surface");
    desk.set_extent(Size{1000.0, 700.0});
    require(desk.minimize(id(2U)), "a maximised window minimises");
    require(!desk.shown(id(2U)) && desk.visible(id(2U)), "it leaves the drawing but stays visible");
    require(desk.active() == id(3U), "the keyboard moves to the topmost window left");
    require(desk.activate(id(2U)) == id(3U), "a minimised window is never activated");
    require(desk.restore(id(2U)), "restoring a minimised window");
    require(desk.state(id(2U)) == SurfaceWindowState::maximized, "returns it to maximised");
    require(desk.restore(id(2U)), "restoring a maximised window");
    require(desk.state(id(2U)) == SurfaceWindowState::normal && *desk.content(id(2U)) == normal,
            "puts back its normal rectangle");
    require(!desk.restore(id(2U)), "a normal window has nothing to restore");
    require(desk.maximize(id(2U)), "maximised again");
    require(desk.set_content(id(2U), Rect{50.0, 60.0, 200.0, 100.0}).has_value(), "a requested frame");
    require(desk.state(id(2U)) == SurfaceWindowState::normal, "makes a maximised window normal");
    require(!desk.minimize(id(1U)) && !desk.maximize(id(1U)), "the primary window has no states");
}

void test_minimized_owner_hides_owned_windows_and_tabs() {
    SurfaceDesk desk = three_windows();
    require(desk.add(layer(4U, 2U, HostWindowKind::standard, Rect{150.0, 150.0, 200.0, 100.0})), "an owned window");
    desk.set_visible(id(4U), true);
    static_cast<void>(desk.activate(id(4U)));
    require(desk.minimize(id(2U)), "its owner minimises");
    require(!desk.shown(id(4U)), "the owned window leaves with it");
    require(desk.active() == id(3U), "and the keyboard leaves it");
    std::vector<SurfaceDeskQuad> quads;
    desk.scene(quads);
    require(quads.size() == 2U, "the scene holds the primary and the tool window");
    std::vector<SurfaceDeskTab> tabs;
    desk.tabs(tabs);
    require(tabs.size() == 1U && tabs[0].window == id(2U), "one tab, the minimised window's");
    require(tabs[0].bounds == (Rect{4.0, 672.0, 160.0, 24.0}), "at the bottom left");
    const SurfaceDeskHit tab = desk.hit(Point{50.0, 680.0});
    require(tab.window == id(2U) && tab.part == SurfaceDeskPart::minimized_tab, "the tab takes the pointer");
    require(desk.hit(Point{150.0, 150.0}).window == id(1U), "a minimised window takes none");
    require(desk.restore(id(2U)) && desk.shown(id(4U)), "restoring brings the owned window back");
    desk.tabs(tabs);
    require(tabs.empty(), "and the tab goes");
}

void test_frame_theme() {
    SurfaceDesk desk = three_windows();
    SurfaceFrameTheme theme{};
    theme.title_bar_height = 32.0;
    theme.border_width = 0.0;
    theme.grip_outside = 6.0;
    theme.grip_inside = 3.0;
    theme.controls = SurfaceControlsSide::left;
    theme.window_controls = SurfaceWindowControls::boxes;
    desk.set_theme(theme);
    require(desk.theme().title_bar_height == 32.0, "the theme is kept");
    const Rect content = *desk.content(id(2U));
    require(*desk.outer(id(2U)) == (Rect{content.x, content.y - 32.0, content.width, content.height + 32.0}),
            "a frame without an edge is the title bar alone");
    require(desk.box(id(2U), SurfaceDeskPart::close_box).x == content.x, "the controls at the left end");
    require(desk.hit(Point{content.x + 10.0, content.y - 10.0}).part == SurfaceDeskPart::close_box,
            "and hit there");
    require(desk.hit(Point{content.right() + 5.0, content.y + 50.0}).part == SurfaceDeskPart::edge_right,
            "the invisible grip outside the frame");
    require(desk.hit(Point{content.right() - 2.0, content.y + 50.0}).part == SurfaceDeskPart::edge_right,
            "and inside it");
    SurfaceDeskLayerInit bare = layer(8U, 1U, HostWindowKind::standard, Rect{600.0, 100.0, 200.0, 100.0});
    bare.title_bar = false;
    require(desk.add(bare), "a window without a title bar");
    desk.set_visible(id(8U), true);
    require((*desk.outer(id(8U))).y == (*desk.content(id(8U))).y, "its frame has no top band without an edge");
    require(desk.box(id(8U), SurfaceDeskPart::close_box).empty(), "nor boxes");
    require(desk.maximize(id(8U)), "it still maximises (its program's own controls ask)");
    require((*desk.content(id(8U))).y == 0.0, "to the whole surface");
}

bool near(double a, double b) {
    const bool close = std::abs(a - b) < 1e-9;
    return close;
}

void test_capsule_parts() {
    SurfaceDesk desk = three_windows();
    require(desk.theme().window_controls == SurfaceWindowControls::capsule,
            "the capsule is the default window controls (owner, 2026-11-15)");
    SurfaceFrameTheme theme{};
    theme.window_controls = SurfaceWindowControls::capsule;
    desk.set_theme(theme);
    // Window 2: client (100, 100, 300, 200), frame (99, 72, 302, 229); the
    // capsule 6 in from the frame's right edge, centred in the 27 unit bar.
    const Rect capsule = desk.capsule(id(2U));
    require(capsule == (Rect{318.0, 76.5, 76.0, 20.0}), "the capsule at the controls' end of the title bar");
    require(desk.box(id(2U), SurfaceDeskPart::capsule_left) == (Rect{318.0, 76.5, 28.0, 20.0}), "its left half");
    require(desk.box(id(2U), SurfaceDeskPart::capsule_help) == (Rect{346.0, 76.5, 20.0, 20.0}),
            "the question mark in the middle");
    require(desk.box(id(2U), SurfaceDeskPart::capsule_right) == (Rect{366.0, 76.5, 28.0, 20.0}), "its right half");
    require(desk.box(id(2U), SurfaceDeskPart::close_box).empty() &&
                desk.box(id(2U), SurfaceDeskPart::maximize_box).empty(),
            "no boxes with the capsule");
    require(desk.hit(Point{330.0, 86.0}).part == SurfaceDeskPart::capsule_left, "the left half is hit");
    require(desk.hit(Point{356.0, 86.0}).part == SurfaceDeskPart::capsule_help, "the question mark is hit");
    require(desk.hit(Point{380.0, 86.0}).part == SurfaceDeskPart::capsule_right, "the right half is hit");
    require(desk.hit(Point{200.0, 86.0}).part == SurfaceDeskPart::title_bar, "the rest is the title bar");
    std::vector<SurfaceDeskQuad> quads;
    desk.scene(quads);
    require(quads.size() == 3U && quads[1].capsule && !quads[1].maximize_box && !quads[1].minimize_box,
            "the scene reports the capsule instead of the boxes");
    require(!quads[0].capsule, "the primary window has none");
    theme.controls = SurfaceControlsSide::left;
    desk.set_theme(theme);
    require(desk.capsule(id(2U)).x == 106.0, "at the left end when the theme says so");
    SurfaceDeskLayerInit bare = layer(8U, 1U, HostWindowKind::standard, Rect{600.0, 100.0, 200.0, 100.0});
    bare.title_bar = false;
    require(desk.add(bare), "a window without a title bar");
    desk.set_visible(id(8U), true);
    require(desk.capsule(id(8U)).empty(), "has no capsule (its program draws its own controls)");
}

void test_tiling_and_capsule_moves() {
    SurfaceDesk desk = three_windows();
    static_cast<void>(desk.activate(id(2U)));
    const Rect normal = *desk.content(id(2U));
    require(desk.tile(id(2U), SurfaceTileSide::left), "a resizable window tiles");
    require(desk.state(id(2U)) == SurfaceWindowState::tiled_left, "to the left half");
    require(*desk.content(id(2U)) == (Rect{1.0, 28.0, 498.0, 671.0}), "the left half inside its frame");
    require(desk.tiled_outer(SurfaceTileSide::left) == (Rect{0.0, 0.0, 500.0, 700.0}), "the left snap preview");
    require(desk.tiled_outer(SurfaceTileSide::right) == (Rect{500.0, 0.0, 500.0, 700.0}), "the right snap preview");
    require(!desk.tile(id(2U), SurfaceTileSide::left), "tiling twice changes nothing");
    require(desk.hit(Point{0.5, 300.0}).part != SurfaceDeskPart::edge_left, "a tiled window has no grips");
    require(desk.tile(id(2U), SurfaceTileSide::right), "it tiles to the other half");
    require(*desk.content(id(2U)) == (Rect{501.0, 28.0, 498.0, 671.0}), "the right half inside its frame");
    desk.set_extent(Size{800.0, 600.0});
    require(*desk.content(id(2U)) == (Rect{401.0, 28.0, 398.0, 571.0}), "it follows the surface");
    desk.set_extent(Size{1000.0, 700.0});
    require(desk.maximize(id(2U)), "a tiled window maximises");
    require(desk.restore(id(2U)) && *desk.content(id(2U)) == normal, "and restores to its normal rectangle");
    require(desk.tile(id(2U), SurfaceTileSide::left) && desk.minimize(id(2U)), "a tiled window minimises");
    require(desk.restore(id(2U)) && desk.state(id(2U)) == SurfaceWindowState::tiled_left,
            "and returns tiled");
    // A capsule move from the tiled state: the window becomes normal at its
    // remembered size, the point at the same fraction of its frame's width.
    require(desk.begin_capsule_move(id(2U), Point{300.0, 10.0}), "a capsule move starts from the tiled state");
    require(desk.state(id(2U)) == SurfaceWindowState::normal && desk.frame_dragging(), "as a normal window's drag");
    const Rect floated = *desk.content(id(2U));
    require(near(floated.x, 300.0 - 0.6 * 302.0 + 1.0) && floated.y == 28.0 && floated.width == normal.width &&
                floated.height == normal.height,
            "at its normal size under the point");
    const std::optional<Rect> moved = desk.frame_drag_to(Point{350.0, 60.0});
    require(moved.has_value() && near((*moved).x, floated.x + 50.0) && (*moved).y == 78.0, "and it follows");
    desk.end_frame_drag();
    require(!desk.tile(id(1U), SurfaceTileSide::left), "the primary window does not tile");
    require(desk.minimize(id(2U)), "minimised");
    require(!desk.begin_capsule_move(id(2U), Point{300.0, 10.0}), "a minimised window is not moved");
}

void test_tint_layer() {
    const SurfacePixel tint = surface_tint_pixel(0x3F7FBFU, 0x50U);
    require(tint.red == 20U && tint.green == 40U && tint.blue == 60U && tint.alpha == 80U,
            "the tint is premultiplied, rounded to nearest");
    std::vector<std::uint8_t> pixels(4U * 4U, 0U);
    const SurfaceTarget target{pixels.data(), 4U * 4U, 4U, 1U};
    std::array<SurfaceComposeLayer, 1> layers{};
    layers[0].kind = SurfaceLayerKind::tint;
    layers[0].bounds = SurfaceDeviceRect{1, 0, 4, 1};
    layers[0].color = tint;
    surface_compose(target, SurfaceDeviceRect{0, 0, 4, 1}, surface_opaque_pixel(0x000000U), layers);
    require(pixels[0] == 0U && pixels[3] == 255U, "the background beside the tint");
    require(pixels[4] == 60U && pixels[5] == 40U && pixels[6] == 20U && pixels[7] == 255U,
            "the tint drawn source-over the background");
}

void test_compose_kernel() {
    std::vector<std::uint8_t> pixels(8U * 4U * 4U, 0U);
    const SurfaceTarget target{pixels.data(), 8U * 4U, 8U, 4U};
    // A 2 by 2 image, opaque red, and a half-transparent white pixel.
    const std::array<std::uint8_t, 16> image{0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 128, 128, 128, 128};
    std::array<SurfaceComposeLayer, 3> layers{};
    layers[0].kind = SurfaceLayerKind::solid;
    layers[0].bounds = SurfaceDeviceRect{0, 0, 4, 4};
    layers[0].color = surface_opaque_pixel(0x00FF00U);
    layers[1].kind = SurfaceLayerKind::opaque_image;
    layers[1].bounds = SurfaceDeviceRect{4, 0, 6, 2};
    layers[1].pixels = image.data();
    layers[1].row_bytes = 8U;
    layers[1].width = 2U;
    layers[1].height = 2U;
    layers[2] = layers[1];
    layers[2].kind = SurfaceLayerKind::blended_image;
    layers[2].bounds = SurfaceDeviceRect{1, 1, 3, 3};
    surface_compose(target, SurfaceDeviceRect{0, 0, 7, 4}, surface_opaque_pixel(0x0000FFU), layers);
    const std::uint8_t* at_0 = &pixels[0];
    require(at_0[1] == 255U && at_0[2] == 0U && at_0[3] == 255U, "the solid layer");
    const std::uint8_t* at_4 = &pixels[4U * 4U];
    require(at_4[2] == 255U && at_4[0] == 0U, "the opaque image copied");
    const std::uint8_t* blended = &pixels[(2U * 8U + 2U) * 4U];
    require(blended[0] == 128U && blended[1] == 255U && blended[2] == 128U && blended[3] == 255U,
            "source-over: white at half alpha over green");
    const std::uint8_t* background = &pixels[(3U * 8U + 6U) * 4U];
    require(background[0] == 255U && background[1] == 0U, "the background beyond the layers");
    const std::uint8_t* outside = &pixels[(3U * 8U + 7U) * 4U];
    require(outside[0] == 0U && outside[3] == 0U, "nothing outside the clip is written");
    std::vector<std::uint8_t> before = pixels;
    surface_compose(target, SurfaceDeviceRect{20, 20, 30, 30}, surface_opaque_pixel(0U), layers);
    require(pixels == before, "a clip outside the surface writes nothing");
}

void test_cursor_sprites() {
    SurfaceCursorSprite sprite{};
    surface_cursor_sprite(CursorKind::arrow, 1U, sprite);
    require(sprite.width > 0U && sprite.pixels.size() == static_cast<std::size_t>(sprite.width) * sprite.height * 4U,
            "the arrow's pixels");
    require(sprite.hotspot_x == 1 && sprite.hotspot_y == 1, "its point is the shape's corner, inside the outline");
    const std::size_t point = (static_cast<std::size_t>(sprite.hotspot_y) * sprite.width + sprite.hotspot_x) * 4U;
    require(sprite.pixels[point] == 0U && sprite.pixels[point + 3U] == 255U, "black at the point");
    require(sprite.pixels[3] == 255U && sprite.pixels[0] == 255U, "outlined in white");
    const std::uint32_t width = sprite.width;
    surface_cursor_sprite(CursorKind::arrow, 2U, sprite);
    require(sprite.width == 2U * width && sprite.hotspot_x == 2, "scaled by whole pixels");
    SurfaceCursorSprite horizontal{};
    SurfaceCursorSprite vertical{};
    surface_cursor_sprite(CursorKind::resize_horizontal, 1U, horizontal);
    surface_cursor_sprite(CursorKind::resize_vertical, 1U, vertical);
    require(horizontal.width == vertical.height && horizontal.height == vertical.width,
            "the vertical arrows are the horizontal ones turned");
    SurfaceCursorSprite hand{};
    surface_cursor_sprite(CursorKind::hand, 1U, hand);
    require(hand.width == width, "the hand shows the arrow until themed parts exist");
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
        test_click_counts();
        test_wheel_and_context_keys();
        test_input_queue_order_and_waits();
        test_input_queue_bound();
        test_device_rects();
        test_window_states();
        test_minimized_owner_hides_owned_windows_and_tabs();
        test_frame_theme();
        test_compose_kernel();
        test_cursor_sprites();
        test_capsule_parts();
        test_tiling_and_capsule_moves();
        test_tint_layer();
    } catch (const std::exception& error) {
        std::cerr << "surface desk tests failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "surface desk tests passed\n";
    return EXIT_SUCCESS;
}
