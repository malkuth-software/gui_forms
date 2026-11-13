#include "gui_forms/controls/link_router.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace gui_forms {
namespace {

// Headings of a search state: east (+x), west (-x), south (+y), north (-y).
// A heading's opposite is the heading with its low bit flipped.
constexpr std::uint8_t east = 0U;
constexpr std::uint8_t west = 1U;
constexpr std::uint8_t south = 2U;
constexpr std::uint8_t north = 3U;
constexpr std::uint8_t heading_count = 4U;
constexpr std::uint32_t no_state = std::numeric_limits<std::uint32_t>::max();
constexpr std::uint8_t unresolved_side = 4U;
// The most points one soft corner is flattened into.
constexpr std::uint32_t maximum_corner_steps = 64U;

std::uint8_t opposite(std::uint8_t heading) noexcept {
    const std::uint8_t flipped = static_cast<std::uint8_t>(heading ^ 1U);
    return flipped;
}

bool horizontal_heading(std::uint8_t heading) noexcept {
    const bool horizontal = heading < south;
    return horizontal;
}

// The heading that leaves a node through `side` (its outward normal).
std::uint8_t outward(std::uint8_t side) noexcept {
    switch (static_cast<PortSide>(side)) {
    case PortSide::left: return west;
    case PortSide::right: return east;
    case PortSide::top: return north;
    case PortSide::bottom: return south;
    case PortSide::automatic:
    default:
        return east;
    }
}

// The stub end of a port at `anchor` on `side`: the anchor moved out by the
// margin. Written as the obstacle edges are (bounds edge plus or minus the
// margin), so the stub end lies on its grid line exactly.
WorldPoint stub_end(const NodeRecord& node, std::uint8_t side, WorldPoint anchor, double margin) noexcept {
    const WorldRect& bounds = node.bounds;
    WorldPoint stub = anchor;
    switch (static_cast<PortSide>(side)) {
    case PortSide::left: stub.x = bounds.x - margin; break;
    case PortSide::right: stub.x = bounds.right() + margin; break;
    case PortSide::top: stub.y = bounds.y - margin; break;
    case PortSide::bottom: stub.y = bounds.bottom() + margin; break;
    case PortSide::automatic:
    default:
        break;
    }
    return stub;
}

WorldPoint centre_of(const WorldRect& bounds) noexcept {
    const WorldPoint centre{bounds.x + bounds.width * 0.5, bounds.y + bounds.height * 0.5};
    return centre;
}

// The edge of `node` facing `other`'s centre: an unresolved automatic
// port's edge, and a fallback route's.
std::uint8_t facing_side(const NodeRecord& node, const NodeRecord& other) noexcept {
    const WorldPoint from = centre_of(node.bounds);
    const WorldPoint to = centre_of(other.bounds);
    const double dx = to.x - from.x;
    const double dy = to.y - from.y;
    if (std::fabs(dx) >= std::fabs(dy)) {
        const PortSide side = dx >= 0.0 ? PortSide::right : PortSide::left;
        return static_cast<std::uint8_t>(side);
    }
    const PortSide side = dy >= 0.0 ? PortSide::bottom : PortSide::top;
    return static_cast<std::uint8_t>(side);
}

// Whether three points lie on one horizontal or vertical line.
bool collinear(WorldPoint a, WorldPoint b, WorldPoint c) noexcept {
    const bool vertical = a.x == b.x && b.x == c.x;
    const bool horizontal = a.y == b.y && b.y == c.y;
    return vertical || horizontal;
}

int sign_of(double value) noexcept {
    if (value > 0.0) return 1;
    if (value < 0.0) return -1;
    return 0;
}

double rounded_64(double value) noexcept {
    const double rounded = std::round(value * 64.0) / 64.0;
    return rounded;
}

// Options outside their ranges are taken at their defaults.
LinkRouterOptions sanitized(const LinkRouterOptions& options) noexcept {
    const LinkRouterOptions defaults{};
    LinkRouterOptions result = options;
    if (!std::isfinite(result.obstacle_margin) || result.obstacle_margin < 0.0) {
        result.obstacle_margin = defaults.obstacle_margin;
    }
    if (!std::isfinite(result.bend_penalty) || result.bend_penalty < 0.0) result.bend_penalty = defaults.bend_penalty;
    if (!std::isfinite(result.crossing_penalty) || result.crossing_penalty < 0.0) {
        result.crossing_penalty = defaults.crossing_penalty;
    }
    if (!std::isfinite(result.nudge_spacing) || result.nudge_spacing < 0.0) {
        result.nudge_spacing = defaults.nudge_spacing;
    }
    if (!std::isfinite(result.corner_radius) || result.corner_radius < 0.0) {
        result.corner_radius = defaults.corner_radius;
    }
    if (!std::isfinite(result.flatten_tolerance) || result.flatten_tolerance <= 0.0) {
        result.flatten_tolerance = defaults.flatten_tolerance;
    }
    if (result.style != LinkRouteStyle::orthogonal && result.style != LinkRouteStyle::soft) {
        result.style = defaults.style;
    }
    return result;
}

} // namespace

std::string_view port_side_name(PortSide side) noexcept {
    switch (side) {
    case PortSide::left: return "left";
    case PortSide::right: return "right";
    case PortSide::top: return "top";
    case PortSide::bottom: return "bottom";
    case PortSide::automatic: return "automatic";
    }
    return "none";
}

// -- ordering predicates -------------------------------------------------------------

