#include "skia_raster.hpp"
#include "gui_forms/engine/part_raster/part_raster.hpp"
#include "gui_forms/paint_framebuffer.hpp"

#include "gui_forms/live_surface.hpp"
#if defined(GUI_FORMS_PREPARED_TEXT)
#include "prepared_skia_frame.hpp"
#endif

#include "../../text/harfbuzz/bundled_face_loader.hpp"
#include "../../../core/resources/fonts/bundled_font_families.hpp"
#include "../../text/harfbuzz/harfbuzz_font_engine.hpp"
#include "../../text/glyph_run/glyph_run_raster.hpp"
#include "../../../core/text/composite/text_composite.hpp"
#include "../../../core/image/resource_pixels.hpp"
#include "../../../core/image/png_writer.hpp"
#include "../../../core/damage/device_damage/device_damage.hpp"
#include "../../../core/engine/primitives/material/material_raster.hpp"
#include "../../../core/theme/theme/material_layers.hpp"
#include "../../../core/interpolation/interpolation.hpp"

#include "include/core/SkCanvas.h"
#include "include/core/SkBlurTypes.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkData.h"
#include "include/core/SkGraphics.h"
#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkMaskFilter.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkRegion.h"
#include "include/core/SkRRect.h"
#include "include/core/SkStream.h"
#include "include/core/SkSurface.h"
#include "include/effects/SkDashPathEffect.h"
#include "include/effects/SkShaderMaskFilter.h"
#include "include/effects/SkGradient.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace gui_forms::render {
namespace {

// Every raster is premultiplied BGRA8 (SK2), which stx_skia's build makes
// Skia's native order on every host so that fills take its N32 blitters.
static_assert(kN32_SkColorType == kBGRA_8888_SkColorType, "stx_skia must be built with SK_R32_SHIFT=16");

SkColor to_sk_color(Color color) noexcept {
    return SkColorSetARGB(color.alpha, color.red, color.green, color.blue);
}

// Copies the device pixels two surfaces share, their top-left overlap, from
// `previous` into `replacement` row by row (SkiaRaster::resize at an
// unchanged scale). Both are premultiplied BGRA8 surfaces of distinct
// storage; `replacement` is clear outside the overlap. False, copying
// nothing, when either surface's pixels cannot be addressed.
bool carry_overlap(SkSurface& previous, SkSurface& replacement) noexcept {
    const std::optional<SkPixmap> source = previous.peekPixels();
    const std::optional<SkPixmap> target = replacement.peekPixels();
    if (!source || !target) return false;
    if ((*source).colorType() != (*target).colorType()) return false;
    const int columns = std::min((*source).width(), (*target).width());
    const int rows = std::min((*source).height(), (*target).height());
    if (columns <= 0 || rows <= 0) return false;
    const std::size_t row_bytes = static_cast<std::size_t>(columns) * 4U;
    const std::byte* const from = static_cast<const std::byte*>((*source).addr());
    std::byte* const to = static_cast<std::byte*>((*target).writable_addr());
    if (from == nullptr || to == nullptr) return false;
    for (int row = 0; row < rows; ++row) {
        const std::size_t source_offset = static_cast<std::size_t>(row) * (*source).rowBytes();
        const std::size_t target_offset = static_cast<std::size_t>(row) * (*target).rowBytes();
        std::memcpy(to + target_offset, from + source_offset, row_bytes);
    }
    return true;
}

SkRect to_sk_rect(Rect rect) noexcept {
    return SkRect::MakeXYWH(static_cast<SkScalar>(rect.x),
                            static_cast<SkScalar>(rect.y),
                            static_cast<SkScalar>(rect.width),
                            static_cast<SkScalar>(rect.height));
}

// Resolve the complete canvas transform, including device scale and retained
// translations. Exact equality deliberately refuses fractional resampling.
bool live_surface_device_rect(const SkCanvas& canvas, const Rect destination,
                              const LiveSurfaceFrame& frame, SkIRect& result) {
    const SkMatrix matrix = canvas.getTotalMatrix();
    if (!matrix.isScaleTranslate() || matrix.getScaleX() <= 0.0f ||
        matrix.getScaleY() <= 0.0f) return false;
    SkRect device = to_sk_rect(destination);
    matrix.mapRect(&device);
    if (!device.isFinite() || device.width() != static_cast<SkScalar>(frame.width()) ||
        device.height() != static_cast<SkScalar>(frame.height())) return false;
    const double left = device.left();
    const double top = device.top();
    const double right = device.right();
    const double bottom = device.bottom();
    if (left != std::floor(left) || top != std::floor(top) ||
        left < std::numeric_limits<int>::min() || top < std::numeric_limits<int>::min() ||
        right > std::numeric_limits<int>::max() || bottom > std::numeric_limits<int>::max()) return false;
    result = SkIRect::MakeLTRB(static_cast<int>(left), static_cast<int>(top),
                              static_cast<int>(right), static_cast<int>(bottom));
    return true;
}

SkRRect to_sk_rrect(Rect rect, double radius) noexcept {
    const SkScalar bounded = static_cast<SkScalar>(std::clamp(
        radius, 0.0, std::max(0.0, std::min(rect.width, rect.height) * 0.5)));
    return SkRRect::MakeRectXY(to_sk_rect(rect), bounded, bounded);
}

// Each radius held within half the shorter side, as to_sk_rrect holds one.
SkRRect to_sk_rrect_corners(Rect rect, PaintCornerRadii radii) noexcept {
    const double limit = std::max(0.0, std::min(rect.width, rect.height) * 0.5);
    const std::array<double, 4> written{radii.top_left, radii.top_right, radii.bottom_right, radii.bottom_left};
    // Skia's order: upper-left, upper-right, lower-right, lower-left.
    std::array<SkVector, 4> corners{};
    for (std::size_t index = 0U; index < corners.size(); ++index) {
        const SkScalar bounded = static_cast<SkScalar>(std::clamp(written[index], 0.0, limit));
        corners[index] = SkVector::Make(bounded, bounded);
    }
    SkRRect result;
    result.setRectRadii(to_sk_rect(rect), corners.data());
    return result;
}

// A gradient's stops in Skia's form, on the stack: capacity
// maximum_gradient_stops, live length `count` (audit P10: no allocation
// per fill).
struct SkGradientData final {
    std::array<SkColor4f, maximum_gradient_stops> colors{};
    std::array<float, maximum_gradient_stops> positions{};
    std::size_t count{};
};

// `stops` passed valid_gradient_stops, so it holds at most
// maximum_gradient_stops.
SkGradientData to_sk_gradient(std::span<const GradientStop> stops) {
    SkGradientData result{};
    result.count = std::min(stops.size(), maximum_gradient_stops);
    for (std::size_t index = 0U; index < result.count; ++index) {
        result.colors[index] = SkColor4f::FromColor(to_sk_color(stops[index].color));
        result.positions[index] = static_cast<float>(stops[index].offset);
    }
    return result;
}

SkTileMode to_sk_tile_mode(GradientSpreadMode spread) noexcept {
    switch (spread) {
    case GradientSpreadMode::pad: return SkTileMode::kClamp;
    case GradientSpreadMode::repeat: return SkTileMode::kRepeat;
    case GradientSpreadMode::reflect: return SkTileMode::kMirror;
    }
    return SkTileMode::kClamp;
}

SkGradient make_sk_gradient(
    const SkGradientData& data,
    GradientSpreadMode spread = GradientSpreadMode::pad) {
    const SkGradient::Colors colors(
        SkSpan<const SkColor4f>(data.colors.data(), data.count),
        SkSpan<const float>(data.positions.data(), data.count),
        to_sk_tile_mode(spread), SkColorSpace::MakeSRGB());
    SkGradient::Interpolation interpolation;
    interpolation.fInPremul = SkGradient::Interpolation::InPremul::kYes;
    interpolation.fColorSpace = SkGradient::Interpolation::ColorSpace::kSRGB;
    return SkGradient(colors, interpolation);
}

SkPaint make_paint(Color color) {
    SkPaint paint;
    paint.setColor(to_sk_color(color));
    paint.setAntiAlias(true);
    return paint;
}

// Edge snapping (owner decision L-Q8, detail::snap_device_edge) under the
// canvas transform. `scale_*` and `translate_*` give the device edges that
// are rounded; `canvas_*` are the canvas's own single-precision values, which
// Skia applies to what is drawn. SkMatrix holds the caller's translation in
// single precision, which can carry an edge lying exactly on a half device
// pixel to the wrong side of it (scale 2, a 0.45 translation: 0.9f is below
// 0.9). Where the canvas is the frame's scale with the caller's translation,
// the edges are taken from that translation in double precision instead.
struct SnapTransform final {
    bool axis_aligned{false};
    double scale_x{1.0};
    double scale_y{1.0};
    double translate_x{0.0};
    double translate_y{0.0};
    double canvas_scale_x{1.0};
    double canvas_scale_y{1.0};
    double canvas_translate_x{0.0};
    double canvas_translate_y{0.0};
};

// Whether a single-precision device translation is `exact` rounded: within
// a few parts in a hundred thousand, far below any offset another level of
// the canvas (a group's device origin, whole pixels) would add.
bool rounds_to(const double rounded, const double exact) noexcept {
    const double difference = std::abs(rounded - exact);
    const bool close_enough = difference <= 1.0e-5 * (1.0 + std::abs(exact));
    return close_enough;
}

// `frame_scale` is the scale the frame began with and `offset` the logical
// translation the caller has applied since, in double precision.
SnapTransform snap_transform(const SkMatrix& matrix, const double frame_scale, const Point offset) {
    SnapTransform transform{};
    const double scale_x = static_cast<double>(matrix.getScaleX());
    const double scale_y = static_cast<double>(matrix.getScaleY());
    if (!matrix.isScaleTranslate() || scale_x == 0.0 || scale_y == 0.0) return transform;
    transform.axis_aligned = true;
    transform.canvas_scale_x = scale_x;
    transform.canvas_scale_y = scale_y;
    transform.canvas_translate_x = static_cast<double>(matrix.getTranslateX());
    transform.canvas_translate_y = static_cast<double>(matrix.getTranslateY());
    transform.scale_x = transform.canvas_scale_x;
    transform.scale_y = transform.canvas_scale_y;
    transform.translate_x = transform.canvas_translate_x;
    transform.translate_y = transform.canvas_translate_y;
    const SkScalar single_scale = static_cast<SkScalar>(frame_scale);
    const double exact_x = offset.x * frame_scale;
    const double exact_y = offset.y * frame_scale;
    const bool frame_transform = matrix.getScaleX() == single_scale && matrix.getScaleY() == single_scale &&
        rounds_to(transform.canvas_translate_x, exact_x) && rounds_to(transform.canvas_translate_y, exact_y);
    if (frame_transform) {
        transform.scale_x = frame_scale;
        transform.scale_y = frame_scale;
        transform.translate_x = exact_x;
        transform.translate_y = exact_y;
    }
    return transform;
}

// `rect`'s device edges, each rounded to a whole device pixel, in device
// space. Only an axis-aligned transform (scale and translation) snaps; under
// any other, false, and the caller keeps antialiased geometry.
bool snapped_device_rect(const SnapTransform& transform, const Rect rect, SkRect& device) {
    if (!transform.axis_aligned || !rect.finite()) return false;
    const double left = gui_forms::detail::snap_device_edge(rect.x * transform.scale_x + transform.translate_x);
    const double top = gui_forms::detail::snap_device_edge(rect.y * transform.scale_y + transform.translate_y);
    const double right =
        gui_forms::detail::snap_device_edge((rect.x + rect.width) * transform.scale_x + transform.translate_x);
    const double bottom =
        gui_forms::detail::snap_device_edge((rect.y + rect.height) * transform.scale_y + transform.translate_y);
    device = SkRect::MakeLTRB(static_cast<SkScalar>(left), static_cast<SkScalar>(top),
                              static_cast<SkScalar>(right), static_cast<SkScalar>(bottom));
    device.sort();
    return true;
}

// The local rectangle the canvas maps to the snapped device edges. At whole
// device pixels a fill without antialiasing covers exactly the pixels between
// the edges; it samples pixel centres, so the hair of error the mapping back
// leaves cannot change a pixel. An antialiased shape is drawn in device
// space instead (snapped_device_rect, the canvas transform reset), where that
// hair would show as a partly covered edge pixel.
bool snapped_local_rect(const SnapTransform& transform, const Rect rect, SkRect& local) {
    SkRect device{};
    if (!snapped_device_rect(transform, rect, device)) return false;
    const double left = static_cast<double>(device.left());
    const double top = static_cast<double>(device.top());
    const double right = static_cast<double>(device.right());
    const double bottom = static_cast<double>(device.bottom());
    local = SkRect::MakeLTRB(static_cast<SkScalar>((left - transform.canvas_translate_x) / transform.canvas_scale_x),
                             static_cast<SkScalar>((top - transform.canvas_translate_y) / transform.canvas_scale_y),
                             static_cast<SkScalar>((right - transform.canvas_translate_x) / transform.canvas_scale_x),
                             static_cast<SkScalar>((bottom - transform.canvas_translate_y) / transform.canvas_scale_y));
    local.sort();
    return true;
}

// A logical corner radius in device pixels along each axis, no more than
// half of the device box it rounds.
SkVector device_radii(const SnapTransform& transform, const double radius, const SkRect& device) {
    const double across = std::abs(transform.scale_x) * radius;
    const double down = std::abs(transform.scale_y) * radius;
    const double half_width = static_cast<double>(device.width()) * 0.5;
    const double half_height = static_cast<double>(device.height()) * 0.5;
    const SkVector radii = SkVector::Make(static_cast<SkScalar>(std::clamp(across, 0.0, half_width)),
                                          static_cast<SkScalar>(std::clamp(down, 0.0, half_height)));
    return radii;
}

// The rectangle to draw for a box filled with `paint`: snapped, with
// antialiasing off, when the transform allows; otherwise the box as given.
SkRect snapped_box(const SnapTransform& transform, const Rect rect, SkPaint& paint) {
    SkRect local = to_sk_rect(rect);
    if (snapped_local_rect(transform, rect, local)) paint.setAntiAlias(false);
    return local;
}

Rect outset_rect(const Rect rect, const double amount) noexcept {
    const Rect result{rect.x - amount, rect.y - amount, rect.width + amount * 2.0, rect.height + amount * 2.0};
    return result;
}

// The largest font file registration accepts; the registry refuses larger data too.
constexpr std::uintmax_t maximum_font_file_bytes = 64U * 1024U * 1024U;

// Reads a whole font file into new data, or returns null when the path is null, the file
// cannot be opened, is empty or larger than the limit, or reads short. The path is UTF-8 on
// Windows, as the file loader this replaces took it, and the native byte string elsewhere.
sk_sp<SkData> read_font_file(const char* path) {
    if (path == nullptr) return nullptr;
#ifdef _WIN32
    const std::u8string_view utf8_path(reinterpret_cast<const char8_t*>(path));
    const std::filesystem::path file_path(utf8_path);
#else
    const std::filesystem::path file_path(path);
#endif
    std::error_code size_error;
    const std::uintmax_t file_bytes = std::filesystem::file_size(file_path, size_error);
    if (size_error || file_bytes == 0 || file_bytes > maximum_font_file_bytes) return nullptr;
    std::ifstream input(file_path, std::ios::binary);
    if (!input) return nullptr;
    const std::size_t length = static_cast<std::size_t>(file_bytes);
    sk_sp<SkData> data = SkData::MakeUninitialized(length);
    char* destination = static_cast<char*>((*data).writable_data());
    input.read(destination, static_cast<std::streamsize>(length));
    const bool complete = static_cast<std::uintmax_t>(input.gcount()) == file_bytes;
    if (!complete) return nullptr;
    return data;
}

// N56/S4: shaped runs and resolved layouts kept across measure and draw.
// A result depends on the text, the FontSpec and the faces the engine holds;
// faces are only ever added, so a table is cleared whenever face_count
// changes, as ShapedTextMemo is. Each table is bounded in entries and in
// the text length it keeps, and is cleared, not evicted, when full: a frame
// asks for the same few hundred strings again, and clearing costs one
// reshape of each.
constexpr std::size_t maximum_kept_text_entries = 2048;
constexpr std::size_t maximum_kept_text_bytes = 256;

struct KeptTextKey final {
    std::string text;
    FontSpec font{};
};

// A lookup key that borrows its text for the duration of one find.
struct KeptTextProbe final {
    std::string_view text;
    FontSpec font{};
};

// FontSpec == compares doubles by value, so -0.0 and 0.0 are one key; adding
// 0.0 folds them to one bit pattern before hashing. Only valid specs, whose
// fields are finite, are kept, so NaN never reaches the table.
[[nodiscard]] std::uint64_t kept_double_bits(const double value) noexcept {
    const double folded = value + 0.0;
    const std::uint64_t bits = std::bit_cast<std::uint64_t>(folded);
    return bits;
}

[[nodiscard]] std::size_t kept_text_hash_value(const std::string_view text, const FontSpec font) noexcept {
    std::uint64_t hash = static_cast<std::uint64_t>(std::hash<std::string_view>{}(text));
    const std::uint64_t multiplier = 0x9E3779B97F4A7C15ULL;
    hash = (hash ^ static_cast<std::uint64_t>(font.role)) * multiplier;
    hash = (hash ^ kept_double_bits(font.size)) * multiplier;
    hash = (hash ^ static_cast<std::uint64_t>(font.weight)) * multiplier;
    hash = (hash ^ static_cast<std::uint64_t>(font.italic ? 1U : 0U)) * multiplier;
    hash = (hash ^ kept_double_bits(font.letter_spacing)) * multiplier;
    const std::size_t result = static_cast<std::size_t>(hash ^ (hash >> 32U));
    return result;
}

struct KeptTextHash final {
    using is_transparent = void;
    [[nodiscard]] std::size_t operator()(const KeptTextKey& key) const noexcept {
        return kept_text_hash_value(key.text, key.font);
    }
    [[nodiscard]] std::size_t operator()(const KeptTextProbe& probe) const noexcept {
        return kept_text_hash_value(probe.text, probe.font);
    }
};

struct KeptTextEqual final {
    using is_transparent = void;
    [[nodiscard]] bool operator()(const KeptTextKey& left, const KeptTextKey& right) const noexcept {
        return left.font == right.font && left.text == right.text;
    }
    [[nodiscard]] bool operator()(const KeptTextProbe& left, const KeptTextKey& right) const noexcept {
        return left.font == right.font && left.text == std::string_view(right.text);
    }
    [[nodiscard]] bool operator()(const KeptTextKey& left, const KeptTextProbe& right) const noexcept {
        return left.font == right.font && std::string_view(left.text) == right.text;
    }
};

[[nodiscard]] bool keepable_text(const std::string_view text, const FontSpec font) noexcept {
    return text.size() <= maximum_kept_text_bytes && valid_font_spec(font);
}

// draw_image below 1x (owner, 2026-11-11, audit P11): neither mipmaps nor an
// area filter of Skia's; the image is resampled through the interpolation
// seam (stx_conv, pixel-area on each axis) to the device size it is drawn at,
// and that copy is drawn instead. A source larger than this many pixels is
// drawn directly: its binary64 working copy (32 bytes a pixel) would cost
// more than the draw is worth.
constexpr std::size_t maximum_resampled_source_pixels = 1024U * 1024U;

// One premultiplied channel in [0, 1] quantized once to a byte.
std::uint8_t channel_byte(double value) noexcept {
    const double scaled = std::round(std::clamp(value, 0.0, 1.0) * 255.0);
    return static_cast<std::uint8_t>(scaled);
}

// `source` (premultiplied BGRA, 8 bits a channel) resampled to `width` x
// `height` pixels, each no larger than the source's: null when the source
// has no readable pixels, is too large (maximum_resampled_source_pixels), or
// the seam refuses. Allocates its working copies once and releases them.
sk_sp<SkImage> resample_image_through_conv(const SkImage& source, int width, int height) {
    // stx_skia's peekPixels returns the pixmap, empty when the pixels are
    // not readable in place.
    const std::optional<SkPixmap> peeked = source.peekPixels();
    if (!peeked || (*peeked).colorType() != kBGRA_8888_SkColorType ||
        (*peeked).alphaType() != kPremul_SkAlphaType) {
        return nullptr;
    }
    const SkPixmap& pixels = *peeked;
    const std::size_t source_width = static_cast<std::size_t>(pixels.width());
    const std::size_t source_height = static_cast<std::size_t>(pixels.height());
    const std::size_t target_width = static_cast<std::size_t>(width);
    const std::size_t target_height = static_cast<std::size_t>(height);
    if (source_width == 0U || source_height == 0U || source_width * source_height > maximum_resampled_source_pixels ||
        target_width == 0U || target_height == 0U || target_width > source_width || target_height > source_height) {
        return nullptr;
    }
    constexpr std::size_t channels = 4U;
    // Source and target samples, row-major, the channels in BGRA order.
    std::vector<double> source_samples(source_width * source_height * channels);
    std::vector<double> target_samples(target_width * target_height * channels);
    for (std::size_t y = 0U; y < source_height; ++y) {
        const std::uint8_t* const row = static_cast<const std::uint8_t*>(pixels.addr(0, static_cast<int>(y)));
        double* const out = source_samples.data() + y * source_width * channels;
        for (std::size_t index = 0U; index < source_width * channels; ++index) {
            out[index] = static_cast<double>(row[index]) / 255.0;
        }
    }
    const interpolation::SourceImage from{source_samples, source_width, source_height, channels,
                                          source_width * channels};
    const interpolation::TargetImage to{target_samples, target_width, target_height, target_width * channels};
    const interpolation::Status status =
        interpolation::resample_image(from, to, interpolation::Sampling::pixel_area, interpolation::ValueRange{});
    if (status != interpolation::Status::ok) return nullptr;
    const std::size_t row_bytes = target_width * channels;
    sk_sp<SkData> data = SkData::MakeUninitialized(row_bytes * target_height);
    std::uint8_t* const bytes = static_cast<std::uint8_t*>((*data).writable_data());
    for (std::size_t pixel = 0U; pixel < target_width * target_height; ++pixel) {
        const std::size_t at = pixel * channels;
        const double alpha = target_samples[at + 3U];
        // The seam clamps each channel alone; colour above alpha is clamped
        // to it, keeping the copy premultiplied.
        bytes[at] = channel_byte(std::min(target_samples[at], alpha));
        bytes[at + 1U] = channel_byte(std::min(target_samples[at + 1U], alpha));
        bytes[at + 2U] = channel_byte(std::min(target_samples[at + 2U], alpha));
        bytes[at + 3U] = channel_byte(alpha);
    }
    const SkImageInfo info = SkImageInfo::Make(width, height, kBGRA_8888_SkColorType, kPremul_SkAlphaType,
                                               SkColorSpace::MakeSRGB());
    sk_sp<SkImage> result = SkImages::RasterFromData(info, std::move(data), row_bytes);
    return result;
}

} // namespace

