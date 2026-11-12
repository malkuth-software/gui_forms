#pragma once
// The node graph (roadmap 2.4, phase N0 skeleton; the node engine study,
// desks/coordinator/NODE_ENGINE_AND_VALUE_BAR_STUDY.md, with the owner's
// rulings of 2026-11-08 as relayed by the coordinator): the model a
// NodeCanvas draws. Nodes, their ports and the links between ports are
// records in the graph's arrays, never child controls (study section 2.1,
// question 1): the canvas paints and hit-tests them.
//
// Identities carry a generation. Removing a record frees its slot for reuse
// with the next generation, so an identity kept past its record's removal
// is stale and every operation refuses it with GraphStatus::stale_identity;
// nothing dereferences a stale identity.
//
// Geometry is in world units (WorldPoint, WorldRect), float64, so deep zoom
// keeps placement; the canvas converts to view units once per frame.
//
// Mutation is through the named operations only. Each validates every
// argument before changing anything, so a refused operation leaves the
// graph exactly as it was, and each successful one advances `revision`,
// which the canvas and the link router read to know their caches are stale.
// Undo, serialization, socket-type conversion and frames arrive with the
// rest of phase N0.

#include "gui_forms/export/application_export.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

// -- world geometry ---------------------------------------------------------------

// A point in graph space (world units, float64).
struct GUI_FORMS_API WorldPoint final {
    double x{};
    double y{};
    friend bool operator==(WorldPoint left, WorldPoint right) noexcept {
        return left.x == right.x && left.y == right.y;
    }
};

// An axis-aligned rectangle in graph space: its top-left corner and size.
struct GUI_FORMS_API WorldRect final {
    double x{};
    double y{};
    double width{};
    double height{};
    [[nodiscard]] double right() const noexcept { return x + width; }
    [[nodiscard]] double bottom() const noexcept { return y + height; }
    // Whether `point` lies inside or on the edge.
    [[nodiscard]] bool contains(WorldPoint point) const noexcept {
        const bool inside = point.x >= x && point.x <= x + width && point.y >= y && point.y <= y + height;
        return inside;
    }
    friend bool operator==(const WorldRect& left, const WorldRect& right) noexcept {
        return left.x == right.x && left.y == right.y && left.width == right.width && left.height == right.height;
    }
};

// -- identities -------------------------------------------------------------------

// A node's identity: its slot and the slot's generation when it was added.
struct GUI_FORMS_API NodeId final {
    std::uint32_t index{};
    std::uint32_t generation{};
    friend bool operator==(NodeId left, NodeId right) noexcept {
        return left.index == right.index && left.generation == right.generation;
    }
};

// A link's identity, as NodeId.
struct GUI_FORMS_API LinkId final {
    std::uint32_t index{};
    std::uint32_t generation{};
    friend bool operator==(LinkId left, LinkId right) noexcept {
        return left.index == right.index && left.generation == right.generation;
    }
};

// A port's position in its node's port list (not an identity: the list is
// fixed when the node is added).
struct GUI_FORMS_API PortIndex final {
    std::uint16_t value{};
    friend bool operator==(PortIndex left, PortIndex right) noexcept { return left.value == right.value; }
};

// One port of one node.
struct GUI_FORMS_API PortRef final {
    NodeId node{};
    PortIndex port{};
    friend bool operator==(PortRef left, PortRef right) noexcept {
        return left.node == right.node && left.port == right.port;
    }
};

// -- records ----------------------------------------------------------------------

// Links run from an output to an input.
enum class PortDirection : std::uint8_t { input, output };

// The node edge a port sits on. `automatic` lets the link router choose the
// edge facing the port's links (study section 2.4, the owner's "port side
// chosen by direction"); the port then sits at that edge's middle, spread
// apart from the node's other automatic ports on the same edge.
enum class PortSide : std::uint8_t { left, right, top, bottom, automatic };

// A port's status lamp (owner ruling, 2026-11-08): off, on (green in the
// theme) or error (red).
enum class PortLamp : std::uint8_t { off, on, error };

// A port: its name (shown and announced), direction, edge, its position
// along a fixed edge as a fraction from the edge's start (left to right,
// top to bottom; ignored for an automatic port), its lamp and its socket
// type (the consumer's number; type conversion arrives with N0's tables).
// An input takes one link; an output any number.
struct GUI_FORMS_API PortRecord final {
    std::string name{};
    PortDirection direction{PortDirection::input};
    PortSide side{PortSide::left};
    double offset{0.5};
    PortLamp lamp{PortLamp::off};
    std::uint16_t socket_type{};
};

// What add_node takes: the node's title, its bounds in world units and its
// ports, copied into the node's record.
struct GUI_FORMS_API NodeSpec final {
    std::string title{};
    WorldRect bounds{};
    std::vector<PortRecord> ports{};
};

// A node slot. `live` is false for a free slot, whose fields are stale.
struct GUI_FORMS_API NodeRecord final {
    std::string title{};
    WorldRect bounds{};
    std::vector<PortRecord> ports{};
    std::uint32_t generation{};
    bool live{};
};

