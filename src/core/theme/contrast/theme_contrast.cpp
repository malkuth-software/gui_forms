// The contrast report (stage T4 phase 1): see theme_contrast.hpp.

#include "theme_contrast.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace gui_forms::detail {
namespace {

// Positions sampled along a face's gradients: 0, 1/16, ..., 1.
constexpr std::size_t sample_count = 17U;
using FaceSamples = std::array<Color, sample_count>;

constexpr double text_minimum = 4.5;
constexpr double disabled_text_minimum = 3.0;
constexpr double high_contrast_text_minimum = 7.0;
constexpr double non_text_minimum = 3.0;

double linear_channel(std::uint8_t value) noexcept {
    const double encoded = static_cast<double>(value) / 255.0;
    if (encoded <= 0.04045) return encoded / 12.92;
    const double linear = std::pow((encoded + 0.055) / 1.055, 2.4);
    return linear;
}

std::uint8_t channel_byte(double value) noexcept {
    const double clamped = std::clamp(value, 0.0, 255.0);
    const double rounded = std::floor(clamped + 0.5);
    return static_cast<std::uint8_t>(rounded);
}

// One channel of `top` blended over `ground` (both 0..255) before coverage.
double blended_channel(double top, double ground, ContrastBlend blend) noexcept {
    switch (blend) {
    case ContrastBlend::multiply: return top * ground / 255.0;
    case ContrastBlend::screen: return 255.0 - (255.0 - top) * (255.0 - ground) / 255.0;
    case ContrastBlend::normal: return top;
    }
    return top;
}

ContrastBlend blend_of(MaterialBlendMode mode) noexcept {
    switch (mode) {
    case MaterialBlendMode::multiply: return ContrastBlend::multiply;
    case MaterialBlendMode::screen: return ContrastBlend::screen;
    case MaterialBlendMode::normal: return ContrastBlend::normal;
    }
    return ContrastBlend::normal;
}

double channel_between(std::uint8_t from, std::uint8_t to, double fraction) noexcept {
    const double value = static_cast<double>(from) + (static_cast<double>(to) - static_cast<double>(from)) * fraction;
    return value;
}

// A gradient's colour at `position` in [0, 1] of its stops, held at the end
// stops outside them; channels and alpha interpolated straight. The stops
// are in ascending order (Theme::create refuses others); none gives clear.
Color gradient_colour_at(const std::vector<GradientStop>& stops, double position) noexcept {
    if (stops.empty()) return Color::rgba(0, 0, 0, 0);
    if (position <= stops.front().offset) return stops.front().color;
    if (position >= stops.back().offset) return stops.back().color;
    std::size_t upper = 1U;
    while (upper < stops.size() && stops[upper].offset < position) ++upper;
    const GradientStop& before = stops[upper - 1U];
    const GradientStop& after = stops[upper];
    const double width = after.offset - before.offset;
    if (width <= 0.0) return after.color;
    const double fraction = (position - before.offset) / width;
    const Color result{
        channel_byte(channel_between(before.color.red, after.color.red, fraction)),
        channel_byte(channel_between(before.color.green, after.color.green, fraction)),
        channel_byte(channel_between(before.color.blue, after.color.blue, fraction)),
        channel_byte(channel_between(before.color.alpha, after.color.alpha, fraction)),
    };
    return result;
}

// A fill layer's colour at `position`, its opacity applied to the alpha;
// none for a layer whose colour is not a function of the position (an
// image or a procedural pattern).
std::optional<Color> base_colour_at(const MaterialFillLayer& fill, double position) noexcept {
    switch (fill.kind) {
    case MaterialFillKind::solid: return fill.color;
    case MaterialFillKind::linear_gradient:
    case MaterialFillKind::radial_gradient:
    case MaterialFillKind::conic_gradient: return gradient_colour_at(fill.stops, position);
    case MaterialFillKind::image:
    case MaterialFillKind::procedural: return std::nullopt;
    }
    // A kind outside the enumeration is not sampled.
    return std::nullopt;
}

std::optional<Color> layer_colour_at(const MaterialFillLayer& fill, double position) noexcept {
    const std::optional<Color> base = base_colour_at(fill, position);
    if (!base) return std::nullopt;
    Color colour = *base;
    const double opacity = std::clamp(fill.opacity, 0.0, 1.0);
    colour.alpha = channel_byte(static_cast<double>(colour.alpha) * opacity);
    return colour;
}

// Fills `samples` with `material`'s fills over `ground` at each position.
// Returns false, leaving `samples` partly written, when a layer cannot be
// sampled.
bool sample_material(const SurfaceMaterial& material, const FaceSamples& ground, FaceSamples& samples) noexcept {
    for (std::size_t index = 0U; index < sample_count; ++index) {
        const double position = static_cast<double>(index) / static_cast<double>(sample_count - 1U);
        Color colour = ground[index];
        for (const MaterialFillLayer& fill : material.fills) {
            const std::optional<Color> layer = layer_colour_at(fill, position);
            if (!layer) return false;
            colour = composite_over(*layer, colour, blend_of(fill.blend));
        }
        samples[index] = colour;
    }
    return true;
}

// The material's edge colour: its border, else the first per-edge border,
// else none (the face is the boundary).
std::optional<Color> edge_colour(const SurfaceMaterial& material) noexcept {
    if (material.border && (*material.border).width > 0.0) return (*material.border).color;
    const std::array<const std::optional<MaterialBorder>*, 4> edges{
        &material.border_edges.top, &material.border_edges.right,
        &material.border_edges.bottom, &material.border_edges.left,
    };
    for (const std::optional<MaterialBorder>* edge : edges) {
        if (*edge && (**edge).width > 0.0) return (**edge).color;
    }
    return std::nullopt;
}

// The worst pair of `foreground` composited over each of `backgrounds`.
struct WorstPair final {
    Color foreground{};
    Color background{};
    double ratio{};
};

WorstPair worst_over(Color foreground, const FaceSamples& backgrounds) noexcept {
    WorstPair worst{};
    worst.ratio = 22.0;
    for (const Color& background : backgrounds) {
        const Color shown = composite_over(foreground, background, ContrastBlend::normal);
        const double ratio = contrast_ratio(shown, background);
        if (ratio < worst.ratio) {
            worst.foreground = shown;
            worst.background = background;
            worst.ratio = ratio;
        }
    }
    return worst;
}

// The worst pair of a face sample against the surface sample at the same
// position (a borderless control's boundary).
WorstPair worst_between(const FaceSamples& face, const FaceSamples& surround) noexcept {
    WorstPair worst{};
    worst.ratio = 22.0;
    for (std::size_t index = 0U; index < sample_count; ++index) {
        const double ratio = contrast_ratio(face[index], surround[index]);
        if (ratio < worst.ratio) {
            worst.foreground = face[index];
            worst.background = surround[index];
            worst.ratio = ratio;
        }
    }
    return worst;
}

// Which pairs a family gates in a state (see measure_theme_contrast).
bool gates_boundary(ControlVisualRole family, ControlSurfaceState state, bool selected) noexcept {
    if (state == ControlSurfaceState::disabled) return false;
    const bool lit = state == ControlSurfaceState::hot || state == ControlSurfaceState::pressed;
    switch (family) {
    case ControlVisualRole::button:
    case ControlVisualRole::accent_button:
    case ControlVisualRole::choice:
    case ControlVisualRole::editor:
    case ControlVisualRole::card:
    case ControlVisualRole::tool_tip:
    case ControlVisualRole::notice: return true;
    // A row or item is flat until it is selected, hot or pressed.
    case ControlVisualRole::selection: return selected || lit;
    case ControlVisualRole::command_button:
    case ControlVisualRole::menu_item:
        return lit;
    case ControlVisualRole::window:
    case ControlVisualRole::panel:
    case ControlVisualRole::progress:
    case ControlVisualRole::link:
    case ControlVisualRole::visited_link:
    // A badge's chip is carried by its label against the chip, not by an
    // edge against the surroundings.
    case ControlVisualRole::badge:
    case ControlVisualRole::count: return false;
    }
    return false;
}

bool gates_focus(ControlVisualRole family, ControlSurfaceState state) noexcept {
    if (state == ControlSurfaceState::disabled) return false;
    switch (family) {
    case ControlVisualRole::button:
    case ControlVisualRole::accent_button:
    case ControlVisualRole::command_button:
    case ControlVisualRole::choice:
    case ControlVisualRole::editor:
    case ControlVisualRole::menu_item:
    case ControlVisualRole::link:
    case ControlVisualRole::visited_link: return true;
    case ControlVisualRole::window:
    case ControlVisualRole::panel:
    case ControlVisualRole::card:
    case ControlVisualRole::selection:
    case ControlVisualRole::progress:
    case ControlVisualRole::tool_tip:
    case ControlVisualRole::notice:
    case ControlVisualRole::badge:
    case ControlVisualRole::count: return false;
    }
    return false;
}

double text_minimum_for(ControlSurfaceState state, bool high_contrast) noexcept {
    if (high_contrast) return high_contrast_text_minimum;
    if (state == ControlSurfaceState::disabled) return disabled_text_minimum;
    return text_minimum;
}

ContrastRow measured_row(const ContrastRow& key, ContrastPair pair, const WorstPair& worst, double minimum) noexcept {
    ContrastRow row = key;
    row.pair = pair;
    row.foreground = worst.foreground;
    row.background = worst.background;
    row.ratio = worst.ratio;
    row.minimum = minimum;
    row.verdict = worst.ratio >= minimum ? ContrastVerdict::pass : ContrastVerdict::fail;
    return row;
}

ContrastRow unmeasured_row(const ContrastRow& key, ContrastPair pair, double minimum) noexcept {
    ContrastRow row = key;
    row.pair = pair;
    row.minimum = minimum;
    row.verdict = ContrastVerdict::not_measured;
    return row;
}

// The surface around a control in one presentation: the panel family's
// resting face over the window family's, over white paper. Returns false
// when either cannot be sampled.
bool sample_surround(const Theme& theme, bool high_contrast, bool dark, FaceSamples& surround) noexcept {
    FaceSamples paper{};
    paper.fill(Color::rgba(255, 255, 255));
    ControlVisualContext resting{};
    resting.high_contrast = high_contrast;
    resting.dark = dark;
    const ControlVisualRecipe& window = theme.resolve(ControlVisualRole::window, resting);
    FaceSamples backplane{};
    if (!sample_material(window.material, paper, backplane)) return false;
    const ControlVisualRecipe& panel = theme.resolve(ControlVisualRole::panel, resting);
    const bool sampled = sample_material(panel.material, backplane, surround);
    return sampled;
}

// Appends the rows of one family, state, selection and presentation.
void measure_recipe(const ControlVisualRecipe& recipe, const ContrastRow& key, bool surround_known,
                    const FaceSamples& surround, std::vector<ContrastRow>& rows) {
    const double text_floor = text_minimum_for(key.state, key.high_contrast);
    const bool disabled = key.state == ControlSurfaceState::disabled;
    const bool boundary = gates_boundary(key.family, key.state, key.selected);
    const bool focus = gates_focus(key.family, key.state);
    FaceSamples face{};
    const bool face_known = surround_known && sample_material(recipe.material, surround, face);
    if (!face_known) {
        rows.push_back(unmeasured_row(key, ContrastPair::text, text_floor));
        rows.push_back(unmeasured_row(key, ContrastPair::muted_text, text_floor));
        if (disabled) return;
        rows.push_back(unmeasured_row(key, ContrastPair::glyph, non_text_minimum));
        if (boundary) rows.push_back(unmeasured_row(key, ContrastPair::boundary, non_text_minimum));
        if (focus) {
            rows.push_back(unmeasured_row(key, ContrastPair::focus_inner, non_text_minimum));
            rows.push_back(unmeasured_row(key, ContrastPair::focus_outer, non_text_minimum));
        }
        return;
    }
    rows.push_back(measured_row(key, ContrastPair::text, worst_over(recipe.text, face), text_floor));
    rows.push_back(measured_row(key, ContrastPair::muted_text, worst_over(recipe.muted_text, face), text_floor));
    if (disabled) return;
    rows.push_back(measured_row(key, ContrastPair::glyph, worst_over(recipe.glyph, face), non_text_minimum));
    if (boundary) {
        const std::optional<Color> edge = edge_colour(recipe.material);
        WorstPair worst{};
        if (edge) {
            worst = worst_over(*edge, surround);
        } else {
            worst = worst_between(face, surround);
        }
        rows.push_back(measured_row(key, ContrastPair::boundary, worst, non_text_minimum));
    }
    if (focus) {
        // An authored outline lies outside the control, between the face
        // and the surround; the house ring is inset inside the face
        // (paint_focus_ring), so the face is on both its sides.
        const WorstPair inner = worst_over(recipe.focus_ring, face);
        const WorstPair outer =
            recipe.authored_focus_outline ? worst_over(recipe.focus_ring, surround) : inner;
        rows.push_back(measured_row(key, ContrastPair::focus_inner, inner, non_text_minimum));
        rows.push_back(measured_row(key, ContrastPair::focus_outer, outer, non_text_minimum));
    }
}

} // namespace

