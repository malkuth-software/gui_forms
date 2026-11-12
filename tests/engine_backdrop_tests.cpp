// The engine's join to the window's backdrop (stage T5, phase C5;
// notes:planning/stages/T5-compositor/PLAN.md §4.1, §4.2 and §4.5):
//
// - the theme's window backdrop record (owner decision T5-Q8, option 1):
//   its request, which a window makes until its application makes one, and
//   the backplane base the window draws over each granted backdrop;
// - glass over that base under each grant, with a reference hash per grant;
// - a grant change makes the backplane image once more and bakes again only
//   the parts that sampled it;
// - reduced transparency flattens glass (owner decision T5-Q16) and high
//   contrast drops it.
//
// Headless and renderer-neutral: the window paints into a painter that keeps
// what it is asked to draw. Run with --print to print the reference hashes
// instead of checking them.

#include "gui_forms/anatomy/types/anatomy_types.hpp"
#include "gui_forms/binding/property_type/property_type.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/display/types/display_types.hpp"
#include "gui_forms/engine/part_raster/part_raster.hpp"
#include "gui_forms/engine/part_table/engine_part_table.hpp"
#include "gui_forms/engine/theme_rules/theme_rules.hpp"
#include "gui_forms/engine/types/engine_types.hpp"
#include "gui_forms/host/compositor/window_backdrop.hpp"
#include "gui_forms/plots/series_column/series_column.hpp"
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

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "gui_forms_engine_backdrop_tests: " << message << '\n';
    std::exit(1);
}

std::uint64_t hash_bytes(std::span<const std::uint8_t> bytes) {
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (const std::uint8_t byte : gui_forms::test_support::rgba_order(bytes)) {
        hash = hash ^ byte;
        hash = hash * 0x100000001b3ULL;
    }
    return hash;
}

std::shared_ptr<PartRaster> solid_raster(DeviceExtent extent, std::array<std::uint8_t, 4> colour) {
    std::shared_ptr<PartRaster> raster = PartRaster::create(extent, DeviceScale{1.0});
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

// A painter that keeps the part rasters composited, the colours filled and
// the paint groups begun, in order, and draws nothing.
class Watch final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color color) override { fills.push_back(color); }
    void fill_rounded_rect(Rect, double, Color color) override { fills.push_back(color); }
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}
    void begin_group(const PaintGroup& group) override { groups.push_back(group.composite); }
    void end_group() override {}
    void draw_part_raster(const std::shared_ptr<const PartRaster>& raster, Point, double) override {
        rasters.push_back(raster);
    }
    void clear() {
        fills.clear();
        groups.clear();
        rasters.clear();
    }
    std::vector<Color> fills;
    std::vector<PaintGroupComposite> groups;
    std::vector<std::shared_ptr<const PartRaster>> rasters;
};

// A backplane-plane control drawing a red engine part raster over its bounds.
class Backdrop final : public Control {
public:
    explicit Backdrop(StableId stable_id) : Control(std::move(stable_id)) { set_paint_plane(PaintPlane::backplane); }
    void on_paint(Painter& painter, Rect) override {
        if (red) painter.draw_part_raster(red, Point{0.0, 0.0}, 1.0);
    }
    std::shared_ptr<const PartRaster> red;
};

// The window's root: it draws nothing, so that what the glass samples is
// the backplane base and the Backdrop alone. (A Panel root's face is an
// engine part raster since T4 138a3f3c, and the backplane composites part
// rasters: an opaque face would cover the base the glass is tested over.)
class Root final : public Control {
public:
    explicit Root(StableId stable_id) : Control(std::move(stable_id)) {}
    void on_paint(Painter&, Rect) override {}
};

// A control-plane control with one glass region and one face drawn by the
// material primitive, which never samples the backplane.
class Pane final : public Control {
public:
    explicit Pane(StableId stable_id) : Control(std::move(stable_id)) {}
    void on_paint(Painter& painter, Rect) override {
        PartPaint glass{};
        glass.role = PartRole::region;
        glass.family = ControlVisualRole::card;
        glass.bounds = Rect{20.0, 20.0, 100.0, 30.0};
        glass.corner_radius = 6.0;
        glass.key = PartKey{"glass", {}, {}};
        paint_part(painter, *this, glass);
        PartPaint face{};
        face.role = PartRole::face;
        face.family = ControlVisualRole::button;
        face.bounds = Rect{140.0, 100.0, 60.0, 24.0};
        face.key = PartKey{"face", {}, {}};
        paint_part(painter, *this, face);
    }
};