class SkiaRaster::Impl {
public:
    struct DecodedImage final {
        std::uint64_t content_hash{};
        sk_sp<SkImage> image;
        // A premultiplied BGRA image's pixels, which `image` shares, kept so
        // that a patch run updates only its rectangle (D4); null for a
        // decoded file. row_bytes is the image's width times four.
        sk_sp<SkData> pixels;
        std::size_t row_bytes{};
        // The ImagePatchRegion::base_image this record was last patched
        // under, or 0.
        std::uint64_t patch_base_image{};
        // fill_image_pattern's tile, resampled once to the device tile size
        // it was last drawn at; null until a pattern needs it. Replacing the
        // image replaces the record, and the tile with it.
        sk_sp<SkImage> device_tile;
        // Every pixel of device_tile has alpha 255.
        bool device_tile_opaque{};
    };

    using ImageMap = std::unordered_map<std::uint64_t, DecodedImage>;

    // One registered face; `bytes` stay valid while `owner` lives (an SkData, a copy, or a
    // sealed bundled face). A lazy fallback has no bytes yet: its coverage and loader instead. A
    // lazy family face has a loader and its numbered family, and no coverage.
    struct FontSource {
        std::optional<FontRole> role;
        std::uint16_t weight;
        bool italic;
        std::span<const std::byte> bytes;
        std::shared_ptr<const void> owner;
        std::span<const gui_forms::detail::CodepointRange> coverage;
        text::LazyFaceLoader loader;
        BundledFontFamily family;
    };
    std::vector<FontSource> font_sources;
    // The process's bundled font set, held so every window shares it (register_bundled_fonts).
    std::shared_ptr<gui_forms::detail::BundledFontSet> bundled_fonts;
    sk_sp<SkSurface> surface;
    // Whether `surface` holds its predecessor's pixels in their overlap
    // (resize at an unchanged scale); false for a first or rescaled surface.
    bool surface_carried{false};
#if defined(GUI_FORMS_PREPARED_TEXT)
    PreparedSkiaFrame prepared_frame{};
    bool prepared_mode{false};
#endif
    ImageMap images;
    // A patch run's base image -> the key its record now has (D4).
    std::unordered_map<std::uint64_t, std::uint64_t> patch_runs;
    // draw_image's resampled copies (resample_image_through_conv), keyed by
    // image, its content (a replaced image misses) and the device size; at
    // most maximum_downscaled_images and maximum_downscaled_bytes, the least
    // recently drawn evicted first.
    struct DownscaledImage final {
        std::uint64_t image{};
        std::uint64_t content_hash{};
        int width{};
        int height{};
        std::uint64_t last_use{};
        sk_sp<SkImage> pixels;
    };
    static constexpr std::size_t maximum_downscaled_images = 64U;
    static constexpr std::size_t maximum_downscaled_bytes = 16U * 1024U * 1024U;
    std::vector<DownscaledImage> downscaled;
    std::size_t downscaled_bytes{};
    std::uint64_t downscale_clock{};

    // The copy of `decoded` (image `image`) to draw over `destination` under
    // `matrix`: null when the draw is not below 1x on either axis, the
    // matrix is not a scale and translation, or the copy cannot be made, so
    // the image is drawn as it is.
    sk_sp<SkImage> downscaled_for(std::uint64_t image, const DecodedImage& decoded, const SkMatrix& matrix,
                                  Rect destination) {
        if (!matrix.isScaleTranslate()) return nullptr;
        const double device_width = destination.width * std::abs(static_cast<double>(matrix.getScaleX()));
        const double device_height = destination.height * std::abs(static_cast<double>(matrix.getScaleY()));
        const int source_width = (*decoded.image).width();
        const int source_height = (*decoded.image).height();
        if (!(device_width >= 0.5) || !(device_height >= 0.5) ||
            !(device_width <= static_cast<double>(source_width)) ||
            !(device_height <= static_cast<double>(source_height))) {
            return nullptr;
        }
        const int width = static_cast<int>(std::lround(device_width));
        const int height = static_cast<int>(std::lround(device_height));
        if (width == source_width && height == source_height) return nullptr;
        downscale_clock = downscale_clock + 1U;
        for (DownscaledImage& kept : downscaled) {
            if (kept.image == image && kept.content_hash == decoded.content_hash && kept.width == width &&
                kept.height == height) {
                kept.last_use = downscale_clock;
                return kept.pixels;
            }
        }
        sk_sp<SkImage> made = resample_image_through_conv(*decoded.image, width, height);
        if (!made) return nullptr;
        const std::size_t bytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U;
        while (!downscaled.empty() &&
               (downscaled.size() >= maximum_downscaled_images || downscaled_bytes + bytes > maximum_downscaled_bytes)) {
            std::size_t oldest = 0U;
            for (std::size_t index = 1U; index < downscaled.size(); ++index) {
                if (downscaled[index].last_use < downscaled[oldest].last_use) oldest = index;
            }
            const std::size_t evicted = static_cast<std::size_t>(downscaled[oldest].width) *
                static_cast<std::size_t>(downscaled[oldest].height) * 4U;
            downscaled_bytes = downscaled_bytes - evicted;
            downscaled.erase(downscaled.begin() + static_cast<std::ptrdiff_t>(oldest));
        }
        if (bytes > maximum_downscaled_bytes) return made;
        DownscaledImage entry{};
        entry.image = image;
        entry.content_hash = decoded.content_hash;
        entry.width = width;
        entry.height = height;
        entry.last_use = downscale_clock;
        entry.pixels = made;
        downscaled.push_back(std::move(entry));
        downscaled_bytes = downscaled_bytes + bytes;
        return made;
    }
    text::HarfBuzzFontEngine text_engine;
    // Identity of text_engine for shaped-text memos. A memo holding an
    // expired or different identity was shaped by another raster.
    std::shared_ptr<const bool> text_engine_identity{std::make_shared<const bool>(true)};
    // Kept shaped runs and resolved layouts (N56/S4), valid while the engine
    // holds kept_face_count faces. References returned by shaped_text and
    // resolved_layout stay valid until the next call to either.
    std::unordered_map<KeptTextKey, text::ShapedText, KeptTextHash, KeptTextEqual> kept_shapes;
    std::unordered_map<KeptTextKey, ResolvedTextLayout, KeptTextHash, KeptTextEqual> kept_layouts;
    std::size_t kept_face_count{};
    // Clears both tables when the engine's faces have changed since they were filled.
    void refresh_kept_text();
    [[nodiscard]] const text::ShapedText& shaped_text(std::string_view text, FontSpec font);
    [[nodiscard]] const ResolvedTextLayout& resolved_layout(std::string_view text, FontSpec font);
    // The result for text that is not kept, so shaped_text can return a reference.
    text::ShapedText unkept_shape;
    ResolvedTextLayout unkept_layout;
    // Draws the run's shared-rasterizer coverage through Skia as an alpha
    // mask. Used only when the canvas pixels cannot be composited directly;
    // Skia then blends, so the bytes may differ from the shared rule.
    void draw_shaped_text(SkCanvas& target, const text::ShapedText& shaped, Point origin,
                          FontSpec font_spec, Color color);

    // ADR-004 text clipping: the device clip that text compositing honours,
    // one entry per canvas save level from text_clip_base upward. Skia cannot
    // report a clip's fractional edges, so SkiaRaster records them itself.
    struct TextClipState final {
        SkRect rect{};
        bool rounded{};
        SkRect rounded_rect{};
        double radius{};
        // The rounded clip enclosing the innermost one (D6); one further out
        // is kept as its bounds in rect.
        bool outer_rounded{};
        SkRect outer_rounded_rect{};
        double outer_radius{};
        // Moves the innermost rounded clip outward before another is added
        // inside it; the clip that was outer is kept as its bounds.
        void push_rounded_clip() {
            if (!rounded) return;
            if (outer_rounded && !rect.intersect(outer_rounded_rect)) rect.setEmpty();
            outer_rounded = true;
            outer_rounded_rect = rounded_rect;
            outer_radius = radius;
        }
        // Disjoint device rectangles of the frame damage and clip_damage.
        bool region_limited{};
        SkRegion region;
    };
    std::vector<TextClipState> text_clips;
    int text_clip_base{};
    std::unique_ptr<text::GlyphRunRasterizer> glyph_runs;

    void reset_text_clips(SkCanvas& target, const SkRegion* region) {
        text_clips.clear();
        TextClipState base{};
        base.rect = SkRect::MakeIWH(target.imageInfo().width(), target.imageInfo().height());
        if (region != nullptr) {
            base.region_limited = true;
            base.region = *region;
        }
        text_clips.push_back(std::move(base));
        text_clip_base = target.getSaveCount();
    }

    // Null when the recorded clips no longer describe the canvas.
    [[nodiscard]] TextClipState* current_text_clip(const SkCanvas& target) noexcept {
        if (text_clips.empty()) return nullptr;
        const int expected = text_clip_base + static_cast<int>(text_clips.size()) - 1;
        if (target.getSaveCount() != expected) return nullptr;
        return &text_clips.back();
    }

    // Rasterizes (or reuses) the run's coverage and composites it with the
    // shared rule into the canvas pixels. False when the canvas cannot be
    // addressed this way; the caller then draws through Skia.
    bool composite_run(SkCanvas& target, const text::ShapedText& shaped, FontSpec font, Point origin, Color color,
                       text::GlyphRunCoverage* kept, double* kept_scale, std::int32_t* kept_phase);
    // The shared compositing step alone: device coverage into the canvas
    // pixels under the recorded clips. False when the canvas cannot be
    // addressed or its clips are unknown.
    bool composite_coverage(SkCanvas& target, const detail::TextCoverage& source, Color color);

    // The canvas pixels and the recorded clip as the shared blends take
    // them, for text coverage and part rasters alike.
    struct SharedComposite final {
        TextClipState* state{};
        detail::TextCompositeTarget target{};
        detail::TextCompositeClip clip{};
    };
    // None when the canvas cannot be addressed as premultiplied BGRA8, or its
    // clips are unknown.
    [[nodiscard]] std::optional<SharedComposite> shared_composite(SkCanvas& target);

    // A part raster into the canvas pixels under the recorded clips. False
    // when the canvas is not a uniform scale and translation equal to the
    // raster's scale, or cannot be addressed.
    bool composite_part(SkCanvas& target, const PartRaster& raster, Point origin, double opacity);
    bool composite_part_scaled(SkCanvas& target, const PartRaster& raster, Rect destination, double opacity);
    // The shared step alone: premultiplied pixels placed in the canvas's
    // device pixels, composited under the recorded clips. False when the
    // canvas cannot be addressed or its clips are unknown.
    bool composite_source(SkCanvas& target, const detail::PartRasterSource& source, double opacity);

    // Paint groups: one level per open group. An isolated level keeps the
    // parent canvas's recorded clips and save floor while its own surface
    // is drawn.
    struct GroupLevel final {
        sk_sp<SkSurface> surface;
        bool isolated{};
        SkIRect device{};
        double opacity{};
        PaintGroupComposite composite{PaintGroupComposite::source_over};
        std::vector<TextClipState> parent_text_clips;
        int parent_text_clip_base{};
        int parent_save_floor{};
    };
    std::array<GroupLevel, maximum_paint_group_depth> groups{};
    std::size_t group_depth{};
    std::size_t refused_open{};
    std::uint64_t group_refusals{};

    // The canvas of the innermost isolated group, or null.
    [[nodiscard]] SkCanvas* group_canvas() const noexcept {
        for (std::size_t level = group_depth; level > 0U; --level) {
            const GroupLevel& open = groups[level - 1U];
            if (open.isolated) return (*open.surface).getCanvas();
        }
        return nullptr;
    }
    // Ends every open group without compositing (a frame's start or end).
    void close_groups() noexcept {
        for (std::size_t level = group_depth; level > 0U; --level) {
            GroupLevel& open = groups[level - 1U];
            if (!open.isolated) continue;
            (*(*open.surface).getCanvas()).restoreAll();
            text_clips = std::move(open.parent_text_clips);
            text_clip_base = open.parent_text_clip_base;
            save_floor = open.parent_save_floor;
            open.isolated = false;
        }
        group_depth = 0U;
        refused_open = 0U;
    }

#if defined(GUI_FORMS_PREPARED_TEXT)
    // Lets prepared text composite through composite_coverage.
    class PreparedSink final : public PreparedCoverageSink {
    public:
        explicit PreparedSink(Impl& owner) : owner_(owner) {}
        bool composite(SkCanvas& target, const detail::TextCoverage& coverage, const Color color) override {
            const bool composited = owner_.composite_coverage(target, coverage, color);
            return composited;
        }
    private:
        Impl& owner_;
    };
#endif
    Size logical_size{};
    double scale{1.0};
    int save_floor{1};
    // The logical translation the caller has applied since the frame began,
    // in double precision, and one saved value per open save(); see
    // SnapTransform. Reset at each frame's start and end.
    Point offset{};
    std::vector<Point> saved_offsets;

    [[nodiscard]] SnapTransform snap_transform_of(const SkCanvas& target) const {
        const SnapTransform transform = snap_transform(target.getTotalMatrix(), scale, offset);
        return transform;
    }
    const ImageRegistry* image_registry{};
    std::uint64_t image_registry_revision{std::numeric_limits<std::uint64_t>::max()};
    // Patch runs applied by copying their rectangle only (D4).
    std::uint64_t image_patches_applied{};
    bool image_registry_synchronized{true};

