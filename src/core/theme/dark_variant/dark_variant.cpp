#include "dark_variant.hpp"

#include "gui_forms/theme/types/theme_types.hpp"

#include <cstddef>
#include <optional>

#include <algorithm>
#include <cmath>

namespace gui_forms::detail {

namespace {

// One lightness map, offset + slope * L, clamped to [minimum, maximum]
// (OKLab lightness, 0 to 1).
struct LightnessMap final {
    double offset{};
    double slope{};
    double minimum{};
    double maximum{1.0};
};

// The fitted maps (the dark studies against the light ones, OKLab
// lightness; the fit's error is in tests/dark_variant_tests.cpp).
// Surfaces rise with L (the light stays upper left); ink and lines fall
// with it.
constexpr LightnessMap chrome_surface_map{0.051, 0.304, 0.18, 0.46};
constexpr LightnessMap stock_surface_map{0.072, 0.177, 0.16, 0.30};
// Paper (L 1) goes to the studies' #0B0F0C, below every chrome face.
constexpr LightnessMap well_surface_map{-0.137, 0.300, 0.10, 0.20};
// The accent's faces sit below the studies' (at most 0.42 rather than
// 0.47) so that the Phosphor focus ring keeps 3:1 against them.
constexpr LightnessMap accent_surface_map{-0.950, 1.400, 0.28, 0.42};
constexpr LightnessMap ink_map{1.296, -1.077, 0.62, 0.95};
// Light ink keeps its lightness, within the band light text reads in.
constexpr LightnessMap light_ink_map{0.0, 1.0, 0.88, 0.97};
// Lines sit above the studies' (0.32 to 0.47) so that a boundary keeps
// 3:1 against the dark stock (the contrast gate wins over the sketches).
constexpr LightnessMap line_map{0.800, -0.334, 0.53, 0.62};
// Disabled ink: the ink map's value times this (the studies' #6F7D66 for
// #62799A).
constexpr double disabled_ink_scale = 0.84;
// Chroma: the light colour's, times this, at most `maximum_chroma`.
constexpr double chroma_scale = 0.5;
constexpr double maximum_chroma = 0.12;
// The hues chrome moves to (the studies' greens lie between 130 and 160
// degrees: ink near the Phosphor green's 134, surfaces greyer near 155).
constexpr double chrome_ink_hue = 132.0;
constexpr double chrome_surface_hue = 152.0;
// The complement's hue (#B36182).
constexpr double accent_hue = 355.5;
// Hues counted as chrome (the blues); others are signals and keep their
// hue.
constexpr double chrome_hue_low = 200.0;
constexpr double chrome_hue_high = 300.0;
// A colour this grey has no hue to keep, and takes this much of its new
// hue, as the studies' greys do.
constexpr double neutral_chroma = 0.012;
// Highlights keep their colour with alpha times this; shades become black
// with alpha raised by `shade_alpha_gain`, at most `shade_alpha_maximum`.
constexpr double highlight_alpha_scale = 0.16;
constexpr double shade_alpha_gain = 0.25;
constexpr double shade_alpha_maximum = 0.7;

// OKLCh: OKLab lightness, chroma and hue in degrees [0, 360).
struct Lch final {
    double lightness{};
    double chroma{};
    double hue_degrees{};
};

constexpr double pi = 3.14159265358979323846;

double linear_channel(std::uint8_t channel) noexcept {
    const double value = static_cast<double>(channel) / 255.0;
    const double result = value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
    return result;
}

double encoded_channel(double linear) noexcept {
    const double result = linear <= 0.0031308 ? linear * 12.92 : 1.055 * std::pow(linear, 1.0 / 2.4) - 0.055;
    return result;
}

// The linear sRGB channels of an OKLCh colour (unclamped).
struct LinearRgb final {
    double red{};
    double green{};
    double blue{};
};

LinearRgb linear_of(const Lch& colour) noexcept {
    const double radians = colour.hue_degrees * pi / 180.0;
    const double a = colour.chroma * std::cos(radians);
    const double b = colour.chroma * std::sin(radians);
    const double l_root = colour.lightness + 0.3963377774 * a + 0.2158037573 * b;
    const double m_root = colour.lightness - 0.1055613458 * a - 0.0638541728 * b;
    const double s_root = colour.lightness - 0.0894841775 * a - 1.2914855480 * b;
    const double l = l_root * l_root * l_root;
    const double m = m_root * m_root * m_root;
    const double s = s_root * s_root * s_root;
    LinearRgb result{};
    result.red = 4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s;
    result.green = -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s;
    result.blue = -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s;
    return result;
}

bool in_gamut(const LinearRgb& colour) noexcept {
    constexpr double tolerance = 1.0e-9;
    const bool result = colour.red >= -tolerance && colour.red <= 1.0 + tolerance &&
                        colour.green >= -tolerance && colour.green <= 1.0 + tolerance &&
                        colour.blue >= -tolerance && colour.blue <= 1.0 + tolerance;
    return result;
}

std::uint8_t channel_byte(double linear) noexcept {
    const double encoded = encoded_channel(std::clamp(linear, 0.0, 1.0));
    const double rounded = std::round(std::clamp(encoded, 0.0, 1.0) * 255.0);
    const std::uint8_t result = static_cast<std::uint8_t>(rounded);
    return result;
}

// The number of bisection steps that reduce an out-of-gamut chroma.
constexpr int gamut_steps = 24;

// `colour` in sRGB with `alpha`: when it lies outside the gamut, its
// chroma is reduced by bisection and its lightness and hue kept.
Color srgb_of(Lch colour, std::uint8_t alpha) noexcept {
    LinearRgb linear = linear_of(colour);
    if (!in_gamut(linear)) {
        double inside = 0.0;
        double outside = colour.chroma;
        for (int step = 0; step < gamut_steps; ++step) {
            const double middle = (inside + outside) * 0.5;
            Lch trial = colour;
            trial.chroma = middle;
            if (in_gamut(linear_of(trial))) {
                inside = middle;
            } else {
                outside = middle;
            }
        }
        colour.chroma = inside;
        linear = linear_of(colour);
    }
    const Color result =
        Color::rgba(channel_byte(linear.red), channel_byte(linear.green), channel_byte(linear.blue), alpha);
    return result;
}

Lch lch_of(Color colour) noexcept;

double mapped(const LightnessMap& map, double lightness) noexcept {
    const double value = map.offset + map.slope * lightness;
    const double result = std::clamp(value, map.minimum, map.maximum);
    return result;
}

bool chrome_hue(const Lch& colour) noexcept {
    if (colour.chroma < neutral_chroma) return true;
    const bool result = colour.hue_degrees >= chrome_hue_low && colour.hue_degrees <= chrome_hue_high;
    return result;
}

// `alpha` (0 to 255) as a fraction, times `scale` plus `gain`, clamped to
// [0, maximum] and rounded back to 0 to 255.
std::uint8_t scaled_alpha(std::uint8_t alpha, double scale, double gain, double maximum) noexcept {
    const double fraction = static_cast<double>(alpha) / 255.0 * scale + gain;
    const double clamped = std::clamp(fraction, 0.0, maximum);
    const double rounded = std::round(clamped * 255.0);
    const std::uint8_t result = static_cast<std::uint8_t>(rounded);
    return result;
}

// `source` at `lightness`, its chroma scaled, its hue moved to `hue` when
// it is chrome (or always, when `every_hue`), its alpha `alpha`.
Color remapped(const Lch& source, double lightness, double hue, bool every_hue, std::uint8_t alpha) noexcept {
    Lch target{};
    target.lightness = lightness;
    target.chroma = std::min(maximum_chroma, source.chroma * chroma_scale);
    target.chroma = std::max(target.chroma, source.chroma < neutral_chroma ? neutral_chroma : 0.0);
    target.hue_degrees = every_hue || chrome_hue(source) ? hue : source.hue_degrees;
    const Color result = srgb_of(target, alpha);
    return result;
}

Lch lch_of(Color colour) noexcept {
    const OklabColour lab = oklab_of(colour);
    Lch result{};
    result.lightness = lab.lightness;
    result.chroma = std::hypot(lab.a, lab.b);
    const double hue = std::atan2(lab.b, lab.a) * 180.0 / pi;
    result.hue_degrees = hue < 0.0 ? hue + 360.0 : hue;
    return result;
}

} // namespace

OklabColour oklab_of(Color colour) noexcept {
    const double red = linear_channel(colour.red);
    const double green = linear_channel(colour.green);
    const double blue = linear_channel(colour.blue);
    const double l = std::cbrt(0.4122214708 * red + 0.5363325363 * green + 0.0514459929 * blue);
    const double m = std::cbrt(0.2119034982 * red + 0.6806995451 * green + 0.1073969566 * blue);
    const double s = std::cbrt(0.0883024619 * red + 0.2817188376 * green + 0.6299787005 * blue);
    OklabColour result{};
    result.lightness = 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s;
    result.a = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s;
    result.b = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s;
    return result;
}

Color dark_colour(Color light, DarkColourClass kind) noexcept {
    const Lch source = lch_of(light);
    const double lightness = source.lightness;
    switch (kind) {
    case DarkColourClass::chrome_surface:
        return remapped(source, mapped(chrome_surface_map, lightness), chrome_surface_hue, false, light.alpha);
    case DarkColourClass::stock_surface:
        return remapped(source, mapped(stock_surface_map, lightness), chrome_surface_hue, false, light.alpha);
    case DarkColourClass::well_surface:
        return remapped(source, mapped(well_surface_map, lightness), chrome_surface_hue, false, light.alpha);
    case DarkColourClass::accent_surface:
        // Every hue of an accent family moves to the complement: the blue
        // File tab and the gold active tool alike.
        return remapped(source, mapped(accent_surface_map, lightness), accent_hue, true, light.alpha);
    case DarkColourClass::ink:
        return remapped(source, mapped(ink_map, lightness), chrome_ink_hue, false, light.alpha);
    case DarkColourClass::light_ink:
        return remapped(source, mapped(light_ink_map, lightness), chrome_ink_hue, false, light.alpha);
    case DarkColourClass::disabled_ink:
        return remapped(source, mapped(ink_map, lightness) * disabled_ink_scale, chrome_ink_hue, false,
                        light.alpha);
    case DarkColourClass::line:
        return remapped(source, mapped(line_map, lightness), chrome_surface_hue, false, light.alpha);
    case DarkColourClass::deep_line:
        return Color::rgba(phosphor_black.red, phosphor_black.green, phosphor_black.blue, light.alpha);
    case DarkColourClass::cue:
        return Color::rgba(phosphor_green.red, phosphor_green.green, phosphor_green.blue, light.alpha);
    case DarkColourClass::accent_line:
        return Color::rgba(phosphor_complement.red, phosphor_complement.green, phosphor_complement.blue,
                           light.alpha);
    case DarkColourClass::highlight:
        return Color::rgba(light.red, light.green, light.blue,
                           scaled_alpha(light.alpha, highlight_alpha_scale, 0.0, 1.0));
    case DarkColourClass::shade:
        return Color::rgba(0, 0, 0, scaled_alpha(light.alpha, 1.0, shade_alpha_gain, shade_alpha_maximum));
    }
    // A class outside the enumeration keeps its colour.
    return light;
}

} // namespace gui_forms::detail

