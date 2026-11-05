#pragma once

#include "gui_forms/surface_root/surface_root.hpp"

namespace gui_forms {

// A top-level window: a SurfaceRoot (its control tree, thread and
// dispatcher) presented through a native host.
class Window : public SurfaceRoot {
public:
    explicit Window(Control::Ptr root, Size client_size = {});
    /// Uses explicit per-window image quotas. The two-argument constructor
    /// retains the normal defaults; large-document consumers choose a budget.
    Window(Control::Ptr root, Size client_size, ImageRegistryLimits image_limits);
    /// Shares an application's image registry and theme with every other
    /// Window constructed with the same resources (stage DK decision 8): an
    /// ImageId, an ImageList and the theme hold in all of them, so a control
    /// subtree moved between them keeps its images. Null resources are
    /// std::invalid_argument; resources made on another thread are
    /// std::logic_error. The window takes the resources' current theme.
    Window(Control::Ptr root, Size client_size, std::shared_ptr<ApplicationResources> resources);
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    [[nodiscard]] CursorMetrics cursor_metrics() const noexcept;
    [[nodiscard]] CursorCapabilities cursor_capabilities() const noexcept;
    [[nodiscard]] CursorLeaseResult begin_cursor_hidden();
    // Synchronous client-DIP placement. Success is a native request, not a
    // fabricated motion/click; OS motion may be delivered or coalesced normally.
    [[nodiscard]] CursorStatus warp_cursor(CursorMetrics metrics, Point target);
    // Host-only: a host calls this after its native window's client area
    // changed size. It does not resize the native window; an application
    // asks for a size with ApplicationWindowHandle::request_client_size.
    void resize(Size client_size);
    void set_scale(double scale);

    // Records one live-surface frame the host copied to the screen. Live
    // frames carry no paint receipt; this only counts their cost.
    void notify_live_presented(std::uint64_t duration_nanoseconds, PresentedArea presented);
    // Called by a host when it attaches this window (stage DP). Throws
    // std::invalid_argument when the declared contract version is older than
    // paint_contract_version, or claims version 2 or later without partial
    // present: such a host fails attachment rather than presenting in a
    // degraded mode.
    void declare_presentation(PresentationDeclaration declaration);
    // The availability record: the host's declaration and the live lease.
    [[nodiscard]] PresentationAvailability presentation_availability() const noexcept;

    [[nodiscard]] bool queue_live_surface_presentation(
        const Control::Ptr& control, std::shared_ptr<LiveSurface> surface);
    // Returns surfaces with a new generation or a changed placement
    // (destination, clip or crossing overlays). Failed native batches request
    // unchanged generations too, with current visibility and overlay clips;
    // callers must not retry stale placements.
    // Optional publication_changed reports newly observed producer generations,
    // independently of composition-only updates or hidden control sampling.
    [[nodiscard]] std::vector<LiveSurfacePresentation>
        take_live_surface_presentations(bool include_unchanged = false,
                                        bool* publication_changed = nullptr);
    // A registration is retained while its control remains attached. Native
    // display clocks use this to stay armed without polling the control tree or
    // requiring one UI callback per producer publication.
    [[nodiscard]] bool has_live_surface_presentations() const noexcept {
        return !live_surface_registrations_.empty();
    }

    // Native host idle handshake. Install/arm on the UI thread. The handler
    // may run on a producer thread and must only schedule lifetime-safe UI work.
    // One publication wakes each armed idle period; arm also checks generations
    // to cover a publication racing the host's final empty presentation drain.
    void set_live_surface_idle_wake_handler(std::function<void()> wake);
    void set_live_surface_idle_waiting(bool waiting);

private:
    friend class SurfaceRoot;
    friend class HostSession;
    // Called by HostSession: seeds the window's bindings with the host's
    // defaults unless they are seeded already.
    void install_host_key_map(const KeyMap& defaults);
    void initialize_cursor_identity() noexcept;
    void invalidate_cursor_interaction(CursorError cause) noexcept;
    std::uint64_t cursor_window_id_{};
    std::uint64_t cursor_metrics_generation_{1};
    struct LiveSurfaceRegistration final {
        Control::WeakPtr control;
        std::shared_ptr<LiveSurface> surface;
        std::uint64_t sampled_epoch{};
        std::uint64_t sampled_generation{};
        // Placement of the last presentation: destination, visible clip
        // before overlay subtraction, and the overlays crossing that clip.
        bool placed{};
        Rect placed_destination{};
        Rect placed_clip{};
        std::vector<Rect> placed_overlays;
        LiveSurfaceWakeConnection idle_wake{};
        std::uint64_t observed_epoch{};
        std::uint64_t observed_generation{};
    };
    using LiveSurfaceRegistrationMap =
        std::unordered_map<std::uint64_t, LiveSurfaceRegistration>;
    void collect_visible_overlay_rectangles(
        const Control::Ptr& control, std::vector<Rect>& rectangles) const;
    [[nodiscard]] static std::vector<Rect> subtract_rectangle(
        Rect source, Rect cover);
    LiveSurfaceRegistrationMap live_surface_registrations_;
    std::shared_ptr<detail::LiveSurfaceIdleWake> live_surface_idle_wake_{};
    PresentationDeclaration presentation_declaration_{};
};

} // namespace gui_forms