    // Registers `bytes`, kept alive by `owner`, with the text engine without copying them.
    [[nodiscard]] bool register_font_bytes(std::optional<FontRole> role, std::uint16_t weight,
                                           bool italic, std::span<const std::byte> bytes,
                                           std::shared_ptr<const void> owner) {
        if (!owner || bytes.empty() || bytes.size() > 64U * 1024U * 1024U) return false;
        // Room first, so the record cannot fail after the engine has the face.
        font_sources.reserve(font_sources.size() + 1U);
        const std::optional<FontFaceId> text_face = text_engine.register_owned_typeface(
            role, weight, italic, bytes, owner);
        if (!text_face) return false;
        font_sources.push_back({role, weight, italic, bytes, std::move(owner), {}, {}, {}});
        return true;
    }

    // Registers a fallback face that the text engine loads when text first needs it.
    [[nodiscard]] bool register_lazy_font(std::uint16_t weight, bool italic,
                                          std::span<const gui_forms::detail::CodepointRange> coverage,
                                          const text::LazyFaceLoader& loader) {
        font_sources.reserve(font_sources.size() + 1U);
        const std::optional<FontFaceId> text_face =
            text_engine.register_lazy_fallback_typeface(weight, italic, coverage, loader);
        if (!text_face) return false;
        font_sources.push_back({std::nullopt, weight, italic, {}, {}, coverage, loader, {}});
        return true;
    }

    // Registers a face of a numbered bundled family that the text engine loads when text first
    // names the family.
    [[nodiscard]] bool register_lazy_family_font(BundledFontFamily family, std::uint16_t weight, bool italic,
                                                 const text::LazyFaceLoader& loader) {
        font_sources.reserve(font_sources.size() + 1U);
        const std::optional<FontFaceId> text_face =
            text_engine.register_lazy_family_typeface(family, weight, italic, loader);
        if (!text_face) return false;
        font_sources.push_back({std::nullopt, weight, italic, {}, {}, {}, loader, family});
        return true;
    }

    [[nodiscard]] bool register_font_data(std::optional<FontRole> role,
                                          std::uint16_t weight, bool italic,
                                          sk_sp<SkData> data) {
        if (!data || (*data).isEmpty()) return false;
        const std::span<const std::byte> bytes(
            static_cast<const std::byte*>((*data).data()), (*data).size());
        std::shared_ptr<const void> owner = std::make_shared<const sk_sp<SkData>>(std::move(data));
        const bool registered = register_font_bytes(role, weight, italic, bytes, std::move(owner));
        return registered;
    }

    [[nodiscard]] SkCanvas* canvas() const noexcept {
        SkCanvas* result = group_canvas();
        if (result != nullptr) return result;
#if defined(GUI_FORMS_PREPARED_TEXT)
        if (prepared_mode) {
            result = prepared_frame.canvas();
            return result;
        }
#endif
        if (surface) result = (*surface).getCanvas();
        return result;
    }

    [[nodiscard]] SkSurface* front_surface() const noexcept {
        SkSurface* result = surface.get();
#if defined(GUI_FORMS_PREPARED_TEXT)
        if (prepared_mode) result = prepared_frame.front();
#endif
        return result;
    }
};

SkiaRaster::SkiaRaster() : impl_(std::make_unique<Impl>()) {
    // Selects stx_skia's SIMD tier for the process (stx_skia/simd.hpp) and installs its
    // kernels before the first draw; thread-safe and idempotent, so every raster calls it.
    SkGraphics::Init();
}
SkiaRaster::~SkiaRaster() = default;

bool SkiaRaster::register_typeface(FontRole role, std::uint16_t weight,
                                   bool italic, std::span<const std::byte> encoded) {
    if (encoded.empty() || encoded.size() > 64U * 1024U * 1024U) return false;
    sk_sp<SkData> data = SkData::MakeWithCopy(encoded.data(), encoded.size());
    const bool registered = (*impl_).register_font_data(role, weight, italic, std::move(data));
    return registered;
}

bool SkiaRaster::register_fallback_typeface(
    std::uint16_t weight, bool italic, std::span<const std::byte> encoded) {
    if (encoded.empty() || encoded.size() > 64U * 1024U * 1024U) return false;
    sk_sp<SkData> data = SkData::MakeWithCopy(encoded.data(), encoded.size());
    const bool registered = (*impl_).register_font_data(std::nullopt, weight, italic, std::move(data));
    return registered;
}

bool SkiaRaster::register_typeface_file(FontRole role, std::uint16_t weight,
                                        bool italic, const char* path) {
    sk_sp<SkData> data = read_font_file(path);
    const bool registered = (*impl_).register_font_data(role, weight, italic, std::move(data));
    return registered;
}

bool SkiaRaster::register_fallback_typeface_file(
    std::uint16_t weight, bool italic, const char* path) {
    sk_sp<SkData> data = read_font_file(path);
    const bool registered = (*impl_).register_font_data(std::nullopt, weight, italic, std::move(data));
    return registered;
}

bool SkiaRaster::register_bundled_fonts(std::shared_ptr<gui_forms::detail::BundledFontSet> set) {
    if (!set) return false;
    for (const gui_forms::detail::BundledFaceUse& use : gui_forms::detail::bundled_face_uses()) {
        // A fallback with a compiled coverage table is read only when text needs it.
        const std::optional<std::span<const gui_forms::detail::CodepointRange>> coverage =
            text::lazy_bundled_coverage(use);
        if (coverage) {
            const text::LazyFaceLoader loader = text::BundledFaceLoader{set, use.file_name};
            if (!(*impl_).register_lazy_font(use.weight, use.italic, *coverage, loader)) return false;
            continue;
        }
        const gui_forms::detail::FontFaceLoad load = (*set).face(use.file_name);
        const std::shared_ptr<const gui_forms::detail::SealedFontFace>* face = load.face();
        bool registered = false;
        if (face != nullptr) {
            const std::span<const std::byte> bytes = (*(*face)).bytes();
            registered = (*impl_).register_font_bytes(use.role, use.weight, use.italic, bytes, *face);
        }
        if (!registered && use.required) return false;
    }
    // The faces of the numbered families a theme may name (font-family: Selawik), each read the
    // first time text names its family.
    for (const gui_forms::detail::BundledFamilyFace& family_face : gui_forms::detail::bundled_family_faces) {
        const text::LazyFaceLoader loader = text::BundledFaceLoader{set, family_face.file_name};
        if (!(*impl_).register_lazy_family_font(family_face.family, family_face.weight, family_face.italic, loader))
            return false;
    }
    (*impl_).bundled_fonts = std::move(set);
    return true;
}

class SkiaFramebuffer final : public PaintFramebuffer {
public:
    SkiaRaster raster;
    bool begin(const ImageRegistry& images, Rect damage) override {
        if (!raster.synchronize_images(images)) return false;
        DamageRegion region;
        region.add(damage);
        raster.begin_frame(region);
        return true;
    }
    void end() override { raster.end_frame(); }
    Painter& painter() noexcept override { return raster; }
    std::span<std::byte> pixels() noexcept override {
        return {static_cast<std::byte*>(const_cast<void*>(raster.pixels())), raster.byte_size()};
    }
    std::size_t row_bytes() const noexcept override { return raster.row_bytes(); }
    std::uint32_t width() const noexcept override { return raster.pixel_width(); }
    std::uint32_t height() const noexcept override { return raster.pixel_height(); }
};
std::unique_ptr<PaintFramebuffer> SkiaRaster::create_framebuffer(Size logical_size, double scale) {
    if (!std::isfinite(scale) || scale <= 0 || !std::isfinite(logical_size.width) ||
        !std::isfinite(logical_size.height) || logical_size.width <= 0 || logical_size.height <= 0 ||
        std::ceil(logical_size.width * scale) * std::ceil(logical_size.height * scale) > 16777216.0)
        return {};
    std::unique_ptr<SkiaFramebuffer> result = std::make_unique<SkiaFramebuffer>();
    for (const Impl::FontSource& source : (*impl_).font_sources) {
        bool registered = false;
        if (source.family.value != 0U) {
            registered = (*(*result).raster.impl_).register_lazy_family_font(source.family, source.weight,
                                                                            source.italic, source.loader);
        } else if (source.loader) {
            registered = (*(*result).raster.impl_).register_lazy_font(source.weight, source.italic,
                                                                     source.coverage, source.loader);
        } else {
            registered = (*(*result).raster.impl_).register_font_bytes(source.role, source.weight, source.italic,
                                                                      source.bytes, source.owner);
        }
        if (!registered) return {};
    }
    (*(*result).raster.impl_).bundled_fonts = (*impl_).bundled_fonts;
    if (!(*result).raster.resize(logical_size, scale)) return {};
    return result;
}

bool SkiaRaster::resize(Size logical_size, double scale) {
#if defined(GUI_FORMS_PREPARED_TEXT)
    // A host using transactions must not replace its coherent front via the
    // historical resize path. Size admission belongs to begin_prepared_frame.
    if ((*impl_).prepared_mode) return false;
#endif
    if (!std::isfinite(scale) || scale <= 0.0 || logical_size.width <= 0.0 ||
        logical_size.height <= 0.0) {
        return false;
    }
    const double requested_width = std::ceil(logical_size.width * scale);
    const double requested_height = std::ceil(logical_size.height * scale);
    if (requested_width > static_cast<double>(std::numeric_limits<int>::max()) ||
        requested_height > static_cast<double>(std::numeric_limits<int>::max())) {
        return false;
    }
    const int width = std::max(1, static_cast<int>(requested_width));
    const int height = std::max(1, static_cast<int>(requested_height));
    if ((*impl_).surface && width == (*(*impl_).surface).width() &&
        height == (*(*impl_).surface).height() && scale == (*impl_).scale) {
        (*impl_).logical_size = logical_size;
        return false;
    }

    // The host-facing surface contract is premultiplied BGRA8 on every target
    // (SK2), named explicitly: Skia's N32 alias follows the build's native
    // channel order, which would make identical renderer commands expose
    // different bytes across hosts.
    const SkImageInfo info = SkImageInfo::Make(
        width, height, kBGRA_8888_SkColorType, kPremul_SkAlphaType,
        SkColorSpace::MakeSRGB());
    sk_sp<SkSurface> replacement = SkSurfaces::Raster(info);
    if (!replacement) {
        return false;
    }
    (*(*replacement).getCanvas()).clear(SK_ColorTRANSPARENT);
    // At an unchanged scale the pixels both extents cover stay as they were,
    // so a host need repaint only what the resize exposed; at a new scale
    // nothing lines up and the surface starts clear.
    bool carried = false;
    if ((*impl_).surface && scale == (*impl_).scale) carried = carry_overlap(*(*impl_).surface, *replacement);
    (*impl_).surface = std::move(replacement);
    (*impl_).surface_carried = carried;
    (*impl_).logical_size = logical_size;
    (*impl_).scale = scale;
    return true;
}

bool SkiaRaster::carried_on_resize() const noexcept {
    const bool carried = (*impl_).surface_carried;
    return carried;
}

void SkiaRaster::begin_frame(const DamageRegion& damage) {
    (*impl_).close_groups();
    SkCanvas* canvas = (*impl_).canvas();
    if (!canvas) {
        return;
    }
    (*canvas).restoreAll();
    (*canvas).resetMatrix();
    (*canvas).scale(static_cast<SkScalar>((*impl_).scale),
                  static_cast<SkScalar>((*impl_).scale));
    (*canvas).save();
    (*impl_).save_floor = (*canvas).getSaveCount();
    (*impl_).offset = Point{};
    (*impl_).saved_offsets.clear();

    if (!damage.empty()) {
        SkRegion region;
        for (Rect rect : damage.rectangles()) {
            const SkIRect pixels = SkIRect::MakeLTRB(
                static_cast<int>(std::floor(rect.x * (*impl_).scale)),
                static_cast<int>(std::floor(rect.y * (*impl_).scale)),
                static_cast<int>(std::ceil((rect.x + rect.width) * (*impl_).scale)),
                static_cast<int>(std::ceil((rect.y + rect.height) * (*impl_).scale)));
            region.op(pixels, SkRegion::kUnion_Op);
        }
        (*canvas).resetMatrix();
        (*canvas).clipRegion(region);
        (*canvas).scale(static_cast<SkScalar>((*impl_).scale),
                      static_cast<SkScalar>((*impl_).scale));
        (*impl_).reset_text_clips(*canvas, &region);
    } else {
        (*impl_).reset_text_clips(*canvas, nullptr);
    }
}

void SkiaRaster::clear_transparent() {
    SkCanvas* const canvas = (*impl_).canvas();
    if (canvas == nullptr) return;
    (*canvas).clear(SK_ColorTRANSPARENT);
}

bool SkiaRaster::scroll_copy(const Rect clip, const Point offset) {
    // The frame's own surface, never an open group's layer.
    SkCanvas* canvas = nullptr;
    bool prepared = false;
#if defined(GUI_FORMS_PREPARED_TEXT)
    prepared = (*impl_).prepared_mode;
    if (prepared) canvas = (*impl_).prepared_frame.canvas();
#endif
    if (!prepared && (*impl_).surface) canvas = (*(*impl_).surface).getCanvas();
    if (canvas == nullptr) return false;
    const std::optional<SkPixmap> peeked = (*canvas).peekPixels();
    if (!peeked || (*peeked).colorType() != kBGRA_8888_SkColorType) return false;
    const SkPixmap& pixels = *peeked;
    const double scale = (*impl_).scale;
    const double values[] = {clip.x * scale, clip.y * scale, clip.width * scale, clip.height * scale,
                             offset.x * scale, offset.y * scale};
    for (const double value : values) {
        if (!std::isfinite(value) || std::abs(value - std::round(value)) > 1.0e-6) return false;
    }
    // Device rectangles: the clip within the surface, and the destination,
    // the clip's pixels that receive a pixel from inside it.
    const long long width = pixels.width();
    const long long height = pixels.height();
    const long long left = std::clamp(std::llround(values[0]), 0LL, width);
    const long long top = std::clamp(std::llround(values[1]), 0LL, height);
    const long long right = std::clamp(std::llround(values[0] + values[2]), left, width);
    const long long bottom = std::clamp(std::llround(values[1] + values[3]), top, height);
    const long long dx = std::llround(values[4]);
    const long long dy = std::llround(values[5]);
    const long long destination_left = std::max(left, left + dx);
    const long long destination_right = std::min(right, right + dx);
    const long long destination_top = std::max(top, top + dy);
    const long long destination_bottom = std::min(bottom, bottom + dy);
    if (destination_left >= destination_right || destination_top >= destination_bottom) return true;
    const std::size_t row_bytes = static_cast<std::size_t>(destination_right - destination_left) * 4U;
    const long long rows = destination_bottom - destination_top;
    // Rows move toward the copy's direction: a row is read before the copy
    // overwrites it. memmove handles the overlap within a row.
    for (long long step = 0; step < rows; ++step) {
        const long long row = dy > 0 ? destination_bottom - 1 - step : destination_top + step;
        void* const target = pixels.writable_addr(static_cast<int>(destination_left), static_cast<int>(row));
        const void* const source = pixels.addr(static_cast<int>(destination_left - dx), static_cast<int>(row - dy));
        std::memmove(target, source, row_bytes);
    }
    return true;
}

void SkiaRaster::clip_damage(const DamageRegion& damage) {
    SkCanvas* const canvas = (*impl_).canvas();
    if (canvas == nullptr || damage.empty()) return;
    SkRegion region;
    for (const Rect rect : damage.rectangles()) {
        const SkIRect pixels = SkIRect::MakeLTRB(
            static_cast<int>(std::floor(rect.x * (*impl_).scale)),
            static_cast<int>(std::floor(rect.y * (*impl_).scale)),
            static_cast<int>(std::ceil((rect.x + rect.width) * (*impl_).scale)),
            static_cast<int>(std::ceil((rect.y + rect.height) * (*impl_).scale)));
        region.op(pixels, SkRegion::kUnion_Op);
    }
    // Regions are in device pixels; restore the logical matrix afterwards.
    const SkMatrix matrix = (*canvas).getTotalMatrix();
    (*canvas).resetMatrix();
    (*canvas).clipRegion(region);
    (*canvas).setMatrix(matrix);
    if (Impl::TextClipState* const state = (*impl_).current_text_clip(*canvas)) {
        if ((*state).region_limited) (*state).region.op(region, SkRegion::kIntersect_Op);
        else (*state).region = region;
        (*state).region_limited = true;
    }
}

void SkiaRaster::end_frame() {
    (*impl_).close_groups();
    if (SkCanvas* canvas = (*impl_).canvas()) {
        (*canvas).restoreAll();
    }
    (*impl_).text_clips.clear();
    (*impl_).offset = Point{};
    (*impl_).saved_offsets.clear();
}

#if defined(GUI_FORMS_PREPARED_TEXT)
PreparedTextStatus SkiaRaster::begin_prepared_frame(const Size logical_size,
    const double scale, DamageRegion& damage) {
    if (!(*impl_).prepared_frame.on_executor()) return PreparedTextStatus::wrong_executor;
    const PreparedTextStatus status = (*impl_).prepared_frame.begin(logical_size, scale, damage);
    if (status == PreparedTextStatus::success) {
        // Select the route only after admission. Failed initial geometry and
        // allocation preserve any ordinary surface; it has no prepared receipt.
        (*impl_).prepared_mode = true;
        (*impl_).surface.reset();
        (*impl_).logical_size = logical_size;
        (*impl_).scale = scale;
        try { begin_frame(damage); }
        catch (...) { (*impl_).prepared_frame.abort(); throw; }
    }
    return status;
}
PreparedTextStatus SkiaRaster::commit_prepared_frame(const PaintReceipt receipt) {
    const PreparedTextStatus status = (*impl_).prepared_frame.commit(receipt);
    return status;
}
void SkiaRaster::abort_prepared_frame() noexcept { (*impl_).prepared_frame.abort(); }
PaintReceipt SkiaRaster::prepared_front_receipt() const noexcept {
    const PaintReceipt result = (*impl_).prepared_frame.receipt();
    return result;
}
double SkiaRaster::prepared_front_scale() const noexcept {
    const double result = (*impl_).prepared_frame.front_scale();
    return result;
}
bool SkiaRaster::prepared_front_matches(const Size logical_size, const double scale) const noexcept {
    const bool result = (*impl_).prepared_frame.matches(logical_size, scale);
    return result;
}
PreparedTextPaintResult SkiaRaster::draw_prepared_text(const PreparedTextLayout& layout,
    const LayoutAuthority expected, const Point baseline, const Color color) {
    Impl::PreparedSink sink(*impl_);
    const PreparedTextPaintResult result = (*impl_).prepared_frame.draw(layout, expected, baseline, color, &sink);
    return result;
}
#endif

