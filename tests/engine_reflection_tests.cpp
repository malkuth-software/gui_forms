// Stage T5, C7 (notes:planning/stages/T5-compositor/PLAN.md §4.4; owner
// T5-Q14 to T5-Q16): the material's reflection of its own finished face or
// of the backplane under it. Checked by what it draws at chosen pixels
// against the same material without a reflection, by the material's
// validation and outsets, by its omission under reduced transparency and
// high contrast in a window, by a backplane reflection rebaked on backplane
// damage under it and not elsewhere, and by reference images: FNV-1a hashes
// of each raster's rows in RGBA order, for each source and edge at scale 1
// and 2, checked on every CI platform.
// GUI_FORMS_ENGINE_IDENTITY_PRINT=1 prints the observed table instead of
// checking it.

#include "gui_forms/anatomy/types/anatomy_types.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/display/types/display_types.hpp"
#include "gui_forms/engine/part_raster/part_raster.hpp"
#include "gui_forms/engine/primitive_registry/primitive_registry.hpp"
#include "gui_forms/engine/theme_rules/theme_rules.hpp"
#include "gui_forms/engine/types/engine_types.hpp"
#include "gui_forms/surface_material/material_fill_layer/material_fill_layer.hpp"
#include "gui_forms/surface_material/painting/surface_material_painting.hpp"
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
#include "support/rgba_order.hpp"
#include "support/theme_definition_heap.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "gui_forms_engine_reflection_tests: " << message << '\n';
    std::exit(1);
}

bool print_mode() {
    const char* const value = std::getenv("GUI_FORMS_ENGINE_IDENTITY_PRINT");
    const bool printing = value != nullptr && std::strcmp(value, "1") == 0;
    return printing;
}

const EnginePrimitive& primitive_named(std::string_view name) {
    const EnginePrimitive* const primitive = builtin_primitive_registry().find(name);
    require(primitive != nullptr, "the built-in registry has the primitive");
    return *primitive;
}

// A raster the tests own: tightly packed rows of BGRA8 bytes.
struct TestRaster final {
    std::vector<std::uint8_t> bytes;
    DeviceExtent extent{};
    std::size_t row_bytes{};

    [[nodiscard]] std::array<std::uint8_t, 4> pixel(std::uint32_t column, std::uint32_t row) const {
        const std::size_t offset = static_cast<std::size_t>(row) * row_bytes + static_cast<std::size_t>(column) * 4U;
        // The raster is BGRA8; the value is red, green, blue, alpha.
        const std::array<std::uint8_t, 4> value{bytes[offset + 2U], bytes[offset + 1U], bytes[offset],
                                                bytes[offset + 3U]};
        return value;
    }
};

std::uint64_t raster_hash(std::span<const std::uint8_t> bytes) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const std::uint8_t byte : gui_forms::test_support::rgba_order(bytes)) {
        hash = hash ^ byte;
        hash = hash * 1099511628211ULL;
    }
    return hash;
}

// The material primitive's outsets, 8 logical units on every side.
constexpr double outset = 8.0;

