// The node graph's undo log and documents (node graph spec section 5 and
// workload 8.8): every recorded operation's inverse restores the saved bytes
// and the identities; grouping makes one step of a gesture, and a drag's
// frames join one record; the bound drops the oldest steps; 1,000 seeded
// edits undone to the start save the loaded document byte for byte; load then
// save is the identity; malformed input is refused with its exact error and
// position and leaves the graph unchanged; every proper prefix of a document
// is refused as truncated; long chains and cycles go through undo, save and
// load without recursion. Headless and renderer-free.

#include "gui_forms/controls/node_graph.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "node-graph-history-tests: " << message << '\n';
    std::exit(1);
}

// The graph's document as a string (measure, then write).
std::string saved(const NodeGraph& graph) {
    const GraphSaveResult measured = graph.save(std::span<char>());
    require(measured.status == GraphSaveStatus::storage_too_small || measured.required == 0U,
            "empty storage measures");
    std::string text(measured.required, '\0');
    const GraphSaveResult written = graph.save(std::span<char>(text.data(), text.size()));
    require(written.status == GraphSaveStatus::ok && written.written == measured.required,
            "save writes the measured size");
    return text;
}

PortRecord port_of(const char* name, PortDirection direction, PortSide side, double offset, std::uint16_t socket) {
    PortRecord port{};
    port.name = name;
    port.direction = direction;
    port.side = side;
    port.offset = offset;
    port.socket_type = socket;
    return port;
}

// A node with one input (port 0) on the left and one output (port 1) on the
// right.
NodeSpec spec_of(const std::string& key, double x, double y) {
    NodeSpec spec{};
    spec.key = key;
    spec.kind = "relay";
    spec.title = key;
    spec.bounds = WorldRect{x, y, 100.0, 60.0};
    spec.ports.push_back(port_of("in", PortDirection::input, PortSide::left, 0.5, 1U));
    spec.ports.push_back(port_of("out", PortDirection::output, PortSide::right, 0.5, 1U));
    return spec;
}

NodeId add(NodeGraph& graph, const std::string& key, double x, double y) {
    const GraphResult added = graph.add_node(spec_of(key, x, y));
    require(added.status == GraphStatus::ok, "a fixture node is added");
    return added.node;
}

LinkId join(NodeGraph& graph, NodeId from, NodeId to) {
    const GraphResult joined = graph.connect(PortRef{from, PortIndex{1U}}, PortRef{to, PortIndex{0U}});
    require(joined.status == GraphStatus::ok, "a fixture link is made");
    return joined.link;
}

// The document of the header's sample.
constexpr std::string_view sample_document = "gui-forms-graph 1;\n"
                                             "node noise {\n"
                                             "    kind: \"noise_texture\";\n"
                                             "    title: \"Noise\";\n"
                                             "    bounds: 120 40 100 60;\n"
                                             "    port: output right 0.5 3 \"color\";\n"
                                             "    scale: \"5.0\";\n"
                                             "}\n"
                                             "node mix {\n"
                                             "    kind: \"mix_color\";\n"
                                             "    title: \"Mix\";\n"
                                             "    bounds: 360 60 100 60;\n"
                                             "    port: input left 0.5 3 \"a\";\n"
                                             "}\n"
                                             "link l.1 noise:0 -> mix:0;\n"
                                             "end;\n";

// A document using every kind of text and number the format writes:
// escapes, a \xHH control byte, raw UTF-8, an empty kind, a property a
// later version might add, negative zero, exponents, a fraction, a
// top port with no name, and an automatic port.
constexpr std::string_view rich_document = "gui-forms-graph 1;\n"
                                           "node a.1 {\n"
                                           "    kind: \"\";\n"
                                           "    title: \"say \\\"hi\\\"\\n\\t\\\\ \\x01\\x7f caf\xc3\xa9\";\n"
                                           "    bounds: -0 1e+21 0.125 0;\n"
                                           "    port: output top 0 0 \"\";\n"
                                           "    port: output automatic 1 65535 \"x\";\n"
                                           "    future_field: \"kept verbatim\";\n"
                                           "    zeta: \"\\r\";\n"
                                           "}\n"
                                           "node b_2 {\n"
                                           "    kind: \"k\";\n"
                                           "    title: \"B\";\n"
                                           "    bounds: -12.5 3.0000000000000004 7 8;\n"
                                           "    port: input bottom 0.3333333333333333 2 \"in\";\n"
                                           "    port: input left 0.5 2 \"in\";\n"
                                           "}\n"
                                           "link wire-a a.1:1 -> b_2:1;\n"
                                           "link l.7 a.1:0 -> b_2:0;\n"
                                           "end;\n";

// The sample with every tolerated variant: comments (whole lines, after
// statements, containing quotes and "end;"), blank lines, tabs, extra and
// missing spaces, CRLF line ends, non-canonical numbers, \xHH escapes in
// both cases, and comments after "end;". Its last line is not a comment, so
// the document up to that line loads (to the sample's graph) and the whole
// is refused.
constexpr std::string_view variant_document = "# a node graph\r\n"
                                              "\n"
                                              "gui-forms-graph   01 ;  # version one\n"
                                              "node\tnoise{\n"
                                              "  kind:\"noise_texture\";\r\n"
                                              "\t\ttitle : \"\\x4Eo\\x69se\" ;\n"
                                              "\n"
                                              "    # \"quoted\" end; in a comment\n"
                                              "    bounds: 1.2e2 40.000 100 6e1;\n"
                                              "    port: output   right .5e0 003 \"color\";\n"
                                              "scale:\"5.0\";\n"
                                              "}  # noise\n"
                                              "node mix {\n"
                                              "    kind: \"mix_color\";\n"
                                              "    title: \"Mix\";\n"
                                              "    bounds: 360 60 100 60;\n"
                                              "    port: input left 0.50 3 \"a\";\n"
                                              "}\n"
                                              "\n"
                                              "link l.1 noise : 0 ->mix:0;\n"
                                              "end;  # done\r\n"
                                              "# trailing comment\n"
                                              "\n"
                                              "end of nothing? no: this line is a comment only if it starts with #\n";

void tolerant_load() {
    // The last line of variant_document is not a comment: refused after end.
    NodeGraph graph;
    const GraphLoadResult refused = graph.load(variant_document);
    const std::size_t extra = variant_document.find("end of nothing");
    require(refused.error == GraphLoadError::syntax && refused.offset == extra,
            "a line after end that is not blank or a comment is refused");
    const std::string_view variant = variant_document.substr(0U, extra);
    const GraphLoadResult result = graph.load(variant);
    if (result.error != GraphLoadError::ok) {
        std::cerr << "node-graph-history-tests: variant refused: " << graph_load_error_name(result.error) << " at "
                  << result.line << ':' << result.column << '\n';
    }
    require(result.error == GraphLoadError::ok, "every tolerated variant loads");
    require(saved(graph) == sample_document, "and saves the canonical sample");
    const std::string_view no_line_end = sample_document.substr(0U, sample_document.size() - 1U);
    require(graph.load(no_line_end).error == GraphLoadError::ok && saved(graph) == sample_document,
            "the last line may lack its line end");
    std::string crlf{};
    for (const char byte : sample_document) {
        if (byte == '\n') crlf.push_back('\r');
        crlf.push_back(byte);
    }
    require(graph.load(crlf).error == GraphLoadError::ok && saved(graph) == sample_document, "CRLF loads");
}

