#pragma once

#include "gui_forms/window/update_scope/update_scope.hpp"

#include "gui_forms/commands/command_route/command_route.hpp"

#include "gui_forms/control.hpp"
#include "gui_forms/display.hpp"
#include "gui_forms/dispatcher.hpp"
#include "gui_forms/engine/motion_clock/motion_clock.hpp"
#include "gui_forms/engine/part_table/engine_part_table.hpp"
#include "gui_forms/live_surface/wake_connection/live_surface_wake_connection.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/metrics.hpp"
#include "gui_forms/paint_framebuffer.hpp"
#include "gui_forms/host/compositor/window_backdrop.hpp"
#include "gui_forms/host/cursor_interaction/cursor_interaction.hpp"
#include "gui_forms/layout/diagnostics/layout_diagnostics.hpp"
#include "gui_forms/resources.hpp"
#include "gui_forms/scheduler.hpp"
#include "gui_forms/window/presentation/presentation_types.hpp"
#include "gui_forms/window/tool_tips/tool_tips.hpp"
#include "gui_forms/visual_inspection/types/visual_inspection_types.hpp"

#include <chrono>
#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace gui_forms {

class UpdateScope;
class Timer;
class BackgroundWorker;
class ToolTip;
class RibbonToolTip;
class ImageList;
class HostServices;
class HostSession;
class BindingSource;
class NotifyIcon;
class BindingContext;
class LiveSurface;
class PaintFramebuffer;
struct HelpRequestEvent;
namespace detail {
struct LiveSurfaceIdleWake;
class PopupAttachment;
class AcceleratorAttachment;
class RecordingPainter;
class ToolTipPresenter;
class MessageBoxHelpRoute;

// What a window offers the help requests no handler of its own took
// (Window::request_help): each HelpProvider of the window is one, offered in
// the order the providers were created. True when it handled the request.
class WindowHelpFallback {
public:
    virtual bool offer_window_help(HelpRequestEvent& request) = 0;

protected:
    ~WindowHelpFallback() {}
};
}

class PopupToken final {
public:
    PopupToken() = default;
    ~PopupToken() { disconnect(); }
    PopupToken(PopupToken&& other) noexcept
        : attachment_(std::move(other.attachment_)) {}
    PopupToken& operator=(PopupToken&& other) noexcept {
        if (this != &other) {
            disconnect();
            attachment_ = std::move(other.attachment_);
        }
        return *this;
    }
    PopupToken(const PopupToken&) = delete;
    PopupToken& operator=(const PopupToken&) = delete;

    void disconnect() noexcept;
    [[nodiscard]] bool connected() const noexcept;
    [[nodiscard]] Event<>* closed_event() noexcept;

private:
    friend class Window;
    explicit PopupToken(std::shared_ptr<detail::PopupAttachment> attachment)
        : attachment_(std::move(attachment)) {}
    std::shared_ptr<detail::PopupAttachment> attachment_;
};

struct KeyGesture final {
    std::uint32_t physical_key{};
    Modifier modifiers{Modifier::none};
    friend constexpr bool operator==(const KeyGesture& left,
                                     const KeyGesture& right) noexcept {
        return left.physical_key == right.physical_key &&
               left.modifiers == right.modifiers;
    }
    friend constexpr std::strong_ordering operator<=>(
        const KeyGesture& left, const KeyGesture& right) noexcept {
        const std::strong_ordering key_order =
            left.physical_key <=> right.physical_key;
        return key_order != 0
            ? key_order : left.modifiers <=> right.modifiers;
    }
};

struct AcceleratorOptions final {
    // Application navigation gestures such as Alt+Left may preempt ordinary
    // editor word-navigation. Active popup/focus scopes always retain first
    // refusal so menus and modal editors remain contained.
    bool before_focused_route{};
};

inline constexpr std::size_t maximum_deferred_inputs = 1024U;

// Renderer-neutral lease-time input diagnostics. Pointer moves may compact,
// but critical input is either retained in order or counted as capacity
// rejected; no event silently enters application code while paint is active.
struct DeferredInputSnapshot final {
    std::size_t pending{};
    std::size_t capacity{maximum_deferred_inputs};
    std::uint64_t deferred{};
    std::uint64_t delivered{};
    std::uint64_t coalesced_moves{};
    std::uint64_t coalesced_drag_overs{};
    std::uint64_t rejected_capacity{};
    std::uint64_t abandoned{};
    std::uint64_t faults{};
    std::uint64_t drains{};
    bool drain_queued{};
    bool draining{};
};

class AcceleratorToken final {
public:
    AcceleratorToken() = default;
    ~AcceleratorToken() { disconnect(); }
    AcceleratorToken(AcceleratorToken&& other) noexcept
        : attachment_(std::move(other.attachment_)) {}
    AcceleratorToken& operator=(AcceleratorToken&& other) noexcept {
        if (this != &other) {
            disconnect();
            attachment_ = std::move(other.attachment_);
        }
        return *this;
    }
    AcceleratorToken(const AcceleratorToken&) = delete;
    AcceleratorToken& operator=(const AcceleratorToken&) = delete;

    void disconnect() noexcept;
    [[nodiscard]] bool connected() const noexcept;

private:
    friend class Window;
    explicit AcceleratorToken(
        std::shared_ptr<detail::AcceleratorAttachment> attachment)
        : attachment_(std::move(attachment)) {}
    std::shared_ptr<detail::AcceleratorAttachment> attachment_;
};

struct PointerCaptureChange final {
    bool captured{};
    RuntimeId control_id{};
    std::string stable_id;
    std::uint64_t pointer_id{};
};

// Focus moved from one control to another (or to or from none) through
// request_focus. `previous_id` and `current_id` are zero for none;
// `stable_id` is the newly focused control's, or empty.
struct FocusedControlChange final {
    RuntimeId previous_id{};
    RuntimeId current_id{};
    std::string stable_id;
};

struct ControlAvailabilityChange final {
    RuntimeId control_id{};
    std::string stable_id;
    bool effectively_visible{};
    bool effectively_enabled{};
};

struct PopupOptions final {
    // Interactive menus and editors require an enabled owner. Passive
    // providers such as ToolTip may opt out while still requiring a live,
    // attached, effectively visible owner.
    bool require_enabled_owner{true};
};

struct FocusScopeId final {
    std::uint64_t value{};
    [[nodiscard]] explicit constexpr operator bool() const noexcept {
        return value != 0;
    }
    friend constexpr bool operator==(const FocusScopeId& left,
                                     const FocusScopeId& right) noexcept {
        return left.value == right.value;
    }
    friend constexpr std::strong_ordering operator<=>(
        const FocusScopeId& left, const FocusScopeId& right) noexcept {
        return left.value <=> right.value;
    }
};

inline constexpr std::size_t maximum_focus_scope_depth = 32U;

struct FocusScopeOptions final {
    // Contained scopes reject attempts to move keyboard focus outside their
    // retained subtree while the scope is active.
    bool contain_focus{true};
    // Closing a scope restores the focus that was active when it opened.
    bool restore_focus{true};
    // If the previous focus is outside the scope, focus the first eligible
    // descendant in stable retained-tree order.
    bool focus_first{true};
};

enum class FocusScopeCloseReason : std::uint8_t {
    explicit_close,
    owner_unavailable,
};

struct FocusScopeChange final {
    FocusScopeId scope{};
    RuntimeId root_id{};
    std::string stable_id;
    std::size_t depth{};
    FocusScopeCloseReason close_reason{FocusScopeCloseReason::explicit_close};
    bool opened{};
    bool restored_focus{};
};

struct ValidationSnapshot final {
    std::uint64_t attempts{};
    std::uint64_t succeeded{};
    std::uint64_t cancelled{};
    std::uint64_t focus_moves_blocked{};
    std::uint64_t reentrant_requests_rejected{};
    std::uint64_t bulk_controls_visited{};
    bool validating{};
};

struct DialogKeySnapshot final {
    std::uint64_t mnemonic_attempts{};
    std::uint64_t mnemonics_handled{};
    std::uint64_t accept_attempts{};
    std::uint64_t accept_handled{};
    std::uint64_t cancel_attempts{};
    std::uint64_t cancel_handled{};
    std::uint64_t command_rejections{};
    std::uint64_t mnemonic_candidates{};
    std::uint64_t mnemonic_collisions{};
    std::uint64_t mnemonic_cycles{};
};

// A control subtree's picture (Window::snapshot_control): the registered
// image, the logical size it covers and its pixels per logical unit.
struct ControlSnapshot final {
    ImageId image{};
    Size logical_size{};
    double scale{};
    std::uint32_t pixel_width{};
    std::uint32_t pixel_height{};
};

class Window {
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