// D4: brings a kept BGRA image up to date by copying only the rectangle
// of its open patch run. The record found is the one synchronized before
// the run (keyed by the run's base image at its base hash) or during it
// (named by patch_runs). Returns false, changing nothing, when no such
// record exists or its pixels are shared; the caller then copies the whole
// image.
bool SkiaRaster::apply_image_patch(const ImageResourceView& resource) {
    const ImagePatchRegion& region = resource.patched;
    if (region.base_hash == 0U ||
        resource.encoding != ImageResourceEncoding::bgra32_premultiplied) {
        return false;
    }
    std::uint64_t key = region.base_image;
    Impl::ImageMap::iterator found = (*impl_).images.find(key);
    bool current = found != (*impl_).images.end() &&
                   (*found).second.content_hash == region.base_hash;
    if (!current) {
        const std::unordered_map<std::uint64_t, std::uint64_t>::const_iterator run =
            (*impl_).patch_runs.find(region.base_image);
        if (run == (*impl_).patch_runs.end()) return false;
        key = (*run).second;
        found = (*impl_).images.find(key);
        current = found != (*impl_).images.end() &&
                  (*found).second.patch_base_image == region.base_image;
    }
    if (!current) return false;
    Impl::DecodedImage& kept = (*found).second;
    const std::size_t width = static_cast<std::size_t>(resource.metadata.width);
    const std::size_t height = static_cast<std::size_t>(resource.metadata.height);
    const std::size_t row_bytes = width * 4U;
    if (!kept.pixels || !kept.image || kept.row_bytes != row_bytes ||
        static_cast<std::size_t>((*kept.image).width()) != width ||
        static_cast<std::size_t>((*kept.image).height()) != height ||
        resource.row_bytes != row_bytes || resource.encoded.size() != row_bytes * height ||
        static_cast<std::uint64_t>(region.x) + region.width > width ||
        static_cast<std::uint64_t>(region.y) + region.height > height) {
        return false;
    }
    // The image shares the pixels: release it so that they are written
    // with no reader, then make a new image over them.
    const SkImageInfo info = (*kept.image).imageInfo();
    kept.image.reset();
    kept.device_tile.reset();
    if (!(*kept.pixels).unique()) {
        (*impl_).images.erase(found);
        return false;
    }
    std::byte* const destination = static_cast<std::byte*>((*kept.pixels).writable_data());
    const std::size_t origin = static_cast<std::size_t>(region.y) * row_bytes +
                               static_cast<std::size_t>(region.x) * 4U;
    const std::size_t span_bytes = static_cast<std::size_t>(region.width) * 4U;
    for (std::uint32_t row = 0U; row < region.height; ++row) {
        const std::size_t offset = origin + static_cast<std::size_t>(row) * row_bytes;
        std::memcpy(destination + offset, resource.encoded.data() + offset, span_bytes);
    }
    sk_sp<SkImage> image = SkImages::RasterFromData(info, kept.pixels, row_bytes);
    if (!image) {
        (*impl_).images.erase(found);
        return false;
    }
    Impl::DecodedImage record = std::move(kept);
    record.image = std::move(image);
    record.content_hash = resource.content_hash;
    record.patch_base_image = region.base_image;
    record.device_tile_opaque = false;
    (*impl_).images.erase(found);
    (*impl_).images[resource.image.value] = std::move(record);
    (*impl_).patch_runs[region.base_image] = resource.image.value;
    ++(*impl_).image_patches_applied;
    return true;
}

std::uint64_t SkiaRaster::image_patches_applied() const noexcept {
    return (*impl_).image_patches_applied;
}

bool SkiaRaster::synchronize_images(const ImageRegistry& registry) {
    const ImageRegistrySnapshot snapshot = registry.snapshot();
    const bool same_registry = &registry == (*impl_).image_registry;
    if (same_registry && snapshot.revision == (*impl_).image_registry_revision) {
        return (*impl_).image_registry_synchronized;
    }
    if (!same_registry) {
        (*impl_).images.clear();
        (*impl_).patch_runs.clear();
    }

    bool synchronized = true;
    // One shared owned decoder for the batch (src/core/image/resource_pixels).
    gui_forms::image::ResourcePixelDecoder decoder{};
    std::unordered_set<std::uint64_t> active;
    for (const ImageId id : registry.image_ids()) {
        active.insert(id.value);
        const std::optional<ImageResourceView> resource = registry.find(id);
        if (!resource) {
            synchronized = false;
            continue;
        }
        const Impl::ImageMap::const_iterator existing = (*impl_).images.find(id.value);
        if (existing != (*impl_).images.end() &&
            (*existing).second.content_hash == (*resource).content_hash) {
            continue;
        }
        if (apply_image_patch(*resource)) {
            continue;
        }

        if ((*resource).metadata.width >
                static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
            (*resource).metadata.height >
                static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
            (*resource).metadata.decoded_byte_count >
                std::numeric_limits<std::size_t>::max()) {
            (*impl_).images.erase(id.value);
            synchronized = false;
            continue;
        }

        const SkImageInfo output_info = SkImageInfo::Make(
            static_cast<int>((*resource).metadata.width),
            static_cast<int>((*resource).metadata.height),
            kBGRA_8888_SkColorType,
            kPremul_SkAlphaType,
            SkColorSpace::MakeSRGB());
        const std::size_t row_bytes = output_info.minRowBytes();
        const std::optional<std::size_t> output_bytes = output_info.checkedByteSize(row_bytes);
        if (row_bytes != static_cast<std::size_t>((*resource).metadata.width) * 4U ||
            !output_bytes || *output_bytes != (*resource).metadata.decoded_byte_count) {
            (*impl_).images.erase(id.value);
            synchronized = false;
            continue;
        }
        // D3: a decoded image goes straight into the storage its SkImage
        // keeps, with no intermediate vector and second copy.
        sk_sp<SkData> pixel_data;
        const bool patchable =
            (*resource).encoding == ImageResourceEncoding::bgra32_premultiplied;
        if (patchable) {
            if ((*resource).row_bytes != row_bytes ||
                (*resource).encoded.size() != (*resource).metadata.decoded_byte_count) {
                (*impl_).images.erase(id.value);
                synchronized = false;
                continue;
            }
            pixel_data = SkData::MakeWithCopy(
                (*resource).encoded.data(), (*resource).encoded.size());
        } else {
            const gui_forms::image::PixelTarget target{gui_forms::image::PixelOrder::bgra,
                                                       gui_forms::image::PixelAlpha::premultiplied};
            pixel_data = SkData::MakeUninitialized(*output_bytes);
            const std::span<std::byte> pixels(
                static_cast<std::byte*>((*pixel_data).writable_data()), *output_bytes);
            const gui_forms::image::ResourcePixelResult decoded =
                decoder.decode(*resource, target, pixels, row_bytes);
            if (decoded.error != gui_forms::image::ResourcePixelError::none ||
                decoded.width != (*resource).metadata.width ||
                decoded.height != (*resource).metadata.height) {
                (*impl_).images.erase(id.value);
                synchronized = false;
                continue;
            }
        }
        sk_sp<SkImage> image = SkImages::RasterFromData(output_info, pixel_data, row_bytes);
        if (!image) {
            (*impl_).images.erase(id.value);
            synchronized = false;
            continue;
        }
        Impl::DecodedImage record{};
        record.content_hash = (*resource).content_hash;
        record.image = std::move(image);
        if (patchable) {
            record.pixels = std::move(pixel_data);
            record.row_bytes = row_bytes;
        }
        (*impl_).images[id.value] = std::move(record);
    }
    for (Impl::ImageMap::iterator iterator = (*impl_).images.begin();
         iterator != (*impl_).images.end();) {
        if (!active.contains((*iterator).first)) {
            iterator = (*impl_).images.erase(iterator);
        } else {
            ++iterator;
        }
    }
    for (std::unordered_map<std::uint64_t, std::uint64_t>::iterator run = (*impl_).patch_runs.begin();
         run != (*impl_).patch_runs.end();) {
        if (!active.contains((*run).second)) {
            run = (*impl_).patch_runs.erase(run);
        } else {
            ++run;
        }
    }
    (*impl_).image_registry_revision = snapshot.revision;
    (*impl_).image_registry = &registry;
    (*impl_).image_registry_synchronized = synchronized;
    return synchronized;
}

const void* SkiaRaster::pixels() const noexcept {
    SkSurface* const surface = (*impl_).front_surface();
    if (surface == nullptr) return nullptr;
    const std::optional<SkPixmap> pixmap = (*surface).peekPixels();
    const void* result = pixmap ? (*pixmap).addr() : nullptr;
    return result;
}

std::size_t SkiaRaster::row_bytes() const noexcept {
    SkSurface* const surface = (*impl_).front_surface();
    if (surface == nullptr) return 0;
    const std::optional<SkPixmap> pixmap = (*surface).peekPixels();
    const std::size_t result = pixmap ? (*pixmap).rowBytes() : 0;
    return result;
}

std::uint32_t SkiaRaster::pixel_width() const noexcept {
    SkSurface* const surface = (*impl_).front_surface();
    std::uint32_t result = 0;
    if (surface != nullptr) result = static_cast<std::uint32_t>((*surface).width());
    return result;
}

std::uint32_t SkiaRaster::pixel_height() const noexcept {
    SkSurface* const surface = (*impl_).front_surface();
    std::uint32_t result = 0;
    if (surface != nullptr) result = static_cast<std::uint32_t>((*surface).height());
    return result;
}

std::size_t SkiaRaster::byte_size() const noexcept {
    return row_bytes() * pixel_height();
}

std::vector<std::byte> SkiaRaster::encode_png() const {
    std::vector<std::byte> result;
    SkSurface* const surface = (*impl_).front_surface();
    if (surface == nullptr) return result;
    const std::optional<SkPixmap> peeked = (*surface).peekPixels();
    if (!peeked || (*peeked).width() <= 0 || (*peeked).height() <= 0) return result;
    const SkPixmap& pixmap = *peeked;
    // The front surface is premultiplied BGRA8; the shared owned encoder
    // unpremultiplies so the file carries straight alpha, as on every host.
    if (pixmap.colorType() != kBGRA_8888_SkColorType || pixmap.alphaType() != kPremul_SkAlphaType) return result;
    const std::optional<std::size_t> pixel_bytes = pixmap.checkedByteSize();
    if (!pixel_bytes) return result;
    gui_forms::image::PngEncodeInput input{};
    input.width = static_cast<std::uint32_t>(pixmap.width());
    input.height = static_cast<std::uint32_t>(pixmap.height());
    input.row_bytes = pixmap.rowBytes();
    input.pixels = std::span<const std::byte>(static_cast<const std::byte*>(pixmap.addr()),
                                              *pixel_bytes);
    input.order = gui_forms::image::PngChannelOrder::bgra;
    input.alpha = gui_forms::image::PngSourceAlpha::premultiplied;
    if (!gui_forms::image::encode_png(input, result)) result.clear();
    return result;
}

void SkiaRaster::save() {
    if (SkCanvas* canvas = (*impl_).canvas()) {
        if (Impl::TextClipState* const state = (*impl_).current_text_clip(*canvas)) {
            const Impl::TextClipState copy = *state;
            (*impl_).text_clips.push_back(copy);
        }
        (*impl_).saved_offsets.push_back((*impl_).offset);
        (*canvas).save();
    }
}

void SkiaRaster::restore() {
    if (SkCanvas* canvas = (*impl_).canvas();
        canvas && (*canvas).getSaveCount() > (*impl_).save_floor) {
        if ((*impl_).current_text_clip(*canvas) != nullptr && (*impl_).text_clips.size() > 1U) {
            (*impl_).text_clips.pop_back();
        }
        if (!(*impl_).saved_offsets.empty()) {
            (*impl_).offset = (*impl_).saved_offsets.back();
            (*impl_).saved_offsets.pop_back();
        }
        (*canvas).restore();
    }
}

void SkiaRaster::translate(Point offset) {
    if (SkCanvas* canvas = (*impl_).canvas()) {
        (*canvas).translate(static_cast<SkScalar>(offset.x), static_cast<SkScalar>(offset.y));
        (*impl_).offset.x += offset.x;
        (*impl_).offset.y += offset.y;
    }
}

void SkiaRaster::clip_rect(Rect rect) {
    if (SkCanvas* canvas = (*impl_).canvas()) {
        (*canvas).clipRect(to_sk_rect(rect), SkClipOp::kIntersect, false);
        if (Impl::TextClipState* const state = (*impl_).current_text_clip(*canvas)) {
            const SkRect device = (*canvas).getTotalMatrix().mapRect(to_sk_rect(rect));
            if (!(*state).rect.intersect(device)) (*state).rect.setEmpty();
        }
    }
}

void SkiaRaster::clip_rounded_rect(Rect rect, double radius) {
    if (SkCanvas* canvas = (*impl_).canvas()) {
        (*canvas).clipRRect(to_sk_rrect(rect, radius), SkClipOp::kIntersect, true);
        if (Impl::TextClipState* const state = (*impl_).current_text_clip(*canvas)) {
            const SkMatrix matrix = (*canvas).getTotalMatrix();
            const SkRect device = matrix.mapRect(to_sk_rect(rect));
            // The innermost rounded clip and the one enclosing it are exact
            // (D6); one further out is kept as its bounds.
            (*state).push_rounded_clip();
            (*state).rounded = true;
            (*state).rounded_rect = device;
            (*state).radius = std::max(0.0, radius) * static_cast<double>(matrix.getScaleX());
            if (!(*state).rect.intersect(device)) (*state).rect.setEmpty();
        }
    }
}

void SkiaRaster::clip_rounded_rect_corners(Rect rect, PaintCornerRadii radii) {
    if (SkCanvas* canvas = (*impl_).canvas()) {
        (*canvas).clipRRect(to_sk_rrect_corners(rect, radii), SkClipOp::kIntersect, true);
        if (Impl::TextClipState* const state = (*impl_).current_text_clip(*canvas)) {
            // The text clip keeps one radius: the per-corner clip is kept as
            // its bounds rounded by the largest radius, so glyphs never pass
            // outside the exact clip (they may lose pixels at a corner whose
            // radius is smaller).
            const SkMatrix matrix = (*canvas).getTotalMatrix();
            const SkRect device = matrix.mapRect(to_sk_rect(rect));
            const double largest = std::max(std::max(radii.top_left, radii.top_right),
                                            std::max(radii.bottom_right, radii.bottom_left));
            (*state).push_rounded_clip();
            (*state).rounded = true;
            (*state).rounded_rect = device;
            (*state).radius = std::max(0.0, largest) * static_cast<double>(matrix.getScaleX());
            if (!(*state).rect.intersect(device)) (*state).rect.setEmpty();
        }
    }
}

void SkiaRaster::fill_rect(Rect rect, Color color) {
    if (SkCanvas* canvas = (*impl_).canvas()) {
        SkPaint paint = make_paint(color);
        const SkRect box = snapped_box((*impl_).snap_transform_of(*canvas), rect, paint);
        (*canvas).drawRect(box, paint);
    }
}

void SkiaRaster::fill_rounded_rect(Rect rect, double radius, Color color) {
    if (SkCanvas* canvas = (*impl_).canvas()) {
        // The bounds snap, in device space; the corners keep their
        // antialiasing.
        const SnapTransform transform = (*impl_).snap_transform_of(*canvas);
        SkRect device{};
        if (snapped_device_rect(transform, rect, device)) {
            const double bounded = std::clamp(radius, 0.0, std::max(0.0, std::min(rect.width, rect.height) * 0.5));
            const SkVector radii = device_radii(transform, bounded, device);
            (*canvas).save();
            (*canvas).resetMatrix();
            (*canvas).drawRRect(SkRRect::MakeRectXY(device, radii.x(), radii.y()), make_paint(color));
            (*canvas).restore();
            return;
        }
        const SkRect bounds = to_sk_rect(rect);
        const SkScalar bounded = static_cast<SkScalar>(std::clamp(
            radius, 0.0, std::max(0.0, static_cast<double>(std::min(bounds.width(), bounds.height())) * 0.5)));
        (*canvas).drawRRect(SkRRect::MakeRectXY(bounds, bounded, bounded), make_paint(color));
    }
}

void SkiaRaster::stroke_rect(Rect rect, Color color, double width) {
    if (SkCanvas* canvas = (*impl_).canvas()) {
        SkPaint paint = make_paint(color);
        // The stroke is centred on the edges. Snapped, it is the ring between
        // its snapped outer and inner edges, filled without antialiasing.
        const double half = width * 0.5;
        const SnapTransform transform = (*impl_).snap_transform_of(*canvas);
        SkRect outer{};
        SkRect inner{};
        const bool snapped = std::isfinite(half) && half > 0.0 &&
            snapped_local_rect(transform, outset_rect(rect, half), outer) &&
            snapped_local_rect(transform, outset_rect(rect, -half), inner);
        if (snapped) {
            SkPathBuilder ring(SkPathFillType::kEvenOdd);
            ring.addRect(outer);
            if (!inner.isEmpty()) ring.addRect(inner);
            const SkPath path = ring.detach();
            paint.setAntiAlias(false);
            (*canvas).drawPath(path, paint);
            return;
        }
        paint.setStyle(SkPaint::kStroke_Style);
        paint.setStrokeWidth(static_cast<SkScalar>(width));
        (*canvas).drawRect(to_sk_rect(rect), paint);
    }
}