// The window recipe's colour, the window material's tint and the cleared
// base, as straight-alpha colours.
constexpr Color recipe_colour = Color::rgba(236, 236, 236, 255);
constexpr Color material_tint = Color::rgba(240, 240, 250, 96);
constexpr Color cleared = Color::rgba(0, 0, 0, 0);

SurfaceMaterial solid_material(Color colour) {
    SurfaceMaterial material{};
    material.fills.clear();
    material.fills.push_back(MaterialFillLayer::solid(colour));
    return material;
}

// The professional theme with a plain window recipe, glass on regions, the
// material primitive on faces, and a window backdrop record: the window
// material with a transparent fallback, a tint over the material and clear
// over transparent.
std::unique_ptr<ThemeDefinition> backdrop_theme() {
    std::unique_ptr<ThemeDefinition> held = test_support::professional_definition();
    ThemeDefinition& definition = *held;
    definition.id = "engine-backdrop";
    ControlRoleRecipes& window = definition.roles[static_cast<std::size_t>(ControlVisualRole::window)];
    for (ControlVisualRecipe& recipe : window.ordinary) recipe.material = solid_material(recipe_colour);
    ThemeRule glass{};
    glass.label = "glass";
    glass.selector.role = PartRole::region;
    glass.declarations.push_back(RuleDeclaration::primitive("glass"));
    definition.rules.rules.push_back(glass);
    ThemeRule face{};
    face.label = "face";
    face.selector.role = PartRole::face;
    face.declarations.push_back(RuleDeclaration::primitive("material"));
    definition.rules.rules.push_back(face);
    definition.window_backdrop.request =
        WindowBackdropRequest::with_fallback(WindowBackdrop::window_material, WindowBackdrop::transparent);
    definition.window_backdrop.bases.push_back(ThemeBackdropBase{WindowBackdrop::window_material,
                                                                 solid_material(material_tint)});
    definition.window_backdrop.bases.push_back(ThemeBackdropBase{WindowBackdrop::transparent, solid_material(cleared)});
    return held;
}

CompositorState every_backdrop() {
    CompositorState state{};
    state.generation = CompositorGeneration{1U};
    state.compositing = CompositorPresence::present;
    for (std::size_t index = 0U; index < window_backdrop_count; ++index) {
        state.support[index] = WindowBackdropSupport::supported;
    }
    return state;
}

struct Fixture final {
    std::shared_ptr<Root> root;
    std::shared_ptr<Backdrop> backdrop;
    std::shared_ptr<Pane> pane;
    std::unique_ptr<Window> window;
    Watch painter{};
};

// The backdrop covers window x 60 to 180; the glass lies at 20 to 120.
std::unique_ptr<Fixture> make_fixture() {
    std::unique_ptr<Fixture> fixture = std::make_unique<Fixture>();
    Fixture& made = *fixture;
    made.root = make_control<Root>(StableId("root"));
    made.backdrop = make_control<Backdrop>(StableId("backdrop"));
    (*made.backdrop).set_requested_bounds(Rect{60.0, 0.0, 120.0, 160.0});
    (*made.backdrop).red = solid_raster(DeviceExtent{120U, 160U}, {255U, 0U, 0U, 255U});
    made.pane = make_control<Pane>(StableId("pane"));
    (*made.pane).set_requested_bounds(Rect{0.0, 0.0, 240.0, 160.0});
    (*made.root).add_child(made.backdrop);
    (*made.root).add_child(made.pane);
    made.window = std::make_unique<Window>(made.root, Size{240.0, 160.0});
    Window& window = *made.window;
    window.set_theme(Theme::create(*backdrop_theme()));
    require(window.declare_non_opaque(), "the window is declared non-opaque");
    window.set_compositor_state(every_backdrop(), false);
    window.perform_layout();
    window.paint(made.painter);
    static_cast<void>(window.take_damage());
    return fixture;
}