// Draws `material` with the material primitive as a part of `logical` size
// at `scale`, into a destination grown by the material's outsets, lending
// `backplane` (borrowed; may be null) at a margin of 0.
TestRaster draw(const SurfaceMaterial& material, Size logical, double scale, const PartRaster* backplane = nullptr) {
    TestRaster raster{};
    const double width = std::ceil((logical.width + outset * 2.0) * scale);
    const double height = std::ceil((logical.height + outset * 2.0) * scale);
    raster.extent = DeviceExtent{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
    raster.row_bytes = static_cast<std::size_t>(raster.extent.width) * 4U;
    raster.bytes.assign(raster.row_bytes * raster.extent.height, 0xA5U);
    PrimitiveTarget target{};
    target.logical_size = logical;
    target.scale = DeviceScale{scale};
    target.material = &material;
    target.backplane = backplane;
    const EnginePrimitive& primitive = primitive_named("material");
    const PrimitiveParameterBlock parameters = default_parameters(primitive.descriptor());
    require(primitive.accepts(parameters, target), "the material primitive accepts the material");
    PrimitivePreparation preparation{};
    require(primitive.prepare(parameters, target, preparation) == PreparationOutcome::prepared, "the part prepares");
    const PartRasterView view{std::span<std::uint8_t>(raster.bytes.data(), raster.bytes.size()), raster.extent,
                              raster.row_bytes};
    require(primitive.rasterize(parameters, target, preparation, view) == RasterOutcome::painted,
            "the material paints");
    return raster;
}

// A face whose rows and columns all differ: a vertical gradient over a
// translucent horizontal one, so a mirrored line is told from its
// neighbours.
SurfaceMaterial striped_face() {
    SurfaceMaterial material{};
    material.fills.clear();
    const std::vector<GradientStop> down{GradientStop{0.0, Color::rgba(220, 40, 30)},
                                         GradientStop{1.0, Color::rgba(30, 60, 210)}};
    const std::vector<GradientStop> across{GradientStop{0.0, Color::rgba(255, 255, 255, 0)},
                                           GradientStop{1.0, Color::rgba(20, 200, 40, 160)}};
    material.fills.push_back(MaterialFillLayer::linear(Point{0.0, 0.0}, Point{0.0, 1.0}, down));
    material.fills.push_back(MaterialFillLayer::linear(Point{0.0, 0.0}, Point{1.0, 0.0}, across));
    return material;
}

constexpr Size face_size{40.0, 20.0};

// The destination's extent for a part of `logical` size at `scale`.
DeviceExtent destination_of(Size logical, double scale) {
    const double width = std::ceil((logical.width + outset * 2.0) * scale);
    const double height = std::ceil((logical.height + outset * 2.0) * scale);
    const DeviceExtent extent{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
    return extent;
}

// An opaque backplane image whose rows and columns all differ.
std::shared_ptr<PartRaster> patterned_backplane(DeviceExtent extent, double scale) {
    std::shared_ptr<PartRaster> raster = PartRaster::create(extent, DeviceScale{scale});
    require(raster != nullptr, "the backplane image is made");
    const PartRasterView view = (*raster).view();
    for (std::size_t row = 0U; row < extent.height; ++row) {
        for (std::size_t column = 0U; column < extent.width; ++column) {
            const std::size_t at = row * view.row_bytes + column * 4U;
            // BGRA8.
            view.pixels[at] = static_cast<std::uint8_t>((column * 5U + row * 3U) % 256U);
            view.pixels[at + 1U] = static_cast<std::uint8_t>((row * 13U) % 256U);
            view.pixels[at + 2U] = static_cast<std::uint8_t>((column * 7U) % 256U);
            view.pixels[at + 3U] = 255U;
        }
    }
    return raster;
}

// The pixel of `raster` at `column`, `row`: red, green, blue, alpha.
std::array<std::uint8_t, 4> pixel_of(const PartRaster& raster, std::size_t column, std::size_t row) {
    const std::size_t at = row * raster.row_bytes() + column * 4U;
    const std::span<const std::uint8_t> pixels = raster.pixels();
    const std::array<std::uint8_t, 4> value{pixels[at + 2U], pixels[at + 1U], pixels[at], pixels[at + 3U]};
    return value;
}

// -- what it draws ----------------------------------------------------------------

// At full opacity, line k past the gap is line k inside the box from the
// edge, byte for byte; the gap and the lines past the reach are as the
// material draws them without a reflection.
void mirrors_the_face() {
    const SurfaceMaterial plain = striped_face();
    SurfaceMaterial reflected = plain;
    reflected.reflection = MaterialReflection{MaterialEdge::bottom, 1.0, 5.0, 1.0, 1.0};
    const TestRaster without = draw(plain, face_size, 1.0);
    const TestRaster with = draw(reflected, face_size, 1.0);
    const std::uint32_t bottom = 8U + 20U;
    for (std::uint32_t column = 8U; column < 48U; ++column) {
        require(with.pixel(column, bottom) == without.pixel(column, bottom), "the gap row is as drawn");
        for (std::uint32_t line = 0U; line < 5U; ++line) {
            require(with.pixel(column, bottom + 1U + line) == with.pixel(column, bottom - 1U - line),
                    "a reflected row repeats the row as far inside the edge");
        }
        require(with.pixel(column, bottom + 6U) == without.pixel(column, bottom + 6U),
                "the row past the reach is as drawn");
    }
    // Outside the box's columns nothing is reflected.
    require(with.pixel(7U, bottom + 2U) == without.pixel(7U, bottom + 2U), "columns beside the box are as drawn");
    // Inside the box nothing changes.
    for (std::uint32_t row = 8U; row < bottom; ++row) {
        require(with.pixel(20U, row) == without.pixel(20U, row), "the face itself is unchanged");
    }

    // The right edge mirrors columns.
    SurfaceMaterial sideways = plain;
    sideways.reflection = MaterialReflection{MaterialEdge::right, 0.0, 4.0, 1.0, 1.0};
    const TestRaster right = draw(sideways, face_size, 1.0);
    const std::uint32_t edge = 8U + 40U;
    for (std::uint32_t line = 0U; line < 4U; ++line) {
        require(right.pixel(edge + line, 15U) == right.pixel(edge - 1U - line, 15U),
                "a reflected column repeats the column as far inside the edge");
    }
}

// The opacity is linear from the start next to the edge to the end at the
// far end, taken at each line's centre.
void fades_linearly() {
    SurfaceMaterial material{};
    material.fills.clear();
    material.fills.push_back(MaterialFillLayer::solid(Color::rgba(255, 255, 255)));
    material.reflection = MaterialReflection{MaterialEdge::top, 0.0, 6.0, 0.6, 0.0};
    const TestRaster raster = draw(material, face_size, 1.0);
    for (std::uint32_t line = 0U; line < 6U; ++line) {
        const double opacity = 0.6 - 0.6 * (static_cast<double>(line) + 0.5) / 6.0;
        const int expected = static_cast<int>(std::floor(opacity * 255.0 + 0.5));
        const std::array<std::uint8_t, 4> pixel = raster.pixel(20U, 8U - 1U - line);
        require(std::abs(static_cast<int>(pixel[3]) - expected) <= 1, "each line's opacity follows the ramp");
        require(pixel[0] == pixel[3], "a white face reflects premultiplied white");
    }
    require(raster.pixel(20U, 1U)[3] == 0U, "past the reach nothing is drawn");
}

// The reflection lies beneath what the face draws past the edge: an under
// glow there keeps its own colour over the reflection.
void lies_beneath_the_glow() {
    SurfaceMaterial glowing = striped_face();
    MaterialGlow glow{};
    glow.color = Color::rgba(255, 236, 160, 200);
    glow.blur_radius = 2.0;
    glow.spread = 1.0;
    glowing.glows.push_back(glow);
    SurfaceMaterial reflected = glowing;
    reflected.reflection = MaterialReflection{MaterialEdge::bottom, 0.0, 6.0, 0.8, 0.8};
    const TestRaster glow_only = draw(glowing, face_size, 1.0);
    const TestRaster both = draw(reflected, face_size, 1.0);
    const std::array<std::uint8_t, 4> under = glow_only.pixel(20U, 29U);
    const std::array<std::uint8_t, 4> drawn = both.pixel(20U, 29U);
    require(under[3] > 0U, "the glow reaches the first reflected row");
    require(drawn[3] >= under[3], "the reflection adds coverage beneath the glow");
    // Source-over of the glow (premultiplied) onto the reflection keeps at
    // least the glow's own premultiplied red.
    require(drawn[0] >= under[0], "the glow stays over the reflection");
}

// -- validation and outsets -------------------------------------------------------

void is_validated() {
    SurfaceMaterial material{};
    material.reflection = MaterialReflection{};
    require(valid_surface_material(material), "the default reflection is valid");
    MaterialReflection& reflection = *material.reflection;
    reflection.gap = 3.0;
    reflection.reach = 5.0;
    require(valid_surface_material(material), "gap and reach may meet the cap");
    reflection.reach = 5.5;
    require(!valid_surface_material(material), "gap and reach past the cap are refused");
    reflection.gap = 0.0;
    reflection.reach = 0.0;
    require(!valid_surface_material(material), "a reflection reaches past its edge");
    reflection.reach = 4.0;
    reflection.gap = -1.0;
    require(!valid_surface_material(material), "a negative gap is refused");
    reflection.gap = 0.0;
    reflection.start_opacity = 1.5;
    require(!valid_surface_material(material), "an opacity above 1 is refused");
    reflection.start_opacity = std::numeric_limits<double>::quiet_NaN();
    require(!valid_surface_material(material), "a NaN opacity is refused");
    reflection.start_opacity = 0.5;
    reflection.edge = static_cast<MaterialEdge>(9);
    require(!valid_surface_material(material), "an unknown edge is refused");

    SurfaceMaterial reaching{};
    reaching.reflection = MaterialReflection{MaterialEdge::left, 2.0, 5.0, 0.5, 0.0};
    const Insets outsets = surface_material_visual_outsets(reaching);
    require(outsets.left == 7.0 && outsets.top == 0.0 && outsets.right == 0.0 && outsets.bottom == 0.0,
            "the reflection reaches past its own edge alone");
    require(!surface_material_opaque(reaching), "a reflecting material is not opaque");
}

// -- reference images -------------------------------------------------------------

// top, right, bottom and left, each at scale 1 then 2.
// The face source's eight (each edge at scale 1 and 2), then the backplane
// source's, over patterned_backplane.
constexpr std::array<std::uint64_t, 16> expected_hashes{
    0x14f6df20874ffb59ULL, 0xc0016f6217ae8cb1ULL, 0xcf14e0c08c5a00edULL, 0x609c71740c7337bdULL,
    0xed29a0a1274f5c72ULL, 0x8d671a9348b478e9ULL, 0xcdb7b05fb073f935ULL, 0xa31f888b4da48ef1ULL,
    0x3cfe0a7f4484184bULL, 0x77dd80e5b6105d0fULL, 0xad821fc38c59fceaULL, 0xf341facffc10fb1cULL,
    0x7429dee566514386ULL, 0x4c532192dccb4dfaULL, 0xf42982ffd70af2f4ULL, 0xd4bd48380959c0d5ULL,
};

void matches_references() {
    const std::array<MaterialEdge, 4> edges{MaterialEdge::top, MaterialEdge::right, MaterialEdge::bottom,
                                            MaterialEdge::left};
    const std::array<double, 2> scales{1.0, 2.0};
    const std::array<const char*, 4> names{"top", "right", "bottom", "left"};
    const std::array<MaterialReflectionSource, 2> sources{MaterialReflectionSource::face,
                                                          MaterialReflectionSource::backplane};
    std::size_t index = 0U;
    bool matched = true;
    for (const MaterialReflectionSource source : sources) {
        for (std::size_t edge = 0U; edge < edges.size(); ++edge) {
            for (const double scale : scales) {
                SurfaceMaterial material = striped_face();
                material.corner_radius = 4.0;
                material.reflection = MaterialReflection{edges[edge], 1.0, 6.0, 0.6, 0.05, source};
                const bool from_backplane = source == MaterialReflectionSource::backplane;
                const std::shared_ptr<PartRaster> backplane =
                    from_backplane ? patterned_backplane(destination_of(face_size, scale), scale) : nullptr;
                const TestRaster raster = draw(material, face_size, scale, backplane.get());
                const std::uint64_t hash = raster_hash(raster.bytes);
                if (print_mode()) {
                    std::cout << "    " << (from_backplane ? "backplane " : "face ") << names[edge] << " x" << scale
                              << " 0x" << std::hex << hash << std::dec << "ULL\n";
                }
                if (hash != expected_hashes[index]) matched = false;
                index = index + 1U;
            }
        }
    }
    if (!print_mode()) require(matched, "each edge's reflection has the same bits everywhere");
}

// -- the backplane source ------------------------------------------------------------

// At full opacity, line k past the edge is the backplane's line k inside the
// box from the edge, byte for byte, beneath an outset the face leaves clear.
// Without a backplane, or with one of another size, nothing is reflected.
void mirrors_the_backplane() {
    const SurfaceMaterial plain = striped_face();
    SurfaceMaterial reflected = plain;
    reflected.reflection =
        MaterialReflection{MaterialEdge::bottom, 0.0, 6.0, 1.0, 1.0, MaterialReflectionSource::backplane};
    const std::shared_ptr<PartRaster> backplane = patterned_backplane(destination_of(face_size, 1.0), 1.0);
    const TestRaster drawn = draw(reflected, face_size, 1.0, backplane.get());
    // The box lies at 8 to 48 across and 8 to 28 down.
    bool mirrored = true;
    for (std::uint32_t line = 0U; line < 6U; ++line) {
        for (std::uint32_t column = 8U; column < 48U; ++column) {
            if (drawn.pixel(column, 28U + line) != pixel_of(*backplane, column, 27U - line)) mirrored = false;
        }
    }
    require(mirrored, "each line past the edge is the backplane's line inside it");
    require(drawn.pixel(7U, 30U) == draw(plain, face_size, 1.0).pixel(7U, 30U), "nothing is drawn beside the box");
    const TestRaster unlent = draw(reflected, face_size, 1.0);
    require(unlent.bytes == draw(plain, face_size, 1.0).bytes, "without a backplane nothing is reflected");
    const std::shared_ptr<PartRaster> other = patterned_backplane(DeviceExtent{20U, 20U}, 1.0);
    const TestRaster mismatched = draw(reflected, face_size, 1.0, other.get());
    require(mismatched.bytes == unlent.bytes, "a backplane of another size is not read");
}

// -- in a window: omitted under reduced transparency and high contrast ------------

// A painter that keeps the part rasters composited and draws nothing.
class Watch final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override {}
    void fill_rounded_rect(Rect, double, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}
    void begin_group(const PaintGroup&) override {}
    void end_group() override {}
    void draw_part_raster(const std::shared_ptr<const PartRaster>& raster, Point, double) override {
        rasters.push_back(raster);
    }
    std::vector<std::shared_ptr<const PartRaster>> rasters;
};

// One face drawn by the material primitive at 40 by 20.
class Face final : public Control {
public:
    explicit Face(StableId stable_id) : Control(std::move(stable_id)) {}
    void on_paint(Painter& painter, Rect) override {
        PartPaint face{};
        face.role = PartRole::face;
        face.family = ControlVisualRole::button;
        face.bounds = Rect{20.0, 20.0, 40.0, 20.0};
        face.key = PartKey{"face", {}, {}};
        paint_part(painter, *this, face);
    }
};

// The professional theme with the material primitive on faces and, when
// `reflecting`, a reflection below every ordinary button material.
std::unique_ptr<ThemeDefinition> face_theme(bool reflecting) {
    std::unique_ptr<ThemeDefinition> held = test_support::professional_definition();
    ThemeDefinition& definition = *held;
    definition.id = reflecting ? "engine-reflection" : "engine-reflection-plain";
    ThemeRule face{};
    face.label = "face";
    face.selector.role = PartRole::face;
    face.declarations.push_back(RuleDeclaration::primitive("material"));
    definition.rules.rules.push_back(face);
    if (reflecting) {
        ControlRoleRecipes& button = definition.roles[static_cast<std::size_t>(ControlVisualRole::button)];
        for (ControlVisualRecipe& recipe : button.ordinary) {
            recipe.material.reflection = MaterialReflection{MaterialEdge::bottom, 1.0, 6.0, 0.6, 0.0};
        }
    }
    return held;
}

// The bytes of the face's raster as `settings` paint it.
std::uint64_t face_hash(bool reflecting, bool reduced_transparency, bool high_contrast) {
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("root"));
    const std::shared_ptr<Face> face = make_control<Face>(StableId("face"));
    (*face).set_requested_bounds(Rect{0.0, 0.0, 100.0, 80.0});
    (*root).add_child(face);
    Window window(root, Size{100.0, 80.0});
    window.set_theme(Theme::create(*face_theme(reflecting)));
    PresentationSettings settings = window.presentation_settings();
    settings.reduced_transparency = reduced_transparency;
    settings.high_contrast = high_contrast;
    window.set_presentation_settings(settings);
    window.perform_layout();
    Watch watch{};
    window.paint(watch);
    // Under high contrast the face may be left to the built-in recipe,
    // which composites no raster: 0 stands for that.
    if (watch.rasters.empty()) return 0U;
    const std::shared_ptr<const PartRaster>& raster = watch.rasters.back();
    const std::uint64_t hash = raster_hash((*raster).pixels());
    return hash;
}

