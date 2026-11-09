#include "gui_forms/component.hpp"
#include "gui_forms/docking.hpp"
#include "gui_forms/event.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using gui_forms::Component;
using gui_forms::DockAutoHideEntry;
using gui_forms::DockArrangedNode;
using gui_forms::DockEdit;
using gui_forms::DockEditOrigin;
using gui_forms::DockFloatingSite;
using gui_forms::DockGroupDescriptor;
using gui_forms::DockGroupId;
using gui_forms::DockLayout;
using gui_forms::DockLayoutChange;
using gui_forms::DockLayoutState;
using gui_forms::DockLayoutUpgradeHook;
using gui_forms::DockLayoutWriteResult;
using gui_forms::DockMonitor;
using gui_forms::DockMonitorMatch;
using gui_forms::DockMonitorPlacement;
using gui_forms::DockNode;
using gui_forms::DockNodeId;
using gui_forms::DockNodeKind;
using gui_forms::DockPanelDescriptor;
using gui_forms::DockPanelId;
using gui_forms::DockPanelKind;
using gui_forms::DockPanelLocation;
using gui_forms::DockPlacementKind;
using gui_forms::DockRefusalLayer;
using gui_forms::DockRefusalReason;
using gui_forms::DockRestoreDiagnostic;
using gui_forms::DockRestoreDiagnosticKind;
using gui_forms::DockRestoreOptions;
using gui_forms::DockRestoreResult;
using gui_forms::DockRestoreStatus;
using gui_forms::DockSide;
using gui_forms::DockSiteId;
using gui_forms::DockSiteRestrictions;
using gui_forms::DockSplitSizes;
using gui_forms::DockSubject;
using gui_forms::DockTransaction;
using gui_forms::DockTransactionResult;
using gui_forms::DockUnknownOwner;
using gui_forms::DockUnknownOwnerKind;
using gui_forms::DockUpgradeDecision;
using gui_forms::DockVisiblePanel;
using gui_forms::Rect;
using gui_forms::Size;
using gui_forms::solve_dock_split;

int failures = 0;

void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

class ChangeRecorder final : public Component {
public:
    void record(const DockLayoutChange& change) {
        ++count;
        last = change;
    }
    std::size_t count{0};
    DockLayoutChange last{};
};

DockPanelDescriptor tool(std::string_view id, DockSide side = DockSide::left) {
    DockPanelDescriptor descriptor{};
    descriptor.id = DockPanelId(std::string(id));
    descriptor.kind = DockPanelKind::tool;
    descriptor.default_placement.side = side;
    return descriptor;
}

DockPanelDescriptor document(std::string_view id) {
    DockPanelDescriptor descriptor{};
    descriptor.id = DockPanelId(std::string(id));
    descriptor.kind = DockPanelKind::document;
    return descriptor;
}

DockPanelId panel(std::string_view id) {
    const DockPanelId result{std::string(id)};
    return result;
}

DockSubject subject(std::string_view id) {
    DockSubject result = DockSubject::of_panel(panel(id));
    return result;
}

DockTransactionResult apply_one(DockLayout& layout, DockEdit edit,
                                DockEditOrigin origin = DockEditOrigin::program) {
    DockTransaction transaction{};
    transaction.origin = origin;
    transaction.edits.push_back(std::move(edit));
    DockTransactionResult result = layout.apply(transaction);
    return result;
}

DockNodeId stack_of(const DockLayout& layout, std::string_view id) {
    const DockPanelLocation location = layout.locate(panel(id));
    return location.stack;
}

// The standard layout of these tests: solution left, properties right,
// output bottom, two documents.
void register_standard(DockLayout& layout) {
    require(layout.register_panel(tool("tools.solution", DockSide::left)), "register solution");
    require(layout.register_panel(tool("tools.properties", DockSide::right)), "register properties");
    require(layout.register_panel(tool("tools.output", DockSide::bottom)), "register output");
    require(layout.register_panel(document("doc.a")), "register doc.a");
    require(layout.register_panel(document("doc.b")), "register doc.b");
}

void show_standard(DockLayout& layout) {
    DockTransaction transaction{};
    const std::array<std::string_view, 5> order{"tools.solution", "tools.properties", "tools.output",
                                                "doc.a", "doc.b"};
    for (const std::string_view id : order) transaction.edits.push_back(DockEdit::show(panel(id)));
    const DockTransactionResult result = layout.apply(transaction);
    require(result.applied, "the standard layout is shown");
}

const std::string standard_xml =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<dock_layout format=\"2\" application_version=\"0\" next_id=\"9\">\n"
    "  <main_site>\n"
    "    <split id=\"7\" axis=\"vertical\" ratio=\"0.75\">\n"
    "      <split id=\"5\" axis=\"horizontal\" ratio=\"0.75\">\n"
    "        <split id=\"3\" axis=\"horizontal\" ratio=\"0.25\">\n"
    "          <tool_stack id=\"2\" selected=\"0\">\n"
    "            <panel id=\"tools.solution\"/>\n"
    "          </tool_stack>\n"
    "          <document_area id=\"1\">\n"
    "            <document_stack id=\"8\" selected=\"1\">\n"
    "              <panel id=\"doc.a\"/>\n"
    "              <panel id=\"doc.b\"/>\n"
    "            </document_stack>\n"
    "          </document_area>\n"
    "        </split>\n"
    "        <tool_stack id=\"4\" selected=\"0\">\n"
    "          <panel id=\"tools.properties\"/>\n"
    "        </tool_stack>\n"
    "      </split>\n"
    "      <tool_stack id=\"6\" selected=\"0\">\n"
    "        <panel id=\"tools.output\"/>\n"
    "      </tool_stack>\n"
    "    </split>\n"
    "  </main_site>\n"
    "  <panel_record id=\"doc.a\" kind=\"document\" last=\"docked\"/>\n"
    "  <panel_record id=\"doc.b\" kind=\"document\" last=\"docked\"/>\n"
    "  <panel_record id=\"tools.output\" kind=\"tool\" last=\"docked\"/>\n"
    "  <panel_record id=\"tools.properties\" kind=\"tool\" last=\"docked\"/>\n"
    "  <panel_record id=\"tools.solution\" kind=\"tool\" last=\"docked\"/>\n"
    "</dock_layout>\n";

bool has_diagnostic(const DockRestoreResult& result, DockRestoreDiagnosticKind kind) {
    for (const DockRestoreDiagnostic& diagnostic : result.diagnostics) {
        if (diagnostic.kind == kind) return true;
    }
    return false;
}

void print_diagnostics(const DockRestoreResult& result) {
    for (const DockRestoreDiagnostic& diagnostic : result.diagnostics) {
        std::cerr << "  diagnostic " << static_cast<int>(diagnostic.kind) << ": " << diagnostic.detail << '\n';
    }
}

// Applies a transaction that must be refused, and checks that nothing
// changed and nothing was published.
void require_refused(DockLayout& layout, const DockTransaction& transaction, DockRefusalReason reason,
                     DockRefusalLayer layer, std::string_view what) {
    ChangeRecorder recorder{};
    gui_forms::on(layout.changed(), recorder, &ChangeRecorder::record);
    const DockLayoutState before = layout.state();
    const std::uint64_t generation = layout.generation();
    const DockTransactionResult checked = layout.check(transaction);
    const DockTransactionResult result = layout.apply(transaction);
    if (result.applied || result.refusal.reason != reason || result.refusal.layer != layer) {
        std::cerr << "FAIL: " << what << ": reason " << static_cast<int>(result.refusal.reason) << ", layer "
                  << static_cast<int>(result.refusal.layer) << '\n';
        ++failures;
    }
    require(checked.refusal.reason == result.refusal.reason, "check agrees with apply");
    require(layout.state() == before, "a refused transaction leaves the model unchanged");
    require(layout.generation() == generation, "a refused transaction keeps the generation");
    require(recorder.count == 0U, "a refused transaction publishes nothing");
}

void require_refused(DockLayout& layout, DockEdit edit, DockRefusalReason reason, DockRefusalLayer layer,
                     std::string_view what, DockEditOrigin origin = DockEditOrigin::program) {
    DockTransaction transaction{};
    transaction.origin = origin;
    transaction.edits.push_back(std::move(edit));
    require_refused(layout, transaction, reason, layer, what);
}

void test_default_layout() {
    DockLayout layout{};
    require(static_cast<bool>(layout.check_invariants()), "the empty layout is valid");
    require(layout.state().main_root == layout.document_area(), "the document area is the root");
    const DockNode* area = layout.find_node(layout.document_area());
    require(area != nullptr && (*area).kind == DockNodeKind::document_area, "the document area exists");
    require(!layout.register_panel(DockPanelDescriptor{}), "an empty panel ID is refused");
    require(layout.register_panel(tool("a")), "a panel registers");
    require(!layout.register_panel(tool("a")), "a panel registers once");
    require(layout.locate(panel("a")).placement == DockPlacementKind::absent, "registering does not place");
}

void test_standard_layout_and_format() {
    DockLayout layout{};
    register_standard(layout);
    ChangeRecorder recorder{};
    gui_forms::on(layout.changed(), recorder, &ChangeRecorder::record);
    show_standard(layout);
    require(recorder.count == 1U && recorder.last.edit_count == 5U && recorder.last.generation == 1U,
            "five edits in one transaction publish one change");
    require(static_cast<bool>(layout.check_invariants()), "standard layout valid");
    const DockLayoutWriteResult written = layout.write_xml();
    require(static_cast<bool>(written), "standard layout writes");
    if (written.text != standard_xml) {
        std::cerr << "FAIL: the written form differs:\n" << written.text;
        ++failures;
    }
    std::vector<DockVisiblePanel> visible{};
    layout.collect_visible_panels(visible);
    require(visible.size() == 5U, "five visible panels");
    std::size_t to_create = 0;
    for (const DockVisiblePanel& entry : visible) {
        if (entry.selected && entry.placement != DockPlacementKind::auto_hidden) ++to_create;
        if (entry.panel == panel("doc.a")) require(!entry.selected, "doc.a is behind doc.b");
    }
    require(to_create == 4U, "four panels are selected in their stacks");
    require(layout.locate(panel("tools.output")).placement == DockPlacementKind::docked, "output docked");
}

void test_round_trip() {
    DockLayout layout{};
    register_standard(layout);
    show_standard(layout);
    require(static_cast<bool>(apply_one(layout, DockEdit::float_subject(subject("tools.output"),
                                                                         Rect{10.5, 20.25, 300.0, 200.0},
                                                                         "DISPLAY1"))),
            "output floats");
    require(static_cast<bool>(apply_one(layout, DockEdit::auto_hide(subject("tools.solution"), DockSide::left,
                                                                     180.0))),
            "solution auto-hides");
    require(static_cast<bool>(apply_one(layout, DockEdit::hide(panel("tools.properties")))), "properties hides");
    gui_forms::DockWindowFrame window{};
    window.frame = Rect{0.0, 0.0, 1280.0, 800.0};
    window.monitor_id = "DISPLAY1";
    window.maximized = true;
    layout.set_main_window_frame(window);
    const DockLayoutWriteResult first = layout.write_xml();
    require(static_cast<bool>(first), "complex layout writes");

    DockLayout restored{};
    register_standard(restored);
    const DockRestoreResult result = restored.restore_xml(first.text, DockRestoreOptions{});
    require(result.status == DockRestoreStatus::restored, "complex layout restores");
    require(result.diagnostics.empty(), "a current layout restores without diagnostics");
    print_diagnostics(result);
    require(restored.state() == layout.state(), "restore reproduces the state");
    const DockLayoutWriteResult second = restored.write_xml();
    require(second.text == first.text, "save and restore round trip is byte-stable");
}

