// The theme engine's composite (T2 plan, phase E2b; GIVEN A2-2, C1-1): a
// part raster is composited by premultiplied source-over through one integer
// arithmetic, weighted by the part's opacity and the clip; the retained
// Painter command keeps the raster, replays it unchanged and is inspectable.
// The cross-host byte identity of the composite is checked by
// part_composite_identity_tests on the Skia raster every host draws with.

#include "../src/core/text/composite/text_composite.hpp"

#include "gui_forms/control.hpp"
#include "gui_forms/engine.hpp"
#include "gui_forms/visual_inspection.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <vector>

namespace {
using namespace gui_forms;
using detail::PartRasterSource;
using detail::TextCompositeClip;
using detail::TextCompositeStatus;
using detail::TextCompositeTarget;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

// A 32-bit BGRA8 pixel from four channels: blue in the low byte, red at
// bit 16.
std::uint32_t pack(unsigned red, unsigned green, unsigned blue, unsigned alpha) {
    return blue | (green << 8U) | (red << 16U) | (alpha << 24U);
}

// A target of `width` x `height` pixels, every one `fill`.
struct TestTarget final {
    std::vector<std::uint32_t> pixels;
    TextCompositeTarget target{};
};

TestTarget make_target(std::uint32_t width, std::uint32_t height, std::uint32_t fill, bool opaque) {
    TestTarget made{};
    made.pixels.assign(static_cast<std::size_t>(width) * height, fill);
    made.target.pixels = std::span<std::uint32_t>(made.pixels.data(), made.pixels.size());
    made.target.width = width;
    made.target.height = height;
    made.target.stride = width;
    made.target.opaque = opaque;
    return made;
}

TextCompositeClip whole(std::uint32_t width, std::uint32_t height) {
    TextCompositeClip clip{};
    clip.rect = {0.0, 0.0, static_cast<double>(width), static_cast<double>(height)};
    return clip;
}

// One premultiplied source pixel, given as red, green, blue and alpha,
// repeated over `width` x `height` as a part raster's BGRA bytes.
std::vector<std::uint8_t> solid_source(std::uint32_t width, std::uint32_t height, std::array<std::uint8_t, 4> pixel) {
    std::vector<std::uint8_t> bytes{};
    bytes.reserve(static_cast<std::size_t>(width) * height * 4U);
    for (std::size_t index = 0U; index < static_cast<std::size_t>(width) * height; ++index) {
        bytes.push_back(pixel[2]);
        bytes.push_back(pixel[1]);
        bytes.push_back(pixel[0]);
        bytes.push_back(pixel[3]);
    }
    return bytes;
}

PartRasterSource source_of(const std::vector<std::uint8_t>& bytes, std::uint32_t width, std::uint32_t height,
                           std::int64_t left, std::int64_t top) {
    const PartRasterSource source{std::span<const std::uint8_t>(bytes.data(), bytes.size()), width, height,
                                  static_cast<std::size_t>(width) * 4U, left, top};
    return source;
}

void test_opacity_quantization() {
    require(detail::part_composite_opacity(0.0) == 0U, "opacity 0 is byte 0");
    require(detail::part_composite_opacity(1.0) == 255U, "opacity 1 is byte 255");
    require(detail::part_composite_opacity(0.5) == 128U, "opacity 0.5 rounds half up to 128");
    require(detail::part_composite_opacity(std::nan("")) == 0U, "NaN opacity is 0");
    require(detail::part_composite_opacity(-1.0) == 0U && detail::part_composite_opacity(2.0) == 255U,
            "opacity outside [0, 1] is clamped");
}

void test_blend_arithmetic() {
    // An opaque source at full opacity and coverage is copied exactly.
    TestTarget copied = make_target(2U, 1U, pack(0U, 0U, 255U, 255U), false);
    const std::vector<std::uint8_t> opaque = solid_source(2U, 1U, {200, 100, 50, 255});
    require(detail::composite_part_raster(source_of(opaque, 2U, 1U, 0, 0), copied.target, whole(2U, 1U), 255U) ==
                TextCompositeStatus::success,
            "the composite succeeds");
    require(copied.pixels[0] == pack(200U, 100U, 50U, 255U) && copied.pixels[1] == copied.pixels[0],
            "an opaque raster is copied exactly");

    // A half-transparent premultiplied source over opaque blue:
    // out = s + d * (255 - 128) / 255, rounded once.
    TestTarget half = make_target(1U, 1U, pack(0U, 0U, 255U, 255U), false);
    const std::vector<std::uint8_t> translucent = solid_source(1U, 1U, {128, 64, 0, 128});
    require(detail::composite_part_raster(source_of(translucent, 1U, 1U, 0, 0), half.target, whole(1U, 1U), 255U) ==
                TextCompositeStatus::success,
            "the translucent composite succeeds");
    require(half.pixels[0] == pack(128U, 64U, 127U, 255U), "premultiplied source-over, rounded once");

    // Opacity 128 weights every channel by 128 * 256 / 65280, rounded.
    TestTarget faded = make_target(1U, 1U, pack(0U, 0U, 0U, 255U), false);
    require(detail::composite_part_raster(source_of(opaque, 1U, 1U, 0, 0), faded.target, whole(1U, 1U), 128U) ==
                TextCompositeStatus::success,
            "the faded composite succeeds");
    require(faded.pixels[0] == pack(100U, 50U, 25U, 255U), "opacity weights each premultiplied channel");

    // An opaque target keeps alpha 255 whatever the blend gives.
    TestTarget dib = make_target(1U, 1U, pack(0U, 0U, 0U, 0U), true);
    require(detail::composite_part_raster(source_of(translucent, 1U, 1U, 0, 0), dib.target, whole(1U, 1U), 255U) ==
                TextCompositeStatus::success,
            "the opaque-target composite succeeds");
    require((dib.pixels[0] >> 24U) == 255U, "an opaque target is written with alpha 255");
}

void test_transparency_and_zero_opacity_write_nothing() {
    TestTarget target = make_target(3U, 3U, 0x11223344U, false);
    const std::vector<std::uint32_t> before = target.pixels;
    const std::vector<std::uint8_t> clear = solid_source(3U, 3U, {0, 0, 0, 0});
    require(detail::composite_part_raster(source_of(clear, 3U, 3U, 0, 0), target.target, whole(3U, 3U), 255U) ==
                TextCompositeStatus::success,
            "a transparent raster composites");
    require(target.pixels == before, "a transparent raster changes nothing (passthrough transparency)");
    const std::vector<std::uint8_t> opaque = solid_source(3U, 3U, {9, 9, 9, 255});
    require(detail::composite_part_raster(source_of(opaque, 3U, 3U, 0, 0), target.target, whole(3U, 3U), 0U) ==
                TextCompositeStatus::success,
            "opacity 0 composites");
    require(target.pixels == before, "opacity 0 changes nothing");
}

void test_placement_and_clip() {
    const std::uint32_t background = pack(10U, 20U, 30U, 255U);
    const std::uint32_t white = pack(255U, 255U, 255U, 255U);
    const std::vector<std::uint8_t> opaque = solid_source(3U, 3U, {255, 255, 255, 255});

    // Placed partly outside the target: only the overlap is written.
    TestTarget shifted = make_target(4U, 4U, background, false);
    require(detail::composite_part_raster(source_of(opaque, 3U, 3U, -1, 2), shifted.target, whole(4U, 4U), 255U) ==
                TextCompositeStatus::success,
            "a raster partly outside the target composites");
    for (std::uint32_t y = 0U; y < 4U; ++y) {
        for (std::uint32_t x = 0U; x < 4U; ++x) {
            const bool inside = x < 2U && y >= 2U;
            const std::uint32_t pixel = shifted.pixels[static_cast<std::size_t>(y) * 4U + x];
            require(pixel == (inside ? white : background), "only the raster's overlap with the target changes");
        }
    }

    // A clip edge through the middle of a pixel blends that pixel partly;
    // pixels outside the clip are untouched.
    TestTarget clipped = make_target(3U, 1U, background, false);
    TextCompositeClip clip{};
    clip.rect = {0.0, 0.0, 1.5, 1.0};
    require(detail::composite_part_raster(source_of(opaque, 3U, 1U, 0, 0), clipped.target, clip, 255U) ==
                TextCompositeStatus::success,
            "a clipped composite succeeds");
    const unsigned middle_red = clipped.pixels[1] & 255U;
    require(clipped.pixels[0] == white, "a pixel inside the clip is covered");
    require(middle_red > 10U && middle_red < 255U, "a pixel half inside the clip is blended in part");
    require(clipped.pixels[2] == background, "a pixel outside the clip is untouched");
}

void test_refusals_write_nothing() {
    TestTarget target = make_target(2U, 2U, 0x01020304U, false);
    const std::vector<std::uint32_t> before = target.pixels;
    const std::vector<std::uint8_t> bytes = solid_source(2U, 2U, {50, 50, 50, 255});
    PartRasterSource short_buffer = source_of(bytes, 2U, 2U, 0, 0);
    short_buffer.pixels = short_buffer.pixels.first(short_buffer.pixels.size() - 1U);
    require(detail::composite_part_raster(short_buffer, target.target, whole(2U, 2U), 255U) ==
                TextCompositeStatus::invalid_geometry,
            "a short source buffer is refused");
    PartRasterSource short_rows = source_of(bytes, 2U, 2U, 0, 0);
    short_rows.row_bytes = 7U;
    require(detail::composite_part_raster(short_rows, target.target, whole(2U, 2U), 255U) ==
                TextCompositeStatus::invalid_geometry,
            "rows shorter than their pixels are refused");
    TextCompositeClip bad{};
    bad.rect = {0.0, 0.0, std::nan(""), 1.0};
    require(detail::composite_part_raster(source_of(bytes, 2U, 2U, 0, 0), target.target, bad, 255U) ==
                TextCompositeStatus::invalid_geometry,
            "a non-finite clip is refused");
    require(target.pixels == before, "every refusal writes nothing");
}

// Every premultiplied source alpha and channel at every opacity stays
// within 0..255 and never darkens a white destination below the source.
void test_premultiplied_sources_never_overflow() {
    const std::array<unsigned, 6> alphas{1U, 37U, 128U, 200U, 254U, 255U};
    const std::array<std::uint8_t, 5> opacities{1U, 64U, 128U, 200U, 255U};
    for (const unsigned alpha : alphas) {
        for (const std::uint8_t opacity : opacities) {
            TestTarget target = make_target(1U, 1U, 0xFFFFFFFFU, false);
            const std::uint8_t channel = static_cast<std::uint8_t>(alpha);
            const std::vector<std::uint8_t> bytes = solid_source(1U, 1U, {channel, channel, channel, channel});
            require(detail::composite_part_raster(source_of(bytes, 1U, 1U, 0, 0), target.target, whole(1U, 1U),
                                                  opacity) == TextCompositeStatus::success,
                    "a premultiplied source composites");
            require(target.pixels[0] == 0xFFFFFFFFU, "white over white stays white at every weight");
        }
    }
}

// -- PartRaster -----------------------------------------------------------------

// A raster shown at another size (E7r): each device pixel samples the two
// texels about its centre with 8-bit weights, transparent beyond the edges.
void test_scaled_sampling() {
    // Red then blue, opaque, shown twice as wide.
    // Red then blue, as BGRA bytes.
    const std::vector<std::uint8_t> pair{0U, 0U, 255U, 255U, 255U, 0U, 0U, 255U};
    PartRasterSource stretched = source_of(pair, 2U, 1U, 0, 0);
    stretched.shown_width = 4U;
    stretched.shown_height = 1U;
    TestTarget wide = make_target(4U, 1U, 0U, false);
    require(detail::composite_part_raster(stretched, wide.target, whole(4U, 1U), 255U) ==
                detail::TextCompositeStatus::success,
            "a stretched raster composites");
    // Pixel 0 samples -1/4 of a texel: three quarters red over nothing;
    // pixel 1 a quarter of the way from red to blue; the rest mirror them.
    require(wide.pixels[0] == pack(191U, 0U, 0U, 191U), "the edge fades out");
    require(wide.pixels[1] == pack(191U, 0U, 64U, 255U), "red leans toward blue");
    require(wide.pixels[2] == pack(64U, 0U, 191U, 255U), "blue leans toward red");
    require(wide.pixels[3] == pack(0U, 0U, 191U, 191U), "the far edge fades out");

    // Shown at its own size, the scaled path is the unscaled one.
    const std::vector<std::uint8_t> grey = solid_source(3U, 2U, {40U, 50U, 60U, 128U});
    PartRasterSource same = source_of(grey, 3U, 2U, 1, 1);
    same.shown_width = 3U;
    same.shown_height = 2U;
    TestTarget shown = make_target(5U, 4U, pack(9U, 8U, 7U, 255U), true);
    TestTarget plain = shown;
    plain.target.pixels = std::span<std::uint32_t>(plain.pixels.data(), plain.pixels.size());
    static_cast<void>(detail::composite_part_raster(same, shown.target, whole(5U, 4U), 200U));
    static_cast<void>(detail::composite_part_raster(source_of(grey, 3U, 2U, 1, 1), plain.target, whole(5U, 4U), 200U));
    require(shown.pixels == plain.pixels, "a raster shown at its own size writes the unscaled bytes");

    // A solid raster shrunk or grown is solid inside, under the clip.
    const std::vector<std::uint8_t> solid = solid_source(4U, 4U, {10U, 20U, 30U, 255U});
    PartRasterSource grown = source_of(solid, 4U, 4U, 0, 0);
    grown.shown_width = 6U;
    grown.shown_height = 6U;
    TestTarget large = make_target(8U, 8U, 0U, false);
    TextCompositeClip clip{};
    clip.rect = {0.0, 0.0, 5.0, 8.0};
    static_cast<void>(detail::composite_part_raster(grown, large.target, clip, 255U));
    require(large.pixels[2U * 8U + 2U] == pack(10U, 20U, 30U, 255U), "the inside is solid");
    require(large.pixels[2U * 8U + 5U] == 0U && large.pixels[6U * 8U + 2U] == 0U,
            "nothing is written beyond the clip or the shown size");

    PartRasterSource huge = source_of(solid, 4U, 4U, 0, 0);
    huge.shown_width = 0xffffffffU;
    TestTarget untouched = make_target(2U, 2U, 0U, false);
    require(detail::composite_part_raster(huge, untouched.target, whole(2U, 2U), 255U) ==
                detail::TextCompositeStatus::invalid_geometry,
            "an unbounded shown size is refused");
}

PartRasterSource added_face(const std::vector<std::uint8_t>& bytes) {
    PartRasterSource source = source_of(bytes, 1U, 1U, 0, 0);
    source.blend = detail::PartCompositeBlend::plus;
    return source;
}

// The plus blend (E7r cross-fade): each weighted channel added, clamped.
// An old face at 1 - t and a new one added at t sum to one face.
void test_plus_blend() {
    // Red 100, green 50, blue 0, alpha 128, as BGRA bytes.
    const std::vector<std::uint8_t> half{0U, 50U, 100U, 128U};
    PartRasterSource added = source_of(half, 1U, 1U, 0, 0);
    added.blend = detail::PartCompositeBlend::plus;
    TestTarget sum = make_target(1U, 1U, pack(200U, 10U, 5U, 200U), false);
    require(detail::composite_part_raster(added, sum.target, whole(1U, 1U), 255U) ==
                detail::TextCompositeStatus::success,
            "a plus source composites");
    require(sum.pixels[0] == pack(255U, 60U, 5U, 255U), "channels add and clamp at 255");

    // The same opaque face faded out and in sums to itself within a step.
    // Red 90, green 140, blue 200, opaque, as BGRA bytes.
    const std::vector<std::uint8_t> face{200U, 140U, 90U, 255U};
    for (unsigned share = 0U; share <= 255U; share = share + 15U) {
        TestTarget group = make_target(1U, 1U, 0U, false);
        static_cast<void>(detail::composite_part_raster(source_of(face, 1U, 1U, 0, 0), group.target, whole(1U, 1U),
                                                        static_cast<std::uint8_t>(255U - share)));
        static_cast<void>(detail::composite_part_raster(added_face(face), group.target, whole(1U, 1U),
                                                        static_cast<std::uint8_t>(share)));
        const std::uint32_t pixel = group.pixels[0];
        const unsigned red = (pixel >> 16U) & 255U;
        const unsigned alpha = pixel >> 24U;
        require(red + 1U >= 90U && red <= 91U && alpha + 1U >= 255U, "a face faded into itself stays itself");
    }
}

// Multiply and screen (E9.6): CSS's separable blend modes in bytes, each
// product rounded once.
void test_separable_blends() {
    // Red 100, green 50, blue 0, alpha 128, as BGRA bytes.
    const std::vector<std::uint8_t> half{0U, 50U, 100U, 128U};
    const std::uint32_t beneath = pack(200U, 10U, 5U, 200U);
    PartRasterSource multiplied = source_of(half, 1U, 1U, 0, 0);
    multiplied.blend = detail::PartCompositeBlend::multiply;
    TestTarget product = make_target(1U, 1U, beneath, false);
    require(detail::composite_part_raster(multiplied, product.target, whole(1U, 1U), 255U) ==
                detail::TextCompositeStatus::success,
            "a multiply source composites");
    // (s(255 − αd) + d(255 − αs) + sd + 127) / 255; alpha αs + αd − αsαd.
    require(product.pixels[0] == pack(200U, 18U, 2U, 228U), "multiply in bytes");
    PartRasterSource screened = source_of(half, 1U, 1U, 0, 0);
    screened.blend = detail::PartCompositeBlend::screen;
    TestTarget lifted = make_target(1U, 1U, beneath, false);
    static_cast<void>(detail::composite_part_raster(screened, lifted.target, whole(1U, 1U), 255U));
    // s + d − (sd + 127) / 255.
    require(lifted.pixels[0] == pack(222U, 58U, 5U, 228U), "screen in bytes");

    // Multiply over opaque white, and screen over opaque black, are the
    // source; an opaque target stays opaque.
    // Red 90, green 140, blue 200, opaque, as BGRA bytes.
    const std::vector<std::uint8_t> face{200U, 140U, 90U, 255U};
    PartRasterSource over_white = source_of(face, 1U, 1U, 0, 0);
    over_white.blend = detail::PartCompositeBlend::multiply;
    TestTarget white = make_target(1U, 1U, pack(255U, 255U, 255U, 255U), true);
    static_cast<void>(detail::composite_part_raster(over_white, white.target, whole(1U, 1U), 255U));
    require(white.pixels[0] == pack(90U, 140U, 200U, 255U), "multiply over white is the source");
    PartRasterSource over_black = source_of(face, 1U, 1U, 0, 0);
    over_black.blend = detail::PartCompositeBlend::screen;
    TestTarget black = make_target(1U, 1U, pack(0U, 0U, 0U, 255U), true);
    static_cast<void>(detail::composite_part_raster(over_black, black.target, whole(1U, 1U), 255U));
    require(black.pixels[0] == pack(90U, 140U, 200U, 255U), "screen over black is the source");

    require(detail::part_composite_blend(PaintGroupComposite::source_over) == detail::PartCompositeBlend::source_over &&
                detail::part_composite_blend(PaintGroupComposite::plus) == detail::PartCompositeBlend::plus &&
                detail::part_composite_blend(PaintGroupComposite::multiply) == detail::PartCompositeBlend::multiply &&
                detail::part_composite_blend(PaintGroupComposite::screen) == detail::PartCompositeBlend::screen &&
                detail::part_composite_blend(static_cast<PaintGroupComposite>(9U)) ==
                    detail::PartCompositeBlend::source_over,
            "each group composite has its blend; another value is source-over");
}

void test_part_raster() {
    const std::shared_ptr<PartRaster> raster = PartRaster::create(DeviceExtent{3U, 2U}, DeviceScale{1.5});
    require(raster != nullptr, "a raster is made");
    require((*raster).row_bytes() == 12U && (*raster).pixels().size() == 24U, "rows are tight");
    bool transparent = true;
    for (const std::uint8_t byte : (*raster).pixels()) {
        if (byte != 0U) transparent = false;
    }
    require(transparent, "a new raster is transparent");
    const PartRasterView view = (*raster).view();
    require(validate_raster_view(view) == RasterViewDefect::none, "its view validates");
    require(PartRaster::create(DeviceExtent{maximum_part_raster_side + 1U, 1U}, DeviceScale{1.0}) == nullptr,
            "an oversized raster is refused");
    require(PartRaster::create(DeviceExtent{1U, 1U}, DeviceScale{0.25}) == nullptr, "a scale below 0.5 is refused");
    require(PartRaster::create(DeviceExtent{1U, 1U}, DeviceScale{std::nan("")}) == nullptr, "a NaN scale is refused");
}

// -- the retained command --------------------------------------------------------

struct PartRasterCall final {
    std::shared_ptr<const PartRaster> raster;
    Point origin{};
    double opacity{};
    // Set for a scaled call: its destination.
    std::optional<Rect> destination{};
};

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
    void draw_image(ImageId, Rect, double) override {}
    void draw_part_raster(const std::shared_ptr<const PartRaster>& raster, Point origin, double opacity) override {
        calls.push_back(PartRasterCall{raster, origin, opacity, std::nullopt});
    }
    void draw_part_raster_scaled(const std::shared_ptr<const PartRaster>& raster, Rect destination,
                                 double opacity) override {
        calls.push_back(PartRasterCall{raster, Point{destination.x, destination.y}, opacity, destination});
    }