void SkiaRaster::stroke_rounded_rect(Rect rect, double radius, Color color,
                                    double width) {
    if (SkCanvas* canvas = (*impl_).canvas()) {
        SkPaint paint = make_paint(color);
        // The stroke is centred on the edges. Snapped, it is the ring between
        // its snapped outer and inner bounds, in device space as a rounded
        // fill snaps its bounds; the corners keep their antialiasing.
        const double half = width * 0.5;
        const SnapTransform transform = (*impl_).snap_transform_of(*canvas);
        SkRect outer{};
        SkRect inner{};
        const bool snapped = std::isfinite(half) && half > 0.0 &&
            snapped_device_rect(transform, outset_rect(rect, half), outer) &&
            snapped_device_rect(transform, outset_rect(rect, -half), inner);
        if (snapped) {
            const double bounded = std::clamp(radius, 0.0, std::max(0.0, std::min(rect.width, rect.height) * 0.5));
            const SkVector outer_radii = device_radii(transform, bounded + half, outer);
            const SkRRect outer_shape = SkRRect::MakeRectXY(outer, outer_radii.x(), outer_radii.y());
            (*canvas).save();
            (*canvas).resetMatrix();
            if (inner.isEmpty()) {
                (*canvas).drawRRect(outer_shape, paint);
            } else {
                const SkVector inner_radii = device_radii(transform, std::max(0.0, bounded - half), inner);
                const SkRRect inner_shape = SkRRect::MakeRectXY(inner, inner_radii.x(), inner_radii.y());
                (*canvas).drawDRRect(outer_shape, inner_shape, paint);
            }
            (*canvas).restore();
            return;
        }
        paint.setStyle(SkPaint::kStroke_Style);
        paint.setStrokeWidth(static_cast<SkScalar>(width));
        (*canvas).drawRRect(to_sk_rrect(rect, radius), paint);
    }
}

void SkiaRaster::fill_linear_gradient(
    Rect rect, Point start, Point end,
    std::span<const GradientStop> stops) {
    fill_linear_gradient_spread(rect, start, end, stops,
                                GradientSpreadMode::pad);
}

void SkiaRaster::fill_linear_gradient_spread(
    Rect rect, Point start, Point end,
    std::span<const GradientStop> stops, GradientSpreadMode spread) {
    SkCanvas* canvas = (*impl_).canvas();
    if (canvas == nullptr || rect.empty() || !valid_gradient_stops(stops) ||
        (spread != GradientSpreadMode::pad &&
         spread != GradientSpreadMode::repeat &&
         spread != GradientSpreadMode::reflect)) {
        return;
    }
    const SkGradientData data = to_sk_gradient(stops);
    const SkPoint points[2]{{static_cast<SkScalar>(start.x),
                             static_cast<SkScalar>(start.y)},
                            {static_cast<SkScalar>(end.x),
                             static_cast<SkScalar>(end.y)}};
    // The painter contract draws nothing for geometry stx_skia refuses (points
    // that overflow float, for example), as for the invalid input checked above.
    SkGradientShaderResult made = SkShaders::LinearGradient(
        points, make_sk_gradient(data, spread));
    if (made.rejection != SkGradientRejection::kNone) return;
    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setShader(std::move(made.shader));
    const SkRect box = snapped_box((*impl_).snap_transform_of(*canvas), rect, paint);
    (*canvas).drawRect(box, paint);
}

void SkiaRaster::fill_radial_gradient(
    Rect rect, Point center, Size radii,
    std::span<const GradientStop> stops) {
    SkCanvas* canvas = (*impl_).canvas();
    if (canvas == nullptr || rect.empty() || radii.width <= 0.0 ||
        radii.height <= 0.0 || !valid_gradient_stops(stops)) {
        return;
    }
    const SkGradientData data = to_sk_gradient(stops);
    SkGradientShaderResult made = SkShaders::RadialGradient(
        {0.0F, 0.0F}, 1.0F, make_sk_gradient(data));
    if (made.rejection != SkGradientRejection::kNone) return;
    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setShader(std::move(made.shader));
    (*canvas).save();
    SkRect box = to_sk_rect(rect);
    const bool snapped = snapped_local_rect((*impl_).snap_transform_of(*canvas), rect, box);
    (*canvas).clipRect(box, SkClipOp::kIntersect, !snapped);
    (*canvas).translate(static_cast<SkScalar>(center.x),
                      static_cast<SkScalar>(center.y));
    (*canvas).scale(static_cast<SkScalar>(radii.width),
                  static_cast<SkScalar>(radii.height));
    (*canvas).drawRect(SkRect::MakeLTRB(
        static_cast<SkScalar>((rect.x - center.x) / radii.width),
        static_cast<SkScalar>((rect.y - center.y) / radii.height),
        static_cast<SkScalar>((rect.x + rect.width - center.x) / radii.width),
        static_cast<SkScalar>((rect.y + rect.height - center.y) / radii.height)),
        paint);
    (*canvas).restore();
}

void SkiaRaster::draw_box_shadow(Rect rect, double corner_radius, Point offset,
                                 double blur_radius, double spread,
                                 Color color) {
    SkCanvas* canvas = (*impl_).canvas();
    if (canvas == nullptr || rect.empty() || color.alpha == 0U ||
        std::isnan(blur_radius) || blur_radius < 0.0) {
        return;
    }
    Rect shadow{rect.x + offset.x - spread, rect.y + offset.y - spread,
                rect.width + spread * 2.0, rect.height + spread * 2.0};
    if (shadow.empty()) return;
    SkPaint paint = make_paint(color);
    if (blur_radius > 0.0) {
        SkBlurMaskFilterResult blur = SkMaskFilter::MakeBlur(
            kNormal_SkBlurStyle, static_cast<SkScalar>(blur_radius * 0.5), false);
        // A blur the filter refuses (a radius past float range) draws no shadow, never a
        // sharp one.
        if (!blur.filter) return;
        paint.setMaskFilter(std::move(blur.filter));
    }
    (*canvas).drawRRect(to_sk_rrect(
        shadow, std::max(0.0, corner_radius + spread)), paint);
}

namespace {

// Blurred coverage of one retained draw_box_shadow command. Its geometry is
// fixed by the command; the mask depends only on the device scale and the
// subpixel phase of the shadow's device origin, not on its position.
class ShadowMaskMemo final : public RetainedDrawMemo {
public:
    SkScalar scale_x{};
    SkScalar scale_y{};
    SkScalar phase_x{};
    SkScalar phase_y{};
    // Mask pixel (0, 0) lies at floor(device origin) - margin.
    int margin{};
    sk_sp<SkImage> mask;
};

} // namespace

void SkiaRaster::draw_retained_box_shadow(const Rect rect, const double corner_radius,
                                          const Point offset, const double blur_radius,
                                          const double spread, const Color color,
                                          const std::shared_ptr<RetainedDrawCache>& cache) {
    SkCanvas* const canvas = (*impl_).canvas();
    if (canvas == nullptr || rect.empty() || color.alpha == 0U ||
        std::isnan(blur_radius) || blur_radius < 0.0) return;
    const SkMatrix matrix = (*canvas).getTotalMatrix();
    // Unblurred shadows, rotations and reflections draw directly. So do
    // square-cornered ones: Skia blurs a rectangle as nine patches it keeps
    // itself, whatever the rectangle's size, while a kept coverage mask is
    // as large as the shadow and is applied through a shader at every
    // pixel (a panned canvas's sheet shadow, benchmarks/
    // canvas_pan_benchmark.cpp).
    const bool square = !(corner_radius + spread > 0.0);
    if (!cache || blur_radius == 0.0 || square || !matrix.isScaleTranslate() ||
        matrix.getScaleX() <= 0.0F || matrix.getScaleY() <= 0.0F) {
        draw_box_shadow(rect, corner_radius, offset, blur_radius, spread, color);
        return;
    }
    const Rect shadow{rect.x + offset.x - spread, rect.y + offset.y - spread,
                      rect.width + spread * 2.0, rect.height + spread * 2.0};
    if (shadow.empty()) return;
    const SkRRect local = to_sk_rrect(shadow, std::max(0.0, corner_radius + spread));
    const SkRect device = matrix.mapRect(local.rect());
    const SkScalar origin_x = std::floor(device.left());
    const SkScalar origin_y = std::floor(device.top());
    const SkScalar phase_x = device.left() - origin_x;
    const SkScalar phase_y = device.top() - origin_y;
    ShadowMaskMemo* memo = dynamic_cast<ShadowMaskMemo*>((*cache).memo.get());
    const bool current = memo != nullptr && (*memo).mask &&
        (*memo).scale_x == matrix.getScaleX() && (*memo).scale_y == matrix.getScaleY() &&
        (*memo).phase_x == phase_x && (*memo).phase_y == phase_y;
    if (!current) {
        // The mask filter's sigma is in device pixels (respectCTM is false).
        const double sigma = blur_radius * 0.5;
        const double margin_extent = std::ceil(sigma * 3.0) + 2.0;
        const double width_extent =
            std::ceil(static_cast<double>(device.right()) - origin_x) + margin_extent * 2.0;
        const double height_extent =
            std::ceil(static_cast<double>(device.bottom()) - origin_y) + margin_extent * 2.0;
        // A coverage mask past this extent is not kept; the shadow draws directly. The checks
        // also keep the conversions to int below in range (NaN fails them too).
        constexpr double largest_kept_extent = 16384.0;
        if (!(width_extent <= largest_kept_extent) || !(height_extent <= largest_kept_extent)) {
            draw_box_shadow(rect, corner_radius, offset, blur_radius, spread, color);
            return;
        }
        const int margin = static_cast<int>(margin_extent);
        const int width = static_cast<int>(width_extent);
        const int height = static_cast<int>(height_extent);
        SkBlurMaskFilterResult blur = SkMaskFilter::MakeBlur(
            kNormal_SkBlurStyle, static_cast<SkScalar>(sigma), false);
        if (!blur.filter) {
            draw_box_shadow(rect, corner_radius, offset, blur_radius, spread, color);
            return;
        }
        sk_sp<SkSurface> coverage = SkSurfaces::Raster(SkImageInfo::MakeA8(width, height));
        if (!coverage) {
            draw_box_shadow(rect, corner_radius, offset, blur_radius, spread, color);
            return;
        }
        SkCanvas& target = *(*coverage).getCanvas();
        target.clear(SK_ColorTRANSPARENT);
        // Same geometry, matrix and blur as draw_box_shadow, moved by whole
        // pixels so every coverage value is the one it would have produced.
        SkMatrix placement = matrix;
        placement.postTranslate(static_cast<SkScalar>(margin) - origin_x,
                                static_cast<SkScalar>(margin) - origin_y);
        target.setMatrix(placement);
        SkPaint opaque{};
        opaque.setAntiAlias(true);
        opaque.setColor(SK_ColorWHITE);
        opaque.setMaskFilter(std::move(blur.filter));
        target.drawRRect(local, opaque);
        std::unique_ptr<ShadowMaskMemo> replacement = std::make_unique<ShadowMaskMemo>();
        (*replacement).scale_x = matrix.getScaleX();
        (*replacement).scale_y = matrix.getScaleY();
        (*replacement).phase_x = phase_x;
        (*replacement).phase_y = phase_y;
        (*replacement).margin = margin;
        (*replacement).mask = (*coverage).makeImageSnapshot();
        memo = replacement.get();
        (*cache).memo = std::move(replacement);
    }
    // Apply the kept coverage through a mask filter so the color is blended
    // by the same mask blitter as draw_box_shadow; an alpha-image draw rounds
    // differently by one or two levels.
    const SkScalar left = origin_x - static_cast<SkScalar>((*memo).margin);
    const SkScalar top = origin_y - static_cast<SkScalar>((*memo).margin);
    const SkMatrix placement = SkMatrix::Translate(left, top);
    SkPaint paint = make_paint(color);
    paint.setMaskFilter(SkShaderMaskFilter::Make(
        (*(*memo).mask).makeShader(SkSamplingOptions(), &placement)));
    (*canvas).save();
    (*canvas).resetMatrix();
    (*canvas).drawRect(SkRect::MakeXYWH(left, top, static_cast<SkScalar>((*(*memo).mask).width()),
                                        static_cast<SkScalar>((*(*memo).mask).height())), paint);
    (*canvas).restore();
}

void SkiaRaster::draw_inset_box_shadow(
    Rect rect, double corner_radius, Point offset, double blur_radius,
    double spread, Color color) {
    SkCanvas* canvas = (*impl_).canvas();
    if (canvas == nullptr || rect.empty() || color.alpha == 0U ||
        std::isnan(blur_radius) || blur_radius < 0.0) {
        return;
    }
    const Rect hole{rect.x + offset.x + spread,
                    rect.y + offset.y + spread,
                    rect.width - spread * 2.0,
                    rect.height - spread * 2.0};
    const double reach = std::max(rect.width, rect.height) +
                         std::abs(offset.x) + std::abs(offset.y) +
                         std::abs(spread) + blur_radius * 3.0 + 1.0;
    const Rect outside{rect.x - reach, rect.y - reach,
                       rect.width + reach * 2.0,
                       rect.height + reach * 2.0};
    // The fill type goes to the constructor, as in the executor's builders.
    // Set afterwards with setFillType, it did not reach the built path in
    // the sanitized build, whose toolkit is instrumented while libskia is
    // not: the even-odd hole was filled (#167).
    SkPathBuilder complement_builder(SkPathFillType::kEvenOdd);
    complement_builder.addRect(to_sk_rect(outside));
    if (!hole.empty()) {
        complement_builder.addRRect(to_sk_rrect(
            hole, std::max(0.0, corner_radius - spread)));
    }
    const SkPath complement = complement_builder.detach();
    SkPaint paint = make_paint(color);
    if (blur_radius > 0.0) {
        SkBlurMaskFilterResult blur = SkMaskFilter::MakeBlur(
            kNormal_SkBlurStyle, static_cast<SkScalar>(blur_radius * 0.5), false);
        // As for draw_box_shadow: a refused blur draws no shadow, never a sharp one.
        if (!blur.filter) return;
        paint.setMaskFilter(std::move(blur.filter));
    }
    (*canvas).save();
    (*canvas).clipRRect(to_sk_rrect(rect, corner_radius),
                       SkClipOp::kIntersect, true);
    (*canvas).drawPath(complement, paint);
    (*canvas).restore();
}

void SkiaRaster::draw_line(Point from, Point to, Color color, double width) {
    if (SkCanvas* canvas = (*impl_).canvas()) {
        SkPaint paint = make_paint(color);
        paint.setStyle(SkPaint::kStroke_Style);
        paint.setStrokeWidth(static_cast<SkScalar>(width));
        (*canvas).drawLine(static_cast<SkScalar>(from.x), static_cast<SkScalar>(from.y),
                         static_cast<SkScalar>(to.x), static_cast<SkScalar>(to.y), paint);
    }
}

void SkiaRaster::Impl::draw_shaped_text(SkCanvas& target, const text::ShapedText& shaped,
                                        const Point origin, const FontSpec font_spec, const Color color) {
    const SkMatrix matrix = target.getTotalMatrix();
    if (matrix.hasPerspective()) return;
    if (!glyph_runs) glyph_runs = std::make_unique<text::GlyphRunRasterizer>(text_engine);
    const SkPoint device = matrix.mapPoint(SkPoint::Make(static_cast<SkScalar>(origin.x), static_cast<SkScalar>(origin.y)));
    text::GlyphRunOrigin placement{};
    text::GlyphRunCoverage coverage{};
    if (matrix.isScaleTranslate() && matrix.getScaleX() > 0.0F && matrix.getScaleX() == matrix.getScaleY()) {
        placement = text::glyph_run_origin(static_cast<double>(device.x()), static_cast<double>(device.y()));
        coverage = (*glyph_runs).rasterize(shaped, font_spec, static_cast<double>(matrix.getScaleX()),
                                           placement.phase_x);
    } else {
        const text::GlyphRunTransform transform{static_cast<double>(matrix.getScaleX()),
                                                static_cast<double>(matrix.getSkewX()),
                                                static_cast<double>(matrix.getSkewY()),
                                                static_cast<double>(matrix.getScaleY())};
        coverage = (*glyph_runs).rasterize_transformed(shaped, font_spec, transform, static_cast<double>(device.x()),
                                                       static_cast<double>(device.y()), {}, placement);
    }
    if (coverage.pixels.empty()) return;
    const SkPixmap mask(SkImageInfo::MakeA8(static_cast<int>(coverage.width), static_cast<int>(coverage.height)),
                        coverage.pixels.data(), static_cast<std::size_t>(coverage.width));
    const sk_sp<SkImage> image = SkImages::RasterFromPixmapCopy(mask);
    if (!image) return;
    // An alpha-only image draws in the paint's colour.
    const SkPaint paint = make_paint(color);
    target.save();
    target.resetMatrix();
    target.drawImage(image, static_cast<SkScalar>(placement.pixel_x + coverage.left),
                     static_cast<SkScalar>(placement.pixel_y + coverage.top), SkSamplingOptions(), &paint);
    target.restore();
}


void SkiaRaster::Impl::refresh_kept_text() {
    const std::size_t faces = text_engine.face_count();
    if (faces == kept_face_count) return;
    kept_shapes.clear();
    kept_layouts.clear();
    kept_face_count = faces;
}

const text::ShapedText& SkiaRaster::Impl::shaped_text(const std::string_view text, const FontSpec font) {
    refresh_kept_text();
    if (!keepable_text(text, font)) {
        unkept_shape = text_engine.shape(text, font);
        return unkept_shape;
    }
    const KeptTextProbe probe{text, font};
    const std::unordered_map<KeptTextKey, text::ShapedText, KeptTextHash, KeptTextEqual>::const_iterator found =
        kept_shapes.find(probe);
    if (found != kept_shapes.end()) return (*found).second;
    // Shaping may open a lazy face; that does not change face_count, and the
    // run it gives is the run every later shaping gives.
    text::ShapedText shaped = text_engine.shape(text, font);
    if (kept_shapes.size() >= maximum_kept_text_entries) kept_shapes.clear();
    KeptTextKey key{std::string(text), font};
    const std::pair<std::unordered_map<KeptTextKey, text::ShapedText, KeptTextHash, KeptTextEqual>::iterator, bool>
        inserted = kept_shapes.emplace(std::move(key), std::move(shaped));
    return (*inserted.first).second;
}

const ResolvedTextLayout& SkiaRaster::Impl::resolved_layout(const std::string_view text, const FontSpec font) {
    refresh_kept_text();
    if (!keepable_text(text, font)) {
        unkept_layout = text_engine.resolve(text, font);
        return unkept_layout;
    }
    const KeptTextProbe probe{text, font};
    const std::unordered_map<KeptTextKey, ResolvedTextLayout, KeptTextHash, KeptTextEqual>::const_iterator found =
        kept_layouts.find(probe);
    if (found != kept_layouts.end()) return (*found).second;
    ResolvedTextLayout layout = text_engine.resolve(text, font);
    if (kept_layouts.size() >= maximum_kept_text_entries) kept_layouts.clear();
    KeptTextKey key{std::string(text), font};
    const std::pair<std::unordered_map<KeptTextKey, ResolvedTextLayout, KeptTextHash, KeptTextEqual>::iterator, bool>
        inserted = kept_layouts.emplace(std::move(key), std::move(layout));
    return (*inserted.first).second;
}