void test_hide_and_show_memory() {
    DockLayout layout{};
    register_standard(layout);
    show_standard(layout);
    const DockNodeId solution_stack = stack_of(layout, "tools.solution");

    // Docked, stack gone: back beside the same neighbour.
    require(static_cast<bool>(apply_one(layout, DockEdit::hide(panel("tools.properties")))), "hide properties");
    require(layout.locate(panel("tools.properties")).placement == DockPlacementKind::hidden, "placeholder kept");
    require(static_cast<bool>(apply_one(layout, DockEdit::show(panel("tools.properties")))), "show properties");
    const DockNode& properties = *layout.find_node(stack_of(layout, "tools.properties"));
    const DockNode& around = *layout.find_node(properties.parent);
    require(around.kind == DockNodeKind::split && around.second == properties.id && around.first.value == 3U,
            "properties returns to the right of the same neighbour");
    require(around.ratio == 0.75, "with the same share");

    // Docked, stack still there: back at its index.
    require(static_cast<bool>(apply_one(layout, DockEdit::hide(panel("doc.a")))), "hide doc.a");
    require(static_cast<bool>(apply_one(layout, DockEdit::show(panel("doc.a")))), "show doc.a");
    require(layout.locate(panel("doc.a")).index == 0U, "doc.a returns to index 0");

    // Floating, site gone: a new site at the remembered frame.
    const Rect frame{40.0, 50.0, 300.0, 200.0};
    require(static_cast<bool>(apply_one(layout, DockEdit::float_subject(subject("tools.output"), frame, "M"))),
            "float output");
    require(static_cast<bool>(apply_one(layout, DockEdit::hide(panel("tools.output")))), "hide output");
    require(layout.state().floating_sites.empty(), "its floating site closed");
    require(static_cast<bool>(apply_one(layout, DockEdit::show(panel("tools.output")))), "show output");
    const DockPanelLocation output = layout.locate(panel("tools.output"));
    require(output.placement == DockPlacementKind::floating, "output floats again");
    require((*layout.find_floating_site(output.site)).frame == frame, "at the remembered frame");

    // Auto-hidden, stack gone: auto-hidden again on the same edge.
    require(static_cast<bool>(apply_one(layout, DockEdit::auto_hide(subject("tools.solution"), DockSide::left))),
            "auto-hide solution");
    require(layout.state().auto_hidden.size() == 1U && layout.state().auto_hidden[0].stack == solution_stack,
            "the stack keeps its ID while auto-hidden");
    require(static_cast<bool>(apply_one(layout, DockEdit::hide(panel("tools.solution")))), "hide solution");
    require(static_cast<bool>(apply_one(layout, DockEdit::show(panel("tools.solution")))), "show solution");
    require(layout.locate(panel("tools.solution")).placement == DockPlacementKind::auto_hidden,
            "solution returns auto-hidden");
    const DockAutoHideEntry& entry = layout.state().auto_hidden[0];
    require(entry.edge == DockSide::left, "on the left edge");

    // Unpinning returns the stack beside its neighbour.
    require(static_cast<bool>(apply_one(layout, DockEdit::unpin(entry.stack))), "unpin");
    require(layout.locate(panel("tools.solution")).placement == DockPlacementKind::docked, "docked again");

    // Closing: a document is gone, a tool keeps its placeholder.
    require(static_cast<bool>(apply_one(layout, DockEdit::close(panel("doc.b")))), "close doc.b");
    require(layout.locate(panel("doc.b")).placement == DockPlacementKind::absent, "a closed document is gone");
    require(static_cast<bool>(apply_one(layout, DockEdit::close(panel("tools.properties")))), "close properties");
    require(layout.locate(panel("tools.properties")).placement == DockPlacementKind::hidden,
            "a closed tool keeps its placeholder");
    require(static_cast<bool>(layout.check_invariants()), "valid after the memory sequence");
}

void test_primitive_edits() {
    DockLayout layout{};
    register_standard(layout);
    require(layout.register_panel(tool("tools.toolbox")), "register toolbox");
    show_standard(layout);

    // dock_into: tab the toolbox into the solution stack at index 0.
    const DockNodeId solution = stack_of(layout, "tools.solution");
    require(static_cast<bool>(apply_one(layout, DockEdit::dock_into(subject("tools.toolbox"), solution, 0))),
            "toolbox tabs into solution");
    const DockNode& tabbed = *layout.find_node(solution);
    require(tabbed.panels.size() == 2U && tabbed.panels[0] == panel("tools.toolbox") && tabbed.selected == 0U,
            "toolbox first and selected");

    // move and select.
    require(static_cast<bool>(apply_one(layout, DockEdit::move(panel("tools.toolbox"), 5))), "move to the end");
    require((*layout.find_node(solution)).panels[1] == panel("tools.toolbox"), "toolbox moved to the end");
    require((*layout.find_node(solution)).selected == 1U, "the selection follows the panel");
    require(static_cast<bool>(apply_one(layout, DockEdit::select(panel("tools.solution")))), "select solution");
    require((*layout.find_node(solution)).selected == 0U, "solution selected");

    // dock_beside: the toolbox to the top of output, sharing 0.4.
    const DockNodeId output = stack_of(layout, "tools.output");
    require(static_cast<bool>(apply_one(layout, DockEdit::dock_beside(subject("tools.toolbox"), output,
                                                                       DockSide::top, 0.4))),
            "toolbox above output");
    const DockNode& toolbox = *layout.find_node(stack_of(layout, "tools.toolbox"));
    const DockNode& split = *layout.find_node(toolbox.parent);
    require(split.axis == gui_forms::DockAxis::vertical && split.first == toolbox.id && split.second == output &&
                split.ratio == 0.4, "a vertical split with the toolbox first at 0.4");

    // Moving a whole stack: merge output's stack into the solution stack.
    require(static_cast<bool>(apply_one(layout, DockEdit::dock_into(DockSubject::of_stack(output), solution))),
            "output's stack merges into solution's");
    require(layout.find_node(output) == nullptr, "the merged stack is gone");
    require(stack_of(layout, "tools.output") == solution, "output is in solution's stack");
    require((*layout.find_node(solution)).panels[(*layout.find_node(solution)).selected] == panel("tools.output"),
            "the merged stack's selection is kept");

    // dock_at_edge with a stack: the solution stack to the bottom edge.
    require(static_cast<bool>(apply_one(layout, DockEdit::dock_at_edge(DockSubject::of_stack(solution),
                                                                        DockSide::bottom, 0.3))),
            "solution stack to the bottom edge");
    const DockNode& root = *layout.find_node(layout.state().main_root);
    require(root.kind == DockNodeKind::split && root.second == solution && root.ratio == 0.7,
            "the stack is the root split's second child at 0.3");

    // set_ratio and set_floating_frame.
    require(static_cast<bool>(apply_one(layout, DockEdit::set_ratio(root.id, 0.6))), "set ratio");
    require((*layout.find_node(layout.state().main_root)).ratio == 0.6, "ratio set");
    require(static_cast<bool>(apply_one(layout, DockEdit::float_subject(subject("doc.a"),
                                                                         Rect{0.0, 0.0, 400.0, 300.0}, "M"))),
            "a document floats");
    require((*layout.find_node(stack_of(layout, "doc.a"))).kind == DockNodeKind::document_stack,
            "a floating document stands in a document stack");
    const DockSiteId site = layout.locate(panel("doc.a")).site;
    require(static_cast<bool>(apply_one(layout, DockEdit::set_floating_frame(site, Rect{5.0, 6.0, 7.0, 8.0},
                                                                              "N"))),
            "set floating frame");
    require((*layout.find_floating_site(site)).frame == Rect{5.0, 6.0, 7.0, 8.0} &&
                (*layout.find_floating_site(site)).monitor_id == "N", "frame and monitor set");
    // Back into the document area.
    require(static_cast<bool>(apply_one(layout, DockEdit::dock_into(subject("doc.a"), layout.document_area()))),
            "doc.a returns to the document area");
    require(layout.state().floating_sites.empty(), "the empty floating site closed");
    require(static_cast<bool>(layout.check_invariants()), "valid after the primitive edits");
}

