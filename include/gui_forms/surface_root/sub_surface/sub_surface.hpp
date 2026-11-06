#pragma once

#include "gui_forms/surface_root/surface_root.hpp"

#include "gui_forms/live_surface.hpp"
#include "gui_forms/live_surface/pacer/live_surface_pacer.hpp"
#include "gui_forms/paint_framebuffer.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace gui_forms {

class Theme;
namespace detail {
struct SubSurfaceChannel;
class SubSurfaceLoop;
class SubSurfaceToolTipRequester;
class SubSurfaceAlertRequester;
}

// One change of a sub-surface's content that must agree with its parent's
// chrome in the same visible frame (design note section 2, redraw-all): made
// on the parent's thread, carried with a configuration
// (SubSurfaceHost::redraw_all), handed to SubSurfaceContent::apply_update on
// the sub-surface's thread after that configuration is applied and before
// its first frame, and destroyed there. Each update is complete, so of
// updates sent before the sub-surface applied any only the newest is
// applied (statistics: updates_coalesced). A content downcasts it to the
// type it made.
class SubSurfaceContentUpdate {
public:
    virtual ~SubSurfaceContentUpdate() = default;
};

// What a sub-surface shows (stage S2, threaded sub-surfaces design note
// section 2). The parent hands one to a SubSurfaceThread, which moves it to
// the sub-surface's thread; it is called and destroyed there, so the
// controls it makes are created on, owned by and released on that thread.
class SubSurfaceContent {
public:
    virtual ~SubSurfaceContent() = default;
    // Called once, on the sub-surface's thread, before the first
    // configuration is applied: the root control of the sub-surface's tree,
    // made on that thread. A null root or an exception stops the
    // sub-surface as failed (SubSurfaceState::failed).
    [[nodiscard]] virtual Control::Ptr build_root() = 0;
    // Called once, on the sub-surface's thread, after the sub-surface made
    // its root of the built tree and before its loop starts: `dispatcher`
    // posts to that thread (the C ABI runs its posted callbacks through it).
    // The base does nothing.
    virtual void root_started(const WindowDispatcher& dispatcher);
    // Called on the sub-surface's thread with the update a configuration
    // carried, after the configuration is applied and before its input,
    // posted work and flush: the frame published at that content epoch
    // shows the update. An exception stops the sub-surface as failed. The
    // base ignores updates.
    virtual void apply_update(SubSurfaceContentUpdate& update);
};

// The state both sides agree on in one visible frame (design note section
// 6), sent by the parent with each content epoch. Move-only: the
// framebuffer goes with it.
struct SubSurfaceConfiguration final {
    // Grows with every configuration the parent sends; each frame the
    // sub-surface publishes at this configuration carries it as its
    // LiveSurface content tag.
    std::uint64_t content_epoch{};
    // The region's logical size and the window scale.
    Size logical_size{};
    double scale{1.0};
    // The device size: ceil(logical_size * scale) on each axis, the size of
    // the framebuffer and of the LiveSurface (resize rule 2: composition is
    // a copy, never a scale).
    std::uint32_t pixel_width{};
    std::uint32_t pixel_height{};
    // Every pixel the sub-surface publishes has alpha 255.
    bool opaque{};
    // The environment (design note sections 3 and 6): the parent's
    // application theme, an immutable snapshot shared, never mutated, across
    // the threads (null keeps the one in force), and the parent's
    // presentation settings (text scale, colour scheme, reduced motion and
    // the rest), applied before the theme so the theme is shown in the new
    // scheme.
    std::shared_ptr<const Theme> theme{};
    PresentationSettings presentation{};
    // Made by the parent on its own thread (SurfaceRoot::create_framebuffer)
    // at the device size, and owned from then on by the sub-surface's
    // thread, which alone uses and destroys it. Null keeps the previous
    // framebuffer; a sub-surface without one lays out but publishes nothing.
    std::unique_ptr<PaintFramebuffer> framebuffer{};
    // The content's update for this epoch (SubSurfaceContentUpdate), or
    // null. A configuration that replaces an unapplied one without an
    // update of its own takes over the replaced one's.
    std::unique_ptr<SubSurfaceContentUpdate> update{};
};

// Where keyboard focus goes when it enters a sub-surface (design note
// section 4).
enum class SubSurfaceFocusEntry : std::uint8_t {
    // Back to the control focused when it left, or the first tab stop when
    // there was none (a click on the host, or programmatic focus).
    restore,
    // The first tab stop: Tab moved focus onto the host.
    first,
    // The last tab stop: Shift+Tab moved focus onto the host.
    last,
};

