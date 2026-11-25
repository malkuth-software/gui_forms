#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/surface_material/finish_fill/finish_fill.hpp"
#include "gui_forms/surface_material/procedural_fill/procedural_fill.hpp"
#include "gui_forms/types.hpp"

#include <cstdint>
#include <optional>
#include <stxlib/optional.hpp>

// The window outer band (owner, 2026-11-08: "a thin effect"; the active
// border study, section 2.2, and the owner's answers of 2026-11-07). A thin
// strip owned by a top-level window along the edges of its client area,
// drawn above every paint plane (overlays included) and hitting nothing.
//
// Reservation is static: declaring the band reserves its width on each
// side (one width on every side, or one per edge), the root is measured against the client size less the band (and
// the chrome insets) and arranged inside it, and nothing the band then
// does (lighting, flashing, a colour change) touches layout; each repaints
// the four strips only. Undeclaring it, a theme or high-contrast change of
// its width, a scale change that snaps it to another width, and entering
// or leaving full screen with WindowBandFullScreen::hides are the changes
// that lay the window out again.
//
// The band is untrusted content of the application (owner, answer 1): it
// can carry focus or attention, but it cannot vouch for sandboxing. Its
// sandbox role is the reservation itself, which a trusted shim outside the
// process can draw into.
//
// Renderer-neutral: the window paints it with Painter fills; host shaping
// (rounded corners) follows only what the host reports
// (WindowCornerReport).
//
// The theme gives the band's look from the `window` rule: `band-width`
// (a length), `band-color` (the rest colour), `band-lit-color` (the lit
// colour, else the window's focus-ring colour) and `band-effect` (`none` or
// `flash`: a theme's band flashes while lit). Under high contrast the band
// draws the high-contrast palette's focus-ring colour, solid, at least the
// palette's focus width wide, and never flashes.
namespace gui_forms {

// Whether a declared band keeps its reservation while the window is full
// screen (owner, answer 3: the developer chooses).
enum class WindowBandFullScreen : std::uint8_t {
    stays,
    hides,
};

// One width per edge, in logical units, before each is snapped to whole
// device pixels (WindowBandOptions::edges). An edge of 0 reserves nothing
// on that side.
struct GUI_FORMS_API WindowBandEdges final {
    double left{};
    double top{};
    double right{};
    double bottom{};

    friend bool operator==(const WindowBandEdges& left, const WindowBandEdges& right) noexcept {
        return left.left == right.left && left.top == right.top && left.right == right.right &&
               left.bottom == right.bottom;
    }
};

// A window's band declaration (Window::declare_band).
struct GUI_FORMS_API WindowBandOptions final {
    // The width asked for on every side, in logical units, before it is
    // snapped to whole device pixels: 0 takes the theme's `band-width`, or
    // one logical unit where the theme declares none. Finite, from 0 to
    // maximum_band_width. Not read while `edges` is set.
    double width{};
    WindowBandFullScreen full_screen{WindowBandFullScreen::stays};
    // Per-edge widths (the owner's approval of 2026-11-13: per-edge widths
    // in logical units). When set, each edge takes its own width, finite
    // from 0 to maximum_band_width, and the theme's `band-width` is not
    // read; under high contrast each edge that has a width is widened to
    // the palette's focus width, and an edge of 0 stays empty.
    stxlib::Optional<WindowBandEdges> edges{};

