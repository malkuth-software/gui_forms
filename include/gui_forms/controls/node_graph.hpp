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
// Geometry is in world units (WorldPoint, WorldRect, declared in
// gui_forms/scene/world_geometry), float64, so deep zoom keeps placement;
// the canvas converts to view units once per frame.
//
// Mutation is through the named operations only. Each validates every
// argument before changing anything, so a refused operation leaves the
// graph exactly as it was, and each successful one advances `revision`,
// which the canvas and the link router read to know their caches are stale.
// set_port_lamp is the exception: lamps are evaluation state, so a lamp
// change advances `lamp_revision` instead and leaves `revision` (and every
// cache keyed on the structure) alone.
//
// Undo (node graph spec section 5, question 3 option (a)): every document
// operation (add_node, remove_node, move_nodes, set_node_bounds, set_title,
// set_property, remove_property, connect, disconnect) appends one record
// holding what its inverse needs; a step is one operation, or every
// operation between begin_undo_group and the matching end_undo_group (one
// gesture, one edit commit). rollback_undo_group cancels an open group
// instead (Escape during a drag), restoring the state at its begin and
// leaving no step. undo and redo restore exact state: the same
// slots, generations, free lists and field bytes, so undoing to the start
// saves the loaded document byte for byte. The log is bounded (UndoLimits:
// 10,000 steps and 64 MiB by default, oldest dropped). set_port_lamp is
// evaluation state, not document state: it is neither recorded nor saved.
//
// Documents (save and load): the versioned text format described above
// NodeGraph::save, with stable text keys for nodes and stable text IDs for
// links. load validates the whole document before changing anything and
// names the byte, line and column of the first error. It tolerates harmless
// spelling variants and save writes the canonical form, so for every
// document save wrote, load then save reproduces it byte for byte.
//
// Nothing here recurses: graphs are runtime-shaped data, and every walk is a
// loop over the slot arrays. Socket-type conversion and frames arrive with
// the rest of phase N0.

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/scene/world_geometry/world_geometry.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

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

// A node's named value: a parameter its consumer gives meaning to (a kind's
// "scale" = "5.0"). The graph keeps it as text and never interprets it, so
// a document keeps properties it does not know verbatim. A name is 1 to 64
// bytes of ASCII letters, digits, '_', '.' and '-', starts with a letter
// and is none of the document's field words (kind, title, bounds, port).
struct GUI_FORMS_API NodeProperty final {
    std::string name{};
    std::string value{};
};

// What add_node takes, copied into the node's record:
// - key: the node's stable text identity, unique among the live nodes and
//   kept by documents (1 to 64 bytes of ASCII letters, digits, '_', '.' and
//   '-'); runtime identities (NodeId) are assigned per graph and never saved;
// - kind: the consumer's kind name ("noise_texture"), opaque to the graph;
// - title, bounds in world units, ports, and properties in order.
// Each text (kind, title, a port name, a property value) is at most
// maximum_graph_text_bytes bytes.
struct GUI_FORMS_API NodeSpec final {
    std::string key{};
    std::string kind{};
    std::string title{};
    WorldRect bounds{};
    std::vector<PortRecord> ports{};
    std::vector<NodeProperty> properties{};
};

// A node slot. `live` is false for a free slot, whose fields are stale.
struct GUI_FORMS_API NodeRecord final {
    std::string key{};
    std::string kind{};
    std::string title{};
    WorldRect bounds{};
    std::vector<PortRecord> ports{};
    std::vector<NodeProperty> properties{};
    std::uint32_t generation{};
    bool live{};
};

