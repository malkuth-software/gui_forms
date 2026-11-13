// Floating windows (stage DK phase D4; owner decisions DK-1, DK-6 to DK-12,
// DK-26, DK-29, DK-41, DK-44) on the headless reference host: the Float
// command and its multi-window trace (created hidden, shown at the commit);
// a release where nothing accepts floats the panel, and snaps back at the
// limit of 32 floating sites; Dock and a caption double-click dock it back,
// destroying the emptied window; a drag from the main window into a
// floating window shows the targets there only; a floating window moved by
// its caption into the main window docks there, the panel appearing in the
// main window before the floating window closes; a user move is written to
// the layout without realizing anything; Ctrl+S from a floating tool window
// reaches the active document; a restored floating window lands inside a
// work area; the close button hides the panels.

#include "gui_forms/application/application.hpp"
#include "gui_forms/binding/property_type/property_type.hpp"
#include "gui_forms/commands/command/command.hpp"
#include "gui_forms/commands/command_route/command_route.hpp"
#include "gui_forms/commands/types/command_types.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/dock_site.hpp"
#include "gui_forms/controls/dock_workspace.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/controls/text_box.hpp"
#include "gui_forms/dirty.hpp"
#include "gui_forms/docking/dock_drop_targets/dock_drop_targets.hpp"
#include "gui_forms/docking/dock_layout/dock_layout.hpp"
#include "gui_forms/docking/types/dock_types.hpp"
#include "gui_forms/event/delegate/delegate.hpp"
#include "gui_forms/event/subscription_token/subscription_token.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/host/windowing/desktop_coordinates.hpp"
#include "gui_forms/host/windowing/host_windowing.hpp"
#include "gui_forms/rules/rule/rule.hpp"
#include "gui_forms/semantics/types/semantic_types.hpp"
#include "gui_forms/surface_root/surface_root.hpp"
#include "gui_forms/text/markdown/markdown.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/window/window.hpp"
#include "application/headless_application.hpp"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "gui_forms_dock_floating_tests: " << message << '\n';
    std::exit(1);
}

Point centre(Rect bounds) {
    const Point point{bounds.x + bounds.width * 0.5, bounds.y + bounds.height * 0.5};
    return point;
}

std::size_t position(const std::string& text, std::string_view fragment) {
    const std::size_t found = text.find(fragment);
    return found;
}

// Notes into the headless trace each time it is arranged in `window`, so a
// test can order the panel's appearance against the native operations.
class TracedPanel final : public Panel {
public:
    TracedPanel(StableId stable_id, host::HeadlessWindowing* const* windowing, const Window* const* window)
        : Panel(std::move(stable_id)), windowing_(windowing), window_(window) {}
    void arrange(Rect final_bounds) override {
        Panel::arrange(final_bounds);
        if (*windowing_ == nullptr || *window_ == nullptr) return;
        if (attached_window() == *window_) (**windowing_).note("arranged " + std::string(stable_id().value()));
    }

private:
    // The factory's, which outlives the panel; set once the run exists.
    host::HeadlessWindowing* const* windowing_{};
    const Window* const* window_{};
};

struct SaveCounter final {
    std::size_t saves{};
    void saved(const CommandInvocation&) { ++saves; }
};

// Builds each panel's content: a panel holding a text box; documents carry
// a command target handling "save"; Properties' panel notes its arrangement
// in the main window once `traced_window` is set.
class PanelFactory final {
public:
    void create(DockPanelCreation& creation) {
        const std::string id = "content." + creation.panel.value;
        std::shared_ptr<Panel> panel{};
        if (creation.panel.value == "properties") {
            panel = make_control<TracedPanel>(StableId(id), &windowing, &traced_window);
        } else {
            panel = make_control<Panel>(StableId(id));
        }
        std::shared_ptr<TextBox> edit = make_control<TextBox>(StableId(id + ".edit"));
        (*edit).set_requested_bounds({6.0, 6.0, 80.0, 22.0});
        (*panel).add_child(edit);
        if (creation.panel.value.rfind("doc.", 0) == 0) {
            std::shared_ptr<CommandTarget> target = std::make_shared<CommandTarget>(creation.panel.value);
            (*target).set_handler("file.save",
                                  Delegate<const CommandInvocation&>::bind<SaveCounter, &SaveCounter::saved>(saves));
            (*panel).set_command_target(target);
            targets.push_back(target);
        }
        creation.content = panel;
    }

