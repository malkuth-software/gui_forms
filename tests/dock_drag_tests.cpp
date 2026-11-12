// Dragging in one window (stage DK phase D3; owner decisions DK-24 to DK-26,
// DK-30, DK-41): the target geometry and its table over a fixture layout;
// the drag state machine's traces (threshold, Escape, capture loss,
// deactivation, a layout change, an invalid drop, Ctrl); the place closing
// up at the start with the model unchanged, and Escape leaving the saved
// layout byte-identical; one transaction and one layout pass per drop; the
// stamp's interpolation and reduced motion, and its frames on the window's
// motion clock (T2 E3) from a test time source; caption, tool-tab and
// document-tab drags; the keyboard move mode reaching exactly the targets
// the pointer reaches; restrictions refusing targets.

#include "gui_forms/binding/property_type/property_type.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/dock_site.hpp"
#include "gui_forms/controls/dock_workspace.hpp"
#include "gui_forms/controls/page_strip.hpp"
#include "gui_forms/controls/page_view.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/controls/text_box.hpp"
#include "gui_forms/dirty.hpp"
#include "gui_forms/docking/dock_drop_targets/dock_drop_targets.hpp"
#include "gui_forms/docking/dock_layout/dock_layout.hpp"
#include "gui_forms/docking/types/dock_types.hpp"
#include "gui_forms/engine/motion_clock/motion_clock.hpp"
#include "gui_forms/event/delegate/delegate.hpp"
#include "gui_forms/event/subscription_token/subscription_token.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/rules/rule/rule.hpp"
#include "gui_forms/scheduler/types/scheduler_types.hpp"
#include "gui_forms/semantics/types/semantic_types.hpp"
#include "gui_forms/text/markdown/markdown.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/window/presentation/presentation_types.hpp"
#include "gui_forms/window/window.hpp"
#include "support/fake_framebuffer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "gui_forms_dock_drag_tests: " << message << '\n';
    std::exit(1);
}

Point centre(Rect bounds) {
    const Point point{bounds.x + bounds.width * 0.5, bounds.y + bounds.height * 0.5};
    return point;
}

bool key(Window& window, std::uint32_t physical_key, Modifier modifiers = Modifier::none) {
    KeyEvent event{KeyAction::down, physical_key};
    event.modifiers = modifiers;
    const bool handled = window.dispatch_key(event);
    return handled;
}

void press(Window& window, Point point) {
    static_cast<void>(window.dispatch_pointer({PointerAction::down, PointerButton::primary, point}));
}

void move(Window& window, Point point, Modifier modifiers = Modifier::none) {
    PointerEvent event{PointerAction::move, PointerButton::none, point};
    event.modifiers = modifiers;
    static_cast<void>(window.dispatch_pointer(event));
    window.perform_layout();
}

void release(Window& window, Point point, Modifier modifiers = Modifier::none) {
    PointerEvent event{PointerAction::up, PointerButton::primary, point};
    event.modifiers = modifiers;
    static_cast<void>(window.dispatch_pointer(event));
}

// Runs the drag's frames well past every motion, so a settling stamp ends.
void settle(Window& window) {
    window.perform_layout();
    static_cast<void>(window.poll_frame_schedule(FrameClock::now() + std::chrono::seconds(1)));
    window.perform_layout();
}

class PanelFactory final {
public:
    void create(DockPanelCreation& creation) {
        const std::string id = "content." + creation.panel.value;
        std::shared_ptr<Panel> panel = make_control<Panel>(StableId(id));
        std::shared_ptr<TextBox> edit = make_control<TextBox>(StableId(id + ".edit"));
        (*edit).set_requested_bounds({6.0, 6.0, 80.0, 22.0});
        (*panel).add_child(edit);
        creation.content = panel;
    }
};

DockPanelRegistration tool(PanelFactory& factory, std::string id, std::string title, DockSide side) {
    DockPanelRegistration registration{};
    registration.descriptor.id = DockPanelId(std::move(id));
    registration.descriptor.kind = DockPanelKind::tool;
    registration.descriptor.minimum_size = {100.0, 60.0};
    registration.descriptor.default_placement.side = side;
    registration.descriptor.default_placement.ratio = 0.25;
    registration.title = std::move(title);
    registration.factory = Delegate<DockPanelCreation&>::bind<PanelFactory, &PanelFactory::create>(factory);
    return registration;
}

DockPanelRegistration document(PanelFactory& factory, std::string id, std::string title) {
    DockPanelRegistration registration{};
    registration.descriptor.id = DockPanelId(std::move(id));
    registration.descriptor.kind = DockPanelKind::document;
    registration.descriptor.minimum_size = {120.0, 80.0};
    registration.title = std::move(title);
    registration.factory = Delegate<DockPanelCreation&>::bind<PanelFactory, &PanelFactory::create>(factory);
    return registration;
}

void apply(DockWorkspace& workspace, DockEdit edit) {
    DockTransaction transaction{};
    transaction.edits.push_back(std::move(edit));
    require(workspace.apply(transaction).applied, "a fixture edit applies");
}

struct CountChanges final {
    std::size_t* count{};
    void operator()(const DockLayoutChange&) const { ++(*count); }
};

struct RecordOutcomes final {
    std::vector<DockDragOutcome>* outcomes{};
    void operator()(const DockDragOutcome& outcome) const { (*outcomes).push_back(outcome); }
};

// Solution and Classes tabbed on the left, Properties on the right (it may
// dock as a document), Output at the bottom, two documents; optionally a
// Notes panel in its own dock group (`isolated`), docked at the top. The
// site fills an 800 by 500 window.
struct Fixture final {
    PanelFactory factory{};
    std::unique_ptr<DockWorkspace> workspace;
    std::shared_ptr<DockSite> site;
    std::unique_ptr<Window> window;
    std::size_t changes{};
    std::vector<DockDragOutcome> outcomes{};
    SubscriptionToken changed{};
    SubscriptionToken ended{};
};

