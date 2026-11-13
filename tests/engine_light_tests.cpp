// The engine's light primitives (T2 plan, phase E9). The overlay shadow
// (GIVEN E1-1) is an engine primitive with the first-party blur: a shadow
// part is drawn by the engine as the material kernel draws the theme's
// overlay shadow layers as outer shadows, identically on every host, with
// the same layers and damage outsets as before and none in high contrast;
// a shadow reaching past the primitive's outsets falls back to
// Painter::draw_box_shadow, counted.
//
// The sheen (owner E9-A) stands behind the overlay seam: its band is baked
// once and composited at the pointer, clipped to the part's shape; a
// pointer move within the part damages only the part and bakes nothing;
// under reduced motion the band rests and the pointer damages nothing. It
// follows the part's presentational scale: stretched with the face while
// the scale moves, baked at the scaled size once it rests.

#include "gui_forms/anatomy/types/anatomy_types.hpp"
#include "gui_forms/binding/property_type/property_type.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/dirty.hpp"
#include "gui_forms/engine/motion_clock/motion_clock.hpp"
#include "gui_forms/engine/motion_value/motion_value.hpp"
#include "gui_forms/engine/part_raster/part_raster.hpp"
#include "gui_forms/engine/primitive_registry/primitive_registry.hpp"
#include "gui_forms/engine/theme_rules/theme_rules.hpp"
#include "gui_forms/engine/types/engine_types.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/metrics/types/metrics_types.hpp"
#include "gui_forms/scheduler/types/scheduler_types.hpp"
#include "gui_forms/surface_material/material_fill_layer/material_fill_layer.hpp"
#include "gui_forms/surface_material/types/surface_material_types.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/theme/theme/theme.hpp"
#include "gui_forms/theme/types/theme_types.hpp"
#include "gui_forms/types/damage_region/damage_region.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/window/presentation/presentation_types.hpp"
#include "gui_forms/window/window.hpp"
#include "support/theme_definition_heap.hpp"
#include "support/rgba_order.hpp"

#include "../src/core/engine/primitives/material/material_primitive.hpp"
#include "../src/core/engine/primitives/light/sheen_primitive.hpp"
#include "../src/core/engine/primitives/material/overlay_shadow_primitive.hpp"
#include "support/routed_input_probe.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace {
using namespace gui_forms;
using std::chrono::milliseconds;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "gui_forms_engine_light_tests: " << message << '\n';
    std::exit(1);
}

bool print_mode(int argc, char** argv) {
    const bool printing = argc > 1 && std::string_view(argv[1]) == "--print";
    return printing;
}

// FNV-1a over bytes, 64 bits.
std::uint64_t hash_bytes(std::span<const std::uint8_t> bytes) {
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (const std::uint8_t byte : gui_forms::test_support::rgba_order(bytes)) {
        hash = hash ^ byte;
        hash = hash * 0x100000001b3ULL;
    }
    return hash;
}

// A painter that keeps the part rasters and box shadows composited, and the
// rounded clips, and draws nothing.
class ShadowWatch final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void clip_rounded_rect(Rect rect, double radius) override {
        clips.push_back(rect);
        clip_radii.push_back(radius);
    }
    void fill_rect(Rect, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}
    void draw_box_shadow(Rect, double, Point, double, double, Color) override { box_shadows = box_shadows + 1U; }
    void draw_part_raster(const std::shared_ptr<const PartRaster>& raster, Point origin, double) override {
        rasters.push_back(raster);
        origins.push_back(origin);
        stretched = false;
    }
    void draw_part_raster_scaled(const std::shared_ptr<const PartRaster>& raster, Rect destination,
                                 double) override {
        rasters.push_back(raster);
        origins.push_back(Point{destination.x, destination.y});
        stretched_to = destination;
        stretched = true;
    }
    std::vector<std::shared_ptr<const PartRaster>> rasters;
    std::vector<Point> origins;
    std::vector<Rect> clips;
    std::vector<double> clip_radii;
    // Whether the last raster was stretched, and over what.
    bool stretched{};
    Rect stretched_to{};
    std::size_t box_shadows{};
};

// The motion clock's time, advanced by hand.
class ManualTime final : public MotionTimeSource {
public:
    [[nodiscard]] FrameTime now() const noexcept override { return time; }
    FrameTime time{std::chrono::seconds(1000)};
};