    std::vector<PartRasterCall> calls;
};

class RasterControl final : public Control {
public:
    explicit RasterControl(StableId id) : Control(std::move(id)) {}

    void on_paint(Painter& painter, Rect) override {
        if (stretched) {
            painter.draw_part_raster_scaled(raster, Rect{2.0, 3.0, 30.0, 15.0}, 0.5);
            return;
        }
        painter.draw_part_raster(raster, Point{3.0, 4.0}, 0.75);
    }

    std::shared_ptr<const PartRaster> raster;
    bool stretched{};
};

void test_command_is_recorded_replayed_and_inspected() {
    const std::shared_ptr<Control> root = make_control<Control>(StableId("root"));
    const std::shared_ptr<RasterControl> control = make_control<RasterControl>(StableId("raster"));
    (*control).set_requested_bounds({10.0, 10.0, 40.0, 30.0});
    const std::shared_ptr<PartRaster> made = PartRaster::create(DeviceExtent{20U, 10U}, DeviceScale{1.0});
    require(made != nullptr, "a raster is made");
    (*control).raster = made;
    (*root).add_child(control);
    Window window(root, Size{100.0, 80.0});
    window.perform_layout();

    CapturePainter first;
    static_cast<void>(window.paint(first, Rect{0.0, 0.0, 100.0, 80.0}));
    require(first.calls.size() == 1U, "the command replays once");
    require(first.calls[0].raster == made, "the recording keeps the very raster");
    require(first.calls[0].origin.x == 3.0 && first.calls[0].origin.y == 4.0 && first.calls[0].opacity == 0.75,
            "origin and opacity replay unchanged");

    // The recording owns a reference: the raster outlives its maker's.
    const std::weak_ptr<const PartRaster> observed = made;
    (*control).raster.reset();
    require(!observed.expired(), "the recorded command keeps the raster alive");
    CapturePainter second;
    static_cast<void>(window.paint(second, Rect{0.0, 0.0, 100.0, 80.0}));
    require(second.calls.size() == 1U && second.calls[0].raster == first.calls[0].raster,
            "a reused display chunk replays the kept raster");

    const VisualInspectionSnapshot snapshot = window.visual_inspection_snapshot();
    const VisualControlInspection* const inspected = snapshot.find("raster");
    require(inspected != nullptr && (*inspected).display_operations.size() == 1U, "the operation is inspectable");
    const VisualPaintOperationSnapshot& operation = (*inspected).display_operations[0];
    require(operation.operation == VisualPaintOperation::draw_part_raster, "it is named draw_part_raster");
    require(std::string(visual_paint_operation_name(operation.operation)) == "draw_part_raster",
            "its registered name is draw_part_raster");
    require(operation.rect.x == 3.0 && operation.rect.y == 4.0 && operation.rect.width == 20.0 &&
                operation.rect.height == 10.0 && operation.scalar == 0.75,
            "inspection reports the raster's logical bounds and opacity");

    // A stretched raster records, replays and inspects its destination.
    (*control).raster = made;
    (*control).stretched = true;
    (*control).invalidate(Dirty::paint);
    CapturePainter scaled;
    static_cast<void>(window.paint(scaled, Rect{0.0, 0.0, 100.0, 80.0}));
    require(scaled.calls.size() == 1U && scaled.calls[0].destination.has_value() &&
                *scaled.calls[0].destination == (Rect{2.0, 3.0, 30.0, 15.0}) && scaled.calls[0].opacity == 0.5,
            "a scaled raster replays its destination and opacity");
    const VisualInspectionSnapshot stretched_snapshot = window.visual_inspection_snapshot();
    const VisualControlInspection* const stretched_control = stretched_snapshot.find("raster");
    require(stretched_control != nullptr && (*stretched_control).display_operations.size() == 1U,
            "the scaled operation is inspectable");
    const VisualPaintOperationSnapshot& stretched_operation = (*stretched_control).display_operations[0];
    require(stretched_operation.operation == VisualPaintOperation::draw_part_raster &&
                stretched_operation.rect == (Rect{2.0, 3.0, 30.0, 15.0}),
            "inspection names it draw_part_raster at its destination");
    (*control).stretched = false;

    // The base painter draws nothing, and a null raster is not recorded.
    (*control).raster = nullptr;
    (*control).invalidate(Dirty::paint);
    CapturePainter third;
    static_cast<void>(window.paint(third, Rect{0.0, 0.0, 100.0, 80.0}));
    require(third.calls.empty(), "a null raster records nothing");
}

} // namespace