std::unique_ptr<Fixture> make_fixture(bool notes = false) {
    std::unique_ptr<Fixture> fixture = std::make_unique<Fixture>();
    (*fixture).workspace = std::make_unique<DockWorkspace>("workspace");
    DockWorkspace& workspace = *(*fixture).workspace;
    PanelFactory& factory = (*fixture).factory;
    require(workspace.register_panel(tool(factory, "solution", "Solution", DockSide::left)) &&
                workspace.register_panel(tool(factory, "classes", "Classes", DockSide::left)),
            "the left panels register");
    DockPanelRegistration properties = tool(factory, "properties", "Properties", DockSide::right);
    properties.descriptor.dock_as_document = true;
    require(workspace.register_panel(properties) &&
                workspace.register_panel(tool(factory, "output", "Output", DockSide::bottom)) &&
                workspace.register_panel(document(factory, "doc.first", "First.txt")) &&
                workspace.register_panel(document(factory, "doc.second", "Second.txt")),
            "the other panels register");
    if (notes) {
        DockPanelRegistration isolated = tool(factory, "notes", "Notes", DockSide::top);
        isolated.descriptor.group = DockGroupId("isolated");
        require(workspace.register_panel(isolated), "Notes registers");
    }
    apply(workspace, DockEdit::show(DockPanelId("doc.first")));
    apply(workspace, DockEdit::show(DockPanelId("doc.second")));
    apply(workspace, DockEdit::show(DockPanelId("solution")));
    const DockNodeId left = workspace.layout().locate(DockPanelId("solution")).stack;
    apply(workspace, DockEdit::dock_into(DockSubject::of_panel(DockPanelId("classes")), left));
    apply(workspace, DockEdit::show(DockPanelId("properties")));
    apply(workspace, DockEdit::show(DockPanelId("output")));
    if (notes) apply(workspace, DockEdit::show(DockPanelId("notes")));
    (*fixture).site = make_control<DockSite>(StableId("site"), workspace);
    (*fixture).window = std::make_unique<Window>((*fixture).site, Size{800.0, 500.0});
    (*(*fixture).window).perform_layout();
    (*fixture).changed = workspace.layout().changed().subscribe(CountChanges{&(*fixture).changes});
    (*fixture).ended = (*(*fixture).site).drag_ended().subscribe(RecordOutcomes{&(*fixture).outcomes});
    return fixture;
}

DockNodeId stack_of(const DockWorkspace& workspace, std::string_view panel) {
    const DockNodeId stack = workspace.layout().locate(DockPanelId(std::string(panel))).stack;
    return stack;
}

const DockSite::Stack* find_stack(const std::vector<DockSite::Stack>& stacks, DockNodeId stack) {
    for (const DockSite::Stack& record : stacks) {
        if (record.stack == stack) return &record;
    }
    return nullptr;
}
// The result points into `stacks`: a temporary list may not be passed.
const DockSite::Stack* find_stack(std::vector<DockSite::Stack>&& stacks, DockNodeId stack) = delete;

Rect stack_bounds(const DockSite& site, DockNodeId stack) {
    const std::vector<DockSite::Stack> stacks = site.realized_stack_items();
    const DockSite::Stack* const record = find_stack(stacks, stack);
    if (record == nullptr) return {};
    return (*record).parts.stack_area.bounds;
}

Rect caption_of(const DockSite& site, DockNodeId stack) {
    const std::vector<DockSite::Stack> stacks = site.realized_stack_items();
    const DockSite::Stack* const record = find_stack(stacks, stack);
    require(record != nullptr, "the stack is realized");
    return (*record).parts.caption_label.bounds;
}

std::string saved(const DockWorkspace& workspace) {
    const DockLayoutWriteResult written = workspace.layout().write_xml();
    require(static_cast<bool>(written), "the layout writes");
    return written.text;
}

// Starts a caption drag of `panel`'s stack and moves it to `to`.
void drag_caption(Fixture& fixture, std::string_view panel, Point to) {
    Window& window = *fixture.window;
    const Point from = centre(caption_of(*fixture.site, stack_of(*fixture.workspace, panel)));
    press(window, from);
    move(window, {from.x + 10.0, from.y + 10.0});
    move(window, to);
}

using TargetKey = std::tuple<int, std::uint32_t, int>;

TargetKey key_of(const DockDropTarget& target) {
    const TargetKey identity{static_cast<int>(target.kind), target.pane.value, static_cast<int>(target.zone)};
    return identity;
}

bool near_square(Rect rect) {
    const bool square = std::fabs(rect.width - rect.height) < 1e-9 && rect.width > 0.0;
    return square;
}

// Ends a drag with Escape and lets the stamp settle.
void cancel_after(Fixture& fixture) {
    require(key(*fixture.window, PhysicalKey::escape), "Escape is taken");
    settle(*fixture.window);
}

// -- geometry ------------------------------------------------------------------

void geometry() {
    const Rect pane{100.0, 100.0, 200.0, 100.0};
    require(dock_zone_at(pane, {110.0, 150.0}) == DockDropZone::left &&
                dock_zone_at(pane, {290.0, 150.0}) == DockDropZone::right &&
                dock_zone_at(pane, {200.0, 105.0}) == DockDropZone::top &&
                dock_zone_at(pane, {200.0, 195.0}) == DockDropZone::bottom &&
                dock_zone_at(pane, {200.0, 150.0}) == DockDropZone::center,
            "a pane's edge bands split it and its middle tabs into it");
    require(dock_zone_at(pane, {150.0, 150.0}) == DockDropZone::left &&
                dock_zone_at(pane, {160.0, 150.0}) == DockDropZone::center,
            "the side bands reach 28 hundredths of the pane");
    const std::array<Rect, dock_drop_zone_count> tiles = dock_cross_tiles(pane, 1.0);
    for (const Rect& tile : tiles) require(pane.contains(tile) && near_square(tile), "the cross lies in the pane");
    require(tiles[static_cast<std::size_t>(DockDropZone::left)].right() <
                tiles[static_cast<std::size_t>(DockDropZone::center)].x,
            "the cross's tiles do not overlap");
    const std::array<Rect, dock_drop_zone_count> small = dock_cross_tiles({0.0, 0.0, 40.0, 40.0}, 1.0);
    require(small[0].width >= DockDropMetrics::minimum_tile && small[0].width < DockDropMetrics::tile,
            "a small pane gets smaller tiles");
    require(dock_drop_highlight(pane, DockDropZone::right, false) == Rect{200.0, 100.0, 100.0, 100.0} &&
                dock_drop_highlight({0.0, 0.0, 800.0, 400.0}, DockDropZone::top, true) == Rect{0.0, 0.0, 800.0, 100.0},
            "a side takes half the pane, an edge a quarter of the window");
    const std::array<DockDropPane, 1> panes{DockDropPane{DockNodeId{7U}, pane}};
    const Rect site{0.0, 0.0, 400.0, 300.0};
    const DockDropHit tile_hit = hit_dock_drop(centre(tiles[static_cast<std::size_t>(DockDropZone::top)]), site, panes, 1.0);
    require(tile_hit.pane == 0U && tile_hit.zone == DockDropZone::top, "a tile wins over the band it lies in");
    const DockDropHit edge = hit_dock_drop({3.0, 150.0}, site, panes, 1.0);
    require(edge.edge && edge.zone == DockDropZone::left, "the window's edge band is a global target");
    const DockDropHit tile_edge = hit_dock_drop(centre(dock_edge_tiles(site, 1.0)[0]), site, panes, 1.0);
    require(tile_edge.edge && tile_edge.zone == DockDropZone::top, "an edge's tile is its global target");
    const DockDropHit gap = hit_dock_drop({50.0, 50.0}, site, panes, 1.0);
    require(gap.over_site && !gap.edge && !gap.pane, "a point between panes is over the site, on no target");
    require(!hit_dock_drop({500.0, 50.0}, site, panes, 1.0).over_site, "outside the site there is nothing");
}

