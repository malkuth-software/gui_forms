// Procedural materials (T2 plan, E10): felt, wood grain, paper fibre,
// pinstripe, brushed metal and the fresco with blooms.
//
// - Each pattern is generated per display scale from its parameters alone,
//   in integer arithmetic: the reference images below are FNV-1a hashes of
//   each pattern's bytes at scales 1, 1.25, 1.5 and 2, checked by every CI
//   platform. GUI_FORMS_ENGINE_IDENTITY_PRINT=1 prints the observed table
//   instead of checking it.
// - A tile repeats seamlessly, and each scale's tile is generated, not
//   resampled from another's.
// - The engine never generates a pattern inside a paint: a part whose
//   pattern is not ready draws its fill's ground; the pattern is made in an
//   idle frame, and the part is repainted with it. A scale change generates
//   the new scale's pattern.
// - A pattern whose generation would exceed the bake budget, or whose bytes
//   would crowd the shared cache, refuses the material: the built-in recipe
//   draws, and the refusal is counted.
//
// `--measure` prints, instead of testing, what each generator's effort
// weight (procedural_pattern.hpp) is chosen from: the median of nine
// generations of a 256 × 256 pattern at scale 1, in nanoseconds per device
// pixel, whose ratio to a solid layer's (gui_forms_engine_material_tests
// --measure) times the solid layer's weight suggests the weight. It never
// decides acceptance.

#include "gui_forms/control.hpp"
#include "gui_forms/controls/panel/panel.hpp"
#include "gui_forms/engine.hpp"
#include "gui_forms/theme.hpp"
#include "gui_forms/window.hpp"

#include "../src/core/engine/primitives/material/material_bake_policy.hpp"
#include "../src/core/engine/primitives/procedural/procedural_pattern.hpp"
#include "support/rgba_order.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "gui_forms_engine_procedural_tests: " << message << '\n';
    std::exit(1);
}

bool print_mode() {
    const char* const value = std::getenv("GUI_FORMS_ENGINE_IDENTITY_PRINT");
    const bool printing = value != nullptr && std::strcmp(value, "1") == 0;
    return printing;
}

// FNV-1a over a raster's live bytes, row by row.
std::uint64_t pattern_hash(const PartRaster& raster) {
    std::uint64_t hash = 14695981039346656037ULL;
    const std::span<const std::uint8_t> pixels = raster.pixels();
    const std::size_t live = static_cast<std::size_t>(raster.extent().width) * 4U;
    for (std::uint32_t row = 0U; row < raster.extent().height; ++row) {
        const std::size_t start = static_cast<std::size_t>(row) * raster.row_bytes();
        for (const std::uint8_t byte : gui_forms::test_support::rgba_order(pixels.subspan(start, live))) {
            hash = hash ^ byte;
            hash = hash * 1099511628211ULL;
        }
    }
    return hash;
}

std::array<std::uint8_t, 4> pixel_of(const PartRaster& raster, std::uint32_t column, std::uint32_t row) {
    const std::size_t at = static_cast<std::size_t>(row) * raster.row_bytes() + static_cast<std::size_t>(column) * 4U;
    const std::span<const std::uint8_t> pixels = raster.pixels();
    // The raster is BGRA8; the value is red, green, blue, alpha.
    const std::array<std::uint8_t, 4> value{pixels[at + 2U], pixels[at + 1U], pixels[at], pixels[at + 3U]};
    return value;
}

// -- the reference patterns --------------------------------------------------------

ProceduralFill felt() {
    ProceduralFill fill{};
    fill.pattern = ProceduralPattern::felt;
    fill.seed = 0x0E10F001U;
    fill.tile = 32.0;
    fill.ground = Color::rgba(128, 128, 128);
    fill.figure = Color::rgba(96, 96, 104);
    return fill;
}

