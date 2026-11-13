// Stage T5, phase C6's renderer-neutral gate
// (notes:planning/stages/T5-compositor/PLAN.md §4.3 and §5 C6): the window's
// renderer replays the backplane plane's whole command list into the
// backplane image glass samples, behind the same borrow.
//
// A small test renderer stands in for a host's CPU raster: its framebuffer
// fills rectangles, copies part rasters and draws each text run as a solid
// block, through the saves, translations and clips a window paint uses. A
// backplane-plane control fills its bounds blue, draws a red part raster
// over its right half and a white text run; glass lies over it. With the
// renderer installed the backplane image is replayed: the glass shows the
// blue fill and the text, nothing is skipped, and the framebuffer is made
// once and reused. Without it, the part rasters alone are composited, as
// E9-B option 1 does (engine_glass_tests keeps those hashes).
#include "gui_forms/anatomy/types/anatomy_types.hpp"
#include "gui_forms/binding/property_type/property_type.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/dirty.hpp"
#include "gui_forms/display/types/display_types.hpp"
#include "gui_forms/engine/part_raster/part_raster.hpp"
#include "gui_forms/engine/part_table/engine_part_table.hpp"
#include "gui_forms/engine/theme_rules/theme_rules.hpp"
#include "gui_forms/engine/types/engine_types.hpp"
#include "gui_forms/resources/image_registry/image_registry.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/theme/theme/theme.hpp"
#include "gui_forms/theme/types/theme_types.hpp"
#include "gui_forms/types/damage_region/damage_region.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/window/window.hpp"
#include "gui_forms/paint_framebuffer.hpp"
#include "support/theme_definition_heap.hpp"
#include "support/rgba_order.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace {

using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "gui_forms_engine_backplane_replay_tests: " << message << '\n';
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

