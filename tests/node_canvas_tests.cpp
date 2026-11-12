// NodeCanvas (node graph gap N1): the public canvas over a NodeGraph, in a
// headless window that is never shown. Covered: the view transform, its
// events and the wheel's zoom about the pointer; the level of detail
// through painting (transitions, hysteresis, what each level draws and
// hit-tests); hit testing against the spatial indexes (ports, captions,
// bodies, links, nothing) and the knife's crossing query; the marquee in
// both modes; a move drag snapped to the world grid as one undo step, the
// click suppressed after it and a press-release without movement narrowing
// the selection; Escape mid-drag rolling the gesture's group back; a link
// drag captured by a port's magnet, a detach dropped off every port asking
// for the palette, and undo restoring the link; lamps repainting without a
// cache rebuild; in-place index moves during a drag; keyboard navigation,
// the keyboard link, Ctrl+arrow moves, Delete and undo; accessibility (the
// group's value, node and port children, their identities, child actions
// and malformed identities refused); the edit host (cancel rolls back,
// commit is one step).

#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/node_canvas.hpp"
#include "gui_forms/controls/node_graph.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/event/delegate/delegate.hpp"
#include "gui_forms/event/subscription_token/subscription_token.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/scene/scene_view/scene_view.hpp"
#include "gui_forms/scene/spatial_index/spatial_index.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/window/window.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "gui_forms_node_canvas_tests: " << message << '\n';
    std::exit(1);
}

// Counts the canvas's batched calls; draws nothing.
class RecordingPainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override { ++fills; }
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override { ++texts; }
    void draw_image(ImageId, Rect, double) override {}
    void stroke_polyline(std::span<const Point>, const StrokeStyle&) override { ++polylines; }
    void fill_rects(std::span<const Rect> rects, Color) override { batched_rects += rects.size(); }
    void draw_markers(std::span<const Point> centres, const MarkerStyle&) override { markers += centres.size(); }
    void fill_grid_pattern(Rect, const GridPattern&) override { ++grids; }

    std::size_t fills{};
    std::size_t texts{};
    std::size_t polylines{};
    std::size_t batched_rects{};
    std::size_t markers{};
    std::size_t grids{};
};

PortRecord port_of(const char* name, PortDirection direction, PortSide side) {
    PortRecord port{};
    port.name = name;
    port.direction = direction;
    port.side = side;
    port.offset = 0.5;
    port.socket_type = 1U;
    return port;
}

NodeSpec spec_of(const char* key, const char* title, double x, double y) {
    NodeSpec spec{};
    spec.key = key;
    spec.kind = key;
    spec.title = title;
    spec.bounds = WorldRect{x, y, 96.0, 64.0};
    return spec;
}

// Three nodes at zoom 1 (world = local): Noise (30, 50) with one output,
// Mix (200, 50) with an input and an output, Out (200, 180) with one input,
// each 96 by 64; Noise.out feeds Mix.in along y = 82. The ports sit at
// Noise.out (126, 82), Mix.in (200, 82), Mix.out (296, 82) and Out.in
// (200, 212). The graph is declared first so it outlives the canvas.
class Fixture final {
public:
    Fixture() {
        NodeSpec noise = spec_of("noise", "Noise", 30.0, 50.0);
        noise.ports.push_back(port_of("out", PortDirection::output, PortSide::right));
        NodeSpec mix = spec_of("mix", "Mix", 200.0, 50.0);
        mix.ports.push_back(port_of("in", PortDirection::input, PortSide::left));
        mix.ports.push_back(port_of("out", PortDirection::output, PortSide::right));
        NodeSpec out = spec_of("out", "Out", 200.0, 180.0);
        out.ports.push_back(port_of("in", PortDirection::input, PortSide::left));
        a = graph.add_node(noise).node;
        b = graph.add_node(mix).node;
        c = graph.add_node(out).node;
        const GraphResult joined = graph.connect(PortRef{a, PortIndex{0U}}, PortRef{b, PortIndex{0U}});
        require(joined.status == GraphStatus::ok, "the fixture's link is made");
        link = joined.link;
        static_cast<void>(graph.clear_undo());
        canvas = make_control<NodeCanvas>(StableId("canvas"));
        (*canvas).set_graph(&graph);
        window = std::make_unique<Window>(canvas, Size{400.0, 300.0});
        (*window).set_active(true);
        (*window).perform_layout();
    }

    void paint() {
        RecordingPainter painter{};
        static_cast<void>((*window).paint(painter, Rect{0.0, 0.0, 400.0, 300.0}));
    }

    void pointer(PointerAction action, PointerButton button, Point at, Modifier modifiers, std::uint32_t clicks) {
        PointerEvent event{};
        event.action = action;
        event.button = button;
        event.position = at;
        event.modifiers = modifiers;
        event.click_count = clicks;
        static_cast<void>((*window).dispatch_pointer(event));
        (*window).perform_layout();
    }