namespace {

// Shaping of one retained draw_text_utf8 command. Text and font are fixed by
// the command; the result also depends on which faces the engine holds.
class ShapedTextMemo final : public RetainedDrawMemo {
public:
    std::weak_ptr<const bool> engine;
    std::size_t face_count{};
    FontSpec font{};
    std::size_t text_bytes{};
    // The text's hash beside its length (N58): a cache handed a different
    // text of the same length is made again rather than drawing the old one.
    std::size_t text_hash{};
    text::ShapedText shaped;
    // ADR-004 coverage of the shaped run at one device scale and phase.
    bool has_coverage{};
    double coverage_scale{};
    std::int32_t coverage_phase{};
    text::GlyphRunCoverage coverage;
};

} // namespace

void SkiaRaster::draw_retained_text_utf8(const Point origin, const std::string_view text,
                                         const FontSpec font_spec, const Color color,
                                         const std::shared_ptr<RetainedDrawCache>& cache) {
    SkCanvas* const canvas = (*impl_).canvas();
    if (canvas == nullptr || text.empty()) return;
    if (!cache) {
        draw_text_utf8(origin, text, font_spec, color);
        return;
    }
    const std::size_t face_count = (*impl_).text_engine.face_count();
    const std::size_t text_hash = std::hash<std::string_view>{}(text);
    ShapedTextMemo* memo = dynamic_cast<ShapedTextMemo*>((*cache).memo.get());
    const bool current = memo != nullptr &&
        (*memo).engine.lock() == (*impl_).text_engine_identity &&
        (*memo).face_count == face_count && (*memo).font == font_spec &&
        (*memo).text_bytes == text.size() && (*memo).text_hash == text_hash;
    if (!current) {
        std::unique_ptr<ShapedTextMemo> replacement = std::make_unique<ShapedTextMemo>();
        (*replacement).engine = (*impl_).text_engine_identity;
        (*replacement).face_count = face_count;
        (*replacement).font = font_spec;
        (*replacement).text_bytes = text.size();
        (*replacement).text_hash = text_hash;
        (*replacement).shaped = (*impl_).shaped_text(text, font_spec);
        memo = replacement.get();
        (*cache).memo = std::move(replacement);
    }
    if ((*impl_).composite_run(*canvas, (*memo).shaped, font_spec, origin, color, &(*memo).coverage,
                               &(*memo).coverage_scale, &(*memo).coverage_phase)) {
        (*memo).has_coverage = true;
        return;
    }
    (*impl_).draw_shaped_text(*canvas, (*memo).shaped, origin, font_spec, color);
}

bool SkiaRaster::Impl::composite_run(SkCanvas& target, const text::ShapedText& shaped, const FontSpec font,
                                     const Point origin, const Color color, text::GlyphRunCoverage* const kept,
                                     double* const kept_scale, std::int32_t* const kept_phase) {
    TextClipState* const state = current_text_clip(target);
    if (state == nullptr) return false;
    const std::optional<SkPixmap> peeked = target.peekPixels();
    if (!peeked) return false;
    const SkPixmap& pixmap = *peeked;
    if (pixmap.alphaType() != kPremul_SkAlphaType ||
        pixmap.colorType() != kBGRA_8888_SkColorType ||
        pixmap.rowBytes() % 4U != 0U) {
        return false;
    }
    const SkMatrix matrix = target.getTotalMatrix();
    if (!matrix.isScaleTranslate() || matrix.getScaleX() <= 0.0F || matrix.getScaleX() != matrix.getScaleY()) return false;
    const double device_scale = static_cast<double>(matrix.getScaleX());
    const SkPoint device = matrix.mapPoint(SkPoint::Make(static_cast<SkScalar>(origin.x), static_cast<SkScalar>(origin.y)));
    const text::GlyphRunOrigin placement = text::glyph_run_origin(static_cast<double>(device.x()), static_cast<double>(device.y()));
    if (!glyph_runs) glyph_runs = std::make_unique<text::GlyphRunRasterizer>(text_engine);
    text::GlyphRunCoverage fresh{};
    const text::GlyphRunCoverage* coverage = &fresh;
    if (kept != nullptr && kept_scale != nullptr && kept_phase != nullptr && *kept_scale == device_scale &&
        *kept_phase == placement.phase_x && !(*kept).pixels.empty()) {
        coverage = kept;
    } else {
        fresh = (*glyph_runs).rasterize(shaped, font, device_scale, placement.phase_x);
        if (kept != nullptr && kept_scale != nullptr && kept_phase != nullptr) {
            *kept = std::move(fresh);
            *kept_scale = device_scale;
            *kept_phase = placement.phase_x;
            coverage = kept;
        }
    }
    if ((*coverage).pixels.empty()) return true;
    const detail::TextCoverage source{.pixels = (*coverage).pixels, .width = (*coverage).width,
                                      .height = (*coverage).height,
                                      .left = placement.pixel_x + (*coverage).left,
                                      .top = placement.pixel_y + (*coverage).top};
    const bool composited = composite_coverage(target, source, color);
    return composited;
}

std::optional<SkiaRaster::Impl::SharedComposite> SkiaRaster::Impl::shared_composite(SkCanvas& target) {
    TextClipState* const state = current_text_clip(target);
    if (state == nullptr) return std::nullopt;
    const std::optional<SkPixmap> peeked = target.peekPixels();
    if (!peeked) return std::nullopt;
    const SkPixmap& pixmap = *peeked;
    if (pixmap.alphaType() != kPremul_SkAlphaType ||
        pixmap.colorType() != kBGRA_8888_SkColorType ||
        pixmap.rowBytes() % 4U != 0U) {
        return std::nullopt;
    }
    const std::size_t stride = pixmap.rowBytes() / 4U;
    const std::size_t pixel_count = stride * static_cast<std::size_t>(pixmap.height());
    detail::TextCompositeTarget composite_target{};
    composite_target.pixels = std::span<std::uint32_t>(pixmap.writable_addr32(0, 0), pixel_count);
    composite_target.width = static_cast<std::uint32_t>(pixmap.width());
    composite_target.height = static_cast<std::uint32_t>(pixmap.height());
    composite_target.stride = stride;
    composite_target.opaque = false;
    detail::TextCompositeClip clip{};
    clip.rect = {static_cast<double>((*state).rect.left()), static_cast<double>((*state).rect.top()),
                 static_cast<double>((*state).rect.width()), static_cast<double>((*state).rect.height())};
    if ((*state).rounded) {
        clip.rounded = true;
        clip.rounded_rect = {static_cast<double>((*state).rounded_rect.left()), static_cast<double>((*state).rounded_rect.top()),
                             static_cast<double>((*state).rounded_rect.width()), static_cast<double>((*state).rounded_rect.height())};
        clip.radius = (*state).radius;
        if ((*state).outer_rounded) {
            clip.outer_rounded = true;
            clip.outer_rounded_rect = {static_cast<double>((*state).outer_rounded_rect.left()),
                                       static_cast<double>((*state).outer_rounded_rect.top()),
                                       static_cast<double>((*state).outer_rounded_rect.width()),
                                       static_cast<double>((*state).outer_rounded_rect.height())};
            clip.outer_radius = (*state).outer_radius;
        }
    }
    const SharedComposite shared{state, composite_target, clip};
    return shared;
}

bool SkiaRaster::Impl::composite_coverage(SkCanvas& target, const detail::TextCoverage& source, const Color color) {
    const std::optional<SharedComposite> shared = shared_composite(target);
    if (!shared) return false;
    const TextClipState* const state = (*shared).state;
    if ((*state).rect.isEmpty()) return true;
    const detail::TextCompositeTarget composite_target = (*shared).target;
    const detail::TextCompositeClip clip = (*shared).clip;
    if (!(*state).region_limited) {
        static_cast<void>(detail::composite_text_coverage(source, composite_target, clip, color));
        return true;
    }
    // Disjoint region rectangles: no pixel is blended twice.
    for (SkRegion::Iterator piece((*state).region); !piece.done(); piece.next()) {
        const SkIRect bounds = piece.rect();
        detail::TextCompositeClip piece_clip = clip;
        const Rect piece_rect{static_cast<double>(bounds.left()), static_cast<double>(bounds.top()),
                              static_cast<double>(bounds.width()), static_cast<double>(bounds.height())};
        piece_clip.rect = Rect::intersection(clip.rect, piece_rect);
        if (piece_clip.rect.empty()) continue;
        static_cast<void>(detail::composite_text_coverage(source, composite_target, piece_clip, color));
    }
    return true;
}

bool SkiaRaster::Impl::composite_part(SkCanvas& target, const PartRaster& raster, const Point origin,
                                      const double opacity) {
    const SkMatrix matrix = target.getTotalMatrix();
    const SkScalar raster_scale = static_cast<SkScalar>(raster.scale().value);
    if (!matrix.isScaleTranslate() || raster_scale <= 0.0F) return false;
    const DeviceExtent covered = raster.extent();
    if (matrix.getScaleX() != raster_scale || matrix.getScaleY() != raster_scale) {
        // Baked at another device scale than the target's (a window painted
        // into a framebuffer of another scale, as an offscreen capture
        // does): sampled over its own logical rectangle rather than lost.
        const double logical_scale = static_cast<double>(raster_scale);
        const Rect destination{origin.x, origin.y, static_cast<double>(covered.width) / logical_scale,
                               static_cast<double>(covered.height) / logical_scale};
        const bool resampled = composite_part_scaled(target, raster, destination, opacity);
        return resampled;
    }
    // The raster's first pixel goes on the device pixel nearest its origin.
    const SkPoint device = matrix.mapPoint(SkPoint::Make(static_cast<SkScalar>(origin.x), static_cast<SkScalar>(origin.y)));
    const DeviceExtent extent = raster.extent();
    const detail::PartRasterSource source{.pixels = raster.pixels(), .width = extent.width, .height = extent.height,
                                          .row_bytes = raster.row_bytes(),
                                          .left = std::llround(static_cast<double>(device.x())),
                                          .top = std::llround(static_cast<double>(device.y())),
                                          .opaque = raster.opaque()};
    const bool composited = composite_source(target, source, opacity);
    return composited;
}

// As composite_part, stretched over `destination`: its corners go on the
// device pixels nearest them, and the shared blend samples the raster, at
// the target's scale whatever scale the raster was baked at.
bool SkiaRaster::Impl::composite_part_scaled(SkCanvas& target, const PartRaster& raster, const Rect destination,
                                             const double opacity) {
    const SkMatrix matrix = target.getTotalMatrix();
    if (!matrix.isScaleTranslate()) return false;
    const SkPoint first = matrix.mapPoint(SkPoint::Make(static_cast<SkScalar>(destination.x),
                                                        static_cast<SkScalar>(destination.y)));
    const SkPoint last = matrix.mapPoint(SkPoint::Make(static_cast<SkScalar>(destination.x + destination.width),
                                                       static_cast<SkScalar>(destination.y + destination.height)));
    const long long left = std::llround(static_cast<double>(first.x()));
    const long long top = std::llround(static_cast<double>(first.y()));
    const long long right = std::llround(static_cast<double>(last.x()));
    const long long bottom = std::llround(static_cast<double>(last.y()));
    if (right <= left || bottom <= top) return true;
    const DeviceExtent extent = raster.extent();
    const detail::PartRasterSource source{.pixels = raster.pixels(), .width = extent.width, .height = extent.height,
                                          .row_bytes = raster.row_bytes(), .left = left, .top = top,
                                          .shown_width = static_cast<std::uint32_t>(right - left),
                                          .shown_height = static_cast<std::uint32_t>(bottom - top)};
    const bool composited = composite_source(target, source, opacity);
    return composited;
}

bool SkiaRaster::Impl::composite_source(SkCanvas& target, const detail::PartRasterSource& source,
                                        const double opacity) {
    const std::optional<SharedComposite> shared = shared_composite(target);
    if (!shared) return false;
    const TextClipState* const state = (*shared).state;
    if ((*state).rect.isEmpty()) return true;
    const std::uint8_t byte = detail::part_composite_opacity(opacity);
    const detail::TextCompositeTarget composite_target = (*shared).target;
    const detail::TextCompositeClip clip = (*shared).clip;
    if (!(*state).region_limited) {
        static_cast<void>(detail::composite_part_raster(source, composite_target, clip, byte));
        return true;
    }
    // Disjoint region rectangles: no pixel is blended twice.
    for (SkRegion::Iterator piece((*state).region); !piece.done(); piece.next()) {
        const SkIRect bounds = piece.rect();
        detail::TextCompositeClip piece_clip = clip;
        const Rect piece_rect{static_cast<double>(bounds.left()), static_cast<double>(bounds.top()),
                              static_cast<double>(bounds.width()), static_cast<double>(bounds.height())};
        piece_clip.rect = Rect::intersection(clip.rect, piece_rect);
        if (piece_clip.rect.empty()) continue;
        static_cast<void>(detail::composite_part_raster(source, composite_target, piece_clip, byte));
    }
    return true;
}

void SkiaRaster::draw_text_utf8(Point origin,
                                std::string_view text,
                                FontSpec font_spec,
                                Color color) {
    if (SkCanvas* canvas = (*impl_).canvas(); canvas && !text.empty()) {
        const text::ShapedText& shaped = (*impl_).shaped_text(text, font_spec);
        if ((*impl_).composite_run(*canvas, shaped, font_spec, origin, color, nullptr, nullptr, nullptr)) return;
        (*impl_).draw_shaped_text(*canvas, shaped, origin, font_spec, color);
    }
}

void SkiaRaster::draw_turned_text_utf8(const Point origin, const std::string_view text, const FontSpec font_spec,
                                       const Color color, const TextOrientation orientation) {
    if (orientation == TextOrientation::horizontal) {
        draw_text_utf8(origin, text, font_spec, color);
        return;
    }
    SkCanvas* const canvas = (*impl_).canvas();
    if (canvas == nullptr || text.empty()) return;
    const text::ShapedText& shaped = (*impl_).shaped_text(text, font_spec);
    // A positive Skia rotation turns clockwise on the y-down surface:
    // bottom_to_top is a quarter turn anticlockwise, top_to_bottom clockwise.
    const SkScalar degrees = orientation == TextOrientation::bottom_to_top ? -90.0F : 90.0F;
    (*canvas).save();
    (*canvas).translate(static_cast<SkScalar>(origin.x), static_cast<SkScalar>(origin.y));
    (*canvas).rotate(degrees);
    const Point run_origin{0.0, 0.0};
    (*impl_).draw_shaped_text(*canvas, shaped, run_origin, font_spec, color);
    (*canvas).restore();
}

Size SkiaRaster::measure_text_utf8(std::string_view text,
                                   FontSpec font_spec) {
    if (text.empty()) return {0.0, font_spec.size};
    const text::ShapedText& shaped = (*impl_).shaped_text(text, font_spec);
    const Size measured{std::max(0.0, shaped.width), std::max(0.0, shaped.height)};
    return measured;
}

ResolvedTextLayout SkiaRaster::resolve_text_layout_utf8(
    std::string_view text, FontSpec font_spec) {
    const ResolvedTextLayout& layout = (*impl_).resolved_layout(text, font_spec);
    return layout;
}

TextExtent SkiaRaster::measure_text_extent_utf8(
    std::string_view text, FontSpec font_spec) {
    const ResolvedTextLayout& layout = (*impl_).resolved_layout(text, font_spec);
    const TextExtent extent = text_extent_of(layout);
    return extent;
}

TextResolutionStatus SkiaRaster::fill_line_break_opportunities_utf8(
    std::string_view text, std::span<LineBreakOpportunity> opportunities) {
    const TextResolutionStatus status =
        text::HarfBuzzFontEngine::fill_line_break_opportunities_utf8(text, opportunities);
    return status;
}

// Below 1x on either axis (and no larger on the other) the image is drawn
// from its resampled copy at the device size (Impl::downscaled_for); at or
// above 1x it is drawn as it is, so those pixels are unchanged.
void SkiaRaster::draw_image(ImageId image, Rect destination, double opacity) {
    const Impl::ImageMap::const_iterator found = (*impl_).images.find(image.value);
    SkCanvas* const canvas = (*impl_).canvas();
    if (canvas == nullptr || found == (*impl_).images.end() || !(opacity > 0.0) || !destination.finite()) return;
    const sk_sp<SkImage> reduced =
        (*impl_).downscaled_for(image.value, (*found).second, (*canvas).getTotalMatrix(), destination);
    const SkImage* const drawn = reduced ? reduced.get() : (*found).second.image.get();
    SkPaint paint;
    paint.setAlphaf(static_cast<float>(std::clamp(opacity, 0.0, 1.0)));
    paint.setAntiAlias(true);
    (*canvas).drawImageRect(drawn, to_sk_rect(destination), SkSamplingOptions(SkFilterMode::kLinear), &paint);
}