std::array<std::uint8_t, 4> pixel_of(const PartRaster& raster, std::size_t column, std::size_t row) {
    const std::size_t at = row * raster.row_bytes() + column * 4U;
    const std::span<const std::uint8_t> pixels = raster.pixels();
    // The raster is BGRA8; the value is red, green, blue, alpha.
    const std::array<std::uint8_t, 4> value{pixels[at + 2U], pixels[at + 1U], pixels[at], pixels[at + 3U]};
    return value;
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

// -- the test renderer ----------------------------------------------------------

// What a save restores: the translation and the clip, in the framebuffer's
// logical units.
struct FillState final {
    Point offset{};
    Rect clip{};
};

// Draws into premultiplied BGRA rows at scale 1, the framebuffer format:
// opaque fills, part rasters copied source-over by their nearest pixel, and each text run as
// a block 6 units wide per byte and 8 high, its baseline at the origin.
// Every other command is counted and not drawn.
class FillPainter final : public Painter {
public:
    void reset(std::uint32_t width, std::uint32_t height) {
        width_ = width;
        height_ = height;
        pixels_.assign(static_cast<std::size_t>(width) * height * 4U, 0U);
        states_.clear();
        state_ = FillState{Point{0.0, 0.0}, Rect{0.0, 0.0, static_cast<double>(width), static_cast<double>(height)}};
    }
    void begin_drawing(Rect damage) {
        states_.clear();
        state_ = FillState{Point{0.0, 0.0}, damage};
    }
    void save() override { states_.push_back(state_); }
    void restore() override {
        if (states_.empty()) return;
        state_ = states_.back();
        states_.pop_back();
    }
    void translate(Point offset) override {
        state_.offset = Point{state_.offset.x + offset.x, state_.offset.y + offset.y};
    }
    void clip_rect(Rect rect) override { state_.clip = Rect::intersection(state_.clip, placed(rect)); }
    void fill_rect(Rect rect, Color colour) override {
        const Rect area = Rect::intersection(placed(rect), state_.clip);
        const std::array<std::uint8_t, 4> premultiplied{premultiply(colour.blue, colour.alpha),
                                                        premultiply(colour.green, colour.alpha),
                                                        premultiply(colour.red, colour.alpha), colour.alpha};
        for_each_pixel(area, premultiplied, nullptr, Point{0.0, 0.0});
    }
    void stroke_rect(Rect, Color, double) override { ++others; }
    void draw_line(Point, Point, Color, double) override { ++others; }
    void draw_text_utf8(Point origin, std::string_view text, FontSpec, Color colour) override {
        ++texts;
        const Rect block{origin.x, origin.y - 8.0, 6.0 * static_cast<double>(text.size()), 8.0};
        fill_rect(block, colour);
    }
    void draw_image(ImageId, Rect, double) override { ++others; }
    void draw_part_raster(const std::shared_ptr<const PartRaster>& raster, Point origin, double) override {
        if (!raster) return;
        const DeviceExtent extent = (*raster).extent();
        const Rect area = Rect::intersection(
            placed(Rect{origin.x, origin.y, static_cast<double>(extent.width), static_cast<double>(extent.height)}),
            state_.clip);
        for_each_pixel(area, {}, raster.get(), Point{origin.x + state_.offset.x, origin.y + state_.offset.y});
    }

    [[nodiscard]] std::span<std::uint8_t> pixels() & noexcept { return pixels_; }
    std::span<std::uint8_t> pixels() && = delete;
    [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }

    std::size_t texts{};
    std::size_t others{};

private:
    static std::uint8_t premultiply(std::uint8_t value, std::uint8_t alpha) noexcept {
        const std::uint32_t scaled = (static_cast<std::uint32_t>(value) * alpha + 127U) / 255U;
        return static_cast<std::uint8_t>(scaled);
    }
    Rect placed(Rect rect) const noexcept {
        const Rect moved{rect.x + state_.offset.x, rect.y + state_.offset.y, rect.width, rect.height};
        return moved;
    }
    // Source-over of `colour`, or of `raster` placed at `raster_origin`,
    // over every pixel whose centre lies in `area`.
    void for_each_pixel(Rect area, std::array<std::uint8_t, 4> colour, const PartRaster* raster,
                        Point raster_origin) {
        if (area.empty()) return;
        const std::int64_t left = std::max<std::int64_t>(0, static_cast<std::int64_t>(std::ceil(area.x - 0.5)));
        const std::int64_t top = std::max<std::int64_t>(0, static_cast<std::int64_t>(std::ceil(area.y - 0.5)));
        const std::int64_t right =
            std::min<std::int64_t>(width_, static_cast<std::int64_t>(std::ceil(area.right() - 0.5)));
        const std::int64_t bottom =
            std::min<std::int64_t>(height_, static_cast<std::int64_t>(std::ceil(area.bottom() - 0.5)));
        for (std::int64_t row = top; row < bottom; ++row) {
            for (std::int64_t column = left; column < right; ++column) {
                std::array<std::uint8_t, 4> source = colour;
                if (raster != nullptr) {
                    const std::int64_t from_column = column - static_cast<std::int64_t>(std::floor(raster_origin.x));
                    const std::int64_t from_row = row - static_cast<std::int64_t>(std::floor(raster_origin.y));
                    if (from_column < 0 || from_row < 0 ||
                        from_column >= static_cast<std::int64_t>((*raster).extent().width) ||
                        from_row >= static_cast<std::int64_t>((*raster).extent().height)) {
                        continue;
                    }
                    const std::array<std::uint8_t, 4> rgba = pixel_of(
                        *raster, static_cast<std::size_t>(from_column), static_cast<std::size_t>(from_row));
                    source = {rgba[2], rgba[1], rgba[0], rgba[3]};
                }
                const std::size_t at = (static_cast<std::size_t>(row) * width_ + static_cast<std::size_t>(column)) * 4U;
                const std::uint32_t keep = 255U - source[3];
                for (std::size_t lane = 0U; lane < 4U; ++lane) {
                    const std::uint32_t under = (static_cast<std::uint32_t>(pixels_[at + lane]) * keep + 127U) / 255U;
                    pixels_[at + lane] = static_cast<std::uint8_t>(std::min(255U, source[lane] + under));
                }
            }
        }
    }

    std::uint32_t width_{};
    std::uint32_t height_{};
    std::vector<std::uint8_t> pixels_;
    std::vector<FillState> states_;
    FillState state_{};
};

class FillFramebuffer final : public PaintFramebuffer {
public:
    FillFramebuffer(std::uint32_t width, std::uint32_t height) { painter_.reset(width, height); }
    bool begin(const ImageRegistry&, Rect damage) override {
        ++begun;
        painter_.begin_drawing(damage);
        return true;
    }
    void end() override { ++ended; }
    Painter& painter() noexcept override { return painter_; }
    BorrowedMutableSpan<std::byte> pixels() noexcept override {
        const std::span<std::uint8_t> bytes = painter_.pixels();
        std::byte* const first = reinterpret_cast<std::byte*>(bytes.data());
        BorrowedMutableSpan<std::byte> view = detail::lend_mutable_view(first, bytes.size(), lender_);
        return view;
    }
    std::size_t row_bytes() const noexcept override { return static_cast<std::size_t>(painter_.width()) * 4U; }
    std::uint32_t width() const noexcept override { return painter_.width(); }
    std::uint32_t height() const noexcept override { return painter_.height(); }

    std::size_t begun{};
    std::size_t ended{};
    FillPainter painter_{};
    detail::BorrowLender lender_;
};

// The window's framebuffer painter: makes FillFramebuffers, counting them;
// `shrink` makes them a pixel short, which the window must refuse.
class FillRaster final : public Painter {
public:
    std::unique_ptr<PaintFramebuffer> create_framebuffer(Size logical_size, double scale) override {
        ++made;
        const std::uint32_t width = static_cast<std::uint32_t>(std::ceil(logical_size.width * scale));
        const std::uint32_t height = static_cast<std::uint32_t>(std::ceil(logical_size.height * scale));
        std::unique_ptr<FillFramebuffer> framebuffer =
            std::make_unique<FillFramebuffer>(shrink ? width - 1U : width, height);
        last = framebuffer.get();
        return framebuffer;
    }
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}

    std::size_t made{};
    bool shrink{};
    // The last framebuffer made, owned by the window.
    FillFramebuffer* last{};
};