// Copy (stage T5): the weighted source replaces the target, transparent
// source pixels included; full weight writes it exactly, and a partly
// covered pixel keeps the rest of the target.
void test_copy_blend() {
    const std::uint32_t beneath = pack(200U, 10U, 5U, 200U);
    // Red 100, green 50, blue 0, alpha 128, as BGRA bytes.
    const std::vector<std::uint8_t> half{0U, 50U, 100U, 128U};
    PartRasterSource copied = source_of(half, 1U, 1U, 0, 0);
    copied.blend = detail::PartCompositeBlend::copy;
    TestTarget replaced = make_target(1U, 1U, beneath, false);
    require(detail::composite_part_raster(copied, replaced.target, whole(1U, 1U), 255U) ==
                detail::TextCompositeStatus::success,
            "a copy source composites");
    require(replaced.pixels[0] == pack(100U, 50U, 0U, 128U), "copy writes the source exactly");

    const std::vector<std::uint8_t> clear{0U, 0U, 0U, 0U};
    PartRasterSource cleared = source_of(clear, 1U, 1U, 0, 0);
    cleared.blend = detail::PartCompositeBlend::copy;
    TestTarget emptied = make_target(1U, 1U, beneath, false);
    static_cast<void>(detail::composite_part_raster(cleared, emptied.target, whole(1U, 1U), 255U));
    require(emptied.pixels[0] == 0U, "copying a transparent pixel clears the target");
    TestTarget scaled_target = make_target(4U, 4U, beneath, false);
    const std::vector<std::uint8_t> scaled_bytes = solid_source(2U, 2U, {0U, 0U, 0U, 0U});
    PartRasterSource scaled_clear = source_of(scaled_bytes, 2U, 2U, 0, 0);
    scaled_clear.shown_width = 4U;
    scaled_clear.shown_height = 4U;
    scaled_clear.blend = detail::PartCompositeBlend::copy;
    static_cast<void>(detail::composite_part_raster(scaled_clear, scaled_target.target, whole(4U, 4U), 255U));
    require(scaled_target.pixels[0] == 0U && scaled_target.pixels[15] == 0U,
            "a scaled transparent copy clears the target");

    // Half weight (opacity 255, clip coverage 128): each channel half the
    // source plus half the target, rounded once each.
    TestTarget edge = make_target(1U, 1U, beneath, false);
    TextCompositeClip half_column{};
    half_column.rect = {0.0, 0.0, 0.5, 1.0};
    static_cast<void>(detail::composite_part_raster(copied, edge.target, half_column, 255U));
    require(edge.pixels[0] == pack(150U, 30U, 3U, 164U), "a partly covered pixel keeps the rest");

    // An opaque target stays opaque; outside the clip nothing is written.
    TestTarget opaque = make_target(2U, 1U, pack(9U, 9U, 9U, 255U), true);
    const std::vector<std::uint8_t> wide_bytes = solid_source(2U, 1U, {0U, 0U, 0U, 0U});
    PartRasterSource wide = source_of(wide_bytes, 2U, 1U, 0, 0);
    wide.blend = detail::PartCompositeBlend::copy;
    TextCompositeClip first_column{};
    first_column.rect = {0.0, 0.0, 1.0, 1.0};
    static_cast<void>(detail::composite_part_raster(wide, opaque.target, first_column, 255U));
    require(opaque.pixels[0] == pack(0U, 0U, 0U, 255U) &&
                opaque.pixels[1] == pack(9U, 9U, 9U, 255U),
            "copy onto an opaque target writes alpha 255 within the clip only");

    require(detail::part_composite_blend(PaintGroupComposite::copy) == detail::PartCompositeBlend::copy,
            "the copy group composite has the copy blend");
}

