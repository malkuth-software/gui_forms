// Checkpoint 28 workstream C (window tree cost and window correctness): a
// control invalidated while scrolled out of its viewport leaves nothing owed
// once the window is painted (C5), and still repaints when scrolled back in;
// a paint requested from inside a paint keeps its rectangles rather than the
// whole window (C6); an abandoned paint marks only the controls it rebuilt
// and carries their flags up to the root (C1); a docked child is measured
// through its cache (N89); a live surface under an open overlay keeps
// presenting every pixel the overlay does not cover, and only those (C7,
// owner ruling D6a); and a hit test whose callbacks never let it converge
// answers the last pass's target and counts the exhausted bound (B11).

#include "gui_forms/basic_controls.hpp"
#include "gui_forms/container_controls.hpp"
#include "gui_forms/live_surface/surface/live_surface.hpp"
#include "gui_forms/live_surface/write_lease/live_surface_write_lease.hpp"
#include "gui_forms/window.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace gui_forms;

void require(const bool condition, const std::string_view message) {
    if (condition) return;
    std::cerr << "window correctness failure: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

class NullPainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override { ++draws; }
    void stroke_rect(Rect, Color, double) override { ++draws; }
    void draw_line(Point, Point, Color, double) override { ++draws; }
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override { ++draws; }
    void draw_image(ImageId, Rect, double) override { ++draws; }

    std::uint64_t draws{};
};

// Paints the pending damage, if any.
void paint_pending(Window& window, NullPainter& painter) {
    const DamageRegion damage = window.take_damage();
    if (!damage.empty()) static_cast<void>(window.paint(painter, damage.bounds()));
}

bool owes_nothing(Window& window) {
    const PaintLeaseState state = window.paint_lease_snapshot().state;
    const bool settled = state == PaintLeaseState::clean || state == PaintLeaseState::ready;
    const bool result = settled && !window.needs_frame();
    return result;
}

// C5: the last item of an unwrapped flow is scrolled out of view.
void test_scrolled_out_control_owes_nothing() {
    std::shared_ptr<FlowLayoutPanelCompat> flow = make_control<FlowLayoutPanelCompat>(StableId("scrolled.flow"));
    (*flow).auto_scroll = true;
    (*flow).wrap_contents = false;
    std::vector<std::shared_ptr<Button>> items;
    for (int index = 0; index < 8; ++index) {
        std::shared_ptr<Button> item =
            make_control<Button>(StableId("scrolled.item." + std::to_string(index)), "Item");
        (*item).set_requested_bounds({0.0, 0.0, 82.0, 76.0});
        (*flow).add_child(item);
        items.push_back(item);
    }
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(flow, Size{300.0, 100.0});
    Window& window = *held_window;
    window.perform_layout();
    NullPainter painter;
    static_cast<void>(window.paint(painter, Rect{0.0, 0.0, 300.0, 100.0}));
    static_cast<void>(window.take_damage());
    require(owes_nothing(window), "a painted window owes nothing");

    const std::shared_ptr<Button> hidden = items.back();
    require((*hidden).absolute_bounds().x >= 300.0, "the last item starts out of view");
    (*hidden).invalidate(invalidation::paint_only);
    paint_pending(window, painter);
    require(owes_nothing(window), "a control invalidated out of view leaves nothing owed after the paint");

    (*flow).scroll_control_into_view(hidden);
    window.perform_layout();
    paint_pending(window, painter);
    require(!has_dirty((*hidden).dirty(), Dirty::paint), "scrolled into view, the control is repainted");
    require(owes_nothing(window), "and nothing is owed afterwards");
}

// Asks for a small explicit paint while the window paints (C6), or fails
// its first paint (C1).
class NestedRequestControl final : public Control {
public:
    explicit NestedRequestControl(StableId id) : Control(std::move(id)) {}
    void on_paint(Painter& painter, Rect damage) override {
        ++paint_calls;
        if (request_nested && paint_calls == 1U) {
            static_cast<void>((*window()).paint(painter, Rect{10.0, 10.0, 20.0, 20.0}));
        }
        if (throw_once && paint_calls == 1U) throw std::runtime_error("paint failed");
        painter.fill_rect(damage, Color::rgba(20, 40, 60));
    }

    bool request_nested{};
    bool throw_once{};
    std::uint64_t paint_calls{};
};

void test_reentrant_request_keeps_its_rectangles() {
    std::shared_ptr<NestedRequestControl> root = make_control<NestedRequestControl>(StableId("nested.root"));
    (*root).set_requested_bounds({0.0, 0.0, 200.0, 100.0});
    (*root).request_nested = true;
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(root, Size{200.0, 100.0});
    Window& window = *held_window;
    window.perform_layout();
    static_cast<void>(window.take_damage());
    NullPainter painter;
    static_cast<void>(window.paint(painter, Rect{0.0, 0.0, 200.0, 100.0}));
    require(window.paint_lease_snapshot().reentrant_requests_deferred == 1U, "the nested request is deferred");
    const DamageRegion follow_up = window.take_damage();
    require(!follow_up.empty(), "the deferred request leaves damage");
    require(follow_up.bounds().width <= 21.0 && follow_up.bounds().height <= 21.0,
            "the deferred request keeps its rectangle, not the whole window");
}