    void press(Point at, Modifier modifiers) {
        pointer(PointerAction::down, PointerButton::primary, at, modifiers, 1U);
    }
    void move(Point at, Modifier modifiers) { pointer(PointerAction::move, PointerButton::none, at, modifiers, 0U); }
    void release(Point at, Modifier modifiers) {
        pointer(PointerAction::up, PointerButton::primary, at, modifiers, 1U);
    }
    void click(Point at) {
        press(at, Modifier::none);
        release(at, Modifier::none);
    }
    void double_click(Point at) {
        click(at);
        pointer(PointerAction::down, PointerButton::primary, at, Modifier::none, 2U);
        pointer(PointerAction::up, PointerButton::primary, at, Modifier::none, 2U);
    }
    // Twelve moves from `from` to `to`, without the press or the release.
    void glide(Point from, Point to, Modifier modifiers) {
        for (std::size_t step = 1U; step <= 12U; ++step) {
            const double t = static_cast<double>(step) / 12.0;
            const Point at{from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t};
            move(at, modifiers);
        }
    }
    void drag(Point from, Point to, Modifier modifiers) {
        press(from, modifiers);
        glide(from, to, modifiers);
        release(to, modifiers);
    }
    void key(std::uint32_t physical_key, Modifier modifiers) {
        KeyEvent event{};
        event.action = KeyAction::down;
        event.physical_key = physical_key;
        event.modifiers = modifiers;
        static_cast<void>((*window).dispatch_key(event));
        event.action = KeyAction::up;
        static_cast<void>((*window).dispatch_key(event));
        (*window).perform_layout();
    }

    [[nodiscard]] WorldRect bounds_of(NodeId node) const {
        const NodeRecord* record = graph.node(node);
        require(record != nullptr, "the node is live");
        return (*record).bounds;
    }

    NodeGraph graph{};
    NodeId a{};
    NodeId b{};
    NodeId c{};
    LinkId link{};
    std::shared_ptr<NodeCanvas> canvas{};
    std::unique_ptr<Window> window{};
};

// -- event recorders (named callables) ----------------------------------------------------------

class CountViews final {
public:
    explicit CountViews(std::size_t& count) : count_(count) {}
    void operator()(const SceneViewTransform&) const { ++count_; }

private:
    std::size_t& count_;
};

class CountSelections final {
public:
    explicit CountSelections(std::size_t& count) : count_(count) {}
    void operator()(const NodeCanvasSelection&) const { ++count_; }

private:
    std::size_t& count_;
};

class RecordConnections final {
public:
    explicit RecordConnections(std::vector<NodeLinkConnected>& connected) : connected_(connected) {}
    void operator()(const NodeLinkConnected& connected) const { connected_.push_back(connected); }

private:
    std::vector<NodeLinkConnected>& connected_;
};

class RecordPalette final {
public:
    explicit RecordPalette(std::vector<NodePaletteRequest>& requests) : requests_(requests) {}
    void operator()(const NodePaletteRequest& request) const { requests_.push_back(request); }

private:
    std::vector<NodePaletteRequest>& requests_;
};

class RecordEditEnds final {
public:
    explicit RecordEditEnds(std::vector<NodeEditEnded>& ends) : ends_(ends) {}
    void operator()(const NodeEditEnded& ended) const { ends_.push_back(ended); }

private:
    std::vector<NodeEditEnded>& ends_;
};

// The application's compatibility answer: no link is acceptable.
class RejectAll final {
public:
    void check(NodeLinkCheck& link) { link.accepted = false; }
};

// The application's edit target: hosts a plain panel as the editor unless
// told to decline.
class EditHost final {
public:
    void edit(NodeEditRequest& request) {
        ++requests;
        last_part = request.part;
        last_bounds = request.bounds;
        if (decline) return;
        request.editor = make_control<Panel>(StableId("canvas.editor"));
    }

    std::size_t requests{};
    NodeEditPart last_part{NodeEditPart::body};
    Rect last_bounds{};
    bool decline{};
};

// -- view -------------------------------------------------------------------------------------

void view_transform_and_events() {
    Fixture fixture{};
    NodeCanvas& canvas = *fixture.canvas;
    std::size_t views = 0U;
    const SubscriptionToken token = canvas.view_changed().subscribe(CountViews(views));
    require(canvas.view() == SceneViewTransform{}, "a new canvas shows world = local");
    require(canvas.set_view(SceneViewTransform{WorldPoint{10.0, 20.0}, 2.0}) && views == 1U,
            "set_view takes a valid view and raises view_changed");
    require(canvas.set_view(SceneViewTransform{WorldPoint{10.0, 20.0}, 2.0}) && views == 1U,
            "the same view raises nothing");
    require(!canvas.set_view(SceneViewTransform{WorldPoint{}, -1.0}) && views == 1U, "an invalid view is refused");
    const std::optional<Point> anchor = canvas.port_view_anchor(PortRef{fixture.a, PortIndex{0U}});
    require(anchor.has_value() && (*anchor).x == (126.0 - 10.0) * 2.0 && (*anchor).y == (82.0 - 20.0) * 2.0,
            "a port's view anchor follows the transform");
    const WorldPoint under = canvas.view().to_world(Point{100.0, 60.0});
    canvas.zoom_about(Point{100.0, 60.0}, 2.0);
    const WorldPoint still = canvas.view().to_world(Point{100.0, 60.0});
    require(canvas.view().zoom == 4.0 && still.x == under.x && still.y == under.y,
            "zoom_about keeps the anchor's world point under it");
    canvas.zoom_about(Point{100.0, 60.0}, 100.0);
    require(canvas.view().zoom == canvas.zoom_range().maximum, "the zoom is held to the range");
    canvas.pan_by(Point{8.0, -4.0});
    require(views == 4U, "zooms and pans raise view_changed");

    // The wheel zooms about the pointer.
    require(canvas.set_view(SceneViewTransform{}), "back to the plain view");
    PointerEvent wheel{};
    wheel.action = PointerAction::wheel;
    wheel.position = Point{200.0, 150.0};
    wheel.wheel_delta = Point{0.0, 1.0};
    static_cast<void>((*fixture.window).dispatch_pointer(wheel));
    const WorldPoint pointer_world = canvas.view().to_world(Point{200.0, 150.0});
    require(std::fabs(canvas.view().zoom - 1.15) < 1.0e-12 && std::fabs(pointer_world.x - 200.0) < 1.0e-9 &&
                std::fabs(pointer_world.y - 150.0) < 1.0e-9,
            "one wheel line zooms by 1.15 about the pointer");

    // Framing everything puts every node inside the view.
    canvas.frame_all();
    const Rect shown = canvas.view().rect_to_view(WorldRect{30.0, 50.0, 266.0, 194.0});
    require(shown.x >= 0.0 && shown.y >= 0.0 && shown.right() <= 400.0 && shown.bottom() <= 300.0,
            "frame_all fits the graph");
}