    friend bool operator==(const WindowBandOptions& left, const WindowBandOptions& right) noexcept {
        return left.width == right.width && left.full_screen == right.full_screen && left.edges == right.edges;
    }
};

// The widest band a window or a theme may ask for, in logical units.
inline constexpr double maximum_band_width = 32.0;

// What the band shows (Window::set_band_appearance; owner, answer 1: an
// appearance configuration, settable dynamically). `rest` draws the rest
// colour (transparent unless the theme or the application gives one, so a
// resting band shows the window's base: reserved space only); `lit` draws
// the lit colour; `flashing` alternates the two at most
// maximum_band_flash_transitions times a second, and rests lit under
// reduced motion, with ambient motion off, or while the window is occluded.
// `cycling` blends between the rest and lit colours along a cosine,
// starting lit, repainting the strips at band_cycling_frames_per_second,
// and rests lit under the same conditions.
// `sweeping` moves along the band's outline (the automation's in-use sign,
// MCP round 1): the band draws its rest colour, and over it a glint of the
// lit colour (band_glint_fraction of the outline, fading behind) runs one
// lap a cycle, repainting at band_cycling_frames_per_second. Under reduced
// motion, with ambient motion off, or while the window is occluded, the
// glint is replaced by a tint change: the whole band draws its rest colour
// moved band_still_tint of the way to the lit colour. A pixel changes at
// most twice a cycle, so the flash limiter's frequency bounds it too. It
// needs one width on every edge and no texture; a band without either
// draws its lit colour.
enum class WindowBandState : std::uint8_t {
    rest,
    lit,
    flashing,
    cycling,
    sweeping,
};

// A sweeping band's glint, as a share of the outline's length, and how far
// a still sweeping band's tint moves from the rest colour to the lit one.
inline constexpr double band_glint_fraction = 0.22;
inline constexpr double band_still_tint = 0.35;

// The flash limiter (study section 5, WCAG 2.3.1): at most this many
// luminance transitions a second, whatever the theme or the application
// asks: a flashing band changes colour once every half second.
inline constexpr unsigned maximum_band_flash_transitions = 2U;
// The same limit in cycles a second (one cycle is two transitions), for
// flashing and cycling alike: a higher `frequency` is lowered to it.
inline constexpr double maximum_band_frequency = 1.0;
// A cycling band repaints its strips at this rate; a flashing one only at
// its transitions.
inline constexpr double band_cycling_frames_per_second = 30.0;

// Which theme colour the band lights in when the application gives no lit
// colour (owner approval of 2026-11-13: "colour or role"). `theme` is the
// theme's `band-lit-color`, else the window's focus-ring colour; the others
// are the theme's basic colours of those names.
enum class WindowBandColourRole : std::uint8_t {
    theme,
    focus,
    accent,
    accent_light,
    text,
    border,
};

// What a band texture draws (WindowBandTexture).
enum class WindowBandTextureSource : std::uint8_t {
    procedural,
    finish,
};

// A texture under the band's colour (owner answer 6: textures are the
// window band's alone). Each strip is its own surface (the top and bottom
// strips run the full width, the side strips lie between them), so a bake
// stays the size of a strip, never the window's; it is drawn beneath the
// band's colour: a resting band with the default transparent rest
// colour shows the texture alone, and a lit colour composites over it.
// `procedural` reads `procedural` (tile from minimum_procedural_tile to
// maximum_procedural_tile, strength in [0, 1]); `finish` reads `finish`,
// any kind but the glint (whose cost the bake budget refuses over a
// window). `opacity` in [0, 1]. Under high contrast no texture is drawn.
struct GUI_FORMS_API WindowBandTexture final {
    WindowBandTextureSource source{WindowBandTextureSource::procedural};
    ProceduralFill procedural{};
    FinishFill finish{};
    double opacity{1.0};

    friend bool operator==(const WindowBandTexture& left, const WindowBandTexture& right) noexcept {
        return left.source == right.source && left.procedural == right.procedural && left.finish == right.finish &&
               left.opacity == right.opacity;
    }
};

struct GUI_FORMS_API WindowBandAppearance final {
    WindowBandState state{WindowBandState::rest};
    // The application's colours, each replacing the theme's while set.
    // Ignored under high contrast, where the band takes the high-contrast
    // palette.
    stxlib::Optional<Color> rest_colour{};
    stxlib::Optional<Color> lit_colour{};
    // The theme colour the band lights in while lit_colour is empty.
    WindowBandColourRole lit_role{WindowBandColourRole::theme};
    // Cycles a second of a flash or a cycle, lowered to
    // maximum_band_frequency; 0, or anything below
    // minimum_active_border_frequency, holds the band lit. Finite, not
    // negative.
    double frequency{maximum_band_frequency};
    stxlib::Optional<WindowBandTexture> texture{};