// A link slot, as NodeRecord.
// `key` is the link's stable text ID (the node key rules), unique among the
// live links, kept by documents and by undo, so a link keeps its ID when
// undo or redo reattaches it.
struct GUI_FORMS_API LinkRecord final {
    std::string key{};
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
    // More than maximum_node_ports ports or maximum_node_properties
    // properties, or the slot index range is full.
    capacity,
    // add_node's key names a live node already, or connect's ID a live link.
    duplicate_key,
    // remove_property's name is not one of the node's properties.
    missing_property,
    // undo with no step to undo, or redo with none to redo.
    nothing_to_undo,
    nothing_to_redo,
    // undo, redo or clear_undo while an undo group is open.
    group_open,
    // end_undo_group with no group open.
    no_group,
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
inline constexpr std::size_t maximum_node_properties = 256U;
// A node key or property name, in bytes.
inline constexpr std::size_t maximum_graph_key_bytes = 64U;
// A kind, title, port name or property value, in bytes.
inline constexpr std::size_t maximum_graph_text_bytes = 65536U;

// -- undo -------------------------------------------------------------------------

// The undo log's bound. When a new step passes either limit, the oldest
// steps are dropped until both hold again, except that the newest step is
// never dropped: one step larger than maximum_bytes empties the rest of the
// log and stays. maximum_steps zero turns recording off (and empties the
// log). Bytes are an estimate: each record's fixed size plus the bytes of the
// texts, ports and positions it keeps.
struct GUI_FORMS_API UndoLimits final {
    std::size_t maximum_steps{10000U};
    std::size_t maximum_bytes{64U * 1024U * 1024U};
};

// -- documents --------------------------------------------------------------------

// The document format version save writes and load accepts.
inline constexpr std::uint32_t graph_document_version = 1U;
// load's bounds on hostile input: nodes and links in one document (ports,
// properties and texts are bounded per node as above).
inline constexpr std::size_t maximum_document_nodes = 1048576U;
inline constexpr std::size_t maximum_document_links = 4194304U;

enum class GraphSaveStatus : std::uint8_t {
    ok,
    // The storage is smaller than the document; nothing was written.
    storage_too_small,
};

// save's result: the status, the bytes written (zero unless ok) and the
// document's size in bytes (always set, so save with empty storage measures).
struct GUI_FORMS_API GraphSaveResult final {
    GraphSaveStatus status{GraphSaveStatus::ok};
    std::size_t written{};
    std::size_t required{};
};

// Why load refused a document. Every refusal leaves the graph and its undo
// log exactly as they were.
enum class GraphLoadError : std::uint8_t {
    ok,
    // load while an undo group is open.
    undo_group_open,
    // The first line is not "gui-forms-graph <version>;".
    bad_header,
    // A version other than graph_document_version.
    unsupported_version,
    // The input ends before the document does (every prefix of a document
    // that ends before the semicolon of "end;" is refused with this, at the
    // input's end).
    truncated,
    // A byte outside a quoted text or comment that the format never uses
    // there (a control byte other than tab and line end, a carriage return
    // not before a line feed, a byte above 0x7E).
    unexpected_character,
    // The line does not have the format's shape at this byte (a missing
    // colon, semicolon, brace or arrow, a field out of order or repeated, a
    // second statement on one line, data after "end;").
    syntax,
    // A document-level line that is not node, link or end, or a node after
    // the first link.
    unknown_statement,
    // A node key or link ID that is empty, too long or has a byte keys
    // never use.
    bad_key,
    // Not a number, not finite, or outside its field's range (a negative
    // size, an offset outside 0 to 1, a socket type or port above 65535).
    bad_number,
    // A quoted text with an unknown escape or a raw control byte.
    bad_text,
    // A direction or side that is not one of the format's words.
    bad_word,
    // A property name that is malformed, reserved, or repeated in its node.
    bad_property,
    // A node key already used by an earlier node, or a link ID by an
    // earlier link.
    duplicate_key,
    // A link names a key no node has.
    unknown_node,
    // A link's port index is beyond its node's ports.
    bad_port,
    // A link that does not run from an output to an input.
    wrong_direction,
    // A link between two ports of one node.
    same_node,
    // A second link into one input.
    occupied,
    // More nodes, links, ports, properties or text bytes than the bounds.
    limit,
};

// load's result: the error and, when it is not ok, its byte offset, and its
// line and column (both from 1, the column counted in bytes).
struct GUI_FORMS_API GraphLoadResult final {
    GraphLoadError error{GraphLoadError::ok};
    std::size_t offset{};
    std::size_t line{};
    std::size_t column{};
};

[[nodiscard]] GUI_FORMS_API std::string_view graph_load_error_name(GraphLoadError error) noexcept;

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