ProceduralFill wood_grain() {
    ProceduralFill fill{};
    fill.pattern = ProceduralPattern::wood_grain;
    fill.seed = 0x0E10F002U;
    fill.tile = 32.0;
    fill.ground = Color::rgba(196, 150, 98);
    fill.figure = Color::rgba(120, 78, 44);
    return fill;
}

ProceduralFill paper_fibre() {
    ProceduralFill fill{};
    fill.pattern = ProceduralPattern::paper_fibre;
    fill.seed = 0x0E10F003U;
    fill.tile = 32.0;
    fill.ground = Color::rgba(250, 248, 242);
    fill.figure = Color::rgba(214, 208, 196);
    return fill;
}

ProceduralFill pinstripe() {
    ProceduralFill fill{};
    fill.pattern = ProceduralPattern::pinstripe;
    fill.tile = 4.0;
    fill.ground = Color::rgba(236, 236, 240);
    fill.figure = Color::rgba(212, 214, 222);
    return fill;
}

ProceduralFill brushed_metal() {
    ProceduralFill fill{};
    fill.pattern = ProceduralPattern::brushed_metal;
    fill.seed = 0x0E10F005U;
    fill.tile = 32.0;
    fill.ground = Color::rgba(190, 192, 196);
    fill.figure = Color::rgba(150, 152, 158);
    return fill;
}

ProceduralFill fresco() {
    ProceduralFill fill{};
    fill.pattern = ProceduralPattern::fresco;
    fill.seed = 0x0E10F006U;
    fill.ground = Color::rgba(201, 212, 229);
    fill.figure = Color::rgba(231, 237, 246);
    fill.accent = Color::rgba(174, 187, 209);
    fill.bloom = Color::rgba(255, 214, 170, 160);
    return fill;
}

// The fresco's box, 96 × 40 logical units, in device pixels at `scale`.
Rect fresco_box(double scale) {
    const Rect box{0.0, 0.0, 96.0 * scale, 40.0 * scale};
    return box;
}

std::shared_ptr<PartRaster> make_pattern(const ProceduralFill& fill, double scale) {
    const DeviceExtent extent = engine_primitives::procedural_extent(fill, fresco_box(scale), scale);
    std::shared_ptr<PartRaster> made = engine_primitives::make_procedural_pattern(fill, scale, extent);
    require(made != nullptr, "the pattern is made");
    return made;
}

using FillMaker = ProceduralFill (*)();

struct PatternCase final {
    const char* name;
    FillMaker make;
    std::array<std::uint64_t, 4> hashes;
};

constexpr std::array<double, 4> reference_scales{1.0, 1.25, 1.5, 2.0};

const std::array<PatternCase, 6> pattern_cases{{
    {"felt", &felt, {0x7d8aff3733987b7dULL, 0x1787cb491f40a94ULL, 0x77082156d168f47fULL, 0x31c594de8aeb3edfULL}},
    {"wood_grain",
     &wood_grain,
     {0x9ba2734cdefad6e1ULL, 0x8af23f19a80e3a1dULL, 0x24d8a0f27fbf1e85ULL, 0x3311474039127709ULL}},
    {"paper_fibre",
     &paper_fibre,
     {0x3393944c436b1b01ULL, 0x88adb83e52b55cb0ULL, 0xd346775e5749d8caULL, 0x5a9e71c52cd45db0ULL}},
    {"pinstripe", &pinstripe, {0xeaa2d783b1f31e3dULL, 0x5a0550cd7371a6cULL, 0xb63760cfe155fc0dULL, 0xa5b54a796f3f3045ULL}},
    {"brushed_metal",
     &brushed_metal,
     {0x722c261189f54ab1ULL, 0xbcc0b17c9bd4229ULL, 0xf45786d4a05fc3a5ULL, 0xde3a6f7a956d762fULL}},
    {"fresco", &fresco, {0xeea4875060068fb8ULL, 0x1494a839944c22b1ULL, 0x982ad0c2f725f128ULL, 0xa1dbe2d512e88855ULL}},
}};

