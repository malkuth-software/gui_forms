// The stock icon set (exhibit gap E10): the embedded Heroicons records read
// back (the converter's round trip on a few icons), the nearest-size
// choice, stock icon image ids, coverage and a baked icon's pixels, the
// theme's colour and high contrast through PictureBox, a theme's
// replacement, and the status icons baked at window launch.
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/controls/picture_box.hpp"
#include "gui_forms/engine/part_raster/part_raster.hpp"
#include "gui_forms/resources/application_resources/application_resources.hpp"
#include "gui_forms/resources/stock_icons/stock_alert_icon.hpp"
#include "gui_forms/resources/stock_icons/stock_icons.hpp"
#include "gui_forms/theme/theme/theme.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/window/window.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace gui_forms;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool near(double left, double right) {
    return std::abs(left - right) <= 0.0005;
}

// A painter keeping the part rasters composited and counting path draws.
class CapturePainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override { ++images; }
    void draw_part_raster(const std::shared_ptr<const PartRaster>& raster, Point origin, double) override {
        rasters.push_back(raster);
        origins.push_back(origin);
    }
    void fill_path(PaintPathView, Color colour, PolygonFillRule) override {
        ++paths;
        path_colour = colour;
    }
    void stroke_path(PaintPathView, const StrokeStyle&) override { ++paths; }

    std::vector<std::shared_ptr<const PartRaster>> rasters;
    std::vector<Point> origins;
    std::size_t images{};
    std::size_t paths{};
    Color path_colour{};
};

// Pixel (x, y) of `raster` as blue, green, red, alpha.
std::array<std::uint8_t, 4> pixel(const PartRaster& raster, std::size_t x, std::size_t y) {
    const BorrowedSpan<std::uint8_t> pixels = raster.pixels();
    const std::size_t at = y * raster.row_bytes() + x * 4U;
    const std::array<std::uint8_t, 4> result{pixels[at], pixels[at + 1U], pixels[at + 2U], pixels[at + 3U]};
    return result;
}

void the_set_and_its_records() {
    const std::span<const std::string_view> names = stock_icon_names();
    require(names.size() == 324U && std::is_sorted(names.begin(), names.end()), "324 names, sorted");
    require(std::binary_search(names.begin(), names.end(), std::string_view{"information-circle"}),
            "the set holds information-circle");
    require(has_stock_icon(StockIcon{"minus", StockIconStyle::outline, StockIconSize::grid_24}) &&
                !has_stock_icon(StockIcon{"minus", StockIconStyle::outline, StockIconSize::grid_16}),
            "outline icons are drawn on the 24-unit grid only");

    // The converter's round trip: the records hold what the SVG files say.
    const std::shared_ptr<const StockIconVectors> minus =
        stock_icon_vectors(StockIcon{"minus", StockIconStyle::outline, StockIconSize::grid_24});
    require(minus != nullptr && (*minus).grid() == 24.0 && (*minus).layer_count() == 1U, "minus has one layer");
    const PaintPathView minus_path = (*minus).layer_path(0U);
    require(minus_path.verbs.size() == 2U && minus_path.verbs[0] == PaintPathVerb::move_to &&
                minus_path.verbs[1] == PaintPathVerb::line_to && near(minus_path.points[0].x, 5.0) &&
                near(minus_path.points[0].y, 12.0) && near(minus_path.points[1].x, 19.0) &&
                near(minus_path.points[1].y, 12.0),
            "minus is M5 12h14");
    require((*minus).layer(0U).paint == StockIconPaint::stroke && (*minus).layer(0U).stroke_width == 1.5 &&
                (*minus).layer(0U).cap == StrokeCap::round,
            "outline strokes are 1.5 units with round caps");
    const std::shared_ptr<const StockIconVectors> cross =
        stock_icon_vectors(StockIcon{"x-mark", StockIconStyle::solid, StockIconSize::grid_20});
    require(cross != nullptr && (*cross).layer(0U).paint == StockIconPaint::fill_nonzero &&
                near((*cross).layer_path(0U).points[0].x, 6.28) && near((*cross).layer_path(0U).points[0].y, 5.22) &&
                (*cross).layer_path(0U).verbs[1] == PaintPathVerb::cubic_to,
            "x-mark starts at 6.28 5.22 and its arcs are cubic curves");
    const std::shared_ptr<const StockIconVectors> warning =
        stock_icon_vectors(StockIcon{"exclamation-triangle", StockIconStyle::solid, StockIconSize::grid_16});
    require(warning != nullptr && (*warning).layer(0U).paint == StockIconPaint::fill_even_odd,
            "the 16-unit warning is filled even-odd");
    require(stock_icon_vectors(StockIcon{"no-such-icon", StockIconStyle::solid, StockIconSize::grid_24}) == nullptr,
            "an unknown icon has no vectors");
}