    // Every recorded operation below may also throw std::bad_alloc while it
    // makes room in the undo log, after validation and before it changes the
    // graph: the graph is then unchanged, though the redo steps may have been
    // discarded.
    [[nodiscard]] GraphResult add_node(const NodeSpec& spec);
    // Removes the node and every link to or from it.
    [[nodiscard]] GraphResult remove_node(NodeId node);
    // Moves every node in `nodes` by `delta`. All identities are checked
    // first; one stale identity refuses the whole move. A node listed twice
    // moves once. Inside an undo group, a move of the same identities in the
    // same order as the group's previous record joins that record (a drag's
    // frames), so a drag records once and allocates nothing after its first
    // frame.
    [[nodiscard]] GraphResult move_nodes(std::span<const NodeId> nodes, WorldPoint delta);
    // Gives the node new bounds (a resize or a placement).
    [[nodiscard]] GraphResult set_node_bounds(NodeId node, WorldRect bounds);
    [[nodiscard]] GraphResult set_title(NodeId node, std::string_view title);
    // Sets the named property's value: in place when the node has it,
    // appended after its other properties otherwise.
    [[nodiscard]] GraphResult set_property(NodeId node, std::string_view name, std::string_view value);
    [[nodiscard]] GraphResult remove_property(NodeId node, std::string_view name);
    // Not recorded for undo and not saved (evaluation state). A change
    // advances lamp_revision, not revision; writing the lamp a port already
    // shows changes neither.
    [[nodiscard]] GraphResult set_port_lamp(PortRef port, PortLamp lamp);
    // Links `from` (an output) to `to` (an input of another node, which has
    // no link yet). The first form names the link "l.<n>" from a counter
    // that only increases (past every "l.<n>" a load read), skipping IDs in
    // use, so no ID is handed out twice in a graph's life; the second takes
    // the caller's ID.
    [[nodiscard]] GraphResult connect(PortRef from, PortRef to);
    [[nodiscard]] GraphResult connect(PortRef from, PortRef to, std::string_view key);
    [[nodiscard]] GraphResult disconnect(LinkId link);

    // -- undo ---------------------------------------------------------------------
    // undo reverts the newest applied step and redo reapplies the oldest
    // reverted one; recording a new operation discards every reverted step.
    // Each advances the revision once (never back to an older number, so
    // caches see every change). An identity that undo made stale is live
    // again after redo: undo and redo restore exact identities. Neither
    // allocates: every record keeps the storage its inverse needs.
    [[nodiscard]] GraphResult undo();
    [[nodiscard]] GraphResult redo();
    // Groups nest; the operations between the outermost begin and its end
    // form one step (an empty group forms none). undo, redo, clear_undo and
    // load are refused while a group is open.
    void begin_undo_group() noexcept;
    [[nodiscard]] GraphResult end_undo_group() noexcept;
    // Cancels the open group: reverts every operation recorded since the
    // outermost begin_undo_group, newest first and exactly as undo does
    // (same slots, generations, free lists, IDs and bytes), removes the
    // group's step so neither undo nor redo sees it, and closes every open
    // level, inner ones included (a later end_undo_group returns no_group).
    // Advances the revision once when it reverted anything. The redo steps
    // that existed before the group were discarded by its first operation,
    // as by any new operation, and stay discarded. Lamps are not recorded,
    // so lamp writes inside the group stay. no_group when no group is open;
    // nothing_to_undo when recording is off (maximum_steps 0): the group is
    // closed and its operations stay. Never allocates.
    [[nodiscard]] GraphResult rollback_undo_group() noexcept;
    // Drops every step.
    [[nodiscard]] GraphResult clear_undo() noexcept;
    // Discards the redo steps, applies the new bound by dropping the oldest
    // steps, and reserves the step table (at most 2 * maximum_steps + 1
    // entries: dropped steps are compacted away once they are half the table).
    void set_undo_limits(UndoLimits limits);
    [[nodiscard]] UndoLimits undo_limits() const noexcept { return undo_limits_; }
    [[nodiscard]] std::size_t undo_steps() const noexcept { return applied_steps_ - first_step_; }
    [[nodiscard]] std::size_t redo_steps() const noexcept { return steps_.size() - applied_steps_; }
    [[nodiscard]] std::size_t undo_bytes() const noexcept { return undo_bytes_; }