void reference_patterns() {
    const bool printing = print_mode();
    bool all_match = true;
    for (const PatternCase& reference : pattern_cases) {
        if (printing) std::cout << "    {\"" << reference.name << "\", &" << reference.name << ", {";
        for (std::size_t index = 0U; index < reference_scales.size(); ++index) {
            const std::shared_ptr<PartRaster> made = make_pattern(reference.make(), reference_scales[index]);
            const std::uint64_t hash = pattern_hash(*made);
            if (printing) {
                std::cout << (index == 0U ? "" : ", ") << "0x" << std::hex << hash << std::dec << "ULL";
                continue;
            }
            if (hash == reference.hashes[index]) continue;
            all_match = false;
            std::cerr << reference.name << " at " << reference_scales[index] << " hash 0x" << std::hex << hash
                      << std::dec << '\n';
        }
        if (printing) std::cout << "}},\n";
    }
    require(printing || all_match, "every pattern matches its reference images");
}

// Equal parameters make equal bytes; another seed makes another pattern,
// but for the pinstripe, which reads none.
void patterns_are_seeded() {
    for (const PatternCase& reference : pattern_cases) {
        const ProceduralFill fill = reference.make();
        const std::shared_ptr<PartRaster> first = make_pattern(fill, 1.25);
        const std::shared_ptr<PartRaster> second = make_pattern(fill, 1.25);
        require(pattern_hash(*first) == pattern_hash(*second), "a pattern is made alike each time");
        ProceduralFill reseeded = fill;
        reseeded.seed = fill.seed + 1U;
        const std::shared_ptr<PartRaster> other = make_pattern(reseeded, 1.25);
        const bool differs = pattern_hash(*other) != pattern_hash(*first);
        const bool reads_seed = fill.pattern != ProceduralPattern::pinstripe;
        require(differs == reads_seed, "the seed chooses the noise");
    }
}

// The largest difference between horizontally adjacent pixels, over the
// tile's interior pairs and over its seam (last column, first column).
struct ColumnSteps final {
    int interior{};
    int seam{};
};

ColumnSteps column_steps(const PartRaster& raster) {
    ColumnSteps steps{};
    const std::uint32_t width = raster.extent().width;
    for (std::uint32_t row = 0U; row < raster.extent().height; ++row) {
        for (std::uint32_t column = 0U; column < width; ++column) {
            const std::uint32_t next = column + 1U == width ? 0U : column + 1U;
            const int step = std::abs(static_cast<int>(pixel_of(raster, column, row)[0]) -
                                      static_cast<int>(pixel_of(raster, next, row)[0]));
            if (next == 0U) {
                steps.seam = std::max(steps.seam, step);
            } else {
                steps.interior = std::max(steps.interior, step);
            }
        }
    }
    return steps;
}

// A tile is round(tile × scale) pixels square at each scale, wraps without
// a seam, and each scale is generated: the tile at scale 2 is not the tile
// at scale 1 enlarged.
void tiles_per_scale() {
    const ProceduralFill fill = felt();
    const std::array<std::uint32_t, 4> sides{32U, 40U, 48U, 64U};
    for (std::size_t index = 0U; index < reference_scales.size(); ++index) {
        const std::shared_ptr<PartRaster> made = make_pattern(fill, reference_scales[index]);
        require((*made).extent() == DeviceExtent{sides[index], sides[index]}, "the tile is round(tile x scale) square");
        const ColumnSteps steps = column_steps(*made);
        require(steps.seam <= steps.interior, "the tile wraps without a seam");
    }
    const std::shared_ptr<PartRaster> single = make_pattern(fill, 1.0);
    const std::shared_ptr<PartRaster> doubled = make_pattern(fill, 2.0);
    bool enlarged = true;
    for (std::uint32_t row = 0U; row < 64U && enlarged; ++row) {
        for (std::uint32_t column = 0U; column < 64U; ++column) {
            if (pixel_of(*doubled, column, row) == pixel_of(*single, column / 2U, row / 2U)) continue;
            enlarged = false;
            break;
        }
    }
    require(!enlarged, "scale 2 is generated, not scale 1 enlarged");
    // The pinstripe's line is the keyline width at each scale.
    const std::shared_ptr<PartRaster> stripes = make_pattern(pinstripe(), 1.5);
    require((*stripes).extent() == DeviceExtent{6U, 6U}, "a pinstripe tile is its period");
    require(pixel_of(*stripes, 0U, 1U) == pixel_of(*stripes, 0U, 0U) &&
                !(pixel_of(*stripes, 0U, 2U) == pixel_of(*stripes, 0U, 0U)),
            "two device rows of line at scale 1.5");
}