    [[nodiscard]] Control::Ptr root() const noexcept { return root_; }
    [[nodiscard]] Size client_size() const noexcept { return client_size_; }
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
    [[nodiscard]] double scale() const noexcept { return scale_; }
    [[nodiscard]] const PresentationSettings& presentation_settings() const noexcept {
        return presentation_settings_;
    }
    void set_presentation_settings(PresentationSettings settings);
    void set_text_scale(double text_scale);
    // The host installs its terminal text engine through this non-owning,
    // renderer-neutral seam. Replacing it invalidates measurement so controls
    // never retain geometry produced by a previous font/raster profile.
    void set_text_metrics_provider(TextMetricsProvider* provider);
    // Native host borrows its painter until shutdown; clear before destroying it.
    // Created framebuffers own their rendering state independently of the host.
    void set_framebuffer_painter(Painter* painter);
    [[nodiscard]] std::unique_ptr<PaintFramebuffer> create_framebuffer(Size logical_size, double scale);
    // A picture of a control and its descendants as the window last laid
    // them out (stage DK phase D3: the drag stamp, owner decision D1): the
    // subtree's retained display chunks are replayed, every paint plane in
    // order, into an offscreen framebuffer of the host's own CPU raster at
    // `scale` device pixels per logical unit, and the pixels are registered
    // as one of this window's images (its application's, where it shares
    // ApplicationResources; premultiplied BGRA). Areas the subtree
    // does not paint stay transparent; the window's backplane is not drawn.
    // None, changing nothing, while no framebuffer painter is installed (a
    // headless window, a host whose fonts are not ready), during a paint,
    // for a control of another window or not shown, for a scale outside
    // (0, 8], or when the framebuffer or the image registry refuses.
    // Cost: one replay of the subtree into ceil(width * scale) by
    // ceil(height * scale) pixels and one copy of them into the registry;
    // chunks the replay had to rebuild are marked for the next real paint.
    // The image lives until release_control_snapshot or the window's end.
    [[nodiscard]] std::optional<ControlSnapshot> snapshot_control(const Control::Ptr& control, double scale);
    // Removes a snapshot's image as remove_image does: every window sharing
    // the registry retires its retained paint, so no chunk replays the dead
    // image. False, changing nothing, for an image that is not there.
    // std::logic_error off the UI thread.
    bool release_control_snapshot(ImageId image);
    [[nodiscard]] TextMetricsProvider* text_metrics_provider() const noexcept {
        return text_metrics_provider_;
    }
    [[nodiscard]] ResolvedTextLayout resolve_text_layout_utf8(
        std::string_view utf8, FontSpec effective_font) const;
    // The installed provider's line-break opportunities
    // (TextMetricsProvider::fill_line_break_opportunities_utf8), or the
    // estimate without one.
    [[nodiscard]] TextResolutionStatus fill_line_break_opportunities_utf8(
        std::string_view utf8, std::span<LineBreakOpportunity> opportunities) const;
    [[nodiscard]] Event<const PresentationSettings&>& presentation_changed() noexcept {
        return presentation_changed_;
    }
    [[nodiscard]] const Theme& theme() const noexcept { return *theme_; }
    // The rules of the user theme layered under every theme of these
    // resources (ApplicationResources::user_theme_rules), or null.
    [[nodiscard]] const CompiledThemeRules* user_theme_rules() const noexcept;
    [[nodiscard]] std::shared_ptr<const Theme> theme_ptr() const noexcept {
        return theme_;
    }
    // Sets the theme of this window and of every window sharing its
    // resources (ApplicationResources::set_theme).
    void set_theme(std::shared_ptr<const Theme> theme);
    // Whether the window has drawn a control's inline style under a theme
    // with rules of its own: the warning W401 `inline_with_theme` (T3, the
    // owner's rule E1), written to the log once per window.
    [[nodiscard]] bool inline_with_theme_reported() const noexcept { return inline_with_theme_reported_; }
    // The image registry and theme this window shares; pass it to another
    // Window's constructor to share them with that window too.
    [[nodiscard]] std::shared_ptr<ApplicationResources> resources() const noexcept {
        return resources_;
    }
    [[nodiscard]] bool shares_resources_with(const Window& other) const noexcept {
        const bool same = resources_ == other.resources_;
        return same;
    }
    [[nodiscard]] Event<const Theme&>& theme_changed() noexcept {
        return theme_changed_;
    }
    [[nodiscard]] bool active() const noexcept { return active_; }
    void set_active(bool active);
    [[nodiscard]] Event<bool>& active_changed() noexcept {
        return active_changed_;
    }
    // Compositor cooperation (stage T5, notes:planning/stages/T5-compositor/
    // PLAN.md §2). Declares, before a host attaches, that this window may
    // be presented non-opaque (owner decision T5-Q4);
    // ApplicationWindowOptions::non_opaque does it for an application
    // window. False, changing nothing, once a host session has been bound
    // to the window: a host fixes what a native window can show when it
    // creates it. UI thread.
    [[nodiscard]] bool declare_non_opaque();
    [[nodiscard]] bool non_opaque_declared() const noexcept { return non_opaque_declared_; }
    // The host's compositor state as last reported. Before any report:
    // compositing unknown and only `opaque` supported.
    [[nodiscard]] const CompositorState& compositor_state() const noexcept { return compositor_state_; }
    // Host-only: the HostSession passes each accepted compositor report
    // here. With `host_preferences` (capability presentation_preferences) a
    // change in the host's high-contrast or reduced-transparency setting
    // is copied into presentation_settings(); a setting the host did not
    // change keeps what the application set. Then the backdrop grant is
    // resolved again.
    void set_compositor_state(const CompositorState& state, bool host_preferences);
    // Asks for a backdrop (owner decisions T5-Q3 and T5-Q5) and returns
    // the grant now in force: the request's first entry this window may be
    // given under the host's state and its presentation settings, else
    // `opaque`, with the reason the preferred entry was or was not given.
    // The request stands: the grant is resolved again whenever the state,
    // the settings or the declaration change. Until the application makes
    // one, the window makes its theme's (ThemeWindowBackdrop::request) and
    // follows each new theme's. std::invalid_argument, changing nothing,
    // for a request window_backdrop_request_valid refuses. UI thread.
    WindowBackdropGrant request_backdrop(const WindowBackdropRequest& request);
    // The request in force: the application's, else the theme's.
    [[nodiscard]] const WindowBackdropRequest& backdrop_request() const noexcept { return backdrop_request_; }
    [[nodiscard]] const WindowBackdropGrant& backdrop_grant() const noexcept { return backdrop_grant_; }
    // Raised on the UI thread after the grant's backdrop, reason or request
    // changes (not for a new generation alone), once per change. When the
    // granted backdrop changes, the backplane's generation has advanced and
    // the window is marked for repaint before it is raised. A handler may
    // call request_backdrop; its change raises its own event within this
    // one, and later handlers of this emission still see this emission's
    // grant.
    [[nodiscard]] Event<const WindowBackdropGrant&>& backdrop_changed() noexcept {
        return backdrop_changed_;
    }
    // Non-owning portable service seam, installed for the lifetime of a
    // HostSession. Renderer-free and headless windows may legitimately return
    // null when no host is attached.
    [[nodiscard]] HostServices* host_services() const noexcept {
        return host_services_;
    }
    // The keys of the named keyboard commands (owner decisions DK-30 and
    // DK-44): the window's KeyBindings, which an application shares among all
    // its windows (Application gives every window it runs its one set, and a
    // floating window takes its main window's). A window made alone has a
    // set of its own. The first HostSession to attach seeds the set with the
    // host's defaults (HostCapabilities::key_map); the PC convention holds
    // until then. Controls ask through Control::effective_key_map.
    [[nodiscard]] const KeyMap& key_map() const noexcept { return (*key_bindings_).map(); }
    [[nodiscard]] const std::shared_ptr<KeyBindings>& key_bindings() const noexcept { return key_bindings_; }
    // Shares `bindings` with this window. A null pointer throws
    // std::invalid_argument and changes nothing.
    void set_key_bindings(std::shared_ptr<KeyBindings> bindings);
    // KeyBindings::rebind and restore on the window's set, for every window
    // sharing it. Throw std::invalid_argument as those do.
    void rebind_key_command(KeyCommand command, KeyBinding binding);
    void restore_key_command(KeyCommand command);

    [[nodiscard]] UpdateScope begin_update();
    void perform_layout();
    void flush();
    // Returns an exact presentation receipt only when a complete candidate
    // replay remains current for its surface epoch. Ignoring the result is
    // valid for renderer-neutral inspection; a native host must require it.
    std::optional<PaintReceipt> paint(
        Painter& painter, Rect requested_damage = {});
    // Damage-preserving paint (stage DP): one lease and one receipt for a
    // bounded set of rectangles, each painted under its own clip so pixels
    // between them are untouched. Rectangles that overlap or nearly touch are
    // merged first; more than DamageRegion::maximum_rectangles collapse to
    // their bounds. An empty set paints the accumulated damage rectangles
    // rather than their bounding box.
    std::optional<PaintReceipt> paint(
        Painter& painter, std::span<const Rect> requested_damage);
    // Preferred host release. Returns false for a duplicate, out-of-order, or
    // replaced-epoch receipt and never advances the presentation revision in
    // that case.
    [[nodiscard]] bool notify_presented(
        PaintReceipt receipt, std::uint64_t duration_nanoseconds = 0U);
    // The same release, also recording the screen area the present copied.
    [[nodiscard]] bool notify_presented(
        PaintReceipt receipt, std::uint64_t duration_nanoseconds, PresentedArea presented);
    // Records one live-surface frame the host copied to the screen. Live
    // frames carry no paint receipt; this only counts their cost.
    void notify_live_presented(std::uint64_t duration_nanoseconds, PresentedArea presented);
    // Synchronous compatibility spelling. It acknowledges the latest complete
    // receipt in the current epoch; asynchronous/native hosts must use the
    // exact-receipt overload above.
    void notify_presented(std::uint64_t duration_nanoseconds = 0U);
    [[nodiscard]] PaintLeaseSnapshot paint_lease_snapshot() const noexcept;
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