// The interior (pixels whose clip coverage is a whole unit) skips the
// coverage arithmetic, and under a rounded clip of radius 0 its bands are
// the whole interior: a rectangular clip and that rounded clip around the
// whole target give the same bytes. The interior against the per-pixel
// formula is test_blocks_match_the_reference.
void test_interior_shortcut_matches_coverage() {
    constexpr std::uint32_t width = 13U;
    constexpr std::uint32_t height = 9U;
    // Premultiplied pixels of varied alpha: opaque, clear and between.
    std::vector<std::uint8_t> bytes{};
    bytes.reserve(static_cast<std::size_t>(width) * height * 4U);
    for (std::uint32_t index = 0U; index < width * height; ++index) {
        const std::uint32_t alpha_choice = index % 5U;
        unsigned alpha = 255U;
        if (alpha_choice == 1U) alpha = 0U;
        if (alpha_choice == 2U) alpha = 128U;
        if (alpha_choice == 3U) alpha = 37U;
        const unsigned red = (index * 7U) % (alpha + 1U);
        const unsigned green = (index * 13U) % (alpha + 1U);
        const unsigned blue = (index * 29U) % (alpha + 1U);
        bytes.push_back(static_cast<std::uint8_t>(blue));
        bytes.push_back(static_cast<std::uint8_t>(green));
        bytes.push_back(static_cast<std::uint8_t>(red));
        bytes.push_back(static_cast<std::uint8_t>(alpha));
    }
    constexpr std::array<detail::PartCompositeBlend, 5> blends{
        detail::PartCompositeBlend::source_over, detail::PartCompositeBlend::plus,
        detail::PartCompositeBlend::multiply, detail::PartCompositeBlend::screen, detail::PartCompositeBlend::copy};
    constexpr std::array<std::uint8_t, 2> opacities{255U, 128U};
    constexpr std::array<Rect, 3> clips{{{1.25, 0.5, 10.5, 7.75}, {0.0, 0.0, 15.0, 11.0}, {3.0, 2.0, 6.0, 4.0}}};
    for (const detail::PartCompositeBlend blend : blends) {
        for (const std::uint8_t opacity : opacities) {
            for (const Rect clip_rect : clips) {
                for (const bool opaque : {false, true}) {
                    PartRasterSource source = source_of(bytes, width, height, 1, 1);
                    source.blend = blend;
                    const std::uint32_t fill = opaque ? pack(90U, 140U, 200U, 255U) : pack(40U, 60U, 20U, 120U);
                    TestTarget shortcut = make_target(width + 2U, height + 2U, fill, opaque);
                    TestTarget covered = make_target(width + 2U, height + 2U, fill, opaque);
                    TextCompositeClip rectangular{};
                    rectangular.rect = clip_rect;
                    TextCompositeClip rounded = rectangular;
                    rounded.rounded = true;
                    rounded.rounded_rect = {0.0, 0.0, static_cast<double>(width + 2U),
                                            static_cast<double>(height + 2U)};
                    rounded.radius = 0.0;
                    require(detail::composite_part_raster(source, shortcut.target, rectangular, opacity) ==
                                TextCompositeStatus::success,
                            "the rectangular clip composites");
                    require(detail::composite_part_raster(source, covered.target, rounded, opacity) ==
                                TextCompositeStatus::success,
                            "the rounded clip composites");
                    require(shortcut.pixels == covered.pixels,
                            "the interior shortcut matches the per-pixel coverage path");
                }
            }
        }
    }
}

