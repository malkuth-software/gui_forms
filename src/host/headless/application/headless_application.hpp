#pragma once

#include "../session/headless_host.hpp"
#include "gui_forms/application.hpp"
#include "gui_forms/host/windowing/host_windowing.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms::detail {
struct ApplicationRunState;
struct ApplicationWindowState;
}

namespace gui_forms::host {

class HeadlessApplication;

// A fault the headless host injects into its next runtime open, to test
// what callers do when a native creation fails after the host has taken the
// entry (ApplicationWindows::open_window consumed it).
enum class HeadlessOpenFault : std::uint8_t {
    none,
    // Refuse with backend_failure and destroy the entry's model, as a
    // native host whose window creation failed releases it.
    refuse_and_destroy,
    // Refuse with backend_failure but keep the model alive in
    // refused_models(), so a test can see whether anything touched it.
    refuse_and_keep,
};

// The headless reference adapter's windowing: a simulated desktop with
// monitors, native window rectangles, visibility, a z-order and a pointer.
// Every native operation appends one line to trace(), so tests can read the
// order in which the portable seam asked for things.
class HeadlessWindowing final : public HostWindowing {
public:
    // Decorations the simulated desktop draws around every window, in
    // logical units: a title bar above the client area and a border on
    // every side.
    static constexpr double title_bar_height = 24.0;
    static constexpr double border_width = 1.0;

    HeadlessWindowing(HeadlessApplication& application, HostCapabilities capabilities);
    // The headless host opens native popups (stage T5, phase (a)).
    [[nodiscard]] bool supports_popup_windows() const noexcept override;
    // True by default; tests turn it off to see the overlay fallback.
    [[nodiscard]] bool supports_splash_windows() const noexcept override;
    void set_splash_windows(bool supported) noexcept { splash_windows_ = supported; }
    // Popup windows, true by default; tests turn them off to see menus fall
    // back to overlays.
    void set_popup_windows(bool supported) noexcept { popup_windows_ = supported; }
    // Translucent transient windows, true by default; tests turn it off to
    // see the opaque path of an X11 run without a compositing manager.
    [[nodiscard]] bool transient_windows_translucent() const noexcept override;
    void set_transient_windows_translucent(bool translucent) noexcept { translucent_ = translucent; }
    // run_modal's nested loop, which a test drives: each turn calls the
    // turn target with the modal window, as a native loop dispatches the
    // next events, until the window is no longer live or the target returns
    // false (the run quits); at most maximum_modal_turns turns. Without a
    // target run_modal is unsupported. Meanwhile every other window that
    // the modal one does not own is input_blocked, and
    // HeadlessApplication::simulate_user_close ignores it.
    static constexpr std::size_t maximum_modal_turns = 1024U;
    [[nodiscard]] bool supports_modal_windows() const noexcept override;
    void set_modal_turn(std::function<bool(HostWindowId)> turn) { modal_turn_ = std::move(turn); }
    [[nodiscard]] bool input_blocked(HostWindowId window) const noexcept;

    // Replaces the simulated monitors (desktop logical units). An empty
    // list is ignored.
    void set_monitors(std::vector<HostMonitor> monitors);
    void set_pointer(DesktopLogicalPoint position) noexcept { pointer_ = position; }
    // A window of another application covering `outer`, above every window
    // of this run. window_at reports no window where it lies.
    void add_foreign_window(Rect outer);
    void clear_foreign_windows() noexcept { foreign_windows_.clear(); }
    // Platform behaviours the policy must survive. Native follow: moving the
    // primary window first moves its floating windows by the same distance
    // and reports them, before the primary window's own report (macOS child
    // windows). Deferred reports: a frame request changes the native window
    // at once but its report waits for flush_frame_reports (X11).
    void set_native_follow(bool enabled) noexcept { native_follow_ = enabled; }
    void set_deferred_frame_reports(bool enabled) noexcept { deferred_reports_ = enabled; }
    void flush_frame_reports();

    [[nodiscard]] const std::string& trace() const noexcept { return trace_; }
    void clear_trace() noexcept { trace_.clear(); }
    // Whether showing, hiding, framing and raising a window append to the
    // trace (on by default). An allocation count of those operations turns
    // it off: each line is formatted on the heap.
    void set_tracing(bool tracing) noexcept { tracing_ = tracing; }
    void note(std::string_view line);
    // Native state, for assertions. Empty for an unknown window.
    [[nodiscard]] std::optional<Rect> native_content(HostWindowId window) const;
    [[nodiscard]] bool native_visible(HostWindowId window) const;
    // Live native windows from the front to the back.
    [[nodiscard]] std::vector<HostWindowId> z_order() const;