    friend bool operator==(const WindowBandAppearance& left, const WindowBandAppearance& right) noexcept {
        return left.state == right.state && left.rest_colour == right.rest_colour &&
               left.lit_colour == right.lit_colour && left.lit_role == right.lit_role &&
               left.frequency == right.frequency && left.texture == right.texture;
    }
};

// Whether `options` is a declaration the window accepts: finite widths
// from 0 to maximum_band_width (the uniform one, or each edge's) and a
// known full-screen choice.
[[nodiscard]] GUI_FORMS_API bool window_band_options_valid(const WindowBandOptions& options) noexcept;
// Whether `appearance` names a known state and role, a finite frequency
// that is not negative, and a texture window_band_texture_valid accepts.
[[nodiscard]] GUI_FORMS_API bool window_band_appearance_valid(const WindowBandAppearance& appearance) noexcept;
// Whether `texture` may be drawn in the band, by WindowBandTexture's rules.
[[nodiscard]] GUI_FORMS_API bool window_band_texture_valid(const WindowBandTexture& texture) noexcept;

// `width` logical units snapped to whole device pixels at `scale`: the
// nearest whole number of device pixels, at least one, back in logical
// units (study section 2.2), so content inside the band keeps landing on
// the device grid. 0 for a width that is not positive and finite or a scale
// that is not.
[[nodiscard]] GUI_FORMS_API double snapped_band_width(double width, double scale) noexcept;

// The corner shape a window asks its host for (owner, answer 4: facilitate
// rounding everywhere, and let a developer opt out where the platform
// allows). `system` keeps whatever the platform does.
enum class WindowCornerPreference : std::uint8_t {
    system,
    square,
    round,
};

// What the host says the window's corners are, as the band follows them.
enum class WindowCornerShape : std::uint8_t {
    // The host reports none: the band draws square corners (the honest
    // fallback on macOS, which has no public corner API, and on X11).
    unknown,
    square,
    rounded,
};

// The host's report of the window's corners, in reply to its preference
// (HostServices::apply_window_corners). `radius` is in logical units, and
// meaningful only for `rounded`. `complied` is false when the platform
// could not do as asked (it rounds regardless, or cannot round), so the
// band follows `shape`, not the preference.
// `top_square` says that the client area's top corners meet a title bar
// the host draws (a titled window on macOS), so they stay square however
// the window's frame is rounded; the band then rounds its bottom corners
// only.
// Where a host's corner report came from (ACTIVE_BORDER_STUDY ruling 4:
// the band follows only what the host can say):
// - platform: the platform shapes the window and states its shape (Windows
//   11's DWM preference and window state; a macOS window in full screen or
//   without a frame, which is square);
// - layout_region: macOS 26's public layout regions, the corner adaptation
//   of the content view's safe area (the layout region API);
// - private_getter: AppKit's private radius getter, read at run time where
//   no public source reports it (GUI_FORMS_MACOS_PRIVATE_CORNER_RADIUS);
// - unknown: nothing reported; the shape is unknown and the band square.
enum class WindowCornerSource : std::uint8_t {
    unknown,
    platform,
    layout_region,
    private_getter,
};

struct GUI_FORMS_API WindowCornerReport final {
    WindowCornerShape shape{WindowCornerShape::unknown};
    double radius{};
    bool complied{};
    bool top_square{};
    WindowCornerSource source{WindowCornerSource::unknown};