// A scaled source: the stepped sample positions and the direct texel
// fetches give the bytes of the divided, edge-tested path, which a rounded
// clip of radius 0 cannot force, so the reference here is the per-pixel
// formula itself (sample_position and the bilinear weights as documented).
void test_scaled_steps_match_the_formula() {
    constexpr std::uint32_t source_width = 5U;
    constexpr std::uint32_t source_height = 4U;
    std::vector<std::uint8_t> bytes{};
    for (std::uint32_t index = 0U; index < source_width * source_height; ++index) {
        const unsigned alpha = 60U + (index * 31U) % 196U;
        bytes.push_back(static_cast<std::uint8_t>((index * 11U) % (alpha + 1U)));
        bytes.push_back(static_cast<std::uint8_t>((index * 23U) % (alpha + 1U)));
        bytes.push_back(static_cast<std::uint8_t>((index * 41U) % (alpha + 1U)));
        bytes.push_back(static_cast<std::uint8_t>(alpha));
    }
    constexpr std::array<std::array<std::uint32_t, 2>, 3> shown{{{13U, 9U}, {7U, 11U}, {3U, 2U}}};
    for (const std::array<std::uint32_t, 2>& size : shown) {
        PartRasterSource source = source_of(bytes, source_width, source_height, 1, 2);
        source.shown_width = size[0];
        source.shown_height = size[1];
        const std::uint32_t width = size[0] + 3U;
        const std::uint32_t height = size[1] + 4U;
        TestTarget target = make_target(width, height, 0U, false);
        require(detail::composite_part_raster(source, target.target, whole(width, height), 255U) ==
                    TextCompositeStatus::success,
                "a scaled source composites");
        for (std::uint32_t y = 0U; y < height; ++y) {
            for (std::uint32_t x = 0U; x < width; ++x) {
                const std::int64_t shown_x = static_cast<std::int64_t>(x) - 1;
                const std::int64_t shown_y = static_cast<std::int64_t>(y) - 2;
                std::uint32_t expected = 0U;
                if (shown_x >= 0 && shown_y >= 0 && shown_x < static_cast<std::int64_t>(size[0]) &&
                    shown_y < static_cast<std::int64_t>(size[1])) {
                    const std::int64_t across =
                        (2 * shown_x + 1) * static_cast<std::int64_t>(source_width) * 256 / (2 * size[0]) - 128;
                    const std::int64_t down =
                        (2 * shown_y + 1) * static_cast<std::int64_t>(source_height) * 256 / (2 * size[1]) - 128;
                    const std::int64_t texel_x = across >> 8;
                    const std::int64_t texel_y = down >> 8;
                    const std::uint64_t after = static_cast<std::uint64_t>(across & 255);
                    const std::uint64_t below = static_cast<std::uint64_t>(down & 255);
                    std::array<std::uint64_t, 4> channels{};
                    constexpr std::array<std::size_t, 4> order{2U, 1U, 0U, 3U};
                    for (std::size_t channel = 0U; channel < 4U; ++channel) {
                        std::array<std::uint64_t, 4> texels{};
                        for (std::size_t corner = 0U; corner < 4U; ++corner) {
                            const std::int64_t tx = texel_x + static_cast<std::int64_t>(corner % 2U);
                            const std::int64_t ty = texel_y + static_cast<std::int64_t>(corner / 2U);
                            if (tx < 0 || ty < 0 || tx >= static_cast<std::int64_t>(source_width) ||
                                ty >= static_cast<std::int64_t>(source_height)) {
                                continue;
                            }
                            texels[corner] = bytes[static_cast<std::size_t>(ty * source_width + tx) * 4U + order[channel]];
                        }
                        const std::uint64_t upper = texels[0] * (256U - after) + texels[1] * after;
                        const std::uint64_t lower = texels[2] * (256U - after) + texels[3] * after;
                        channels[channel] = (upper * (256U - below) + lower * below + 32768U) >> 16U;
                    }
                    // Over a transparent target at full weight the result is
                    // the weighted source itself.
                    if (channels[3] != 0U) {
                        expected = pack(static_cast<unsigned>(channels[0]), static_cast<unsigned>(channels[1]),
                                        static_cast<unsigned>(channels[2]), static_cast<unsigned>(channels[3]));
                    }
                }
                require(target.pixels[static_cast<std::size_t>(y) * width + x] == expected,
                        "a stepped scaled sample equals the formula");
            }
        }
    }
}


