// A layered user theme recolours a progress bar's fill and a list's
// selected rows (theme lab finding, cp29): a user sheet's `!important`
// background for `progress-bar::part(fill)`, `progress-bar::role(fill)` or
// `:family(progress):state(selected)` reaches the fill, in the continuous
// style and in each block of the blocks style, and one for
// `:family(selection):state(selected)` or `list-box::role(selection)`
// reaches the selected row's plate. Each case paints a real window over
// the default gui-forms theme and looks for the authored colour where the
// part lands: the centre of an engine raster, or the top layer of a
// material, or a plain fill. Without the user theme no such colour is
// painted, so each case proves the sheet changed the part. A layered user
// theme's findings are its own sheet's: the stock floor compiled beneath it
// is the colour authority and reports no W201, while the user's own literal
// edge colour does.

#include "gui_forms/gui_forms.hpp"
#include "gui_forms/engine/effect_registry/effect_registry.hpp"
#include "gui_forms/theme/stock_anatomies/stock_anatomies.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace gui_forms;

void require(bool condition, std::string_view message) {
    if (condition) return;
    std::cerr << "gui_forms_theme_user_fill_selection_tests: " << message << '\n';
    std::exit(1);
}

// The authored colour, #ff00ff, which no stock recipe uses.
constexpr Color authored = Color::rgba(255, 0, 255);

bool is_authored(Color colour) noexcept {
    const bool same = colour.red == authored.red && colour.green == authored.green &&
                      colour.blue == authored.blue && colour.alpha == authored.alpha;
    return same;
}

// Whether the pixel at the centre of `raster` is the authored colour, opaque
// (premultiplied BGRA8, tight rows).
bool raster_centre_is_authored(const PartRaster& raster) noexcept {
    const DeviceExtent extent = raster.extent();
    if (extent.width == 0U || extent.height == 0U) return false;
    const std::size_t column = static_cast<std::size_t>(extent.width / 2U);
    const std::size_t row = static_cast<std::size_t>(extent.height / 2U);
    const std::size_t offset = row * raster.row_bytes() + column * 4U;
    const std::span<const std::uint8_t> pixels = raster.pixels();
    if (offset + 4U > pixels.size()) return false;
    const bool same = pixels[offset] == authored.blue && pixels[offset + 1U] == authored.green &&
                      pixels[offset + 2U] == authored.red && pixels[offset + 3U] == authored.alpha;
    return same;
}

// Whether the visible (top) layer of `material` is the authored colour.
bool material_top_is_authored(const SurfaceMaterial& material) noexcept {
    if (material.fills.empty()) return false;
    const MaterialFillLayer& top = material.fills.back();
    const bool same = top.kind == MaterialFillKind::solid && is_authored(top.color);
    return same;
}

// A painter that counts the places the authored colour lands and draws
// nothing.
class ColourWatch final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color color) override { note(is_authored(color)); }
    void fill_rounded_rect(Rect, double, Color color) override { note(is_authored(color)); }
    void fill_rounded_rect_corners(Rect, PaintCornerRadii, Color color) override { note(is_authored(color)); }
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}
    void draw_part_raster(const std::shared_ptr<const PartRaster>& raster, Point, double) override {
        if (raster != nullptr) note(raster_centre_is_authored(*raster));
    }
    void draw_part_raster_scaled(const std::shared_ptr<const PartRaster>& raster, Rect, double) override {
        if (raster != nullptr) note(raster_centre_is_authored(*raster));
    }
    void draw_surface_material(Rect, const SurfaceMaterial& material) override {
        note(material_top_is_authored(material));
    }
    void draw_baked_surface_material(Rect, const std::shared_ptr<const SurfaceMaterial>& material,
                                     const std::shared_ptr<const PartRaster>& raster, double) override {
        if (raster != nullptr) {
            note(raster_centre_is_authored(*raster));
            return;
        }
        if (material != nullptr) note(material_top_is_authored(*material));
    }

    std::size_t authored_places{};

private:
    void note(bool landed) noexcept {
        if (landed) authored_places = authored_places + 1U;
    }
};

// `sheet` compiled as a layered user theme (its header implied) by
// `resources`.
ThemeCompileResult compile_user_sheet(ApplicationResources& resources, std::string_view sheet) {
    UserThemeFiles files{};
    files.name = "fill-selection";
    files.main_sheet = std::string(sheet);
    files.header_implied = true;
    ThemeCompileOptions options{};
    options.controls = stock_control_anatomies();
    options.effects = &built_in_effect_registry();
    const ThemeLibrary library{};
    ThemeCompileResult compiled = resources.compile_user_theme(files, library, options);
    for (const ThemeCompileDiagnostic& diagnostic : compiled.diagnostics()) {
        std::cerr << "  " << format_theme_diagnostic(diagnostic) << '\n';
    }
    return compiled;
}

// A user sheet compiled as a layered user theme into `resources` and
// selected; `sheet` empty selects none.
void select_user_sheet(ApplicationResources& resources, std::string_view sheet) {
    if (sheet.empty()) return;
    const ThemeCompileResult compiled = compile_user_sheet(resources, sheet);
    require(compiled.succeeded(), "the user sheet compiles");
    require(compiled.diagnostics().empty(), "the user sheet compiles without findings");
    resources.set_user_theme(compiled.theme());
}