// -- the state machine ---------------------------------------------------------

void threshold_escape_and_close_up() {
    std::unique_ptr<Fixture> fixture = make_fixture();
    DockSite& site = *(*fixture).site;
    DockWorkspace& workspace = *(*fixture).workspace;
    Window& window = *(*fixture).window;
    const std::string before = saved(workspace);
    const DockNodeId output = stack_of(workspace, "output");
    const DockNodeId documents = stack_of(workspace, "doc.second");
    const Rect documents_before = stack_bounds(site, documents);
    const Point from = centre(caption_of(site, output));
    press(window, from);
    require(site.drag_phase() == DockDragPhase::armed && site.drag_subject() &&
                (*site.drag_subject()).kind == DockSubjectKind::stack && (*site.drag_subject()).stack == output,
            "a press on a caption arms a drag of its whole stack");
    move(window, {from.x + 3.0, from.y});
    require(site.drag_phase() == DockDragPhase::armed, "below the threshold the drag stays armed");
    move(window, {from.x, from.y - 4.0});
    require(site.drag_phase() == DockDragPhase::dragging, "four units along either axis start the drag");
    require(stack_bounds(site, output).empty() && site.collapsed_stacks().empty() &&
                stack_bounds(site, documents).height > documents_before.height,
            "the stack leaves its place, which closes up (the documents take it)");
    require((*fixture).changes == 0U && saved(workspace) == before && !site.drag_targets().empty(),
            "the model is unchanged during the drag; the targets are planned");
    require(key(window, PhysicalKey::escape) && site.drag_phase() == DockDragPhase::settling,
            "Escape ends the drag; the stamp settles back");
    settle(window);
    require(site.drag_phase() == DockDragPhase::idle && saved(workspace) == before && (*fixture).changes == 0U &&
                !stack_bounds(site, output).empty() &&
                stack_bounds(site, documents) == documents_before,
            "after Escape the saved layout is byte-identical and the stack is back");
    require((*fixture).outcomes.size() == 1U && (*fixture).outcomes[0].end() == DockDragEnd::escaped,
            "the drag reports its end once");
    release(window, from);
    require((*fixture).outcomes.size() == 1U, "a release after the end does nothing more");
    // A release while armed is an ordinary click.
    press(window, from);
    release(window, from);
    require(site.drag_phase() == DockDragPhase::idle && (*fixture).outcomes.size() == 1U,
            "a press released below the threshold drags nothing");
}

void drop_is_one_transaction() {
    std::unique_ptr<Fixture> fixture = make_fixture();
    DockSite& site = *(*fixture).site;
    DockWorkspace& workspace = *(*fixture).workspace;
    Window& window = *(*fixture).window;
    const Point properties = centre(stack_bounds(site, stack_of(workspace, "properties")));
    drag_caption(*fixture, "output", properties);
    const std::optional<DockDropTarget> target = site.drag_target();
    require(target && (*target).kind == DockDropTargetKind::pane && (*target).zone == DockDropZone::center &&
                (*target).pane == stack_of(workspace, "properties"),
            "over a pane's middle the target tabs into it");
    require(site.parts().drop_area.present && site.parts().drop_area.bounds == (*target).highlight &&
                site.parts().drag_stamp.present,
            "the area the stack would take is tinted and the stamp is shown");
    const std::uint64_t passes = window.metrics_snapshot().arrange_passes;
    release(window, properties);
    window.perform_layout();
    const std::uint64_t used = window.metrics_snapshot().arrange_passes - passes;
    std::cout << "drop: " << (*fixture).changes << " transaction(s), " << used << " layout pass(es)\n";
    require((*fixture).changes == 1U && used == 1U, "a drop is one transaction and one layout pass");
    require(stack_of(workspace, "output") == stack_of(workspace, "properties") &&
                workspace.active_panel() && (*workspace.active_panel()).value == "output",
            "Output is tabbed into Properties' stack and active");
    require((*fixture).outcomes.size() == 1U && (*fixture).outcomes[0].end() == DockDragEnd::dropped &&
                (*(*fixture).outcomes[0].result()).applied,
            "the drag reports its drop");
    settle(window);
    require(site.drag_phase() == DockDragPhase::idle && site.drag_stamp().empty(), "the stamp settled and left");
}