void link_ids() {
    NodeGraph graph;
    const NodeId a = add(graph, "a", 0.0, 0.0);
    const NodeId b = add(graph, "b", 200.0, 0.0);
    const NodeId c = add(graph, "c", 400.0, 0.0);
    const LinkId ab = join(graph, a, b);
    require((*graph.link(ab)).key == "l.1" && graph.find_link("l.1") == ab, "an automatic ID");
    const GraphResult named = graph.connect(PortRef{b, PortIndex{1U}}, PortRef{c, PortIndex{0U}}, "feed");
    require(named.status == GraphStatus::ok && graph.find_link("feed") == named.link, "a chosen ID");
    require(graph.disconnect(named.link).status == GraphStatus::ok && !(graph.find_link("feed") == named.link),
            "a disconnected link's ID is free");
    require(graph.connect(PortRef{a, PortIndex{1U}}, PortRef{c, PortIndex{0U}}, "l.1").status ==
                GraphStatus::duplicate_key,
            "a live ID is refused");
    require(graph.connect(PortRef{a, PortIndex{1U}}, PortRef{c, PortIndex{0U}}, "bad id").status ==
                GraphStatus::invalid_argument,
            "a malformed ID is refused");
    require(graph.undo().status == GraphStatus::ok && graph.find_link("feed") == named.link,
            "undo reattaches the link with its ID");
    require(graph.redo().status == GraphStatus::ok && graph.undo().status == GraphStatus::ok, "and redo detaches it");
    require(graph.remove_node(b).status == GraphStatus::ok && graph.link_count() == 0U, "b takes both links");
    require(graph.undo().status == GraphStatus::ok && graph.find_link("l.1") == ab &&
                graph.find_link("feed") == named.link,
            "undoing the removal keeps both IDs");
    const NodeId d = add(graph, "d", 600.0, 0.0);
    const GraphResult second = graph.connect(PortRef{c, PortIndex{1U}}, PortRef{d, PortIndex{0U}});
    require(second.status == GraphStatus::ok && (*graph.link(second.link)).key == "l.2",
            "automatic IDs count on");

    NodeGraph loaded;
    require(loaded.load(rich_document).error == GraphLoadError::ok, "the rich document loads");
    require(loaded.link(loaded.find_link("wire-a")) != nullptr && loaded.link(loaded.find_link("l.7")) != nullptr,
            "loaded links keep their IDs");
    const NodeId a1 = loaded.find_node("a.1");
    const NodeId extra = loaded.add_node(spec_of("extra", 0.0, 0.0)).node;
    const GraphResult next = loaded.connect(PortRef{a1, PortIndex{1U}}, PortRef{extra, PortIndex{0U}});
    require(next.status == GraphStatus::ok && (*loaded.link(next.link)).key == "l.8", "the automatic counter starts past every loaded l.<n>");
}

// Loaded IDs out of order, with gaps and leading zeros: "l.012" counts as
// 12, and IDs that only look automatic ("l.", "l.x3", "l.-4") do not count.
void gapped_link_ids() {
    std::string document(sample_document);
    const std::string_view one_port = "    port: input left 0.5 3 \"a\";\n";
    document.replace(document.find(one_port), one_port.size(),
                     "    port: input left 0.5 3 \"a\";\n"
                     "    port: input left 0.5 3 \"b\";\n"
                     "    port: input left 0.5 3 \"c\";\n"
                     "    port: input left 0.5 3 \"d\";\n"
                     "    port: input left 0.5 3 \"e\";\n");
    const std::string_view one_link = "link l.1 noise:0 -> mix:0;\n";
    document.replace(document.find(one_link), one_link.size(),
                     "link l.3 noise:0 -> mix:0;\n"
                     "link l.012 noise:0 -> mix:1;\n"
                     "link l. noise:0 -> mix:2;\n"
                     "link l.x3 noise:0 -> mix:3;\n"
                     "link l.-4 noise:0 -> mix:4;\n");
    NodeGraph graph;
    require(graph.load(document).error == GraphLoadError::ok, "out-of-order, gapped and padded IDs load");
    require(graph.link(graph.find_link("l.012")) != nullptr && graph.link(graph.find_link("l.-4")) != nullptr,
            "each ID is kept as spelled");
    const NodeId noise = graph.find_node("noise");
    const NodeId extra = add(graph, "extra", 0.0, 200.0);
    const GraphResult first = graph.connect(PortRef{noise, PortIndex{0U}}, PortRef{extra, PortIndex{0U}});
    require(first.status == GraphStatus::ok && (*graph.link(first.link)).key == "l.13",
            "the counter starts past the largest loaded number, whatever its spelling");
    require(graph.disconnect(first.link).status == GraphStatus::ok, "the new link is removed");
    const GraphResult again = graph.connect(PortRef{noise, PortIndex{0U}}, PortRef{extra, PortIndex{0U}});
    require(again.status == GraphStatus::ok && (*graph.link(again.link)).key == "l.14",
            "and does not go back to a freed or skipped number");
    NodeGraph reloaded;
    require(reloaded.load(saved(graph)).error == GraphLoadError::ok, "the edited graph reloads");
    const GraphResult after = reloaded.connect(PortRef{reloaded.find_node("noise"), PortIndex{0U}},
                                               PortRef{reloaded.find_node("mix"), PortIndex{0U}});
    require(after.status == GraphStatus::occupied, "mix:0 still has its link");
    const NodeId spare = add(reloaded, "spare", 0.0, 400.0);
    const GraphResult next = reloaded.connect(PortRef{reloaded.find_node("noise"), PortIndex{0U}},
                                              PortRef{spare, PortIndex{0U}});
    require(next.status == GraphStatus::ok && (*reloaded.link(next.link)).key == "l.15",
            "a reload of a saved graph continues the count");
}

