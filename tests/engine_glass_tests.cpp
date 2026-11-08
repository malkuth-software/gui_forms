// Glass and the window's backplane image (T2 E9.3; owner E9-B, 2026-10-22):
// the glass primitive's blur, the backplane image the core bakes from the
// window's backplane material and the backplane plane's engine part rasters
// (option 1), its cache against the backplane's damage generation, and
// that glass never samples the control plane.
//
// Run with --print to print the reference hash instead of checking it.

#include "gui_forms/gui_forms.hpp"
#include "support/theme_definition_heap.hpp"
#include "support/rgba_order.hpp"

#include "../src/core/engine/primitives/light/glass_primitive.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace {

using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "gui_forms_engine_glass_tests: " << message << '\n';
    std::exit(1);
}

bool print_mode(int argc, char** argv) {
    const bool printing = argc > 1 && std::string_view(argv[1]) == "--print";
    return printing;
}

std::uint64_t hash_bytes(std::span<const std::uint8_t> bytes) {
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (const std::uint8_t byte : gui_forms::test_support::rgba_order(bytes)) {
        hash = hash ^ byte;
        hash = hash * 0x100000001b3ULL;
    }
    return hash;
}

// A raster of `extent` at `scale` with every pixel `colour` (premultiplied).
std::shared_ptr<PartRaster> solid_raster(DeviceExtent extent, double scale, std::array<std::uint8_t, 4> colour) {
    std::shared_ptr<PartRaster> raster = PartRaster::create(extent, DeviceScale{scale});
    require(raster != nullptr, "the raster is made");
    const PartRasterView view = (*raster).view();
    for (std::size_t row = 0U; row < extent.height; ++row) {
        for (std::size_t column = 0U; column < extent.width; ++column) {
            const std::size_t at = row * view.row_bytes + column * 4U;
            // `colour` is red, green, blue, alpha; the raster is BGRA8.
            view.pixels[at] = colour[2];
            view.pixels[at + 1U] = colour[1];
            view.pixels[at + 2U] = colour[0];
            view.pixels[at + 3U] = colour[3];
        }
    }
    return raster;
}

std::array<std::uint8_t, 4> pixel_of(const PartRaster& raster, std::size_t column, std::size_t row) {
    const std::size_t at = row * raster.row_bytes() + column * 4U;
    const std::span<const std::uint8_t> pixels = raster.pixels();
    // The raster is BGRA8; the value is red, green, blue, alpha.
    const std::array<std::uint8_t, 4> value{pixels[at + 2U], pixels[at + 1U], pixels[at], pixels[at + 3U]};
    return value;
}

// -- the primitive ---------------------------------------------------------------

const EnginePrimitive& glass() {
    const EnginePrimitive* const found = builtin_primitive_registry().find("glass");
    require(found != nullptr, "glass is registered");
    return *found;
}

// Draws glass of `logical` size at `scale` over `backplane` (null for
// none) with `parameters`, lent `material` (null for none), under reduced
// transparency when `reduced`.
std::shared_ptr<PartRaster> draw_glass(const PrimitiveParameterBlock& parameters, Size logical, double scale,
                                       const PartRaster* backplane, const SurfaceMaterial* material = nullptr,
                                       bool reduced = false) {
    const EnginePrimitive& primitive = glass();
    PrimitiveTarget target{};
    target.logical_size = logical;
    target.scale = DeviceScale{scale};
    target.backplane = backplane;
    target.material = material;
    target.reduced_transparency = reduced;
    require(primitive.accepts(parameters, target), "glass accepts the target");
    const std::optional<std::uint32_t> margin = primitive.backplane_margin(parameters, target.scale);
    require(margin.has_value(), "glass samples the backplane");
    target.backplane_margin = *margin;
    const DeviceExtent extent{static_cast<std::uint32_t>(logical.width * scale),
                              static_cast<std::uint32_t>(logical.height * scale)};
    std::shared_ptr<PartRaster> drawn = PartRaster::create(extent, target.scale);
    PrimitivePreparation preparation{};
    require(primitive.prepare(parameters, target, preparation) == PreparationOutcome::prepared, "glass prepares");
    require(primitive.rasterize(parameters, target, preparation, (*drawn).view()) == RasterOutcome::painted,
            "glass paints");
    return drawn;
}