bool LinkRouter::LaterEntry::operator()(const HeapEntry& left, const HeapEntry& right) const noexcept {
    if (left.estimate != right.estimate) return left.estimate > right.estimate;
    return left.sequence > right.sequence;
}

bool LinkRouter::ShorterFirst::operator()(std::uint32_t left, std::uint32_t right) const noexcept {
    const double left_key = (*keys)[left];
    const double right_key = (*keys)[right];
    if (left_key != right_key) return left_key < right_key;
    return left < right;
}

bool LinkRouter::PortKeyOrder::operator()(const PortKey& left, const PortKey& right) const noexcept {
    if (left.far != right.far) return left.far < right.far;
    return left.port < right.port;
}

bool LinkRouter::RouteSlotOrder::operator()(const RoutedLink& left, const RoutedLink& right) const noexcept {
    return left.link.index < right.link.index;
}

bool LinkRouter::SegmentOrder::operator()(const Segment& left, const Segment& right) const noexcept {
    if (left.horizontal != right.horizontal) return left.horizontal;
    if (left.coordinate != right.coordinate) return left.coordinate < right.coordinate;
    if (left.low != right.low) return left.low < right.low;
    if (left.route != right.route) return left.route < right.route;
    return left.first_point < right.first_point;
}

// Within a shared channel, smaller keys take the tracks towards the smaller
// coordinate (see nudge for the keys).
bool LinkRouter::TrackOrder::operator()(const Segment& left, const Segment& right) const noexcept {
    if (left.key != right.key) return left.key < right.key;
    if (left.tie != right.tie) return left.tie < right.tie;
    if (left.route != right.route) return left.route < right.route;
    return left.first_point < right.first_point;
}

// -- public ----------------------------------------------------------------------------

void LinkRouter::reserve(std::size_t nodes, std::size_t links) {
    obstacles_.reserve(nodes);
    port_base_.reserve(nodes);
    order_.reserve(links);
    order_keys_.reserve(links);
    routes_.reserve(links);
    points_.reserve(links * 8U);
    drawn_.reserve(links * 32U);
    segments_.reserve(links * 4U);
    columns_.reserve(nodes * 2U + links * 2U + 2U);
    rows_.reserve(nodes * 2U + links * 2U + 2U);
}

std::span<const WorldPoint> LinkRouter::points(const RoutedLink& route) const noexcept {
    if (static_cast<std::size_t>(route.first_point) + route.point_count > points_.size()) return {};
    const std::span<const WorldPoint> range(points_.data() + route.first_point, route.point_count);
    return range;
}

std::span<const WorldPoint> LinkRouter::drawn(const RoutedLink& route) const noexcept {
    if (static_cast<std::size_t>(route.first_drawn) + route.drawn_count > drawn_.size()) return {};
    const std::span<const WorldPoint> range(drawn_.data() + route.first_drawn, route.drawn_count);
    return range;
}

const RoutedLink* LinkRouter::find(LinkId link) const noexcept {
    for (const RoutedLink& route : routes_) {
        if (route.link == link) return &route;
    }
    return nullptr;
}

std::size_t LinkRouter::capacity_sum() const noexcept {
    const std::size_t sum = obstacles_.capacity() + port_base_.capacity() + resolved_sides_.capacity() +
                            resolved_offsets_.capacity() + port_far_.capacity() + port_far_set_.capacity() +
                            port_keys_.capacity() + order_.capacity() + order_keys_.capacity() +
                            columns_.capacity() + rows_.capacity() + horizontal_blocked_.capacity() +
                            vertical_blocked_.capacity() + horizontal_used_.capacity() +
                            vertical_used_.capacity() + cost_.capacity() + parent_.capacity() + seen_.capacity() +
                            closed_.capacity() + heap_.capacity() + path_.capacity() + routes_.capacity() +
                            points_.capacity() + drawn_.capacity() + segments_.capacity();
    return sum;
}

LinkRouteStatistics LinkRouter::route(const NodeGraph& graph, const LinkRouterOptions& options) {
    LinkRouteStatistics statistics{};
    const std::size_t capacity_before = capacity_sum();
    const LinkRouterOptions chosen = sanitized(options);
    collect_obstacles(graph, chosen);
    prepare_ports(graph);
    bool grid_ready = build_grid(graph, chosen);
    route_pass(graph, chosen, grid_ready, statistics);
    // Automatic ports take the edge their first link chose and are spread
    // along it; the links are then routed again from those anchors.
    if (spread_automatic_ports(graph)) {
        grid_ready = build_grid(graph, chosen);
        route_pass(graph, chosen, grid_ready, statistics);
    }
    nudge(chosen, statistics);
    flatten(chosen);
    statistics.crossings = count_crossings();
    statistics.grid_columns = columns_.size();
    statistics.grid_rows = rows_.size();
    if (capacity_sum() != capacity_before) statistics.storage_growths = 1U;
    routed_revision_ = graph.revision();
    return statistics;
}

// -- preparation -------------------------------------------------------------------------

void LinkRouter::collect_obstacles(const NodeGraph& graph, const LinkRouterOptions& options) {
    const std::span<const NodeRecord> nodes = graph.node_slots();
    const double margin = options.obstacle_margin;
    obstacles_.assign(nodes.size(), Box{});
    for (std::size_t index = 0U; index < nodes.size(); ++index) {
        const NodeRecord& node = nodes[index];
        if (!node.live) continue;
        Box box{};
        box.left = node.bounds.x - margin;
        box.top = node.bounds.y - margin;
        box.right = node.bounds.right() + margin;
        box.bottom = node.bounds.bottom() + margin;
        obstacles_[index] = box;
    }
}