// -- the block kernels against the documented formulas -------------------------------
//
// The interior's block kernels (text_composite.cpp) must give the per-pixel
// path's bytes. The reference here is the formulas as the header states
// them, pixel by pixel, with each pixel's clip coverage from
// text_clip_coverage; randomized blits cover rectangular, rounded and
// nested rounded clips on whole and fractional pixels, scaled sources and
// opaque sources recorded as such.

// The two byte identities the kernels rest on, checked over their whole
// domains: the 16-bit lane form of rounded_quotient_by_255 is the rounded
// quotient for every product of two bytes, and a channel weighted at
// opacity o times a whole unit of clip coverage is that rounded product.
void test_block_identities_are_exact() {
    for (unsigned value = 0U; value <= 65025U; ++value) {
        const std::uint16_t lane = static_cast<std::uint16_t>(value);
        const std::uint16_t biased = static_cast<std::uint16_t>(lane + 128U);
        const std::uint16_t carried = static_cast<std::uint16_t>(biased + (biased >> 8U));
        const unsigned quotient = static_cast<std::uint16_t>(carried >> 8U);
        require(quotient == (value + 127U) / 255U, "the 16-bit quotient lane is the rounded quotient");
        require(detail::rounded_quotient_by_255(value) == quotient, "the lane form is rounded_quotient_by_255");
    }
    for (std::uint64_t channel = 0U; channel <= 255U; ++channel) {
        for (std::uint64_t opacity = 0U; opacity <= 255U; ++opacity) {
            const std::uint64_t wide = (channel * opacity * 256U + 32640U) / 65280U;
            const unsigned product = static_cast<unsigned>(channel * opacity);
            require(wide == detail::rounded_quotient_by_255(product),
                    "a whole-unit weight is the rounded product of channel and opacity");
        }
    }
}

// A small deterministic generator (xorshift32), so that a failure repeats.
struct TestRandom final {
    std::uint32_t state{0x2026111bU};
};

std::uint32_t next_random(TestRandom& random) {
    std::uint32_t value = random.state;
    value ^= value << 13U;
    value ^= value >> 17U;
    value ^= value << 5U;
    random.state = value;
    return value;
}

std::uint32_t random_below(TestRandom& random, const std::uint32_t bound) {
    const std::uint32_t value = next_random(random) % bound;
    return value;
}

// A clip on whole pixels or on 1/256 of one, sometimes rounded, sometimes
// with an outer rounded clip, sometimes reaching outside the target.
TextCompositeClip random_clip(TestRandom& random, const std::uint32_t width, const std::uint32_t height) {
    const bool whole_pixels = random_below(random, 2U) == 0U;
    const double step = whole_pixels ? 1.0 : 1.0 / 256.0;
    const std::uint32_t scale = whole_pixels ? 1U : 256U;
    TextCompositeClip clip{};
    clip.rect.x = static_cast<double>(static_cast<std::int64_t>(random_below(random, width * scale)) - 8) * step;
    clip.rect.y = static_cast<double>(static_cast<std::int64_t>(random_below(random, height * scale)) - 8) * step;
    clip.rect.width = static_cast<double>(random_below(random, width * scale)) * step;
    clip.rect.height = static_cast<double>(random_below(random, height * scale)) * step;
    if (random_below(random, 2U) == 0U) {
        clip.rounded = true;
        clip.rounded_rect.x = clip.rect.x + static_cast<double>(static_cast<std::int64_t>(random_below(random, 9U)) - 4) * step;
        clip.rounded_rect.y = clip.rect.y + static_cast<double>(static_cast<std::int64_t>(random_below(random, 9U)) - 4) * step;
        clip.rounded_rect.width = clip.rect.width + static_cast<double>(random_below(random, 9U)) * step;
        clip.rounded_rect.height = clip.rect.height + static_cast<double>(random_below(random, 9U)) * step;
        clip.radius = static_cast<double>(random_below(random, 3000U)) / 100.0;
        if (random_below(random, 3U) == 0U) {
            clip.outer_rounded = true;
            clip.outer_rounded_rect.x = clip.rect.x - static_cast<double>(random_below(random, 6U));
            clip.outer_rounded_rect.y = clip.rect.y - static_cast<double>(random_below(random, 6U));
            clip.outer_rounded_rect.width = clip.rect.width + static_cast<double>(random_below(random, 12U));
            clip.outer_rounded_rect.height = clip.rect.height + static_cast<double>(random_below(random, 12U));
            clip.outer_radius = static_cast<double>(random_below(random, 2000U)) / 100.0;
        }
    }
    return clip;
}

// A source pixel's premultiplied red, green, blue and alpha.
using SourceChannels = std::array<std::uint64_t, 4>;

// Source-over of `source` at weight w = opacity x clip coverage, as the
// header states it: s' = (s * w + 32640) / 65280 per channel, alpha
// included; out = min(255, s' + (d * (255 - alpha') + 127) / 255), alpha
// 255 on an opaque target. A pixel whose weighted alpha is 0 is left as it
// is.
std::uint32_t reference_over(const std::uint32_t previous, const SourceChannels& source, const std::uint64_t weight,
                             const bool opaque) {
    std::array<std::uint64_t, 4> weighted{};
    for (std::size_t channel = 0U; channel < 4U; ++channel) {
        weighted[channel] = (source[channel] * weight + 32640U) / 65280U;
    }
    if (weighted[3] == 0U) return previous;
    const std::uint64_t remaining = 255U - weighted[3];
    constexpr std::array<unsigned, 4> shifts{16U, 8U, 0U, 24U};
    std::uint32_t result = 0U;
    for (std::size_t channel = 0U; channel < 4U; ++channel) {
        const std::uint64_t destination = (previous >> shifts[channel]) & 255U;
        std::uint64_t out = std::min<std::uint64_t>(255U, weighted[channel] + (destination * remaining + 127U) / 255U);
        if (channel == 3U && opaque) out = 255U;
        result |= static_cast<std::uint32_t>(out) << shifts[channel];
    }
    return result;
}

// One source channel at texel (x, y): 0 beyond the source's edges.
std::uint64_t reference_texel(const std::vector<std::uint8_t>& bytes, const std::uint32_t width,
                              const std::uint32_t height, const std::int64_t x, const std::int64_t y,
                              const std::size_t byte) {
    if (x < 0 || y < 0 || x >= static_cast<std::int64_t>(width) || y >= static_cast<std::int64_t>(height)) return 0U;
    const std::uint64_t value = bytes[(static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)) * 4U + byte];
    return value;
}