// -- level of detail --------------------------------------------------------------------------

void level_of_detail() {
    Fixture fixture{};
    NodeCanvas& canvas = *fixture.canvas;
    fixture.paint();
    NodeCanvasStatistics before = canvas.statistics();
    require(canvas.detail_level() == SceneDetailLevel::full && before.frames_painted >= 1U,
            "zoom 1 paints at full detail");
    require(before.ports_painted >= 4U && before.last_frame_nodes == 3U, "full detail paints every port and node");
    const NodeCanvasHit port_hit = canvas.hit_test(Point{126.0, 82.0});
    require(port_hit.kind == NodeCanvasHitKind::port, "at full detail ports are hit");

    require(canvas.set_view(SceneViewTransform{WorldPoint{}, 0.4}), "zoom out to compact");
    fixture.paint();
    require(canvas.detail_level() == SceneDetailLevel::compact &&
                canvas.statistics().level_transitions == before.level_transitions + 1U,
            "zoom 0.4 is compact, one transition");

    require(canvas.set_view(SceneViewTransform{WorldPoint{}, 0.05}), "zoom out to the overview");
    fixture.paint();
    require(canvas.detail_level() == SceneDetailLevel::overview, "zoom 0.05 is the overview");
    const std::uint64_t ports = canvas.statistics().ports_painted;
    fixture.paint();
    require(canvas.statistics().ports_painted == ports, "the overview paints no ports");
    const NodeCanvasHit overview_hit = canvas.hit_test(Point{126.0 * 0.05, 82.0 * 0.05});
    require(overview_hit.kind != NodeCanvasHitKind::port, "where ports are not drawn they are not hit");

    // Hysteresis: 0.62 is above the full threshold (0.6) but inside its
    // band, so from below it is compact, and from full it stays full.
    require(canvas.set_view(SceneViewTransform{WorldPoint{}, 0.62}), "zoom to the band");
    fixture.paint();
    require(canvas.detail_level() == SceneDetailLevel::compact, "rising into the band stops at compact");
    require(canvas.set_view(SceneViewTransform{WorldPoint{}, 1.0}), "zoom to full");
    fixture.paint();
    require(canvas.set_view(SceneViewTransform{WorldPoint{}, 0.58}), "fall into the band");
    fixture.paint();
    require(canvas.detail_level() == SceneDetailLevel::full, "falling into the band keeps full");
    require(!canvas.set_detail_thresholds(SceneDetailThresholds{0.5, 0.2, 0.6, 1.1}),
            "descending thresholds are refused");

    // Paint allocates nothing per node: the counters' frame sizes stay the
    // visible set's.
    require(canvas.statistics().last_frame_nodes == 3U && canvas.visible_nodes().size() == 3U,
            "the visible set is every node in view");
    require(canvas.set_view(SceneViewTransform{WorldPoint{1000.0, 1000.0}, 1.0}), "look away from the graph");
    fixture.paint();
    require(canvas.visible_nodes().empty() && canvas.statistics().last_frame_nodes == 0U,
            "with nothing in view nothing is painted");
}

// -- hit testing ------------------------------------------------------------------------------

void hit_testing() {
    Fixture fixture{};
    NodeCanvas& canvas = *fixture.canvas;
    const NodeCanvasHit body = canvas.hit_test(Point{78.0, 100.0});
    require(body.kind == NodeCanvasHitKind::node_body && body.node == fixture.a, "a body is hit");
    const NodeCanvasHit caption = canvas.hit_test(Point{78.0, 60.0});
    require(caption.kind == NodeCanvasHitKind::node_caption && caption.node == fixture.a, "a caption is hit");
    const NodeCanvasHit port = canvas.hit_test(Point{131.0, 84.0});
    require(port.kind == NodeCanvasHitKind::port && port.node == fixture.a && port.port.value == 0U,
            "a port is hit within its radius, before the body");
    const NodeCanvasHit link = canvas.hit_test(Point{163.0, 84.0});
    require(link.kind == NodeCanvasHitKind::link && link.link == fixture.link, "a link is hit within 4 units");
    const NodeCanvasHit nothing = canvas.hit_test(Point{350.0, 280.0});
    require(nothing.kind == NodeCanvasHitKind::none, "empty canvas hits nothing");
    require(canvas.node_index_statistics().builds == 1U && canvas.statistics().node_index_builds == 1U,
            "hit tests share one index build");
    require(canvas.node_index_statistics().queries >= 5U, "each hit test queries the index");

    // The knife's query: a segment across the link.
    std::array<LinkId, 4> crossed{};
    const std::size_t count = canvas.links_crossing(Point{163.0, 60.0}, Point{163.0, 110.0}, crossed);
    require(count == 1U && crossed[0] == fixture.link, "a segment across the link crosses it");
    require(canvas.links_crossing(Point{350.0, 0.0}, Point{350.0, 300.0}, crossed) == 0U,
            "a segment away from every link crosses none");

    // A moved node is hit at its new place once the graph changed.
    require(fixture.graph.move_nodes(std::span<const NodeId>(&fixture.c, 1U), WorldPoint{0.0, -100.0}).status ==
                    GraphStatus::ok,
            "move Out up");
    const NodeCanvasHit moved = canvas.hit_test(Point{248.0, 110.0});
    require(moved.kind == NodeCanvasHitKind::node_body && moved.node == fixture.c &&
                canvas.statistics().node_index_builds == 2U,
            "a revision change rebuilds the index for the next query");
}