void sample_format() {
    NodeGraph graph;
    NodeSpec noise{};
    noise.key = "noise";
    noise.kind = "noise_texture";
    noise.title = "Noise";
    noise.bounds = WorldRect{120.0, 40.0, 100.0, 60.0};
    noise.ports.push_back(port_of("color", PortDirection::output, PortSide::right, 0.5, 3U));
    noise.properties.push_back(NodeProperty{"scale", "5.0"});
    NodeSpec mix{};
    mix.key = "mix";
    mix.kind = "mix_color";
    mix.title = "Mix";
    mix.bounds = WorldRect{360.0, 60.0, 100.0, 60.0};
    mix.ports.push_back(port_of("a", PortDirection::input, PortSide::left, 0.5, 3U));
    const GraphResult first = graph.add_node(noise);
    const GraphResult second = graph.add_node(mix);
    require(first.status == GraphStatus::ok && second.status == GraphStatus::ok, "the sample nodes are added");
    require(graph.connect(PortRef{first.node, PortIndex{0U}}, PortRef{second.node, PortIndex{0U}}).status ==
                GraphStatus::ok,
            "the sample link is made");
    require(graph.set_port_lamp(PortRef{first.node, PortIndex{0U}}, PortLamp::on).status == GraphStatus::ok,
            "a lamp is lit");
    require(saved(graph) == sample_document, "save writes the documented format (lamps are not saved)");

    NodeGraph loaded;
    const GraphLoadResult result = loaded.load(sample_document);
    require(result.error == GraphLoadError::ok, "the sample loads");
    require(saved(loaded) == sample_document, "load then save reproduces the sample");
    require(loaded.find_node("mix") == NodeId{1U, 1U} && loaded.node_count() == 2U && loaded.link_count() == 1U,
            "nodes take slots in document order");
    const NodeProperty* const scale = loaded.property(loaded.find_node("noise"), "scale");
    require(scale != nullptr && (*scale).value == "5.0", "properties load as text");
    require(loaded.undo_steps() == 0U, "a load is the undo log's start");

    NodeGraph rich;
    require(rich.load(rich_document).error == GraphLoadError::ok, "every escape and number form loads");
    require(saved(rich) == rich_document, "load then save is the identity on every form");
    const NodeRecord* const a = rich.node(rich.find_node("a.1"));
    require(a != nullptr && (*a).title == std::string("say \"hi\"\n\t\\ \x01\x7f caf\xc3\xa9") &&
                std::signbit((*a).bounds.x) && (*a).bounds.y == 1e21,
            "texts and numbers decode exactly");
    require(rich.property(rich.find_node("a.1"), "future_field") != nullptr, "an unknown field is kept");
}

void every_byte_round_trips() {
    NodeGraph graph;
    NodeSpec spec = spec_of("bytes", 0.0, 0.0);
    spec.title.clear();
    for (std::uint32_t code = 0U; code < 256U; ++code) spec.title.push_back(static_cast<char>(code));
    spec.properties.push_back(NodeProperty{"all", spec.title});
    require(graph.add_node(spec).status == GraphStatus::ok, "a title of every byte is accepted");
    const std::string document = saved(graph);
    NodeGraph loaded;
    require(loaded.load(document).error == GraphLoadError::ok, "it loads");
    require(saved(loaded) == document, "and saves to the same bytes");
    require((*loaded.node(loaded.find_node("bytes"))).title == spec.title, "every byte survives");
}

// Checks one operation's undo and redo: the saved bytes before and after, the
// revision advancing on each, and one step per operation.
struct Snapshot final {
    std::string document{};
    std::uint64_t revision{};
    std::size_t steps{};
};

Snapshot snapshot(const NodeGraph& graph) {
    Snapshot taken{};
    taken.document = saved(graph);
    taken.revision = graph.revision();
    taken.steps = graph.undo_steps();
    return taken;
}

void check_inverse(NodeGraph& graph, const Snapshot& before, const char* operation) {
    const Snapshot after = snapshot(graph);
    require(after.steps == before.steps + 1U, operation);
    require(after.revision > before.revision, operation);
    require(graph.undo().status == GraphStatus::ok, operation);
    require(saved(graph) == before.document, operation);
    require(graph.revision() > after.revision && graph.redo_steps() == 1U, operation);
    const std::uint64_t undone = graph.revision();
    require(graph.redo().status == GraphStatus::ok, operation);
    require(saved(graph) == after.document && graph.revision() > undone, operation);
}

void every_inverse() {
    NodeGraph graph;
    const NodeId a = add(graph, "a", 0.0, 0.0);
    const NodeId b = add(graph, "b", 200.0, 0.0);
    const NodeId c = add(graph, "c", 400.0, 0.0);

    Snapshot before = snapshot(graph);
    const NodeId d = add(graph, "d", 600.0, 0.0);
    check_inverse(graph, before, "add_node (appended slot) undoes and redoes");
    require(graph.undo().status == GraphStatus::ok && graph.node(d) == nullptr && graph.node_slots().size() == 3U,
            "undoing an appended add removes the slot");
    require(graph.redo().status == GraphStatus::ok && graph.node(d) != nullptr,
            "redo restores the same identity");

    const LinkId ab = join(graph, a, b);
    const LinkId bc = join(graph, b, c);
    const LinkId bd = graph.connect(PortRef{b, PortIndex{1U}}, PortRef{d, PortIndex{0U}}).link;
    before = snapshot(graph);
    require(graph.remove_node(b).status == GraphStatus::ok && graph.link_count() == 0U, "b and its links go");
    check_inverse(graph, before, "remove_node with links undoes and redoes");
    require(graph.undo().status == GraphStatus::ok, "undo the removal");
    require(graph.node(b) != nullptr && graph.link(ab) != nullptr && graph.link(bc) != nullptr &&
                graph.link(bd) != nullptr && graph.find_node("b") == b,
            "the node, its links and its key come back with the same identities");

    // A reused slot: remove d, then add e into its slot.
    require(graph.remove_node(d).status == GraphStatus::ok, "d is removed");
    before = snapshot(graph);
    const NodeId e = add(graph, "e", 0.0, 300.0);
    require(e.index == d.index && e.generation == d.generation + 1U, "e reuses d's slot");
    check_inverse(graph, before, "add_node (reused slot) undoes and redoes");
    require(graph.undo().status == GraphStatus::ok && graph.node(e) == nullptr && graph.node(d) == nullptr,
            "undoing a reused add frees the slot and both identities are stale");
    require(graph.redo().status == GraphStatus::ok && graph.node(e) != nullptr, "redo revives e");

    before = snapshot(graph);
    const std::array<NodeId, 3> moved{a, c, a};
    require(graph.move_nodes(moved, WorldPoint{0.1, 0.2}).status == GraphStatus::ok, "a move");
    check_inverse(graph, before, "move_nodes (a node listed twice) undoes to the exact positions");

    before = snapshot(graph);
    require(graph.set_node_bounds(c, WorldRect{1.0, 2.0, 3.0, 4.0}).status == GraphStatus::ok, "new bounds");
    check_inverse(graph, before, "set_node_bounds undoes and redoes");

    before = snapshot(graph);
    require(graph.set_title(a, "A much longer title than the key").status == GraphStatus::ok, "a title");
    check_inverse(graph, before, "set_title undoes and redoes");

    before = snapshot(graph);
    require(graph.set_property(a, "gain", "0.5").status == GraphStatus::ok, "a new property");
    check_inverse(graph, before, "set_property (append) undoes and redoes");
    require(graph.set_property(a, "mode", "fast").status == GraphStatus::ok &&
                graph.set_property(a, "tail", "1").status == GraphStatus::ok,
            "more properties");
    before = snapshot(graph);
    require(graph.set_property(a, "gain", "0.75").status == GraphStatus::ok &&
                (*graph.property(a, "gain")).value == "0.75",
            "a replaced property");
    check_inverse(graph, before, "set_property (replace) undoes and redoes");

    before = snapshot(graph);
    require(graph.remove_property(a, "mode").status == GraphStatus::ok && graph.property(a, "mode") == nullptr,
            "a middle property is removed");
    check_inverse(graph, before, "remove_property undoes to the same position");
    require(graph.remove_property(a, "absent").status == GraphStatus::missing_property,
            "removing an absent property is refused");

    require(graph.disconnect(bc).status == GraphStatus::ok, "a free link slot");
    before = snapshot(graph);
    const LinkId reused = graph.connect(PortRef{e, PortIndex{1U}}, PortRef{c, PortIndex{0U}}).link;
    require(reused.index == bc.index, "connect reuses the freed link slot");
    check_inverse(graph, before, "connect (reused slot) undoes and redoes");
    before = snapshot(graph);
    const LinkId appended = graph.connect(PortRef{c, PortIndex{1U}}, PortRef{e, PortIndex{0U}}).link;
    require(graph.link(appended) != nullptr && appended.index == bd.index, "another reused link slot");
    check_inverse(graph, before, "connect (reused slot, older free entry) undoes and redoes");

    before = snapshot(graph);
    require(graph.disconnect(ab).status == GraphStatus::ok, "a disconnect");
    check_inverse(graph, before, "disconnect undoes and redoes");
    require(graph.undo().status == GraphStatus::ok && graph.link(ab) != nullptr &&
                (*graph.link(ab)).from == PortRef{a, PortIndex{1U}},
            "the link comes back with its ends");

    const std::size_t steps = graph.undo_steps();
    require(graph.set_port_lamp(PortRef{a, PortIndex{0U}}, PortLamp::error).status == GraphStatus::ok &&
                graph.undo_steps() == steps,
            "lamps are not recorded");
    const std::uint64_t revision = graph.revision();
    require(graph.set_title(NodeId{99U, 1U}, "x").status == GraphStatus::stale_identity &&
                graph.undo_steps() == steps && graph.revision() == revision,
            "a refused operation records nothing");
}

