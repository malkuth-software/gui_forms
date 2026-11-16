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
// paint; band_flash_due drives a flash or a cycle.

#include "gui_forms/window.hpp"

#include "gui_forms/control/active_border/active_border.hpp"
#include "gui_forms/host/services/host_services.hpp"
#include "gui_forms/surface_material/material_fill_layer/material_fill_layer.hpp"
#include "gui_forms/surface_material/painting/surface_material_painting.hpp"
#include "gui_forms/surface_material/types/surface_material_types.hpp"
#include "gui_forms/theme/theme/theme.hpp"
#include "../../display/recording_painter/recording_painter.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <optional>
#include <stxlib/optional.hpp>
#include <span>
#include <stdexcept>

namespace gui_forms {

// The flash wake's target: the window whose band changes phase.
struct SurfaceRoot::BandFlashWake final {
    SurfaceRoot* root{};

    void operator()(FrameTime now) const { (*root).band_flash_due(now); }
};

namespace {

// The width a band takes when neither the window nor its theme gives one.
constexpr double default_band_width = 1.0;

constexpr Color transparent_colour = Color::rgba(0, 0, 0, 0);

// A flash's half period at `frequency` cycles a second (above 0).
FrameInterval band_flash_half_period(double frequency) noexcept {
    const std::chrono::duration<double> seconds(0.5 / frequency);
    const FrameInterval interval = std::chrono::ceil<FrameInterval>(seconds);
    return interval;
}

// The interval between a cycle's frames.
FrameInterval band_cycle_interval() noexcept {
    const std::chrono::duration<double> seconds(1.0 / band_cycling_frames_per_second);
    const FrameInterval interval = std::chrono::ceil<FrameInterval>(seconds);
    return interval;
}

bool any_edge(const Insets& widths) noexcept {
    const bool any = widths.left > 0.0 || widths.top > 0.0 || widths.right > 0.0 || widths.bottom > 0.0;
    return any;
}

double widest_edge(const Insets& widths) noexcept {
    const double widest = std::max(std::max(widths.left, widths.top), std::max(widths.right, widths.bottom));
    return widest;
}

bool uniform_edges(const Insets& widths) noexcept {
    const bool uniform = widths.left == widths.top && widths.top == widths.right && widths.right == widths.bottom;
    return uniform;
}

bool band_width_valid(double width) noexcept {
    const bool valid = std::isfinite(width) && width >= 0.0 && width <= maximum_band_width;
    return valid;
}

// Each edge of `asked` snapped at `scale`; an edge of 0 stays 0.
Insets snapped_band_edges(const Insets& asked, double scale) noexcept {
    const Insets snapped{snapped_band_width(std::min(asked.left, maximum_band_width), scale),
                         snapped_band_width(std::min(asked.top, maximum_band_width), scale),
                         snapped_band_width(std::min(asked.right, maximum_band_width), scale),
                         snapped_band_width(std::min(asked.bottom, maximum_band_width), scale)};
    return snapped;
}

// Each edge that has a width widened to `minimum`.
Insets widened_band_edges(const Insets& asked, double minimum) noexcept {
    Insets widened = asked;
    if (widened.left > 0.0) widened.left = std::max(widened.left, minimum);
    if (widened.top > 0.0) widened.top = std::max(widened.top, minimum);
    if (widened.right > 0.0) widened.right = std::max(widened.right, minimum);
    if (widened.bottom > 0.0) widened.bottom = std::max(widened.bottom, minimum);
    return widened;
}

// The four strips of a band `widths` wide inside a client area of
// `client`: top and bottom full width, left and right between them. A
// strip is empty on an edge without width and on an axis the band covers.
std::array<Rect, 4> band_strips(Size client, const Insets& widths) noexcept {
    const double top = std::min(widths.top, client.height);
    const double bottom = std::min(widths.bottom, client.height);
    const double down = std::max(client.height - widths.top - widths.bottom, 0.0);
    const std::array<Rect, 4> strips{
        Rect{0.0, 0.0, client.width, top},
        Rect{0.0, std::max(client.height - widths.bottom, 0.0), client.width, bottom},
        Rect{0.0, widths.top, std::min(widths.left, client.width), down},
        Rect{std::max(client.width - widths.right, 0.0), widths.top, std::min(widths.right, client.width), down},
    };
    return strips;
}

// The lit colour a role names, while the application gives none.
Color role_colour(WindowBandColourRole role, const BandDeclaration& themed, const ControlVisualRecipe& recipe,
                  const BasicControlStyle& style) noexcept {
    switch (role) {
    case WindowBandColourRole::theme: return themed.lit_colour.value_or(recipe.focus_ring);
    case WindowBandColourRole::focus: return recipe.focus_ring;
    case WindowBandColourRole::accent: return style.accent;
    case WindowBandColourRole::accent_light: return style.accent_light;
    case WindowBandColourRole::text: return style.text;
    case WindowBandColourRole::border: return style.border;
    }
    // A role outside the enumeration (refused on input) takes the theme's.
    return themed.lit_colour.value_or(recipe.focus_ring);
}

// The motion the engine limiter allows (resolve_active_border, the control
// border's: the red flash rule, rest under suppressed motion) for a band
// asked to flash or cycle between `lit` and `rest` at `frequency`, further
// lowered to maximum_band_frequency.
ActiveBorderPose limited_band_motion(Color lit, Color rest, ActiveBorderMotion motion, double frequency,
                                     bool suppressed) noexcept {
    ActiveBorder asked{};
    asked.colour = lit;
    asked.alternate = rest;
    asked.motion = motion;
    asked.frequency = frequency;
    ActiveBorderConditions conditions{};
    conditions.rest = suppressed;
    ActiveBorderPose pose = resolve_active_border(asked, conditions);
    pose.frequency = std::min(pose.frequency, maximum_band_frequency);
    return pose;
}

// What the band resolves to now (SurfaceRoot::refresh_band).
struct ResolvedBand final {
    Insets reserved{};
    Color rest{transparent_colour};
    Color lit{transparent_colour};
    bool flashing{};
    bool cycling{};
    double frequency{};
    bool textured{};
};

// One strip's texture as a material: the texture's one fill layer, square
// and borderless.
SurfaceMaterial band_texture_material(const WindowBandTexture& texture) {
    SurfaceMaterial material{};
    material.fills.clear();
    if (texture.source == WindowBandTextureSource::finish) {
        material.fills.push_back(MaterialFillLayer::finish_fill(texture.finish, texture.opacity));
    } else {
        material.fills.push_back(MaterialFillLayer::procedural_pattern(texture.procedural, texture.opacity));
    }
    return material;
}

} // namespace

bool window_band_options_valid(const WindowBandOptions& options) noexcept {
    bool width = band_width_valid(options.width);
    if (options.edges) {
        const WindowBandEdges& edges = *options.edges;
        width = band_width_valid(edges.left) && band_width_valid(edges.top) && band_width_valid(edges.right) &&
                band_width_valid(edges.bottom);
    }
    const bool full_screen =
        options.full_screen == WindowBandFullScreen::stays || options.full_screen == WindowBandFullScreen::hides;
    return width && full_screen;
}

bool window_band_texture_valid(const WindowBandTexture& texture) noexcept {
    const bool opacity = std::isfinite(texture.opacity) && texture.opacity >= 0.0 && texture.opacity <= 1.0;
    if (!opacity) return false;
    switch (texture.source) {
    case WindowBandTextureSource::procedural: break;
    case WindowBandTextureSource::finish:
        // The glint's cost is refused over a window's strips.
        if (texture.finish.kind == FinishKind::glint) return false;
        break;
    default: return false;
    }
    const SurfaceMaterial material = band_texture_material(texture);
    const bool valid = valid_surface_material(material);
    return valid;
}

bool window_band_appearance_valid(const WindowBandAppearance& appearance) noexcept {
    const bool known = appearance.state == WindowBandState::rest || appearance.state == WindowBandState::lit ||
                       appearance.state == WindowBandState::flashing || appearance.state == WindowBandState::cycling;
    bool role = false;
    switch (appearance.lit_role) {
    case WindowBandColourRole::theme:
    case WindowBandColourRole::focus:
    case WindowBandColourRole::accent:
    case WindowBandColourRole::accent_light:
    case WindowBandColourRole::text:
    case WindowBandColourRole::border: role = true; break;
    default: role = false; break;
    }
    const bool frequency = std::isfinite(appearance.frequency) && appearance.frequency >= 0.0;
    const bool texture = !appearance.texture || window_band_texture_valid(*appearance.texture);
    return known && role && frequency && texture;
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
    const Insets& band = band_.reserved;
    const double width = std::max(client_size_.width - band.left - band.right, 0.0);
    const double height = std::max(client_size_.height - band.top - band.bottom, 0.0);
    const Rect content{band.left, band.top, width, height};
    return content;
}

bool SurfaceRoot::band_contains(Point position) const noexcept {
    if (!any_edge(band_.reserved)) return false;
    const Rect client{0.0, 0.0, client_size_.width, client_size_.height};
    const bool in_client = client.contains(position);
    const bool in_content = content_rect().contains(position);
    return in_client && !in_content;
}

void SurfaceRoot::damage_band_strips(Insets widths) {
    if (!any_edge(widths)) return;
    // A rounded band's corners reach in by their radius: the corner squares
    // are damaged whole.
    Insets reach = widths;
    if (band_.corners.shape == WindowCornerShape::rounded) {
        const double radius = band_.corners.radius;
        reach = Insets{std::max(widths.left, radius), std::max(widths.top, radius), std::max(widths.right, radius),
                       std::max(widths.bottom, radius)};
    }
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
        Insets asked{};
        if (band_.options.edges) {
            const WindowBandEdges& edges = *band_.options.edges;
            asked = Insets{edges.left, edges.top, edges.right, edges.bottom};
        } else {
            double uniform = default_band_width;
            const double* const themed_width = themed.width.value();
            if (themed_width != nullptr && *themed_width > 0.0) uniform = *themed_width;
            if (band_.options.width > 0.0) uniform = band_.options.width;
            asked = Insets{uniform, uniform, uniform, uniform};
        }
        const WindowBandAppearance& appearance = band_.appearance;
        if (context.high_contrast) {
            // The high-contrast palette: its focus colour, solid, at least
            // its focus width, never moving and never textured.
            asked = widened_band_edges(asked, recipe.focus_width);
            resolved.lit = recipe.focus_ring;
        } else {
            const BasicControlStyle& style = (*theme_).basic_style();
            resolved.rest = appearance.rest_colour.value_or(themed.rest_colour.value_or(transparent_colour));
            resolved.lit =
                appearance.lit_colour.value_or(role_colour(appearance.lit_role, themed, recipe, style));
            const bool theme_flash = themed.effect == stxlib::Optional<BandEffect>::of(BandEffect::flash);
            const bool asked_flash = appearance.state == WindowBandState::flashing ||
                                     (appearance.state == WindowBandState::lit && theme_flash);
            const bool asked_cycle = appearance.state == WindowBandState::cycling;
            if (asked_flash || asked_cycle) {
                // The engine limiter: reduced motion, ambient motion off and
                // an occluded window still the band at its lit pose; a
                // saturated red never flashes; at most
                // maximum_band_frequency cycles a second.
                const ActiveBorderMotion motion = asked_cycle ? ActiveBorderMotion::cycling : ActiveBorderMotion::flashing;
                const ActiveBorderPose pose = limited_band_motion(resolved.lit, resolved.rest, motion,
                                                                  appearance.frequency,
                                                                  motion_clock_.ambient_suppressed());
                resolved.flashing = pose.motion == ActiveBorderMotion::flashing;
                resolved.cycling = pose.motion == ActiveBorderMotion::cycling;
                if (resolved.flashing || resolved.cycling) resolved.frequency = pose.frequency;
            }
            resolved.textured = appearance.texture.has_value();
        }
        resolved.reserved = snapped_band_edges(asked, scale_);
        if (!any_edge(resolved.reserved)) resolved.textured = false;
    }

