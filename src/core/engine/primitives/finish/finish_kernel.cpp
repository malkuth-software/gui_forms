#include "finish_kernel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

// Identical bytes on every host: no fused multiply-add (see engine_math.cpp
// and finish_kernel.hpp). The reference (JavaScript) never fuses.
#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace gui_forms::engine_primitives {
namespace {

#include "finish_tables.inc"

// ---- integer hash, noise and cells ------------------------------------------------
//
// The arithmetic follows the reference: 32-bit multiplies that wrap,
// logical shifts, doubles elsewhere.

// An integer-valued double as the reference's `v | 0` reads it: truncated
// toward zero, then modulo 2^32. The kernel's arguments are positions
// within a raster (at most maximum_part_raster_side device pixels over
// cells of at least one pixel) and a seed plus a small stream number, so
// they lie far within the 64-bit range the conversion needs.
std::uint32_t wrap32(double value) noexcept {
    const std::int64_t whole = static_cast<std::int64_t>(value);
    const std::uint32_t wrapped = static_cast<std::uint32_t>(whole);
    return wrapped;
}

// A 32-bit hash of a lattice point and a stream, in [0, 1).
double hash_unit(std::uint32_t x, std::uint32_t y, std::uint32_t s) noexcept {
    std::uint32_t h = (x * 0x27d4eb2dU) ^ (y * 0x165667b1U) ^ (s * 0x9e3779b1U);
    h = (h ^ (h >> 15U)) * 0x85ebca6bU;
    h = (h ^ (h >> 13U)) * 0xc2b2ae35U;
    const double unit = static_cast<double>(h ^ (h >> 16U)) / 4294967296.0;
    return unit;
}

double hash_at(double x, double y, double s) noexcept {
    const double unit = hash_unit(wrap32(x), wrap32(y), wrap32(s));
    return unit;
}

// Value noise with smoothstep interpolation; `cell` is in device pixels and
// never smaller than one, so no octave aliases at any scale.
double noise(double x, double y, double cell, double s) noexcept {
    const double c = cell < 1.0 ? 1.0 : cell;
    const double fx0 = x / c;
    const double fy0 = y / c;
    const double ix = std::floor(fx0);
    const double iy = std::floor(fy0);
    const double fx = fx0 - ix;
    const double fy = fy0 - iy;
    const double ux = fx * fx * (3.0 - 2.0 * fx);
    const double uy = fy * fy * (3.0 - 2.0 * fy);
    const double a = hash_at(ix, iy, s);
    const double b = hash_at(ix + 1.0, iy, s);
    const double cc = hash_at(ix, iy + 1.0, s);
    const double d = hash_at(ix + 1.0, iy + 1.0, s);
    const double value = a + (b - a) * ux + (cc - a) * uy + (a - b - cc + d) * ux * uy;
    return value;
}

// Jittered-cell (Worley) distances in cell units: the nearest, the second
// nearest, and the nearest cell's identity hash.
struct CellDistances final {
    double nearest{};
    double second{};
    double identity{};
};

CellDistances cells(double x, double y, double cell, double s) noexcept {
    const double c = cell < 1.0 ? 1.0 : cell;
    const double gx = x / c;
    const double gy = y / c;
    const double ix = std::floor(gx);
    const double iy = std::floor(gy);
    double d1 = 9.0;
    double d2 = 9.0;
    double id = 0.0;
    // The 3 × 3 cells around the point, rows outer, as the reference walks
    // them: the order decides which of two equal distances is nearest.
    for (int j = -1; j <= 1; ++j) {
        for (int i = -1; i <= 1; ++i) {
            const double cx = ix + i;
            const double cy = iy + j;
            const double dx = gx - (cx + 0.15 + 0.7 * hash_at(cx, cy, s));
            const double dy = gy - (cy + 0.15 + 0.7 * hash_at(cx, cy, s + 1.0));
            const double d = dx * dx + dy * dy;
            if (d < d1) {
                d2 = d1;
                d1 = d;
                id = hash_at(cx, cy, s + 2.0);
            } else if (d < d2) {
                d2 = d;
            }
        }
    }
    const CellDistances distances{std::sqrt(d1), std::sqrt(d2), id};
    return distances;
}

double smooth(double a, double b, double x) noexcept {
    double t = (x - a) / (b - a);
    t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
    const double smoothed = t * t * (3.0 - 2.0 * t);
    return smoothed;
}

// `base` raised to the whole power `exponent` by repeated squaring, in the
// reference's order (finish_kernel.js, power).
double power(double base, std::uint32_t exponent) noexcept {
    double result = 1.0;
    double square = base;
    std::uint32_t remaining = exponent;
    while (remaining > 0U) {
        if ((remaining & 1U) != 0U) result = result * square;
        square = square * square;
        remaining = remaining >> 1U;
    }
    return result;
}

// ---- colour -------------------------------------------------------------------------

struct Linear final {
    double red{};
    double green{};
    double blue{};
};

Linear linear_of(Color colour) noexcept {
    const Linear linear{finish_linear_table[colour.red], finish_linear_table[colour.green],
                        finish_linear_table[colour.blue]};
    return linear;
}

// Linear light to an sRGB byte through the reference's 4097-step table.
std::uint8_t encode(double value) noexcept {
    const double clamped = value <= 0.0 ? 0.0 : (value >= 1.0 ? 1.0 : value);
    const std::size_t step = static_cast<std::size_t>(clamped * 4096.0);
    return finish_encode_table[step];
}

// ---- the finishes ---------------------------------------------------------------------

// A finish's shading constants: the height field's slope gain, the sheen's
// weight and whole-number exponent, how far the sheen takes the base colour
// (0 a dielectric's white sheen, 1 a metal's), and a colour of its own.
struct FinishConstants final {
    double relief{};
    double gloss{};
    std::uint32_t shininess{1U};
    double metallic{};
    bool own_colour{};
    Color colour{};
};

constexpr FinishConstants constants_of(FinishKind kind) noexcept {
    switch (kind) {
    case FinishKind::dusty_matte: return FinishConstants{0.35, 0.015, 6U, 0.0, false, Color{}};
    case FinishKind::orange_peel: return FinishConstants{0.85, 0.22, 38U, 0.0, false, Color{}};
    case FinishKind::hammertone: return FinishConstants{2.1, 0.32, 22U, 0.65, false, Color{}};
    case FinishKind::wrinkle: return FinishConstants{1.7, 0.10, 14U, 0.0, false, Color{}};
    case FinishKind::bead_blast: return FinishConstants{0.65, 0.45, 7U, 1.0, true, Color::rgba(0x8b, 0x91, 0x94)};
    case FinishKind::sand_cast: return FinishConstants{1.35, 0.05, 9U, 0.0, false, Color{}};
    case FinishKind::flat: return FinishConstants{0.0, 0.0, 1U, 0.0, false, Color{}};
    case FinishKind::cloth_weave: return FinishConstants{1.1, 0.03, 8U, 0.0, false, Color{}};
    }
    // A kind outside the enumeration is never accepted
    // (valid_surface_material): the flat finish's constants.
    return FinishConstants{0.0, 0.0, 1U, 0.0, false, Color{}};
}

// One finish evaluated at device position (x, y) relative to the surface's
// origin, at `k` device pixels per logical unit (the scale times the
// grain), under seed `s`. The template argument is the declared choice of
// finish: each instantiation is the whole row loop for one finish, so
// nothing is dispatched per pixel. The formulas are the reference's, term
// for term and in its order.
template <FinishKind Kind>
FinishSample evaluate(double x, double y, double k, double s) noexcept {
    double h = 0.0;
    double a = 1.0;
    double d = 0.0;
    double sp = 1.0;
    if constexpr (Kind == FinishKind::dusty_matte) {
        h = 0.6 * noise(x, y, 3.0 * k, s) + 0.4 * noise(x, y, 1.2 * k, s + 1.0);
        a = 1.0 + (noise(x, y, 48.0 * k, s + 2.0) - 0.5) * 0.05;
        const double fleck = std::max(1.0, k * 0.8);
        const double speck = hash_at(std::floor(x / fleck), std::floor(y / fleck), s + 3.0);
        const double haze = noise(x, y, 90.0 * k, s + 4.0);
        d = 0.10 * haze + (speck > 0.985 ? 0.35 * (speck - 0.985) / 0.015 : 0.0);
    } else if constexpr (Kind == FinishKind::orange_peel) {
        h = 0.7 * noise(x, y, 5.5 * k, s) + 0.3 * noise(x, y, 2.6 * k, s + 1.0);
        a = 1.0 + (noise(x, y, 60.0 * k, s + 2.0) - 0.5) * 0.025;
    } else if constexpr (Kind == FinishKind::hammertone) {
        const CellDistances large = cells(x, y, 8.5 * k, s);
        const double big = smooth(0.0, 0.95, large.nearest);
        const CellDistances little = cells(x, y, 4.1 * k, s + 9.0);
        const double fine = smooth(0.0, 0.95, little.nearest);
        h = 0.7 * big * big + 0.3 * fine * fine + 0.06 * noise(x, y, 1.5 * k, s + 5.0);
        a = 1.0 + (large.identity - 0.5) * 0.05 + (noise(x, y, 40.0 * k, s + 4.0) - 0.5) * 0.06;
        const double flake_cell = std::max(1.0, k);
        const double flake = hash_at(std::floor(x / flake_cell), std::floor(y / flake_cell), s + 6.0);
        sp = flake > 0.93 ? 2.6 : 0.8;
    } else if constexpr (Kind == FinishKind::wrinkle) {
        const double wx = x + (noise(x, y, 7.0 * k, s + 7.0) - 0.5) * 6.0 * k;
        const double wy = y + (noise(x, y, 7.0 * k, s + 8.0) - 0.5) * 6.0 * k;
        const double r1 = 1.0 - std::abs(2.0 * noise(wx, wy, 2.3 * k, s) - 1.0);
        const double r2 = 1.0 - std::abs(2.0 * noise(wx, wy, 1.1 * k, s + 1.0) - 1.0);
        h = 0.75 * r1 * r1 + 0.25 * r2;
        a = 1.0 - (1.0 - r1) * 0.05;
    } else if constexpr (Kind == FinishKind::bead_blast) {
        h = 0.5 * noise(x, y, 0.9 * k, s) + 0.5 * noise(x, y, 1.7 * k, s + 1.0);
        a = 1.0 + (noise(x, y, 30.0 * k, s + 2.0) - 0.5) * 0.04;
    } else if constexpr (Kind == FinishKind::sand_cast) {
        const CellDistances grains = cells(x, y, 3.2 * k, s);
        const double pit = grains.identity > 0.72 ? 1.0 - smooth(0.0, 0.32, grains.nearest) : 0.0;
        h = 0.55 * noise(x, y, 1.1 * k, s + 1.0) + 0.45 * noise(x, y, 2.8 * k, s + 2.0) - 0.9 * pit;
        a = 1.0 - pit * 0.18 + (noise(x, y, 25.0 * k, s + 3.0) - 0.5) * 0.05;
        d = pit * 0.15;
    } else if constexpr (Kind == FinishKind::cloth_weave) {
        // Threads 3 logical units apart. The thread over a cell is the
        // weft (horizontal) where the cell's column plus row is odd, the
        // warp (vertical) where it is even: a plain weave. `across` runs
        // over the thread's width, `along` its length within the cell.
        const double c = 3.0 * k < 1.0 ? 1.0 : 3.0 * k;
        const double gx = x / c;
        const double gy = y / c;
        const double ix = std::floor(gx);
        const double iy = std::floor(gy);
        const double fx = gx - ix;
        const double fy = gy - iy;
        const bool weft_over = ((wrap32(ix) + wrap32(iy)) & 1U) != 0U;
        const double across = weft_over ? fy : fx;
        const double along = weft_over ? fx : fy;
        const double thread = weft_over ? iy : ix;
        // The slub: the yarn's thickness drifts along each thread with a
        // 7-unit noise; 31 cells between threads keep neighbours apart.
        const double slub = weft_over ? noise(x, thread * 31.0 * c, 7.0 * k, s + 21.0)
                                      : noise(thread * 31.0 * c, y, 7.0 * k, s + 20.0);
        // A rounded section, 0 at the gaps and 1 at the thread's middle; the
        // thread dips where it passes under the crossing threads.
        const double profile = 4.0 * across * (1.0 - across);
        const double dip = 2.0 * along - 1.0;
        h = profile * (0.8 + 0.4 * slub) * (1.0 - 0.35 * dip * dip);
        const double dye = hash_at(thread, weft_over ? 1.0 : 0.0, s + 22.0);
        a = (0.82 + 0.18 * profile) * (1.0 + (dye - 0.5) * 0.08) * (0.94 + 0.12 * slub);
        d = weft_over ? 1.0 : 0.0;
    }
    const FinishSample sample{static_cast<float>(h), static_cast<float>(a), static_cast<float>(d),
                              static_cast<float>(sp)};
    return sample;
}

// Fills `row` with the `count` + 1 samples of device row `y` from device
// column `x0`, positions relative to the surface's origin.
template <FinishKind Kind>
void fill_row(std::vector<FinishSample>& row, std::int64_t y, std::int64_t x0, std::int64_t count, FinishRect surface,
              double k, double s) noexcept {
    const double down = static_cast<double>(y - surface.y);
    for (std::int64_t i = 0; i <= count; ++i) {
        const double across = static_cast<double>(x0 + i - surface.x);
        row[static_cast<std::size_t>(i)] = evaluate<Kind>(across, down, k, s);
    }
}

// The light and half vectors from whole degrees of azimuth (0 from the
// right, counter-clockwise) and elevation, the eye straight above, as the
// reference's lightOf computes them.
struct Lighting final {
    std::array<double, 3> light{};
    std::array<double, 3> half{};
};

Lighting lighting_of(std::int32_t azimuth_degrees, std::int32_t elevation_degrees) noexcept {
    const std::size_t azimuth = static_cast<std::size_t>(((azimuth_degrees % 360) + 360) % 360);
    const std::size_t elevation = static_cast<std::size_t>(std::clamp(elevation_degrees, 0, 90));
    const double cos_az = finish_degree_cosines[azimuth];
    const double sin_az = finish_degree_sines[azimuth];
    const double cos_el = finish_degree_cosines[elevation];
    const double sin_el = finish_degree_sines[elevation];
    Lighting lit{};
    lit.light = {cos_el * cos_az, -cos_el * sin_az, sin_el};
    const double hz = lit.light[2] + 1.0;
    const double hn = std::sqrt(lit.light[0] * lit.light[0] + lit.light[1] * lit.light[1] + hz * hz);
    lit.half = {lit.light[0] / hn, lit.light[1] / hn, hz / hn};
    return lit;
}

// The damage clipped to the surface and the destination, in destination
// pixels; empty when they miss.
FinishRect clipped(FinishRect surface, FinishRect damage, DeviceExtent extent) noexcept {
    const std::int64_t x0 = std::max({surface.x, damage.x, std::int64_t{0}});
    const std::int64_t y0 = std::max({surface.y, damage.y, std::int64_t{0}});
    const std::int64_t x1 =
        std::min({surface.x + surface.width, damage.x + damage.width, static_cast<std::int64_t>(extent.width)});
    const std::int64_t y1 =
        std::min({surface.y + surface.height, damage.y + damage.height, static_cast<std::int64_t>(extent.height)});
    const FinishRect area{x0, y0, std::max<std::int64_t>(0, x1 - x0), std::max<std::int64_t>(0, y1 - y0)};
    return area;
}

// The whole paint for one finish: its constants, colours and light read
// once, then row by row, each pixel shaded from its sample and its right
// and lower neighbours' heights. The two scratch rows swap roles each row.
template <FinishKind Kind>
std::size_t paint_rows(const FinishFill& fill, double scale, FinishRect surface, FinishRect area,
                       const PartRasterView& destination, FinishScratch& scratch) noexcept {
    constexpr FinishConstants constants = constants_of(Kind);
    const double k = scale * fill.grain;
    const double s = static_cast<double>(fill.seed);
    const Linear base = linear_of(constants.own_colour ? constants.colour : fill.paint);
    const Linear second = linear_of(fill.secondary);
    const Lighting lit = lighting_of(fill.light_azimuth_degrees, fill.light_elevation_degrees);
    const double ambient = fill.ambient;
    const double kd = 1.0 - ambient;
    const double relief = constants.relief * k * fill.relief;
    const double flat_diffuse = ambient + kd * lit.light[2];
    const double gloss = constants.gloss * fill.gloss;
    const double flat_spec = gloss * power(lit.half[2], constants.shininess);
    const double metal = constants.metallic;
    const std::int64_t count = area.width;
    std::vector<FinishSample>* top = &scratch.upper;
    std::vector<FinishSample>* bottom = &scratch.lower;
    fill_row<Kind>(*top, area.y, area.x, count, surface, k, s);
    for (std::int64_t y = area.y; y < area.y + area.height; ++y) {
        fill_row<Kind>(*bottom, y + 1, area.x, count, surface, k, s);
        const std::vector<FinishSample>& upper = *top;
        const std::vector<FinishSample>& lower = *bottom;
        const std::size_t row_start = static_cast<std::size_t>(y) * destination.row_bytes;
        std::uint8_t* pixel =
            destination.pixels.data() + row_start + static_cast<std::size_t>(area.x) * part_raster_bytes_per_pixel;
        for (std::size_t at = 0U; at < static_cast<std::size_t>(count); ++at) {
            const double ht = upper[at].height;
            const double gx = (static_cast<double>(upper[at + 1U].height) - ht) * relief;
            const double gy = (static_cast<double>(lower[at].height) - ht) * relief;
            const double inverse = 1.0 / std::sqrt(gx * gx + gy * gy + 1.0);
            const double nx = -gx * inverse;
            const double ny = -gy * inverse;
            const double nz = inverse;
            const double ndl = nx * lit.light[0] + ny * lit.light[1] + nz * lit.light[2];
            const double diffuse = (ambient + kd * (ndl > 0.0 ? ndl : 0.0)) / flat_diffuse;
            const double ndh = nx * lit.half[0] + ny * lit.half[1] + nz * lit.half[2];
            const double sheen = ndh > 0.0 ? power(ndh, constants.shininess) : 0.0;
            const double spec = (gloss * sheen - flat_spec * 0.6) * static_cast<double>(upper[at].specular);
            const double a = upper[at].albedo;
            const double d = upper[at].mix;
            double r = 0.0;
            double g = 0.0;
            double b = 0.0;
            if constexpr (Kind == FinishKind::cloth_weave) {
                // The weft takes the second colour; the albedo shades both
                // threads.
                r = (base.red * (1.0 - d) + second.red * d) * a;
                g = (base.green * (1.0 - d) + second.green * d) * a;
                b = (base.blue * (1.0 - d) + second.blue * d) * a;
            } else {
                r = (base.red * a) * (1.0 - d) + second.red * d;
                g = (base.green * a) * (1.0 - d) + second.green * d;
                b = (base.blue * a) * (1.0 - d) + second.blue * d;
            }
            const double sr = spec * (1.0 - metal + metal * r * 2.2);
            const double sg = spec * (1.0 - metal + metal * g * 2.2);
            const double sb = spec * (1.0 - metal + metal * b * 2.2);
            pixel[0] = encode(b * diffuse + sb);
            pixel[1] = encode(g * diffuse + sg);
            pixel[2] = encode(r * diffuse + sr);
            pixel[3] = 255U;
            pixel = pixel + part_raster_bytes_per_pixel;
        }
        std::vector<FinishSample>* const swap = top;
        top = bottom;
        bottom = swap;
    }
    const std::size_t written = static_cast<std::size_t>(count) * static_cast<std::size_t>(area.height);
    return written;
}

} // namespace