void repaint(Fixture& fixture) {
    Window& window = *fixture.window;
    fixture.painter.clear();
    const DamageRegion damage = window.take_damage();
    if (!damage.empty()) window.paint(fixture.painter, damage.bounds());
}

// The glass raster last composited: the one of the glass's size.
std::shared_ptr<const PartRaster> last_glass(const Fixture& fixture) {
    for (std::size_t index = fixture.painter.rasters.size(); index > 0U; --index) {
        const std::shared_ptr<const PartRaster>& raster = fixture.painter.rasters[index - 1U];
        if ((*raster).extent().width == 100U && (*raster).extent().height == 30U) return raster;
    }
    return nullptr;
}

// The window's frame starts with its backplane base: under a non-opaque
// grant in a copy group (C1), filled with the base's colour.
void check_base(const Fixture& fixture, bool cleared_start, Color base, const char* message) {
    const Watch& watch = fixture.painter;
    require(!watch.fills.empty(), "the window fills its backplane");
    const bool copy_first = !watch.groups.empty() && watch.groups[0] == PaintGroupComposite::copy;
    require(copy_first == cleared_start, "a non-opaque grant starts the frame clear");
    require(watch.fills[0] == base, message);
}

// -- the theme's record ---------------------------------------------------------

void the_record_is_validated() {
    std::unique_ptr<ThemeDefinition> opaque_base = backdrop_theme();
    (*opaque_base).window_backdrop.bases.push_back(ThemeBackdropBase{WindowBackdrop::opaque,
                                                                     solid_material(recipe_colour)});
    bool refused = false;
    try {
        static_cast<void>(Theme::create(*opaque_base));
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused, "a base for opaque is refused: the window recipe is that base");
    std::unique_ptr<ThemeDefinition> bad_request = backdrop_theme();
    (*bad_request).window_backdrop.request.length = 0U;
    refused = false;
    try {
        static_cast<void>(Theme::create(*bad_request));
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused, "an invalid request is refused");
    std::unique_ptr<ThemeDefinition> empty_base = backdrop_theme();
    SurfaceMaterial empty{};
    empty.fills.clear();
    (*empty_base).window_backdrop.bases[1].material = empty;
    refused = false;
    try {
        static_cast<void>(Theme::create(*empty_base));
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused, "an invalid base material is refused");
    std::unique_ptr<ThemeDefinition> twice = backdrop_theme();
    (*twice).window_backdrop.bases.push_back(ThemeBackdropBase{WindowBackdrop::transparent, solid_material(cleared)});
    refused = false;
    try {
        static_cast<void>(Theme::create(*twice));
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused, "a second base for one backdrop is refused");
    const std::shared_ptr<const Theme> plain = Theme::create(*test_support::professional_definition());
    require((*plain).definition().window_backdrop.request == WindowBackdropRequest{},
            "a theme that says nothing asks for opaque");
}

// -- the window and its glass by grant --------------------------------------------

// Glass over each grant's base, at scale 1; recorded on Windows x64 and
// checked on every CI platform.
constexpr std::uint64_t expected_material_glass = 0xfbba146fe1250931ULL;
constexpr std::uint64_t expected_transparent_glass = 0x23ad431cdfd8b14dULL;
constexpr std::uint64_t expected_opaque_glass = 0xc94637b61bd6b5d9ULL;
constexpr std::uint64_t expected_flattened_glass = 0xb1b9fad963ba8839ULL;

struct GlassHashes final {
    std::uint64_t material{};
    std::uint64_t transparent{};
    std::uint64_t opaque{};
    std::uint64_t flattened{};
};