    const bool was_cycling = band_.cycling;
    if (!resolved.flashing) band_.lit_phase = true;
    Color drawn = resolved.lit;
    if (band_.appearance.state == WindowBandState::rest) drawn = resolved.rest;
    if (resolved.flashing && !band_.lit_phase) drawn = resolved.rest;
    if (resolved.cycling && was_cycling && band_.frequency == resolved.frequency) {
        // A running cycle keeps its phase.
        const std::chrono::duration<double> elapsed = motion_clock_.now() - band_.cycle_start;
        ActiveBorderPose pose{};
        pose.lit = resolved.lit;
        pose.alternate = resolved.rest;
        pose.motion = ActiveBorderMotion::cycling;
        pose.frequency = resolved.frequency;
        drawn = active_border_colour_at(pose, elapsed.count());
    } else if (resolved.cycling) {
        // A cycle starts lit, now.
        band_.cycle_start = motion_clock_.now();
    }
    if (!any_edge(resolved.reserved)) drawn = transparent_colour;

    const Insets previous_widths = band_.reserved;
    const Color previous_drawn = band_.drawn;
    const bool previous_textured = band_.textured;
    band_.rest = resolved.rest;
    band_.lit = resolved.lit;
    band_.drawn = drawn;
    band_.flashing = resolved.flashing;
    band_.cycling = resolved.cycling;
    band_.frequency = resolved.frequency;
    band_.textured = resolved.textured;
    if (!(resolved.reserved == previous_widths)) {
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
    } else if (!(drawn == previous_drawn) || previous_textured != resolved.textured) {
        damage_band_strips(band_.reserved);
    }

