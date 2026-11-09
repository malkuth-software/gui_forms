#include "gui_forms/controls/node_canvas/node_graph/node_graph.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace gui_forms {
namespace {

bool finite_rect(const WorldRect& bounds) noexcept {
    const bool finite = std::isfinite(bounds.x) && std::isfinite(bounds.y) && std::isfinite(bounds.width) &&
                        std::isfinite(bounds.height) && bounds.width >= 0.0 && bounds.height >= 0.0;
    return finite;
}

bool valid_port(const PortRecord& port) noexcept {
    const bool offset_valid = std::isfinite(port.offset) && port.offset >= 0.0 && port.offset <= 1.0;
    const bool side_valid = port.side == PortSide::left || port.side == PortSide::right ||
                            port.side == PortSide::top || port.side == PortSide::bottom ||
                            port.side == PortSide::automatic;
    return offset_valid && side_valid;
}

// Makes room for `needed` entries in a free list, growing geometrically, so
// a later free never allocates.
void reserve_free_list(std::vector<std::uint32_t>& list, std::size_t needed) {
    if (list.capacity() >= needed) return;
    const std::size_t grown = std::max(needed, list.capacity() * 2U);
    list.reserve(grown);
}

// The next generation of a freed slot; zero is skipped so that a
// zero-initialised identity is never live.
std::uint32_t next_generation(std::uint32_t generation) noexcept {
    std::uint32_t next = generation + 1U;
    if (next == 0U) next = 1U;
    return next;
}

} // namespace

std::string_view graph_status_name(GraphStatus status) noexcept {
    switch (status) {
    case GraphStatus::ok: return "ok";
    case GraphStatus::stale_identity: return "stale_identity";
    case GraphStatus::bad_port: return "bad_port";
    case GraphStatus::wrong_direction: return "wrong_direction";
    case GraphStatus::occupied: return "occupied";
    case GraphStatus::same_node: return "same_node";
    case GraphStatus::invalid_argument: return "invalid_argument";
    case GraphStatus::capacity: return "capacity";
    }
    return "none";
}

void NodeGraph::reserve(std::size_t nodes, std::size_t links) {
    nodes_.reserve(nodes);
    links_.reserve(links);
    free_nodes_.reserve(nodes);
    free_links_.reserve(links);
    move_marks_.reserve(nodes);
}

bool NodeGraph::live(NodeId node) const noexcept {
    if (node.index >= nodes_.size()) return false;
    const NodeRecord& record = nodes_[node.index];
    const bool alive = record.live && record.generation == node.generation;
    return alive;
}

bool NodeGraph::live(LinkId link) const noexcept {
    if (link.index >= links_.size()) return false;
    const LinkRecord& record = links_[link.index];
    const bool alive = record.live && record.generation == link.generation;
    return alive;
}

void NodeGraph::advance() noexcept {
    ++revision_;
    if (revision_ == 0U) revision_ = 1U;
}

GraphResult NodeGraph::add_node(const NodeSpec& spec) {
    GraphResult result{};
    if (!finite_rect(spec.bounds)) {
        result.status = GraphStatus::invalid_argument;
        return result;
    }
    if (spec.ports.size() > maximum_node_ports) {
        result.status = GraphStatus::capacity;
        return result;
    }
    for (const PortRecord& port : spec.ports) {
        if (valid_port(port)) continue;
        result.status = GraphStatus::invalid_argument;
        return result;
    }
    const bool reuses = !free_nodes_.empty();
    if (!reuses && nodes_.size() >= std::numeric_limits<std::uint32_t>::max()) {
        result.status = GraphStatus::capacity;
        return result;
    }
    // Copies first, so a failed allocation leaves the graph unchanged.
    NodeRecord record{};
    record.title = spec.title;
    record.bounds = spec.bounds;
    record.ports = spec.ports;
    record.live = true;
    std::uint32_t index = 0U;
    if (reuses) {
        index = free_nodes_.back();
        record.generation = next_generation(nodes_[index].generation);
        nodes_[index] = std::move(record);
        free_nodes_.pop_back();
    } else {
        // The free list and the move marks grow with the slots, so removing
        // and moving nodes never allocate.
        reserve_free_list(free_nodes_, nodes_.size() + 1U);
        move_marks_.reserve(nodes_.size() + 1U);
        index = static_cast<std::uint32_t>(nodes_.size());
        record.generation = 1U;
        nodes_.push_back(std::move(record));
        move_marks_.push_back(0U);
    }
    ++live_nodes_;
    advance();
    result.node = NodeId{index, nodes_[index].generation};
    return result;
}

