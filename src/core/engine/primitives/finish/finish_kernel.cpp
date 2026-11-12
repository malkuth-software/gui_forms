#include "finish_kernel.hpp"

#include "gui_forms/engine/engine_math/engine_math.hpp"

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
    // The glint shades itself (paint_glints); never painted by paint_rows.
    case FinishKind::glint: return FinishConstants{0.0, 0.0, 1U, 0.0, false, Color{}};
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

// ---- glints (phase 2) -----------------------------------------------------------------
//
// A port of the study's paint_glints (cpp/surface_kernels.cpp, itself a port
// of glint_kernel.js): blasted metal as a field of facets with random tilts,
// each pixel bright where one of its facets mirrors the light into the eye.
// Facets smaller than a pixel are counted (a Poisson draw from the lobe's
// density, blended over four half-vector bins, gathered at jittered speck
// points); facets wider than a pixel are jittered cells with their own tilt;
// the two are blended where a facet is about a pixel wide. Every pixel is a
// function of its position, the scale and the fill alone: no scratch.
//
// Where the study called libm (e^x, ln x, cosine and sine, the Fresnel
// power, hypot) the port calls engine_math, whole powers by squaring and
// square roots, which give the same bits on every host. The JavaScript
// reference calls Math.exp and Math.log, whose last bits vary between
// engines, so the glint's bytes are held to this kernel's own recorded
// hashes and the reference stays its statistical description.

// A preset's fitted constants (glint_kernel.js, PRESETS): the facet and
// speck sizes (logical units), the Beckmann slope spread, the facet
// tolerance (in slope), the diffuse and specular weights, the blast's
// mottle, the oxide film's iridescence, and the paint coat's share and its
// own slope spread.
struct GlintConstants final {
    double grain{};
    double speck{};
    double alpha{};
    double tau{};
    double diffuse{};
    double specular{};
    double mottle{};
    double iridescence{};
    double film{};
    double film_alpha{};
};

constexpr GlintConstants glint_constants_of(GlintPreset preset) noexcept {
    switch (preset) {
    case GlintPreset::bead_blast: return GlintConstants{0.05, 0.75, 0.32, 0.03, 0.79, 0.57, 0.05, 0.0, 0.0, 0.3};
    case GlintPreset::sand_blast: return GlintConstants{0.05, 0.75, 0.42, 0.034, 0.46, 1.23, 0.06, 0.0, 0.0, 0.3};
    case GlintPreset::iridescent: return GlintConstants{0.10, 1.4, 0.4, 0.034, 0.37, 2.82, 0.06, 0.27, 0.0, 0.3};
    case GlintPreset::coarse_grit: return GlintConstants{0.9, 0.0, 0.45, 0.05, 0.33, 0.7, 0.08, 0.0, 0.0, 0.3};
    case GlintPreset::painted_sand: return GlintConstants{0.05, 0.75, 0.42, 0.034, 0.9, 1.23, 0.05, 0.0, 0.7, 0.3};
    }
    // A preset outside the enumeration is never accepted
    // (valid_surface_material): the sand blast's.
    return GlintConstants{0.05, 0.75, 0.42, 0.034, 0.46, 1.23, 0.06, 0.0, 0.0, 0.3};
}

// The study's fitted geometry: the light 900 and the eye 700 logical units
// above the surface (here above its centre), ambient 0.35.
constexpr double glint_light_height = 900.0;
constexpr double glint_eye_height = 700.0;
constexpr double glint_ambient = 0.35;
// The reference's π and its truncated 2π, kept as it writes them.
constexpr double glint_pi = 3.141592653589793;
constexpr double glint_turn = 6.283185307;

// What a paint reads once: the preset, the derived spreads and the colours,
// the seed and the light's and eye's positions in logical units from the
// surface's origin.
struct GlintState final {
    GlintConstants constants{};
    double sigma2{};
    double tau_area{};
    double bin_width{};
    std::uint32_t seed{};
    Linear metal{};
    Linear paint{};
    double specular{};
    double centre_x{};
    double centre_y{};
};

