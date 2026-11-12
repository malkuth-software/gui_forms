#pragma once

// Capturing without a host window (owner request 2026-11-11, the theme
// palette): a window or a control tree laid out and painted through the
// CPU raster every host paints with, at a chosen scale, colour scheme and
// contrast, into pixels the caller owns (WindowCapture: premultiplied
// BGRA8, encode_window_capture_png for a PNG). Nothing is shown, activated
// or focused on screen, so a palette or a test runs in the background.
//
//     OffscreenRasterCreateResult made = OffscreenRaster::create();
//     std::unique_ptr<OffscreenRaster> raster = made.take_raster();   // null: made.error()
//     Window window(root, Size{320.0, 200.0}, resources);
//     OffscreenAttachment attached(*raster, window);                 // after both
//     OffscreenCaptureOptions options{};
//     options.scale = 2.0;
//     options.colour_scheme = ColourScheme::dark;
//     std::optional<WindowCapture> picture = attached.capture_control(button, options);
//
// The renderer is private to the GUI.Forms library: no renderer type
// appears here. Link GUIForms::Application, which holds it; a program
// linking only the core libraries has no definition of these members.

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/window/window.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <variant>

namespace gui_forms {

namespace detail {
struct OffscreenRasterAccess;
} // namespace detail

enum class OffscreenRasterError : std::uint8_t {
    none,
    // No directory holds the bundled faces: the options name none, and the
    // places the hosts look (GUI_FORMS_FONT_DIR, beside the program, beside
    // the GUI.Forms library) hold none, or the program has no host to ask
    // (it links the core libraries alone). bundled_font_refusal names the
    // directories tried.
    fonts_not_found,
    // A directory was found but the raster refused its faces (a face
    // missing, short or altered).
    fonts_refused,
    // The raster could not make its first surface.
    raster_refused,
};

struct GUI_FORMS_API OffscreenRasterOptions final {
    // The bundled font directory (assets/fonts, installed as
    // share/GUIForms/fonts); empty looks where the hosts look.
    std::filesystem::path font_directory{};
};

class OffscreenRaster;

// OffscreenRaster::create's outcome: the raster, or why there is none.
// Made only through `created` and `refused`.
class GUI_FORMS_API OffscreenRasterCreateResult final {
public:
    [[nodiscard]] static OffscreenRasterCreateResult created(std::unique_ptr<OffscreenRaster> raster);
    [[nodiscard]] static OffscreenRasterCreateResult refused(OffscreenRasterError error, std::string detail);
    OffscreenRasterCreateResult(OffscreenRasterCreateResult&&) noexcept;
    OffscreenRasterCreateResult& operator=(OffscreenRasterCreateResult&&) noexcept;
    OffscreenRasterCreateResult(const OffscreenRasterCreateResult&) = delete;
    OffscreenRasterCreateResult& operator=(const OffscreenRasterCreateResult&) = delete;
    ~OffscreenRasterCreateResult();

    // Whether a raster was made (true still after take_raster).
    [[nodiscard]] bool succeeded() const noexcept;
    // The raster, moved out to the caller; null after a refusal or a
    // second take.
    [[nodiscard]] std::unique_ptr<OffscreenRaster> take_raster() noexcept;
    // none when a raster was made.
    [[nodiscard]] OffscreenRasterError error() const noexcept;
    // For a refusal, what was refused and where it looked; empty otherwise.
    [[nodiscard]] std::string detail() const;

private:
    struct Refusal final {
        OffscreenRasterError error{OffscreenRasterError::fonts_not_found};
        std::string detail{};
    };
    explicit OffscreenRasterCreateResult(std::variant<std::unique_ptr<OffscreenRaster>, Refusal> state);
    std::variant<std::unique_ptr<OffscreenRaster>, Refusal> state_;
};

// A CPU raster with the bundled fonts registered and no host window. A
// window borrows it through an OffscreenAttachment; captures make their own
// framebuffers, so one raster serves windows of any size and scale, one at
// a time or together.
//
// One thread: the one that made it, which is the UI thread of every window
// attached to it. Not copyable or movable: windows hold its address.
// Destroy it after every attachment to it has ended (its destructor stops
// the program otherwise, since a window would keep a dangling painter).
class GUI_FORMS_API OffscreenRaster final {
public:
    // Finds the bundled faces (OffscreenRasterOptions), reads and seals
    // them once for the process (the hosts' shared set) and makes the
    // raster. On the UI thread.
    [[nodiscard]] static OffscreenRasterCreateResult create(const OffscreenRasterOptions& options = {});
    ~OffscreenRaster();
    OffscreenRaster(const OffscreenRaster&) = delete;
    OffscreenRaster& operator=(const OffscreenRaster&) = delete;
    OffscreenRaster(OffscreenRaster&&) = delete;
    OffscreenRaster& operator=(OffscreenRaster&&) = delete;