void LinkRouter::prepare_ports(const NodeGraph& graph) {
    const std::span<const NodeRecord> nodes = graph.node_slots();
    port_base_.assign(nodes.size(), 0U);
    std::uint32_t total = 0U;
    for (std::size_t index = 0U; index < nodes.size(); ++index) {
        port_base_[index] = total;
        if (nodes[index].live) total += static_cast<std::uint32_t>(nodes[index].ports.size());
    }
    resolved_sides_.assign(total, unresolved_side);
    resolved_offsets_.assign(total, 0.5);
    port_far_.assign(total, 0.0);
    port_far_set_.assign(total, 0U);
}

LinkRouter::RouteEnd LinkRouter::make_end(const NodeGraph& graph, PortRef port, PortRef other_end) const noexcept {
    RouteEnd end{};
    end.node = port.node.index;
    end.port = port.port.value;
    const PortRecord* const record = graph.port(port);
    if (record == nullptr) return end;
    if ((*record).side != PortSide::automatic) {
        end.side_count = 1U;
        end.sides[0] = static_cast<std::uint8_t>((*record).side);
        end.offset = (*record).offset;
        return end;
    }
    end.automatic = true;
    const std::uint32_t global = port_base_[port.node.index] + port.port.value;
    if (resolved_sides_[global] != unresolved_side) {
        end.side_count = 1U;
        end.sides[0] = resolved_sides_[global];
        end.offset = resolved_offsets_[global];
        return end;
    }
    // The owner's ruling: an automatic port's side is chosen by direction,
    // the edge facing the link's other node, not by the search's cost (a
    // route leaving another edge can have fewer bends).
    const NodeRecord& node = graph.node_slots()[port.node.index];
    const NodeRecord& other = graph.node_slots()[other_end.node.index];
    end.side_count = 1U;
    end.sides[0] = facing_side(node, other);
    end.offset = 0.5;
    return end;
}

std::size_t LinkRouter::grid_index(const std::vector<double>& lines, double value) const noexcept {
    const std::vector<double>::const_iterator found = std::lower_bound(lines.begin(), lines.end(), value);
    const std::size_t index = static_cast<std::size_t>(found - lines.begin());
    return index;
}

void LinkRouter::fill_candidates(const NodeGraph& graph, const RouteEnd& end, double margin,
                                 std::array<Candidate, 4>& candidates) const noexcept {
    const NodeRecord& node = graph.node_slots()[end.node];
    const std::size_t width = columns_.size();
    for (std::size_t index = 0U; index < candidates.size(); ++index) {
        Candidate& candidate = candidates[index];
        candidate = Candidate{};
        if (index >= end.side_count) continue;
        candidate.side = end.sides[index];
        candidate.anchor = port_anchor(node, static_cast<PortSide>(candidate.side), end.offset);
        candidate.stub = stub_end(node, candidate.side, candidate.anchor, margin);
        const std::size_t column = grid_index(columns_, candidate.stub.x);
        const std::size_t row = grid_index(rows_, candidate.stub.y);
        if (column >= columns_.size() || row >= rows_.size()) continue;
        if (columns_[column] != candidate.stub.x || rows_[row] != candidate.stub.y) continue;
        candidate.vertex = static_cast<std::uint32_t>(column + row * width);
        candidate.valid = true;
    }
}

