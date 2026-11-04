// Stage T5, C7 (notes:planning/stages/T5-compositor/PLAN.md §4.4; owner
// T5-Q14 to T5-Q16): the material's reflection of its own finished face.
// Checked by what it draws at chosen pixels against the same material
// without a reflection, by the material's validation and outsets, by its
// omission under reduced transparency and high contrast in a window, and by
// reference images: FNV-1a hashes of each raster's rows in RGBA order, for
// each edge at scale 1 and 2, checked on every CI platform.
// GUI_FORMS_ENGINE_IDENTITY_PRINT=1 prints the observed table instead of
// checking it.

#include "gui_forms/gui_forms.hpp"
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
// at `scale`, into a destination grown by the material's outsets.
TestRaster draw(const SurfaceMaterial& material, Size logical, double scale) {
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
constexpr std::array<std::uint64_t, 8> expected_hashes{
    0x14f6df20874ffb59ULL, 0xc0016f6217ae8cb1ULL, 0xcf14e0c08c5a00edULL, 0x609c71740c7337bdULL,
    0xed29a0a1274f5c72ULL, 0x8d671a9348b478e9ULL, 0xcdb7b05fb073f935ULL, 0xa31f888b4da48ef1ULL,
};

void matches_references() {
    const std::array<MaterialEdge, 4> edges{MaterialEdge::top, MaterialEdge::right, MaterialEdge::bottom,
                                            MaterialEdge::left};
    const std::array<double, 2> scales{1.0, 2.0};
    const std::array<const char*, 4> names{"top", "right", "bottom", "left"};
    std::size_t index = 0U;
    bool matched = true;
    for (std::size_t edge = 0U; edge < edges.size(); ++edge) {
        for (const double scale : scales) {
            SurfaceMaterial material = striped_face();
            material.corner_radius = 4.0;
            material.reflection = MaterialReflection{edges[edge], 1.0, 6.0, 0.6, 0.05};
            const TestRaster raster = draw(material, face_size, scale);
            const std::uint64_t hash = raster_hash(raster.bytes);
            if (print_mode()) {
                std::cout << "    " << names[edge] << " x" << scale << " 0x" << std::hex << hash << std::dec
                          << "ULL\n";
            }
            if (hash != expected_hashes[index]) matched = false;
            index = index + 1U;
        }
    }
    if (!print_mode()) require(matched, "each edge's reflection has the same bits everywhere");
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
    omitted_where_transparency_is_reduced();
    std::cout << "engine reflection tests passed\n";
    return 0;
}