// Vertical stripes four pixels wide, opaque black and white, as a backplane
// of `extent`.
std::shared_ptr<PartRaster> stripes(DeviceExtent extent) {
    std::shared_ptr<PartRaster> raster = solid_raster(extent, 1.0, {0U, 0U, 0U, 255U});
    const PartRasterView view = (*raster).view();
    for (std::size_t row = 0U; row < extent.height; ++row) {
        for (std::size_t column = 0U; column < extent.width; ++column) {
            if ((column / 4U) % 2U == 0U) continue;
            const std::size_t at = row * view.row_bytes + column * 4U;
            view.pixels[at] = 255U;
            view.pixels[at + 1U] = 255U;
            view.pixels[at + 2U] = 255U;
        }
    }
    return raster;
}

// The glass over the stripes at scale 1, default parameters; recorded on
// Windows x64 and checked on every CI platform.
constexpr std::uint64_t expected_glass_hash = 0x748e797fedc763e5ULL;

void the_blur(bool printing) {
    const EnginePrimitive& primitive = glass();
    const PrimitiveParameterBlock defaults = default_parameters(primitive.descriptor());
    // σ = blur × scale / 2; three boxes of 2h + 1, read 3h past each side.
    require(engine_primitives::glass_box_half(6.0, 1.0) == 3U, "blur 6 at scale 1 is a box of 7");
    require(*primitive.backplane_margin(defaults, DeviceScale{1.0}) == 9U, "blur 6 at scale 1 reads 9 pixels past");
    require(engine_primitives::glass_box_half(8.0, 2.0) == 8U, "blur 8 at scale 2 is a box of 17");
    require(engine_primitives::glass_box_half(0.0, 2.0) == 0U, "blur 0 is no blur");

    // A uniform backplane stays uniform: with no tint the glass is exactly
    // the backplane.
    PrimitiveParameterBlock clear = defaults;
    clear.values[1].colour = Color::rgba(0, 0, 0, 0);
    const std::shared_ptr<PartRaster> uniform = solid_raster(DeviceExtent{58U, 38U}, 1.0, {200U, 100U, 50U, 255U});
    const std::shared_ptr<PartRaster> plain = draw_glass(clear, Size{40.0, 20.0}, 1.0, uniform.get());
    for (std::size_t row = 0U; row < 20U; ++row) {
        for (std::size_t column = 0U; column < 40U; ++column) {
            require(pixel_of(*plain, column, row) == (std::array<std::uint8_t, 4>{200U, 100U, 50U, 255U}),
                    "a uniform backplane is unchanged by the blur");
        }
    }

    // Without a backplane the glass is its tint alone.
    const std::shared_ptr<PartRaster> tinted = draw_glass(defaults, Size{40.0, 20.0}, 1.0, nullptr);
    require(pixel_of(*tinted, 20U, 10U) == (std::array<std::uint8_t, 4>{64U, 64U, 64U, 64U}),
            "without a backplane the glass is its tint");

    // Stripes are smoothed: no pixel is black or white any more, and the
    // result is the same on every host.
    const std::shared_ptr<PartRaster> striped = stripes(DeviceExtent{58U, 38U});
    const std::shared_ptr<PartRaster> smoothed = draw_glass(defaults, Size{40.0, 20.0}, 1.0, striped.get());
    for (std::size_t column = 0U; column < 40U; ++column) {
        const std::array<std::uint8_t, 4> value = pixel_of(*smoothed, column, 10U);
        require(value[0] > 64U && value[0] < 255U && value[3] == 255U, "the stripes are blurred");
    }
    // A backplane of the wrong margin is not sampled.
    const std::shared_ptr<PartRaster> narrow = solid_raster(DeviceExtent{40U, 20U}, 1.0, {200U, 0U, 0U, 255U});
    const std::shared_ptr<PartRaster> refused = draw_glass(defaults, Size{40.0, 20.0}, 1.0, narrow.get());
    require(pixel_of(*refused, 20U, 10U) == pixel_of(*tinted, 20U, 10U), "a backplane of another size is not read");
    const std::uint64_t hash = hash_bytes((*smoothed).pixels());
    if (printing) {
        std::cout << "glass hash 0x" << std::hex << hash << std::dec << '\n';
        return;
    }
    require(hash == expected_glass_hash, "the glass has the same bits on every host");
}