// C1: a paint that fails after rebuilding a sibling marks that sibling and
// carries its flag to the root; the next paint completes and owes nothing.
void test_abandoned_paint_marks_rebuilt_controls() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("abandon.root"));
    (*root).set_requested_bounds({0.0, 0.0, 200.0, 100.0});
    std::shared_ptr<Panel> holder = make_control<Panel>(StableId("abandon.holder"));
    (*holder).set_requested_bounds({0.0, 0.0, 200.0, 100.0});
    std::shared_ptr<Panel> first = make_control<Panel>(StableId("abandon.first"));
    (*first).set_requested_bounds({0.0, 0.0, 50.0, 50.0});
    std::shared_ptr<NestedRequestControl> failing =
        make_control<NestedRequestControl>(StableId("abandon.failing"));
    (*failing).set_requested_bounds({100.0, 0.0, 50.0, 50.0});
    (*root).add_child(holder);
    (*holder).add_child(first);
    (*holder).add_child(failing);
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(root, Size{200.0, 100.0});
    Window& window = *held_window;
    window.perform_layout();
    NullPainter painter;
    static_cast<void>(window.paint(painter, Rect{0.0, 0.0, 200.0, 100.0}));
    static_cast<void>(window.take_damage());

    (*first).invalidate(invalidation::paint_only);
    (*failing).invalidate(invalidation::paint_only);
    (*failing).throw_once = true;
    (*failing).paint_calls = 0U;
    bool failed = false;
    try {
        static_cast<void>(window.paint(painter, Rect{0.0, 0.0, 200.0, 100.0}));
    } catch (const std::exception&) {
        failed = true;
    }
    require(failed && window.paint_lease_snapshot().leases_abandoned == 1U, "the paint is abandoned");
    require(has_dirty((*first).dirty(), Dirty::paint), "the rebuilt sibling is paint-dirty again");
    require(has_dirty((*holder).subtree_dirty(), Dirty::paint) && has_dirty((*root).subtree_dirty(), Dirty::paint),
            "its flag reaches the root's summary");
    paint_pending(window, painter);
    require(!has_dirty((*first).dirty(), Dirty::paint) && !has_dirty((*root).subtree_dirty(), Dirty::paint),
            "the next paint completes");
    require(owes_nothing(window), "and owes nothing");
}

// N89: an arrange of docked children answers their measures from the cache.
void test_docked_children_use_the_measure_cache() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("dock.root"));
    (*root).set_requested_bounds({0.0, 0.0, 200.0, 100.0});
    std::shared_ptr<Panel> top = make_control<Panel>(StableId("dock.top"));
    (*top).set_requested_bounds({0.0, 0.0, 200.0, 20.0});
    (*top).dock = DockStyle::top;
    std::shared_ptr<Panel> fill = make_control<Panel>(StableId("dock.fill"));
    (*fill).dock = DockStyle::fill;
    (*root).add_child(top);
    (*root).add_child(fill);
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(root, Size{200.0, 100.0});
    Window& window = *held_window;
    window.perform_layout();
    const std::uint64_t hits_before = window.metrics_snapshot().measure_cache_hits;
    (*root).invalidate(Dirty::arrange);
    window.perform_layout();
    require(window.metrics_snapshot().measure_cache_hits > hits_before,
            "a second arrange answers the docked children's measures from the cache");
    require((*fill).absolute_bounds().y == 20.0 && (*fill).absolute_bounds().height == 80.0,
            "the fill child takes what the top child leaves");
}

// C7: a live control under an open overlay whose child reaches outside the
// overlay root. The presented clips avoid both covers and add up to the
// uncovered area; a new frame under the cover damages it for retained
// painting rather than presenting it.
void fill_frame(LiveSurfaceWriteLease& lease, std::byte value) {
    for (std::byte& pixel : lease.pixels()) pixel = value;
}

