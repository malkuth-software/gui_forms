#include "pages.hpp"

#include "node-graphs_form.hpp"

#include "gui_forms/controls/node_canvas.hpp"
#include "gui_forms/controls/node_graph.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gui_forms::gallery::pages {

namespace {

// -- the page's graph -----------------------------------------------------------------------
//
// Four nodes and three links, fixed: two sources feed a mix, which feeds an
// output. Every key, kind and link ID is dashed, so the theme's `:item()`
// rules and the anatomy name them as stable IDs. Bounds are world units on
// multiples of the canvas's 16-unit grid; the canvas opens at world (0, 0)
// at zoom 1, where all four fit inside it. One socket type throughout.

constexpr std::uint16_t colour_socket = 1U;

struct PagePort final {
    std::string_view name{};
    PortDirection direction{PortDirection::input};
    PortSide side{PortSide::left};
    double offset{0.5};
    PortLamp lamp{PortLamp::off};
};

struct PageNode final {
    std::string_view key{};
    std::string_view kind{};
    std::string_view title{};
    WorldRect bounds{};
    // The first `port_count` entries of `ports` are the node's.
    std::array<PagePort, 3> ports{};
    std::size_t port_count{};
};

// A link from `from_node`'s port `from_port` to `to_node`'s port `to_port`
// (indices into page_nodes and their ports).
struct PageLink final {
    std::string_view key{};
    std::size_t from_node{};
    std::size_t from_port{};
    std::size_t to_node{};
    std::size_t to_port{};
};

constexpr std::array<PageNode, 4> page_nodes{{
    {.key = "noise-source",
     .kind = "noise-texture",
     .title = "Noise",
     .bounds = WorldRect{48.0, 48.0, 144.0, 96.0},
     .ports = {{{.name = "out", .direction = PortDirection::output, .side = PortSide::right}}},
     .port_count = 1U},
    {.key = "colour-ramp",
     .kind = "colour-ramp",
     .title = "Gradient",
     .bounds = WorldRect{48.0, 208.0, 144.0, 96.0},
     .ports = {{{.name = "out", .direction = PortDirection::output, .side = PortSide::right}}},
     .port_count = 1U},
    {.key = "colour-mix",
     .kind = "mix-colour",
     .title = "Mix",
     .bounds = WorldRect{320.0, 112.0, 160.0, 128.0},
     .ports = {{{.name = "colour-a", .direction = PortDirection::input, .side = PortSide::left, .offset = 0.375},
                {.name = "colour-b", .direction = PortDirection::input, .side = PortSide::left, .offset = 0.75},
                {.name = "out",
                 .direction = PortDirection::output,
                 .side = PortSide::right,
                 .offset = 0.5,
                 .lamp = PortLamp::on}}},
     .port_count = 3U},
    {.key = "image-output",
     .kind = "image-output",
     .title = "Output",
     .bounds = WorldRect{608.0, 128.0, 144.0, 96.0},
     .ports = {{{.name = "image", .direction = PortDirection::input, .side = PortSide::left}}},
     .port_count = 1U},
}};

constexpr std::array<PageLink, 3> page_links{{
    {.key = "noise-to-mix", .from_node = 0U, .from_port = 0U, .to_node = 2U, .to_port = 0U},
    {.key = "ramp-to-mix", .from_node = 1U, .from_port = 0U, .to_node = 2U, .to_port = 1U},
    {.key = "mix-to-output", .from_node = 2U, .from_port = 2U, .to_node = 3U, .to_port = 0U},
}};

[[nodiscard]] NodeSpec node_spec(const PageNode& node) {
    NodeSpec spec{};
    spec.key.assign(node.key);
    spec.kind.assign(node.kind);
    spec.title.assign(node.title);
    spec.bounds = node.bounds;
    spec.ports.reserve(node.port_count);
    for (std::size_t index = 0U; index < node.port_count; ++index) {
        const PagePort& port = node.ports[index];
        PortRecord record{};
        record.name.assign(port.name);
        record.direction = port.direction;
        record.side = port.side;
        record.offset = port.offset;
        record.lamp = port.lamp;
        record.socket_type = colour_socket;
        spec.ports.push_back(std::move(record));
    }
    return spec;
}

// "1 node", "4 nodes".
void append_count(std::string& text, std::size_t count, std::string_view one, std::string_view many) {
    text.append(std::to_string(count));
    text.push_back(' ');
    text.append(count == 1U ? one : many);
}

} // namespace