// -- the window ---------------------------------------------------------------------

// The window's own paint: keeps the part rasters composited, draws nothing.
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

// The backplane-plane control at window x 60 to 180: a blue fill over its
// bounds, and past them (clipped away), a red part raster over its right
// half, and a white text run at (10, 40) of its own.
class Backdrop final : public Control {
public:
    explicit Backdrop(StableId stable_id) : Control(std::move(stable_id)) { set_paint_plane(PaintPlane::backplane); }
    void on_paint(Painter& painter, Rect bounds) override {
        painter.fill_rect(Rect{bounds.x - 40.0, bounds.y, bounds.width + 80.0, bounds.height}, Color::rgba(0, 0, 255));
        painter.draw_part_raster(red, Point{60.0, 0.0}, 1.0);
        painter.draw_text_utf8(Point{10.0, 40.0}, "glass", FontSpec{}, Color::rgba(255, 255, 255));
    }
    std::shared_ptr<const PartRaster> red;
};

// A control-plane control with one glass region at window (20, 20), 140 by
// 30: over the panel alone at its left, the blue fill and the text in its
// middle, the red raster at its right.
class Pane final : public Control {
public:
    explicit Pane(StableId stable_id) : Control(std::move(stable_id)) {}
    void on_paint(Painter& painter, Rect) override {
        PartPaint part{};
        part.role = PartRole::region;
        part.family = ControlVisualRole::card;
        part.bounds = Rect{20.0, 20.0, 140.0, 30.0};
        part.corner_radius = 0.0;
        part.key = PartKey{"glass", {}, {}};
        paint_part(painter, *this, part);
    }
};

struct Fixture final {
    std::shared_ptr<Panel> root;
    std::shared_ptr<Backdrop> backdrop;
    std::shared_ptr<Pane> pane;
    std::unique_ptr<Window> window;
    RasterWatch painter{};
};

