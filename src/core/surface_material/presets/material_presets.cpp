#include "gui_forms/surface_material/presets/material_presets.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <span>
#include <stdexcept>
#include <utility>

namespace gui_forms {
namespace {

std::uint8_t channel_of(double value) noexcept {
    const double bounded = std::clamp(std::round(value), 0.0, 255.0);
    return static_cast<std::uint8_t>(bounded);
}

double interpolated(std::uint8_t low, std::uint8_t high, double f) noexcept {
    const double from = static_cast<double>(low);
    const double value = from + (static_cast<double>(high) - from) * f;
    return value;
}

// The colour of valid gradient stops at `t` in [0, 1], interpolated in
// straight alpha between the stops on either side, as the engine's bake
// interpolates them.
Color stop_colour_at(std::span<const GradientStop> stops, double t) noexcept {
    std::size_t next = 1U;
    while (next + 1U < stops.size() && t > stops[next].offset) next = next + 1U;
    const GradientStop& low = stops[next - 1U];
    const GradientStop& high = stops[next];
    const double span = high.offset - low.offset;
    const double f = span > 0.0 ? std::clamp((t - low.offset) / span, 0.0, 1.0) : 1.0;
    const Color colour = Color::rgba(channel_of(interpolated(low.color.red, high.color.red, f)),
                                     channel_of(interpolated(low.color.green, high.color.green, f)),
                                     channel_of(interpolated(low.color.blue, high.color.blue, f)),
                                     channel_of(interpolated(low.color.alpha, high.color.alpha, f)));
    return colour;
}

// The first fill's colour at `t` along it: a solid fill's colour, or a
// linear gradient's (which `t` of 0, 0.5 and 1 reach in every spread).
Color first_fill_colour(const MaterialFillLayer& fill, double t) noexcept {
    if (fill.kind != MaterialFillKind::linear_gradient) return fill.color;
    const Color colour = stop_colour_at(std::span<const GradientStop>(fill.stops.data(), fill.stops.size()), t);
    return colour;
}

// `colour` moved towards white by `amount` in [0, 1], its alpha kept.
Color lifted(Color colour, double amount) noexcept {
    const Color result = Color::rgba(channel_of(interpolated(colour.red, 255U, amount)),
                                     channel_of(interpolated(colour.green, 255U, amount)),
                                     channel_of(interpolated(colour.blue, 255U, amount)), colour.alpha);
    return result;
}

// `colour` moved towards black by `amount` in [0, 1], its alpha kept.
Color darkened(Color colour, double amount) noexcept {
    const Color result = Color::rgba(channel_of(interpolated(colour.red, 0U, amount)),
                                     channel_of(interpolated(colour.green, 0U, amount)),
                                     channel_of(interpolated(colour.blue, 0U, amount)), colour.alpha);
    return result;
}

} // namespace

SurfaceMaterial gloss_material(const GlossColours& colours, double corner_radius) {
    if (!std::isfinite(corner_radius) || corner_radius < 0.0 || corner_radius > 4096.0) {
        throw std::invalid_argument("a gloss material's corner radius lies in [0, 4096]");
    }
    const GradientStop stops[] = {
        {0.0, colours.top},
        {gloss_break_start, colours.above_break},
        {gloss_break_end, colours.below_break},
        {1.0, colours.bottom},
    };
    SurfaceMaterial material{};
    material.fills.clear();
    material.fills.push_back(MaterialFillLayer::linear(Point{0.0, 0.0}, Point{0.0, 1.0}, stops, std::size(stops)));
    const double border_width = 1.0;
    material.border.set(MaterialBorder{colours.border, border_width});
    MaterialKeyline shine{};
    shine.edge = MaterialEdge::top;
    shine.color = colours.shine;
    shine.width = 1.0;
    shine.inset = border_width;
    material.keylines.push_back(shine);
    material.corner_radius = corner_radius;
    return material;
}

SurfaceMaterial finished_surface_material(const SurfaceMaterial& material) {
    SurfaceMaterial result = material;
    result.finish = 0.0;
    if (!(material.finish > 0.0) || material.fills.empty()) return result;
    const double finish = std::min(material.finish, 1.0);
    const MaterialFillLayer& first = material.fills.front();
    const Color top = first_fill_colour(first, 0.0);
    const Color middle = first_fill_colour(first, 0.5);
    const Color bottom = first_fill_colour(first, 1.0);
    const double lift = finish_lift * finish;
    const GradientStop stops[] = {
        {0.0, lifted(top, lift)},
        {gloss_break_start, lifted(middle, lift)},
        {gloss_break_end, darkened(middle, finish_break_darkening * finish)},
        {1.0, bottom},
    };
    MaterialFillLayer rebuilt = MaterialFillLayer::linear(Point{0.0, 0.0}, Point{0.0, 1.0}, stops, std::size(stops));
    rebuilt.blend = first.blend;
    result.fills.front() = std::move(rebuilt);
    MaterialKeyline shine{};
    shine.edge = MaterialEdge::top;
    shine.color = Color::rgba(255, 255, 255, channel_of(finish_shine_alpha * finish));
    shine.width = 1.0;
    shine.inset = material.border.present() ? (*material.border.value()).width : 0.0;
    result.keylines.push_back(shine);
    return result;
}

} // namespace gui_forms