    [[nodiscard]] DamageRegion take_damage();
    [[nodiscard]] DamageRegion take_damage(PaintPlane plane);
    [[nodiscard]] bool needs_frame() const noexcept;
    [[nodiscard]] std::optional<FrameTime> next_wake() const noexcept;
    [[nodiscard]] FrameRequestToken schedule_paint(const Control::Ptr& control,
                                                   FrameTime deadline);
    [[nodiscard]] FrameRequestToken schedule_ui_timer(
        Component& owner, FrameInterval interval, FrameTime first_deadline,
        std::function<void(FrameTime)> callback);
    [[nodiscard]] FramePollResult poll_frame_schedule(FrameTime now);
    void cancel_frame_requests();
    void set_occluded(bool occluded, FrameTime transition_time);
    [[nodiscard]] bool occluded() const noexcept { return occluded_; }
    [[nodiscard]] bool check_access() const noexcept;
    void verify_access(std::string_view operation = "window access");
    [[nodiscard]] bool invoke_required() const noexcept { return !check_access(); }
    [[nodiscard]] DispatchOperation begin_invoke(std::function<void()> callback);
    [[nodiscard]] DispatchOperation begin_invoke(
        const Control::Ptr& owner, std::function<void()> callback);
    // Synchronous Invoke never starts a nested message pump. UI-thread calls
    // execute inline; worker calls require an installed running host wake seam.
    void invoke(std::function<void()> callback);
    void invoke(const Control::Ptr& owner, std::function<void()> callback);
    // A handle to this window's dispatcher that any thread may keep and post
    // through after the window has closed (WindowDispatcher).
    [[nodiscard]] WindowDispatcher dispatcher() const;
    [[nodiscard]] DispatchDrainResult drain_posted_work(
        std::size_t maximum_callbacks = maximum_callbacks_per_dispatch_turn);
    [[nodiscard]] DispatcherSnapshot dispatcher_snapshot() const noexcept;
    // Host adapters install a thread-safe wake primitive. Installing a handler
    // after work was queued immediately publishes one coalesced wake. The
    // handler runs under the dispatcher's lock, on whichever thread posted:
    // it may only signal (set a flag, post a message, write a pipe) and must
    // never call back into the window. In return, once this call replaces or
    // removes a handler, no wake through the old one is under way or to come,
    // so its target may be destroyed. A thread may post (begin_invoke, invoke)
    // while holding locks of its own, so the lock order is the poster's locks,
    // then the dispatcher's, then any the handler takes: the handler must take
    // no lock that a posting thread may hold. The stock hosts' handlers take
    // none of GUI.Forms' or the application's.
    void set_dispatch_wake_handler(std::function<void()> wake);
    // Host adapters also install a paint wake seam. Retained invalidation may
    // originate from another Window's callback, so relying only on the host
    // currently dispatching input can strand valid dirty state indefinitely.
    // Wakes coalesce until the host consumes damage and remain suppressed while
    // the model is explicitly occluded.
    void set_paint_wake_handler(std::function<void()> wake);
    // Stops accepting posted work and cancels everything pending. A host
    // releases pointer capture first (see HostSession), so that controls
    // that post about the release meet an accepting dispatcher.
    void shutdown_dispatcher() noexcept;

    [[nodiscard]] ImageLoadResult load_png(std::span<const std::byte> encoded);
    [[nodiscard]] ImageLoadResult load_bgra32_premultiplied(
        std::uint32_t width, std::uint32_t height, std::uint64_t row_bytes,
        std::span<const std::byte> pixels);
    [[nodiscard]] ImageLoadResult replace_png(ImageId image,
                                               std::span<const std::byte> encoded);
    [[nodiscard]] ImageLoadResult replace_png(ImageId image,
                                               std::span<const std::byte> encoded,
                                               Control& consumer);
    [[nodiscard]] ImageLoadResult replace_bgra32_premultiplied(
        ImageId image, std::uint32_t width, std::uint32_t height,
        std::uint64_t row_bytes, std::span<const std::byte> pixels,
        Control& consumer);
    [[nodiscard]] ImageLoadResult update_bgra32_premultiplied(
        ImageId image, std::uint32_t width, std::uint32_t height,
        std::uint64_t row_bytes, std::span<const std::byte> pixels,
        Control& consumer);
    [[nodiscard]] ImageLoadResult patch_bgra32_premultiplied(
        ImageId image, std::uint32_t x, std::uint32_t y,
        std::uint32_t width, std::uint32_t height,
        std::uint64_t source_row_bytes, std::span<const std::byte> pixels,
        Control& consumer, Rect local_damage);
    [[nodiscard]] bool remove_image(ImageId image);
    [[nodiscard]] const ImageRegistry& image_resources() const noexcept {
        return (*resources_).images();
    }
    [[nodiscard]] ImageRegistrySnapshot image_resource_snapshot() const noexcept {
        return (*resources_).images().snapshot();
    }

    [[nodiscard]] Control::Ptr find(std::string_view stable_id) const;
    [[nodiscard]] Control::Ptr hit_test(Point position);
    bool request_focus(const Control::Ptr& control);
    [[nodiscard]] Control::Ptr focused_control() const noexcept { return focused_.lock(); }
    // Raised by request_focus after focus moved, once the controls' own
    // focus notifications (on_focus_changed, focus_observed) ran; not raised
    // when focus is dropped because the focused control left the window. A
    // docking site follows it to keep its workspace's active panel. A handler
    // may request focus again; that move raises its own event afterwards.
    [[nodiscard]] Event<const FocusedControlChange&>& focused_control_changed() noexcept {
        return focused_control_changed_;
    }
    // Help asked of the window as a whole, as Windows Forms raises
    // HelpRequested on a message box's owner form: the Help button of a
    // message box the window owns, or F1 while it is open
    // (show_message_box). The request names the root control, at the
    // window's centre, with no help string, keyword or namespace. When no
    // handler sets `handled`, the window's HelpProviders are offered it in
    // the order they were created, each raising its own help_requested with
    // its namespace and the root's mapping where it has one, until one
    // handles it (the order F1 takes from a control to its provider).
    // Handlers run while the box is open: they may open other windows but
    // cannot close the box.
    [[nodiscard]] Event<HelpRequestEvent&>& help_requested() noexcept { return help_requested_; }
    // The interactions a window raises, by the name of their event accessor,
    // appended in declaration order: the window events the C ABI subscribes
    // by name (stage C, tools/abi_event_catalog.py). Window is not a
    // Component, so this mirrors Component::append_interactions without
    // being virtual. Every window raises the same ones, so it is static and
    // may run on any thread.
    static void append_interactions(std::vector<InteractionDeclaration>& interactions);
    // Raises the window's help request, as a message box's Help button or
    // F1 does: true when a handler or a provider handled it. UI thread. A
    // handler may dispose a provider, which is then not offered the
    // request, or destroy the window, which ends the offers.
    bool request_help(bool keyboard_initiated = false);
    // Browser-style focus-visible modality: keyboard and semantic navigation
    // expose a cue; primary-pointer focus suppresses it without losing focus.
    [[nodiscard]] bool focus_cue_visible() const noexcept {
        return focus_cue_visible_;
    }
    bool validate_control(const Control::Ptr& control,
                          Control* destination = nullptr,
                          bool bulk = false);
    bool validate_children(
        const Control::Ptr& container,
        ValidationConstraints constraints = ValidationConstraints::selectable);
    [[nodiscard]] ValidationSnapshot validation_snapshot() const noexcept;
    [[nodiscard]] FocusScopeId begin_focus_scope(
        const Control::Ptr& root,
        const Control::Ptr& preferred_focus = {},
        FocusScopeOptions options = {});
    bool end_focus_scope(
        FocusScopeId scope,
        FocusScopeCloseReason reason = FocusScopeCloseReason::explicit_close);
    [[nodiscard]] std::size_t focus_scope_depth() const noexcept;
    [[nodiscard]] Control::Ptr active_focus_scope_root() const noexcept;
    bool move_focus(bool forward = true);
    // F6 regions (owner decision RB-17): the window's ordered list of focus
    // regions, such as a ribbon, a work pane, side panels and a status bar.
    // F6 moves focus to the next region and Shift+F6 to the previous,
    // wrapping around, when the focused control and its ancestors leave the
    // key unhandled. Entering a region focuses the control that last had
    // focus inside it, or else its first Tab stop. Regions must be attached
    // to this window, distinct and not nested in one another; otherwise this
    // throws std::invalid_argument and keeps the previous list. An empty
    // list turns F6 cycling off. A region that is detached, hidden or
    // disabled, or holds no Tab stop, is skipped.
    void set_focus_regions(std::vector<Control::Ptr> regions);
    [[nodiscard]] std::vector<Control::Ptr> focus_regions() const;
    // Appends one region, for a control that registers itself (a Ribbon).
    // Returns false, changing nothing, for a control that is not attached to
    // this window, is already a region, or nests with one.
    bool add_focus_region(const Control::Ptr& region);
    // Moves focus as F6 (forward) or Shift+F6 does. From outside every
    // region, forward enters the first region and backward the last. Returns
    // false, leaving focus alone, when no other region can take focus or a
    // focus scope (a popup or dialog) is active.
    bool move_focus_region(bool forward = true);
    void set_accept_button(const Control::Ptr& control);
    void set_cancel_button(const Control::Ptr& control);
    [[nodiscard]] Control::Ptr accept_button() const noexcept {
        return accept_button_.lock();
    }
    [[nodiscard]] Control::Ptr cancel_button() const noexcept {
        return cancel_button_.lock();
    }
    [[nodiscard]] DialogKeySnapshot dialog_key_snapshot() const noexcept;
    [[nodiscard]] DialogResult dialog_result() const noexcept {
        return dialog_result_;
    }
    void set_dialog_result(DialogResult result);
    [[nodiscard]] Event<DialogResult>& dialog_result_changed() noexcept {
        return dialog_result_changed_;
    }
    [[nodiscard]] Event<const FocusScopeChange&>& focus_scope_changed() noexcept {
        return focus_scope_changed_;
    }
    void capture_pointer(const Control::Ptr& control, std::uint64_t pointer_id = 1);
    void release_pointer();
    [[nodiscard]] Control::Ptr captured_control() const noexcept { return captured_.lock(); }
    [[nodiscard]] std::uint64_t captured_pointer_id() const noexcept {
        return captured_pointer_id_;
    }
    [[nodiscard]] Event<const PointerCaptureChange&>& pointer_capture_changed() noexcept {
        return pointer_capture_changed_;
    }
    [[nodiscard]] Event<const ControlAvailabilityChange&>&
    control_availability_changed() noexcept {
        return control_availability_changed_;
    }
    [[nodiscard]] PopupToken open_popup(const Control::Ptr& owner,
                                        const Control::Ptr& popup,
                                        PopupOptions options = {});