void target_table() {
    std::unique_ptr<Fixture> fixture = make_fixture();
    DockSite& site = *(*fixture).site;
    DockWorkspace& workspace = *(*fixture).workspace;
    Window& window = *(*fixture).window;
    const DockNodeId properties = stack_of(workspace, "properties");
    const DockNodeId documents = stack_of(workspace, "doc.second");
    const Rect pane = stack_bounds(site, properties);
    drag_caption(*fixture, "output", centre(pane));
    const Rect properties_pane = stack_bounds(site, properties);
    const Rect documents_pane = stack_bounds(site, documents);
    struct Row final {
        Point point{};
        bool target{};
        DockDropTargetKind kind{DockDropTargetKind::pane};
        DockNodeId pane{};
        DockDropZone zone{DockDropZone::center};
    };
    const std::vector<Row> rows{
        {centre(properties_pane), true, DockDropTargetKind::pane, properties, DockDropZone::center},
        {{properties_pane.x + 12.0, properties_pane.y + properties_pane.height * 0.5}, true, DockDropTargetKind::pane,
         properties, DockDropZone::left},
        {{properties_pane.x + properties_pane.width * 0.5, properties_pane.bottom() - 12.0}, true,
         DockDropTargetKind::pane, properties, DockDropZone::bottom},
        {{documents_pane.x + documents_pane.width * 0.5, documents_pane.y + 12.0}, true, DockDropTargetKind::pane,
         documents, DockDropZone::top},
        // Output is no document: the document stack's middle refuses it.
        {centre(documents_pane), false, DockDropTargetKind::pane, documents, DockDropZone::center},
        {{3.0, 250.0}, true, DockDropTargetKind::edge, DockNodeId{}, DockDropZone::left},
        {{400.0, 497.0}, true, DockDropTargetKind::edge, DockNodeId{}, DockDropZone::bottom},
        {{797.0, 250.0}, true, DockDropTargetKind::edge, DockNodeId{}, DockDropZone::right},
    };
    for (const Row& row : rows) {
        move(window, row.point);
        const std::optional<DockDropTarget> target = site.drag_target();
        require(target.has_value() == row.target, "the table says whether a point has a target");
        if (!row.target) {
            require(site.parts().drop_cue.present && !site.parts().drop_area.present,
                    "where nothing accepts the drop the rejected cue shows");
            continue;
        }
        require((*target).kind == row.kind && (*target).zone == row.zone &&
                    ((*target).kind == DockDropTargetKind::edge || (*target).pane == row.pane),
                "the table's target");
    }
    // Over a pane its cross shows (and the four edge tiles), the lit tile
    // being the target's; the document stack's centre is refused.
    move(window, centre(documents_pane));
    const std::vector<DockSite::Tile> tiles = site.realized_tiles();
    require(tiles.size() == 9U, "five tiles over the hovered pane and four at the edges");
    std::size_t refused = 0U;
    for (const DockSite::Tile& tile : tiles) {
        if (tile.kind == DockDropTargetKind::pane && tile.zone == DockDropZone::center) {
            require(tile.states.disabled && !tile.states.selected, "the refused centre's tile is disabled");
        }
        if (tile.states.disabled) ++refused;
    }
    require(refused == 1U, "only the refused tile is disabled");
    move(window, centre(properties_pane));
    std::size_t lit = 0U;
    for (const DockSite::Tile& tile : site.realized_tiles()) {
        if (tile.states.selected) ++lit;
    }
    require(lit == 1U, "one tile is lit");
    // Ctrl suppresses docking (D6-1).
    move(window, centre(properties_pane), Modifier::control);
    require(site.drag_suppressed() && !site.drag_target(), "Ctrl held suppresses docking");
    const std::string before = saved(workspace);
    release(window, centre(properties_pane), Modifier::control);
    settle(window);
    require(saved(workspace) == before && (*fixture).changes == 0U && (*fixture).outcomes.size() == 1U &&
                (*fixture).outcomes[0].end() == DockDragEnd::rejected,
            "a release with docking suppressed snaps back");
}

void cancellations() {
    // Capture taken by another control.
    {
        std::unique_ptr<Fixture> fixture = make_fixture();
        drag_caption(*fixture, "output", {400.0, 200.0});
        Window& window = *(*fixture).window;
        const std::string before = saved(*(*fixture).workspace);
        window.capture_pointer(window.find("content.doc.second"));
        require((*fixture).outcomes.size() == 1U && (*fixture).outcomes[0].end() == DockDragEnd::capture_lost &&
                    saved(*(*fixture).workspace) == before,
                "losing the capture to another control cancels");
    }
    // Capture released with no drop following it.
    {
        std::unique_ptr<Fixture> fixture = make_fixture();
        drag_caption(*fixture, "output", {400.0, 200.0});
        Window& window = *(*fixture).window;
        window.release_pointer();
        require((*fixture).outcomes.empty(), "a release of the capture waits for the dispatch to end");
        static_cast<void>(window.drain_posted_work());
        require((*fixture).outcomes.size() == 1U && (*fixture).outcomes[0].end() == DockDragEnd::capture_lost,
                "a capture released with no drop cancels");
    }
    // The window deactivates.
    {
        std::unique_ptr<Fixture> fixture = make_fixture();
        drag_caption(*fixture, "output", {400.0, 200.0});
        (*(*fixture).window).set_active(false);
        require((*fixture).outcomes.size() == 1U && (*fixture).outcomes[0].end() == DockDragEnd::deactivated,
                "deactivating the window cancels");
    }
    // The program changes the layout under the drag.
    {
        std::unique_ptr<Fixture> fixture = make_fixture();
        drag_caption(*fixture, "output", {400.0, 200.0});
        static_cast<void>((*(*fixture).workspace).hide_panel(DockPanelId("properties"), DockEditOrigin::program));
        require((*fixture).outcomes.size() == 1U && (*fixture).outcomes[0].end() == DockDragEnd::layout_changed &&
                    (*(*fixture).site).drag_phase() != DockDragPhase::dragging,
                "a layout change by the program cancels the drag");
    }
    // An invalid drop snaps back.
    {
        std::unique_ptr<Fixture> fixture = make_fixture();
        DockSite& site = *(*fixture).site;
        const Rect documents = stack_bounds(site, stack_of(*(*fixture).workspace, "doc.second"));
        drag_caption(*fixture, "output", centre(documents));
        const std::string before = saved(*(*fixture).workspace);
        require(!site.drag_target(), "the documents' middle refuses Output");
        release(*(*fixture).window, centre(documents));
        settle(*(*fixture).window);
        require((*fixture).outcomes.size() == 1U && (*fixture).outcomes[0].end() == DockDragEnd::rejected &&
                    saved(*(*fixture).workspace) == before && (*fixture).changes == 0U,
                "a release where nothing accepts snaps back with the layout unchanged");
    }
}

