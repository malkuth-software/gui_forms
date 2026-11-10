// Automatic link routing (roadmap 2.4; owner ruling of 2026-11-08):
// golden traces for a straight link, an L-shaped link and a pair of
// automatic ports choosing the edges that face each other; obstacle
// avoidance (no segment enters a node); parallel links sharing a channel
// nudged apart without adding a crossing; crossing avoidance; live
// re-routing after a move; the soft style's corners within their bound;
// fallback routes; determinism; and no storage growth when the same graph is
// routed again (a drag routes on every frame). Headless and renderer-free.

#include "gui_forms/controls/node_canvas/link_router/link_router.hpp"
#include "gui_forms/controls/node_canvas/node_graph/node_graph.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "link-router-tests: " << message << '\n';
    std::exit(1);
}

PortRecord port_of(PortDirection direction, PortSide side, double offset) {
    PortRecord port{};
    port.name = direction == PortDirection::input ? "in" : "out";
    port.direction = direction;
    port.side = side;
    port.offset = offset;
    return port;
}

NodeId add_box(NodeGraph& graph, double x, double y, std::vector<PortRecord> ports) {
    NodeSpec spec{};
    spec.title = "box";
    spec.bounds = WorldRect{x, y, 100.0, 100.0};
    spec.ports = std::move(ports);
    const GraphResult added = graph.add_node(spec);
    require(added.status == GraphStatus::ok, "a fixture node is added");
    return added.node;
}

LinkId link_of(NodeGraph& graph, NodeId from, std::uint16_t from_port, NodeId to, std::uint16_t to_port) {
    const GraphResult joined = graph.connect(PortRef{from, PortIndex{from_port}}, PortRef{to, PortIndex{to_port}});
    require(joined.status == GraphStatus::ok, "a fixture link is made");
    return joined.link;
}

LinkRouterOptions orthogonal_options() {
    LinkRouterOptions options{};
    options.style = LinkRouteStyle::orthogonal;
    return options;
}

std::string trace_of(const LinkRouter& router) {
    std::string trace;
    append_link_route_trace(trace, router);
    return trace;
}

void require_trace(const LinkRouter& router, const std::string& expected, const char* message) {
    const std::string trace = trace_of(router);
    if (trace != expected) std::cerr << trace;
    require(trace == expected, message);
}

// Whether any segment of any route passes through a node's open interior.
bool enters_a_node(const NodeGraph& graph, const LinkRouter& router) {
    for (const RoutedLink& route : router.routes()) {
        const std::span<const WorldPoint> points = router.points(route);
        for (std::size_t index = 0U; index + 1U < points.size(); ++index) {
            const WorldPoint a = points[index];
            const WorldPoint b = points[index + 1U];
            for (const NodeRecord& node : graph.node_slots()) {
                if (!node.live) continue;
                const WorldRect& box = node.bounds;
                const double low_x = std::min(a.x, b.x);
                const double high_x = std::max(a.x, b.x);
                const double low_y = std::min(a.y, b.y);
                const double high_y = std::max(a.y, b.y);
                const bool overlaps_x = high_x > box.x && low_x < box.right() && (high_x > low_x || (a.x > box.x && a.x < box.right()));
                const bool overlaps_y = high_y > box.y && low_y < box.bottom() && (high_y > low_y || (a.y > box.y && a.y < box.bottom()));
                if (overlaps_x && overlaps_y) return true;
            }
        }
    }
    return false;
}

// Whether every route is orthogonal and runs from its anchors.
bool well_formed(const LinkRouter& router) {
    for (const RoutedLink& route : router.routes()) {
        const std::span<const WorldPoint> points = router.points(route);
        if (points.size() < 2U) return false;
        if (!(points.front() == route.from_anchor) || !(points.back() == route.to_anchor)) return false;
        if (route.fallback) continue;
        for (std::size_t index = 0U; index + 1U < points.size(); ++index) {
            const bool axis = points[index].x == points[index + 1U].x || points[index].y == points[index + 1U].y;
            if (!axis) return false;
        }
    }
    return true;
}