    // Tooltips (owner, 2026-10-28). The window has one presenter, which
    // shows the plain tooltip text of its controls and of their authored
    // parts (see ToolTipSettings) in a retained, input-transparent overlay.
    // The window's switch, when set, decides whether its tooltips show;
    // unset, the default, the program's switch decides
    // (Application::tool_tips_shown). Turning tooltips off closes one that
    // shows. UI thread.
    [[nodiscard]] std::optional<bool> tool_tips_shown_setting() const noexcept {
        return tool_tips_shown_;
    }
    void set_tool_tips_shown(std::optional<bool> shown);
    // Whether this window's tooltips show now: its own switch, or the
    // program's while it has none.
    [[nodiscard]] bool tool_tips_shown() const noexcept;
    [[nodiscard]] const ToolTipSettings& tool_tip_settings() const noexcept {
        return tool_tip_settings_;
    }
    // Throws std::invalid_argument for a negative delay or a maximum width
    // outside 80 to 2048 (or not finite), changing nothing. A tooltip that
    // shows is laid out again at a new width.
    void set_tool_tip_settings(ToolTipSettings settings);
    // Shows `target`'s own tooltip at once, below it, whatever the switches,
    // for `duration` (none: the settings' auto_pop_delay; zero: until
    // hidden). Throws std::invalid_argument for a target that is not a live
    // control attached to this window or whose tooltip text is empty, and
    // for a negative duration.
    void show_tool_tip(const Control::Ptr& target,
                       std::optional<std::chrono::milliseconds> duration = std::nullopt);
    // Closes the tooltip that shows, and cancels one waiting to show.
    void hide_tool_tip();
    [[nodiscard]] bool tool_tip_visible() const noexcept;
    // The control whose tooltip shows, the authored part's ID (empty for the
    // control itself) and the shown text; none and empty while none shows.
    [[nodiscard]] Control::Ptr tool_tip_target() const noexcept;
    [[nodiscard]] std::string tool_tip_item_id() const;
    [[nodiscard]] std::string tool_tip_text() const;
    // Raised after a tooltip opened, and after it closed.
    [[nodiscard]] Event<const ToolTipEvent&>& tool_tip_visibility_changed() noexcept {
        return tool_tip_visibility_changed_;
    }
    // Window accelerators are tried only after the focused retained route
    // declines a key, preserving editor/menu ownership of their native keys.
    // Registrations are revoked by either the token or owner disposal.
    [[nodiscard]] AcceleratorToken register_accelerator(
        Component& owner, KeyGesture gesture, std::function<bool()> callback,
        AcceleratorOptions options = {});
    // Offers a key press to this window's accelerators that wait for the
    // focused route (those not before_focused_route), as a key the window's
    // own route declined would be: true when one took it. For a window
    // passing on keys it did not use, as a floating docking window passes
    // them to its main window (stage DK phase D4, owner decision DK-29:
    // shortcuts work from a floating tool window). UI thread.
    bool offer_accelerators(const KeyEvent& event);
    // The command routing chain (owner decision DK-29; the order is in
    // gui_forms/commands/command_route). Names the target of the active
    // panel, active document or main window stage, or clears it with null.
    // The window observes the target and never extends its life; a
    // destroyed target drops out of the chain. The focused route has no
    // single target (controls carry its targets): naming it throws
    // std::invalid_argument and changes nothing.
    void set_command_target(CommandRouteStage stage, const std::shared_ptr<CommandTarget>& target);
    [[nodiscard]] std::shared_ptr<CommandTarget> command_target(CommandRouteStage stage) const;
    // Where the chain would run a command with this identity now, from the
    // current focus and targets, without running it: outcome handled with
    // the stage and target, or unhandled.
    [[nodiscard]] CommandRouteResult resolve_command(std::string_view command_id) const;
    // Offers `invocation` to the chain and runs the first handler for
    // `invocation.command_id`, holding its target alive while it runs; the
    // chain is read when the routing starts, so a handler that moves focus
    // or names other targets changes only later routings. Exactly one
    // handler runs, or none (unhandled; refused when nested deeper than
    // maximum_command_route_depth inside handlers, counted per UI thread
    // across windows). A handler may route other commands; an exception
    // from a handler propagates to the caller. Command::execute_routed is
    // the usual caller.
    CommandRouteResult route_command(const CommandInvocation& invocation);
    [[nodiscard]] Control::Ptr pressed_control() const noexcept { return pressed_.lock(); }

    // During an active paint lease these return true when the input was
    // retained for posted delivery and false only when the fixed queue bound
    // rejected it. Outside paint the return value remains the routed handled
    // result.
    bool dispatch_pointer(PointerEvent event);
    bool dispatch_key(KeyEvent event);
    bool dispatch_text(TextInputEvent event);
    [[nodiscard]] DeferredInputSnapshot deferred_input_snapshot() const noexcept;
    [[nodiscard]] DragDispatchResult dispatch_drag(DragEvent event);
    void cancel_drag() noexcept;