void test_restrictions() {
    DockLayout layout{};
    register_standard(layout);
    DockPanelDescriptor pinned = tool("tools.fixed");
    pinned.allowed.floating = false;
    pinned.allowed.tabbed = false;
    pinned.allowed.hidden = false;
    pinned.allowed.closed = false;
    require(layout.register_panel(pinned), "register fixed");
    DockPanelDescriptor alpha = tool("tools.alpha");
    alpha.group = DockGroupId("alpha");
    require(layout.register_panel(alpha), "register alpha");
    DockPanelDescriptor beta = tool("tools.beta");
    beta.group = DockGroupId("beta");
    require(layout.register_panel(beta), "register beta");
    DockPanelDescriptor docked_document = tool("tools.preview");
    docked_document.dock_as_document = true;
    require(layout.register_panel(docked_document), "register preview");
    show_standard(layout);
    DockTransaction shows{};
    shows.edits.push_back(DockEdit::show(panel("tools.fixed")));
    shows.edits.push_back(DockEdit::show(panel("tools.alpha")));
    shows.edits.push_back(DockEdit::show(panel("tools.beta")));
    require(static_cast<bool>(layout.apply(shows)), "show the restricted panels");

    // Panel layer.
    require_refused(layout, DockEdit::float_subject(subject("tools.fixed"), Rect{0.0, 0.0, 100.0, 100.0}, ""),
                    DockRefusalReason::placement_not_allowed, DockRefusalLayer::panel, "fixed may not float");
    require_refused(layout, DockEdit::dock_into(subject("tools.fixed"), stack_of(layout, "tools.solution")),
                    DockRefusalReason::tabbing_not_allowed, DockRefusalLayer::panel, "fixed may not tab");
    require_refused(layout, DockEdit::dock_into(subject("tools.output"), stack_of(layout, "tools.fixed")),
                    DockRefusalReason::tabbing_not_allowed, DockRefusalLayer::panel, "nothing tabs with fixed");
    require_refused(layout, DockEdit::hide(panel("tools.fixed")), DockRefusalReason::placement_not_allowed,
                    DockRefusalLayer::panel, "fixed may not hide");
    require_refused(layout, DockEdit::close(panel("tools.fixed")), DockRefusalReason::placement_not_allowed,
                    DockRefusalLayer::panel, "fixed may not close");
    require_refused(layout, DockEdit::dock_at_edge(subject("doc.a"), DockSide::left),
                    DockRefusalReason::kind_mismatch, DockRefusalLayer::panel,
                    "a document may not dock outside the document area");
    require_refused(layout, DockEdit::dock_into(subject("tools.output"), layout.document_area()),
                    DockRefusalReason::kind_mismatch, DockRefusalLayer::panel,
                    "a tool panel may not enter the document area");
    require(static_cast<bool>(apply_one(layout, DockEdit::dock_into(subject("tools.preview"),
                                                                     layout.document_area()))),
            "a dock-as-document tool enters the document area");
    require_refused(layout, DockEdit::dock_into(subject("doc.a"), stack_of(layout, "tools.output")),
                    DockRefusalReason::kind_mismatch, DockRefusalLayer::panel, "a document may not tab with tools");

    // Group layer.
    require_refused(layout, DockEdit::dock_into(subject("tools.beta"), stack_of(layout, "tools.alpha")),
                    DockRefusalReason::group_not_admitted, DockRefusalLayer::group, "beta may not join alpha");
    require_refused(layout, DockEdit::dock_into(subject("tools.output"), stack_of(layout, "tools.alpha")),
                    DockRefusalReason::group_not_admitted, DockRefusalLayer::group,
                    "the default group may not join alpha");
    DockGroupDescriptor admits{};
    admits.id = DockGroupId("alpha");
    admits.admits.push_back(DockGroupId("beta"));
    layout.set_group(admits);
    require(static_cast<bool>(apply_one(layout, DockEdit::dock_into(subject("tools.beta"),
                                                                     stack_of(layout, "tools.alpha")))),
            "beta joins alpha once alpha admits beta");

    // Site layer.
    DockSiteRestrictions tools_only{};
    tools_only.accepts_documents = false;
    layout.set_floating_site_restrictions(tools_only);
    require_refused(layout, DockEdit::float_subject(subject("doc.b"), Rect{0.0, 0.0, 100.0, 100.0}, ""),
                    DockRefusalReason::site_refuses_kind, DockRefusalLayer::site,
                    "floating sites refuse documents");
    DockSiteRestrictions only_alpha{};
    only_alpha.admitted_groups.push_back(DockGroupId("alpha"));
    layout.set_floating_site_restrictions(only_alpha);
    require_refused(layout, DockEdit::float_subject(subject("tools.output"), Rect{0.0, 0.0, 100.0, 100.0}, ""),
                    DockRefusalReason::site_refuses_group, DockRefusalLayer::site,
                    "floating sites admit only alpha");
    layout.set_floating_site_restrictions(DockSiteRestrictions{});
    layout.set_user_docking_locked(true);
    require_refused(layout, DockEdit::float_subject(subject("tools.output"), Rect{0.0, 0.0, 100.0, 100.0}, ""),
                    DockRefusalReason::user_docking_locked, DockRefusalLayer::site, "locked against the user",
                    DockEditOrigin::user);
    require(static_cast<bool>(apply_one(layout, DockEdit::select(panel("doc.a")), DockEditOrigin::user)),
            "selection is allowed while locked");
    require(static_cast<bool>(apply_one(layout, DockEdit::float_subject(subject("tools.output"),
                                                                         Rect{0.0, 0.0, 100.0, 100.0}, ""))),
            "the program may still move panels");
    layout.set_user_docking_locked(false);

    // Model.
    require_refused(layout, DockEdit::show(panel("tools.nobody")), DockRefusalReason::unknown_panel,
                    DockRefusalLayer::model, "unknown panel");
    require_refused(layout, DockEdit::dock_into(subject("tools.output"), DockNodeId{999}),
                    DockRefusalReason::unknown_node, DockRefusalLayer::model, "unknown node");
    require_refused(layout, DockEdit::dock_beside(subject("tools.output"), layout.document_area(), DockSide::left,
                                                  1.0),
                    DockRefusalReason::invalid_ratio, DockRefusalLayer::model, "ratio 1 is outside (0, 1)");
    require_refused(layout, DockEdit::float_subject(subject("tools.solution"), Rect{0.0, 0.0, 0.0, 10.0}, ""),
                    DockRefusalReason::invalid_frame, DockRefusalLayer::model, "an empty frame");
    require_refused(layout, DockEdit::dock_beside(subject("tools.solution"), stack_of(layout, "tools.solution"),
                                                  DockSide::left),
                    DockRefusalReason::target_is_subject, DockRefusalLayer::model,
                    "a lone panel beside its own stack");
    require_refused(layout, DockEdit::show(panel("tools.solution")), DockRefusalReason::already_placed,
                    DockRefusalLayer::model, "showing a placed panel");
    require_refused(layout, DockEdit::set_ratio(stack_of(layout, "tools.solution"), 0.5),
                    DockRefusalReason::not_a_split, DockRefusalLayer::model, "a stack has no ratio");
    require(static_cast<bool>(layout.check_invariants()), "valid after the restriction checks");
}

void test_floating_limit() {
    DockLayout layout{};
    for (std::size_t index = 0; index <= gui_forms::dock_maximum_floating_sites; ++index) {
        const std::string id = "tools.t" + std::to_string(index);
        require(layout.register_panel(tool(id)), "register a tool");
    }
    DockTransaction floats{};
    for (std::size_t index = 0; index < gui_forms::dock_maximum_floating_sites; ++index) {
        const std::string id = "tools.t" + std::to_string(index);
        floats.edits.push_back(DockEdit::float_subject(subject(id), Rect{0.0, 0.0, 100.0, 100.0}, ""));
    }
    require(static_cast<bool>(layout.apply(floats)), "32 floating sites");
    const std::string last = "tools.t" + std::to_string(gui_forms::dock_maximum_floating_sites);
    require_refused(layout, DockEdit::float_subject(subject(last), Rect{0.0, 0.0, 100.0, 100.0}, ""),
                    DockRefusalReason::floating_site_limit, DockRefusalLayer::model, "the 33rd floating site");
}

void test_pins() {
    DockLayout layout{};
    register_standard(layout);
    require(layout.register_panel(tool("tools.toolbox")), "register toolbox");
    show_standard(layout);
    const DockNodeId solution = stack_of(layout, "tools.solution");
    require(layout.pin_owner(panel("tools.solution")) == solution, "a lone panel's pin is its stack's");
    require(static_cast<bool>(apply_one(layout, DockEdit::dock_into(subject("tools.toolbox"), solution))),
            "toolbox tabs into solution");
    require(layout.pin_owner(panel("tools.toolbox")) == solution, "a group's panels share the group's pin");
    require_refused(layout, DockEdit::auto_hide(subject("tools.toolbox"), DockSide::left),
                    DockRefusalReason::not_root_level, DockRefusalLayer::model,
                    "a panel inside a group has no pin of its own");
    require(!layout.pin_owner(panel("doc.a")).has_value(), "documents carry no pin");
    std::vector<DockNodeId> pinnable{};
    layout.collect_pinnable_items(pinnable);
    require(pinnable.size() == 3U, "three docked tool stacks carry pins");
    require(static_cast<bool>(apply_one(layout, DockEdit::auto_hide(DockSubject::of_stack(solution),
                                                                     DockSide::left))),
            "the group auto-hides as a unit");
    require(layout.locate(panel("tools.toolbox")).placement == DockPlacementKind::auto_hidden &&
                layout.locate(panel("tools.solution")).placement == DockPlacementKind::auto_hidden,
            "both panels auto-hidden");
    require(layout.pin_owner(panel("tools.toolbox")) == solution, "the auto-hidden group keeps its pin");
    layout.collect_pinnable_items(pinnable);
    require(pinnable.size() == 3U && pinnable.back() == solution, "auto-hidden stacks are listed last");
    require(static_cast<bool>(apply_one(layout, DockEdit::float_subject(subject("tools.output"),
                                                                         Rect{0.0, 0.0, 100.0, 100.0}, ""))),
            "output floats");
    require(!layout.pin_owner(panel("tools.output")).has_value(), "a floating stack carries no pin");
    require_refused(layout, DockEdit::auto_hide(subject("tools.output"), DockSide::left),
                    DockRefusalReason::not_in_main_site, DockRefusalLayer::model,
                    "a floating stack cannot auto-hide");
    require(static_cast<bool>(apply_one(layout, DockEdit::unpin(solution))), "unpin the group");
    require(layout.locate(panel("tools.toolbox")).stack == solution &&
                layout.locate(panel("tools.toolbox")).placement == DockPlacementKind::docked,
            "the group returns docked with its ID");
    require_refused(layout, DockEdit::unpin(solution), DockRefusalReason::not_auto_hidden, DockRefusalLayer::model,
                    "unpinning a docked stack");
}

// Pinning out and unpinning are inverse edits (DockAutoHideEntry): with
// nothing between, every stack in the standard layout comes back to the
// state it left, the split's identity and ratio included, and the saved
// layout is byte for byte the same; an auto-hidden layout saves and
// restores the split to recreate.
void test_pin_round_trip_exact() {
    DockLayout layout{};
    register_standard(layout);
    show_standard(layout);
    require(static_cast<bool>(apply_one(layout, DockEdit::set_ratio(DockNodeId{3}, 0.1))),
            "a ratio that 1 - (1 - r) does not give back exactly");
    const std::array<std::string_view, 3> tools{"tools.solution", "tools.properties", "tools.output"};
    for (const std::string_view tool : tools) {
        const DockLayoutState before = layout.state();
        const std::string saved = layout.write_xml().text;
        const DockNodeId stack = layout.locate(panel(tool)).stack;
        require(static_cast<bool>(apply_one(layout, DockEdit::auto_hide(DockSubject::of_stack(stack), DockSide::left))),
                "the stack pins out");
        const std::string pinned = layout.write_xml().text;
        require(pinned.find(" split=\"") != std::string::npos && pinned.find(" split_ratio=\"") != std::string::npos,
                "the saved auto-hide entry names the split to recreate");
        DockLayout restored{};
        register_standard(restored);
        const DockRestoreResult result = restored.restore_xml(pinned, DockRestoreOptions{});
        require(result.status == DockRestoreStatus::restored && restored.write_xml().text == pinned,
                "an auto-hidden layout restores and writes again the same");
        require(static_cast<bool>(apply_one(restored, DockEdit::unpin(stack))) && restored.write_xml().text == saved,
                "unpinning the restored layout gives the layout before the pin");
        require(static_cast<bool>(apply_one(layout, DockEdit::unpin(stack))), "the stack docks back");
        require(layout.state() == before && layout.write_xml().text == saved,
                "pin then unpin leaves the layout exactly as it was");
    }
    require(static_cast<bool>(layout.check_invariants()), "valid after the round trips");
}