void reserve_finish_scratch(FinishScratch& scratch, std::uint32_t width) {
    const std::size_t samples = static_cast<std::size_t>(width) + 1U;
    scratch.upper.resize(samples);
    scratch.lower.resize(samples);
}

std::size_t paint_finish(const FinishFill& fill, double scale, FinishRect surface, FinishRect damage,
                         const PartRasterView& destination, FinishScratch& scratch) noexcept {
    const FinishRect area = clipped(surface, damage, destination.extent);
    if (area.width == 0 || area.height == 0) return 0U;
    const std::size_t needed = static_cast<std::size_t>(area.width) + 1U;
    if (scratch.upper.size() < needed || scratch.lower.size() < needed) return 0U;
    switch (fill.kind) {
    case FinishKind::dusty_matte:
        return paint_rows<FinishKind::dusty_matte>(fill, scale, surface, area, destination, scratch);
    case FinishKind::orange_peel:
        return paint_rows<FinishKind::orange_peel>(fill, scale, surface, area, destination, scratch);
    case FinishKind::hammertone:
        return paint_rows<FinishKind::hammertone>(fill, scale, surface, area, destination, scratch);
    case FinishKind::wrinkle: return paint_rows<FinishKind::wrinkle>(fill, scale, surface, area, destination, scratch);
    case FinishKind::bead_blast:
        return paint_rows<FinishKind::bead_blast>(fill, scale, surface, area, destination, scratch);
    case FinishKind::sand_cast:
        return paint_rows<FinishKind::sand_cast>(fill, scale, surface, area, destination, scratch);
    case FinishKind::flat: return paint_rows<FinishKind::flat>(fill, scale, surface, area, destination, scratch);
    case FinishKind::cloth_weave:
        return paint_rows<FinishKind::cloth_weave>(fill, scale, surface, area, destination, scratch);
    }
    // A kind outside the enumeration is never accepted
    // (valid_surface_material): the flat finish, the paint as a solid.
    return paint_rows<FinishKind::flat>(fill, scale, surface, area, destination, scratch);
}

std::uint64_t finish_effort_weight(FinishKind kind) noexcept {
    const FinishEffortWeights& weights = finish_effort_weights;
    switch (kind) {
    case FinishKind::dusty_matte: return weights.dusty_matte;
    case FinishKind::orange_peel: return weights.orange_peel;
    case FinishKind::hammertone: return weights.hammertone;
    case FinishKind::wrinkle: return weights.wrinkle;
    case FinishKind::bead_blast: return weights.bead_blast;
    case FinishKind::sand_cast: return weights.sand_cast;
    case FinishKind::flat: return weights.flat;
    case FinishKind::cloth_weave: return weights.cloth_weave;
    }
    // A kind outside the enumeration is never accepted: past the budget.
    const std::uint64_t unbounded = maximum_material_bake_effort.units + 1U;
    return unbounded;
}

} // namespace gui_forms::engine_primitives
