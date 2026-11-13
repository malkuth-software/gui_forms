#include "gui_forms/engine/light_planes/light_planes.hpp"

#include "gui_forms/engine/engine_math/engine_math.hpp"
#include "../primitives/finish/finish_kernel.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <fstream>
#include <iterator>
#include <utility>

// Identical bytes on every host: no fused multiply-add (engine_math.cpp).
#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace gui_forms::engine_light {
namespace {

constexpr std::size_t header_bytes = 12U;

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

Light light_at(const LightPlanes& planes, std::size_t pixel, std::span<const LampDrive> drives, double room) noexcept {
    const std::size_t at = pixel * 3U;
    const std::size_t plane = static_cast<std::size_t>(planes.width) * planes.height * 3U;
    double red = room * static_cast<double>(planes.room[at]);
    double green = room * static_cast<double>(planes.room[at + 1U]);
    double blue = room * static_cast<double>(planes.room[at + 2U]);
    for (std::size_t lamp = 0U; lamp < planes.lamp_count; ++lamp) {
        const LampDrive& drive = drives[lamp];
        const std::size_t start = lamp * plane + at;
        red = red + (drive.colour.red * drive.power) * static_cast<double>(planes.lamps[start]);
        green = green + (drive.colour.green * drive.power) * static_cast<double>(planes.lamps[start + 1U]);
        blue = blue + (drive.colour.blue * drive.power) * static_cast<double>(planes.lamps[start + 2U]);
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
Light sample(const LightPlanes& planes, Tap column, Tap row, std::span<const LampDrive> drives, double room) noexcept {
    const std::size_t width = planes.width;
    const Light top_left = light_at(planes, row.first * width + column.first, drives, room);
    const Light top_right = light_at(planes, row.first * width + column.second, drives, room);
    const Light bottom_left = light_at(planes, row.second * width + column.first, drives, room);
    const Light bottom_right = light_at(planes, row.second * width + column.second, drives, room);
    const Light top = mix(top_left, top_right, column.weight);
    const Light bottom = mix(bottom_left, bottom_right, column.weight);
    const Light both = mix(top, bottom, row.weight);
    return both;
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
    const bool same = a.room == b.room && a.exposure == b.exposure && a.press_mix == b.press_mix;
    return same;
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

std::size_t composite_light_planes(const LightPlanes& up, const LightPlanes& down, std::span<const LampDrive> drives,
                                   const LightComposite& settings, LightPlaneRect rect,
                                   const PartRasterView& destination) noexcept {
    if (rect.width <= 0 || rect.height <= 0) return 0U;
    if (!same_shape(up, down) || !complete(up) || !complete(down)) return 0U;
    if (drives.size() < up.lamp_count) return 0U;
    const std::int64_t x0 = std::max<std::int64_t>(rect.x, 0);
    const std::int64_t y0 = std::max<std::int64_t>(rect.y, 0);
    const std::int64_t x1 = std::min<std::int64_t>(rect.x + rect.width, destination.extent.width);
    const std::int64_t y1 = std::min<std::int64_t>(rect.y + rect.height, destination.extent.height);
    if (x1 <= x0 || y1 <= y0) return 0U;
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
            Light light = sample(up, column, row, drives, settings.room);
            if (pressed) light = mix(light, sample(down, column, row, drives, settings.room), press);
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
    if (!planes || extent.width == 0U || extent.height == 0U) {
        release();
        return nullptr;
    }
    const bool kept = raster_ && planes_ == planes && same_drive(drive_, drive) &&
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
    static_cast<void>(composite_light_planes(*planes, *planes, drives, settings, rect, (*made).view()));
    (*made).record_opacity();
    planes_ = planes;
    raster_ = made;
    drive_ = drive;
    settings_ = settings;
    composites_ = composites_ + 1U;
    return raster_;
}

void LightPlaneRaster::release() noexcept {
    planes_.reset();
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

std::shared_ptr<const LightPlanes> LightPlaneCache::lend(std::string_view name) {
    if (!light_plane_name_valid(name)) return nullptr;
    for (const CachedBake& bake : bakes_) {
        if (bake.name == name) return bake.planes;
    }
    CachedBake bake{};
    bake.name = std::string(name);
    reads_ = reads_ + 1U;
    const std::filesystem::path path = directory_ / (bake.name + ".f32");
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        bake.defect = LightPlanesDefect::unreadable;
        bakes_.push_back(std::move(bake));
        return nullptr;
    }
    const std::vector<char> text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(text.data()), text.size());
    std::shared_ptr<LightPlanes> planes = std::make_shared<LightPlanes>();
    bake.defect = read_light_planes(bytes, *planes);
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