// A facet density and the hue seed of its glints.
struct GlintDensity final {
    double value{};
    double hue{};
};

// A Poisson draw of mean `lambda` from the uniform `u` (by inversion, at
// most 64 steps), or past a mean of 24 the normal approximation with the
// second uniform `u2` as its angle.
std::int32_t poisson(double lambda, double u, double u2) noexcept {
    if (!(lambda > 0.0)) return 0;
    if (lambda > 24.0) {
        const double radius = std::sqrt(-2.0 * engine_logarithm(std::max(u, 1e-12)));
        const double z = radius * engine_sine_cosine(glint_turn * u2).cosine;
        const double n = std::floor(lambda + std::sqrt(lambda) * z + 0.5);
        const std::int32_t count = n > 0.0 ? static_cast<std::int32_t>(n) : 0;
        return count;
    }
    double p = engine_exponential(-lambda);
    double f = p;
    std::int32_t n = 0;
    while (u > f && n < 64) {
        n = n + 1;
        p = p * (lambda / static_cast<double>(n));
        f = f + p;
    }
    return n;
}

// The hash key of half-vector bin (cx, cy), as the reference's
// Math.imul(cx, 0x632be5ab) ^ Math.imul(cy, 0x85157af5) gives it, modulo
// 2^32.
std::uint32_t bin_key(double cx, double cy) noexcept {
    const std::uint32_t key = (wrap32(cx) * 0x632be5abU) ^ (wrap32(cy) * 0x85157af5U);
    return key;
}

// Facets smaller than a pixel: the glint count of each of the four bins
// around the half-vector slope (hx, hy) for the point (x, y), blended
// bilinearly, so that a moving light makes glints come and go rather than
// swell. The reference's sums of signed 32-bit values are taken modulo 2^32,
// as its `| 0` reads them.
GlintDensity sub_pixel_density(const GlintState& state, double x, double y, double facets, double hx, double hy) noexcept {
    const double w = state.bin_width;
    const double bx = hx / w - 0.5;
    const double by = hy / w - 0.5;
    const double ix = std::floor(bx);
    const double iy = std::floor(by);
    const double fx = bx - ix;
    const double fy = by - iy;
    const std::uint32_t xi = wrap32(x);
    const std::uint32_t yi = wrap32(y);
    double sum = 0.0;
    double hue_sum = 0.0;
    double weight_sum = 0.0;
    for (int j = 0; j < 2; ++j) {
        for (int i = 0; i < 2; ++i) {
            const double cx = ix + i;
            const double cy = iy + j;
            const double sx = (cx + 0.5) * w;
            const double sy = (cy + 0.5) * w;
            const double pdf = engine_exponential(-(sx * sx + sy * sy) / state.sigma2) / (glint_pi * state.sigma2);
            const double lambda = facets * pdf * state.tau_area;
            const std::uint32_t key = bin_key(cx, cy);
            const double u = hash_unit(xi ^ key, yi + key, state.seed);
            const double u2 = hash_unit(xi + key, yi ^ key, state.seed + 1U);
            const std::int32_t n = poisson(lambda, u, u2);
            const double weight = (i != 0 ? fx : 1.0 - fx) * (j != 0 ? fy : 1.0 - fy);
            sum = sum + weight * static_cast<double>(n);
            if (n > 0) {
                hue_sum = hue_sum + weight * hash_unit(xi, yi, key + state.seed + 2U);
                weight_sum = weight_sum + weight;
            }
        }
    }
    const GlintDensity density{sum / (facets * state.tau_area), weight_sum > 0.0 ? hue_sum / weight_sum : 0.0};
    return density;
}