void test_transactions() {
    DockLayout layout{};
    register_standard(layout);
    show_standard(layout);
    ChangeRecorder recorder{};
    gui_forms::on(layout.changed(), recorder, &ChangeRecorder::record);

    DockTransaction failing{};
    failing.edits.push_back(DockEdit::hide(panel("tools.output")));
    failing.edits.push_back(DockEdit::float_subject(subject("tools.properties"), Rect{0.0, 0.0, 50.0, 50.0}, ""));
    failing.edits.push_back(DockEdit::show(panel("tools.missing")));
    require_refused(layout, failing, DockRefusalReason::unknown_panel, DockRefusalLayer::model,
                    "the third edit refuses the whole transaction");
    const DockTransactionResult refused = layout.apply(failing);
    require(refused.refusal.edit_index == 2U, "the refusal names the third edit");
    require(layout.locate(panel("tools.output")).placement == DockPlacementKind::docked,
            "the first edit was not applied");

    DockTransaction moving{};
    moving.origin = DockEditOrigin::user;
    moving.edits.push_back(DockEdit::hide(panel("tools.output")));
    moving.edits.push_back(DockEdit::float_subject(subject("tools.properties"), Rect{0.0, 0.0, 50.0, 50.0}, ""));
    moving.edits.push_back(DockEdit::dock_into(subject("tools.output"), stack_of(layout, "tools.solution")));
    const std::uint64_t before = layout.generation();
    const DockTransactionResult checked = layout.check(moving);
    require(checked.applied && layout.generation() == before && recorder.count == 0U, "check commits nothing");
    const DockTransactionResult applied = layout.apply(moving);
    require(applied.applied && applied.generation == before + 1U, "one generation per transaction");
    require(recorder.count == 1U && recorder.last.edit_count == 3U && recorder.last.origin == DockEditOrigin::user,
            "one change for three edits");
    const DockTransactionResult empty = layout.apply(DockTransaction{});
    require(empty.applied && recorder.count == 1U && layout.generation() == before + 1U,
            "an empty transaction publishes nothing");
}

void test_split_solver_and_arrange() {
    require(solve_dock_split(100.0, 0.25, 0.0, 0.0) == DockSplitSizes{25.0, 75.0, true}, "plain ratio");
    require(solve_dock_split(100.0, 0.1, 20.0, 10.0) == DockSplitSizes{20.0, 80.0, true}, "first minimum");
    require(solve_dock_split(100.0, 0.95, 0.0, 30.0) == DockSplitSizes{70.0, 30.0, true}, "second minimum");
    require(solve_dock_split(20.0, 0.5, 15.0, 5.0) == DockSplitSizes{15.0, 5.0, true}, "exactly the minimums");
    require(solve_dock_split(20.0, 0.5, 30.0, 10.0) == DockSplitSizes{15.0, 5.0, false},
            "too small: in proportion to the minimums");
    require(solve_dock_split(-5.0, 0.5, 0.0, 0.0) == DockSplitSizes{0.0, 0.0, false}, "negative span");
    require(solve_dock_split(std::nan(""), 0.5, 0.0, 0.0).first == 0.0, "span not finite");
    require(solve_dock_split(100.0, std::nan(""), 0.0, 0.0) == DockSplitSizes{50.0, 50.0, true},
            "ratio not finite is 0.5");

    DockLayout layout{};
    DockPanelDescriptor solution = tool("tools.solution");
    solution.minimum_size = Size{220.0, 50.0};
    require(layout.register_panel(solution), "register solution");
    DockPanelDescriptor output = tool("tools.output", DockSide::bottom);
    output.minimum_size = Size{10.0, 120.0};
    require(layout.register_panel(output), "register output");
    require(layout.register_panel(document("doc.a")), "register doc.a");
    DockTransaction shows{};
    shows.edits.push_back(DockEdit::show(panel("tools.solution")));
    shows.edits.push_back(DockEdit::show(panel("tools.output")));
    shows.edits.push_back(DockEdit::show(panel("doc.a")));
    require(static_cast<bool>(layout.apply(shows)), "show three");
    const Size minimum = layout.minimum_size(layout.state().main_root, 4.0);
    require(minimum.width == 224.0 && minimum.height == 174.0, "minimum adds along the axis plus seams");
    std::vector<DockArrangedNode> arranged{};
    layout.arrange(layout.state().main_root, Rect{0.0, 0.0, 800.0, 600.0}, 4.0, arranged);
    require(arranged.size() == 6U, "every node is arranged");
    Rect solution_bounds{};
    Rect output_bounds{};
    for (const DockArrangedNode& node : arranged) {
        if (node.node == stack_of(layout, "tools.solution")) solution_bounds = node.bounds;
        if (node.node == stack_of(layout, "tools.output")) output_bounds = node.bounds;
        require(node.minimums_met, "800 by 600 meets every minimum");
    }
    require(output_bounds == Rect{0.0, 451.0, 800.0, 149.0}, "output gets a quarter of 596");
    require(solution_bounds == Rect{0.0, 0.0, 220.0, 447.0}, "solution keeps its 220 minimum");
    layout.arrange(layout.state().main_root, Rect{0.0, 0.0, 100.0, 100.0}, 4.0, arranged);
    require(!arranged[0].minimums_met, "a 100 by 100 window is too small");
}

DockMonitor monitor(std::string_view id, Rect work_area, bool primary) {
    DockMonitor result{};
    result.id = std::string(id);
    result.work_area = work_area;
    result.primary = primary;
    return result;
}

void test_monitor_fallback() {
    const std::array<DockMonitor, 2> monitors{monitor("left", Rect{0.0, 0.0, 1920.0, 1040.0}, true),
                                              monitor("right", Rect{1920.0, 0.0, 1920.0, 1040.0}, false)};
    const DockMonitorPlacement same = gui_forms::place_on_monitor(Rect{2000.0, 100.0, 800.0, 600.0}, "right",
                                                                  monitors);
    require(same.match == DockMonitorMatch::same_id && !same.clamped && same.monitor_id == "right", "same ID");
    const DockMonitorPlacement overlap = gui_forms::place_on_monitor(Rect{1800.0, 50.0, 300.0, 200.0}, "gone",
                                                                     monitors);
    require(overlap.match == DockMonitorMatch::most_overlap && overlap.monitor_id == "right",
            "most overlap wins");
    const DockMonitorPlacement primary = gui_forms::place_on_monitor(Rect{-5000.0, -5000.0, 300.0, 200.0},
                                                                     "gone", monitors);
    require(primary.match == DockMonitorMatch::primary && primary.monitor_id == "left" && primary.clamped,
            "no overlap: the primary, clamped");
    require(primary.frame == Rect{-236.0, 0.0, 300.0, 200.0}, "64 units of the caption stay visible");
    const DockMonitorPlacement low = gui_forms::place_on_monitor(Rect{100.0, 1030.0, 300.0, 200.0}, "left",
                                                                 monitors);
    require(low.clamped && low.frame.y == 1008.0, "the caption's full height stays inside");
    const DockMonitorPlacement none = gui_forms::place_on_monitor(Rect{1.0, 2.0, 3.0, 4.0}, "x", {});
    require(none.match == DockMonitorMatch::none && none.frame == Rect{1.0, 2.0, 3.0, 4.0}, "no monitors");
}

// Restore fixtures ------------------------------------------------------

const char* const missing_monitor_xml =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<dock_layout format=\"2\" application_version=\"0\" next_id=\"6\">\n"
    "  <main_site>\n"
    "    <window x=\"2000\" y=\"100\" width=\"800\" height=\"600\" monitor=\"right\" maximized=\"false\"/>\n"
    "    <document_area id=\"1\"/>\n"
    "  </main_site>\n"
    "  <floating_site id=\"2\" x=\"1800\" y=\"50\" width=\"300\" height=\"200\" monitor=\"gone\">\n"
    "    <tool_stack id=\"3\" selected=\"0\"><panel id=\"tools.output\"/></tool_stack>\n"
    "  </floating_site>\n"
    "  <floating_site id=\"4\" x=\"-5000\" y=\"-5000\" width=\"300\" height=\"200\" monitor=\"gone\">\n"
    "    <tool_stack id=\"5\" selected=\"0\"><panel id=\"tools.properties\"/></tool_stack>\n"
    "  </floating_site>\n"
    "</dock_layout>\n";

void test_restore_missing_monitor() {
    DockLayout layout{};
    register_standard(layout);
    const std::array<DockMonitor, 2> monitors{monitor("left", Rect{0.0, 0.0, 1920.0, 1040.0}, true),
                                              monitor("right", Rect{1920.0, 0.0, 1920.0, 1040.0}, false)};
    DockRestoreOptions options{};
    options.monitors = monitors;
    const DockRestoreResult result = layout.restore_xml(missing_monitor_xml, options);
    require(result.status == DockRestoreStatus::restored, "missing monitor fixture restores");
    print_diagnostics(result);
    require(has_diagnostic(result, DockRestoreDiagnosticKind::monitor_fallback), "the fallback is reported");
    require(has_diagnostic(result, DockRestoreDiagnosticKind::frame_clamped), "the clamp is reported");
    const DockFloatingSite& overlapping = *layout.find_floating_site(DockSiteId{2});
    require(overlapping.monitor_id == "right" && overlapping.frame == Rect{1800.0, 50.0, 300.0, 200.0},
            "the overlapping window moves to the monitor it overlaps most, unclamped");
    const DockFloatingSite& lost = *layout.find_floating_site(DockSiteId{4});
    require(lost.monitor_id == "left" && lost.frame == Rect{-236.0, 0.0, 300.0, 200.0},
            "the lost window lands on the primary with its caption reachable");
    require((*layout.state().main_window).monitor_id == "right", "the main window keeps its monitor");
    require(layout.state().next_id == 6U, "next ID kept");
}

const char* const removed_plugin_xml =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<dock_layout format=\"2\" application_version=\"0\" next_id=\"7\">\n"
    "  <main_site>\n"
    "    <split id=\"5\" axis=\"horizontal\" ratio=\"0.25\">\n"
    "      <tool_stack id=\"2\" selected=\"1\">\n"
    "        <panel id=\"tools.solution\"/>\n"
    "        <panel id=\"plugin.graph\"/>\n"
    "      </tool_stack>\n"
    "      <split id=\"6\" axis=\"horizontal\" ratio=\"0.75\">\n"
    "        <document_area id=\"1\"/>\n"
    "        <tool_stack id=\"3\" selected=\"0\"><panel id=\"plugin.scope\"/></tool_stack>\n"
    "      </split>\n"
    "    </split>\n"
    "  </main_site>\n"
    "  <panel_record id=\"plugin.graph\" kind=\"tool\" last=\"docked\"/>\n"
    "  <panel_record id=\"plugin.scope\" kind=\"tool\" last=\"docked\"/>\n"
    "  <panel_record id=\"tools.solution\" kind=\"tool\" last=\"docked\"/>\n"
    "</dock_layout>\n";