    if (!band_.flashing && !band_.cycling) {
        band_.flash_wake.disconnect();
        return;
    }
    // A motion that changed kind waits a whole step of the new kind.
    if (band_.cycling != was_cycling) band_.flash_wake.disconnect();
    if (band_.flash_wake.connected()) return;
    const FrameInterval step = band_.cycling ? band_cycle_interval() : band_flash_half_period(band_.frequency);
    const FrameTime next = motion_clock_.now() + step;
    band_.flash_wake = request_one_shot_wake(next, BandFlashWake{this});
}

void SurfaceRoot::band_flash_due(FrameTime now) {
    band_.flash_wake.disconnect();
    if ((!band_.flashing && !band_.cycling) || !any_edge(band_.reserved)) return;
    if (band_.cycling) {
        ActiveBorderPose pose{};
        pose.lit = band_.lit;
        pose.alternate = band_.rest;
        pose.motion = ActiveBorderMotion::cycling;
        pose.frequency = band_.frequency;
        const std::chrono::duration<double> elapsed = now - band_.cycle_start;
        const Color drawn = active_border_colour_at(pose, elapsed.count());
        ++band_.cycle_frames;
        if (!(drawn == band_.drawn)) {
            band_.drawn = drawn;
            damage_band_strips(band_.reserved);
        }
        band_.flash_wake = request_one_shot_wake(now + band_cycle_interval(), BandFlashWake{this});
        return;
    }
    band_.lit_phase = !band_.lit_phase;
    ++band_.flash_changes;
    band_.drawn = band_.lit_phase ? band_.lit : band_.rest;
    damage_band_strips(band_.reserved);
    // The next change half a period after this one; refresh_band keeps it,
    // or disconnects it when the flash was stilled meanwhile.
    band_.flash_wake = request_one_shot_wake(now + band_flash_half_period(band_.frequency), BandFlashWake{this});
    refresh_band();
}