// Whether two different routes have collinear overlapping segments (what
// nudging removes).
bool shares_a_track(const LinkRouter& router) {
    const std::span<const RoutedLink> routes = router.routes();
    for (std::size_t first = 0U; first < routes.size(); ++first) {
        for (std::size_t second = first + 1U; second < routes.size(); ++second) {
            const std::span<const WorldPoint> one = router.points(routes[first]);
            const std::span<const WorldPoint> other = router.points(routes[second]);
            for (std::size_t a = 1U; a + 2U < one.size(); ++a) {
                for (std::size_t b = 1U; b + 2U < other.size(); ++b) {
                    const WorldPoint a0 = one[a];
                    const WorldPoint a1 = one[a + 1U];
                    const WorldPoint b0 = other[b];
                    const WorldPoint b1 = other[b + 1U];
                    if (a0.y == a1.y && b0.y == b1.y && a0.y == b0.y) {
                        const double low = std::max(std::min(a0.x, a1.x), std::min(b0.x, b1.x));
                        const double high = std::min(std::max(a0.x, a1.x), std::max(b0.x, b1.x));
                        if (high > low) return true;
                    }
                    if (a0.x == a1.x && b0.x == b1.x && a0.x == b0.x) {
                        const double low = std::max(std::min(a0.y, a1.y), std::min(b0.y, b1.y));
                        const double high = std::min(std::max(a0.y, a1.y), std::max(b0.y, b1.y));
                        if (high > low) return true;
                    }
                }
            }
        }
    }
    return false;
}

// -- golden routes --------------------------------------------------------------------------

void straight_and_l_shaped() {
    NodeGraph graph;
    const NodeId a = add_box(graph, 0.0, 0.0, {port_of(PortDirection::output, PortSide::right, 0.5)});
    const NodeId b = add_box(graph, 200.0, 0.0, {port_of(PortDirection::input, PortSide::left, 0.5)});
    static_cast<void>(link_of(graph, a, 0U, b, 0U));
    LinkRouter router;
    const LinkRouteStatistics straight = router.route(graph, orthogonal_options());
    require(straight.routed == 1U && straight.fallbacks == 0U, "one link is routed");
    require_trace(router, "link 0.1 right>left bends=0: 100,50 200,50\n",
                  "facing ports at one height give a straight link");
    require(router.routed_revision() == graph.revision(), "the router records the revision it routed");

    // The input moved down and right: out of the output's edge, down the
    // input's margin line, into the input (two turns; the arrival turn is
    // as dear as the other way round, and is found first).
    const std::array<NodeId, 1> moved{b};
    require(graph.move_nodes(moved, WorldPoint{0.0, 150.0}).status == GraphStatus::ok, "the input node moves");
    static_cast<void>(router.route(graph, orthogonal_options()));
    require_trace(router, "link 0.1 right>left bends=2: 100,50 188,50 188,200 200,200\n",
                  "an offset input gives an L-shaped route along the margin lines");
    require(well_formed(router) && !enters_a_node(graph, router), "the route is orthogonal and enters no node");
}

void automatic_sides_face_each_other() {
    NodeGraph graph;
    const NodeId a = add_box(graph, 0.0, 0.0, {port_of(PortDirection::output, PortSide::automatic, 0.5)});
    const NodeId b = add_box(graph, 0.0, 200.0, {port_of(PortDirection::input, PortSide::automatic, 0.5)});
    static_cast<void>(link_of(graph, a, 0U, b, 0U));
    LinkRouter router;
    static_cast<void>(router.route(graph, orthogonal_options()));
    require_trace(router, "link 0.1 bottom>top bends=0: 50,100 50,200\n",
                  "automatic ports take the edges facing each other (port side chosen by direction)");

    // Moved beside it, the same ports turn to face sideways.
    const std::array<NodeId, 1> moved{b};
    require(graph.move_nodes(moved, WorldPoint{300.0, -200.0}).status == GraphStatus::ok, "the input moves beside");
    static_cast<void>(router.route(graph, orthogonal_options()));
    const RoutedLink& route = router.routes()[0];
    require(route.from_side == PortSide::right && route.to_side == PortSide::left && route.bends == 0U,
            "side by side, the automatic ports face sideways");
}

