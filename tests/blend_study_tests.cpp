// The blend and opaque study (gui_forms/diagnostics/blend_study): the planes'
// copy and layer kernels count copied, blended and overdrawn device pixels
// and fill the depth histogram; a refused frame counts nothing but its
// refusal; the heat map tints inside its clip only; a window counts nothing
// while the study is off, and while it is on charges a control's opaque and
// translucent fills, its repeated opaque fill as overdraw, at device scale;
// an image the painter knows to be opaque but draws blended is counted, and
// one it draws as a copy is copied; the process switch reaches windows
// without a setting of their own.
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/diagnostics/blend_study/blend_study.hpp"
#include "gui_forms/resources/image_registry/image_registry.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/window/window.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "../src/core/image/png_writer.hpp"
#include "../src/core/image/resource_pixels.hpp"

namespace {

using namespace gui_forms;

class NullPainter : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void clip_rounded_rect(Rect, double) override {}
    void fill_rect(Rect, Color) override {}
    void fill_rounded_rect(Rect, double, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void stroke_rounded_rect(Rect, double, Color, double) override {}
    void fill_linear_gradient(Rect, Point, Point, std::span<const GradientStop>) override {}
    void fill_radial_gradient(Rect, Point, Size, std::span<const GradientStop>) override {}
    void draw_box_shadow(Rect, double, Point, double, double, Color) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}
};

// A painter that reports every image with the opacity it is given.
class ImageOpacityPainter final : public NullPainter {
public:
    explicit ImageOpacityPainter(const ImageDrawOpacity reported) : reported_(reported) {}
    [[nodiscard]] ImageDrawOpacity image_draw_opacity(ImageId) const noexcept override { return reported_; }

private:
    ImageDrawOpacity reported_{};
};