// -- marquee ----------------------------------------------------------------------------------

void marquee() {
    Fixture fixture{};
    NodeCanvas& canvas = *fixture.canvas;
    std::size_t selections = 0U;
    const SubscriptionToken token = canvas.selection_changed().subscribe(CountSelections(selections));
    fixture.drag(Point{10.0, 10.0}, Point{310.0, 140.0}, Modifier::none);
    require(canvas.selection().size() == 2U && canvas.selected(fixture.a) && canvas.selected(fixture.b) &&
                !canvas.selected(fixture.c),
            "the marquee selects the nodes it contains");
    require(canvas.statistics().marquee_selections == 1U && selections >= 1U, "the marquee is counted and raised");
    // Contained: the box reaching into Out selects only Noise.
    fixture.drag(Point{10.0, 10.0}, Point{220.0, 200.0}, Modifier::none);
    require(canvas.selection().size() == 1U && canvas.selected(fixture.a), "containment needs the whole node");
    // Alt: touching is enough.
    fixture.drag(Point{10.0, 10.0}, Point{220.0, 200.0}, Modifier::alt);
    require(canvas.selection().size() == 3U, "with Alt the marquee selects what it touches");
    // Shift adds to the selection.
    canvas.select_only(fixture.c);
    fixture.drag(Point{10.0, 10.0}, Point{140.0, 130.0}, Modifier::shift);
    require(canvas.selection().size() == 2U && canvas.selected(fixture.a) && canvas.selected(fixture.c),
            "with Shift the marquee adds");
    // A press on empty canvas without a drag clears the selection.
    fixture.click(Point{350.0, 280.0});
    require(canvas.selection().empty(), "a click on empty canvas clears the selection");
    // select_in_rect directly, both modes.
    require(canvas.select_in_rect(WorldRect{0.0, 0.0, 400.0, 300.0}, SpatialQueryMode::contained, false) == 3U,
            "select_in_rect contains every node");
    require(canvas.select_in_rect(WorldRect{290.0, 60.0, 10.0, 30.0}, SpatialQueryMode::intersecting, false) == 1U &&
                canvas.selected(fixture.b),
            "select_in_rect by intersection");
}

// -- move, snap and the click after a drag ---------------------------------------------------------

void move_snap_and_suppressed_click() {
    Fixture fixture{};
    NodeCanvas& canvas = *fixture.canvas;
    fixture.paint();
    canvas.select_only(fixture.a);
    canvas.add_to_selection(fixture.b);
    const std::size_t steps = fixture.graph.undo_steps();
    const std::uint64_t builds = canvas.statistics().node_index_builds;
    fixture.press(Point{78.0, 100.0}, Modifier::none);
    fixture.glide(Point{78.0, 100.0}, Point{115.0, 121.0}, Modifier::none);
    require(canvas.gesture() == NodeCanvasGesture::move, "a press on a selected body drags the selection");
    require(canvas.statistics().index_moves_in_place > 0U && canvas.statistics().node_index_builds == builds,
            "the drag's frames move index entries in place, without a rebuild");
    fixture.release(Point{115.0, 121.0}, Modifier::none);
    const WorldRect noise = fixture.bounds_of(fixture.a);
    const WorldRect mix = fixture.bounds_of(fixture.b);
    require(std::fmod(noise.x, 16.0) == 0.0 && std::fmod(noise.y, 16.0) == 0.0,
            "the dragged node lands on the world grid");
    require(noise.x != 30.0 && mix.x - noise.x == 170.0 && mix.y - noise.y == 0.0,
            "the whole selection moves together");
    require(fixture.graph.undo_steps() == steps + 1U, "one drag is one undo step");
    require(canvas.selection().size() == 2U && canvas.statistics().clicks_suppressed == 1U,
            "the release of a drag is no click: the selection is kept");
    require(canvas.statistics().gestures_committed >= 1U, "the gesture commits");

    require(canvas.undo(), "undo the drag");
    require(fixture.bounds_of(fixture.a) == WorldRect{30.0, 50.0, 96.0, 64.0} &&
                fixture.bounds_of(fixture.b) == WorldRect{200.0, 50.0, 96.0, 64.0},
            "one undo restores every node");
    require(canvas.redo() && fixture.bounds_of(fixture.a) == noise, "redo moves them again");
    require(canvas.undo(), "undo once more");

    // A press and release without movement on a selected node is a click:
    // it narrows the selection.
    fixture.click(Point{78.0, 100.0});
    require(canvas.selection().size() == 1U && canvas.selected(fixture.a), "a click narrows the selection");
    require(fixture.graph.undo_steps() == steps, "a click records nothing");

    // With the snap off the node follows the pointer exactly.
    canvas.set_snap_to_grid(false);
    fixture.drag(Point{78.0, 100.0}, Point{85.0, 109.0}, Modifier::none);
    require(fixture.bounds_of(fixture.a) == WorldRect{37.0, 59.0, 96.0, 64.0}, "without the snap the move is exact");
}

