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
#include "gui_forms/engine/theme_rules/theme_rules.hpp"
#include "gui_forms/surface_material/types/surface_material_types.hpp"
#include "gui_forms/theme/types/theme_types.hpp"
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
    // Light text on a dark face (white on the accent): it stays light.
    light_ink,
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
    // A state signal whose hue is its meaning (a lamp on or in error, a
    // success or error mark; cp31): its hue and chroma are kept and its
    // lightness moved into the band signals read in on the dark stock. A
    // colour too grey to carry a state (an unlit lamp) darkens as an unlit
    // chrome face, grey.
    signal,
    // A pale state tint (a rejected drop's ground; cp31): its hue kept at
    // low chroma, darkened as the stock.
    signal_tint,
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

// `material`, a light material of a part of family `family`, darkened in
// place by the rule derive_dark_recipes applies to a recipe's material:
// opaque faces by the family's face class (gold as the accent), edges as
// lines, and translucent layers, keylines and inset shadows as highlights
// or shades by their lightness against the face. Images and procedural
// layers are kept.
void darken_material(SurfaceMaterial& material, ControlVisualRole family) noexcept;

// A colour parameter of a primitive, declared in light terms, as the dark
// variant draws it: by its colour role where it names one (a role's
// background as a face, its foreground as ink, its lines, cues, highlights
// and shades by their own classes); a literal colour as ink when it is
// darker than mid-grey and as the family's face otherwise.
[[nodiscard]] Color dark_parameter_colour(Color light, ColourOrigin origin, ControlVisualRole family) noexcept;

} // namespace gui_forms::detail