// Owns the node-graphs page: the controls demo/pages/node-graphs.form
// describes, the graph the canvas shows, and the status line under it. The
// canvas borrows the graph (NodeCanvas::set_graph), so the page detaches it
// before the graph goes: the window may keep the canvas after the page.
// The canvas edits the graph in place (moves, links, deletes, undo); the
// status line follows its selection and edits. Nothing runs on a clock:
// no link is marked flowing, so the canvas paints only on change.
class NodeGraphsPage final : public gui_forms::Component {
public:
    explicit NodeGraphsPage(const std::shared_ptr<Surface>& body);
    ~NodeGraphsPage() override;
    NodeGraphsPage(const NodeGraphsPage&) = delete;
    NodeGraphsPage& operator=(const NodeGraphsPage&) = delete;

private:
    void mount_document(const std::shared_ptr<Surface>& body);
    void build_graph();
    void on_selection_changed(const NodeCanvasSelection& selection);
    void on_graph_edited(const NodeGraphEdited& edited);
    void show_status();

    // Declared before the document so that it outlives the document's
    // references to the canvas; the destructor detaches it first anyway.
    NodeGraph graph_{};
    // Owns the document's controls; the shell's page body holds them too
    // once mount_document() has moved them there.
    NodeGraphsDocument<NodeGraphsPage> document_{};
    std::string status_{};
};

NodeGraphsPage::NodeGraphsPage(const std::shared_ptr<Surface>& body) {
    document_.initialize_component(*this);
    mount_document(body);
    build_graph();
    NodeCanvas& canvas = *document_.gallery_node_graphs_canvas;
    canvas.set_graph(&graph_);
    on(canvas.selection_changed(), *this, &NodeGraphsPage::on_selection_changed);
    on(canvas.graph_edited(), *this, &NodeGraphsPage::on_graph_edited);
    show_status();
}

NodeGraphsPage::~NodeGraphsPage() {
    // The canvas may outlive the page in the window's tree: it must not
    // keep a borrow of graph_.
    (*document_.gallery_node_graphs_canvas).set_graph(nullptr);
}

void NodeGraphsPage::mount_document(const std::shared_ptr<Surface>& body) {
    // The document's root is a declared boundary only: its children move
    // into the shell's page body, a design surface of the same 1000 x 660
    // size, where each is placed by its bounds in design coordinates. The
    // copy of the child list keeps the iteration valid while the root
    // shrinks.
    Control& root = *document_.gallery_node_graphs_document;
    const std::vector<Control::Ptr> children(root.children().begin(), root.children().end());
    for (const Control::Ptr& child : children) {
        Control::Ptr moved = root.remove_child((*child).runtime_id());
        (*body).add_child(std::move(moved));
    }
}

// The fixed graph, built with no undo history: the canvas's first undo
// step is the user's. A refusal leaves the graph short of that node or
// link; the page shows what was made.
void NodeGraphsPage::build_graph() {
    std::array<NodeId, page_nodes.size()> made{};
    for (std::size_t index = 0U; index < page_nodes.size(); ++index) {
        const NodeSpec spec = node_spec(page_nodes[index]);
        const GraphResult added = graph_.add_node(spec);
        if (added.status == GraphStatus::ok) made[index] = added.node;
    }
    for (const PageLink& link : page_links) {
        const PortRef from{made[link.from_node], PortIndex{static_cast<std::uint16_t>(link.from_port)}};
        const PortRef to{made[link.to_node], PortIndex{static_cast<std::uint16_t>(link.to_port)}};
        static_cast<void>(graph_.connect(from, to, link.key));
    }
    static_cast<void>(graph_.clear_undo());
}

void NodeGraphsPage::on_selection_changed(const NodeCanvasSelection& selection) {
    static_cast<void>(selection);
    show_status();
}

void NodeGraphsPage::on_graph_edited(const NodeGraphEdited& edited) {
    static_cast<void>(edited);
    show_status();
}

// "4 nodes, 3 links; nothing selected", "...; 2 nodes selected" or
// "...; a link selected".
void NodeGraphsPage::show_status() {
    const NodeCanvas& canvas = *document_.gallery_node_graphs_canvas;
    status_.clear();
    append_count(status_, graph_.node_count(), "node", "nodes");
    status_.append(", ");
    append_count(status_, graph_.link_count(), "link", "links");
    status_.append("; ");
    const std::size_t chosen = canvas.selection().size();
    if (chosen > 0U) {
        append_count(status_, chosen, "node", "nodes");
        status_.append(" selected");
    } else if (canvas.selected_link().has_value()) {
        status_.append("a link selected");
    } else {
        status_.append("nothing selected");
    }
    (*document_.gallery_node_graphs_status).text = status_;
}

void add_node_graphs(const std::shared_ptr<Surface>& page, const std::shared_ptr<PageContext>& context) {
    (*context).components.add(std::make_shared<NodeGraphsPage>(page));
}

} // namespace gui_forms::gallery::pages