// A backplane-plane control drawing an engine part raster of one colour
// over its bounds, which the backplane image composites.
class Backdrop final : public Control {
public:
    explicit Backdrop(StableId stable_id) : Control(std::move(stable_id)) { set_paint_plane(PaintPlane::backplane); }
    void on_paint(Painter& painter, Rect) override {
        if (colour) painter.draw_part_raster(colour, Point{0.0, 0.0}, 1.0);
    }
    std::shared_ptr<const PartRaster> colour;
};

// An opaque raster of `extent`, of one colour (red, green, blue).
std::shared_ptr<PartRaster> solid_raster(DeviceExtent extent, std::array<std::uint8_t, 3> colour) {
    std::shared_ptr<PartRaster> raster = PartRaster::create(extent, DeviceScale{1.0});
    require(raster != nullptr, "the raster is made");
    const PartRasterView view = (*raster).view();
    for (std::size_t at = 0U; at + 3U < view.pixels.size(); at = at + 4U) {
        view.pixels[at] = colour[2];
        view.pixels[at + 1U] = colour[1];
        view.pixels[at + 2U] = colour[0];
        view.pixels[at + 3U] = 255U;
    }
    return raster;
}

struct BackplaneFixture final {
    std::shared_ptr<Panel> root;
    std::shared_ptr<Backdrop> backdrop;
    std::shared_ptr<Face> face;
    std::unique_ptr<Window> window;
    Watch watch{};
};

