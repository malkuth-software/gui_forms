// The node graph model (roadmap 2.4, phase N0 skeleton): adding, moving and
// removing nodes, typed refusals of connect (stale identity, bad port, wrong
// direction, same node, occupied input), stale identities after removal and
// slot reuse with a new generation, removal taking the node's links, lamps,
// titles, the revision counter, and the rule that a refused operation
// leaves the graph unchanged. Headless and renderer-free.

#include "gui_forms/controls/node_graph.hpp"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "node-graph-tests: " << message << '\n';
    std::exit(1);
}

PortRecord port_of(const char* name, PortDirection direction, PortSide side, double offset) {
    PortRecord port{};
    port.name = name;
    port.direction = direction;
    port.side = side;
    port.offset = offset;
    return port;
}

// A node at (x, y), 100 by 60, with one input on the left and two outputs on
// the right.
NodeSpec spec_at(const char* title, double x, double y) {
    NodeSpec spec{};
    spec.key = title;
    spec.title = title;
    spec.bounds = WorldRect{x, y, 100.0, 60.0};
    spec.ports.push_back(port_of("in", PortDirection::input, PortSide::left, 0.5));
    spec.ports.push_back(port_of("out", PortDirection::output, PortSide::right, 0.33));
    spec.ports.push_back(port_of("extra", PortDirection::output, PortSide::right, 0.66));
    return spec;
}

void add_move_remove() {
    NodeGraph graph;
    graph.reserve(4U, 4U);
    const std::uint64_t start = graph.revision();
    const GraphResult first = graph.add_node(spec_at("Noise", 0.0, 0.0));
    const GraphResult second = graph.add_node(spec_at("Mix", 200.0, 40.0));
    require(first.status == GraphStatus::ok && second.status == GraphStatus::ok && graph.node_count() == 2U,
            "two nodes are added");
    require(graph.revision() == start + 2U, "each operation advances the revision once");
    const NodeRecord* const noise = graph.node(first.node);
    require(noise != nullptr && (*noise).title == "Noise" && (*noise).ports.size() == 3U,
            "the record holds the title and the ports");

    const std::array<NodeId, 3> moved{first.node, second.node, first.node};
    require(graph.move_nodes(moved, WorldPoint{10.0, -5.0}).status == GraphStatus::ok, "a move succeeds");
    require((*graph.node(first.node)).bounds == WorldRect{10.0, -5.0, 100.0, 60.0} &&
                (*graph.node(second.node)).bounds == WorldRect{210.0, 35.0, 100.0, 60.0},
            "a node listed twice moves once");

    const PortRef out{first.node, PortIndex{1U}};
    const PortRef in{second.node, PortIndex{0U}};
    const GraphResult joined = graph.connect(out, in);
    require(joined.status == GraphStatus::ok && graph.link_count() == 1U, "an output connects to an input");
    require(graph.link_into(in) == joined.link, "the input knows its link");

    const GraphResult removed = graph.remove_node(first.node);
    require(removed.status == GraphStatus::ok && graph.node_count() == 1U && graph.link_count() == 0U,
            "removing a node removes its links");
    require(graph.node(first.node) == nullptr && graph.link(joined.link) == nullptr,
            "the removed node's and link's identities are stale");
    const std::uint64_t before = graph.revision();
    require(graph.move_nodes(moved, WorldPoint{1.0, 1.0}).status == GraphStatus::stale_identity &&
                (*graph.node(second.node)).bounds == WorldRect{210.0, 35.0, 100.0, 60.0} &&
                graph.revision() == before,
            "one stale identity refuses the whole move and changes nothing");

    const GraphResult reused = graph.add_node(spec_at("Again", 0.0, 0.0));
    require(reused.node.index == first.node.index && reused.node.generation == first.node.generation + 1U,
            "a freed slot is reused with the next generation");
    require(graph.node(first.node) == nullptr && graph.node(reused.node) != nullptr,
            "the old identity stays stale after reuse");
}