void automatic_ports_spread_in_order() {
    NodeGraph graph;
    // One node with two automatic outputs; two targets to its right, the
    // upper target linked from the second port. Spreading orders the ports
    // by their far ends, so the links do not cross.
    const NodeId a = add_box(graph, 0.0, 0.0,
                             {port_of(PortDirection::output, PortSide::automatic, 0.5),
                              port_of(PortDirection::output, PortSide::automatic, 0.5)});
    const NodeId upper = add_box(graph, 300.0, -150.0, {port_of(PortDirection::input, PortSide::left, 0.5)});
    const NodeId lower = add_box(graph, 300.0, 150.0, {port_of(PortDirection::input, PortSide::left, 0.5)});
    static_cast<void>(link_of(graph, a, 0U, lower, 0U));
    static_cast<void>(link_of(graph, a, 1U, upper, 0U));
    LinkRouter router;
    const LinkRouteStatistics statistics = router.route(graph, orthogonal_options());
    const RoutedLink& to_lower = router.routes()[0];
    const RoutedLink& to_upper = router.routes()[1];
    if (to_lower.from_side != PortSide::right || to_upper.from_side != PortSide::right) {
        std::cerr << "automatic outputs leave by " << port_side_name(to_lower.from_side) << " and "
                  << port_side_name(to_upper.from_side) << " with " << to_lower.bends << " and " << to_upper.bends
                  << " bends\n";
    }
    require(to_lower.from_side == PortSide::right && to_upper.from_side == PortSide::right,
            "both automatic outputs face the targets");
    // The spread offsets are thirds of the edge: (rank + 1) / 3 times its
    // height, within rounding of 100 / 3 and 200 / 3.
    constexpr double rounding = 1e-9;
    const bool upper_third = std::fabs(to_upper.from_anchor.y - 100.0 / 3.0) <= rounding;
    const bool lower_third = std::fabs(to_lower.from_anchor.y - 200.0 / 3.0) <= rounding;
    if (!upper_third || !lower_third) {
        std::cerr << "automatic outputs anchored at y " << to_upper.from_anchor.y << " and " << to_lower.from_anchor.y
                  << ", fallbacks " << statistics.fallbacks << "\n";
    }
    require(to_upper.from_anchor.y < to_lower.from_anchor.y && upper_third && lower_third,
            "the ports are spread along the edge in the order of their far ends");
    require(statistics.crossings == 0U && well_formed(router) && !enters_a_node(graph, router),
            "the spread links neither cross nor enter a node");
}

// -- obstacles, channels and crossings ---------------------------------------------------------

void around_an_obstacle_with_nudging() {
    NodeGraph graph;
    const NodeId a = add_box(graph, 0.0, 0.0,
                             {port_of(PortDirection::output, PortSide::right, 0.25),
                              port_of(PortDirection::output, PortSide::right, 0.75)});
    static_cast<void>(add_box(graph, 200.0, 0.0, {}));
    const NodeId c = add_box(graph, 400.0, 0.0,
                             {port_of(PortDirection::input, PortSide::left, 0.25),
                              port_of(PortDirection::input, PortSide::left, 0.75)});
    static_cast<void>(link_of(graph, a, 0U, c, 0U));
    static_cast<void>(link_of(graph, a, 1U, c, 1U));
    LinkRouter router;
    const LinkRouteStatistics statistics = router.route(graph, orthogonal_options());
    require(statistics.fallbacks == 0U && well_formed(router), "both links are routed orthogonally");
    require(!enters_a_node(graph, router), "the links go round the node between (obstacle avoidance)");
    for (const RoutedLink& route : router.routes()) {
        require(route.bends >= 4U, "going round takes at least four turns");
    }
    require(!shares_a_track(router), "links sharing a channel are nudged apart");
    require(statistics.crossings == 0U, "the nudge adds no crossing");
    const std::string first = trace_of(router);
    const LinkRouteStatistics again = router.route(graph, orthogonal_options());
    require(trace_of(router) == first, "routing the same graph again gives the same routes (determinism)");
    require(again.storage_growths == 0U, "routing the same graph again allocates nothing");
}

