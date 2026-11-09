// Splashscreens (owner, 2026-11-08: APP_WINDOW_RULES_DESIGN Part S): the
// portable half. The platform halves are open_platform_splashscreen in each
// host; Splashscreen::show is in the runtime (GUIForms::Application).

#include "splashscreen_surface.hpp"

#include "gui_forms/resources/image_registry/image_registry.hpp"
#include "gui_forms/text/types/text_types.hpp"

#include <algorithm>
#include <cmath>
#include <span>
#include <utility>

namespace gui_forms {

namespace {

// The splashscreen that shows, or null. Main thread only.
Splashscreen* current_splashscreen = nullptr;

// The fixed built-in styling (owner, 2026-11-08: Q-S1 (a)), in logical
// units from the window's edges.
constexpr double status_margin = 16.0;
constexpr double status_font_size = 13.0;
constexpr double bar_height = 3.0;
constexpr double bar_gap = 8.0;
constexpr Color status_color = Color::rgba(255, 255, 255);
constexpr Color status_offset_color = Color::rgba(0, 0, 0, 140);
constexpr Color track_color = Color::rgba(255, 255, 255, 64);
constexpr Color bar_color = Color::rgba(255, 255, 255);

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

bool extent_valid(const std::uint32_t extent) noexcept {
    const bool valid =
        extent >= SplashscreenOptions::minimum_extent && extent <= SplashscreenOptions::maximum_extent;
    return valid;
}

} // namespace

namespace detail {

std::optional<Size> splashscreen_size(const SplashscreenOptions& options) noexcept {
    const std::span<const std::byte> encoded(options.artwork.data(), options.artwork.size());
    const PngValidationResult validation = validate_png(encoded);
    if (!validation) return std::nullopt;
    const PngMetadata metadata = validation.metadata;
    if (!extent_valid(metadata.width) || !extent_valid(metadata.height)) return std::nullopt;
    const Size size{static_cast<double>(metadata.width), static_cast<double>(metadata.height)};
    return size;
}

void paint_splashscreen(Painter& painter, const SplashscreenFrame& frame) {
    const Size size = frame.size;
    if (frame.images != nullptr && frame.artwork.value != 0U) {
        painter.draw_image(frame.artwork, Rect{0.0, 0.0, size.width, size.height});
    }
    const double inner_width = std::max(0.0, size.width - status_margin * 2.0);
    const double bar_top = size.height - status_margin - bar_height;
    if (!frame.status.empty()) {
        const FontSpec font{.role = FontRole::control, .size = status_font_size};
        const double baseline = frame.progress.has_value() ? bar_top - bar_gap : size.height - status_margin;
        painter.save();
        painter.clip_rect(Rect{status_margin, baseline - 16.0, inner_width + 1.0, 21.0});
        painter.draw_text_utf8(Point{status_margin + 1.0, baseline + 1.0}, frame.status, font,
                               status_offset_color);
        painter.draw_text_utf8(Point{status_margin, baseline}, frame.status, font, status_color);
        painter.restore();
    }
    if (frame.progress.has_value()) {
        const Rect track{status_margin, bar_top, inner_width, bar_height};
        painter.fill_rect(track, track_color);
        const double filled = track.width * std::clamp(*frame.progress, 0.0, 1.0);
        if (filled > 0.0) painter.fill_rect(Rect{track.x, track.y, filled, track.height}, bar_color);
    }
}

void hand_over_splashscreen() noexcept {
    if (current_splashscreen != nullptr) (*current_splashscreen).close();
}

} // namespace detail

Splashscreen::Splashscreen(SplashscreenOptions options, std::unique_ptr<ImageRegistry> images,
                           const ImageId artwork, const Size size) noexcept
    : options_(std::move(options)), images_(std::move(images)), artwork_(artwork), size_(size) {}

Splashscreen::~Splashscreen() {
    close();
}

SplashscreenResult Splashscreen::show_on(SplashscreenOptions options,
                                         std::unique_ptr<detail::SplashscreenSurface> surface) {
    SplashscreenResult result{};
    const std::optional<Size> size = detail::splashscreen_size(options);
    if (!size.has_value() || !text_valid(options.status) || !progress_valid(options.progress)) {
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
    // The artwork's own registry: the one image, decoded by the surface's
    // raster when it synchronizes.
    std::unique_ptr<ImageRegistry> images = std::make_unique<ImageRegistry>();
    const std::span<const std::byte> encoded(options.artwork.data(), options.artwork.size());
    const ImageLoadResult loaded = (*images).load_png(encoded);
    if (!loaded) {
        result.error = SplashscreenError::invalid_argument;
        return result;
    }
    std::unique_ptr<Splashscreen> splashscreen(
        new Splashscreen(std::move(options), std::move(images), loaded.image, *size));
    (*splashscreen).surface_ = std::move(surface);
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
        detail::SplashscreenFrame frame{};
        frame.size = size_;
        frame.status = options_.status;
        frame.progress = options_.progress;
        frame.images = images_.get();
        frame.artwork = artwork_;
        presented = (*surface_).present(frame);
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