void grouping() {
    NodeGraph graph;
    const NodeId a = add(graph, "a", 0.0, 0.0);
    const NodeId b = add(graph, "b", 200.0, 0.0);
    const NodeId c = add(graph, "c", 400.0, 0.0);
    const std::string start = saved(graph);
    const std::size_t steps = graph.undo_steps();

    // A drag: 60 frames of the same selection join one record.
    const std::array<NodeId, 2> selection{a, b};
    graph.begin_undo_group();
    require(graph.move_nodes(selection, WorldPoint{1.0, 0.5}).status == GraphStatus::ok, "the first frame");
    require(graph.move_nodes(selection, WorldPoint{1.0, 0.5}).status == GraphStatus::ok, "the second frame");
    const std::size_t bytes = graph.undo_bytes();
    for (int frame = 2; frame < 60; ++frame) {
        require(graph.move_nodes(selection, WorldPoint{1.0, 0.5}).status == GraphStatus::ok, "a frame");
    }
    require(graph.undo_bytes() == bytes, "a drag's frames join one record");
    require(graph.undo().status == GraphStatus::group_open && graph.redo().status == GraphStatus::group_open &&
                graph.clear_undo().status == GraphStatus::group_open,
            "undo, redo and clear wait for the group");
    require(graph.load(start).error == GraphLoadError::undo_group_open, "load waits for the group");
    graph.begin_undo_group();
    require(graph.connect(PortRef{b, PortIndex{1U}}, PortRef{c, PortIndex{0U}}).status == GraphStatus::ok &&
                graph.set_title(c, "joined").status == GraphStatus::ok,
            "a nested group's operations");
    require(graph.end_undo_group().status == GraphStatus::ok, "the inner group ends");
    require(graph.end_undo_group().status == GraphStatus::ok, "the outer group ends");
    require(graph.undo_steps() == steps + 1U, "the whole gesture is one step");
    require((*graph.node(a)).bounds.x == 60.0, "the drag moved 60 units");
    const std::string after = saved(graph);
    require(graph.undo().status == GraphStatus::ok && saved(graph) == start, "one undo reverts the gesture");
    require(graph.redo().status == GraphStatus::ok && saved(graph) == after, "one redo reapplies it");

    graph.begin_undo_group();
    require(graph.end_undo_group().status == GraphStatus::ok && graph.undo_steps() == steps + 1U,
            "an empty group makes no step");
    require(graph.end_undo_group().status == GraphStatus::no_group, "an unmatched end is refused");

    // A move of a different selection inside a group is its own record.
    graph.begin_undo_group();
    const std::array<NodeId, 1> only_c{c};
    require(graph.move_nodes(selection, WorldPoint{0.0, 1.0}).status == GraphStatus::ok &&
                graph.move_nodes(only_c, WorldPoint{0.0, 1.0}).status == GraphStatus::ok &&
                graph.move_nodes(selection, WorldPoint{0.0, 1.0}).status == GraphStatus::ok,
            "moves of different selections");
    require(graph.end_undo_group().status == GraphStatus::ok, "the group ends");
    require(graph.undo().status == GraphStatus::ok && saved(graph) == after,
            "different selections undo together and exactly");

    require(graph.undo().status == GraphStatus::ok, "back to the start");
    require(graph.set_title(a, "new").status == GraphStatus::ok && graph.redo_steps() == 0U,
            "a new operation discards the redo steps");
    require(graph.redo().status == GraphStatus::nothing_to_redo, "nothing to redo");
}