void nearest_size_is_chosen() {
    const std::optional<StockIcon> at_16 = nearest_stock_icon("check-circle", StockIconStyle::solid, 16.0);
    const std::optional<StockIcon> at_17 = nearest_stock_icon("check-circle", StockIconStyle::solid, 17.0);
    const std::optional<StockIcon> at_18 = nearest_stock_icon("check-circle", StockIconStyle::solid, 18.0);
    const std::optional<StockIcon> at_22 = nearest_stock_icon("check-circle", StockIconStyle::solid, 22.0);
    const std::optional<StockIcon> at_48 = nearest_stock_icon("check-circle", StockIconStyle::solid, 48.0);
    const std::optional<StockIcon> at_8 = nearest_stock_icon("check-circle", StockIconStyle::solid, 8.0);
    require(at_16 && (*at_16).size == StockIconSize::grid_16, "16 units shows the 16-unit drawing");
    require(at_17 && (*at_17).size == StockIconSize::grid_16, "17 units is nearest 16");
    require(at_18 && (*at_18).size == StockIconSize::grid_20, "18 units is a tie that goes to the larger, 20");
    require(at_22 && (*at_22).size == StockIconSize::grid_24, "22 units is a tie that goes to the larger, 24");
    require(at_48 && (*at_48).size == StockIconSize::grid_24, "48 units shows the 24-unit drawing");
    require(at_8 && (*at_8).size == StockIconSize::grid_16, "8 units shows the 16-unit drawing");
    const std::optional<StockIcon> outline = nearest_stock_icon("check-circle", StockIconStyle::outline, 16.0);
    require(outline && (*outline).size == StockIconSize::grid_24, "an outline icon has the 24-unit drawing only");
    const std::optional<StockIcon> lacking = nearest_stock_icon("plus-small", StockIconStyle::solid, 16.0);
    require(lacking && (*lacking).size == StockIconSize::grid_20, "a solid icon without a 16-unit drawing shows 20");
    require(!nearest_stock_icon("no-such-icon", StockIconStyle::solid, 16.0) &&
                !nearest_stock_icon("check-circle", StockIconStyle::solid, 0.0),
            "an unknown name or a side that is not positive chooses nothing");
}

