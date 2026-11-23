#include "gui_forms/engine/light_planes/light_planes.hpp"

#include "gui_forms/engine/engine_math/engine_math.hpp"
#include "gui_forms/allocation/library_memory/library_memory.hpp"
#include "../primitives/finish/finish_kernel.hpp"
#include "../../../platform/library_files.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <stxlib/buffer.hpp>
#include <stxlib/result.hpp>
#include <stxlib/types.hpp>
#include <stxlib/view.hpp>
#include <utility>

// Identical bytes on every host: no fused multiply-add (engine_math.cpp).
// Only in the reference arithmetic (GUI_FORMS_REFERENCE_ARITHMETIC, owner
// 2026-11-16): deployment builds leave contraction on (CMakeLists.txt,
// "Floating-point contraction").
#if defined(__clang__) && defined(GUI_FORMS_REFERENCE_ARITHMETIC)
#pragma clang fp contract(off)
#endif

namespace gui_forms::engine_light {
namespace {

constexpr std::size_t header_bytes = 12U;

// One shipped store: its name and its bytes (static storage).
struct ShippedStore final {
    std::string_view name{};
    std::span<const unsigned char> bytes{};
};

// The shipped stores, generated from assets/light_planes at build time
// (cmake/embed_light_planes.cmake): shipped_stores, by name.
#include "shipped_light_planes.inc"

std::uint32_t little_endian_word(std::span<const std::byte> bytes, std::size_t at) noexcept {
    const std::uint32_t b0 = static_cast<std::uint32_t>(bytes[at]);
    const std::uint32_t b1 = static_cast<std::uint32_t>(bytes[at + 1U]);
    const std::uint32_t b2 = static_cast<std::uint32_t>(bytes[at + 2U]);
    const std::uint32_t b3 = static_cast<std::uint32_t>(bytes[at + 3U]);
    const std::uint32_t word = b0 | (b1 << 8U) | (b2 << 16U) | (b3 << 24U);
    return word;
}

float little_endian_float(std::span<const std::byte> bytes, std::size_t at) noexcept {
    const float value = std::bit_cast<float>(little_endian_word(bytes, at));
    return value;
}

// The bake's shape from its header, validated: the pixel count and the
// floats each pixel carries (three for the room, three a lamp, one for
// coverage).
struct PlaneShape final {
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t lamp_count{};
    std::size_t pixels{};
};

LightPlanesDefect shape_of(std::span<const std::byte> bytes, PlaneShape& shape) noexcept {
    if (bytes.size() < header_bytes) return LightPlanesDefect::short_header;
    const std::int32_t width = static_cast<std::int32_t>(little_endian_word(bytes, 0U));
    const std::int32_t height = static_cast<std::int32_t>(little_endian_word(bytes, 4U));
    const std::int32_t lamps = static_cast<std::int32_t>(little_endian_word(bytes, 8U));
    if (width <= 0 || height <= 0) return LightPlanesDefect::empty_size;
    const std::int64_t largest = static_cast<std::int64_t>(maximum_light_plane_side);
    if (width > largest || height > largest) return LightPlanesDefect::too_large;
    if (lamps < 0 || lamps > static_cast<std::int32_t>(maximum_light_plane_lamps)) {
        return LightPlanesDefect::too_many_lamps;
    }
    shape.width = static_cast<std::uint32_t>(width);
    shape.height = static_cast<std::uint32_t>(height);
    shape.lamp_count = static_cast<std::uint32_t>(lamps);
    // At most 2048² pixels of 28 floats: well within std::size_t.
    shape.pixels = static_cast<std::size_t>(shape.width) * shape.height;
    const std::size_t floats = shape.pixels * (3U * (1U + static_cast<std::size_t>(shape.lamp_count)) + 1U);
    if (bytes.size() != header_bytes + floats * 4U) return LightPlanesDefect::length_mismatch;
    return LightPlanesDefect::none;
}

// Copies `count` floats from byte `at` into `out` (sized by the caller);
// false at the first value that is not finite.
bool read_floats(std::span<const std::byte> bytes, std::size_t at, std::vector<float>& out) noexcept {
    for (std::size_t index = 0U; index < out.size(); ++index) {
        const float value = little_endian_float(bytes, at + index * 4U);
        if (!std::isfinite(value)) return false;
        out[index] = value;
    }
    return true;
}

bool coverage_in_range(const std::vector<float>& coverage) noexcept {
    for (const float value : coverage) {
        if (value < 0.0F || value > 1.0F) return false;
    }
    return true;
}

// One bake pixel's light with every lamp driven, premultiplied: the room
// at its gain, each lamp at its power and colour, in panel_lamps' order
// (the colour times the power, then the plane).
struct Light final {
    double red{};
    double green{};
    double blue{};
    double coverage{};
};

Light light_at(const LightPlanes& planes, std::size_t pixel, std::span<const LampDrive> drives, double room,
               LampColour filter) noexcept {
    const std::size_t at = pixel * 3U;
    const std::size_t plane = static_cast<std::size_t>(planes.width) * planes.height * 3U;
    // The room's light crosses the face's filter twice, a lamp's once
    // (dialog_demo.cpp, lamp_light); a white filter changes nothing.
    double red = (room * (filter.red * filter.red)) * static_cast<double>(planes.room[at]);
    double green = (room * (filter.green * filter.green)) * static_cast<double>(planes.room[at + 1U]);
    double blue = (room * (filter.blue * filter.blue)) * static_cast<double>(planes.room[at + 2U]);
    for (std::size_t lamp = 0U; lamp < planes.lamp_count; ++lamp) {
        const LampDrive& drive = drives[lamp];
        const std::size_t start = lamp * plane + at;
        red = red + (drive.colour.red * filter.red * drive.power) * static_cast<double>(planes.lamps[start]);
        green = green + (drive.colour.green * filter.green * drive.power) * static_cast<double>(planes.lamps[start + 1U]);
        blue = blue + (drive.colour.blue * filter.blue * drive.power) * static_cast<double>(planes.lamps[start + 2U]);
    }
    const Light light{red, green, blue, static_cast<double>(planes.coverage[pixel])};
    return light;
}

// Where a destination pixel samples the bake along one axis: the two
// neighbouring bake pixels and the weight of the second.
struct Tap final {
    std::size_t first{};
    std::size_t second{};
    double weight{};
};

// Pixel `index` of `length` destination pixels over `side` bake pixels,
// centres aligned; clamped at the edges. A length equal to the side reads
// each bake pixel with weight 0 on its neighbour.
Tap tap_at(std::int64_t index, std::int64_t length, std::uint32_t side) noexcept {
    const double ratio = static_cast<double>(side) / static_cast<double>(length);
    const double position = (static_cast<double>(index) + 0.5) * ratio - 0.5;
    const double last = static_cast<double>(side) - 1.0;
    const double clamped = position < 0.0 ? 0.0 : (position > last ? last : position);
    const double first = std::floor(clamped);
    const std::size_t lower = static_cast<std::size_t>(first);
    const std::size_t upper = std::min(lower + 1U, static_cast<std::size_t>(side) - 1U);
    const Tap tap{lower, upper, clamped - first};
    return tap;
}

Light mix(Light a, Light b, double t) noexcept {
    const Light mixed{a.red + (b.red - a.red) * t, a.green + (b.green - a.green) * t, a.blue + (b.blue - a.blue) * t,
                      a.coverage + (b.coverage - a.coverage) * t};
    return mixed;
}

// The bake's light at (column, row) taps, bilinearly.
Light sample(const LightPlanes& planes, Tap column, Tap row, std::span<const LampDrive> drives, double room,
             LampColour filter) noexcept {
    const std::size_t width = planes.width;
    const Light top_left = light_at(planes, row.first * width + column.first, drives, room, filter);
    const Light top_right = light_at(planes, row.first * width + column.second, drives, room, filter);
    const Light bottom_left = light_at(planes, row.second * width + column.first, drives, room, filter);
    const Light bottom_right = light_at(planes, row.second * width + column.second, drives, room, filter);
    const Light top = mix(top_left, top_right, column.weight);
    const Light bottom = mix(bottom_left, bottom_right, column.weight);
    const Light both = mix(top, bottom, row.weight);
    return both;
}

// The tap at a continuous bake position `position` (pixel centres at
// whole numbers plus one half), clamped to the bake's `side`.
Tap tap_at_position(double position, std::uint32_t side) noexcept {
    const double centred = position - 0.5;
    const double last = static_cast<double>(side) - 1.0;
    const double clamped = centred < 0.0 ? 0.0 : (centred > last ? last : centred);
    const double first = std::floor(clamped);
    const std::size_t lower = static_cast<std::size_t>(first);
    const std::size_t upper = std::min(lower + 1U, static_cast<std::size_t>(side) - 1U);
    const Tap tap{lower, upper, clamped - first};
    return tap;
}

// One binary16 value (IEEE half precision) as a float, exactly: its bits
// rebuilt as a binary32's, a subnormal as its mantissa times 2^−24. An
// infinity or a NaN comes back as such (refused by the caller).
float half_to_float(std::uint16_t half) noexcept {
    const std::uint32_t sign = static_cast<std::uint32_t>(half & 0x8000U) << 16U;
    const std::uint32_t exponent = (static_cast<std::uint32_t>(half) >> 10U) & 0x1fU;
    const std::uint32_t mantissa = static_cast<std::uint32_t>(half) & 0x3ffU;
    if (exponent == 0U) {
        // 0 or a subnormal: mantissa × 2^−24, exact in binary32.
        const float magnitude = static_cast<float>(mantissa) * 5.9604644775390625e-8F;
        const float value = sign != 0U ? -magnitude : magnitude;
        return value;
    }
    if (exponent == 0x1fU) {
        const std::uint32_t special = sign | 0x7f800000U | (mantissa << 13U);
        return std::bit_cast<float>(special);
    }
    const std::uint32_t bits = sign | ((exponent - 15U + 127U) << 23U) | (mantissa << 13U);
    return std::bit_cast<float>(bits);
}

std::uint16_t little_endian_half(std::span<const std::byte> bytes, std::size_t at) noexcept {
    const std::uint32_t b0 = static_cast<std::uint32_t>(bytes[at]);
    const std::uint32_t b1 = static_cast<std::uint32_t>(bytes[at + 1U]);
    return static_cast<std::uint16_t>(b0 | (b1 << 8U));
}

// The store's header: "GFLP", version, width, height, lamp count and
// aperture.
constexpr std::size_t store_header_bytes = 24U;

// Reads `count` grey binary16 values from byte `at` into every third float
// of `out` starting at `channel_start`, `stride` floats apart (3 for a
// colour plane, filling all three channels; 1 for coverage). False at the
// first value that is not finite.
bool read_grey_plane(std::span<const std::byte> bytes, std::size_t at, std::size_t count, std::vector<float>& out,
                     std::size_t out_start, std::size_t stride) noexcept {
    for (std::size_t index = 0U; index < count; ++index) {
        const float value = half_to_float(little_endian_half(bytes, at + index * 2U));
        if (!std::isfinite(value)) return false;
        const std::size_t to = out_start + index * stride;
        for (std::size_t channel = 0U; channel < stride; ++channel) out[to + channel] = value;
    }
    return true;
}

// A straight sRGB byte premultiplied by an alpha byte, rounded.
std::uint8_t premultiplied(std::uint8_t straight, std::uint32_t alpha) noexcept {
    const std::uint32_t product = static_cast<std::uint32_t>(straight) * alpha + 127U;
    return static_cast<std::uint8_t>(product / 255U);
}

bool same_shape(const LightPlanes& a, const LightPlanes& b) noexcept {
    const bool same = a.width == b.width && a.height == b.height && a.lamp_count == b.lamp_count;
    return same;
}

// Whether the planes hold what their shape says (a LightPlanes built by
// hand rather than read).
bool complete(const LightPlanes& planes) noexcept {
    const std::size_t pixels = static_cast<std::size_t>(planes.width) * planes.height;
    const bool whole = pixels > 0U && planes.room.size() == pixels * 3U &&
                       planes.lamps.size() == pixels * 3U * planes.lamp_count && planes.coverage.size() == pixels;
    return whole;
}

// One sRGB channel byte as linear light (the sRGB curve).
double decoded_channel(std::uint8_t encoded) noexcept {
    const double value = static_cast<double>(encoded) / 255.0;
    if (value <= 0.04045) return value / 12.92;
    const double linear = engine_power((value + 0.055) / 1.055, 2.4);
    return linear;
}

bool same_drive(LampDrive a, LampDrive b) noexcept {
    const bool same = a.colour.red == b.colour.red && a.colour.green == b.colour.green &&
                      a.colour.blue == b.colour.blue && a.power == b.power;
    return same;
}

bool same_settings(const LightComposite& a, const LightComposite& b) noexcept {
    const bool same = a.room == b.room && a.exposure == b.exposure && a.press_mix == b.press_mix &&
                      a.filter.red == b.filter.red && a.filter.green == b.filter.green &&
                      a.filter.blue == b.filter.blue && a.shape == b.shape && same_lamp_glints(a.glints, b.glints);
    return same;
}

// The toned light of the bake's pixel at continuous position (bx, by):
// bilinear in linear premultiplied light, then unpremultiplied and toned
// per channel.
struct Toned final {
    double red{};
    double green{};
    double blue{};
};

Toned toned_at(const LightPlanes& planes, double bx, double by, std::span<const LampDrive> drives,
               const LightComposite& settings) noexcept {
    const Tap column = tap_at_position(bx, planes.width);
    const Tap row = tap_at_position(by, planes.height);
    const Light light = sample(planes, column, row, drives, settings.room, settings.filter);
    const double cover = light.coverage < 0.0 ? 0.0 : (light.coverage > 1.0 ? 1.0 : light.coverage);
    const double inverse = cover > 1e-6 ? 1.0 / cover : 0.0;
    const double exposure = settings.exposure;
    const Toned toned{1.0 - engine_exponential(-(light.red * inverse) * exposure),
                      1.0 - engine_exponential(-(light.green * inverse) * exposure),
                      1.0 - engine_exponential(-(light.blue * inverse) * exposure)};
    return toned;
}

double clamped_unit(double value) noexcept {
    const double clamped = value < 0.0 ? 0.0 : (value > 1.0 ? 1.0 : value);
    return clamped;
}

// A straight linear colour at `alpha` as a premultiplied BGRA8 pixel,
// through the finishes' sRGB table.
void store_linear(std::uint8_t* pixel, double red, double green, double blue, double alpha) noexcept {
    const double cover = clamped_unit(alpha);
    const std::uint32_t alpha_byte = static_cast<std::uint32_t>(std::floor(cover * 255.0 + 0.5));
    pixel[0] = premultiplied(engine_primitives::encode_linear(blue), alpha_byte);
    pixel[1] = premultiplied(engine_primitives::encode_linear(green), alpha_byte);
    pixel[2] = premultiplied(engine_primitives::encode_linear(red), alpha_byte);
    pixel[3] = static_cast<std::uint8_t>(alpha_byte);
}

// The jewel's dark rim's linear light and its share (demo.html,
// drawIndicator: the panel round the hole darkened 85% toward 0.004).
constexpr double jewel_rim_light = 0.004;
constexpr double jewel_rim_strength = 0.85;
// The pinpoint's halo (dialog_demo.cpp, place_indicator): its strength and
// where it begins, as a share of the dot's radius, and its spread.
constexpr double pinpoint_halo_strength = 0.22;
constexpr double pinpoint_halo_start = 0.6;
constexpr double pinpoint_halo_spread = 1.2;

// The jewel or pinpoint over `rect` (LightShape::jewel, pinpoint): the
// bake's central disc of its aperture seen through a dot, as the study's
// place_indicator samples it (3 × 3 a pixel, each toned, then averaged),
// with the jewel's rim or the pinpoint's halo. Writes [x0, x1) × [y0, y1)
// of `destination`; the caller has validated both.
void composite_dot(const LightPlanes& planes, std::span<const LampDrive> drives, const LightComposite& settings,
                   LightPlaneRect rect, const PartRasterView& destination, std::int64_t x0, std::int64_t y0,
                   std::int64_t x1, std::int64_t y1) noexcept {
    const bool pinpoint = settings.shape == LightShape::pinpoint;
    const double half = static_cast<double>(std::min(rect.width, rect.height)) * 0.5;
    const double centre_x = static_cast<double>(rect.width) * 0.5;
    const double centre_y = static_cast<double>(rect.height) * 0.5;
    const double rim = pinpoint ? 0.0 : half * jewel_rim_share;
    const double radius = pinpoint ? half * pinpoint_core_share : half - rim;
    const double aperture = planes.aperture > 0.0 ? planes.aperture : 1.0;
    const double hole = aperture * static_cast<double>(planes.width) * 0.5;
    const double step = radius > 0.0 ? hole / radius : 0.0;
    const double bake_centre_x = static_cast<double>(planes.width) * 0.5;
    const double bake_centre_y = static_cast<double>(planes.height) * 0.5;
    const LampDrive halo_drive = drives.empty() ? LampDrive{} : drives[0];
    const double spread = pinpoint_halo_spread * radius * radius;
    // The sparkle's glints: their light (the drive's colour whitened, at
    // its power, at most 1), centres in pixels from the dot's centre and
    // the core's spread.
    const double glint_power = clamped_unit(halo_drive.power) * lamp_sparkle_gain;
    const double brightest =
        std::max({halo_drive.colour.red, halo_drive.colour.green, halo_drive.colour.blue, 1e-9});
    const LampColour glint_light{0.5 + 0.5 * halo_drive.colour.red / brightest,
                                 0.5 + 0.5 * halo_drive.colour.green / brightest,
                                 0.5 + 0.5 * halo_drive.colour.blue / brightest};
    const double glint_spread = std::max(lamp_sparkle_least_spread, lamp_sparkle_spread_share * radius);
    const double glint_spread2 = 2.0 * glint_spread * glint_spread;
    bool sparkling = false;
    for (const LampGlint& glint : settings.glints) {
        if (glint.strength > 0.0) sparkling = true;
    }
    sparkling = sparkling && glint_power > 0.0;
    for (std::int64_t y = y0; y < y1; ++y) {
        const std::size_t row_start = static_cast<std::size_t>(y) * destination.row_bytes;
        std::uint8_t* pixel =
            destination.pixels.data() + row_start + static_cast<std::size_t>(x0) * part_raster_bytes_per_pixel;
        const double oy = static_cast<double>(y - rect.y) + 0.5 - centre_y;
        for (std::int64_t x = x0; x < x1; ++x) {
            const double ox = static_cast<double>(x - rect.x) + 0.5 - centre_x;
            const double d = std::sqrt(ox * ox + oy * oy);
            const double inside = clamped_unit(radius + 0.5 - d);
            double red = 0.0;
            double green = 0.0;
            double blue = 0.0;
            if (inside > 0.0) {
                for (int j = 0; j < 3; ++j) {
                    for (int i = 0; i < 3; ++i) {
                        const double sx = bake_centre_x + (ox - 0.5 + (static_cast<double>(i) + 0.5) / 3.0) * step;
                        const double sy = bake_centre_y + (oy - 0.5 + (static_cast<double>(j) + 0.5) / 3.0) * step;
                        const Toned toned = toned_at(planes, sx, sy, drives, settings);
                        red = red + toned.red / 9.0;
                        green = green + toned.green / 9.0;
                        blue = blue + toned.blue / 9.0;
                    }
                }
                if (sparkling) {
                    // Each glint: a soft core with a faint cross through it,
                    // as a point of light catches in a facet.
                    double glow = 0.0;
                    for (const LampGlint& glint : settings.glints) {
                        if (!(glint.strength > 0.0)) continue;
                        const double gx = ox - glint.x * radius;
                        const double gy = oy - glint.y * radius;
                        const double core = engine_exponential(-(gx * gx + gy * gy) / glint_spread2);
                        const double across = engine_exponential(-(gy * gy) / (0.25 * glint_spread2)) *
                                              engine_exponential(-std::abs(gx) / (3.0 * glint_spread));
                        const double down = engine_exponential(-(gx * gx) / (0.25 * glint_spread2)) *
                                            engine_exponential(-std::abs(gy) / (3.0 * glint_spread));
                        glow = glow + glint.strength * (core + 0.35 * (across + down));
                    }
                    red = clamped_unit(red + glow * glint_power * glint_light.red);
                    green = clamped_unit(green + glow * glint_power * glint_light.green);
                    blue = clamped_unit(blue + glow * glint_power * glint_light.blue);
                }
            }
            // What lies under the jewel: the rim (a dark layer) or the halo
            // (the lamp's light), each premultiplied, then the jewel over it.
            double under_red = 0.0;
            double under_green = 0.0;
            double under_blue = 0.0;
            double under_alpha = 0.0;
            if (pinpoint) {
                const double beyond = std::max(0.0, d - radius * pinpoint_halo_start);
                const double halo = spread > 0.0 ? pinpoint_halo_strength * engine_exponential(-(beyond * beyond) / spread) : 0.0;
                const double power = halo_drive.power * halo;
                under_red = clamped_unit(power * halo_drive.colour.red * settings.filter.red);
                under_green = clamped_unit(power * halo_drive.colour.green * settings.filter.green);
                under_blue = clamped_unit(power * halo_drive.colour.blue * settings.filter.blue);
                under_alpha = std::max({under_red, under_green, under_blue});
            } else {
                const double ring = clamped_unit(radius + rim - d) - clamped_unit(radius - d);
                under_alpha = jewel_rim_strength * ring;
                under_red = jewel_rim_light * under_alpha;
                under_green = under_red;
                under_blue = under_red;
            }
            const double alpha = inside + under_alpha * (1.0 - inside);
            const double inverse = alpha > 1e-9 ? 1.0 / alpha : 0.0;
            const double out_red = (red * inside + under_red * (1.0 - inside)) * inverse;
            const double out_green = (green * inside + under_green * (1.0 - inside)) * inverse;
            const double out_blue = (blue * inside + under_blue * (1.0 - inside)) * inverse;
            store_linear(pixel, out_red, out_green, out_blue, alpha);
            pixel = pixel + part_raster_bytes_per_pixel;
        }
    }
}

} // namespace

LightPlanesDefect read_light_planes(std::span<const std::byte> bytes, LightPlanes& planes) {
    PlaneShape shape{};
    const LightPlanesDefect header = shape_of(bytes, shape);
    if (header != LightPlanesDefect::none) return header;
    LightPlanes read{};
    read.width = shape.width;
    read.height = shape.height;
    read.lamp_count = shape.lamp_count;
    read.room.resize(shape.pixels * 3U);
    read.lamps.resize(shape.pixels * 3U * shape.lamp_count);
    read.coverage.resize(shape.pixels);
    std::size_t at = header_bytes;
    if (!read_floats(bytes, at, read.room)) return LightPlanesDefect::not_finite;
    at = at + read.room.size() * 4U;
    if (!read_floats(bytes, at, read.lamps)) return LightPlanesDefect::not_finite;
    at = at + read.lamps.size() * 4U;
    if (!read_floats(bytes, at, read.coverage)) return LightPlanesDefect::not_finite;
    if (!coverage_in_range(read.coverage)) return LightPlanesDefect::coverage_out_of_range;
    planes = std::move(read);
    return LightPlanesDefect::none;
}

LightPlanesDefect read_light_plane_store(std::span<const std::byte> bytes, LightPlanes& planes) {
    if (bytes.size() < store_header_bytes) return LightPlanesDefect::short_header;
    const bool magic = bytes[0] == std::byte{'G'} && bytes[1] == std::byte{'F'} && bytes[2] == std::byte{'L'} &&
                       bytes[3] == std::byte{'P'};
    if (!magic) return LightPlanesDefect::not_a_store;
    if (little_endian_word(bytes, 4U) != light_plane_store_version) return LightPlanesDefect::unsupported_version;
    const std::uint32_t width = little_endian_word(bytes, 8U);
    const std::uint32_t height = little_endian_word(bytes, 12U);
    const std::uint32_t lamps = little_endian_word(bytes, 16U);
    const double aperture = static_cast<double>(little_endian_float(bytes, 20U));
    if (width == 0U || height == 0U) return LightPlanesDefect::empty_size;
    if (width > maximum_light_plane_side || height > maximum_light_plane_side) return LightPlanesDefect::too_large;
    if (lamps > maximum_light_plane_lamps) return LightPlanesDefect::too_many_lamps;
    if (!std::isfinite(aperture) || aperture < 0.0 || aperture > 1.0) return LightPlanesDefect::aperture_out_of_range;
    // At most 2048² pixels of ten planes: well within std::size_t.
    const std::size_t pixels = static_cast<std::size_t>(width) * height;
    const std::size_t values = pixels * (2U + static_cast<std::size_t>(lamps));
    if (bytes.size() != store_header_bytes + values * 2U) return LightPlanesDefect::length_mismatch;
    LightPlanes read{};
    read.width = width;
    read.height = height;
    read.lamp_count = lamps;
    read.aperture = aperture;
    read.room.resize(pixels * 3U);
    read.lamps.resize(pixels * 3U * lamps);
    read.coverage.resize(pixels);
    std::size_t at = store_header_bytes;
    if (!read_grey_plane(bytes, at, pixels, read.room, 0U, 3U)) return LightPlanesDefect::not_finite;
    at = at + pixels * 2U;
    for (std::uint32_t lamp = 0U; lamp < lamps; ++lamp) {
        const std::size_t start = static_cast<std::size_t>(lamp) * pixels * 3U;
        if (!read_grey_plane(bytes, at, pixels, read.lamps, start, 3U)) return LightPlanesDefect::not_finite;
        at = at + pixels * 2U;
    }
    if (!read_grey_plane(bytes, at, pixels, read.coverage, 0U, 1U)) return LightPlanesDefect::not_finite;
    if (!coverage_in_range(read.coverage)) return LightPlanesDefect::coverage_out_of_range;
    planes = std::move(read);
    return LightPlanesDefect::none;
}

std::span<const std::byte> shipped_light_plane_store(std::string_view name) noexcept {
    for (const ShippedStore& store : shipped_stores) {
        if (store.name == name) return std::as_bytes(store.bytes);
    }
    return {};
}

LampColour jewel_filter(LampColour colour) noexcept {
    const double largest = std::max({colour.red, colour.green, colour.blue});
    if (!(largest > 0.0) || !std::isfinite(largest)) return LampColour{1.0, 1.0, 1.0};
    constexpr double share = 0.6;
    const LampColour filter{(1.0 - share) + share * clamped_unit(colour.red / largest),
                            (1.0 - share) + share * clamped_unit(colour.green / largest),
                            (1.0 - share) + share * clamped_unit(colour.blue / largest)};
    return filter;
}

std::size_t composite_light_planes(const LightPlanes& up, const LightPlanes& down, std::span<const LampDrive> drives,
                                   const LightComposite& settings, LightPlaneRect rect,
                                   const PartRasterView& destination) noexcept {
    if (rect.width <= 0 || rect.height <= 0) return 0U;
    // The destination is a caller's view (A-gf-core-05-F4): its shape is
    // checked against its buffer before any write, and a rectangle whose
    // far edge overflows is refused. With the sums in range, x - rect.x and
    // y - rect.y below stay in [0, width) and [0, height).
    if (validate_raster_view(destination) != RasterViewDefect::none) return 0U;
    if (rect.x > std::numeric_limits<std::int64_t>::max() - rect.width) return 0U;
    if (rect.y > std::numeric_limits<std::int64_t>::max() - rect.height) return 0U;
    if (!same_shape(up, down) || !complete(up) || !complete(down)) return 0U;
    if (drives.size() < up.lamp_count) return 0U;
    const std::int64_t x0 = std::max<std::int64_t>(rect.x, 0);
    const std::int64_t y0 = std::max<std::int64_t>(rect.y, 0);
    const std::int64_t x1 = std::min<std::int64_t>(rect.x + rect.width, destination.extent.width);
    const std::int64_t y1 = std::min<std::int64_t>(rect.y + rect.height, destination.extent.height);
    if (x1 <= x0 || y1 <= y0) return 0U;
    if (settings.shape != LightShape::whole) {
        composite_dot(up, drives, settings, rect, destination, x0, y0, x1, y1);
        const std::size_t dotted = static_cast<std::size_t>(x1 - x0) * static_cast<std::size_t>(y1 - y0);
        return dotted;
    }
    // Chosen once: whether the down bake is read at all.
    const double press = settings.press_mix;
    const bool pressed = press != 0.0;
    const double exposure = settings.exposure;
    for (std::int64_t y = y0; y < y1; ++y) {
        const Tap row = tap_at(y - rect.y, rect.height, up.height);
        const std::size_t row_start = static_cast<std::size_t>(y) * destination.row_bytes;
        std::uint8_t* pixel =
            destination.pixels.data() + row_start + static_cast<std::size_t>(x0) * part_raster_bytes_per_pixel;
        for (std::int64_t x = x0; x < x1; ++x) {
            const Tap column = tap_at(x - rect.x, rect.width, up.width);
            Light light = sample(up, column, row, drives, settings.room, settings.filter);
            if (pressed) light = mix(light, sample(down, column, row, drives, settings.room, settings.filter), press);
            const double cover = light.coverage < 0.0 ? 0.0 : (light.coverage > 1.0 ? 1.0 : light.coverage);
            const double inverse = cover > 1e-6 ? 1.0 / cover : 0.0;
            const double red = 1.0 - engine_exponential(-(light.red * inverse) * exposure);
            const double green = 1.0 - engine_exponential(-(light.green * inverse) * exposure);
            const double blue = 1.0 - engine_exponential(-(light.blue * inverse) * exposure);
            const std::uint32_t alpha = static_cast<std::uint32_t>(std::floor(cover * 255.0 + 0.5));
            // The finishes' sRGB table: the study's, identical in panel_lamps.html.
            pixel[0] = premultiplied(engine_primitives::encode_linear(blue), alpha);
            pixel[1] = premultiplied(engine_primitives::encode_linear(green), alpha);
            pixel[2] = premultiplied(engine_primitives::encode_linear(red), alpha);
            pixel[3] = static_cast<std::uint8_t>(alpha);
            pixel = pixel + part_raster_bytes_per_pixel;
        }
    }
    const std::size_t written = static_cast<std::size_t>(x1 - x0) * static_cast<std::size_t>(y1 - y0);
    return written;
}

double lamp_power(bool on, double seconds, bool reduced_motion) noexcept {
    if (!on) return 0.0;
    if (reduced_motion || !(seconds < lamp_strike_seconds)) return 1.0;
    const double since = seconds < 0.0 ? 0.0 : seconds;
    const double power = lamp_strike_start + (1.0 - lamp_strike_start) * (since / lamp_strike_seconds);
    return power;
}

double key_press_mix(double from, double to, double seconds, bool reduced_motion) noexcept {
    if (reduced_motion || !(seconds < key_press_seconds)) return to;
    const double u = seconds < 0.0 ? 0.0 : seconds / key_press_seconds;
    const double eased = 1.0 - (1.0 - u) * (1.0 - u);
    const double mixed = from + (to - from) * eased;
    return mixed;
}

namespace {

// A 32-bit finaliser (MurmurHash3's) over the seed, the slot and a draw's
// number: the fault's dice, the same on every host.
std::uint32_t fault_hash(std::uint32_t seed, std::uint64_t slot, std::uint32_t draw) noexcept {
    std::uint32_t h = seed ^ static_cast<std::uint32_t>(slot) ^ (static_cast<std::uint32_t>(slot >> 32U) * 0x9e3779b9U);
    h = h ^ (draw * 0x85ebca6bU);
    h = h ^ (h >> 16U);
    h = h * 0x85ebca6bU;
    h = h ^ (h >> 13U);
    h = h * 0xc2b2ae35U;
    h = h ^ (h >> 16U);
    return h;
}

// A draw in [0, 1).
double fault_unit(std::uint32_t seed, std::uint64_t slot, std::uint32_t draw) noexcept {
    const double unit = static_cast<double>(fault_hash(seed, slot, draw)) / 4294967296.0;
    return unit;
}

// The power factor the event of `slot` gives at `seconds` since the fault
// began; 1 before it, after it, and for a slot with no event.
double fault_event(std::uint32_t seed, std::uint64_t slot, double seconds) noexcept {
    const double kind = fault_unit(seed, slot, 0U);
    if (kind < 0.40) return 1.0;
    const bool dropout = kind < 0.75;
    const double start = static_cast<double>(slot) * lamp_fault_slot_seconds +
                         lamp_fault_latest_start * fault_unit(seed, slot, 1U);
    if (seconds < start) return 1.0;
    const double low = dropout ? 0.03 + 0.07 * fault_unit(seed, slot, 2U) : 0.35 + 0.25 * fault_unit(seed, slot, 2U);
    const double hold = dropout ? 0.05 + 0.13 * fault_unit(seed, slot, 3U) : 0.03 + 0.05 * fault_unit(seed, slot, 3U);
    if (seconds < start + hold) return low;
    const double constant = dropout ? 0.03 + 0.12 * fault_unit(seed, slot, 4U) : 0.02 + 0.04 * fault_unit(seed, slot, 4U);
    const double caught = dropout ? 0.40 + 0.40 * fault_unit(seed, slot, 5U) : low;
    const double since = seconds - (start + hold);
    const double recovered = 1.0 - (1.0 - caught) * engine_exponential(-since / constant);
    return recovered;
}

} // namespace

double lamp_fault_flicker(double seconds, std::uint32_t seed, bool reduced_motion) noexcept {
    if (reduced_motion || !std::isfinite(seconds) || !(seconds > 0.0)) return 1.0;
    // Past 10^15 seconds (some 30 million years) the slot index nears the
    // doubles' whole-number limit; a fault that old stays lit.
    if (!(seconds < 1.0e15)) return 1.0;
    const std::uint64_t slot = static_cast<std::uint64_t>(seconds / lamp_fault_slot_seconds);
    const double here = fault_event(seed, slot, seconds);
    const double before = slot > 0U ? fault_event(seed, slot - 1U, seconds) : 1.0;
    const double power = std::min(here, before);
    return power;
}

namespace {

// The glint stream `stream` holds in `slot`, at `seconds`: none (strength
// 0) when the slot has none or it is not lit now.
LampGlint sparkle_glint(std::uint32_t seed, std::uint32_t stream, std::uint64_t slot, double slot_start,
                        double seconds) noexcept {
    const std::uint32_t stream_seed = seed ^ (0x51ed27a5U * (stream + 1U));
    LampGlint none{};
    if (fault_unit(stream_seed, slot, 10U) >= 0.55) return none;
    const double room = lamp_sparkle_slot_seconds - lamp_sparkle_glint_seconds;
    const double start = slot_start + room * fault_unit(stream_seed, slot, 11U);
    const double into = seconds - start;
    if (!(into > 0.0 && into < lamp_sparkle_glint_seconds)) return none;
    const double swell = engine_sine_cosine(3.141592653589793 * into / lamp_sparkle_glint_seconds).sine;
    const double facet = std::floor(8.0 * fault_unit(stream_seed, slot, 12U));
    const double angle = (facet + 0.5) * (3.141592653589793 / 4.0);
    const double out = 0.3 + 0.4 * fault_unit(stream_seed, slot, 13U);
    const EngineSineCosine direction = engine_sine_cosine(angle);
    const LampGlint glint{out * direction.cosine, out * direction.sine,
                          swell * swell * (0.6 + 0.4 * fault_unit(stream_seed, slot, 14U))};
    return glint;
}

} // namespace

bool same_lamp_glints(const LampGlints& left, const LampGlints& right) noexcept {
    for (std::size_t index = 0U; index < left.size(); ++index) {
        const bool same = left[index].x == right[index].x && left[index].y == right[index].y &&
                          left[index].strength == right[index].strength;
        if (!same) return false;
    }
    return true;
}

LampGlints lamp_sparkle_glints(std::uint32_t seed, double seconds) noexcept {
    LampGlints glints{};
    // As old a sparkle as a fault (lamp_fault_flicker) stays steady.
    if (!std::isfinite(seconds) || !(seconds > 0.0) || !(seconds < 1.0e15)) return glints;
    for (std::uint32_t stream = 0U; stream < static_cast<std::uint32_t>(lamp_sparkle_streams); ++stream) {
        // Each later stream's slots begin a share of a slot later.
        const double offset = lamp_sparkle_slot_seconds * static_cast<double>(stream) /
                              static_cast<double>(lamp_sparkle_streams);
        const double shifted = seconds - offset;
        if (shifted < 0.0) continue;
        const std::uint64_t slot = static_cast<std::uint64_t>(shifted / lamp_sparkle_slot_seconds);
        const double slot_start = offset + static_cast<double>(slot) * lamp_sparkle_slot_seconds;
        glints[stream] = sparkle_glint(seed, stream, slot, slot_start, seconds);
    }
    return glints;
}

bool lamp_settled(double seconds) noexcept {
    const bool settled = !(seconds < lamp_strike_seconds);
    return settled;
}

bool key_press_settled(double seconds) noexcept {
    const bool settled = !(seconds < key_press_seconds);
    return settled;
}

double lamp_cooling_power(double from, double seconds, bool reduced_motion) noexcept {
    if (reduced_motion || !(seconds < lamp_cool_seconds) || !(from > 0.0)) return 0.0;
    const double u = seconds < 0.0 ? 0.0 : seconds / lamp_cool_seconds;
    const double left = 1.0 - u;
    const double power = from * left * left;
    return power;
}

bool lamp_cooled(double seconds) noexcept {
    const bool cooled = !(seconds < lamp_cool_seconds);
    return cooled;
}

double lit_key_lean_degrees(double height_share) noexcept {
    const double t = std::clamp((height_share - 0.25) / 0.75, 0.0, 1.0);
    const double lean = 3.0 + 17.0 * t;
    return lean;
}

LampColour lamp_colour_from_srgb(Color colour) noexcept {
    const LampColour linear{decoded_channel(colour.red), decoded_channel(colour.green),
                            decoded_channel(colour.blue)};
    return linear;
}

std::shared_ptr<const PartRaster> LightPlaneRaster::composite(const std::shared_ptr<const LightPlanes>& planes,
                                                              LampDrive drive, const LightComposite& settings,
                                                              DeviceExtent extent, DeviceScale scale) {
    const std::shared_ptr<const PartRaster> result = composite(planes, planes, drive, settings, extent, scale);
    return result;
}

std::shared_ptr<const PartRaster> LightPlaneRaster::composite(const std::shared_ptr<const LightPlanes>& planes,
                                                              const std::shared_ptr<const LightPlanes>& down,
                                                              LampDrive drive, const LightComposite& settings,
                                                              DeviceExtent extent, DeviceScale scale) {
    if (!planes || extent.width == 0U || extent.height == 0U) {
        release();
        return nullptr;
    }
    const std::shared_ptr<const LightPlanes>& pressed = down ? down : planes;
    const bool kept = raster_ && planes_ == planes && down_ == pressed && same_drive(drive_, drive) &&
                      same_settings(settings_, settings) && (*raster_).extent() == extent &&
                      (*raster_).scale() == scale;
    if (kept) return raster_;
    const std::shared_ptr<PartRaster> made = PartRaster::create(extent, scale);
    if (!made) {
        release();
        return nullptr;
    }
    // Every lamp of the bake at the one drive.
    std::array<LampDrive, maximum_light_plane_lamps> drives{};
    drives.fill(drive);
    const LightPlaneRect rect{0, 0, static_cast<std::int64_t>(extent.width), static_cast<std::int64_t>(extent.height)};
    static_cast<void>(composite_light_planes(*planes, *pressed, drives, settings, rect, (*made).view()));
    (*made).record_opacity();
    planes_ = planes;
    down_ = pressed;
    raster_ = made;
    drive_ = drive;
    settings_ = settings;
    composites_ = composites_ + 1U;
    return raster_;
}

void LightPlaneRaster::release() noexcept {
    planes_.reset();
    down_.reset();
    raster_.reset();
}

bool light_plane_name_valid(std::string_view name) noexcept {
    if (name.empty() || name.size() > 64U) return false;
    for (const char c : name) {
        const bool allowed = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        if (!allowed) return false;
    }
    return true;
}

LightPlaneCache::LightPlaneCache(std::filesystem::path directory) : directory_(std::move(directory)) {}

namespace {

// The whole file at `path`, or nothing when it cannot be read (STXlib's
// read_file, unbounded as the stream read was).
bool read_file(const std::filesystem::path& path, std::vector<char>& text) {
    const stxlib::OperationResult<stxlib::Buffer<stxlib::Byte>> read =
        platform::read_whole_file(path, std::numeric_limits<std::size_t>::max());
    if (read.failed()) return false;
    const stxlib::View<stxlib::Byte> bytes = (*read.value()).view();
    const char* const first = reinterpret_cast<const char*>(bytes.data());
    text.assign(first, first + bytes.count());
    return true;
}

} // namespace

std::shared_ptr<const LightPlanes> LightPlaneCache::lend(std::string_view name) {
    if (!light_plane_name_valid(name)) return nullptr;
    for (const CachedBake& bake : bakes_) {
        if (bake.name == name) return bake.planes;
    }
    CachedBake bake{};
    bake.name = std::string(name);
    reads_ = reads_ + 1U;
    std::shared_ptr<LightPlanes> planes = gui_forms::detail::make_library_shared<LightPlanes>();
    std::vector<char> text{};
    // The directory's float planes, then its store, then the shipped store.
    if (!directory_.empty() && read_file(directory_ / (bake.name + ".f32"), text)) {
        const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(text.data()), text.size());
        bake.defect = read_light_planes(bytes, *planes);
    } else if (!directory_.empty() && read_file(directory_ / (bake.name + ".gflp"), text)) {
        const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(text.data()), text.size());
        bake.defect = read_light_plane_store(bytes, *planes);
    } else {
        const std::span<const std::byte> shipped = shipped_light_plane_store(name);
        bake.defect = shipped.empty() ? LightPlanesDefect::unreadable : read_light_plane_store(shipped, *planes);
    }
    if (bake.defect == LightPlanesDefect::none) bake.planes = std::move(planes);
    std::shared_ptr<const LightPlanes> lent = bake.planes;
    bakes_.push_back(std::move(bake));
    return lent;
}

LightPlanesDefect LightPlaneCache::defect(std::string_view name) const noexcept {
    for (const CachedBake& bake : bakes_) {
        if (bake.name == name) return bake.defect;
    }
    return LightPlanesDefect::none;
}

} // namespace gui_forms::engine_light