bool LinkRouter::build_grid(const NodeGraph& graph, const LinkRouterOptions& options) {
    const std::span<const NodeRecord> nodes = graph.node_slots();
    const std::span<const LinkRecord> links = graph.link_slots();
    const double margin = options.obstacle_margin;
    columns_.clear();
    rows_.clear();
    bool any = false;
    double low_x = 0.0;
    double low_y = 0.0;
    double high_x = 0.0;
    double high_y = 0.0;
    for (std::size_t index = 0U; index < nodes.size(); ++index) {
        if (!nodes[index].live) continue;
        const Box& box = obstacles_[index];
        columns_.push_back(box.left);
        columns_.push_back(box.right);
        rows_.push_back(box.top);
        rows_.push_back(box.bottom);
        if (!any) {
            low_x = box.left;
            low_y = box.top;
            high_x = box.right;
            high_y = box.bottom;
            any = true;
        }
        low_x = std::min(low_x, box.left);
        low_y = std::min(low_y, box.top);
        high_x = std::max(high_x, box.right);
        high_y = std::max(high_y, box.bottom);
    }
    if (!any) return false;
    // A frame one margin outside everything, so routes can go round.
    columns_.push_back(low_x - margin);
    columns_.push_back(high_x + margin);
    rows_.push_back(low_y - margin);
    rows_.push_back(high_y + margin);
    for (const LinkRecord& link : links) {
        if (!link.live) continue;
        const std::array<RouteEnd, 2> ends{make_end(graph, link.from, link.to), make_end(graph, link.to, link.from)};
        for (const RouteEnd& end : ends) {
            const NodeRecord& node = nodes[end.node];
            for (std::size_t index = 0U; index < end.side_count; ++index) {
                const std::uint8_t side = end.sides[index];
                const WorldPoint anchor = port_anchor(node, static_cast<PortSide>(side), end.offset);
                const WorldPoint stub = stub_end(node, side, anchor, margin);
                if (horizontal_heading(outward(side))) {
                    rows_.push_back(stub.y);
                } else {
                    columns_.push_back(stub.x);
                }
            }
        }
    }
    std::sort(columns_.begin(), columns_.end());
    columns_.erase(std::unique(columns_.begin(), columns_.end()), columns_.end());
    std::sort(rows_.begin(), rows_.end());
    rows_.erase(std::unique(rows_.begin(), rows_.end()), rows_.end());
    const std::size_t width = columns_.size();
    const std::size_t height = rows_.size();
    if (width < 2U || height < 2U) return false;
    if (width > maximum_route_grid_vertices / height) return false;
    const std::size_t vertices = width * height;
    horizontal_blocked_.assign((width - 1U) * height, 0U);
    vertical_blocked_.assign(width * (height - 1U), 0U);
    for (std::size_t index = 0U; index < nodes.size(); ++index) {
        if (!nodes[index].live) continue;
        const Box& box = obstacles_[index];
        const std::size_t left = grid_index(columns_, box.left);
        const std::size_t right = grid_index(columns_, box.right);
        const std::size_t top = grid_index(rows_, box.top);
        const std::size_t bottom = grid_index(rows_, box.bottom);
        // Edges strictly inside the box are blocked; its boundary is free.
        for (std::size_t row = top + 1U; row < bottom; ++row) {
            for (std::size_t column = left; column < right; ++column) {
                horizontal_blocked_[column + row * (width - 1U)] = 1U;
            }
        }
        for (std::size_t row = top; row < bottom; ++row) {
            for (std::size_t column = left + 1U; column < right; ++column) {
                vertical_blocked_[column + row * width] = 1U;
            }
        }
    }
    // The vertices are bounded by maximum_route_grid_vertices above; the
    // states (four headings a vertex) fit when the vertices do.
    if (vertices > std::numeric_limits<std::size_t>::max() / heading_count) return false;
    const std::size_t states = vertices * heading_count;
    if (cost_.size() < states) {
        cost_.resize(states, 0.0);
        parent_.resize(states, no_state);
        seen_.resize(states, 0U);
        closed_.resize(states, 0U);
    }
    horizontal_used_.assign(vertices, 0U);
    vertical_used_.assign(vertices, 0U);
    return true;
}

// -- routing ---------------------------------------------------------------------------

void LinkRouter::route_pass(const NodeGraph& graph, const LinkRouterOptions& options, bool grid_ready,
                            LinkRouteStatistics& statistics) {
    const std::span<const NodeRecord> nodes = graph.node_slots();
    const std::span<const LinkRecord> links = graph.link_slots();
    routes_.clear();
    points_.clear();
    statistics.routed = 0U;
    statistics.fallbacks = 0U;
    std::fill(horizontal_used_.begin(), horizontal_used_.end(), 0U);
    std::fill(vertical_used_.begin(), vertical_used_.end(), 0U);
    order_.clear();
    order_keys_.assign(links.size(), 0.0);
    for (std::uint32_t index = 0U; index < links.size(); ++index) {
        const LinkRecord& link = links[index];
        if (!link.live) continue;
        const WorldPoint from = centre_of(nodes[link.from.node.index].bounds);
        const WorldPoint to = centre_of(nodes[link.to.node.index].bounds);
        order_keys_[index] = std::fabs(to.x - from.x) + std::fabs(to.y - from.y);
        order_.push_back(index);
    }
    const ShorterFirst shorter{&order_keys_};
    std::sort(order_.begin(), order_.end(), shorter);
    for (const std::uint32_t index : order_) {
        const LinkRecord& link = links[index];
        const RouteEnd from = make_end(graph, link.from, link.to);
        const RouteEnd to = make_end(graph, link.to, link.from);
        RoutedLink route{};
        route.link = LinkId{index, link.generation};
        const bool found = grid_ready && search(graph, options, from, to, route, statistics);
        if (!found) {
            route_fallback(graph, options, from, to, route);
            ++statistics.fallbacks;
        }
        ++statistics.routed;
        routes_.push_back(route);
    }
    const RouteSlotOrder by_slot{};
    std::sort(routes_.begin(), routes_.end(), by_slot);
}