// -- the stamp -----------------------------------------------------------------

void stamp_motion() {
    std::unique_ptr<Fixture> fixture = make_fixture();
    DockSite& site = *(*fixture).site;
    Window& window = *(*fixture).window;
    const Rect source = stack_bounds(site, stack_of(*(*fixture).workspace, "output"));
    drag_caption(*fixture, "output", {400.0, 200.0});
    const Rect early = site.drag_stamp();
    require(early.width > 100.0, "the stamp starts near the panel's own size");
    static_cast<void>(window.poll_frame_schedule(FrameClock::now() + std::chrono::milliseconds(400)));
    const Rect stamp = site.drag_stamp();
    const double longer = std::max(stamp.width, stamp.height);
    require(std::fabs(longer - 100.0) < 0.5 && std::fabs(stamp.width / stamp.height - source.width / source.height) < 0.01,
            "after 220 ms the stamp is about 100 units long, the panel's shape");
    require(stamp.contains(Point{400.0, 200.0}), "the stamp follows the pointer, held where it was grabbed");
    require(site.parts().drag_stamp_label.present && stamp.contains(site.parts().drag_stamp_label.bounds),
            "the stamp's label lies in it");
    cancel_after(*fixture);

    // Reduced motion: no interpolation, no settling.
    std::unique_ptr<Fixture> reduced = make_fixture();
    PresentationSettings settings = (*(*reduced).window).presentation_settings();
    settings.reduced_motion = true;
    (*(*reduced).window).set_presentation_settings(settings);
    (*(*reduced).window).perform_layout();
    drag_caption(*reduced, "output", {400.0, 200.0});
    const Rect immediate = (*(*reduced).site).drag_stamp();
    require(std::fabs(std::max(immediate.width, immediate.height) - 100.0) < 0.5,
            "under reduced motion the stamp is small at once");
    require(key(*(*reduced).window, PhysicalKey::escape), "Escape is taken");
    (*(*reduced).window).perform_layout();
    require((*(*reduced).site).drag_phase() == DockDragPhase::idle, "under reduced motion nothing settles");
}

// The test's time for the window's motion clock: moves start at it and the
// window is polled at it, never at the wall clock.
class ManualTime final : public MotionTimeSource {
public:
    [[nodiscard]] FrameTime now() const noexcept override { return time; }
    FrameTime time{std::chrono::seconds(1000)};
};

// Polls every 16 ms of the test's time, laying out after each, until
// nothing is scheduled; returns the motion clock's frames.
std::size_t run_frames(Window& window, ManualTime& time) {
    std::size_t frames = 0U;
    for (int k = 0; k < 100; ++k) {
        time.time += std::chrono::milliseconds(16);
        const FramePollResult result = window.poll_frame_schedule(time.time);
        window.perform_layout();
        require(result.motion_clock_ticks <= 1U,
                "the drag's motion runs on the clock's one request");
        frames += result.motion_clock_ticks;
        if (!result.next_wake) break;
    }
    return frames;
}

// The stamp's frames (T2 E3, GIVEN D1-1): the shrink and the settling are
// each 220 ms, so 14 frames of 16 ms, with the tiles' 100 ms fade inside the
// shrink's on the same request; afterwards nothing is scheduled. Under
// reduced motion and in an occluded window there are none at all.
void stamp_frames() {
    std::unique_ptr<Fixture> fixture = make_fixture();
    DockSite& site = *(*fixture).site;
    Window& window = *(*fixture).window;
    ManualTime time{};
    window.motion_clock().set_time_source(&time);
    // The press arms a request of its own (not the clock's); count from
    // after it.
    const Point from = centre(caption_of(site, stack_of(*(*fixture).workspace, "output")));
    press(window, from);
    const std::uint64_t requests = window.metrics_snapshot().scheduled_frame_requests;
    move(window, {from.x + 10.0, from.y + 10.0});
    move(window, {400.0, 200.0});
    require(site.drag_phase() == DockDragPhase::dragging && window.motion_clock().moving_count() == 2U,
            "the stamp shrinks and the hovered pane's tiles fade in");
    const std::size_t shrink = run_frames(window, time);
    const Rect stamp = site.drag_stamp();
    require(shrink == 14U && std::fabs(std::max(stamp.width, stamp.height) - 100.0) < 0.5,
            "the shrink takes 14 frames, the tile fade inside them, and ends about 100 units long");
    require(window.metrics_snapshot().scheduled_frame_requests - requests == 1U && !window.motion_clock().armed(),
            "the stamp and the tiles shared one frame request, now disconnected");
    time.time += std::chrono::milliseconds(16);
    require(window.poll_frame_schedule(time.time).motion_clock_ticks == 0U, "a still drag has no frames");
    require(key(window, PhysicalKey::escape) && site.drag_phase() == DockDragPhase::settling, "Escape settles");
    window.perform_layout();
    const std::size_t settling = run_frames(window, time);
    require(settling == 14U && site.drag_phase() == DockDragPhase::idle && site.drag_stamp().empty(),
            "the settling takes 14 frames, then the drag is over");
    time.time += std::chrono::milliseconds(16);
    require(window.poll_frame_schedule(time.time).motion_clock_ticks == 0U && !window.motion_clock().armed(),
            "and nothing is scheduled");

    std::unique_ptr<Fixture> reduced = make_fixture();
    Window& calm = *(*reduced).window;
    PresentationSettings settings = calm.presentation_settings();
    settings.reduced_motion = true;
    calm.set_presentation_settings(settings);
    calm.perform_layout();
    drag_caption(*reduced, "output", {400.0, 200.0});
    require(!calm.motion_clock().armed(), "under reduced motion the drag schedules no frames");
    require(key(calm, PhysicalKey::escape), "Escape is taken");
    calm.perform_layout();
    require((*(*reduced).site).drag_phase() == DockDragPhase::idle && !calm.motion_clock().armed(),
            "and ends without settling frames");

    std::unique_ptr<Fixture> hidden = make_fixture();
    Window& occluded = *(*hidden).window;
    occluded.set_occluded(true, FrameClock::now());
    drag_caption(*hidden, "output", {400.0, 200.0});
    const Rect immediate = (*(*hidden).site).drag_stamp();
    require(!occluded.motion_clock().armed() && std::fabs(std::max(immediate.width, immediate.height) - 100.0) < 0.5,
            "in an occluded window the stamp is small at once and nothing is scheduled");
    require(key(occluded, PhysicalKey::escape), "Escape is taken");
    occluded.perform_layout();
    require((*(*hidden).site).drag_phase() == DockDragPhase::idle && !occluded.motion_clock().armed(),
            "and the drag ends at once");
}