// -- the glass recipe: the material's layers over the backdrop ----------------------

// Glass with the material's layers: blur 6, recipe `button` (choice 4),
// layers `material` (choice 1).
PrimitiveParameterBlock material_glass() {
    PrimitiveParameterBlock parameters = default_parameters(glass().descriptor());
    parameters.values[3].number = 4.0;
    parameters.values[4].number = 1.0;
    return parameters;
}

// Half-transparent blue over the backdrop, with a white keyline on top.
SurfaceMaterial translucent_material() {
    SurfaceMaterial material{};
    material.fills.clear();
    material.fills.push_back(MaterialFillLayer::solid(Color::rgba(0, 0, 255, 128)));
    MaterialKeyline keyline{};
    keyline.edge = MaterialEdge::top;
    keyline.color = Color::rgba(255, 255, 255, 255);
    material.keylines.push_back(keyline);
    MaterialShadow outer{};
    material.shadows.push_back(outer);
    return material;
}

// The glass over the stripes with translucent_material's layers at scale 1;
// recorded on macOS arm64 and checked on every CI platform.
constexpr std::uint64_t expected_material_glass_hash = 0xd5a8856b87b8bab5ULL;

void the_glass_recipe(bool printing) {
    const PrimitiveParameterBlock parameters = material_glass();
    const SurfaceMaterial material = translucent_material();
    const std::shared_ptr<PartRaster> uniform = solid_raster(DeviceExtent{58U, 38U}, 1.0, {200U, 100U, 50U, 255U});
    const std::shared_ptr<PartRaster> drawn = draw_glass(parameters, Size{40.0, 20.0}, 1.0, uniform.get(), &material);
    // Inside: the blue fill over the backdrop, with no tint: 128/255 of blue
    // over (200, 100, 50). The outer shadow lies past the box and is not drawn.
    const double keep = 1.0 - 128.0 / 255.0;
    const std::array<std::uint8_t, 4> inside = pixel_of(*drawn, 20U, 10U);
    const int red = static_cast<int>(std::floor(200.0 * keep + 0.5));
    const int blue = static_cast<int>(std::floor(128.0 + 50.0 * keep + 0.5));
    require(std::abs(static_cast<int>(inside[0]) - red) <= 1 && std::abs(static_cast<int>(inside[2]) - blue) <= 1 &&
                inside[3] == 255U,
            "the material's fill lies over the backdrop in place of the tint");
    // The keyline is the top row.
    require(pixel_of(*drawn, 20U, 0U) == (std::array<std::uint8_t, 4>{255U, 255U, 255U, 255U}),
            "the material's keyline is drawn");

    // An opaque fill hides the backdrop.
    SurfaceMaterial opaque{};
    const std::shared_ptr<PartRaster> covered = draw_glass(parameters, Size{40.0, 20.0}, 1.0, uniform.get(), &opaque);
    require(pixel_of(*covered, 20U, 10U) == (std::array<std::uint8_t, 4>{255U, 255U, 255U, 255U}),
            "an opaque fill covers the backdrop");

    // Under reduced transparency the flattened backdrop is opaque, and so is
    // the material over it.
    SurfaceMaterial clear{};
    clear.fills.clear();
    clear.fills.push_back(MaterialFillLayer::solid(Color::rgba(0, 0, 0, 0)));
    const std::shared_ptr<PartRaster> flat =
        draw_glass(parameters, Size{40.0, 20.0}, 1.0, nullptr, &material, true);
    require(pixel_of(*flat, 20U, 10U)[3] == 255U, "reduced transparency draws the recipe opaque");
    // A clear material over a uniform backdrop is the backdrop itself.
    const std::shared_ptr<PartRaster> bare = draw_glass(parameters, Size{40.0, 20.0}, 1.0, uniform.get(), &clear);
    require(pixel_of(*bare, 20U, 10U) == (std::array<std::uint8_t, 4>{200U, 100U, 50U, 255U}),
            "without layers to draw the glass is its backdrop, untinted");

    // Refusals: no material lent, or an image fill; the filter layers need
    // neither.
    const EnginePrimitive& primitive = glass();
    PrimitiveTarget target{};
    target.logical_size = Size{40.0, 20.0};
    target.scale = DeviceScale{1.0};
    require(!primitive.accepts(parameters, target), "the material layers refuse without a material");
    require(primitive.accepts(default_parameters(primitive.descriptor()), target), "the filter layers need none");
    SurfaceMaterial imaged{};
    imaged.fills.clear();
    imaged.fills.push_back(MaterialFillLayer::stretched_image(ImageId{1U}, Size{4.0, 4.0}));
    target.material = &imaged;
    require(!primitive.accepts(parameters, target), "an image fill is refused");

    const std::shared_ptr<PartRaster> striped = stripes(DeviceExtent{58U, 38U});
    const std::shared_ptr<PartRaster> recipe = draw_glass(parameters, Size{40.0, 20.0}, 1.0, striped.get(), &material);
    const std::uint64_t hash = hash_bytes((*recipe).pixels());
    if (printing) {
        std::cout << "material glass hash 0x" << std::hex << hash << std::dec << '\n';
        return;
    }
    require(hash == expected_material_glass_hash, "the glass recipe has the same bits on every host");
}