void SurfaceRoot::paint_band(detail::RecordingPainter& candidate, std::span<const Rect> painted) {
    const Insets& widths = band_.reserved;
    const bool coloured = band_.drawn.alpha != 0U;
    if (!any_edge(widths) || (!coloured && !band_.textured)) return;
    const Rect client{0.0, 0.0, client_size_.width, client_size_.height};
    // One stroke along a rounded outline needs one width on every side; a
    // band of differing edges draws square strips.
    const bool rounded = band_.corners.shape == WindowCornerShape::rounded && band_.corners.radius > 0.0 &&
                         uniform_edges(widths) && !band_.textured;
    const std::array<Rect, 4> strips = band_strips(client_size_, widths);
    const Rect content = content_rect();
    std::optional<SurfaceMaterial> texture{};
    if (band_.textured && band_.appearance.texture) texture = band_texture_material(*band_.appearance.texture);
    for (const Rect& rectangle : painted) {
        // A painted rectangle wholly inside the content draws no band.
        if (content.contains(rectangle)) continue;
        candidate.save();
        candidate.clip_rect(rectangle);
        if (rounded) {
            // One stroke along the window's rounded outline: centred half
            // the width in, its radius as much smaller.
            const double width = widths.left;
            const double half = width * 0.5;
            const Rect path{client.x + half, client.y + half, std::max(client.width - width, 0.0),
                            std::max(client.height - width, 0.0)};
            const double radius = std::max(band_.corners.radius - half, 0.0);
            candidate.stroke_rounded_rect(path, radius, band_.drawn, width);
        } else {
            for (const Rect& strip : strips) {
                const Rect touched = Rect::intersection(strip, rectangle);
                if (touched.empty()) continue;
                // The texture beneath, the colour over it.
                if (texture) paint_surface_material(candidate, strip, *texture);
                if (coloured) candidate.fill_rect(strip, band_.drawn);
            }
        }
        candidate.restore();
    }
}