std::unique_ptr<ThemeDefinition> glass_theme() {
    std::unique_ptr<ThemeDefinition> held = test_support::professional_definition();
    ThemeDefinition& definition = *held;
    definition.id = "engine-backplane-replay";
    ThemeRule rule{};
    rule.label = "glass";
    rule.selector.role = PartRole::region;
    rule.declarations.push_back(RuleDeclaration::primitive("glass"));
    // A light blur, so that a column of the glass still shows what lies
    // under it.
    rule.declarations.push_back(RuleDeclaration::number_value("blur", 1.0));
    definition.rules.rules.push_back(rule);
    return held;
}

// The window, laid out; painted once when `raster` is given, after it is
// installed as the framebuffer painter.
std::unique_ptr<Fixture> make_fixture(FillRaster* raster) {
    std::unique_ptr<Fixture> fixture = std::make_unique<Fixture>();
    Fixture& made = *fixture;
    made.root = make_control<Panel>(StableId("root"));
    made.backdrop = make_control<Backdrop>(StableId("backdrop"));
    (*made.backdrop).set_requested_bounds(Rect{60.0, 0.0, 120.0, 100.0});
    (*made.backdrop).red = solid_raster(DeviceExtent{60U, 100U}, {255U, 0U, 0U, 255U});
    made.pane = make_control<Pane>(StableId("pane"));
    (*made.pane).set_requested_bounds(Rect{0.0, 0.0, 240.0, 100.0});
    (*made.root).add_child(made.backdrop);
    (*made.root).add_child(made.pane);
    made.window = std::make_unique<Window>(made.root, Size{240.0, 100.0});
    Window& window = *made.window;
    window.set_theme(Theme::create(*glass_theme()));
    if (raster != nullptr) window.set_framebuffer_painter(raster);
    window.perform_layout();
    window.paint(made.painter);
    static_cast<void>(window.take_damage());
    return fixture;
}

std::shared_ptr<const PartRaster> last_glass(const Fixture& fixture) {
    for (std::size_t index = fixture.painter.rasters.size(); index > 0U; --index) {
        const std::shared_ptr<const PartRaster>& raster = fixture.painter.rasters[index - 1U];
        if ((*raster).extent().width == 140U && (*raster).extent().height == 30U) return raster;
    }
    return nullptr;
}

void repaint(Fixture& fixture) {
    Window& window = *fixture.window;
    const DamageRegion damage = window.take_damage();
    if (!damage.empty()) window.paint(fixture.painter, damage.bounds());
}

// Glass columns: 10 (window x 30) over the panel alone; 60
// and 95 (window x 80 and 115) over the blue fill, the first also over the
// text, which lies at window x 70 to 100 and y 32 to 40; 125 (window x 145)
// over the red raster.
constexpr std::size_t panel_column = 10U;
constexpr std::size_t text_column = 60U;
constexpr std::size_t blue_column = 95U;
constexpr std::size_t red_column = 125U;
// Glass row 16 is window y 36, inside the text block (y 32 to 40).
constexpr std::size_t text_row = 16U;

// Glass over the replayed backplane at scale 1; recorded on Windows x64
// and checked on every CI platform (the test renderer is the same
// everywhere).
constexpr std::uint64_t expected_replayed_glass = 0xef3210890a0c0db5ULL;

void the_backplane_is_replayed(bool printing) {
    FillRaster raster{};
    std::unique_ptr<Fixture> fixture = make_fixture(&raster);
    const std::shared_ptr<const PartRaster> glass = last_glass(*fixture);
    require(glass != nullptr, "the glass is composited");
    const RasterCacheStatistics made = (*(*fixture).window).engine_parts().cache_statistics();
    require(made.backplane_bakes == 1U && made.backplane_replays == 1U, "the backplane image is replayed once");
    require(made.backplane_skipped_commands == 0U, "a replayed backplane skips nothing");
    require((*(*fixture).window).metrics().snapshot().backplane_replay, "the metrics report the replay");
    require(raster.made == 1U && raster.last != nullptr, "one framebuffer is made");
    require((*raster.last).painter_.texts == 1U, "the text is replayed into the backplane");
    require((*raster.last).begun == 1U && (*raster.last).ended == 1U, "the replay begins and ends its frame");
    const std::array<std::uint8_t, 4> red = pixel_of(*glass, red_column, 15U);
    const std::array<std::uint8_t, 4> blue = pixel_of(*glass, blue_column, 25U);
    const std::array<std::uint8_t, 4> text = pixel_of(*glass, text_column, text_row);
    const std::array<std::uint8_t, 4> panel = pixel_of(*glass, panel_column, 15U);
    require(red[0] > red[2] + 100U, "the glass shows the backplane's part raster");
    require(blue[2] > blue[0] + 100U, "the glass shows the backplane's fill");
    require(text[0] > 150U && text[1] > 150U && text[2] > 150U, "the glass shows the backplane's text");
    require(panel[2] < blue[2], "the fill is clipped to the control that drew it");
    const std::uint64_t hash = hash_bytes((*glass).pixels());
    if (printing) {
        std::cout << "replayed glass hash 0x" << std::hex << hash << std::dec << '\n';
        return;
    }
    require(hash == expected_replayed_glass, "the glass over the replayed backplane has the same bits everywhere");
}