    // -- documents ----------------------------------------------------------------
    // The document, version 1, as save writes it (the canonical form): ASCII
    // outside quoted texts, one statement or field per line, every line
    // ending in "\n", fields indented four spaces:
    //
    //     gui-forms-graph 1;
    //     node noise {
    //         kind: "noise_texture";
    //         title: "Noise";
    //         bounds: 120 40 100 60;
    //         port: output right 0.5 3 "color";
    //         scale: "5.0";
    //     }
    //     node mix {
    //         kind: "mix_color";
    //         title: "Mix";
    //         bounds: 360 60 100 60;
    //         port: input left 0.5 3 "a";
    //     }
    //     link l.1 noise:0 -> mix:0;
    //     end;
    //
    // - Nodes in slot order, then links in slot order, then "end;", which
    //   makes every truncation detectable. Runtime identities are not saved:
    //   a node is named by its key, a link by its ID, and a link's ends by
    //   "<node key>:<port index>".
    // - In a node: kind, title, bounds (x y width height), then its ports in
    //   order (direction, side, offset, socket type, name), then its
    //   properties in order. Lamps are evaluation state and are not saved.
    // - Numbers are the shortest decimal that reads back to the same double
    //   (std::to_chars), integers plain decimal.
    // - Texts are quoted; the escapes are \\, \", \n, \t, \r, and \xHH
    //   (lowercase hex) for every other byte below 0x20 and for 0x7F. Bytes
    //   from 0x80 are written as they are (UTF-8 is not checked).
    //
    // What load also accepts (all normalised by the next save): spaces and
    // tabs anywhere between tokens and at line starts and ends; blank lines;
    // comments, from "#" outside a text to the end of its line (a whole line
    // or after a statement); CRLF line ends; no line end after "end;";
    // numbers in any std::from_chars spelling ("60.0", "6e1", "007"); \xHH
    // in either case for any byte. What stays fixed, and is refused with
    // its position: one field or statement per line, every keyword,
    // colon, semicolon, brace and arrow, kind, title and bounds first and in
    // that order, ports before properties.
    //
    // The guarantee: for every document save wrote, load then save
    // reproduces it byte for byte, and undoing every operation since a load
    // saves the loaded document's canonical bytes.
    //
    // Unknown fields: a node line `<name>: "<text>";` whose name is not a
    // field word is a property, kept, in order, and written back, so a later
    // version's quoted node fields survive a version 1 edit. Any other
    // unknown line is refused: in a node with the error of its first wrong
    // byte, at document level with unknown_statement. A later version that
    // changes meaning (for example a move to the form syntax, with a
    // converter) raises graph_document_version.