// The professional theme with the material primitive on faces and a
// reflection of the backplane below every ordinary button material.
std::unique_ptr<ThemeDefinition> backplane_theme() {
    std::unique_ptr<ThemeDefinition> held = face_theme(false);
    ThemeDefinition& definition = *held;
    definition.id = "engine-reflection-backplane";
    ControlRoleRecipes& button = definition.roles[static_cast<std::size_t>(ControlVisualRole::button)];
    for (ControlVisualRecipe& recipe : button.ordinary) {
        recipe.material.reflection =
            MaterialReflection{MaterialEdge::bottom, 0.0, 6.0, 1.0, 1.0, MaterialReflectionSource::backplane};
    }
    return held;
}

// The backdrop covers the whole 100 by 80 window in red; the face lies at
// 20 to 60 across and 20 to 40 down.
std::unique_ptr<BackplaneFixture> make_backplane_fixture() {
    std::unique_ptr<BackplaneFixture> fixture = std::make_unique<BackplaneFixture>();
    BackplaneFixture& made = *fixture;
    made.root = make_control<Panel>(StableId("root"));
    made.backdrop = make_control<Backdrop>(StableId("backdrop"));
    (*made.backdrop).set_requested_bounds(Rect{0.0, 0.0, 100.0, 80.0});
    (*made.backdrop).colour = solid_raster(DeviceExtent{100U, 80U}, {255U, 0U, 0U});
    made.face = make_control<Face>(StableId("face"));
    (*made.face).set_requested_bounds(Rect{0.0, 0.0, 100.0, 80.0});
    (*made.root).add_child(made.backdrop);
    (*made.root).add_child(made.face);
    made.window = std::make_unique<Window>(made.root, Size{100.0, 80.0});
    Window& window = *made.window;
    window.set_theme(Theme::create(*backplane_theme()));
    window.perform_layout();
    window.paint(made.watch);
    static_cast<void>(window.take_damage());
    return fixture;
}