void test_restore_removed_plugin() {
    DockLayout layout{};
    register_standard(layout);
    const DockRestoreResult result = layout.restore_xml(removed_plugin_xml, DockRestoreOptions{});
    require(result.status == DockRestoreStatus::restored, "removed plugin fixture restores");
    require(has_diagnostic(result, DockRestoreDiagnosticKind::unknown_panel_kept_as_placeholder),
            "unknown panels are reported");
    require(layout.locate(panel("plugin.graph")).placement == DockPlacementKind::hidden &&
                layout.locate(panel("plugin.scope")).placement == DockPlacementKind::hidden,
            "unknown panels are kept as placeholders");
    require(layout.find_node(DockNodeId{3}) == nullptr && layout.find_node(DockNodeId{6}) == nullptr,
            "the emptied stack and its split are pruned");
    std::vector<DockVisiblePanel> visible{};
    layout.collect_visible_panels(visible);
    require(visible.size() == 1U && visible[0].panel == panel("tools.solution") && visible[0].selected,
            "only the known panel is visible, and selected");
    // The plugin returns: its panels go back where they were.
    require(layout.register_panel(tool("plugin.graph")), "register graph");
    require(layout.register_panel(tool("plugin.scope")), "register scope");
    require(static_cast<bool>(apply_one(layout, DockEdit::show(panel("plugin.graph")))), "show graph");
    require(stack_of(layout, "plugin.graph") == DockNodeId{2} && layout.locate(panel("plugin.graph")).index == 1U,
            "graph returns to its stack and index");
    require(static_cast<bool>(apply_one(layout, DockEdit::show(panel("plugin.scope")))), "show scope");
    const DockNode& scope = *layout.find_node(stack_of(layout, "plugin.scope"));
    const DockNode& split = *layout.find_node(scope.parent);
    require(split.second == scope.id && split.first == DockNodeId{1} && split.ratio == 0.75,
            "scope returns to the right of the document area");
}

const char* const format_1_xml =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<dock_layout format=\"1\" application_version=\"0\" next_id=\"5\">\n"
    "  <main_site>\n"
    "    <split id=\"3\" axis=\"horizontal\" ratio=\"0.25\">\n"
    "      <stack kind=\"tool\" id=\"2\" selected=\"0\"><panel id=\"tools.solution\"/></stack>\n"
    "      <document_area id=\"1\">\n"
    "        <stack kind=\"document\" id=\"4\" selected=\"0\"><panel id=\"doc.a\"/></stack>\n"
    "      </document_area>\n"
    "    </split>\n"
    "  </main_site>\n"
    "</dock_layout>\n";

const char* const format_2_equivalent_xml =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<dock_layout format=\"2\" application_version=\"0\" next_id=\"5\">\n"
    "  <main_site>\n"
    "    <split id=\"3\" axis=\"horizontal\" ratio=\"0.25\">\n"
    "      <tool_stack id=\"2\" selected=\"0\">\n"
    "        <panel id=\"tools.solution\"/>\n"
    "      </tool_stack>\n"
    "      <document_area id=\"1\">\n"
    "        <document_stack id=\"4\" selected=\"0\">\n"
    "          <panel id=\"doc.a\"/>\n"
    "        </document_stack>\n"
    "      </document_area>\n"
    "    </split>\n"
    "  </main_site>\n"
    "  <panel_record id=\"doc.a\" kind=\"document\" last=\"docked\"/>\n"
    "  <panel_record id=\"tools.solution\" kind=\"tool\" last=\"docked\"/>\n"
    "</dock_layout>\n";

void test_restore_older_format() {
    DockLayout layout{};
    register_standard(layout);
    const DockRestoreResult result = layout.restore_xml(format_1_xml, DockRestoreOptions{});
    require(result.status == DockRestoreStatus::restored, "format 1 restores");
    require(has_diagnostic(result, DockRestoreDiagnosticKind::older_format_migrated), "the migration is reported");
    require(result.diagnostics.size() == 1U, "and nothing else");
    print_diagnostics(result);
    const DockLayoutWriteResult written = layout.write_xml();
    require(written.text == format_2_equivalent_xml, "format 1 migrates to the format 2 form");
}

const char* const format_3_xml =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<dock_layout format=\"3\" application_version=\"0\" next_id=\"9\" theme=\"dark\">\n"
    "  <named_layouts><layout name=\"debug\"/></named_layouts>\n"
    "  <main_site>\n"
    "    <split id=\"3\" axis=\"horizontal\" ratio=\"0.25\">\n"
    "      <tool_stack id=\"2\" selected=\"0\" pinned_width=\"40\"><panel id=\"tools.solution\"/></tool_stack>\n"
    "      <split id=\"7\" axis=\"vertical\" ratio=\"0.5\">\n"
    "        <document_area id=\"1\"/>\n"
    "        <grid_stack id=\"8\"><panel id=\"tools.output\"/></grid_stack>\n"
    "      </split>\n"
    "    </split>\n"
    "  </main_site>\n"
    "</dock_layout>\n";

void test_restore_newer_format() {
    DockLayout layout{};
    register_standard(layout);
    const DockRestoreResult result = layout.restore_xml(format_3_xml, DockRestoreOptions{});
    require(result.status == DockRestoreStatus::restored, "format 3 restores best-effort");
    print_diagnostics(result);
    require(has_diagnostic(result, DockRestoreDiagnosticKind::newer_format_read), "newer format reported");
    require(has_diagnostic(result, DockRestoreDiagnosticKind::unknown_attribute), "unknown attributes reported");
    require(has_diagnostic(result, DockRestoreDiagnosticKind::unknown_element), "unknown elements reported");
    require(has_diagnostic(result, DockRestoreDiagnosticKind::panel_moved_to_placeholder),
            "a panel inside an unknown element is reported");
    require(layout.locate(panel("tools.output")).placement == DockPlacementKind::hidden,
            "and kept as a placeholder");
    const DockNode& root = *layout.find_node(layout.state().main_root);
    require(root.id == DockNodeId{3} && root.second == DockNodeId{1}, "the split left with one child collapses");
    const DockLayoutWriteResult written = layout.write_xml();
    require(written.text.find("theme=\"dark\"") != std::string::npos &&
                written.text.find("<named_layouts>") != std::string::npos &&
                written.text.find("pinned_width=\"40\"") != std::string::npos &&
                written.text.find("grid_stack") == std::string::npos &&
                written.text.find("format=\"2\"") != std::string::npos,
            "unknown fields are written back (DK decision 48), except an element holding panels; the format "
            "written is this framework's");
}

// A newer layout's unknown fields on every kind of known element, with
// comments, in the writer's own layout (DK decision 48). Restoring and
// saving gives the same text but for the documented normalizations: the
// format attribute is the one this framework writes, and comments are not
// kept.
const std::string unknown_fields_xml =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<!-- written by a newer framework -->\n"
    "<dock_layout format=\"3\" application_version=\"0\" next_id=\"9\" theme=\"dark\">\n"
    "  <named_layouts>\n"
    "    <layout name=\"debug\"/>\n"
    "  </named_layouts>\n"
    "  <main_site>\n"
    "    <window x=\"10\" y=\"20\" width=\"800\" height=\"600\" monitor=\"M\" maximized=\"false\" snapped=\"left\"/>\n"
    "    <split id=\"7\" axis=\"vertical\" ratio=\"0.75\" animate=\"true\">\n"
    "      <split id=\"5\" axis=\"horizontal\" ratio=\"0.75\">\n"
    "        <split id=\"3\" axis=\"horizontal\" ratio=\"0.25\">\n"
    "          <tool_stack id=\"2\" selected=\"0\" pinned_width=\"40\">\n"
    "            <panel id=\"tools.solution\" badge=\"3\"/>\n"
    "            <accent colour=\"blue\"/>\n"
    "          </tool_stack>\n"
    "          <document_area id=\"1\">\n"
    "            <!-- the documents -->\n"
    "            <document_stack id=\"8\" selected=\"1\">\n"
    "              <panel id=\"doc.a\"/>\n"
    "              <panel id=\"doc.b\"/>\n"
    "            </document_stack>\n"
    "            <watermark>Drop files here</watermark>\n"
    "          </document_area>\n"
    "        </split>\n"
    "        <tool_stack id=\"4\" selected=\"0\">\n"
    "          <panel id=\"tools.properties\"/>\n"
    "        </tool_stack>\n"
    "      </split>\n"
    "      <tool_stack id=\"6\" selected=\"0\">\n"
    "        <panel id=\"tools.output\"/>\n"
    "      </tool_stack>\n"
    "    </split>\n"
    "    <guides visible=\"true\"/>\n"
    "  </main_site>\n"
    "  <panel_record id=\"doc.a\" kind=\"document\" last=\"docked\" colour=\"red\"/>\n"
    "  <panel_record id=\"doc.b\" kind=\"document\" last=\"docked\"/>\n"
    "  <panel_record id=\"tools.output\" kind=\"tool\" last=\"docked\"/>\n"
    "  <panel_record id=\"tools.properties\" kind=\"tool\" last=\"docked\">\n"
    "    <docked stack=\"4\" index=\"0\" neighbour=\"0\" side=\"left\" ratio=\"0.25\" pinned=\"yes\"/>\n"
    "  </panel_record>\n"
    "  <panel_record id=\"tools.solution\" kind=\"tool\" last=\"docked\">\n"
    "    <history count=\"2\"/>\n"
    "  </panel_record>\n"
    "  <statistics opened=\"12\"/>\n"
    "</dock_layout>\n";

std::string normalized_unknown_fields_xml() {
    std::string text = unknown_fields_xml;
    const std::array<std::string_view, 2> comments{"<!-- written by a newer framework -->\n",
                                                   "            <!-- the documents -->\n"};
    for (const std::string_view comment : comments) {
        const std::size_t at = text.find(comment);
        if (at != std::string::npos) text.erase(at, comment.size());
    }
    const std::string_view newer = "format=\"3\"";
    const std::size_t at = text.find(newer);
    if (at != std::string::npos) text.replace(at, newer.size(), "format=\"2\"");
    return text;
}

bool contains(const std::string& text, std::string_view part) {
    const bool found = text.find(part) != std::string::npos;
    return found;
}

bool dropped(const DockTransactionResult& result, DockUnknownOwnerKind kind, std::uint32_t id,
             std::string_view panel_id) {
    for (const DockUnknownOwner& owner : result.dropped_unknown_fields) {
        if (owner.kind == kind && owner.id == id && owner.panel.value == panel_id) return true;
    }
    return false;
}