void stock_icon_image_ids() {
    const ImageId nearest = stock_icon_image("check-circle", StockIconStyle::solid);
    require(nearest.value != 0U && is_stock_icon_image(nearest), "a stock icon id is made");
    const std::optional<StockIconImage> parts = stock_icon_image_parts(nearest);
    require(parts && (*parts).name == "check-circle" && (*parts).style == StockIconStyle::solid && !(*parts).size,
            "the id names the icon and leaves the size to the drawn size");
    const std::optional<StockIcon> drawn = stock_icon_for_image(nearest, 20.0);
    require(drawn && (*drawn).size == StockIconSize::grid_20, "the drawn size chooses the variant");
    const ImageId fixed = stock_icon_image(StockIcon{"check-circle", StockIconStyle::solid, StockIconSize::grid_16});
    const std::optional<StockIcon> fixed_drawn = stock_icon_for_image(fixed, 48.0);
    require(fixed_drawn && (*fixed_drawn).size == StockIconSize::grid_16, "a fixed size is kept whatever the side");
    require(stock_icon_image("no-such-icon", StockIconStyle::solid).value == 0U &&
                stock_icon_image(StockIcon{"minus", StockIconStyle::outline, StockIconSize::grid_16}).value == 0U,
            "an unknown name or variant is no image");
    require(!is_stock_icon_image(ImageId{(1ULL << 63U) | (1ULL << 40U)}) && !is_stock_icon_image(ImageId{42U}),
            "ids with stray bits, and registry ids, are not stock icons");
}

void coverage_and_a_baked_icon() {
    // stop, 16/solid: a rect from 3 to 13 with corners of radius 1.5.
    const std::shared_ptr<const StockIconVectors> stop =
        stock_icon_vectors(StockIcon{"stop", StockIconStyle::solid, StockIconSize::grid_16});
    require(stop != nullptr, "stop is in the set");
    std::vector<std::uint8_t> coverage(16U * 16U);
    require(rasterize_stock_icon(*stop, 16U, coverage), "stop rasterizes at 16 pixels");
    require(coverage[8U * 16U + 8U] == 255U && coverage[8U * 16U + 3U] == 255U && coverage[8U * 16U + 12U] == 255U,
            "the rect's inside is covered to its edges");
    require(coverage[8U * 16U + 2U] == 0U && coverage[8U * 16U + 13U] == 0U && coverage[1U * 16U + 1U] == 0U,
            "outside the rect is uncovered");
    require(coverage[3U * 16U + 3U] > 0U && coverage[3U * 16U + 3U] < 255U, "a rounded corner is partly covered");
    require(!rasterize_stock_icon(*stop, 0U, coverage) && !rasterize_stock_icon(*stop, 17U, coverage),
            "a zero side or short storage writes nothing");

    // minus, 24/outline at 24 pixels: a 1.5-unit stroke along y = 12.
    const std::shared_ptr<const StockIconVectors> minus =
        stock_icon_vectors(StockIcon{"minus", StockIconStyle::outline, StockIconSize::grid_24});
    std::vector<std::uint8_t> line(24U * 24U);
    require(rasterize_stock_icon(*minus, 24U, line), "minus rasterizes at 24 pixels");
    require(line[11U * 24U + 12U] == 191U && line[12U * 24U + 12U] == 191U && line[10U * 24U + 12U] == 0U,
            "the stroke covers the two rows it straddles by three quarters");
    require(line[11U * 24U + 4U] > 0U && line[11U * 24U + 4U] < line[11U * 24U + 12U] && line[11U * 24U + 2U] == 0U,
            "the round cap reaches past the end point and fades");

    const Color red{200U, 10U, 20U, 255U};
    const std::shared_ptr<const PartRaster> baked = bake_stock_icon(*stop, 16U, DeviceScale{1.0}, red);
    require(baked != nullptr && (*baked).extent() == DeviceExtent{16U, 16U} && (*baked).scale() == DeviceScale{1.0},
            "the baked raster is 16 pixels square at scale 1");
    require(pixel(*baked, 8U, 8U) == std::array<std::uint8_t, 4>{20U, 10U, 200U, 255U},
            "a covered pixel is the colour, premultiplied BGRA");
    require(pixel(*baked, 0U, 0U) == std::array<std::uint8_t, 4>{0U, 0U, 0U, 0U}, "an uncovered pixel is clear");
    const std::array<std::uint8_t, 4> corner = pixel(*baked, 3U, 3U);
    require(corner[3] > 0U && corner[3] < 255U && corner[2] <= corner[3], "a partly covered pixel is premultiplied");
}