// -- tab drags -----------------------------------------------------------------

void tab_drags() {
    std::unique_ptr<Fixture> fixture = make_fixture();
    DockSite& site = *(*fixture).site;
    DockWorkspace& workspace = *(*fixture).workspace;
    Window& window = *(*fixture).window;
    const DockNodeId left = stack_of(workspace, "classes");
    const std::shared_ptr<PageView> view = site.stack_view(left);
    require(view != nullptr, "the left stack is realized");
    Rect tab{};
    for (const PageStrip::Item& item : (*(*view).strip()).realized_items()) {
        if (item.page.value == "classes") tab = item.parts.tab.bounds;
    }
    const Rect strip = (*(*view).strip()).absolute_bounds();
    const Point at{strip.x + tab.x + tab.width * 0.5, strip.y + tab.y + tab.height * 0.5};
    press(window, at);
    move(window, {at.x, at.y - 6.0});
    require(site.drag_phase() == DockDragPhase::dragging && (*site.drag_subject()).kind == DockSubjectKind::panel &&
                (*site.drag_subject()).panel.value == "classes",
            "a tool tab's drag is the docking drag of one panel");
    require(!stack_bounds(site, left).empty() && (*view).pages().count() == 1U && (*fixture).changes == 0U,
            "the stack stays with its other panel; the dragged page left its view");
    move(window, {797.0, 250.0});
    release(window, {797.0, 250.0});
    settle(window);
    require((*fixture).changes == 1U && !(stack_of(workspace, "classes") == stack_of(workspace, "solution")),
            "a drop on the right edge gives Classes a stack of its own");
    const Rect classes = stack_bounds(site, stack_of(workspace, "classes"));
    require(std::fabs(classes.right() - 800.0) < 0.5 && std::fabs(classes.height - 500.0) < 0.5,
            "the global target runs the whole height at the window's right edge");

    // A document tab slides along its strip, then tears off below it.
    const DockNodeId documents = stack_of(workspace, "doc.second");
    const std::shared_ptr<PageView> document_view = site.stack_view(documents);
    Rect document_tab{};
    for (const PageStrip::Item& item : (*(*document_view).strip()).realized_items()) {
        if (item.page.value == "doc.first") document_tab = item.parts.tab.bounds;
    }
    const Rect document_strip = (*(*document_view).strip()).absolute_bounds();
    const Point grab{document_strip.x + document_tab.x + document_tab.width * 0.5,
                     document_strip.y + document_tab.y + document_tab.height * 0.5};
    press(window, grab);
    // The press selects the tab (a select edit) before anything drags.
    const std::size_t pressed = (*fixture).changes;
    move(window, {grab.x + 20.0, grab.y});
    require(site.drag_phase() == DockDragPhase::idle && (*(*document_view).strip()).dragged_page(),
            "along its strip a document tab slides (the strip's live reorder)");
    move(window, {grab.x + 20.0, grab.y + 80.0});
    require(site.drag_phase() == DockDragPhase::dragging && (*site.drag_subject()).panel.value == "doc.first" &&
                !(*(*document_view).strip()).dragged_page(),
            "leaving the strip's band tears the document off");
    require(key(window, PhysicalKey::escape), "Escape is taken");
    settle(window);
    require((*fixture).changes == pressed && (*document_view).pages().count() == 2U,
            "Escape returns the document to its stack, the layout unchanged");
}

// -- the keyboard ---------------------------------------------------------------

void keyboard_reaches_every_target() {
    std::unique_ptr<Fixture> fixture = make_fixture();
    DockSite& site = *(*fixture).site;
    DockWorkspace& workspace = *(*fixture).workspace;
    Window& window = *(*fixture).window;
    const std::string before = saved(workspace);
    require(site.run_position_command(DockPanelId("output"), DockPositionCommand::move) &&
                site.drag_phase() == DockDragPhase::dragging,
            "the position menu's Move starts the keyboard move mode");
    window.perform_layout();
    require(site.drag_target().has_value() && !site.drag_announcement().empty(),
            "it starts on a target and announces it");
    const std::vector<SemanticNode> announced = site.semantic_virtual_children();
    require(!announced.empty() && announced.back().stable_id == "site.announcements" &&
                announced.back().role == SemanticRole::live_region &&
                announced.back().name == site.drag_announcement(),
            "the announcement is the name of the site's live region");
    std::set<TargetKey> planned{};
    for (const DockDropTarget& target : site.drag_targets()) planned.insert(key_of(target));
    // The pointer: a dense grid over the window.
    std::set<TargetKey> pointer{};
    const std::span<const DockDropPane> panes = site.drag_panes();
    const std::vector<DockDropPane> pane_copy(panes.begin(), panes.end());
    const std::vector<DockDropTarget> targets(site.drag_targets().begin(), site.drag_targets().end());
    const Rect surface{0.0, 0.0, 800.0, 500.0};
    for (double y = 0.5; y < 500.0; y += 2.0) {
        for (double x = 0.5; x < 800.0; x += 2.0) {
            const DockDropHit hit = hit_dock_drop({x, y}, surface, pane_copy, 1.0);
            const std::optional<std::size_t> found = find_dock_drop_target(targets, pane_copy, hit);
            if (found) pointer.insert(key_of(targets[*found]));
        }
    }
    // The keyboard: from every Tab stop, every arrow.
    std::set<TargetKey> keyboard{};
    const std::array<std::uint32_t, 4> arrows{PhysicalKey::left, PhysicalKey::right, PhysicalKey::up, PhysicalKey::down};
    for (std::size_t stop = 0; stop < targets.size() + 2U; ++stop) {
        require(key(window, PhysicalKey::tab), "Tab steps to the next pane or edge");
        const std::optional<DockDropTarget> at = site.drag_target();
        require(at.has_value(), "every stop is a target");
        keyboard.insert(key_of(*at));
        for (const std::uint32_t first : arrows) {
            for (const std::uint32_t second : arrows) {
                const std::optional<DockDropTarget> start = site.drag_target();
                static_cast<void>(key(window, first));
                if (site.drag_target()) keyboard.insert(key_of(*site.drag_target()));
                static_cast<void>(key(window, second));
                if (site.drag_target()) keyboard.insert(key_of(*site.drag_target()));
                // Back to the stop: Shift+Tab then Tab.
                static_cast<void>(start);
                require(key(window, PhysicalKey::tab, Modifier::shift) && key(window, PhysicalKey::tab),
                        "Shift+Tab and Tab return to the stop");
            }
        }
    }
    std::cout << "targets: planned " << planned.size() << ", pointer " << pointer.size() << ", keyboard "
              << keyboard.size() << '\n';
    require(pointer == planned && keyboard == planned, "the keyboard reaches exactly the targets the pointer reaches");
    require(saved(workspace) == before && (*fixture).changes == 0U, "stepping changes nothing");
    require(key(window, PhysicalKey::escape) && site.drag_phase() == DockDragPhase::settling, "Escape cancels");
    settle(window);
    require(saved(workspace) == before, "the layout is as it was");
    // Enter commits through the same transaction as a drop: the move_panel
    // key command on the focused Output.
    require(workspace.activate_panel(DockPanelId("output")), "Output is active");
    require(key(window, PhysicalKey::m, Modifier::control | Modifier::alt | Modifier::shift) &&
                site.drag_phase() == DockDragPhase::dragging,
            "Ctrl+Alt+Shift+M starts the move mode");
    window.perform_layout();
    require(key(window, PhysicalKey::enter) && (*fixture).changes == 1U && (*fixture).outcomes.size() == 2U &&
                (*fixture).outcomes[1].end() == DockDragEnd::dropped,
            "Enter commits one transaction");
    settle(window);
}

