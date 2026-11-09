// The window outer band (owner, 2026-11-08; the active border study,
// section 2.2, and the owner's answers of 2026-11-07). See
// gui_forms/window/band/window_band.hpp for the contract.
//
// The main sequence: Window::declare_band, set_band_appearance and
// report_full_screen record what the application and host ask; every
// change, and every change of what the band depends on (theme, scale,
// presentation, occlusion), goes through SurfaceRoot::refresh_band, which
// resolves the reserved width and the drawn colour and applies the one
// consequence each needs: a layout for a new reserved width, damage of the
// four strips for a new colour. paint_band draws the strips last in each
// paint; band_flash_due drives a flash.

#include "gui_forms/window.hpp"

#include "gui_forms/host/services/host_services.hpp"
#include "gui_forms/theme/theme/theme.hpp"
#include "../../display/recording_painter/recording_painter.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <span>
#include <stdexcept>

namespace gui_forms {

namespace detail {

// The flash wake's target: the window whose band changes phase.
struct BandFlashWake final {
    SurfaceRoot* root{};

    void operator()(FrameTime now) const { (*root).band_flash_due(now); }
};

} // namespace detail

namespace {

// The flash limiter (maximum_band_flash_transitions a second): one colour
// change every half second.
constexpr FrameInterval band_flash_half_period =
    std::chrono::duration_cast<FrameInterval>(std::chrono::milliseconds(1000 / maximum_band_flash_transitions));

// The width a band takes when neither the window nor its theme gives one.
constexpr double default_band_width = 1.0;

constexpr Color transparent_colour = Color::rgba(0, 0, 0, 0);

// The four strips of a band `width` wide inside a client area of `client`:
// top and bottom full width, left and right between them. A strip is empty
// on an axis the band covers.
std::array<Rect, 4> band_strips(Size client, double width) noexcept {
    const double across = std::min(width, client.height);
    const double down = std::max(client.height - 2.0 * width, 0.0);
    const double side = std::min(width, client.width);
    const std::array<Rect, 4> strips{
        Rect{0.0, 0.0, client.width, across},
        Rect{0.0, std::max(client.height - width, 0.0), client.width, across},
        Rect{0.0, width, side, down},
        Rect{std::max(client.width - width, 0.0), width, side, down},
    };
    return strips;
}

// What the band resolves to now (SurfaceRoot::refresh_band).
struct ResolvedBand final {
    double reserved{};
    Color rest{transparent_colour};
    Color lit{transparent_colour};
    bool flashing{};
};

} // namespace

bool window_band_options_valid(const WindowBandOptions& options) noexcept {
    const bool width = std::isfinite(options.width) && options.width >= 0.0 && options.width <= maximum_band_width;
    const bool full_screen =
        options.full_screen == WindowBandFullScreen::stays || options.full_screen == WindowBandFullScreen::hides;
    return width && full_screen;
}

bool window_band_appearance_valid(const WindowBandAppearance& appearance) noexcept {
    const bool known = appearance.state == WindowBandState::rest || appearance.state == WindowBandState::lit ||
                       appearance.state == WindowBandState::flashing;
    return known;
}

double snapped_band_width(double width, double scale) noexcept {
    if (!std::isfinite(width) || width <= 0.0 || !std::isfinite(scale) || scale <= 0.0) return 0.0;
    const double device = std::max(std::round(width * scale), 1.0);
    const double snapped = device / scale;
    return snapped;
}

const char* window_corner_preference_name(WindowCornerPreference preference) noexcept {
    switch (preference) {
    case WindowCornerPreference::system: return "system";
    case WindowCornerPreference::square: return "square";
    case WindowCornerPreference::round: return "round";
    }
    return "unknown";
}

const char* window_corner_shape_name(WindowCornerShape shape) noexcept {
    switch (shape) {
    case WindowCornerShape::unknown: return "unknown";
    case WindowCornerShape::square: return "square";
    case WindowCornerShape::rounded: return "rounded";
    }
    return "unknown";
}

// --- The surface side -------------------------------------------------------

Rect SurfaceRoot::content_rect() const noexcept {
    const double band = band_.reserved;
    const double width = std::max(client_size_.width - 2.0 * band, 0.0);
    const double height = std::max(client_size_.height - 2.0 * band, 0.0);
    const Rect content{band, band, width, height};
    return content;
}

bool SurfaceRoot::band_contains(Point position) const noexcept {
    if (band_.reserved <= 0.0) return false;
    const Rect client{0.0, 0.0, client_size_.width, client_size_.height};
    const bool in_client = client.contains(position);
    const bool in_content = content_rect().contains(position);
    return in_client && !in_content;
}

void SurfaceRoot::damage_band_strips(double width) {
    if (width <= 0.0) return;
    // A rounded band's corners reach in by their radius: the corner squares
    // are damaged whole.
    const bool rounded = band_.corners.shape == WindowCornerShape::rounded;
    const double reach = rounded ? std::max(width, band_.corners.radius) : width;
    const std::array<Rect, 4> strips = band_strips(client_size_, reach);
    for (const Rect& strip : strips) {
        if (!strip.empty()) add_damage(strip, PaintPlane::overlay);
    }
    touch_paint();
}

void SurfaceRoot::refresh_band() {
    const Window* const window = root_ ? (*root_).attached_window() : nullptr;
    if (window == nullptr || theme_ == nullptr) return;
    ResolvedBand resolved{};
    const bool hidden = band_.full_screen && band_.options.full_screen == WindowBandFullScreen::hides;
    if (band_.declared && !hidden) {
        const BandDeclaration themed = (*theme_).window_band(*window);
        ControlVisualContext context;
        context.high_contrast = presentation_settings_.high_contrast;
        context.dark = presentation_settings_.colour_scheme == ColourScheme::dark;
        const ControlVisualRecipe& recipe = (*theme_).resolve(ControlVisualRole::window, context);
        double asked = default_band_width;
        if (themed.width && *themed.width > 0.0) asked = *themed.width;
        if (band_.options.width > 0.0) asked = band_.options.width;
        const WindowBandAppearance& appearance = band_.appearance;
        if (context.high_contrast) {
            // The high-contrast palette: its focus colour, solid, at least
            // its focus width, never flashing.
            asked = std::max(asked, recipe.focus_width);
            resolved.lit = recipe.focus_ring;
        } else {
            resolved.rest = appearance.rest_colour.value_or(themed.rest_colour.value_or(transparent_colour));
            resolved.lit = appearance.lit_colour.value_or(themed.lit_colour.value_or(recipe.focus_ring));
            const bool theme_flash = themed.effect && *themed.effect == BandEffect::flash;
            const bool asked_flash = appearance.state == WindowBandState::flashing ||
                                     (appearance.state == WindowBandState::lit && theme_flash);
            // Reduced motion, ambient motion off and an occluded window
            // still the flash at its lit pose.
            resolved.flashing = asked_flash && !motion_clock_.ambient_suppressed();
        }
        resolved.reserved = snapped_band_width(std::min(asked, maximum_band_width), scale_);
    }

    if (!resolved.flashing) band_.lit_phase = true;
    Color drawn = resolved.lit;
    if (band_.appearance.state == WindowBandState::rest) drawn = resolved.rest;
    if (resolved.flashing && !band_.lit_phase) drawn = resolved.rest;
    if (resolved.reserved <= 0.0) drawn = transparent_colour;

    const double previous_width = band_.reserved;
    const Color previous_drawn = band_.drawn;
    band_.rest = resolved.rest;
    band_.lit = resolved.lit;
    band_.drawn = drawn;
    band_.flashing = resolved.flashing;
    if (resolved.reserved != previous_width) {
        // A new reservation: the root's slot moves, everything is placed
        // again, and the whole client area repaints (set_chrome_insets).
        settle_dirty();
        const Rect old_slot = root_slot();
        band_.reserved = resolved.reserved;
        const Rect new_slot = root_slot();
        add_damage_all_planes({0.0, 0.0, client_size_.width, client_size_.height});
        mark_child_layout_slot(*root_);
        mark_resized_measure(*root_, old_slot.width != new_slot.width, old_slot.height != new_slot.height);
        touch_paint();
    } else if (!(drawn == previous_drawn)) {
        damage_band_strips(band_.reserved);
    }

    if (!band_.flashing) {
        band_.flash_wake.disconnect();
        return;
    }
    if (band_.flash_wake.connected()) return;
    const FrameTime next = motion_clock_.now() + band_flash_half_period;
    band_.flash_wake = request_one_shot_wake(next, detail::BandFlashWake{this});
}

void SurfaceRoot::band_flash_due(FrameTime now) {
    band_.flash_wake.disconnect();
    if (!band_.flashing || band_.reserved <= 0.0) return;
    band_.lit_phase = !band_.lit_phase;
    ++band_.flash_changes;
    band_.drawn = band_.lit_phase ? band_.lit : band_.rest;
    damage_band_strips(band_.reserved);
    // The next change half a period after this one; refresh_band keeps it,
    // or disconnects it when the flash was stilled meanwhile.
    band_.flash_wake = request_one_shot_wake(now + band_flash_half_period, detail::BandFlashWake{this});
    refresh_band();
}

void SurfaceRoot::paint_band(detail::RecordingPainter& candidate, std::span<const Rect> painted) {
    const double width = band_.reserved;
    if (width <= 0.0 || band_.drawn.alpha == 0U) return;
    const Rect client{0.0, 0.0, client_size_.width, client_size_.height};
    const bool rounded = band_.corners.shape == WindowCornerShape::rounded && band_.corners.radius > 0.0;
    const std::array<Rect, 4> strips = band_strips(client_size_, width);
    const Rect content = content_rect();
    for (const Rect& rectangle : painted) {
        // A painted rectangle wholly inside the content draws no band.
        if (content.contains(rectangle)) continue;
        candidate.save();
        candidate.clip_rect(rectangle);
        if (rounded) {
            // One stroke along the window's rounded outline: centred half
            // the width in, its radius as much smaller.
            const double half = width * 0.5;
            const Rect path{client.x + half, client.y + half, std::max(client.width - width, 0.0),
                            std::max(client.height - width, 0.0)};
            const double radius = std::max(band_.corners.radius - half, 0.0);
            candidate.stroke_rounded_rect(path, radius, band_.drawn, width);
        } else {
            for (const Rect& strip : strips) {
                const Rect touched = Rect::intersection(strip, rectangle);
                if (!touched.empty()) candidate.fill_rect(strip, band_.drawn);
            }
        }
        candidate.restore();
    }
}

// --- The window's band API --------------------------------------------------

void Window::declare_band(const WindowBandOptions& options) {
    require_root_thread("band declaration");
    if (!window_band_options_valid(options)) {
        throw std::invalid_argument("GUI.Forms band width must be finite from 0 to 32 with a known full-screen choice");
    }
    band_.declared = true;
    band_.options = options;
    refresh_band();
}

void Window::undeclare_band() {
    require_root_thread("band removal");
    if (!band_.declared) return;
    band_.declared = false;
    refresh_band();
}

void Window::set_band_appearance(const WindowBandAppearance& appearance) {
    require_root_thread("band appearance");
    if (!window_band_appearance_valid(appearance)) {
        throw std::invalid_argument("GUI.Forms band appearance names an unknown state");
    }
    if (band_.appearance == appearance) return;
    band_.appearance = appearance;
    refresh_band();
}

WindowBandSnapshot Window::band_snapshot() const noexcept {
    WindowBandSnapshot snapshot{};
    snapshot.declared = band_.declared;
    snapshot.reserved = band_.reserved > 0.0;
    snapshot.width = band_.reserved;
    snapshot.options = band_.options;
    snapshot.appearance = band_.appearance;
    snapshot.drawn = band_.drawn;
    snapshot.flashing = band_.flashing;
    snapshot.corners = band_.corners;
    snapshot.flash_changes = band_.flash_changes;
    return snapshot;
}

void Window::report_full_screen(bool full_screen) {
    require_root_thread("full-screen report");
    if (band_.full_screen == full_screen) return;
    band_.full_screen = full_screen;
    refresh_band();
}

void Window::set_corner_preference(WindowCornerPreference preference) {
    require_root_thread("corner preference");
    const bool known = preference == WindowCornerPreference::system || preference == WindowCornerPreference::square ||
                       preference == WindowCornerPreference::round;
    if (!known) throw std::invalid_argument("GUI.Forms corner preference is not a WindowCornerPreference");
    if (band_.corner_preference == preference) return;
    band_.corner_preference = preference;
    apply_corner_preference();
}

void Window::apply_corner_preference() {
    WindowCornerReport report{};
    if (host_services_ != nullptr) {
        const HostWindowCornersResult applied = (*host_services_).apply_window_corners(band_.corner_preference);
        report = applied.report;
    }
    if (report == band_.corners) return;
    // The old corners' reach and the new: each repaints its strips.
    damage_band_strips(band_.reserved);
    band_.corners = report;
    damage_band_strips(band_.reserved);
}

} // namespace gui_forms
