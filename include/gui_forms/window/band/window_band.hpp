#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/types.hpp"

#include <cstdint>
#include <optional>

// The window outer band (owner, 2026-11-08: "a thin effect"; the active
// border study, section 2.2, and the owner's answers of 2026-11-07). A thin
// strip owned by a top-level window along the edges of its client area,
// drawn above every paint plane (overlays included) and hitting nothing.
//
// Reservation is static: declaring the band reserves its width on every
// side, the root is measured against the client size less the band (and
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

// A window's band declaration (Window::declare_band).
struct GUI_FORMS_API WindowBandOptions final {
    // The width asked for, in logical units, before it is snapped to whole
    // device pixels: 0 takes the theme's `band-width`, or one logical unit
    // where the theme declares none. Finite, from 0 to maximum_band_width.
    double width{};
    WindowBandFullScreen full_screen{WindowBandFullScreen::stays};

    friend bool operator==(const WindowBandOptions& left, const WindowBandOptions& right) noexcept {
        return left.width == right.width && left.full_screen == right.full_screen;
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
enum class WindowBandState : std::uint8_t {
    rest,
    lit,
    flashing,
};

// The flash limiter (study section 5, WCAG 2.3.1): at most this many
// luminance transitions a second, whatever the theme or the application
// asks: a flashing band changes colour once every half second.
inline constexpr unsigned maximum_band_flash_transitions = 2U;

struct GUI_FORMS_API WindowBandAppearance final {
    WindowBandState state{WindowBandState::rest};
    // The application's colours, each replacing the theme's while set.
    // Ignored under high contrast, where the band takes the high-contrast
    // palette.
    std::optional<Color> rest_colour{};
    std::optional<Color> lit_colour{};

    friend bool operator==(const WindowBandAppearance& left, const WindowBandAppearance& right) noexcept {
        return left.state == right.state && left.rest_colour == right.rest_colour &&
               left.lit_colour == right.lit_colour;
    }
};

// Whether `options` is a declaration the window accepts: a finite width
// from 0 to maximum_band_width and a known full-screen choice.
[[nodiscard]] GUI_FORMS_API bool window_band_options_valid(const WindowBandOptions& options) noexcept;
// Whether `appearance` names a known state.
[[nodiscard]] GUI_FORMS_API bool window_band_appearance_valid(const WindowBandAppearance& appearance) noexcept;

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
struct GUI_FORMS_API WindowCornerReport final {
    WindowCornerShape shape{WindowCornerShape::unknown};
    double radius{};
    bool complied{};

    friend bool operator==(const WindowCornerReport& left, const WindowCornerReport& right) noexcept {
        return left.shape == right.shape && left.radius == right.radius && left.complied == right.complied;
    }
};

[[nodiscard]] GUI_FORMS_API const char* window_corner_preference_name(WindowCornerPreference preference) noexcept;
[[nodiscard]] GUI_FORMS_API const char* window_corner_shape_name(WindowCornerShape shape) noexcept;

// A window's band as it stands (Window::band_snapshot): whether it is
// declared and reserved, the reserved width (snapped, logical units), the
// colours the strips draw now, and the corners it follows.
struct GUI_FORMS_API WindowBandSnapshot final {
    bool declared{};
    // Declared and not hidden by full screen.
    bool reserved{};
    double width{};
    WindowBandOptions options{};
    WindowBandAppearance appearance{};
    // The colour the strips draw now (the flash phase included).
    Color drawn{Color::rgba(0, 0, 0, 0)};
    // Whether a flash is running now (not stilled).
    bool flashing{};
    WindowCornerReport corners{};
    // The flash phase changes the strips have been damaged for, counted
    // from the window's creation.
    std::uint64_t flash_changes{};
};

} // namespace gui_forms