void bases_by_grant(GlassHashes& hashes) {
    std::unique_ptr<Fixture> fixture = make_fixture();
    Fixture& made = *fixture;
    Window& window = *made.window;
    // The theme's request, granted: the window material, with the tint.
    require(window.backdrop_request() == (*backdrop_theme()).window_backdrop.request,
            "the window makes its theme's request");
    require(window.backdrop_grant().granted == WindowBackdrop::window_material, "the window material is granted");
    check_base(made, true, material_tint, "the window draws the material's base");
    const std::shared_ptr<const PartRaster> over_material = last_glass(made);
    require(over_material != nullptr, "the glass is composited over the material's base");
    hashes.material = hash_bytes((*over_material).pixels());
    // Over the material's partly clear tint the glass is partly clear too:
    // the compositor's material shows through it.
    const std::array<std::uint8_t, 4> beside = pixel_of(*over_material, 10U, 15U);
    require(beside[3] < 255U, "glass over a partly clear base is partly clear");

    // The application's request replaces the theme's: transparent, clear.
    const RasterCacheStatistics before = window.engine_parts().cache_statistics();
    const WindowBackdropGrant transparent = window.request_backdrop(WindowBackdropRequest::preferring(WindowBackdrop::transparent));
    require(transparent.granted == WindowBackdrop::transparent, "transparent is granted");
    repaint(made);
    check_base(made, true, cleared, "the window draws transparent's clear base");
    const RasterCacheStatistics after = window.engine_parts().cache_statistics();
    require(after.backplane_bakes == before.backplane_bakes + 1U, "a grant change makes the backplane image once");
    require(after.bakes == before.bakes + 1U, "a grant change bakes again only the part that sampled the backplane");
    const std::shared_ptr<const PartRaster> over_clear = last_glass(made);
    require(over_clear != nullptr, "the glass is composited over the clear base");
    hashes.transparent = hash_bytes((*over_clear).pixels());
    // Over the clear base, away from the backdrop's red, glass is its tint.
    const std::array<std::uint8_t, 4> clear_beside = pixel_of(*over_clear, 10U, 15U);
    require(clear_beside == (std::array<std::uint8_t, 4>{64U, 64U, 64U, 64U}), "glass over a clear base is its tint there");

    // Opaque: the window recipe, drawn as before stage T5.
    static_cast<void>(window.request_backdrop(WindowBackdropRequest{}));
    require(window.backdrop_grant().granted == WindowBackdrop::opaque, "opaque is granted");
    repaint(made);
    check_base(made, false, recipe_colour, "an opaque window draws its recipe");
    const std::shared_ptr<const PartRaster> over_recipe = last_glass(made);
    require(over_recipe != nullptr, "the glass is composited over the recipe");
    hashes.opaque = hash_bytes((*over_recipe).pixels());
    require((*over_recipe).pixels().size() == (*over_clear).pixels().size(), "the glass keeps its size");
    require(std::memcmp((*over_recipe).pixels().data(), (*over_clear).pixels().data(),
                        (*over_recipe).pixels().size()) != 0,
            "the glass shows the base it lies over");

    // A theme without a base for the granted backdrop keeps the recipe.
    std::unique_ptr<ThemeDefinition> no_bases = backdrop_theme();
    (*no_bases).window_backdrop.bases.pop_back();
    window.set_theme(Theme::create(*no_bases));
    static_cast<void>(window.request_backdrop(WindowBackdropRequest::preferring(WindowBackdrop::transparent)));
    repaint(made);
    check_base(made, true, recipe_colour, "a backdrop the theme gives no base draws the recipe");
}

// The application's request stands over a theme change; the theme's
// follows the theme.
void requests_follow_the_theme() {
    std::unique_ptr<Fixture> fixture = make_fixture();
    Window& window = *(*fixture).window;
    std::unique_ptr<ThemeDefinition> transient = backdrop_theme();
    (*transient).id = "engine-backdrop-transient";
    (*transient).window_backdrop.request = WindowBackdropRequest::preferring(WindowBackdrop::transient_material);
    window.set_theme(Theme::create(*transient));
    require(window.backdrop_grant().granted == WindowBackdrop::transient_material,
            "a window following its theme takes the new theme's request");
    static_cast<void>(window.request_backdrop(WindowBackdropRequest::preferring(WindowBackdrop::transparent)));
    window.set_theme(Theme::create(*backdrop_theme()));
    require(window.backdrop_grant().granted == WindowBackdrop::transparent,
            "the application's request stands over a theme change");
}

// -- the user's settings ----------------------------------------------------------