void escape_rolls_back() {
    Fixture fixture{};
    NodeCanvas& canvas = *fixture.canvas;
    canvas.select_only(fixture.a);
    const std::size_t steps = fixture.graph.undo_steps();
    const std::uint64_t cancelled = canvas.statistics().gestures_cancelled;
    fixture.press(Point{78.0, 100.0}, Modifier::none);
    fixture.glide(Point{78.0, 100.0}, Point{150.0, 180.0}, Modifier::none);
    require(fixture.bounds_of(fixture.a).x != 30.0, "the node moved during the drag");
    fixture.key(PhysicalKey::escape, Modifier::none);
    require(canvas.gesture() == NodeCanvasGesture::none, "Escape ends the gesture");
    fixture.release(Point{150.0, 180.0}, Modifier::none);
    require(fixture.bounds_of(fixture.a) == WorldRect{30.0, 50.0, 96.0, 64.0}, "the node is back where it was");
    require(fixture.graph.undo_steps() == steps && canvas.statistics().gestures_cancelled == cancelled + 1U,
            "a cancelled drag leaves no undo step");
}

// -- links ------------------------------------------------------------------------------------

void link_drags_and_detach() {
    Fixture fixture{};
    NodeCanvas& canvas = *fixture.canvas;
    fixture.paint();
    std::vector<NodeLinkConnected> connected{};
    std::vector<NodePaletteRequest> palette{};
    const SubscriptionToken connections = canvas.link_connected().subscribe(RecordConnections(connected));
    const SubscriptionToken requests = canvas.palette_requested().subscribe(RecordPalette(palette));
    const PortRef out_in{fixture.c, PortIndex{0U}};
    const std::size_t steps = fixture.graph.undo_steps();
    // From Mix.out to Out.in: the magnet captures the input.
    fixture.drag(Point{296.0, 82.0}, Point{203.0, 209.0}, Modifier::none);
    require(fixture.graph.link(fixture.graph.link_into(out_in)) != nullptr, "the link drag connects");
    require(connected.size() == 1U && connected[0].to == out_in && connected[0].from.node == fixture.b,
            "link_connected names the ports");
    require(canvas.statistics().magnet_captures >= 1U && canvas.statistics().connections_made == 1U,
            "the magnet captured the port");
    require(fixture.graph.undo_steps() == steps + 1U, "a connection is one undo step");

    // Detach Mix.in and drop it on empty canvas: the palette is asked for,
    // with the source output.
    const PortRef mix_in{fixture.b, PortIndex{0U}};
    fixture.drag(Point{200.0, 82.0}, Point{350.0, 280.0}, Modifier::none);
    require(fixture.graph.link(fixture.graph.link_into(mix_in)) == nullptr, "the detached link is gone");
    require(palette.size() == 1U && palette[0].from.has_value() && (*palette[0].from).node == fixture.a,
            "a link dropped off every port asks for the palette with its output");
    require(std::fabs(palette[0].local.x - 350.0) < 2.0 && std::fabs(palette[0].local.y - 280.0) < 2.0,
            "the palette is asked for at the release");
    require(fixture.graph.undo_steps() == steps + 2U && canvas.statistics().links_detached == 1U,
            "the detach is one step");
    require(canvas.undo() && fixture.graph.link(fixture.graph.link_into(mix_in)) != nullptr,
            "undo restores the detached link");

    // A refused check: the application rejects every link.
    RejectAll reject{};
    NodeCanvasCallbacks callbacks{};
    callbacks.check_link = Delegate<NodeLinkCheck&>::bind<RejectAll, &RejectAll::check>(reject);
    canvas.set_callbacks(callbacks);
    const GraphResult refused =
        canvas.connect_ports(PortRef{fixture.a, PortIndex{0U}}, PortRef{fixture.c, PortIndex{0U}});
    require(refused.status != GraphStatus::ok, "a rejected link is not made");
    canvas.set_callbacks(NodeCanvasCallbacks{});
}

void cut_links_with_the_knife() {
    Fixture fixture{};
    NodeCanvas& canvas = *fixture.canvas;
    const std::size_t steps = fixture.graph.undo_steps();
    require(canvas.cut_links(Point{163.0, 60.0}, Point{163.0, 110.0}) == 1U && fixture.graph.link_count() == 0U,
            "the knife cuts the link it crosses");
    require(fixture.graph.undo_steps() == steps + 1U && canvas.undo() && fixture.graph.link_count() == 1U,
            "one cut is one step");
}

// -- lamps --------------------------------------------------------------------------------------

void lamps_keep_the_caches() {
    Fixture fixture{};
    NodeCanvas& canvas = *fixture.canvas;
    fixture.paint();
    const NodeCanvasStatistics before = canvas.statistics();
    require(fixture.graph.set_port_lamp(PortRef{fixture.b, PortIndex{0U}}, PortLamp::on).status == GraphStatus::ok,
            "a lamp is lit");
    canvas.refresh();
    fixture.paint();
    const NodeCanvasStatistics after = canvas.statistics();
    require(after.node_index_builds == before.node_index_builds &&
                after.link_index_builds == before.link_index_builds,
            "a lamp change rebuilds no index");
    require(after.lamp_repaints == before.lamp_repaints + 1U && after.frames_painted > before.frames_painted,
            "a lamp change repaints");
    require(after.lamps_painted > before.lamps_painted, "the lit lamp is painted");
    const std::vector<SemanticNode> children = canvas.semantic_virtual_children();
    bool lit = false;
    for (const SemanticNode& node : children) {
        for (const SemanticNode& port : node.children) {
            if (port.value == "on") lit = true;
        }
    }
    require(lit, "the lit port's semantic value is on");
}