// -- the engine --------------------------------------------------------------------

class ManualTime final : public MotionTimeSource {
public:
    [[nodiscard]] FrameTime now() const noexcept override { return time; }
    FrameTime time{std::chrono::seconds(1000)};
};

// A painter that keeps the last part raster composited, and draws nothing.
class RasterWatch final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}
    void draw_part_raster(const std::shared_ptr<const PartRaster>& raster, Point, double) override {
        last = raster;
        rasters = rasters + 1U;
    }
    std::shared_ptr<const PartRaster> last{};
    std::size_t rasters{};
};

class Pad final : public Control {
public:
    explicit Pad(StableId stable_id) : Control(std::move(stable_id)) {}
    void on_paint(Painter& painter, Rect) override { paint_part(painter, *this, face()); }
    [[nodiscard]] PartPaint face() const {
        PartPaint part{};
        part.role = PartRole::face;
        part.family = ControlVisualRole::button;
        part.bounds = Rect{20.0, 20.0, size.width, size.height};
        part.key = PartKey{"face", {}, {}};
        return part;
    }
    Size size{40.0, 24.0};
};

ThemeRule material_rule() {
    ThemeRule rule{};
    rule.label = "pad";
    rule.level = CascadeLevel::theme_sheet;
    rule.selector.stable_id = "pad";
    rule.selector.part = "face";
    rule.declarations.push_back(RuleDeclaration::primitive("material"));
    rule.declarations.push_back(RuleDeclaration::choice_value("recipe", "button"));
    return rule;
}

// The professional theme with every button state's material one
// procedural fill. The definition is held on the heap: a ThemeDefinition is
// some 105 KB and is never kept on a test's stack (theme_types.hpp); `new`
// from the returned value builds it there directly.
std::shared_ptr<const Theme> procedural_theme(const ProceduralFill& fill) {
    const std::unique_ptr<ThemeDefinition> held(new ThemeDefinition(windows_professional_theme_definition()));
    ThemeDefinition& definition = *held;
    definition.id = "engine-procedural-test";
    SurfaceMaterial material{};
    material.fills.clear();
    material.fills.push_back(MaterialFillLayer::procedural_pattern(fill));
    ControlRoleRecipes& button = definition.roles[static_cast<std::size_t>(ControlVisualRole::button)];
    for (ControlVisualRecipe& recipe : button.ordinary) recipe.material = material;
    definition.rules.rules.push_back(material_rule());
    const std::shared_ptr<const Theme> theme = Theme::create(definition);
    return theme;
}

struct Fixture final {
    std::shared_ptr<Panel> root;
    std::shared_ptr<Pad> pad;
    std::unique_ptr<Window> window;
    ManualTime time{};
    RasterWatch painter{};
};

void repaint(Fixture& fixture) {
    Window& window = *fixture.window;
    window.perform_layout();
    const DamageRegion damage = window.take_damage();
    if (!damage.empty()) window.paint(fixture.painter, damage.bounds());
}

// One idle frame of 16 ms of the test's time, then a paint of what it
// damaged.
void run_frame(Fixture& fixture) {
    fixture.time.time += std::chrono::milliseconds(16);
    static_cast<void>((*fixture.window).poll_frame_schedule(fixture.time.time));
    repaint(fixture);
}

