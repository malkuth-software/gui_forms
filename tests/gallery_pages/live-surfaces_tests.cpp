#include "support.hpp"

#include "gui_forms/surface_root/sub_surface/sub_surface_host.hpp"
#include "gui_forms/controls/button.hpp"
#include "gui_forms/controls/drawing_surface.hpp"
#include "gui_forms/controls/label.hpp"
#include "gui_forms/controls/track_bar.hpp"

#include <string_view>

namespace gallery_pages_tests {

void test_live_surfaces(gui_forms::Window& window) {
    select_page(window, "live-surfaces");
    const std::shared_ptr<gui_forms::DrawingSurface> view =
        std::dynamic_pointer_cast<DrawingSurface>(
            window.find("gallery.live-surfaces.view"));
    const std::shared_ptr<gui_forms::Label> status =
        std::dynamic_pointer_cast<Label>(window.find("gallery.live-surfaces.status"));
    const std::shared_ptr<gui_forms::Label> telemetry =
        std::dynamic_pointer_cast<Label>(window.find("gallery.live-surfaces.telemetry"));
    require(view && status && telemetry && (*view).has_paint_callback(),
            "live-surfaces page must present through a public DrawingSurface");
    for (const char* id : {"gallery.live-surfaces.start", "gallery.live-surfaces.stop",
                           "gallery.live-surfaces.publish",
                           "gallery.live-surfaces.reconfigure"}) {
        const std::shared_ptr<gui_forms::Button> button =
            std::dynamic_pointer_cast<Button>(window.find(id));
        require(button != nullptr && (*button).command_connected(),
                "live-surfaces producer buttons must bind page-owned Commands");
    }
    // The document binds the interval Value (FD-6): the track bar starts at
    // the Value's 120 ms.
    const std::shared_ptr<gui_forms::TrackBar> interval =
        std::dynamic_pointer_cast<TrackBar>(window.find("gallery.live-surfaces.interval"));
    require(interval != nullptr && (*interval).value.read() == 120.0,
            "the interval track bar must start at its bound Value");
    // The threaded sub-surface: a public control with its own anatomy.
    const std::shared_ptr<gui_forms::SubSurfaceHost> host = std::dynamic_pointer_cast<SubSurfaceHost>(
        window.find("gallery.live-surfaces.sub-surface.host"));
    require(host != nullptr && (*host).anatomy().control_id == std::string_view("sub_surface_host"),
            "live-surfaces page must show a SubSurfaceHost with its own anatomy");
    require((*host).parts().region.present && (*host).parts().focus.present,
            "the sub-surface host publishes its region and focus parts");
    const std::string semantics = window.semantic_snapshot().to_json();
    require(semantics.find("\"role\":\"image\"") != std::string::npos &&
                semantics.find("Live surface presentation") != std::string::npos,
            "the live-surface presenter must publish image semantics");

    // One explicit publish registers the surface with the window presenter.
    click(window, window.find("gallery.live-surfaces.publish"));
    require((*status).text.read().find("Published generation 1") != std::string::npos &&
                (*status).text.read().find("whole surface") != std::string::npos,
            "the first publication must cover the whole surface");
    const std::vector<LiveSurfacePresentation> presentations =
        window.take_live_surface_presentations();
    require(!presentations.empty() &&
                presentations.front().control == (*view).runtime_id() &&
                presentations.front().surface != nullptr,
            "publishing must queue a presentation for the page's presenter control");
    const std::shared_ptr<LiveSurface> surface = presentations.front().surface;
    LiveSurfaceSnapshot snapshot = (*surface).snapshot();
    require(snapshot.publishes == 1U && snapshot.description.width == 160U &&
                snapshot.description.height == 100U && snapshot.description.opaque,
            "the page surface must start at 160x100 with one opaque publication");

    // A presenter that already holds the previous frame receives only the
    // damage the producer promised: one 20-pixel band of the 160-pixel width.
    int presenter{};
    const LiveSurfaceFrame first = (*surface).acquire_for_presentation(&presenter);
    require(first && (first.damage() == Rect{0.0, 0.0, 160.0, 100.0}),
            "a presenter's first acquisition must receive the whole surface");
    click(window, window.find("gallery.live-surfaces.publish"));
    const LiveSurfaceFrame second = (*surface).acquire_for_presentation(&presenter);
    require(second && second.generation() == 2U &&
                (second.damage() == Rect{20.0, 0.0, 20.0, 100.0}) &&
                (*status).text.read().find("damage 20,0 20×100") != std::string::npos,
            "a band publication must present damage-limited to that band");

    // The producer Timer ticks on the UI thread and publishes one band per tick.
    click(window, window.find("gallery.live-surfaces.start"));
    require(window.next_wake().has_value() &&
                (*status).text.read().find("Producer running") != std::string::npos,
            "the start Command must arm the page Timer on the window queue");
    const FramePollResult tick = window.poll_frame_schedule(
        FrameClock::now() + std::chrono::seconds(1));
    snapshot = (*surface).snapshot();
    require(tick.ui_timer_ticks >= 1U && snapshot.publishes >= 3U &&
                snapshot.published_generation >= 3U,
            "a Timer tick must write and publish one frame through a write lease");
    click(window, window.find("gallery.live-surfaces.stop"));
    require(!window.next_wake().has_value() &&
                (*status).text.read().find("stopped") != std::string::npos,
            "the stop Command must leave the scheduler quiescent");

    // Reconfigure starts a new epoch and carries the previous frame until the
    // first publication at the new size.
    click(window, window.find("gallery.live-surfaces.reconfigure"));
    snapshot = (*surface).snapshot();
    require(snapshot.description.width == 240U && snapshot.description.height == 150U &&
                snapshot.carries_previous_frame &&
                (*telemetry).text.read().find("240×150") != std::string::npos &&
                (*telemetry).text.read().find("previous epoch") != std::string::npos,
            "reconfigure must replace the pool and carry the previous frame");
    click(window, window.find("gallery.live-surfaces.publish"));
    snapshot = (*surface).snapshot();
    require(!snapshot.carries_previous_frame && snapshot.published_generation == 1U &&
                (*status).text.read().find("Published generation 1") != std::string::npos,
            "the first publication of a new epoch must replace the carried frame");

    // Hidden-page suspension: a tick on an invisible presenter stops the Timer.
    click(window, window.find("gallery.live-surfaces.start"));
    require(window.next_wake().has_value(), "restart must arm the producer again");
    // Hide behind a static page: the inspection page schedules its own
    // overlay work and would keep a wake armed.
    select_page(window, "dock-and-anchor");
    // select_page leaves the pointer over a navigation button, whose shell
    // tooltip would otherwise arm its own show/auto-pop deadlines; move off it
    // so the only scheduled wake is the producer Timer.
    static_cast<void>(window.dispatch_pointer(
        {PointerAction::move, PointerButton::none, {1000.0, 700.0}}));
    require(!(*view).effectively_visible() && window.next_wake().has_value(),
            "the hidden presenter must still have one armed producer deadline");
    const FramePollResult hidden_tick = window.poll_frame_schedule(
        FrameClock::now() + std::chrono::seconds(1));
    require(hidden_tick.ui_timer_ticks >= 1U &&
                (*status).text.read().find("suspended") != std::string::npos,
            "a tick on a hidden presenter must stop the producer Timer");
    // A queued presentation keeps a window wake armed until a host takes it,
    // so quiescence is proven by the Timer: a later poll ticks it no more.
    const FramePollResult after_suspension = window.poll_frame_schedule(
        FrameClock::now() + std::chrono::seconds(3));
    require(after_suspension.ui_timer_ticks == 0U,
            "a hidden live-surface page must suspend its producer Timer");
    static_cast<void>(window.poll_frame_schedule(
        FrameClock::now() + std::chrono::seconds(2)));
    require((*surface).snapshot().published_generation == 1U,
            "a suspended producer must not publish while its page is hidden");
    select_page(window, "live-surfaces");
}

}  // namespace gallery_pages_tests