bool LinkRouter::search(const NodeGraph& graph, const LinkRouterOptions& options, const RouteEnd& from,
                        const RouteEnd& to, RoutedLink& route, LinkRouteStatistics& statistics) {
    const std::size_t width = columns_.size();
    const std::size_t height = rows_.size();
    std::array<Candidate, 4> starts{};
    std::array<Candidate, 4> goals{};
    fill_candidates(graph, from, options.obstacle_margin, starts);
    fill_candidates(graph, to, options.obstacle_margin, goals);
    ++search_stamp_;
    if (search_stamp_ == 0U) {
        std::fill(seen_.begin(), seen_.end(), 0U);
        std::fill(closed_.begin(), closed_.end(), 0U);
        search_stamp_ = 1U;
    }
    const std::uint32_t stamp = search_stamp_;
    heap_.clear();
    std::uint32_t sequence = 0U;
    const LaterEntry later{};
    bool any_goal = false;
    for (const Candidate& goal : goals) any_goal = any_goal || goal.valid;
    if (!any_goal) return false;
    // The heuristic: the Manhattan distance to the nearest goal (admissible
    // and consistent, as every step costs at least its length).
    const double infinity = std::numeric_limits<double>::infinity();
    for (const Candidate& start : starts) {
        if (!start.valid) continue;
        const std::uint32_t state = start.vertex * heading_count + outward(start.side);
        if (seen_[state] == stamp) continue;
        seen_[state] = stamp;
        cost_[state] = 0.0;
        parent_[state] = no_state;
        double estimate = infinity;
        for (const Candidate& goal : goals) {
            if (!goal.valid) continue;
            const double distance = std::fabs(goal.stub.x - start.stub.x) + std::fabs(goal.stub.y - start.stub.y);
            estimate = std::min(estimate, distance);
        }
        heap_.push_back(HeapEntry{estimate, sequence, state});
        ++sequence;
        std::push_heap(heap_.begin(), heap_.end(), later);
    }
    double best_total = infinity;
    std::uint32_t best_state = no_state;
    std::size_t best_goal = 0U;
    while (!heap_.empty()) {
        std::pop_heap(heap_.begin(), heap_.end(), later);
        const HeapEntry entry = heap_.back();
        heap_.pop_back();
        if (entry.estimate >= best_total) break;
        const std::uint32_t state = entry.state;
        if (closed_[state] == stamp) continue;
        closed_[state] = stamp;
        ++statistics.expanded;
        const std::uint32_t vertex = state / heading_count;
        const std::uint8_t heading = static_cast<std::uint8_t>(state % heading_count);
        const double cost = cost_[state];
        for (std::size_t index = 0U; index < goals.size(); ++index) {
            const Candidate& goal = goals[index];
            if (!goal.valid || goal.vertex != vertex) continue;
            // Arriving into the node along its inward normal is free; across
            // it, one turn; against it, two.
            const std::uint8_t inward = opposite(outward(goal.side));
            double extra = 0.0;
            if (heading == opposite(inward)) {
                extra = 2.0 * options.bend_penalty;
            } else if (heading != inward) {
                extra = options.bend_penalty;
            }
            const double total = cost + extra;
            if (total < best_total) {
                best_total = total;
                best_state = state;
                best_goal = index;
            }
        }
        const std::size_t column = vertex % width;
        const std::size_t row = vertex / width;
        for (std::uint8_t next = 0U; next < heading_count; ++next) {
            if (next == opposite(heading)) continue;
            std::size_t neighbour = 0U;
            double length = 0.0;
            if (next == east) {
                if (column + 1U >= width || horizontal_blocked_[column + row * (width - 1U)] != 0U) continue;
                neighbour = vertex + 1U;
                length = columns_[column + 1U] - columns_[column];
            } else if (next == west) {
                if (column == 0U || horizontal_blocked_[(column - 1U) + row * (width - 1U)] != 0U) continue;
                neighbour = vertex - 1U;
                length = columns_[column] - columns_[column - 1U];
            } else if (next == south) {
                if (row + 1U >= height || vertical_blocked_[column + row * width] != 0U) continue;
                neighbour = vertex + width;
                length = rows_[row + 1U] - rows_[row];
            } else {
                if (row == 0U || vertical_blocked_[column + (row - 1U) * width] != 0U) continue;
                neighbour = vertex - width;
                length = rows_[row] - rows_[row - 1U];
            }
            double step = length;
            if (next != heading) step += options.bend_penalty;
            const bool crosses = horizontal_heading(next) ? vertical_used_[neighbour] != 0U
                                                          : horizontal_used_[neighbour] != 0U;
            if (crosses) step += options.crossing_penalty;
            const std::uint32_t next_state = static_cast<std::uint32_t>(neighbour * heading_count + next);
            const double next_cost = cost + step;
            if (seen_[next_state] == stamp && next_cost >= cost_[next_state]) continue;
            seen_[next_state] = stamp;
            cost_[next_state] = next_cost;
            parent_[next_state] = state;
            const double x = columns_[neighbour % width];
            const double y = rows_[neighbour / width];
            double estimate = infinity;
            for (const Candidate& goal : goals) {
                if (!goal.valid) continue;
                const double distance = std::fabs(goal.stub.x - x) + std::fabs(goal.stub.y - y);
                estimate = std::min(estimate, distance);
            }
            heap_.push_back(HeapEntry{next_cost + estimate, sequence, next_state});
            ++sequence;
            std::push_heap(heap_.begin(), heap_.end(), later);
        }
    }
    if (best_state == no_state) return false;

    // The path, start to goal, and the edge it started from.
    path_.clear();
    std::uint32_t first_state = best_state;
    for (std::uint32_t state = best_state; state != no_state; state = parent_[state]) {
        path_.push_back(state / heading_count);
        first_state = state;
    }
    std::reverse(path_.begin(), path_.end());
    const std::uint32_t first_vertex = first_state / heading_count;
    const std::uint8_t first_heading = static_cast<std::uint8_t>(first_state % heading_count);
    std::size_t start_index = 0U;
    for (std::size_t index = 0U; index < starts.size(); ++index) {
        const Candidate& start = starts[index];
        if (start.valid && start.vertex == first_vertex && outward(start.side) == first_heading) {
            start_index = index;
            break;
        }
    }
    const Candidate& start = starts[start_index];
    const Candidate& goal = goals[best_goal];
    // Later searches see this route's crossings.
    for (std::size_t index = 1U; index < path_.size(); ++index) {
        const std::uint32_t before = path_[index - 1U];
        const std::uint32_t after = path_[index];
        const bool horizontal = before / width == after / width;
        std::vector<std::uint8_t>& used = horizontal ? horizontal_used_ : vertical_used_;
        used[before] = 1U;
        used[after] = 1U;
    }
    route.from_side = static_cast<PortSide>(start.side);
    route.to_side = static_cast<PortSide>(goal.side);
    route.from_anchor = start.anchor;
    route.to_anchor = goal.anchor;
    route.first_point = static_cast<std::uint32_t>(points_.size());
    points_.push_back(start.anchor);
    for (const std::uint32_t vertex : path_) {
        points_.push_back(WorldPoint{columns_[vertex % width], rows_[vertex / width]});
    }
    points_.push_back(goal.anchor);
    route.point_count = static_cast<std::uint32_t>(points_.size() - route.first_point);
    compress(route);
    return true;
}