// -- keyboard -----------------------------------------------------------------------------------

void keyboard() {
    Fixture fixture{};
    NodeCanvas& canvas = *fixture.canvas;
    fixture.paint();
    require((*fixture.window).request_focus(fixture.canvas), "the canvas takes focus");
    require(canvas.focused_node().has_value() && *canvas.focused_node() == fixture.a,
            "focus enters at the first visible node");
    fixture.key(PhysicalKey::right, Modifier::none);
    require(canvas.selection().size() == 1U && canvas.selected(fixture.b), "right goes to the node to the right");
    require(canvas.announcement() == "Mix, mix", "the arrival is announced");
    fixture.key(PhysicalKey::down, Modifier::none);
    require(canvas.selected(fixture.c) && *canvas.focused_node() == fixture.c, "down goes to the node below");

    const std::size_t steps = fixture.graph.undo_steps();
    fixture.key(PhysicalKey::right, Modifier::control);
    require(fixture.bounds_of(fixture.c).x == 216.0 && fixture.graph.undo_steps() == steps + 1U,
            "Ctrl+right moves the selection one grid cell, one step");
    fixture.key(PhysicalKey::z, Modifier::control);
    require(fixture.bounds_of(fixture.c).x == 200.0, "Ctrl+Z undoes it");
    fixture.key(PhysicalKey::z, Modifier::control | Modifier::shift);
    require(fixture.bounds_of(fixture.c).x == 216.0, "Ctrl+Shift+Z redoes it");
    fixture.key(PhysicalKey::z, Modifier::control);

    // Out has no output: no keyboard link.
    fixture.key(PhysicalKey::l, Modifier::none);
    require(canvas.announcement() == "No output to link", "a node without outputs says so");
    // Up to Mix; L links from its output, Enter connects to Out.in.
    fixture.key(PhysicalKey::up, Modifier::none);
    require(canvas.selected(fixture.b), "up goes back to Mix");
    fixture.key(PhysicalKey::l, Modifier::none);
    require(canvas.announcement() == "Link to Out in, 1 of 1", "the keyboard link announces its target");
    fixture.key(PhysicalKey::down, Modifier::none);
    require(canvas.announcement() == "Link to Out in, 1 of 1", "one target cycles to itself");
    fixture.key(PhysicalKey::enter, Modifier::none);
    require(canvas.announcement() == "Linked" &&
                fixture.graph.link(fixture.graph.link_into(PortRef{fixture.c, PortIndex{0U}})) != nullptr,
            "Enter connects");
    // With the only input taken, no keyboard link starts.
    fixture.key(PhysicalKey::l, Modifier::none);
    require(canvas.announcement() == "No compatible input", "with Out.in taken nothing is compatible");

    // Enter walks into the ports; arrows walk them; Escape leaves.
    fixture.key(PhysicalKey::enter, Modifier::none);
    require(canvas.focused_port().has_value() && (*canvas.focused_port()).value == 0U &&
                canvas.announcement() == "input in",
            "Enter enters the ports");
    fixture.key(PhysicalKey::down, Modifier::none);
    require((*canvas.focused_port()).value == 1U && canvas.announcement() == "output out", "arrows walk the ports");
    fixture.key(PhysicalKey::escape, Modifier::none);
    require(!canvas.focused_port().has_value(), "Escape leaves the ports");

    // Delete removes the selection; Ctrl+Z brings it back.
    fixture.key(PhysicalKey::delete_forward, Modifier::none);
    require(fixture.graph.node(fixture.b) == nullptr && fixture.graph.node_count() == 2U, "Delete removes Mix");
    fixture.key(PhysicalKey::z, Modifier::control);
    require(fixture.graph.node(fixture.b) != nullptr && fixture.graph.link_count() == 2U,
            "Ctrl+Z restores Mix and its links");

    // Ctrl+A, Home and the zoom keys.
    fixture.key(PhysicalKey::a, Modifier::control);
    require(canvas.selection().size() == 3U, "Ctrl+A selects every node");
    const double zoom = canvas.view().zoom;
    fixture.key(PhysicalKey::equal, Modifier::none);
    require(std::fabs(canvas.view().zoom - zoom * 1.25) < 1.0e-12, "= zooms in");
    fixture.key(PhysicalKey::minus, Modifier::none);
    require(std::fabs(canvas.view().zoom - zoom) < 1.0e-12, "- zooms out");
    require(canvas.statistics().key_presses_suppressed > 0U, "the canvas's keys produce no text");
}

// -- accessibility ----------------------------------------------------------------------------------

std::string node_id_text(NodeId node) {
    std::string text{"canvas.node-"};
    text.append(std::to_string(node.index));
    text.push_back('-');
    text.append(std::to_string(node.generation));
    return text;
}

