#pragma once

// The control border (the active border study, phase 2; roadmap row 2.7): an
// appearance any control may carry, set at run time through
// Control::set_active_border. It is a solid rounded stroke painted after the
// control's own content, outside its box by default (in visual outsets, so
// it takes no layout space and is not hit), lit in one colour, flashing
// between two or cycling smoothly between them. It is presentation only: it
// says nothing to assistive technology, and the focus ring stays on its own
// path (owner answer 5). Textures are the window band's (answer 6).
//
// Safety is the engine's, not the caller's: resolve_active_border limits a
// flash or a cycle to maximum_active_border_frequency (three luminance
// transitions a second, WCAG 2.3.1), refuses to flash saturated red, and
// rests the border at its lit pose, steady, under reduced motion, with the
// application's ambient motion off, in an occluded window, and in high
// contrast, where it also takes the high-contrast colour and at least
// minimum_high_contrast_active_border_width.

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/scheduler.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"

#include <cstdint>
#include <stxlib/optional.hpp>

namespace gui_forms {

// How the border's colour moves: `steady` stays lit; `flashing` alternates
// lit and alternate, each held half a cycle; `cycling` blends between them
// along a cosine, starting lit.
enum class ActiveBorderMotion : std::uint8_t {
    steady,
    flashing,
    cycling,
};

// The bounds a border's numbers must keep (active_border_valid).
inline constexpr double maximum_active_border_width = 8.0;
inline constexpr double maximum_active_border_offset = 8.0;
inline constexpr double maximum_active_border_radius = 64.0;
// Cycles a second: one cycle is two luminance transitions, so 1.5 is three
// transitions a second.
inline constexpr double maximum_active_border_frequency = 1.5;
inline constexpr double minimum_high_contrast_active_border_width = 2.0;
// A cycling border repaints at this rate; a flashing one only at its
// transitions.
inline constexpr double active_border_cycling_frames_per_second = 30.0;

// What the program asks for. Lengths are logical units. `colour` empty is
// the theme's accent; `alternate` empty is the lit colour fully
// transparent, so a flash blinks the border off. `offset` is the gap
// between the control's box and the stroke's inner edge: negative draws
// inside the box. `frequency` is in cycles a second, for flashing and
// cycling only.
struct GUI_FORMS_API ActiveBorder final {
    stxlib::Optional<Color> colour{};
    stxlib::Optional<Color> alternate{};
    double width{2.0};
    double offset{1.0};
    double corner_radius{};
    ActiveBorderMotion motion{ActiveBorderMotion::steady};
    double frequency{1.0};

    friend constexpr bool operator==(const ActiveBorder& left, const ActiveBorder& right) noexcept {
        return left.colour == right.colour && left.alternate == right.alternate && left.width == right.width &&
               left.offset == right.offset && left.corner_radius == right.corner_radius &&
               left.motion == right.motion && left.frequency == right.frequency;
    }
};

// Whether `border` may be set: width finite in (0, maximum_active_border_width],
// offset finite within plus or minus maximum_active_border_offset, radius
// finite in [0, maximum_active_border_radius], frequency finite and not
// negative, and the motion one of the enumerators. A frequency above the
// limit is valid: the limiter lowers it.
[[nodiscard]] GUI_FORMS_API bool active_border_valid(const ActiveBorder& border) noexcept;

// Whether `colour` is a saturated red for the flash rule: its red channel is
// at least 0.8 of the sum of its channels and the colour is not transparent.
[[nodiscard]] GUI_FORMS_API bool active_border_saturated_red(Color colour) noexcept;

// What the window is like when the border paints.
struct GUI_FORMS_API ActiveBorderConditions final {
    // The theme's accent and the high-contrast stroke colour.
    Color theme_colour{};
    Color high_contrast_colour{};
    // Reduced motion, ambient motion off or an occluded window.
    bool rest{};
    bool high_contrast{};
};

// What the border paints after the limiter: the colours, the motion and its
// frequency (0 when steady), and the width.
struct GUI_FORMS_API ActiveBorderPose final {
    Color lit{};
    Color alternate{};
    ActiveBorderMotion motion{ActiveBorderMotion::steady};
    double frequency{};
    double width{};

    friend constexpr bool operator==(const ActiveBorderPose& left, const ActiveBorderPose& right) noexcept {
        return left.lit == right.lit && left.alternate == right.alternate && left.motion == right.motion &&
               left.frequency == right.frequency && left.width == right.width;
    }
};

// The border's pose under `conditions`, by the rules in this header's
// comment. A valid `border` is assumed; a frequency that is not above 0
// makes the border steady.
[[nodiscard]] GUI_FORMS_API ActiveBorderPose resolve_active_border(const ActiveBorder& border,
                                                                   const ActiveBorderConditions& conditions) noexcept;

// The colour `pose` shows `seconds` after its motion started (a negative or
// non-finite time is time 0): flashing is lit in the first half of each
// cycle, cycling mixes the channels by 0.5 - 0.5 cos(2 pi frequency t).
[[nodiscard]] GUI_FORMS_API Color active_border_colour_at(const ActiveBorderPose& pose, double seconds) noexcept;

// How often a moving pose must repaint: half a cycle for flashing, the
// cycling frame rate for cycling; zero when steady.
[[nodiscard]] GUI_FORMS_API FrameInterval active_border_interval(const ActiveBorderPose& pose) noexcept;

// How far a border reaches outside the control's box on every side: the
// offset plus the width, not below 0.
[[nodiscard]] GUI_FORMS_API double active_border_reach(const ActiveBorder& border) noexcept;

// The rectangle the stroke's centre line follows around a box of `size`,
// and the radius it uses there (the corner radius grown by the distance
// from the box to the centre line, not below 0).
struct GUI_FORMS_API ActiveBorderPath final {
    Rect rect{};
    double radius{};
};
[[nodiscard]] GUI_FORMS_API ActiveBorderPath active_border_path(const ActiveBorder& border, Size size) noexcept;

} // namespace gui_forms