    // The directory its faces came from.
    [[nodiscard]] const std::filesystem::path& font_directory() const noexcept;
    // How many attachments to it are alive.
    [[nodiscard]] std::size_t attachments() const noexcept { return attachments_; }

    // The renderer's private state, defined by the library.
    struct Parts;

private:
    friend struct detail::OffscreenRasterAccess;
    friend class OffscreenAttachment;
    explicit OffscreenRaster(std::unique_ptr<Parts> parts);

    std::unique_ptr<Parts> parts_;
    std::size_t attachments_{};
};

// What OffscreenAttachment's captures draw. The settings apply for the
// capture alone: the window's own scale and presentation are put back
// after it (a scale change relays the window out, both ways).
struct GUI_FORMS_API OffscreenCaptureOptions final {
    // Device pixels per logical unit, in (0, 8]. The window is laid out at
    // this scale for the capture, as a host lays it out on such a display.
    double scale{1.0};
    // The colour scheme drawn; none keeps the window's.
    std::optional<ColourScheme> colour_scheme{};
    // Forced high contrast; none keeps the window's.
    std::optional<bool> high_contrast{};
    // capture_window: the window's in-surface overlays.
    bool include_overlays{true};
    // capture_control: logical units around the control (ControlCaptureOptions::margin).
    double margin{};
    // capture_control: the window's backplane under the control.
    bool include_backplane{true};
};

// While it lives, `window` measures text with `raster` and draws captures
// through it: it is the window's text metrics provider and framebuffer
// painter. Its end puts back the provider the window had and removes the
// painter. Attach only a window with no host (a window an application run
// shows keeps its host's raster). Declare it after the raster and the
// window, so that it ends before both. std::logic_error off the window's
// thread (which must be the raster's). Not copyable or movable.
class GUI_FORMS_API OffscreenAttachment final {
public:
    OffscreenAttachment(OffscreenRaster& raster, Window& window);
    ~OffscreenAttachment();
    OffscreenAttachment(const OffscreenAttachment&) = delete;
    OffscreenAttachment& operator=(const OffscreenAttachment&) = delete;
    OffscreenAttachment(OffscreenAttachment&&) = delete;
    OffscreenAttachment& operator=(OffscreenAttachment&&) = delete;

    // The whole window (Window::capture_window) at `options`. None as
    // capture_window, or for a refused scale; the window's settings are
    // put back either way.
    [[nodiscard]] std::optional<WindowCapture> capture_window(const OffscreenCaptureOptions& options);
    // One control and its descendants (Window::capture_control) at
    // `options`. None as capture_control.
    [[nodiscard]] std::optional<WindowCapture> capture_control(const Control::Ptr& control,
                                                             const OffscreenCaptureOptions& options);

    [[nodiscard]] Window& window() const noexcept { return window_; }

private:
    OffscreenRaster& raster_;
    Window& window_;
    TextMetricsProvider* previous_provider_{};
};

} // namespace gui_forms