    // Native bookkeeping used by HeadlessApplication.
    // `decorated` false for a popup: no title bar or border, so its outer
    // frame is its client area.
    void create_native(HostWindowId window, std::string_view stable_id, Rect content,
                       Size minimum_size, bool decorated);
    void remove_native(HostWindowId window) noexcept;
    void set_native_visible(HostWindowId window, bool visible);
    // Moves or resizes the native window as the user would and reports it.
    void move_native(HostWindowId window, Rect content);
    [[nodiscard]] HostWindowFrame native_frame(HostWindowId window) const;

protected:
    [[nodiscard]] HostWindowFrameResult frame_impl(HostWindowId window) override;
    [[nodiscard]] HostServiceStatus request_frame_impl(HostWindowId window,
                                                       Rect content) override;
    [[nodiscard]] HostScreenPointResult pointer_position_impl() override;
    [[nodiscard]] HostWindowAtResult window_at_impl(DesktopLogicalPoint point) override;
    [[nodiscard]] HostServiceStatus bring_to_front_impl(HostWindowId window) override;
    [[nodiscard]] HostServiceStatus destroy_window_impl(HostWindowId window) override;
    [[nodiscard]] HostMonitorResult query_monitors_impl() override;
    void query_monitors_into_impl(HostMonitorResult& result) override;
    [[nodiscard]] HostServiceStatus show_impl(HostWindowId window) override;
    [[nodiscard]] HostServiceStatus hide_impl(HostWindowId window) override;
    [[nodiscard]] HostServiceStatus run_modal_impl(HostWindowId window) override;

private:
    struct NativeWindow final {
        HostWindowId id{};
        std::string stable_id{};
        Rect content{};
        Size minimum_size{};
        bool visible{};
        bool decorated{true};
    };
    [[nodiscard]] NativeWindow* native(HostWindowId window) noexcept;
    [[nodiscard]] const NativeWindow* native(HostWindowId window) const noexcept;
    void raise_native(HostWindowId window);

    struct DeferredReport final {
        HostWindowId window{};
        Rect old_content{};
    };

    // The owner; outlives this object (it is a member of the owner).
    HeadlessApplication* application_{};
    bool native_follow_{};
    bool deferred_reports_{};
    std::vector<DeferredReport> deferred_;
    std::vector<HostMonitor> monitors_;
    DesktopLogicalPoint pointer_{};
    std::vector<Rect> foreign_windows_;
    // Live native windows; `order_` holds their identities front to back.
    std::vector<NativeWindow> natives_;
    std::vector<HostWindowId> order_;
    std::string trace_;
    bool tracing_{true};
    bool splash_windows_{true};
    bool popup_windows_{true};
    bool translucent_{true};
    std::function<bool(HostWindowId)> modal_turn_{};
    // Windows a run_modal call under way blocked, each listed once per
    // call that blocked it.
    std::vector<HostWindowId> blocked_;
};

// A deterministic multi-window application for tests: Application::run's
// contract without a native loop. The constructor validates the entries as
// Application::validate does, opens every window in ownership order and
// calls each ready callback; the test then drives windows through host()
// and the simulate_* operations. Programs reach the windowing through
// ApplicationWindowHandle::windows(), exactly as on a native host.
class HeadlessApplication final {
public:
    // Throws std::invalid_argument when Application::validate refuses the
    // entries (a test fixture error).
    explicit HeadlessApplication(std::vector<ApplicationWindow> windows,
                                 HostCapabilities capabilities = headless_capabilities());
    // Closes every open window (closed callbacks run) and ends the run.
    ~HeadlessApplication();
    HeadlessApplication(const HeadlessApplication&) = delete;
    HeadlessApplication& operator=(const HeadlessApplication&) = delete;

    [[nodiscard]] HeadlessWindowing& windowing() noexcept { return windowing_; }
    [[nodiscard]] ApplicationWindows windows() const;
    // The handle of a live window, or an inactive handle.
    [[nodiscard]] ApplicationWindowHandle handle(std::string_view stable_id) const;
    // The protocol adapter and model of a live window, or null.
    [[nodiscard]] HeadlessHost* host(HostWindowId window) noexcept;
    [[nodiscard]] Window* model(HostWindowId window) noexcept;
    [[nodiscard]] HostWindowId id(std::string_view stable_id) const;