// A control casting a shadow from a 120 by 40 surface of radius 4.
class Caster final : public Control {
public:
    explicit Caster(StableId stable_id) : Control(std::move(stable_id)) {}
    void on_paint(Painter& painter, Rect) override {
        PartPaint part{};
        part.role = PartRole::shadow;
        part.family = ControlVisualRole::tool_tip;
        part.bounds = surface;
        part.corner_radius = 4.0;
        part.key = PartKey{"shadow", {}, {}};
        paint_part(painter, *this, part);
    }
    Rect surface{20.0, 20.0, 120.0, 40.0};
};

struct Fixture final {
    std::shared_ptr<Panel> root;
    std::shared_ptr<Caster> caster;
    std::unique_ptr<Window> window;
    ShadowWatch painter{};
};

std::unique_ptr<Fixture> make_fixture(const ThemeDefinition& definition, double scale) {
    std::unique_ptr<Fixture> fixture = std::make_unique<Fixture>();
    Fixture& made = *fixture;
    made.root = make_control<Panel>(StableId("root"));
    made.caster = make_control<Caster>(StableId("caster"));
    (*made.caster).set_requested_bounds(Rect{0.0, 0.0, 200.0, 120.0});
    (*made.root).add_child(made.caster);
    made.window = std::make_unique<Window>(made.root, Size{240.0, 160.0});
    Window& window = *made.window;
    window.set_scale(scale);
    window.set_theme(Theme::create(definition));
    window.perform_layout();
    window.paint(made.painter);
    return fixture;
}

// The material the engine lends for `layers`: one transparent fill and the
// shadows, at radius 4.
SurfaceMaterial overlay_material(const std::vector<MaterialShadow>& layers) {
    SurfaceMaterial material{};
    material.fills.assign(1U, MaterialFillLayer::solid(Color::rgba(0, 0, 0, 0)));
    material.shadows = layers;
    material.corner_radius = 4.0;
    return material;
}

// The whole bake of the overlay shadow for the caster's surface at `scale`.
std::vector<std::uint8_t> whole_bake(const std::vector<MaterialShadow>& layers, double scale) {
    const SurfaceMaterial material = overlay_material(layers);
    PrimitiveTarget target{};
    target.logical_size = Size{120.0, 40.0};
    target.scale = DeviceScale{scale};
    target.material = &material;
    const double outset = engine_primitives::overlay_shadow_outset;
    const DeviceExtent extent{static_cast<std::uint32_t>(std::ceil((120.0 + outset * 2.0) * scale)),
                              static_cast<std::uint32_t>(std::ceil((40.0 + outset * 2.0) * scale))};
    const std::shared_ptr<PartRaster> raster = PartRaster::create(extent, DeviceScale{scale});
    require(raster != nullptr, "a raster is made");
    engine_primitives::bake_material(material, target, (*raster).view(), outset);
    const std::span<const std::uint8_t> pixels = (*raster).pixels();
    std::vector<std::uint8_t> bytes(pixels.begin(), pixels.end());
    return bytes;
}

// The overlay shadow's bits at scale 1 for the provisional layers, recorded
// on Windows x64 and checked on every CI platform.
constexpr std::uint64_t expected_shadow_hash = 0x12c9cfd27f7ea098ULL;

void overlay_shadow_is_an_engine_primitive(bool printing) {
    require(builtin_primitive_registry().find("overlay_shadow") != nullptr, "the overlay shadow is registered");
    const std::unique_ptr<ThemeDefinition> held_definition = test_support::professional_definition();
    const ThemeDefinition& definition = *held_definition;
    const double scales[] = {1.0, 1.5, 2.0};
    for (const double scale : scales) {
        std::unique_ptr<Fixture> fixture = make_fixture(definition, scale);
        Fixture& made = *fixture;
        require(made.painter.box_shadows == 0U, "no host blur draws the shadow");
        require(made.painter.rasters.size() == 1U, "the engine composites one shadow raster");
        const PartRaster& raster = *made.painter.rasters[0];
        const double outset = engine_primitives::overlay_shadow_outset;
        require(made.painter.origins[0].x == 20.0 - outset && made.painter.origins[0].y == 20.0 - outset,
                "the raster covers the surface grown by the outsets");
        const std::span<const std::uint8_t> pixels = raster.pixels();
        const std::vector<std::uint8_t> drawn(pixels.begin(), pixels.end());
        require(drawn == whole_bake(definition.overlay_shadow, scale),
                "the shadow is the material kernel's outer shadows, byte for byte, sliced or not");
        if (scale != 1.0) continue;
        const std::uint64_t hash = hash_bytes(pixels);
        if (printing) {
            std::cout << "overlay shadow hash 0x" << std::hex << hash << std::dec << '\n';
            continue;
        }
        require(hash == expected_shadow_hash, "the shadow has the same bits on every host");
    }
}