void LinkRouter::route_fallback(const NodeGraph& graph, const LinkRouterOptions& options, const RouteEnd& from,
                                const RouteEnd& to, RoutedLink& route) {
    const std::span<const NodeRecord> nodes = graph.node_slots();
    const NodeRecord& source = nodes[from.node];
    const NodeRecord& target = nodes[to.node];
    const std::uint8_t from_side = from.side_count == 1U ? from.sides[0] : facing_side(source, target);
    const std::uint8_t to_side = to.side_count == 1U ? to.sides[0] : facing_side(target, source);
    route.fallback = true;
    route.from_side = static_cast<PortSide>(from_side);
    route.to_side = static_cast<PortSide>(to_side);
    route.from_anchor = port_anchor(source, route.from_side, from.offset);
    route.to_anchor = port_anchor(target, route.to_side, to.offset);
    route.first_point = static_cast<std::uint32_t>(points_.size());
    points_.push_back(route.from_anchor);
    points_.push_back(stub_end(source, from_side, route.from_anchor, options.obstacle_margin));
    points_.push_back(stub_end(target, to_side, route.to_anchor, options.obstacle_margin));
    points_.push_back(route.to_anchor);
    route.point_count = 4U;
    route.bends = 0U;
}

// Drops repeated points and the middle of three collinear ones, in place.
void LinkRouter::compress(RoutedLink& route) noexcept {
    const std::size_t first = route.first_point;
    const std::size_t end = first + route.point_count;
    std::size_t written = first;
    for (std::size_t read = first; read < end; ++read) {
        const WorldPoint point = points_[read];
        if (written > first && points_[written - 1U] == point) continue;
        if (written >= first + 2U && collinear(points_[written - 2U], points_[written - 1U], point)) {
            points_[written - 1U] = point;
            continue;
        }
        points_[written] = point;
        ++written;
    }
    points_.resize(written);
    route.point_count = static_cast<std::uint32_t>(written - first);
    route.bends = route.point_count >= 2U ? route.point_count - 2U : 0U;
}

bool LinkRouter::spread_automatic_ports(const NodeGraph& graph) {
    const std::span<const NodeRecord> nodes = graph.node_slots();
    const std::span<const LinkRecord> links = graph.link_slots();
    bool any = false;
    // An automatic port takes the edge of its first link (in slot order),
    // and remembers where that link's far end lies along the edge.
    for (const RoutedLink& route : routes_) {
        const LinkRecord& link = links[route.link.index];
        const std::array<PortRef, 2> ends{link.from, link.to};
        const std::array<PortSide, 2> sides{route.from_side, route.to_side};
        const std::array<WorldPoint, 2> fars{route.to_anchor, route.from_anchor};
        for (std::size_t end = 0U; end < 2U; ++end) {
            const PortRecord* const port = graph.port(ends[end]);
            if (port == nullptr || (*port).side != PortSide::automatic) continue;
            const std::uint32_t global = port_base_[ends[end].node.index] + ends[end].port.value;
            if (port_far_set_[global] != 0U) continue;
            const bool vertical_edge = sides[end] == PortSide::left || sides[end] == PortSide::right;
            port_far_[global] = vertical_edge ? fars[end].y : fars[end].x;
            port_far_set_[global] = 1U;
            resolved_sides_[global] = static_cast<std::uint8_t>(sides[end]);
            any = true;
        }
    }
    if (!any) return false;
    // A node's automatic ports on one edge, spread evenly along it in the
    // order of their far ends.
    for (std::size_t index = 0U; index < nodes.size(); ++index) {
        const NodeRecord& node = nodes[index];
        if (!node.live) continue;
        for (std::uint8_t side = 0U; side < unresolved_side; ++side) {
            port_keys_.clear();
            for (std::uint16_t port = 0U; port < node.ports.size(); ++port) {
                const std::uint32_t global = port_base_[index] + port;
                if (node.ports[port].side != PortSide::automatic || resolved_sides_[global] != side) continue;
                port_keys_.push_back(PortKey{port_far_[global], port});
            }
            if (port_keys_.empty()) continue;
            const PortKeyOrder by_far{};
            std::sort(port_keys_.begin(), port_keys_.end(), by_far);
            const double count = static_cast<double>(port_keys_.size());
            for (std::size_t rank = 0U; rank < port_keys_.size(); ++rank) {
                const std::uint32_t global = port_base_[index] + port_keys_[rank].port;
                resolved_offsets_[global] = (static_cast<double>(rank) + 1.0) / (count + 1.0);
            }
        }
    }
    return true;
}

// -- nudging -----------------------------------------------------------------------------