// Specks about a pixel and a half wide: two jittered points in each cell
// of `speck` device pixels, each drawing its own count for the facets under
// it, blended by (1 - d²)² over the points within one cell of the pixel;
// the nearest point stands in when none is.
GlintDensity speck_density(const GlintState& state, double x, double y, double speck, double facet_px, double hx,
                           double hy) noexcept {
    const double per_pixel = 1.0 / (facet_px * facet_px);
    if (speck <= 1.01) return sub_pixel_density(state, x, y, per_pixel, hx, hy);
    const double gx = x / speck;
    const double gy = y / speck;
    const double ix = std::floor(gx);
    const double iy = std::floor(gy);
    const double facets = per_pixel * speck * speck * 0.5;
    const double s = static_cast<double>(static_cast<std::int32_t>(state.seed));
    double sum = 0.0;
    double weight_sum = 0.0;
    double hue = 0.0;
    double nearest = 9.0;
    double nearest_x = 0.0;
    double nearest_y = 0.0;
    for (int j = -1; j <= 1; ++j) {
        for (int i = -1; i <= 1; ++i) {
            const double cx = ix + i;
            const double cy = iy + j;
            for (int d = 0; d < 2; ++d) {
                const double px = cx + hash_at(cx, cy, s + 30.0 + d * 2);
                const double py = cy + hash_at(cx, cy, s + 31.0 + d * 2);
                const double dx = gx - px;
                const double dy = gy - py;
                const double q = dx * dx + dy * dy;
                if (q < nearest) {
                    nearest = q;
                    nearest_x = cx * 2.0 + d;
                    nearest_y = cy;
                }
                if (q < 1.0) {
                    const double weight = (1.0 - q) * (1.0 - q);
                    const GlintDensity one = sub_pixel_density(state, cx * 2.0 + d, cy, facets, hx, hy);
                    sum = sum + weight * one.value;
                    if (weight > weight_sum * 0.5) hue = one.hue;
                    weight_sum = weight_sum + weight;
                }
            }
        }
    }
    if (weight_sum > 1e-6) {
        const GlintDensity blended{sum / weight_sum, hue};
        return blended;
    }
    return sub_pixel_density(state, nearest_x, nearest_y, facets, hx, hy);
}

// Facets wider than a pixel: the nearest jittered facet centre owns the
// pixel, its tilt drawn from the Beckmann slope distribution, with a thin
// dark seam where facets meet.
GlintDensity cell_density(const GlintState& state, double x, double y, double size, double hx, double hy) noexcept {
    const double gx = x / size;
    const double gy = y / size;
    const double ix = std::floor(gx);
    const double iy = std::floor(gy);
    double best = 9.0;
    double second = 9.0;
    double bx = 0.0;
    double by = 0.0;
    const double s = static_cast<double>(static_cast<std::int32_t>(state.seed));
    for (int j = -1; j <= 1; ++j) {
        for (int i = -1; i <= 1; ++i) {
            const double cx = ix + i;
            const double cy = iy + j;
            const double dx = gx - (cx + 0.1 + 0.8 * hash_at(cx, cy, s + 10.0));
            const double dy = gy - (cy + 0.1 + 0.8 * hash_at(cx, cy, s + 11.0));
            const double d = dx * dx + dy * dy;
            if (d < best) {
                second = best;
                best = d;
                bx = cx;
                by = cy;
            } else if (d < second) {
                second = d;
            }
        }
    }
    const double u1 = std::max(1e-9, hash_at(bx, by, s + 12.0));
    const double u2 = hash_at(bx, by, s + 13.0);
    const double r = std::sqrt(state.sigma2 * -engine_logarithm(u1));
    const EngineSineCosine turned = engine_sine_cosine(glint_turn * u2);
    const double dx = hx - r * turned.cosine;
    const double dy = hy - r * turned.sine;
    const double seam = std::sqrt(second) - std::sqrt(best);
    const double edge = seam < 0.08 ? 0.6 + 5.0 * seam : 1.0;
    const double tau = state.constants.tau;
    const double value = engine_exponential(-(dx * dx + dy * dy) / (tau * tau)) / state.tau_area * edge;
    const GlintDensity density{value, hash_at(bx, by, s + 14.0)};
    return density;
}