void crossings_are_avoided() {
    NodeGraph graph;
    // Two sources stacked on the left and two targets stacked on the right,
    // linked straight across; a third link from the lower source to a target
    // above everything would cross the upper link if routed through, and the
    // crossing penalty sends it round instead.
    const NodeId upper = add_box(graph, 0.0, 0.0, {port_of(PortDirection::output, PortSide::right, 0.5)});
    const NodeId lower = add_box(graph, 0.0, 200.0,
                                 {port_of(PortDirection::output, PortSide::right, 0.5),
                                  port_of(PortDirection::output, PortSide::top, 0.5)});
    const NodeId target_upper = add_box(graph, 400.0, 0.0, {port_of(PortDirection::input, PortSide::left, 0.5)});
    const NodeId target_lower = add_box(graph, 400.0, 200.0, {port_of(PortDirection::input, PortSide::left, 0.5)});
    const NodeId above = add_box(graph, 200.0, -300.0, {port_of(PortDirection::input, PortSide::bottom, 0.5)});
    static_cast<void>(link_of(graph, upper, 0U, target_upper, 0U));
    static_cast<void>(link_of(graph, lower, 0U, target_lower, 0U));
    static_cast<void>(link_of(graph, lower, 1U, above, 0U));
    LinkRouter router;
    LinkRouterOptions options = orthogonal_options();
    // Going round the upper source costs about two margins of length and two
    // turns more than crossing (some 200 units), above the default penalty
    // of 80, so the default crosses; a penalty above the detour avoids it.
    options.crossing_penalty = 400.0;
    const LinkRouteStatistics avoided = router.route(graph, options);
    require(well_formed(router) && !enters_a_node(graph, router), "the three links are well formed");
    options.crossing_penalty = 0.0;
    const LinkRouteStatistics careless = router.route(graph, options);
    require(avoided.crossings <= careless.crossings, "the crossing penalty never adds crossings");
    require(avoided.crossings == 0U, "with the penalty the links do not cross");
}

void live_rerouting_after_moves() {
    NodeGraph graph;
    const NodeId a = add_box(graph, 0.0, 0.0, {port_of(PortDirection::output, PortSide::right, 0.5)});
    const NodeId b = add_box(graph, 400.0, 0.0, {port_of(PortDirection::input, PortSide::left, 0.5)});
    const NodeId blocker = add_box(graph, 200.0, 300.0, {});
    static_cast<void>(link_of(graph, a, 0U, b, 0U));
    LinkRouter router;
    router.reserve(3U, 1U);
    static_cast<void>(router.route(graph, orthogonal_options()));
    require(router.routes()[0].bends == 0U, "with the blocker away the link is straight");
    // The drag: the blocker moves into the link's way, frame by frame.
    const std::array<NodeId, 1> dragged{blocker};
    for (int frame = 0; frame < 6; ++frame) {
        require(graph.move_nodes(dragged, WorldPoint{0.0, -50.0}).status == GraphStatus::ok, "a drag frame moves");
        const LinkRouteStatistics statistics = router.route(graph, orthogonal_options());
        require(statistics.fallbacks == 0U && !enters_a_node(graph, router) && well_formed(router),
                "every frame's route avoids the dragged node");
    }
    require(router.routes()[0].bends >= 4U, "with the blocker in the way the link goes round it");
    require(graph.move_nodes(dragged, WorldPoint{0.0, 300.0}).status == GraphStatus::ok, "the blocker leaves");
    static_cast<void>(router.route(graph, orthogonal_options()));
    require(router.routes()[0].bends == 0U, "the link straightens again");
}