    // Writes the document into `storage`. Measures first: storage shorter
    // than the document is refused with storage_too_small and left
    // untouched, and `required` is set either way. Never allocates.
    [[nodiscard]] GraphSaveResult save(std::span<char> storage) const;
    // Replaces the graph's contents with the document's. The whole document
    // is read and checked first (syntax, bounds, keys, links) into work
    // storage bounded by the document; only then is the graph changed, with
    // nothing left that can fail. Nodes take slots 0, 1, ... in document
    // order with a generation newer than any the slot had, so no identity
    // from before the load is live after it. The undo log is emptied: the
    // loaded document is the start. One revision advance. May throw
    // std::bad_alloc while building the work storage, before any change.
    [[nodiscard]] GraphLoadResult load(std::string_view document);

    // The record of a live identity, or null for a stale one. Borrowed until
    // the next operation.
    [[nodiscard]] const NodeRecord* node(NodeId node) const noexcept;
    // The live node with this key, or a stale identity (O(log n)).
    [[nodiscard]] NodeId find_node(std::string_view key) const noexcept;
    // The live link with this ID, or a stale identity (O(log n)).
    [[nodiscard]] LinkId find_link(std::string_view key) const noexcept;
    // The node's named property, or null. Borrowed until the next operation.
    [[nodiscard]] const NodeProperty* property(NodeId node, std::string_view name) const noexcept;
    [[nodiscard]] const LinkRecord* link(LinkId link) const noexcept;
    [[nodiscard]] const PortRecord* port(PortRef port) const noexcept;
    // The link into an input, or a stale identity when it has none.
    [[nodiscard]] LinkId link_into(PortRef input) const noexcept;

    // Every slot, live or free, in slot order (the order the canvas paints
    // and the router routes in); an identity is {index, record.generation}.
    [[nodiscard]] std::span<const NodeRecord> node_slots() const& noexcept {
        return std::span<const NodeRecord>(nodes_.data(), nodes_.size());
    }
    std::span<const NodeRecord> node_slots() const&& = delete;
    [[nodiscard]] std::span<const LinkRecord> link_slots() const& noexcept {
        return std::span<const LinkRecord>(links_.data(), links_.size());
    }
    std::span<const LinkRecord> link_slots() const&& = delete;
    [[nodiscard]] std::size_t node_count() const noexcept { return live_nodes_; }
    [[nodiscard]] std::size_t link_count() const noexcept { return live_links_; }
    // Advances on every successful operation except set_port_lamp (and on
    // undo, redo, load and a rollback that reverted something); never zero.
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
    // Advances on every set_port_lamp that changes a lamp; never zero.
    // Lamps also change with the structure (load turns every lamp off; an
    // undo can bring back a node with its lamps), so a painter of lamps
    // watches both counters.
    [[nodiscard]] std::uint64_t lamp_revision() const noexcept { return lamp_revision_; }

private:
    enum class UndoOperation : std::uint8_t {
        add_node,
        remove_node,
        move_nodes,
        set_node_bounds,
        set_title,
        set_property,
        remove_property,
        connect,
        disconnect,
    };
    // One recorded operation. The *_first fields index the payload arrays
    // below; each is that array's size when the record was made, so they
    // never decrease along the log.
    struct UndoRecord final {
        UndoOperation operation{UndoOperation::add_node};
        NodeId node{};
        LinkId link{};
        // add_node and connect: the reused slot's generation before, and
        // whether the slot was appended instead.
        std::uint32_t generation_before{};
        bool appended{};
        // set_property: whether it replaced a value (else it appended).
        bool replaced{};
        // set_property and remove_property: the property's position.
        std::uint32_t position{};
        WorldRect bounds_before{};
        WorldRect bounds_after{};
        std::size_t node_first{};
        std::size_t link_first{};
        std::size_t link_count{};
        std::size_t move_first{};
        std::size_t move_count{};
        std::size_t text_first{};
    };
    // A step: records [first_record, first_record + record_count).
    struct UndoStep final {
        std::size_t first_record{};
        std::size_t record_count{};
        std::size_t bytes{};
    };
    // A link record kept in the log (with its ID): one remove_node took with
    // it, one disconnect removed, or, while undone, one connect made.
    struct RemovedLink final {
        std::uint32_t index{};
        LinkRecord record{};
    };
    // One node of a move: where it was and where the move left it.
    struct MovedNode final {
        NodeId node{};
        WorldPoint before{};
        WorldPoint after{};
    };