void expect(const bool condition, const std::string_view message) {
    if (condition) return;
    std::cerr << "blend study failure: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

std::uint64_t histogram_sum(const BlendFrameCounters& counters) {
    std::uint64_t sum = 0U;
    for (const std::uint64_t bin : counters.depth_histogram) sum += bin;
    return sum;
}

// An opaque black frame of `width` by `height` premultiplied BGRA pixels.
struct Frame final {
    std::uint32_t width{};
    std::uint32_t height{};
    std::vector<std::byte> pixels{};

    Frame(const std::uint32_t frame_width, const std::uint32_t frame_height)
        : width(frame_width), height(frame_height),
          pixels(static_cast<std::size_t>(frame_width) * frame_height * 4U, std::byte{0}) {
        for (std::size_t index = 3U; index < pixels.size(); index += 4U) pixels[index] = std::byte{255};
    }

    [[nodiscard]] InvalidationOverlayTarget target() {
        InvalidationOverlayTarget made{};
        made.pixels = std::span<std::byte>(pixels);
        made.width = width;
        made.height = height;
        made.row_bytes = static_cast<std::size_t>(width) * 4U;
        return made;
    }

    [[nodiscard]] unsigned channel(const std::uint32_t x, const std::uint32_t y, const std::size_t offset) const {
        const std::size_t index = (static_cast<std::size_t>(y) * width + x) * 4U + offset;
        const unsigned value = std::to_integer<unsigned>(pixels[index]);
        return value;
    }
};

void planes_kernels() {
    BlendStudyPlanes planes;
    const std::array<DevicePixelRect, 1> whole{{DevicePixelRect{0, 0, 10, 10}}};
    expect(planes.begin_frame(10U, 10U, whole), "a 10 x 10 frame begins");
    // A blended 4 x 4 write at the corner, then a copy of 2 x 2 inside it,
    // then a copy of the whole frame.
    planes.layer(DevicePixelRect{0, 0, 4, 4}, BlendWrite::blended);
    planes.replace(DevicePixelRect{1, 1, 3, 3});
    planes.layer(DevicePixelRect{2, 2, 6, 6}, BlendWrite::blended);
    const BlendFrameCounters first = planes.end_frame(whole);
    expect(first.blended_pixels == 32U, "two blended 4 x 4 writes count 32 pixels");
    expect(first.copied_pixels == 4U, "a 2 x 2 copy counts 4 pixels");
    expect(first.overdraw_pixels == 4U && first.overdraw_writes == 4U,
           "the copy hides the four pixels the blend wrote first");
    expect(histogram_sum(first) == 100U, "the histogram covers every painted pixel");
    // Depth 2: the copy's (2,2) square's single pixel at (2,2) is copy then
    // blend; (2..3, 4..5) etc. are blended over a blend.
    expect(first.depth_histogram[0] == 100U - 16U - 12U, "untouched pixels are bin 0");
    expect(first.depth_histogram[2] >= 1U, "a blend over a copy is depth two");

    const BlendHeatMapView view = planes.heat_map(BlendStudyMode::blend_heat_map);
    expect(view.width == 10U && view.height == 10U && view.classes.size() == 100U, "the view spans the planes");
    expect(view.classes[1U * 10U + 1U] == blend_class_copied, "a pixel only copied is copied");
    expect(view.classes[2U * 10U + 2U] == (blend_class_copied | blend_class_blended),
           "a copy blended over is both");
    expect(view.classes[0] == blend_class_blended, "a pixel only blended is blended");

    // The next frame clears only what it paints.
    const std::array<DevicePixelRect, 1> corner{{DevicePixelRect{0, 0, 2, 2}}};
    expect(planes.begin_frame(10U, 10U, corner), "a second frame begins");
    planes.replace(DevicePixelRect{0, 0, 10, 10});
    const BlendFrameCounters second = planes.end_frame(corner);
    expect(second.copied_pixels == 100U, "a whole-frame copy counts every pixel");
    // Last frame touched 28 pixels; the 4 painted again were cleared.
    expect(second.overdraw_pixels == 28U - 4U, "pixels outside the painted rectangle keep last frame's writes");
    expect(histogram_sum(second) == 4U && second.depth_histogram[1] == 4U,
           "the histogram covers the painted rectangle only");

    expect(!planes.begin_frame(0U, 10U, corner), "an empty frame is refused");
    planes.replace(DevicePixelRect{0, 0, 1, 1});
    const BlendFrameCounters refused = planes.end_frame(corner);
    expect(refused.refused == 1U && refused.copied_pixels == 0U, "a refused frame counts only its refusal");
    expect(!planes.begin_frame(70000U, 70000U, corner), "a frame over the limit is refused");
    planes.release();
    expect(planes.heat_map(BlendStudyMode::overdraw_heat_map).depth.empty(), "released planes show nothing");
}

void heat_map_composition() {
    BlendStudyPlanes planes;
    const std::array<DevicePixelRect, 1> whole{{DevicePixelRect{0, 0, 8, 8}}};
    expect(planes.begin_frame(8U, 8U, whole), "an 8 x 8 frame begins");
    planes.replace(DevicePixelRect{0, 0, 8, 8});
    planes.layer(DevicePixelRect{0, 0, 4, 8}, BlendWrite::blended);
    planes.layer(DevicePixelRect{0, 0, 2, 8}, BlendWrite::blended);
    static_cast<void>(planes.end_frame(whole));

    Frame depth(8U, 8U);
    compose_blend_heat_map(depth.target(), planes.heat_map(BlendStudyMode::overdraw_heat_map),
                           DevicePixelRect{0, 0, 8, 8});
    expect(depth.channel(6U, 0U, 1U) == 0U && depth.channel(6U, 0U, 0U) == 0U, "depth one is not tinted");
    expect(depth.channel(3U, 0U, 0U) > 100U, "depth two is tinted blue");
    expect(depth.channel(0U, 0U, 1U) > 80U, "depth three is tinted green");

    Frame classes(8U, 8U);
    compose_blend_heat_map(classes.target(), planes.heat_map(BlendStudyMode::blend_heat_map),
                           DevicePixelRect{0, 0, 6, 8});
    expect(classes.channel(5U, 0U, 1U) > 80U && classes.channel(5U, 0U, 2U) == 0U, "only copied is green");
    expect(classes.channel(0U, 0U, 2U) > 100U, "copied and blended over is orange");
    expect(classes.channel(7U, 0U, 1U) == 0U && classes.channel(7U, 0U, 2U) == 0U,
           "composition writes inside its clip only");

    Frame off(8U, 8U);
    compose_blend_heat_map(off.target(), planes.heat_map(BlendStudyMode::counters), DevicePixelRect{0, 0, 8, 8});
    expect(off.channel(0U, 0U, 2U) == 0U, "a counting-only view tints nothing");
}

// Paints an opaque fill of its bounds twice, a translucent fill and an
// opaque gradient strip, and an image.
class FillControl final : public Control {
public:
    using Control::Control;
    void on_paint(Painter& painter, Rect) override {
        const Rect local{0.0, 0.0, 50.0, 40.0};
        painter.fill_rect(local, Color::rgba(10, 20, 30));
        painter.fill_rect(local, Color::rgba(40, 50, 60));
        painter.fill_rect({0.0, 0.0, 10.0, 10.0}, Color::rgba(0, 0, 0, 128));
        const std::array<GradientStop, 2> stops{{{0.0, Color::rgba(0, 0, 0)}, {1.0, Color::rgba(255, 255, 255)}}};
        painter.fill_linear_gradient({0.0, 30.0, 50.0, 10.0}, {0.0, 30.0}, {50.0, 30.0}, stops);
        painter.draw_image(image, {20.0, 0.0, 10.0, 10.0}, 1.0);
    }
    ImageId image{};
};

void window_counts() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("blend.root"));
    std::shared_ptr<FillControl> fill = make_control<FillControl>(StableId("blend.fill"));
    (*fill).set_bounds({10.0, 10.0, 50.0, 40.0});
    (*root).add_child(fill);
    Window window(root, Size{200.0, 120.0});
    const std::vector<std::byte> opaque_pixels(16U, std::byte{255});
    const ImageLoadResult loaded = window.load_bgra32_premultiplied(2U, 2U, 8U, opaque_pixels);
    expect(static_cast<bool>(loaded), "a 2 x 2 opaque image loads");
    (*fill).image = loaded.image;
    window.perform_layout();
    NullPainter painter;

    window.set_blend_study(BlendStudyMode::off);
    static_cast<void>(window.paint(painter, {0.0, 0.0, 200.0, 120.0}));
    expect(window.blend_study_snapshot().frames == 0U, "off, a window counts nothing");
    expect(window.blend_heat_map().mode == BlendStudyMode::off, "off, a window shows no heat map");

    window.set_blend_study(BlendStudyMode::counters);
    static_cast<void>(window.paint(painter, {0.0, 0.0, 200.0, 120.0}));
    const BlendStudySnapshot one = window.blend_study_snapshot();
    expect(one.frames == 1U && one.mode == BlendStudyMode::counters, "on, a paint counts one frame");
    const BlendFrameCounters& frame = one.last;
    expect(frame.refused == 0U, "the frame is counted");
    expect(frame.copied_pixels >= 2U * 2000U, "both opaque fills of 50 x 40 are copies");
    expect(frame.overdraw_pixels >= 2000U && frame.overdraw_writes >= 2000U,
           "the second opaque fill hides the first");
    expect(frame.blended_pixels >= 100U, "the translucent fill blends");
    expect(frame.opaque_fills_blended >= 3U, "two opaque fills and an opaque gradient are issued source-over");
    expect(frame.draws_counted >= 5U, "every draw is charged");
    expect(histogram_sum(frame) == 200U * 120U, "the histogram covers the painted window");
    expect(frame.opaque_images_blended == 0U, "an image the painter knows nothing of is not counted opaque");
    expect(window.blend_heat_map().mode == BlendStudyMode::off, "counting only shows no heat map");

    window.set_scale(2.0);
    window.perform_layout();
    static_cast<void>(window.take_damage());
    static_cast<void>(window.paint(painter, {0.0, 0.0, 200.0, 120.0}));
    const BlendFrameCounters& doubled = window.blend_study_snapshot().last;
    expect(doubled.overdraw_pixels >= 4U * 2000U, "at scale 2 the overdraw is in device pixels");
    expect(histogram_sum(doubled) == 400U * 240U, "at scale 2 the histogram covers the device pixels");
    expect(window.blend_study_snapshot().frames == 2U &&
               window.blend_study_snapshot().totals.overdraw_pixels ==
                   one.last.overdraw_pixels + doubled.overdraw_pixels,
           "the sums add each frame once");

    ImageOpacityPainter blended_image(ImageDrawOpacity{true, false});
    static_cast<void>(window.paint(blended_image, {0.0, 0.0, 200.0, 120.0}));
    expect(window.blend_study_snapshot().last.opaque_images_blended == 1U,
           "an opaque image drawn blended is counted");
    ImageOpacityPainter copied_image(ImageDrawOpacity{true, true});
    static_cast<void>(window.paint(copied_image, {0.0, 0.0, 200.0, 120.0}));
    expect(window.blend_study_snapshot().last.opaque_images_blended == 0U,
           "an opaque image drawn as a copy is not counted");

    window.set_blend_study(BlendStudyMode::overdraw_heat_map);
    static_cast<void>(window.paint(painter, {0.0, 0.0, 200.0, 120.0}));
    const BlendHeatMapView view = window.blend_heat_map();
    expect(view.mode == BlendStudyMode::overdraw_heat_map && view.width == 400U && view.height == 240U,
           "a heat-map mode lends the planes at device size");

    window.reset_blend_study();
    expect(window.blend_study_snapshot().frames == 0U, "a reset zeroes the frames");
    window.set_blend_study(BlendStudyMode::off);
    expect(window.blend_heat_map().mode == BlendStudyMode::off, "turned off, the planes are released");
}

