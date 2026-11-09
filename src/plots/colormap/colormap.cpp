#include "gui_forms/plots/colormap/colormap.hpp"

#include "gui_forms/engine/engine_math/engine_math.hpp"

#include <algorithm>
#include <cmath>

namespace gui_forms::plots {

namespace {

// The decode table: entry k is the linear value of encoded byte k (IEC
// 61966-2-1), built once on first use (thread-safe static).
struct SrgbTable final {
    std::array<double, 256> linear{};
    // The midpoints between consecutive entries: a linear value encodes to
    // the byte whose interval holds it.
    std::array<double, 255> thresholds{};
};

SrgbTable make_srgb_table() noexcept {
    SrgbTable table{};
    for (std::size_t index = 0U; index < 256U; ++index) {
        const double encoded = static_cast<double>(index) / 255.0;
        const double linear = encoded <= 0.04045 ? encoded / 12.92
                                                 : engine_power((encoded + 0.055) / 1.055, 2.4);
        table.linear[index] = linear;
    }
    for (std::size_t index = 0U; index < 255U; ++index) {
        table.thresholds[index] = (table.linear[index] + table.linear[index + 1U]) * 0.5;
    }
    return table;
}

const SrgbTable& srgb_table() noexcept {
    static const SrgbTable table = make_srgb_table();
    return table;
}

Color hex(std::uint32_t rgb) noexcept {
    const Color color = Color::rgba(static_cast<std::uint8_t>((rgb >> 16U) & 0xffU),
                                    static_cast<std::uint8_t>((rgb >> 8U) & 0xffU),
                                    static_cast<std::uint8_t>(rgb & 0xffU));
    return color;
}

// A continuous map of ten evenly spaced stops, mixed in encoded sRGB.
Colormap ten_stop_map(const std::array<std::uint32_t, 10>& colours) noexcept {
    Colormap map{};
    map.kind = ColormapKind::continuous;
    map.space = ColormapSpace::srgb_encoded;
    map.stop_count = colours.size();
    for (std::size_t index = 0U; index < colours.size(); ++index) {
        map.stops[index].offset = static_cast<double>(index) / 9.0;
        map.stops[index].color = hex(colours[index]);
    }
    // 9/9 is exactly 1.
    return map;
}

// The CC0 maps' published colours at 0, 1/9, ..., 1.
constexpr std::array<std::uint32_t, 10> viridis_colours{
    0x440154U, 0x482878U, 0x3E4A89U, 0x31688EU, 0x26828EU, 0x1F9E89U, 0x35B779U, 0x6DCD59U, 0xB4DE2CU, 0xFDE725U,
};
constexpr std::array<std::uint32_t, 10> magma_colours{
    0x000004U, 0x180F3EU, 0x451077U, 0x721F81U, 0x9F2F7FU, 0xCD4071U, 0xF1605DU, 0xFD9567U, 0xFEC98DU, 0xFCFDBFU,
};
constexpr std::array<std::uint32_t, 10> inferno_colours{
    0x000004U, 0x1B0C42U, 0x4B0C6BU, 0x781C6DU, 0xA52C60U, 0xCF4446U, 0xED6925U, 0xFB9A06U, 0xF7D03CU, 0xFCFFA4U,
};
constexpr std::array<std::uint32_t, 10> plasma_colours{
    0x0D0887U, 0x47039FU, 0x7301A8U, 0x9C179EU, 0xBD3786U, 0xD8576BU, 0xED7953U, 0xFA9E3BU, 0xFDC926U, 0xF0F921U,
};

Colormap default_map() noexcept {
    Colormap map{};
    map.kind = ColormapKind::continuous;
    map.space = ColormapSpace::linear_srgb;
    map.stop_count = 4U;
    map.stops[0] = ColormapStop{0.0, hex(0x1D3B6EU)};
    map.stops[1] = ColormapStop{1.0 / 3.0, hex(0x2A9D8FU)};
    map.stops[2] = ColormapStop{2.0 / 3.0, hex(0xE9C46AU)};
    map.stops[3] = ColormapStop{1.0, hex(0xB5462DU)};
    return map;
}

struct BuiltInMaps final {
    std::array<Colormap, 5> maps{};
};

BuiltInMaps make_built_in_maps() noexcept {
    BuiltInMaps built{};
    built.maps[0] = ten_stop_map(viridis_colours);
    built.maps[1] = ten_stop_map(magma_colours);
    built.maps[2] = ten_stop_map(inferno_colours);
    built.maps[3] = ten_stop_map(plasma_colours);
    built.maps[4] = default_map();
    return built;
}

std::uint8_t mix_channel(std::uint8_t from, std::uint8_t to, double fraction, ColormapSpace space) noexcept {
    if (space == ColormapSpace::srgb_encoded) {
        const double mixed = static_cast<double>(from) + (static_cast<double>(to) - static_cast<double>(from)) * fraction;
        const double rounded = std::clamp(std::round(mixed), 0.0, 255.0);
        return static_cast<std::uint8_t>(rounded);
    }
    const double low = srgb_to_linear(from);
    const double high = srgb_to_linear(to);
    const std::uint8_t result = linear_to_srgb(low + (high - low) * fraction);
    return result;
}

std::uint8_t mix_alpha(std::uint8_t from, std::uint8_t to, double fraction) noexcept {
    const double mixed = static_cast<double>(from) + (static_cast<double>(to) - static_cast<double>(from)) * fraction;
    const double rounded = std::clamp(std::round(mixed), 0.0, 255.0);
    return static_cast<std::uint8_t>(rounded);
}

} // namespace

double srgb_to_linear(std::uint8_t encoded) noexcept {
    const double result = srgb_table().linear[encoded];
    return result;
}

std::uint8_t linear_to_srgb(double linear) noexcept {
    const SrgbTable& table = srgb_table();
    if (!(linear > 0.0)) return 0U;
    const std::array<double, 255>::const_iterator position =
        std::upper_bound(table.thresholds.begin(), table.thresholds.end(), linear);
    const std::ptrdiff_t index = position - table.thresholds.begin();
    return static_cast<std::uint8_t>(index);
}

bool valid_colormap(const Colormap& map) noexcept {
    if (map.stop_count == 0U || map.stop_count > maximum_colormap_stops) return false;
    if (map.kind == ColormapKind::qualitative) return true;
    if (map.stop_count < 2U) return false;
    if (map.stops[0].offset != 0.0 || map.stops[map.stop_count - 1U].offset != 1.0) return false;
    for (std::size_t index = 1U; index < map.stop_count; ++index) {
        const double offset = map.stops[index].offset;
        if (!std::isfinite(offset) || offset < map.stops[index - 1U].offset) return false;
    }
    return true;
}

Color sample_colormap(const Colormap& map, double position) noexcept {
    if (std::isnan(position) || !valid_colormap(map)) return map.gap_color;
    if (map.kind == ColormapKind::qualitative) {
        const double index = std::floor(position);
        const double wrapped = std::fmod(index, static_cast<double>(map.stop_count));
        const double positive = wrapped < 0.0 ? wrapped + static_cast<double>(map.stop_count) : wrapped;
        return map.stops[static_cast<std::size_t>(positive)].color;
    }
    const double clamped = std::clamp(position, 0.0, 1.0);
    std::size_t upper = 1U;
    while (upper + 1U < map.stop_count && map.stops[upper].offset < clamped) upper = upper + 1U;
    const ColormapStop& low = map.stops[upper - 1U];
    const ColormapStop& high = map.stops[upper];
    const double width = high.offset - low.offset;
    const double fraction = width > 0.0 ? std::clamp((clamped - low.offset) / width, 0.0, 1.0) : 1.0;
    Color color{};
    color.red = mix_channel(low.color.red, high.color.red, fraction, map.space);
    color.green = mix_channel(low.color.green, high.color.green, fraction, map.space);
    color.blue = mix_channel(low.color.blue, high.color.blue, fraction, map.space);
    color.alpha = mix_alpha(low.color.alpha, high.color.alpha, fraction);
    return color;
}

bool sample_colormap_span(const Colormap& map, const Scale& scale, std::span<const double> values,
                          std::span<Color> out) noexcept {
    if (out.size() < values.size() || !valid_colormap(map)) return false;
    Scale unit = scale;
    unit.device_start = 0.0;
    unit.device_end = 1.0;
    const ScaleMappingResult mapping = ScaleMapping::make(unit);
    if (mapping.check != ScaleCheck::valid) return false;
    for (std::size_t index = 0U; index < values.size(); ++index) {
        const double position = mapping.mapping.forward(values[index]);
        out[index] = sample_colormap(map, position);
    }
    return true;
}

const Colormap& built_in_colormap(BuiltInColormap which) noexcept {
    static const BuiltInMaps built = make_built_in_maps();
    const std::size_t index = static_cast<std::size_t>(which);
    return built.maps[index < built.maps.size() ? index : built.maps.size() - 1U];
}

const Colormap* find_built_in_colormap(std::string_view name) noexcept {
    if (name == "viridis") return &built_in_colormap(BuiltInColormap::viridis);
    if (name == "magma") return &built_in_colormap(BuiltInColormap::magma);
    if (name == "inferno") return &built_in_colormap(BuiltInColormap::inferno);
    if (name == "plasma") return &built_in_colormap(BuiltInColormap::plasma);
    if (name == "default") return &built_in_colormap(BuiltInColormap::gui_forms_default);
    return nullptr;
}

} // namespace gui_forms::plots
