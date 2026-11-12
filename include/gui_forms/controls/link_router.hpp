#pragma once
// Automatic edge routing for node graphs (roadmap 2.4; owner ruling of
// 2026-11-08 as relayed by the coordinator: obstacle-avoiding connector
// routing, orthogonal or soft-curved, re-routed live while dragging, parallel
// links nudged apart, crossings minimised, port side chosen by direction;
// libavoid-style in behaviour, our own implementation). Renderer-free and
// headless: a NodeCanvas draws what it computes.
//
// The method, per route() call:
//
//  1. Obstacles are the live nodes' bounds grown by `obstacle_margin` on
//     every side. Each link end leaves its port along the port edge's outward
//     normal for exactly the margin (the port's stub), so it starts and ends
//     on an obstacle's boundary.
//  2. The routing grid is the sparse orthogonal grid of every obstacle edge,
//     every stub end and a frame round them all. A grid edge is blocked when
//     it lies inside an obstacle; edges along a boundary are free, so routes
//     keep the margin from every node.
//  3. Links are routed one at a time, shortest first (ties by slot), by A*
//     over (grid vertex, heading) states. A step costs its length, a turn
//     `bend_penalty`, and entering a vertex that an earlier route crosses
//     perpendicularly `crossing_penalty`, so later links avoid crossing
//     earlier ones where a detour is cheaper. An automatic port's edge is
//     chosen by direction (the owner's ruling): the edge of its node facing
//     the centre of the link's other node, along the dominant axis.
//  4. An automatic port takes the edge its first link chose; a node's
//     automatic ports on one edge are spread along it in the order of their
//     links' far ends (which keeps their links from crossing), and every
//     link is routed again with those anchors.
//  5. Nudging: interior segments of different links that lie on one grid
//     line and overlap are moved apart by up to `nudge_spacing` (less when
//     the margin is narrower). Their order across the channel comes from
//     where each leaves it: a segment that ends before the others and turns
//     to one side takes that side, so its turn crosses none of the segments
//     that go on; segments that run the whole channel nest by the length of
//     their turns (longer turns inside). Stub segments stay on their ports.
//  6. The drawn path is the orthogonal polyline, or, in the soft style, the
//     polyline with each corner replaced by a quadratic curve of up to
//     `corner_radius`, flattened so every drawn point lies within
//     `flatten_tolerance` of the curve.
//
// Determinism: the same graph and options give the same routes on every
// host (fixed order, ties broken by insertion sequence, no hashing).
//
// Memory: all storage is the router's and is reused; once a graph of a
// given size has been routed, routing it again (as a drag does on every
// frame) allocates nothing unless the grid grows (LinkRouteStatistics
// storage_growths counts growths). The grid is capped at
// maximum_route_grid_vertices; a larger graph gets straight fallback routes.
//
// Threads: a router is used on one thread, with a graph not mutated during
// route().

#include "gui_forms/controls/node_graph.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

enum class LinkRouteStyle : std::uint8_t { orthogonal, soft };

struct LinkRouterOptions final {
    LinkRouteStyle style{LinkRouteStyle::soft};
    // The clearance kept round every node, and each port stub's length.
    double obstacle_margin{12.0};
    // What a turn costs, in world units of length.
    double bend_penalty{40.0};
    // What crossing an earlier route costs, in world units of length.
    double crossing_penalty{80.0};
    // The distance between parallel links sharing a channel.
    double nudge_spacing{6.0};
    // The soft style's largest corner radius.
    double corner_radius{10.0};
    // The soft style's largest distance between a drawn point and its curve.
    double flatten_tolerance{0.25};
};

inline constexpr std::size_t maximum_route_grid_vertices = 1048576U;

// One routed link. `points` are its orthogonal polyline from the output's
// anchor to the input's, after nudging, as a range of LinkRouter::points;
// `drawn` is what is painted and hit-tested (the same polyline, or the soft
// style's flattened curve), as a range of LinkRouter::drawn. A fallback
// route found no orthogonal path (a port inside another node's margin, or
// the grid cap) and is the straight line between the stub ends.
struct RoutedLink final {
    LinkId link{};
    PortSide from_side{PortSide::right};
    PortSide to_side{PortSide::left};
    WorldPoint from_anchor{};
    WorldPoint to_anchor{};
    std::uint32_t first_point{};
    std::uint32_t point_count{};
    std::uint32_t first_drawn{};
    std::uint32_t drawn_count{};
    std::uint32_t bends{};
    bool fallback{};
};

