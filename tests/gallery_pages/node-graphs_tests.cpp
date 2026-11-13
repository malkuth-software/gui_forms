#include "support.hpp"
#include "gui_forms/controls/label.hpp"
#include "gui_forms/controls/node_canvas.hpp"
#include "gui_forms/controls/node_graph.hpp"

#include <span>
#include <string>

namespace gallery_pages_tests {

// The node-graphs page (cp31 fold D return): the NodeCanvas shows the
// page's fixed graph (four nodes with dashed keys, three links, the mix's
// lit lamp), publishes the canvas anatomy, and the status line follows the
// canvas's selection and its undoable edits.
void test_node_graphs(gui_forms::Window& window) {
    select_page(window, "node-graphs");
    require(window.find("gallery.node-graphs.heading") != nullptr, "node-graphs page shows its heading");
    const std::shared_ptr<NodeCanvas> canvas =
        std::dynamic_pointer_cast<NodeCanvas>(window.find("gallery.node-graphs.canvas"));
    const std::shared_ptr<Label> status =
        std::dynamic_pointer_cast<Label>(window.find("gallery.node-graphs.status"));
    require(canvas != nullptr && status != nullptr, "the page shows its canvas and status line");
    require((*canvas).anatomy().control_id == "node_canvas", "the canvas publishes the canvas anatomy");

    NodeGraph* const graph = (*canvas).graph();
    require(graph != nullptr && (*graph).node_count() == 4U && (*graph).link_count() == 3U,
            "the canvas shows the page's four nodes and three links");
    const std::span<const NodeRecord> slots = (*graph).node_slots();
    require(slots.size() == 4U && slots[0].key == "noise-source" && slots[2].key == "colour-mix" &&
                slots[3].key == "image-output",
            "the nodes keep their dashed keys in order");
    require(slots[2].ports.size() == 3U && slots[2].ports[2].lamp == PortLamp::on, "the mix's output lamp is lit");
    // Generations read now: the span is not used after the graph changes.
    const NodeId noise{0U, slots[0].generation};
    const NodeId output{3U, slots[3].generation};
    require((*graph).undo_steps() == 0U, "the page's graph starts with no undo history");
    require((*status).text.read() == "4 nodes, 3 links; nothing selected", "the status line counts the graph");

    // Selection: all, then none.
    (*canvas).select_all();
    window.perform_layout();
    require((*canvas).selection().size() == 4U && (*status).text.read() == "4 nodes, 3 links; 4 nodes selected",
            "selecting every node is reported");
    (*canvas).clear_selection();
    window.perform_layout();
    require((*status).text.read() == "4 nodes, 3 links; nothing selected", "clearing the selection is reported");

    // An edit through the canvas is one undo step; undo restores the graph.
    (*canvas).select_only(output);
    require((*canvas).delete_selection(), "the output node is deleted through the canvas");
    window.perform_layout();
    require((*graph).node_count() == 3U && (*graph).link_count() == 2U &&
                (*status).text.read() == "3 nodes, 2 links; nothing selected",
            "deleting a node and its link is reported");
    require((*canvas).undo(), "the delete is undone through the canvas");
    window.perform_layout();
    require((*graph).node_count() == 4U && (*graph).link_count() == 3U &&
                (*status).text.read().find("4 nodes, 3 links; ") == 0U,
            "undo restores the node and its link");

    // The focused node is always an item of the canvas, painted or not:
    // "<canvas id>.node-<slot>-<generation>".
    require((*canvas).set_focused_node(noise), "the noise node takes the keyboard focus");
    window.perform_layout();
    const std::string semantics = window.semantic_snapshot().to_json();
    const std::string noise_item = "gallery.node-graphs.canvas.node-0-" + std::to_string(noise.generation);
    require(semantics.find(noise_item) != std::string::npos, "the canvas publishes its focused node as an item");
}

} // namespace gallery_pages_tests
