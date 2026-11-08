// Splashscreens (owner, 2026-11-08: APP_WINDOW_RULES_DESIGN Part S): the
// portable half. The platform halves are open_platform_splashscreen in each
// host; Splashscreen::show is in the runtime (GUIForms::Application).

#include "splashscreen_surface.hpp"

#include "gui_forms/text/types/text_types.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace gui_forms {

namespace {

// The splashscreen that shows, or null. Main thread only.
Splashscreen* current_splashscreen = nullptr;

bool text_valid(const std::string_view text) noexcept {
    if (text.size() > SplashscreenOptions::maximum_text_bytes) return false;
    const Utf8ValidationResult validation = validate_utf8(text);
    return validation.valid();
}

bool progress_valid(const std::optional<double> progress) noexcept {
    if (!progress.has_value()) return true;
    const bool valid = std::isfinite(*progress) && *progress >= 0.0 && *progress <= 1.0;
    return valid;
}

bool options_valid(const SplashscreenOptions& options) noexcept {
    const bool size_valid = std::isfinite(options.size.width) && std::isfinite(options.size.height) &&
                            options.size.width >= 160.0 && options.size.width <= 1600.0 &&
                            options.size.height >= 160.0 && options.size.height <= 1600.0;
    const bool valid = size_valid && text_valid(options.title) && text_valid(options.status) &&
                       progress_valid(options.progress);
    return valid;
}

} // namespace

namespace detail {

void paint_splashscreen(Painter& painter, const SplashscreenOptions& options) {
    const Size size = options.size;
    painter.fill_rect(Rect{0.0, 0.0, size.width, size.height}, options.background);
    painter.stroke_rect(Rect{0.5, 0.5, size.width - 1.0, size.height - 1.0}, options.accent, 1.0);
    const FontSpec title_font{.role = FontRole::control, .size = 20.0, .weight = 700};
    const FontSpec status_font{.role = FontRole::control, .size = 13.0};
    constexpr double margin = 24.0;
    if (!options.title.empty()) {
        painter.save();
        painter.clip_rect(Rect{margin, margin, size.width - margin * 2.0, 40.0});
        painter.draw_text_utf8(Point{margin, margin + 24.0}, options.title, title_font, options.foreground);
        painter.restore();
    }
    const double bar_height = 4.0;
    const double status_baseline = size.height - margin - bar_height - 10.0;
    if (!options.status.empty()) {
        painter.save();
        painter.clip_rect(Rect{margin, status_baseline - 16.0, size.width - margin * 2.0, 22.0});
        painter.draw_text_utf8(Point{margin, status_baseline}, options.status, status_font, options.foreground);
        painter.restore();
    }
    if (options.progress.has_value()) {
        const Rect track{margin, size.height - margin - bar_height, size.width - margin * 2.0, bar_height};
        const Color track_color = Color::rgba(options.accent.red, options.accent.green, options.accent.blue, 48);
        painter.fill_rect(track, track_color);
        const double filled = track.width * std::clamp(*options.progress, 0.0, 1.0);
        if (filled > 0.0) painter.fill_rect(Rect{track.x, track.y, filled, track.height}, options.accent);
    }
}

void hand_over_splashscreen() noexcept {
    if (current_splashscreen != nullptr) (*current_splashscreen).close();
}

} // namespace detail

Splashscreen::Splashscreen(SplashscreenOptions options, std::unique_ptr<detail::SplashscreenSurface> surface) noexcept
    : options_(std::move(options)), surface_(std::move(surface)) {}

Splashscreen::~Splashscreen() {
    close();
}

SplashscreenResult Splashscreen::show_on(SplashscreenOptions options,
                                         std::unique_ptr<detail::SplashscreenSurface> surface) {
    SplashscreenResult result{};
    if (!options_valid(options)) {
        result.error = SplashscreenError::invalid_argument;
        return result;
    }
    if (current_splashscreen != nullptr) {
        result.error = SplashscreenError::already_shown;
        return result;
    }
    if (!surface) {
        result.error = SplashscreenError::unsupported;
        return result;
    }
    std::unique_ptr<Splashscreen> splashscreen(new Splashscreen(std::move(options), std::move(surface)));
    if (!(*splashscreen).present()) {
        result.error = SplashscreenError::unsupported;
        return result;
    }
    current_splashscreen = splashscreen.get();
    result.splashscreen = std::move(splashscreen);
    return result;
}

bool Splashscreen::present() noexcept {
    if (!surface_) return false;
    bool presented = false;
    try {
        presented = (*surface_).present(options_);
    } catch (...) {
        presented = false;
    }
    if (presented) frames_ = frames_ + 1U;
    return presented;
}

SplashscreenError Splashscreen::set_status(const std::string_view status) {
    if (!text_valid(status)) return SplashscreenError::invalid_argument;
    options_.status.assign(status);
    if (surface_ && !present()) return SplashscreenError::unsupported;
    return SplashscreenError::none;
}

SplashscreenError Splashscreen::set_progress(const std::optional<double> progress) {
    if (!progress_valid(progress)) return SplashscreenError::invalid_argument;
    options_.progress = progress;
    if (surface_ && !present()) return SplashscreenError::unsupported;
    return SplashscreenError::none;
}

void Splashscreen::close() noexcept {
    if (current_splashscreen == this) current_splashscreen = nullptr;
    // The surface's destructor closes the window.
    surface_.reset();
}

} // namespace gui_forms