void connect_refusals() {
    NodeGraph graph;
    const NodeId a = graph.add_node(spec_at("A", 0.0, 0.0)).node;
    const NodeId b = graph.add_node(spec_at("B", 200.0, 0.0)).node;
    const NodeId c = graph.add_node(spec_at("C", 400.0, 0.0)).node;
    const PortRef a_out{a, PortIndex{1U}};
    const PortRef b_in{b, PortIndex{0U}};
    const PortRef c_out{c, PortIndex{1U}};
    require(graph.connect(a_out, b_in).status == GraphStatus::ok, "the first link is made");
    const std::uint64_t before = graph.revision();
    require(graph.connect(c_out, b_in).status == GraphStatus::occupied, "an input takes one link");
    require(graph.connect(b_in, a_out).status == GraphStatus::wrong_direction, "links run output to input");
    require(graph.connect(PortRef{a, PortIndex{1U}}, PortRef{a, PortIndex{0U}}).status == GraphStatus::same_node,
            "a node does not link to itself");
    require(graph.connect(PortRef{a, PortIndex{9U}}, b_in).status == GraphStatus::bad_port,
            "a port beyond the node's ports is refused");
    require(graph.connect(PortRef{NodeId{7U, 1U}, PortIndex{1U}}, b_in).status == GraphStatus::stale_identity,
            "an unknown node is a stale identity");
    require(graph.revision() == before && graph.link_count() == 1U, "refused connects change nothing");
    require(graph.connect(PortRef{a, PortIndex{2U}}, PortRef{c, PortIndex{0U}}).status == GraphStatus::ok &&
                graph.connect(a_out, PortRef{c, PortIndex{0U}}).status == GraphStatus::occupied,
            "an output feeds several inputs; each input still takes one");
    require(graph_status_name(GraphStatus::occupied) == "occupied", "statuses have names");
}

void lamps_titles_and_arguments() {
    NodeGraph graph;
    const NodeId a = graph.add_node(spec_at("A", 0.0, 0.0)).node;
    require(graph.set_port_lamp(PortRef{a, PortIndex{0U}}, PortLamp::on).status == GraphStatus::ok &&
                (*graph.port(PortRef{a, PortIndex{0U}})).lamp == PortLamp::on,
            "a port's lamp is set");
    require(graph.set_port_lamp(PortRef{a, PortIndex{1U}}, PortLamp::error).status == GraphStatus::ok &&
                (*graph.port(PortRef{a, PortIndex{1U}})).lamp == PortLamp::error,
            "a lamp shows an error");
    require(graph.set_port_lamp(PortRef{a, PortIndex{5U}}, PortLamp::on).status == GraphStatus::bad_port,
            "a lamp on a missing port is refused");
    require(graph.set_title(a, "Renamed").status == GraphStatus::ok && (*graph.node(a)).title == "Renamed",
            "a title is set");

    NodeSpec bad = spec_at("Bad", 0.0, 0.0);
    bad.bounds.width = -1.0;
    require(graph.add_node(bad).status == GraphStatus::invalid_argument, "a negative size is refused");
    NodeSpec nan = spec_at("NaN", std::numeric_limits<double>::quiet_NaN(), 0.0);
    require(graph.add_node(nan).status == GraphStatus::invalid_argument, "a non-finite position is refused");
    NodeSpec bad_offset = spec_at("Offset", 0.0, 0.0);
    bad_offset.ports[0].offset = 1.5;
    require(graph.add_node(bad_offset).status == GraphStatus::invalid_argument, "an offset past the edge is refused");
    NodeSpec crowded = spec_at("Crowded", 0.0, 0.0);
    crowded.ports.resize(maximum_node_ports + 1U);
    require(graph.add_node(crowded).status == GraphStatus::capacity, "too many ports are refused");
    require(graph.node_count() == 1U, "refused adds leave one node");
    const std::array<NodeId, 1> one{a};
    require(graph.move_nodes(one, WorldPoint{std::numeric_limits<double>::infinity(), 0.0}).status ==
                GraphStatus::invalid_argument,
            "a non-finite move is refused");
    require(graph.set_node_bounds(a, WorldRect{5.0, 6.0, 50.0, 40.0}).status == GraphStatus::ok &&
                (*graph.node(a)).bounds == WorldRect{5.0, 6.0, 50.0, 40.0},
            "a node takes new bounds");
}

void anchors() {
    NodeRecord node{};
    node.bounds = WorldRect{10.0, 20.0, 100.0, 60.0};
    require(port_anchor(node, PortSide::left, 0.5) == WorldPoint{10.0, 50.0}, "a left anchor lies on the left edge");
    require(port_anchor(node, PortSide::right, 0.25) == WorldPoint{110.0, 35.0}, "a right anchor at a quarter");
    require(port_anchor(node, PortSide::top, 0.5) == WorldPoint{60.0, 20.0}, "a top anchor");
    require(port_anchor(node, PortSide::bottom, 1.0) == WorldPoint{110.0, 80.0}, "a bottom anchor at the end");
}

} // namespace

int main() {
    add_move_remove();
    connect_refusals();
    lamps_titles_and_arguments();
    anchors();
    std::cout << "node-graph-tests: records, generations, typed refusals, lamps and anchors passed\n";
    return 0;
}