void high_contrast_casts_none() {
    const std::unique_ptr<ThemeDefinition> held_definition = test_support::professional_definition();
    ThemeDefinition& definition = *held_definition;
    std::unique_ptr<Fixture> fixture = make_fixture(definition, 1.0);
    Fixture& made = *fixture;
    PresentationSettings settings = (*made.window).presentation_settings();
    settings.high_contrast = true;
    (*made.window).set_presentation_settings(settings);
    made.painter.rasters.clear();
    made.painter.box_shadows = 0U;
    (*made.window).perform_layout();
    (*made.window).paint(made.painter);
    require(made.painter.rasters.empty() && made.painter.box_shadows == 0U, "high contrast casts no shadow");
}

void too_wide_falls_back() {
    const std::unique_ptr<ThemeDefinition> held_definition = test_support::professional_definition();
    ThemeDefinition& definition = *held_definition;
    definition.overlay_shadow = {MaterialShadow{.offset = {2.0, 4.0}, .blur_radius = 24.0, .spread = 0.0,
                                                .color = Color::rgba(20, 30, 42, 64), .inset = false}};
    std::unique_ptr<Fixture> fixture = make_fixture(definition, 1.0);
    Fixture& made = *fixture;
    require(made.painter.rasters.empty() && made.painter.box_shadows == 1U,
            "a shadow reaching past the outsets is drawn by the host's blur");
    require((*made.window).metrics_snapshot().engine_primitive_refusals == 1U, "and the refusal is counted");
}

// -- the sheen ---------------------------------------------------------------------

// A control showing a keyed sheen over a 100 by 30 face of radius 6, hot
// while the pointer is over the control.
class Glint final : public gui_forms::tests::RoutedInputProbe<Control> {
public:
    explicit Glint(StableId stable_id) : gui_forms::tests::RoutedInputProbe<Control>(std::move(stable_id)) {}
    void routed_pointer(PointerEvent& event) override {
        if (event.action == PointerAction::enter) {
            hot = true;
            invalidate(Dirty::paint);
        }
        if (event.action == PointerAction::leave) {
            hot = false;
            invalidate(Dirty::paint);
        }
    }
    void on_paint(Painter& painter, Rect) override {
        PartPaint part{};
        part.role = PartRole::sheen;
        part.family = ControlVisualRole::button;
        part.bounds = face;
        part.corner_radius = 6.0;
        part.key = PartKey{"sheen", {}, {}};
        part.granted_overflow = overflow;
        if (hot) part.states.insert(AnatomyState::hot);
        paint_part(painter, *this, part);
    }
    bool hot{};
    Rect face{20.0, 20.0, 100.0, 30.0};
    Insets overflow{};
};

struct SheenFixture final {
    ManualTime time{};
    std::shared_ptr<Panel> root;
    std::shared_ptr<Glint> glint;
    std::unique_ptr<Window> window;
    ShadowWatch painter{};
};

// The part grows to `hot_scale` while hot, linearly over 100 ms.
PropertyTransition grow(double hot_scale) {
    PropertyTransition transition{};
    transition.property = std::string(scale_property);
    PresentationState hot{};
    hot.when.push_back(AnatomyState::hot);
    hot.target = hot_scale;
    PresentationState rest{};
    rest.target = 1.0;
    transition.states.push_back(hot);
    transition.states.push_back(rest);
    transition.curve = MotionCurve::timed(milliseconds(100), MotionEasing::linear);
    return transition;
}

// A sheen theme; with `hot_scale` above 1 the part pops while hot, within
// a granted overflow of 10 on every side.
std::unique_ptr<SheenFixture> make_sheen(double scale, bool reduced_motion, double hot_scale = 1.0) {
    const std::unique_ptr<ThemeDefinition> held_definition = test_support::professional_definition();
    ThemeDefinition& definition = *held_definition;
    definition.id = "engine-light-sheen";
    ThemeRule rule{};
    rule.label = "sheen";
    rule.selector.role = PartRole::sheen;
    rule.declarations.push_back(RuleDeclaration::primitive("sheen"));
    if (hot_scale != 1.0) rule.transitions.push_back(grow(hot_scale));
    definition.rules.rules.push_back(rule);
    std::unique_ptr<SheenFixture> fixture = std::make_unique<SheenFixture>();
    SheenFixture& made = *fixture;
    made.root = make_control<Panel>(StableId("root"));
    made.glint = make_control<Glint>(StableId("glint"));
    (*made.glint).set_requested_bounds(Rect{0.0, 0.0, 200.0, 120.0});
    if (hot_scale != 1.0) (*made.glint).overflow = Insets{10.0, 10.0, 10.0, 10.0};
    (*made.root).add_child(made.glint);
    made.window = std::make_unique<Window>(made.root, Size{240.0, 160.0});
    Window& window = *made.window;
    window.motion_clock().set_time_source(&made.time);
    window.set_scale(scale);
    window.set_theme(Theme::create(definition));
    require(window.theme().engine().rules().diagnostics().empty(), "the sheen theme compiles cleanly");
    if (reduced_motion) {
        PresentationSettings settings = window.presentation_settings();
        settings.reduced_motion = true;
        window.set_presentation_settings(settings);
    }
    window.perform_layout();
    window.paint(made.painter);
    static_cast<void>(window.take_damage());
    return fixture;
}