std::unique_ptr<Fixture> make_fixture(const ProceduralFill& fill, Size size, double scale) {
    std::unique_ptr<Fixture> fixture = std::make_unique<Fixture>();
    Fixture& made = *fixture;
    made.root = make_control<Panel>(StableId("root"));
    made.pad = make_control<Pad>(StableId("pad"));
    (*made.pad).size = size;
    (*made.pad).set_requested_bounds(Rect{0.0, 0.0, size.width + 60.0, size.height + 60.0});
    (*made.root).add_child(made.pad);
    made.window = std::make_unique<Window>(made.root, Size{size.width + 100.0, size.height + 100.0});
    Window& window = *made.window;
    window.motion_clock().set_time_source(&made.time);
    window.set_scale(scale);
    window.set_theme(procedural_theme(fill));
    require(window.theme().engine().rules().diagnostics().empty(), "the procedural theme compiles cleanly");
    window.perform_layout();
    window.paint(made.painter);
    static_cast<void>(window.take_damage());
    return fixture;
}

// Whether the face of the last raster (the box starts 8 × scale pixels in)
// shows `pattern` tile pixel for pixel over its first `count` pixels.
bool face_shows(const RasterWatch& painter, const PartRaster& pattern, double scale, std::uint32_t count) {
    if (!painter.last) return false;
    const std::uint32_t inset = static_cast<std::uint32_t>(8.0 * scale);
    const DeviceExtent tile = pattern.extent();
    for (std::uint32_t row = 0U; row < count; ++row) {
        for (std::uint32_t column = 0U; column < count; ++column) {
            const std::array<std::uint8_t, 4> shown = pixel_of(*painter.last, inset + column, inset + row);
            const std::array<std::uint8_t, 4> made = pixel_of(pattern, column % tile.width, row % tile.height);
            if (!(shown == made)) return false;
        }
    }
    return true;
}

bool face_is_ground(const RasterWatch& painter, Color ground, double scale) {
    if (!painter.last) return false;
    const std::uint32_t inset = static_cast<std::uint32_t>(8.0 * scale);
    const std::array<std::uint8_t, 4> expected{ground.red, ground.green, ground.blue, ground.alpha};
    for (std::uint32_t row = 0U; row < 16U; ++row) {
        for (std::uint32_t column = 0U; column < 16U; ++column) {
            if (!(pixel_of(*painter.last, inset + column, inset + row) == expected)) return false;
        }
    }
    return true;
}

// The first paint draws the ground and queues the pattern; no paint
// generates it; an idle frame does, and the part is repainted with it.
void generated_in_idle_frames() {
    const ProceduralFill fill = felt();
    std::unique_ptr<Fixture> fixture = make_fixture(fill, Size{40.0, 24.0}, 1.0);
    Fixture& made = *fixture;
    Window& window = *made.window;
    const RasterCacheStatistics first = window.engine_parts().cache_statistics();
    require(first.procedural_generations == 0U && first.procedural_placeholders == 1U && first.procedural_queued == 1U,
            "the first paint queues the pattern and generates nothing");
    require(face_is_ground(made.painter, fill.ground, 1.0), "until it is ready the face shows the ground");
    (*made.pad).invalidate(Dirty::paint);
    repaint(made);
    require(window.engine_parts().cache_statistics().procedural_generations == 0U,
            "a paint never generates a pattern");

    const std::size_t painted = made.painter.rasters;
    run_frame(made);
    const RasterCacheStatistics idle = window.engine_parts().cache_statistics();
    require(idle.procedural_generations == 1U && idle.procedural_queued == 0U && idle.procedural_patterns == 1U,
            "an idle frame generates the pattern");
    require(made.painter.rasters == painted + 1U, "the part that drew the ground is repainted");
    const std::shared_ptr<PartRaster> expected = make_pattern(fill, 1.0);
    require(face_shows(made.painter, *expected, 1.0, 24U), "and shows the pattern, tile pixel for pixel");
    require(idle.procedural_hits >= 1U, "lent from the shared cache");

    // Settled: further idle frames neither generate nor repaint.
    run_frame(made);
    run_frame(made);
    require(window.engine_parts().cache_statistics().procedural_generations == 1U &&
                made.painter.rasters == painted + 1U,
            "nothing more is generated or repainted");
}