double relative_luminance(Color colour) noexcept {
    const double red = linear_channel(colour.red);
    const double green = linear_channel(colour.green);
    const double blue = linear_channel(colour.blue);
    const double luminance = 0.2126 * red + 0.7152 * green + 0.0722 * blue;
    return luminance;
}

double contrast_ratio(Color first, Color second) noexcept {
    const double first_luminance = relative_luminance(first);
    const double second_luminance = relative_luminance(second);
    const double lighter = std::max(first_luminance, second_luminance);
    const double darker = std::min(first_luminance, second_luminance);
    const double ratio = (lighter + 0.05) / (darker + 0.05);
    return ratio;
}

Color composite_over(Color top, Color ground, ContrastBlend blend) noexcept {
    const double coverage = static_cast<double>(top.alpha) / 255.0;
    const double red = blended_channel(top.red, ground.red, blend);
    const double green = blended_channel(top.green, ground.green, blend);
    const double blue = blended_channel(top.blue, ground.blue, blend);
    const Color result{
        channel_byte(red * coverage + static_cast<double>(ground.red) * (1.0 - coverage)),
        channel_byte(green * coverage + static_cast<double>(ground.green) * (1.0 - coverage)),
        channel_byte(blue * coverage + static_cast<double>(ground.blue) * (1.0 - coverage)),
        255U,
    };
    return result;
}