struct PictureFixture final {
    PictureFixture() : window(root, {200.0, 120.0}) {
        (*box).set_requested_bounds({10.0, 10.0, 20.0, 20.0});
        (*box).size_mode = PictureBoxSizeMode::stretch_image;
        (*box).border_style = BorderStyle::none;
        (*box).image = stock_icon_image("check-circle", StockIconStyle::solid);
        (*root).add_child(box);
        window.perform_layout();
    }

    Color glyph() const { return (*box).effective_theme().resolve((*box).visual_role.read(), (*box).visual_context()).glyph; }

    std::shared_ptr<Panel> root = make_control<Panel>(StableId("icons.root"));
    std::shared_ptr<PictureBox> box = make_control<PictureBox>(StableId("icons.box"));
    Window window;
};

void theme_colour_and_high_contrast() {
    PictureFixture fixture;
    require((*fixture.box).has_valid_image(), "a stock icon is a valid PictureBox image");
    CapturePainter ordinary;
    (*fixture.box).on_paint(ordinary, Rect{0.0, 0.0, 20.0, 20.0});
    // The icon is the last raster composited (the panel's face may be one
    // before it).
    require(!ordinary.rasters.empty() && ordinary.images == 0U, "the box composites a baked raster");
    const PartRaster& drawn = *ordinary.rasters.back();
    require(drawn.extent() == DeviceExtent{20U, 20U}, "baked at the drawn size for the window's scale");
    const Color expected = fixture.glyph();
    const std::array<std::uint8_t, 4> centre = pixel(drawn, 3U, 10U);
    require(centre[3] == expected.alpha && centre[2] == static_cast<std::uint8_t>((expected.red * expected.alpha + 127U) / 255U),
            "the icon is in the theme's glyph colour");

    PresentationSettings settings = fixture.window.presentation_settings();
    settings.high_contrast = true;
    fixture.window.set_presentation_settings(settings);
    const Color contrast = fixture.glyph();
    require(!(contrast == expected), "high contrast gives the glyph another colour");
    CapturePainter forced;
    (*fixture.box).on_paint(forced, Rect{0.0, 0.0, 20.0, 20.0});
    require(!forced.rasters.empty(), "high contrast composites a baked raster");
    const std::array<std::uint8_t, 4> forced_centre = pixel(*forced.rasters.back(), 3U, 10U);
    require(forced_centre[3] == contrast.alpha &&
                forced_centre[2] == static_cast<std::uint8_t>((contrast.red * contrast.alpha + 127U) / 255U) &&
                forced_centre[0] == static_cast<std::uint8_t>((contrast.blue * contrast.alpha + 127U) / 255U),
            "the icon follows high contrast");

    const StockIconBakeCounts before = stock_icon_bake_counts();
    CapturePainter again;
    (*fixture.box).on_paint(again, Rect{0.0, 0.0, 20.0, 20.0});
    const StockIconBakeCounts after = stock_icon_bake_counts();
    require(!again.rasters.empty() && again.rasters.back() == forced.rasters.back() &&
                after.coverage_bakes == before.coverage_bakes && after.raster_bakes == before.raster_bakes,
            "a repaint reuses the baked raster: nothing is rasterized per frame");
}

void detached_icons_draw_their_vectors() {
    PictureBox box(StableId("icons.detached"));
    box.set_requested_bounds({0.0, 0.0, 24.0, 24.0});
    box.image = stock_icon_image("check-circle", StockIconStyle::solid);
    CapturePainter painter;
    paint_stock_icon(painter, box, box.image.read(), Rect{0.0, 0.0, 24.0, 24.0}, Color{1U, 2U, 3U, 255U});
    require(painter.rasters.empty() && painter.paths >= 1U && painter.path_colour == Color{1U, 2U, 3U, 255U},
            "a detached control draws the icon's paths");
}