// The oxide film's rainbow: the hue `h` in [0, 1) as red, green and blue.
Linear rainbow(double h) noexcept {
    const std::array<double, 3> offsets{5.0, 3.0, 1.0};
    std::array<double, 3> channel{};
    for (std::size_t at = 0U; at < 3U; ++at) {
        const double k = std::fmod(offsets[at] + h * 6.0, 6.0);
        channel[at] = 1.0 - std::max(0.0, std::min({k, 4.0 - k, 1.0}));
    }
    const Linear colour{channel[0], channel[1], channel[2]};
    return colour;
}

// A soft shoulder, so that glints saturate toward white rather than clip.
double glint_tone(double v) noexcept {
    if (v < 0.8) return v;
    const double toned = 0.8 + 0.2 * (1.0 - engine_exponential(-(v - 0.8) * 5.0));
    return toned;
}

// One pixel at device position (x, y) from the surface's origin, `k`
// device pixels per logical unit, written as opaque BGRA.
void shade_glint(const GlintState& state, double x, double y, double k, double grain, std::uint8_t* pixel) noexcept {
    const GlintConstants& constants = state.constants;
    const double lx = state.centre_x - x / k;
    const double ly = state.centre_y - y / k;
    const double lz = glint_light_height;
    const double vx = state.centre_x - x / k;
    const double vy = state.centre_y - y / k;
    const double vz = glint_eye_height;
    const double li = 1.0 / std::sqrt(lx * lx + ly * ly + lz * lz);
    const double vi = 1.0 / std::sqrt(vx * vx + vy * vy + vz * vz);
    const double lux = lx * li;
    const double luy = ly * li;
    const double luz = lz * li;
    const double vuz = vz * vi;
    const double hx3 = lux + vx * vi;
    const double hy3 = luy + vy * vi;
    const double hz3 = luz + vuz;
    const double hi = 1.0 / std::sqrt(hx3 * hx3 + hy3 * hy3 + hz3 * hz3);
    const double half_x = hx3 * hi;
    const double half_y = hy3 * hi;
    const double half_z = hz3 * hi;
    // The facet slope that would mirror the light into the eye.
    const double hx = -half_x / half_z;
    const double hy = -half_y / half_z;
    const double facet_px = constants.grain * grain * k;
    const double speck = std::max(1.0, constants.speck * grain * k);
    GlintDensity density{};
    if (facet_px <= 0.7) {
        density = speck_density(state, x, y, speck, facet_px, hx, hy);
    } else if (facet_px >= 1.3) {
        density = cell_density(state, x, y, facet_px, hx, hy);
    } else {
        const double t = (facet_px - 0.7) / 0.6;
        const GlintDensity a = sub_pixel_density(state, x, y, 1.0 / (facet_px * facet_px), hx, hy);
        const GlintDensity b = cell_density(state, x, y, facet_px, hx, hy);
        density = GlintDensity{a.value + (b.value - a.value) * t, t < 0.5 ? a.hue : b.hue};
    }
    // Beckmann D = slope density / cos⁴θh; radiance D F / (4 n·v).
    const double cos2 = half_z * half_z;
    const double d = density.value / (cos2 * cos2);
    const double vdh = lux * half_x + luy * half_y + luz * half_z;
    const double fresnel = power(1.0 - vdh, 5U);
    const double film = constants.film;
    const double f_metal = 0.6 + 0.4 * fresnel;
    const double spec_metal = d * f_metal / (4.0 * vuz) * state.specular * (1.0 - film) * (1.0 - film);
    double spec_paint = 0.0;
    if (film > 0.0) {
        const double a2 = constants.film_alpha * constants.film_alpha;
        const double lobe = engine_exponential(-(hx * hx + hy * hy) / a2) / (glint_pi * a2) / (cos2 * cos2);
        const double f_paint = 0.04 + 0.96 * fresnel;
        spec_paint = lobe * f_paint / (4.0 * vuz) * state.specular * film;
    }
    const double ndl = luz > 0.0 ? luz : 0.0;
    const double s = static_cast<double>(static_cast<std::int32_t>(state.seed));
    const double mottle = 1.0 + (noise(x, y, 40.0 * k, s + 20.0) - 0.5) * constants.mottle;
    const double diffuse = (glint_ambient + (1.0 - glint_ambient) * ndl) * mottle;
    Linear tint{1.0, 1.0, 1.0};
    if (constants.iridescence > 0.0) {
        const Linear hue = rainbow(density.hue);
        const double m = constants.iridescence;
        tint = Linear{1.0 - m + m * hue.red * 1.6, 1.0 - m + m * hue.green * 1.6, 1.0 - m + m * hue.blue * 1.6};
    }
    const Linear& base = film > 0.0 ? state.paint : state.metal;
    const double r = base.red * diffuse * constants.diffuse + spec_metal * state.metal.red * tint.red + spec_paint;
    const double g = base.green * diffuse * constants.diffuse + spec_metal * state.metal.green * tint.green + spec_paint;
    const double b = base.blue * diffuse * constants.diffuse + spec_metal * state.metal.blue * tint.blue + spec_paint;
    pixel[0] = encode(glint_tone(b));
    pixel[1] = encode(glint_tone(g));
    pixel[2] = encode(glint_tone(r));
    pixel[3] = 255U;
}