// A scale change draws the ground again, then generates the new scale's
// pattern in an idle frame: the pattern at scale 2, not scale 1's
// resampled.
void scale_change_regenerates() {
    const ProceduralFill fill = wood_grain();
    std::unique_ptr<Fixture> fixture = make_fixture(fill, Size{40.0, 24.0}, 1.0);
    Fixture& made = *fixture;
    Window& window = *made.window;
    run_frame(made);
    require(window.engine_parts().cache_statistics().procedural_generations == 1U, "scale 1's pattern is made");
    window.set_scale(2.0);
    repaint(made);
    const RasterCacheStatistics changed = window.engine_parts().cache_statistics();
    require(changed.procedural_generations == 1U && changed.procedural_queued == 1U,
            "the new scale's pattern is queued, not made in the paint");
    require(face_is_ground(made.painter, fill.ground, 2.0), "the face shows the ground meanwhile");
    run_frame(made);
    const RasterCacheStatistics regenerated = window.engine_parts().cache_statistics();
    require(regenerated.procedural_generations == 2U && regenerated.procedural_patterns == 2U,
            "the scale-2 pattern is generated");
    const std::shared_ptr<PartRaster> expected = make_pattern(fill, 2.0);
    require((*expected).extent() == DeviceExtent{64U, 64U}, "scale 2's tile is 64 pixels");
    require(face_shows(made.painter, *expected, 2.0, 48U), "the face shows the scale-2 pattern");
}

// The cost-bound fallback: a pattern past the bake budget, or past
// the pattern byte limit, refuses the material.
void cost_bound_fallback() {
    const EnginePrimitive* const primitive = builtin_primitive_registry().find("material");
    require(primitive != nullptr, "the built-in registry has the material primitive");
    const PrimitiveDescriptor& descriptor = (*primitive).descriptor();
    PrimitiveParameterBlock parameters = default_parameters(descriptor);
    require(set_choice_parameter(parameters, descriptor, "recipe", "button") == ParameterAssignment::assigned,
            "a recipe role is a choice");

    SurfaceMaterial small{};
    small.fills.clear();
    small.fills.push_back(MaterialFillLayer::procedural_pattern(felt()));
    PrimitiveTarget target{};
    target.logical_size = Size{40.0, 24.0};
    target.scale = DeviceScale{1.0};
    target.material = &small;
    require((*primitive).accepts(parameters, target), "a 32-unit felt tile is within the budget");

    // A 256-unit felt tile at scale 4: 1024 x 1024 pixels, past both.
    ProceduralFill large = felt();
    large.tile = 256.0;
    SurfaceMaterial heavy{};
    heavy.fills.clear();
    heavy.fills.push_back(MaterialFillLayer::procedural_pattern(large));
    target.material = &heavy;
    target.scale = DeviceScale{4.0};
    const DeviceExtent extent = engine_primitives::procedural_extent(large, Rect{}, 4.0);
    require(engine_primitives::procedural_generation_effort(large, extent) >
                engine_primitives::maximum_material_bake_effort,
            "its generation's effort is past the budget");
    require(!(*primitive).accepts(parameters, target), "the material is refused");

    // A 256-unit pinstripe at scale 4: cheap to generate, but 4 MB.
    ProceduralFill wide = pinstripe();
    wide.tile = 256.0;
    SurfaceMaterial striped{};
    striped.fills.clear();
    striped.fills.push_back(MaterialFillLayer::procedural_pattern(wide));
    target.material = &striped;
    const DeviceExtent stripes = engine_primitives::procedural_extent(wide, Rect{}, 4.0);
    require(engine_primitives::procedural_generation_effort(wide, stripes) <=
                engine_primitives::maximum_material_bake_effort,
            "the wide pinstripe's generation is within the budget");
    require(!(*primitive).accepts(parameters, target), "but its bytes refuse the material");

    // Through the engine: a fresco over a 2000 x 400 box holds 3.2 MB.
    std::unique_ptr<Fixture> fixture = make_fixture(fresco(), Size{2000.0, 400.0}, 1.0);
    Fixture& made = *fixture;
    require(made.painter.rasters == 0U, "a refused material composites no raster");
    require((*made.window).metrics_snapshot().engine_primitive_refusals == 1U, "the refusal is counted");
    require((*made.window).engine_parts().cache_statistics().procedural_queued == 0U, "and nothing is queued");
}