void process_switch() {
    const BlendStudyMode before = blend_study_default();
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("blend.switch"));
    Window following(root, Size{40.0, 30.0});
    std::shared_ptr<Panel> other = make_control<Panel>(StableId("blend.opted"));
    Window opted_out(other, Size{40.0, 30.0});
    opted_out.set_blend_study(BlendStudyMode::off);
    set_blend_study_default(BlendStudyMode::counters);
    expect(blend_study_default() == BlendStudyMode::counters, "the process switch reads counters");
    expect(following.blend_study_mode() == BlendStudyMode::counters,
           "a window without a setting follows the process switch");
    expect(opted_out.blend_study_mode() == BlendStudyMode::off, "a window's own setting wins");
    NullPainter painter;
    static_cast<void>(following.paint(painter, {0.0, 0.0, 40.0, 30.0}));
    expect(following.blend_study_snapshot().frames == 1U, "a following window counts");
    opted_out.set_blend_study(std::nullopt);
    expect(opted_out.blend_study_mode() == BlendStudyMode::counters, "a cleared setting follows the switch again");
    set_blend_study_default(before);
}

void counters_json() {
    BlendFrameCounters counters{};
    counters.copied_pixels = 3U;
    counters.depth_histogram[1] = 9U;
    BlendFrameCounters totals{};
    add_blend_frame(totals, counters);
    add_blend_frame(totals, counters);
    expect(totals.copied_pixels == 6U && totals.depth_histogram[1] == 18U, "sums add field by field");
    const std::string json = blend_frame_counters_json(totals);
    expect(json.find("\"copied_pixels\":6") != std::string::npos, "the JSON names each field");
    expect(json.find("\"depth_histogram\":[0,18,0") != std::string::npos, "the histogram is an array");
}

