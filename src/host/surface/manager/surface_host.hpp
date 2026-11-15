#pragma once
// What a single-surface host supplies to the window manager (owner
// decision, 2026-11-15): one surface, its input, and a present call. The
// manager (surface_manager_internal.hpp) does the rest: every window of the
// run is a layer it composes into the surface, with frames, title bars,
// stacking, focus, modal blocking, popups and, where the host has none, a
// drawn cursor.
//
// Hosts: the browser (src/host/web/, a WebGL2 texture in a canvas), the
// planned FreeBSD framebuffer appliance (STXlib's display and input
// surface), and any later host with one surface. A test host
// (tests/surface_window_manager_tests.cpp) keeps the surface in memory.
//
// Threads: every call is made on the manager's thread (the run's UI
// thread), except that the host's input queue is posted to from any thread.
// The host outlives the run, and its queue outlives every model's wake
// handlers (the manager clears them when a window closes).

#include "../compose/surface_compose.hpp"
#include "../input/surface_input.hpp"
#include "../../../core/resources/fonts/bundled_font_set.hpp"
#include "gui_forms/host.hpp"
#include "gui_forms/platform/surface_host.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

namespace gui_forms::host::surface_detail {

// The host's surface: `height` rows of `width` premultiplied BGRA8 pixels,
// `row_bytes` apart, owned by the host. The pixels stay valid, and keep
// what the manager wrote, until the next acquire_surface with another size.
struct SurfaceView final {
    std::uint8_t* pixels{};
    std::size_t row_bytes{};
    std::uint32_t width{};
    std::uint32_t height{};
};

enum class SurfacePresentResult : std::uint8_t {
    // The damaged pixels are on screen (or copied out); the surface may be
    // written again at once.
    presented,
    // The host reads the surface later (a browser presents on its next
    // animation frame): the manager writes no pixel of it until a
    // SurfaceInputKind::presented record arrives.
    pending,
    // Another surface holds the screen (a virtual terminal switch): nothing
    // was shown, and the manager keeps its damage until an activation record
    // composes everything again.
    inactive,
    // The present failed; the damage is kept and tried again.
    failed,
};

// The surface's logical size (the desktop's units) and device scale when
// the run begins.
struct SurfaceHostStart final {
    Size extent{1.0, 1.0};
    double scale{1.0};
};

class SurfaceHost {
public:
    SurfaceHost() = default;
    SurfaceHost(const SurfaceHost&) = delete;
    SurfaceHost& operator=(const SurfaceHost&) = delete;
    virtual ~SurfaceHost();

    // What the host offers, reported to every window and the windowing.
    [[nodiscard]] virtual HostCapabilities capabilities() const = 0;
    // Whether the host shows a pointer cursor of its own (set_cursor); when
    // it does not, the manager draws one into the surface.
    [[nodiscard]] virtual bool draws_cursor() const noexcept = 0;
    // The monitor's identity in HostMonitor::id.
    [[nodiscard]] virtual std::string_view monitor_id() const noexcept = 0;
    // The interface fonts every raster reads, and the font set's page
    // capability on this platform.
    [[nodiscard]] virtual std::filesystem::path fonts_directory() const = 0;
    [[nodiscard]] virtual gui_forms::detail::SealedPageFunctions sealed_pages() const noexcept = 0;
    // Binds the host's surface and input at the run's start.
    virtual SurfaceHostStart start() = 0;
    // The queue the host posts its input to.
    [[nodiscard]] virtual SurfaceInputQueue& input() noexcept = 0;

    // A surface of `width` by `height` device pixels (both at least 1).
    // Returns null pixels when the host cannot supply it; the manager then
    // composes nothing and tries again on the next turn.
    virtual SurfaceView acquire_surface(std::uint32_t width, std::uint32_t height) = 0;
    // Shows the damaged rectangles of the surface last acquired (at least
    // one, within it).
    virtual SurfacePresentResult present(std::span<const SurfaceDeviceRect> damage) = 0;
    // Whether the host should post SurfaceInputKind::frame on each display
    // frame (a live surface animates).
    virtual void want_frames(bool wanted) = 0;

    // The primary window's title (a document title, a terminal's name).
    virtual void set_title(std::string_view title) = 0;
    // The host's own cursor; called only when draws_cursor().
    virtual void set_cursor(CursorKind cursor) = 0;
    // Keeps the pointer's events coming while a press lasts outside the
    // surface (a browser's pointer capture); a host without one ignores it.
    virtual void pointer_capture(bool captured) = 0;
    // Writes the host's clipboard; the manager keeps its own copy too.
    virtual void write_clipboard(std::string_view text) = 0;
    // Where text input is composed (surface logical units) and whether a
    // text control has the keyboard there (a browser places its hidden
    // input element, a framebuffer host its candidate window).
    virtual void place_text_input(Rect caret, bool editing) = 0;
    // The primary window's full-screen toggle; a host that is always full
    // screen ignores it.
    virtual void toggle_full_screen() = 0;
    // Starts the host's own dialog for `request` (an open, save or colour
    // chooser), answered later by a SurfaceInputKind::dialog record naming
    // `ticket`. A refusal (unsupported, a failure) is returned at once and
    // no record follows. Message boxes are drawn by the manager and never
    // reach here.
    virtual HostServiceStatus start_dialog(const HostDialogRequest& request, std::uint64_t ticket) = 0;
};

// Runs the windows on `host` until the primary window closes, on the
// calling thread: 0 after an ordinary close, 1 when the run failed, 2 when a
// window could not be opened.
int run_surface_application(SurfaceHost& host, std::vector<SurfaceApplicationWindow> windows,
                            SurfaceApplicationOptions application);

} // namespace gui_forms::host::surface_detail