// Unknown fields are kept and written back in place; they survive an edit
// that does not touch their element; an edit that removes an element drops
// its unknown fields and says so.
void test_unknown_fields_kept() {
    DockLayout layout{};
    register_standard(layout);
    const DockRestoreResult result = layout.restore_xml(unknown_fields_xml, DockRestoreOptions{});
    require(result.status == DockRestoreStatus::restored, "the newer layout restores");
    require(has_diagnostic(result, DockRestoreDiagnosticKind::unknown_attribute) &&
                has_diagnostic(result, DockRestoreDiagnosticKind::unknown_element) &&
                !has_diagnostic(result, DockRestoreDiagnosticKind::unknown_fields_dropped),
            "the unknown fields are reported, and none is dropped");
    const std::string expected = normalized_unknown_fields_xml();
    const std::string written = layout.write_xml().text;
    if (written != expected) std::cerr << written;
    require(written == expected,
            "saving writes the unknown fields back in place: the same text but the format and the comments");
    DockLayout again{};
    register_standard(again);
    require(again.restore_xml(written, DockRestoreOptions{}).status == DockRestoreStatus::restored &&
                again.write_xml().text == expected,
            "and a second round trip is stable");

    // An edit that leaves their elements in place keeps them.
    const DockTransactionResult ratio = apply_one(layout, DockEdit::set_ratio(DockNodeId{5}, 0.6));
    require(static_cast<bool>(ratio) && ratio.dropped_unknown_fields.empty(), "a ratio change drops nothing");
    const std::string after_ratio = layout.write_xml().text;
    require(contains(after_ratio, "pinned_width=\"40\"") && contains(after_ratio, "<accent colour=\"blue\"/>") &&
                contains(after_ratio, "badge=\"3\"") && contains(after_ratio, "theme=\"dark\"") &&
                contains(after_ratio, "<watermark>Drop files here</watermark>") &&
                contains(after_ratio, "animate=\"true\""),
            "unknown fields survive an edit that does not touch their element");
    // Moving a panel keeps the fields of its <panel> with it.
    const DockTransactionResult moved = apply_one(
        layout, DockEdit::dock_into(DockSubject::of_panel(panel("tools.solution")), DockNodeId{4}));
    require(static_cast<bool>(moved), "Solution joins Properties");
    require(dropped(moved, DockUnknownOwnerKind::node, 2U, "") &&
                !dropped(moved, DockUnknownOwnerKind::panel, 0U, "tools.solution"),
            "its emptied stack's fields are dropped and reported; the panel's go with it");
    const std::string after_move = layout.write_xml().text;
    require(!contains(after_move, "pinned_width") && !contains(after_move, "<accent") &&
                contains(after_move, "<panel id=\"tools.solution\" badge=\"3\"/>"),
            "the removed stack's fields are gone, the panel's are written where it now stands");
    // Hiding the panel removes its <panel> element and with it its fields.
    const DockTransactionResult hidden = apply_one(layout, DockEdit::hide(panel("tools.solution")));
    require(static_cast<bool>(hidden) && dropped(hidden, DockUnknownOwnerKind::panel, 0U, "tools.solution") &&
                !contains(layout.write_xml().text, "badge"),
            "a hidden panel's reference fields are dropped and reported");
    require(contains(layout.write_xml().text, "<history count=\"2\"/>"), "its record's fields stay");
    require(static_cast<bool>(layout.check_invariants()), "valid throughout");

    // A restore that removes an element (an unregistered panel's stack)
    // drops that element's fields and reports it.
    DockLayout partial{};
    const std::array<std::string_view, 4> registered{"doc.a", "doc.b", "tools.properties", "tools.output"};
    for (const std::string_view id : registered) {
        DockPanelDescriptor descriptor{};
        descriptor.id = panel(id);
        descriptor.kind = id.rfind("doc", 0) == 0 ? DockPanelKind::document : DockPanelKind::tool;
        require(partial.register_panel(descriptor), "a panel registers");
    }
    const DockRestoreResult without = partial.restore_xml(unknown_fields_xml, DockRestoreOptions{});
    require(without.status == DockRestoreStatus::restored &&
                has_diagnostic(without, DockRestoreDiagnosticKind::unknown_fields_dropped) &&
                !contains(partial.write_xml().text, "pinned_width") &&
                contains(partial.write_xml().text, "theme=\"dark\""),
            "a restore that removes an element drops its unknown fields with a diagnostic");
}

void test_restore_corrupt() {
    const std::array<std::pair<const char*, DockRestoreDiagnosticKind>, 12> corrupt{{
        {"", DockRestoreDiagnosticKind::unreadable_xml},
        {"not xml at all", DockRestoreDiagnosticKind::unreadable_xml},
        {"<dock_layout format=\"2\"><main_site>", DockRestoreDiagnosticKind::unreadable_xml},
        {"<!DOCTYPE dock_layout><dock_layout format=\"2\"/>", DockRestoreDiagnosticKind::unreadable_xml},
        {"<other/>", DockRestoreDiagnosticKind::not_a_layout},
        {"<dock_layout format=\"0\"/>", DockRestoreDiagnosticKind::unsupported_format_version},
        {"<dock_layout/>", DockRestoreDiagnosticKind::unsupported_format_version},
        {"<dock_layout format=\"2\"/>", DockRestoreDiagnosticKind::structure_invalid},
        {"<dock_layout format=\"2\"><main_site><split id=\"2\" axis=\"horizontal\" ratio=\"abc\">"
         "<document_area id=\"1\"/><tool_stack id=\"3\"><panel id=\"tools.output\"/></tool_stack>"
         "</split></main_site></dock_layout>",
         DockRestoreDiagnosticKind::invalid_value},
        {"<dock_layout format=\"2\"><main_site><tool_stack id=\"3\"><panel id=\"tools.output\"/></tool_stack>"
         "</main_site></dock_layout>",
         DockRestoreDiagnosticKind::structure_invalid},
        {"<dock_layout format=\"2\"><main_site><split id=\"1\" axis=\"horizontal\" ratio=\"0.5\">"
         "<document_area id=\"1\"/><tool_stack id=\"3\"><panel id=\"tools.output\"/></tool_stack>"
         "</split></main_site></dock_layout>",
         DockRestoreDiagnosticKind::invalid_value},
        {"<dock_layout format=\"2\"><main_site><document_area id=\"1\"/></main_site>"
         "<floating_site id=\"2\" x=\"0\" y=\"0\" width=\"10\" height=\"10\"><document_area id=\"3\"/>"
         "</floating_site></dock_layout>",
         DockRestoreDiagnosticKind::structure_invalid},
    }};
    for (std::size_t index = 0; index < corrupt.size(); ++index) {
        DockLayout layout{};
        register_standard(layout);
        show_standard(layout);
        ChangeRecorder recorder{};
        gui_forms::on(layout.changed(), recorder, &ChangeRecorder::record);
        const DockLayoutState before = layout.state();
        const DockRestoreResult result = layout.restore_xml(corrupt[index].first, DockRestoreOptions{});
        const bool kept = result.status == DockRestoreStatus::kept_current && layout.state() == before &&
                          recorder.count == 0U;
        if (!kept || !has_diagnostic(result, corrupt[index].second)) {
            std::cerr << "FAIL: corrupt fixture " << index << " was not refused as expected\n";
            print_diagnostics(result);
            ++failures;
        }
    }
}

// Identity exhaustion. A layout file cannot carry next_id or a node ID past
// dock_identity_limit, so the count cannot wrap to the null ID; a layout
// whose count has reached the limit restores and round-trips, and an edit
// that needs a new ID is refused with identities_exhausted.
const char* const exhausted_xml =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<dock_layout format=\"2\" application_version=\"0\" next_id=\"4294967294\">\n"
    "  <main_site>\n"
    "    <split id=\"4294967293\" axis=\"horizontal\" ratio=\"0.25\">\n"
    "      <tool_stack id=\"2\" selected=\"0\"><panel id=\"tools.solution\"/></tool_stack>\n"
    "      <document_area id=\"1\"/>\n"
    "    </split>\n"
    "  </main_site>\n"
    "  <panel_record id=\"tools.solution\" kind=\"tool\" last=\"docked\"/>\n"
    "</dock_layout>\n";

void test_identity_limit() {
    require(gui_forms::dock_identity_limit == 4294967294U, "the limit is one below the largest count");
    const std::array<const char*, 4> beyond{{
        "<dock_layout format=\"2\" next_id=\"4294967295\"><main_site><document_area id=\"1\"/>"
        "</main_site></dock_layout>",
        "<dock_layout format=\"2\" next_id=\"2\"><main_site><document_area id=\"4294967295\"/>"
        "</main_site></dock_layout>",
        "<dock_layout format=\"2\" next_id=\"2\"><main_site><document_area id=\"4294967294\"/>"
        "</main_site></dock_layout>",
        "<dock_layout format=\"2\" next_id=\"4294967295\"><main_site><split id=\"4294967295\" "
        "axis=\"horizontal\" ratio=\"0.5\"><document_area id=\"1\"/><tool_stack id=\"3\">"
        "<panel id=\"tools.output\"/></tool_stack></split></main_site></dock_layout>",
    }};
    for (std::size_t index = 0; index < beyond.size(); ++index) {
        DockLayout layout{};
        register_standard(layout);
        show_standard(layout);
        const DockLayoutState before = layout.state();
        const DockRestoreResult result = layout.restore_xml(beyond[index], DockRestoreOptions{});
        const bool kept = result.status == DockRestoreStatus::kept_current && layout.state() == before;
        if (!kept || !has_diagnostic(result, DockRestoreDiagnosticKind::invalid_value)) {
            std::cerr << "FAIL: identity fixture " << index << " was not refused as invalid\n";
            print_diagnostics(result);
            ++failures;
        }
    }

    DockLayout layout{};
    register_standard(layout);
    const DockRestoreResult result = layout.restore_xml(exhausted_xml, DockRestoreOptions{});
    print_diagnostics(result);
    require(result.status == DockRestoreStatus::restored, "a layout at the limit restores");
    require(layout.state().next_id == gui_forms::dock_identity_limit, "next ID at the limit");
    require(static_cast<bool>(layout.check_invariants()), "an exhausted layout is valid");
    require_refused(layout, DockEdit::show(panel("tools.output")), DockRefusalReason::identities_exhausted,
                    DockRefusalLayer::model, "a new stack needs an ID");
    require_refused(layout, DockEdit::float_subject(subject("tools.solution"), Rect{0.0, 0.0, 100.0, 100.0}, ""),
                    DockRefusalReason::identities_exhausted, DockRefusalLayer::model, "a new stack and site");
    require_refused(layout,
                    DockEdit::dock_beside(DockSubject::of_stack(DockNodeId{2}), layout.document_area(),
                                          DockSide::right),
                    DockRefusalReason::identities_exhausted, DockRefusalLayer::model, "a new split needs an ID");
    require(static_cast<bool>(apply_one(layout, DockEdit::set_ratio(DockNodeId{4294967293U}, 0.5))),
            "an edit that needs no new ID still applies");
    require(layout.state().next_id == gui_forms::dock_identity_limit, "refusals leave the count at the limit");
    const DockLayoutWriteResult first = layout.write_xml();
    require(static_cast<bool>(first), "an exhausted layout writes");
    DockLayout restored{};
    register_standard(restored);
    const DockRestoreResult again = restored.restore_xml(first.text, DockRestoreOptions{});
    require(again.status == DockRestoreStatus::restored && restored.state() == layout.state(),
            "an exhausted layout round-trips");
}

// A main site with two document areas is refused, and the diagnostic names
// what is wrong: not "no document area" but not exactly one.
void test_restore_two_document_areas() {
    DockLayout layout{};
    const DockRestoreResult result = layout.restore_xml(
        "<dock_layout format=\"2\"><main_site><split id=\"1\" axis=\"horizontal\" ratio=\"0.5\">"
        "<document_area id=\"2\"/><document_area id=\"3\"/></split></main_site></dock_layout>",
        DockRestoreOptions{});
    bool named = false;
    for (const DockRestoreDiagnostic& diagnostic : result.diagnostics) {
        if (diagnostic.kind != DockRestoreDiagnosticKind::structure_invalid) continue;
        named = diagnostic.detail == "the main site does not hold exactly one document area";
    }
    if (result.status != DockRestoreStatus::kept_current || !named) print_diagnostics(result);
    require(result.status == DockRestoreStatus::kept_current && named,
            "two document areas are refused as not exactly one");
}

struct RenameUpgrade final {
    std::uint32_t seen_version{0};
    bool reject{false};
};