struct LinkRouteStatistics final {
    std::size_t routed{};
    std::size_t fallbacks{};
    std::size_t grid_columns{};
    std::size_t grid_rows{};
    // A* states expanded over all searches.
    std::size_t expanded{};
    // Perpendicular crossings between different links after nudging.
    std::size_t crossings{};
    // Segments moved by nudging.
    std::size_t nudged{};
    // Times any of the router's storage grew during this call.
    std::size_t storage_growths{};
};

class LinkRouter final {
public:
    // Reserves for a graph of `nodes` nodes and `links` links (the grid's
    // storage grows on first use as needed).
    void reserve(std::size_t nodes, std::size_t links);

    // Routes every live link of `graph`, replacing the previous routes.
    // Options outside their ranges (non-finite, negative, a zero tolerance)
    // are taken at their defaults.
    LinkRouteStatistics route(const NodeGraph& graph, const LinkRouterOptions& options);

    // The routes in link slot order; borrowed until the next route().
    [[nodiscard]] std::span<const RoutedLink> routes() const& noexcept { return routes_; }
    std::span<const RoutedLink> routes() const&& = delete;
    [[nodiscard]] std::span<const WorldPoint> points(const RoutedLink& route) const noexcept;
    [[nodiscard]] std::span<const WorldPoint> drawn(const RoutedLink& route) const noexcept;
    // The route of a link, or null.
    [[nodiscard]] const RoutedLink* find(LinkId link) const noexcept;
    // The revision of the graph the routes were made from (zero before any).
    [[nodiscard]] std::uint64_t routed_revision() const noexcept { return routed_revision_; }

private:
    // A node's bounds grown by the margin, by edge (computed exactly as the
    // stub ends are, so a stub end lies on its grid line bit for bit).
    struct Box final {
        double left{};
        double top{};
        double right{};
        double bottom{};
    };
    // A port edge the search may use: the anchor, the stub end, the stub
    // end's grid vertex and whether it lies on the grid.
    struct Candidate final {
        std::uint8_t side{};
        WorldPoint anchor{};
        WorldPoint stub{};
        std::uint32_t vertex{};
        bool valid{};
    };
    // Sorts the routing order by length, then slot.
    struct ShorterFirst final {
        const std::vector<double>* keys{};
        bool operator()(std::uint32_t left, std::uint32_t right) const noexcept;
    };
    // Sorts a node's automatic ports on one edge by their far ends.
    struct PortKey final {
        double far{};
        std::uint16_t port{};
    };
    struct PortKeyOrder final {
        bool operator()(const PortKey& left, const PortKey& right) const noexcept;
    };
    struct RouteSlotOrder final {
        bool operator()(const RoutedLink& left, const RoutedLink& right) const noexcept;
    };
    // One end of a link as the search sees it.
    struct RouteEnd final {
        std::uint32_t node{};
        std::uint16_t port{};
        std::uint8_t side_count{};
        std::array<std::uint8_t, 4> sides{};
        double offset{0.5};
        bool automatic{};
    };
    // One A* heap entry; ties go to the earlier pushed.
    struct HeapEntry final {
        double estimate{};
        std::uint32_t sequence{};
        std::uint32_t state{};
    };
    struct LaterEntry final {
        bool operator()(const HeapEntry& left, const HeapEntry& right) const noexcept;
    };
    // An interior segment for nudging: its line, its extent along the line,
    // and at each end the side its neighbouring segment leaves to (-1 or
    // +1, towards the smaller or larger coordinate across the line) and that
    // neighbour's length. `key` and `tie` order a shared channel's tracks.
    struct Segment final {
        bool horizontal{};
        double coordinate{};
        double low{};
        double high{};
        int side_low{};
        int side_high{};
        double leg_low{};
        double leg_high{};
        double key{};
        double tie{};
        std::uint32_t route{};
        std::uint32_t first_point{};
    };
    struct SegmentOrder final {
        bool operator()(const Segment& left, const Segment& right) const noexcept;
    };
    struct TrackOrder final {
        bool operator()(const Segment& left, const Segment& right) const noexcept;
    };