void move_to(Window& window, Point point) {
    static_cast<void>(window.dispatch_pointer({PointerAction::move, PointerButton::none, point}));
}

// Paints what is pending and returns the damage painted.
DamageRegion repaint(SheenFixture& fixture) {
    Window& window = *fixture.window;
    const DamageRegion damage = window.take_damage();
    if (!damage.empty()) window.paint(fixture.painter, damage.bounds());
    return damage;
}

// The left edge at which the 24-unit band was last composited.
double band_left(const SheenFixture& fixture) {
    require(!fixture.painter.origins.empty(), "the band was composited");
    const double left = fixture.painter.origins.back().x;
    return left;
}

// The band at scale 1: zero at both edges, symmetric, strongest at its
// centre, uniform down the part; recorded on Windows x64 and checked on
// every CI platform.
constexpr std::uint64_t expected_band_hash = 0xd4c2e5fcf3e31bf5ULL;

void sheen_band_is_deterministic(bool printing) {
    require(builtin_primitive_registry().find("sheen") != nullptr, "the sheen is registered");
    std::unique_ptr<SheenFixture> fixture = make_sheen(1.0, false);
    const SheenFixture& made = *fixture;
    require(made.painter.rasters.size() == 1U, "the sheen composites one band");
    const PartRaster& band = *made.painter.rasters[0];
    require(band.extent().width == 24U && band.extent().height == 30U, "the band is its width by the part's height");
    const std::span<const std::uint8_t> pixels = band.pixels();
    const std::size_t row = band.row_bytes();
    // Alpha by column, from the first row; every row repeats it.
    for (std::size_t line = 1U; line < 30U; ++line) {
        require(std::memcmp(pixels.data(), pixels.data() + line * row, row) == 0, "the band is uniform down the part");
    }
    std::uint8_t strongest = 0U;
    for (std::size_t column = 0U; column < 24U; ++column) {
        const std::uint8_t alpha = pixels[column * 4U + 3U];
        const std::uint8_t mirrored = pixels[(23U - column) * 4U + 3U];
        require(alpha == mirrored, "the band is symmetric");
        strongest = std::max(strongest, alpha);
    }
    require(pixels[3] < pixels[11U * 4U + 3U] && strongest == pixels[11U * 4U + 3U] && strongest <= 96U,
            "the band fades from its centre to its edges");
    const std::uint64_t hash = hash_bytes(pixels);
    if (printing) {
        std::cout << "sheen band hash 0x" << std::hex << hash << std::dec << '\n';
        return;
    }
    require(hash == expected_band_hash, "the band has the same bits on every host");
}

void sheen_follows_the_pointer() {
    std::unique_ptr<SheenFixture> fixture = make_sheen(1.0, false);
    SheenFixture& made = *fixture;
    Window& window = *made.window;
    // At rest, a quarter of the way across: 20 + 25 - 12.
    require(band_left(made) == 33.0, "the band rests a quarter of the way across");
    require(made.painter.clips.size() == 1U && made.painter.clips[0] == Rect{20.0, 20.0, 100.0, 30.0} &&
                made.painter.clip_radii[0] == 6.0,
            "the band is clipped to the part's rounded shape");
    const std::shared_ptr<const PartRaster> baked = made.painter.rasters[0];

    move_to(window, Point{60.0, 35.0});
    static_cast<void>(repaint(made));
    require(band_left(made) == 48.0, "the band's centre follows the pointer: 20 + 40 - 12");

    // A move within the part damages only the part and bakes nothing.
    const MetricsSnapshot before = window.metrics_snapshot();
    move_to(window, Point{90.0, 40.0});
    const DamageRegion damage = repaint(made);
    require(damage.rectangles().size() == 1U && damage.rectangles()[0] == Rect{20.0, 20.0, 100.0, 30.0},
            "a pointer move damages only the sheen's part");
    require(band_left(made) == 78.0, "the band moves with the pointer: 20 + 70 - 12");
    require(made.painter.rasters.back() == baked, "the same band is composited, never baked again");
    const MetricsSnapshot after = window.metrics_snapshot();
    require(after.engine_input_damaged_parts == before.engine_input_damaged_parts + 1U,
            "the move damaged one reading part");

    // Leaving the control returns the band to its rest.
    move_to(window, Point{230.0, 150.0});
    static_cast<void>(repaint(made));
    require(band_left(made) == 33.0, "the band rests again once the pointer leaves");
    require(made.painter.rasters.back() == baked, "and is still the band baked first");
}