// The authored places a window paints with `root` under `sheet`.
std::size_t authored_places(const std::shared_ptr<Control>& root, std::string_view sheet) {
    const std::shared_ptr<ApplicationResources> resources = std::make_shared<ApplicationResources>();
    select_user_sheet(*resources, sheet);
    Window window(root, Size{320.0, 160.0}, resources);
    require((*resources).user_theme_rules() != nullptr || sheet.empty(), "the user theme is layered");
    window.perform_layout();
    ColourWatch painter{};
    window.paint(painter);
    const std::size_t places = painter.authored_places;
    return places;
}

std::shared_ptr<Control> progress_root(ProgressBarVisualStyle style) {
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("fill.root"));
    const std::shared_ptr<ProgressBar> bar = make_control<ProgressBar>(StableId("fill.bar"));
    (*bar).set_requested_bounds(Rect{20.0, 20.0, 240.0, 24.0});
    (*bar).visual_style = style;
    (*bar).value = 60.0;
    (*root).add_child(bar);
    return root;
}

std::shared_ptr<Control> list_root() {
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("selection.root"));
    const std::shared_ptr<ListBox> list = make_control<ListBox>(StableId("selection.list"));
    (*list).set_requested_bounds(Rect{20.0, 20.0, 200.0, 120.0});
    (*list).items = {"Alpha", "Beta", "Gamma"};
    (*list).select_index(1U);
    (*root).add_child(list);
    return root;
}

constexpr std::string_view fill_by_part = "progress-bar::part(fill) { background: #ff00ff !important }\n";
constexpr std::string_view fill_by_role = "progress-bar::role(fill) { background: #ff00ff !important }\n";
constexpr std::string_view fill_by_family =
    ":family(progress):state(selected) { background: #ff00ff !important }\n";
constexpr std::string_view selection_by_family =
    ":family(selection):state(selected) { background: #ff00ff !important }\n";
constexpr std::string_view selection_by_role = "list-box::role(selection) { background: #ff00ff !important }\n";

void test_progress_fill(ProgressBarVisualStyle style, const char* const style_name) {
    const std::string name(style_name);
    require(authored_places(progress_root(style), std::string_view{}) == 0U,
            (name + ": without a user theme the fill is not the authored colour").c_str());
    require(authored_places(progress_root(style), fill_by_part) > 0U,
            (name + ": progress-bar::part(fill) recolours the fill").c_str());
    require(authored_places(progress_root(style), fill_by_role) > 0U,
            (name + ": progress-bar::role(fill) recolours the fill").c_str());
    require(authored_places(progress_root(style), fill_by_family) > 0U,
            (name + ": :family(progress):state(selected) recolours the fill").c_str());
}

void test_blocks_each_recoloured() {
    // 60 of 100 across 236 units of interior: several blocks, each a fill
    // part of its own, every one recoloured.
    const std::size_t places = authored_places(progress_root(ProgressBarVisualStyle::blocks), fill_by_part);
    require(places >= 2U, "blocks: each block is recoloured, not only one");
}

void test_list_selection() {
    require(authored_places(list_root(), std::string_view{}) == 0U,
            "list: without a user theme the selected row is not the authored colour");
    require(authored_places(list_root(), selection_by_family) > 0U,
            "list: :family(selection):state(selected) recolours the selected row");
    require(authored_places(list_root(), selection_by_role) > 0U,
            "list: list-box::role(selection) recolours the selected row");
}

void test_findings_are_the_users() {
    // No floor finding: the stock sheet beneath is the colour authority.
    ApplicationResources resources{};
    const ThemeCompileResult clean = compile_user_sheet(resources, fill_by_part);
    require(clean.succeeded() && clean.diagnostics().empty(),
            "a layered user theme reports none of the stock floor's W201 findings");
    // The user's own literal edge colour is reported, at the user's sheet.
    const ThemeCompileResult edged =
        compile_user_sheet(resources, "progress-bar::part(fill) { border: 1px solid #123456 }\n");
    require(edged.succeeded(), "a literal edge colour is a warning, not a refusal");
    std::size_t unremappable = 0U;
    for (const ThemeCompileDiagnostic& diagnostic : edged.diagnostics()) {
        if (diagnostic.code != "W201") continue;
        unremappable = unremappable + 1U;
        require(diagnostic.file.find("gui-forms/") != 0U, "the W201 names the user's sheet, not the floor");
    }
    require(unremappable == 1U, "the user's own literal edge colour is reported once (W201)");
}

} // namespace

int main() {
    test_findings_are_the_users();
    test_progress_fill(ProgressBarVisualStyle::continuous, "continuous");
    test_progress_fill(ProgressBarVisualStyle::blocks, "blocks");
    test_blocks_each_recoloured();
    test_list_selection();
    std::cout << "gui_forms_theme_user_fill_selection_tests: passed\n";
    return 0;
}