void accessibility() {
    Fixture fixture{};
    NodeCanvas& canvas = *fixture.canvas;
    fixture.paint();
    const SemanticDescriptor descriptor = canvas.semantic_descriptor();
    require(descriptor.role == SemanticRole::group && descriptor.value == "3 nodes, 1 links",
            "the canvas is a group valued by its counts");
    require(has_semantic_state(descriptor.states, SemanticState::multiselectable),
            "the canvas is multiselectable");
    const std::vector<SemanticNode> children = canvas.semantic_virtual_children();
    require(children.size() == 4U, "three node children and the announcements region");
    require(children.back().role == SemanticRole::live_region && children.back().stable_id == "canvas.announcements",
            "the announcements region is always present");
    const std::string mix_id = node_id_text(fixture.b);
    const SemanticNode* mix = nullptr;
    for (const SemanticNode& child : children) {
        if (child.stable_id == mix_id) mix = &child;
    }
    require(mix != nullptr, "each visible node has its identity");
    require((*mix).role == SemanticRole::list_item && (*mix).name == "Mix, mix", "a node is a list item named by title");
    require((*mix).children.size() == 2U && (*mix).children[0].stable_id == mix_id + ".port-0" &&
                (*mix).children[0].name == "input in, connected to Noise" && (*mix).children[1].name == "output out",
            "ports are children; an input names its source (no flow relations yet, gap 13)");
    require((*mix).children[0].position_in_set == std::optional<std::size_t>{1U} &&
                (*mix).children[0].set_size == std::optional<std::size_t>{2U},
            "ports have positions in their set");

    // Child actions select and reveal; press opens an edit.
    require(canvas.on_semantic_child_action(node_id_text(fixture.c), SemanticAction::select, std::string_view{}) &&
                canvas.selection().size() == 1U && canvas.selected(fixture.c),
            "select on a node child selects it");
    require(canvas.on_semantic_child_action(mix_id + ".port-1", SemanticAction::focus, std::string_view{}) &&
                canvas.selected(fixture.b),
            "a port child acts on its node");
    const std::array<std::string_view, 8> malformed{
        "canvas.node-", "canvas.node-1", "canvas.node-x-1", "canvas.node-1-", "other.node-1-1",
        "canvas.node-1-999", "canvas.node-1-1.port-", "canvas.node-1-1.port-1x",
    };
    for (const std::string_view text : malformed) {
        require(!canvas.on_semantic_child_action(text, SemanticAction::select, std::string_view{}),
                "a malformed or stale identity is refused");
    }
    require(!canvas.on_semantic_child_action(mix_id, SemanticAction::increment, std::string_view{}),
            "an action nodes do not take is refused");
    // The focused node is realized even out of view.
    require(canvas.set_focused_node(fixture.c), "focus Out");
    require(canvas.set_view(SceneViewTransform{WorldPoint{-1000.0, -1000.0}, 1.0}), "look away");
    fixture.paint();
    const std::vector<SemanticNode> away = canvas.semantic_virtual_children();
    require(away.size() == 2U && away[0].stable_id == node_id_text(fixture.c),
            "out of view only the focused node is realized");
}

// -- edit host ----------------------------------------------------------------------------------

void edit_host() {
    Fixture fixture{};
    NodeCanvas& canvas = *fixture.canvas;
    EditHost host{};
    NodeCanvasCallbacks callbacks{};
    callbacks.edit = Delegate<NodeEditRequest&>::bind<EditHost, &EditHost::edit>(host);
    canvas.set_callbacks(callbacks);
    std::vector<NodeEditEnded> ends{};
    const SubscriptionToken token = canvas.edit_ended().subscribe(RecordEditEnds(ends));
    const std::size_t steps = fixture.graph.undo_steps();

    // Double-click on a caption opens a rename over the caption band.
    fixture.double_click(Point{78.0, 60.0});
    require(canvas.editing() && canvas.edited_node() == fixture.a && host.last_part == NodeEditPart::caption,
            "a double-click on a caption edits it");
    require(host.last_bounds == Rect{30.0, 50.0, 96.0, NodeCanvas::caption_height}, "the editor covers the caption");
    require(!canvas.delete_selection(), "the canvas does not edit the graph while an editor is open");
    require(fixture.graph.set_title(fixture.a, "Renamed").status == GraphStatus::ok, "the editor renames");
    canvas.end_edit(NodeEditEnd::cancel);
    require(!canvas.editing() && (*fixture.graph.node(fixture.a)).title == "Noise" &&
                fixture.graph.undo_steps() == steps,
            "cancel rolls the editor's changes back");
    require(ends.size() == 1U && ends[0].end == NodeEditEnd::cancel, "edit_ended reports the cancel");

    // F2 edits the focused node's body; commit is one step.
    require((*fixture.window).request_focus(fixture.canvas), "focus the canvas");
    canvas.select_only(fixture.a);
    fixture.key(PhysicalKey::f2, Modifier::none);
    require(canvas.editing() && host.last_part == NodeEditPart::body, "F2 edits the body");
    require(fixture.graph.set_title(fixture.a, "Renamed").status == GraphStatus::ok &&
                fixture.graph.set_property(fixture.a, "scale", "5").status == GraphStatus::ok,
            "the editor makes two changes");
    canvas.end_edit(NodeEditEnd::commit);
    require((*fixture.graph.node(fixture.a)).title == "Renamed" && fixture.graph.undo_steps() == steps + 1U,
            "commit keeps the changes as one step");
    require(canvas.undo() && (*fixture.graph.node(fixture.a)).title == "Noise", "one undo reverts the edit");

    // A declined edit opens nothing and records nothing.
    host.decline = true;
    require(!canvas.begin_edit(fixture.a, NodeEditPart::body) && !canvas.editing(), "a declined edit opens nothing");
    require(canvas.statistics().edit_requests == 3U, "every request is counted");
    canvas.set_callbacks(NodeCanvasCallbacks{});
}

// -- a detached canvas closes its gesture -----------------------------------------------------------