// -- the window --------------------------------------------------------------------

// A painter that keeps the part rasters composited and draws nothing.
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
        rasters.push_back(raster);
    }
    std::vector<std::shared_ptr<const PartRaster>> rasters;
};

// A backplane-plane control drawing a red engine part raster over its
// bounds, and a fill the backplane image skips.
class Backdrop final : public Control {
public:
    explicit Backdrop(StableId stable_id) : Control(std::move(stable_id)) { set_paint_plane(PaintPlane::backplane); }
    void on_paint(Painter& painter, Rect bounds) override {
        painter.fill_rect(bounds, Color::rgba(0, 0, 255, 255));
        if (red) painter.draw_part_raster(red, Point{0.0, 0.0}, 1.0);
    }
    std::shared_ptr<const PartRaster> red;
};

// A control-plane control drawing a green raster over the glass: glass must
// never show it.
class Cover final : public Control {
public:
    explicit Cover(StableId stable_id) : Control(std::move(stable_id)) {}
    void on_paint(Painter& painter, Rect) override {
        if (green) painter.draw_part_raster(green, Point{0.0, 0.0}, 1.0);
    }
    std::shared_ptr<const PartRaster> green;
};

// A control-plane control with one glass region at `face`.
class Pane final : public Control {
public:
    explicit Pane(StableId stable_id) : Control(std::move(stable_id)) {}
    void on_paint(Painter& painter, Rect) override {
        PartPaint part{};
        part.role = PartRole::region;
        part.family = ControlVisualRole::card;
        part.bounds = face;
        part.corner_radius = 6.0;
        part.key = PartKey{"glass", {}, {}};
        paint_part(painter, *this, part);
    }
    Rect face{20.0, 20.0, 100.0, 30.0};
};