// --- The window's band API --------------------------------------------------

void Window::declare_band(const WindowBandOptions& options) {
    require_root_thread("band declaration");
    if (!window_band_options_valid(options)) {
        throw std::invalid_argument(
            "GUI.Forms band widths must be finite from 0 to 32 with a known full-screen choice");
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
        throw std::invalid_argument(
            "GUI.Forms band appearance needs a known state and role, a finite frequency not below 0 and a valid "
            "texture");
    }
    if (band_.appearance == appearance) return;
    band_.appearance = appearance;
    refresh_band();
}

WindowBandSnapshot Window::band_snapshot() const noexcept {
    WindowBandSnapshot snapshot{};
    snapshot.declared = band_.declared;
    snapshot.reserved = any_edge(band_.reserved);
    snapshot.width = widest_edge(band_.reserved);
    snapshot.edges = band_.reserved;
    snapshot.options = band_.options;
    snapshot.appearance = band_.appearance;
    snapshot.drawn = band_.drawn;
    snapshot.flashing = band_.flashing;
    snapshot.cycling = band_.cycling;
    snapshot.frequency = band_.frequency;
    snapshot.textured = band_.textured;
    snapshot.corners = band_.corners;
    snapshot.flash_changes = band_.flash_changes;
    snapshot.cycle_frames = band_.cycle_frames;
    return snapshot;
}

WindowShapingReport Window::shaping_report() const {
    WindowShapingReport report{};
    report.full_screen = band_.full_screen;
    report.preference = band_.corner_preference;
    report.corners = band_.corners;
    if (host_services_ == nullptr) return report;
    const HostServicesSnapshot services = (*host_services_).snapshot();
    const HostCapabilities& capabilities = services.capabilities;
    report.host_attached = true;
    report.corners_reported = capabilities.supports(HostCapability::window_corners);
    report.state_reported = capabilities.supports(HostCapability::window_state_reports);
    if (report.corners_reported) {
        report.square_corners = capabilities.window_corner_shapes.square;
        report.rounded_corners = capabilities.window_corner_shapes.rounded;
    }
    // No host draws the band natively yet (the Windows 11 contract is
    // written for its host: docs/user/window-band.md).
    report.native_border = false;
    return report;
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