enum class SubSurfaceInputKind : std::uint8_t {
    pointer,
    key,
    text,
    // The host gained keyboard focus in an active window: the sub-surface
    // becomes active and focus enters it (`focus_entry`).
    focus_enter,
    // The host lost keyboard focus or its window was deactivated: the
    // sub-surface becomes inactive and keeps its focused control.
    focus_leave,
};

// One input event forwarded from the host to the sub-surface's thread
// (design note section 4). Only the member `kind` names is meaningful.
// Pointer positions are in the sub-surface's logical coordinates.
struct SubSurfaceInput final {
    SubSurfaceInputKind kind{SubSurfaceInputKind::pointer};
    PointerEvent pointer{};
    KeyEvent key{};
    TextInputEvent text{};
    SubSurfaceFocusEntry focus_entry{SubSurfaceFocusEntry::restore};
};

// Where a sub-surface returns the key events its tree left unhandled
// (design note section 4: the parent then runs its own bubble, accelerators,
// dialog keys and focus traversal, which is how Tab leaves the
// sub-surface). Called on the parent's thread only, through the parent's
// dispatcher, in the order the keys were forwarded; `sequence` is the one
// SubSurfaceThread::post_input returned for the key. A target the parent
// has released receives nothing.
class SubSurfaceKeyReturn {
public:
    virtual ~SubSurfaceKeyReturn() = default;
    virtual void return_unhandled_key(const KeyEvent& event, std::uint64_t sequence) = 0;
};

// What a sub-surface asks its parent to show (design note section 15): a
// sub-surface borrows its region from its parent and never shows anything
// beyond it itself, so a tooltip is asked for, and the parent root decides
// whether and where it shows.
enum class SubSurfaceRequestKind : std::uint8_t {
    tool_tip,
    // SurfaceRoot::raise_alert on the sub-surface: the parent raises it on
    // the host and relays its one answer.
    alert,
};

// What made the sub-surface ask for a tooltip.
enum class SubSurfaceToolTipOrigin : std::uint8_t {
    // The pointer is over the thing.
    pointer,
    // Keyboard focus is on the thing.
    keyboard,
    // SurfaceRoot::show_tool_tip or request_tool_tip on the sub-surface.
    program,
};

// One request, posted on the sub-surface's thread and received on the
// parent's. Content travels as data. The parent answers with a
// SurfaceRequestOutcome carrying the request's `id` (declined: its policy
// refused it, or it has no window; closed: shown, then closed by the parent,
// which keeps it until revoked; revoked: the pointer or keyboard focus left
// the host; stale: older than the newest configuration, or than the pointer
// or focus leaving).
struct SubSurfaceRequest final {
    // From the sub-surface's channel: 1 for the first, growing by one.
    std::uint64_t id{};
    SubSurfaceRequestKind kind{SubSurfaceRequestKind::tool_tip};
    // The configuration the anchor was measured in; the parent drops a
    // request measured in an older one (the sub-surface asks again).
    std::uint64_t content_epoch{};
    // The sequence of the newest input the sub-surface had dispatched when
    // it asked (SubSurfaceThread::post_input); 0 for none. The parent drops
    // a pointer request older than the pointer's leaving its host, and a
    // keyboard request older than focus leaving it.
    std::uint64_t input_sequence{};
    // The thing's bounds, in the sub-surface's logical coordinates.
    Rect anchor{};
    std::string text{};
    // The thing's semantic description, when it has one.
    std::string description{};
    // An alert's caption, buttons, icon and default choice (AlertRequest;
    // `text` is its message). Alerts are never stale: they carry no anchor
    // and no input.
    std::string caption{};
    HostMessageButtons buttons{HostMessageButtons::ok};
    HostMessageIcon icon{HostMessageIcon::none};
    HostDialogChoice default_choice{HostDialogChoice::none};
    SubSurfaceToolTipOrigin origin{SubSurfaceToolTipOrigin::pointer};
    // For a program request (SurfaceRoot::show_tool_tip or
    // request_tool_tip on the sub-surface): how long it shows (none: the
    // parent's auto-pop delay; zero: until revoked).
    std::optional<std::chrono::milliseconds> duration{};
};

// Where a sub-surface's requests go: called on the parent's thread only,
// through the parent's dispatcher, in the order the sub-surface posted. A
// revoke names a request posted earlier. A target the parent has released
// receives nothing.
class SubSurfaceRequestTarget {
public:
    virtual ~SubSurfaceRequestTarget() = default;
    virtual void receive_request(const SubSurfaceRequest& request) = 0;
    virtual void receive_revoke(std::uint64_t id) = 0;
};