    void collect_obstacles(const NodeGraph& graph, const LinkRouterOptions& options);
    void prepare_ports(const NodeGraph& graph);
    // `other_end` is the link's other end: an unresolved automatic port takes the
    // edge facing its node.
    [[nodiscard]] RouteEnd make_end(const NodeGraph& graph, PortRef port, PortRef other_end) const noexcept;
    void fill_candidates(const NodeGraph& graph, const RouteEnd& end, double margin,
                         std::array<Candidate, 4>& candidates) const noexcept;
    [[nodiscard]] bool build_grid(const NodeGraph& graph, const LinkRouterOptions& options);
    void route_pass(const NodeGraph& graph, const LinkRouterOptions& options, bool grid_ready,
                    LinkRouteStatistics& statistics);
    [[nodiscard]] bool search(const NodeGraph& graph, const LinkRouterOptions& options, const RouteEnd& from,
                              const RouteEnd& to, RoutedLink& route, LinkRouteStatistics& statistics);
    void route_fallback(const NodeGraph& graph, const LinkRouterOptions& options, const RouteEnd& from,
                        const RouteEnd& to, RoutedLink& route);
    void compress(RoutedLink& route) noexcept;
    [[nodiscard]] bool spread_automatic_ports(const NodeGraph& graph);
    void nudge(const LinkRouterOptions& options, LinkRouteStatistics& statistics);
    void flatten(const LinkRouterOptions& options);
    [[nodiscard]] std::size_t count_crossings() const noexcept;
    [[nodiscard]] std::size_t grid_index(const std::vector<double>& lines, double value) const noexcept;
    [[nodiscard]] std::size_t capacity_sum() const noexcept;

    // Per node slot: its grown bounds (meaningful for live nodes).
    std::vector<Box> obstacles_{};
    // Per node slot, the index of its first port in the per-port arrays.
    std::vector<std::uint32_t> port_base_{};
    // Per port: the edge an automatic port resolved to (4 = unresolved), its
    // spread offset, and the far end of its first link along its edge.
    std::vector<std::uint8_t> resolved_sides_{};
    std::vector<double> resolved_offsets_{};
    std::vector<double> port_far_{};
    std::vector<std::uint8_t> port_far_set_{};
    std::vector<PortKey> port_keys_{};
    // The routing order (link slot indices) and its keys (per link slot).
    std::vector<std::uint32_t> order_{};
    std::vector<double> order_keys_{};
    // The grid.
    std::vector<double> columns_{};
    std::vector<double> rows_{};
    std::vector<std::uint8_t> horizontal_blocked_{};
    std::vector<std::uint8_t> vertical_blocked_{};
    std::vector<std::uint8_t> horizontal_used_{};
    std::vector<std::uint8_t> vertical_used_{};
    // A* storage, one entry per (vertex, heading) state.
    std::vector<double> cost_{};
    std::vector<std::uint32_t> parent_{};
    std::vector<std::uint32_t> seen_{};
    std::vector<std::uint32_t> closed_{};
    std::vector<HeapEntry> heap_{};
    std::vector<std::uint32_t> path_{};
    std::uint32_t search_stamp_{};
    // Output.
    std::vector<RoutedLink> routes_{};
    std::vector<WorldPoint> points_{};
    std::vector<WorldPoint> drawn_{};
    std::vector<Segment> segments_{};
    std::uint64_t routed_revision_{};
};

// Appends one line per route to `out`: the link's slot and generation, its
// resolved edges, bend count, a fallback mark and its orthogonal points with
// coordinates rounded to 1/64 (the drag trace's rounding), for golden traces.
void append_link_route_trace(std::string& out, const LinkRouter& router);

[[nodiscard]] std::string_view port_side_name(PortSide side) noexcept;

} // namespace gui_forms