void reduced_transparency_flattens_glass(GlassHashes& hashes) {
    std::unique_ptr<Fixture> fixture = make_fixture();
    Fixture& made = *fixture;
    Window& window = *made.window;
    PresentationSettings settings = window.presentation_settings();
    settings.reduced_transparency = true;
    const RasterCacheStatistics before = window.engine_parts().cache_statistics();
    window.set_presentation_settings(settings);
    require(window.backdrop_grant().granted == WindowBackdrop::opaque &&
                window.backdrop_grant().reason == WindowBackdropReason::reduced_transparency,
            "reduced transparency withdraws the backdrop");
    repaint(made);
    check_base(made, false, recipe_colour, "a withdrawn backdrop draws the recipe");
    const RasterCacheStatistics after = window.engine_parts().cache_statistics();
    require(after.bakes == before.bakes + 1U, "reduced transparency bakes again only the glass");
    const std::shared_ptr<const PartRaster> flattened = last_glass(made);
    require(flattened != nullptr, "flattened glass is composited");
    hashes.flattened = hash_bytes((*flattened).pixels());
    // Unblurred: the backdrop's edge at glass column 40 stays sharp, and
    // the glass is opaque inside its corners.
    const std::array<std::uint8_t, 4> left = pixel_of(*flattened, 39U, 15U);
    const std::array<std::uint8_t, 4> right = pixel_of(*flattened, 40U, 15U);
    require(left[3] == 255U && right[3] == 255U, "flattened glass is opaque");
    require(left[1] > right[1] + 100U, "flattened glass is not blurred: the edge beneath it stays sharp");
    // The tint over the recipe colour, exactly.
    const int tinted = (64 * 255 + (255 - 64) * 236 + 127) / 255;
    require(std::abs(static_cast<int>(left[1]) - tinted) <= 1, "flattened glass is its tint over the backplane");

    // Back again: glass blurs again, baked once more.
    settings.reduced_transparency = false;
    window.set_presentation_settings(settings);
    repaint(made);
    const std::shared_ptr<const PartRaster> blurred = last_glass(made);
    require(blurred != nullptr, "the glass is composited again");
    const std::array<std::uint8_t, 4> edge_left = pixel_of(*blurred, 39U, 15U);
    const std::array<std::uint8_t, 4> edge_right = pixel_of(*blurred, 40U, 15U);
    const int step = std::abs(static_cast<int>(edge_right[1]) - static_cast<int>(edge_left[1]));
    const int flat_step = std::abs(static_cast<int>(right[1]) - static_cast<int>(left[1]));
    require(step < flat_step, "glass blurs again once transparency is restored");
}

void high_contrast_drops_glass() {
    std::unique_ptr<Fixture> fixture = make_fixture();
    Fixture& made = *fixture;
    Window& window = *made.window;
    PresentationSettings settings = window.presentation_settings();
    settings.high_contrast = true;
    window.set_presentation_settings(settings);
    require(window.backdrop_grant().granted == WindowBackdrop::opaque &&
                window.backdrop_grant().reason == WindowBackdropReason::high_contrast,
            "high contrast withdraws the backdrop");
    repaint(made);
    require(last_glass(made) == nullptr, "high contrast draws no glass");
}

} // namespace

int main(int argc, char** argv) {
    const bool printing = argc > 1 && std::string_view(argv[1]) == "--print";
    the_record_is_validated();
    GlassHashes hashes{};
    bases_by_grant(hashes);
    requests_follow_the_theme();
    reduced_transparency_flattens_glass(hashes);
    high_contrast_drops_glass();
    if (printing) {
        std::cout << std::hex << "material 0x" << hashes.material << "\ntransparent 0x" << hashes.transparent
                  << "\nopaque 0x" << hashes.opaque << "\nflattened 0x" << hashes.flattened << std::dec << '\n';
        return 0;
    }
    require(hashes.material == expected_material_glass, "glass over the material's base has the same bits everywhere");
    require(hashes.transparent == expected_transparent_glass, "glass over the clear base has the same bits everywhere");
    require(hashes.opaque == expected_opaque_glass, "glass over the recipe has the same bits everywhere");
    require(hashes.flattened == expected_flattened_glass, "flattened glass has the same bits everywhere");
    std::cout << "engine backdrop tests passed\n";
    return 0;
}