DockUpgradeDecision upgrade_layout(void* context, std::uint32_t saved_version, DockLayoutState& state) {
    RenameUpgrade& upgrade = *static_cast<RenameUpgrade*>(context);
    upgrade.seen_version = saved_version;
    if (upgrade.reject) return DockUpgradeDecision::reject;
    const bool renamed = gui_forms::rename_dock_panel(state, panel("tools.explorer"), panel("tools.solution"));
    const DockUpgradeDecision decision = renamed ? DockUpgradeDecision::accept : DockUpgradeDecision::reject;
    return decision;
}

const char* const application_1_xml =
    "<dock_layout format=\"2\" application_version=\"1\" next_id=\"4\">\n"
    "  <main_site>\n"
    "    <split id=\"3\" axis=\"horizontal\" ratio=\"0.25\">\n"
    "      <tool_stack id=\"2\" selected=\"0\"><panel id=\"tools.explorer\"/></tool_stack>\n"
    "      <document_area id=\"1\"/>\n"
    "    </split>\n"
    "  </main_site>\n"
    "</dock_layout>\n";

void test_restore_application_upgrade() {
    DockLayout layout{};
    register_standard(layout);
    RenameUpgrade upgrade{};
    DockRestoreOptions options{};
    options.application_version = 2;
    options.upgrade = DockLayoutUpgradeHook{.upgrade = upgrade_layout, .context = &upgrade};
    const DockRestoreResult result = layout.restore_xml(application_1_xml, options);
    require(result.status == DockRestoreStatus::restored && upgrade.seen_version == 1U, "the hook ran on version 1");
    require(has_diagnostic(result, DockRestoreDiagnosticKind::application_version_upgraded), "upgrade reported");
    require(stack_of(layout, "tools.solution") == DockNodeId{2}, "the renamed panel stands where the old one stood");
    require(layout.locate(panel("tools.explorer")).placement == DockPlacementKind::absent, "the old name is gone");
    require(layout.state().application_version == 2U, "the layout takes the current application version");

    DockLayout rejecting{};
    register_standard(rejecting);
    RenameUpgrade refuse{};
    refuse.reject = true;
    options.upgrade = DockLayoutUpgradeHook{.upgrade = upgrade_layout, .context = &refuse};
    const DockLayoutState before = rejecting.state();
    const DockRestoreResult rejected = rejecting.restore_xml(application_1_xml, options);
    require(rejected.status == DockRestoreStatus::kept_current && rejecting.state() == before,
            "a rejected upgrade keeps the current layout");
    require(has_diagnostic(rejected, DockRestoreDiagnosticKind::application_rejected), "rejection reported");
}

const char* const lazy_xml =
    "<dock_layout format=\"2\" application_version=\"0\" next_id=\"8\">\n"
    "  <main_site>\n"
    "    <split id=\"3\" axis=\"horizontal\" ratio=\"0.25\">\n"
    "      <tool_stack id=\"2\" selected=\"1\">\n"
    "        <panel id=\"tools.solution\"/><panel id=\"tools.properties\"/><panel id=\"tools.toolbox\"/>\n"
    "      </tool_stack>\n"
    "      <document_area id=\"1\">\n"
    "        <document_stack id=\"4\" selected=\"0\"><panel id=\"doc.a\"/><panel id=\"doc.b\"/></document_stack>\n"
    "      </document_area>\n"
    "    </split>\n"
    "  </main_site>\n"
    "  <auto_hidden edge=\"bottom\" extent=\"200\" neighbour=\"3\" side=\"bottom\" ratio=\"0.25\">\n"
    "    <tool_stack id=\"6\" selected=\"0\"><panel id=\"tools.output\"/></tool_stack>\n"
    "  </auto_hidden>\n"
    "  <panel_record id=\"tools.hidden\" kind=\"tool\" last=\"docked\"/>\n"
    "</dock_layout>\n";

void test_lazy_creation_report() {
    DockLayout layout{};
    register_standard(layout);
    require(layout.register_panel(tool("tools.toolbox")), "register toolbox");
    require(layout.register_panel(tool("tools.hidden")), "register hidden");
    const DockRestoreResult result = layout.restore_xml(lazy_xml, DockRestoreOptions{});
    require(result.status == DockRestoreStatus::restored, "lazy fixture restores");
    print_diagnostics(result);
    std::vector<DockVisiblePanel> visible{};
    layout.collect_visible_panels(visible);
    std::vector<std::string> created{};
    for (const DockVisiblePanel& entry : visible) {
        if (entry.selected && entry.placement != DockPlacementKind::auto_hidden) created.push_back(entry.panel.value);
    }
    require(created.size() == 2U && created[0] == "tools.properties" && created[1] == "doc.a",
            "a factory is needed only for the selected panels of visible stacks");
    require(visible.size() == 6U, "six panels in stacks");
    require(layout.locate(panel("tools.hidden")).placement == DockPlacementKind::hidden, "the hidden one waits");
}

// Random edit sequences ---------------------------------------------------

struct EditRandom final {
    std::uint64_t state{0x2545F4914F6CDD1DULL};

    std::uint64_t next() noexcept {
        state ^= state << 13U;
        state ^= state >> 7U;
        state ^= state << 17U;
        return state;
    }

    std::size_t below(std::size_t bound) noexcept {
        if (bound == 0U) return 0U;
        const std::size_t value = static_cast<std::size_t>(next() % bound);
        return value;
    }
};

void register_random_panels(DockLayout& layout, std::vector<DockPanelId>& panels) {
    for (std::size_t index = 0; index < 8; ++index) {
        DockPanelDescriptor descriptor = tool("tools.r" + std::to_string(index),
                                              static_cast<DockSide>(index % 4U));
        if (index == 1U) descriptor.allowed.floating = false;
        if (index == 2U) descriptor.allowed.tabbed = false;
        if (index == 3U) descriptor.group = DockGroupId("beta");
        if (index == 4U) descriptor.dock_as_document = true;
        if (index == 5U) descriptor.allowed.auto_hidden = false;
        if (index == 6U) descriptor.default_placement.kind = DockPlacementKind::floating;
        if (index == 7U) descriptor.default_placement.kind = DockPlacementKind::auto_hidden;
        require(layout.register_panel(descriptor), "register a random tool");
        panels.push_back(descriptor.id);
    }
    for (std::size_t index = 0; index < 5; ++index) {
        const DockPanelDescriptor descriptor = document("doc.r" + std::to_string(index));
        require(layout.register_panel(descriptor), "register a random document");
        panels.push_back(descriptor.id);
    }
    panels.push_back(panel("tools.unregistered"));
}

DockEdit random_edit(const DockLayout& layout, const std::vector<DockPanelId>& panels, EditRandom& random) {
    const DockLayoutState& state = layout.state();
    std::vector<DockNodeId> nodes{};
    for (const DockNode& node : state.nodes) nodes.push_back(node.id);
    nodes.push_back(DockNodeId{static_cast<std::uint32_t>(random.below(4096))});
    const DockNodeId node = nodes[random.below(nodes.size())];
    const DockPanelId& chosen = panels[random.below(panels.size())];
    const DockSubject subject = random.below(4) == 0U ? DockSubject::of_stack(node) : DockSubject::of_panel(chosen);
    const DockSide side = static_cast<DockSide>(random.below(4));
    const std::array<double, 5> ratios{0.25, 0.5, 0.75, 0.1, 1.5};
    const double ratio = ratios[random.below(ratios.size())];
    const Rect frame{static_cast<double>(random.below(1000)), static_cast<double>(random.below(800)),
                     100.0 + static_cast<double>(random.below(400)), random.below(10) == 0U ? 0.0 : 150.0};
    DockSiteId site{};
    if (!state.floating_sites.empty()) site = state.floating_sites[random.below(state.floating_sites.size())].id;
    const std::uint32_t index = static_cast<std::uint32_t>(random.below(4));
    switch (random.below(13)) {
    case 0: return DockEdit::dock_into(subject, node, index);
    case 1: return DockEdit::dock_beside(subject, node, side, ratio);
    case 2: return DockEdit::dock_at_edge(subject, side, ratio);
    case 3: return DockEdit::float_subject(subject, frame, "M" + std::to_string(random.below(3)));
    case 4: return DockEdit::auto_hide(subject, side, 120.0 + static_cast<double>(random.below(200)));
    case 5: return DockEdit::unpin(node);
    case 6: return DockEdit::hide(chosen);
    case 7: return DockEdit::show(chosen);
    case 8: return DockEdit::close(chosen);
    case 9: return DockEdit::move(chosen, index);
    case 10: return DockEdit::select(chosen);
    case 11: return DockEdit::set_ratio(node, ratio);
    default: return DockEdit::set_floating_frame(site, frame, "M0");
    }
}

void test_random_edit_sequences() {
    DockLayout layout{};
    std::vector<DockPanelId> panels{};
    register_random_panels(layout, panels);
    DockLayout mirror{};
    std::vector<DockPanelId> mirror_panels{};
    register_random_panels(mirror, mirror_panels);
    EditRandom random{};
    const std::size_t steps = 4000;
    std::size_t applied = 0;
    std::size_t round_trips = 0;
    std::size_t largest_tree = 0;
    for (std::size_t step = 0; step < steps; ++step) {
        if (random.below(50) == 0U) layout.set_user_docking_locked(random.below(2) == 0U);
        DockTransaction transaction{};
        transaction.origin = random.below(3) == 0U ? DockEditOrigin::user : DockEditOrigin::program;
        const std::size_t count = 1U + random.below(3);
        for (std::size_t edit = 0; edit < count; ++edit) {
            transaction.edits.push_back(random_edit(layout, panels, random));
        }
        const DockLayoutState before = layout.state();
        const std::uint64_t generation = layout.generation();
        const DockTransactionResult result = layout.apply(transaction);
        const gui_forms::DockInvariantResult invariants = layout.check_invariants();
        if (!invariants) {
            std::cerr << "FAIL: step " << step << " broke an invariant: " << invariants.problem << '\n';
            ++failures;
            return;
        }
        if (result.applied) {
            ++applied;
            require(layout.generation() == generation + 1U, "an applied transaction advances the generation");
        } else {
            require(layout.state() == before && layout.generation() == generation,
                    "a refused random transaction changes nothing");
        }
        largest_tree = std::max(largest_tree, layout.state().nodes.size());
        if (step % 50U == 49U) {
            const DockLayoutWriteResult first = layout.write_xml();
            const DockRestoreResult restored = mirror.restore_xml(first.text, DockRestoreOptions{});
            const DockLayoutWriteResult second = mirror.write_xml();
            const bool stable = static_cast<bool>(first) && restored.status == DockRestoreStatus::restored &&
                                restored.diagnostics.empty() && second.text == first.text &&
                                mirror.state() == layout.state();
            if (!stable) {
                std::cerr << "FAIL: step " << step << " did not round trip\n" << first.text;
                print_diagnostics(restored);
                ++failures;
                return;
            }
            ++round_trips;
        }
    }
    layout.set_user_docking_locked(false);
    require(applied > steps / 10U && applied < steps, "random sequences mix accepted and refused transactions");
    std::cout << "random edits: " << steps << " transactions, " << applied << " applied, " << round_trips
              << " byte-stable round trips, largest tree " << largest_tree << " nodes\n";
}