struct GlassFixture final {
    std::shared_ptr<Panel> root;
    std::shared_ptr<Backdrop> backdrop;
    std::shared_ptr<Cover> cover;
    std::shared_ptr<Pane> pane;
    std::unique_ptr<Window> window;
    RasterWatch painter{};
};

std::unique_ptr<ThemeDefinition> glass_theme(double blur) {
    std::unique_ptr<ThemeDefinition> held = test_support::professional_definition();
    ThemeDefinition& definition = *held;
    definition.id = "engine-glass";
    ThemeRule rule{};
    rule.label = "glass";
    rule.selector.role = PartRole::region;
    rule.declarations.push_back(RuleDeclaration::primitive("glass"));
    rule.declarations.push_back(RuleDeclaration::number_value("blur", blur));
    definition.rules.rules.push_back(rule);
    return held;
}

// The backdrop covers window x 60 to 180; the glass lies at 20 to 120.
std::unique_ptr<GlassFixture> make_glass(double blur, bool covered) {
    std::unique_ptr<GlassFixture> fixture = std::make_unique<GlassFixture>();
    GlassFixture& made = *fixture;
    made.root = make_control<Panel>(StableId("root"));
    made.backdrop = make_control<Backdrop>(StableId("backdrop"));
    (*made.backdrop).set_requested_bounds(Rect{60.0, 0.0, 120.0, 160.0});
    (*made.backdrop).red = solid_raster(DeviceExtent{120U, 160U}, 1.0, {255U, 0U, 0U, 255U});
    made.cover = make_control<Cover>(StableId("cover"));
    (*made.cover).set_requested_bounds(Rect{0.0, 0.0, 240.0, 160.0});
    if (covered) (*made.cover).green = solid_raster(DeviceExtent{240U, 160U}, 1.0, {0U, 255U, 0U, 255U});
    made.pane = make_control<Pane>(StableId("pane"));
    (*made.pane).set_requested_bounds(Rect{0.0, 0.0, 240.0, 160.0});
    (*made.root).add_child(made.backdrop);
    (*made.root).add_child(made.cover);
    (*made.root).add_child(made.pane);
    made.window = std::make_unique<Window>(made.root, Size{240.0, 160.0});
    Window& window = *made.window;
    window.set_theme(Theme::create(*glass_theme(blur)));
    window.perform_layout();
    window.paint(made.painter);
    static_cast<void>(window.take_damage());
    return fixture;
}

// The glass raster last composited: the one of the face's size.
std::shared_ptr<const PartRaster> last_glass(const GlassFixture& fixture) {
    for (std::size_t index = fixture.painter.rasters.size(); index > 0U; --index) {
        const std::shared_ptr<const PartRaster>& raster = fixture.painter.rasters[index - 1U];
        if ((*raster).extent().width == 100U && (*raster).extent().height == 30U) return raster;
    }
    return nullptr;
}

void repaint(GlassFixture& fixture) {
    Window& window = *fixture.window;
    const DamageRegion damage = window.take_damage();
    if (!damage.empty()) window.paint(fixture.painter, damage.bounds());
}

void glass_samples_the_backplane() {
    std::unique_ptr<GlassFixture> fixture = make_glass(6.0, false);
    const std::shared_ptr<const PartRaster> raster = last_glass(*fixture);
    require(raster != nullptr, "the glass is composited");
    // Window x 110 (glass column 90) is over the red backdrop; window x 30
    // (glass column 10) is over the panel alone.
    const std::array<std::uint8_t, 4> over_red = pixel_of(*raster, 90U, 15U);
    const std::array<std::uint8_t, 4> over_panel = pixel_of(*raster, 10U, 15U);
    require(over_red[0] > over_red[1] + 100U, "the glass shows the backplane's part raster");
    require(over_panel[0] < over_red[0], "the glass elsewhere shows the backplane material");
    const RasterCacheStatistics made = (*(*fixture).window).engine_parts().cache_statistics();
    require(made.backplane_bakes == 1U && made.backplane_images == 1U, "one backplane image is made and held");
    // The panel's recipe and the backdrop's fill are not part rasters.
    require(made.backplane_skipped_commands >= 1U, "the commands the backplane image skips are counted");

    // Glass never samples the control plane: a green control between the
    // backplane and the glass leaves its bytes unchanged.
    std::unique_ptr<GlassFixture> covered = make_glass(6.0, true);
    const std::shared_ptr<const PartRaster> under_cover = last_glass(*covered);
    require(under_cover != nullptr, "the covered glass is composited");
    const bool same = std::memcmp((*raster).pixels().data(), (*under_cover).pixels().data(),
                                  (*raster).pixels().size()) == 0;
    require(same, "the glass never samples the control plane");
}