// Where a sub-surface is in its life, as its parent last saw it.
enum class SubSurfaceState : std::uint8_t {
    // The thread is building the content's tree.
    starting,
    // The tree is built and the loop runs.
    running,
    // The loop ended after stop(); the tree is released.
    stopped,
    // The content refused (a null root or an exception); the thread ended.
    failed,
};

// Counters of one sub-surface (design note section 8, rules), written by its
// thread and read by its parent under the channel's lock.
struct SubSurfaceStatistics final {
    std::uint64_t configurations_received{};
    // Configurations replaced by a newer one before the thread applied them
    // (newest wins: resize rule 3).
    std::uint64_t configurations_coalesced{};
    std::uint64_t configurations_applied{};
    // Content updates handed to the content (apply_update), and updates
    // replaced by a newer one before the thread applied them.
    std::uint64_t updates_applied{};
    std::uint64_t updates_coalesced{};
    // LiveSurface reconfigures: device-size changes (resize cost A).
    std::uint64_t surface_reconfigures{};
    std::uint64_t frames_published{};
    // Paints whose frame waited because every LiveSurface buffer was held;
    // the damage is published with the next frame.
    std::uint64_t publish_retries{};
    // The content epoch of the newest published frame.
    std::uint64_t published_content_epoch{};
    // Turns of the loop: each wake by a configuration, a post, a paint wake
    // or a due frame request. An idle sub-surface does not turn (no
    // perpetual redraw loop).
    std::uint64_t loop_turns{};
    // Pacing (design note section 8): frames rendered because the window
    // did not show the previous one within one and a half refreshes, and
    // the refreshes those waits missed. Frames shown are in the
    // LiveSurface's presentation record.
    std::uint64_t deadline_wakes{};
    std::uint64_t missed_frames{};
    // Input (design note section 4): events posted, events the tree
    // dispatched (in posting order, never dropped while running), the
    // sequence of the newest dispatched, and the unhandled keys sent back
    // to the parent.
    std::uint64_t inputs_received{};
    std::uint64_t inputs_dispatched{};
    std::uint64_t last_input_sequence{};
    std::uint64_t keys_returned{};
    // Requests (design note section 15): posted to the parent, revoked by
    // the sub-surface, outcomes received, and outcomes for a request the
    // sub-surface had already revoked (dropped).
    std::uint64_t requests_posted{};
    std::uint64_t requests_revoked{};
    std::uint64_t outcomes_received{};
    std::uint64_t outcomes_stale{};
};

// A SurfaceRoot whose output is a LiveSurface and whose host is a control of
// its parent window (design note section 2). Made, run and destroyed by a
// SubSurfaceThread on the thread it owns; every member is that thread's
// (SurfaceRoot::check_access). Controls inside see window() null.
class SubSurface final : public SurfaceRoot {
public:
    // Binds the root to the calling thread and attaches `root`, which must
    // be alive and without a parent (std::invalid_argument or
    // std::logic_error otherwise). `resources` must be made on this thread.
    SubSurface(Control::Ptr root, std::shared_ptr<ApplicationResources> resources);
    ~SubSurface();
    SubSurface(const SubSurface&) = delete;
    SubSurface& operator=(const SubSurface&) = delete;

    // The agreed size and scale of a configuration: the surface side of
    // Window::resize and Window::set_scale. Root thread only.
    void apply_size(Size logical_size);
    void apply_scale(double scale);
    // Focus enters from the host (SubSurfaceInputKind::focus_enter): the
    // sub-surface becomes active and focus goes where `entry` says; a
    // sub-surface without tab stops focuses nothing. Root thread only.
    void enter_focus(SubSurfaceFocusEntry entry);
    // Focus leaves through the host (SubSurfaceInputKind::focus_leave): the
    // sub-surface becomes inactive, keeps its focused control and revokes
    // its keyboard tooltip request. Root thread only.
    void leave_focus();

private:
    friend class detail::SubSurfaceLoop;
    friend class detail::SubSurfaceToolTipRequester;
    friend class detail::SubSurfaceAlertRequester;

    // Set by the loop that runs this root (null for a SubSurface made
    // without a SubSurfaceThread: its requests go nowhere).
    std::shared_ptr<detail::SubSurfaceChannel> channel_{};
    // The content epoch of the configuration applied last.
    std::uint64_t content_epoch_{};
    // The sequence of the input being dispatched, or of the last one.
    std::uint64_t input_sequence_{};
    // Observes the route SurfaceRoot::tool_tips_ owns.
    detail::SubSurfaceToolTipRequester* requester_{};
    // Observes the route SurfaceRoot::alerts_ owns.
    detail::SubSurfaceAlertRequester* alert_requester_{};
};