void sheen_rests_under_reduced_motion() {
    std::unique_ptr<SheenFixture> fixture = make_sheen(1.0, true);
    SheenFixture& made = *fixture;
    Window& window = *made.window;
    move_to(window, Point{60.0, 35.0});
    static_cast<void>(repaint(made));
    require(band_left(made) == 33.0, "under reduced motion the band rests where the pointer is");
    move_to(window, Point{90.0, 40.0});
    require(window.take_damage().empty(), "and a pointer move within the part damages nothing");
}

void sheen_at_scale_two() {
    std::unique_ptr<SheenFixture> fixture = make_sheen(2.0, false);
    const SheenFixture& made = *fixture;
    require(made.painter.rasters.size() == 1U, "the sheen composites one band at scale 2");
    const PartRaster& band = *made.painter.rasters[0];
    require(band.extent().width == 48U && band.extent().height == 60U, "the band is baked at device scale");
    require(band_left(made) == 33.0, "and rests at the same logical place");
}

bool near(double left, double right) {
    const bool close = std::abs(left - right) < 1e-9;
    return close;
}

// The part pops to 1.2 (its granted limit: 1 + 2 × 10 / 100) while hot.
void sheen_follows_the_presentational_scale() {
    std::unique_ptr<SheenFixture> fixture = make_sheen(1.0, false, 1.2);
    SheenFixture& made = *fixture;
    Window& window = *made.window;
    const std::shared_ptr<const PartRaster> baked = made.painter.rasters.back();
    move_to(window, Point{60.0, 35.0});
    static_cast<void>(repaint(made));
    std::size_t moving_frames = 0U;
    for (int frame = 0; frame < 12; ++frame) {
        made.time.time += milliseconds(16);
        static_cast<void>(window.poll_frame_schedule(made.time.time));
        static_cast<void>(repaint(made));
        if (!made.painter.stretched) continue;
        moving_frames = moving_frames + 1U;
        // The face as shown: the clip is its rounded shape, scaled with it.
        const Rect face = made.painter.clips.back();
        const Rect band = made.painter.stretched_to;
        const double factor = face.width / 100.0;
        require(factor > 1.0 && factor < 1.2 + 1e-9, "the face grows within its grant");
        require(near(band.y, face.y) && near(band.height, face.height) && near(band.width, 24.0 * factor),
                "while the scale moves the band is stretched with the face");
        require(near(band.x + band.width * 0.5, 60.0), "and stays centred at the pointer");
        require(near(made.painter.clip_radii.back(), 6.0 * factor), "clipped to the face's scaled shape");
        require(made.painter.rasters.back() == baked, "the move stretches the band baked first, baking nothing");
    }
    require(moving_frames >= 3U, "the pop spans frames");
    // At rest at 1.2 the band is baked sharp at the scaled size.
    require(!made.painter.stretched, "at rest the band is composited unstretched");
    const PartRaster& sharp = *made.painter.rasters.back();
    require(sharp.extent().width == 29U && sharp.extent().height == 36U, "baked at 24 × 1.2 by 30 × 1.2");
    const Rect face = made.painter.clips.back();
    require(near(face.x, 10.0) && near(face.y, 17.0) && near(face.width, 120.0) && near(face.height, 36.0),
            "clipped to the popped face");
    require(near(made.painter.clip_radii.back(), 6.0), "with the part's own radius, as the face is baked");
    require(near(made.painter.origins.back().x, 60.0 - 14.4) && near(made.painter.origins.back().y, 17.0),
            "the band's centre stays at the pointer and its top at the face's");
}

} // namespace

int main(int argc, char** argv) {
    const bool printing = print_mode(argc, argv);
    overlay_shadow_is_an_engine_primitive(printing);
    high_contrast_casts_none();
    too_wide_falls_back();
    sheen_band_is_deterministic(printing);
    sheen_follows_the_pointer();
    sheen_rests_under_reduced_motion();
    sheen_at_scale_two();
    sheen_follows_the_presentational_scale();
    std::cout << "engine light tests passed\n";
    return 0;
}