    host::HeadlessWindowing* windowing{};
    const Window* traced_window{};
    SaveCounter saves{};
    std::vector<std::shared_ptr<CommandTarget>> targets{};
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

void register_panels(DockWorkspace& workspace, PanelFactory& factory) {
    require(workspace.register_panel(tool(factory, "solution", "Solution", DockSide::left)) &&
                workspace.register_panel(tool(factory, "properties", "Properties", DockSide::right)) &&
                workspace.register_panel(tool(factory, "output", "Output", DockSide::bottom)) &&
                workspace.register_panel(document(factory, "doc.first", "First.txt")),
            "the panels register");
}

void show_panels(DockWorkspace& workspace) {
    const std::vector<std::string> shown{"doc.first", "solution", "properties", "output"};
    for (const std::string& panel : shown) {
        DockTransaction transaction{};
        transaction.edits.push_back(DockEdit::show(DockPanelId(panel)));
        require(workspace.apply(transaction).applied, "a fixture panel shows");
    }
}

struct KeepWindow final {
    ApplicationWindowHandle* handle{};
    Window** model{};
    void operator()(Window& window, ApplicationWindowHandle value) const {
        *handle = value;
        *model = &window;
    }
};

struct RecordOutcomes final {
    std::vector<DockDragOutcome>* outcomes{};
    void operator()(const DockDragOutcome& outcome) const { (*outcomes).push_back(outcome); }
};

// A main window (client area at 100,100 on the desktop, 900 by 600) whose
// root is the main DockSite, with the workspace's windows attached. Members
// go in reverse order: the run (and its windows) first, then the site, then
// the workspace.
struct App final {
    PanelFactory factory{};
    std::unique_ptr<DockWorkspace> workspace{};
    std::shared_ptr<DockSite> site{};
    // The main window's root when the site is held in a panel (make_app's
    // `in_panel`), else null.
    std::shared_ptr<Panel> root{};
    ApplicationWindowHandle main{};
    Window* main_model{};
    std::unique_ptr<host::HeadlessApplication> application{};
    std::vector<DockDragOutcome> outcomes{};
    SubscriptionToken ended{};
};

std::unique_ptr<App> make_app(bool show = true, const std::string* restored = nullptr, bool in_panel = false) {
    std::unique_ptr<App> app = std::make_unique<App>();
    (*app).workspace = std::make_unique<DockWorkspace>("workspace");
    DockWorkspace& workspace = *(*app).workspace;
    register_panels(workspace, (*app).factory);
    if (restored != nullptr) {
        const DockRestoreResult result = workspace.layout().restore_xml(*restored, DockRestoreOptions{});
        require(result.status == DockRestoreStatus::restored, "the saved layout restores");
    } else if (show) {
        show_panels(workspace);
    }
    (*app).site = make_control<DockSite>(StableId("site"), workspace);
    Control::Ptr root = (*app).site;
    if (in_panel) {
        (*app).root = make_control<Panel>(StableId("root"));
        (*(*app).site).set_requested_bounds({0.0, 0.0, 900.0, 600.0});
        (*(*app).root).add_child((*app).site);
        root = (*app).root;
    }
    std::vector<ApplicationWindow> windows{};
    ApplicationWindow primary{};
    primary.stable_id = "main";
    primary.model = std::make_unique<Window>(root, Size{900.0, 600.0});
    primary.options.title = "main";
    primary.options.initial_size = {900.0, 600.0};
    primary.options.minimum_size = {150.0, 150.0};
    primary.options.ready = KeepWindow{&(*app).main, &(*app).main_model};
    windows.push_back(std::move(primary));
    (*app).application = std::make_unique<host::HeadlessApplication>(std::move(windows));
    (*app).factory.windowing = &(*(*app).application).windowing();
    (*(*app).main_model).perform_layout();
    require(workspace.attach_windows((*app).main, "main"), "the workspace attaches to the main window");
    (*(*app).main_model).perform_layout();
    (*app).ended = (*(*app).site).drag_ended().subscribe(RecordOutcomes{&(*app).outcomes});
    return app;
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

Rect caption_of(const DockSite& site, DockNodeId stack) {
    const std::vector<DockSite::Stack> stacks = site.realized_stack_items();
    const DockSite::Stack* const record = find_stack(stacks, stack);
    require(record != nullptr, "the stack is realized");
    return (*record).parts.caption_label.bounds;
}

DockSiteId floating_site_of(const DockWorkspace& workspace, std::string_view panel) {
    const DockSiteId site = workspace.layout().locate(DockPanelId(std::string(panel))).site;
    return site;
}

Window* model_of(App& app, DockSiteId site) {
    const ApplicationWindowHandle handle = (*app.workspace).floating_window(site);
    if (!handle.active()) return nullptr;
    Window* const model = (*app.application).model(handle.id());
    return model;
}

Rect content_of(const ApplicationWindowHandle& handle) {
    const HostWindowFrameResult frame = handle.frame();
    require(frame.status.accepted(), "the window has a frame");
    return frame.frame.content;
}

void lay_out_all(App& app) {
    (*app.main_model).perform_layout();
    for (const DockFloatingSite& site : (*app.workspace).layout().state().floating_sites) {
        Window* const model = model_of(app, site.id);
        if (model != nullptr) (*model).perform_layout();
    }
}

void press(Window& window, Point point, std::uint32_t clicks = 1U) {
    PointerEvent event{PointerAction::down, PointerButton::primary, point};
    event.click_count = clicks;
    static_cast<void>(window.dispatch_pointer(event));
}

void move(Window& window, Point point, Modifier modifiers = Modifier::none) {
    PointerEvent event{PointerAction::move, PointerButton::none, point};
    event.modifiers = modifiers;
    static_cast<void>(window.dispatch_pointer(event));
}

void release(Window& window, Point point) {
    PointerEvent event{PointerAction::up, PointerButton::primary, point};
    static_cast<void>(window.dispatch_pointer(event));
}

// Floats Properties with the position menu's Float, in front of the main
// window's right half.
DockSiteId float_properties(App& app) {
    DockSite& site = *app.site;
    const DockPanelId properties("properties");
    require(site.position_command_enabled(properties, DockPositionCommand::float_panel),
            "Float is enabled once windows are attached");
    require(site.run_position_command(properties, DockPositionCommand::float_panel), "Float runs");
    (*app.application).reclaim();
    lay_out_all(app);
    const DockSiteId floating = floating_site_of(*app.workspace, "properties");
    require(!(floating == DockSiteId{}), "Properties is in a floating site");
    return floating;
}

// -- the Float command and its trace -------------------------------------------

void float_command_trace() {
    std::unique_ptr<App> app = make_app();
    DockWorkspace& workspace = *(*app).workspace;
    host::HeadlessWindowing& windowing = (*(*app).application).windowing();
    const Rect docked = caption_of(*(*app).site, stack_of(workspace, "properties"));
    windowing.clear_trace();
    const DockSiteId floating = float_properties(*app);
    const std::string trace = windowing.trace();
    std::cout << "Float trace:\n" << trace;
    const std::size_t opened = position(trace, "open id=2");
    const std::size_t shown = position(trace, "show id=2");
    require(opened != std::string::npos && trace.find("visible=0", opened) < trace.find("\n", opened),
            "the floating window is created hidden");
    require(shown != std::string::npos && opened < shown, "and shown at the commit, after it is created");
    require(workspace.floating_window_count() == 1U, "one floating window");
    const ApplicationWindowHandle handle = workspace.floating_window(floating);
    Window* const model = model_of(*app, floating);
    require(handle.active() && model != nullptr, "the window is live");
    DockSite* const floating_site = workspace.floating_site(floating);
    require(floating_site != nullptr && (*floating_site).floating() && (*model).root().get() == floating_site,
            "a floating DockSite is the window's root");
    const Control::Ptr content = workspace.panel_content(DockPanelId("properties"));
    require(content && (*content).attached_window() == model, "Properties' content moved into the floating window");
    // The window stands where the panel stood: its frame is the stack's
    // rectangle on the desktop.
    const Rect frame = content_of(handle);
    require(std::abs(frame.x - (100.0 + docked.x)) < 40.0 && frame.width > 50.0,
            "the window opens where the panel stood");
    require((*workspace.layout().find_floating_site(floating)).frame == frame, "the layout holds the window's frame");
    // Dock returns it where it was docked, and the emptied window goes.
    require((*(*app).site).position_command_enabled(DockPanelId("properties"), DockPositionCommand::dock) ||
                (*floating_site).position_command_enabled(DockPanelId("properties"), DockPositionCommand::dock),
            "Dock is enabled for a floating panel");
    windowing.clear_trace();
    require((*floating_site).run_position_command(DockPanelId("properties"), DockPositionCommand::dock), "Dock runs");
    require(workspace.layout().locate(DockPanelId("properties")).placement == DockPlacementKind::docked,
            "Properties is docked again");
    require(position(windowing.trace(), "destroy id=2") != std::string::npos && workspace.floating_window_count() == 0U,
            "the emptied floating window is destroyed");
    (*(*app).application).reclaim();
}

// -- releases where nothing accepts -----------------------------------------------

void release_outside_floats() {
    std::unique_ptr<App> app = make_app();
    DockWorkspace& workspace = *(*app).workspace;
    Window& main = *(*app).main_model;
    const Point from = centre(caption_of(*(*app).site, stack_of(workspace, "output")));
    press(main, from);
    move(main, {from.x + 10.0, from.y + 10.0});
    main.perform_layout();
    // Outside the main window (its client area ends at 900).
    const Point outside{1100.0, 300.0};
    move(main, outside);
    require((*(*app).site).drag_target() == std::nullopt, "nothing accepts outside the window");
    release(main, outside);
    (*(*app).application).reclaim();
    lay_out_all(*app);
    require((*app).outcomes.size() == 1U && (*app).outcomes[0].end() == DockDragEnd::floated,
            "the release floats the stack");
    const DockSiteId floating = floating_site_of(workspace, "output");
    require(!(floating == DockSiteId{}) && workspace.floating_window_count() == 1U, "Output floats in a new window");
    const Rect frame = content_of(workspace.floating_window(floating));
    // The pointer holds the panel where it grabbed it: inside the frame.
    require(frame.contains(Point{100.0 + outside.x, 100.0 + outside.y}), "the window opens under the pointer");

    // A double-click on the floating caption docks it back.
    DockSite& floating_site = *workspace.floating_site(floating);
    Window& floating_window = *model_of(*app, floating);
    const Point caption = centre(caption_of(floating_site, stack_of(workspace, "output")));
    press(floating_window, caption, 1U);
    release(floating_window, caption);
    press(floating_window, caption, 2U);
    release(floating_window, caption);
    (*(*app).application).reclaim();
    require(workspace.layout().locate(DockPanelId("output")).placement == DockPlacementKind::docked &&
                workspace.floating_window_count() == 0U,
            "a double-click on a floating caption docks the stack back and closes its window");
}

void float_limit_snaps_back() {
    std::unique_ptr<App> app = make_app();
    DockWorkspace& workspace = *(*app).workspace;
    // Fill the layout to its 32 floating sites with panels of their own.
    for (std::size_t index = 0; index < dock_maximum_floating_sites; ++index) {
        const std::string id = "extra." + std::to_string(index);
        require(workspace.register_panel(tool((*app).factory, id, id, DockSide::left)), "an extra panel registers");
        DockTransaction transaction{};
        transaction.edits.push_back(DockEdit::show(DockPanelId(id)));
        transaction.edits.push_back(DockEdit::float_subject(DockSubject::of_panel(DockPanelId(id)),
                                                            Rect{200.0 + static_cast<double>(index), 800.0, 120.0, 80.0},
                                                            std::string{}));
        require(workspace.apply(transaction).applied, "an extra panel floats");
    }
    (*(*app).application).reclaim();
    require(workspace.floating_window_count() == dock_maximum_floating_sites, "32 floating windows are open");
    require(!(*(*app).site).position_command_enabled(DockPanelId("output"), DockPositionCommand::float_panel) &&
                !workspace.can_float(DockSubject::of_panel(DockPanelId("output"))),
            "at the limit Float is disabled");
    Window& main = *(*app).main_model;
    main.perform_layout();
    const Point from = centre(caption_of(*(*app).site, stack_of(workspace, "output")));
    press(main, from);
    move(main, {from.x + 10.0, from.y + 10.0});
    main.perform_layout();
    const Point gap{450.0, 250.0};
    // Ctrl suppresses docking: nothing accepts, and the subject may not
    // float, so the rejected cue shows.
    move(main, gap, Modifier::control);
    require(!(*(*app).site).parts().drop_cue.bounds.empty(), "the rejected cue shows where a release snaps back");
    release(main, gap);
    require((*app).outcomes.size() == 1U && (*app).outcomes[0].end() == DockDragEnd::rejected &&
                workspace.layout().locate(DockPanelId("output")).placement == DockPlacementKind::docked,
            "at the limit a release where nothing accepts snaps back");
    (*(*app).application).reclaim();
}

// -- drags across windows ------------------------------------------------------

void drag_into_floating_window() {
    std::unique_ptr<App> app = make_app();
    DockWorkspace& workspace = *(*app).workspace;
    const DockSiteId floating = float_properties(*app);
    // Move the floating window clear of the main window.
    const ApplicationWindowHandle handle = workspace.floating_window(floating);
    (*(*app).application).simulate_user_frame(handle.id(), Rect{1100.0, 200.0, 300.0, 300.0});
    lay_out_all(*app);
    DockSite& floating_site = *workspace.floating_site(floating);
    Window& main = *(*app).main_model;
    const Point from = centre(caption_of(*(*app).site, stack_of(workspace, "output")));
    press(main, from);
    move(main, {from.x + 10.0, from.y + 10.0});
    main.perform_layout();
    // Over the floating window's middle: main-window coordinates of the
    // desktop point (1250, 350).
    const Point over{1250.0 - 100.0, 350.0 - 100.0};
    move(main, over);
    require(floating_site.showing_remote_drag() && !(*(*app).site).drag_target(),
            "the floating window shows the drag; the main window shows no target");
    const std::optional<DockDropTarget> target = floating_site.drag_target();
    require(target && (*target).zone == DockDropZone::center && (*target).kind == DockDropTargetKind::pane,
            "the floating window's pane centre is selected");
    // Only the hovered pane's cross: no window-edge tiles in a floating site.
    require(floating_site.realized_tiles().size() == dock_drop_zone_count, "five tiles, over the hovered pane only");
    require((*(*app).site).realized_tiles().size() == 4U, "the main window shows only its own edges");
    release(main, over);
    (*(*app).application).reclaim();
    require((*app).outcomes.size() == 1U && (*app).outcomes[0].end() == DockDragEnd::dropped &&
                (*app).outcomes[0].across_windows(),
            "the drop lands in the other window");
    require(workspace.layout().locate(DockPanelId("output")).stack == stack_of(workspace, "properties"),
            "Output is tabbed into the floating stack");
    require(!floating_site.showing_remote_drag(), "the floating window stops showing the drag");
}

void move_floating_window_into_main() {
    std::unique_ptr<App> app = make_app();
    DockWorkspace& workspace = *(*app).workspace;
    host::HeadlessWindowing& windowing = (*(*app).application).windowing();
    const DockSiteId floating = float_properties(*app);
    DockSite& floating_site = *workspace.floating_site(floating);
    Window& floating_window = *model_of(*app, floating);
    const ApplicationWindowHandle handle = workspace.floating_window(floating);
    const Rect start = content_of(handle);
    const Point caption = centre(caption_of(floating_site, stack_of(workspace, "properties")));
    const DesktopLogicalPoint grab{start.x + caption.x, start.y + caption.y};
    windowing.set_pointer(grab);
    press(floating_window, caption);
    windowing.set_pointer({grab.x + 12.0, grab.y + 12.0});
    move(floating_window, {caption.x + 12.0, caption.y + 12.0});
    require(floating_site.moving_window(), "the caption of a one-stack floating window moves the window");
    // The window moves with the pointer from where the threshold was met.
    windowing.set_pointer({grab.x + 42.0, grab.y + 32.0});
    move(floating_window, {caption.x, caption.y});
    const Rect moved = content_of(handle);
    require(moved.x == start.x + 30.0 && moved.y == start.y + 20.0, "the window follows the desktop pointer");
    // Over the Solution stack in the main window (left, at about 60, 200).
    const DockNodeId solution = stack_of(workspace, "solution");
    const std::vector<DockSite::Stack> stacks = (*(*app).site).realized_stack_items();
    const DockSite::Stack* const record = find_stack(stacks, solution);
    require(record != nullptr, "Solution is realized");
    const Point over_solution = centre((*record).parts.stack_area.bounds);
    const DesktopLogicalPoint desktop{100.0 + over_solution.x, 100.0 + over_solution.y};
    windowing.set_pointer(desktop);
    move(floating_window, {caption.x + 1.0, caption.y + 1.0});
    require((*(*app).site).showing_remote_drag(), "the main window, under the moving window, shows the targets");
    const std::optional<DockDropTarget> target = (*(*app).site).drag_target();
    require(target && (*target).pane == solution && (*target).zone == DockDropZone::center,
            "the hovered pane's centre is selected");
    (*app).factory.traced_window = (*app).main_model;
    windowing.clear_trace();
    release(floating_window, {caption.x + 1.0, caption.y + 1.0});
    const std::string trace = windowing.trace();
    std::cout << "Redock trace:\n" << trace;
    const std::size_t arranged = position(trace, "arranged content.properties");
    const std::size_t destroyed = position(trace, "destroy id=2");
    require(arranged != std::string::npos && destroyed != std::string::npos && arranged < destroyed,
            "the panel appears in the main window before the floating window closes");
    require(workspace.layout().locate(DockPanelId("properties")).stack == solution &&
                workspace.floating_window_count() == 0U,
            "Properties is tabbed into Solution's stack");
    (*app).factory.traced_window = nullptr;
    (*(*app).application).reclaim();
}

void user_move_is_saved() {
    std::unique_ptr<App> app = make_app();
    DockWorkspace& workspace = *(*app).workspace;
    const DockSiteId floating = float_properties(*app);
    const std::uint64_t tree = workspace.tree_generation();
    const std::uint64_t generation = workspace.layout().generation();
    const Rect moved{700.0, 640.0, 260.0, 220.0};
    (*(*app).application).simulate_user_frame(workspace.floating_window(floating).id(), moved);
    const DockFloatingSite& site = *workspace.layout().find_floating_site(floating);
    require(site.frame == moved && site.monitor_id == "headless.monitor.0", "the moved frame is in the layout");
    require(workspace.layout().generation() == generation + 1U && workspace.tree_generation() == tree,
            "as one frames-only change, which realizes nothing");
    const DockLayoutWriteResult written = workspace.layout().write_xml();
    require(static_cast<bool>(written) && written.text.find("700") != std::string::npos,
            "the floating frame is saved with the layout");
    // The program moving the site moves the window.
    DockTransaction transaction{};
    transaction.edits.push_back(DockEdit::set_floating_frame(floating, Rect{300.0, 300.0, 260.0, 220.0}, std::string{}));
    require(workspace.apply(transaction).applied, "the program sets the frame");
    require(content_of(workspace.floating_window(floating)) == (Rect{300.0, 300.0, 260.0, 220.0}),
            "the window follows the program's frame");
}

// -- commands and keys ---------------------------------------------------------

void ctrl_s_reaches_the_active_document() {
    std::unique_ptr<App> app = make_app();
    DockWorkspace& workspace = *(*app).workspace;
    Window& main = *(*app).main_model;
    require(workspace.activate_panel(DockPanelId("doc.first"), DockActivationCause::programmatic),
            "the document is active");
    const std::shared_ptr<Command> save = std::make_shared<Command>("file.save", "Save");
    AcceleratorToken shortcut = (*save).bind_shortcut(main, KeyGesture{PhysicalKey::s, Modifier::control});
    const DockSiteId floating = float_properties(*app);
    Window& floating_window = *model_of(*app, floating);
    require(workspace.activate_panel(DockPanelId("properties"), DockActivationCause::programmatic),
            "the floating tool panel is active");
    require(floating_window.focused_control() && workspace.active_document() == DockPanelId("doc.first"),
            "focus is in the floating window; the document stays the active document");
    require(floating_window.resolve_command("file.save").outcome == CommandRouteOutcome::handled &&
                floating_window.resolve_command("file.save").stage == CommandRouteStage::active_document,
            "the floating window's chain reaches the active document");
    require(floating_window.key_bindings() == main.key_bindings(), "one application key set (DK-44)");
    KeyEvent event{KeyAction::down, PhysicalKey::s};
    event.modifiers = Modifier::control;
    require(floating_window.dispatch_key(event), "Ctrl+S is taken in the floating window");
    require((*app).factory.saves.saves == 1U, "Ctrl+S from the floating tool window saved the active document");
    shortcut.disconnect();
}

// -- restore and closing -------------------------------------------------------

void restored_window_lands_in_a_work_area() {
    std::string saved{};
    {
        PanelFactory factory{};
        DockWorkspace workspace("workspace");
        register_panels(workspace, factory);
        show_panels(workspace);
        DockTransaction transaction{};
        transaction.edits.push_back(DockEdit::float_subject(DockSubject::of_panel(DockPanelId("properties")),
                                                            Rect{5000.0, 4000.0, 300.0, 200.0}, "gone.monitor"));
        require(workspace.apply(transaction).applied, "Properties floats far away");
        const DockLayoutWriteResult written = workspace.layout().write_xml();
        require(static_cast<bool>(written), "the layout writes");
        saved = written.text;
    }
    std::unique_ptr<App> app = make_app(false, &saved);
    DockWorkspace& workspace = *(*app).workspace;
    const DockSiteId floating = floating_site_of(workspace, "properties");
    require(workspace.floating_window_count() == 1U, "the restored floating site opens its window on attach");
    const Rect frame = content_of(workspace.floating_window(floating));
    const Rect work_area{0.0, 0.0, 1920.0, 1040.0};
    require(frame.x >= work_area.x && frame.y >= work_area.y && frame.right() <= work_area.right() &&
                frame.bottom() <= work_area.bottom() && frame.width == 300.0 && frame.height == 200.0,
            "the restored window lands wholly inside the work area, its size kept");
    require((*workspace.layout().find_floating_site(floating)).frame == frame &&
                (*workspace.layout().find_floating_site(floating)).monitor_id == "headless.monitor.0",
            "the placed frame is written back to the layout");
}

void close_button_hides_panels() {
    std::unique_ptr<App> app = make_app();
    DockWorkspace& workspace = *(*app).workspace;
    const DockSiteId floating = float_properties(*app);
    (*(*app).application).simulate_user_close(workspace.floating_window(floating).id());
    require(workspace.floating_window_count() == 1U, "the close request is held");
    static_cast<void>((*(*app).main_model).drain_posted_work());
    (*(*app).application).reclaim();
    require(workspace.layout().locate(DockPanelId("properties")).placement == DockPlacementKind::hidden &&
                workspace.floating_window_count() == 0U,
            "the close button hides the panel and the window closes");
}

// The main site leaves its window while a floating window is open: the
// floating window's close button still closes it. With no main window to
// post the close to, the window's panels are hidden at once, inside the
// close request, and the host closes the window.
void close_button_without_a_main_site() {
    std::unique_ptr<App> app = make_app(true, nullptr, true);
    DockWorkspace& workspace = *(*app).workspace;
    const DockSiteId floating = float_properties(*app);
    static_cast<void>((*(*app).root).remove_child((*(*app).site).runtime_id()));
    require((*(*app).site).attached_window() == nullptr && workspace.floating_window_count() == 1U,
            "the main site leaves its window and the floating window stays");
    (*(*app).application).simulate_user_close(workspace.floating_window(floating).id());
    (*(*app).application).reclaim();
    require(workspace.layout().locate(DockPanelId("properties")).placement == DockPlacementKind::hidden &&
                workspace.layout().state().floating_sites.empty() && workspace.floating_window_count() == 0U,
            "without a main site the close button hides the panel and the window closes");
}

// A floating window holding a document asks before it closes (phase D6): a
// cancelled prompt keeps the window and its panels as they were.
struct CancelClosing final {
    std::size_t* asked{};
    bool* cancel{};
    void operator()(DockDocumentClosing& closing) const {
        *asked = *asked + 1U;
        closing.cancel = *cancel && closing.cause == DockCloseCause::floating_window;
    }
};

void close_button_asks_about_documents() {
    std::unique_ptr<App> app = make_app();
    DockWorkspace& workspace = *(*app).workspace;
    DockTransaction transaction{};
    transaction.edits.push_back(DockEdit::float_subject(DockSubject::of_panel(DockPanelId("doc.first")),
                                                        Rect{300.0, 300.0, 320.0, 220.0}, "headless.monitor.0"));
    require(workspace.apply(transaction).applied, "the document floats");
    (*(*app).application).reclaim();
    lay_out_all(*app);
    const DockSiteId floating = floating_site_of(workspace, "doc.first");
    require(!(floating == DockSiteId{}) && workspace.floating_window_count() == 1U,
            "the document stands in a floating window");
    std::size_t asked = 0U;
    bool cancel = true;
    const SubscriptionToken token = workspace.document_closing().subscribe(CancelClosing{&asked, &cancel});
    const std::uint64_t before = workspace.layout().generation();
    (*(*app).application).simulate_user_close(workspace.floating_window(floating).id());
    static_cast<void>((*(*app).main_model).drain_posted_work());
    (*(*app).application).reclaim();
    require(asked == 1U && workspace.floating_window_count() == 1U && workspace.layout().generation() == before &&
                workspace.layout().locate(DockPanelId("doc.first")).placement == DockPlacementKind::floating,
            "a cancelled prompt keeps the window and its document");
    cancel = false;
    (*(*app).application).simulate_user_close(workspace.floating_window(floating).id());
    static_cast<void>((*(*app).main_model).drain_posted_work());
    (*(*app).application).reclaim();
    require(asked == 2U && workspace.floating_window_count() == 0U &&
                workspace.layout().locate(DockPanelId("doc.first")).placement == DockPlacementKind::absent,
            "an allowed prompt closes the document and the window");
}

// A restore on live windows opens the floating windows the saved layout
// holds and destroys those it drops (phase D7).
void load_layout_on_live_windows() {
    std::unique_ptr<App> app = make_app();
    DockWorkspace& workspace = *(*app).workspace;
    const std::string docked_layout = workspace.save_layout().text;
    static_cast<void>(float_properties(*app));
    const std::string floating_layout = workspace.save_layout().text;
    require(workspace.load_layout(docked_layout).status == DockRestoreStatus::restored, "the docked layout loads");
    (*(*app).application).reclaim();
    require(workspace.floating_window_count() == 0U &&
                workspace.layout().locate(DockPanelId("properties")).placement == DockPlacementKind::docked,
            "the floating window it drops is destroyed and Properties is docked");
    require(workspace.load_layout(floating_layout).status == DockRestoreStatus::restored, "the floating layout loads");
    (*(*app).application).reclaim();
    lay_out_all(*app);
    const DockSiteId floating = floating_site_of(workspace, "properties");
    const Control::Ptr content = workspace.panel_content(DockPanelId("properties"));
    require(workspace.floating_window_count() == 1U && model_of(*app, floating) != nullptr && content &&
                (*content).attached_window() == model_of(*app, floating),
            "its floating window opens again with Properties inside");
}

// -- retitling a panel -----------------------------------------------------------

const SemanticNode* find_semantic(const std::vector<SemanticNode>& nodes, std::string_view id) {
    for (const SemanticNode& node : nodes) {
        if (node.stable_id == id) return &node;
        const SemanticNode* const found = find_semantic(node.children, id);
        if (found != nullptr) return found;
    }
    return nullptr;
}
// The result points into `nodes`: a temporary list may not be passed.
const SemanticNode* find_semantic(std::vector<SemanticNode>&& nodes, std::string_view id) = delete;

// set_panel_title: the title reads back, the docked stack's pane takes the
// new name, and the floating window showing the panel is retitled through
// the host; the same title again does nothing, and an unregistered panel
// is refused.
void set_panel_title_retitles() {
    std::unique_ptr<App> app = make_app();
    DockWorkspace& workspace = *(*app).workspace;
    host::HeadlessWindowing& windowing = (*(*app).application).windowing();
    const DockSiteId floating = float_properties(*app);
    const ApplicationWindowHandle handle = workspace.floating_window(floating);
    require(handle.active(), "Properties has its own window");

    windowing.clear_trace();
    require(workspace.set_panel_title(DockPanelId("properties"), "Inspector") &&
                workspace.panel_title(DockPanelId("properties")) == "Inspector",
            "the new title reads back");
    const std::string retitle = "title id=" + std::to_string(handle.id().value) + " text=Inspector";
    require(position(windowing.trace(), retitle) != std::string::npos,
            "the floating window showing the panel is retitled");

    windowing.clear_trace();
    require(workspace.set_panel_title(DockPanelId("properties"), "Inspector") &&
                position(windowing.trace(), "title id=") == std::string::npos,
            "the same title again is accepted and retitles nothing");

    require(workspace.set_panel_title(DockPanelId("solution"), "Explorer"), "a docked panel is retitled");
    lay_out_all(*app);
    const SemanticSnapshot snapshot = (*(*app).main_model).semantic_snapshot();
    const std::string pane = (*(*app).site).stack_identity(stack_of(workspace, "solution"));
    const SemanticNode* const node = find_semantic(snapshot.roots, pane);
    require(node != nullptr && (*node).name == "Explorer", "the docked stack's pane takes the new name");

    require(!workspace.set_panel_title(DockPanelId("nobody"), "Nobody") &&
                workspace.panel_title(DockPanelId("nobody")) == "nobody",
            "an unregistered panel is refused and keeps answering its identity");
    require(workspace.set_panel_title(DockPanelId("output"), std::string{}) &&
                workspace.panel_title(DockPanelId("output")) == "output",
            "an empty title falls back to the identity");
    (*(*app).application).reclaim();
}

} // namespace

int main() {
    float_command_trace();
    release_outside_floats();
    float_limit_snaps_back();
    drag_into_floating_window();
    move_floating_window_into_main();
    user_move_is_saved();
    ctrl_s_reaches_the_active_document();
    restored_window_lands_in_a_work_area();
    close_button_hides_panels();
    close_button_without_a_main_site();
    close_button_asks_about_documents();
    load_layout_on_live_windows();
    set_panel_title_retitles();
    std::cout << "Floating windows: the Float trace, floating on release and at the limit, docking back, drags "
                 "across windows, moving by the caption, frames saved, Ctrl+S, restore and closing passed\n";
    return EXIT_SUCCESS;
}
