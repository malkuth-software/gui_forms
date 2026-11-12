// A window's teardown and the controls that post work from what it tells
// them. Closing a window (its destructor, or the host's closed event)
// revokes pointer capture and focus, which controls hear about; a control
// may answer by posting work to the window's dispatcher (DockSite does, to
// recheck capture after a release). Teardown must tell them while the
// dispatcher still accepts work, then shut it down, which cancels whatever
// was posted: no control may meet a shut-down dispatcher from a teardown
// notification. Covered: a probe that posts on capture loss and focus loss,
// destroyed with its window and closed by the host; and a DockSite whose
// pointer drag is in progress when its window is destroyed or closed.

#include "gui_forms/binding/property_type/property_type.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/dock_site.hpp"
#include "gui_forms/controls/dock_workspace.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/dirty.hpp"
#include "gui_forms/dispatcher/operation/dispatch_operation.hpp"
#include "gui_forms/dispatcher/types/dispatcher_types.hpp"
#include "gui_forms/docking/types/dock_types.hpp"
#include "gui_forms/event/delegate/delegate.hpp"
#include "gui_forms/event/subscription_token/subscription_token.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/host/types/host_types.hpp"
#include "gui_forms/rules/rule/rule.hpp"
#include "gui_forms/surface_root/surface_root.hpp"
#include "gui_forms/text/markdown/markdown.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/window/window.hpp"
#include "headless_host.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <memory>
#include <string>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "gui_forms_window_teardown_tests: " << message << '\n';
    std::exit(1);
}

Point centre(Rect bounds) {
    const Point point{bounds.x + bounds.width * 0.5, bounds.y + bounds.height * 0.5};
    return point;
}

// What the probe posts: nothing to do but be called, which a cancelled
// operation never is.
struct Recheck final {
    int* calls{};
    void operator()() const { ++(*calls); }
};

// Captures the pointer on a press, takes focus, and posts a recheck to its
// window whenever it loses capture or focus.
class Poster final : public Control {
public:
    explicit Poster(StableId stable_id) : Control(std::move(stable_id)) { focusable = true; }

    void on_pointer(PointerEvent& event) override {
        if (event.action != PointerAction::down) return;
        Window* const host = attached_window();
        if (host == nullptr) return;
        (*host).capture_pointer(shared_from_this(), 1U);
        static_cast<void>((*host).request_focus(shared_from_this()));
        event.handled = true;
    }

    void on_capture(const PointerCaptureChange& change) {
        if (change.captured) return;
        post("capture");
    }

    void on_focus_changed(bool focused) override {
        Control::on_focus_changed(focused);
        if (!focused) post("focus");
    }

    std::vector<DispatchOperation> posted{};
    std::vector<std::string> reasons{};
    int calls{};

private:
    void post(const char* reason) {
        Window* const host = attached_window();
        if (host == nullptr) return;
        posted.push_back((*host).begin_invoke(shared_from_this(), Recheck{&calls}));
        reasons.emplace_back(reason);
    }
};

struct PosterFixture final {
    std::shared_ptr<Poster> poster;
    std::unique_ptr<Window> window;
    SubscriptionToken capture{};
};

std::unique_ptr<PosterFixture> make_poster_fixture() {
    std::unique_ptr<PosterFixture> fixture = std::make_unique<PosterFixture>();
    (*fixture).poster = make_control<Poster>(StableId("poster"));
    (*(*fixture).poster).set_requested_bounds({0.0, 0.0, 100.0, 80.0});
    (*fixture).window = std::make_unique<Window>((*fixture).poster, Size{100.0, 80.0});
    Window& window = *(*fixture).window;
    Poster& poster = *(*fixture).poster;
    (*fixture).capture = window.pointer_capture_changed().subscribe(
        poster, Delegate<const PointerCaptureChange&>::bind<Poster, &Poster::on_capture>(poster));
    window.perform_layout();
    static_cast<void>(window.dispatch_pointer({PointerAction::down, PointerButton::primary, {20.0, 20.0}}));
    require(window.captured_control() == (*fixture).poster && window.focused_control() == (*fixture).poster,
            "the probe holds capture and focus");
    return fixture;
}

void poster_outlives_its_destroyed_window() {
    std::unique_ptr<PosterFixture> fixture = make_poster_fixture();
    std::shared_ptr<Poster> poster = (*fixture).poster;
    (*fixture).window.reset();
    require(!(*poster).posted.empty(), "the window's teardown told the probe it lost capture or focus");
    for (const DispatchOperation& operation : (*poster).posted) {
        require(operation.state() == DispatchOperationState::cancelled,
                "work posted during teardown is cancelled when the dispatcher shuts down");
    }
    require((*poster).calls == 0, "and never runs");
}