    // User actions.
    void simulate_user_frame(HostWindowId window, Rect content);
    // The close button: the closing handler decides; an accepted close hides
    // a hide_on_close window and closes any other. Closing the primary
    // window ends the run's windows, as on a native host.
    void simulate_user_close(HostWindowId window);
    // The user's session ends (log out, restart, shut down), as Windows'
    // WM_QUERYENDSESSION then WM_ENDSESSION: every open window's closing
    // handler is asked with HostCloseReason::session_end, newest first.
    // When one cancels, nothing closes and the result is false (the session
    // end is called off). Otherwise every window closes, newest first, with
    // the closed event's reason session_end, and the result is true. A
    // hide_on_close window closes rather than hides.
    bool simulate_session_end();
    // The application gains or loses activation (the user switched to or
    // from another application).
    void simulate_application_active(bool active);
    // The user minimizes or restores a window.
    void simulate_minimized(HostWindowId window, bool minimized);
    // The platform's compositor or the user's settings change (stage T5):
    // `state` replaces the simulated state with the next generation (its
    // own generation is ignored), and every open window receives it as a
    // HostCompositorEvent, in opening order. A window opened later receives
    // the current state right after its attach. No state is reported until
    // the first call.
    void simulate_compositor_state(CompositorState state);
    [[nodiscard]] const CompositorState& compositor_state() const noexcept { return compositor_state_; }
    // Applies `fault` to the next runtime open only, after admission and
    // after the entry is consumed.
    void inject_open_fault(HeadlessOpenFault fault) noexcept { open_fault_ = fault; }
    // Models of runtime opens refused by refuse_and_keep, in refusal order;
    // owned here until the application ends.
    [[nodiscard]] const std::vector<std::unique_ptr<Window>>& refused_models() const noexcept {
        return refused_models_;
    }
    // Releases the models and protocol adapters of windows closed earlier.
    // A test calls it between steps, as a native host does at the end of a
    // loop turn, so a window is never released inside one of its own
    // callbacks; the destructor also runs it.
    void reclaim();

private:
    friend class HeadlessWindowing;
    friend struct HeadlessApplicationAccess;
    // The run's open operation (ApplicationWindows::open_window).
    struct HeadlessOpen final {
        HeadlessApplication* application{};
        ApplicationWindowOpenResult operator()(ApplicationWindow&& entry) const;
    };
    struct WindowEntry final {
        HostWindowId id{};
        std::string stable_id{};
        // Declared before `host`, which borrows it, so the host is destroyed
        // first.
        std::unique_ptr<Window> model{};
        std::unique_ptr<HeadlessHost> host{};
        ApplicationWindowOptions options{};
        std::shared_ptr<detail::ApplicationWindowState> state{};
        bool closed{};
    };

    [[nodiscard]] ApplicationWindowOpenResult open(ApplicationWindow&& entry, bool runtime);
    [[nodiscard]] HostServiceStatus destroy(HostWindowId window);
    void close(WindowEntry& entry, HostCloseReason reason);
    void request_close(HostWindowId window);
    void show(HostWindowId window, bool visible);
    [[nodiscard]] HostServiceStatus resize(HostWindowId window, Size client_size);
    void native_frame_changed(HostWindowId window, Rect old_content, Rect content);
    [[nodiscard]] WindowEntry* entry(HostWindowId window) noexcept;
    [[nodiscard]] Rect default_placement(HostWindowKind kind, HostWindowId owner,
                                       Size size) const;
    [[nodiscard]] std::uint64_t next_timestamp() noexcept { return ++timestamp_; }

    std::shared_ptr<detail::ApplicationRunState> run_;
    HeadlessWindowing windowing_;
    // Open and not yet reclaimed windows, in opening order.
    std::vector<std::unique_ptr<WindowEntry>> entries_;
    std::uint64_t timestamp_{};
    std::uint32_t placements_{};
    HeadlessOpenFault open_fault_{HeadlessOpenFault::none};
    std::vector<std::unique_ptr<Window>> refused_models_;
    // The simulated compositor; generation 0 until the first report.
    CompositorState compositor_state_{};
};

} // namespace gui_forms::host