// Shown pixel (x, y) of a source shown across shown_width x shown_height:
// its own texel unscaled, otherwise its centre sampled bilinearly as
// PartRasterSource states it.
SourceChannels reference_sample(const std::vector<std::uint8_t>& bytes, const std::uint32_t width,
                                const std::uint32_t height, const std::uint32_t shown_width,
                                const std::uint32_t shown_height, const std::int64_t x, const std::int64_t y) {
    constexpr std::array<std::size_t, 4> order{2U, 1U, 0U, 3U};
    SourceChannels channels{};
    if (shown_width == width && shown_height == height) {
        for (std::size_t channel = 0U; channel < 4U; ++channel) {
            channels[channel] = reference_texel(bytes, width, height, x, y, order[channel]);
        }
        return channels;
    }
    const std::int64_t across = (2 * x + 1) * static_cast<std::int64_t>(width) * 256 / (2 * static_cast<std::int64_t>(shown_width)) - 128;
    const std::int64_t down = (2 * y + 1) * static_cast<std::int64_t>(height) * 256 / (2 * static_cast<std::int64_t>(shown_height)) - 128;
    const std::int64_t texel_x = across >> 8;
    const std::int64_t texel_y = down >> 8;
    const std::uint64_t after = static_cast<std::uint64_t>(across & 255);
    const std::uint64_t below = static_cast<std::uint64_t>(down & 255);
    for (std::size_t channel = 0U; channel < 4U; ++channel) {
        const std::size_t byte = order[channel];
        const std::uint64_t upper = reference_texel(bytes, width, height, texel_x, texel_y, byte) * (256U - after) +
                                    reference_texel(bytes, width, height, texel_x + 1, texel_y, byte) * after;
        const std::uint64_t lower = reference_texel(bytes, width, height, texel_x, texel_y + 1, byte) * (256U - after) +
                                    reference_texel(bytes, width, height, texel_x + 1, texel_y + 1, byte) * after;
        channels[channel] = (upper * (256U - below) + lower * below + 32768U) >> 16U;
    }
    return channels;
}

// Source pixels of one of four kinds: random translucent, opaque, a part's
// opaque body with a soft edge and clear corners, or text-like runs.
enum class SourceKind : std::uint8_t { translucent, opaque, part_like, text_like };

std::vector<std::uint8_t> random_source(TestRandom& random, const std::uint32_t width, const std::uint32_t height,
                                        const SourceKind kind) {
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(width) * height * 4U, std::uint8_t{0U});
    for (std::uint32_t y = 0U; y < height; ++y) {
        for (std::uint32_t x = 0U; x < width; ++x) {
            unsigned alpha = 255U;
            if (kind == SourceKind::translucent) alpha = random_below(random, 256U);
            if (kind == SourceKind::part_like) {
                const std::uint32_t edge = std::min(std::min(x, width - 1U - x), std::min(y, height - 1U - y));
                const bool corner = (x < 3U || x + 3U >= width) && (y < 3U || y + 3U >= height);
                alpha = corner ? 0U : (edge == 0U ? 96U : 255U);
            }
            if (kind == SourceKind::text_like) {
                const std::uint32_t phase = (x + y * 3U) % 7U;
                alpha = phase < 2U ? 0U : (phase == 2U ? random_below(random, 256U) : 255U);
            }
            const std::size_t offset = (static_cast<std::size_t>(y) * width + x) * 4U;
            for (std::size_t channel = 0U; channel < 3U; ++channel) {
                const unsigned value = random_below(random, 256U);
                bytes[offset + channel] = static_cast<std::uint8_t>((value * alpha + 127U) / 255U);
            }
            bytes[offset + 3U] = static_cast<std::uint8_t>(alpha);
        }
    }
    return bytes;
}

std::vector<std::uint32_t> random_pixels(TestRandom& random, const std::size_t count, const bool opaque_alpha) {
    std::vector<std::uint32_t> pixels(count, 0U);
    for (std::uint32_t& pixel : pixels) {
        pixel = next_random(random);
        if (opaque_alpha) pixel |= 0xff000000U;
    }
    return pixels;
}

// Source-over part blits, unscaled and scaled, against reference_over.
void test_blocks_match_the_reference() {
    TestRandom random{};
    for (std::uint32_t round = 0U; round < 800U; ++round) {
        const std::uint32_t width = 30U + random_below(random, 110U);
        const std::uint32_t height = 16U + random_below(random, 24U);
        const std::uint32_t source_width = 1U + random_below(random, 180U);
        const std::uint32_t source_height = 1U + random_below(random, 40U);
        const SourceKind kind = static_cast<SourceKind>(random_below(random, 4U));
        const std::vector<std::uint8_t> bytes = random_source(random, source_width, source_height, kind);
        const bool opaque = random_below(random, 2U) == 0U;
        const std::vector<std::uint32_t> before = random_pixels(random, static_cast<std::size_t>(width) * height,
                                                                random_below(random, 2U) == 0U);
        const TextCompositeClip clip = random_clip(random, width, height);
        PartRasterSource source = source_of(bytes, source_width, source_height,
                                            static_cast<std::int64_t>(random_below(random, width + 20U)) - 20,
                                            static_cast<std::int64_t>(random_below(random, height + 20U)) - 20);
        if (random_below(random, 3U) == 0U) {
            source.shown_width = 1U + random_below(random, 220U);
            source.shown_height = 1U + random_below(random, 60U);
        }
        // The maker's record: only a wholly opaque source may claim it.
        source.opaque = kind == SourceKind::opaque && random_below(random, 2U) == 0U;
        const std::uint8_t opacity =
            random_below(random, 3U) == 0U ? std::uint8_t{255U} : static_cast<std::uint8_t>(random_below(random, 256U));
        std::vector<std::uint32_t> blocks = before;
        TextCompositeTarget target{};
        target.pixels = std::span<std::uint32_t>(blocks.data(), blocks.size());
        target.width = width;
        target.height = height;
        target.stride = width;
        target.opaque = opaque;
        require(detail::composite_part_raster(source, target, clip, opacity) == TextCompositeStatus::success,
                "a randomized part blit composites");
        const std::uint32_t shown_width = source.shown_width != 0U ? source.shown_width : source_width;
        const std::uint32_t shown_height = source.shown_height != 0U ? source.shown_height : source_height;
        for (std::uint32_t y = 0U; y < height; ++y) {
            for (std::uint32_t x = 0U; x < width; ++x) {
                const std::size_t at = static_cast<std::size_t>(y) * width + x;
                const std::int64_t shown_x = static_cast<std::int64_t>(x) - source.left;
                const std::int64_t shown_y = static_cast<std::int64_t>(y) - source.top;
                std::uint32_t expected = before[at];
                const std::uint64_t coverage = detail::text_clip_coverage(clip, x, y);
                const bool shown = shown_x >= 0 && shown_y >= 0 && shown_x < static_cast<std::int64_t>(shown_width) &&
                                   shown_y < static_cast<std::int64_t>(shown_height);
                if (shown && coverage != 0U) {
                    const SourceChannels channels = reference_sample(bytes, source_width, source_height, shown_width,
                                                                     shown_height, shown_x, shown_y);
                    expected = reference_over(before[at], channels, opacity * coverage, opaque);
                }
                require(blocks[at] == expected, "a part blit equals the source-over formula, pixel by pixel");
            }
        }
    }
}

// Text: alpha = (coverage x colour alpha x clip + 32640) / 65280, each
// channel (s * alpha + d * (255 - alpha) + 127) / 255, alpha 255 on an
// opaque target; source mode blends the premultiplied colour at the
// coverage x clip alone. A pixel at alpha 0 is left as it is.
std::uint32_t reference_text(const std::uint32_t previous, const std::uint64_t coverage, const Color colour,
                             const std::uint64_t clip, const bool opaque, const detail::TextCompositeMode mode) {
    const bool replaces = mode == detail::TextCompositeMode::source;
    const std::uint64_t colour_alpha = replaces ? 255U : colour.alpha;
    const std::uint64_t alpha = (coverage * colour_alpha * clip + 32640U) / 65280U;
    if (alpha == 0U) return previous;
    std::array<std::uint64_t, 4> source{colour.red, colour.green, colour.blue, 255U};
    if (replaces) {
        for (std::size_t channel = 0U; channel < 3U; ++channel) {
            source[channel] = (source[channel] * colour.alpha + 127U) / 255U;
        }
        source[3] = colour.alpha;
    }
    constexpr std::array<unsigned, 4> shifts{16U, 8U, 0U, 24U};
    std::uint32_t result = 0U;
    for (std::size_t channel = 0U; channel < 4U; ++channel) {
        const std::uint64_t destination = (previous >> shifts[channel]) & 255U;
        std::uint64_t out = (source[channel] * alpha + destination * (255U - alpha) + 127U) / 255U;
        if (channel == 3U && opaque) out = 255U;
        result |= static_cast<std::uint32_t>(out) << shifts[channel];
    }
    return result;
}