void LinkRouter::nudge(const LinkRouterOptions& options, LinkRouteStatistics& statistics) {
    segments_.clear();
    for (std::uint32_t route_index = 0U; route_index < routes_.size(); ++route_index) {
        const RoutedLink& route = routes_[route_index];
        if (route.fallback || route.point_count < 4U) continue;
        // Interior segments only: the first and last leave and enter ports.
        for (std::uint32_t at = 1U; at + 2U < route.point_count; ++at) {
            const std::size_t index = route.first_point + at;
            const WorldPoint before = points_[index - 1U];
            const WorldPoint start = points_[index];
            const WorldPoint end = points_[index + 1U];
            const WorldPoint after = points_[index + 2U];
            Segment segment{};
            segment.horizontal = start.y == end.y;
            segment.route = route_index;
            segment.first_point = static_cast<std::uint32_t>(index);
            // Along the line: the extent; across it: where each neighbour
            // goes. The low end is the start when the segment runs forward.
            const double start_along = segment.horizontal ? start.x : start.y;
            const double end_along = segment.horizontal ? end.x : end.y;
            segment.coordinate = segment.horizontal ? start.y : start.x;
            const double before_across = segment.horizontal ? before.y : before.x;
            const double after_across = segment.horizontal ? after.y : after.x;
            const int side_before = sign_of(before_across - segment.coordinate);
            const int side_after = sign_of(after_across - segment.coordinate);
            const double leg_before = std::fabs(before_across - segment.coordinate);
            const double leg_after = std::fabs(after_across - segment.coordinate);
            const bool forward = start_along <= end_along;
            segment.low = forward ? start_along : end_along;
            segment.high = forward ? end_along : start_along;
            segment.side_low = forward ? side_before : side_after;
            segment.side_high = forward ? side_after : side_before;
            segment.leg_low = forward ? leg_before : leg_after;
            segment.leg_high = forward ? leg_after : leg_before;
            segments_.push_back(segment);
        }
    }
    const SegmentOrder by_line{};
    std::sort(segments_.begin(), segments_.end(), by_line);
    // Each line's overlapping segments form a cluster; a cluster of k
    // segments takes k tracks centred on the line, no further apart than
    // the margin allows on either side.
    const double room = options.obstacle_margin - 1.0;
    std::size_t cluster = 0U;
    while (cluster < segments_.size()) {
        const Segment& head = segments_[cluster];
        std::size_t end = cluster + 1U;
        double reach = head.high;
        while (end < segments_.size()) {
            const Segment& next = segments_[end];
            if (next.horizontal != head.horizontal || next.coordinate != head.coordinate || next.low >= reach) break;
            reach = std::max(reach, next.high);
            ++end;
        }
        const std::size_t count = end - cluster;
        if (count >= 2U && room > 0.0) {
            // A segment that stops short of the channel's far end and turns
            // to side s must lie on side s of those that go on, or its turn
            // crosses them; the further short it stops, the further out.
            // Likewise at the near end. Segments that span the channel nest
            // by their turns' lengths, longer turns inside.
            double low_end = segments_[cluster].low;
            double high_end = segments_[cluster].high;
            for (std::size_t index = cluster; index < end; ++index) {
                low_end = std::min(low_end, segments_[index].low);
                high_end = std::max(high_end, segments_[index].high);
            }
            for (std::size_t index = cluster; index < end; ++index) {
                Segment& segment = segments_[index];
                segment.key = static_cast<double>(segment.side_high) * (high_end - segment.high) +
                              static_cast<double>(segment.side_low) * (segment.low - low_end);
                segment.tie = static_cast<double>(segment.side_low) * segment.leg_low +
                              static_cast<double>(segment.side_high) * segment.leg_high;
            }
            const TrackOrder by_turn{};
            std::sort(segments_.begin() + static_cast<std::ptrdiff_t>(cluster),
                      segments_.begin() + static_cast<std::ptrdiff_t>(end), by_turn);
            const double spread = static_cast<double>(count - 1U);
            const double spacing = std::min(options.nudge_spacing, 2.0 * room / spread);
            for (std::size_t rank = 0U; rank < count; ++rank) {
                const Segment& segment = segments_[cluster + rank];
                const double offset = (static_cast<double>(rank) - spread * 0.5) * spacing;
                if (offset == 0.0) continue;
                WorldPoint& start = points_[segment.first_point];
                WorldPoint& finish = points_[segment.first_point + 1U];
                if (segment.horizontal) {
                    start.y += offset;
                    finish.y += offset;
                } else {
                    start.x += offset;
                    finish.x += offset;
                }
                ++statistics.nudged;
            }
        }
        cluster = end;
    }
}

// -- drawing paths ------------------------------------------------------------------------

