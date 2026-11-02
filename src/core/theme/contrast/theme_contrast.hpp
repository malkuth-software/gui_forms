#pragma once

// The contrast report (stage T4 phase 1; design language 5.7). For every
// recipe family, surface state, selection and presentation (ordinary and
// GUI.Forms' own high contrast) of a theme, it measures the pairs the
// specification gates: text and muted text on their face (4.5:1; 3:1 when
// disabled; 7:1 for all text in high contrast), a glyph on its face, the
// control's boundary against the surface around it, and the focus ring
// against both its neighbours (3:1 each).
//
// It reads the theme's built-in recipes (Theme::resolve(role, context)),
// not a control's rules, and measures colours, not pixels:
// - a face is sampled at 17 evenly spaced positions of its gradients, every
//   fill layer evaluated at the same position and composited in paint order
//   over the surface around it, so a pair's ratio is its worst over the
//   positions. That is exact for today's vertical recipe gradients and an
//   approximation for layers of different directions; the pixel measurement
//   of T4 phase 2 (where text actually sits) replaces it;
// - the surface around a control is the panel family's resting face over the
//   window family's, over white paper, in the same presentation;
// - a face holding an image or procedural layer is not measured.
// The rows are data for the T4 gate and the stage record; nothing here
// changes how anything is drawn.

#include "gui_forms/theme/theme/theme.hpp"
#include "gui_forms/theme/types/theme_types.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace gui_forms::detail {

// The WCAG 2 relative luminance of `colour`'s sRGB channels, from 0 (black)
// to 1 (white). Alpha is ignored: composite a translucent colour first.
[[nodiscard]] double relative_luminance(Color colour) noexcept;

// The WCAG 2 contrast ratio of two colours, the lighter's luminance plus
// 0.05 over the darker's: from 1 (equal) to 21 (black and white). Alpha is
// ignored.
[[nodiscard]] double contrast_ratio(Color first, Color second) noexcept;

// How a layer meets what is under it, as the material painter blends it.
enum class ContrastBlend : std::uint8_t { normal, multiply, screen };

// `top` drawn over the opaque `ground` in `blend`, with `top`'s straight
// alpha as coverage; each channel rounded to the nearest 8-bit value. The
// result is opaque.
[[nodiscard]] Color composite_over(Color top, Color ground, ContrastBlend blend) noexcept;

// What a row measures.
enum class ContrastPair : std::uint8_t {
    // The recipe's text on its face.
    text,
    // The recipe's muted text on its face.
    muted_text,
    // The recipe's glyph colour (a mark, a state cue) on its face.
    glyph,
    // The control's edge (its border, else its face) against the surface
    // around it.
    boundary,
    // The focus ring against the face it surrounds.
    focus_inner,
    // The focus ring against the surface around the control.
    focus_outer,
};

enum class ContrastVerdict : std::uint8_t { pass, fail, not_measured };

// One measured pair. `foreground` and `background` are the pair at the
// worst position, both opaque; `ratio` is theirs. A row not measured holds
// a ratio of 0.
struct ContrastRow final {
    ControlVisualRole family{ControlVisualRole::window};
    ControlSurfaceState state{ControlSurfaceState::normal};
    bool selected{};
    bool high_contrast{};
    ContrastPair pair{ContrastPair::text};
    Color foreground{};
    Color background{};
    double ratio{};
    double minimum{};
    ContrastVerdict verdict{ContrastVerdict::not_measured};
};

struct ContrastCounts final {
    std::size_t passed{};
    std::size_t failed{};
    std::size_t not_measured{};
};

// Clears `rows` and fills it with `theme`'s report: for each presentation
// (ordinary, then high contrast), family, surface state and selection (not
// selected, then selected), the pairs that apply to that family and state,
// in ContrastPair order. Disabled controls measure their text only;
// boundaries are gated for the families that draw a control's own edge
// (button, accent button, choice, editor, card, tool tip, selection) in
// every state, and for command buttons and menu items while hot or pressed
// (flat at rest, design language 12.3); focus is gated for the families a
// focused control uses.
void measure_theme_contrast(const Theme& theme, std::vector<ContrastRow>& rows);

[[nodiscard]] ContrastCounts count_contrast_verdicts(std::span<const ContrastRow> rows) noexcept;

[[nodiscard]] std::string_view contrast_pair_name(ContrastPair pair) noexcept;
[[nodiscard]] std::string_view contrast_verdict_name(ContrastVerdict verdict) noexcept;
[[nodiscard]] std::string_view control_surface_state_name(ControlSurfaceState state) noexcept;

} // namespace gui_forms::detail