    [[nodiscard]] bool live(NodeId node) const noexcept;
    [[nodiscard]] bool live(LinkId link) const noexcept;
    void advance() noexcept;
    [[nodiscard]] GraphStatus check_link(PortRef from, PortRef to) const noexcept;
    [[nodiscard]] GraphResult install_link(PortRef from, PortRef to, std::string& key);
    [[nodiscard]] bool recording() const noexcept { return undo_limits_.maximum_steps > 0U; }
    [[nodiscard]] bool group_step_open() const noexcept { return group_depth_ > 0U && group_step_open_; }
    void prepare_record(std::size_t nodes, std::size_t links, std::size_t moves, std::size_t texts);
    void commit_record(const UndoRecord& record, std::size_t bytes) noexcept;
    void discard_redo_steps() noexcept;
    void trim_steps() noexcept;
    void compact_steps() noexcept;
    void clear_log() noexcept;
    void revert(const UndoRecord& record) noexcept;
    void reapply(const UndoRecord& record) noexcept;
    void detach_node(std::uint32_t index, NodeRecord& keep) noexcept;
    void attach_node(std::uint32_t index, NodeRecord& kept) noexcept;
    void detach_link(std::uint32_t index, LinkRecord& keep) noexcept;
    void attach_link(std::uint32_t index, LinkRecord& kept) noexcept;

    std::vector<NodeRecord> nodes_{};
    std::vector<LinkRecord> links_{};
    // Free slots, reused last freed first.
    std::vector<std::uint32_t> free_nodes_{};
    std::vector<std::uint32_t> free_links_{};
    // Per node slot: the revision of the last move that moved it.
    std::vector<std::uint64_t> move_marks_{};
    // Live node slots ordered by key, for find_node and duplicate checks.
    // Its capacity grows with the slots, so undo and redo never allocate.
    std::vector<std::uint32_t> key_order_{};
    // Live link slots ordered by ID, as key_order_.
    std::vector<std::uint32_t> link_key_order_{};
    // The next automatic link ID's number ("l.<n>"); never decreases.
    std::uint64_t next_link_number_{1U};
    std::size_t live_nodes_{};
    std::size_t live_links_{};
    std::uint64_t revision_{1U};
    std::uint64_t lamp_revision_{1U};

    // The undo log. Steps [first_step_, applied_steps_) can be undone and
    // [applied_steps_, size) redone; steps before first_step_ were dropped
    // by the bound and await compaction.
    std::vector<UndoStep> steps_{};
    std::vector<UndoRecord> records_{};
    // Payloads, swapped between the graph and the log by undo and redo:
    // whole node records (add_node, remove_node), removed links, moved
    // nodes, and texts (a title, a property's value, a removed property).
    std::vector<NodeRecord> node_payload_{};
    std::vector<RemovedLink> link_payload_{};
    std::vector<MovedNode> move_payload_{};
    std::vector<NodeProperty> text_payload_{};
    std::size_t first_step_{};
    std::size_t applied_steps_{};
    std::size_t undo_bytes_{};
    std::size_t group_depth_{};
    // Whether the open group has made its step yet.
    bool group_step_open_{};
    UndoLimits undo_limits_{};
};

// Where a port sits on its node's edge `side` (left, right, top or bottom;
// a fixed port's own side and offset, or, for an automatic port, `side` with
// `offset`), in world units, on the node's boundary.
[[nodiscard]] GUI_FORMS_API WorldPoint port_anchor(const NodeRecord& node, PortSide side, double offset) noexcept;

} // namespace gui_forms
