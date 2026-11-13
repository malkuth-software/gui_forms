#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/content/frame_sink/content_frame_sink.hpp"
#include "gui_forms/content/hosted/hosted_content.hpp"
#include "gui_forms/content/operation/content_operation.hpp"
#include "gui_forms/content/types/content_types.hpp"
#include "gui_forms/content/watchdog/content_watchdog.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/live_surface/surface/live_surface.hpp"
#include "gui_forms/scheduler/frame_request_token/frame_request_token.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace gui_forms {

namespace detail {
struct ContentChannel;
}

// A region whose pixels a consumer renders itself, on its own execution
// context, with its own engine and fonts (owner decisions 2026-11-13: the
// interface/content split and the integrity boundary). The consumer
// publishes finished frames through the ContentFrameSink open_content hands
// it; the surface composes the newest finished frame into the window and
// never waits for the next one. UI thread, like every control of its window.
//
// Integrity, not security: content that stalls, fails or publishes nonsense
// cannot stop the interface or corrupt it. Every frame and size is validated
// before a byte is read (validate_content_frame); a refusal leaves the last
// good frame on screen. A watchdog notices missed frames: content that
// publishes no frame and reports no heartbeat for longer than the stale
// interval is marked stale, keeps its last good frame under a veil with the
// stale caption, and publishes the busy state; the next frame or heartbeat
// makes it live again. The surface does not govern what the consumer's code
// does with direct access to the process.
//
// Execution: the consumer's threads touch only the sink; the surface reads
// only atomics, the LiveSurface's newest frame and, under a lock held for a
// few moves, the semantic text. Status changes are seen by the watchdog's
// check, a UI timer that rests while the window is occluded (a check after a
// gap longer than two periods restarts the interval rather than calling the
// content stale), and by a notice the sink posts through the window's
// dispatcher for the first frame and for new semantic text.
//
// Termination (content/operation/content_operation.hpp): terminate asks the
// content to stop through its sink's stop token and never waits. A program
// the surface runs (start_program) that has not finished when the
// termination's grace passes is abandoned at the next check: its channel is
// revoked and its thread detached, and the last good frame stays, veiled.
// Content on the consumer's own threads is revoked at once (the surface
// cannot see those threads end). The surface's own close, replacement and
// destruction, and its leaving the window (window close, application
// shutdown), stop a running program with a bounded wait of the grace, then
// abandon it: they never join a hung content thread.
//
// Hosted content (start_hosted_content): the surface forwards its own input
// to the program's process, in the owner's input order
// (manifests/abi/interaction_order.json, ADR-003 amendment 2026-11-12).
// Each physical event goes to the program after GUI.Forms has raised it on
// the surface (the window's key events, the preview and the surface's own
// key_down, key_up, mouse_down, mouse_move, mouse_up and mouse_wheel, and
// for text the window's and the surface's key_press), and only when none of
// them handled it: a subscriber or rule that sets handled (or a key_down
// that sets suppress_key_press) keeps the event from the program, as it
// keeps it from a control's own handling. A forwarded event is marked
// handled. Derived events (click, double click) are not forwarded; the
// program derives its own from the physical ones, which arrive in rule 1's
// order. Focus goes as got and lost focus, after GUI.Forms' own focus
// events; a key up the window makes when it loses activation is forwarded
// too, so the program never keeps a key down. Dialog keys the window
// processes (Tab and the like) do not reach the surface and are not
// forwarded. Pointer positions are in the region's device pixels. When a
// handler disposes the surface the rest of the sequence is not forwarded
// (rule 5). Opening other content or closing it ends the forwarding.
//
// Accessibility: the consumer's pixels carry no text. It supplies the
// surface's semantic text (ContentSemantics) through the sink from its own
// thread, or through set_content_semantics on the UI thread; the surface
// publishes a figure node named by it, with the text as its value.
class GUI_FORMS_API ContentSurface final : public Control {
public:
    explicit ContentSurface(StableId stable_id);
    // Revokes the channel: the consumer's sink reports revoked from then on.
    ~ContentSurface() override;

    // Opens a new channel and returns its sink, revoking any earlier channel
    // (that sink reports revoked) and dropping its frame. `opaque` is the
    // placeholder's promise until the first frame (each frame states its
    // own). Status becomes waiting; the watchdog starts once the surface is
    // in a window. Throws std::bad_alloc when the surface's first buffers
    // cannot be made.
    [[nodiscard]] ContentFrameSink open_content(bool opaque);
    // Revokes the channel and clears the region. Status becomes
    // disconnected. Idempotent.
    // Stops a running program first (bounded, as the class comment says).
    void close_content();
    [[nodiscard]] bool content_open() const noexcept { return channel_ != nullptr; }

