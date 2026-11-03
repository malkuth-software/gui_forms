#pragma once
// The dark colour rule (stage T4 phase 5a; design language 5.8 rule 3,
// 12.3 and 12.4; rendering reference 6). The default's dark variant is
// derived from its light theme by rule, never authored colour by colour:
// each light colour is mapped by the class of thing it paints, in OKLCh.
//
// - Surfaces keep their order (a gradient's lit top stays lighter than
//   its foot: the light stays upper left) and are compressed into a dark
//   band. Wells, lighter than the chrome in light, go darker than it
//   (inset stays inset).
// - Ink and lines invert, so they stay legible on the dark surfaces.
// - Chrome hues (the blues) move to the Phosphor pair's green (#82B361)
//   at reduced chroma; the accent and active-tool families move to its
//   complement (#B36182). Signal hues (amber, red, green) keep their hue.
// - Cues (focus, selection lines, checks, the default ring) become the
//   Phosphor green itself; an accent's edge becomes the complement.
// - Highlights dim and shades deepen toward black: relief keeps its
//   sides.
//
// The parameters are fitted to the accepted dark studies' tokens
// (theme_studies_source.html `.t-dark` against `.t-default`); the fit's
// error is recorded by tests/dark_variant_tests.cpp. Alpha is kept,
// except that highlights and shades change theirs as stated.
#include "gui_forms/types/paint_types/paint_types.hpp"

#include <cstdint>

namespace gui_forms::detail {

// What a light colour paints, which decides how it darkens.
enum class DarkColourClass : std::uint8_t {
    // A chrome face: buttons, strips, tabs, rails, hot and selected plates.
    chrome_surface,
    // The near-white stock of windows, panels and disabled faces.
    stock_surface,
    // A well: an entry field's or a list's paper.
    well_surface,
    // A face of the accent or active-tool families (the File tab, gold).
    accent_surface,
    // Text and glyphs.
    ink,
    // Disabled text: the ink rule, dimmed (M104: reduced, still visible).
    disabled_ink,
    // An ordinary edge or divider.
    line,
    // A dark outer edge (a frame, a tab's outer keyline): black.
    deep_line,
    // A cue: focus, a selection line, a check, the default ring.
    cue,
    // An accent or active tool's edge.
    accent_line,
    // A light relief line or sheen, usually white with alpha.
    highlight,
    // A dark relief line or a shadow.
    shade,
};

// The Phosphor pair (design language 5.2 pair B) and its complement.
inline constexpr Color phosphor_black = Color::rgba(0, 0, 0);
inline constexpr Color phosphor_green = Color::rgba(130, 179, 97);
inline constexpr Color phosphor_complement = Color::rgba(179, 97, 130);

// A colour in OKLab (Bjorn Ottosson's), from its sRGB channels; alpha is
// ignored. The core keeps its own conversion: GUI.Drawing's lives in a
// library the core does not link.
struct OklabColour final {
    double lightness{};
    double a{};
    double b{};
};
[[nodiscard]] OklabColour oklab_of(Color colour) noexcept;

// `light` as the dark variant paints it, for a colour of class `kind`.
// Deterministic; the result is in the sRGB gamut (chroma is reduced, never
// lightness or hue, when the mapped colour lies outside it).
[[nodiscard]] Color dark_colour(Color light, DarkColourClass kind) noexcept;

} // namespace gui_forms::detail