void measure_theme_contrast(const Theme& theme, std::vector<ContrastRow>& rows) {
    rows.clear();
    // The presentations: ordinary, dark (a derived dark variant only) and
    // high contrast.
    struct Presentation final {
        bool high_contrast{};
        bool dark{};
    };
    const std::array<Presentation, 3> presentations{
        Presentation{false, false}, Presentation{false, true}, Presentation{true, false}};
    const bool measures_dark = theme.definition().dark_variant == ThemeDarkVariant::derived;
    const std::array<bool, 2> selections{false, true};
    for (const Presentation& presentation : presentations) {
        if (presentation.dark && !measures_dark) continue;
        const bool high_contrast = presentation.high_contrast;
        FaceSamples surround{};
        const bool surround_known = sample_surround(theme, high_contrast, presentation.dark, surround);
        for (std::size_t family = 0U; family < control_visual_role_count; ++family) {
            for (std::size_t state = 0U; state < control_surface_state_count; ++state) {
                for (const bool selected : selections) {
                    ContrastRow key{};
                    key.family = static_cast<ControlVisualRole>(family);
                    key.state = static_cast<ControlSurfaceState>(state);
                    key.selected = selected;
                    key.high_contrast = high_contrast;
                    key.dark = presentation.dark;
                    ControlVisualContext context{};
                    context.surface = key.state;
                    context.selected = selected;
                    context.high_contrast = high_contrast;
                    context.dark = presentation.dark;
                    const ControlVisualRecipe& recipe = theme.resolve(key.family, context);
                    measure_recipe(recipe, key, surround_known, surround, rows);
                }
            }
        }
    }
}

