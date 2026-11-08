#pragma once

#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace gui_forms {

namespace detail {
class SplashscreenSurface;
} // namespace detail

// A splashscreen (owner, 2026-11-08: APP_WINDOW_RULES_DESIGN Part S; a
// Splash of kind splashscreen) shown before Application::create, while the
// program loads: a borderless window centred on the primary monitor that
// takes no input, painted by a minimal path of its own (a filled face, the
// title, one status line and an optional progress bar, drawn with the
// bundled control face through the host's CPU raster; no theme, controls
// or layout), so that it shows before the theme is compiled.
struct SplashscreenOptions final {
    static constexpr std::size_t maximum_text_bytes = 512U;
    // Logical units; each extent 160 to 1600.
    Size size{440.0, 240.0};
    // UTF-8, at most maximum_text_bytes each.
    std::string title{};
    std::string status{};
    // In [0, 1]; none shows no bar.
    std::optional<double> progress{};
    Color background{Color::rgba(250, 252, 255)};
    Color foreground{Color::rgba(31, 37, 44)};
    Color accent{Color::rgba(0, 103, 192)};
};

// What Splashscreen::show and the setters refuse.
enum class SplashscreenError : std::uint8_t {
    none,
    // A size out of range, text that is not UTF-8 or too long, or a
    // progress outside [0, 1].
    invalid_argument,
    // Another splashscreen shows; there is one at a time.
    already_shown,
    // The host has no splashscreen (no display, an unsupported platform),
    // or could not present it. The program goes on without one.
    unsupported,
};

class Splashscreen;

struct SplashscreenResult final {
    SplashscreenError error{SplashscreenError::none};
    std::unique_ptr<Splashscreen> splashscreen{};

    [[nodiscard]] explicit operator bool() const noexcept {
        const bool shown = splashscreen != nullptr;
        return shown;
    }
};

// One shown splashscreen, owned by the program. Its window closes when it
// is destroyed, when close() is called, or (the handover) when the first
// window of Application::run is ready; after that the setters change its
// text and do nothing more. Each setter presents the new frame at once,
// without a run loop. Main thread only (the thread that later runs the
// application). Link GUIForms::Application for show.
class Splashscreen final {
public:
    [[nodiscard]] static SplashscreenResult show(SplashscreenOptions options);

    ~Splashscreen();
    Splashscreen(const Splashscreen&) = delete;
    Splashscreen& operator=(const Splashscreen&) = delete;

    // invalid_argument for text that is not UTF-8 or too long, or a
    // progress outside [0, 1], changing nothing.
    SplashscreenError set_status(std::string_view status);
    SplashscreenError set_progress(std::optional<double> progress);
    void close() noexcept;

    [[nodiscard]] bool shown() const noexcept { return surface_ != nullptr; }
    [[nodiscard]] const SplashscreenOptions& options() const noexcept { return options_; }
    // Frames presented, the first included.
    [[nodiscard]] std::uint64_t frames() const noexcept { return frames_; }

    // Shows `options` through `surface` (a host's or a test's): the
    // portable half of show, which supplies the platform's surface.
    [[nodiscard]] static SplashscreenResult show_on(SplashscreenOptions options,
                                                    std::unique_ptr<detail::SplashscreenSurface> surface);

private:
    Splashscreen(SplashscreenOptions options, std::unique_ptr<detail::SplashscreenSurface> surface) noexcept;
    [[nodiscard]] bool present() noexcept;

    SplashscreenOptions options_;
    std::unique_ptr<detail::SplashscreenSurface> surface_;
    std::uint64_t frames_{};
};

} // namespace gui_forms