// rollback_undo_group cancels an open gesture: the graph returns exactly to
// its state at the outermost begin, and no step is left to undo or redo.
void rollback_groups() {
    NodeGraph graph;
    const NodeId a = add(graph, "a", 0.0, 0.0);
    const NodeId b = add(graph, "b", 200.0, 0.0);
    const NodeId c = add(graph, "c", 400.0, 0.0);
    const LinkId ab = join(graph, a, b);
    require(graph.set_title(c, "kept").status == GraphStatus::ok, "a step before the gesture");
    require(graph.rollback_undo_group().status == GraphStatus::no_group, "no group, nothing to roll back");

    const std::string start = saved(graph);
    const std::size_t steps = graph.undo_steps();
    const std::size_t bytes = graph.undo_bytes();
    const std::size_t nodes = graph.node_slots().size();
    const std::size_t links = graph.link_slots().size();
    const std::array<NodeId, 2> selection{a, b};
    graph.begin_undo_group();
    for (int frame = 0; frame < 10; ++frame) {
        require(graph.move_nodes(selection, WorldPoint{3.0, 1.0}).status == GraphStatus::ok, "a drag frame");
    }
    graph.begin_undo_group();
    const NodeId d = add(graph, "d", 600.0, 0.0);
    require(graph.connect(PortRef{c, PortIndex{1U}}, PortRef{d, PortIndex{0U}}).status == GraphStatus::ok &&
                graph.disconnect(ab).status == GraphStatus::ok && graph.remove_node(a).status == GraphStatus::ok &&
                graph.set_property(b, "gain", "2").status == GraphStatus::ok &&
                graph.set_port_lamp(PortRef{c, PortIndex{0U}}, PortLamp::on).status == GraphStatus::ok,
            "a nested group's operations");
    const std::uint64_t before = graph.revision();
    require(graph.rollback_undo_group().status == GraphStatus::ok, "the open gesture rolls back");
    require(saved(graph) == start, "to the exact state at the outermost begin");
    require(graph.undo_steps() == steps && graph.redo_steps() == 0U && graph.undo_bytes() == bytes,
            "leaving no step on either stack");
    require(graph.revision() > before, "the rollback advances the revision");
    require(graph.node(a) != nullptr && graph.link(ab) != nullptr && graph.node(d) == nullptr &&
                graph.find_link("l.2") == LinkId{},
            "with the same identities as before the gesture");
    require(graph.node_slots().size() == nodes && graph.link_slots().size() == links,
            "and the same slot tables");
    require((*graph.port(PortRef{c, PortIndex{0U}})).lamp == PortLamp::on, "lamps are not rolled back");
    require(graph.end_undo_group().status == GraphStatus::no_group, "every level was closed");
    require(graph.undo().status == GraphStatus::ok && (*graph.node(c)).title == "c",
            "undo reaches the step before the gesture");
    require(graph.redo().status == GraphStatus::ok && saved(graph) == start, "and redo returns");

    // A redo step from before the group is discarded by its first operation
    // (as by any new operation) and stays discarded.
    require(graph.undo().status == GraphStatus::ok && graph.redo_steps() == 1U, "a redo step");
    graph.begin_undo_group();
    require(graph.set_title(b, "gone").status == GraphStatus::ok, "the group's first operation");
    require(graph.rollback_undo_group().status == GraphStatus::ok && graph.redo_steps() == 0U &&
                graph.undo_steps() == steps - 1U && (*graph.node(b)).title == "b",
            "the rollback does not bring back discarded redo steps");

    // An empty group rolls back to nothing: no step, no revision.
    const std::uint64_t quiet = graph.revision();
    graph.begin_undo_group();
    require(graph.rollback_undo_group().status == GraphStatus::ok && graph.revision() == quiet &&
                graph.undo_steps() == steps - 1U,
            "an empty group rolls back without a change");

    // Recording off: the operations stay and the group closes.
    UndoLimits off{};
    off.maximum_steps = 0U;
    graph.set_undo_limits(off);
    graph.begin_undo_group();
    require(graph.set_title(b, "stays").status == GraphStatus::ok, "an unrecorded operation");
    require(graph.rollback_undo_group().status == GraphStatus::nothing_to_undo && (*graph.node(b)).title == "stays",
            "with recording off nothing can be rolled back");
    require(graph.end_undo_group().status == GraphStatus::no_group, "but the group is closed");
}

void bound() {
    NodeGraph graph;
    const NodeId a = add(graph, "a", 0.0, 0.0);
    UndoLimits limits{};
    limits.maximum_steps = 5U;
    graph.set_undo_limits(limits);
    require(graph.undo_steps() == 1U, "lowering the bound keeps the newest steps");
    std::vector<std::string> states{};
    states.push_back(saved(graph));
    for (int edit = 0; edit < 1000; ++edit) {
        const std::string title = "t" + std::to_string(edit);
        require(graph.set_title(a, title).status == GraphStatus::ok, "an edit");
        states.push_back(saved(graph));
        require(graph.undo_steps() <= 5U, "the log never passes its step bound");
    }
    require(graph.undo_steps() == 5U, "five steps are kept");
    for (std::size_t back = 1U; back <= 5U; ++back) {
        require(graph.undo().status == GraphStatus::ok, "a kept step undoes");
        require(saved(graph) == states[states.size() - 1U - back], "to the state before it");
    }
    require(graph.undo().status == GraphStatus::nothing_to_undo, "the dropped steps are gone");
    for (std::size_t forward = 0U; forward < 5U; ++forward) {
        require(graph.redo().status == GraphStatus::ok, "a step redoes");
    }
    require(saved(graph) == states.back(), "redo returns to the newest state");

    // The byte bound: steps are dropped oldest first, but the newest stays
    // even when it alone is over the bound.
    limits.maximum_steps = 100U;
    limits.maximum_bytes = 4096U;
    graph.set_undo_limits(limits);
    for (int edit = 0; edit < 20; ++edit) {
        require(graph.set_title(a, std::string(1000U, static_cast<char>('a' + edit))).status == GraphStatus::ok,
                "a large edit");
        require(graph.undo_bytes() <= 4096U || graph.undo_steps() == 1U, "the byte bound holds");
    }
    require(graph.undo_steps() < 20U && graph.undo_steps() >= 1U, "old steps were dropped for bytes");
    require(graph.set_title(a, std::string(10000U, 'z')).status == GraphStatus::ok && graph.undo_steps() == 1U &&
                graph.undo_bytes() > 4096U,
            "a step larger than the bound stays alone");

    limits.maximum_steps = 0U;
    graph.set_undo_limits(limits);
    require(graph.undo_steps() == 0U && graph.undo_bytes() == 0U, "a zero bound empties the log");
    require(graph.set_title(a, "unrecorded").status == GraphStatus::ok && graph.undo_steps() == 0U &&
                graph.undo().status == GraphStatus::nothing_to_undo,
            "and records nothing");
}

// A small deterministic generator (xorshift64).
struct Seeded final {
    std::uint64_t state{0x9E3779B97F4A7C15ULL};
    std::uint64_t next() noexcept {
        state ^= state << 13U;
        state ^= state >> 7U;
        state ^= state << 17U;
        return state;
    }
    std::size_t below(std::size_t count) noexcept { return static_cast<std::size_t>(next() % count); }
};

// The k-th live node, or a stale identity when there are none.
NodeId live_node(const NodeGraph& graph, std::size_t pick) {
    const std::span<const NodeRecord> slots = graph.node_slots();
    if (graph.node_count() == 0U) return NodeId{};
    std::size_t remaining = pick % graph.node_count();
    for (std::uint32_t index = 0U; index < slots.size(); ++index) {
        if (!slots[index].live) continue;
        if (remaining == 0U) return NodeId{index, slots[index].generation};
        --remaining;
    }
    return NodeId{};
}

LinkId live_link(const NodeGraph& graph, std::size_t pick) {
    const std::span<const LinkRecord> slots = graph.link_slots();
    if (graph.link_count() == 0U) return LinkId{};
    std::size_t remaining = pick % graph.link_count();
    for (std::uint32_t index = 0U; index < slots.size(); ++index) {
        if (!slots[index].live) continue;
        if (remaining == 0U) return LinkId{index, slots[index].generation};
        --remaining;
    }
    return LinkId{};
}