ContrastCounts count_contrast_verdicts(std::span<const ContrastRow> rows) noexcept {
    ContrastCounts counts{};
    for (const ContrastRow& row : rows) {
        if (row.verdict == ContrastVerdict::pass) {
            ++counts.passed;
        } else if (row.verdict == ContrastVerdict::fail) {
            ++counts.failed;
        } else {
            ++counts.not_measured;
        }
    }
    return counts;
}

std::string_view contrast_pair_name(ContrastPair pair) noexcept {
    switch (pair) {
    case ContrastPair::text: return "text";
    case ContrastPair::muted_text: return "muted_text";
    case ContrastPair::glyph: return "glyph";
    case ContrastPair::boundary: return "boundary";
    case ContrastPair::focus_inner: return "focus_inner";
    case ContrastPair::focus_outer: return "focus_outer";
    }
    return "unknown";
}

std::string_view contrast_verdict_name(ContrastVerdict verdict) noexcept {
    switch (verdict) {
    case ContrastVerdict::pass: return "pass";
    case ContrastVerdict::fail: return "fail";
    case ContrastVerdict::not_measured: return "not_measured";
    }
    return "unknown";
}

std::string_view control_surface_state_name(ControlSurfaceState state) noexcept {
    switch (state) {
    case ControlSurfaceState::normal: return "normal";
    case ControlSurfaceState::hot: return "hot";
    case ControlSurfaceState::pressed: return "pressed";
    case ControlSurfaceState::pending: return "pending";
    case ControlSurfaceState::invalid: return "invalid";
    case ControlSurfaceState::disabled: return "disabled";
    case ControlSurfaceState::deactivated: return "deactivated";
    case ControlSurfaceState::count: return "count";
    }
    return "unknown";
}

} // namespace gui_forms::detail