void LinkRouter::flatten(const LinkRouterOptions& options) {
    drawn_.clear();
    for (RoutedLink& route : routes_) {
        route.first_drawn = static_cast<std::uint32_t>(drawn_.size());
        const std::size_t first = route.first_point;
        const std::size_t count = route.point_count;
        if (options.style == LinkRouteStyle::orthogonal || count < 3U || options.corner_radius <= 0.0) {
            for (std::size_t index = 0U; index < count; ++index) drawn_.push_back(points_[first + index]);
            route.drawn_count = static_cast<std::uint32_t>(drawn_.size() - route.first_drawn);
            continue;
        }
        drawn_.push_back(points_[first]);
        for (std::size_t index = 1U; index + 1U < count; ++index) {
            const WorldPoint before = points_[first + index - 1U];
            const WorldPoint corner = points_[first + index];
            const WorldPoint after = points_[first + index + 1U];
            const double in_length = std::hypot(corner.x - before.x, corner.y - before.y);
            const double out_length = std::hypot(after.x - corner.x, after.y - corner.y);
            if (in_length <= 0.0 || out_length <= 0.0) continue;
            // Half of each leg at most, so neighbouring corners never overlap.
            const double radius = std::min(options.corner_radius, std::min(in_length, out_length) * 0.5);
            const WorldPoint entry{corner.x - (corner.x - before.x) / in_length * radius,
                                   corner.y - (corner.y - before.y) / in_length * radius};
            const WorldPoint exit{corner.x + (after.x - corner.x) / out_length * radius,
                                  corner.y + (after.y - corner.y) / out_length * radius};
            // A quadratic from entry to exit with the corner as its control
            // point. Its second derivative is 2D (D = entry - 2 corner +
            // exit), so n equal chords stray at most |D| / (4 n^2) from it:
            // n = ceil(sqrt(|D| / (4 tolerance))) keeps every chord within
            // the tolerance.
            const double second_x = entry.x - 2.0 * corner.x + exit.x;
            const double second_y = entry.y - 2.0 * corner.y + exit.y;
            const double magnitude = std::hypot(second_x, second_y);
            double wanted = std::ceil(std::sqrt(magnitude / (4.0 * options.flatten_tolerance)));
            wanted = std::max(1.0, std::min(wanted, static_cast<double>(maximum_corner_steps)));
            const std::uint32_t steps = static_cast<std::uint32_t>(wanted);
            for (std::uint32_t step = 0U; step <= steps; ++step) {
                const double t = static_cast<double>(step) / static_cast<double>(steps);
                const double u = 1.0 - t;
                const WorldPoint point{u * u * entry.x + 2.0 * u * t * corner.x + t * t * exit.x,
                                       u * u * entry.y + 2.0 * u * t * corner.y + t * t * exit.y};
                drawn_.push_back(point);
            }
        }
        drawn_.push_back(points_[first + count - 1U]);
        route.drawn_count = static_cast<std::uint32_t>(drawn_.size() - route.first_drawn);
    }
}

// Perpendicular crossings between segments of different routes, each
// segment's ends excluded (a shared port or a touching corner is no
// crossing). Quadratic in the segment count; a statistic and a test aid.
std::size_t LinkRouter::count_crossings() const noexcept {
    std::size_t crossings = 0U;
    for (std::size_t first = 0U; first < routes_.size(); ++first) {
        const RoutedLink& one = routes_[first];
        for (std::size_t second = first + 1U; second < routes_.size(); ++second) {
            const RoutedLink& other = routes_[second];
            for (std::uint32_t a = 0U; a + 1U < one.point_count; ++a) {
                const WorldPoint a0 = points_[one.first_point + a];
                const WorldPoint a1 = points_[one.first_point + a + 1U];
                for (std::uint32_t b = 0U; b + 1U < other.point_count; ++b) {
                    const WorldPoint b0 = points_[other.first_point + b];
                    const WorldPoint b1 = points_[other.first_point + b + 1U];
                    const bool a_horizontal = a0.y == a1.y && a0.x != a1.x;
                    const bool a_vertical = a0.x == a1.x && a0.y != a1.y;
                    const bool b_horizontal = b0.y == b1.y && b0.x != b1.x;
                    const bool b_vertical = b0.x == b1.x && b0.y != b1.y;
                    if (a_horizontal && b_vertical) {
                        const bool across = b0.x > std::min(a0.x, a1.x) && b0.x < std::max(a0.x, a1.x) &&
                                            a0.y > std::min(b0.y, b1.y) && a0.y < std::max(b0.y, b1.y);
                        if (across) ++crossings;
                    } else if (a_vertical && b_horizontal) {
                        const bool across = a0.x > std::min(b0.x, b1.x) && a0.x < std::max(b0.x, b1.x) &&
                                            b0.y > std::min(a0.y, a1.y) && b0.y < std::max(a0.y, a1.y);
                        if (across) ++crossings;
                    }
                }
            }
        }
    }
    return crossings;
}

void append_link_route_trace(std::string& out, const LinkRouter& router) {
    for (const RoutedLink& route : router.routes()) {
        std::array<char, 128> head{};
        const std::string_view from = port_side_name(route.from_side);
        const std::string_view to = port_side_name(route.to_side);
        // Side names are a few characters; the bound keeps the precision an int.
        const int from_length = static_cast<int>(std::min(from.size(), std::size_t{16U}));
        const int to_length = static_cast<int>(std::min(to.size(), std::size_t{16U}));
        const int written = std::snprintf(head.data(), head.size(), "link %u.%u %.*s>%.*s bends=%u%s:",
                                          static_cast<unsigned>(route.link.index),
                                          static_cast<unsigned>(route.link.generation), from_length,
                                          from.data(), to_length, to.data(),
                                          static_cast<unsigned>(route.bends), route.fallback ? " fallback" : "");
        if (written > 0) out.append(head.data(), std::min(static_cast<std::size_t>(written), head.size() - 1U));
        for (const WorldPoint point : router.points(route)) {
            std::array<char, 64> text{};
            const int length = std::snprintf(text.data(), text.size(), " %.6g,%.6g", rounded_64(point.x),
                                             rounded_64(point.y));
            if (length > 0) out.append(text.data(), std::min(static_cast<std::size_t>(length), text.size() - 1U));
        }
        out.push_back('\n');
    }
}

} // namespace gui_forms