void NodeGraph::free_link(std::uint32_t index) noexcept {
    LinkRecord& record = links_[index];
    record.live = false;
    // The free list was reserved to the link slots' count, so this never
    // allocates (see connect).
    free_links_.push_back(index);
    --live_links_;
}

GraphResult NodeGraph::remove_node(NodeId node) {
    GraphResult result{};
    if (!live(node)) {
        result.status = GraphStatus::stale_identity;
        return result;
    }
    for (std::uint32_t index = 0U; index < links_.size(); ++index) {
        const LinkRecord& record = links_[index];
        if (!record.live) continue;
        if (!(record.from.node == node) && !(record.to.node == node)) continue;
        free_link(index);
    }
    NodeRecord& record = nodes_[node.index];
    record.live = false;
    record.ports.clear();
    record.title.clear();
    free_nodes_.push_back(node.index);
    --live_nodes_;
    advance();
    return result;
}

GraphResult NodeGraph::move_nodes(std::span<const NodeId> nodes, WorldPoint delta) {
    GraphResult result{};
    if (nodes.empty() || !std::isfinite(delta.x) || !std::isfinite(delta.y)) {
        result.status = GraphStatus::invalid_argument;
        return result;
    }
    for (const NodeId node : nodes) {
        if (live(node)) continue;
        result.status = GraphStatus::stale_identity;
        return result;
    }
    // A node listed twice moves once: each moved slot is marked with the
    // revision this move makes, in storage kept with the slots, so a move
    // allocates nothing (a drag moves nodes on every frame).
    const std::uint64_t mark = revision_ + 1U;
    for (const NodeId node : nodes) {
        if (move_marks_[node.index] == mark) continue;
        move_marks_[node.index] = mark;
        NodeRecord& record = nodes_[node.index];
        record.bounds.x += delta.x;
        record.bounds.y += delta.y;
    }
    advance();
    return result;
}

GraphResult NodeGraph::set_node_bounds(NodeId node, WorldRect bounds) {
    GraphResult result{};
    if (!live(node)) {
        result.status = GraphStatus::stale_identity;
        return result;
    }
    if (!finite_rect(bounds)) {
        result.status = GraphStatus::invalid_argument;
        return result;
    }
    nodes_[node.index].bounds = bounds;
    advance();
    return result;
}

GraphResult NodeGraph::set_title(NodeId node, std::string_view title) {
    GraphResult result{};
    if (!live(node)) {
        result.status = GraphStatus::stale_identity;
        return result;
    }
    nodes_[node.index].title.assign(title);
    advance();
    return result;
}

GraphResult NodeGraph::set_port_lamp(PortRef port, PortLamp lamp) {
    GraphResult result{};
    if (!live(port.node)) {
        result.status = GraphStatus::stale_identity;
        return result;
    }
    NodeRecord& record = nodes_[port.node.index];
    if (port.port.value >= record.ports.size()) {
        result.status = GraphStatus::bad_port;
        return result;
    }
    if (lamp != PortLamp::off && lamp != PortLamp::on && lamp != PortLamp::error) {
        result.status = GraphStatus::invalid_argument;
        return result;
    }
    record.ports[port.port.value].lamp = lamp;
    advance();
    return result;
}