void the_part_raster_image_remains() {
    std::unique_ptr<Fixture> fixture = make_fixture(nullptr);
    const std::shared_ptr<const PartRaster> glass = last_glass(*fixture);
    require(glass != nullptr, "the glass is composited");
    const RasterCacheStatistics made = (*(*fixture).window).engine_parts().cache_statistics();
    require(made.backplane_bakes == 1U && made.backplane_replays == 0U, "without a renderer nothing is replayed");
    require(made.backplane_skipped_commands >= 2U, "the fill and the text are skipped and counted");
    require(!(*(*fixture).window).metrics().snapshot().backplane_replay, "the metrics report no replay");
    const std::array<std::uint8_t, 4> red = pixel_of(*glass, red_column, 15U);
    const std::array<std::uint8_t, 4> blue = pixel_of(*glass, blue_column, 25U);
    require(red[0] > red[2] + 100U, "the part raster is composited");
    require(blue[2] < blue[0] + 100U, "the fill is not");

    // A framebuffer of another extent is refused: the part-raster image is
    // made instead.
    FillRaster short_raster{};
    short_raster.shrink = true;
    std::unique_ptr<Fixture> refused = make_fixture(&short_raster);
    const RasterCacheStatistics fallback = (*(*refused).window).engine_parts().cache_statistics();
    require(short_raster.made >= 1U, "the window asks for a framebuffer");
    require(fallback.backplane_replays == 0U && fallback.backplane_skipped_commands >= 2U,
            "a framebuffer of another extent is not used");
}

void the_framebuffer_is_reused() {
    FillRaster raster{};
    std::unique_ptr<Fixture> fixture = make_fixture(&raster);
    Fixture& made = *fixture;
    const std::uint64_t first = (*made.window).engine_parts().cache_statistics().backplane_replays;
    // Backplane damage: the image is replayed again into the same
    // framebuffer, its pixels restored from the base first.
    (*made.backdrop).invalidate(Dirty::paint);
    repaint(made);
    const RasterCacheStatistics after = (*made.window).engine_parts().cache_statistics();
    require(after.backplane_replays == first + 1U, "backplane damage replays the image again");
    require(raster.made == 1U, "the framebuffer is reused");
    const std::shared_ptr<const PartRaster> glass = last_glass(made);
    require(glass != nullptr, "the glass is composited again");
    const std::array<std::uint8_t, 4> blue = pixel_of(*glass, blue_column, 25U);
    require(blue[2] > blue[0] + 100U, "the second replay shows the fill");
    // A new framebuffer painter releases the old framebuffer.
    FillRaster other{};
    (*made.window).set_framebuffer_painter(&other);
    (*made.backdrop).invalidate(Dirty::paint);
    repaint(made);
    require(other.made == 1U, "a new renderer makes its own framebuffer");
    (*made.window).set_framebuffer_painter(nullptr);
}

} // namespace

int main(int argc, char** argv) {
    const bool printing = print_mode(argc, argv);
    the_backplane_is_replayed(printing);
    if (printing) return 0;
    the_part_raster_image_remains();
    the_framebuffer_is_reused();
    std::cout << "gui_forms_engine_backplane_replay_tests passed\n";
    return 0;
}