// The glint over `area`: the preset and colours read once, then each
// pixel on its own.
std::size_t paint_glints(const FinishFill& fill, double scale, FinishRect surface, FinishRect area,
                         const PartRasterView& destination) noexcept {
    GlintState state{};
    state.constants = glint_constants_of(fill.glint);
    state.sigma2 = state.constants.alpha * state.constants.alpha;
    state.tau_area = glint_pi * state.constants.tau * state.constants.tau;
    state.bin_width = state.constants.tau * 2.0;
    state.seed = static_cast<std::uint32_t>(fill.seed);
    state.metal = linear_of(fill.secondary);
    state.paint = linear_of(fill.paint);
    state.specular = state.constants.specular * fill.gloss;
    state.centre_x = static_cast<double>(surface.width) * 0.5 / scale;
    state.centre_y = static_cast<double>(surface.height) * 0.5 / scale;
    for (std::int64_t y = area.y; y < area.y + area.height; ++y) {
        const std::size_t row_start = static_cast<std::size_t>(y) * destination.row_bytes;
        std::uint8_t* pixel =
            destination.pixels.data() + row_start + static_cast<std::size_t>(area.x) * part_raster_bytes_per_pixel;
        const double down = static_cast<double>(y - surface.y);
        for (std::int64_t x = area.x; x < area.x + area.width; ++x) {
            shade_glint(state, static_cast<double>(x - surface.x), down, scale, fill.grain, pixel);
            pixel = pixel + part_raster_bytes_per_pixel;
        }
    }
    const std::size_t written = static_cast<std::size_t>(area.width) * static_cast<std::size_t>(area.height);
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
    case FinishKind::glint: return paint_glints(fill, scale, surface, area, destination);
    }
    // A kind outside the enumeration is never accepted
    // (valid_surface_material): the flat finish, the paint as a solid.
    return paint_rows<FinishKind::flat>(fill, scale, surface, area, destination, scratch);
}

std::uint8_t encode_linear(double linear) noexcept {
    const std::uint8_t byte = encode(linear);
    return byte;
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
    case FinishKind::glint: return weights.glint;
    }
    // A kind outside the enumeration is never accepted: past the budget.
    const std::uint64_t unbounded = maximum_material_bake_effort.units + 1U;
    return unbounded;
}

} // namespace gui_forms::engine_primitives