// The parent's side of one sub-surface: owns its thread (owner decision 9,
// the library's thread) and the channel the two sides share. Made, used and
// destroyed on the parent's thread. Nothing crosses but the configurations,
// the stop request and the published frames (house style 8).
class SubSurfaceThread final {
public:
    // Starts the thread. It makes ApplicationResources of its own showing
    // `theme`, calls content.build_root(), makes a SubSurface of the root,
    // and waits for the first configuration: it produces nothing before the
    // first agreed size (resize rule 5). It publishes into `surface`,
    // reconfiguring it whenever the device size changes. `text_measure`, if
    // not null, is a framebuffer made by the parent whose painter measures
    // the sub-surface's text for its whole life (any size; one pixel will
    // do); without one, text is measured by the estimate. `layered_user_theme`,
    // if not null, is the parent's user theme under the layered policy
    // (ApplicationResources::user_theme_ptr), selected in the sub-surface's
    // resources before its root exists, so its rules reach every part there
    // as they do in the parent (design note section 14); a user theme
    // replacing the program's arrives as `theme` itself. Null content,
    // surface or theme, or a `layered_user_theme` not compiled as a user
    // theme, is std::invalid_argument; a thread the system refuses is
    // std::system_error.
    SubSurfaceThread(std::unique_ptr<SubSurfaceContent> content,
                     std::shared_ptr<LiveSurface> surface,
                     std::shared_ptr<const Theme> theme,
                     std::unique_ptr<PaintFramebuffer> text_measure,
                     std::shared_ptr<const Theme> layered_user_theme = {});
    // Stops and joins (stop()).
    ~SubSurfaceThread();
    SubSurfaceThread(const SubSurfaceThread&) = delete;
    SubSurfaceThread& operator=(const SubSurfaceThread&) = delete;

    // Hands the sub-surface a new configuration and wakes it. Only the
    // newest unapplied one is kept (configurations_coalesced counts the
    // others). An epoch not above the last one sent is std::invalid_argument;
    // after stop() the configuration is released and nothing changes.
    void configure(SubSurfaceConfiguration configuration);
    // Queues one input event for the sub-surface's tree and wakes it; the
    // thread dispatches queued events in this order, after applying the
    // newest configuration and before its next flush. Returns the event's
    // sequence: 1 for the first, growing by one. After stop() or a failure
    // the event is discarded and 0 is returned.
    std::uint64_t post_input(const SubSurfaceInput& input);
    // Where unhandled keys go: posted through `parent` (the dispatcher of
    // the window that owns this object) to `target`, held weakly. Replaces
    // any earlier target; an empty `target` returns keys to no one.
    void set_key_return(WindowDispatcher parent, std::weak_ptr<SubSurfaceKeyReturn> target);
    // Where requests go (design note section 15): posted through `parent`
    // to `target`, held weakly. Replaces any earlier target; an empty one
    // receives nothing and the sub-surface posts nothing.
    void set_request_target(WindowDispatcher parent, std::weak_ptr<SubSurfaceRequestTarget> target);
    // Queues the parent's answer to a request and wakes the sub-surface,
    // which hands it to its tooltip route after its input, before its
    // flush (an answer to request_tool_tip goes on to that request's
    // answer target). Discarded after stop() or a failure.
    void post_request_outcome(const SurfaceRequestOutcome& outcome);
    // Revokes the sub-surface (it publishes no frame from now on), asks its
    // loop to stop, and joins the thread: the loop finishes its current
    // flush, releases its tree on its own thread and exits (design note
    // section 9). Idempotent.
    void stop() noexcept;
    // Posts to the sub-surface's thread. Empty (posting throws, as an empty
    // WindowDispatcher does) until the state is running; any thread may keep
    // the handle after the sub-surface stopped, and posts then fail as
    // posts to a closed window do.
    [[nodiscard]] WindowDispatcher dispatcher() const;
    [[nodiscard]] SubSurfaceState state() const;
    // What the content threw, for state failed; empty otherwise.
    [[nodiscard]] std::string failure() const;
    [[nodiscard]] SubSurfaceStatistics statistics() const;
    // The sub-surface's text input caret (SurfaceRoot::text_input_caret) in
    // its own coordinates, as its thread last published it at the end of a
    // turn; none while nothing there has focus (design note section 4: the
    // input method follows it without a synchronous call).
    [[nodiscard]] std::optional<Rect> text_input_caret() const;

private:
    std::shared_ptr<detail::SubSurfaceChannel> channel_;
    std::thread thread_;
    std::uint64_t last_content_epoch_{};
};

} // namespace gui_forms