    [[nodiscard]] MetricsSnapshot metrics_snapshot() const { return metrics_.snapshot(); }
    // The theme engine's records of this window's keyed parts (T2 plan,
    // EnginePartTable), for inspection. The engine alone changes them.
    [[nodiscard]] const EnginePartTable& engine_parts() const noexcept { return engine_parts_; }
    // The window's motion clock (T2 plan, phase E3): it advances the motion
    // values of this window's controls on one frame request. A control's
    // MotionValue finds it here; tests install a time source on it.
    [[nodiscard]] MotionClock& motion_clock() noexcept { return motion_clock_; }
    [[nodiscard]] const MotionClock& motion_clock() const noexcept { return motion_clock_; }
    // Values the application publishes for the theme engine (T2 E6; GIVEN
    // B1-1): named, numeric and read-only to themes. A theme's expressions
    // read the ones its rule sheet names (ThemeRuleSheet::published_values);
    // nothing in a theme writes them. Publishing a changed value, or
    // withdrawing one, damages only the parts whose expressions read it,
    // within their extents. A name that is not published reads as not
    // published: NaN, so the expression reading it gives its fallback.
    //
    // publish_value returns false and changes nothing for a name that is not
    // 1 to 64 characters of lowercase ASCII letters, digits, '-' and '_'
    // starting with a letter, or for a value that is not finite. UI thread
    // only.
    bool publish_value(std::string_view name, double value);
    void withdraw_value(std::string_view name);
    [[nodiscard]] std::optional<double> published_value(std::string_view name) const;
    Metrics& metrics() noexcept { return metrics_; }
    void reset_activity_metrics() noexcept { metrics_.reset_activity(); }
    // Nonfatal layout diagnostics (stage L, phase L5; PLAN section 2.5), in
    // the order the layout passes recorded them: at most
    // maximum_layout_diagnostics, the later ones counted in
    // dropped_layout_diagnostics(). Clearing empties both. The span is
    // valid until the next layout pass or clear.
    [[nodiscard]] std::span<const LayoutDiagnostic> layout_diagnostics() const& noexcept {
        return layout_diagnostics_;
    }
    [[nodiscard]] std::span<const LayoutDiagnostic> layout_diagnostics() const&& = delete;
    [[nodiscard]] std::uint64_t dropped_layout_diagnostics() const noexcept { return dropped_layout_diagnostics_; }
    void clear_layout_diagnostics() noexcept;
    // Containing-block dependencies (PLAN section 3.3), for inspection: how
    // many containers resolved an absolutely positioned child's containing
    // block through `watched` (the block's control, or one between it and
    // the container), the number of controls watched, and how many times a
    // watched control over maximum_containing_block_dependents marked its
    // whole subtree for arrange instead.
    [[nodiscard]] std::size_t containing_block_dependent_count(RuntimeId watched) const noexcept;
    [[nodiscard]] std::size_t containing_block_watch_count() const noexcept {
        return containing_block_watches_.size();
    }
    [[nodiscard]] std::uint64_t containing_block_subtree_arranges() const noexcept {
        return containing_block_subtree_arranges_;
    }
    // Captures the committed retained visual state without exposing a renderer
    // or native host object. Layout is brought to its ordinary read barrier;
    // painting is never forced, so display_chunk_current truthfully reports
    // whether operation/font inspection describes the current control state.
    [[nodiscard]] VisualInspectionSnapshot visual_inspection_snapshot(
        VisualInspectionOptions options = {});
    [[nodiscard]] SemanticSnapshot semantic_snapshot();
    [[nodiscard]] std::uint64_t semantic_generation() const noexcept {
        return semantic_generation_;
    }
    // Focus goes to a focusable control directly. For a control that is not
    // focusable (a composite whose focus belongs to an inner editor or part)
    // the control's own on_semantic_action decides, and the action counts as
    // performed only when focus then lies in the control's subtree.
    bool perform_semantic_action(std::string_view stable_id,
                                 SemanticAction action,
                                 std::string_view value = {});
    // Performs `action` on a semantic node a host published, named by the
    // runtime identity of a control: that control's own node when
    // `virtual_child` is empty, otherwise the virtual child with that stable
    // identity published by the control or by a control inside it (the
    // semantic tree publishes an unexposed control's virtual children at its
    // nearest exposed ancestor, so hosts name that ancestor). Runtime
    // identities are never reused, so a host still holding the node of a
    // disposed or replaced control is refused (false) instead of reaching
    // another control (interview GF017), and a virtual child whose stable
    // identity another control's virtual child repeats is searched for only
    // under the named control. Otherwise the same rules as the overload
    // above: UI thread only, deferred while painting, and refused outside the
    // active focus scope.
    bool perform_semantic_action(RuntimeId control, std::string_view virtual_child,
                                 SemanticAction action, std::string_view value = {});
    // Fills `layout` (caller-owned storage, emptied first and reused) with
    // the text layout of the control with runtime identity `control`: its
    // visual lines, graphemes and word starts, built only now (owner
    // decision A-T1, 12.13). Text is published on a control's own node,
    // never on a virtual child. UI thread only, and not deferred: during
    // paint it answers during_paint and the host asks again. A runtime
    // identity no attached control holds answers stale_identity (interview
    // GF017). Reading changes no text, selection or scroll position.
    [[nodiscard]] SemanticTextResult semantic_text_layout(RuntimeId control, SemanticTextLayout& layout);
    // Moves the text selection of the control with runtime identity
    // `control` (A-T1); focus does not move. Refused as semantic_text_layout
    // refuses, and also for a control outside the active focus scope
    // (outside_focus_scope) or an offset off a grapheme boundary
    // (off_boundary); a refusal changes nothing. A done change reaches hosts
    // through the next snapshot's text_selection field.
    [[nodiscard]] SemanticTextResult set_semantic_text_selection(RuntimeId control,
                                                                 SemanticTextSelection selection);

private:
    friend class HostSession;
    friend class MotionClock;
    // Called by the motion clock: its single frame request, first due at
    // `first_deadline` and then every motion_frame_interval. Each frame
    // calls the clock's advance; the clock disconnects the request when
    // nothing moves. Throws std::length_error at the request limit.
    [[nodiscard]] FrameRequestToken request_motion_frames(FrameTime first_deadline);
    // Called by the motion clock while only ambient effects run (E7): its
    // request's next frame is due at `deadline`, the earliest effect's,
    // rather than one motion_frame_interval after the last.
    void set_motion_frame_deadline(FrameTime deadline) noexcept;
    // Called by HostSession: seeds the window's bindings with the host's
    // defaults unless they are seeded already.
    void install_host_key_map(const KeyMap& defaults);
    void initialize_cursor_identity() noexcept;
    // The destructor's first dispatcher step: stops host wakes and marks the
    // dispatcher closing, so that a post reaching it after shutdown is
    // refused by name instead of throwing (DispatcherState::closing).
    void begin_dispatcher_teardown() noexcept;
    void invalidate_cursor_interaction(CursorError cause) noexcept;
    friend class Control;
    friend class UpdateScope;
    friend class Timer;
    // Reads lifetime_ and frame_request_cancellations_: a timer waiting
    // for its owner's paint holds no request, and stops instead of
    // resuming when this window cancelled its requests meanwhile.
    friend class AnimationTimer;
    friend class detail::ToolTipPresenter;
    // Called by Control::set_tool_tip_text: a tooltip that shows the
    // control's text shows the new text, or closes when it is empty.
    void tool_tip_text_changed(Control& control);
    // Called by RibbonToolTip: while covered, the plain presenter shows no
    // tooltip for `control`'s part `item_id` (empty: the control itself),
    // because the enhanced one describes it. Covers count, so two providers
    // covering one thing both uncover it before it shows again.
    void cover_tool_tip(RuntimeId control, std::string_view item_id, bool covered);
    // dispatch_key's routing, after the paint-lease check.
    bool route_key(KeyEvent event);
    // Called by the tooltip presenter: a one-shot wake at `deadline` on the
    // UI thread, revoked by the token.
    [[nodiscard]] FrameRequestToken request_tool_tip_wake(
        FrameTime deadline, std::function<void(FrameTime)> callback);
    friend class BackgroundWorker;
    friend class ToolTip;
    friend class RibbonToolTip;
    friend class ErrorProvider;
    friend class HelpProvider;
    friend class detail::MessageBoxHelpRoute;
    // Called by HelpProvider: its fallback joins the window's help offers on
    // construction and leaves them on disposal.
    void add_help_fallback(detail::WindowHelpFallback& fallback);
    void remove_help_fallback(const detail::WindowHelpFallback& fallback) noexcept;
    friend class ImageList;
    friend class ApplicationResources;
    // Called by ApplicationResources for every window sharing it.
    void apply_theme(const std::shared_ptr<const Theme>& theme);
    // Called by ApplicationResources before a theme change (P12b): the
    // inline styles of the window's root and popups compiled again against
    // `theme` (Control::collect_inline_restyles).
    [[nodiscard]] bool collect_inline_restyles(const Theme& theme, std::vector<detail::InlineRestyle>& restyles,
                                               std::vector<ThemeCompileDiagnostic>& diagnostics) const;
    // Installs what collect_inline_restyles collected
    // (Control::commit_inline_restyles).
    static void commit_inline_restyles(std::vector<detail::InlineRestyle>& restyles);
    void repaint_for_image_replacement();
    void repaint_for_image_removal();
    friend class BindingSource;
    friend class NotifyIcon;
    friend class OpenFileDialog;
    friend class SaveFileDialog;
    friend class BindingContext;
    friend class detail::PopupAttachment;
    friend class ThemeEngine;
    // Called by ThemeEngine::paint for a part of `control`, which has an
    // inline style, under a theme with rules of its own: the first time,
    // writes W401 to the log and notes it (inline_with_theme_reported).
    void note_inline_with_theme(const Control& control);
    // Called by ThemeEngine::paint for a part painted with a key: makes or
    // updates its record and counts a new record's index fallback. Returns
    // the record's handle, or none when the table has no record for it.
    std::optional<PartRecordHandle> note_engine_part(const Control& control, const PartKey& key,
                                                     AnatomyStateSet states);
    // Called by Control::report_opaque_cover: marks the cover on the
    // display chunk being recorded, when `control` is recording it into
    // `painter` (RecordingPainter::mark_opaque_cover).
    void note_opaque_cover(const Control& control, const Painter& painter, Rect local) noexcept;
    // Called by ThemeEngine::paint: the part's raster from its record's
    // cache (EnginePartTable::raster_for), on the paint in progress.
    [[nodiscard]] PartRasterResult engine_part_raster(PartRecordHandle handle, const PartRasterRequest& request);
    // Called by ThemeEngine::paint for a part without a record: a raster
    // for this paint only, assembled from the table's slice references when
    // its primitive slices (EnginePartTable::raster_without_record).
    [[nodiscard]] PartRasterResult engine_unkeyed_raster(const PartRasterRequest& request);
    // Called by ThemeEngine::paint (E7r): queues the shared keyframe for
    // `request` to bake in an idle frame (EnginePartTable::
    // queue_idle_keyframe), and keeps an idle frame request while any wait.
    void engine_queue_keyframe(const PartRasterRequest& request);
    // Keeps one engine_idle frame request while the engine has idle work:
    // keyframes or procedural patterns queued (E7r, E10).
    void arm_engine_idle();
    // Where a part ThemeEngine::paint draws with `painter` lands (E6): the
    // control whose display chunk records it (the control being recorded,
    // which may paint on behalf of `control`, or else `control` itself), the
    // painter's origin in that control's coordinates, and that origin in
    // window coordinates.
    struct EnginePaintPlace final {
        const Control* painted_by{};
        Point origin{};
        Point window_origin{};
    };
    [[nodiscard]] EnginePaintPlace engine_paint_place(const Control& control, const Painter& painter) const;
    // Called by ThemeEngine::paint for a keyed part: what its paint read
    // and where it was drawn (EnginePartTable::note_paint).
    void note_engine_paint(PartRecordHandle handle, std::uint64_t inputs_read, const Control& painted_by,
                           Rect extent);
    // The window's backplane under a part (E9-B): `image` covers a raster
    // of `extent` device pixels whose top-left corner lies at
    // `raster_origin` (window logical units) at `scale`, grown by `margin`
    // device pixels on every side; `origin` is its top-left pixel in the
    // window's device pixels and `generation` the backplane's damage
    // generation it was made at. Held in the engine table's shared cache
    // per region (EnginePartTable::held_backplane) and made on first need:
    // the window's backplane base, then the backplane plane's commands,
    // replayed whole by the window's renderer (stage T5, C6: text and
    // application drawing included) when it can make a framebuffer, else
    // only their engine part rasters (option 1; everything else skipped
    // and counted). `image` is null when no image can be made.
    struct EngineBackplane final {
        std::shared_ptr<const PartRaster> image{};
        Point origin{};
        std::uint64_t generation{};
    };
    [[nodiscard]] EngineBackplane engine_backplane(Point raster_origin, DeviceExtent extent, DeviceScale scale,
                                                   std::uint32_t margin);
    // Called by ThemeEngine::paint for a keyed part: whether its paint
    // sampled the backplane (EnginePartTable::note_backplane_reader).
    void note_engine_backplane_reader(PartRecordHandle handle, bool reads) noexcept;
    // The backplane's damage generation (E9-B): advanced by every damage
    // to the backplane plane, a theme change and an activation change.
    [[nodiscard]] std::uint64_t backplane_generation() const noexcept { return backplane_generation_; }
    // The pointer's window position for the engine's pointer inputs, or
    // none while it is outside the window.
    [[nodiscard]] const std::optional<Point>& engine_pointer() const noexcept { return engine_pointer_; }
    // Damage by dependency (E6): invalidates the extent of every keyed part
    // reading any of `inputs` (only hot parts when `hot_only`), or reading
    // the published value `name`, on the control that recorded it, and
    // counts it in the metrics.
    void damage_engine_readers(std::uint64_t inputs, bool hot_only);
    void damage_published_readers(std::string_view name);
    // Advances the backplane's damage generation and invalidates the extent
    // of every keyed part that sampled the backplane within `damage`
    // (window logical units), or every such part when `damage` is null.
    void damage_backplane_readers(const Rect* damage);
    // After the engine made a procedural pattern (E10): invalidates the
    // extent of every keyed part whose raster drew a fill's ground in its
    // place, or, for made_unrecorded, every control and the backplane.
    void damage_pattern_waiters(PatternGeneration generation);
    // Composites the backplane-plane display chunks of `control` and its
    // subtree, clipped to `clip` (window logical units), into `image`
    // covering `region`; adds the commands skipped to `skipped`.
    void composite_backplane_subtree(const Control::Ptr& control, Rect clip, const BackplaneRegion& region,
                                     const PartRasterView& image, std::uint64_t& skipped) const;
    // The full-fidelity backplane (stage T5, C6; PLAN.md §4.3): replays
    // the backplane plane's display chunks, clipped to `covered` (window
    // logical units), over `image`, which covers `region` and already
    // holds the base, through a framebuffer of the window's renderer, with
    // the saves, translations and clips the window paint uses. False,
    // leaving `image` as it was, without a renderer that makes a
    // framebuffer of the region's exact extent.
    [[nodiscard]] bool replay_backplane(Rect covered, const BackplaneRegion& region, const PartRasterView& image);
    void replay_backplane_subtree(const Control::Ptr& control, Rect clip, Painter& painter) const;
    struct EngineDamage final {
        std::uint64_t parts{};
        double area{};
    };
    // Invalidates one record's extent on the control that recorded it, if
    // that control is still in this window; returns whether it did.
    bool damage_engine_part(const EnginePartRecord& record, EngineDamage& damage);
    // Follows the pointer for the engine: a move inside the window damages
    // the hot parts reading the pointer.
    void note_engine_pointer(const PointerEvent& event);
    friend class PartMotion;
    // Called by PartMotion (E7): one of a part's transition values stepped.
    // Its extent repaints on the control that recorded it; when the value
    // settled, the record's motion_unsettled follows its other values.
    void engine_part_moved(PartRecordHandle handle, bool settled);
    // Called by ThemeEngine::paint: the record a handle names, to run its
    // transitions; null when the handle is stale.
    [[nodiscard]] EnginePartRecord* engine_part_record(PartRecordHandle handle) noexcept {
        return engine_parts_.record(handle);
    }
    // Called by ThemeEngine::paint: runs a part's ambient effect on the
    // motion clock, its frames at least `interval` apart, or stops it.
    void run_engine_ambient(PartRecordHandle handle, FrameInterval interval);
    void stop_engine_ambient(PartRecordHandle handle) noexcept;
    // Called by MotionClock: one ambient frame for the record in `slot`
    // with `generation`. Returns false, and repaints nothing, when the
    // record is gone or the part was not repainted since the frame that
    // saw paint `seen`, so that the effect ends; otherwise repaints the
    // part's extent and updates `seen`.
    bool engine_ambient_frame(std::uint32_t slot, std::uint32_t generation, std::uint64_t& seen);
    // Called by MotionClock when ambient effects stop: the part repaints, at
    // its static pose.
    void repaint_engine_part(std::uint32_t slot, std::uint32_t generation);
    // Repaints every part whose last paint had an ambient effect, so that
    // its effect resumes: when the window is shown again, reduced motion
    // ends or the motion switch is turned on.
    void resume_engine_ambient();
    friend class detail::AcceleratorAttachment;