void test_live_surface_under_overlay() {
    std::shared_ptr<Control> root = make_control<Control>(StableId("live-cover.root"));
    (*root).set_requested_bounds({0.0, 0.0, 100.0, 80.0});
    std::shared_ptr<Control> live = make_control<Control>(StableId("live-cover.content"));
    (*live).set_requested_bounds({10.0, 10.0, 70.0, 50.0});
    (*root).add_child(live);
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(root, Size{100.0, 80.0});
    Window& window = *held_window;
    window.perform_layout();

    std::shared_ptr<Control> overlay = make_control<Control>(StableId("live-cover.overlay"));
    (*overlay).set_requested_bounds({20.0, 20.0, 30.0, 20.0});
    // The child leaves its root on the right: window {45, 20, 20, 10}.
    std::shared_ptr<Control> leaving = make_control<Control>(StableId("live-cover.leaving"));
    (*leaving).set_requested_bounds({25.0, 0.0, 20.0, 10.0});
    (*overlay).add_child(leaving);
    OverlayToken token = window.open_overlay(root, overlay);
    window.perform_layout();
    require(token.connected(), "the overlay opens");

    std::shared_ptr<LiveSurface> surface = LiveSurface::create({70U, 50U});
    require(static_cast<bool>(surface), "the live surface is made");
    LiveSurfaceWriteLease first = (*surface).try_acquire_write();
    require(static_cast<bool>(first), "the first frame is written");
    fill_frame(first, std::byte{0x31});
    require(first.publish() == 1U, "the first frame is published");
    require(window.queue_live_surface_presentation(live, surface), "the live control presents the surface");

    const Rect overlay_root_cover{20.0, 20.0, 30.0, 20.0};
    const Rect leaving_cover{45.0, 20.0, 20.0, 10.0};
    const std::vector<LiveSurfacePresentation> presented = window.take_live_surface_presentations();
    require(!presented.empty(), "an open overlay does not suspend the whole live surface");
    double presented_area = 0.0;
    for (const LiveSurfacePresentation& presentation : presented) {
        require(Rect::intersection(presentation.clip, overlay_root_cover).empty(),
                "no presented pixel lies under the overlay root");
        require(Rect::intersection(presentation.clip, leaving_cover).empty(),
                "no presented pixel lies under the child that leaves it");
        presented_area += presentation.clip.area();
    }
    // 70 x 50 less the two covers' union (600 + 200 - 50 overlap).
    require(std::abs(presented_area - (3500.0 - 750.0)) < 1.0e-6, "every uncovered pixel is presented");

    static_cast<void>(window.take_damage());
    LiveSurfaceWriteLease second = (*surface).try_acquire_write();
    require(static_cast<bool>(second), "the second frame is written");
    fill_frame(second, std::byte{0x32});
    require(second.publish() == 2U, "the second frame is published");
    const std::vector<LiveSurfacePresentation> advanced = window.take_live_surface_presentations();
    require(!advanced.empty(), "uncovered pixels advance with the new frame");
    const DamageRegion beneath = window.take_damage();
    const bool under_cover = !Rect::intersection(beneath.bounds(), overlay_root_cover).empty() ||
                             !Rect::intersection(beneath.bounds(), leaving_cover).empty();
    require(!beneath.empty() && under_cover, "the covered share reaches retained painting as damage");
    token.disconnect();
}

// B11: a control whose hit test changes the semantic generation each time
// it is asked, so no pass converges.
class RestlessHitControl final : public Control {
public:
    explicit RestlessHitControl(StableId id) : Control(std::move(id)) {}
    bool hit_test_local(Point local_point) const override {
        ++asked;
        if (other) (*other).invalidate(Dirty::semantics);
        const bool inside = Control::hit_test_local(local_point);
        return inside;
    }

    std::shared_ptr<Control> other;
    mutable std::uint64_t asked{};
};

void test_non_converging_hit_test() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("restless.root"));
    (*root).set_requested_bounds({0.0, 0.0, 100.0, 100.0});
    std::shared_ptr<Panel> other = make_control<Panel>(StableId("restless.other"));
    (*other).set_requested_bounds({80.0, 80.0, 10.0, 10.0});
    std::shared_ptr<RestlessHitControl> restless = make_control<RestlessHitControl>(StableId("restless.target"));
    (*restless).set_requested_bounds({10.0, 10.0, 40.0, 40.0});
    (*root).add_child(other);
    (*root).add_child(restless);
    const std::unique_ptr<Window> held_window = std::make_unique<Window>(root, Size{100.0, 100.0});
    Window& window = *held_window;
    window.perform_layout();
    const MetricsSnapshot before = window.metrics_snapshot();
    (*restless).other = other;
    (*restless).asked = 0U;
    const Control::Ptr hit = window.hit_test({20.0, 20.0});
    (*restless).other.reset();
    const MetricsSnapshot after = window.metrics_snapshot();
    require(hit == restless, "a non-converging hit test answers the last pass's target");
    require((*restless).asked >= 4U, "every bounded pass asked the control");
    require(after.callback_arbitration_retries == before.callback_arbitration_retries + 4U,
            "each of the four passes is counted as a retry");
    require(after.callback_arbitration_limit_hits == before.callback_arbitration_limit_hits + 1U,
            "the exhausted bound is counted once");
}

} // namespace

int main() {
    test_scrolled_out_control_owes_nothing();
    test_reentrant_request_keeps_its_rectangles();
    test_abandoned_paint_marks_rebuilt_controls();
    test_docked_children_use_the_measure_cache();
    test_live_surface_under_overlay();
    test_non_converging_hit_test();
    std::cout << "window correctness tests passed\n";
    return EXIT_SUCCESS;
}
