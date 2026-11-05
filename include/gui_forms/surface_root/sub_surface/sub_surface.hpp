#pragma once

#include "gui_forms/surface_root/surface_root.hpp"

#include "gui_forms/live_surface.hpp"
#include "gui_forms/live_surface/pacer/live_surface_pacer.hpp"
#include "gui_forms/paint_framebuffer.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace gui_forms {

class Theme;
namespace detail {
struct SubSurfaceChannel;
}

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
    // do); without one, text is measured by the estimate. Null content,
    // surface or theme is std::invalid_argument; a thread the system
    // refuses is std::system_error.
    SubSurfaceThread(std::unique_ptr<SubSurfaceContent> content,
                     std::shared_ptr<LiveSurface> surface,
                     std::shared_ptr<const Theme> theme,
                     std::unique_ptr<PaintFramebuffer> text_measure);
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

private:
    std::shared_ptr<detail::SubSurfaceChannel> channel_;
    std::thread thread_;
    std::uint64_t last_content_epoch_{};
};

} // namespace gui_forms