void test_text_blocks_match_the_reference() {
    TestRandom random{};
    random.state = 0x7e47b10cU;
    for (std::uint32_t round = 0U; round < 800U; ++round) {
        const std::uint32_t width = 30U + random_below(random, 110U);
        const std::uint32_t height = 16U + random_below(random, 24U);
        const std::uint32_t coverage_width = 1U + random_below(random, 180U);
        const std::uint32_t coverage_height = 1U + random_below(random, 40U);
        // Text-like runs (gaps, soft edges, solid stems) or noise.
        std::vector<std::uint8_t> coverage(static_cast<std::size_t>(coverage_width) * coverage_height, std::uint8_t{0U});
        const bool runs = random_below(random, 2U) == 0U;
        for (std::size_t index = 0U; index < coverage.size(); ++index) {
            const std::uint32_t roll = random_below(random, 100U);
            std::uint32_t value = roll < 60U ? 0U : (roll < 85U ? 255U : random_below(random, 256U));
            if (runs) value = (index % 11U) < 4U ? 0U : ((index % 11U) == 4U ? random_below(random, 256U) : 255U);
            coverage[index] = static_cast<std::uint8_t>(value);
        }
        const bool opaque = random_below(random, 2U) == 0U;
        const std::vector<std::uint32_t> before = random_pixels(random, static_cast<std::size_t>(width) * height,
                                                                random_below(random, 2U) == 0U);
        const TextCompositeClip clip = random_clip(random, width, height);
        const std::uint8_t colour_alpha =
            random_below(random, 2U) == 0U ? std::uint8_t{255U} : static_cast<std::uint8_t>(random_below(random, 256U));
        const Color colour = Color::rgba(static_cast<std::uint8_t>(random_below(random, 256U)),
                                         static_cast<std::uint8_t>(random_below(random, 256U)),
                                         static_cast<std::uint8_t>(random_below(random, 256U)), colour_alpha);
        const detail::TextCompositeMode mode = random_below(random, 4U) == 0U ? detail::TextCompositeMode::source
                                                                              : detail::TextCompositeMode::source_over;
        detail::TextCoverage source{};
        source.pixels = std::span<const std::uint8_t>(coverage.data(), coverage.size());
        source.width = coverage_width;
        source.height = coverage_height;
        source.left = static_cast<std::int64_t>(random_below(random, width + 20U)) - 20;
        source.top = static_cast<std::int64_t>(random_below(random, height + 20U)) - 20;
        std::vector<std::uint32_t> blocks = before;
        TextCompositeTarget target{};
        target.pixels = std::span<std::uint32_t>(blocks.data(), blocks.size());
        target.width = width;
        target.height = height;
        target.stride = width;
        target.opaque = opaque;
        require(detail::composite_text_coverage(source, target, clip, colour, mode) == TextCompositeStatus::success,
                "a randomized text blit composites");
        const bool draws = colour.alpha != 0U || mode == detail::TextCompositeMode::source;
        for (std::uint32_t y = 0U; y < height; ++y) {
            for (std::uint32_t x = 0U; x < width; ++x) {
                const std::size_t at = static_cast<std::size_t>(y) * width + x;
                const std::int64_t column = static_cast<std::int64_t>(x) - source.left;
                const std::int64_t row = static_cast<std::int64_t>(y) - source.top;
                std::uint32_t expected = before[at];
                const bool inside = column >= 0 && row >= 0 && column < static_cast<std::int64_t>(coverage_width) &&
                                    row < static_cast<std::int64_t>(coverage_height);
                if (draws && inside) {
                    const std::uint64_t covered = coverage[static_cast<std::size_t>(row) * coverage_width +
                                                           static_cast<std::size_t>(column)];
                    const std::uint64_t clipped = detail::text_clip_coverage(clip, x, y);
                    expected = reference_text(before[at], covered, colour, clipped, opaque, mode);
                }
                require(blocks[at] == expected, "a text blit equals the text formula, pixel by pixel");
            }
        }
    }
}

// A wholly opaque raster recorded as such copies its interior rows: the
// bytes are those of the same raster blended, and PartRaster records the
// opacity only while no view may still write it.
void test_opaque_rasters_copy_rows() {
    const std::shared_ptr<PartRaster> raster = PartRaster::create(DeviceExtent{6U, 4U}, DeviceScale{1.0});
    require(raster != nullptr, "a raster is made");
    const PartRasterView view = (*raster).view();
    for (std::size_t offset = 0U; offset < view.pixels.size(); offset += 4U) {
        view.pixels[offset] = static_cast<std::uint8_t>(offset);
        view.pixels[offset + 1U] = static_cast<std::uint8_t>(offset * 3U);
        view.pixels[offset + 2U] = static_cast<std::uint8_t>(offset * 7U);
        view.pixels[offset + 3U] = 255U;
    }
    require(!(*raster).opaque(), "a raster is not recorded opaque before its maker records it");
    (*raster).record_opacity();
    require((*raster).opaque(), "a wholly opaque raster is recorded opaque");
    const std::vector<std::uint8_t> bytes((*raster).pixels().begin(), (*raster).pixels().end());
    PartRasterSource copied = source_of(bytes, 6U, 4U, 2, 1);
    copied.opaque = true;
    const PartRasterSource blended = source_of(bytes, 6U, 4U, 2, 1);
    TestTarget first = make_target(10U, 7U, pack(10U, 20U, 30U, 255U), false);
    TestTarget second = make_target(10U, 7U, pack(10U, 20U, 30U, 255U), false);
    TextCompositeClip clip{};
    clip.rect = {2.5, 1.0, 6.0, 4.0};
    require(detail::composite_part_raster(copied, first.target, clip, 255U) == TextCompositeStatus::success,
            "the recorded raster composites");
    require(detail::composite_part_raster(blended, second.target, clip, 255U) == TextCompositeStatus::success,
            "the unrecorded raster composites");
    require(first.pixels == second.pixels, "copied rows equal blended ones");
    const PartRasterView again = (*raster).view();
    require(!(*raster).opaque(), "a new view clears the record");
    again.pixels[3] = 254U;
    (*raster).record_opacity();
    require(!(*raster).opaque(), "a raster with one translucent pixel is not recorded opaque");
}

int main() {
    test_opacity_quantization();
    test_blend_arithmetic();
    test_transparency_and_zero_opacity_write_nothing();
    test_placement_and_clip();
    test_refusals_write_nothing();
    test_premultiplied_sources_never_overflow();
    test_scaled_sampling();
    test_plus_blend();
    test_separable_blends();
    test_copy_blend();
    test_interior_shortcut_matches_coverage();
    test_scaled_steps_match_the_formula();
    test_block_identities_are_exact();
    test_blocks_match_the_reference();
    test_text_blocks_match_the_reference();
    test_opaque_rasters_copy_rows();
    test_part_raster();
    test_command_is_recorded_replayed_and_inspected();
    std::cout << "part composite tests passed\n";
    return 0;
}
