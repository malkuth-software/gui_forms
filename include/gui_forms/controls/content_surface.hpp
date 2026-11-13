#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/content/frame_sink/content_frame_sink.hpp"
#include "gui_forms/content/types/content_types.hpp"
#include "gui_forms/content/watchdog/content_watchdog.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/live_surface/surface/live_surface.hpp"
#include "gui_forms/scheduler/frame_request_token/frame_request_token.hpp"

#include <array>
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
    void close_content();
    [[nodiscard]] bool content_open() const noexcept { return channel_ != nullptr; }

    [[nodiscard]] ContentSurfaceStatus status() const noexcept { return status_; }
    // Raised on the UI thread when status() changes, with the new status.
    [[nodiscard]] Event<ContentSurfaceStatus>& status_changed() noexcept { return status_changed_; }

    // How long content may publish nothing before it is stale
    // (ContentWatchdog: zero disables the watchdog; shorter than
    // minimum_content_stale_interval is raised to it). Restarts the interval.
    void set_stale_interval(FrameInterval interval);
    [[nodiscard]] FrameInterval stale_interval() const noexcept { return watchdog_.interval(); }
    // One watchdog check at `now`: what the surface's timer does each period.
    // Hosts and tests may call it; it does nothing without open content.
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
    // Sends the region's device size and the window scale.
    void update_request();
    // Registers the surface (or, without one, an empty placeholder) with the
    // window's live-surface presenter.
    void register_presentation();
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
    ContentSemantics semantics_{};
    std::uint64_t semantics_generation_{};
    // Indexed by ContentSurfaceStatus.
    std::array<std::string, 8> captions_{};
    bool opaque_{};
    Event<ContentSurfaceStatus> status_changed_;
};

} // namespace gui_forms
