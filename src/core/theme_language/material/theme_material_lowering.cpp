#include "theme_material_lowering.hpp"

#include "../source_text/theme_source_text.hpp"

#include "gui_forms/typography/app_fonts/app_fonts.hpp"
#include "gui_forms/typography/font_families/font_families.hpp"

#include "gui_forms/engine/authored_gradient/authored_gradient.hpp"
#include "gui_forms/engine/engine_math/engine_math.hpp"
#include "gui_forms/surface_material/colour_matrix/colour_matrix.hpp"
#include "gui_forms/surface_material/painting/surface_material_painting.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// A filter's blur and a drop shadow's colour are part of the material the
// engine bakes, so they are computed without fused multiply-add: the same
// sheet gives the same material on every host.
#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace gui_forms::theme_language {

namespace {

// -- the names --------------------------------------------------------------------

struct NamedMaterialProperty final {
    std::string_view name{};
    MaterialProperty property{MaterialProperty::background};
};

// Sorted by name.
constexpr std::array<NamedMaterialProperty, 99> material_properties{{
    {.name = "background", .property = MaterialProperty::background},
    {.name = "background-blend-mode", .property = MaterialProperty::background_blend_mode},
    {.name = "background-color", .property = MaterialProperty::background_color},
    {.name = "background-image", .property = MaterialProperty::background_image},
    {.name = "background-position", .property = MaterialProperty::background_position},
    {.name = "background-repeat", .property = MaterialProperty::background_repeat},
    {.name = "background-size", .property = MaterialProperty::background_size},
    {.name = "background-texture", .property = MaterialProperty::background_texture},
    {.name = "border", .property = MaterialProperty::border},
    {.name = "border-bottom", .property = MaterialProperty::border_bottom},
    {.name = "border-bottom-color", .property = MaterialProperty::border_bottom_color},
    {.name = "border-bottom-left-radius", .property = MaterialProperty::border_bottom_left_radius},
    {.name = "border-bottom-right-radius", .property = MaterialProperty::border_bottom_right_radius},
    {.name = "border-bottom-style", .property = MaterialProperty::border_bottom_style},
    {.name = "border-bottom-width", .property = MaterialProperty::border_bottom_width},
    {.name = "border-color", .property = MaterialProperty::border_color},
    {.name = "border-image", .property = MaterialProperty::border_image},
    {.name = "border-left", .property = MaterialProperty::border_left},
    {.name = "border-left-color", .property = MaterialProperty::border_left_color},
    {.name = "border-left-style", .property = MaterialProperty::border_left_style},
    {.name = "border-left-width", .property = MaterialProperty::border_left_width},
    {.name = "border-radius", .property = MaterialProperty::border_radius},
    {.name = "border-right", .property = MaterialProperty::border_right},
    {.name = "border-right-color", .property = MaterialProperty::border_right_color},
    {.name = "border-right-style", .property = MaterialProperty::border_right_style},
    {.name = "border-right-width", .property = MaterialProperty::border_right_width},
    {.name = "border-style", .property = MaterialProperty::border_style},
    {.name = "border-top", .property = MaterialProperty::border_top},
    {.name = "border-top-color", .property = MaterialProperty::border_top_color},
    {.name = "border-top-left-radius", .property = MaterialProperty::border_top_left_radius},
    {.name = "border-top-right-radius", .property = MaterialProperty::border_top_right_radius},
    {.name = "border-top-style", .property = MaterialProperty::border_top_style},
    {.name = "border-top-width", .property = MaterialProperty::border_top_width},
    {.name = "border-width", .property = MaterialProperty::border_width},
    {.name = "box-shadow", .property = MaterialProperty::box_shadow},
    {.name = "clip-path", .property = MaterialProperty::clip_path},
    {.name = "color", .property = MaterialProperty::color},
    {.name = "corner-radius", .property = MaterialProperty::corner_radius},
    {.name = "default-outline", .property = MaterialProperty::default_outline},
    {.name = "edge-shadow", .property = MaterialProperty::edge_shadow},
    {.name = "fill-color", .property = MaterialProperty::fill_color},
    {.name = "fill-text-color", .property = MaterialProperty::fill_text_color},
    {.name = "filter", .property = MaterialProperty::filter},
    {.name = "flare", .property = MaterialProperty::flare},
    {.name = "flow", .property = MaterialProperty::flow},
    {.name = "flow-spacing", .property = MaterialProperty::flow_spacing},
    {.name = "flow-speed", .property = MaterialProperty::flow_speed},
    {.name = "font-family", .property = MaterialProperty::font_family},
    {.name = "font-size", .property = MaterialProperty::font_size},
    {.name = "font-style", .property = MaterialProperty::font_style},
    {.name = "font-weight", .property = MaterialProperty::font_weight},
    {.name = "gap", .property = MaterialProperty::gap},
    {.name = "glow", .property = MaterialProperty::glow},
    {.name = "glow-placement", .property = MaterialProperty::glow_placement},
    {.name = "glyph-color", .property = MaterialProperty::glyph_color},
    {.name = "gradient-dither", .property = MaterialProperty::gradient_dither},
    {.name = "grid-band", .property = MaterialProperty::grid_band},
    {.name = "grid-spacing", .property = MaterialProperty::grid_spacing},
    {.name = "halo", .property = MaterialProperty::halo},
    {.name = "height", .property = MaterialProperty::height},
    {.name = "keylines", .property = MaterialProperty::keylines},
    {.name = "letter-spacing", .property = MaterialProperty::letter_spacing},
    {.name = "margin-bottom", .property = MaterialProperty::margin_bottom},
    {.name = "margin-top", .property = MaterialProperty::margin_top},
    {.name = "mask-composite", .property = MaterialProperty::mask_composite},
    {.name = "mask-image", .property = MaterialProperty::mask_image},
    {.name = "mask-mode", .property = MaterialProperty::mask_mode},
    {.name = "mask-position", .property = MaterialProperty::mask_position},
    {.name = "mask-repeat", .property = MaterialProperty::mask_repeat},
    {.name = "mask-size", .property = MaterialProperty::mask_size},
    {.name = "min-width", .property = MaterialProperty::min_width},
    {.name = "muted-color", .property = MaterialProperty::muted_color},
    {.name = "needle-shape", .property = MaterialProperty::needle_shape},
    {.name = "outline", .property = MaterialProperty::outline},
    {.name = "outline-color", .property = MaterialProperty::outline_color},
    {.name = "outline-offset", .property = MaterialProperty::outline_offset},
    {.name = "outline-style", .property = MaterialProperty::outline_style},
    {.name = "outline-width", .property = MaterialProperty::outline_width},
    {.name = "padding-inline", .property = MaterialProperty::padding_inline},
    {.name = "reflection", .property = MaterialProperty::reflection},
    {.name = "scrim-color", .property = MaterialProperty::scrim_color},
    {.name = "seat", .property = MaterialProperty::seat},
    {.name = "stripe-angle", .property = MaterialProperty::stripe_angle},
    {.name = "stripe-color", .property = MaterialProperty::stripe_color},
    {.name = "stripe-direction", .property = MaterialProperty::stripe_direction},
    {.name = "stripe-gloss", .property = MaterialProperty::stripe_gloss},
    {.name = "stripe-ground", .property = MaterialProperty::stripe_ground},
    {.name = "stripe-lean", .property = MaterialProperty::stripe_lean},
    {.name = "stripe-pitch", .property = MaterialProperty::stripe_pitch},
    {.name = "stripe-ratio", .property = MaterialProperty::stripe_ratio},
    {.name = "stripe-reduced-motion", .property = MaterialProperty::stripe_reduced_motion},
    {.name = "stripe-speed", .property = MaterialProperty::stripe_speed},
    {.name = "stroke-color", .property = MaterialProperty::stroke_color},
    {.name = "stroke-dash", .property = MaterialProperty::stroke_dash},
    {.name = "stroke-width", .property = MaterialProperty::stroke_width},
    {.name = "surface-finish", .property = MaterialProperty::surface_finish},
    {.name = "taper", .property = MaterialProperty::taper},
    {.name = "text-shadow", .property = MaterialProperty::text_shadow},
    {.name = "transform", .property = MaterialProperty::transform},
}};

// The recipe's longhands (P6f), each the one recipe field it sets.
struct RecipeLonghand final {
    MaterialProperty property{MaterialProperty::color};
    RecipeField field{RecipeField::text};
};

constexpr std::array<RecipeLonghand, 41> recipe_longhands{{
    {.property = MaterialProperty::height, .field = RecipeField::block_size},
    {.property = MaterialProperty::min_width, .field = RecipeField::minimum_inline_size},
    {.property = MaterialProperty::padding_inline, .field = RecipeField::padding_inline},
    {.property = MaterialProperty::gap, .field = RecipeField::gap},
    {.property = MaterialProperty::margin_top, .field = RecipeField::margin_block_start},
    {.property = MaterialProperty::margin_bottom, .field = RecipeField::margin_block_end},
    {.property = MaterialProperty::color, .field = RecipeField::text},
    {.property = MaterialProperty::outline_color, .field = RecipeField::outline_colour},
    {.property = MaterialProperty::outline_offset, .field = RecipeField::outline_offset},
    {.property = MaterialProperty::outline_style, .field = RecipeField::outline_style},
    {.property = MaterialProperty::outline_width, .field = RecipeField::outline_width},
    {.property = MaterialProperty::text_shadow, .field = RecipeField::text_shadow},
    {.property = MaterialProperty::font_family, .field = RecipeField::font_family},
    {.property = MaterialProperty::font_size, .field = RecipeField::font_size},
    {.property = MaterialProperty::font_style, .field = RecipeField::font_style},
    {.property = MaterialProperty::font_weight, .field = RecipeField::font_weight},
    {.property = MaterialProperty::letter_spacing, .field = RecipeField::letter_spacing},
    {.property = MaterialProperty::muted_color, .field = RecipeField::muted_text},
    {.property = MaterialProperty::glyph_color, .field = RecipeField::glyph},
    {.property = MaterialProperty::fill_text_color, .field = RecipeField::fill_text},
    {.property = MaterialProperty::needle_shape, .field = RecipeField::needle_shape},
    {.property = MaterialProperty::scrim_color, .field = RecipeField::scrim_colour},
    {.property = MaterialProperty::stroke_width, .field = RecipeField::stroke_width},
    {.property = MaterialProperty::stripe_pitch, .field = RecipeField::stripe_pitch},
    {.property = MaterialProperty::stripe_ratio, .field = RecipeField::stripe_ratio},
    {.property = MaterialProperty::stripe_angle, .field = RecipeField::stripe_angle},
    {.property = MaterialProperty::stripe_lean, .field = RecipeField::stripe_lean},
    {.property = MaterialProperty::stripe_direction, .field = RecipeField::stripe_direction},
    {.property = MaterialProperty::stripe_speed, .field = RecipeField::stripe_speed},
    {.property = MaterialProperty::stripe_color, .field = RecipeField::stripe_colour},
    {.property = MaterialProperty::stripe_ground, .field = RecipeField::stripe_ground_colour},
    {.property = MaterialProperty::stripe_gloss, .field = RecipeField::stripe_gloss},
    {.property = MaterialProperty::stripe_reduced_motion, .field = RecipeField::stripe_reduced_motion},
    {.property = MaterialProperty::fill_color, .field = RecipeField::fill_colour},
    {.property = MaterialProperty::stroke_color, .field = RecipeField::stroke_colour},
    {.property = MaterialProperty::corner_radius, .field = RecipeField::corner_radius},
    {.property = MaterialProperty::grid_spacing, .field = RecipeField::grid_spacing},
    {.property = MaterialProperty::grid_band, .field = RecipeField::grid_band},
    {.property = MaterialProperty::flow, .field = RecipeField::flow_style},
    {.property = MaterialProperty::flow_speed, .field = RecipeField::flow_speed},
    {.property = MaterialProperty::flow_spacing, .field = RecipeField::flow_spacing},
}};

// CSS blend modes the engine does not composite (CSS_COVERAGE section 7).
// Sorted.
constexpr std::array<std::string_view, 15> blend_modes_not_admitted{
    "color", "color-burn", "color-dodge", "darken", "difference", "exclusion", "hard-light", "hue",
    "lighten", "luminosity", "overlay", "plus-darker", "plus-lighter", "saturation", "soft-light",
};
static_assert(std::is_sorted(blend_modes_not_admitted.begin(), blend_modes_not_admitted.end()),
              "binary_search needs the table sorted");

struct NamedGradient final {
    std::string_view name{};
    MaterialFillKind kind{MaterialFillKind::linear_gradient};
    bool repeating{};
    // The theme language's reflecting- forms (owner, 2026-11-16; not CSS):
    // repeating, each period the one before mirrored.
    bool reflecting{};
};

constexpr std::array<NamedGradient, 9> gradient_functions{{
    {.name = "linear-gradient", .kind = MaterialFillKind::linear_gradient, .repeating = false},
    {.name = "radial-gradient", .kind = MaterialFillKind::radial_gradient, .repeating = false},
    {.name = "conic-gradient", .kind = MaterialFillKind::conic_gradient, .repeating = false},
    {.name = "repeating-linear-gradient", .kind = MaterialFillKind::linear_gradient, .repeating = true},
    {.name = "repeating-radial-gradient", .kind = MaterialFillKind::radial_gradient, .repeating = true},
    {.name = "repeating-conic-gradient", .kind = MaterialFillKind::conic_gradient, .repeating = true},
    {.name = "reflecting-linear-gradient", .kind = MaterialFillKind::linear_gradient, .repeating = true,
     .reflecting = true},
    {.name = "reflecting-radial-gradient", .kind = MaterialFillKind::radial_gradient, .repeating = true,
     .reflecting = true},
    {.name = "reflecting-conic-gradient", .kind = MaterialFillKind::conic_gradient, .repeating = true,
     .reflecting = true},
}};

// The built-in procedural patterns `pattern()` names (ProceduralPattern);
// the fresco, which reads more colours, is not among them. Each hatch
// style is a pattern name of its own, its tile one 8-unit hatch cell.
struct NamedPattern final {
    std::string_view name{};
    ProceduralPattern pattern{ProceduralPattern::felt};
    double tile{64.0};
    HatchStyle hatch_style{HatchStyle::horizontal};
};

constexpr std::array<NamedPattern, 12> pattern_names{{
    {.name = "checker", .pattern = ProceduralPattern::checker, .tile = 2.0},
    {.name = "felt", .pattern = ProceduralPattern::felt, .tile = 64.0},
    {.name = "wood-grain", .pattern = ProceduralPattern::wood_grain, .tile = 64.0},
    {.name = "paper-fibre", .pattern = ProceduralPattern::paper_fibre, .tile = 64.0},
    {.name = "pinstripe", .pattern = ProceduralPattern::pinstripe, .tile = 4.0},
    {.name = "brushed-metal", .pattern = ProceduralPattern::brushed_metal, .tile = 64.0},
    {.name = "hatch-horizontal", .pattern = ProceduralPattern::hatch, .tile = 8.0,
     .hatch_style = HatchStyle::horizontal},
    {.name = "hatch-vertical", .pattern = ProceduralPattern::hatch, .tile = 8.0, .hatch_style = HatchStyle::vertical},
    {.name = "hatch-forward-diagonal", .pattern = ProceduralPattern::hatch, .tile = 8.0,
     .hatch_style = HatchStyle::forward_diagonal},
    {.name = "hatch-backward-diagonal", .pattern = ProceduralPattern::hatch, .tile = 8.0,
     .hatch_style = HatchStyle::backward_diagonal},
    {.name = "hatch-cross", .pattern = ProceduralPattern::hatch, .tile = 8.0, .hatch_style = HatchStyle::cross},
    {.name = "hatch-diagonal-cross", .pattern = ProceduralPattern::hatch, .tile = 8.0,
     .hatch_style = HatchStyle::diagonal_cross},
}};

constexpr std::string_view pattern_function_name = "pattern";

// The surface finishes `finish()` names (FinishKind; skeudoindustrial
// surfaces, phases 1 and 2): the finishes, and the glint in each of its
// presets.
struct NamedFinish final {
    std::string_view name{};
    FinishKind kind{FinishKind::orange_peel};
    GlintPreset glint{GlintPreset::sand_blast};
};

constexpr std::array<NamedFinish, 13> finish_names{{
    {.name = "dusty-matte", .kind = FinishKind::dusty_matte},
    {.name = "orange-peel", .kind = FinishKind::orange_peel},
    {.name = "hammertone", .kind = FinishKind::hammertone},
    {.name = "wrinkle", .kind = FinishKind::wrinkle},
    {.name = "bead-blast", .kind = FinishKind::bead_blast},
    {.name = "sand-cast", .kind = FinishKind::sand_cast},
    {.name = "flat", .kind = FinishKind::flat},
    {.name = "cloth-weave", .kind = FinishKind::cloth_weave},
    {.name = "bead-blast-glint", .kind = FinishKind::glint, .glint = GlintPreset::bead_blast},
    {.name = "sand-blast-glint", .kind = FinishKind::glint, .glint = GlintPreset::sand_blast},
    {.name = "iridescent-glint", .kind = FinishKind::glint, .glint = GlintPreset::iridescent},
    {.name = "coarse-grit-glint", .kind = FinishKind::glint, .glint = GlintPreset::coarse_grit},
    {.name = "painted-sand-glint", .kind = FinishKind::glint, .glint = GlintPreset::painted_sand},
}};

constexpr std::string_view finish_function_name = "finish";

// The material shape functions (owner 2026-11-16): `transform` takes
// rotate() and scale(), `clip-path` polygon() and path().
constexpr std::string_view rotate_function_name = "rotate";
constexpr std::string_view scale_function_name = "scale";
constexpr std::string_view polygon_function_name = "polygon";
constexpr std::string_view path_function_name = "path";

// The linear map rotate(degrees) names: clockwise on screen (y down), as
// CSS turns. A multiple of 90 degrees is exact.
[[nodiscard]] MaterialTransform rotation_transform(double degrees) noexcept {
    // The engine's deterministic sine and cosine: the same bits on every
    // host, and exact at every quarter turn.
    const EngineSineCosine turn = engine_sine_cosine_degrees(degrees);
    const double cosine = turn.cosine;
    const double sine = turn.sine;
    MaterialTransform turned{};
    turned.xx = cosine;
    turned.yx = sine;
    turned.xy = -sine;
    turned.yy = cosine;
    return turned;
}

// `first` applied after `second`: the CSS transform list applies its last
// function to a point first.
[[nodiscard]] MaterialTransform composed_transform(const MaterialTransform& first,
                                                   const MaterialTransform& second) noexcept {
    MaterialTransform product{};
    product.xx = first.xx * second.xx + first.xy * second.yx;
    product.yx = first.yx * second.xx + first.yy * second.yx;
    product.xy = first.xx * second.xy + first.xy * second.yy;
    product.yy = first.yx * second.xy + first.yy * second.yy;
    return product;
}

// -- SVG path data for clip-path: path() ------------------------------------------

// Why a path() string did not read.
enum class SvgPathProblem : std::uint8_t {
    none,
    // Not SVG path data: a missing number or flag, a stray character, or a
    // path that does not start with a move.
    syntax,
    // More than MaterialClipPath::maximum_points points, or a coordinate
    // beyond MaterialClipPath::maximum_units.
    too_large
};

// Reads SVG path data (M, L, H, V, C, S, Q, T, A, Z, each absolute or
// relative, a command repeating over further coordinates, a move's further
// pairs as lines) into a clip path's verbs and points, in px from the box's
// top-left. Numbers are read exactly as decimals: at most 15 significant
// digits and an exponent of at most 20 in size, so each is one correctly
// rounded division or multiplication by an exact power of ten.
//
// S and T become a cubic and a quadratic whose first control point is the
// previous curve's last control point reflected about the current point
// (the current point itself after any other command), as SVG defines them.
// An arc becomes cubic Béziers by the SVG endpoint-to-centre conversion
// (radii scaled up when too small, a zero radius a line, equal endpoints
// nothing) and a fixed subdivision: n = ceil(|sweep| / 90 degrees) equal
// pieces (1 to 4; a sweep within 1e-9 radians above a multiple of 90
// degrees takes the lower count), each the cubic with handles
// 4/3 tan(piece / 4) of the radius, its last end the arc's exact end. The
// count and the formulas are fixed; the points use the host's sin, cos,
// tan, atan2 and sqrt, so they can differ in the last bits between hosts.
class SvgPathReader final {
public:
    explicit SvgPathReader(std::string_view text) noexcept : text_(text) {}

    [[nodiscard]] SvgPathProblem read(MaterialClipPath& clip);

private:
    void skip_separators() noexcept;
    [[nodiscard]] bool at_number() const noexcept;
    [[nodiscard]] bool read_number(double& value) noexcept;
    [[nodiscard]] bool read_pair(Point& point) noexcept;
    [[nodiscard]] bool read_flag(bool& flag) noexcept;
    [[nodiscard]] SvgPathProblem push_arc(Point radii, double rotation_degrees, bool large_arc, bool sweep, Point end,
                                          MaterialClipPath& clip);
    [[nodiscard]] SvgPathProblem push(MaterialClipVerb verb, std::span<const Point> points, MaterialClipPath& clip);