// One seeded edit; returns whether it was applied (refusals are expected
// for some random choices, such as an occupied input).
bool seeded_edit(NodeGraph& graph, Seeded& dice, std::size_t& next_key) {
    const std::size_t choice = dice.below(10U);
    const NodeId node = live_node(graph, dice.below(1U << 20U));
    GraphResult result{};
    result.status = GraphStatus::invalid_argument;
    switch (choice) {
    case 0U: {
        const std::string key = "n" + std::to_string(next_key);
        ++next_key;
        result = graph.add_node(spec_of(key, static_cast<double>(dice.below(5000U)) * 0.25, 7.5));
        break;
    }
    case 1U:
        if (graph.node_count() > 4U) result = graph.remove_node(node);
        break;
    case 2U: {
        const std::array<NodeId, 3> selection{node, live_node(graph, dice.below(1U << 20U)),
                                              live_node(graph, dice.below(1U << 20U))};
        const double dx = static_cast<double>(dice.below(1000U)) * 0.1 - 50.0;
        result = graph.move_nodes(selection, WorldPoint{dx, 0.3});
        break;
    }
    case 3U:
        result = graph.set_node_bounds(node, WorldRect{0.1 * static_cast<double>(dice.below(100U)), -3.0,
                                                       50.0 + static_cast<double>(dice.below(50U)), 40.0});
        break;
    case 4U: result = graph.set_title(node, "title " + std::to_string(dice.below(1000U))); break;
    case 5U: {
        const std::array<std::string_view, 3> names{"gain", "mode", "seed"};
        result = graph.set_property(node, names[dice.below(3U)], std::to_string(dice.next()));
        break;
    }
    case 6U: {
        const std::array<std::string_view, 3> names{"gain", "mode", "seed"};
        result = graph.remove_property(node, names[dice.below(3U)]);
        break;
    }
    case 7U:
    case 8U: {
        const NodeId target = live_node(graph, dice.below(1U << 20U));
        result = graph.connect(PortRef{node, PortIndex{1U}}, PortRef{target, PortIndex{0U}});
        break;
    }
    case 9U: {
        const LinkId link = live_link(graph, dice.below(1U << 20U));
        result = graph.disconnect(link);
        break;
    }
    default: break;
    }
    return result.status == GraphStatus::ok;
}

void seeded_edits_undo_to_the_start() {
    // A generated graph: 500 nodes on a jittered grid, links to near
    // neighbours, saved and reloaded (workload 8.8 at test size).
    NodeGraph built;
    Seeded dice{};
    for (std::size_t index = 0U; index < 500U; ++index) {
        const double x = static_cast<double>(index % 25U) * 160.0 + static_cast<double>(dice.below(17U));
        const double y = static_cast<double>(index / 25U) * 96.0 + static_cast<double>(dice.below(13U));
        static_cast<void>(add(built, "g" + std::to_string(index), x, y));
    }
    for (std::size_t index = 1U; index < 500U; ++index) {
        const NodeId to = built.find_node("g" + std::to_string(index));
        const NodeId from = built.find_node("g" + std::to_string(dice.below(index)));
        require(built.connect(PortRef{from, PortIndex{1U}}, PortRef{to, PortIndex{0U}}).status == GraphStatus::ok,
                "a generated link");
    }
    const std::string original = saved(built);
    NodeGraph graph;
    require(graph.load(original).error == GraphLoadError::ok, "the generated graph loads");
    require(saved(graph) == original, "and saves to the same bytes");

    std::size_t next_key = 0U;
    std::size_t applied = 0U;
    while (applied < 1000U) {
        if (seeded_edit(graph, dice, next_key)) ++applied;
    }
    require(graph.undo_steps() == 1000U, "every edit is one step");
    const std::string edited = saved(graph);
    require(edited != original, "the edits changed the document");
    std::size_t undone = 0U;
    while (graph.undo().status == GraphStatus::ok) ++undone;
    require(undone == 1000U, "every edit undoes");
    require(saved(graph) == original, "undo to the start reproduces the loaded document byte for byte");
    std::size_t redone = 0U;
    while (graph.redo().status == GraphStatus::ok) ++redone;
    require(redone == 1000U && saved(graph) == edited, "redo to the end reproduces the edited document");
    undone = 0U;
    while (graph.undo().status == GraphStatus::ok) ++undone;
    require(undone == 1000U && saved(graph) == original, "and undo again reproduces the start");
}

// One malformed document: its error and the byte the error names, given as
// the first occurrence of `marker` plus `shift` (or the document's end when
// `marker` is empty).
struct Malformed final {
    std::string document{};
    GraphLoadError error{GraphLoadError::ok};
    std::string marker{};
    std::size_t shift{};
};

// The sample with one replacement.
std::string sample_with(std::string_view from, std::string_view to) {
    std::string document(sample_document);
    const std::size_t at = document.find(from);
    require(at != std::string::npos, "the replaced text is in the sample");
    document.replace(at, from.size(), to);
    return document;
}

Malformed malformed(std::string document, GraphLoadError error, std::string marker, std::size_t shift) {
    Malformed entry{};
    entry.document = std::move(document);
    entry.error = error;
    entry.marker = std::move(marker);
    entry.shift = shift;
    return entry;
}