GraphResult NodeGraph::connect(PortRef from, PortRef to) {
    GraphResult result{};
    if (!live(from.node) || !live(to.node)) {
        result.status = GraphStatus::stale_identity;
        return result;
    }
    const NodeRecord& source = nodes_[from.node.index];
    const NodeRecord& target = nodes_[to.node.index];
    if (from.port.value >= source.ports.size() || to.port.value >= target.ports.size()) {
        result.status = GraphStatus::bad_port;
        return result;
    }
    if (from.node == to.node) {
        result.status = GraphStatus::same_node;
        return result;
    }
    if (source.ports[from.port.value].direction != PortDirection::output ||
        target.ports[to.port.value].direction != PortDirection::input) {
        result.status = GraphStatus::wrong_direction;
        return result;
    }
    if (live(link_into(to))) {
        result.status = GraphStatus::occupied;
        return result;
    }
    const bool reuses = !free_links_.empty();
    if (!reuses && links_.size() >= std::numeric_limits<std::uint32_t>::max()) {
        result.status = GraphStatus::capacity;
        return result;
    }
    LinkRecord record{};
    record.from = from;
    record.to = to;
    record.live = true;
    std::uint32_t index = 0U;
    if (reuses) {
        index = free_links_.back();
        record.generation = next_generation(links_[index].generation);
        links_[index] = record;
        free_links_.pop_back();
    } else {
        // Grow the free list with the slots, so freeing a link never
        // allocates (free_link runs inside remove_node, after validation).
        reserve_free_list(free_links_, links_.size() + 1U);
        index = static_cast<std::uint32_t>(links_.size());
        record.generation = 1U;
        links_.push_back(record);
    }
    ++live_links_;
    advance();
    result.link = LinkId{index, links_[index].generation};
    return result;
}

GraphResult NodeGraph::disconnect(LinkId link) {
    GraphResult result{};
    if (!live(link)) {
        result.status = GraphStatus::stale_identity;
        return result;
    }
    free_link(link.index);
    advance();
    return result;
}

const NodeRecord* NodeGraph::node(NodeId node) const noexcept {
    if (!live(node)) return nullptr;
    return &nodes_[node.index];
}

const LinkRecord* NodeGraph::link(LinkId link) const noexcept {
    if (!live(link)) return nullptr;
    return &links_[link.index];
}

const PortRecord* NodeGraph::port(PortRef port) const noexcept {
    if (!live(port.node)) return nullptr;
    const NodeRecord& record = nodes_[port.node.index];
    if (port.port.value >= record.ports.size()) return nullptr;
    return &record.ports[port.port.value];
}

LinkId NodeGraph::link_into(PortRef input) const noexcept {
    for (std::uint32_t index = 0U; index < links_.size(); ++index) {
        const LinkRecord& record = links_[index];
        if (record.live && record.to == input) return LinkId{index, record.generation};
    }
    return LinkId{};
}

WorldPoint port_anchor(const NodeRecord& node, PortSide side, double offset) noexcept {
    const WorldRect& bounds = node.bounds;
    WorldPoint anchor{};
    switch (side) {
    case PortSide::left: anchor = WorldPoint{bounds.x, bounds.y + bounds.height * offset}; break;
    case PortSide::right: anchor = WorldPoint{bounds.right(), bounds.y + bounds.height * offset}; break;
    case PortSide::top: anchor = WorldPoint{bounds.x + bounds.width * offset, bounds.y}; break;
    case PortSide::bottom: anchor = WorldPoint{bounds.x + bounds.width * offset, bounds.bottom()}; break;
    case PortSide::automatic:
    default:
        // Automatic is resolved by the caller; the node's centre stands in.
        anchor = WorldPoint{bounds.x + bounds.width * 0.5, bounds.y + bounds.height * 0.5};
        break;
    }
    return anchor;
}

} // namespace gui_forms