void poster_survives_the_host_closing() {
    std::unique_ptr<PosterFixture> fixture = make_poster_fixture();
    Window& window = *(*fixture).window;
    // The host borrows the window, so it goes first.
    std::unique_ptr<host::HeadlessHost> host = std::make_unique<host::HeadlessHost>(window);
    static_cast<void>((*host).dispatch(HostAttachEvent{{100.0, 80.0}, 1.0}, 1));
    const HostDispatchResult closed = (*host).dispatch(HostClosedEvent{HostCloseReason::user}, 2);
    require(closed.accepted(), "the closed event is accepted");
    require(!window.captured_control() && !window.dispatcher_snapshot().accepting,
            "closing released capture and shut the dispatcher down");
    const Poster& poster = *(*fixture).poster;
    require(!poster.posted.empty() && poster.reasons.front() == "capture",
            "the probe heard of the capture release");
    for (const DispatchOperation& operation : poster.posted) {
        require(operation.state() == DispatchOperationState::cancelled, "its recheck was cancelled by the shutdown");
    }
    // The window is destroyed after its host closed it: the dispatcher is
    // already shut down when its teardown revokes the probe's focus, and the
    // probe's post is refused by name instead of throwing.
    const std::size_t before = poster.posted.size();
    std::shared_ptr<Poster> kept = (*fixture).poster;
    host.reset();
    (*fixture).window.reset();
    require((*kept).posted.size() == before + 1U && (*kept).reasons.back() == "focus" &&
                (*kept).posted.back().state() == DispatchOperationState::cancelled && (*kept).calls == 0,
            "a post from the teardown of a closed window comes back cancelled");
}

void refusal_is_only_for_teardown() {
    std::unique_ptr<PosterFixture> fixture = make_poster_fixture();
    Window& window = *(*fixture).window;
    window.shutdown_dispatcher();
    bool rejected = false;
    try {
        static_cast<void>(window.begin_invoke((*fixture).poster, Recheck{&(*(*fixture).poster).calls}));
    } catch (const std::logic_error&) {
        rejected = true;
    }
    require(rejected && window.dispatcher_snapshot().teardown_refusals == 0U,
            "outside teardown, posting to a shut-down dispatcher still throws");
    // The probe still holds capture and focus: destroying the window now
    // revokes both after the shutdown, and both posts are refused by name.
    std::shared_ptr<Poster> kept = (*fixture).poster;
    const std::size_t before = (*kept).posted.size();
    (*fixture).window.reset();
    require((*kept).posted.size() == before + 2U && (*kept).posted.back().state() == DispatchOperationState::cancelled,
            "and the destructor's own notifications are refused, not thrown");
}

// -- a DockSite mid-drag -------------------------------------------------------

class PanelFactory final {
public:
    void create(DockPanelCreation& creation) {
        const std::string id = "content." + creation.panel.value;
        std::shared_ptr<Panel> panel = make_control<Panel>(StableId(id));
        creation.content = panel;
    }
};

DockPanelRegistration registration(PanelFactory& factory, std::string id, std::string title, DockPanelKind kind,
                                   DockSide side) {
    DockPanelRegistration entry{};
    entry.descriptor.id = DockPanelId(std::move(id));
    entry.descriptor.kind = kind;
    entry.descriptor.minimum_size = {100.0, 60.0};
    entry.descriptor.default_placement.side = side;
    entry.descriptor.default_placement.ratio = 0.25;
    entry.title = std::move(title);
    entry.factory = Delegate<DockPanelCreation&>::bind<PanelFactory, &PanelFactory::create>(factory);
    return entry;
}

void apply(DockWorkspace& workspace, DockEdit edit) {
    DockTransaction transaction{};
    transaction.edits.push_back(std::move(edit));
    require(workspace.apply(transaction).applied, "a fixture edit applies");
}

struct RecordOutcomes final {
    std::vector<DockDragOutcome>* outcomes{};
    void operator()(const DockDragOutcome& outcome) const { (*outcomes).push_back(outcome); }
};

// Output at the bottom and one document; the site fills an 800 by 500
// window, and a caption drag of Output is under way.
struct DockFixture final {
    PanelFactory factory{};
    std::unique_ptr<DockWorkspace> workspace;
    std::shared_ptr<DockSite> site;
    std::unique_ptr<Window> window;
    std::vector<DockDragOutcome> outcomes{};
    SubscriptionToken ended{};
};