void malformed_documents() {
    const std::string_view link_line = "link l.1 noise:0 -> mix:0";
    std::vector<Malformed> cases{};
    cases.push_back(malformed("", GraphLoadError::truncated, "", 0U));
    cases.push_back(malformed(sample_with("gui-forms-graph", "gui-forms-graf"), GraphLoadError::bad_header,
                              "gui-forms-graf", 0U));
    cases.push_back(malformed(sample_with("graph 1;", "graph 2;"), GraphLoadError::unsupported_version, "2;", 0U));
    cases.push_back(malformed(sample_with("graph 1;", "graph x;"), GraphLoadError::bad_number, "x;", 0U));
    cases.push_back(malformed(sample_with("graph 1;", "graph 1"), GraphLoadError::syntax, "\nnode noise", 0U));
    cases.push_back(malformed(sample_with("node mix", "frame mix"), GraphLoadError::unknown_statement, "frame", 0U));
    cases.push_back(malformed(sample_with("node mix", "\x01node mix"), GraphLoadError::unexpected_character,
                              "\x01", 0U));
    cases.push_back(malformed(sample_with("node mix", "node mix\r"), GraphLoadError::unexpected_character, "\r",
                              0U));
    cases.push_back(malformed(sample_with("node mix", "node m!x"), GraphLoadError::syntax, "!x", 0U));
    cases.push_back(malformed(sample_with("node mix", std::string("node ") + std::string(65U, 'k')),
                              GraphLoadError::bad_key, "kkk", 0U));
    cases.push_back(malformed(sample_with("    kind: \"mix_color\";\n", ""), GraphLoadError::syntax,
                              "title: \"Mix\"", 0U));
    cases.push_back(malformed(sample_with("\"Mix\";", "\"Mix\""), GraphLoadError::syntax, "\"Mix\"", 5U));
    cases.push_back(malformed(sample_with("    bounds: 360", "}\n    bounds: 360"), GraphLoadError::syntax,
                              "}\n    bounds: 360", 0U));
    cases.push_back(malformed(sample_with("}\nlink", "} link"), GraphLoadError::syntax, "link l.1", 0U));
    cases.push_back(malformed(sample_with("360 60 100 60", "360 60 -100 60"), GraphLoadError::bad_number, "-100",
                              0U));
    cases.push_back(malformed(sample_with("360 60 100 60", "360 inf 100 60"), GraphLoadError::bad_number, "inf",
                              0U));
    cases.push_back(malformed(sample_with("360 60 100 60", "360 60 100"), GraphLoadError::bad_number, "100;", 3U));
    cases.push_back(malformed(sample_with("left 0.5 3", "left 1.5 3"), GraphLoadError::bad_number, "1.5", 0U));
    cases.push_back(malformed(sample_with("left 0.5 3", "left 0.5 65536"), GraphLoadError::bad_number, "65536",
                              0U));
    cases.push_back(malformed(sample_with("input left", "inward left"), GraphLoadError::bad_word, "inward", 0U));
    cases.push_back(malformed(sample_with("input left", "input middle"), GraphLoadError::bad_word, "middle", 0U));
    cases.push_back(malformed(sample_with("    scale: \"5.0\";\n", "    scale: \"5.0\";\n    scale: \"6\";\n"),
                              GraphLoadError::bad_property, "scale: \"6\"", 0U));
    cases.push_back(malformed(sample_with("    scale:", "    9scale:"), GraphLoadError::bad_property, "9scale", 0U));
    cases.push_back(malformed(sample_with("    scale:", "    title:"), GraphLoadError::bad_property,
                              "title: \"5.0\"", 0U));
    cases.push_back(malformed(sample_with("    scale: \"5.0\";\n",
                                          "    scale: \"5.0\";\n    port: output right 0.5 3 \"late\";\n"),
                              GraphLoadError::syntax, "port: output right 0.5 3 \"late\"", 0U));
    cases.push_back(malformed(sample_with("\"5.0\"", "\"5\\q\""), GraphLoadError::bad_text, "\\q", 0U));
    cases.push_back(malformed(sample_with("\"5.0\"", "\"\\x1g\""), GraphLoadError::bad_text, "\\x1g", 0U));
    cases.push_back(malformed(sample_with("\"5.0\"", "\"5\x01\""), GraphLoadError::bad_text, "\x01", 0U));
    cases.push_back(malformed(sample_with("\"5.0\";\n}", "\"5.0;\n}"), GraphLoadError::bad_text, "\n}", 0U));
    cases.push_back(malformed(sample_with("node mix", "node noise"), GraphLoadError::duplicate_key,
                              "noise {\n    kind: \"mix", 0U));
    cases.push_back(malformed(sample_with(link_line, "link l!1 noise:0 -> mix:0"), GraphLoadError::bad_key, "!1",
                              0U));
    cases.push_back(malformed(sample_with(link_line, "link noise:0 -> mix:0"), GraphLoadError::bad_key,
                              ":0 -> mix:0", 0U));
    cases.push_back(malformed(sample_with(link_line, "link l.1 noise:0 -> max:0"), GraphLoadError::unknown_node,
                              "max:0", 0U));
    cases.push_back(malformed(sample_with(link_line, "link l.1 noise:0 -> mix:1"), GraphLoadError::bad_port,
                              "mix:1", 4U));
    cases.push_back(malformed(sample_with(link_line, "link l.1 mix:0 -> noise:0"), GraphLoadError::wrong_direction,
                              "link l.1 mix", 9U));
    cases.push_back(malformed(sample_with(link_line, "link l.1 noise:0 -> noise:0"), GraphLoadError::same_node,
                              "-> noise", 3U));
    cases.push_back(malformed(sample_with(link_line, "link l.1 noise:0 => mix:0"), GraphLoadError::syntax, "=>",
                              0U));
    cases.push_back(malformed(sample_with("link l.1 noise:0 -> mix:0;\n",
                                          "link l.1 noise:0 -> mix:0;\nlink l.2 noise:0 -> mix:0;\n"),
                              GraphLoadError::occupied, "mix:0;\nend", 0U));
    cases.push_back(malformed(sample_with("link l.1 noise:0 -> mix:0;\n",
                                          "link l.1 noise:0 -> mix:0;\nlink l.1 noise:0 -> mix:0;\n"),
                              GraphLoadError::duplicate_key, "l.1 noise:0 -> mix:0;\nend", 0U));
    cases.push_back(malformed(sample_with("end;\n", "node late {\n"), GraphLoadError::unknown_statement,
                              "node late", 0U));
    cases.push_back(malformed(sample_with("end;\n", "end;\nmore"), GraphLoadError::syntax, "more", 0U));
    cases.push_back(malformed(sample_with("end;\n", "end; end;\n"), GraphLoadError::syntax, "end;\n", 0U));
    cases.push_back(malformed(sample_with("end;\n", ""), GraphLoadError::truncated, "", 0U));
    std::string many_ports(sample_document);
    {
        std::string ports{};
        for (std::size_t index = 0U; index <= maximum_node_ports; ++index) {
            ports += "    port: input left 0.5 3 \"p\";\n";
        }
        const std::size_t at = many_ports.find("    port: input left 0.5 3 \"a\";\n");
        many_ports.replace(at, 32U, ports);
        // The 257th port line, at its first token.
        const std::size_t limit_token = many_ports.find("    port: input left 0.5 3 \"p\";\n") + 256U * 32U + 4U;
        cases.push_back(malformed(many_ports, GraphLoadError::limit, "", limit_token));
    }
    std::string many_properties(sample_document);
    {
        std::string properties{};
        for (std::size_t index = 0U; index <= maximum_node_properties; ++index) {
            properties += "    p" + std::to_string(1000U + index) + ": \"\";\n";
        }
        const std::size_t at = many_properties.find("    scale: \"5.0\";\n");
        many_properties.replace(at, 18U, properties);
        cases.push_back(malformed(many_properties, GraphLoadError::limit, "p1256", 0U));
    }
    cases.push_back(malformed(sample_with("\"5.0\"", "\"" + std::string(maximum_graph_text_bytes + 1U, 'v') + "\""),
                              GraphLoadError::limit, "\"vvv", 0U));

    NodeGraph graph;
    require(graph.load(sample_document).error == GraphLoadError::ok, "the sample loads");
    const NodeId held = graph.find_node("noise");
    require(graph.set_title(held, "edited").status == GraphStatus::ok, "an undoable edit");
    const std::string kept = saved(graph);
    const std::uint64_t revision = graph.revision();
    for (const Malformed& entry : cases) {
        const GraphLoadResult result = graph.load(entry.document);
        std::size_t expected = entry.document.size();
        if (!entry.marker.empty()) {
            const std::size_t found = entry.document.find(entry.marker);
            require(found != std::string::npos, "the case's marker is in its document");
            expected = found + entry.shift;
        } else if (entry.shift != 0U) {
            expected = entry.shift;
        }
        std::size_t line = 1U;
        std::size_t line_start = 0U;
        for (std::size_t index = 0U; index < expected; ++index) {
            if (entry.document[index] != '\n') continue;
            ++line;
            line_start = index + 1U;
        }
        if (result.error != entry.error || result.offset != expected) {
            std::cerr << "node-graph-history-tests: expected " << graph_load_error_name(entry.error) << " at "
                      << expected << ", got " << graph_load_error_name(result.error) << " at " << result.offset
                      << '\n';
        }
        require(result.error == entry.error, "a malformed document is refused with its error");
        require(result.offset == expected, "at the byte that is wrong");
        require(result.line == line && result.column == expected - line_start + 1U, "with its line and column");
        require(saved(graph) == kept && graph.revision() == revision && graph.undo_steps() == 1U,
                "a refused load leaves the graph and its undo log unchanged");
    }
    const GraphLoadResult header = graph.load(sample_with("graph 1;", "graph 2;"));
    require(header.line == 1U && header.column == 17U, "positions count from line 1, column 1");
    const GraphLoadResult far = graph.load(sample_with("input left", "input middle"));
    require(far.line == 13U && far.column == 17U, "a later line's position");
    require(graph.undo().status == GraphStatus::ok && (*graph.node(held)).title == "Noise",
            "the undo log survived the refusals");
}