// -- the soft style, fallbacks ------------------------------------------------------------------

double distance_to_polyline(WorldPoint point, std::span<const WorldPoint> polyline) {
    double best = std::numeric_limits<double>::infinity();
    for (std::size_t index = 0U; index + 1U < polyline.size(); ++index) {
        const WorldPoint a = polyline[index];
        const WorldPoint b = polyline[index + 1U];
        const double dx = b.x - a.x;
        const double dy = b.y - a.y;
        const double length = dx * dx + dy * dy;
        double t = 0.0;
        if (length > 0.0) t = std::clamp(((point.x - a.x) * dx + (point.y - a.y) * dy) / length, 0.0, 1.0);
        const double x = a.x + dx * t - point.x;
        const double y = a.y + dy * t - point.y;
        best = std::min(best, std::sqrt(x * x + y * y));
    }
    return best;
}

void soft_corners() {
    NodeGraph graph;
    const NodeId a = add_box(graph, 0.0, 0.0, {port_of(PortDirection::output, PortSide::right, 0.5)});
    const NodeId b = add_box(graph, 200.0, 150.0, {port_of(PortDirection::input, PortSide::left, 0.5)});
    static_cast<void>(link_of(graph, a, 0U, b, 0U));
    LinkRouter router;
    LinkRouterOptions options{};
    options.style = LinkRouteStyle::soft;
    options.corner_radius = 10.0;
    static_cast<void>(router.route(graph, options));
    const RoutedLink& route = router.routes()[0];
    const std::span<const WorldPoint> corners = router.points(route);
    const std::span<const WorldPoint> drawn = router.drawn(route);
    require(drawn.size() > corners.size(), "the soft style flattens each corner into several points");
    require(drawn.front() == route.from_anchor && drawn.back() == route.to_anchor, "the curve keeps its anchors");
    // A corner's quadratic, with its control point at the corner, never
    // strays more than a quarter of the radius from the two legs.
    double worst = 0.0;
    for (const WorldPoint point : drawn) worst = std::max(worst, distance_to_polyline(point, corners));
    require(worst <= options.corner_radius * 0.25 + 1e-9, "every drawn point lies within a quarter radius of the route");
}

void fallback_inside_another_margin() {
    NodeGraph graph;
    const NodeId a = add_box(graph, 0.0, 0.0, {port_of(PortDirection::output, PortSide::right, 0.5)});
    // Five units to the right: a's stub ends inside this node's margin.
    static_cast<void>(add_box(graph, 105.0, 0.0, {}));
    const NodeId b = add_box(graph, 400.0, 0.0, {port_of(PortDirection::input, PortSide::left, 0.5)});
    static_cast<void>(link_of(graph, a, 0U, b, 0U));
    LinkRouter router;
    const LinkRouteStatistics statistics = router.route(graph, orthogonal_options());
    require(statistics.fallbacks == 1U && router.routes()[0].fallback, "a port boxed in by a neighbour falls back");
    require(router.points(router.routes()[0]).size() == 4U, "a fallback runs anchor, stub, stub, anchor");

    NodeGraph empty;
    const LinkRouteStatistics nothing = router.route(empty, orthogonal_options());
    require(nothing.routed == 0U && router.routes().empty(), "an empty graph routes nothing");
}

} // namespace

int main() {
    straight_and_l_shaped();
    automatic_sides_face_each_other();
    automatic_ports_spread_in_order();
    around_an_obstacle_with_nudging();
    crossings_are_avoided();
    live_rerouting_after_moves();
    soft_corners();
    fallback_inside_another_margin();
    std::cout << "link-router-tests: golden routes, automatic sides and spreading, obstacles, nudging, crossings, "
                 "live re-routing, soft corners, fallbacks and determinism passed\n";
    return 0;
}