// -- restrictions --------------------------------------------------------------

void restrictions_refuse_targets() {
    std::unique_ptr<Fixture> fixture = make_fixture(true);
    DockSite& site = *(*fixture).site;
    DockWorkspace& workspace = *(*fixture).workspace;
    Window& window = *(*fixture).window;
    drag_caption(*fixture, "notes", {400.0, 250.0});
    window.perform_layout();
    const DockNodeId properties = stack_of(workspace, "properties");
    bool beside = false;
    bool into = false;
    for (const DockDropTarget& target : site.drag_targets()) {
        if (target.kind != DockDropTargetKind::pane || !(target.pane == properties)) continue;
        if (target.zone == DockDropZone::center) into = true;
        if (target.zone == DockDropZone::left) beside = true;
    }
    require(beside && !into, "a panel of another dock group may split Properties but not tab into it");
    move(window, centre(stack_bounds(site, properties)));
    require(!site.drag_target(), "the refused centre selects nothing");
    bool disabled = false;
    for (const DockSite::Tile& tile : site.realized_tiles()) {
        if (tile.kind == DockDropTargetKind::pane && tile.zone == DockDropZone::center) disabled = tile.states.disabled;
    }
    require(disabled, "its tile is disabled");
    // A locked workspace refuses every user target.
    require(key(window, PhysicalKey::escape), "Escape");
    settle(window);
    workspace.layout().set_user_docking_locked(true);
    drag_caption(*fixture, "notes", {400.0, 250.0});
    window.perform_layout();
    require(site.drag_targets().empty(), "a locked layout plans no targets");
    require(key(window, PhysicalKey::escape), "Escape");
    settle(window);
}

// Ctrl+F6 steps documents in most-recently-used order (owner decision
// DK-46), on along one order while Ctrl is held.
void documents_by_recent_use() {
    std::unique_ptr<Fixture> fixture = make_fixture();
    DockWorkspace& workspace = *(*fixture).workspace;
    Window& window = *(*fixture).window;
    require(workspace.register_panel(document((*fixture).factory, "doc.third", "Third.txt")), "a third document");
    apply(workspace, DockEdit::show(DockPanelId("doc.third")));
    window.perform_layout();
    // Tab order: first, second, third. Use: second, third, then first.
    require(workspace.activate_panel(DockPanelId("doc.second")) && workspace.activate_panel(DockPanelId("doc.third")) &&
                workspace.activate_panel(DockPanelId("doc.first")),
            "the documents activate in turn");
    require(key(window, PhysicalKey::f6, Modifier::control) && (*workspace.active_document()).value == "doc.third",
            "Ctrl+F6 goes to the document used before (not the next tab, Second)");
    require(key(window, PhysicalKey::f6, Modifier::control) && (*workspace.active_document()).value == "doc.second",
            "with Ctrl held it steps on along the same order");
    KeyEvent up{KeyAction::up, PhysicalKey::left_control};
    static_cast<void>(window.dispatch_key(up));
    require(key(window, PhysicalKey::f6, Modifier::control) && (*workspace.active_document()).value == "doc.third",
            "after Ctrl's release the order starts again from the new most recent");
    require(key(window, PhysicalKey::f6, Modifier::control | Modifier::shift) &&
                (*workspace.active_document()).value == "doc.second",
            "Ctrl+Shift+F6 steps back along the run");
}