// Damaged saved layouts: every restore either keeps the current layout or
// produces a valid one that writes again.
void test_mutated_restores() {
    DockLayout source{};
    register_standard(source);
    show_standard(source);
    require(static_cast<bool>(apply_one(source, DockEdit::float_subject(subject("tools.output"),
                                                                         Rect{10.0, 20.0, 300.0, 200.0}, "M"))),
            "output floats");
    require(static_cast<bool>(apply_one(source, DockEdit::auto_hide(subject("tools.solution"), DockSide::left))),
            "solution auto-hides");
    require(static_cast<bool>(apply_one(source, DockEdit::hide(panel("doc.a")))), "doc.a hides");
    const std::string seed = source.write_xml().text;
    const std::array<std::string_view, 10> tokens{"0", "9", "-1", "1e308", "\"", "<", "/>", "</split>",
                                                  "<panel id=\"doc.a\"/>", " id=\"2\""};
    EditRandom random{};
    std::size_t restored = 0;
    const std::size_t iterations = 3000;
    for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
        std::string input = seed;
        const std::size_t mutations = 1U + random.below(4);
        for (std::size_t step = 0; step < mutations; ++step) {
            const std::size_t at = random.below(input.size());
            const std::size_t choice = random.below(6);
            if (choice == 0U) {
                input.erase(at, 1U + random.below(24));
            } else if (choice == 1U) {
                input.insert(at, tokens[random.below(tokens.size())]);
            } else if (choice >= 2U && choice <= 4U) {
                // Changing a digit keeps the document well-formed and
                // reaches the loader's checks on IDs, ratios and frames.
                const std::size_t digit = input.find_first_of("0123456789", at);
                if (digit != std::string::npos) input[digit] = static_cast<char>('0' + random.below(10));
            } else {
                const std::string copied = input.substr(random.below(input.size()), 1U + random.below(48));
                input.insert(at, copied);
            }
        }
        DockLayout layout{};
        register_standard(layout);
        show_standard(layout);
        const DockLayoutState before = layout.state();
        const DockRestoreResult result = layout.restore_xml(input, DockRestoreOptions{});
        if (result.status == DockRestoreStatus::kept_current) {
            if (!(layout.state() == before) || result.diagnostics.empty()) {
                std::cerr << "FAIL: mutation " << iteration << " kept a changed layout or said nothing\n";
                ++failures;
            }
            continue;
        }
        ++restored;
        const gui_forms::DockInvariantResult invariants = layout.check_invariants();
        const DockLayoutWriteResult written = layout.write_xml();
        if (!invariants || !written) {
            std::cerr << "FAIL: mutation " << iteration << " restored an invalid layout: " << invariants.problem
                      << '\n';
            ++failures;
        }
    }
    std::cout << "mutated restores: " << iterations << " damaged layouts, " << restored << " restored\n";
}

} // namespace

// The main site's restrictions (the site layer for the main tools and the
// document area): refusals name the site layer, the floating sites keep
// their own record, and restoring the defaults admits again.
void test_main_site_restrictions() {
    DockLayout layout{};
    register_standard(layout);
    DockPanelDescriptor alpha = tool("tools.alpha");
    alpha.group = DockGroupId("alpha");
    require(layout.register_panel(alpha), "register alpha");
    show_standard(layout);
    require(static_cast<bool>(apply_one(layout, DockEdit::show(panel("tools.alpha")))), "show alpha");

    DockSiteRestrictions no_tools{};
    no_tools.accepts_tools = false;
    layout.set_main_site_restrictions(no_tools);
    require_refused(layout, DockEdit::dock_into(subject("tools.output"), stack_of(layout, "tools.solution")),
                    DockRefusalReason::site_refuses_kind, DockRefusalLayer::site,
                    "a main site without tools refuses a tool docked there");
    require_refused(layout, DockEdit::dock_at_edge(subject("tools.properties"), DockSide::left),
                    DockRefusalReason::site_refuses_kind, DockRefusalLayer::site,
                    "and a tool docked at its edge");
    require(static_cast<bool>(apply_one(layout, DockEdit::float_subject(subject("tools.output"),
                                                                         Rect{0.0, 0.0, 200.0, 100.0}, "")))),
            "the floating sites keep their own restrictions");

    DockSiteRestrictions no_documents{};
    no_documents.accepts_documents = false;
    layout.set_main_site_restrictions(no_documents);
    require(static_cast<bool>(apply_one(layout, DockEdit::float_subject(subject("doc.a"),
                                                                         Rect{0.0, 0.0, 300.0, 200.0}, "")))),
            "a document may still float");
    require_refused(layout, DockEdit::dock_into(subject("doc.a"), layout.document_area()),
                    DockRefusalReason::site_refuses_kind, DockRefusalLayer::site,
                    "a main site without documents refuses one back into the document area");

    DockSiteRestrictions only_alpha{};
    only_alpha.admitted_groups.push_back(DockGroupId("alpha"));
    layout.set_main_site_restrictions(only_alpha);
    require_refused(layout, DockEdit::dock_into(subject("tools.output"), stack_of(layout, "tools.solution")),
                    DockRefusalReason::site_refuses_group, DockRefusalLayer::site,
                    "a main site admitting only alpha refuses the default group");
    require(static_cast<bool>(apply_one(layout, DockEdit::dock_at_edge(subject("tools.alpha"), DockSide::right))),
            "and admits alpha");

    layout.set_main_site_restrictions(DockSiteRestrictions{});
    require(static_cast<bool>(apply_one(layout, DockEdit::dock_into(subject("tools.output"),
                                                                     stack_of(layout, "tools.solution")))) &&
                static_cast<bool>(apply_one(layout, DockEdit::dock_into(subject("doc.a"), layout.document_area()))),
            "the default restrictions admit tools and documents again");
    require(static_cast<bool>(layout.check_invariants()), "valid after the main-site restriction checks");
}

// set_application_version: recorded in the state and the saved layout, not
// a layout change (no generation, no event); a restore with the same
// version reports no upgrade, another version reports one.
void test_application_version() {
    DockLayout layout{};
    register_standard(layout);
    show_standard(layout);
    ChangeRecorder recorder{};
    gui_forms::on(layout.changed(), recorder, &ChangeRecorder::record);
    const std::uint64_t generation = layout.generation();
    layout.set_application_version(7U);
    require(layout.state().application_version == 7U, "the version reads back");
    require(layout.generation() == generation && recorder.count == 0U,
            "setting the version is not a layout change and publishes nothing");
    const DockLayoutWriteResult written = layout.write_xml();
    require(static_cast<bool>(written) && written.text.find("application_version=\"7\"") != std::string::npos,
            "the saved layout carries the version");

    DockLayout same{};
    register_standard(same);
    DockRestoreOptions current{};
    current.application_version = 7U;
    const DockRestoreResult restored = same.restore_xml(written.text, current);
    require(restored.status == DockRestoreStatus::restored &&
                !has_diagnostic(restored, DockRestoreDiagnosticKind::application_version_upgraded) &&
                same.state().application_version == 7U,
            "a restore at the same version reports no upgrade");

    DockLayout newer{};
    register_standard(newer);
    DockRestoreOptions next{};
    next.application_version = 8U;
    const DockRestoreResult upgraded = newer.restore_xml(written.text, next);
    require(upgraded.status == DockRestoreStatus::restored &&
                has_diagnostic(upgraded, DockRestoreDiagnosticKind::application_version_upgraded) &&
                newer.state().application_version == 8U,
            "a restore at another version reports the upgrade and takes the current version");
}

// The refusals of DockEdit::set_ratio and DockEdit::set_floating_frame, and
// the effect of set_ratio on the arranged rectangles.
void test_ratio_and_frame_edits() {
    DockLayout layout{};
    register_standard(layout);
    show_standard(layout);
    const DockNodeId root = layout.state().main_root;
    require((*layout.find_node(root)).kind == DockNodeKind::split, "the standard layout's root is a split");
    std::vector<DockArrangedNode> before{};
    layout.arrange(root, Rect{0.0, 0.0, 1000.0, 600.0}, 0.0, before);
    const double old_ratio = (*layout.find_node(root)).ratio;
    const double new_ratio = old_ratio < 0.5 ? 0.6 : 0.3;
    require(static_cast<bool>(apply_one(layout, DockEdit::set_ratio(root, new_ratio))) &&
                (*layout.find_node(root)).ratio == new_ratio,
            "a valid ratio is set");
    std::vector<DockArrangedNode> after{};
    layout.arrange(root, Rect{0.0, 0.0, 1000.0, 600.0}, 0.0, after);
    require(before.size() == after.size() && before.size() > 1U && !(before[1].bounds == after[1].bounds),
            "a new ratio moves the split's children");
    require_refused(layout, DockEdit::set_ratio(root, 0.0), DockRefusalReason::invalid_ratio,
                    DockRefusalLayer::model, "ratio 0 is outside (0, 1)");
    require_refused(layout, DockEdit::set_ratio(root, std::nan("")), DockRefusalReason::invalid_ratio,
                    DockRefusalLayer::model, "a ratio that is not a number");
    require_refused(layout, DockEdit::set_ratio(DockNodeId{999}, 0.5), DockRefusalReason::unknown_node,
                    DockRefusalLayer::model, "set_ratio on an unknown node");

    require(static_cast<bool>(apply_one(layout, DockEdit::float_subject(subject("tools.output"),
                                                                         Rect{10.0, 10.0, 200.0, 100.0}, "M")))),
            "output floats");
    const DockSiteId site = layout.locate(panel("tools.output")).site;
    require_refused(layout, DockEdit::set_floating_frame(site, Rect{0.0, 0.0, 0.0, 50.0}, "M"),
                    DockRefusalReason::invalid_frame, DockRefusalLayer::model, "an empty floating frame");
    require_refused(layout, DockEdit::set_floating_frame(DockSiteId{999}, Rect{0.0, 0.0, 50.0, 50.0}, "M"),
                    DockRefusalReason::unknown_site, DockRefusalLayer::model, "a frame for an unknown site");
    require((*layout.find_floating_site(site)).frame == Rect{10.0, 10.0, 200.0, 100.0},
            "refused frame edits leave the frame");
}

int main() {
    test_default_layout();
    test_standard_layout_and_format();
    test_round_trip();
    test_hide_and_show_memory();
    test_primitive_edits();
    test_restrictions();
    test_main_site_restrictions();
    test_application_version();
    test_ratio_and_frame_edits();
    test_floating_limit();
    test_pins();
    test_pin_round_trip_exact();
    test_transactions();
    test_split_solver_and_arrange();
    test_monitor_fallback();
    test_restore_missing_monitor();
    test_restore_removed_plugin();
    test_restore_older_format();
    test_restore_newer_format();
    test_unknown_fields_kept();
    test_restore_corrupt();
    test_identity_limit();
    test_restore_two_document_areas();
    test_restore_application_upgrade();
    test_lazy_creation_report();
    test_random_edit_sequences();
    test_mutated_restores();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "dock layout tests passed\n";
    return 0;
}