std::unique_ptr<DockFixture> make_dragging_fixture() {
    std::unique_ptr<DockFixture> fixture = std::make_unique<DockFixture>();
    (*fixture).workspace = std::make_unique<DockWorkspace>("workspace");
    DockWorkspace& workspace = *(*fixture).workspace;
    PanelFactory& factory = (*fixture).factory;
    require(workspace.register_panel(registration(factory, "output", "Output", DockPanelKind::tool, DockSide::bottom)) &&
                workspace.register_panel(
                    registration(factory, "doc", "Doc.txt", DockPanelKind::document, DockSide::left)),
            "the panels register");
    apply(workspace, DockEdit::show(DockPanelId("doc")));
    apply(workspace, DockEdit::show(DockPanelId("output")));
    (*fixture).site = make_control<DockSite>(StableId("site"), workspace);
    (*fixture).window = std::make_unique<Window>((*fixture).site, Size{800.0, 500.0});
    Window& window = *(*fixture).window;
    window.perform_layout();
    (*fixture).ended = (*(*fixture).site).drag_ended().subscribe(RecordOutcomes{&(*fixture).outcomes});
    const DockNodeId stack = workspace.layout().locate(DockPanelId("output")).stack;
    const std::vector<DockSite::Stack> stacks = (*(*fixture).site).realized_stack_items();
    Rect caption{};
    for (const DockSite::Stack& record : stacks) {
        if (record.stack == stack) caption = record.parts.caption_label.bounds;
    }
    require(!caption.empty(), "Output's caption is realized");
    const Point from = centre(caption);
    static_cast<void>(window.dispatch_pointer({PointerAction::down, PointerButton::primary, from}));
    static_cast<void>(window.dispatch_pointer({PointerAction::move, PointerButton::none, {from.x + 10.0, from.y + 10.0}}));
    window.perform_layout();
    static_cast<void>(window.dispatch_pointer({PointerAction::move, PointerButton::none, {400.0, 200.0}}));
    window.perform_layout();
    require((*(*fixture).site).drag_phase() == DockDragPhase::dragging && window.captured_control() == (*fixture).site,
            "the drag is under way and holds the pointer");
    return fixture;
}

void window_destroyed_mid_drag() {
    std::unique_ptr<DockFixture> fixture = make_dragging_fixture();
    const std::string before = (*(*fixture).workspace).layout().write_xml().text;
    (*fixture).window.reset();
    require((*(*fixture).site).attached_window() == nullptr && (*(*fixture).site).drag_phase() == DockDragPhase::idle,
            "destroying the window mid-drag ends the drag without terminating");
    require((*(*fixture).workspace).layout().write_xml().text == before, "and leaves the layout as it was");
}

// An application that shuts the dispatcher down itself and then releases
// the pointer: the site cannot wait a turn to recheck the capture, so the
// drag ends as capture lost at once.
void released_after_shutdown_mid_drag() {
    std::unique_ptr<DockFixture> fixture = make_dragging_fixture();
    Window& window = *(*fixture).window;
    window.shutdown_dispatcher();
    window.release_pointer();
    require((*(*fixture).site).drag_phase() != DockDragPhase::dragging && (*fixture).outcomes.size() == 1U &&
                (*fixture).outcomes[0].end() == DockDragEnd::capture_lost,
            "a release after shutdown ends the drag at once as capture lost");
}

void host_closes_mid_drag() {
    std::unique_ptr<DockFixture> fixture = make_dragging_fixture();
    Window& window = *(*fixture).window;
    std::unique_ptr<host::HeadlessHost> host = std::make_unique<host::HeadlessHost>(window);
    static_cast<void>((*host).dispatch(HostAttachEvent{{800.0, 500.0}, 1.0}, 1));
    const HostDispatchResult closed = (*host).dispatch(HostClosedEvent{HostCloseReason::user}, 2);
    require(closed.accepted() && !window.captured_control(), "the host's close releases the drag's capture");
    host.reset();
    (*fixture).window.reset();
    require((*(*fixture).site).drag_phase() == DockDragPhase::idle, "and the window then goes without terminating");
}

} // namespace

int main() {
    poster_outlives_its_destroyed_window();
    poster_survives_the_host_closing();
    refusal_is_only_for_teardown();
    window_destroyed_mid_drag();
    released_after_shutdown_mid_drag();
    host_closes_mid_drag();
    std::cout << "window teardown: posting on capture and focus loss, destroyed and host-closed windows, a DockSite "
                 "drag in progress passed\n";
    return EXIT_SUCCESS;
}