// Every prefix that ends before the semicolon of "end;" is truncated, at its
// end; the prefixes from that semicolon on load (only blank lines and
// comments follow).
void truncated_documents() {
    NodeGraph graph;
    const std::string_view variant = variant_document.substr(0U, variant_document.find("end of nothing"));
    const std::array<std::string_view, 3> documents{sample_document, rich_document, variant};
    for (const std::string_view document : documents) {
        const std::size_t semicolon = document.rfind("end;") + 3U;
        for (std::size_t length = 0U; length < document.size(); ++length) {
            const GraphLoadResult result = graph.load(document.substr(0U, length));
            if (length <= semicolon) {
                require(result.error == GraphLoadError::truncated && result.offset == length,
                        "every prefix cut before the end is truncated, at its end");
            } else {
                require(result.error == GraphLoadError::ok, "a prefix past \"end;\" loads");
            }
        }
        require(graph.load(document).error == GraphLoadError::ok, "the whole document loads");
    }
}

void save_storage() {
    NodeGraph graph;
    require(graph.load(sample_document).error == GraphLoadError::ok, "the sample loads");
    std::string storage(sample_document.size() - 1U, '#');
    const GraphSaveResult small = graph.save(std::span<char>(storage.data(), storage.size()));
    require(small.status == GraphSaveStatus::storage_too_small && small.written == 0U &&
                small.required == sample_document.size(),
            "short storage is refused with the required size");
    require(storage == std::string(sample_document.size() - 1U, '#'), "and left untouched");
    NodeGraph empty;
    require(saved(empty) == "gui-forms-graph 1;\nend;\n", "an empty graph's document");
    NodeGraph reloaded;
    require(reloaded.load("gui-forms-graph 1;\nend;\n").error == GraphLoadError::ok && reloaded.node_count() == 0U,
            "an empty document loads");
}

void identities_across_load() {
    NodeGraph graph;
    const NodeId a = add(graph, "a", 0.0, 0.0);
    const NodeId b = add(graph, "b", 0.0, 0.0);
    const NodeId c = add(graph, "c", 0.0, 0.0);
    const LinkId ab = join(graph, a, b);
    require(graph.load(sample_document).error == GraphLoadError::ok, "a load over a graph");
    require(graph.node(a) == nullptr && graph.node(b) == nullptr && graph.node(c) == nullptr &&
                graph.link(ab) == nullptr,
            "no identity from before the load is live after it");
    require(graph.find_node("noise").index == 0U && graph.find_node("noise").generation == a.generation + 1U,
            "a kept slot's generation advances");
    require(graph.node_slots().size() == 3U && !graph.node_slots()[2].live, "the third slot stays, freed");
    const NodeId d = add(graph, "d", 0.0, 0.0);
    require(d.index == 2U && d.generation == c.generation + 1U, "it is reused with a newer generation");
    require(graph.undo().status == GraphStatus::ok && graph.undo().status == GraphStatus::nothing_to_undo,
            "the log starts at the load");
}

void add_refusals() {
    NodeGraph graph;
    static_cast<void>(add(graph, "a", 0.0, 0.0));
    require(graph.add_node(spec_of("a", 0.0, 0.0)).status == GraphStatus::duplicate_key, "keys are unique");
    require(graph.add_node(spec_of("", 0.0, 0.0)).status == GraphStatus::invalid_argument, "a key is required");
    require(graph.add_node(spec_of("a b", 0.0, 0.0)).status == GraphStatus::invalid_argument,
            "a key has no spaces");
    require(graph.add_node(spec_of(std::string(65U, 'k'), 0.0, 0.0)).status == GraphStatus::invalid_argument,
            "a key is at most 64 bytes");
    NodeSpec reserved = spec_of("r", 0.0, 0.0);
    reserved.properties.push_back(NodeProperty{"bounds", "1"});
    require(graph.add_node(reserved).status == GraphStatus::invalid_argument, "a field word is not a property");
    NodeSpec repeated = spec_of("r", 0.0, 0.0);
    repeated.properties.push_back(NodeProperty{"gain", "1"});
    repeated.properties.push_back(NodeProperty{"gain", "2"});
    require(graph.add_node(repeated).status == GraphStatus::invalid_argument, "a property is named once");
    NodeSpec long_title = spec_of("r", 0.0, 0.0);
    long_title.title.assign(maximum_graph_text_bytes + 1U, 't');
    require(graph.add_node(long_title).status == GraphStatus::invalid_argument, "a text is bounded");
    const NodeId a = graph.find_node("a");
    require(graph.set_property(a, "-x", "1").status == GraphStatus::invalid_argument &&
                graph.set_property(a, "kind", "1").status == GraphStatus::invalid_argument,
            "set_property checks names");
    require(graph.node_count() == 1U && graph.undo_steps() == 1U, "refusals add nothing and record nothing");
}

void chains_and_cycles() {
    // A 4,000-node chain closed into a cycle, built as one step.
    NodeGraph graph;
    graph.begin_undo_group();
    NodeId previous{};
    NodeId first{};
    for (std::size_t index = 0U; index < 4000U; ++index) {
        const NodeId node = add(graph, "c" + std::to_string(index), static_cast<double>(index) * 120.0, 0.0);
        if (index == 0U) first = node;
        if (index > 0U) static_cast<void>(join(graph, previous, node));
        previous = node;
    }
    static_cast<void>(join(graph, previous, first));
    require(graph.end_undo_group().status == GraphStatus::ok && graph.undo_steps() == 1U, "built as one step");
    require(graph.link_count() == 4000U, "the cycle is closed");
    const std::string document = saved(graph);
    NodeGraph loaded;
    require(loaded.load(document).error == GraphLoadError::ok && saved(loaded) == document,
            "a cyclic chain round-trips");

    // Remove every node in one gesture; one undo restores the cycle.
    graph.begin_undo_group();
    for (std::size_t index = 0U; index < 4000U; ++index) {
        require(graph.remove_node(graph.find_node("c" + std::to_string(index))).status == GraphStatus::ok,
                "a chain node is removed");
    }
    require(graph.end_undo_group().status == GraphStatus::ok && graph.node_count() == 0U, "the chain is gone");
    require(graph.undo().status == GraphStatus::ok && saved(graph) == document,
            "one undo restores the whole cycle exactly");
    require(graph.undo().status == GraphStatus::ok && graph.node_count() == 0U && graph.node_slots().empty(),
            "undoing the build empties the slots");
    require(graph.redo().status == GraphStatus::ok && saved(graph) == document, "redo rebuilds it");
}

} // namespace

int main() {
    sample_format();
    tolerant_load();
    link_ids();
    gapped_link_ids();
    every_byte_round_trips();
    every_inverse();
    grouping();
    rollback_groups();
    bound();
    seeded_edits_undo_to_the_start();
    malformed_documents();
    truncated_documents();
    save_storage();
    identities_across_load();
    add_refusals();
    chains_and_cycles();
    std::cout << "node-graph-history-tests: inverses, groups, bounds, seeded edits, documents and errors passed\n";
    return 0;
}