    // Opens a new channel (open_content) and runs `program` on a thread of
    // its own (ContentWorker) with its sink. A running program is stopped
    // first. Null program is std::invalid_argument; a thread the system
    // refuses is std::system_error (the channel is closed again).
    void start_program(std::unique_ptr<ContentProgram> program, bool opaque);
    // Makes a hosted program (make_hosted_content), starts it as
    // start_program does, and forwards the surface's input to its process
    // (the class comment). Returns the input channel, which the surface also
    // keeps. Throws what make_hosted_content and start_program throw.
    HostedInput start_hosted_content(HostedContentOptions options, bool opaque);
    // The input channel the surface forwards to; an empty one (every send
    // dropped) without hosted content.
    [[nodiscard]] const HostedInput& hosted_input() const& noexcept { return hosted_input_; }
    const HostedInput& hosted_input() const&& = delete;
    // Whether a program the surface started has not yet been reaped
    // (finished, failed or abandoned).
    [[nodiscard]] bool program_running() const noexcept { return worker_ != nullptr; }
    // Asks the content to stop and returns at once (the class comment).
    // Status becomes terminating while a program runs, ended once it
    // finished (failed if it threw), abandoned when the grace passed first;
    // content on the consumer's own threads becomes ended at once. Nothing
    // without content, or after it ended.
    void terminate();
    // The bounded wait of a termination, close, replacement or departure
    // (default_content_termination_grace; capped at
    // maximum_content_termination_grace).
    void set_termination_grace(std::chrono::milliseconds grace);
    [[nodiscard]] std::chrono::milliseconds termination_grace() const noexcept { return termination_grace_; }
    // What the program threw, for status failed; empty otherwise.
    [[nodiscard]] const std::string& failure() const& noexcept { return failure_; }
    const std::string& failure() const&& = delete;

    [[nodiscard]] ContentSurfaceStatus status() const noexcept { return status_; }
    // Raised on the UI thread when status() changes, with the new status.
    [[nodiscard]] Event<ContentSurfaceStatus>& status_changed() noexcept { return status_changed_; }

    // How long content may publish nothing before it is stale
    // (ContentWatchdog: zero disables the watchdog; shorter than
    // minimum_content_stale_interval is raised to it). Restarts the interval.
    void set_stale_interval(FrameInterval interval);
    [[nodiscard]] FrameInterval stale_interval() const noexcept { return watchdog_.interval(); }
    // One check at `now`: what the surface's timer does each period. Reaps a
    // program that finished, abandons one whose termination's grace passed,
    // then observes the watchdog. Hosts and tests may call it; it does
    // nothing without open content.
    void check_watchdog(FrameTime now);

    // The text the veil shows for a status (stale, abandoned and failed have
    // English defaults; others show nothing). Empty hides the caption; the
    // veil is still drawn.
    void set_status_caption(ContentSurfaceStatus status, std::string caption);
    [[nodiscard]] const std::string& status_caption(ContentSurfaceStatus status) const noexcept;

    // The channel's counters; empty without open content.
    [[nodiscard]] ContentSurfaceStatistics statistics() const noexcept;
    // The newest request sent to the content (device size, scale and
    // generation).
    [[nodiscard]] ContentRequest request() const noexcept;
    // The semantic text the surface publishes: the newest the sink delivered
    // or set_content_semantics set.
    [[nodiscard]] const ContentSemantics& content_semantics() const noexcept { return semantics_; }
    // Sets the semantic text on the UI thread; std::invalid_argument when
    // validate_content_semantics refuses it (nothing changes then).
    void set_content_semantics(ContentSemantics semantics);
    // The surface the frames are published into; null without content. Kept
    // after a termination, so the last good frame stays on screen.
    [[nodiscard]] std::shared_ptr<LiveSurface> live_surface() const noexcept { return surface_; }

    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    // Arranges as any control, then sends the region's device size and the
    // window scale to the content when either changed.
    void arrange(Rect final_bounds) override;
    // Composes the newest frame into the region (Painter::draw_live_surface),
    // and the veil and caption while the status asks for them.
    void on_paint(Painter& painter, Rect local_damage) override;
    // Forwards text (an input method's composition included) to hosted
    // content when nothing handled it (the class comment).
    void on_text_input(TextInputEvent& event) override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    // A figure named by the semantic text's name (the accessible name, else
    // "Content", without one), whose value is its text; busy while stale or
    // terminating.
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;

