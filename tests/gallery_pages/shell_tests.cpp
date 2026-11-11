#include "support.hpp"
#include "gui_forms/prepared_text/types/prepared_text_types.hpp"
#include "../../src/core/theme/contrast/theme_contrast.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace gallery_pages_tests {

void test_page_contract_and_rendering() {
    std::unique_ptr<Window> owned = gallery::make_gallery();
    Window& window = *owned;
    window.perform_layout();
    require(window.metrics_snapshot().control_count >= 200U,
            "the Gallery must retain a broad public control population");
    require(window.find("gallery.text.primary") &&
                window.find("gallery.collections.single.list") &&
                window.find("gallery.animation.easing") &&
                window.find("gallery.containers.split") &&
                window.find("gallery.images.picture.4") &&
                window.find("gallery.date.primary") &&
                window.find("gallery.host.clipboard.editor") &&
                window.find("gallery.layout.flow.0") &&
                window.find("gallery.layout.table") &&
                window.find("gallery.dock.canvas") &&
                window.find("gallery.anchor.canvas"),
            "the Gallery must publish stable IDs for every proving family");

    CountingPainter painter;
    window.paint(painter, {0.0, 0.0, 1280.0, 820.0});
    require(painter.commands > 150U,
            "the Gallery must generate a substantial retained painter command surface");
    for (const gallery::GalleryPageSpec& page : gallery::gallery_pages) {
        select_page(window, page.id);
    }
}

void test_gallery_semantic_surface() {
    std::unique_ptr<Window> owned = gallery::make_gallery();
    Window& window = *owned;
    const SemanticSnapshot snapshot = window.semantic_snapshot();
    require(snapshot.node_count >= 30U && !snapshot.roots.empty(),
            "visible gallery page must publish a substantial semantic surface");
    require(snapshot.to_json().find("gallery.controls.button.0") != std::string::npos &&
                snapshot.to_json().find("\"role\":\"check_box\"") != std::string::npos &&
                snapshot.to_json().find("\"role\":\"radio_button\"") != std::string::npos,
            "gallery semantics must include distinct stock control roles and stable IDs");
}

namespace {

// A painter that keeps the first plate a control paints (an engine raster's
// centre, unpremultiplied; a material's top solid fill; or a plain fill at
// least `plate_width` wide) and the last text colour, and draws nothing.
class PlateAndInkPainter final : public Painter {
public:
    explicit PlateAndInkPainter(double plate_width) noexcept : plate_width_(plate_width) {}

    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect rect, Color color) override {
        if (rect.width >= plate_width_) note_plate(color);
    }
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color color) override {
        ink = color;
        inked = true;
    }
    PreparedTextPaintResult draw_prepared_text(const PreparedTextLayout&, const LayoutAuthority, const Point,
                                               const Color color) override {
        ink = color;
        inked = true;
        const PreparedTextPaintResult unavailable{};
        return unavailable;
    }
    void draw_image(ImageId, Rect, double) override {}
    void draw_part_raster(const std::shared_ptr<const PartRaster>& raster, Point, double) override {
        if (raster != nullptr) note_raster(*raster);
    }
    void draw_part_raster_scaled(const std::shared_ptr<const PartRaster>& raster, Rect, double) override {
        if (raster != nullptr) note_raster(*raster);
    }
    void draw_surface_material(Rect, const SurfaceMaterial& material) override { note_material(material); }
    void draw_baked_surface_material(Rect, const std::shared_ptr<const SurfaceMaterial>& material,
                                     const std::shared_ptr<const PartRaster>& raster, double) override {
        if (raster != nullptr) {
            note_raster(*raster);
            return;
        }
        if (material != nullptr) note_material(*material);
    }

    Color plate{};
    bool plated{};
    Color ink{};
    bool inked{};

private:
    void note_plate(Color color) noexcept {
        if (plated) return;
        plate = color;
        plated = true;
    }
    // The centre pixel of premultiplied BGRA8 rows, as its straight colour.
    void note_raster(const PartRaster& raster) noexcept {
        const DeviceExtent extent = raster.extent();
        if (extent.width == 0U || extent.height == 0U) return;
        const std::size_t offset = static_cast<std::size_t>(extent.height / 2U) * raster.row_bytes() +
                                   static_cast<std::size_t>(extent.width / 2U) * 4U;
        const std::span<const std::uint8_t> pixels = raster.pixels();
        if (offset + 4U > pixels.size() || pixels[offset + 3U] == 0U) return;
        const unsigned alpha = pixels[offset + 3U];
        const unsigned blue = pixels[offset] * 255U / alpha;
        const unsigned green = pixels[offset + 1U] * 255U / alpha;
        const unsigned red = pixels[offset + 2U] * 255U / alpha;
        note_plate(Color::rgba(static_cast<std::uint8_t>(red), static_cast<std::uint8_t>(green),
                               static_cast<std::uint8_t>(blue), static_cast<std::uint8_t>(alpha)));
    }
    void note_material(const SurfaceMaterial& material) noexcept {
        if (material.fills.empty()) return;
        const MaterialFillLayer& top = material.fills.back();
        if (top.kind == MaterialFillKind::solid) note_plate(top.color);
    }

    double plate_width_{};
};

// The text colour `id` paints, painted alone.
Color label_ink(Window& window, std::string_view id) {
    const Control::Ptr label = window.find(id);
    require(label != nullptr, "the lifecycle card's labels are in the Gallery");
    PlateAndInkPainter painter(1.0e9);
    const Rect bounds = (*label).committed_arranged_bounds();
    (*label).on_paint(painter, Rect{0.0, 0.0, bounds.width, bounds.height});
    require(painter.inked, "a lifecycle card label paints its text");
    return painter.ink;
}

} // namespace

// The basics page's lifecycle card under the dark scheme (theme lab
// finding, cp29): its plate follows the scheme, so its labels' light ink
// reads on it at the body-text ratio (4.5:1). The card's plate was a
// literal near-white fill whatever the scheme. The basics page is shown
// first: a page not shown arranges its card to no size, which paints no
// plate.
void test_basics_card_dark_contrast() {
    std::unique_ptr<Window> owned = gallery::make_gallery();
    Window& window = *owned;
    PresentationSettings settings = window.presentation_settings();
    settings.colour_scheme = ColourScheme::dark;
    window.set_presentation_settings(settings);
    window.perform_layout();
    select_page(window, "basics");
    const Control::Ptr card = window.find("gallery.lifecycle-card");
    require(card != nullptr, "the basics page holds the lifecycle card");
    reveal_in_page_viewport(window, card);
    const Rect bounds = (*card).committed_arranged_bounds();
    require(bounds.width > 0.0 && bounds.height > 0.0, "the shown basics page arranges the lifecycle card");
    PlateAndInkPainter painter(bounds.width * 0.5);
    (*card).on_paint(painter, Rect{0.0, 0.0, bounds.width, bounds.height});
    require(painter.plated, "the lifecycle card paints a plate");
    const double plate_luminance = detail::relative_luminance(painter.plate);
    require(plate_luminance < 0.2, "under the dark scheme the lifecycle card's plate is dark");
    const Color title = label_ink(window, "gallery.lifecycle-title");
    const Color status = label_ink(window, "gallery.lifecycle-status");
    require(detail::contrast_ratio(title, painter.plate) >= 4.5,
            "the card title reads on the dark plate at 4.5:1");
    require(detail::contrast_ratio(status, painter.plate) >= 4.5,
            "the card status reads on the dark plate at 4.5:1");
}

} // namespace gallery_pages_tests