// -- measuring the generation costs ------------------------------------------------

void measure() {
    const double pixels = 256.0 * 256.0;
    for (const PatternCase& reference : pattern_cases) {
        ProceduralFill fill = reference.make();
        fill.tile = 256.0;
        const DeviceExtent extent{256U, 256U};
        std::shared_ptr<PartRaster> raster = PartRaster::create(extent, DeviceScale{1.0});
        require(raster != nullptr, "the measured raster is made");
        engine_primitives::ProceduralScratch scratch{};
        std::array<double, 9> samples{};
        for (double& sample : samples) {
            const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
            engine_primitives::generate_procedural(fill, 1.0, (*raster).view(), scratch);
            const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
            sample = std::chrono::duration<double, std::nano>(end - start).count();
        }
        std::sort(samples.begin(), samples.end());
        std::cout << reference.name << ' ' << std::fixed << std::setprecision(2) << samples[4] / pixels
                  << " ns per pixel\n";
    }
}

// The checker: a 2-unit tile is a dither of logical pixels, the ground at
// the tile's top left and bottom right, the figure beside them; at scale 2
// each cell is 2 × 2 device pixels.
void checker_dithers() {
    ProceduralFill fill{};
    fill.pattern = ProceduralPattern::checker;
    fill.tile = 2.0;
    fill.ground = Color::rgba(212, 208, 200);
    fill.figure = Color::rgba(255, 255, 255);
    const std::array<std::uint8_t, 4> ground{212, 208, 200, 255};
    const std::array<std::uint8_t, 4> figure{255, 255, 255, 255};
    const std::shared_ptr<PartRaster> one = make_pattern(fill, 1.0);
    require((*one).extent().width == 2U && (*one).extent().height == 2U, "the checker's tile is 2 pixels at scale 1");
    require(pixel_of(*one, 0U, 0U) == ground && pixel_of(*one, 1U, 1U) == ground, "the ground on the diagonal");
    require(pixel_of(*one, 1U, 0U) == figure && pixel_of(*one, 0U, 1U) == figure, "the figure beside it");
    const std::shared_ptr<PartRaster> two = make_pattern(fill, 2.0);
    require((*two).extent().width == 4U && (*two).extent().height == 4U, "the checker's tile is 4 pixels at scale 2");
    for (std::uint32_t row = 0U; row < 4U; ++row) {
        for (std::uint32_t column = 0U; column < 4U; ++column) {
            const bool diagonal = (row >= 2U) == (column >= 2U);
            const std::array<std::uint8_t, 4>& expected = diagonal ? ground : figure;
            require(pixel_of(*two, column, row) == expected, "each cell is 2 x 2 pixels at scale 2");
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--measure") == 0) {
        measure();
        return 0;
    }
    reference_patterns();
    checker_dithers();
    patterns_are_seeded();
    tiles_per_scale();
    generated_in_idle_frames();
    scale_change_regenerates();
    cost_bound_fallback();
    std::cout << "gui_forms_engine_procedural_tests passed\n";
    return 0;
}