// The face's raster last composited: the one of its destination's size.
std::shared_ptr<const PartRaster> last_face(const BackplaneFixture& fixture) {
    const DeviceExtent extent = destination_of(face_size, 1.0);
    for (std::size_t index = fixture.watch.rasters.size(); index > 0U; --index) {
        const std::shared_ptr<const PartRaster>& raster = fixture.watch.rasters[index - 1U];
        if ((*raster).extent().width == extent.width && (*raster).extent().height == extent.height) return raster;
    }
    return nullptr;
}

void repaint(BackplaneFixture& fixture) {
    Window& window = *fixture.window;
    const DamageRegion damage = window.take_damage();
    if (!damage.empty()) window.paint(fixture.watch, damage.bounds());
}

void follows_backplane_damage() {
    std::unique_ptr<BackplaneFixture> fixture = make_backplane_fixture();
    BackplaneFixture& made = *fixture;
    const std::shared_ptr<const PartRaster> first = last_face(made);
    require(first != nullptr, "the face is drawn by the engine");
    // Destination row 30 is the reflection's third line, column 20 inside
    // the box's width.
    const std::array<std::uint8_t, 4> over_red = pixel_of(*first, 20U, 30U);
    require(over_red[0] > over_red[1] + 100U && over_red[0] > over_red[2] + 100U,
            "the reflection shows the backplane under the face");
    const std::uint64_t bakes = (*made.window).engine_parts().cache_statistics().bakes;
    // Backplane damage away from the face leaves its raster as it was.
    (*made.backdrop).invalidate(Rect{80.0, 60.0, 10.0, 10.0});
    repaint(made);
    require((*made.window).engine_parts().cache_statistics().bakes == bakes,
            "backplane damage away from the face does not rebake it");
    // Backplane damage under the face rebakes it over the new backplane.
    (*made.backdrop).colour = solid_raster(DeviceExtent{100U, 80U}, {0U, 0U, 255U});
    (*made.backdrop).invalidate(Rect{20.0, 20.0, 40.0, 20.0});
    repaint(made);
    require((*made.window).engine_parts().cache_statistics().bakes > bakes,
            "backplane damage under the face rebakes it");
    const std::shared_ptr<const PartRaster> after = last_face(made);
    const std::array<std::uint8_t, 4> over_blue = pixel_of(*after, 20U, 30U);
    require(over_blue[2] > over_blue[0] + 100U, "the reflection shows the changed backplane");
    // Under reduced transparency it is omitted, so it reads no backplane.
    PresentationSettings settings = (*made.window).presentation_settings();
    settings.reduced_transparency = true;
    (*made.window).set_presentation_settings(settings);
    repaint(made);
    const std::shared_ptr<const PartRaster> reduced = last_face(made);
    require(reduced != nullptr && pixel_of(*reduced, 20U, 30U)[3] < 255U,
            "reduced transparency omits the backplane reflection");
}

void omitted_where_transparency_is_reduced() {
    const std::uint64_t reflected = face_hash(true, false, false);
    const std::uint64_t plain = face_hash(false, false, false);
    require(reflected != 0U && plain != 0U, "the face is drawn by the engine");
    require(reflected != plain, "a reflecting theme draws the reflection");
    require(face_hash(true, true, false) != 0U, "under reduced transparency the engine still draws the face");
    require(face_hash(true, true, false) == face_hash(false, true, false),
            "reduced transparency omits the reflection");
    require(face_hash(true, false, true) == face_hash(false, false, true), "high contrast omits the reflection");
}

} // namespace

int main() {
    mirrors_the_face();
    fades_linearly();
    lies_beneath_the_glow();
    is_validated();
    matches_references();
    mirrors_the_backplane();
    omitted_where_transparency_is_reduced();
    follows_backplane_damage();
    std::cout << "engine reflection tests passed\n";
    return 0;
}