    using ControlList = std::vector<Control::Ptr>;
    using StableIdMap = std::unordered_map<std::string, Control::WeakPtr>;
    using PopupList =
        std::vector<std::shared_ptr<detail::PopupAttachment>>;
    using AcceleratorList =
        std::vector<std::shared_ptr<detail::AcceleratorAttachment>>;

    struct FocusChangePublication final {
        Window* window{};
        Control::Ptr control;
        bool focused{};
        void operator()() const;
    };
    struct FocusScopePublication final {
        Window* window{};
        Control::Ptr owner;
        std::shared_ptr<unsigned char> identity;
        FocusScopeChange change;
        void operator()() const;
    };
    struct PointerCapturePublication final {
        Window* window{};
        Control::Ptr owner;
        PointerCaptureChange change;
        void operator()() const;
    };
    struct ControlAvailabilityPublication final {
        Window* window{};
        Control::Ptr control;
        ControlAvailabilityChange change;
        void operator()() const;
    };
    struct DeferredInputVisitor final {
        Window* window{};
        template <typename InputEvent>
        void operator()(InputEvent&& event) const;
    };

    void attach_subtree(const Control::Ptr& control, const Control::WeakPtr& parent);
    void attach_subtree_state(const Control::Ptr& control,
                              const Control::WeakPtr& parent,
                              ControlList& attached);
    void detach_subtree(const Control::Ptr& control);
    void detach_subtree_state(const Control::Ptr& control,
                              ControlList& detached);
    void dispose_subtree(const Control::Ptr& control) noexcept;
    void revoke_interaction_for_subtree(const Control::Ptr& control,
                                        bool notify_focus);
    void close_focus_scopes_for_subtree(const Control::Ptr& control);
    bool end_focus_scope(
        FocusScopeId scope, FocusScopeCloseReason reason,
        const Control::Ptr& notification_owner);
    void revoke_focus_scopes_for_subtree(const Control::Ptr& control) noexcept;
    void close_popups_for_subtree(const Control::Ptr& control) noexcept;
    void close_popup(detail::PopupAttachment& popup) noexcept;
    void close_accelerator(detail::AcceleratorAttachment& accelerator) noexcept;
    [[nodiscard]] bool dispatch_accelerator(const KeyEvent& event,
                                            bool preemptive);
    [[nodiscard]] bool focus_allowed_by_active_scope(
        const Control::Ptr& control) const noexcept;
    [[nodiscard]] std::vector<Control::Ptr> focus_candidates(
        const Control::Ptr& scope_root) const;
    static bool tab_order_less(const Control::Ptr& left,
                               const Control::Ptr& right) noexcept;
    void collect_focus_candidates(const Control::Ptr& control,
                                  ControlList& result,
                                  bool focusable_only) const;
    void collect_mnemonic_candidates(const Control::Ptr& control,
                                     const Control::Ptr& root,
                                     char32_t character,
                                     ControlList& result) const;
    void validate_children_recursive(const Control::Ptr& parent,
                                     const Control::Ptr& container,
                                     ValidationConstraints constraints,
                                     bool& accepted);
    [[nodiscard]] bool validate_focus_transition(
        const Control::Ptr& previous, const Control::Ptr& destination,
        AutoValidate mode);
    void change_pointer_capture(const Control::Ptr& control,
                                std::uint64_t pointer_id,
                                bool revoked);
    // Marks `control` as the pointer's hovered control or not, and repaints
    // it when its anatomy declares a hot state (stage T4), so that a
    // control drawing hot from its anatomy states needs no hover code.
    void set_pointer_over(Control& control, bool over);
    // Forgets the hovered control, if any, without events or repaint: the
    // pointer left the window, or the control left the interaction.
    void forget_hover() noexcept;
    void on_eligibility_changed(const Control::Ptr& control);
    void on_hit_test_transparency_changed(const Control::Ptr& control);
    void publish_control_availability(Control& control);
    void register_subtree(const Control::Ptr& control);
    void collect_subtree_registration(
        const Control::Ptr& control, ControlList& controls,
        std::unordered_set<std::string>& local_ids);
    void unregister_subtree(const Control::Ptr& control);
    void mark_dirty(Control& control, Dirty dirty);
    void mark_paint_dirty(Control& control, Rect local_damage);
    void mark_subtree_dirty(Control& control, Dirty dirty);
    // Marks for measure the controls a client-size change reaches (see
    // Window::resize): `control`, and below it each child whose parent
    // requests no extent on a changed axis.
    void mark_resized_measure(Control& control, bool width_reaches, bool height_reaches);
    void apply_subtree_dirty(Control& control, Dirty dirty,
                             double& requested_damage_area);
    void mark_child_layout_slot(Control& control);
    void change_paint_plane(Control& control, PaintPlane plane);
    void add_damage(Rect damage, PaintPlane plane);
    void add_damage_all_planes(Rect damage);
    void request_paint_wake() noexcept;
    void acknowledge_paint_wake_if_damage_drained() noexcept;
    void touch_paint() noexcept;
    // The paint lease's state, derived from the facts each time (in paint,
    // a change during render, occlusion, what is owed, the revisions), so
    // it can never report a queued paint with nothing to paint.
    [[nodiscard]] PaintLeaseState paint_lease_state() const noexcept;
    // A semantic action that arrived during paint. `owner` is zero for one
    // named by stable identity alone; otherwise it is the publishing
    // control's runtime identity and `stable_id` the virtual child (empty for
    // the control's own node).
    struct DeferredSemanticInput final {
        std::string stable_id;
        SemanticAction action{SemanticAction::focus};
        std::string value;
        RuntimeId owner{};
    };
    // The attached control, in the root tree or a popup, with runtime
    // identity `control`, or empty.
    [[nodiscard]] Control::Ptr find_runtime_control(RuntimeId control) const;
    // Whether a semantic action may reach `control` under the active focus
    // scope: no scope contains focus, `control` lies inside the scope, or it
    // lies in a popup whose owner does (the reach of the stable-identity
    // overload's virtual-child search).
    [[nodiscard]] bool semantic_scope_reaches(const Control::Ptr& control) const;
    using DeferredInput = std::variant<
        PointerEvent, KeyEvent, TextInputEvent, DragEvent,
        DeferredSemanticInput>;
    [[nodiscard]] bool defer_input(DeferredInput input);
    void schedule_deferred_input_drain() noexcept;
    void drain_deferred_input();
    void abandon_deferred_input() noexcept;
    void abandon_deferred_drag() noexcept;
    void add_subtree_damage(const Control::Ptr& control);
    void ensure_layout(bool read_barrier);
    void leave_update_scope();
    void flush_if_outermost();
    [[nodiscard]] std::vector<Control::Ptr> route_to(const Control::Ptr& target) const;
    [[nodiscard]] Control::Ptr drop_target_at(Point position);
    [[nodiscard]] DragDispatchResult route_drag(const Control::Ptr& target,
                                                DragEvent event);
    [[nodiscard]] Control::Ptr hit_test_recursive(const Control::Ptr& control,
                                                  Point window_position) const;
    // Paints the window's backplane into one painted rectangle of the
    // candidate, in a copy group when the window starts clear.
    void paint_backplane_rectangle(detail::RecordingPainter& candidate, Rect rectangle,
                                   const SurfaceMaterial& backplane, bool starts_clear);
    // The last plane in which a control covers the whole of `rectangle`
    // (find_paint_cover, `path` its scratch), or paint_plane_count.
    [[nodiscard]] std::size_t covering_plane(Rect rectangle, bool explicit_request,
                                             std::vector<Control::Ptr>& path) const;
    // Paints plane `index` of one painted rectangle (plane_paint_bounds),
    // from the control that covers it there when one does; `path` is
    // scratch.
    void paint_plane_rectangle(Painter& candidate, std::size_t index, Rect rectangle, bool explicit_request,
                               std::vector<Control::Ptr>& path, std::uint64_t& visited_nodes,
                               std::uint64_t& painted_controls, std::uint64_t& consumed_invalidations,
                               std::uint64_t& chunks_rebuilt, std::uint64_t& chunks_reused,
                               std::uint64_t& commands_replayed);
    // The control painted last in `plane` whose current display chunk
    // leaves every device pixel of `damage` opaque (its opaque cover hint),
    // looking in `control`'s subtree as paint_recursive(control) paints it.
    // When there is one, appends the path to it, `control` first.
    [[nodiscard]] bool find_paint_cover(const Control::Ptr& control, Rect damage, PaintPlane plane,
                                        std::vector<Control::Ptr>& path) const;
    // Paints `damage` in `plane` as paint_recursive(path.front()) would,
    // from path.back() on: what it paints before path.back() lies beneath
    // that control's opaque cover (find_paint_cover).
    void paint_from_cover(std::span<const Control::Ptr> path,
                          Painter& painter,
                          Rect damage,
                          PaintPlane plane,
                          std::uint64_t& visited_nodes,
                          std::uint64_t& painted_controls,
                          std::uint64_t& consumed_invalidations,
                          std::uint64_t& chunks_rebuilt,
                          std::uint64_t& chunks_reused,
                          std::uint64_t& commands_replayed);
    // Recomputes `control`'s subtree paint-dirty summary from its own flag
    // and its children's summaries, after painting it.
    void summarize_subtree_paint(Control& control) const noexcept;
    // `control`'s child viewport in window coordinates.
    [[nodiscard]] Rect absolute_child_viewport_of(const Control& control) const;
    void paint_recursive(const Control::Ptr& control,
                         Painter& painter,
                         Rect window_damage,
                         PaintPlane plane,
                         std::uint64_t& visited_nodes,
                         std::uint64_t& painted_controls,
                         std::uint64_t& consumed_invalidations,
                         std::uint64_t& chunks_rebuilt,
                         std::uint64_t& chunks_reused,
                         std::uint64_t& commands_replayed);
    void measure_dirty_recursive(const Control::Ptr& control,
                                 Size available,
                                 std::uint64_t& visited_nodes,
                                 std::uint64_t& callbacks);
    void arrange_dirty_recursive(const Control::Ptr& control,
                                 Rect final_bounds,
                                 std::uint64_t& visited_nodes,
                                 std::uint64_t& callbacks);
    [[nodiscard]] bool has_runnable_layout_dirty(
        const Control::Ptr& control) const noexcept;
    [[nodiscard]] bool has_any_runnable_layout_dirty() const noexcept;
    [[nodiscard]] bool has_popup_layout_dirty() const noexcept;
    void note_suspended_layout_request(Control& control) noexcept;
    // Containing blocks of absolutely positioned children (stage L, phase
    // L5; PLAN sections 2.5 and 3.3). A container that placed such children
    // registers the controls whose arrangement moves their containing block
    // relative to it; an empty list withdraws it. When a watched control's
    // arranged bounds change in an arrange pass, or its positioning changes
    // (mark_containing_block_dependents), each container watching it is
    // marked for arrange: it is below the watched control, so the same
    // top-down pass arranges it. Entries go when their control leaves the
    // window.
    void set_containing_block_dependencies(Control& dependent, std::span<const RuntimeId> watched);
    void mark_containing_block_dependents(Control& watched);
    void mark_subtree_arrange(Control& control) noexcept;
    // Removes `dependent` from the watch of `watched`; a watch left with no
    // dependent, and not overflowed, goes.
    void withdraw_containing_block_dependent(RuntimeId watched, RuntimeId dependent) noexcept;
    void erase_containing_block_entries(RuntimeId control) noexcept;
    // Records containing_block_not_parent when `child`'s containing block
    // resolved to `ancestor` and that is not its last resolution; a
    // resolution to its parent is remembered and not recorded.
    void note_containing_block(const Control& child, const Control& ancestor);
    void record_layout_diagnostic(LayoutDiagnostic diagnostic);
    // Marks the ancestors of a control whose layout became dirty, up to and
    // including the first layout boundary (Control::size_depends_on_content):
    // measure and arrange below the boundary, arrange only at it.
    void mark_layout_ancestors(Control& control) noexcept;
    void commit_layout_requests_recursive(const Control::Ptr& control) noexcept;
    [[nodiscard]] Dirty recompute_subtree_dirty(const Control::Ptr& control) noexcept;
    void clear_layout_dirty_subtree(const Control::Ptr& control) noexcept;
    void clear_paint_dirty_subtree(const Control::Ptr& control) noexcept;
    [[nodiscard]] std::uint64_t display_cache_entries(
        const Control::Ptr& control) const noexcept;
    void update_display_cache_metrics() noexcept;
    void compact_frame_requests() noexcept;
    [[nodiscard]] Rect absolute_bounds_of(const Control& control) const;
    [[nodiscard]] Rect visual_bounds_of(const Control& control,
                                        Insets outsets) const;
    [[nodiscard]] Rect paint_damage_bounds_of(const Control& control) const;
    [[nodiscard]] bool eligible(const Control::Ptr& control) const noexcept;
    [[nodiscard]] bool move_focus_after(const Control::Ptr& origin);
    void set_focus_cue_visible(bool visible);
    [[nodiscard]] bool validate_command_activation(
        const Control::Ptr& destination);
    [[nodiscard]] bool dispatch_mnemonic(char32_t character);
    [[nodiscard]] bool dispatch_dialog_button(bool accept);
    [[nodiscard]] bool dispatch_dialog_key(const KeyEvent& event);
    void merge_drag_route(DragDispatchResult& aggregate,
                          const Control::Ptr& target, DragEvent event);
    void leave_drag_target(const DragEvent& event,
                           DragDispatchResult& aggregate);
    void clear_dialog_targets_for_subtree(const Control::Ptr& control) noexcept;
    void require_ui_thread(std::string_view operation);