void glass_follows_backplane_damage() {
    std::unique_ptr<GlassFixture> fixture = make_glass(6.0, false);
    GlassFixture& made = *fixture;
    // Parts baked so far: the glass alone.
    const std::uint64_t first = (*made.window).engine_parts().cache_statistics().bakes;
    // A control-plane change elsewhere bakes no backplane.
    (*made.cover).invalidate(Rect{200.0, 120.0, 10.0, 10.0});
    repaint(made);
    require((*made.window).engine_parts().cache_statistics().backplane_bakes == 1U,
            "control-plane damage leaves the backplane image");
    // Backplane damage away from the glass leaves the glass as it was.
    (*made.backdrop).invalidate(Rect{100.0, 120.0, 10.0, 10.0});
    repaint(made);
    const RasterCacheStatistics away = (*made.window).engine_parts().cache_statistics();
    require(away.bakes == first && away.backplane_bakes == 1U,
            "backplane damage away from the glass does not repaint it");
    // Backplane damage under the glass repaints it over a new image.
    (*made.backdrop).red = solid_raster(DeviceExtent{120U, 160U}, 1.0, {0U, 0U, 255U, 255U});
    (*made.backdrop).invalidate(Rect{0.0, 20.0, 60.0, 30.0});
    repaint(made);
    const RasterCacheStatistics after = (*made.window).engine_parts().cache_statistics();
    require(after.backplane_bakes == 2U, "backplane damage under the glass makes its image again");
    const std::shared_ptr<const PartRaster> raster = last_glass(made);
    const std::array<std::uint8_t, 4> over_blue = pixel_of(*raster, 90U, 15U);
    require(over_blue[2] > over_blue[0] + 100U, "the glass shows the changed backplane");
}

void over_the_cap_falls_back() {
    std::unique_ptr<GlassFixture> fixture = make_glass(9.0, false);
    const std::vector<RuleDiagnostic>& diagnostics = (*(*fixture).window).theme().engine().rules().diagnostics();
    bool out_of_range = false;
    for (const RuleDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.kind == RuleDiagnosticKind::parameter_out_of_range) out_of_range = true;
    }
    require(out_of_range, "a blur past 8 is rejected when the theme compiles");
    // The rejected value never reaches the primitive: the glass keeps its
    // default blur, within the cap.
    std::unique_ptr<GlassFixture> within = make_glass(6.0, false);
    const std::shared_ptr<const PartRaster> capped = last_glass(*fixture);
    const std::shared_ptr<const PartRaster> default_blur = last_glass(*within);
    require(capped != nullptr && default_blur != nullptr, "the glass draws");
    const bool same = std::memcmp((*capped).pixels().data(), (*default_blur).pixels().data(),
                                  (*capped).pixels().size()) == 0;
    require(same, "a blur past the cap falls back to the default");
}

} // namespace

int main(int argc, char** argv) {
    const bool printing = print_mode(argc, argv);
    the_blur(printing);
    the_glass_recipe(printing);
    if (printing) return 0;
    glass_samples_the_backplane();
    glass_follows_backplane_damage();
    over_the_cap_falls_back();
    std::cout << "engine glass tests passed\n";
    return 0;
}