namespace gui_forms {

namespace {

using detail::DarkColourClass;

// The face class a family's plates take (design language 5.8 rule 3): the
// stock of windows, panels, cards and tooltips; the wells of entry fields,
// choices and progress tracks (inset stays inset); the accent family; and
// chrome for every other family.
DarkColourClass face_class_of(ControlVisualRole family) noexcept {
    switch (family) {
    case ControlVisualRole::window:
    case ControlVisualRole::panel:
    case ControlVisualRole::card:
    case ControlVisualRole::tool_tip:
    case ControlVisualRole::notice: return DarkColourClass::stock_surface;
    case ControlVisualRole::choice:
    case ControlVisualRole::editor:
    case ControlVisualRole::progress: return DarkColourClass::well_surface;
    case ControlVisualRole::accent_button:
    case ControlVisualRole::badge: return DarkColourClass::accent_surface;
    case ControlVisualRole::button:
    case ControlVisualRole::command_button:
    case ControlVisualRole::menu_item:
    case ControlVisualRole::selection:
    case ControlVisualRole::link:
    case ControlVisualRole::visited_link:
    case ControlVisualRole::count: return DarkColourClass::chrome_surface;
    }
    // A family outside the enumeration darkens as chrome.
    return DarkColourClass::chrome_surface;
}

// The gold of an active tool (a selected action): hue between these
// degrees and chroma at least `gold_chroma`. A gold face darkens as the
// accent family (the complement), as the studies' active tool does.
constexpr double gold_hue_low = 60.0;
constexpr double gold_hue_high = 110.0;
constexpr double gold_chroma = 0.06;
// The lightness a face is taken to have when it has no opaque colour.
constexpr double default_face_lightness = 0.9;

double hue_of(const detail::OklabColour& lab) noexcept {
    const double hue = std::atan2(lab.b, lab.a) * 180.0 / 3.14159265358979323846;
    const double result = hue < 0.0 ? hue + 360.0 : hue;
    return result;
}

bool gold(Color colour) noexcept {
    const detail::OklabColour lab = detail::oklab_of(colour);
    const double chroma = std::hypot(lab.a, lab.b);
    const double hue = hue_of(lab);
    const bool result = chroma >= gold_chroma && hue >= gold_hue_low && hue <= gold_hue_high;
    return result;
}

// The colour a material's face shows first: its first opaque solid colour
// or gradient stop, if any.
const Color* first_face_colour(const SurfaceMaterial& material) noexcept {
    for (const MaterialFillLayer& layer : material.fills) {
        if (layer.kind == MaterialFillKind::solid && layer.color.alpha == 255) return &layer.color;
        const bool gradient = layer.kind == MaterialFillKind::linear_gradient ||
                              layer.kind == MaterialFillKind::radial_gradient ||
                              layer.kind == MaterialFillKind::conic_gradient;
        if (!gradient) continue;
        for (const GradientStop& stop : layer.stops) {
            if (stop.color.alpha == 255) return &stop.color;
        }
    }
    return nullptr;
}

// How one recipe's colours darken: its face class, the face's lightness
// (a translucent layer, keyline or inset shadow lighter than it is a
// highlight, darker a shade), and its edge class.
struct DarkRecipePlan final {
    DarkColourClass face{DarkColourClass::chrome_surface};
    DarkColourClass edge{DarkColourClass::line};
    double face_lightness{default_face_lightness};
};

DarkRecipePlan plan_of(const SurfaceMaterial& material, ControlVisualRole family) noexcept {
    DarkRecipePlan plan{};
    plan.face = face_class_of(family);
    const Color* const face = first_face_colour(material);
    if (face != nullptr) {
        plan.face_lightness = detail::oklab_of(*face).lightness;
        if (gold(*face)) plan.face = DarkColourClass::accent_surface;
    }
    if (plan.face == DarkColourClass::accent_surface) plan.edge = DarkColourClass::accent_line;
    return plan;
}

// A relief colour's class: lighter than the face, a highlight; otherwise a
// shade.
DarkColourClass relief_class(Color colour, const DarkRecipePlan& plan) noexcept {
    const bool lighter = detail::oklab_of(colour).lightness > plan.face_lightness;
    const DarkColourClass result = lighter ? DarkColourClass::highlight : DarkColourClass::shade;
    return result;
}

// A fill colour's class: an opaque one is the face; a translucent one, a
// highlight or a shade by its lightness; a screen layer lights, a multiply
// layer shades.
DarkColourClass fill_class(Color colour, MaterialBlendMode blend, const DarkRecipePlan& plan) noexcept {
    if (blend == MaterialBlendMode::screen) return DarkColourClass::highlight;
    if (blend == MaterialBlendMode::multiply) return DarkColourClass::shade;
    if (colour.alpha == 255) return plan.face;
    const DarkColourClass result = relief_class(colour, plan);
    return result;
}

void darken_fill(MaterialFillLayer& layer, const DarkRecipePlan& plan) noexcept {
    switch (layer.kind) {
    case MaterialFillKind::solid:
        layer.color = detail::dark_colour(layer.color, fill_class(layer.color, layer.blend, plan));
        return;
    case MaterialFillKind::linear_gradient:
    case MaterialFillKind::radial_gradient:
    case MaterialFillKind::conic_gradient:
        for (GradientStop& stop : layer.stops) {
            stop.color = detail::dark_colour(stop.color, fill_class(stop.color, layer.blend, plan));
        }
        return;
    case MaterialFillKind::image:
    case MaterialFillKind::procedural: return; // Content, not chrome: kept.
    default: return; // A kind outside the enumeration is kept as it is.
    }
}

void darken_border(std::optional<MaterialBorder>& border, const DarkRecipePlan& plan) noexcept {
    if (!border) return;
    (*border).color = detail::dark_colour((*border).color, plan.edge);
}

void darken_planned(SurfaceMaterial& material, const DarkRecipePlan& plan) noexcept {
    for (MaterialFillLayer& layer : material.fills) darken_fill(layer, plan);
    for (MaterialShadow& shadow : material.shadows) {
        const DarkColourClass kind = shadow.inset ? relief_class(shadow.color, plan) : DarkColourClass::shade;
        shadow.color = detail::dark_colour(shadow.color, kind);
    }
    for (MaterialGlow& glow : material.glows) {
        glow.color = detail::dark_colour(glow.color, DarkColourClass::highlight);
    }
    darken_border(material.border, plan);
    darken_border(material.border_edges.top, plan);
    darken_border(material.border_edges.right, plan);
    darken_border(material.border_edges.bottom, plan);
    darken_border(material.border_edges.left, plan);
    for (MaterialKeyline& line : material.keylines) {
        line.color = detail::dark_colour(line.color, relief_class(line.color, plan));
    }
    if (material.drop_shadow) {
        (*material.drop_shadow).color = detail::dark_colour((*material.drop_shadow).color, DarkColourClass::shade);
    }
}

// Text's class: light text on a darker face stays light; disabled text
// dims; other text inverts as ink.
DarkColourClass text_class(Color text, const DarkRecipePlan& plan, bool disabled) noexcept {
    if (disabled) return DarkColourClass::disabled_ink;
    const bool light = detail::oklab_of(text).lightness > plan.face_lightness;
    const DarkColourClass result = light ? DarkColourClass::light_ink : DarkColourClass::ink;
    return result;
}

ControlVisualRecipe dark_recipe(const ControlVisualRecipe& light, ControlVisualRole family, bool disabled) {
    const DarkRecipePlan plan = plan_of(light.material, family);
    ControlVisualRecipe result = light;
    darken_planned(result.material, plan);
    result.text = detail::dark_colour(light.text, text_class(light.text, plan, disabled));
    result.muted_text = detail::dark_colour(light.muted_text, text_class(light.muted_text, plan, disabled));
    result.glyph = detail::dark_colour(light.glyph, text_class(light.glyph, plan, disabled));
    if (light.fill_text) {
        const Color over_fill = *light.fill_text;
        result.fill_text = detail::dark_colour(over_fill, text_class(over_fill, plan, disabled));
    }
    result.focus_ring = detail::dark_colour(light.focus_ring, DarkColourClass::cue);
    result.default_ring = detail::dark_colour(light.default_ring, DarkColourClass::cue);
    if (result.text_shadow) {
        ControlTextShadow& shadow = *result.text_shadow;
        shadow.color = detail::dark_colour(shadow.color, relief_class(shadow.color, plan));
    }
    return result;
}

} // namespace

namespace {

// A role's class (design language 5.4): its background side a face (paper
// a well, the stock the stock, gold and the accent the accent, other
// grounds chrome), its foreground side ink; lines, cues, highlights and
// shades by their own classes.
DarkColourClass role_class(ColourOrigin origin, ControlVisualRole family) noexcept {
    const bool foreground = origin.side == ColourRoleSide::foreground;
    switch (origin.role) {
    case ColourRole::ink_on_paper:
    case ColourRole::muted_ink_on_paper:
    case ColourRole::signal_on_paper:
    case ColourRole::success_on_paper:
    case ColourRole::command_link_ink_on_paper:
        return foreground ? DarkColourClass::ink : DarkColourClass::well_surface;
    case ColourRole::ink_on_near_white_stock:
        return foreground ? DarkColourClass::ink : DarkColourClass::stock_surface;
    case ColourRole::ink_on_gold:
        return foreground ? DarkColourClass::light_ink : DarkColourClass::accent_surface;
    case ColourRole::on_accent_on_accent:
        return foreground ? DarkColourClass::light_ink : DarkColourClass::accent_surface;
    case ColourRole::disabled_ink_on_chrome:
        return foreground ? DarkColourClass::disabled_ink : DarkColourClass::chrome_surface;
    case ColourRole::disabled_ink_on_paper:
        return foreground ? DarkColourClass::disabled_ink : DarkColourClass::well_surface;
    case ColourRole::ink_on_soft_paper:
    case ColourRole::ink_on_chrome:
    case ColourRole::ink_on_chrome_low:
    case ColourRole::selection_ink_on_selection:
    case ColourRole::pending_ink_on_tint:
    case ColourRole::invalid_ink_on_tint:
        return foreground ? DarkColourClass::ink : DarkColourClass::chrome_surface;
    case ColourRole::chrome_highlight: return DarkColourClass::highlight;
    case ColourRole::chrome:
    case ColourRole::chrome_middle:
    case ColourRole::chrome_low: return DarkColourClass::chrome_surface;
    case ColourRole::ordinary_line: return DarkColourClass::line;
    case ColourRole::dark_line: return DarkColourClass::deep_line;
    case ColourRole::shadow: return DarkColourClass::shade;
    case ColourRole::selection_line:
    case ColourRole::focus: return DarkColourClass::cue;
    case ColourRole::phosphor_on_instrument_black:
    case ColourRole::count:
    default: break; // Falls to the face class below.
    }
    // The instrument's phosphor on black is dark already: its face class
    // stands for a role outside the enumeration too.
    return face_class_of(family);
}

// A literal colour darker than this OKLab lightness is taken as ink.
constexpr double literal_ink_lightness = 0.5;

} // namespace

namespace detail {

void darken_material(SurfaceMaterial& material, ControlVisualRole family) noexcept {
    const DarkRecipePlan plan = plan_of(material, family);
    darken_planned(material, plan);
}

Color dark_parameter_colour(Color light, ColourOrigin origin, ControlVisualRole family) noexcept {
    if (origin.source == ColourSource::role) {
        if (origin.role == ColourRole::phosphor_on_instrument_black) return light;
        return dark_colour(light, role_class(origin, family));
    }
    const bool ink = oklab_of(light).lightness < literal_ink_lightness;
    const DarkColourClass kind = ink ? DarkColourClass::ink : face_class_of(family);
    return dark_colour(light, kind);
}

} // namespace detail

ControlRoleRecipes derive_dark_recipes(ControlRoleRecipes recipes, ControlVisualRole family) {
    for (std::size_t index = 0; index < control_surface_state_count; ++index) {
        const bool disabled = index == static_cast<std::size_t>(ControlSurfaceState::disabled);
        recipes.dark[index] = dark_recipe(recipes.ordinary[index], family, disabled);
        recipes.dark_selected[index] = dark_recipe(recipes.selected[index], family, disabled);
    }
    return recipes;
}

} // namespace gui_forms