void a_theme_replaces_an_icon() {
    std::vector<PaintPathVerb> verbs{PaintPathVerb::move_to, PaintPathVerb::line_to, PaintPathVerb::line_to,
                                     PaintPathVerb::line_to, PaintPathVerb::close};
    std::vector<Point> points{Point{0.0, 0.0}, Point{16.0, 0.0}, Point{16.0, 16.0}, Point{0.0, 16.0}};
    std::vector<StockIconLayer> layers{StockIconLayer{5U, 4U, StockIconPaint::fill_nonzero, 0.0, StrokeCap::round}};
    const std::shared_ptr<const StockIconVectors> square =
        StockIconVectors::create(16.0, std::move(verbs), std::move(points), std::move(layers));
    ThemeDefinition definition = windows_professional_theme_definition();
    definition.id = "stock-icon-replacement";
    const StockIcon stop{"stop", StockIconStyle::solid, StockIconSize::grid_16};
    definition.stock_icons.push_back(StockIconReplacement{stop, square});
    const std::shared_ptr<const Theme> theme = Theme::create(definition);
    require((*theme).stock_icon_replacement(stop) == square, "the theme holds its replacement");
    require((*theme).stock_icon_replacement(StockIcon{"stop", StockIconStyle::solid, StockIconSize::grid_20}) ==
                nullptr,
            "other variants keep the built-in drawing");

    PictureFixture fixture;
    (*fixture.window.resources()).set_theme(theme);
    (*fixture.box).set_requested_bounds({10.0, 10.0, 16.0, 16.0});
    (*fixture.box).image = stock_icon_image(stop);
    fixture.window.perform_layout();
    CapturePainter painter;
    (*fixture.box).on_paint(painter, Rect{0.0, 0.0, 16.0, 16.0});
    require(!painter.rasters.empty() && pixel(*painter.rasters.back(), 0U, 0U)[3] == fixture.glyph().alpha,
            "the replacement's square corner is drawn where the built-in corner is round");

    ThemeDefinition unknown = windows_professional_theme_definition();
    unknown.stock_icons.push_back(StockIconReplacement{StockIcon{"no-such-icon", StockIconStyle::solid,
                                                                 StockIconSize::grid_16},
                                                       square});
    bool refused = false;
    try {
        static_cast<void>(Theme::create(unknown));
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused, "a replacement of an unknown name is refused");
    bool empty_refused = false;
    try {
        static_cast<void>(StockIconVectors::create(16.0, {}, {}, {}));
    } catch (const std::invalid_argument&) {
        empty_refused = true;
    }
    require(empty_refused, "vectors without layers are refused");
}

void status_icons_bake_at_launch() {
    PictureFixture fixture;
    const StockIconBakeCounts counts = stock_icon_bake_counts();
    require(counts.prebaked_scales >= 1U && counts.cached_coverage >= 15U,
            "a window bakes the five status icons on three grids at its scale");
    fixture.window.set_scale(2.0);
    require(stock_icon_bake_counts().prebaked_scales >= 2U, "a new scale bakes them again");

    const std::optional<HostImage> alert = stock_alert_icon_image(fixture.window, HostMessageIcon::error);
    require(alert && (*alert).width == 64U && (*alert).height == 64U && (*alert).pixels.size() == 64U * 64U * 4U,
            "a message box icon is 32 units at the window's scale");
    require(!stock_alert_icon_image(fixture.window, HostMessageIcon::none), "no icon gives no image");
}

} // namespace

int main() {
    try {
        the_set_and_its_records();
        nearest_size_is_chosen();
        stock_icon_image_ids();
        coverage_and_a_baked_icon();
        theme_colour_and_high_contrast();
        detached_icons_draw_their_vectors();
        a_theme_replaces_an_icon();
        status_icons_bake_at_launch();
    } catch (const std::exception& error) {
        std::cerr << "stock icons tests failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "stock icons tests passed\n";
    return EXIT_SUCCESS;
}