void detach_mid_gesture() {
    Fixture fixture{};
    NodeCanvas& canvas = *fixture.canvas;
    canvas.select_only(fixture.a);
    const std::size_t steps = fixture.graph.undo_steps();
    fixture.press(Point{78.0, 100.0}, Modifier::none);
    fixture.glide(Point{78.0, 100.0}, Point{150.0, 150.0}, Modifier::none);
    fixture.window.reset();
    require(canvas.gesture() == NodeCanvasGesture::none, "detaching ends the gesture");
    require(fixture.bounds_of(fixture.a) == WorldRect{30.0, 50.0, 96.0, 64.0} && fixture.graph.undo_steps() == steps,
            "the gesture's group is rolled back");
    require(fixture.graph.end_undo_group().status == GraphStatus::no_group, "no group is left open");
}

// -- the theme's look -------------------------------------------------------------------------

// The canvas paints from the theme's canvas look: the grid spacing is the
// theme's (16 in the GUI.Forms theme) until the canvas sets its own, and a
// class names a grid variant the next resolution reads.
void look_follows_the_theme() {
    Fixture fixture{};
    NodeCanvas& canvas = *fixture.canvas;
    require(canvas.grid_spacing() == 16.0 && canvas.canvas_look().grid.spacing == 16.0,
            "the grid spacing is the theme's");
    require(canvas.set_grid_spacing(32.0) && canvas.grid_spacing() == 32.0 && canvas.canvas_look().grid.spacing == 32.0,
            "the canvas's own spacing replaces the theme's");
    require(!canvas.set_grid_spacing(0.0) && canvas.grid_spacing() == 32.0, "a spacing under one unit is refused");
    canvas.clear_grid_spacing();
    require(canvas.grid_spacing() == 16.0, "clearing it brings the theme's back");
    const Color paper = canvas.canvas_look().background.fill;
    canvas.classes = {"blue"};
    const Color blue = canvas.canvas_look().background.fill;
    require(blue != paper, "the blue class is the blue grid variant");
    canvas.classes = std::vector<std::string>{};
    require(canvas.canvas_look().background.fill == paper, "without the class the paper variant returns");
    fixture.paint();
    require(canvas.statistics().frames_painted >= 1U && canvas.statistics().ports_painted >= 4U,
            "the look paints every port");
}

// -- subclassing ------------------------------------------------------------------------------

// The Windows Forms pattern on NodeCanvas: a subclass overrides the
// raisers, sees each event's arguments, and calls the base, which raises
// the event to subscribers. `raised` counts the subscriber's calls, so the
// counts before and after the base call show the base raises.
class RaisingCanvas final : public NodeCanvas {
public:
    RaisingCanvas(StableId stable_id, const std::size_t& raised) : NodeCanvas(std::move(stable_id)), raised_(raised) {}
    std::size_t selections{};
    std::size_t views{};
    std::size_t edits{};
    std::size_t connections{};
    std::size_t raised_before_base{};
    std::size_t raised_after_base{};

protected:
    void on_selection_changed(const NodeCanvasSelection& arguments) override {
        ++selections;
        raised_before_base = raised_;
        NodeCanvas::on_selection_changed(arguments);
        raised_after_base = raised_;
    }
    void on_view_changed(const SceneViewTransform& arguments) override {
        ++views;
        NodeCanvas::on_view_changed(arguments);
    }
    void on_graph_edited(const NodeGraphEdited& arguments) override {
        ++edits;
        NodeCanvas::on_graph_edited(arguments);
    }
    void on_link_connected(const NodeLinkConnected& arguments) override {
        ++connections;
        NodeCanvas::on_link_connected(arguments);
    }

private:
    const std::size_t& raised_;
};

void subclass_overrides_raisers() {
    NodeGraph graph{};
    NodeSpec mix = spec_of("mix", "Mix", 200.0, 50.0);
    mix.ports.push_back(port_of("out", PortDirection::output, PortSide::right));
    NodeSpec out = spec_of("out", "Out", 200.0, 180.0);
    out.ports.push_back(port_of("in", PortDirection::input, PortSide::left));
    const NodeId b = graph.add_node(mix).node;
    const NodeId c = graph.add_node(out).node;
    std::size_t raised = 0U;
    const std::shared_ptr<RaisingCanvas> canvas = make_control<RaisingCanvas>(StableId("canvas"), raised);
    (*canvas).set_graph(&graph);
    Window window(canvas, Size{400.0, 300.0});
    window.set_active(true);
    window.perform_layout();
    const SubscriptionToken token = (*canvas).selection_changed().subscribe(CountSelections(raised));

    (*canvas).select_only(b);
    require((*canvas).selections == 1U && (*canvas).raised_before_base == 0U && (*canvas).raised_after_base == 1U &&
                raised == 1U,
            "the override runs first and the base raises selection_changed");
    require((*canvas).set_view(SceneViewTransform{WorldPoint{10.0, 0.0}, 1.0}) && (*canvas).views == 1U,
            "a view change runs on_view_changed");
    const GraphResult joined = (*canvas).connect_ports(PortRef{b, PortIndex{0U}}, PortRef{c, PortIndex{0U}});
    require(joined.status == GraphStatus::ok && (*canvas).connections == 1U && (*canvas).edits >= 1U,
            "a connection runs on_link_connected and on_graph_edited");
    static_cast<void>(token);
}

} // namespace

int main() {
    view_transform_and_events();
    level_of_detail();
    hit_testing();
    marquee();
    move_snap_and_suppressed_click();
    escape_rolls_back();
    link_drags_and_detach();
    cut_links_with_the_knife();
    lamps_keep_the_caches();
    keyboard();
    accessibility();
    edit_host();
    detach_mid_gesture();
    look_follows_the_theme();
    subclass_overrides_raisers();
    std::cout << "gui_forms_node_canvas_tests passed\n";
    return 0;
}