// A link slot, as NodeRecord.
struct GUI_FORMS_API LinkRecord final {
    PortRef from{};
    PortRef to{};
    std::uint32_t generation{};
    bool live{};
};

// -- results ----------------------------------------------------------------------

enum class GraphStatus : std::uint8_t {
    ok,
    // An identity whose record was removed (or that never existed).
    stale_identity,
    // A port index beyond its node's ports.
    bad_port,
    // connect's `from` is not an output or its `to` is not an input.
    wrong_direction,
    // connect's input already has a link.
    occupied,
    // connect between two ports of one node.
    same_node,
    // A non-finite or negative size, a non-finite position or offset, or an
    // empty span where one is required.
    invalid_argument,
    // More than maximum_node_ports ports, or the slot index range is full.
    capacity,
};

// An operation's result: the status and, for add_node and connect, the new
// record's identity (zero otherwise).
struct GUI_FORMS_API GraphResult final {
    GraphStatus status{GraphStatus::ok};
    NodeId node{};
    LinkId link{};
};

[[nodiscard]] GUI_FORMS_API std::string_view graph_status_name(GraphStatus status) noexcept;

inline constexpr std::size_t maximum_node_ports = 256U;

// -- the graph --------------------------------------------------------------------

// Owned by its consumer and borrowed by the canvases that draw it, on one
// thread (the consumer's UI thread). Not copyable: identities name slots of
// one graph.
class GUI_FORMS_API NodeGraph final {
public:
    NodeGraph() = default;
    NodeGraph(const NodeGraph&) = delete;
    NodeGraph& operator=(const NodeGraph&) = delete;

    // Reserves slot storage so that loading `nodes` nodes and `links` links
    // reallocates nothing (ports and titles are each node's own storage).
    void reserve(std::size_t nodes, std::size_t links);

    [[nodiscard]] GraphResult add_node(const NodeSpec& spec);
    // Removes the node and every link to or from it.
    [[nodiscard]] GraphResult remove_node(NodeId node);
    // Moves every node in `nodes` by `delta`. All identities are checked
    // first; one stale identity refuses the whole move. A node listed twice
    // moves once.
    [[nodiscard]] GraphResult move_nodes(std::span<const NodeId> nodes, WorldPoint delta);
    // Gives the node new bounds (a resize or a placement).
    [[nodiscard]] GraphResult set_node_bounds(NodeId node, WorldRect bounds);
    [[nodiscard]] GraphResult set_title(NodeId node, std::string_view title);
    [[nodiscard]] GraphResult set_port_lamp(PortRef port, PortLamp lamp);
    // Links `from` (an output) to `to` (an input of another node, which has
    // no link yet).
    [[nodiscard]] GraphResult connect(PortRef from, PortRef to);
    [[nodiscard]] GraphResult disconnect(LinkId link);

    // The record of a live identity, or null for a stale one. Borrowed until
    // the next operation.
    [[nodiscard]] const NodeRecord* node(NodeId node) const noexcept;
    [[nodiscard]] const LinkRecord* link(LinkId link) const noexcept;
    [[nodiscard]] const PortRecord* port(PortRef port) const noexcept;
    // The link into an input, or a stale identity when it has none.
    [[nodiscard]] LinkId link_into(PortRef input) const noexcept;

    // Every slot, live or free, in slot order (the order the canvas paints
    // and the router routes in); an identity is {index, record.generation}.
    [[nodiscard]] std::span<const NodeRecord> node_slots() const& noexcept { return nodes_; }
    std::span<const NodeRecord> node_slots() const&& = delete;
    [[nodiscard]] std::span<const LinkRecord> link_slots() const& noexcept { return links_; }
    std::span<const LinkRecord> link_slots() const&& = delete;
    [[nodiscard]] std::size_t node_count() const noexcept { return live_nodes_; }
    [[nodiscard]] std::size_t link_count() const noexcept { return live_links_; }
    // Advances on every successful operation; never zero.
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

private:
    [[nodiscard]] bool live(NodeId node) const noexcept;
    [[nodiscard]] bool live(LinkId link) const noexcept;
    void advance() noexcept;
    void free_link(std::uint32_t index) noexcept;

    std::vector<NodeRecord> nodes_{};
    std::vector<LinkRecord> links_{};
    // Free slots, reused last freed first.
    std::vector<std::uint32_t> free_nodes_{};
    std::vector<std::uint32_t> free_links_{};
    // Per node slot: the revision of the last move that moved it.
    std::vector<std::uint64_t> move_marks_{};
    std::size_t live_nodes_{};
    std::size_t live_links_{};
    std::uint64_t revision_{1U};
};

// Where a port sits on its node's edge `side` (left, right, top or bottom;
// a fixed port's own side and offset, or, for an automatic port, `side` with
// `offset`), in world units, on the node's boundary.
[[nodiscard]] GUI_FORMS_API WorldPoint port_anchor(const NodeRecord& node, PortSide side, double offset) noexcept;

} // namespace gui_forms