    // Stage T1 anatomy, as dot members. `content` is the composed frame,
    // present while there is a surface; `veil` covers the region while the
    // status is stale, abandoned or failed.
    struct GUI_FORMS_API Parts final {
        PartGeometry region;
        PartGeometry content;
        PartGeometry veil;
        PartGeometry focus;
    };
    struct GUI_FORMS_API States final {
        bool normal{};
        bool enabled{};
        bool disabled{};
        bool focused{};
        bool focus_visible{};
    };
    [[nodiscard]] Parts parts() const;
    [[nodiscard]] States states() const;

protected:
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;
    void on_attached_to_window() override;
    void on_detaching_from_window(SurfaceRoot& former_window) noexcept override;
    // Input forwarding to hosted content (the class comment): each calls the
    // base, then forwards what nothing handled.
    void on_key_down(KeyEventArgs& arguments) override;
    void on_key_up(KeyEventArgs& arguments) override;
    void on_mouse_down(MouseEventArgs& arguments) override;
    void on_mouse_up(MouseEventArgs& arguments) override;
    void on_mouse_move(MouseEventArgs& arguments) override;
    void on_mouse_wheel(MouseEventArgs& arguments) override;
    void on_got_focus() override;
    void on_lost_focus() override;

private:
    // Receives the sink's posted notices (first frame, semantic text) on the
    // UI thread. Owned by the surface; the channel holds it weakly.
    class Notices;
    // The watchdog's timer callback; holds the surface weakly.
    class WatchdogTick;

    // A notice arrived: takes new semantic text, notes the first frame.
    void take_notices();
    void set_status(ContentSurfaceStatus status);
    // Starts or stops the watchdog's timer as the window, the channel and
    // the interval allow.
    void schedule_watchdog();
    void watchdog_tick(FrameTime now);
    // Reaps a program that finished, and abandons one whose termination's
    // grace passed at `now`.
    void check_worker(FrameTime now);
    // A finished program's outcome: joins its thread, revokes the channel
    // and sets ended or failed (published only when `publish`).
    void reap_worker(bool publish);
    // Stops a running program with the bounded wait (the class comment) and
    // records its outcome without raising status_changed when `publish` is
    // false (the surface is leaving its window or being destroyed).
    void stop_worker(bool publish) noexcept;
    // Sends the region's device size and the window scale.
    void update_request();
    // Registers the surface (or, without one, an empty placeholder) with the
    // window's live-surface presenter.
    void register_presentation();
    // Forwards a pointer event the surface's raisers left unhandled.
    void forward_pointer(MouseEventArgs& arguments);
    // Forwards a key the surface's raisers left unhandled.
    void forward_key(KeyEventArgs& arguments, KeyAction action);
    // Whether the veil is drawn for the current status.
    [[nodiscard]] bool veiled() const noexcept;

    std::shared_ptr<detail::ContentChannel> channel_{};
    std::shared_ptr<LiveSurface> surface_{};
    // An empty surface registered in place of a closed one (it never holds
    // a frame), made on first use.
    std::shared_ptr<LiveSurface> placeholder_{};
    std::shared_ptr<Notices> notices_{};
    ContentWatchdog watchdog_{};
    FrameRequestToken watchdog_timer_{};
    // The time of the timer's last check, to tell an occlusion gap.
    FrameTime last_check_{};
    bool checked_once_{};
    ContentSurfaceStatus status_{ContentSurfaceStatus::disconnected};
    std::unique_ptr<ContentWorker> worker_{};
    std::chrono::milliseconds termination_grace_{default_content_termination_grace};
    // When a termination in progress abandons the program.
    FrameTime termination_deadline_{};
    std::string failure_{};
    ContentSemantics semantics_{};
    std::uint64_t semantics_generation_{};
    // Indexed by ContentSurfaceStatus.
    std::array<std::string, 8> captions_{};
    bool opaque_{};
    // The hosted program's input channel while the surface forwards to one.
    HostedInput hosted_input_{};
    bool forwarding_{};
    Event<ContentSurfaceStatus> status_changed_;
};

} // namespace gui_forms