void SkiaRaster::begin_group(const PaintGroup& group) {
    Impl& impl = *impl_;
    if (impl.group_depth >= maximum_paint_group_depth) {
        // Refused by name: drawn without isolation, limited to its bounds.
        save();
        clip_rect(group.bounds);
        impl.refused_open = impl.refused_open + 1U;
        impl.group_refusals = impl.group_refusals + 1U;
        return;
    }
    Impl::GroupLevel& level = impl.groups[impl.group_depth];
    level.isolated = false;
    level.opacity = group.opacity;
    level.composite = group.composite;
    save();
    clip_rect(group.bounds);
    const bool visible = group.opacity > 0.0;
    if (!visible) clip_rect(Rect{});
    impl.group_depth = impl.group_depth + 1U;
    SkCanvas* const parent = impl.canvas();
    const bool isolate = group.opacity < 1.0 || group.isolated || group.composite != PaintGroupComposite::source_over;
    if (!visible || !isolate || parent == nullptr) return;
    // The group covers the recorded clip, which now includes its bounds, in
    // the parent's device pixels. A parent whose clips or pixels are unknown,
    // or an isolation surface that cannot be allocated, draws the group
    // directly, without its opacity or composite: counted as a refusal
    // (audit P9), as a group past the depth bound is.
    const Impl::TextClipState* const state = impl.current_text_clip(*parent);
    if (state == nullptr) {
        impl.group_refusals = impl.group_refusals + 1U;
        return;
    }
    const std::optional<SkPixmap> parent_view = (*parent).peekPixels();
    if (!parent_view) {
        impl.group_refusals = impl.group_refusals + 1U;
        return;
    }
    const SkPixmap& parent_pixels = *parent_view;
    SkIRect device = (*state).rect.roundOut();
    if (!device.intersect(SkIRect::MakeWH(parent_pixels.width(), parent_pixels.height()))) return;
    const int width = device.width();
    const int height = device.height();
    if (!level.surface || (*level.surface).width() < width || (*level.surface).height() < height) {
        const int grown_width = level.surface ? std::max(width, (*level.surface).width()) : width;
        const int grown_height = level.surface ? std::max(height, (*level.surface).height()) : height;
        const SkImageInfo info = SkImageInfo::Make(grown_width, grown_height, kBGRA_8888_SkColorType,
                                                   kPremul_SkAlphaType, SkColorSpace::MakeSRGB());
        sk_sp<SkSurface> grown = SkSurfaces::Raster(info);
        if (!grown) {
            impl.group_refusals = impl.group_refusals + 1U;
            return;
        }
        level.surface = std::move(grown);
    }
    SkCanvas& canvas = *(*level.surface).getCanvas();
    canvas.restoreAll();
    canvas.resetMatrix();
    canvas.save();
    canvas.clipRect(SkRect::MakeIWH(width, height), SkClipOp::kIntersect, false);
    canvas.clear(SK_ColorTRANSPARENT);
    SkMatrix matrix = (*parent).getTotalMatrix();
    matrix.postTranslate(static_cast<SkScalar>(-device.left()), static_cast<SkScalar>(-device.top()));
    canvas.setMatrix(matrix);
    // The group's own recorded clips: its whole surface region. The
    // parent's rounded clip and damage region apply when it composites.
    level.parent_text_clips = std::move(impl.text_clips);
    level.parent_text_clip_base = impl.text_clip_base;
    level.parent_save_floor = impl.save_floor;
    impl.text_clips.clear();
    Impl::TextClipState base{};
    base.rect = SkRect::MakeIWH(width, height);
    impl.text_clips.push_back(std::move(base));
    impl.text_clip_base = canvas.getSaveCount();
    impl.save_floor = canvas.getSaveCount();
    level.device = device;
    level.isolated = true;
}

void SkiaRaster::end_group() {
    Impl& impl = *impl_;
    if (impl.refused_open > 0U) {
        impl.refused_open = impl.refused_open - 1U;
        restore();
        return;
    }
    if (impl.group_depth == 0U) return;
    Impl::GroupLevel& level = impl.groups[impl.group_depth - 1U];
    if (!level.isolated) {
        impl.group_depth = impl.group_depth - 1U;
        restore();
        return;
    }
    SkCanvas& canvas = *(*level.surface).getCanvas();
    canvas.restoreAll();
    impl.text_clips = std::move(level.parent_text_clips);
    impl.text_clip_base = level.parent_text_clip_base;
    impl.save_floor = level.parent_save_floor;
    level.isolated = false;
    impl.group_depth = impl.group_depth - 1U;
    // Back on the parent: its clip as the group began, then the composite.
    restore();
    SkCanvas* const parent = impl.canvas();
    if (parent == nullptr) return;
    const std::optional<SkPixmap> peeked = (*level.surface).peekPixels();
    if (!peeked) return;
    const SkPixmap& pixmap = *peeked;
    const std::size_t width = static_cast<std::size_t>(level.device.width());
    const std::size_t height = static_cast<std::size_t>(level.device.height());
    const std::size_t length = (height - 1U) * pixmap.rowBytes() + width * 4U;
    const std::span<const std::uint8_t> bytes(static_cast<const std::uint8_t*>(pixmap.addr()), length);
    const detail::PartRasterSource source{.pixels = bytes, .width = static_cast<std::uint32_t>(width),
                                          .height = static_cast<std::uint32_t>(height),
                                          .row_bytes = pixmap.rowBytes(), .left = level.device.left(),
                                          .top = level.device.top(),
                                          .blend = detail::part_composite_blend(level.composite)};
    static_cast<void>(impl.composite_source(*parent, source, level.opacity));
}

std::uint64_t SkiaRaster::group_refusals() const noexcept {
    return (*impl_).group_refusals;
}

void SkiaRaster::draw_part_raster(const std::shared_ptr<const PartRaster>& raster, const Point origin,
                                  const double opacity) {
    SkCanvas* const canvas = (*impl_).canvas();
    if (canvas == nullptr || !raster) return;
    static_cast<void>((*impl_).composite_part(*canvas, *raster, origin, opacity));
}

void SkiaRaster::draw_part_raster_scaled(const std::shared_ptr<const PartRaster>& raster, const Rect destination,
                                         const double opacity) {
    SkCanvas* const canvas = (*impl_).canvas();
    if (canvas == nullptr || !raster) return;
    static_cast<void>((*impl_).composite_part_scaled(*canvas, *raster, destination, opacity));
}

void SkiaRaster::draw_surface_material(const Rect bounds, const SurfaceMaterial& authored) {
    SkCanvas* const canvas = (*impl_).canvas();
    if (canvas == nullptr) return;
    const SkMatrix matrix = (*canvas).getTotalMatrix();
    const bool uniform = matrix.isScaleTranslate() && matrix.getScaleX() == matrix.getScaleY();
    if (!uniform || detail::material_painting(authored) == detail::MaterialPainting::layers) {
        detail::paint_material_layers(*this, bounds, authored);
        return;
    }
    const std::optional<SurfaceMaterial> stripped = detail::material_as_painted(authored);
    const SurfaceMaterial& material = stripped ? *stripped : authored;
    const double outset = detail::material_raster_outset(material);
    const DeviceScale scale{static_cast<double>(matrix.getScaleX())};
    const std::shared_ptr<const PartRaster> raster =
        detail::bake_material_raster(material, Size{bounds.width, bounds.height}, scale, outset);
    const Point origin{bounds.x - outset, bounds.y - outset};
    if (raster && (*impl_).composite_part(*canvas, *raster, origin, 1.0)) return;
    detail::paint_material_layers(*this, bounds, authored);
}

void SkiaRaster::draw_baked_surface_material(const Rect bounds, const std::shared_ptr<const SurfaceMaterial>& material,
                                             const std::shared_ptr<const PartRaster>& raster, const double outset) {
    SkCanvas* const canvas = (*impl_).canvas();
    if (canvas == nullptr || !material) return;
    const Point origin{bounds.x - outset, bounds.y - outset};
    if (raster && (*impl_).composite_part(*canvas, *raster, origin, 1.0)) return;
    draw_surface_material(bounds, *material);
}

void SkiaRaster::draw_live_surface(const std::shared_ptr<LiveSurface> surface,
                                   const Rect destination, const double opacity) {
    if (!surface) return;
    const LiveSurfaceFrame frame = (*surface).acquire_latest();
    static_cast<void>(draw_live_surface_frame(frame, destination, opacity));
}

Rect SkiaRaster::opaque_live_surface_cover(const std::shared_ptr<LiveSurface>& surface,
                                           const Rect destination, const double opacity) {
    const Rect nothing{};
    SkCanvas* const canvas = (*impl_).canvas();
    if (!surface || canvas == nullptr || destination.empty() || !destination.finite() ||
        !(opacity >= 1.0) || fail_next_live_image_) {
        return nothing;
    }
    const LiveSurfaceFrame frame = (*surface).acquire_latest();
    if (!frame || !frame.opaque() || frame.width() == 0U || frame.height() == 0U ||
        frame.pixel_format() != LiveSurfacePixelFormat::bgra32_premultiplied_srgb ||
        frame.row_bytes() < static_cast<std::uint64_t>(frame.width()) * 4U || frame.pixels().empty()) {
        return nothing;
    }
    // Rectangle clips are hard-edged (clip_rect); a rounded clip blends its
    // edge pixels with what lies beneath. Untracked clips are refused too.
    const Impl::TextClipState* const clip = (*impl_).current_text_clip(*canvas);
    if (clip == nullptr || (*clip).rounded) return nothing;
    const SkMatrix matrix = (*canvas).getTotalMatrix();
    if (!matrix.isScaleTranslate() || !(matrix.getScaleX() > 0.0F) || !(matrix.getScaleY() > 0.0F)) {
        return nothing;
    }
    const double scale_x = matrix.getScaleX();
    const double scale_y = matrix.getScaleY();
    const double translate_x = matrix.getTranslateX();
    const double translate_y = matrix.getTranslateY();
    // Whole device pixels inside the destination: a hard-edged draw writes
    // every pixel whose centre lies inside, so at least these.
    const double left = std::ceil(destination.x * scale_x + translate_x);
    const double top = std::ceil(destination.y * scale_y + translate_y);
    const double right = std::floor((destination.x + destination.width) * scale_x + translate_x);
    const double bottom = std::floor((destination.y + destination.height) * scale_y + translate_y);
    if (!(right > left) || !(bottom > top)) return nothing;
    const Rect cover{(left - translate_x) / scale_x, (top - translate_y) / scale_y, (right - left) / scale_x,
                     (bottom - top) / scale_y};
    ++covers_promised_;
    return cover;
}

bool SkiaRaster::draw_live_surface_frame(const LiveSurfaceFrame& frame,
                                         const Rect destination, const double opacity) {
    SkCanvas* const canvas = (*impl_).canvas();
    if (canvas == nullptr || destination.empty() ||
        !destination.finite() || !std::isfinite(opacity) || opacity <= 0.0) {
        return false;
    }
    if (!frame || frame.width() == 0U || frame.height() == 0U ||
        frame.row_bytes() < static_cast<std::uint64_t>(frame.width()) * 4U ||
        frame.pixels().empty()) {
        return false;
    }
    const bool copy = frame.opaque() && opacity >= 1.0;
    const SkAlphaType alpha = copy ? kOpaque_SkAlphaType : kPremul_SkAlphaType;
    if (frame.pixel_format() != LiveSurfacePixelFormat::bgra32_premultiplied_srgb) return false;
    if (fail_next_live_image_) {
        fail_next_live_image_ = false;
        return false;
    }
    const SkImageInfo info = SkImageInfo::Make(
        static_cast<int>(frame.width()), static_cast<int>(frame.height()),
        kBGRA_8888_SkColorType, alpha,
        SkColorSpace::MakeSRGB());
    SkIRect device{};
    const bool pixel_exact = live_surface_device_rect(*canvas, destination, frame, device);
    if (copy && pixel_exact && (*canvas).isClipRect()) {
        // writePixels ignores both matrix and clip. The raster's isClipRect
        // admits only full-coverage rectangular clips; complex/AA clips must
        // go through drawImageRect so retained overlays and edges survive.
        SkIRect clipped = device;
        if (!clipped.intersect((*canvas).getDeviceClipBounds())) return true;
        const std::size_t source_y = static_cast<std::size_t>(clipped.top() - device.top());
        const std::size_t source_x = static_cast<std::size_t>(clipped.left() - device.left());
        const std::size_t stride = static_cast<std::size_t>(frame.row_bytes());
        const std::size_t offset = source_y * stride + source_x * 4U;
        // Alpha is promised 255, so it is also valid premultiplied storage.
        // Match the raster alpha type to retain Skia's byte-copy path.
        const SkImageInfo clipped_info = info.makeWH(clipped.width(), clipped.height());
        const SkImageInfo copy_info = clipped_info.makeAlphaType(kPremul_SkAlphaType);
        const bool written = (*canvas).writePixels(copy_info, frame.pixels().data() + offset,
                                                   stride, clipped.left(), clipped.top());
        return written;
    }
    // The frame's pixels are lent to the image for this draw only. borrowed is
    // declared first so the image is released before it; it stops the program if
    // anything (a cache, say) still holds the pixels when the draw returns.
    const SkBorrowedData borrowed(frame.pixels().data(), frame.pixels().size());
    sk_sp<SkImage> image = SkImages::RasterFromData(
        info, borrowed.data(), static_cast<size_t>(frame.row_bytes()));
    if (!image) return false;

    SkPaint paint{};
    if (copy) paint.setBlendMode(SkBlendMode::kSrc);
    paint.setAlphaf(static_cast<float>(std::clamp(opacity, 0.0, 1.0)));
    paint.setAntiAlias(false);
    (*canvas).drawImageRect(
        image.get(),
        SkRect::MakeWH(static_cast<SkScalar>(frame.width()),
                       static_cast<SkScalar>(frame.height())),
        to_sk_rect(destination),
        SkSamplingOptions(copy && pixel_exact ? SkFilterMode::kNearest : SkFilterMode::kLinear), &paint,
        SkCanvas::kStrict_SrcRectConstraint);
    return true;
}

void SkiaRaster::draw_image_region(ImageId image, Rect source,
                                   Rect destination, double opacity) {
    draw_image_region_sampled(image, source, destination,
                              ImageSampling::linear, opacity);
}

void SkiaRaster::draw_image_region_sampled(
    ImageId image, Rect source, Rect destination, ImageSampling sampling,
    double opacity) {
    const Impl::ImageMap::const_iterator found = (*impl_).images.find(image.value);
    SkCanvas* canvas = (*impl_).canvas();
    if (canvas == nullptr || found == (*impl_).images.end() || source.empty() ||
        destination.empty() || !source.finite() || !destination.finite() ||
        !std::isfinite(opacity) || opacity <= 0.0) {
        return;
    }
    const Rect image_bounds{
        0.0, 0.0,
        static_cast<double>((*(*found).second.image).width()),
        static_cast<double>((*(*found).second.image).height())};
    if (!image_bounds.contains(source)) return;
    SkPaint paint;
    paint.setAlphaf(static_cast<float>(std::clamp(opacity, 0.0, 1.0)));
    // Sampling filters pixels; rectangle coverage must stay hard-edged.
    // Coverage AA attenuates a tile edge even through a disjoint hard clip
    // when its one-pixel sampling gutter is subpixel after minification.
    paint.setAntiAlias(false);
    (*canvas).drawImageRect((*found).second.image.get(), to_sk_rect(source),
                          to_sk_rect(destination),
                          SkSamplingOptions(sampling == ImageSampling::nearest
                              ? SkFilterMode::kNearest : SkFilterMode::kLinear),
                          &paint,
                          SkCanvas::kStrict_SrcRectConstraint);
}

// fill_image_pattern where a tile spans whole device pixels and starts on
// one, under a scale and translation: the tile is resampled once to device
// resolution (linear, wrapping, as the direct draw samples it) and repeated
// without further sampling. Each device pixel then takes the value the
// direct draw would compute there, rounded to a byte before blending rather
// than after (at most one level apart where the pattern is translucent;
// equal where it is opaque, up to the float rounding of sample positions).
// An opaque tile at full opacity under a hard rectangular clip is then
// copied row by row (copy_opaque_tile) rather than shaded. Resampling a
// 128-pixel felt at device scale 2 for every pixel of a 2560 x 1294 canvas
// cost some 230 ms a frame on the Windows test machine (benchmarks/
// canvas_pan_benchmark.cpp); the kept tile costs one 256 x 256 resample.
// False when the conditions do not hold; the caller draws directly.
namespace {

// Whether every pixel of a premultiplied BGRA pixmap has alpha 255.
bool every_pixel_opaque(const SkPixmap& pixels) {
    for (int y = 0; y < pixels.height(); ++y) {
        const std::uint8_t* row = static_cast<const std::uint8_t*>(pixels.addr(0, y));
        for (int x = 0; x < pixels.width(); ++x) {
            if (row[static_cast<std::size_t>(x) * 4U + 3U] != 255U) return false;
        }
    }
    return true;
}

// Repeats an opaque device tile, whose pixel (0, 0) lies at device
// (origin_x, origin_y), over `target` (device pixels, inside `pixels`) by
// copying rows: what a source-over draw of an opaque tile without sampling
// writes, byte for byte, without a shader per pixel.
void copy_opaque_tile(const SkPixmap& pixels, const SkPixmap& tile, const long long origin_x,
                      const long long origin_y, const SkIRect target) {
    const long long tile_width = tile.width();
    const long long tile_height = tile.height();
    const long long first_column = ((target.left() - origin_x) % tile_width + tile_width) % tile_width;
    for (int y = target.top(); y < target.bottom(); ++y) {
        const long long tile_row = ((y - origin_y) % tile_height + tile_height) % tile_height;
        const std::byte* source = static_cast<const std::byte*>(tile.addr(0, static_cast<int>(tile_row)));
        std::byte* written = static_cast<std::byte*>(pixels.writable_addr(target.left(), y));
        long long remaining = target.width();
        long long column = first_column;
        while (remaining > 0) {
            const long long run = std::min(remaining, tile_width - column);
            std::memcpy(written, source + column * 4, static_cast<std::size_t>(run) * 4U);
            written += run * 4;
            remaining -= run;
            column = 0;
        }
    }
}

bool fill_device_pattern(SkCanvas& canvas, const SkImage& image, sk_sp<SkImage>& device_tile,
                         bool& device_tile_opaque, const Rect destination, const Size logical_tile_size,
                         const double opacity) {
    const SkMatrix matrix = canvas.getTotalMatrix();
    if (!matrix.isScaleTranslate() || !(matrix.getScaleX() > 0.0F) || !(matrix.getScaleY() > 0.0F)) return false;
    const double tile_width = logical_tile_size.width * matrix.getScaleX();
    const double tile_height = logical_tile_size.height * matrix.getScaleY();
    const double origin_x = destination.x * matrix.getScaleX() + matrix.getTranslateX();
    const double origin_y = destination.y * matrix.getScaleY() + matrix.getTranslateY();
    // A kept tile larger than this (4 MB) is not worth its memory.
    constexpr double largest_device_tile = 1024.0;
    if (tile_width != std::round(tile_width) || tile_height != std::round(tile_height) ||
        origin_x != std::round(origin_x) || origin_y != std::round(origin_y) || !(tile_width >= 1.0) ||
        !(tile_height >= 1.0) || !(tile_width <= largest_device_tile) || !(tile_height <= largest_device_tile) ||
        !(std::abs(origin_x) <= 1.0e9) || !(std::abs(origin_y) <= 1.0e9)) {
        return false;
    }
    const int width = static_cast<int>(tile_width);
    const int height = static_cast<int>(tile_height);
    const bool current = device_tile && (*device_tile).width() == width && (*device_tile).height() == height;
    if (!current) {
        const SkImageInfo info = SkImageInfo::Make(width, height, kBGRA_8888_SkColorType, kPremul_SkAlphaType,
                                                   SkColorSpace::MakeSRGB());
        sk_sp<SkSurface> tile = SkSurfaces::Raster(info);
        if (!tile) return false;
        const SkMatrix resample = SkMatrix::Scale(
            static_cast<SkScalar>(tile_width / image.width()),
            static_cast<SkScalar>(tile_height / image.height()));
        SkPaint fill;
        fill.setAntiAlias(false);
        fill.setBlendMode(SkBlendMode::kSrc);
        fill.setShader(image.makeShader(SkTileMode::kRepeat, SkTileMode::kRepeat,
                                        SkSamplingOptions(SkFilterMode::kLinear), resample));
        (*(*tile).getCanvas()).drawRect(SkRect::MakeIWH(width, height), fill);
        device_tile = (*tile).makeImageSnapshot();
        if (!device_tile) return false;
        const std::optional<SkPixmap> kept = (*device_tile).peekPixels();
        device_tile_opaque = kept && every_pixel_opaque(*kept);
    }
    const SkRect device = matrix.mapRect(to_sk_rect(destination));
    // An opaque tile at full opacity under a hard rectangular clip is copied
    // (the non-antialiased rectangle covers the pixels its rounded edges
    // enclose, as Skia rounds them).
    if (device_tile_opaque && opacity >= 1.0 && canvas.isClipRect()) {
        const std::optional<SkPixmap> peeked = canvas.peekPixels();
        const std::optional<SkPixmap> tile_pixels = (*device_tile).peekPixels();
        if (peeked && tile_pixels && (*peeked).colorType() == kBGRA_8888_SkColorType) {
            SkIRect target = device.round();
            const bool inside = target.intersect(canvas.getDeviceClipBounds()) &&
                                target.intersect(SkIRect::MakeWH((*peeked).width(), (*peeked).height()));
            if (inside) {
                copy_opaque_tile(*peeked, *tile_pixels, static_cast<long long>(origin_x),
                                 static_cast<long long>(origin_y), target);
            }
            return true;
        }
    }
    const SkMatrix placement = SkMatrix::Translate(static_cast<SkScalar>(origin_x), static_cast<SkScalar>(origin_y));
    SkPaint paint;
    paint.setAlphaf(static_cast<float>(std::clamp(opacity, 0.0, 1.0)));
    paint.setAntiAlias(false);
    paint.setShader((*device_tile).makeShader(SkTileMode::kRepeat, SkTileMode::kRepeat,
                                              SkSamplingOptions(SkFilterMode::kNearest), placement));
    canvas.save();
    canvas.resetMatrix();
    canvas.drawRect(device, paint);
    canvas.restore();
    return true;
}

} // namespace