    Control::Ptr root_;
    Size client_size_{};
    std::uint64_t cursor_window_id_{};
    std::uint64_t cursor_metrics_generation_{1};
    double scale_{1.0};
    PresentationSettings presentation_settings_{};
    TextMetricsProvider* text_metrics_provider_{};
    Painter* framebuffer_painter_{};
    // The backplane replay's framebuffer (C6), made from the framebuffer
    // painter and reused while the region's extent and scale stay the
    // same; released when the painter changes.
    std::unique_ptr<PaintFramebuffer> backplane_framebuffer_{};
    double backplane_framebuffer_scale_{};
    Event<const PresentationSettings&> presentation_changed_;
    std::shared_ptr<const Theme> theme_;
    Event<const Theme&> theme_changed_;
    Event<bool> active_changed_;
    bool active_{true};
    StableIdMap stable_ids_;
    Control::WeakPtr focused_;
    // One F6 region and the control that last had focus inside it. Both are
    // observed, never owned.
    struct FocusRegion final {
        Control::WeakPtr region;
        Control::WeakPtr remembered;
    };
    std::vector<FocusRegion> focus_regions_;
    void remember_region_focus(const Control::Ptr& control);
    [[nodiscard]] bool focus_into_region(FocusRegion& region);
    bool focus_cue_visible_{};
    Control::WeakPtr accept_button_;
    Control::WeakPtr cancel_button_;
    std::uint64_t mnemonic_attempts_{};
    std::uint64_t mnemonics_handled_{};
    std::uint64_t accept_attempts_{};
    std::uint64_t accept_handled_{};
    std::uint64_t cancel_attempts_{};
    std::uint64_t cancel_handled_{};
    std::uint64_t dialog_command_rejections_{};
    std::uint64_t mnemonic_candidates_{};
    std::uint64_t mnemonic_collisions_{};
    std::uint64_t mnemonic_cycles_{};
    Control::WeakPtr mnemonic_cursor_;
    char32_t mnemonic_cursor_character_{};
    DialogResult dialog_result_{DialogResult::none};
    Event<DialogResult> dialog_result_changed_;
    std::uint64_t validation_attempts_{};
    std::uint64_t validation_succeeded_{};
    std::uint64_t validation_cancelled_{};
    std::uint64_t validation_focus_moves_blocked_{};
    std::uint64_t validation_reentrant_requests_rejected_{};
    std::uint64_t validation_bulk_controls_visited_{};
    bool validating_{};
    struct FocusScopeState final {
        FocusScopeId id{};
        Control::WeakPtr root;
        Control::WeakPtr previous_focus;
        std::string stable_id;
        RuntimeId root_id{};
        FocusScopeOptions options{};
        bool active{true};
    };
    using FocusScopeList = std::vector<FocusScopeState>;
    FocusScopeList focus_scopes_;
    Event<const FocusScopeChange&> focus_scope_changed_;
    std::uint64_t next_focus_scope_id_{1U};
    Control::WeakPtr captured_;
    std::uint64_t captured_pointer_id_{};
    Event<const PointerCaptureChange&> pointer_capture_changed_;
    Event<const FocusedControlChange&> focused_control_changed_;
    Event<HelpRequestEvent&> help_requested_;
    // The window's HelpProviders' fallbacks, in creation order; each
    // provider removes its own on disposal.
    std::vector<detail::WindowHelpFallback*> help_fallbacks_;
    Event<const ControlAvailabilityChange&> control_availability_changed_;
    PopupList popups_;
    AcceleratorList accelerators_;
    // The window's one tooltip presenter; made with the window, shut down
    // first in its destructor (it holds a popup).
    std::unique_ptr<detail::ToolTipPresenter> tool_tip_presenter_;
    ToolTipSettings tool_tip_settings_{};
    std::optional<bool> tool_tips_shown_{};
    Event<const ToolTipEvent&> tool_tip_visibility_changed_;
    // The active panel's, active document's and main window's command
    // targets, in that order; observed, never owned.
    std::array<std::weak_ptr<CommandTarget>, 3> command_targets_{};
    Control::WeakPtr pressed_;
    Control::WeakPtr hovered_;
    Control::WeakPtr drag_target_;
    std::uint64_t drag_session_id_{};
    DragEffect drag_last_accepted_effect_{DragEffect::none};
    std::array<DamageRegion, paint_plane_count> plane_damage_;
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
    struct PaintControlCheckpoint final {
        Control::Ptr control;
        std::shared_ptr<const detail::DisplayChunk> chunk;
    };
    using PaintControlCheckpointList =
        std::vector<PaintControlCheckpoint>;
    void collect_paint_checkpoints(
        const Control::Ptr& control, PaintControlCheckpointList& checkpoints);
    // Restores the paint pass's checkpoints, re-damages its rectangles and
    // releases the checkpoints (paint_checkpoints_ is left empty).
    void abandon_paint(std::uint64_t generation_before);
    // One paint pass over `rectangles` (window coordinates, disjoint after
    // merging). With `explicit_request`, every plane paints each rectangle;
    // otherwise each plane paints only where it holds damage.
    std::optional<PaintReceipt> paint_rectangles(
        Painter& painter, std::span<const Rect> rectangles,
        bool explicit_request);
    [[nodiscard]] Rect plane_paint_bounds(std::size_t index, Rect rectangle,
                                          bool explicit_request) const;
    [[nodiscard]] bool has_plane_damage() const noexcept;
    // Whether a paint is owed: a change during the last render, or damage on
    // any plane. Damage is clipped to the client area and recorded for every
    // visible change, so it is what a paint can discharge. A control's own
    // paint-dirty mark is not owed: it only rebuilds the control's chunk when
    // a paint next reaches it, and a control outside the client area or a
    // viewport is never reached. Derived each time, so no overload of
    // take_damage, paint or a dirty mark can leave it stale.
    [[nodiscard]] bool paint_pending() const noexcept;
    void collect_visible_overlay_rectangles(
        const Control::Ptr& control, std::vector<Rect>& rectangles) const;
    [[nodiscard]] static std::vector<Rect> subtract_rectangle(
        Rect source, Rect cover);
    LiveSurfaceRegistrationMap live_surface_registrations_;
    std::shared_ptr<detail::LiveSurfaceIdleWake> live_surface_idle_wake_{};
    Metrics metrics_;
    // The theme engine's records of this window's keyed parts. A control's
    // records are released when it leaves the window.
    EnginePartTable engine_parts_;
    // After metrics_, which it reports into, so that it is destroyed first.
    MotionClock motion_clock_{*this};
    // The values the application publishes for the engine, in publication
    // order (E6).
    struct PublishedValue final {
        std::string name;
        double value{};
    };
    std::vector<PublishedValue> published_values_;
    std::optional<Point> engine_pointer_{};
    // The control whose display chunk is being recorded and its recorder,
    // while one is (E6); null otherwise. Observing: both live on the stack
    // of the paint that records.
    const Control* recording_control_{};
    detail::RecordingPainter* recording_painter_{};
    // Shared with every window constructed with the same resources; this
    // window is registered with them from construction to destruction.
    std::shared_ptr<ApplicationResources> resources_;
    std::uint64_t display_generation_{};
    // The backplane's damage generation (E9-B); starts at 1.
    std::uint64_t backplane_generation_{1U};
    using FrameRequestList =
        std::vector<std::shared_ptr<detail::ScheduledFrameRequest>>;
    FrameRequestList frame_requests_;
    // Counts cancel_frame_requests calls, so that work which holds no
    // request while it waits (AnimationTimer) can tell it was cancelled.
    std::uint64_t frame_request_cancellations_{};
    bool in_frame_poll_{};
    std::shared_ptr<detail::WindowLifetime> lifetime_;
    std::thread::id ui_thread_;
    std::shared_ptr<detail::DispatcherState> dispatcher_state_;
    std::uint64_t update_depth_{};
    bool layout_dirty_{true};
    bool hit_test_dirty_{true};
    bool in_layout_{};
    // Advances at the start of each bounded layout pass and once after the
    // last, so Control::measure_for_layout counts one control's measure
    // callbacks per pass, and calls between passes apart from either.
    std::uint64_t layout_pass_sequence_{1U};
    // Layout diagnostics and containing-block dependencies (stage L, phase
    // L5). A watch's dependents are the containers that registered it, by
    // identity with an observing reference (find_runtime_control walks the
    // tree); a container's path is the watches it registered, for removing
    // it. An overflowed watch keeps no further dependents and marks its
    // subtree for arrange instead, until it leaves the window.
    struct ContainingBlockDependent final {
        RuntimeId id{};
        Control::WeakPtr control{};
    };
    struct ContainingBlockWatch final {
        std::vector<ContainingBlockDependent> dependents{};
        bool overflowed{};
    };
    std::unordered_map<std::uint64_t, ContainingBlockWatch> containing_block_watches_{};
    std::unordered_map<std::uint64_t, std::vector<RuntimeId>> containing_block_paths_{};
    // Each absolutely positioned child's last containing block resolution.
    std::unordered_map<std::uint64_t, RuntimeId> containing_block_resolutions_{};
    std::uint64_t containing_block_subtree_arranges_{};
    std::vector<LayoutDiagnostic> layout_diagnostics_{};
    std::uint64_t dropped_layout_diagnostics_{};
    bool in_paint_{};
    // Paint-pass scratch, reused by every pass so that a frame allocates
    // nothing once they have grown to the window's size (paint_rectangles
    // is not reentrant: in_paint_ guards both). paint_checkpoints_ holds
    // each control and its chunk only during a pass and is cleared, keeping
    // its capacity, when the pass completes or is abandoned;
    // painted_rectangles_ is the pass's merged window-space rectangles.
    PaintControlCheckpointList paint_checkpoints_{};
    std::vector<Rect> painted_rectangles_{};
    bool inline_with_theme_reported_{};
    std::uint64_t content_revision_{1U};
    std::uint64_t rendered_revision_{};
    std::uint64_t presented_revision_{};
    std::uint64_t surface_epoch_{1U};
    std::uint64_t paint_leases_started_{};
    std::uint64_t paint_leases_completed_{};
    std::uint64_t paint_leases_abandoned_{};
    std::uint64_t reentrant_paint_requests_deferred_{};
    std::uint64_t paint_wakes_queued_{};
    std::uint64_t paint_wakes_coalesced_{};
    std::uint64_t presentation_receipts_accepted_{};
    std::uint64_t presentation_receipts_rejected_{};
    PresentationDeclaration presentation_declaration_{};
    bool dirty_after_render_{};
    std::deque<DeferredInput> deferred_inputs_;
    std::uint64_t deferred_inputs_received_{};
    std::uint64_t deferred_inputs_delivered_{};
    std::uint64_t deferred_input_moves_coalesced_{};
    std::uint64_t deferred_drag_overs_coalesced_{};
    std::uint64_t deferred_inputs_rejected_{};
    std::uint64_t deferred_inputs_abandoned_{};
    std::uint64_t deferred_input_faults_{};
    std::uint64_t deferred_input_drains_{};
    DispatchOperation deferred_input_drain_operation_;
    bool deferred_input_drain_queued_{};
    bool draining_deferred_input_{};
    bool in_lifecycle_notification_{};
    bool second_layout_pass_requested_{};
    bool occluded_{};
    std::function<void()> paint_wake_handler_;
    bool paint_wake_pending_{};
    std::uint64_t semantic_generation_{1U};
    bool in_semantic_snapshot_{};
    HostServices* host_services_{};
    // Compositor cooperation (stage T5). `host_session_bound_` is set by the
    // first HostSession and never cleared; it ends declare_non_opaque.
    bool host_session_bound_{};
    bool non_opaque_declared_{};
    CompositorState compositor_state_{};
    WindowBackdropRequest backdrop_request_{};
    // Whether backdrop_request_ is the application's (request_backdrop)
    // rather than the theme's.
    bool application_backdrop_request_{};
    WindowBackdropGrant backdrop_grant_{};
    Event<const WindowBackdropGrant&> backdrop_changed_;
    // Resolves the grant from the request, the compositor state, the
    // declaration and the presentation settings, applies a changed backdrop
    // through the host services, and raises backdrop_changed when it changed.
    void refresh_backdrop();
    // The material the window's backplane is drawn from (PLAN.md §4.1), by
    // the presentation and the engine's backplane image alike: the theme's
    // base for the granted backdrop when it has one, else the window
    // recipe's material for `context`. Borrowed from theme_, valid until
    // the theme is replaced.
    [[nodiscard]] const SurfaceMaterial& backplane_base(const ControlVisualContext& context) const noexcept;
    // The keyboard commands' keys, shared with the application's other
    // windows (see key_map); never null.
    std::shared_ptr<KeyBindings> key_bindings_{std::make_shared<KeyBindings>()};
};

} // namespace gui_forms