    std::string_view text_;
    std::size_t at_{};
    Point current_{};
    Point start_{};
    // The last control point of the previous command when it was a cubic
    // (C or S) or a quadratic (Q or T), for S and T to reflect.
    Point last_cubic_control_{};
    Point last_quadratic_control_{};
    bool after_cubic_{};
    bool after_quadratic_{};
};

void SvgPathReader::skip_separators() noexcept {
    while (at_ < text_.size()) {
        const char character = text_[at_];
        const bool separator = character == ' ' || character == '\t' || character == '\n' || character == '\r' ||
                               character == '\f' || character == ',';
        if (!separator) return;
        at_ = at_ + 1U;
    }
}

bool SvgPathReader::at_number() const noexcept {
    if (at_ >= text_.size()) return false;
    const char character = text_[at_];
    const bool starts = (character >= '0' && character <= '9') || character == '.' || character == '-' ||
                        character == '+';
    return starts;
}

bool SvgPathReader::read_number(double& value) noexcept {
    skip_separators();
    bool negative = false;
    if (at_ < text_.size() && (text_[at_] == '-' || text_[at_] == '+')) {
        negative = text_[at_] == '-';
        at_ = at_ + 1U;
    }
    std::int64_t mantissa = 0;
    int significant = 0;
    int scale = 0;
    bool digits = false;
    bool point = false;
    while (at_ < text_.size()) {
        const char character = text_[at_];
        if (character == '.' && !point) {
            point = true;
            at_ = at_ + 1U;
            continue;
        }
        if (character < '0' || character > '9') break;
        digits = true;
        const std::int64_t digit = static_cast<std::int64_t>(character - '0');
        if (significant > 0 || digit != 0) {
            if (significant >= 15) return false;
            significant = significant + 1;
        }
        mantissa = mantissa * 10 + digit;
        if (point) scale = scale - 1;
        at_ = at_ + 1U;
    }
    if (!digits) return false;
    if (at_ < text_.size() && (text_[at_] == 'e' || text_[at_] == 'E')) {
        at_ = at_ + 1U;
        bool exponent_negative = false;
        if (at_ < text_.size() && (text_[at_] == '-' || text_[at_] == '+')) {
            exponent_negative = text_[at_] == '-';
            at_ = at_ + 1U;
        }
        int exponent = 0;
        bool exponent_digits = false;
        while (at_ < text_.size() && text_[at_] >= '0' && text_[at_] <= '9') {
            exponent = exponent * 10 + (text_[at_] - '0');
            if (exponent > 20) return false;
            exponent_digits = true;
            at_ = at_ + 1U;
        }
        if (!exponent_digits) return false;
        scale = exponent_negative ? scale - exponent : scale + exponent;
    }
    if (scale < -22 || scale > 22) return false;
    double power = 1.0;
    for (int step = 0; step < std::abs(scale); ++step) power = power * 10.0;
    const double magnitude = static_cast<double>(mantissa);
    const double read = scale < 0 ? magnitude / power : magnitude * power;
    value = negative ? -read : read;
    return true;
}

bool SvgPathReader::read_pair(Point& point) noexcept {
    double x = 0.0;
    double y = 0.0;
    if (!read_number(x) || !read_number(y)) return false;
    point = Point{x, y};
    return true;
}

bool SvgPathReader::read_flag(bool& flag) noexcept {
    skip_separators();
    if (at_ >= text_.size()) return false;
    const char character = text_[at_];
    if (character != '0' && character != '1') return false;
    flag = character == '1';
    at_ = at_ + 1U;
    return true;
}

// The arc pieces' handle factor and count (SvgPathReader).
constexpr double arc_piece_limit = 1.5707963267948966192; // 90 degrees
constexpr double arc_piece_slack = 1.0e-9;

SvgPathProblem SvgPathReader::push_arc(Point radii, double rotation_degrees, bool large_arc, bool sweep, Point end,
                                       MaterialClipPath& clip) {
    const Point begin = current_;
    if (begin == end) return SvgPathProblem::none;
    double radius_x = std::abs(radii.x);
    double radius_y = std::abs(radii.y);
    if (radius_x == 0.0 || radius_y == 0.0) {
        const std::array<Point, 1> line{end};
        return push(MaterialClipVerb::line, line, clip);
    }
    // The engine's deterministic trigonometry throughout, so the control
    // points have the same bits on every host.
    const EngineSineCosine rotation = engine_sine_cosine_degrees(rotation_degrees);
    const double cosine = rotation.cosine;
    const double sine = rotation.sine;
    // The endpoints' half difference in the ellipse's own axes (SVG F.6.5).
    const double half_x = (begin.x - end.x) * 0.5;
    const double half_y = (begin.y - end.y) * 0.5;
    const double turned_x = cosine * half_x + sine * half_y;
    const double turned_y = -sine * half_x + cosine * half_y;
    const double reach = (turned_x * turned_x) / (radius_x * radius_x) + (turned_y * turned_y) / (radius_y * radius_y);
    if (reach > 1.0) {
        const double grow = std::sqrt(reach);
        radius_x = radius_x * grow;
        radius_y = radius_y * grow;
    }
    const double rx2 = radius_x * radius_x;
    const double ry2 = radius_y * radius_y;
    const double tx2 = turned_x * turned_x;
    const double ty2 = turned_y * turned_y;
    const double numerator = rx2 * ry2 - rx2 * ty2 - ry2 * tx2;
    const double denominator = rx2 * ty2 + ry2 * tx2;
    const double ratio = denominator > 0.0 ? std::max(0.0, numerator / denominator) : 0.0;
    const double side = large_arc == sweep ? -1.0 : 1.0;
    const double factor = side * std::sqrt(ratio);
    const double centre_turned_x = factor * radius_x * turned_y / radius_y;
    const double centre_turned_y = -factor * radius_y * turned_x / radius_x;
    const Point centre{cosine * centre_turned_x - sine * centre_turned_y + (begin.x + end.x) * 0.5,
                       sine * centre_turned_x + cosine * centre_turned_y + (begin.y + end.y) * 0.5};
    const double start_x = (turned_x - centre_turned_x) / radius_x;
    const double start_y = (turned_y - centre_turned_y) / radius_y;
    const double end_x = (-turned_x - centre_turned_x) / radius_x;
    const double end_y = (-turned_y - centre_turned_y) / radius_y;
    const double start_angle = engine_arctangent2(start_y, start_x);
    double sweep_angle = engine_arctangent2(start_x * end_y - start_y * end_x, start_x * end_x + start_y * end_y);
    if (!sweep && sweep_angle > 0.0) sweep_angle = sweep_angle - 2.0 * 3.14159265358979323846;
    if (sweep && sweep_angle < 0.0) sweep_angle = sweep_angle + 2.0 * 3.14159265358979323846;
    const double pieces_wanted = std::ceil(std::abs(sweep_angle) / arc_piece_limit - arc_piece_slack);
    const std::size_t pieces = static_cast<std::size_t>(std::clamp(pieces_wanted, 1.0, 4.0));
    const double piece = sweep_angle / static_cast<double>(pieces);
    const double handle = 4.0 / 3.0 * engine_tangent(piece * 0.25);
    for (std::size_t index = 0U; index < pieces; ++index) {
        const double from = start_angle + piece * static_cast<double>(index);
        const double to = from + piece;
        const EngineSineCosine from_turn = engine_sine_cosine(from);
        const EngineSineCosine to_turn = engine_sine_cosine(to);
        const double from_cos = from_turn.cosine;
        const double from_sin = from_turn.sine;
        const double to_cos = to_turn.cosine;
        const double to_sin = to_turn.sine;
        // On the unit circle, then onto the ellipse: scaled, turned, moved.
        const std::array<Point, 3> unit{Point{from_cos - handle * from_sin, from_sin + handle * from_cos},
                                        Point{to_cos + handle * to_sin, to_sin - handle * to_cos},
                                        Point{to_cos, to_sin}};
        std::array<Point, 3> placed{};
        for (std::size_t point = 0U; point < unit.size(); ++point) {
            const double across = radius_x * unit[point].x;
            const double down = radius_y * unit[point].y;
            placed[point] = Point{centre.x + cosine * across - sine * down, centre.y + sine * across + cosine * down};
        }
        if (index + 1U == pieces) placed[2] = end;
        const SvgPathProblem problem = push(MaterialClipVerb::cubic, placed, clip);
        if (problem != SvgPathProblem::none) return problem;
    }
    return SvgPathProblem::none;
}

SvgPathProblem SvgPathReader::push(MaterialClipVerb verb, std::span<const Point> points, MaterialClipPath& clip) {
    if (clip.points.size() + points.size() > MaterialClipPath::maximum_points) return SvgPathProblem::too_large;
    for (const Point point : points) {
        const bool bounded = std::abs(point.x) <= MaterialClipPath::maximum_units &&
                             std::abs(point.y) <= MaterialClipPath::maximum_units;
        if (!bounded) return SvgPathProblem::too_large;
        MaterialClipPoint written{};
        written.x_units = point.x;
        written.y_units = point.y;
        clip.points.push_back(written);
    }
    clip.verbs.push_back(verb);
    return SvgPathProblem::none;
}

SvgPathProblem SvgPathReader::read(MaterialClipPath& clip) {
    clip.verbs.clear();
    clip.points.clear();
    char command = '\0';
    skip_separators();
    while (at_ < text_.size()) {
        const char character = text_[at_];
        const bool letter = (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z');
        if (letter) {
            command = character;
            at_ = at_ + 1U;
        } else if (command == '\0' || !at_number()) {
            return SvgPathProblem::syntax;
        } else if (command == 'Z' || command == 'z') {
            // Coordinates after a close need a command of their own.
            return SvgPathProblem::syntax;
        }
        const bool relative = command >= 'a' && command <= 'z';
        const char upper = relative ? static_cast<char>(command - 'a' + 'A') : command;
        const Point origin = relative ? current_ : Point{0.0, 0.0};
        if (clip.verbs.empty() && upper != 'M') return SvgPathProblem::syntax;
        std::array<Point, 3> points{};
        SvgPathProblem problem = SvgPathProblem::none;
        bool cubic = false;
        bool quadratic = false;
        switch (upper) {
        case 'M': {
            if (!read_pair(points[0])) return SvgPathProblem::syntax;
            points[0] = Point{origin.x + points[0].x, origin.y + points[0].y};
            problem = push(MaterialClipVerb::move, std::span<const Point>(points.data(), 1U), clip);
            current_ = points[0];
            start_ = points[0];
            // Further pairs after a move are lines.
            command = relative ? 'l' : 'L';
            break;
        }
        case 'L': {
            if (!read_pair(points[0])) return SvgPathProblem::syntax;
            points[0] = Point{origin.x + points[0].x, origin.y + points[0].y};
            problem = push(MaterialClipVerb::line, std::span<const Point>(points.data(), 1U), clip);
            current_ = points[0];
            break;
        }
        case 'H': {
            double x = 0.0;
            if (!read_number(x)) return SvgPathProblem::syntax;
            points[0] = Point{origin.x + x, current_.y};
            problem = push(MaterialClipVerb::line, std::span<const Point>(points.data(), 1U), clip);
            current_ = points[0];
            break;
        }
        case 'V': {
            double y = 0.0;
            if (!read_number(y)) return SvgPathProblem::syntax;
            points[0] = Point{current_.x, origin.y + y};
            problem = push(MaterialClipVerb::line, std::span<const Point>(points.data(), 1U), clip);
            current_ = points[0];
            break;
        }
        case 'Q':
        case 'T': {
            const bool smooth = upper == 'T';
            std::size_t first = 0U;
            if (smooth) {
                // The previous quadratic's control reflected about the
                // current point, or the current point.
                points[0] = after_quadratic_ ? Point{2.0 * current_.x - last_quadratic_control_.x,
                                                     2.0 * current_.y - last_quadratic_control_.y}
                                             : current_;
                first = 1U;
            }
            for (std::size_t index = first; index < 2U; ++index) {
                if (!read_pair(points[index])) return SvgPathProblem::syntax;
                points[index] = Point{origin.x + points[index].x, origin.y + points[index].y};
            }
            problem = push(MaterialClipVerb::quadratic, std::span<const Point>(points.data(), 2U), clip);
            last_quadratic_control_ = points[0];
            current_ = points[1];
            quadratic = true;
            break;
        }
        case 'C':
        case 'S': {
            const bool smooth = upper == 'S';
            std::size_t first = 0U;
            if (smooth) {
                points[0] = after_cubic_ ? Point{2.0 * current_.x - last_cubic_control_.x,
                                                 2.0 * current_.y - last_cubic_control_.y}
                                         : current_;
                first = 1U;
            }
            for (std::size_t index = first; index < 3U; ++index) {
                if (!read_pair(points[index])) return SvgPathProblem::syntax;
                points[index] = Point{origin.x + points[index].x, origin.y + points[index].y};
            }
            problem = push(MaterialClipVerb::cubic, std::span<const Point>(points.data(), 3U), clip);
            last_cubic_control_ = points[1];
            current_ = points[2];
            cubic = true;
            break;
        }
        case 'A': {
            Point radii{};
            double rotation = 0.0;
            bool large_arc = false;
            bool sweep = false;
            Point end{};
            if (!read_pair(radii) || !read_number(rotation)) return SvgPathProblem::syntax;
            if (!read_flag(large_arc) || !read_flag(sweep) || !read_pair(end)) return SvgPathProblem::syntax;
            end = Point{origin.x + end.x, origin.y + end.y};
            problem = push_arc(radii, rotation, large_arc, sweep, end, clip);
            current_ = end;
            break;
        }
        case 'Z':
            problem = push(MaterialClipVerb::close, std::span<const Point>(), clip);
            current_ = start_;
            break;
        default: return SvgPathProblem::syntax;
        }
        if (problem != SvgPathProblem::none) return problem;
        after_cubic_ = cubic;
        after_quadratic_ = quadratic;
        skip_separators();
    }
    if (clip.verbs.empty()) return SvgPathProblem::syntax;
    return SvgPathProblem::none;
}

// The largest seed `finish()` admits: the finish's seed is a 32-bit signed
// integer, written non-negative.
constexpr double maximum_finish_seed = 2147483647.0;

// The patterns a canvas texture layer names in `procedural()`
// (`background-texture`), every tiled built-in pattern: the fresco covers
// a box and does not tile. Each hatch style is a kind of its own, as in
// `pattern()`. A layer may instead be a surface finish,
// written as `finish()` is in a fill (finish_names).
struct CanvasTextureKind final {
    std::string_view name{};
    ProceduralPattern pattern{ProceduralPattern::felt};
    HatchStyle hatch_style{HatchStyle::horizontal};
};

constexpr std::array<CanvasTextureKind, 12> canvas_texture_kinds{{
    {.name = "felt", .pattern = ProceduralPattern::felt},
    {.name = "wood-grain", .pattern = ProceduralPattern::wood_grain},
    {.name = "paper-fibre", .pattern = ProceduralPattern::paper_fibre},
    {.name = "pinstripe", .pattern = ProceduralPattern::pinstripe},
    {.name = "brushed-metal", .pattern = ProceduralPattern::brushed_metal},
    {.name = "checker", .pattern = ProceduralPattern::checker},
    {.name = "hatch-horizontal", .pattern = ProceduralPattern::hatch, .hatch_style = HatchStyle::horizontal},
    {.name = "hatch-vertical", .pattern = ProceduralPattern::hatch, .hatch_style = HatchStyle::vertical},
    {.name = "hatch-forward-diagonal", .pattern = ProceduralPattern::hatch,
     .hatch_style = HatchStyle::forward_diagonal},
    {.name = "hatch-backward-diagonal", .pattern = ProceduralPattern::hatch,
     .hatch_style = HatchStyle::backward_diagonal},
    {.name = "hatch-cross", .pattern = ProceduralPattern::hatch, .hatch_style = HatchStyle::cross},
    {.name = "hatch-diagonal-cross", .pattern = ProceduralPattern::hatch, .hatch_style = HatchStyle::diagonal_cross},
}};

constexpr std::string_view procedural_function_name = "procedural";

// The largest seed a texture layer admits: ProceduralFill::seed's range.
constexpr double maximum_texture_seed = 4294967295.0;

[[nodiscard]] const NamedGradient* gradient_function(std::string_view name) noexcept {
    for (const NamedGradient& gradient : gradient_functions) {
        if (equals_ascii_folded(name, gradient.name)) return &gradient;
    }
    return nullptr;
}

// CSS's filter functions (Filter Effects Level 1), CSS_COVERAGE section 5.
enum class FilterFunction : std::uint8_t {
    blur,
    brightness,
    contrast,
    drop_shadow,
    grayscale,
    hue_rotate,
    invert,
    opacity,
    saturate,
    sepia,
};

struct NamedFilter final {
    std::string_view name{};
    FilterFunction function{FilterFunction::blur};
};

constexpr std::array<NamedFilter, 10> filter_functions{{
    {.name = "blur", .function = FilterFunction::blur},
    {.name = "brightness", .function = FilterFunction::brightness},
    {.name = "contrast", .function = FilterFunction::contrast},
    {.name = "drop-shadow", .function = FilterFunction::drop_shadow},
    {.name = "grayscale", .function = FilterFunction::grayscale},
    {.name = "hue-rotate", .function = FilterFunction::hue_rotate},
    {.name = "invert", .function = FilterFunction::invert},
    {.name = "opacity", .function = FilterFunction::opacity},
    {.name = "saturate", .function = FilterFunction::saturate},
    {.name = "sepia", .function = FilterFunction::sepia},
}};

[[nodiscard]] const NamedFilter* filter_function(std::string_view name) noexcept {
    for (const NamedFilter& filter : filter_functions) {
        if (equals_ascii_folded(name, filter.name)) return &filter;
    }
    return nullptr;
}

// One 8-bit straight channel through a filter's row, clamped and rounded as
// the engine stores a colour.
[[nodiscard]] std::uint8_t filtered_channel(const MaterialColourMatrix& matrix, std::size_t row,
                                            const std::array<double, 4>& straight) noexcept {
    const std::size_t first = row * MaterialColourMatrix::columns;
    double sum = matrix.values[first + 4U];
    for (std::size_t column = 0U; column < 4U; ++column) sum = sum + matrix.values[first + column] * straight[column];
    const double clamped = std::clamp(sum, 0.0, 1.0);
    const double scaled = std::floor(clamped * 255.0 + 0.5);
    const std::uint8_t channel = static_cast<std::uint8_t>(scaled);
    return channel;
}

// `colour` through `matrix`: what CSS's colour functions written after a
// drop-shadow() do to the shadow, which the engine draws beneath the
// filtered raster in its own colour.
[[nodiscard]] Color filtered_colour(const MaterialColourMatrix& matrix, Color colour) noexcept {
    const std::array<double, 4> straight{colour.red / 255.0, colour.green / 255.0, colour.blue / 255.0,
                                         colour.alpha / 255.0};
    const Color filtered{filtered_channel(matrix, 0U, straight), filtered_channel(matrix, 1U, straight),
                         filtered_channel(matrix, 2U, straight), filtered_channel(matrix, 3U, straight)};
    return filtered;
}

// A position keyword (CSS <position>), or `none` for a length-percentage.
enum class PositionWord : std::uint8_t { none, left, center, right, top, bottom };

[[nodiscard]] bool across_word(PositionWord word) noexcept {
    const bool across = word == PositionWord::left || word == PositionWord::center || word == PositionWord::right;
    return across;
}

[[nodiscard]] bool down_word(PositionWord word) noexcept {
    const bool down = word == PositionWord::top || word == PositionWord::center || word == PositionWord::bottom;
    return down;
}

[[nodiscard]] BoxLength word_length(PositionWord word) noexcept {
    switch (word) {
    case PositionWord::left:
    case PositionWord::top: return BoxLength{0.0, 0.0};
    case PositionWord::right:
    case PositionWord::bottom: return BoxLength{1.0, 0.0};
    case PositionWord::center:
    case PositionWord::none: break;
    // A value outside the enumeration is centred, as `center` is.
    default: break;
    }
    return BoxLength{0.5, 0.0};
}

// The first of an edge's three fields, its style; its width and colour
// follow.
[[nodiscard]] MaterialField edge_style_field(std::size_t edge) noexcept {
    const std::size_t first = static_cast<std::size_t>(MaterialField::border_top_style) + edge * 3U;
    return static_cast<MaterialField>(first);
}

[[nodiscard]] MaterialField edge_field(std::size_t edge, std::size_t part) noexcept {
    const std::size_t field = static_cast<std::size_t>(edge_style_field(edge)) + part;
    return static_cast<MaterialField>(field);
}

[[nodiscard]] MaterialField corner_field(std::size_t corner) noexcept {
    const std::size_t field = static_cast<std::size_t>(MaterialField::corner_top_left) + corner;
    return static_cast<MaterialField>(field);
}

// A field that keeps the floor's value: what a shorthand sets for a part it
// omits (the language's `initial`, the built-in recipe's value).
[[nodiscard]] MaterialDeclaration initial_field(MaterialField field) {
    MaterialDeclaration declaration{};
    declaration.field = field;
    declaration.initial = true;
    return declaration;
}

// The edges or corners CSS's one to four values set, in CSS's order: one
// value sets all four; two, the first and third and then the second and
// fourth; three, the first, the second and fourth, and the third.
constexpr std::array<std::array<std::size_t, 4>, 4> four_sides{{
    {0U, 0U, 0U, 0U},
    {0U, 1U, 0U, 1U},
    {0U, 1U, 2U, 1U},
    {0U, 1U, 2U, 3U},
}};

// One image layer as a sheet wrote it: a gradient, or, `imaged`, an image
// resource's tiled layer at its own size (CSS's initial size, repeat and
// position).
struct WrittenLayer final {
    AuthoredGradient gradient{};
    MaterialFillLayer image{};
    bool imaged{};
    // A pattern() layer (also `imaged`: fixed, in `image`).
    bool patterned{};
};

// One layer of the `background` shorthand: its image, and the position,
// size and repetition it names, else CSS's initial ones.
struct BackgroundLayer final {
    bool imaged{};
    bool none{};
    bool positioned{};
    bool sized{};
    bool repeated{};
    MaterialImagePosition position{};
    MaterialImageSize size{};
    MaterialImageRepeat repeat{MaterialImageRepeat::repeat};
};

// Components [first, end) of a value, at one nesting level.
struct ComponentRange final {
    std::size_t first{};
    std::size_t end{};
};

// One lowering: reads `value_` and writes `lowering_`. Each reading
// function returns false once it has recorded a problem.
class MaterialReader final {
public:
    MaterialReader(const ThemeValue& value, std::uint32_t start, std::span<const ThemeImageResource> images,
                   MaterialLowering& lowering) noexcept
        : value_(value), start_(start), images_(images), lowering_(lowering) {}

    void lower(MaterialProperty property);

private:
    bool fail(DiagnosticCode code, std::uint32_t token, std::string detail);
    bool drop_unknown_functions(MaterialProperty property);
    [[nodiscard]] std::size_t next(std::size_t index) const noexcept;
    // The token a "one value" problem is placed at: the second top-level
    // component's, or the last component's when the value has no second
    // (one function with arguments, or a lone component a shape needs two
    // of: A-gf-core-15-F1). The value has at least one component.
    [[nodiscard]] std::uint32_t after_first_token() const noexcept;
    [[nodiscard]] std::size_t count(ComponentRange range) const noexcept;
    void split(ComponentRange range, std::vector<ComponentRange>& parts) const;
    [[nodiscard]] bool keyword(std::size_t index, std::string_view lower_name) const noexcept;
    [[nodiscard]] PositionWord position_word(std::size_t index) const noexcept;

    bool read_colour(std::size_t index, Color& colour);
    bool read_length(std::size_t index, bool percentages, bool negatives, BoxLength& length);
    // Whether a gradient is resolved now (authored_gradient_fixed), which
    // one whose lengths may be in em is not.
    [[nodiscard]] bool fixed_gradient(const AuthoredGradient& gradient) const noexcept;
    // CSS's currentcolor as the lowering reads it (P8c), noting that it did.
    [[nodiscard]] Color current_colour() noexcept;
    bool read_angle(std::size_t index, double& degrees);
    bool read_turn_fraction(std::size_t index, double& fraction);
    bool read_position(ComponentRange range, BoxLength& across, BoxLength& down);
    bool read_stops(std::span<const ComponentRange> arguments, MaterialFillKind kind,
                    std::vector<AuthoredGradientStop>& stops);
    bool read_linear_configuration(ComponentRange range, AuthoredGradient& gradient, bool& configured);
    bool read_radial_configuration(ComponentRange range, AuthoredGradient& gradient, bool& configured);
    bool read_conic_configuration(ComponentRange range, AuthoredGradient& gradient, bool& configured);
    bool read_gradient(std::size_t index, AuthoredGradient& gradient);
    bool read_pattern(std::size_t index, MaterialFillLayer& layer);
    bool read_finish(std::size_t index, MaterialFillLayer& layer);
    bool read_procedural(std::size_t index, CanvasTextureLayer& layer);
    bool read_texture_layer(ComponentRange range, CanvasTextureLayer& layer);
    bool read_image(std::size_t index, WrittenLayer& layer);
    bool read_resource_image(std::size_t index, MaterialFillLayer& layer);
    bool push_fills(std::span<const WrittenLayer> written);
    bool read_image_position(ComponentRange range, MaterialImagePosition& position);
    bool read_image_size(ComponentRange range, MaterialImageSize& size);
    bool read_image_repeat(ComponentRange range, MaterialImageRepeat& repeat);
    [[nodiscard]] bool repeat_word(std::size_t index) const noexcept;
    [[nodiscard]] bool size_component(std::size_t index) const noexcept;
    bool read_background_layer(ComponentRange range, bool last, MaterialDeclaration& colour,
                               std::vector<WrittenLayer>& written, BackgroundLayer& layer);
    bool read_style(std::size_t index, MaterialDeclaration& styled);
    bool read_width(std::size_t index, double& width);
    bool read_radius(std::size_t index, MaterialDeclaration& corner);
    bool read_shadow(ComponentRange range, MaterialShadow& shadow);
    bool read_amount(std::size_t function, double& amount);
    bool read_colour_function(std::size_t function, FilterFunction kind, MaterialColourMatrix& matrix);
    bool read_blur(std::size_t function, double& deviation);
    bool read_drop_shadow(std::size_t function, MaterialDropShadow& shadow);
    bool read_glow(ComponentRange range, MaterialGlow& glow);
    [[nodiscard]] bool style_word(std::size_t index) const noexcept;
    [[nodiscard]] bool width_word(std::size_t index) const noexcept;

    bool lower_background_colour();
    bool lower_background_image();
    bool lower_blend_modes();
    bool lower_positions(MaterialField field, std::size_t maximum_layers);
    bool lower_sizes(MaterialField field, std::size_t maximum_layers);
    bool lower_repeats(MaterialField field, std::size_t maximum_layers);
    bool lower_background();
    bool lower_edges(std::size_t first_edge, std::size_t edge_count);
    bool lower_sides(std::size_t part);
    bool lower_edge_field(std::size_t edge, std::size_t part);
    bool lower_border_image();
    bool lower_radii();
    bool lower_corner(std::size_t corner);
    bool lower_shadows();
    bool lower_filter();
    bool lower_finish();
    bool lower_seat();
    bool lower_gradient_dither();
    bool lower_transform();
    bool lower_clip_path();
    bool read_scale_factor(ComponentRange part, double& factor);
    bool read_clip_rule(ComponentRange part, bool& read, MaterialClipRule& rule);
    bool read_polygon(std::size_t function, MaterialClipPath& clip);
    bool read_svg_path(std::size_t function, MaterialClipPath& clip);
    bool lower_glows();
    bool lower_glow_placements();
    bool lower_mask();
    bool lower_mask_modes();
    bool lower_mask_composites();
    bool lower_reflection();
    bool lower_keylines();
    [[nodiscard]] bool outline_style_word(std::size_t index) const noexcept;
    bool read_outline_style(std::size_t index, RecipeOutlineStyle& style);
    bool read_outline_width(std::size_t index, double& width);
    bool lower_text_colour();
    bool lower_recipe_colour(RecipeField field);
    bool lower_default_outline();
    bool lower_stroke_width();
    bool lower_needle_shape();
    bool lower_outline();
    bool lower_outline_style();
    bool lower_outline_colour();
    bool lower_outline_width();
    bool lower_outline_offset();
    bool lower_part_length(RecipeField field, double minimum, double maximum);
    bool lower_stripe_pitch();
    bool lower_stripe_number(RecipeField field, double minimum, double maximum);
    bool lower_stripe_angle();
    bool lower_stripe_sense(RecipeField field);
    bool lower_stripe_gloss();
    bool lower_stripe_reduced_motion();
    bool lower_stroke_dash();
    bool lower_halo();
    bool lower_flow_style();
    bool lower_background_texture();
    bool lower_part_shape(RecipeField length_field, RecipeField colour_field, bool colour_required);
    bool lower_text_shadow();
    bool lower_font_family();
    bool lower_font_size();
    bool lower_font_style();
    bool lower_font_weight();
    bool lower_letter_spacing();

    const ThemeValue& value_;
    std::uint32_t start_{};
    std::span<const ThemeImageResource> images_{};
    MaterialLowering& lowering_;
};

bool MaterialReader::fail(DiagnosticCode code, std::uint32_t token, std::string detail) {
    const bool warning = diagnostic_severity(code) == DiagnosticSeverity::warning;
    lowering_.outcome = warning ? MaterialLoweringOutcome::dropped : MaterialLoweringOutcome::refused;
    lowering_.declarations.clear();
    lowering_.recipe_fields.clear();
    lowering_.problem.code = code;
    lowering_.problem.token = token;
    lowering_.problem.detail = std::move(detail);
    return false;
}

// A function the material properties do not know is dropped with a warning
// (B2: an unknown function), as it is anywhere in a sheet. `procedural` is
// known to `background-texture` alone.
bool MaterialReader::drop_unknown_functions(MaterialProperty property) {
    const bool textured = property == MaterialProperty::background_texture;
    for (const ValueComponent& component : value_.components) {
        if (component.kind != ValueComponentKind::function) continue;
        if (gradient_function(component.text) != nullptr || filter_function(component.text) != nullptr) continue;
        if (equals_ascii_folded(component.text, pattern_function_name)) continue;
        if (equals_ascii_folded(component.text, finish_function_name)) continue;
        if (textured && equals_ascii_folded(component.text, procedural_function_name)) continue;
        const bool transform_function = equals_ascii_folded(component.text, rotate_function_name) ||
                                        equals_ascii_folded(component.text, scale_function_name);
        if (property == MaterialProperty::transform && transform_function) continue;
        const bool clip_function = equals_ascii_folded(component.text, polygon_function_name) ||
                                   equals_ascii_folded(component.text, path_function_name);
        if (property == MaterialProperty::clip_path && clip_function) continue;
        return fail(DiagnosticCode::unknown_function, component.token, clipped_theme_detail(component.text));
    }
    return true;
}

std::size_t MaterialReader::next(std::size_t index) const noexcept {
    const std::size_t after = value_.components[index].subtree_end;
    return after;
}

std::uint32_t MaterialReader::after_first_token() const noexcept {
    const std::size_t second = next(0U);
    if (second < value_.components.size()) {
        const std::uint32_t token = value_.components[second].token;
        return token;
    }
    const std::uint32_t last = value_.components.back().token;
    return last;
}

std::size_t MaterialReader::count(ComponentRange range) const noexcept {
    std::size_t components = 0U;
    for (std::size_t index = range.first; index < range.end; index = next(index)) components = components + 1U;
    return components;
}

// The comma-separated parts of `range`, into `parts`, which is cleared
// first. An empty part is kept, for its reader to refuse.
void MaterialReader::split(ComponentRange range, std::vector<ComponentRange>& parts) const {
    parts.clear();
    std::size_t first = range.first;
    for (std::size_t index = range.first; index < range.end; index = next(index)) {
        if (value_.components[index].kind != ValueComponentKind::comma) continue;
        parts.push_back(ComponentRange{first, index});
        first = index + 1U;
    }
    parts.push_back(ComponentRange{first, range.end});
}

bool MaterialReader::keyword(std::size_t index, std::string_view lower_name) const noexcept {
    const ValueComponent& component = value_.components[index];
    const bool matches = component.kind == ValueComponentKind::keyword && equals_ascii_folded(component.text, lower_name);
    return matches;
}

PositionWord MaterialReader::position_word(std::size_t index) const noexcept {
    if (keyword(index, "left")) return PositionWord::left;
    if (keyword(index, "center")) return PositionWord::center;
    if (keyword(index, "right")) return PositionWord::right;
    if (keyword(index, "top")) return PositionWord::top;
    if (keyword(index, "bottom")) return PositionWord::bottom;
    return PositionWord::none;
}

// -- values -----------------------------------------------------------------------

bool MaterialReader::read_colour(std::size_t index, Color& colour) {
    const ValueComponent& component = value_.components[index];
    if (component.kind == ValueComponentKind::colour) {
        colour = component.colour;
        return true;
    }
    if (component.kind == ValueComponentKind::expression) {
        return fail(DiagnosticCode::value_form_not_admitted, component.token, "an expression that reads inputs");
    }
    if (component.kind == ValueComponentKind::keyword) {
        if (equals_ascii_folded(component.text, "currentcolor")) {
            colour = current_colour();
            return true;
        }
        const std::optional<Color> named = named_colour(component.text);
        if (named) {
            colour = *named;
            return true;
        }
    }
    return fail(DiagnosticCode::value_wrong_kind, component.token, "a colour");
}

// A <length> (px, or a unitless 0) or, with `percentages`, a <percentage>
// of the box or gradient; negative only with `negatives`.
bool MaterialReader::read_length(std::size_t index, bool percentages, bool negatives, BoxLength& length) {
    const ValueComponent& component = value_.components[index];
    const std::string expected = percentages ? "a length or a percentage" : "a length";
    if (component.kind == ValueComponentKind::expression) {
        return fail(DiagnosticCode::value_form_not_admitted, component.token, "an expression that reads inputs");
    }
    if (component.kind != ValueComponentKind::number) {
        return fail(DiagnosticCode::value_wrong_kind, component.token, expected);
    }
    const bool zero = component.unit == ValueUnit::none && component.number == 0.0;
    const bool pixels = component.unit == ValueUnit::px || component.unit == ValueUnit::em;
    const bool percent = percentages && component.unit == ValueUnit::percent;
    if (!zero && !pixels && !percent) return fail(DiagnosticCode::value_wrong_kind, component.token, expected);
    if (!negatives && component.number < 0.0) {
        return fail(DiagnosticCode::value_out_of_range, component.token, clipped_theme_detail(component.text));
    }
    length = percent ? BoxLength{component.number / 100.0, 0.0} : BoxLength{0.0, component.number};
    // An em length is read at the lowering's em (P8c).
    if (component.unit == ValueUnit::em) length = BoxLength{0.0, component.number * lowering_.em};
    return true;
}

Color MaterialReader::current_colour() noexcept {
    lowering_.read_current_colour = true;
    return lowering_.current_colour;
}

bool MaterialReader::fixed_gradient(const AuthoredGradient& gradient) const noexcept {
    const bool fixed = !lowering_.keep_gradients && authored_gradient_fixed(gradient);
    return fixed;
}

// An <angle> (read in degrees), or a unitless 0.
bool MaterialReader::read_angle(std::size_t index, double& degrees) {
    const ValueComponent& component = value_.components[index];
    const bool number = component.kind == ValueComponentKind::number;
    const bool angle = number && component.unit == ValueUnit::degrees;
    const bool zero = number && component.unit == ValueUnit::none && component.number == 0.0;
    if (!angle && !zero) return fail(DiagnosticCode::value_wrong_kind, component.token, "an angle");
    degrees = component.number;
    return true;
}

// A conic stop's position: an angle or a percentage, as a fraction of a
// turn.
bool MaterialReader::read_turn_fraction(std::size_t index, double& fraction) {
    const ValueComponent& component = value_.components[index];
    if (component.kind == ValueComponentKind::number && component.unit == ValueUnit::percent) {
        fraction = component.number / 100.0;
        return true;
    }
    double degrees = 0.0;
    if (!read_angle(index, degrees)) {
        return fail(DiagnosticCode::value_wrong_kind, component.token, "an angle or a percentage");
    }
    fraction = degrees / 360.0;
    return true;
}

// CSS's <position> of one or two values; three or four are not admitted.
bool MaterialReader::read_position(ComponentRange range, BoxLength& across, BoxLength& down) {
    const std::size_t values = count(range);
    const std::uint32_t token = range.first < range.end ? value_.components[range.first].token : start_;
    if (values == 0U) return fail(DiagnosticCode::value_syntax, token, "a position");
    if (values > 2U) return fail(DiagnosticCode::value_form_not_admitted, token, "a position of three or four values");
    const std::size_t first = range.first;
    const PositionWord first_word = position_word(first);
    across = BoxLength{0.5, 0.0};
    down = BoxLength{0.5, 0.0};
    if (values == 1U) {
        if (first_word == PositionWord::top || first_word == PositionWord::bottom) {
            down = word_length(first_word);
            return true;
        }
        if (first_word != PositionWord::none) {
            across = word_length(first_word);
            return true;
        }
        return read_length(first, true, true, across);
    }
    const std::size_t second = next(first);
    const PositionWord second_word = position_word(second);
    const bool words = first_word != PositionWord::none && second_word != PositionWord::none;
    // Two keywords may come either way round: `top left` is `left top`.
    const bool swapped = words && (first_word == PositionWord::top || first_word == PositionWord::bottom ||
                                   second_word == PositionWord::left || second_word == PositionWord::right);
    const std::size_t across_index = swapped ? second : first;
    const std::size_t down_index = swapped ? first : second;
    const PositionWord across_value = swapped ? second_word : first_word;
    const PositionWord down_value = swapped ? first_word : second_word;
    if (across_value != PositionWord::none && !across_word(across_value)) {
        return fail(DiagnosticCode::value_syntax, value_.components[across_index].token, "a position");
    }
    if (down_value != PositionWord::none && !down_word(down_value)) {
        return fail(DiagnosticCode::value_syntax, value_.components[down_index].token, "a position");
    }
    if (across_value != PositionWord::none) {
        across = word_length(across_value);
    } else if (!read_length(across_index, true, true, across)) {
        return false;
    }
    if (down_value != PositionWord::none) {
        down = word_length(down_value);
        return true;
    }
    return read_length(down_index, true, true, down);
}

// The colour stops, one argument each: a colour and up to two positions,
// each position a stop of that colour.
bool MaterialReader::read_stops(std::span<const ComponentRange> arguments, MaterialFillKind kind,
                                std::vector<AuthoredGradientStop>& stops) {
    stops.clear();
    for (const ComponentRange& argument : arguments) {
        const std::size_t values = count(argument);
        if (values == 0U) return fail(DiagnosticCode::value_syntax, start_, "a colour stop");
        const ValueComponent& first = value_.components[argument.first];
        if (values == 1U && first.kind == ValueComponentKind::number) {
            return fail(DiagnosticCode::value_form_not_admitted, first.token, "a colour hint");
        }
        if (values > 3U) return fail(DiagnosticCode::value_syntax, first.token, "a colour and up to two positions");
        AuthoredGradientStop stop{};
        if (!read_colour(argument.first, stop.colour)) return false;
        if (values == 1U) {
            stops.push_back(stop);
            continue;
        }
        for (std::size_t index = next(argument.first); index < argument.end; index = next(index)) {
            stop.positioned = true;
            if (kind == MaterialFillKind::conic_gradient) {
                double fraction = 0.0;
                if (!read_turn_fraction(index, fraction)) return false;
                stop.position = BoxLength{fraction, 0.0};
            } else if (!read_length(index, true, true, stop.position)) {
                return false;
            }
            stops.push_back(stop);
        }
    }
    if (stops.size() > maximum_authored_gradient_stops) {
        return fail(DiagnosticCode::value_out_of_range, value_.components[arguments.front().first].token,
                    "at most " + std::to_string(maximum_authored_gradient_stops) + " colour stops");
    }
    return true;
}

// `<angle>` or `to <side-or-corner>`; `configured` is false for a first
// argument that is a colour stop.
bool MaterialReader::read_linear_configuration(ComponentRange range, AuthoredGradient& gradient, bool& configured) {
    configured = false;
    const std::size_t values = count(range);
    if (values == 0U) return true;
    const ValueComponent& first = value_.components[range.first];
    if (keyword(range.first, "to")) {
        configured = true;
        if (values < 2U || values > 3U) return fail(DiagnosticCode::value_syntax, first.token, "a side or a corner");
        const std::size_t side = next(range.first);
        const PositionWord one = position_word(side);
        const PositionWord other = values == 3U ? position_word(next(side)) : PositionWord::none;
        if (values == 2U) {
            if (one == PositionWord::top) gradient.angle_degrees = 0.0;
            if (one == PositionWord::right) gradient.angle_degrees = 90.0;
            if (one == PositionWord::bottom) gradient.angle_degrees = 180.0;
            if (one == PositionWord::left) gradient.angle_degrees = 270.0;
            if (one == PositionWord::none || one == PositionWord::center) {
                return fail(DiagnosticCode::value_syntax, value_.components[side].token, "a side or a corner");
            }
            return true;
        }
        const bool one_down = one == PositionWord::top || one == PositionWord::bottom;
        const PositionWord vertical = one_down ? one : other;
        const PositionWord horizontal = one_down ? other : one;
        const bool corner = (vertical == PositionWord::top || vertical == PositionWord::bottom) &&
                            (horizontal == PositionWord::left || horizontal == PositionWord::right);
        if (!corner) return fail(DiagnosticCode::value_syntax, value_.components[side].token, "a side or a corner");
        const bool top = vertical == PositionWord::top;
        const bool left = horizontal == PositionWord::left;
        if (top && left) gradient.corner = GradientCorner::top_left;
        if (top && !left) gradient.corner = GradientCorner::top_right;
        if (!top && !left) gradient.corner = GradientCorner::bottom_right;
        if (!top && left) gradient.corner = GradientCorner::bottom_left;
        return true;
    }
    const bool angle = first.kind == ValueComponentKind::number &&
                       (first.unit == ValueUnit::degrees || (first.unit == ValueUnit::none && first.number == 0.0));
    if (!angle) return true;
    configured = true;
    if (values != 1U) return fail(DiagnosticCode::value_syntax, value_.components[next(range.first)].token, "a comma");
    return read_angle(range.first, gradient.angle_degrees);
}

// `[<ending-shape> || <size>]? [at <position>]? [focus <position>]?`;
// `configured` is false for a first argument that is a colour stop. The
// focus (GUI.Forms, not CSS; owner, 2026-11-16) is where position 0 lies,
// read as the centre is; a repeating gradient's periods count from it.
bool MaterialReader::read_radial_configuration(ComponentRange range, AuthoredGradient& gradient, bool& configured) {
    configured = false;
    if (range.first >= range.end) return true;
    const ValueComponent& first = value_.components[range.first];
    const bool word = keyword(range.first, "circle") || keyword(range.first, "ellipse") ||
                      keyword(range.first, "closest-side") || keyword(range.first, "farthest-side") ||
                      keyword(range.first, "closest-corner") || keyword(range.first, "farthest-corner") ||
                      keyword(range.first, "at") || keyword(range.first, "focus");
    if (!word && first.kind != ValueComponentKind::number) return true;
    configured = true;
    bool shape = false;
    bool sized = false;
    std::array<std::size_t, 2> lengths{};
    std::size_t length_count = 0U;
    std::size_t index = range.first;
    for (; index < range.end && !keyword(index, "at") && !keyword(index, "focus"); index = next(index)) {
        const ValueComponent& component = value_.components[index];
        if (keyword(index, "circle") || keyword(index, "ellipse")) {
            if (shape) return fail(DiagnosticCode::value_syntax, component.token, "one shape");
            shape = true;
            gradient.circle = keyword(index, "circle");
            continue;
        }
        std::optional<RadialExtent> extent{};
        if (keyword(index, "closest-side")) extent = RadialExtent::closest_side;
        if (keyword(index, "farthest-side")) extent = RadialExtent::farthest_side;
        if (keyword(index, "closest-corner")) extent = RadialExtent::closest_corner;
        if (keyword(index, "farthest-corner")) extent = RadialExtent::farthest_corner;
        if (extent) {
            if (sized) return fail(DiagnosticCode::value_syntax, component.token, "one size");
            sized = true;
            gradient.extent = *extent;
            continue;
        }
        if (component.kind != ValueComponentKind::number || length_count == lengths.size() ||
            (sized && length_count == 0U)) {
            return fail(DiagnosticCode::value_syntax, component.token, "a shape, a size or `at`");
        }
        lengths[length_count] = index;
        length_count = length_count + 1U;
    }
    if (length_count > 0U) {
        if (sized) return fail(DiagnosticCode::value_syntax, value_.components[lengths[0]].token, "one size");
        gradient.extent = RadialExtent::lengths;
        if (!shape) gradient.circle = length_count == 1U;
        const bool one_for_circle = gradient.circle == (length_count == 1U);
        if (!one_for_circle) {
            const char* const expected = gradient.circle ? "one length for a circle" : "two radii for an ellipse";
            return fail(DiagnosticCode::value_syntax, value_.components[lengths[0]].token, expected);
        }
        if (!read_length(lengths[0], !gradient.circle, false, gradient.radius_x)) return false;
        if (length_count == 2U && !read_length(lengths[1], true, false, gradient.radius_y)) return false;
    }
    if (index >= range.end) return true;
    std::size_t focus = index;
    if (keyword(index, "at")) {
        focus = next(index);
        while (focus < range.end && !keyword(focus, "focus")) focus = next(focus);
        const ComponentRange position{next(index), focus};
        if (!read_position(position, gradient.centre_x, gradient.centre_y)) return false;
    }
    if (focus >= range.end) return true;
    const ComponentRange focus_position{next(focus), range.end};
    if (focus_position.first >= focus_position.end) {
        return fail(DiagnosticCode::value_syntax, value_.components[focus].token, "a position");
    }
    gradient.focused = true;
    return read_position(focus_position, gradient.focus_x, gradient.focus_y);
}

// `[from <angle>]? [at <position>]?`; `configured` is false for a first
// argument that is a colour stop.
bool MaterialReader::read_conic_configuration(ComponentRange range, AuthoredGradient& gradient, bool& configured) {
    configured = false;
    if (range.first >= range.end) return true;
    if (!keyword(range.first, "from") && !keyword(range.first, "at")) return true;
    configured = true;
    gradient.angle_degrees = 0.0;
    std::size_t index = range.first;
    if (keyword(index, "from")) {
        const std::size_t angle = next(index);
        if (angle >= range.end) return fail(DiagnosticCode::value_syntax, value_.components[index].token, "an angle");
        if (!read_angle(angle, gradient.angle_degrees)) return false;
        index = next(angle);
    }
    if (index >= range.end) return true;
    if (!keyword(index, "at")) return fail(DiagnosticCode::value_syntax, value_.components[index].token, "`at`");
    const ComponentRange position{next(index), range.end};
    return read_position(position, gradient.centre_x, gradient.centre_y);
}

bool MaterialReader::read_gradient(std::size_t index, AuthoredGradient& gradient) {
    const ValueComponent& function = value_.components[index];
    const NamedGradient& named = *gradient_function(function.text);
    gradient = AuthoredGradient{};
    gradient.kind = named.kind;
    gradient.repeating = named.repeating;
    gradient.reflecting = named.reflecting;
    std::vector<ComponentRange> arguments{};
    split(ComponentRange{index + 1U, function.subtree_end}, arguments);
    bool configured = false;
    bool read = false;
    switch (named.kind) {
    case MaterialFillKind::linear_gradient: read = read_linear_configuration(arguments[0], gradient, configured); break;
    case MaterialFillKind::radial_gradient: read = read_radial_configuration(arguments[0], gradient, configured); break;
    case MaterialFillKind::conic_gradient: read = read_conic_configuration(arguments[0], gradient, configured); break;
    case MaterialFillKind::solid:
    case MaterialFillKind::image:
    case MaterialFillKind::procedural:
    case MaterialFillKind::finish: break;
    // A value outside the enumeration names no gradient function: nothing
    // is read.
    default: break;
    }
    if (!read) return false;
    const std::span<const ComponentRange> stop_arguments =
        std::span<const ComponentRange>(arguments).subspan(configured ? 1U : 0U);
    if (stop_arguments.empty()) return fail(DiagnosticCode::value_syntax, function.token, "a colour stop");
    if (!read_stops(stop_arguments, named.kind, gradient.stops)) return false;
    if (!valid_authored_gradient(gradient)) {
        return fail(DiagnosticCode::value_out_of_range, function.token, clipped_theme_detail(function.text));
    }
    return true;
}

// `pattern(<name>, <ground>, <figure> [, <tile>])`: a built-in procedural
// pattern (pattern_names) in two colours, its tile a length within
// [minimum_procedural_tile, maximum_procedural_tile] (by default the
// pattern's own: 2px for the checker, a 50% dither), at full strength and
// seed 0. The engine generates it per display scale and draws the ground
// until it is ready (E10).
bool MaterialReader::read_pattern(std::size_t index, MaterialFillLayer& layer) {
    const ValueComponent& function = value_.components[index];
    std::vector<ComponentRange> arguments{};
    split(ComponentRange{index + 1U, function.subtree_end}, arguments);
    if (arguments.size() < 3U || arguments.size() > 4U) {
        return fail(DiagnosticCode::value_syntax, function.token, "a pattern, two colours and an optional tile");
    }
    for (const ComponentRange& argument : arguments) {
        if (count(argument) != 1U) return fail(DiagnosticCode::value_syntax, function.token, "one value per argument");
    }
    const NamedPattern* named = nullptr;
    for (const NamedPattern& candidate : pattern_names) {
        if (keyword(arguments[0].first, candidate.name)) named = &candidate;
    }
    if (named == nullptr) {
        return fail(DiagnosticCode::value_wrong_kind, value_.components[arguments[0].first].token,
                    "checker, felt, wood-grain, paper-fibre, pinstripe, brushed-metal or a hatch-* style");
    }
    ProceduralFill fill{};
    fill.pattern = (*named).pattern;
    fill.tile = (*named).tile;
    fill.hatch_style = (*named).hatch_style;
    fill.strength = 1.0;
    if (!read_colour(arguments[1].first, fill.ground)) return false;
    if (!read_colour(arguments[2].first, fill.figure)) return false;
    fill.accent = fill.ground;
    if (arguments.size() == 4U) {
        BoxLength tile{};
        if (!read_length(arguments[3].first, false, false, tile)) return false;
        if (!(tile.units >= minimum_procedural_tile && tile.units <= maximum_procedural_tile)) {
            return fail(DiagnosticCode::value_out_of_range, value_.components[arguments[3].first].token,
                        "a tile from 2px to 256px");
        }
        fill.tile = tile.units;
    }
    layer = MaterialFillLayer::procedural_pattern(fill);
    return true;
}

// `finish(<name>, <paint>, <secondary> [, <seed> [, <grain>]])`: a surface
// finish (finish_names) in its paint and second colour (the dust's, or the
// cloth's weft), its seed a whole number from 0 to 2^31 - 1 (7, the
// study's, by default) and its grain a number from 0.5 to 4 (1 by
// default), with the study's light, ambient, relief and gloss. The engine
// paints it per device pixel as it bakes the part (skeudoindustrial
// surfaces, phase 1). A finish layer, like a pattern, takes no position,
// size or repetition.
bool MaterialReader::read_finish(std::size_t index, MaterialFillLayer& layer) {
    const ValueComponent& function = value_.components[index];
    std::vector<ComponentRange> arguments{};
    split(ComponentRange{index + 1U, function.subtree_end}, arguments);
    if (arguments.size() < 3U || arguments.size() > 5U) {
        return fail(DiagnosticCode::value_syntax, function.token,
                    "a finish, two colours, and an optional seed and grain");
    }
    for (const ComponentRange& argument : arguments) {
        if (count(argument) != 1U) return fail(DiagnosticCode::value_syntax, function.token, "one value per argument");
    }
    const NamedFinish* named = nullptr;
    for (const NamedFinish& candidate : finish_names) {
        if (keyword(arguments[0].first, candidate.name)) named = &candidate;
    }
    if (named == nullptr) {
        return fail(DiagnosticCode::value_wrong_kind, value_.components[arguments[0].first].token,
                    "dusty-matte, orange-peel, hammertone, wrinkle, bead-blast, sand-cast, flat, cloth-weave "
                    "or a glint");
    }
    FinishFill finish{};
    finish.kind = (*named).kind;
    finish.glint = (*named).glint;
    if (!read_colour(arguments[1].first, finish.paint)) return false;
    if (!read_colour(arguments[2].first, finish.secondary)) return false;
    if (arguments.size() >= 4U) {
        const ValueComponent& seed = value_.components[arguments[3].first];
        const bool number = seed.kind == ValueComponentKind::number && seed.unit == ValueUnit::none;
        if (!number) return fail(DiagnosticCode::value_wrong_kind, seed.token, "a whole number seed");
        const bool whole = seed.number >= 0.0 && seed.number <= maximum_finish_seed &&
                           std::floor(seed.number) == seed.number;
        if (!whole) {
            return fail(DiagnosticCode::value_out_of_range, seed.token, "a seed from 0 to 2147483647");
        }
        finish.seed = static_cast<std::int32_t>(seed.number);
    }
    if (arguments.size() == 5U) {
        const ValueComponent& grain = value_.components[arguments[4].first];
        const bool number = grain.kind == ValueComponentKind::number && grain.unit == ValueUnit::none;
        if (!number) return fail(DiagnosticCode::value_wrong_kind, grain.token, "a number grain");
        if (!(grain.number >= minimum_finish_grain && grain.number <= maximum_finish_grain)) {
            return fail(DiagnosticCode::value_out_of_range, grain.token, "a grain from 0.5 to 4");
        }
        finish.grain = grain.number;
    }
    layer = MaterialFillLayer::finish_fill(finish);
    return true;
}

bool MaterialReader::read_image(std::size_t index, WrittenLayer& layer) {
    layer = WrittenLayer{};
    const ValueComponent& component = value_.components[index];
    const bool gradient_written =
        component.kind == ValueComponentKind::function && gradient_function(component.text) != nullptr;
    if (gradient_written) return read_gradient(index, layer.gradient);
    const bool pattern_written =
        component.kind == ValueComponentKind::function && equals_ascii_folded(component.text, pattern_function_name);
    if (pattern_written) {
        layer.imaged = true;
        layer.patterned = true;
        return read_pattern(index, layer.image);
    }
    const bool finish_written =
        component.kind == ValueComponentKind::function && equals_ascii_folded(component.text, finish_function_name);
    if (finish_written) {
        // A finish covers its box as a pattern does: no position, size
        // or repetition.
        layer.imaged = true;
        layer.patterned = true;
        return read_finish(index, layer.image);
    }
    if (component.kind == ValueComponentKind::resource) {
        layer.imaged = true;
        return read_resource_image(index, layer.image);
    }
    return fail(DiagnosticCode::value_wrong_kind, component.token, "an image or a gradient");
}

// A resource the manifest lists and binds to an image: its tiled layer at
// its own size. A listed resource that is no image is the wrong kind.
bool MaterialReader::read_resource_image(std::size_t index, MaterialFillLayer& layer) {
    const ValueComponent& component = value_.components[index];
    for (const ThemeImageResource& resource : images_) {
        if (resource.resource != component.text) continue;
        layer = MaterialFillLayer::tiled_image(resource.image, resource.pixel_size, resource.pixels_per_logical_pixel);
        return true;
    }
    return fail(DiagnosticCode::value_wrong_kind, component.token, "an image resource");
}

// The `fills` declaration of `written`, in CSS's order (the top layer
// first): the layers bottom first, an image and a fixed gradient as they
// are and the other gradients resolved when the material is assembled for
// a part.
bool MaterialReader::push_fills(std::span<const WrittenLayer> written) {
    MaterialDeclaration declaration{};
    declaration.field = MaterialField::fills;
    bool box_relative = false;
    for (const WrittenLayer& layer : written) {
        box_relative = box_relative || (!layer.imaged && !fixed_gradient(layer.gradient));
    }
    SurfaceMaterial checked{};
    checked.fills.clear();
    for (std::size_t from_top = written.size(); from_top > 0U; --from_top) {
        const WrittenLayer& source = written[from_top - 1U];
        MaterialFillLayer layer = source.image;
        bool fixed = true;
        if (!source.imaged) {
            fixed = fixed_gradient(source.gradient);
            // A box only a gradient that reads the box uses; the check
            // takes a sample one.
            resolve_authored_gradient(source.gradient, Size{100.0, 100.0}, layer);
        }
        checked.fills.push_back(layer);
        if (!fixed) layer = MaterialFillLayer{};
        declaration.fills.push_back(layer);
        if (!box_relative) continue;
        AuthoredGradient entry = fixed ? AuthoredGradient{} : source.gradient;
        if (fixed) entry.kind = MaterialFillKind::solid;
        declaration.gradients.push_back(std::move(entry));
    }
    if (!checked.fills.empty() && !valid_surface_material(checked)) {
        return fail(DiagnosticCode::value_out_of_range, start_, "a layer no material draws");
    }
    lowering_.declarations.push_back(std::move(declaration));
    return true;
}

// -- image layers' positions, sizes and repetitions --------------------------------

// A position the engine's image fills draw: CSS's <position> of one or two
// values, its percentages within [0, 100%] of the room the box leaves
// around the image and its lengths at most 4096 px.
bool MaterialReader::read_image_position(ComponentRange range, MaterialImagePosition& position) {
    if (!read_position(range, position.across, position.down)) return false;
    const std::array<BoxLength, 2> axes{position.across, position.down};
    for (const BoxLength axis : axes) {
        const bool anchored = axis.fraction >= 0.0 && axis.fraction <= 1.0;
        const bool offset = axis.units >= -4096.0 && axis.units <= 4096.0;
        if (!anchored || !offset) {
            return fail(DiagnosticCode::value_out_of_range, value_.components[range.first].token,
                        "a position beyond the box's room");
        }
    }
    return true;
}

bool MaterialReader::size_component(std::size_t index) const noexcept {
    const bool length = value_.components[index].kind == ValueComponentKind::number;
    const bool word = keyword(index, "auto") || keyword(index, "cover") || keyword(index, "contain");
    return length || word;
}

// CSS's <bg-size>: auto, cover, contain, or one length-percentage across
// with the other side auto. The engine keeps an image's proportions, so of
// two lengths only `100% 100%`, the box, is admitted.
bool MaterialReader::read_image_size(ComponentRange range, MaterialImageSize& size) {
    size = MaterialImageSize{};
    const std::size_t values = count(range);
    const std::uint32_t token = range.first < range.end ? value_.components[range.first].token : start_;
    if (values == 0U || values > 2U) return fail(DiagnosticCode::value_syntax, token, "one or two sizes");
    const std::size_t first = range.first;
    if (values == 1U && (keyword(first, "cover") || keyword(first, "contain"))) {
        size.sizing = keyword(first, "cover") ? MaterialImageSizing::cover : MaterialImageSizing::contain;
        return true;
    }
    const std::size_t second = values == 2U ? next(first) : first;
    const bool first_auto = keyword(first, "auto");
    const bool second_auto = values == 1U || keyword(second, "auto");
    if (first_auto && second_auto) return true;
    if (!first_auto && !second_auto) {
        const ValueComponent& across = value_.components[first];
        const ValueComponent& down = value_.components[second];
        const bool whole_box = across.kind == ValueComponentKind::number && across.unit == ValueUnit::percent &&
                               across.number == 100.0 && down.kind == ValueComponentKind::number &&
                               down.unit == ValueUnit::percent && down.number == 100.0;
        if (!whole_box) {
            return fail(DiagnosticCode::value_form_not_admitted, token, "a size of two lengths but 100% 100%");
        }
        size.sizing = MaterialImageSizing::stretch;
        return true;
    }
    const std::size_t length_index = first_auto ? second : first;
    if (!read_length(length_index, true, false, size.length)) return false;
    if (size.length.fraction == 0.0 && size.length.units == 0.0) {
        return fail(DiagnosticCode::value_form_not_admitted, value_.components[length_index].token, "a size of zero");
    }
    size.sizing = first_auto ? MaterialImageSizing::height : MaterialImageSizing::width;
    return true;
}

bool MaterialReader::repeat_word(std::size_t index) const noexcept {
    const bool word = keyword(index, "repeat") || keyword(index, "no-repeat") || keyword(index, "repeat-x") ||
                      keyword(index, "repeat-y") || keyword(index, "space") || keyword(index, "round") ||
                      keyword(index, "mirror") || keyword(index, "extend");
    return word;
}

// CSS's <repeat-style>: repeat, or no-repeat (one tile alone), on both
// axes, or the theme language's mirror (every other tile reflected) and
// extend (one tile, its edges extended; owner, 2026-11-16), also on both;
// repeating along one axis only, space and round are not admitted.
bool MaterialReader::read_image_repeat(ComponentRange range, MaterialImageRepeat& repeat) {
    const std::size_t values = count(range);
    const std::uint32_t token = range.first < range.end ? value_.components[range.first].token : start_;
    if (values == 0U || values > 2U) return fail(DiagnosticCode::value_syntax, token, "one or two repetitions");
    for (std::size_t index = range.first; index < range.end; index = next(index)) {
        const ValueComponent& component = value_.components[index];
        if (repeat_word(index)) continue;
        if (component.kind == ValueComponentKind::keyword) {
            return fail(DiagnosticCode::keyword_unknown, component.token, clipped_theme_detail(component.text));
        }
        return fail(DiagnosticCode::value_wrong_kind, component.token, "a repetition");
    }
    const std::size_t first = range.first;
    const std::size_t second = values == 2U ? next(first) : first;
    const bool both_repeat = keyword(first, "repeat") && keyword(second, "repeat");
    const bool both_alone = keyword(first, "no-repeat") && keyword(second, "no-repeat");
    const bool both_mirror = keyword(first, "mirror") && keyword(second, "mirror");
    const bool both_extend = keyword(first, "extend") && keyword(second, "extend");
    if (!both_repeat && !both_alone && !both_mirror && !both_extend) {
        return fail(DiagnosticCode::value_form_not_admitted, token,
                    clipped_theme_detail(value_.components[first].text));
    }
    repeat = MaterialImageRepeat::repeat;
    if (both_alone) repeat = MaterialImageRepeat::no_repeat;
    if (both_mirror) repeat = MaterialImageRepeat::mirror;
    if (both_extend) repeat = MaterialImageRepeat::extend;
    return true;
}

// `background-position` or `mask-position` (as `field` says): one entry
// per layer, at most `maximum_layers`.
bool MaterialReader::lower_positions(MaterialField field, std::size_t maximum_layers) {
    std::vector<ComponentRange> layers{};
    split(ComponentRange{0U, value_.components.size()}, layers);
    if (layers.size() > maximum_layers) {
        return fail(DiagnosticCode::too_many_layers, start_, std::to_string(maximum_layers));
    }
    MaterialDeclaration declaration{};
    declaration.field = field;
    declaration.image_positions.resize(layers.size());
    for (std::size_t layer = 0U; layer < layers.size(); ++layer) {
        if (!read_image_position(layers[layer], declaration.image_positions[layer])) return false;
    }
    lowering_.declarations.push_back(std::move(declaration));
    return true;
}

// `background-size` or `mask-size` (as `field` says): one entry per
// layer, at most `maximum_layers`.
bool MaterialReader::lower_sizes(MaterialField field, std::size_t maximum_layers) {
    std::vector<ComponentRange> layers{};
    split(ComponentRange{0U, value_.components.size()}, layers);
    if (layers.size() > maximum_layers) {
        return fail(DiagnosticCode::too_many_layers, start_, std::to_string(maximum_layers));
    }
    MaterialDeclaration declaration{};
    declaration.field = field;
    declaration.image_sizes.resize(layers.size());
    for (std::size_t layer = 0U; layer < layers.size(); ++layer) {
        if (!read_image_size(layers[layer], declaration.image_sizes[layer])) return false;
    }
    lowering_.declarations.push_back(std::move(declaration));
    return true;
}

// `background-repeat` or `mask-repeat` (as `field` says): one entry per
// layer, at most `maximum_layers`.
bool MaterialReader::lower_repeats(MaterialField field, std::size_t maximum_layers) {
    std::vector<ComponentRange> layers{};
    split(ComponentRange{0U, value_.components.size()}, layers);
    if (layers.size() > maximum_layers) {
        return fail(DiagnosticCode::too_many_layers, start_, std::to_string(maximum_layers));
    }
    MaterialDeclaration declaration{};
    declaration.field = field;
    declaration.image_repeats.resize(layers.size());
    for (std::size_t layer = 0U; layer < layers.size(); ++layer) {
        if (!read_image_repeat(layers[layer], declaration.image_repeats[layer])) return false;
    }
    lowering_.declarations.push_back(std::move(declaration));
    return true;
}

// -- borders, radii and shadows --------------------------------------------------

bool MaterialReader::style_word(std::size_t index) const noexcept {
    constexpr std::array<std::string_view, 10> styles{"none",   "hidden", "solid", "dashed", "dotted",
                                                      "double", "groove", "ridge", "inset",  "outset"};
    for (const std::string_view style : styles) {
        if (keyword(index, style)) return true;
    }
    return false;
}

bool MaterialReader::width_word(std::size_t index) const noexcept {
    const bool word = keyword(index, "thin") || keyword(index, "medium") || keyword(index, "thick");
    return word;
}

// A <line-style> into `styled`'s border_style, empty for none (CSS's
// `none` and `hidden`), and edge_lines: double, groove and ridge are a
// solid edge drawn as two lines (MaterialEdgeLines).
bool MaterialReader::read_style(std::size_t index, MaterialDeclaration& styled) {
    const ValueComponent& component = value_.components[index];
    if (component.kind != ValueComponentKind::keyword) {
        return fail(DiagnosticCode::value_wrong_kind, component.token, "a border style");
    }
    styled.edge_lines = MaterialEdgeLines::single;
    styled.border_style = MaterialBorderStyle::solid;
    if (keyword(index, "none") || keyword(index, "hidden")) {
        styled.border_style.reset();
        return true;
    }
    if (keyword(index, "solid")) return true;
    if (keyword(index, "dashed")) {
        styled.border_style = MaterialBorderStyle::dashed;
        return true;
    }
    if (keyword(index, "dotted")) {
        styled.border_style = MaterialBorderStyle::dotted;
        return true;
    }
    if (keyword(index, "double")) {
        styled.edge_lines = MaterialEdgeLines::double_line;
        return true;
    }
    if (keyword(index, "groove")) {
        styled.edge_lines = MaterialEdgeLines::groove;
        return true;
    }
    if (keyword(index, "ridge")) {
        styled.edge_lines = MaterialEdgeLines::ridge;
        return true;
    }
    if (keyword(index, "inset") || keyword(index, "outset")) {
        return fail(DiagnosticCode::value_form_not_admitted, component.token, clipped_theme_detail(component.text));
    }
    return fail(DiagnosticCode::keyword_unknown, component.token, clipped_theme_detail(component.text));
}

// A <line-width>: a length, or CSS's thin (1px), medium (3px) or thick
// (5px).
bool MaterialReader::read_width(std::size_t index, double& width) {
    if (keyword(index, "thin")) {
        width = 1.0;
        return true;
    }
    if (keyword(index, "medium")) {
        width = 3.0;
        return true;
    }
    if (keyword(index, "thick")) {
        width = 5.0;
        return true;
    }
    BoxLength length{};
    if (!read_length(index, false, false, length)) return false;
    width = length.units;
    return true;
}

// One corner's radius: a length, or a percentage, a fraction of the box's
// shorter side (R2-11).
bool MaterialReader::read_radius(std::size_t index, MaterialDeclaration& corner) {
    BoxLength length{};
    if (!read_length(index, true, false, length)) return false;
    corner.relative = length.units == 0.0 && length.fraction != 0.0;
    corner.number = corner.relative ? length.fraction : length.units;
    return true;
}

// `border` and an edge's shorthand: `<line-width> || <line-style> ||
// <color>` for edges [first_edge, first_edge + edge_count); an omitted
// part keeps the floor's (the language's `initial`).
bool MaterialReader::lower_edges(std::size_t first_edge, std::size_t edge_count) {
    std::optional<std::size_t> style_at{};
    std::optional<std::size_t> width_at{};
    std::optional<std::size_t> colour_at{};
    for (std::size_t index = 0U; index < value_.components.size(); index = next(index)) {
        const ValueComponent& component = value_.components[index];
        const bool width = width_word(index) || component.kind == ValueComponentKind::number;
        std::optional<std::size_t>& slot = width ? width_at : (style_word(index) ? style_at : colour_at);
        if (slot) return fail(DiagnosticCode::value_syntax, component.token, "one width, one style and one colour");
        slot = index;
    }
    MaterialDeclaration style{};
    double width = 0.0;
    Color colour{};
    if (style_at && !read_style(*style_at, style)) return false;
    if (width_at && !read_width(*width_at, width)) return false;
    if (colour_at && !read_colour(*colour_at, colour)) return false;
    for (std::size_t edge = first_edge; edge < first_edge + edge_count; ++edge) {
        MaterialDeclaration styled = initial_field(edge_field(edge, 0U));
        MaterialDeclaration wide = initial_field(edge_field(edge, 1U));
        MaterialDeclaration coloured = initial_field(edge_field(edge, 2U));
        if (style_at) {
            styled.initial = false;
            styled.border_style = style.border_style;
            styled.edge_lines = style.edge_lines;
        }
        if (width_at) {
            wide.initial = false;
            wide.number = width;
        }
        if (colour_at) {
            coloured.initial = false;
            coloured.colour = colour;
        }
        lowering_.declarations.push_back(std::move(styled));
        lowering_.declarations.push_back(std::move(wide));
        lowering_.declarations.push_back(std::move(coloured));
    }
    return true;
}

// `border-style`, `border-width` or `border-color` (`part` 0, 1 or 2): one
// to four values, CSS's order.
bool MaterialReader::lower_sides(std::size_t part) {
    const std::size_t values = count(ComponentRange{0U, value_.components.size()});
    if (values > 4U) {
        return fail(DiagnosticCode::value_syntax, value_.components.back().token, "one to four values");
    }
    std::array<MaterialDeclaration, 4> read{};
    std::size_t index = 0U;
    for (std::size_t value = 0U; value < values; ++value) {
        MaterialDeclaration& declaration = read[value];
        if (part == 0U && !read_style(index, declaration)) return false;
        if (part == 1U && !read_width(index, declaration.number)) return false;
        if (part == 2U && !read_colour(index, declaration.colour)) return false;
        index = next(index);
    }
    const std::array<std::size_t, 4>& sides = four_sides[values - 1U];
    for (std::size_t edge = 0U; edge < 4U; ++edge) {
        MaterialDeclaration declaration = read[sides[edge]];
        declaration.field = edge_field(edge, part);
        lowering_.declarations.push_back(std::move(declaration));
    }
    return true;
}

bool MaterialReader::lower_edge_field(std::size_t edge, std::size_t part) {
    if (value_.components.size() != 1U) {
        return fail(DiagnosticCode::value_syntax, after_first_token(), "one value");
    }
    MaterialDeclaration declaration{};
    declaration.field = edge_field(edge, part);
    if (part == 0U && !read_style(0U, declaration)) return false;
    if (part == 1U && !read_width(0U, declaration.number)) return false;
    if (part == 2U && !read_colour(0U, declaration.colour)) return false;
    lowering_.declarations.push_back(std::move(declaration));
    return true;
}

// `border-image`: none, or a bound image resource with `<slice>{1,4} fill`,
// `/ <width>{1,4}` and optionally `stretch`, as the engine's nine-patch
// (CSS_COVERAGE section 2). The nine-patch draws its middle and stretches
// its edges at one scale, so the language admits what it draws: `fill`
// written, the widths written (`auto`, the slices at the image's own
// size, or lengths), each in the same proportion to its slice, no outset
// and only `stretch`. Its longhands wait (E999). The engine draws it as the
// top fill layer in place of the border's lines.
bool MaterialReader::lower_border_image() {
    MaterialDeclaration declaration{};
    declaration.field = MaterialField::border_image;
    const std::size_t end = value_.components.size();
    if (count(ComponentRange{0U, end}) == 1U && keyword(0U, "none")) {
        lowering_.declarations.push_back(std::move(declaration));
        return true;
    }
    std::optional<std::size_t> source_at{};
    bool no_source = false;
    bool filled = false;
    ComponentRange slices{};
    ComponentRange widths{};
    ComponentRange outsets{};
    ComponentRange repeats{};
    std::size_t index = 0U;
    while (index < end) {
        const ValueComponent& component = value_.components[index];
        const bool source =
            component.kind == ValueComponentKind::function || component.kind == ValueComponentKind::resource;
        if (source || keyword(index, "none")) {
            if (source_at || no_source) return fail(DiagnosticCode::value_syntax, component.token, "one image");
            no_source = !source;
            if (source) source_at = index;
            index = next(index);
            continue;
        }
        const bool slice_part = component.kind == ValueComponentKind::number || keyword(index, "fill");
        if (slice_part) {
            if (slices.end != 0U) return fail(DiagnosticCode::value_syntax, component.token, "one slice");
            slices.first = index;
            while (index < end) {
                const bool part = value_.components[index].kind == ValueComponentKind::number;
                if (keyword(index, "fill")) {
                    if (filled) return fail(DiagnosticCode::value_syntax, value_.components[index].token, "one fill");
                    filled = true;
                } else if (!part) {
                    break;
                }
                index = next(index);
            }
            slices.end = index;
            if (index < end && value_.components[index].kind == ValueComponentKind::slash) {
                index = next(index);
                widths.first = index;
                while (index < end && (value_.components[index].kind == ValueComponentKind::number ||
                                       keyword(index, "auto"))) {
                    index = next(index);
                }
                widths.end = index;
            }
            if (index < end && value_.components[index].kind == ValueComponentKind::slash) {
                index = next(index);
                outsets.first = index;
                while (index < end && value_.components[index].kind == ValueComponentKind::number) index = next(index);
                outsets.end = index;
            }
            continue;
        }
        const bool repeat_part = keyword(index, "stretch") || keyword(index, "repeat") || keyword(index, "round") ||
                                 keyword(index, "space");
        if (repeat_part) {
            if (repeats.end != 0U) return fail(DiagnosticCode::value_syntax, component.token, "one repetition");
            repeats.first = index;
            while (index < end && (keyword(index, "stretch") || keyword(index, "repeat") || keyword(index, "round") ||
                                   keyword(index, "space"))) {
                index = next(index);
            }
            repeats.end = index;
            continue;
        }
        if (component.kind == ValueComponentKind::slash) {
            return fail(DiagnosticCode::value_syntax, component.token, "a width after the slices");
        }
        if (component.kind == ValueComponentKind::keyword) {
            return fail(DiagnosticCode::keyword_unknown, component.token, clipped_theme_detail(component.text));
        }
        return fail(DiagnosticCode::value_wrong_kind, component.token, "a border image");
    }
    if (!source_at) {
        // CSS's initial source, none: nothing is drawn.
        lowering_.declarations.push_back(std::move(declaration));
        return true;
    }
    const ValueComponent& source = value_.components[*source_at];
    if (source.kind == ValueComponentKind::function) {
        return fail(DiagnosticCode::value_form_not_admitted, source.token, "a gradient as a border image");
    }
    const std::uint32_t whole = source.token;
    for (std::size_t repeat = repeats.first; repeat < repeats.end; repeat = next(repeat)) {
        if (keyword(repeat, "stretch")) continue;
        return fail(DiagnosticCode::value_form_not_admitted, value_.components[repeat].token,
                    clipped_theme_detail(value_.components[repeat].text));
    }
    if (repeats.end != 0U && count(repeats) > 2U) return fail(DiagnosticCode::value_syntax, whole, "one or two repetitions");
    const std::size_t slice_count = slices.end == 0U ? 0U : count(slices) - (filled ? 1U : 0U);
    if (slice_count == 0U) return fail(DiagnosticCode::value_form_not_admitted, whole, "a border image without slices");
    if (slice_count > 4U) return fail(DiagnosticCode::value_syntax, whole, "one to four slices");
    if (!filled) {
        return fail(DiagnosticCode::value_form_not_admitted, whole, "a border image without fill (its middle)");
    }
    const std::size_t width_count = widths.end == 0U ? 0U : count(widths);
    if (width_count == 0U) {
        return fail(DiagnosticCode::value_form_not_admitted, whole, "a border image without its widths");
    }
    if (width_count > 4U) return fail(DiagnosticCode::value_syntax, whole, "one to four widths");
    for (std::size_t outset = outsets.first; outset < outsets.end; outset = next(outset)) {
        if (value_.components[outset].number == 0.0) continue;
        return fail(DiagnosticCode::value_form_not_admitted, value_.components[outset].token, "a border image outset");
    }
    MaterialFillLayer image{};
    if (!read_resource_image(*source_at, image)) return false;
    const double density = image.image_scale;
    // The slices in source pixels and the widths in logical units, in CSS's
    // order: top, right, bottom, left.
    std::array<double, 4> slice_pixels{};
    std::array<double, 4> read_slices{};
    std::array<bool, 4> slice_percent{};
    std::size_t written = 0U;
    for (std::size_t at = slices.first; at < slices.end; at = next(at)) {
        if (keyword(at, "fill")) continue;
        const ValueComponent& component = value_.components[at];
        const bool plain = component.unit == ValueUnit::none;
        const bool percent = component.unit == ValueUnit::percent;
        if (!plain && !percent) return fail(DiagnosticCode::value_wrong_kind, component.token, "a slice");
        if (component.number < 0.0) {
            return fail(DiagnosticCode::value_out_of_range, component.token, clipped_theme_detail(component.text));
        }
        read_slices[written] = percent ? component.number / 100.0 : component.number;
        slice_percent[written] = percent;
        written = written + 1U;
    }
    std::array<std::optional<double>, 4> read_widths{};
    written = 0U;
    for (std::size_t at = widths.first; at < widths.end; at = next(at)) {
        const ValueComponent& component = value_.components[at];
        if (!keyword(at, "auto")) {
            if (component.kind == ValueComponentKind::number && component.unit == ValueUnit::none &&
                component.number != 0.0) {
                return fail(DiagnosticCode::value_form_not_admitted, component.token,
                            "a border image width in border widths");
            }
            BoxLength length{};
            if (!read_length(at, false, false, length)) return false;
            read_widths[written] = length.units;
        }
        written = written + 1U;
    }
    const std::array<std::size_t, 4>& slice_sides = four_sides[slice_count - 1U];
    const std::array<std::size_t, 4>& width_sides = four_sides[width_count - 1U];
    std::optional<double> scale{};
    for (std::size_t side = 0U; side < 4U; ++side) {
        const double slice = read_slices[slice_sides[side]];
        // A percentage is of the image's height for the top and bottom,
        // its width for the sides.
        const double across = side % 2U == 0U ? image.image_pixel_size.height : image.image_pixel_size.width;
        slice_pixels[side] = slice_percent[slice_sides[side]] ? slice * across : slice;
        const std::optional<double> width = read_widths[width_sides[side]];
        const double logical = width ? *width : slice_pixels[side] / density;
        if (slice_pixels[side] == 0.0 && logical == 0.0) continue;
        if (slice_pixels[side] == 0.0 || logical == 0.0) {
            return fail(DiagnosticCode::value_form_not_admitted, whole, "a border image width without its slice");
        }
        const double side_scale = slice_pixels[side] / logical;
        if (scale && *scale != side_scale) {
            return fail(DiagnosticCode::value_form_not_admitted, whole,
                        "border image widths out of proportion to their slices");
        }
        scale = side_scale;
    }
    const Insets slice{slice_pixels[3], slice_pixels[0], slice_pixels[1], slice_pixels[2]};
    const MaterialFillLayer patch =
        MaterialFillLayer::nine_patch(image.image, image.image_pixel_size, slice, scale ? *scale : density);
    SurfaceMaterial checked{};
    checked.fills = {patch};
    if (!valid_surface_material(checked)) return fail(DiagnosticCode::value_out_of_range, whole, "a border image");
    declaration.border_image = patch;
    lowering_.declarations.push_back(std::move(declaration));
    return true;
}

// `border-radius`: one to four radii, CSS's order; elliptical radii (`/`)
// are not admitted (CSS_COVERAGE section 2).
bool MaterialReader::lower_radii() {
    for (const ValueComponent& component : value_.components) {
        if (component.kind == ValueComponentKind::slash) {
            return fail(DiagnosticCode::value_form_not_admitted, component.token, "elliptical radii");
        }
    }
    const std::size_t values = count(ComponentRange{0U, value_.components.size()});
    if (values > 4U) {
        return fail(DiagnosticCode::value_syntax, value_.components.back().token, "one to four radii");
    }
    std::array<MaterialDeclaration, 4> read{};
    std::size_t index = 0U;
    for (std::size_t value = 0U; value < values; ++value) {
        if (!read_radius(index, read[value])) return false;
        index = next(index);
    }
    const std::array<std::size_t, 4>& sides = four_sides[values - 1U];
    for (std::size_t corner = 0U; corner < 4U; ++corner) {
        MaterialDeclaration declaration = read[sides[corner]];
        declaration.field = corner_field(corner);
        lowering_.declarations.push_back(std::move(declaration));
    }
    return true;
}

bool MaterialReader::lower_corner(std::size_t corner) {
    if (value_.components.size() != 1U) {
        return fail(DiagnosticCode::value_form_not_admitted, after_first_token(), "elliptical radii");
    }
    MaterialDeclaration declaration{};
    declaration.field = corner_field(corner);
    if (!read_radius(0U, declaration)) return false;
    lowering_.declarations.push_back(std::move(declaration));
    return true;
}

// One shadow: `inset? && <length>{2,4} && <color>?`, its colour by default
// CSS's currentcolor.
bool MaterialReader::read_shadow(ComponentRange range, MaterialShadow& shadow) {
    shadow = MaterialShadow{};
    shadow.offset = Point{};
    shadow.blur_radius = 0.0;
    bool inset = false;
    bool coloured = false;
    std::array<double, 4> lengths{};
    std::size_t length_count = 0U;
    bool lengths_done = false;
    for (std::size_t index = range.first; index < range.end; index = next(index)) {
        const ValueComponent& component = value_.components[index];
        if (keyword(index, "inset")) {
            if (inset) return fail(DiagnosticCode::value_syntax, component.token, "one inset");
            inset = true;
            lengths_done = length_count > 0U;
            continue;
        }
        if (component.kind == ValueComponentKind::number) {
            if (lengths_done || length_count == lengths.size()) {
                return fail(DiagnosticCode::value_syntax, component.token, "two to four adjacent lengths");
            }
            BoxLength length{};
            const bool negatives = length_count != 2U;
            if (!read_length(index, false, negatives, length)) return false;
            lengths[length_count] = length.units;
            length_count = length_count + 1U;
            continue;
        }
        if (coloured) return fail(DiagnosticCode::value_syntax, component.token, "one colour");
        coloured = true;
        lengths_done = length_count > 0U;
        if (!read_colour(index, shadow.color)) return false;
    }
    const std::uint32_t token = range.first < range.end ? value_.components[range.first].token : start_;
    if (length_count < 2U) return fail(DiagnosticCode::value_syntax, token, "two to four lengths");
    if (!coloured) shadow.color = current_colour();
    shadow.offset = Point{lengths[0], lengths[1]};
    shadow.blur_radius = lengths[2];
    shadow.spread = lengths[3];
    shadow.inset = inset;
    return true;
}

// `box-shadow`: none, or up to SurfaceMaterial::maximum_shadows shadows,
// CSS's first on top (the material draws its list bottom first).
bool MaterialReader::lower_shadows() {
    MaterialDeclaration declaration{};
    declaration.field = MaterialField::shadows;
    const ComponentRange whole{0U, value_.components.size()};
    if (count(whole) == 1U && keyword(0U, "none")) {
        lowering_.declarations.push_back(std::move(declaration));
        return true;
    }
    std::vector<ComponentRange> shadows{};
    split(whole, shadows);
    if (shadows.size() > SurfaceMaterial::maximum_shadows) {
        return fail(DiagnosticCode::too_many_layers, start_,
                    std::to_string(SurfaceMaterial::maximum_shadows) + " shadows");
    }
    declaration.shadows.resize(shadows.size());
    for (std::size_t shadow = 0U; shadow < shadows.size(); ++shadow) {
        if (!read_shadow(shadows[shadow], declaration.shadows[shadows.size() - 1U - shadow])) return false;
    }
    SurfaceMaterial checked{};
    checked.shadows = declaration.shadows;
    if (!valid_surface_material(checked)) return fail(DiagnosticCode::value_out_of_range, start_, "a shadow");
    lowering_.declarations.push_back(std::move(declaration));
    return true;
}

// -- filters, finish, glows and masks ---------------------------------------------

// A colour function's amount: a number or a percentage, 1 when omitted;
// negative is out of range, and CSS's factories clamp the rest.
bool MaterialReader::read_amount(std::size_t function, double& amount) {
    const ValueComponent& written = value_.components[function];
    const ComponentRange arguments{function + 1U, written.subtree_end};
    const std::size_t values = count(arguments);
    amount = 1.0;
    if (values == 0U) return true;
    if (values > 1U) return fail(DiagnosticCode::value_syntax, written.token, "one amount");
    const ValueComponent& component = value_.components[arguments.first];
    if (component.kind == ValueComponentKind::expression) {
        return fail(DiagnosticCode::value_form_not_admitted, component.token, "an expression that reads inputs");
    }
    const bool number = component.kind == ValueComponentKind::number;
    const bool plain = number && component.unit == ValueUnit::none;
    const bool percent = number && component.unit == ValueUnit::percent;
    if (!plain && !percent) return fail(DiagnosticCode::value_wrong_kind, component.token, "a number or a percentage");
    if (component.number < 0.0) {
        return fail(DiagnosticCode::value_out_of_range, component.token, clipped_theme_detail(component.text));
    }
    amount = percent ? component.number / 100.0 : component.number;
    return true;
}

bool MaterialReader::read_colour_function(std::size_t function, FilterFunction kind, MaterialColourMatrix& matrix) {
    if (kind == FilterFunction::hue_rotate) {
        const ValueComponent& written = value_.components[function];
        const ComponentRange arguments{function + 1U, written.subtree_end};
        const std::size_t values = count(arguments);
        double degrees = 0.0;
        if (values > 1U) return fail(DiagnosticCode::value_syntax, written.token, "one angle");
        if (values == 1U && !read_angle(arguments.first, degrees)) return false;
        matrix = MaterialColourMatrix::hue_rotate(degrees);
        return true;
    }
    double amount = 1.0;
    if (!read_amount(function, amount)) return false;
    switch (kind) {
    case FilterFunction::brightness: matrix = MaterialColourMatrix::brightness(amount); return true;
    case FilterFunction::contrast: matrix = MaterialColourMatrix::contrast(amount); return true;
    case FilterFunction::grayscale: matrix = MaterialColourMatrix::grayscale(amount); return true;
    case FilterFunction::invert: matrix = MaterialColourMatrix::invert(amount); return true;
    case FilterFunction::opacity: matrix = MaterialColourMatrix::opacity(amount); return true;
    case FilterFunction::saturate: matrix = MaterialColourMatrix::saturate(amount); return true;
    case FilterFunction::sepia: matrix = MaterialColourMatrix::sepia(amount); return true;
    case FilterFunction::blur:
    case FilterFunction::drop_shadow:
    case FilterFunction::hue_rotate: break;
    // A value outside the enumeration is no colour function: the matrix
    // is left as it is.
    default: break;
    }
    return true;
}

// blur()'s standard deviation, a length, 0 when omitted.
bool MaterialReader::read_blur(std::size_t function, double& deviation) {
    const ValueComponent& written = value_.components[function];
    const ComponentRange arguments{function + 1U, written.subtree_end};
    const std::size_t values = count(arguments);
    deviation = 0.0;
    if (values == 0U) return true;
    if (values > 1U) return fail(DiagnosticCode::value_syntax, written.token, "one length");
    BoxLength length{};
    if (!read_length(arguments.first, false, false, length)) return false;
    deviation = length.units;
    return true;
}

// drop-shadow(): a colour and two or three lengths, as a shadow's without
// inset or spread; its colour by default currentcolor.
bool MaterialReader::read_drop_shadow(std::size_t function, MaterialDropShadow& shadow) {
    const ValueComponent& written = value_.components[function];
    const ComponentRange arguments{function + 1U, written.subtree_end};
    std::array<double, 3> lengths{};
    std::size_t length_count = 0U;
    bool lengths_done = false;
    bool coloured = false;
    for (std::size_t index = arguments.first; index < arguments.end; index = next(index)) {
        const ValueComponent& component = value_.components[index];
        if (component.kind == ValueComponentKind::number) {
            if (lengths_done || length_count == lengths.size()) {
                return fail(DiagnosticCode::value_syntax, component.token, "two or three adjacent lengths");
            }
            BoxLength length{};
            const bool negatives = length_count != 2U;
            if (!read_length(index, false, negatives, length)) return false;
            lengths[length_count] = length.units;
            length_count = length_count + 1U;
            continue;
        }
        if (coloured) return fail(DiagnosticCode::value_syntax, component.token, "one colour");
        coloured = true;
        lengths_done = length_count > 0U;
        if (!read_colour(index, shadow.color)) return false;
    }
    if (length_count < 2U) return fail(DiagnosticCode::value_syntax, written.token, "two or three lengths");
    if (!coloured) shadow.color = current_colour();
    shadow.offset = Point{lengths[0], lengths[1]};
    shadow.blur_radius = lengths[2];
    return true;
}

// `filter`: none, or a list of functions. The engine applies a material's
// filters in one order (the colour filter, the drop shadow beneath, the
// blur), so a list lowers when that order draws it: the colour functions
// compose into one matrix in source order; blur()s combine as Gaussians do,
// in quadrature, into filter_blur = 2σ; one drop-shadow(), before any
// blur(), whose colour the colour functions after it also turn. A
// drop-shadow() after blur(), a second drop-shadow(), opacity() after a
// drop-shadow() (which would fade the shadow twice) and an SVG filter
// reference are not admitted.
bool MaterialReader::lower_filter() {
    MaterialDeclaration colour{};
    colour.field = MaterialField::colour_filter;
    MaterialDeclaration drop{};
    drop.field = MaterialField::drop_shadow;
    MaterialDeclaration blur{};
    blur.field = MaterialField::filter_blur;
    const ComponentRange whole{0U, value_.components.size()};
    const bool none = count(whole) == 1U && keyword(0U, "none");
    MaterialColourMatrix composed{};
    MaterialColourMatrix after_shadow{};
    bool filtered = false;
    double variance = 0.0;
    bool blurred = false;
    MaterialDropShadow shadow{};
    bool shadowed = false;
    for (std::size_t index = 0U; index < whole.end && !none; index = next(index)) {
        const ValueComponent& component = value_.components[index];
        if (component.kind == ValueComponentKind::resource) {
            return fail(DiagnosticCode::value_form_not_admitted, component.token, "an SVG filter reference");
        }
        const NamedFilter* const named =
            component.kind == ValueComponentKind::function ? filter_function(component.text) : nullptr;
        if (named == nullptr) return fail(DiagnosticCode::value_wrong_kind, component.token, "a filter function");
        const FilterFunction kind = (*named).function;
        if (kind == FilterFunction::blur) {
            double deviation = 0.0;
            if (!read_blur(index, deviation)) return false;
            variance = variance + deviation * deviation;
            blurred = true;
            continue;
        }
        if (kind == FilterFunction::drop_shadow) {
            if (shadowed) return fail(DiagnosticCode::value_form_not_admitted, component.token, "a second drop-shadow()");
            if (blurred) return fail(DiagnosticCode::value_form_not_admitted, component.token, "drop-shadow() after blur()");
            if (!read_drop_shadow(index, shadow)) return false;
            shadowed = true;
            continue;
        }
        if (kind == FilterFunction::opacity && shadowed) {
            return fail(DiagnosticCode::value_form_not_admitted, component.token, "opacity() after drop-shadow()");
        }
        MaterialColourMatrix matrix{};
        if (!read_colour_function(index, kind, matrix)) return false;
        composed = composed.then(matrix);
        filtered = true;
        if (shadowed) after_shadow = after_shadow.then(matrix);
    }
    if (filtered) colour.colour_filter = composed;
    if (shadowed) {
        shadow.color = filtered_colour(after_shadow, shadow.color);
        drop.drop_shadow = shadow;
    }
    blur.number = 2.0 * std::sqrt(variance);
    SurfaceMaterial checked{};
    checked.colour_filter = colour.colour_filter;
    checked.drop_shadow = drop.drop_shadow;
    checked.filter_blur = blur.number;
    if (!valid_surface_material(checked)) return fail(DiagnosticCode::value_out_of_range, start_, "a filter");
    lowering_.declarations.push_back(std::move(colour));
    lowering_.declarations.push_back(std::move(drop));
    lowering_.declarations.push_back(std::move(blur));
    return true;
}

// `surface-finish`: from 0, the material as written, to 1, a full gloss; a
// number or a percentage.
bool MaterialReader::lower_finish() {
    if (value_.components.size() != 1U) {
        return fail(DiagnosticCode::value_syntax, after_first_token(), "one amount");
    }
    const ValueComponent& component = value_.components[0];
    if (component.kind == ValueComponentKind::expression) {
        return fail(DiagnosticCode::value_form_not_admitted, component.token, "an expression that reads inputs");
    }
    const bool number = component.kind == ValueComponentKind::number;
    const bool plain = number && component.unit == ValueUnit::none;
    const bool percent = number && component.unit == ValueUnit::percent;
    if (!plain && !percent) return fail(DiagnosticCode::value_wrong_kind, component.token, "a number or a percentage");
    const double amount = percent ? component.number / 100.0 : component.number;
    if (!(amount >= 0.0 && amount <= 1.0)) {
        return fail(DiagnosticCode::value_out_of_range, component.token, clipped_theme_detail(component.text));
    }
    MaterialDeclaration declaration{};
    declaration.field = MaterialField::finish;
    declaration.number = amount;
    lowering_.declarations.push_back(std::move(declaration));
    return true;
}

// `seat: none | <height> [<colour>]`: the part's height above its panel, a
// length (negative: sunk into it) or a percentage of its shorter side,
// and its shadow's colour (default_seat_colour). The height is clamped to
// maximum_seat_height when the material is assembled.
bool MaterialReader::lower_seat() {
    MaterialDeclaration declaration{};
    declaration.field = MaterialField::seat;
    declaration.colour = default_seat_colour;
    const std::size_t values = count(ComponentRange{0U, value_.components.size()});
    if (values == 1U && keyword(0U, "none")) {
        lowering_.declarations.push_back(std::move(declaration));
        return true;
    }
    if (values != 1U && values != 2U) {
        return fail(DiagnosticCode::value_syntax, value_.components[0].token, "a height and a colour");
    }
    BoxLength height{};
    if (!read_length(0U, true, true, height)) return false;
    declaration.relative = height.units == 0.0 && height.fraction != 0.0;
    declaration.number = declaration.relative ? height.fraction : height.units;
    if (values == 2U && !read_colour(next(0U), declaration.colour)) return false;
    lowering_.declarations.push_back(std::move(declaration));
    return true;
}

// `gradient-dither`: how the material's finished pixels are quantized to
// 8 bits (MaterialDither): `none` rounds, `ordered` dithers by the 8 x 8
// Bayer matrix, `blue-noise` by the 64 x 64 blue-noise table.
bool MaterialReader::lower_gradient_dither() {
    if (value_.components.size() != 1U) {
        return fail(DiagnosticCode::value_syntax, after_first_token(), "one dither");
    }
    const ValueComponent& component = value_.components[0];
    MaterialDeclaration declaration{};
    declaration.field = MaterialField::gradient_dither;
    if (keyword(0U, "none")) {
        declaration.dither = MaterialDither::none;
    } else if (keyword(0U, "ordered")) {
        declaration.dither = MaterialDither::ordered;
    } else if (keyword(0U, "blue-noise")) {
        declaration.dither = MaterialDither::blue_noise;
    } else if (component.kind == ValueComponentKind::keyword) {
        return fail(DiagnosticCode::keyword_unknown, component.token, clipped_theme_detail(component.text));
    } else {
        return fail(DiagnosticCode::value_wrong_kind, component.token, "none, ordered or blue-noise");
    }
    lowering_.declarations.push_back(std::move(declaration));
    return true;
}

// `transform: none | <function>+` (owner 2026-11-16), on a material: the
// functions rotate(<angle>) and scale(<number> [, <number>]) (a number or
// a percentage; one value scales both axes), composed as CSS composes a
// transform list, about the box's centre (MaterialTransform).
bool MaterialReader::lower_transform() {
    MaterialDeclaration declaration{};
    declaration.field = MaterialField::transform;
    const ComponentRange whole{0U, value_.components.size()};
    if (count(whole) == 1U && keyword(0U, "none")) {
        lowering_.declarations.push_back(std::move(declaration));
        return true;
    }
    MaterialTransform composed{};
    std::vector<ComponentRange> parts;
    for (std::size_t index = 0U; index < whole.end; index = next(index)) {
        const ValueComponent& component = value_.components[index];
        const bool function = component.kind == ValueComponentKind::function;
        const bool rotate = function && equals_ascii_folded(component.text, rotate_function_name);
        const bool scale = function && equals_ascii_folded(component.text, scale_function_name);
        if (!rotate && !scale) return fail(DiagnosticCode::value_wrong_kind, component.token, "rotate() or scale()");
        split(ComponentRange{index + 1U, next(index)}, parts);
        MaterialTransform step{};
        if (rotate) {
            if (parts.size() != 1U || count(parts[0]) != 1U) {
                return fail(DiagnosticCode::value_syntax, component.token, "one angle");
            }
            double degrees = 0.0;
            if (!read_angle(parts[0].first, degrees)) return false;
            step = rotation_transform(degrees);
        } else {
            if (parts.size() != 1U && parts.size() != 2U) {
                return fail(DiagnosticCode::value_syntax, component.token, "one or two factors");
            }
            double across = 1.0;
            if (!read_scale_factor(parts[0], across)) return false;
            double down = across;
            if (parts.size() == 2U && !read_scale_factor(parts[1], down)) return false;
            step.xx = across;
            step.yy = down;
        }
        composed = composed_transform(composed, step);
    }
    SurfaceMaterial checked{};
    checked.transform = composed;
    if (!valid_surface_material(checked)) return fail(DiagnosticCode::value_out_of_range, start_, "a transform");
    declaration.transform = composed;
    lowering_.declarations.push_back(std::move(declaration));
    return true;
}

// One scale() factor: a number, or a percentage of 1.
bool MaterialReader::read_scale_factor(ComponentRange part, double& factor) {
    if (count(part) != 1U) {
        const std::uint32_t token =
            part.first < value_.components.size() ? value_.components[part.first].token : start_;
        return fail(DiagnosticCode::value_syntax, token, "one factor");
    }
    const ValueComponent& component = value_.components[part.first];
    const bool number = component.kind == ValueComponentKind::number;
    const bool plain = number && component.unit == ValueUnit::none;
    const bool percent = number && component.unit == ValueUnit::percent;
    if (!plain && !percent) return fail(DiagnosticCode::value_wrong_kind, component.token, "a number or a percentage");
    factor = percent ? component.number / 100.0 : component.number;
    return true;
}

// A clip path's leading fill rule, `nonzero` or `evenodd`, when `part` is
// one such keyword; `read` says whether it was.
bool MaterialReader::read_clip_rule(ComponentRange part, bool& read, MaterialClipRule& rule) {
    read = false;
    if (count(part) != 1U || value_.components[part.first].kind != ValueComponentKind::keyword) return true;
    if (keyword(part.first, "nonzero")) {
        rule = MaterialClipRule::nonzero;
    } else if (keyword(part.first, "evenodd")) {
        rule = MaterialClipRule::even_odd;
    } else {
        const ValueComponent& component = value_.components[part.first];
        return fail(DiagnosticCode::keyword_unknown, component.token, clipped_theme_detail(component.text));
    }
    read = true;
    return true;
}

// polygon([<fill-rule>,] <x> <y>, <x> <y>, ...): at least three vertices,
// each a length (px) or a percentage of the box; em is not admitted, so the
// outline never depends on the part's font.
bool MaterialReader::read_polygon(std::size_t function, MaterialClipPath& clip) {
    const ValueComponent& written = value_.components[function];
    std::vector<ComponentRange> parts;
    split(ComponentRange{function + 1U, next(function)}, parts);
    bool ruled = false;
    if (!read_clip_rule(parts[0], ruled, clip.rule)) return false;
    const std::size_t first_vertex = ruled ? 1U : 0U;
    const std::size_t vertices = parts.size() - first_vertex;
    if (vertices < 3U) return fail(DiagnosticCode::value_syntax, written.token, "at least three vertices");
    if (vertices > MaterialClipPath::maximum_points) {
        return fail(DiagnosticCode::value_out_of_range, written.token, "at most 256 vertices");
    }
    for (std::size_t vertex = first_vertex; vertex < parts.size(); ++vertex) {
        const ComponentRange part = parts[vertex];
        if (count(part) != 2U) return fail(DiagnosticCode::value_syntax, written.token, "a vertex of two lengths");
        const std::size_t across_index = part.first;
        const std::size_t down_index = next(part.first);
        const std::array<std::size_t, 2> indices{across_index, down_index};
        for (const std::size_t index : indices) {
            const ValueComponent& component = value_.components[index];
            if (component.kind == ValueComponentKind::number && component.unit == ValueUnit::em) {
                return fail(DiagnosticCode::value_form_not_admitted, component.token, "an em length in a clip path");
            }
        }
        BoxLength across{};
        BoxLength down{};
        if (!read_length(across_index, true, true, across)) return false;
        if (!read_length(down_index, true, true, down)) return false;
        MaterialClipPoint point{};
        point.x_units = across.units;
        point.x_fraction = across.fraction;
        point.y_units = down.units;
        point.y_fraction = down.fraction;
        clip.points.push_back(point);
        const MaterialClipVerb verb = vertex == first_vertex ? MaterialClipVerb::move : MaterialClipVerb::line;
        clip.verbs.push_back(verb);
    }
    clip.verbs.push_back(MaterialClipVerb::close);
    return true;
}

// path([<fill-rule>,] "<SVG path data>"): coordinates in px from the box's
// top-left; S, T and A lowered to cubics and quadratics (SvgPathReader).
bool MaterialReader::read_svg_path(std::size_t function, MaterialClipPath& clip) {
    const ValueComponent& written = value_.components[function];
    std::vector<ComponentRange> parts;
    split(ComponentRange{function + 1U, next(function)}, parts);
    bool ruled = false;
    if (!read_clip_rule(parts[0], ruled, clip.rule)) return false;
    const std::size_t data_part = ruled ? 1U : 0U;
    if (parts.size() != data_part + 1U || count(parts[data_part]) != 1U) {
        return fail(DiagnosticCode::value_syntax, written.token, "one string of path data");
    }
    const ValueComponent& data = value_.components[parts[data_part].first];
    if (data.kind != ValueComponentKind::string) {
        return fail(DiagnosticCode::value_wrong_kind, data.token, "a string of path data");
    }
    SvgPathReader reader(data.text);
    const SvgPathProblem problem = reader.read(clip);
    switch (problem) {
    case SvgPathProblem::none: return true;
    case SvgPathProblem::too_large:
        return fail(DiagnosticCode::value_out_of_range, data.token, "at most 256 points within 100000px");
    case SvgPathProblem::syntax:
    default: break;
    }
    return fail(DiagnosticCode::value_syntax, data.token, "SVG path data");
}

// `clip-path: none | polygon() | path()` (owner 2026-11-16), on a
// material (MaterialClipPath). Reference boxes, basic shapes other than
// polygon() and url() references are not admitted.
bool MaterialReader::lower_clip_path() {
    MaterialDeclaration declaration{};
    declaration.field = MaterialField::clip_path;
    const ComponentRange whole{0U, value_.components.size()};
    if (count(whole) == 1U && keyword(0U, "none")) {
        lowering_.declarations.push_back(std::move(declaration));
        return true;
    }
    if (count(whole) != 1U) return fail(DiagnosticCode::value_syntax, after_first_token(), "one shape");
    const ValueComponent& component = value_.components[0];
    const bool function = component.kind == ValueComponentKind::function;
    const bool polygon = function && equals_ascii_folded(component.text, polygon_function_name);
    const bool path = function && equals_ascii_folded(component.text, path_function_name);
    if (component.kind == ValueComponentKind::keyword) {
        return fail(DiagnosticCode::value_form_not_admitted, component.token, "a reference box");
    }
    if (!polygon && !path) return fail(DiagnosticCode::value_wrong_kind, component.token, "polygon() or path()");
    MaterialClipPath clip{};
    const bool read = polygon ? read_polygon(0U, clip) : read_svg_path(0U, clip);
    if (!read) return false;
    SurfaceMaterial checked{};
    checked.clip_path = clip;
    if (!valid_surface_material(checked)) return fail(DiagnosticCode::value_out_of_range, start_, "a clip path");
    declaration.clip_path = std::move(clip);
    lowering_.declarations.push_back(std::move(declaration));
    return true;
}

// One glow: a colour and one or two lengths, its blur and its spread.
bool MaterialReader::read_glow(ComponentRange range, MaterialGlow& glow) {
    glow = MaterialGlow{};
    glow.blur_radius = 0.0;
    std::array<double, 2> lengths{};
    std::size_t length_count = 0U;
    bool lengths_done = false;
    bool coloured = false;
    for (std::size_t index = range.first; index < range.end; index = next(index)) {
        const ValueComponent& component = value_.components[index];
        if (component.kind == ValueComponentKind::number) {
            if (lengths_done || length_count == lengths.size()) {
                return fail(DiagnosticCode::value_syntax, component.token, "one or two adjacent lengths");
            }
            BoxLength length{};
            if (!read_length(index, false, false, length)) return false;
            lengths[length_count] = length.units;
            length_count = length_count + 1U;
            continue;
        }
        if (coloured) return fail(DiagnosticCode::value_syntax, component.token, "one colour");
        coloured = true;
        lengths_done = length_count > 0U;
        if (!read_colour(index, glow.color)) return false;
    }
    const std::uint32_t token = range.first < range.end ? value_.components[range.first].token : start_;
    if (length_count == 0U) return fail(DiagnosticCode::value_syntax, token, "one or two lengths");
    if (!coloured) glow.color = current_colour();
    glow.blur_radius = lengths[0];
    glow.spread = lengths[1];
    return true;
}

// `glow`: none, or up to SurfaceMaterial::maximum_glows glows, in the
// order glow-placement reads them.
bool MaterialReader::lower_glows() {
    MaterialDeclaration declaration{};
    declaration.field = MaterialField::glows;
    const ComponentRange whole{0U, value_.components.size()};
    if (count(whole) == 1U && keyword(0U, "none")) {
        lowering_.declarations.push_back(std::move(declaration));
        return true;
    }
    std::vector<ComponentRange> glows{};
    split(whole, glows);
    if (glows.size() > SurfaceMaterial::maximum_glows) {
        return fail(DiagnosticCode::too_many_layers, start_, std::to_string(SurfaceMaterial::maximum_glows) + " glows");
    }
    declaration.glows.resize(glows.size());
    for (std::size_t glow = 0U; glow < glows.size(); ++glow) {
        if (!read_glow(glows[glow], declaration.glows[glow])) return false;
    }
    SurfaceMaterial checked{};
    checked.glows = declaration.glows;
    if (!valid_surface_material(checked)) return fail(DiagnosticCode::value_out_of_range, start_, "a glow");
    lowering_.declarations.push_back(std::move(declaration));
    return true;
}

// `glow-placement`: under or over for each glow, repeated over the glows
// as CSS repeats a short list.
bool MaterialReader::lower_glow_placements() {
    std::vector<ComponentRange> placements{};
    split(ComponentRange{0U, value_.components.size()}, placements);
    MaterialDeclaration declaration{};
    declaration.field = MaterialField::glow_placements;
    for (const ComponentRange& placement : placements) {
        if (count(placement) != 1U) return fail(DiagnosticCode::value_syntax, start_, "one placement per glow");
        const ValueComponent& component = value_.components[placement.first];
        if (keyword(placement.first, "under")) {
            declaration.glow_placements.push_back(MaterialGlowPlacement::under);
        } else if (keyword(placement.first, "over")) {
            declaration.glow_placements.push_back(MaterialGlowPlacement::over);
        } else if (component.kind == ValueComponentKind::keyword) {
            return fail(DiagnosticCode::keyword_unknown, component.token, clipped_theme_detail(component.text));
        } else {
            return fail(DiagnosticCode::value_wrong_kind, component.token, "under or over");
        }
    }
    lowering_.declarations.push_back(std::move(declaration));
    return true;
}

// `keylines`: `none`, or one or more rings separated by commas, each one to
// four colours in CSS's box order (top, right, bottom, left; an omitted
// side takes the opposite one's, the left the right's), each line one unit
// wide. The first ring lies one unit inside the border, each later ring one
// unit inside the ring before it (T4 floor sheet, owner option 1; several
// rings for a bevel two lines deep inside a frame). A ring's lines are
// drawn top, left, right, bottom, so the top and bottom lines lie over the
// side lines' ends; at most SurfaceMaterial::maximum_keylines lines in all.
bool MaterialReader::lower_keylines() {
    MaterialDeclaration declaration{};
    declaration.field = MaterialField::keylines;
    const ComponentRange whole{0U, value_.components.size()};
    if (count(whole) == 1U && keyword(0U, "none")) {
        lowering_.declarations.push_back(std::move(declaration));
        return true;
    }
    std::vector<ComponentRange> rings{};
    split(whole, rings);
    constexpr std::size_t ring_lines = 4U;
    if (rings.size() * ring_lines > SurfaceMaterial::maximum_keylines) {
        return fail(DiagnosticCode::value_syntax, start_, "at most two rings of keylines");
    }
    const std::array<MaterialEdge, ring_lines> drawn_order{MaterialEdge::top, MaterialEdge::left, MaterialEdge::right,
                                                           MaterialEdge::bottom};
    const std::array<std::size_t, ring_lines> side_of_drawn{0U, 3U, 1U, 2U};
    declaration.keylines.reserve(rings.size() * ring_lines);
    for (std::size_t ring = 0U; ring < rings.size(); ++ring) {
        const ComponentRange range = rings[ring];
        const std::size_t written = count(range);
        if (written == 0U || written > ring_lines) {
            return fail(DiagnosticCode::value_syntax, start_, "one to four colours in each ring");
        }
        std::array<Color, ring_lines> sides{};
        std::size_t index = range.first;
        for (std::size_t side = 0U; side < written; ++side) {
            if (!read_colour(index, sides[side])) return false;
            index = next(index);
        }
        // CSS's box expansion: the top's for the bottom, the right's for
        // the left.
        if (written < 2U) sides[1] = sides[0];
        if (written < 3U) sides[2] = sides[0];
        if (written < 4U) sides[3] = sides[1];
        const double inset = 1.0 + static_cast<double>(ring);
        for (std::size_t line = 0U; line < ring_lines; ++line) {
            const MaterialKeyline keyline{drawn_order[line], sides[side_of_drawn[line]], 1.0, inset};
            declaration.keylines.push_back(keyline);
        }
    }
    lowering_.declarations.push_back(std::move(declaration));
    return true;
}

// `mask-image`: none, or up to SurfaceMaterial::maximum_mask_layers
// layers in CSS's order (the top layer first), each one gradient or one
// image, at most one of them an image; an image tiled at its own size
// (CSS's initial mask size and repetition), a fixed gradient resolved now,
// the others when the material is assembled for a part. The layers are
// stored bottom first, read by alpha and added until `mask-mode` and
// `mask-composite` say otherwise.
bool MaterialReader::lower_mask() {
    MaterialDeclaration declaration{};
    declaration.field = MaterialField::mask;
    const ComponentRange whole{0U, value_.components.size()};
    if (count(whole) == 1U && keyword(0U, "none")) {
        lowering_.declarations.push_back(std::move(declaration));
        return true;
    }
    std::vector<ComponentRange> layers{};
    split(whole, layers);
    if (layers.size() > SurfaceMaterial::maximum_mask_layers) {
        return fail(DiagnosticCode::too_many_layers, start_,
                    std::to_string(SurfaceMaterial::maximum_mask_layers) + " mask layers");
    }
    const std::size_t layer_count = layers.size();
    declaration.masks.resize(layer_count);
    std::vector<AuthoredGradient> gradients(layer_count);
    bool deferred = false;
    SurfaceMaterial checked{};
    checked.masks.resize(layer_count);
    for (std::size_t from_top = 0U; from_top < layer_count; ++from_top) {
        const ComponentRange range = layers[from_top];
        if (count(range) != 1U) return fail(DiagnosticCode::value_syntax, start_, "one image in each mask layer");
        WrittenLayer written{};
        if (!read_image(range.first, written)) return false;
        const std::size_t bottom_first = layer_count - 1U - from_top;
        MaterialFillLayer layer = written.image;
        if (!written.imaged) resolve_authored_gradient(written.gradient, Size{100.0, 100.0}, layer);
        checked.masks[bottom_first].source = layer;
        if (written.imaged || fixed_gradient(written.gradient)) {
            declaration.masks[bottom_first] = layer;
            continue;
        }
        declaration.masks[bottom_first] = MaterialFillLayer{};
        gradients[bottom_first] = std::move(written.gradient);
        deferred = true;
    }
    if (!valid_surface_material(checked)) return fail(DiagnosticCode::value_out_of_range, start_, "a mask");
    // One authored gradient per layer when any waits for the part's box;
    // a layer kept as written has a solid entry (MaterialDeclaration).
    if (deferred) declaration.gradients = std::move(gradients);
    lowering_.declarations.push_back(std::move(declaration));
    return true;
}

// `mask-mode`: one keyword per layer, in CSS's order: `alpha`,
// `luminance`, `match-source` (an image's alpha, as CSS reads an image
// layer), or the theme language's `inverse-alpha` and `inverse-luminance`
// (owner 2026-11-16; not CSS).
bool MaterialReader::lower_mask_modes() {
    std::vector<ComponentRange> layers{};
    split(ComponentRange{0U, value_.components.size()}, layers);
    if (layers.size() > SurfaceMaterial::maximum_mask_layers) {
        return fail(DiagnosticCode::too_many_layers, start_, std::to_string(SurfaceMaterial::maximum_mask_layers));
    }
    MaterialDeclaration declaration{};
    declaration.field = MaterialField::mask_modes;
    declaration.mask_modes.reserve(layers.size());
    for (const ComponentRange range : layers) {
        const ValueComponent& component = value_.components[range.first];
        if (count(range) != 1U) return fail(DiagnosticCode::value_syntax, component.token, "one mask mode");
        MaterialMaskMode mode = MaterialMaskMode::alpha;
        if (keyword(range.first, "alpha") || keyword(range.first, "match-source")) {
            mode = MaterialMaskMode::alpha;
        } else if (keyword(range.first, "luminance")) {
            mode = MaterialMaskMode::luminance;
        } else if (keyword(range.first, "inverse-alpha")) {
            mode = MaterialMaskMode::inverse_alpha;
        } else if (keyword(range.first, "inverse-luminance")) {
            mode = MaterialMaskMode::inverse_luminance;
        } else if (component.kind == ValueComponentKind::keyword) {
            return fail(DiagnosticCode::keyword_unknown, component.token, clipped_theme_detail(component.text));
        } else {
            return fail(DiagnosticCode::value_wrong_kind, component.token, "a mask mode");
        }
        declaration.mask_modes.push_back(mode);
    }
    lowering_.declarations.push_back(std::move(declaration));
    return true;
}

// `mask-composite`: one keyword per layer, in CSS's order: `add`,
// `subtract`, `intersect` or `exclude` (MaterialMaskComposite).
bool MaterialReader::lower_mask_composites() {
    std::vector<ComponentRange> layers{};
    split(ComponentRange{0U, value_.components.size()}, layers);
    if (layers.size() > SurfaceMaterial::maximum_mask_layers) {
        return fail(DiagnosticCode::too_many_layers, start_, std::to_string(SurfaceMaterial::maximum_mask_layers));
    }
    MaterialDeclaration declaration{};
    declaration.field = MaterialField::mask_composites;
    declaration.mask_composites.reserve(layers.size());
    for (const ComponentRange range : layers) {
        const ValueComponent& component = value_.components[range.first];
        if (count(range) != 1U) return fail(DiagnosticCode::value_syntax, component.token, "one mask composite");
        MaterialMaskComposite composite = MaterialMaskComposite::add;
        if (keyword(range.first, "add")) {
            composite = MaterialMaskComposite::add;
        } else if (keyword(range.first, "subtract")) {
            composite = MaterialMaskComposite::subtract;
        } else if (keyword(range.first, "intersect")) {
            composite = MaterialMaskComposite::intersect;
        } else if (keyword(range.first, "exclude")) {
            composite = MaterialMaskComposite::exclude;
        } else if (component.kind == ValueComponentKind::keyword) {
            return fail(DiagnosticCode::keyword_unknown, component.token, clipped_theme_detail(component.text));
        } else {
            return fail(DiagnosticCode::value_wrong_kind, component.token, "a mask composite");
        }
        declaration.mask_composites.push_back(composite);
    }
    lowering_.declarations.push_back(std::move(declaration));
    return true;
}

// `reflection` (stage T5, C7): none, or the edge to mirror about (CSS
// -webkit-box-reflect's above, below, left or right), then up to two
// lengths, the gap and the reach (0 and the rest of SurfaceMaterial::
// maximum_reflection_reach by default), then up to two opacities, a number
// or a percentage each, next to the edge and at the far end (0.5 and 0 by
// default). A unitless 0 is a length until an opacity has been read.
bool MaterialReader::lower_reflection() {
    MaterialDeclaration declaration{};
    declaration.field = MaterialField::reflection;
    const ComponentRange whole{0U, value_.components.size()};
    if (count(whole) == 1U && keyword(0U, "none")) {
        lowering_.declarations.push_back(std::move(declaration));
        return true;
    }
    MaterialReflection reflection{};
    if (keyword(0U, "above")) {
        reflection.edge = MaterialEdge::top;
    } else if (keyword(0U, "below")) {
        reflection.edge = MaterialEdge::bottom;
    } else if (keyword(0U, "left")) {
        reflection.edge = MaterialEdge::left;
    } else if (keyword(0U, "right")) {
        reflection.edge = MaterialEdge::right;
    } else {
        const ValueComponent& first = value_.components[0];
        if (first.kind == ValueComponentKind::keyword) {
            return fail(DiagnosticCode::keyword_unknown, first.token, clipped_theme_detail(first.text));
        }
        return fail(DiagnosticCode::value_wrong_kind, first.token, "none, above, below, left or right");
    }
    std::array<double, 2> lengths{};
    std::size_t length_count = 0U;
    std::array<double, 2> opacities{};
    std::size_t opacity_count = 0U;
    for (std::size_t index = next(0U); index < whole.end; index = next(index)) {
        const ValueComponent& component = value_.components[index];
        // The source keyword, last: the backplane under the box.
        if (keyword(index, "backplane")) {
            if (next(index) != whole.end) {
                return fail(DiagnosticCode::value_syntax, component.token, "backplane last");
            }
            reflection.source = MaterialReflectionSource::backplane;
            continue;
        }
        if (component.kind == ValueComponentKind::keyword) {
            return fail(DiagnosticCode::keyword_unknown, component.token, clipped_theme_detail(component.text));
        }
        if (component.kind != ValueComponentKind::number) {
            return fail(DiagnosticCode::value_wrong_kind, component.token, "a length or an opacity");
        }
        const bool zero = component.unit == ValueUnit::none && component.number == 0.0;
        const bool length_unit = component.unit == ValueUnit::px || component.unit == ValueUnit::em;
        const bool is_length = length_unit || (zero && opacity_count == 0U && length_count < lengths.size());
        if (is_length) {
            if (opacity_count > 0U || length_count == lengths.size()) {
                return fail(DiagnosticCode::value_syntax, component.token, "two lengths, then two opacities");
            }
            BoxLength length{};
            if (!read_length(index, false, false, length)) return false;
            lengths[length_count] = length.units;
            length_count = length_count + 1U;
            continue;
        }
        const bool plain = component.unit == ValueUnit::none;
        const bool percent = component.unit == ValueUnit::percent;
        if (!plain && !percent) return fail(DiagnosticCode::value_wrong_kind, component.token, "a length or an opacity");
        if (opacity_count == opacities.size()) {
            return fail(DiagnosticCode::value_syntax, component.token, "at most two opacities");
        }
        opacities[opacity_count] = percent ? component.number / 100.0 : component.number;
        opacity_count = opacity_count + 1U;
    }
    if (length_count > 0U) reflection.gap = lengths[0];
    reflection.reach = length_count > 1U ? lengths[1] : SurfaceMaterial::maximum_reflection_reach - reflection.gap;
    if (opacity_count > 0U) reflection.start_opacity = opacities[0];
    if (opacity_count > 1U) reflection.end_opacity = opacities[1];
    SurfaceMaterial checked{};
    checked.reflection = reflection;
    if (!valid_surface_material(checked)) return fail(DiagnosticCode::value_out_of_range, start_, "a reflection");
    declaration.reflection = reflection;
    lowering_.declarations.push_back(std::move(declaration));
    return true;
}

// -- the properties ---------------------------------------------------------------

bool MaterialReader::lower_background_colour() {
    if (value_.components.size() != 1U) {
        return fail(DiagnosticCode::value_syntax, after_first_token(), "one colour");
    }
    MaterialDeclaration declaration{};
    declaration.field = MaterialField::background_colour;
    if (!read_colour(0U, declaration.colour)) return false;
    lowering_.declarations.push_back(std::move(declaration));
    return true;
}

bool MaterialReader::lower_background_image() {
    const ComponentRange whole{0U, value_.components.size()};
    std::vector<WrittenLayer> written{};
    if (count(whole) == 1U && keyword(0U, "none")) return push_fills(written);
    std::vector<ComponentRange> layers{};
    split(whole, layers);
    if (layers.size() > SurfaceMaterial::maximum_fill_layers) {
        return fail(DiagnosticCode::too_many_layers, start_, std::to_string(SurfaceMaterial::maximum_fill_layers));
    }
    written.resize(layers.size());
    for (std::size_t layer = 0U; layer < layers.size(); ++layer) {
        const ComponentRange range = layers[layer];
        if (count(range) != 1U) return fail(DiagnosticCode::value_syntax, start_, "one image per layer");
        if (keyword(range.first, "none")) {
            // Kept out so that the blend modes count the drawn layers.
            return fail(DiagnosticCode::value_form_not_admitted, value_.components[range.first].token,
                        "none among other layers");
        }
        if (!read_image(range.first, written[layer])) return false;
    }
    return push_fills(written);
}

bool MaterialReader::lower_blend_modes() {
    std::vector<ComponentRange> modes{};
    split(ComponentRange{0U, value_.components.size()}, modes);
    MaterialDeclaration declaration{};
    declaration.field = MaterialField::fill_blend_modes;
    for (const ComponentRange& mode : modes) {
        if (count(mode) != 1U) return fail(DiagnosticCode::value_syntax, start_, "one blend mode per layer");
        const ValueComponent& component = value_.components[mode.first];
        if (keyword(mode.first, "normal")) {
            declaration.blend_modes.push_back(MaterialBlendMode::normal);
        } else if (keyword(mode.first, "multiply")) {
            declaration.blend_modes.push_back(MaterialBlendMode::multiply);
        } else if (keyword(mode.first, "screen")) {
            declaration.blend_modes.push_back(MaterialBlendMode::screen);
        } else if (component.kind == ValueComponentKind::keyword && css_blend_mode_not_admitted(component.text)) {
            return fail(DiagnosticCode::value_form_not_admitted, component.token, clipped_theme_detail(component.text));
        } else if (component.kind == ValueComponentKind::keyword) {
            return fail(DiagnosticCode::keyword_unknown, component.token, clipped_theme_detail(component.text));
        } else {
            return fail(DiagnosticCode::value_wrong_kind, component.token, "a blend mode");
        }
    }
    lowering_.declarations.push_back(std::move(declaration));
    return true;
}

// One layer of the shorthand: an image or `none`, a position with an
// optional `/ size`, a repetition, and, in the last layer, a colour. A
// layer's attachment, origin and clip are not admitted: the engine draws
// every layer over the whole box, as it scrolls.
bool MaterialReader::read_background_layer(ComponentRange range, bool last, MaterialDeclaration& colour,
                                           std::vector<WrittenLayer>& written, BackgroundLayer& layer) {
    if (range.first >= range.end) return fail(DiagnosticCode::value_syntax, start_, "a background layer");
    bool coloured = false;
    std::size_t index = range.first;
    while (index < range.end) {
        const ValueComponent& component = value_.components[index];
        const bool image =
            component.kind == ValueComponentKind::function || component.kind == ValueComponentKind::resource;
        if (image || keyword(index, "none")) {
            if (layer.imaged || layer.none) {
                return fail(DiagnosticCode::value_syntax, component.token, "one image per layer");
            }
            layer.none = !image;
            layer.imaged = image;
            if (image) {
                written.emplace_back();
                if (!read_image(index, written.back())) return false;
            }
            index = next(index);
            continue;
        }
        if (component.kind == ValueComponentKind::number || position_word(index) != PositionWord::none) {
            if (layer.positioned) return fail(DiagnosticCode::value_syntax, component.token, "one position");
            const std::size_t first = index;
            while (index < range.end && (value_.components[index].kind == ValueComponentKind::number ||
                                         position_word(index) != PositionWord::none)) {
                index = next(index);
            }
            if (!read_image_position(ComponentRange{first, index}, layer.position)) return false;
            layer.positioned = true;
            if (index >= range.end || value_.components[index].kind != ValueComponentKind::slash) continue;
            index = next(index);
            const std::size_t size_first = index;
            while (index < range.end && size_component(index)) index = next(index);
            if (!read_image_size(ComponentRange{size_first, index}, layer.size)) return false;
            layer.sized = true;
            continue;
        }
        if (component.kind == ValueComponentKind::slash) {
            return fail(DiagnosticCode::value_syntax, component.token, "a size after a position");
        }
        if (repeat_word(index)) {
            if (layer.repeated) return fail(DiagnosticCode::value_syntax, component.token, "one repetition");
            const std::size_t first = index;
            while (index < range.end && repeat_word(index)) index = next(index);
            if (!read_image_repeat(ComponentRange{first, index}, layer.repeat)) return false;
            layer.repeated = true;
            continue;
        }
        const bool named =
            named_colour(component.text).has_value() || equals_ascii_folded(component.text, "currentcolor");
        const bool colour_word = component.kind == ValueComponentKind::keyword && named;
        if (component.kind == ValueComponentKind::colour || colour_word) {
            if (!last || coloured) {
                return fail(DiagnosticCode::value_syntax, component.token, "a colour only in the last layer");
            }
            coloured = true;
            if (!read_colour(index, colour.colour)) return false;
            index = next(index);
            continue;
        }
        if (component.kind == ValueComponentKind::keyword) {
            return fail(DiagnosticCode::value_form_not_admitted, component.token,
                        "a background layer's attachment, origin or clip");
        }
        return fail(DiagnosticCode::value_wrong_kind, component.token, "a background layer");
    }
    return true;
}

// The shorthand: the image layers with their positions, sizes and
// repetitions (CSS's initial ones where a layer names none), and the last
// layer's colour (CSS's initial, transparent, when it names none). A
// gradient layer covers the box, so one written with a position, size or
// repetition other than the initial is not admitted.
bool MaterialReader::lower_background() {
    std::vector<ComponentRange> ranges{};
    split(ComponentRange{0U, value_.components.size()}, ranges);
    if (ranges.size() > SurfaceMaterial::maximum_fill_layers) {
        return fail(DiagnosticCode::too_many_layers, start_, std::to_string(SurfaceMaterial::maximum_fill_layers));
    }
    MaterialDeclaration colour{};
    colour.field = MaterialField::background_colour;
    colour.colour = Color::rgba(0, 0, 0, 0);
    MaterialDeclaration positions{};
    positions.field = MaterialField::fill_positions;
    MaterialDeclaration sizes{};
    sizes.field = MaterialField::fill_sizes;
    MaterialDeclaration repeats{};
    repeats.field = MaterialField::fill_repeats;
    std::vector<WrittenLayer> written{};
    written.reserve(ranges.size());
    for (std::size_t index = 0U; index < ranges.size(); ++index) {
        const bool last = index + 1U == ranges.size();
        const std::size_t before = written.size();
        BackgroundLayer layer{};
        if (!read_background_layer(ranges[index], last, colour, written, layer)) return false;
        if (layer.none && ranges.size() > 1U) {
            return fail(DiagnosticCode::value_form_not_admitted, value_.components[ranges[index].first].token,
                        "none among other layers");
        }
        const bool gradient = written.size() > before && (!written.back().imaged || written.back().patterned);
        const bool placed = !(layer.position == MaterialImagePosition{}) || !(layer.size == MaterialImageSize{}) ||
                            layer.repeat != MaterialImageRepeat::repeat;
        if (gradient && placed) {
            return fail(DiagnosticCode::value_form_not_admitted, value_.components[ranges[index].first].token,
                        "a gradient or pattern layer's position, size or repetition");
        }
        if (written.size() == before) continue;
        positions.image_positions.push_back(layer.position);
        sizes.image_sizes.push_back(layer.size);
        repeats.image_repeats.push_back(layer.repeat);
    }
    lowering_.declarations.push_back(std::move(colour));
    lowering_.declarations.push_back(std::move(positions));
    lowering_.declarations.push_back(std::move(sizes));
    lowering_.declarations.push_back(std::move(repeats));
    return push_fills(written);
}

// -- the recipe's own fields (P6f) ------------------------------------------------

// A one-value recipe field: `color`, an outline longhand.
RecipeDeclaration recipe_declaration(RecipeField field) noexcept {
    RecipeDeclaration declaration{};
    declaration.field = field;
    return declaration;
}

bool MaterialReader::outline_style_word(std::size_t index) const noexcept {
    const bool word = keyword(index, "auto") || style_word(index);
    return word;
}

// An outline's style: `auto` is the theme's ring, `solid` an outline
// outside the bounds, `none` none. The ring is stroked solid, so the other
// line styles are not admitted; `hidden` is not an outline style.
bool MaterialReader::read_outline_style(std::size_t index, RecipeOutlineStyle& style) {
    const ValueComponent& component = value_.components[index];
    if (component.kind != ValueComponentKind::keyword) {
        return fail(DiagnosticCode::value_wrong_kind, component.token, "an outline style");
    }
    if (keyword(index, "auto")) {
        style = RecipeOutlineStyle::ring;
        return true;
    }
    if (keyword(index, "solid")) {
        style = RecipeOutlineStyle::solid;
        return true;
    }
    if (keyword(index, "dotted")) {
        style = RecipeOutlineStyle::dotted;
        return true;
    }
    if (keyword(index, "none")) {
        style = RecipeOutlineStyle::none;
        return true;
    }
    if (style_word(index) && !keyword(index, "hidden")) {
        return fail(DiagnosticCode::value_form_not_admitted, component.token, clipped_theme_detail(component.text));
    }
    return fail(DiagnosticCode::keyword_unknown, component.token, clipped_theme_detail(component.text));
}

// An outline's width: a <line-width>, at most maximum_recipe_outline_width.
bool MaterialReader::read_outline_width(std::size_t index, double& width) {
    if (!read_width(index, width)) return false;
    if (width > maximum_recipe_outline_width) {
        const ValueComponent& component = value_.components[index];
        return fail(DiagnosticCode::value_out_of_range, component.token, clipped_theme_detail(component.text));
    }
    return true;
}

// `color`: one colour, the recipe's text colour.
bool MaterialReader::lower_text_colour() {
    if (value_.components.size() != 1U) {
        return fail(DiagnosticCode::value_syntax, after_first_token(), "one colour");
    }
    RecipeDeclaration declaration = recipe_declaration(RecipeField::text);
    if (!read_colour(0U, declaration.colour)) return false;
    lowering_.recipe_fields.push_back(declaration);
    return true;
}

// `muted-color` and `glyph-color`: one colour, the recipe's muted text or
// glyph colour (`field`).
bool MaterialReader::lower_recipe_colour(RecipeField field) {
    if (value_.components.size() != 1U) {
        return fail(DiagnosticCode::value_syntax, after_first_token(), "one colour");
    }
    RecipeDeclaration declaration = recipe_declaration(field);
    if (!read_colour(0U, declaration.colour)) return false;
    lowering_.recipe_fields.push_back(declaration);
    return true;
}

// `stroke-width`: one length, an arc's thickness and a needle's width at
// its hub, at most maximum_recipe_stroke_width.
bool MaterialReader::lower_stroke_width() {
    if (value_.components.size() != 1U) {
        return fail(DiagnosticCode::value_syntax, after_first_token(), "one width");
    }
    RecipeDeclaration declaration = recipe_declaration(RecipeField::stroke_width);
    if (!read_width(0U, declaration.number)) return false;
    if (declaration.number > maximum_recipe_stroke_width) {
        const ValueComponent& component = value_.components[0U];
        return fail(DiagnosticCode::value_out_of_range, component.token, clipped_theme_detail(component.text));
    }
    lowering_.recipe_fields.push_back(declaration);
    return true;
}

// `needle-shape`: `line`, `tapered` or `dot` (NeedleShape).
bool MaterialReader::lower_needle_shape() {
    if (value_.components.size() != 1U) {
        return fail(DiagnosticCode::value_syntax, after_first_token(), "one needle shape");
    }
    const ValueComponent& component = value_.components[0U];
    if (component.kind != ValueComponentKind::keyword) {
        return fail(DiagnosticCode::value_wrong_kind, component.token, "a needle shape");
    }
    RecipeDeclaration declaration = recipe_declaration(RecipeField::needle_shape);
    if (keyword(0U, "line")) {
        declaration.needle_shape = NeedleShape::line;
    } else if (keyword(0U, "tapered")) {
        declaration.needle_shape = NeedleShape::tapered;
    } else if (keyword(0U, "dot")) {
        declaration.needle_shape = NeedleShape::dot;
    } else {
        return fail(DiagnosticCode::keyword_unknown, component.token, clipped_theme_detail(component.text));
    }
    lowering_.recipe_fields.push_back(declaration);
    return true;
}

// `default-outline`: a width and a colour, each at most once and in any
// order, the default button's second perimeter; one it omits takes its
// `initial` (the floor's value).
bool MaterialReader::lower_default_outline() {
    RecipeDeclaration width = recipe_declaration(RecipeField::default_outline_width);
    RecipeDeclaration colour = recipe_declaration(RecipeField::default_outline_colour);
    bool sized = false;
    bool coloured = false;
    for (std::size_t index = 0U; index < value_.components.size(); index = next(index)) {
        const ValueComponent& component = value_.components[index];
        if (component.kind == ValueComponentKind::number || width_word(index)) {
            if (sized) return fail(DiagnosticCode::value_syntax, component.token, "one width");
            sized = true;
            if (!read_outline_width(index, width.number)) return false;
            continue;
        }
        if (coloured) return fail(DiagnosticCode::value_syntax, component.token, "one colour");
        coloured = true;
        if (!read_colour(index, colour.colour)) return false;
    }
    width.initial = !sized;
    colour.initial = !coloured;
    lowering_.recipe_fields.push_back(width);
    lowering_.recipe_fields.push_back(colour);
    return true;
}

// `outline`: a colour, a style and a width, each at most once and in any
// order; one it omits takes its `initial` (the floor's value). The offset
// is not the shorthand's.
bool MaterialReader::lower_outline() {
    RecipeDeclaration style = recipe_declaration(RecipeField::outline_style);
    RecipeDeclaration colour = recipe_declaration(RecipeField::outline_colour);
    RecipeDeclaration width = recipe_declaration(RecipeField::outline_width);
    bool styled = false;
    bool coloured = false;
    bool sized = false;
    for (std::size_t index = 0U; index < value_.components.size(); index = next(index)) {
        const ValueComponent& component = value_.components[index];
        if (outline_style_word(index)) {
            if (styled) return fail(DiagnosticCode::value_syntax, component.token, "one outline style");
            styled = true;
            if (!read_outline_style(index, style.outline_style)) return false;
            continue;
        }
        if (component.kind == ValueComponentKind::number || width_word(index)) {
            if (sized) return fail(DiagnosticCode::value_syntax, component.token, "one outline width");
            sized = true;
            if (!read_outline_width(index, width.number)) return false;
            continue;
        }
        if (coloured) return fail(DiagnosticCode::value_syntax, component.token, "one outline colour");
        coloured = true;
        if (!read_colour(index, colour.colour)) return false;
    }
    style.initial = !styled;
    colour.initial = !coloured;
    width.initial = !sized;
    lowering_.recipe_fields.push_back(style);
    lowering_.recipe_fields.push_back(colour);
    lowering_.recipe_fields.push_back(width);
    return true;
}

bool MaterialReader::lower_outline_style() {
    if (value_.components.size() != 1U) {
        return fail(DiagnosticCode::value_syntax, after_first_token(), "one outline style");
    }
    RecipeDeclaration declaration = recipe_declaration(RecipeField::outline_style);
    if (!read_outline_style(0U, declaration.outline_style)) return false;
    lowering_.recipe_fields.push_back(declaration);
    return true;
}

bool MaterialReader::lower_outline_colour() {
    if (value_.components.size() != 1U) {
        return fail(DiagnosticCode::value_syntax, after_first_token(), "one colour");
    }
    RecipeDeclaration declaration = recipe_declaration(RecipeField::outline_colour);
    if (!read_colour(0U, declaration.colour)) return false;
    lowering_.recipe_fields.push_back(declaration);
    return true;
}

bool MaterialReader::lower_outline_width() {
    if (value_.components.size() != 1U) {
        return fail(DiagnosticCode::value_syntax, after_first_token(), "one width");
    }
    RecipeDeclaration declaration = recipe_declaration(RecipeField::outline_width);
    if (!read_outline_width(0U, declaration.number)) return false;
    lowering_.recipe_fields.push_back(declaration);
    return true;
}

// `outline-offset`: a length, negative inward, within
// ±maximum_recipe_outline_offset. The theme's ring (`auto`) has none.
bool MaterialReader::lower_outline_offset() {
    if (value_.components.size() != 1U) {
        return fail(DiagnosticCode::value_syntax, after_first_token(), "one length");
    }
    BoxLength length{};
    if (!read_length(0U, false, true, length)) return false;
    if (std::abs(length.units) > maximum_recipe_outline_offset) {
        const ValueComponent& component = value_.components[0];
        return fail(DiagnosticCode::value_out_of_range, component.token, clipped_theme_detail(component.text));
    }
    RecipeDeclaration declaration = recipe_declaration(RecipeField::outline_offset);
    declaration.number = length.units;
    lowering_.recipe_fields.push_back(declaration);
    return true;
}

// A part metric (`height`, `min-width`, `padding-inline`, `gap`,
// `margin-top`, `margin-bottom`): one length, negative only for a margin,
// within `maximum` either way.
// A part's metric: one length in [minimum, maximum] (a negative minimum
// admits negative lengths: a margin running the part into its neighbour, or
// a gap overlapping the parts it separates).
bool MaterialReader::lower_part_length(RecipeField field, double minimum, double maximum) {
    if (value_.components.size() != 1U) {
        return fail(DiagnosticCode::value_syntax, after_first_token(), "one length");
    }
    BoxLength length{};
    const bool negatives = minimum < 0.0;
    if (!read_length(0U, false, negatives, length)) return false;
    if (length.units < minimum || length.units > maximum) {
        const ValueComponent& component = value_.components[0];
        return fail(DiagnosticCode::value_out_of_range, component.token, clipped_theme_detail(component.text));
    }
    RecipeDeclaration declaration = recipe_declaration(field);
    declaration.number = length.units;
    lowering_.recipe_fields.push_back(declaration);
    return true;
}

// `stripe-pitch`: `auto` (lowered as 0: the fill's inner thickness) or one
// length from minimum_stripe_pitch to maximum_stripe_pitch.
bool MaterialReader::lower_stripe_pitch() {
    if (value_.components.size() != 1U) {
        return fail(DiagnosticCode::value_syntax, after_first_token(), "auto or one length");
    }
    RecipeDeclaration declaration = recipe_declaration(RecipeField::stripe_pitch);
    if (keyword(0U, "auto")) {
        declaration.number = 0.0;
        lowering_.recipe_fields.push_back(declaration);
        return true;
    }
    BoxLength length{};
    if (!read_length(0U, false, false, length)) return false;
    if (length.units < minimum_stripe_pitch || length.units > maximum_stripe_pitch) {
        const ValueComponent& component = value_.components[0U];
        return fail(DiagnosticCode::value_out_of_range, component.token, clipped_theme_detail(component.text));
    }
    declaration.number = length.units;
    lowering_.recipe_fields.push_back(declaration);
    return true;
}

// `stripe-ratio` and `stripe-speed`: one plain number within the field's
// range.
bool MaterialReader::lower_stripe_number(RecipeField field, double minimum, double maximum) {
    if (value_.components.size() != 1U) {
        return fail(DiagnosticCode::value_syntax, after_first_token(), "one number");
    }
    const ValueComponent& component = value_.components[0U];
    const bool plain = component.kind == ValueComponentKind::number && component.unit == ValueUnit::none;
    if (!plain) return fail(DiagnosticCode::value_wrong_kind, component.token, "a number");
    if (!(component.number >= minimum && component.number <= maximum)) {
        return fail(DiagnosticCode::value_out_of_range, component.token, clipped_theme_detail(component.text));
    }
    RecipeDeclaration declaration = recipe_declaration(field);
    declaration.number = component.number;
    lowering_.recipe_fields.push_back(declaration);
    return true;
}

// `stripe-angle`: one angle from minimum_stripe_angle to
// maximum_stripe_angle degrees.
bool MaterialReader::lower_stripe_angle() {
    if (value_.components.size() != 1U) {
        return fail(DiagnosticCode::value_syntax, after_first_token(), "one angle");
    }
    RecipeDeclaration declaration = recipe_declaration(RecipeField::stripe_angle);
    if (!read_angle(0U, declaration.number)) return false;
    if (!(declaration.number >= minimum_stripe_angle && declaration.number <= maximum_stripe_angle)) {
        const ValueComponent& component = value_.components[0U];
        return fail(DiagnosticCode::value_out_of_range, component.token, clipped_theme_detail(component.text));
    }
    lowering_.recipe_fields.push_back(declaration);
    return true;
}

// `stripe-lean` and `stripe-direction`: `forward` or `backward`
// (StripeSense).
bool MaterialReader::lower_stripe_sense(RecipeField field) {
    if (value_.components.size() != 1U) {
        return fail(DiagnosticCode::value_syntax, after_first_token(), "forward or backward");
    }
    const ValueComponent& component = value_.components[0U];
    if (component.kind != ValueComponentKind::keyword) {
        return fail(DiagnosticCode::value_wrong_kind, component.token, "forward or backward");
    }
    RecipeDeclaration declaration = recipe_declaration(field);
    if (keyword(0U, "forward")) {
        declaration.stripe_sense = StripeSense::forward;
    } else if (keyword(0U, "backward")) {
        declaration.stripe_sense = StripeSense::backward;
    } else {
        return fail(DiagnosticCode::keyword_unknown, component.token, clipped_theme_detail(component.text));
    }
    lowering_.recipe_fields.push_back(declaration);
    return true;
}

// `stripe-gloss`: `auto` or `none` (StripeGloss).
bool MaterialReader::lower_stripe_gloss() {
    if (value_.components.size() != 1U) {
        return fail(DiagnosticCode::value_syntax, after_first_token(), "auto or none");
    }
    const ValueComponent& component = value_.components[0U];
    if (component.kind != ValueComponentKind::keyword) {
        return fail(DiagnosticCode::value_wrong_kind, component.token, "auto or none");
    }
    RecipeDeclaration declaration = recipe_declaration(RecipeField::stripe_gloss);
    if (keyword(0U, "auto")) {
        declaration.stripe_gloss = StripeGloss::automatic;
    } else if (keyword(0U, "none")) {
        declaration.stripe_gloss = StripeGloss::none;
    } else {
        return fail(DiagnosticCode::keyword_unknown, component.token, clipped_theme_detail(component.text));
    }
    lowering_.recipe_fields.push_back(declaration);
    return true;
}

// `stripe-reduced-motion`: `still` or `none` (StripeReducedMotion).
bool MaterialReader::lower_stripe_reduced_motion() {
    if (value_.components.size() != 1U) {
        return fail(DiagnosticCode::value_syntax, after_first_token(), "still or none");
    }
    const ValueComponent& component = value_.components[0U];
    if (component.kind != ValueComponentKind::keyword) {
        return fail(DiagnosticCode::value_wrong_kind, component.token, "still or none");
    }
    RecipeDeclaration declaration = recipe_declaration(RecipeField::stripe_reduced_motion);
    if (keyword(0U, "still")) {
        declaration.stripe_reduced_motion = StripeReducedMotion::still;
    } else if (keyword(0U, "none")) {
        declaration.stripe_reduced_motion = StripeReducedMotion::none;
    } else {
        return fail(DiagnosticCode::keyword_unknown, component.token, clipped_theme_detail(component.text));
    }
    lowering_.recipe_fields.push_back(declaration);
    return true;
}

// `stroke-dash` (the node canvas, cp31): `none`, or two lengths, the dash
// and the gap, each at most maximum_canvas_dash. A dash of 0 with a gap
// would draw nothing and is refused; a gap of 0 draws solid.
bool MaterialReader::lower_stroke_dash() {
    RecipeDeclaration dash = recipe_declaration(RecipeField::stroke_dash);
    RecipeDeclaration gap = recipe_declaration(RecipeField::stroke_gap);
    const ValueComponent& first = value_.components[0U];
    const std::size_t second_index = next(0U);
    const bool one = second_index >= value_.components.size();
    if (one && keyword(0U, "none")) {
        lowering_.recipe_fields.push_back(dash);
        lowering_.recipe_fields.push_back(gap);
        return true;
    }
    if (one || next(second_index) < value_.components.size()) {
        return fail(DiagnosticCode::value_syntax, first.token, "none or a dash and a gap");
    }
    BoxLength dash_length{};
    BoxLength gap_length{};
    if (!read_length(0U, false, false, dash_length)) return false;
    if (!read_length(second_index, false, false, gap_length)) return false;
    if (dash_length.units > maximum_canvas_dash) {
        return fail(DiagnosticCode::value_out_of_range, first.token, clipped_theme_detail(first.text));
    }
    const ValueComponent& second = value_.components[second_index];
    if (gap_length.units > maximum_canvas_dash) {
        return fail(DiagnosticCode::value_out_of_range, second.token, clipped_theme_detail(second.text));
    }
    if (dash_length.units == 0.0 && gap_length.units > 0.0) {
        return fail(DiagnosticCode::value_out_of_range, first.token, "a dash of 0 draws nothing");
    }
    dash.number = dash_length.units;
    gap.number = gap_length.units;
    lowering_.recipe_fields.push_back(dash);
    lowering_.recipe_fields.push_back(gap);
    return true;
}

// `halo` (the node canvas, cp31): `none`, or a width (at most
// maximum_canvas_halo_width) and a colour, each once, in any order.
bool MaterialReader::lower_halo() {
    RecipeDeclaration width = recipe_declaration(RecipeField::halo_width);
    RecipeDeclaration colour = recipe_declaration(RecipeField::halo_colour);
    const bool one = next(0U) >= value_.components.size();
    if (one && keyword(0U, "none")) {
        colour.colour = Color::rgba(0, 0, 0, 0);
        lowering_.recipe_fields.push_back(width);
        lowering_.recipe_fields.push_back(colour);
        return true;
    }
    bool sized = false;
    bool coloured = false;
    for (std::size_t index = 0U; index < value_.components.size(); index = next(index)) {
        const ValueComponent& component = value_.components[index];
        if (component.kind == ValueComponentKind::number) {
            if (sized) return fail(DiagnosticCode::value_syntax, component.token, "one width");
            sized = true;
            BoxLength read{};
            if (!read_length(index, false, false, read)) return false;
            if (read.units > maximum_canvas_halo_width) {
                return fail(DiagnosticCode::value_out_of_range, component.token, clipped_theme_detail(component.text));
            }
            width.number = read.units;
            continue;
        }
        if (coloured) return fail(DiagnosticCode::value_syntax, component.token, "one colour");
        coloured = true;
        if (!read_colour(index, colour.colour)) return false;
    }
    if (!sized || !coloured) {
        return fail(DiagnosticCode::value_syntax, value_.components[0U].token, "none or a width and a colour");
    }
    lowering_.recipe_fields.push_back(width);
    lowering_.recipe_fields.push_back(colour);
    return true;
}

// `flow` (the node canvas, cp31): `none`, `dots` or `dashes`
// (CanvasFlowStyle).
bool MaterialReader::lower_flow_style() {
    if (next(0U) < value_.components.size()) {
        return fail(DiagnosticCode::value_syntax, after_first_token(), "none, dots or dashes");
    }
    const ValueComponent& component = value_.components[0U];
    if (component.kind != ValueComponentKind::keyword) {
        return fail(DiagnosticCode::value_wrong_kind, component.token, "none, dots or dashes");
    }
    RecipeDeclaration declaration = recipe_declaration(RecipeField::flow_style);
    if (keyword(0U, "none")) {
        declaration.flow_style = CanvasFlowStyle::none;
    } else if (keyword(0U, "dots")) {
        declaration.flow_style = CanvasFlowStyle::dots;
    } else if (keyword(0U, "dashes")) {
        declaration.flow_style = CanvasFlowStyle::dashes;
    } else {
        return fail(DiagnosticCode::keyword_unknown, component.token, clipped_theme_detail(component.text));
    }
    lowering_.recipe_fields.push_back(declaration);
    return true;
}

// `background-texture` (NODE_GRAPH_SPEC gap 2; cp31): `none`, or one to
// maximum_canvas_texture_layers comma-separated layers, bottom first, each
// `procedural(<kind>, <seed>, <tile>, <strength>) multiply <percentage>`
// (read_texture_layer). The layers are baked procedural surfaces, never an
// image: the engine generates each pattern per display scale in an idle
// frame (E10) and the canvas composites them under its grid. A contrast
// above 3 % (the design language's sub-3 % texture rule,
// maximum_canvas_texture_contrast) is out of range: the declaration is
// dropped, never clamped silently. `multiply` is the only blend, and is
// written; the canvas screens instead on a dark well.
bool MaterialReader::lower_background_texture() {
    RecipeDeclaration texture = recipe_declaration(RecipeField::texture_layers);
    const ValueComponent& first = value_.components[0U];
    if (next(0U) >= value_.components.size() && keyword(0U, "none")) {
        lowering_.recipe_fields.push_back(texture);
        return true;
    }
    std::vector<ComponentRange> layers{};
    split(ComponentRange{0U, value_.components.size()}, layers);
    if (layers.size() > maximum_canvas_texture_layers) {
        return fail(DiagnosticCode::value_out_of_range, first.token, "at most three texture layers");
    }
    for (const ComponentRange& range : layers) {
        CanvasTextureLayer& layer = texture.texture.items[texture.texture.count];
        if (!read_texture_layer(range, layer)) return false;
        texture.texture.count = texture.texture.count + 1U;
    }
    lowering_.recipe_fields.push_back(texture);
    return true;
}

// One texture layer: `procedural(...)` (read_procedural) or `finish(...)`
// (read_finish, as a fill writes it; any finish but a glint), then
// `multiply` and a percentage from 0% to 3%.
bool MaterialReader::read_texture_layer(ComponentRange range, CanvasTextureLayer& layer) {
    const std::uint32_t token = range.first < range.end ? value_.components[range.first].token : start_;
    if (count(range) != 3U) {
        return fail(DiagnosticCode::value_syntax, token,
                    "none, or procedural(...) or finish(...), multiply <percentage>");
    }
    const std::size_t function_index = range.first;
    const std::size_t blend_index = next(function_index);
    const std::size_t contrast_index = next(blend_index);
    const ValueComponent& function = value_.components[function_index];
    const bool written = function.kind == ValueComponentKind::function;
    const bool procedural = written && equals_ascii_folded(function.text, procedural_function_name);
    const bool finished = written && equals_ascii_folded(function.text, finish_function_name);
    if (!procedural && !finished) {
        return fail(DiagnosticCode::value_wrong_kind, function.token, "procedural(...) or finish(...)");
    }
    if (procedural) {
        layer.source = CanvasTextureSource::procedural;
        if (!read_procedural(function_index, layer)) return false;
    } else {
        MaterialFillLayer read{};
        if (!read_finish(function_index, read)) return false;
        if (read.finish.kind == FinishKind::glint) {
            const std::size_t kind_index = function_index + 1U;
            return fail(DiagnosticCode::value_wrong_kind, value_.components[kind_index].token,
                        "a finish other than a glint");
        }
        layer.source = CanvasTextureSource::finish;
        layer.finish = read.finish;
    }
    const ValueComponent& blend = value_.components[blend_index];
    if (blend.kind != ValueComponentKind::keyword) {
        return fail(DiagnosticCode::value_wrong_kind, blend.token, "multiply");
    }
    if (!keyword(blend_index, "multiply")) {
        return fail(DiagnosticCode::keyword_unknown, blend.token, clipped_theme_detail(blend.text));
    }
    const ValueComponent& amount = value_.components[contrast_index];
    const bool percentage = amount.kind == ValueComponentKind::number && amount.unit == ValueUnit::percent;
    if (!percentage) return fail(DiagnosticCode::value_wrong_kind, amount.token, "a percentage");
    const double fraction = amount.number / 100.0;
    if (!(fraction >= 0.0 && fraction <= maximum_canvas_texture_contrast)) {
        return fail(DiagnosticCode::value_out_of_range, amount.token, clipped_theme_detail(amount.text));
    }
    layer.contrast = fraction;
    return true;
}

// `procedural(<kind>, <seed>, <tile>, <strength>)`: a tiled built-in
// pattern (canvas_texture_kinds), a whole seed from 0 to 4294967295, a tile
// length from 2px to 96px (maximum_canvas_texture_tile) and a strength
// from 0 to 1.
bool MaterialReader::read_procedural(std::size_t index, CanvasTextureLayer& layer) {
    const ValueComponent& function = value_.components[index];
    std::vector<ComponentRange> arguments{};
    split(ComponentRange{index + 1U, function.subtree_end}, arguments);
    if (arguments.size() != 4U) {
        return fail(DiagnosticCode::value_syntax, function.token, "a kind, a seed, a tile and a strength");
    }
    for (const ComponentRange& argument : arguments) {
        if (count(argument) != 1U) return fail(DiagnosticCode::value_syntax, function.token, "one value per argument");
    }
    const CanvasTextureKind* named = nullptr;
    for (const CanvasTextureKind& candidate : canvas_texture_kinds) {
        if (keyword(arguments[0].first, candidate.name)) named = &candidate;
    }
    if (named == nullptr) {
        return fail(DiagnosticCode::value_wrong_kind, value_.components[arguments[0].first].token,
                    "felt, wood-grain, paper-fibre, pinstripe, brushed-metal, checker or a hatch-* style");
    }
    const ValueComponent& seed = value_.components[arguments[1].first];
    const bool whole = seed.kind == ValueComponentKind::number && seed.unit == ValueUnit::none &&
                       std::isfinite(seed.number) && std::floor(seed.number) == seed.number;
    if (!whole) return fail(DiagnosticCode::value_wrong_kind, seed.token, "a whole seed");
    if (!(seed.number >= 0.0 && seed.number <= maximum_texture_seed)) {
        return fail(DiagnosticCode::value_out_of_range, seed.token, clipped_theme_detail(seed.text));
    }
    BoxLength tile{};
    if (!read_length(arguments[2].first, false, false, tile)) return false;
    if (!(tile.units >= minimum_procedural_tile && tile.units <= maximum_canvas_texture_tile)) {
        return fail(DiagnosticCode::value_out_of_range, value_.components[arguments[2].first].token,
                    "a tile from 2px to 96px");
    }
    const ValueComponent& strength = value_.components[arguments[3].first];
    if (strength.kind != ValueComponentKind::number || strength.unit != ValueUnit::none) {
        return fail(DiagnosticCode::value_wrong_kind, strength.token, "a strength from 0 to 1");
    }
    if (!(strength.number >= 0.0 && strength.number <= 1.0)) {
        return fail(DiagnosticCode::value_out_of_range, strength.token, clipped_theme_detail(strength.text));
    }
    layer.pattern = (*named).pattern;
    layer.hatch_style = (*named).hatch_style;
    layer.seed = static_cast<std::uint32_t>(seed.number);
    layer.tile = tile.units;
    layer.strength = strength.number;
    return true;
}

// A part's shape: `taper` and `flare` (a length and an optional colour) and
// `edge-shadow` (a length and a colour), each at most once and in any
// order. An omitted colour takes its `initial` (none: the part's own).
bool MaterialReader::lower_part_shape(RecipeField length_field, RecipeField colour_field, bool colour_required) {
    RecipeDeclaration length = recipe_declaration(length_field);
    RecipeDeclaration colour = recipe_declaration(colour_field);
    bool sized = false;
    bool coloured = false;
    for (std::size_t index = 0U; index < value_.components.size(); index = next(index)) {
        const ValueComponent& component = value_.components[index];
        if (component.kind == ValueComponentKind::number) {
            if (sized) return fail(DiagnosticCode::value_syntax, component.token, "one length");
            sized = true;
            BoxLength read{};
            if (!read_length(index, false, false, read)) return false;
            if (read.units > maximum_part_shape) {
                return fail(DiagnosticCode::value_out_of_range, component.token, clipped_theme_detail(component.text));
            }
            length.number = read.units;
            continue;
        }
        if (coloured) return fail(DiagnosticCode::value_syntax, component.token, "one colour");
        coloured = true;
        if (!read_colour(index, colour.colour)) return false;
    }
    if (!sized) return fail(DiagnosticCode::value_syntax, after_first_token(), "a length");
    if (colour_required && !coloured) {
        return fail(DiagnosticCode::value_syntax, after_first_token(), "a length and a colour");
    }
    colour.initial = !coloured;
    lowering_.recipe_fields.push_back(length);
    lowering_.recipe_fields.push_back(colour);
    return true;
}

// `font-family` (T4 fonts; named families, owner decision of 2026-11-10;
// stacks and generics, owner decisions of 2026-11-13): a comma-separated
// list of bundled family names (gui_forms/typography/font_families/font_families.hpp),
// each quoted or written as words, in any ASCII case, and CSS generic
// families, unquoted (`sans-serif`, `serif`, `monospace`, `system-ui`, the
// `ui-*` names and the rest of generic_font_families, each drawing the
// bundled family that table chooses), optionally ended by a role,
// `control`, `content` or `monospace`:
// `Selawik`, `"Portsmouth Rapids", Carlito`, `Selawik, sans-serif, monospace`.
// The first available family is the font's family; the others form a
// registered stack drawn after it; the role draws last. `monospace` last is
// the role, elsewhere the generic. A name the registry does not have that is
// not a generic (a host font, or a substitution table's request such as Arial) is passed over
// with a warning (W113), never refused: GUI.Forms draws its bundled faces,
// never a host font. When no named family is available and no role is
// written, the declaration is dropped with that warning. Without a written
// role, a role's family as the first family brings its role (Carlito and
// sans-serif: content), and any other family the content role. A role's
// family written alone stands for its role, with no family
// (`font-family: Cousine` is the monospace role, as before stacks). In a
// composite's look (admit_app_fonts) a name no bundled family has may name a
// live application family, which is then a family like any other; such a
// family has no role, so without a written role it brings the content role.
bool MaterialReader::lower_font_family() {
    RecipeDeclaration declaration = recipe_declaration(RecipeField::font_family);
    std::vector<ComponentRange> entries{};
    split(ComponentRange{0U, value_.components.size()}, entries);
    // Every family named, then the ones the registry has, in order, each once.
    constexpr std::size_t maximum_named = 1U + maximum_font_stack_families;
    std::array<FontFamilyId, maximum_named> available{};
    std::size_t available_count = 0U;
    std::size_t named_count = 0U;
    std::optional<FontRole> written_role{};
    std::uint32_t first_unavailable_token = start_;
    std::string first_unavailable{};
    for (std::size_t entry_index = 0U; entry_index < entries.size(); ++entry_index) {
        const ComponentRange entry = entries[entry_index];
        if (entry.first == entry.end) {
            const std::uint32_t token = entry.first < value_.components.size()
                                            ? value_.components[entry.first].token
                                            : start_;
            return fail(DiagnosticCode::value_syntax, token, "a bundled font family or role");
        }
        const ValueComponent& first = value_.components[entry.first];
        if (written_role) {
            return fail(DiagnosticCode::value_form_not_admitted, first.token, "a font role only as the last entry");
        }
        const std::size_t words = count(entry);
        if (words == 1U && first.kind == ValueComponentKind::keyword) {
            if (keyword(entry.first, "control")) {
                written_role = FontRole::control;
            } else if (keyword(entry.first, "content")) {
                written_role = FontRole::content;
            } else if (keyword(entry.first, "monospace") && entry_index + 1U == entries.size()) {
                // Last, `monospace` is the role (as before generics); elsewhere the generic.
                written_role = FontRole::monospace;
            }
            if (written_role) continue;
        }
        // A family name: one string, or words joined by single spaces.
        std::string name{};
        if (words == 1U && first.kind == ValueComponentKind::string) {
            name = std::string(first.text);
        } else {
            for (std::size_t index = entry.first; index < entry.end; index = next(index)) {
                const ValueComponent& component = value_.components[index];
                if (component.kind != ValueComponentKind::keyword) {
                    return fail(DiagnosticCode::value_wrong_kind, component.token, "a bundled font family or role");
                }
                if (index != entry.first) name += ' ';
                name += component.text;
            }
        }
        named_count = named_count + 1U;
        if (named_count > maximum_named) {
            return fail(DiagnosticCode::value_form_not_admitted, first.token, "at most five font families");
        }
        // A CSS generic, unquoted, draws its bundled family (generic_font_families: our
        // mapping); `emoji` names none, since the emoji fallback face serves every font.
        const GenericFontFamily* const generic =
            words == 1U && first.kind == ValueComponentKind::keyword ? generic_font_family(name) : nullptr;
        if (generic != nullptr && (*generic).family.value == 0U) continue;
        const FontFamilyRecord* const record =
            generic != nullptr ? font_family_record((*generic).family) : font_family_named(name);
        FontFamilyId family{};
        if (record != nullptr) family = (*record).id;
        if (record == nullptr && generic == nullptr && lowering_.admit_app_fonts) family = app_font_family_named(name);
        if (family.value == 0U) {
            std::string detail = clipped_theme_detail(name);
            if (first_unavailable.empty()) {
                first_unavailable = detail;
                first_unavailable_token = first.token;
            }
            lowering_.warnings.push_back(
                MaterialLoweringProblem{DiagnosticCode::font_family_unavailable, first.token, std::move(detail)});
            continue;
        }
        bool listed = false;
        for (std::size_t index = 0U; index < available_count; ++index) {
            if (available[index] == family) listed = true;
        }
        if (listed) continue;
        available[available_count] = family;
        available_count = available_count + 1U;
    }
    if (available_count == 0U) {
        // Only `emoji`: nothing to set, and nothing to warn about.
        if (!written_role && first_unavailable.empty()) return true;
        if (!written_role) {
            return fail(DiagnosticCode::font_family_unavailable, first_unavailable_token, first_unavailable);
        }
        declaration.font_role = *written_role;
        lowering_.recipe_fields.push_back(declaration);
        return true;
    }
    // Null for an application family, which has no role.
    const FontFamilyRecord* const first_family = font_family_record(available[0]);
    std::optional<FontRole> first_role{};
    if (first_family != nullptr) first_role = (*first_family).role;
    FontRole role = FontRole::content;
    if (written_role) {
        role = *written_role;
    } else if (first_role) {
        role = *first_role;
    }
    declaration.font_role = role;
    const bool role_alone = available_count == 1U && first_role && *first_role == role;
    if (!role_alone) declaration.font_family = available[0];
    if (available_count > 1U) {
        const std::span<const FontFamilyId> rest(available.data() + 1U, available_count - 1U);
        // A full stack registry leaves the font its first family and its role.
        declaration.font_stack = register_font_family_stack(rest);
    }
    lowering_.recipe_fields.push_back(declaration);
    return true;
}

// `font-size`: one length, in px from minimum_rule_font_size to
// maximum_rule_font_size, or in em above 0 (of the control's own size,
// bounded when it resolves).
bool MaterialReader::lower_font_size() {
    if (value_.components.size() != 1U) {
        return fail(DiagnosticCode::value_syntax, after_first_token(), "one length");
    }
    BoxLength length{};
    if (!read_length(0U, false, false, length)) return false;
    const ValueComponent& component = value_.components[0];
    const bool in_em = component.unit == ValueUnit::em;
    const bool sized = in_em ? component.number > 0.0
                             : length.units >= minimum_rule_font_size && length.units <= maximum_rule_font_size;
    if (!sized) {
        return fail(DiagnosticCode::value_out_of_range, component.token, clipped_theme_detail(component.text));
    }
    RecipeDeclaration declaration = recipe_declaration(RecipeField::font_size);
    declaration.number = length.units;
    lowering_.recipe_fields.push_back(declaration);
    return true;
}

// `font-style`: normal or italic; oblique, which the bundled faces do not
// draw, is refused.
bool MaterialReader::lower_font_style() {
    if (value_.components.size() != 1U) {
        return fail(DiagnosticCode::value_form_not_admitted, after_first_token(), "normal or italic");
    }
    const ValueComponent& component = value_.components[0];
    if (component.kind != ValueComponentKind::keyword) {
        return fail(DiagnosticCode::value_wrong_kind, component.token, "normal or italic");
    }
    RecipeDeclaration declaration = recipe_declaration(RecipeField::font_style);
    if (keyword(0U, "oblique")) {
        return fail(DiagnosticCode::value_form_not_admitted, component.token, clipped_theme_detail(component.text));
    }
    if (keyword(0U, "italic")) {
        declaration.italic = true;
    } else if (!keyword(0U, "normal")) {
        return fail(DiagnosticCode::keyword_unknown, component.token, clipped_theme_detail(component.text));
    }
    lowering_.recipe_fields.push_back(declaration);
    return true;
}

// `font-weight`: normal (400), bold (700), or a whole number from 100 to
// 900. bolder and lighter, relative to a parent's weight, are refused.
bool MaterialReader::lower_font_weight() {
    if (value_.components.size() != 1U) {
        return fail(DiagnosticCode::value_syntax, after_first_token(), "one weight");
    }
    const ValueComponent& component = value_.components[0];
    RecipeDeclaration declaration = recipe_declaration(RecipeField::font_weight);
    if (component.kind == ValueComponentKind::keyword) {
        if (keyword(0U, "bolder") || keyword(0U, "lighter")) {
            return fail(DiagnosticCode::value_form_not_admitted, component.token,
                        clipped_theme_detail(component.text));
        }
        if (keyword(0U, "normal")) {
            declaration.number = 400.0;
        } else if (keyword(0U, "bold")) {
            declaration.number = 700.0;
        } else {
            return fail(DiagnosticCode::keyword_unknown, component.token, clipped_theme_detail(component.text));
        }
        lowering_.recipe_fields.push_back(declaration);
        return true;
    }
    if (component.kind != ValueComponentKind::number || component.unit != ValueUnit::none) {
        return fail(DiagnosticCode::value_wrong_kind, component.token, "a weight");
    }
    const bool whole = std::floor(component.number) == component.number;
    if (!whole || component.number < minimum_rule_font_weight || component.number > maximum_rule_font_weight) {
        return fail(DiagnosticCode::value_out_of_range, component.token, clipped_theme_detail(component.text));
    }
    declaration.number = component.number;
    lowering_.recipe_fields.push_back(declaration);
    return true;
}

// `letter-spacing`: normal (0), or one length, negative or not, in px or
// em (of the resolved size); valid_font_spec bounds it at that size.
bool MaterialReader::lower_letter_spacing() {
    if (value_.components.size() != 1U) {
        return fail(DiagnosticCode::value_syntax, after_first_token(), "one length");
    }
    RecipeDeclaration declaration = recipe_declaration(RecipeField::letter_spacing);
    if (keyword(0U, "normal")) {
        lowering_.recipe_fields.push_back(declaration);
        return true;
    }
    BoxLength length{};
    if (!read_length(0U, false, true, length)) return false;
    const ValueComponent& component = value_.components[0];
    const bool in_em = component.unit == ValueUnit::em;
    const bool bounded = in_em || (length.units >= -maximum_rule_font_size * 0.25 &&
                                   length.units <= maximum_rule_font_size);
    if (!bounded) {
        return fail(DiagnosticCode::value_out_of_range, component.token, clipped_theme_detail(component.text));
    }
    declaration.number = length.units;
    lowering_.recipe_fields.push_back(declaration);
    return true;
}

// `text-shadow`: none, or one shadow: two or three adjacent lengths (the
// offset and the blur) and a colour, in either order. The recipe carries
// one shadow; its colour by default currentcolor.
bool MaterialReader::lower_text_shadow() {
    RecipeDeclaration declaration = recipe_declaration(RecipeField::text_shadow);
    const ComponentRange whole{0U, value_.components.size()};
    if (count(whole) == 1U && keyword(0U, "none")) {
        lowering_.recipe_fields.push_back(declaration);
        return true;
    }
    std::vector<ComponentRange> shadows{};
    split(whole, shadows);
    if (shadows.size() > 1U) return fail(DiagnosticCode::too_many_layers, start_, "1 text shadow");
    std::array<double, 3> lengths{};
    std::size_t length_count = 0U;
    bool lengths_done = false;
    bool coloured = false;
    for (std::size_t index = 0U; index < value_.components.size(); index = next(index)) {
        const ValueComponent& component = value_.components[index];
        if (component.kind == ValueComponentKind::number) {
            if (lengths_done || length_count == lengths.size()) {
                return fail(DiagnosticCode::value_syntax, component.token, "two or three adjacent lengths");
            }
            BoxLength length{};
            if (!read_length(index, false, length_count != 2U, length)) return false;
            lengths[length_count] = length.units;
            length_count = length_count + 1U;
            continue;
        }
        if (coloured) return fail(DiagnosticCode::value_syntax, component.token, "one colour");
        coloured = true;
        lengths_done = length_count > 0U;
        if (!read_colour(index, declaration.colour)) return false;
    }
    if (length_count < 2U) return fail(DiagnosticCode::value_syntax, start_, "two or three lengths");
    if (!coloured) declaration.colour = current_colour();
    declaration.shadowed = true;
    declaration.shadow_offset = Point{lengths[0], lengths[1]};
    declaration.shadow_blur = lengths[2];
    if (!recipe_declaration_admitted(declaration)) {
        return fail(DiagnosticCode::value_out_of_range, start_, "a text shadow");
    }
    lowering_.recipe_fields.push_back(declaration);
    return true;
}

void MaterialReader::lower(MaterialProperty property) {
    lowering_.outcome = MaterialLoweringOutcome::lowered;
    lowering_.read_current_colour = false;
    lowering_.declarations.clear();
    lowering_.recipe_fields.clear();
    lowering_.warnings.clear();
    lowering_.problem = MaterialLoweringProblem{};
    if (value_.components.empty()) {
        fail(DiagnosticCode::value_syntax, start_, "a value");
        return;
    }
    if (!drop_unknown_functions(property)) return;
    switch (property) {
    case MaterialProperty::background: static_cast<void>(lower_background()); return;
    case MaterialProperty::background_color: static_cast<void>(lower_background_colour()); return;
    case MaterialProperty::background_image: static_cast<void>(lower_background_image()); return;
    case MaterialProperty::background_blend_mode: static_cast<void>(lower_blend_modes()); return;
    case MaterialProperty::background_position:
        static_cast<void>(lower_positions(MaterialField::fill_positions, SurfaceMaterial::maximum_fill_layers));
        return;
    case MaterialProperty::background_size:
        static_cast<void>(lower_sizes(MaterialField::fill_sizes, SurfaceMaterial::maximum_fill_layers));
        return;
    case MaterialProperty::background_repeat:
        static_cast<void>(lower_repeats(MaterialField::fill_repeats, SurfaceMaterial::maximum_fill_layers));
        return;
    case MaterialProperty::border: static_cast<void>(lower_edges(0U, 4U)); return;
    case MaterialProperty::border_top: static_cast<void>(lower_edges(0U, 1U)); return;
    case MaterialProperty::border_right: static_cast<void>(lower_edges(1U, 1U)); return;
    case MaterialProperty::border_bottom: static_cast<void>(lower_edges(2U, 1U)); return;
    case MaterialProperty::border_left: static_cast<void>(lower_edges(3U, 1U)); return;
    case MaterialProperty::border_style: static_cast<void>(lower_sides(0U)); return;
    case MaterialProperty::border_width: static_cast<void>(lower_sides(1U)); return;
    case MaterialProperty::border_color: static_cast<void>(lower_sides(2U)); return;
    case MaterialProperty::border_top_style: static_cast<void>(lower_edge_field(0U, 0U)); return;
    case MaterialProperty::border_top_width: static_cast<void>(lower_edge_field(0U, 1U)); return;
    case MaterialProperty::border_top_color: static_cast<void>(lower_edge_field(0U, 2U)); return;
    case MaterialProperty::border_right_style: static_cast<void>(lower_edge_field(1U, 0U)); return;
    case MaterialProperty::border_right_width: static_cast<void>(lower_edge_field(1U, 1U)); return;
    case MaterialProperty::border_right_color: static_cast<void>(lower_edge_field(1U, 2U)); return;
    case MaterialProperty::border_bottom_style: static_cast<void>(lower_edge_field(2U, 0U)); return;
    case MaterialProperty::border_bottom_width: static_cast<void>(lower_edge_field(2U, 1U)); return;
    case MaterialProperty::border_bottom_color: static_cast<void>(lower_edge_field(2U, 2U)); return;
    case MaterialProperty::border_left_style: static_cast<void>(lower_edge_field(3U, 0U)); return;
    case MaterialProperty::border_left_width: static_cast<void>(lower_edge_field(3U, 1U)); return;
    case MaterialProperty::border_left_color: static_cast<void>(lower_edge_field(3U, 2U)); return;
    case MaterialProperty::border_image: static_cast<void>(lower_border_image()); return;
    case MaterialProperty::border_radius: static_cast<void>(lower_radii()); return;
    case MaterialProperty::border_top_left_radius: static_cast<void>(lower_corner(0U)); return;
    case MaterialProperty::border_top_right_radius: static_cast<void>(lower_corner(1U)); return;
    case MaterialProperty::border_bottom_right_radius: static_cast<void>(lower_corner(2U)); return;
    case MaterialProperty::border_bottom_left_radius: static_cast<void>(lower_corner(3U)); return;
    case MaterialProperty::box_shadow: static_cast<void>(lower_shadows()); return;
    case MaterialProperty::filter: static_cast<void>(lower_filter()); return;
    case MaterialProperty::glow: static_cast<void>(lower_glows()); return;
    case MaterialProperty::glow_placement: static_cast<void>(lower_glow_placements()); return;
    case MaterialProperty::mask_image: static_cast<void>(lower_mask()); return;
    case MaterialProperty::mask_mode: static_cast<void>(lower_mask_modes()); return;
    case MaterialProperty::mask_composite: static_cast<void>(lower_mask_composites()); return;
    case MaterialProperty::mask_position:
        static_cast<void>(lower_positions(MaterialField::mask_positions, SurfaceMaterial::maximum_mask_layers));
        return;
    case MaterialProperty::mask_size:
        static_cast<void>(lower_sizes(MaterialField::mask_sizes, SurfaceMaterial::maximum_mask_layers));
        return;
    case MaterialProperty::mask_repeat:
        static_cast<void>(lower_repeats(MaterialField::mask_repeats, SurfaceMaterial::maximum_mask_layers));
        return;
    case MaterialProperty::reflection: static_cast<void>(lower_reflection()); return;
    case MaterialProperty::surface_finish: static_cast<void>(lower_finish()); return;
    case MaterialProperty::seat: static_cast<void>(lower_seat()); return;
    case MaterialProperty::gradient_dither: static_cast<void>(lower_gradient_dither()); return;
    case MaterialProperty::transform: static_cast<void>(lower_transform()); return;
    case MaterialProperty::clip_path: static_cast<void>(lower_clip_path()); return;
    case MaterialProperty::keylines: static_cast<void>(lower_keylines()); return;
    case MaterialProperty::muted_color: static_cast<void>(lower_recipe_colour(RecipeField::muted_text)); return;
    case MaterialProperty::glyph_color: static_cast<void>(lower_recipe_colour(RecipeField::glyph)); return;
    case MaterialProperty::fill_text_color: static_cast<void>(lower_recipe_colour(RecipeField::fill_text)); return;
    case MaterialProperty::default_outline: static_cast<void>(lower_default_outline()); return;
    case MaterialProperty::stroke_width: static_cast<void>(lower_stroke_width()); return;
    case MaterialProperty::needle_shape: static_cast<void>(lower_needle_shape()); return;
    case MaterialProperty::scrim_color: static_cast<void>(lower_recipe_colour(RecipeField::scrim_colour)); return;
    case MaterialProperty::color: static_cast<void>(lower_text_colour()); return;
    case MaterialProperty::outline: static_cast<void>(lower_outline()); return;
    case MaterialProperty::outline_color: static_cast<void>(lower_outline_colour()); return;
    case MaterialProperty::outline_offset: static_cast<void>(lower_outline_offset()); return;
    case MaterialProperty::outline_style: static_cast<void>(lower_outline_style()); return;
    case MaterialProperty::outline_width: static_cast<void>(lower_outline_width()); return;
    case MaterialProperty::text_shadow: static_cast<void>(lower_text_shadow()); return;
    case MaterialProperty::font_family: static_cast<void>(lower_font_family()); return;
    case MaterialProperty::font_size: static_cast<void>(lower_font_size()); return;
    case MaterialProperty::font_style: static_cast<void>(lower_font_style()); return;
    case MaterialProperty::font_weight: static_cast<void>(lower_font_weight()); return;
    case MaterialProperty::letter_spacing: static_cast<void>(lower_letter_spacing()); return;
    case MaterialProperty::height:
        static_cast<void>(lower_part_length(RecipeField::block_size, 0.0, maximum_part_metric));
        return;
    case MaterialProperty::min_width:
        static_cast<void>(lower_part_length(RecipeField::minimum_inline_size, 0.0, maximum_part_metric));
        return;
    case MaterialProperty::padding_inline:
        static_cast<void>(lower_part_length(RecipeField::padding_inline, 0.0, maximum_part_metric));
        return;
    case MaterialProperty::gap:
        static_cast<void>(lower_part_length(RecipeField::gap, -maximum_part_margin, maximum_part_metric));
        return;
    case MaterialProperty::margin_top:
        static_cast<void>(lower_part_length(RecipeField::margin_block_start, -maximum_part_margin, maximum_part_margin));
        return;
    case MaterialProperty::margin_bottom:
        static_cast<void>(lower_part_length(RecipeField::margin_block_end, -maximum_part_margin, maximum_part_margin));
        return;
    case MaterialProperty::taper:
        static_cast<void>(lower_part_shape(RecipeField::taper, RecipeField::taper_colour, false));
        return;
    case MaterialProperty::flare:
        static_cast<void>(lower_part_shape(RecipeField::flare, RecipeField::flare_colour, true));
        return;
    case MaterialProperty::edge_shadow:
        static_cast<void>(lower_part_shape(RecipeField::edge_shadow_width, RecipeField::edge_shadow_colour, true));
        return;
    case MaterialProperty::stripe_pitch: static_cast<void>(lower_stripe_pitch()); return;
    case MaterialProperty::stripe_ratio:
        static_cast<void>(lower_stripe_number(RecipeField::stripe_ratio, minimum_stripe_ratio, maximum_stripe_ratio));
        return;
    case MaterialProperty::stripe_angle: static_cast<void>(lower_stripe_angle()); return;
    case MaterialProperty::stripe_lean: static_cast<void>(lower_stripe_sense(RecipeField::stripe_lean)); return;
    case MaterialProperty::stripe_direction:
        static_cast<void>(lower_stripe_sense(RecipeField::stripe_direction));
        return;
    case MaterialProperty::stripe_speed:
        static_cast<void>(lower_stripe_number(RecipeField::stripe_speed, 0.0, maximum_stripe_speed));
        return;
    case MaterialProperty::stripe_color: static_cast<void>(lower_recipe_colour(RecipeField::stripe_colour)); return;
    case MaterialProperty::stripe_ground:
        static_cast<void>(lower_recipe_colour(RecipeField::stripe_ground_colour));
        return;
    case MaterialProperty::stripe_gloss: static_cast<void>(lower_stripe_gloss()); return;
    case MaterialProperty::stripe_reduced_motion: static_cast<void>(lower_stripe_reduced_motion()); return;
    case MaterialProperty::fill_color: static_cast<void>(lower_recipe_colour(RecipeField::fill_colour)); return;
    case MaterialProperty::stroke_color: static_cast<void>(lower_recipe_colour(RecipeField::stroke_colour)); return;
    case MaterialProperty::stroke_dash: static_cast<void>(lower_stroke_dash()); return;
    case MaterialProperty::halo: static_cast<void>(lower_halo()); return;
    case MaterialProperty::corner_radius:
        static_cast<void>(lower_part_length(RecipeField::corner_radius, 0.0, maximum_canvas_corner_radius));
        return;
    case MaterialProperty::grid_spacing:
        static_cast<void>(
            lower_part_length(RecipeField::grid_spacing, minimum_canvas_grid_spacing, maximum_canvas_grid_spacing));
        return;
    case MaterialProperty::grid_band:
        static_cast<void>(lower_part_length(RecipeField::grid_band, minimum_canvas_grid_band, maximum_canvas_grid_band));
        return;
    case MaterialProperty::background_texture: static_cast<void>(lower_background_texture()); return;
    case MaterialProperty::flow: static_cast<void>(lower_flow_style()); return;
    case MaterialProperty::flow_speed:
        static_cast<void>(lower_stripe_number(RecipeField::flow_speed, 0.0, maximum_canvas_flow_speed));
        return;
    case MaterialProperty::flow_spacing:
        static_cast<void>(
            lower_part_length(RecipeField::flow_spacing, minimum_canvas_flow_spacing, maximum_canvas_flow_spacing));
        return;
    // A value outside the enumeration names no property: nothing is lowered.
    default: return;
    }
}

} // namespace

std::optional<MaterialProperty> material_property(std::string_view name) noexcept {
    for (const NamedMaterialProperty& named : material_properties) {
        if (named.name == name) return named.property;
    }
    return std::nullopt;
}

bool css_blend_mode_not_admitted(std::string_view name) noexcept {
    std::string folded(name);
    for (char& character : folded) {
        if (character >= 'A' && character <= 'Z') character = static_cast<char>(character - 'A' + 'a');
    }
    const bool listed = std::binary_search(blend_modes_not_admitted.begin(), blend_modes_not_admitted.end(),
                                           std::string_view(folded));
    return listed;
}

void material_property_fields(MaterialProperty property, std::vector<MaterialField>& fields) {
    fields.clear();
    switch (property) {
    case MaterialProperty::background:
        fields.push_back(MaterialField::background_colour);
        fields.push_back(MaterialField::fill_positions);
        fields.push_back(MaterialField::fill_sizes);
        fields.push_back(MaterialField::fill_repeats);
        fields.push_back(MaterialField::fills);
        return;
    case MaterialProperty::background_color: fields.push_back(MaterialField::background_colour); return;
    case MaterialProperty::background_image: fields.push_back(MaterialField::fills); return;
    case MaterialProperty::background_blend_mode: fields.push_back(MaterialField::fill_blend_modes); return;
    case MaterialProperty::background_position: fields.push_back(MaterialField::fill_positions); return;
    case MaterialProperty::background_size: fields.push_back(MaterialField::fill_sizes); return;
    case MaterialProperty::background_repeat: fields.push_back(MaterialField::fill_repeats); return;
    case MaterialProperty::border:
        for (std::size_t edge = 0U; edge < 4U; ++edge) {
            for (std::size_t part = 0U; part < 3U; ++part) fields.push_back(edge_field(edge, part));
        }
        return;
    case MaterialProperty::border_top:
        for (std::size_t part = 0U; part < 3U; ++part) fields.push_back(edge_field(0U, part));
        return;
    case MaterialProperty::border_right:
        for (std::size_t part = 0U; part < 3U; ++part) fields.push_back(edge_field(1U, part));
        return;
    case MaterialProperty::border_bottom:
        for (std::size_t part = 0U; part < 3U; ++part) fields.push_back(edge_field(2U, part));
        return;
    case MaterialProperty::border_left:
        for (std::size_t part = 0U; part < 3U; ++part) fields.push_back(edge_field(3U, part));
        return;
    case MaterialProperty::border_style:
        for (std::size_t edge = 0U; edge < 4U; ++edge) fields.push_back(edge_field(edge, 0U));
        return;
    case MaterialProperty::border_width:
        for (std::size_t edge = 0U; edge < 4U; ++edge) fields.push_back(edge_field(edge, 1U));
        return;
    case MaterialProperty::border_color:
        for (std::size_t edge = 0U; edge < 4U; ++edge) fields.push_back(edge_field(edge, 2U));
        return;
    case MaterialProperty::border_top_style: fields.push_back(edge_field(0U, 0U)); return;
    case MaterialProperty::border_top_width: fields.push_back(edge_field(0U, 1U)); return;
    case MaterialProperty::border_top_color: fields.push_back(edge_field(0U, 2U)); return;
    case MaterialProperty::border_right_style: fields.push_back(edge_field(1U, 0U)); return;
    case MaterialProperty::border_right_width: fields.push_back(edge_field(1U, 1U)); return;
    case MaterialProperty::border_right_color: fields.push_back(edge_field(1U, 2U)); return;
    case MaterialProperty::border_bottom_style: fields.push_back(edge_field(2U, 0U)); return;
    case MaterialProperty::border_bottom_width: fields.push_back(edge_field(2U, 1U)); return;
    case MaterialProperty::border_bottom_color: fields.push_back(edge_field(2U, 2U)); return;
    case MaterialProperty::border_left_style: fields.push_back(edge_field(3U, 0U)); return;
    case MaterialProperty::border_left_width: fields.push_back(edge_field(3U, 1U)); return;
    case MaterialProperty::border_left_color: fields.push_back(edge_field(3U, 2U)); return;
    case MaterialProperty::border_image: fields.push_back(MaterialField::border_image); return;
    case MaterialProperty::border_radius:
        for (std::size_t corner = 0U; corner < 4U; ++corner) fields.push_back(corner_field(corner));
        return;
    case MaterialProperty::border_top_left_radius: fields.push_back(corner_field(0U)); return;
    case MaterialProperty::border_top_right_radius: fields.push_back(corner_field(1U)); return;
    case MaterialProperty::border_bottom_right_radius: fields.push_back(corner_field(2U)); return;
    case MaterialProperty::border_bottom_left_radius: fields.push_back(corner_field(3U)); return;
    case MaterialProperty::box_shadow: fields.push_back(MaterialField::shadows); return;
    case MaterialProperty::filter:
        fields.push_back(MaterialField::colour_filter);
        fields.push_back(MaterialField::drop_shadow);
        fields.push_back(MaterialField::filter_blur);
        return;
    case MaterialProperty::glow: fields.push_back(MaterialField::glows); return;
    case MaterialProperty::glow_placement: fields.push_back(MaterialField::glow_placements); return;
    case MaterialProperty::mask_image: fields.push_back(MaterialField::mask); return;
    case MaterialProperty::mask_mode: fields.push_back(MaterialField::mask_modes); return;
    case MaterialProperty::mask_composite: fields.push_back(MaterialField::mask_composites); return;
    case MaterialProperty::mask_position: fields.push_back(MaterialField::mask_positions); return;
    case MaterialProperty::mask_size: fields.push_back(MaterialField::mask_sizes); return;
    case MaterialProperty::mask_repeat: fields.push_back(MaterialField::mask_repeats); return;
    case MaterialProperty::reflection: fields.push_back(MaterialField::reflection); return;
    case MaterialProperty::surface_finish: fields.push_back(MaterialField::finish); return;
    case MaterialProperty::seat: fields.push_back(MaterialField::seat); return;
    case MaterialProperty::gradient_dither: fields.push_back(MaterialField::gradient_dither); return;
    case MaterialProperty::transform: fields.push_back(MaterialField::transform); return;
    case MaterialProperty::clip_path: fields.push_back(MaterialField::clip_path); return;
    case MaterialProperty::keylines: fields.push_back(MaterialField::keylines); return;
    case MaterialProperty::muted_color:
    case MaterialProperty::glyph_color:
    case MaterialProperty::fill_text_color:
    case MaterialProperty::default_outline:
    case MaterialProperty::stroke_width:
    case MaterialProperty::needle_shape:
    case MaterialProperty::scrim_color:
    case MaterialProperty::color:
    case MaterialProperty::outline:
    case MaterialProperty::outline_color:
    case MaterialProperty::outline_offset:
    case MaterialProperty::outline_style:
    case MaterialProperty::outline_width:
    case MaterialProperty::text_shadow:
    case MaterialProperty::font_family:
    case MaterialProperty::font_size:
    case MaterialProperty::font_style:
    case MaterialProperty::font_weight:
    case MaterialProperty::letter_spacing:
    case MaterialProperty::height:
    case MaterialProperty::min_width:
    case MaterialProperty::padding_inline:
    case MaterialProperty::gap:
    case MaterialProperty::margin_top:
    case MaterialProperty::margin_bottom:
    case MaterialProperty::taper:
    case MaterialProperty::flare:
    case MaterialProperty::edge_shadow:
    case MaterialProperty::stripe_angle:
    case MaterialProperty::stripe_color:
    case MaterialProperty::stripe_direction:
    case MaterialProperty::stripe_gloss:
    case MaterialProperty::stripe_ground:
    case MaterialProperty::stripe_lean:
    case MaterialProperty::stripe_pitch:
    case MaterialProperty::stripe_ratio:
    case MaterialProperty::stripe_reduced_motion:
    case MaterialProperty::stripe_speed:
    case MaterialProperty::background_texture:
    case MaterialProperty::corner_radius:
    case MaterialProperty::fill_color:
    case MaterialProperty::flow:
    case MaterialProperty::flow_spacing:
    case MaterialProperty::flow_speed:
    case MaterialProperty::grid_band:
    case MaterialProperty::grid_spacing:
    case MaterialProperty::halo:
    case MaterialProperty::stroke_color:
    case MaterialProperty::stroke_dash: return;
    // A value outside the enumeration names no property: it sets no field.
    default: return;
    }
}

bool recipe_property(MaterialProperty property) noexcept {
    const RecipePropertyFields fields = recipe_property_fields(property);
    return fields.count != 0U;
}

bool stripe_property(MaterialProperty property) noexcept {
    switch (property) {
    case MaterialProperty::stripe_angle:
    case MaterialProperty::stripe_color:
    case MaterialProperty::stripe_direction:
    case MaterialProperty::stripe_gloss:
    case MaterialProperty::stripe_ground:
    case MaterialProperty::stripe_lean:
    case MaterialProperty::stripe_pitch:
    case MaterialProperty::stripe_ratio:
    case MaterialProperty::stripe_reduced_motion:
    case MaterialProperty::stripe_speed: return true;
    // Every other property, and a value outside the enumeration, is not a
    // stripe property.
    default: return false;
    }
}

bool canvas_property(MaterialProperty property) noexcept {
    switch (property) {
    case MaterialProperty::background_texture:
    case MaterialProperty::corner_radius:
    case MaterialProperty::fill_color:
    case MaterialProperty::flow:
    case MaterialProperty::flow_spacing:
    case MaterialProperty::flow_speed:
    case MaterialProperty::grid_band:
    case MaterialProperty::grid_spacing:
    case MaterialProperty::halo:
    case MaterialProperty::stroke_color:
    case MaterialProperty::stroke_dash: return true;
    // Every other property, and a value outside the enumeration, is not a
    // canvas property.
    default: return false;
    }
}

RecipePropertyFields recipe_property_fields(MaterialProperty property) noexcept {
    RecipePropertyFields result{};
    if (property == MaterialProperty::outline) {
        result.fields = {RecipeField::outline_style, RecipeField::outline_colour, RecipeField::outline_width};
        result.count = 3U;
        return result;
    }
    if (property == MaterialProperty::default_outline) {
        result.fields = {RecipeField::default_outline_width, RecipeField::default_outline_colour};
        result.count = 2U;
        return result;
    }
    if (property == MaterialProperty::taper) {
        result.fields = {RecipeField::taper, RecipeField::taper_colour};
        result.count = 2U;
        return result;
    }
    if (property == MaterialProperty::flare) {
        result.fields = {RecipeField::flare, RecipeField::flare_colour};
        result.count = 2U;
        return result;
    }
    if (property == MaterialProperty::edge_shadow) {
        result.fields = {RecipeField::edge_shadow_width, RecipeField::edge_shadow_colour};
        result.count = 2U;
        return result;
    }
    if (property == MaterialProperty::stroke_dash) {
        result.fields = {RecipeField::stroke_dash, RecipeField::stroke_gap};
        result.count = 2U;
        return result;
    }
    if (property == MaterialProperty::halo) {
        result.fields = {RecipeField::halo_width, RecipeField::halo_colour};
        result.count = 2U;
        return result;
    }
    if (property == MaterialProperty::background_texture) {
        result.fields[0] = RecipeField::texture_layers;
        result.count = 1U;
        return result;
    }
    for (const RecipeLonghand& longhand : recipe_longhands) {
        if (longhand.property != property) continue;
        result.fields[0] = longhand.field;
        result.count = 1U;
    }
    return result;
}

void lower_material_value(MaterialProperty property, const ThemeValue& value, std::uint32_t start,
                          std::span<const ThemeImageResource> images, MaterialLowering& lowering) {
    MaterialReader reader(value, start, images, lowering);
    reader.lower(property);
}

} // namespace gui_forms::theme_language