void SkiaRaster::fill_image_pattern(
    ImageId image, Size source_pixel_size, Rect destination,
    Size logical_tile_size, ImagePatternWrap wrap, double opacity) {
    const Impl::ImageMap::iterator found = (*impl_).images.find(image.value);
    SkCanvas* canvas = (*impl_).canvas();
    if (canvas == nullptr || found == (*impl_).images.end() ||
        wrap != ImagePatternWrap::tile || destination.empty() ||
        !destination.finite() || !std::isfinite(source_pixel_size.width) ||
        !std::isfinite(source_pixel_size.height) ||
        !std::isfinite(logical_tile_size.width) ||
        !std::isfinite(logical_tile_size.height) ||
        source_pixel_size.width != (*(*found).second.image).width() ||
        source_pixel_size.height != (*(*found).second.image).height() ||
        logical_tile_size.width <= 0.0 || logical_tile_size.height <= 0.0 ||
        !std::isfinite(opacity) || opacity <= 0.0) {
        return;
    }
    if (!direct_patterns_ && fill_device_pattern(*canvas, *(*found).second.image, (*found).second.device_tile,
                                                 (*found).second.device_tile_opaque, destination, logical_tile_size,
                                                 opacity)) {
        return;
    }
    SkMatrix local;
    local.setScaleTranslate(
        static_cast<SkScalar>(logical_tile_size.width /
                              source_pixel_size.width),
        static_cast<SkScalar>(logical_tile_size.height /
                              source_pixel_size.height),
        static_cast<SkScalar>(destination.x),
        static_cast<SkScalar>(destination.y));
    SkPaint paint;
    paint.setAlphaf(static_cast<float>(std::clamp(opacity, 0.0, 1.0)));
    paint.setAntiAlias(false);
    paint.setShader((*(*found).second.image).makeShader(
        SkTileMode::kRepeat, SkTileMode::kRepeat,
        SkSamplingOptions(SkFilterMode::kLinear), local));
    (*canvas).drawRect(to_sk_rect(destination), paint);
}

namespace {

SkPoint to_sk_point(Point point) noexcept {
    return SkPoint::Make(static_cast<SkScalar>(point.x), static_cast<SkScalar>(point.y));
}

bool finite_mark_point(Point point) noexcept {
    return std::isfinite(point.x) && std::isfinite(point.y);
}

SkPaint::Join to_sk_join(StrokeJoin join) noexcept {
    switch (join) {
    case StrokeJoin::miter: return SkPaint::kMiter_Join;
    case StrokeJoin::round: return SkPaint::kRound_Join;
    case StrokeJoin::bevel: return SkPaint::kBevel_Join;
    }
    return SkPaint::kMiter_Join;
}

SkPaint::Cap to_sk_cap(StrokeCap cap) noexcept {
    switch (cap) {
    case StrokeCap::butt: return SkPaint::kButt_Cap;
    case StrokeCap::round: return SkPaint::kRound_Cap;
    case StrokeCap::square: return SkPaint::kSquare_Cap;
    }
    return SkPaint::kButt_Cap;
}

// Configures `paint` to stroke as `style` says (validated). False when
// stx_skia refuses the dash pattern; the call then draws nothing rather than
// a solid line.
bool configure_mark_stroke(SkPaint& paint, const StrokeStyle& style) {
    paint = make_paint(style.color);
    paint.setStyle(SkPaint::kStroke_Style);
    paint.setStrokeWidth(static_cast<SkScalar>(style.width));
    paint.setStrokeJoin(to_sk_join(style.join));
    paint.setStrokeMiter(static_cast<SkScalar>(style.miter_limit));
    paint.setStrokeCap(to_sk_cap(style.cap));
    if (style.dash_count == 0U) return true;
    std::array<SkScalar, maximum_dash_intervals> intervals{};
    for (std::size_t index = 0U; index < style.dash_count; ++index) {
        intervals[index] = static_cast<SkScalar>(style.dashes[index]);
    }
    SkDashPathEffectResult made = SkDashPathEffect::Make(
        SkSpan<const SkScalar>(intervals.data(), style.dash_count), static_cast<SkScalar>(style.dash_phase));
    if (made.rejection != SkDashRejection::kNone) return false;
    paint.setPathEffect(std::move(made.effect));
    return true;
}

// The marker shape at the origin, built once per draw_markers call.
SkPath marker_path(const MarkerStyle& style) {
    SkPathBuilder builder;
    const double half = style.size * 0.5;
    if (style.shape == MarkerShape::circle) {
        builder.addOval(SkRect::MakeLTRB(static_cast<SkScalar>(-half), static_cast<SkScalar>(-half),
                                         static_cast<SkScalar>(half), static_cast<SkScalar>(half)));
        return builder.detach();
    }
    std::array<Point, maximum_marker_vertices> outline{};
    const std::size_t vertex_count = marker_outline(style.shape, Point{}, style.size, outline);
    if (vertex_count != 0U) {
        builder.moveTo(to_sk_point(outline[0]));
        for (std::size_t index = 1U; index < vertex_count; ++index) builder.lineTo(to_sk_point(outline[index]));
        builder.close();
        return builder.detach();
    }
    std::array<PaintSegment, maximum_marker_lines> lines{};
    const std::size_t line_count = marker_lines(style.shape, Point{}, style.size, lines);
    for (std::size_t index = 0U; index < line_count; ++index) {
        builder.moveTo(to_sk_point(lines[index].from));
        builder.lineTo(to_sk_point(lines[index].to));
    }
    return builder.detach();
}

bool marker_is_stroked(MarkerShape shape) noexcept {
    return shape == MarkerShape::cross || shape == MarkerShape::plus || shape == MarkerShape::asterisk;
}

// The point count a path builder reserves for a batch, held within int.
int reserved_points(std::size_t count) noexcept {
    constexpr std::size_t limit = std::size_t{1} << 24U;
    const std::size_t bounded = std::min(count, limit);
    return static_cast<int>(bounded);
}

} // namespace

void SkiaRaster::stroke_polyline(std::span<const Point> points, const StrokeStyle& style) {
    SkCanvas* canvas = (*impl_).canvas();
    if (canvas == nullptr || points.size() < 2U || !valid_stroke_style(style)) return;
    for (const Point point : points) {
        if (std::isinf(point.x) || std::isinf(point.y)) return;
    }
    SkPaint paint;
    if (!configure_mark_stroke(paint, style)) return;
    // Each run of finite points is one contour; a NaN starts the next.
    SkPathBuilder builder;
    builder.incReserve(reserved_points(points.size()));
    bool in_run = false;
    for (const Point point : points) {
        if (!finite_mark_point(point)) {
            in_run = false;
            continue;
        }
        if (in_run) {
            builder.lineTo(to_sk_point(point));
        } else {
            builder.moveTo(to_sk_point(point));
        }
        in_run = true;
    }
    (*canvas).drawPath(builder.detach(), paint);
}

void SkiaRaster::fill_polygon(std::span<const Point> points, Color color, PolygonFillRule rule) {
    SkCanvas* canvas = (*impl_).canvas();
    const bool known_rule = rule == PolygonFillRule::nonzero || rule == PolygonFillRule::even_odd;
    if (canvas == nullptr || !known_rule || points.size() < 3U) return;
    for (const Point point : points) {
        if (!finite_mark_point(point)) return;
    }
    SkPathBuilder builder(rule == PolygonFillRule::even_odd ? SkPathFillType::kEvenOdd : SkPathFillType::kWinding);
    builder.incReserve(reserved_points(points.size()));
    builder.moveTo(to_sk_point(points[0]));
    for (std::size_t index = 1U; index < points.size(); ++index) builder.lineTo(to_sk_point(points[index]));
    builder.close();
    (*canvas).drawPath(builder.detach(), make_paint(color));
}

void SkiaRaster::fill_band(std::span<const Point> upper, std::span<const Point> lower, Color color) {
    SkCanvas* canvas = (*impl_).canvas();
    if (canvas == nullptr || upper.size() < 2U || upper.size() != lower.size()) return;
    for (std::size_t index = 0U; index < upper.size(); ++index) {
        if (!finite_mark_point(upper[index]) || !finite_mark_point(lower[index])) return;
    }
    SkPathBuilder builder(SkPathFillType::kWinding);
    builder.incReserve(reserved_points(upper.size() * 2U));
    builder.moveTo(to_sk_point(upper[0]));
    for (std::size_t index = 1U; index < upper.size(); ++index) builder.lineTo(to_sk_point(upper[index]));
    for (std::size_t index = lower.size(); index > 0U; --index) builder.lineTo(to_sk_point(lower[index - 1U]));
    builder.close();
    (*canvas).drawPath(builder.detach(), make_paint(color));
}

void SkiaRaster::stroke_segments(std::span<const PaintSegment> segments, const StrokeStyle& style) {
    SkCanvas* canvas = (*impl_).canvas();
    if (canvas == nullptr || segments.empty() || !valid_stroke_style(style)) return;
    SkPaint paint;
    if (!configure_mark_stroke(paint, style)) return;
    // One contour per segment: caps end each, dashes start again on each.
    SkPathBuilder builder;
    builder.incReserve(reserved_points(segments.size() * 2U));
    for (const PaintSegment segment : segments) {
        if (!finite_mark_point(segment.from) || !finite_mark_point(segment.to)) continue;
        builder.moveTo(to_sk_point(segment.from));
        builder.lineTo(to_sk_point(segment.to));
    }
    (*canvas).drawPath(builder.detach(), paint);
}

void SkiaRaster::draw_markers(std::span<const Point> centers, const MarkerStyle& style) {
    SkCanvas* canvas = (*impl_).canvas();
    if (canvas == nullptr || centers.empty() || !valid_marker_style(style)) return;
    const bool stroked = marker_is_stroked(style.shape);
    const bool draw_fill = !stroked && style.fill.alpha != 0U;
    const bool draw_edge = style.edge.alpha != 0U && style.edge_width > 0.0;
    if (!draw_fill && !draw_edge) return;
    const SkPath shape = marker_path(style);
    SkPaint fill = make_paint(style.fill);
    SkPaint edge = make_paint(style.edge);
    edge.setStyle(SkPaint::kStroke_Style);
    edge.setStrokeWidth(static_cast<SkScalar>(style.edge_width));
    // A transparent paint draws nothing visible; each marker is its fill
    // then its edge, so a later marker covers an earlier one whole.
    if (!draw_fill) fill.setColor(SK_ColorTRANSPARENT);
    if (!draw_edge) edge.setColor(SK_ColorTRANSPARENT);
    for (const Point center : centers) {
        if (!finite_mark_point(center)) continue;
        (*canvas).save();
        (*canvas).translate(static_cast<SkScalar>(center.x), static_cast<SkScalar>(center.y));
        (*canvas).drawPath(shape, fill);
        (*canvas).drawPath(shape, edge);
        (*canvas).restore();
    }
}

void SkiaRaster::fill_conic_gradient(Rect rect, Point center, double start_degrees,
                                     std::span<const GradientStop> stops) {
    SkCanvas* canvas = (*impl_).canvas();
    if (canvas == nullptr || rect.empty() || !rect.finite() || !finite_mark_point(center) ||
        !std::isfinite(start_degrees) || !valid_gradient_stops(stops)) {
        return;
    }
    // One full turn from the start angle; the sweep runs clockwise from +x
    // in y-down coordinates, as the painter contract does.
    const double start = std::fmod(start_degrees, 360.0);
    const SkGradientData data = to_sk_gradient(stops);
    SkGradientShaderResult made = SkShaders::SweepGradient(to_sk_point(center), static_cast<SkScalar>(start),
                                                           static_cast<SkScalar>(start + 360.0),
                                                           make_sk_gradient(data));
    if (made.rejection != SkGradientRejection::kNone) return;
    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setShader(std::move(made.shader));
    const SkRect box = snapped_box((*impl_).snap_transform_of(*canvas), rect, paint);
    (*canvas).drawRect(box, paint);
}

void SkiaRaster::fill_rounded_rect_corners(Rect rect, PaintCornerRadii radii, Color color) {
    SkCanvas* canvas = (*impl_).canvas();
    if (canvas == nullptr || rect.empty()) return;
    // As fill_rounded_rect: the bounds snap in device space and the corners
    // keep their antialiasing.
    const SnapTransform transform = (*impl_).snap_transform_of(*canvas);
    SkRect device{};
    if (!snapped_device_rect(transform, rect, device)) {
        (*canvas).drawRRect(to_sk_rrect_corners(rect, radii), make_paint(color));
        return;
    }
    const double limit = std::max(0.0, std::min(rect.width, rect.height) * 0.5);
    const std::array<double, 4> written{radii.top_left, radii.top_right, radii.bottom_right, radii.bottom_left};
    std::array<SkVector, 4> corners{};
    for (std::size_t index = 0U; index < corners.size(); ++index) {
        corners[index] = device_radii(transform, std::clamp(written[index], 0.0, limit), device);
    }
    SkRRect shape;
    shape.setRectRadii(device, corners.data());
    (*canvas).save();
    (*canvas).resetMatrix();
    (*canvas).drawRRect(shape, make_paint(color));
    (*canvas).restore();
}

void SkiaRaster::stroke_rounded_rect_corners(Rect rect, PaintCornerRadii radii, Color color, double width) {
    SkCanvas* canvas = (*impl_).canvas();
    if (canvas == nullptr || rect.empty() || !std::isfinite(width) || width <= 0.0) return;
    // The ring between the snapped outer and inner bounds, as
    // stroke_rounded_rect draws it; a square corner stays square on both
    // sides, a rounded one grows by half the width outside and shrinks by
    // it inside.
    const double half = width * 0.5;
    const SnapTransform transform = (*impl_).snap_transform_of(*canvas);
    SkRect outer{};
    SkRect inner{};
    const bool snapped = snapped_device_rect(transform, outset_rect(rect, half), outer) &&
                         snapped_device_rect(transform, outset_rect(rect, -half), inner);
    SkPaint paint = make_paint(color);
    if (!snapped) {
        paint.setStyle(SkPaint::kStroke_Style);
        paint.setStrokeWidth(static_cast<SkScalar>(width));
        (*canvas).drawRRect(to_sk_rrect_corners(rect, radii), paint);
        return;
    }
    const double limit = std::max(0.0, std::min(rect.width, rect.height) * 0.5);
    const std::array<double, 4> written{radii.top_left, radii.top_right, radii.bottom_right, radii.bottom_left};
    std::array<SkVector, 4> outer_corners{};
    std::array<SkVector, 4> inner_corners{};
    for (std::size_t index = 0U; index < outer_corners.size(); ++index) {
        const double bounded = std::clamp(written[index], 0.0, limit);
        const double outside = bounded > 0.0 ? bounded + half : 0.0;
        outer_corners[index] = device_radii(transform, outside, outer);
        inner_corners[index] = device_radii(transform, std::max(0.0, bounded - half), inner);
    }
    SkRRect outer_shape;
    outer_shape.setRectRadii(outer, outer_corners.data());
    (*canvas).save();
    (*canvas).resetMatrix();
    if (inner.isEmpty()) {
        (*canvas).drawRRect(outer_shape, paint);
    } else {
        SkRRect inner_shape;
        inner_shape.setRectRadii(inner, inner_corners.data());
        (*canvas).drawDRRect(outer_shape, inner_shape, paint);
    }
    (*canvas).restore();
}

void shutdown_skia_library() {
    SkGraphics::Shutdown();
}

} // namespace gui_forms::render
