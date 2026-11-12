// The lamps' and lit keys' baker (skeudoindustrial surfaces, phase 3),
// ported from the industrial-surfaces study
// (notes:experiments/industrial-surfaces/cpp/lamp_keys.cpp at c9bef59). It
// is an offline tool: the integrator runs it and ships its bakes, which
// engine_light::LightPlaneCache reads and composites at run time; nothing
// here runs in a program. One change from the study: legends are drawn by
// GUI.Drawing's text engine (the shared HarfBuzz and FreeType engine) in the
// bundled Portsmouth Mono (owner, 2026-11-12), as coverage masks the lens
// samples, in place of the study's stroke font.
//
// Usage: gui_forms_lamp_keys <output directory> <size in pixels> <family>
//        gui_forms_lamp_keys --legend-test
// The second checks the legend masks and exits.
//
// Dialog keys lit from within (a copy of lamps.cpp for the OK and CANCEL keys,
// with what the engine renders taught: the colour is the lamp's; a clear key
// has a black void behind it; its coil is large and sparse in a faint haze; the
// top has a moulded relief on its normal).
//
// Panel lamps lit from within: an elevated keycap, a faceted jewel indicator and
// a split-legend annunciator, rendered with the participating-child law of the
// bfft photonic transport engine (photonic_transport_field/native/
// child_boundary_response.hpp: ConvexOpticalChild::evaluate_law).
//
// Each lens is a convex dielectric child. A ray refracts in; along every chord
// inside, light is retained by T = exp(−∫σ) and the medium contributes its own
// source radiance, L_out = B²·T·L_in + B·(1−T)·S; internal reflections are
// followed leg by leg until the weight falls below a cutoff. The source function
// varies in space: each lamp is a white-hot coiled filament on two dark lead
// wires, suspended in the lens, and the lens carries the lamp's light diffused
// to a degree set by its grade. Reflected copies of the filament in the lens's
// faces arise from the internal legs; nothing is sampled at random inside a lens.
//
// Lighting is linear in each lamp's power, so each pixel is written to one
// image for the room's light and one per lamp at unit white power. A frame is
// tone(room + Σ power_i × colour_i × lamp_i), per channel.
//
// Usage: lamps <output directory> <size in pixels> <keycap|annunciator|indicator>
// Writes <family>_<grade>_<up|down>.f32: int32 width, height and lamp count,
// then the room image and each lamp's image, width × height × 3 float32 each;
// and <family>_<grade>_<state>_<off|on>.bmp previews.

#include "drawing_skia.hpp"
#include "gui_forms/drawing/bitmap/bitmap.hpp"
#include "gui_forms/drawing/brush/brush.hpp"
#include "gui_forms/drawing/color/color.hpp"
#include "gui_forms/drawing/font/font.hpp"
#include "gui_forms/drawing/geometry/point_f/point_f.hpp"
#include "gui_forms/drawing/graphics_recorder/graphics_recorder.hpp"
#include "gui_forms/drawing/string_format/string_format.hpp"
#include "gui_forms/drawing/types/drawing_types.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