// Where dragging meets auto-hide (phase D5): an auto-hidden stack dragged
// out of its flyout by the caption; Escape leaves it auto-hidden and the
// layout byte-identical; a drop docks it in one transaction. And a window
// shrinking under a drag collapses stacks behind side tabs, the targets
// following, the drag going on.
void auto_hide_meets_dragging() {
    std::unique_ptr<Fixture> fixture = make_fixture();
    DockSite& site = *(*fixture).site;
    DockWorkspace& workspace = *(*fixture).workspace;
    Window& window = *(*fixture).window;
    PresentationSettings settings = window.presentation_settings();
    settings.reduced_motion = true;
    window.set_presentation_settings(settings);
    require(site.run_position_command(DockPanelId("properties"), DockPositionCommand::auto_hide) &&
                workspace.layout().locate(DockPanelId("properties")).placement == DockPlacementKind::auto_hidden,
            "Properties is auto-hidden");
    window.perform_layout();
    const std::string pinned = saved(workspace);
    const std::size_t changes = (*fixture).changes;
    const DockNodeId stack = stack_of(workspace, "properties");
    for (int attempt = 0; attempt < 2; ++attempt) {
        require(site.open_flyout(DockPanelId("properties"), DockFlyoutCause::programmatic), "the flyout opens");
        window.perform_layout();
        const Point from = centre(caption_of(site, stack));
        press(window, from);
        move(window, {from.x - 10.0, from.y + 10.0});
        require(site.drag_phase() == DockDragPhase::dragging && (*site.drag_subject()).stack == stack &&
                    !site.flyout_open(),
                "the flyout's caption drags its stack out, closing the flyout");
        const Point output = centre(stack_bounds(site, stack_of(workspace, "output")));
        move(window, output);
        require(site.drag_target() && (*site.drag_target()).zone == DockDropZone::center, "over Output's middle");
        if (attempt == 0) {
            require(key(window, PhysicalKey::escape), "Escape is taken");
            settle(window);
            require(saved(workspace) == pinned && (*fixture).changes == changes &&
                        workspace.layout().locate(DockPanelId("properties")).placement == DockPlacementKind::auto_hidden,
                    "after Escape the stack is still auto-hidden and the layout is as it was");
            continue;
        }
        release(window, output);
        settle(window);
        require((*fixture).changes == changes + 1U &&
                    workspace.layout().locate(DockPanelId("properties")).placement == DockPlacementKind::docked &&
                    stack_of(workspace, "properties") == stack_of(workspace, "output") &&
                    workspace.layout().state().auto_hidden.empty(),
                "a drop docks the auto-hidden stack in one transaction");
    }
    require(static_cast<bool>(workspace.layout().check_invariants()), "the layout stays valid");

    // The window shrinks during a drag.
    std::unique_ptr<Fixture> shrinking = make_fixture();
    DockSite& small_site = *(*shrinking).site;
    Window& small_window = *(*shrinking).window;
    const std::string before = saved(*(*shrinking).workspace);
    drag_caption(*shrinking, "output", {400.0, 200.0});
    small_window.resize({250.0, 260.0});
    small_window.perform_layout();
    require(small_site.drag_phase() == DockDragPhase::dragging && !small_site.collapsed_stacks().empty() &&
                !small_site.drag_panes().empty(),
            "stacks collapse behind side tabs under the drag, which goes on over the panes left");
    for (const DockDropPane& pane : small_site.drag_panes()) {
        for (const DockNodeId collapsed : small_site.collapsed_stacks()) {
            require(!(pane.node == collapsed), "a collapsed stack is no pane");
        }
    }
    require(key(small_window, PhysicalKey::escape), "Escape is taken");
    settle(small_window);
    small_window.resize({800.0, 500.0});
    small_window.perform_layout();
    require(saved(*(*shrinking).workspace) == before && (*shrinking).changes == 0U && small_site.collapsed_stacks().empty(),
            "the layout is as it was and the stacks are back");
}

// The stamp shows the panel's picture where the window can snapshot it, and
// releases it when the drag ends; elsewhere the content well stands in.
void stamp_picture() {
    std::unique_ptr<Fixture> plain = make_fixture();
    drag_caption(*plain, "output", {400.0, 200.0});
    require((*(*plain).site).parts().drag_stamp.present && !(*(*plain).site).parts().drag_stamp_image.present,
            "without a raster the stamp has no picture (the well stands in)");
    cancel_after(*plain);

    std::unique_ptr<Fixture> fixture = make_fixture();
    Window& window = *(*fixture).window;
    test_support::FakeRaster raster{};
    window.set_framebuffer_painter(&raster);
    const std::uint64_t images = window.image_resources().snapshot().resource_count;
    drag_caption(*fixture, "output", {400.0, 200.0});
    const DockSite::Parts parts = (*(*fixture).site).parts();
    require(raster.made == 1U && parts.drag_stamp_image.present && parts.drag_stamp.bounds.contains(parts.drag_stamp_image.bounds),
            "the drag snapshots the panel once and shows the picture in the stamp");
    require(window.image_resources().snapshot().resource_count == images + 1U, "the picture is one image");
    cancel_after(*fixture);
    require(window.image_resources().snapshot().resource_count == images, "the picture is released at the end");
    window.set_framebuffer_painter(nullptr);
}

} // namespace

// Audit N76: a workspace that goes away while the stamp settles leaves no
// layout to settle into; the drag session ends at the next layout pass
// instead of staying in settling, so a new drag can start.
void settling_ends_without_a_workspace() {
    std::unique_ptr<Fixture> fixture = make_fixture();
    DockSite& site = *(*fixture).site;
    Window& window = *(*fixture).window;
    const Point from = centre(caption_of(site, stack_of(*(*fixture).workspace, "output")));
    press(window, from);
    move(window, {from.x, from.y - 20.0});
    require(site.drag_phase() == DockDragPhase::dragging, "the drag starts");
    require(key(window, PhysicalKey::escape) && site.drag_phase() == DockDragPhase::settling,
            "Escape starts the settling");
    (*fixture).changed.disconnect();
    (*fixture).workspace.reset();
    window.perform_layout();
    static_cast<void>(window.poll_frame_schedule(FrameClock::now() + std::chrono::seconds(1)));
    window.perform_layout();
    require(site.drag_phase() == DockDragPhase::idle && site.drag_stamp().empty(),
            "with no workspace the session ends instead of settling forever");
}

int main() {
    geometry();
    threshold_escape_and_close_up();
    drop_is_one_transaction();
    target_table();
    cancellations();
    stamp_motion();
    stamp_frames();
    tab_drags();
    keyboard_reaches_every_target();
    restrictions_refuse_targets();
    documents_by_recent_use();
    auto_hide_meets_dragging();
    stamp_picture();
    settling_ends_without_a_workspace();
    std::cout << "DockSite dragging: geometry, the target table, threshold, Escape, capture loss, deactivation, "
                 "layout changes, invalid drops, Ctrl, one transaction per drop, the stamp and its frames, tab drags, the "
                 "keyboard move mode and restrictions passed\n";
    return EXIT_SUCCESS;
}