    friend bool operator==(const WindowCornerReport& left, const WindowCornerReport& right) noexcept {
        return left.shape == right.shape && left.radius == right.radius && left.complied == right.complied &&
               left.top_square == right.top_square && left.source == right.source;
    }
};

// The corner shapes a host may report (HostCapabilities::
// window_corner_shapes): Windows 11 rounds or squares, macOS reports square
// corners where they are (full screen, a borderless window) and nothing
// otherwise, X11 reports none.
struct GUI_FORMS_API WindowCornerShapes final {
    bool square{};
    bool rounded{};

    friend bool operator==(const WindowCornerShapes& left, const WindowCornerShapes& right) noexcept {
        return left.square == right.square && left.rounded == right.rounded;
    }
};

[[nodiscard]] GUI_FORMS_API const char* window_corner_preference_name(WindowCornerPreference preference) noexcept;
[[nodiscard]] GUI_FORMS_API const char* window_corner_shape_name(WindowCornerShape shape) noexcept;
[[nodiscard]] GUI_FORMS_API const char* window_corner_source_name(WindowCornerSource source) noexcept;

// A window's band as it stands (Window::band_snapshot): whether it is
// declared and reserved, the reserved widths (snapped, logical units), the
// colours the strips draw now, and the corners it follows.
struct GUI_FORMS_API WindowBandSnapshot final {
    bool declared{};
    // Declared, not hidden by full screen, and wider than 0 on some edge.
    bool reserved{};
    // The widest reserved edge; `edges` holds each.
    double width{};
    Insets edges{};
    WindowBandOptions options{};
    WindowBandAppearance appearance{};
    // The colour the strips draw now (the flash phase included).
    Color drawn{Color::rgba(0, 0, 0, 0)};
    // Whether a flash is running now (not stilled).
    bool flashing{};
    // Whether a cycle is running now (not stilled).
    bool cycling{};
    // The frequency a flash or cycle runs at after the limiter, in cycles
    // a second; 0 when neither runs.
    double frequency{};
    // Whether the strips draw a texture now.
    bool textured{};
    WindowCornerReport corners{};
    // The flash phase changes the strips have been damaged for, counted
    // from the window's creation.
    std::uint64_t flash_changes{};
    // The cycle frames the strips have been damaged for, counted from the
    // window's creation.
    std::uint64_t cycle_frames{};
    // Whether a march or a sweep moves now (not stilled), how far round it
    // is (a fraction of one cycle, in [0, 1)), and the frames it has
    // damaged the strips for, counted from the window's creation.
    bool moving{};
    double motion_phase{};
    std::uint64_t motion_frames{};
};

// What a window's shaping can rely on (Window::shaping_report; the owner's
// approval of 2026-11-13: the host capability reported). Every field is
// what the attached host says, never assumed:
// - `corners_reported`: the host shapes and reports the window's corners
//   (HostCapability::window_corners); without it the band draws square.
// - `square_corners` and `rounded_corners`: the shapes the host may report
//   (HostCapabilities::window_corner_shapes), whatever is asked.
// - `state_reported`: the host reports full screen and the changes that
//   may reshape corners (HostCapability::window_state_reports); without it
//   a band declared to hide in full screen never hides.
// - `native_border`: the host can draw the band's colour itself outside the
//   client area. No host can yet; the Windows 11 contract is written for
//   its host (docs/user/window-band.md).
struct GUI_FORMS_API WindowShapingReport final {
    bool host_attached{};
    bool corners_reported{};
    bool square_corners{};
    bool rounded_corners{};
    bool state_reported{};
    bool native_border{};
    bool full_screen{};
    WindowCornerPreference preference{WindowCornerPreference::system};
    WindowCornerReport corners{};

    friend bool operator==(const WindowShapingReport& left, const WindowShapingReport& right) noexcept {
        return left.host_attached == right.host_attached && left.corners_reported == right.corners_reported &&
               left.square_corners == right.square_corners && left.rounded_corners == right.rounded_corners &&
               left.state_reported == right.state_reported && left.native_border == right.native_border &&
               left.full_screen == right.full_screen && left.preference == right.preference &&
               left.corners == right.corners;
    }
};

} // namespace gui_forms