namespace lamps {

constexpr double pi = 3.14159265358979323846;

struct Vec3 final {
    double x{};
    double y{};
    double z{};
};

Vec3 add(Vec3 a, Vec3 b) { return Vec3{a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 sub(Vec3 a, Vec3 b) { return Vec3{a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 scale(Vec3 a, double s) { return Vec3{a.x * s, a.y * s, a.z * s}; }
Vec3 mul(Vec3 a, Vec3 b) { return Vec3{a.x * b.x, a.y * b.y, a.z * b.z}; }
double dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 cross(Vec3 a, Vec3 b) { return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
double length(Vec3 a) { return std::sqrt(dot(a, a)); }
Vec3 normalized(Vec3 a) { return scale(a, 1.0 / length(a)); }
double max_component(Vec3 a) { return std::max(a.x, std::max(a.y, a.z)); }
Vec3 reflected(Vec3 d, Vec3 n) { return normalized(sub(d, scale(n, 2.0 * dot(d, n)))); }

Vec3 exp_neg(Vec3 sigma, double t) {
    return Vec3{std::exp(-sigma.x * t), std::exp(-sigma.y * t), std::exp(-sigma.z * t)};
}

double smoothstep(double a, double b, double x) {
    const double t = std::clamp((x - a) / (b - a), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// Polynomial smooth maximum: max(a, b) raised by at most k/4 where they meet,
// which rounds the edge between two planes with a radius of about k.
double smooth_max(double a, double b, double k) {
    const double h = std::max(k - std::abs(a - b), 0.0) / k;
    return std::max(a, b) + h * h * k * 0.25;
}

// ---- scene -------------------------------------------------------------------------------
//
// Units: a lens is about 0.68 across; z up out of the panel; y up the screen.

enum class Family : std::uint8_t {
    keycap,
    jewel,
    annunciator,
};

// How the lens's face is lettered.
enum class Legend : std::uint8_t {
    none,
    black_ink,   // opaque black lettering on the lit field
    white_ink,   // opaque white lettering on the lit field
    dead_front,  // the face painted black; the letters are white diffuser ink and glow
    grey_ink,    // opaque mid-grey lettering
    lit_ink,     // white diffuser ink on the open face: grey by the room, lit through by the lamps
};

// A lens grade as a participating medium.
struct Grade final {
    const char* name{};
    Vec3 density{};           // extinction σ per channel
    double albedo{};          // share of σ that re-emits (the rest is absorbed)
    double glow_reach{};      // distance over which a lamp's diffused light falls to half
    double room{};            // the room light the lens itself carries, as source radiance
    double rear_reflector{};  // reflectance of a silvered back face (0: plain polished face)
    double glow_gain{1.0};    // brightness of the diffused lamp light against the filament's
    double rear_diffuser{};   // albedo of a frosted back face that scatters the lamps' light
    Vec3 pigment{};           // added absorption per channel: the material's own colour
    double near_reach{};      // a second, short reach: a brighter, soft area round the lamp
    double near_gain{};       // its brightness against the long glow (0: none)
};

// One lamp: a coiled filament about the axis a–b, on two lead wires that run
// down from its ends to the base of the lens.
struct Lamp final {
    Vec3 a{};
    Vec3 b{};
    double coil_radius{0.014};
    double pitch{0.011};
    double wire_radius{0.0032};
    double lead_radius{0.0045};
};

constexpr int max_lamps = 2;

struct Plane final {
    Vec3 normal{};
    double offset{};  // the plane is dot(normal, p) = offset
};

// One line of legend ink: a coverage mask, row 0 at the top, laid over
// the lens's x, y plane (y up). Mask pixel (column, row) covers the square
// whose top left corner is (left + column · pitch, top − row · pitch).
struct LegendMask final {
    std::uint32_t columns{};
    std::uint32_t rows{};
    double left{};
    double top{};
    double pitch{};
    std::vector<float> coverage;
};

struct Scene final {
    Family family{Family::keycap};
    // keycap and annunciator body
    double half_base{0.34};
    double half_top{0.275};
    double bottom{-0.07};
    double top{0.30};
    double chamfer{0.05};
    double corner{0.07};
    double softness{0.03};
    double dish{0.012};
    double aspect{1.0};  // the lens's height on screen over its width
    // jewel body: the convex hull's planes, and a little edge softening
    std::vector<Plane> facets;
    double facet_softness{0.003};
    // the panel
    bool round_well{false};
    double well_half{0.385};
    double well_radius{0.06};
    double well_floor{-0.22};
    double press{0.0};
    // lamps
    std::array<Lamp, max_lamps> lamps{};
    int lamp_count{1};
    double filament_radiance{24.0};
    // annunciator: an opaque divider between its halves, the legend, and
    // whether the sidewalls are painted to block light
    bool divider{false};
    Legend legend_style{Legend::none};
    bool sidewall_blocked{false};
    double field_of_view{21.0};  // the camera's, in degrees: small lamps are framed closer
    // A control draws only itself: no panel, no well, nothing beyond its own
    // edge. The camera looks straight down and the image spans exactly
    // ±frame_half_x by ±frame_half_y at z = 0; rays that miss the lens are
    // transparent.
    bool control_only{false};
    double frame_half_x{};
    double frame_half_y{};
    // The control camera's eye: straight above by default; moved toward the
    // viewer (negative y) it looks at the key a little from the front, so the
    // front sidewall shows. The frame is the window on z = 0 that the key's
    // projection fills, centred at (0, frame_centre_y).
    Vec3 control_eye{0.0, 0.0, 2.6};
    double frame_centre_y{};
    // The legend's lines, each a coverage mask (LegendWriter).
    std::vector<LegendMask> legends;
    Grade grade{};
    // the moulded relief of a box lens's top: slopes of its orange-peel waves
    // and of its finer grain (0: polished)
    double relief{};
    double grain{};
    // a matte black back face: a clear key over a black void, where nothing
    // reflects back up from behind its cold coil
    bool black_back{};
    // the coil gives its light but is not seen: no wire in the medium, only
    // its diffused glow
    bool hidden_coil{};
};

Vec3 lowered(const Scene& scene, Vec3 p) { return Vec3{p.x, p.y, p.z + scene.press}; }

double box_lens_distance(const Scene& scene, Vec3 world) {
    const Vec3 p = lowered(scene, world);
    // y is measured in units of the aspect, so the lens is aspect × as tall
    const double ax = std::abs(p.x);
    const double ay = std::abs(p.y) / scene.aspect;
    const double height = scene.top - scene.bottom;
    const double slope = (scene.half_base - scene.half_top) / height;
    const double side_norm = 1.0 / std::sqrt(1.0 + slope * slope);
    const double half_here = scene.half_base - slope * (p.z - scene.bottom);
    const double across = std::min(1.0, ax / scene.half_top);
    const double top_here = scene.top - scene.dish * (1.0 - across * across);
    const double k = scene.softness;
    double d = p.z - top_here;
    d = smooth_max(d, scene.bottom - p.z, k);
    d = smooth_max(d, (ax - half_here) * side_norm, k);
    d = smooth_max(d, (ay - half_here) * side_norm * scene.aspect, k);
    const double c = scene.chamfer;
    d = smooth_max(d, (ax + p.z - (scene.half_top + scene.top - c)) * 0.7071067811865476, k);
    d = smooth_max(d, (std::abs(p.y) + p.z - (scene.half_top * scene.aspect + scene.top - c)) * 0.7071067811865476, k);
    d = smooth_max(d, (ax + std::abs(p.y) - (half_here * (1.0 + scene.aspect) - scene.corner)) * 0.7071067811865476, k);
    return d;
}

double jewel_distance(const Scene& scene, Vec3 world) {
    const Vec3 p = lowered(scene, world);
    double d = -1e9;
    for (const Plane& plane : scene.facets) {
        d = smooth_max(d, dot(plane.normal, p) - plane.offset, scene.facet_softness);
    }
    return d;
}

double lens_distance(const Scene& scene, Vec3 p) {
    switch (scene.family) {
    case Family::jewel: return jewel_distance(scene, p);
    case Family::keycap:
    case Family::annunciator:
    default: return box_lens_distance(scene, p);
    }
}

double rounded_rect(double x, double y, double half_x, double half_y, double radius) {
    const double qx = std::abs(x) - half_x + radius;
    const double qy = std::abs(y) - half_y + radius;
    const double ox = std::max(qx, 0.0);
    const double oy = std::max(qy, 0.0);
    return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0) - radius;
}

// The panel with its well: solid below z = 0 outside the well, and below the
// well's floor.
double plate_distance(const Scene& scene, Vec3 p) {
    const double well = scene.round_well ? std::sqrt(p.x * p.x + p.y * p.y) - scene.well_half
                                         : rounded_rect(p.x, p.y, scene.well_half, scene.well_half * scene.aspect, scene.well_radius);
    return std::min(std::max(p.z, -well), p.z - scene.well_floor);
}

// A jewel cut as a round brilliant's crown: a flat table, three rings of
// facets falling away from it, a short vertical girdle and a flat back.
void cut_jewel(Scene& scene) {
    scene.facets.clear();
    const double table_z = 0.22;
    scene.facets.push_back(Plane{Vec3{0.0, 0.0, 1.0}, table_z});
    scene.facets.push_back(Plane{Vec3{0.0, 0.0, -1.0}, 0.08});  // the back, at z = −0.08
    struct Ring final {
        int count;
        double tilt_degrees;
        double inner_radius;
        double outer_radius;
        double phase;  // as a fraction of one facet
    };
    const std::array<Ring, 3> rings = {
        Ring{8, 22.0, 0.10, 0.19, 0.0},
        Ring{16, 45.0, 0.19, 0.26, 0.5},
        Ring{16, 68.0, 0.26, 0.30, 0.0},
    };
    double z = table_z;
    for (const Ring& ring : rings) {
        const double tilt = ring.tilt_degrees * pi / 180.0;
        for (int i = 0; i < ring.count; ++i) {
            const double azimuth = 2.0 * pi * (i + ring.phase) / ring.count;
            const Vec3 n{std::sin(tilt) * std::cos(azimuth), std::sin(tilt) * std::sin(azimuth), std::cos(tilt)};
            const Vec3 through{ring.inner_radius * std::cos(azimuth), ring.inner_radius * std::sin(azimuth), z};
            scene.facets.push_back(Plane{n, dot(n, through)});
        }
        z -= (ring.outer_radius - ring.inner_radius) * std::tan(tilt);
    }
    for (int i = 0; i < 16; ++i) {
        const double azimuth = 2.0 * pi * (i + 0.5) / 16.0;
        scene.facets.push_back(Plane{Vec3{std::cos(azimuth), std::sin(azimuth), 0.0}, 0.30});
    }
}

// ---- legend: Portsmouth Mono through GUI.Drawing's text engine ---------------------------

// Draws legend lines in the bundled Portsmouth Mono with GUI.Drawing's
// text engine, as coverage masks. One thread: the one that made it (the
// executor's rule); the masks it makes are read by any.
class LegendWriter final {
public:
    explicit LegendWriter(const char* font_path) {
        std::ifstream file(font_path, std::ios::binary);
        if (!file) return;
        const std::vector<char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        std::vector<std::byte> face(bytes.size());
        for (std::size_t index = 0; index < bytes.size(); ++index) {
            face[index] = static_cast<std::byte>(bytes[index]);
        }
        ready_ = !face.empty() && executor_.register_typeface(family, face);
    }

    [[nodiscard]] bool ready() const noexcept { return ready_; }

    // One line of `text`, centred on x = 0, the foot of its ink on
    // `baseline`, its ink `height` tall (the capitals: legends are upper
    // case), into `mask`. The line is drawn at a 160-pixel em and its ink
    // measured, so the mask's pitch makes the ink exactly `height` tall;
    // one empty pixel borders the ink. False, leaving `mask` as it was,
    // when the face is not ready, the text is empty or draws no ink.
    bool draw(const std::string& text, double baseline, double height, LegendMask& mask) {
        if (!ready_ || text.empty() || !(height > 0.0)) return false;
        constexpr double em = 160.0;
        const double width = em * (0.75 * static_cast<double>(text.size()) + 1.0);
        const std::uint32_t columns = static_cast<std::uint32_t>(std::ceil(width));
        const std::uint32_t rows = static_cast<std::uint32_t>(em * 2.0);
        gui_drawing::Bitmap canvas(columns, rows);
        gui_drawing::GraphicsRecorder recorder;
        recorder.clear(gui_drawing::Color::from_argb(0U, 0U, 0U, 0U));
        const gui_drawing::Font face(std::string(family), em, gui_drawing::FontStyle::regular,
                                     gui_drawing::GraphicsUnit::pixel);
        const gui_drawing::SolidBrush ink(gui_drawing::Color::from_argb(255U, 255U, 255U, 255U));
        recorder.draw_string(text, face, ink, gui_drawing::PointF{em * 0.5, em * 0.25}, gui_drawing::StringFormat());
        const gui_drawing::render::RasterResult drawn = executor_.execute(recorder, canvas);
        if (!drawn) return false;
        // The ink's bounds: the first and last rows and columns with any.
        std::uint32_t first_row = rows;
        std::uint32_t last_row = 0;
        std::uint32_t first_column = columns;
        std::uint32_t last_column = 0;
        for (std::uint32_t y = 0; y < rows; ++y) {
            for (std::uint32_t x = 0; x < columns; ++x) {
                if (canvas.get_pixel(x, y).alpha() == 0U) continue;
                first_row = std::min(first_row, y);
                last_row = std::max(last_row, y);
                first_column = std::min(first_column, x);
                last_column = std::max(last_column, x);
            }
        }
        if (first_row > last_row || first_column > last_column) return false;
        if (first_row == 0U || first_column == 0U || last_row + 1U >= rows || last_column + 1U >= columns) {
            return false;  // the ink reached the canvas's edge: a line too long to measure
        }
        const std::uint32_t ink_rows = last_row - first_row + 1U;
        const std::uint32_t ink_columns = last_column - first_column + 1U;
        LegendMask made{};
        made.columns = ink_columns + 2U;
        made.rows = ink_rows + 2U;
        made.pitch = height / static_cast<double>(ink_rows);
        made.left = -0.5 * static_cast<double>(ink_columns) * made.pitch - made.pitch;
        made.top = baseline + height + made.pitch;
        made.coverage.assign(static_cast<std::size_t>(made.columns) * made.rows, 0.0F);
        for (std::uint32_t row = 0; row < made.rows; ++row) {
            for (std::uint32_t column = 0; column < made.columns; ++column) {
                const std::uint32_t alpha = canvas.get_pixel(first_column - 1U + column, first_row - 1U + row).alpha();
                made.coverage[static_cast<std::size_t>(row) * made.columns + column] =
                    static_cast<float>(static_cast<double>(alpha) / 255.0);
            }
        }
        mask = std::move(made);
        return true;
    }

private:
    static constexpr const char* family = "Portsmouth Mono";
    gui_drawing::render::SkiaExecutor executor_;
    bool ready_{};
};

// Appends one line of `text`, centred on x = 0 with its baseline at
// `baseline`, capitals `height` tall; a line the writer cannot draw is
// left out (the key bakes without it).
void set_line(Scene& scene, LegendWriter& writer, const std::string& text, double baseline, double height) {
    LegendMask mask{};
    if (writer.draw(text, baseline, height, mask)) scene.legends.push_back(std::move(mask));
}

// A mask's pixel, 0 outside it.
double mask_pixel(const LegendMask& mask, double column, double row) {
    if (column < 0.0 || row < 0.0) return 0.0;
    const std::size_t x = static_cast<std::size_t>(column);
    const std::size_t y = static_cast<std::size_t>(row);
    if (x >= mask.columns || y >= mask.rows) return 0.0;
    return static_cast<double>(mask.coverage[y * mask.columns + x]);
}

// A mask's coverage at (x, y), bilinear between pixel centres.
double mask_coverage(const LegendMask& mask, double x, double y) {
    const double column = (x - mask.left) / mask.pitch - 0.5;
    const double row = (mask.top - y) / mask.pitch - 0.5;
    const bool inside = column > -1.0 && row > -1.0 && column < static_cast<double>(mask.columns) &&
                        row < static_cast<double>(mask.rows);
    if (!inside) return 0.0;
    const double column0 = std::floor(column);
    const double row0 = std::floor(row);
    const double across = column - column0;
    const double down = row - row0;
    const double top = mask_pixel(mask, column0, row0) * (1.0 - across) + mask_pixel(mask, column0 + 1.0, row0) * across;
    const double bottom =
        mask_pixel(mask, column0, row0 + 1.0) * (1.0 - across) + mask_pixel(mask, column0 + 1.0, row0 + 1.0) * across;
    const double covered = top * (1.0 - down) + bottom * down;
    return covered;
}

// Share of a point's footprint covered by legend ink.
double legend_coverage(const Scene& scene, double x, double y) {
    double covered = 0.0;
    for (const LegendMask& mask : scene.legends) {
        covered = std::max(covered, mask_coverage(mask, x, y));
    }
    return covered;
}

// ---- marching -----------------------------------------------------------------------------

enum class Surface : std::uint8_t {
    none,
    lens,
    plate,
};

struct Hit final {
    Surface surface{Surface::none};
    double t{};
};

Hit march_outside(const Scene& scene, Vec3 origin, Vec3 direction) {
    Hit hit{};
    double t = 0.0;
    for (int step = 0; step < 200; ++step) {
        const Vec3 p = add(origin, scale(direction, t));
        if ((p.z > 0.8 && direction.z > 0.0) || t > 8.0) {
            return hit;
        }
        const double dl = lens_distance(scene, p);
        const double dp = scene.control_only ? 1e9 : plate_distance(scene, p);
        const double d = std::min(dl, dp);
        if (d < 2e-4) {
            hit.surface = dl < dp ? Surface::lens : Surface::plate;
            hit.t = t;
            return hit;
        }
        t += d * 0.9;
    }
    return hit;
}

double march_inside(const Scene& scene, Vec3 origin, Vec3 direction) {
    double t = 0.0;
    for (int step = 0; step < 240; ++step) {
        const double d = -lens_distance(scene, add(origin, scale(direction, t)));
        if (d < 2e-4) {
            return t;
        }
        t += d * 0.9;
        if (t > 4.0) {
            return t;
        }
    }
    return t;
}

// ---- the moulded relief ---------------------------------------------------------------------
//
// A height field over the lens's top, as a slope (dh/dx, dh/dy). The waves are
// Stillwater's caustic surface (playsuite-games shared/ambient/src/caustics.cpp,
// 0efc1f0): fourteen integer wave vectors on a tile, amplitude 1/|k|^1.75,
// scaled so that typical peaks reach a slope of 1. The grain is smooth value
// noise on cells about two pixels across.

struct ReliefWave final {
    double kx{};
    double ky{};
    double amplitude{};
    double phase{};
};

struct ReliefWaves final {
    std::array<ReliefWave, 14> waves{};
};

ReliefWaves make_relief_waves() {
    ReliefWaves result{};
    std::uint32_t state = 1U * 2654435761U + 12345U;
    std::size_t count = 0;
    double bound = 0.0;
    while (count < result.waves.size()) {
        state = state * 1664525U + 1013904223U;
        std::uint32_t x = state;
        x ^= x >> 16U;
        x *= 0x7FEB352DU;
        x ^= x >> 15U;
        const int kx = static_cast<int>(x % 15U) - 7;
        state = state * 1664525U + 1013904223U;
        x = state;
        x ^= x >> 16U;
        x *= 0x7FEB352DU;
        x ^= x >> 15U;
        const int ky = static_cast<int>(x % 15U) - 7;
        const double radius = std::sqrt(static_cast<double>(kx * kx + ky * ky));
        if (radius < 1.4 || radius > 7.2) {
            continue;
        }
        state = state * 1664525U + 1013904223U;
        x = state;
        x ^= x >> 16U;
        x *= 0x7FEB352DU;
        x ^= x >> 15U;
        ReliefWave wave{};
        wave.kx = kx;
        wave.ky = ky;
        wave.amplitude = 1.0 / std::pow(radius, 1.75);
        wave.phase = static_cast<double>(x & 0xFFFFFFU) / 16777216.0;
        bound += wave.amplitude * radius;
        result.waves[count] = wave;
        ++count;
    }
    for (ReliefWave& wave : result.waves) {
        wave.amplitude /= bound * 0.45;
    }
    return result;
}

const ReliefWaves relief_waves = make_relief_waves();
constexpr double relief_tile = 0.36;   // lens units per wave tile
constexpr double grain_cell = 0.0045;  // lens units per grain cell

double grain_hash(int x, int y) {
    std::uint32_t h = static_cast<std::uint32_t>(x) * 0x8DA6B343U ^ static_cast<std::uint32_t>(y) * 0xD8163841U;
    h ^= h >> 13U;
    h *= 0x85EBCA6BU;
    h ^= h >> 16U;
    return static_cast<double>(h & 0xFFFFFFU) / 16777216.0 * 2.0 - 1.0;
}

void relief_slope(const Scene& scene, double x, double y, double& gx, double& gy) {
    gx = 0.0;
    gy = 0.0;
    if (scene.relief > 0.0) {
        const double k = 2.0 * pi / relief_tile;
        for (const ReliefWave& wave : relief_waves.waves) {
            const double c = std::cos(k * (wave.kx * x + wave.ky * y) + 2.0 * pi * wave.phase) * wave.amplitude;
            gx += c * wave.kx;
            gy += c * wave.ky;
        }
        gx *= scene.relief;
        gy *= scene.relief;
    }
    if (scene.grain > 0.0) {
        const double u = x / grain_cell;
        const double v = y / grain_cell;
        const int i = static_cast<int>(std::floor(u));
        const int j = static_cast<int>(std::floor(v));
        const double fu = u - i;
        const double fv = v - j;
        const double su = fu * fu * fu * (fu * (fu * 6.0 - 15.0) + 10.0);
        const double sv = fv * fv * fv * (fv * (fv * 6.0 - 15.0) + 10.0);
        const double du = 30.0 * fu * fu * (fu * (fu - 2.0) + 1.0);
        const double dv = 30.0 * fv * fv * (fv * (fv - 2.0) + 1.0);
        const double a = grain_hash(i, j);
        const double b = grain_hash(i + 1, j);
        const double c = grain_hash(i, j + 1);
        const double d = grain_hash(i + 1, j + 1);
        // h = a + (b − a)·su + (c − a)·sv + (a − b − c + d)·su·sv, per cell
        gx += scene.grain * 0.5 * ((b - a) + (a - b - c + d) * sv) * du;
        gy += scene.grain * 0.5 * ((c - a) + (a - b - c + d) * su) * dv;
    }
}

// The lens's normal, with the moulded relief on its top face only (on the
// chamfer, at grazing angles, it sparkled as a ragged rim). It is used both
// entering and leaving, so reflection and refraction see the same surface.
Vec3 lens_normal(const Scene& scene, Vec3 p) {
    const double e = 2e-4;
    const double dx = lens_distance(scene, Vec3{p.x + e, p.y, p.z}) - lens_distance(scene, Vec3{p.x - e, p.y, p.z});
    const double dy = lens_distance(scene, Vec3{p.x, p.y + e, p.z}) - lens_distance(scene, Vec3{p.x, p.y - e, p.z});
    const double dz = lens_distance(scene, Vec3{p.x, p.y, p.z + e}) - lens_distance(scene, Vec3{p.x, p.y, p.z - e});
    const Vec3 n = normalized(Vec3{dx, dy, dz});
    if (scene.family == Family::jewel || (scene.relief <= 0.0 && scene.grain <= 0.0) || n.z < 0.85) {
        return n;
    }
    double gx = 0.0;
    double gy = 0.0;
    relief_slope(scene, p.x, p.y, gx, gy);
    const double weight = smoothstep(0.85, 0.97, n.z);
    return normalized(Vec3{n.x - gx * weight, n.y - gy * weight, n.z});
}

Vec3 plate_normal(const Scene& scene, Vec3 p) {
    const double e = 2e-4;
    const double dx = plate_distance(scene, Vec3{p.x + e, p.y, p.z}) - plate_distance(scene, Vec3{p.x - e, p.y, p.z});
    const double dy = plate_distance(scene, Vec3{p.x, p.y + e, p.z}) - plate_distance(scene, Vec3{p.x, p.y - e, p.z});
    const double dz = plate_distance(scene, Vec3{p.x, p.y, p.z + e}) - plate_distance(scene, Vec3{p.x, p.y, p.z - e});
    return normalized(Vec3{dx, dy, dz});
}

// ---- light ----------------------------------------------------------------------------------

// A dark room with one large softbox above and in front of the panel.
double environment(Vec3 d) {
    if (d.z <= 0.0) {
        return 0.004;
    }
    const Vec3 key_light = normalized(Vec3{-0.15, -0.55, 1.0});
    const Vec3 u = normalized(cross(key_light, Vec3{0.0, 0.0, 1.0}));
    const Vec3 v = cross(u, key_light);
    const double c = dot(d, key_light);
    double box = 0.0;
    if (c > 0.0) {
        const double a = std::abs(dot(d, u)) / 0.42;
        const double b = std::abs(dot(d, v)) / 0.22;
        const double m = std::max(a, b);
        box = m < 1.0 ? 1.0 : std::max(0.0, 1.0 - (m - 1.0) * 6.0);
    }
    return 0.012 + 0.03 * d.z + 3.0 * box;
}

double fresnel_dielectric(double cos_i, double eta_i, double eta_t) {
    const double sin_t = eta_i / eta_t * std::sqrt(std::max(0.0, 1.0 - cos_i * cos_i));
    if (sin_t >= 1.0) {
        return 1.0;
    }
    const double cos_t = std::sqrt(std::max(0.0, 1.0 - sin_t * sin_t));
    const double rs = (eta_i * cos_i - eta_t * cos_t) / (eta_i * cos_i + eta_t * cos_t);
    const double rp = (eta_t * cos_i - eta_i * cos_t) / (eta_t * cos_i + eta_i * cos_t);
    return 0.5 * (rs * rs + rp * rp);
}

void basis(Vec3 n, Vec3& u, Vec3& v) {
    const Vec3 helper = std::abs(n.z) < 0.9 ? Vec3{0.0, 0.0, 1.0} : Vec3{1.0, 0.0, 0.0};
    u = normalized(cross(helper, n));
    v = cross(n, u);
}

// A fixed, well-spread set of cosine-weighted directions about n, rotated by
// `rotation`: a spiral on the disc lifted to the hemisphere.
Vec3 spiral_direction(int i, int count, double rotation, Vec3 n) {
    const double golden = 2.399963229728653;
    const double r1 = (i + 0.5) / count;
    const double r = std::sqrt(r1);
    const double phi = i * golden + rotation;
    Vec3 u{};
    Vec3 v{};
    basis(n, u, v);
    return normalized(add(add(scale(u, r * std::cos(phi)), scale(v, r * std::sin(phi))), scale(n, std::sqrt(1.0 - r1))));
}

double hashed_rotation(Vec3 p) {
    const std::uint32_t qx = static_cast<std::uint32_t>(static_cast<std::int32_t>(std::floor(p.x * 4096.0)));
    const std::uint32_t qy = static_cast<std::uint32_t>(static_cast<std::int32_t>(std::floor(p.y * 4096.0)));
    const std::uint32_t qz = static_cast<std::uint32_t>(static_cast<std::int32_t>(std::floor(p.z * 4096.0)));
    std::uint32_t h = qx * 0x27d4eb2dU ^ qy * 0x165667b1U ^ qz * 0x9e3779b1U;
    h = (h ^ (h >> 15U)) * 0x85ebca6bU;
    h = (h ^ (h >> 13U)) * 0xc2b2ae35U;
    h ^= h >> 16U;
    return 2.0 * pi * (h * (1.0 / 4294967296.0));
}

// The room's light on an upward-facing matte surface, per unit albedo
// (cosine-weighted mean of the environment), computed once.
double room_on_upward_surface() {
    double sum = 0.0;
    const int count = 4096;
    for (int i = 0; i < count; ++i) {
        sum += environment(spiral_direction(i, count, 0.0, Vec3{0.0, 0.0, 1.0}));
    }
    return sum / count;
}

// ---- the participating medium -----------------------------------------------------------

double axis_distance(const Scene& scene, const Lamp& lamp, Vec3 p) {
    const Vec3 a = sub(lamp.a, Vec3{0.0, 0.0, scene.press});
    const Vec3 b = sub(lamp.b, Vec3{0.0, 0.0, scene.press});
    const Vec3 ba = sub(b, a);
    const double h = std::clamp(dot(sub(p, a), ba) / dot(ba, ba), 0.0, 1.0);
    return length(sub(p, add(a, scale(ba, h))));
}

// Distance to the filament's wire. The coil runs along x; the nearest point is
// taken among the three turns at the point's own angle, which is close to
// exact while the pitch is small against the coil radius.
double filament_distance(const Scene& scene, const Lamp& lamp, Vec3 p) {
    const Vec3 c = sub(scale(add(lamp.a, lamp.b), 0.5), Vec3{0.0, 0.0, scene.press});
    const double half = 0.5 * (lamp.b.x - lamp.a.x);
    const Vec3 q = sub(p, c);
    const double theta = std::atan2(q.z, q.y);
    const double k0 = std::round(q.x / lamp.pitch - theta / (2.0 * pi));
    double best = 1e9;
    for (int k = -1; k <= 1; ++k) {
        const double x = std::clamp(lamp.pitch * (theta / (2.0 * pi) + k0 + k), -half, half);
        const double angle = 2.0 * pi * x / lamp.pitch;
        const Vec3 h{x, lamp.coil_radius * std::cos(angle), lamp.coil_radius * std::sin(angle)};
        best = std::min(best, length(sub(q, h)));
    }
    return best - lamp.wire_radius;
}

double lead_distance(const Scene& scene, const Lamp& lamp, Vec3 p) {
    const double foot = scene.family == Family::jewel ? -0.075 - scene.press : scene.bottom - scene.press + 0.01;
    double best = 1e9;
    for (int side = 0; side < 2; ++side) {
        const Vec3 top = sub(side == 0 ? lamp.a : lamp.b, Vec3{0.0, 0.0, scene.press});
        const double z = std::clamp(p.z, foot, top.z);
        best = std::min(best, length(sub(p, Vec3{top.x, top.y, z})));
    }
    return best - lamp.lead_radius;
}

constexpr double wire_density = 400.0;

struct MediumPoint final {
    Vec3 extinction{};
    std::array<Vec3, max_lamps> glow{};  // σ·S per lamp, at unit lamp power
    Vec3 room{};
};

MediumPoint medium_at(const Scene& scene, Vec3 p) {
    const Grade& g = scene.grade;
    const double soft = 0.0008;
    MediumPoint m{};
    double solid = 0.0;
    std::array<double, max_lamps> hot{};
    for (int i = 0; i < scene.lamp_count; ++i) {
        const Lamp& lamp = scene.lamps[static_cast<std::size_t>(i)];
        const double wire = scene.hidden_coil ? 0.0 : 1.0 - smoothstep(-soft, soft, filament_distance(scene, lamp, p));
        const double lead = scene.hidden_coil ? 0.0 : 1.0 - smoothstep(-soft, soft, lead_distance(scene, lamp, p));
        hot[static_cast<std::size_t>(i)] = wire;
        solid = std::max(solid, std::max(wire, lead));
    }
    if (scene.divider) {
        // an opaque sheet between the two halves, below the lens face
        solid = std::max(solid, 1.0 - smoothstep(0.003, 0.0045, std::abs(p.y)));
    }
    const double dense = wire_density * solid;
    m.extinction = add(add(g.density, g.pigment), Vec3{dense, dense, dense});
    for (int i = 0; i < scene.lamp_count; ++i) {
        const Lamp& lamp = scene.lamps[static_cast<std::size_t>(i)];
        const double beyond = std::max(0.0, axis_distance(scene, lamp, p) - lamp.coil_radius);
        const double reach = g.glow_reach;
        double diffused = reach * reach / (reach * reach + beyond * beyond);
        if (g.near_gain > 0.0) {
            const double near = g.near_reach;
            diffused += g.near_gain * near * near / (near * near + beyond * beyond);
        }
        if (scene.divider && (p.y > 0.0) != (lamp.a.y > 0.0)) {
            diffused = 0.0;  // the divider keeps each lamp's light to its own half
        }
        const double filament = wire_density * hot[static_cast<std::size_t>(i)] * scene.filament_radiance;
        m.glow[static_cast<std::size_t>(i)] = add(Vec3{filament, filament, filament},
                                                  scale(g.density, g.albedo * g.glow_gain * diffused * (1.0 - solid)));
    }
    m.room = scale(g.density, g.albedo * g.room * (1.0 - solid));
    return m;
}

double wire_clearance(const Scene& scene, Vec3 p) {
    if (scene.hidden_coil) {
        return 1.0;  // no wire to step round
    }
    double d = 1e9;
    for (int i = 0; i < scene.lamp_count; ++i) {
        const Lamp& lamp = scene.lamps[static_cast<std::size_t>(i)];
        d = std::min(d, std::min(filament_distance(scene, lamp, p), lead_distance(scene, lamp, p)));
    }
    return std::max(0.0, d);
}

struct Radiance final {
    Vec3 base{};
    std::array<Vec3, max_lamps> glow{};
};

Radiance radiance_add(const Radiance& a, const Radiance& b) {
    Radiance r{};
    r.base = add(a.base, b.base);
    for (int i = 0; i < max_lamps; ++i) {
        r.glow[static_cast<std::size_t>(i)] = add(a.glow[static_cast<std::size_t>(i)], b.glow[static_cast<std::size_t>(i)]);
    }
    return r;
}

Radiance radiance_scale(const Radiance& a, Vec3 w) {
    Radiance r{};
    r.base = mul(a.base, w);
    for (int i = 0; i < max_lamps; ++i) {
        r.glow[static_cast<std::size_t>(i)] = mul(a.glow[static_cast<std::size_t>(i)], w);
    }
    return r;
}

double channel(Vec3 v, int c) { return c == 0 ? v.x : (c == 1 ? v.y : v.z); }

void add_channel(Vec3& v, int c, double amount) {
    if (c == 0) {
        v.x += amount;
    } else if (c == 1) {
        v.y += amount;
    } else {
        v.z += amount;
    }
}

constexpr double lens_index = 1.49;

// One chord: midpoint quadrature of the source densities under the running
// transmittance, each step exact for constant σ and S over it, and shortened
// near the wires so that a wire a pixel wide is never stepped over.
Vec3 integrate_chord(const Scene& scene, Vec3 origin, Vec3 direction, double chord, Radiance& out) {
    Vec3 transmittance{1.0, 1.0, 1.0};
    double t = 0.0;
    for (int step = 0; step < 800 && t < chord; ++step) {
        const double clearance = wire_clearance(scene, add(origin, scale(direction, t)));
        const double dt = std::min(chord - t, std::clamp(clearance * 0.5, 0.0006, 0.008));
        const MediumPoint m = medium_at(scene, add(origin, scale(direction, t + 0.5 * dt)));
        t += dt;
        const Vec3 step_t = exp_neg(m.extinction, dt);
        for (int c = 0; c < 3; ++c) {
            const double sigma = channel(m.extinction, c);
            const double factor = sigma > 1e-9 ? (1.0 - channel(step_t, c)) / sigma : dt;
            const double w = channel(transmittance, c) * factor;
            add_channel(out.base, c, w * channel(m.room, c));
            for (int i = 0; i < scene.lamp_count; ++i) {
                add_channel(out.glow[static_cast<std::size_t>(i)], c, w * channel(m.glow[static_cast<std::size_t>(i)], c));
            }
        }
        transmittance = mul(transmittance, step_t);
        if (max_component(transmittance) < 1e-4) {
            break;
        }
    }
    return transmittance;
}

Radiance outside_radiance(const Scene& scene, Vec3 origin, Vec3 direction, int depth, bool gather);

// What covers the lens's outer face at a point with outward normal n.
struct Coat final {
    bool painted{};       // black paint: no light passes
    double ink{};         // share of the footprint under legend ink
    double ink_albedo{};  // the ink's reflectance (opaque inks)
    bool ink_glows{};     // dead front: the ink is a diffuser the lamps shine through
};

Coat coat_at(const Scene& scene, Vec3 p, Vec3 n) {
    Coat coat{};
    const bool face = n.z > 0.9;
    if (scene.black_back && n.z < -0.7) {
        coat.painted = true;
        return coat;
    }
    switch (scene.legend_style) {
    case Legend::dead_front:
        coat.painted = true;
        coat.ink = face ? legend_coverage(scene, p.x, p.y) : 0.0;
        coat.ink_albedo = 0.72;
        coat.ink_glows = true;
        return coat;
    case Legend::black_ink:
    case Legend::white_ink:
    case Legend::grey_ink:
        coat.painted = scene.sidewall_blocked && !face && n.z > -0.7;
        coat.ink = face ? legend_coverage(scene, p.x, p.y) : 0.0;
        coat.ink_albedo = scene.legend_style == Legend::white_ink ? 0.80 : (scene.legend_style == Legend::grey_ink ? 0.22 : 0.03);
        return coat;
    case Legend::lit_ink:
        coat.painted = scene.sidewall_blocked && !face && n.z > -0.7;
        coat.ink = face ? legend_coverage(scene, p.x, p.y) : 0.0;
        coat.ink_albedo = 0.72;
        coat.ink_glows = true;
        return coat;
    case Legend::none:
    default:
        coat.painted = scene.sidewall_blocked && !face && n.z > -0.7;
        return coat;
    }
}

// Irradiance from a lamp's coil at a point of a surface whose outward normal
// is `outward`, per unit lamp power: the coil seen as a cylinder of radiance
// filament_radiance, summed over eight lengths, each its projected area times
// the cosine at the surface over the squared distance. Lamps behind the
// divider do not reach the other half.
double lamp_irradiance(const Scene& scene, const Lamp& lamp, Vec3 p, Vec3 outward) {
    if (scene.divider && (p.y > 0.0) != (lamp.a.y > 0.0)) {
        return 0.0;
    }
    const Vec3 a = sub(lamp.a, Vec3{0.0, 0.0, scene.press});
    const Vec3 b = sub(lamp.b, Vec3{0.0, 0.0, scene.press});
    const int pieces = 8;
    const double piece = length(sub(b, a)) / pieces;
    double e = 0.0;
    for (int j = 0; j < pieces; ++j) {
        const Vec3 q = add(a, scale(sub(b, a), (j + 0.5) / pieces));
        const Vec3 to = sub(q, p);
        const double d2 = dot(to, to);
        const double cos_surface = -dot(to, outward) / std::sqrt(d2);  // the lamp is inside, opposite the outward normal
        if (cos_surface > 0.0) {
            e += 2.0 * lamp.coil_radius * piece * cos_surface / d2;
        }
    }
    return scene.filament_radiance * e;
}

double room_light = 0.0;  // set once in main: room_on_upward_surface()

// The legs inside a lens, from a point inside it: each chord contributes its
// source and retains T; each exit returns a port to the outside and the
// reflected remainder continues (the participating child law).
Radiance internal_response(const Scene& scene, Vec3 p, Vec3 d, Vec3 weight, int depth, bool gather) {
    Radiance out{};
    for (int leg = 0; leg < 10 && max_component(weight) > 2e-3; ++leg) {
        const double t_exit = march_inside(scene, p, d);
        Radiance chord{};
        const Vec3 retained = integrate_chord(scene, p, d, t_exit, chord);
        out = radiance_add(out, radiance_scale(chord, weight));
        weight = mul(weight, retained);
        p = add(p, scale(d, t_exit));
        const Vec3 outward = lens_normal(scene, p);
        const double c = std::max(0.0, dot(d, outward));
        const Coat coat = coat_at(scene, p, outward);
        if (scene.grade.rear_diffuser > 0.0 && outward.z < -0.7) {
            // a frosted back: it re-emits the lamps' light diffusely and
            // takes the leg no further
            for (int i = 0; i < scene.lamp_count; ++i) {
                const double l = scene.grade.rear_diffuser * lamp_irradiance(scene, scene.lamps[static_cast<std::size_t>(i)], p, outward) / pi;
                out.glow[static_cast<std::size_t>(i)] = add(out.glow[static_cast<std::size_t>(i)], scale(weight, l));
            }
            break;
        }
        if (scene.grade.rear_reflector > 0.0 && outward.z < -0.7) {
            weight = scale(weight, scene.grade.rear_reflector);
        } else if (coat.painted) {
            // black paint takes almost everything; dead-front ink returns a
            // share diffusely, here kept along the mirror direction
            weight = scale(weight, 0.03 * (1.0 - coat.ink) + (coat.ink_glows ? 0.35 : coat.ink_albedo) * coat.ink);
        } else {
            // opaque ink blocks its share and reflects by its albedo
            const double f_out = fresnel_dielectric(c, lens_index, 1.0);
            const double open = 1.0 - coat.ink;
            if (f_out < 1.0 && depth > 0 && open > 0.0) {
                const double k = 1.0 - lens_index * lens_index * (1.0 - c * c);
                const Vec3 t = normalized(add(scale(d, lens_index), scale(outward, std::sqrt(std::max(0.0, k)) - lens_index * c)));
                const Radiance beyond = outside_radiance(scene, add(p, scale(outward, 4e-4)), t, depth - 1, gather);
                out = radiance_add(out, radiance_scale(beyond, scale(weight, (1.0 - f_out) * open)));
            }
            weight = scale(weight, f_out * open + coat.ink_albedo * coat.ink);
        }
        d = reflected(d, outward);
        p = sub(p, scale(outward, 4e-4));
    }
    return out;
}

constexpr int diffuser_gather_count = 12;

// The legend's white ink is a thin diffuser: it passes on the light arriving
// from inside the lens, averaged over the hemisphere below it.
Radiance through_ink(const Scene& scene, Vec3 p, Vec3 n, int depth) {
    Radiance sum{};
    const Vec3 inward = scale(n, -1.0);
    const double rotation = hashed_rotation(p);
    const Vec3 start = sub(p, scale(n, 4e-4));
    for (int i = 0; i < diffuser_gather_count; ++i) {
        const Vec3 d = spiral_direction(i, diffuser_gather_count, rotation, inward);
        sum = radiance_add(sum, internal_response(scene, start, d, Vec3{1.0, 1.0, 1.0}, depth, false));
    }
    const double k = 1.0 / diffuser_gather_count;
    return radiance_scale(sum, Vec3{k, k, k});
}


Radiance lens_response(const Scene& scene, Vec3 position, Vec3 direction, int depth, bool gather) {
    Radiance out{};
    const Vec3 n = lens_normal(scene, position);
    const double cos_i = std::max(0.0, -dot(direction, n));
    const double f_in = fresnel_dielectric(cos_i, 1.0, lens_index);
    if (depth > 0) {
        const Radiance mirror = outside_radiance(scene, add(position, scale(n, 4e-4)), reflected(direction, n), depth - 1, gather);
        out = radiance_add(out, radiance_scale(mirror, Vec3{f_in, f_in, f_in}));
    } else {
        const double e = f_in * environment(reflected(direction, n));
        out.base = add(out.base, Vec3{e, e, e});
    }
    const Coat coat = coat_at(scene, position, n);
    if (coat.painted) {
        // Gloss black paint; on a dead front, legend in white diffuser ink
        // which the room lights from outside and the lamps from inside.
        const double ink = coat.ink_glows ? coat.ink : 0.0;
        const double under = 1.0 - f_in;
        const double paint = 0.025 * room_light * (1.0 - ink) * under;
        out.base = add(out.base, Vec3{paint, paint, paint});
        if (ink > 0.0) {
            const double lit = 0.72 * room_light * ink * under;
            out.base = add(out.base, Vec3{lit, lit, lit});
            if (depth > 0) {
                const Radiance inner = through_ink(scene, position, n, depth - 1);
                out = radiance_add(out, radiance_scale(inner, Vec3{0.6 * ink * under, 0.6 * ink * under, 0.6 * ink * under}));
            }
        }
        return out;
    }
    // diffuser ink on an open face: lit by the room from outside and by the
    // lamps from inside, through it
    if (coat.ink_glows && coat.ink > 0.0) {
        const double under = 1.0 - f_in;
        const double lit = 0.72 * room_light * coat.ink * under;
        out.base = add(out.base, Vec3{lit, lit, lit});
        if (depth > 0) {
            const Radiance inner = through_ink(scene, position, n, depth - 1);
            const double k = 0.6 * coat.ink * under;
            out = radiance_add(out, radiance_scale(inner, Vec3{k, k, k}));
        }
        // and the lamps' own light falling on the ink from below, passed on
        // diffusely (as a frosted back passes it): the letters glow
        for (int i = 0; i < scene.lamp_count; ++i) {
            const double l = 0.6 * coat.ink * under * lamp_irradiance(scene, scene.lamps[static_cast<std::size_t>(i)], position, n) / pi;
            out.glow[static_cast<std::size_t>(i)] = add(out.glow[static_cast<std::size_t>(i)], Vec3{l, l, l});
        }
    } else if (coat.ink > 0.0) {
        // opaque ink on an unpainted face: lit by the room only, and it blocks
        // its share of the view into the lens
        const double lit = coat.ink * coat.ink_albedo * room_light * (1.0 - f_in);
        out.base = add(out.base, Vec3{lit, lit, lit});
    }
    const double open = (1.0 - f_in) * (1.0 - coat.ink);
    if (open <= 0.0) {
        return out;
    }
    const double eta = 1.0 / lens_index;
    const double cos_t2 = 1.0 - eta * eta * (1.0 - cos_i * cos_i);
    const Vec3 d = normalized(add(scale(direction, eta), scale(n, eta * cos_i - std::sqrt(std::max(0.0, cos_t2)))));
    return radiance_add(out, internal_response(scene, sub(position, scale(n, 4e-4)), d, Vec3{open, open, open}, depth, gather));
}

// The panel face gathers 16 directions; the well's interior, which a clear
// lens magnifies, gathers 48.
constexpr int plate_gather_count = 16;
constexpr int well_gather_count = 48;

Radiance plate_response(const Scene& scene, Vec3 position, Vec3 direction, int depth, bool gather) {
    Radiance out{};
    const Vec3 n = plate_normal(scene, position);
    const bool top_face = position.z > -1e-3 && n.z > 0.9;
    const double albedo = top_face ? 0.035 : 0.10;
    if (top_face) {
        // satin coat: the room reflected, softly
        const double coat = fresnel_dielectric(std::max(0.0, -dot(direction, n)), 1.0, 1.5);
        const Vec3 r = reflected(direction, n);
        const double e = coat * 0.5 * (environment(r) + environment(normalized(add(r, Vec3{0.08, 0.05, 0.0}))));
        out.base = add(out.base, Vec3{e, e, e});
    }
    if (!gather || depth <= 0) {
        const double e = albedo * room_light;
        out.base = add(out.base, Vec3{e, e, e});
        return out;
    }
    const double rotation = hashed_rotation(position);
    const Vec3 lifted = add(position, scale(n, 4e-4));
    const int count = top_face ? plate_gather_count : well_gather_count;
    Radiance sum{};
    for (int i = 0; i < count; ++i) {
        sum = radiance_add(sum, outside_radiance(scene, lifted, spiral_direction(i, count, rotation, n), depth - 1, false));
    }
    const double k = albedo / count;
    return radiance_add(out, radiance_scale(sum, Vec3{k, k, k}));
}

Radiance outside_radiance(const Scene& scene, Vec3 origin, Vec3 direction, int depth, bool gather) {
    const Hit hit = march_outside(scene, origin, direction);
    if (hit.surface == Surface::none) {
        const double e = environment(direction);
        Radiance r{};
        r.base = Vec3{e, e, e};
        return r;
    }
    const Vec3 p = add(origin, scale(direction, hit.t));
    if (hit.surface == Surface::lens) {
        return lens_response(scene, p, direction, depth, gather);
    }
    return plate_response(scene, p, direction, depth, gather);
}

// ---- camera and images ----------------------------------------------------------------------

struct Image final {
    int width{};
    int height{};
    int lamps{};
    std::vector<float> base;                         // width × height × 3, linear
    std::array<std::vector<float>, max_lamps> glow;  // per lamp, at unit white power
    std::vector<float> alpha;                        // width × height: the control's coverage
};

// Head-on, as a control is seen on screen, from a little in front.
struct Camera final {
    Vec3 eye{0.0, -0.32, 2.6};
    Vec3 target{0.0, 0.0, 0.08};
    double field_of_view{21.0};  // degrees, vertical
};

struct RowJob final {
    const Scene* scene{};
    int grid{};
    int first_row{};
    int row_step{};
    Image* image{};
};

void render_rows(RowJob job) {
    const Scene& scene = *job.scene;
    Image& image = *job.image;
    Camera camera{};
    camera.field_of_view = scene.field_of_view;
    const Vec3 forward = normalized(sub(camera.target, camera.eye));
    const Vec3 right = normalized(cross(forward, Vec3{0.0, 1.0, 0.0}));
    const Vec3 up = cross(right, forward);
    const double half = std::tan(camera.field_of_view * pi / 360.0);
    const int w = image.width;
    const int h = image.height;
    const int g = job.grid;
    const Vec3 above = scene.control_eye;
    for (int y = job.first_row; y < h; y += job.row_step) {
        for (int x = 0; x < w; ++x) {
            Radiance sum{};
            int covered = 0;
            for (int s = 0; s < g * g; ++s) {
                const double sx = (x + ((s % g) + 0.5) / g) / w * 2.0 - 1.0;
                const double sy = 1.0 - (y + ((s / g) + 0.5) / g) / h * 2.0;
                if (scene.control_only) {
                    const Vec3 direction = normalized(sub(Vec3{sx * scene.frame_half_x, scene.frame_centre_y + sy * scene.frame_half_y, 0.0}, above));
                    const Hit hit = march_outside(scene, above, direction);
                    if (hit.surface == Surface::lens) {
                        sum = radiance_add(sum, lens_response(scene, add(above, scale(direction, hit.t)), direction, 3, false));
                        ++covered;
                    }
                    continue;
                }
                const Vec3 direction = normalized(add(forward, add(scale(right, sx * half), scale(up, sy * half))));
                sum = radiance_add(sum, outside_radiance(scene, camera.eye, direction, 3, true));
                ++covered;
            }
            const double inv = 1.0 / (g * g);
            image.alpha[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)] = static_cast<float>(covered * inv);
            const std::size_t at = (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 3U;
            image.base[at] = static_cast<float>(sum.base.x * inv);
            image.base[at + 1U] = static_cast<float>(sum.base.y * inv);
            image.base[at + 2U] = static_cast<float>(sum.base.z * inv);
            for (int i = 0; i < image.lamps; ++i) {
                const Vec3 v = sum.glow[static_cast<std::size_t>(i)];
                std::vector<float>& out = image.glow[static_cast<std::size_t>(i)];
                out[at] = static_cast<float>(v.x * inv);
                out[at + 1U] = static_cast<float>(v.y * inv);
                out[at + 2U] = static_cast<float>(v.z * inv);
            }
        }
    }
}

Image render(const Scene& scene, int width, int height, int grid) {
    Image image{};
    image.width = width;
    image.height = height;
    image.lamps = scene.lamp_count;
    const std::size_t pixels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    const std::size_t count = pixels * 3U;
    image.base.assign(count, 0.0F);
    image.alpha.assign(pixels, 0.0F);
    for (int i = 0; i < image.lamps; ++i) {
        image.glow[static_cast<std::size_t>(i)].assign(count, 0.0F);
    }
    const unsigned threads = std::max(1U, std::thread::hardware_concurrency());
    std::vector<std::thread> workers;
    workers.reserve(threads);
    for (unsigned i = 0; i < threads; ++i) {
        workers.emplace_back(render_rows, RowJob{&scene, grid, static_cast<int>(i), static_cast<int>(threads), &image});
    }
    for (std::thread& worker : workers) {
        worker.join();
    }
    return image;
}

bool write_buffers(const std::string& path, const Image& image) {
    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) {
        return false;
    }
    // width, height, lamp count, then base, each lamp, and the coverage
    // plane; colour planes are premultiplied by coverage
    const std::int32_t header[3] = {image.width, image.height, image.lamps};
    bool ok = std::fwrite(header, sizeof(header), 1U, file) == 1U;
    ok = ok && std::fwrite(image.base.data(), sizeof(float), image.base.size(), file) == image.base.size();
    for (int i = 0; i < image.lamps; ++i) {
        const std::vector<float>& glow = image.glow[static_cast<std::size_t>(i)];
        ok = ok && std::fwrite(glow.data(), sizeof(float), glow.size(), file) == glow.size();
    }
    ok = ok && std::fwrite(image.alpha.data(), sizeof(float), image.alpha.size(), file) == image.alpha.size();
    return std::fclose(file) == 0 && ok;
}

std::uint8_t encode(double v) {
    const double c = std::clamp(v, 0.0, 1.0);
    const double s = c <= 0.0031308 ? c * 12.92 : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
    return static_cast<std::uint8_t>(std::lround(s * 255.0));
}

double tone(double v) { return 1.0 - std::exp(-v * 1.3); }

// A 24-bit bottom-up BMP of tone(base + Σ power × colour_i × glow_i).
bool write_preview(const std::string& path, const Image& image, const std::array<Vec3, max_lamps>& colours, double power) {
    const int w = image.width;
    const int h = image.height;
    const int row = (w * 3 + 3) & ~3;
    const std::uint32_t data_size = static_cast<std::uint32_t>(row * h);
    std::array<std::uint8_t, 54> head{};
    head[0] = 'B';
    head[1] = 'M';
    const std::uint32_t file_size = 54U + data_size;
    for (int i = 0; i < 4; ++i) {
        head[static_cast<std::size_t>(2 + i)] = static_cast<std::uint8_t>(file_size >> (8 * i));
        head[static_cast<std::size_t>(18 + i)] = static_cast<std::uint8_t>(static_cast<std::uint32_t>(w) >> (8 * i));
        head[static_cast<std::size_t>(22 + i)] = static_cast<std::uint8_t>(static_cast<std::uint32_t>(h) >> (8 * i));
        head[static_cast<std::size_t>(34 + i)] = static_cast<std::uint8_t>(data_size >> (8 * i));
    }
    head[10] = 54;
    head[14] = 40;
    head[26] = 1;
    head[28] = 24;
    std::vector<std::uint8_t> pixels(data_size, 0U);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t at = (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 3U;
            std::array<double, 3> v = {image.base[at], image.base[at + 1U], image.base[at + 2U]};
            for (int i = 0; i < image.lamps; ++i) {
                const std::vector<float>& glow = image.glow[static_cast<std::size_t>(i)];
                const Vec3 c = colours[static_cast<std::size_t>(i)];
                v[0] += power * c.x * glow[at];
                v[1] += power * c.y * glow[at + 1U];
                v[2] += power * c.z * glow[at + 2U];
            }
            const std::size_t out = static_cast<std::size_t>(h - 1 - y) * static_cast<std::size_t>(row) + static_cast<std::size_t>(x) * 3U;
            const double a = image.alpha[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)];
            // unpremultiply, tone, and lay over a dark panel grey
            const double behind = 0.018;
            const double inv_a = a > 1e-6 ? 1.0 / a : 0.0;
            pixels[out] = encode(tone(v[2] * inv_a) * a + behind * (1.0 - a));
            pixels[out + 1U] = encode(tone(v[1] * inv_a) * a + behind * (1.0 - a));
            pixels[out + 2U] = encode(tone(v[0] * inv_a) * a + behind * (1.0 - a));
        }
    }
    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) {
        return false;
    }
    bool ok = std::fwrite(head.data(), 1U, head.size(), file) == head.size();
    ok = ok && std::fwrite(pixels.data(), 1U, pixels.size(), file) == pixels.size();
    return std::fclose(file) == 0 && ok;
}

// ---- the three families ------------------------------------------------------------------------

Scene make_keycap() {
    Scene scene{};
    scene.family = Family::keycap;
    scene.lamps[0] = Lamp{Vec3{-0.10, 0.0, 0.12}, Vec3{0.10, 0.0, 0.12}};
    scene.lamp_count = 1;
    return scene;
}

Scene make_jewel() {
    Scene scene{};
    scene.family = Family::jewel;
    cut_jewel(scene);
    scene.round_well = true;
    scene.well_half = 0.32;
    scene.lamps[0] = Lamp{Vec3{-0.06, 0.0, 0.03}, Vec3{0.06, 0.0, 0.03}, 0.012, 0.010, 0.0030, 0.0040};
    scene.lamp_count = 1;
    return scene;
}

// A split caption, as on an annunciator panel: a rectangular lens, two lamps
// behind a divider (one per half), and a two-line legend.
//
// `sidewall` is the lens's height above the panel: 0.12 is a flush caption,
// 0.4 or so a keycap. Taller lenses taper a little more.
Scene make_annunciator(LegendWriter& writer, const std::string& upper, const std::string& lower, double sidewall) {
    Scene scene{};
    scene.family = Family::annunciator;
    scene.aspect = 0.62;
    scene.half_base = 0.34;
    scene.half_top = 0.315 - 0.12 * std::max(0.0, sidewall - 0.12);
    scene.top = sidewall;
    scene.bottom = -0.07;
    scene.chamfer = 0.022;
    scene.corner = 0.04;
    scene.softness = 0.014;
    scene.dish = 0.0;
    scene.well_half = 0.365;
    scene.well_radius = 0.03;
    scene.lamps[0] = Lamp{Vec3{-0.09, 0.075, 0.02}, Vec3{0.09, 0.075, 0.02}};
    scene.lamps[1] = Lamp{Vec3{-0.09, -0.075, 0.02}, Vec3{0.09, -0.075, 0.02}};
    scene.lamp_count = 2;
    // one line of legend: centred, both lamps lighting one field, no divider
    const bool single = lower.empty();
    scene.divider = !single;
    scene.control_only = true;
    // the camera is 2.6 above the panel: a taller top projects larger
    const double reach = std::max(scene.half_base, scene.half_top * 2.6 / (2.6 - scene.top));
    scene.frame_half_x = reach + 0.004;
    scene.frame_half_y = reach * scene.aspect + 0.004;
    if (single) {
        set_line(scene, writer, upper, -0.04, 0.08);
    } else {
        set_line(scene, writer, upper, 0.028, 0.06);
        set_line(scene, writer, lower, -0.088, 0.06);
    }
    return scene;
}

// A small jewel indicator, as on old panels: the faceted jewel sits under the
// panel and only its crown shows through a round hole smaller than the jewel,
// framed close so that it renders small. The lamp is behind the crown.
Scene make_indicator() {
    Scene scene = make_jewel();
    scene.well_half = 0.19;
    scene.field_of_view = 19.0;
    return scene;
}

struct Style final {
    const char* name{};
    Grade grade{};
    Legend legend{Legend::none};
    bool blocked{};
    const char* upper{""};
    const char* lower{""};
    bool coil_at_back{};  // long horizontal coils just above a frosted back
    double sidewall{0.12};  // the lens's height above the panel
    bool one_coil{};        // one large, sparse coil across the middle (the dialog's OK)
    bool deep_lamps{};      // lamps at the bottom of an opaque shell, seen only as their diffused light
    double relief{};        // the top's orange-peel slope
    double grain{};         // the top's grain slope
    double height_share{};  // a dialog key's height over its short side (shape_key); 0: use sidewall
};

// Long, open coils lying just above the caption's back face, one per half.
void set_back_coils(Scene& scene) {
    const double z = scene.bottom + 0.022;
    scene.lamps[0] = Lamp{Vec3{-0.11, 0.075, z}, Vec3{0.11, 0.075, z}, 0.012, 0.019, 0.0028, 0.0035};
    scene.lamps[1] = Lamp{Vec3{-0.11, -0.075, z}, Vec3{0.11, -0.075, z}, 0.012, 0.019, 0.0028, 0.0035};
}

// The dialog keys' proportions. A key's height above the panel is given as a
// share of its short side (the owner's judgements on this 0.68 × 0.42 key:
// 0.28, a flush caption, too flat; 0.95, a keycap, too tall; 0.57
// right). Its sides lean in more as it gets taller, from nearly
// upright on a flush caption (3°) to about a sculpted keycap's lean (20° at
// a full short side; a DSA keycap's sides lean about 20°: 18.2 mm base,
// 12.7 mm top, 7.6 mm tall):
//   lean = 3° + 17° × clamp((height share − 0.25) / 0.75, 0, 1).
void shape_key(Scene& scene, double height_share) {
    const double short_side = 2.0 * scene.half_base * scene.aspect;
    scene.top = height_share * short_side;
    const double t = std::clamp((height_share - 0.25) / 0.75, 0.0, 1.0);
    const double lean = (3.0 + 17.0 * t) * pi / 180.0;
    scene.half_top = scene.half_base - std::tan(lean) * (scene.top - scene.bottom);
}

// Tilts the control camera toward the viewer by `degrees` and fits the frame
// to the key's projection: the base and top corners carried along the rays
// from the eye to z = 0, with a margin of 0.004.
void tilt_control_camera(Scene& scene, double degrees) {
    const double height = 2.6;
    scene.control_eye = Vec3{0.0, -height * std::tan(degrees * pi / 180.0), height};
    const Vec3 eye = scene.control_eye;
    double low_x = 1e9;
    double high_x = -1e9;
    double low_y = 1e9;
    double high_y = -1e9;
    const std::array<double, 2> levels = {scene.bottom, scene.top};
    const std::array<double, 2> halves = {scene.half_base, std::max(scene.half_base, scene.half_top)};
    for (std::size_t level = 0; level < levels.size(); ++level) {
        for (int corner = 0; corner < 4; ++corner) {
            const double x = (corner & 1) != 0 ? halves[level] : -halves[level];
            const double y = (corner & 2) != 0 ? halves[level] * scene.aspect : -halves[level] * scene.aspect;
            const double k = eye.z / (eye.z - levels[level]);  // along the ray from the eye, down to z = 0
            const double px = eye.x + (x - eye.x) * k;
            const double py = eye.y + (y - eye.y) * k;
            low_x = std::min(low_x, px);
            high_x = std::max(high_x, px);
            low_y = std::min(low_y, py);
            high_y = std::max(high_y, py);
        }
    }
    scene.frame_half_x = 0.5 * (high_x - low_x) + 0.004;
    scene.frame_half_y = 0.5 * (high_y - low_y) + 0.004;
    scene.frame_centre_y = 0.5 * (high_y + low_y);
}

// One large, sparse coil across a single-line key: few wide turns low in the
// lens, over the black void behind it.
void set_one_coil(Scene& scene) {
    scene.hidden_coil = true;
    const double z = scene.bottom + 0.022;
    scene.lamps[0] = Lamp{Vec3{-0.17, 0.0, z}, Vec3{0.17, 0.0, z}, 0.034, 0.062, 0.0034, 0.0040};
    scene.lamp_count = 1;
}

// The legend masks' check (--legend-test): a line drawn in Portsmouth
// Mono is inked, exactly as tall as asked, centred on x = 0, sits on its
// baseline, and covers nothing beside it; an empty line draws nothing.
int legend_test(LegendWriter& writer) {
    int failures = 0;
    LegendMask mask{};
    const bool drawn = writer.draw("CANCEL", -0.04, 0.08, mask);
    if (!drawn || mask.rows < 3U || mask.columns < 3U) {
        std::fprintf(stderr, "FAIL: CANCEL draws no legend\n");
        return 1;
    }
    const double ink_height = static_cast<double>(mask.rows - 2U) * mask.pitch;
    if (std::abs(ink_height - 0.08) > 1e-9) {
        std::fprintf(stderr, "FAIL: the ink is %g tall, not 0.08\n", ink_height);
        ++failures;
    }
    const double centre = mask.left + 0.5 * static_cast<double>(mask.columns) * mask.pitch;
    if (std::abs(centre) > 1e-9) {
        std::fprintf(stderr, "FAIL: the line is centred on %g, not 0\n", centre);
        ++failures;
    }
    const double foot = mask.top - static_cast<double>(mask.rows - 1U) * mask.pitch;
    if (std::abs(foot - (-0.04)) > 1e-9) {
        std::fprintf(stderr, "FAIL: the ink's foot is at %g, not the baseline\n", foot);
        ++failures;
    }
    double inked = 0.0;
    for (const float value : mask.coverage) inked = std::max(inked, static_cast<double>(value));
    if (!(inked > 0.9)) {
        std::fprintf(stderr, "FAIL: the legend's ink is faint (%g)\n", inked);
        ++failures;
    }
    Scene scene{};
    scene.legends.push_back(mask);
    if (legend_coverage(scene, mask.left - 0.01, -0.0) != 0.0 || legend_coverage(scene, 0.0, 0.2) != 0.0) {
        std::fprintf(stderr, "FAIL: the legend covers points beside it\n");
        ++failures;
    }
    LegendMask empty{};
    if (writer.draw("", 0.0, 0.08, empty)) {
        std::fprintf(stderr, "FAIL: an empty line draws\n");
        ++failures;
    }
    if (failures == 0) std::printf("lamp_keys legend tests passed\n");
    return failures == 0 ? 0 : 1;
}

}  // namespace lamps

int main(int argc, char** argv) {
    lamps::LegendWriter writer(GUI_FORMS_LEGEND_FONT);
    if (!writer.ready()) {
        std::fprintf(stderr, "lamps: cannot load Portsmouth Mono from %s\n", GUI_FORMS_LEGEND_FONT);
        return 1;
    }
    if (argc == 2 && std::string(argv[1]) == "--legend-test") return lamps::legend_test(writer);
    if (argc < 4) {
        std::fprintf(stderr, "usage: gui_forms_lamp_keys <output directory> <size> <keycap|annunciator|dialog|indicator>\n"
                             "       gui_forms_lamp_keys --legend-test\n");
        return 2;
    }
    const std::string directory = argv[1];
    const int size = std::clamp(std::atoi(argv[2]), 16, 2048);
    const std::string family = argv[3];
    lamps::room_light = lamps::room_on_upward_surface();
    // name, σ per channel, albedo, glow reach, room source, silvered back, glow gain
    const lamps::Grade clear{"clear", lamps::Vec3{0.05, 0.05, 0.05}, 0.9, 0.03, 0.02, 0.0, 1.0};
    const lamps::Grade clear_frosted_back{"clear", lamps::Vec3{0.05, 0.05, 0.05}, 0.9, 0.03, 0.02, 0.0, 1.0, 0.85};
    const lamps::Grade backed{"backed", lamps::Vec3{0.05, 0.05, 0.05}, 0.9, 0.04, 0.02, 0.85, 2.0};
    const lamps::Grade opaque{"opaque", lamps::Vec3{22.0, 22.0, 22.0}, 0.97, 0.18, 0.10, 0.0, 6.0};
    std::vector<lamps::Style> styles;
    if (family == "annunciator") {
        styles = {
            lamps::Style{"opaque_black_blocked", opaque, lamps::Legend::black_ink, true, "HYD", "PRESS"},
            lamps::Style{"opaque_white_open", opaque, lamps::Legend::white_ink, false, "AC", "FAIL"},
            lamps::Style{"clear_black_blocked", clear_frosted_back, lamps::Legend::black_ink, true, "MASTER", "WARNING", true},
            lamps::Style{"clear_white_open", clear_frosted_back, lamps::Legend::white_ink, false, "ANTI-ICE", "ON", true},
            lamps::Style{"opaque_deadfront", opaque, lamps::Legend::dead_front, true, "MASTER", "CAUTION"},
            lamps::Style{"opaque_black_blocked_mid", opaque, lamps::Legend::black_ink, true, "HYD", "PRESS", false, 0.24},
            lamps::Style{"opaque_black_blocked_keycap", opaque, lamps::Legend::black_ink, true, "HYD", "PRESS", false, 0.40},
            lamps::Style{"opaque_black_open_keycap", opaque, lamps::Legend::black_ink, false, "AC", "FAIL", false, 0.40},
            lamps::Style{"clear_black_blocked_keycap", clear_frosted_back, lamps::Legend::black_ink, true, "MASTER", "WARNING", true, 0.40},
        };
    } else if (family == "dialog") {
        // The colour is the lamp's (applied at composite): CANCEL is white
        // opaque plastic, OK clear acrylic with a faint haze (extinction 0.6,
        // its glow reaching three times as far as the panel captions') over a
        // black void, with no frosted back. Both tops carry the moulded relief.
        // Height 0.24, the panel's mid captions'.
        // OK is lit as the ANTI-ICE caption (approved): clear, its lamp just
        // above a frosted back that re-emits only the lamp's light, so it is
        // dark when off and brightest round the lamp when on. CANCEL's white
        // plastic adds a short-reach brightening round its lamps.
        lamps::Grade white_near = opaque;
        white_near.near_reach = 0.06;
        white_near.near_gain = 4.0;
        styles = {
            lamps::Style{"cancel", white_near, lamps::Legend::grey_ink, false, "CANCEL", "", false, 0.24, false, true, 0.05, 0.05, 0.57},
            lamps::Style{"ok", clear_frosted_back, lamps::Legend::white_ink, false, "OK", "", false, 0.24, true, false, 0.05, 0.05, 0.57},
        };
    } else if (family == "indicator") {
        styles = {
            lamps::Style{"backed", backed, lamps::Legend::none, false, "", ""},
            lamps::Style{"clear", clear, lamps::Legend::none, false, "", ""},
        };
    } else if (family == "keycap") {
        styles = {
            lamps::Style{"clear", clear, lamps::Legend::none, false, "", ""},
            lamps::Style{"opaque", opaque, lamps::Legend::none, false, "", ""},
        };
    } else {
        std::fprintf(stderr, "unknown family %s\n", family.c_str());
        return 2;
    }
    const std::array<lamps::Vec3, lamps::max_lamps> colours = {lamps::Vec3{1.0, 0.55, 0.10}, lamps::Vec3{1.0, 0.16, 0.05}};
    for (const lamps::Style& style : styles) {
        lamps::Scene shape = (family == "annunciator" || family == "dialog") ? lamps::make_annunciator(writer, style.upper, style.lower, style.sidewall)
                             : family == "indicator" ? lamps::make_indicator()
                                                     : lamps::make_keycap();
        shape.grade = style.grade;
        shape.legend_style = style.legend;
        shape.sidewall_blocked = style.blocked;
        if (style.coil_at_back) {
            lamps::set_back_coils(shape);
        }
        if (style.one_coil) {
            lamps::set_one_coil(shape);
        }
        if (style.deep_lamps) {
            shape.hidden_coil = true;
            for (lamps::Lamp& lamp : shape.lamps) {
                lamp.a.z = shape.bottom + 0.012;
                lamp.b.z = shape.bottom + 0.012;
            }
        }
        shape.relief = style.relief;
        shape.grain = style.grain;
        if (family == "dialog") {
            shape.aspect = 0.36;  // as wide as a program dialog's buttons (about 2.8 : 1)
        }
        if (style.height_share > 0.0) {
            lamps::shape_key(shape, style.height_share);
        }
        if (family == "dialog") {
            lamps::tilt_control_camera(shape, 16.0);
        }
        for (int pressed = 0; pressed < 2; ++pressed) {
            lamps::Scene scene = shape;
            scene.press = pressed != 0 ? (family == "indicator" ? 0.0 : 0.04) : 0.0;
            const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
            int width = size;
            int height = size;
            if (scene.control_only) {
                height = std::max(1, static_cast<int>(std::lround(size * scene.frame_half_y / scene.frame_half_x)));
            }
            const lamps::Image image = lamps::render(scene, width, height, 2);
            const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            const std::string stem = directory + "/" + family + "_" + style.name + (pressed != 0 ? "_down" : "_up");
            const bool written = lamps::write_buffers(stem + ".f32", image) &&
                                 lamps::write_preview(stem + "_off.bmp", image, colours, 0.0) &&
                                 lamps::write_preview(stem + "_on.bmp", image, colours, 1.0);
            std::printf("%-12s %-22s %-4s %dx%d  %.2f s%s\n", family.c_str(), style.name, pressed != 0 ? "down" : "up", width, height,
                        seconds, written ? "" : "  (write failed)");
            if (!written) {
                return 1;
            }
        }
    }
    return 0;
}