// The PNG path flags an image with no transparency at decode
// (ResourcePixelResult::every_pixel_opaque): an all-opaque file is flagged,
// one with a single transparent pixel is not.
bool decoded_opaque(const std::uint8_t corner_alpha) {
    std::vector<std::byte> pixels(4U * 4U * 4U, std::byte{255});
    pixels[3] = static_cast<std::byte>(corner_alpha);
    pixels[0] = static_cast<std::byte>(corner_alpha);
    pixels[1] = static_cast<std::byte>(corner_alpha);
    pixels[2] = static_cast<std::byte>(corner_alpha);
    image::PngEncodeInput input{};
    input.width = 4U;
    input.height = 4U;
    input.row_bytes = 16U;
    input.pixels = std::span<const std::byte>(pixels.data(), pixels.size());
    input.order = image::PngChannelOrder::bgra;
    input.alpha = image::PngSourceAlpha::premultiplied;
    std::vector<std::byte> encoded{};
    expect(image::encode_png(input, encoded), "the test image encodes");
    ImageRegistry registry;
    const ImageLoadResult loaded = registry.load_png(encoded);
    expect(static_cast<bool>(loaded), "the test image loads");
    const std::optional<ImageResourceView> view = registry.find(loaded.image);
    expect(view.has_value(), "the test image is found");
    std::vector<std::byte> decoded(4U * 4U * 4U, std::byte{0});
    image::ResourcePixelDecoder decoder;
    const image::PixelTarget target{image::PixelOrder::bgra, image::PixelAlpha::premultiplied};
    const image::ResourcePixelResult result = decoder.decode(*view, target, decoded, 16U);
    expect(result.error == image::ResourcePixelError::none, "the test image decodes");
    return result.every_pixel_opaque;
}

void decode_opacity() {
    expect(decoded_opaque(255U), "an image with no transparency is flagged opaque at decode");
    expect(!decoded_opaque(0U), "an image with a transparent pixel is not flagged opaque");
}

} // namespace

int main() {
    planes_kernels();
    heat_map_composition();
    window_counts();
    process_switch();
    counters_json();
    decode_opacity();
    std::cout << "blend study tests passed\n";
    return EXIT_SUCCESS;
}
