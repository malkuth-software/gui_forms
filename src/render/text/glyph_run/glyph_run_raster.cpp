#include "glyph_run_raster.hpp"

#include "../../../core/resources/fonts/program_typefaces.hpp"
#include "../../../core/types/painter/text_turn.hpp"
#include "gui_forms/typography/app_fonts/app_fonts.hpp"

#include <stxlib/result.hpp>
#include <stxlib/types.hpp>
#include <stxlib/view.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace gui_forms::render::text {
namespace {

// Coverage dimensions above this are reported as oversized rather than allocated.
constexpr std::int64_t maximum_extent = glyph_run_maximum_extent;
// STXlib's largest size, 2^24 pixels to the em, in 26.6.
constexpr double maximum_size_26_6 = 1073741824.0;
// STXlib's largest thickening, in 26.6 (DrawStyle::thicken).
constexpr long long maximum_thicken = 1024;

std::int64_t floor_div_64(const std::int64_t value) noexcept {
    std::int64_t quotient = value / 64;
    if (value % 64 != 0 && value < 0) --quotient;
    return quotient;
}

// `value` limited to the 32-bit range STXlib's run coordinates take.
std::int32_t clamp_to_i32(const std::int64_t value) noexcept {
    const std::int64_t low = std::numeric_limits<std::int32_t>::min();
    const std::int64_t high = std::numeric_limits<std::int32_t>::max();
    const std::int64_t clamped = std::clamp(value, low, high);
    return static_cast<std::int32_t>(clamped);
}

// A device pixel box, [left, right) x [top, bottom), relative to the origin pixel.
struct PixelBox final {
    std::int64_t left{};
    std::int64_t top{};
    std::int64_t right{};
    std::int64_t bottom{};
};

// One run's coverage, copied into the rasterizer's piece storage until the
// runs are composited: its bytes at `offset`, its box relative to the
// origin pixel.
struct CoveragePiece final {
    std::int64_t left{};
    std::int64_t top{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::size_t offset{};
};

} // namespace

GlyphRunOrigin glyph_run_origin(const double device_x, const double device_y) noexcept {
    GlyphRunOrigin origin{};
    const std::int64_t fixed_x = std::llround(device_x * 64.0);
    origin.pixel_x = floor_div_64(fixed_x);
    origin.phase_x = static_cast<std::int32_t>(fixed_x - origin.pixel_x * 64);
    origin.pixel_y = std::llround(device_y);
    return origin;
}

namespace {

// How a run's glyphs reach device pixels. Device position of glyph (x, y),
// in 26.6 relative to the origin pixel, is
// offset + llround((linear * (x, y)) * 64).
struct RunPlacement final {
    std::int64_t offset_x{};
    std::int64_t offset_y{};
    GlyphRunTransform linear{};
    // What STXlib draws each glyph with: its size (26.6), fit, thickening,
    // matrix (Fit::none only) and window.
    stxlib::glyphs::DrawStyle style{};
};

} // namespace

class GlyphRunRasterizer::Impl final {
public:
    class Runner;
    explicit Impl(const HarfBuzzFontEngine& engine) : engine_(engine) {}
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    // The executor's face for the engine's face `id`, opened on first use;
    // null when the engine has no source for it, or when an application
    // family's face will not open (the family is quarantined then). A
    // bundled face that will not open throws std::runtime_error.
    detail::ExecutorFace* face(const FontFaceId id) {
        for (OpenFace& open : faces_) {
            if (open.id == id) return open.face.get();
        }
        const std::optional<FontFaceSource> source = engine_.face_source(id);
        if (!source) return nullptr;
        // The registry's typeface of the face (a bundled or application face's, registered
        // with the bytes' owner), else the one the executors drawing these bytes share.
        detail::TypefaceBorrow typeface =
            detail::program_typeface((*source).bytes, (*source).owner, (*source).face_index);
        std::unique_ptr<detail::ExecutorFace> opened = std::make_unique<detail::ExecutorFace>();
        detail::FaceByteLimits limits{};
        limits.mask_width = static_cast<std::uint32_t>(maximum_extent);
        limits.mask_height = static_cast<std::uint32_t>(maximum_extent);
        const bool usable = (*opened).open(std::move(typeface), limits);
        if (!usable) {
            // An application family's face that will not open (refused, or its family unloaded
            // and its typeface retired) draws nothing from now on: its glyphs count as missing.
            // A bundled face's failure is the renderer's.
            if (!app_font_family_number((*source).family)) throw std::runtime_error("Glyph run face open failed");
            quarantine_app_font_family((*source).family, AppFontQuarantine::rasterising_failed);
            return nullptr;
        }
        OpenFace record{};
        record.id = id;
        record.family = (*source).family;
        record.face = std::move(opened);
        faces_.push_back(std::move(record));
        detail::ExecutorFace* const result = faces_.back().face.get();
        return result;
    }

    // The family of the face `id` face() opened, or zero.
    FontFamilyId face_family(const FontFaceId id) const noexcept {
        for (const OpenFace& open : faces_) {
            if (open.id == id) return open.family;
        }
        return FontFamilyId{};
    }

private:
    struct OpenFace final {
        FontFaceId id{};
        FontFamilyId family{};
        std::unique_ptr<detail::ExecutorFace> face{};
    };

    const HarfBuzzFontEngine& engine_;
    std::vector<OpenFace> faces_;
    // Working storage kept from run to run (one executor), so a run whose
    // glyphs and size were met before allocates nothing of its own: the
    // placed glyphs, STXlib's mask of one font run, and the font runs'
    // masks until they are composited.
    std::vector<stxlib::glyphs::RunGlyph> run_glyphs_;
    stxlib::glyphs::RunCoverage run_coverage_{};
    std::vector<CoveragePiece> pieces_;
    std::vector<std::uint8_t> piece_pixels_;
};

GlyphRunRasterizer::GlyphRunRasterizer(const HarfBuzzFontEngine& engine) : impl_(std::make_unique<Impl>(engine)) {}

GlyphRunRasterizer::~GlyphRunRasterizer() = default;

namespace {

// Fake-bold thickening for a device pixel size, in 26.6: 1/24 of the size at
// 9 px falling linearly to 1/32 at 36 px and beyond, no more than STXlib's
// largest thickening. The amount is GUI.Forms' choice; how an outline is
// thickened is STXlib's integer rule (DrawStyle::thicken).
std::int32_t thicken_strength(const double device_size) noexcept {
    const double clamped = std::clamp(device_size, 9.0, 36.0);
    const double ratio = 1.0 / 24.0 + (clamped - 9.0) / 27.0 * (1.0 / 32.0 - 1.0 / 24.0);
    const double fixed = device_size * ratio * 64.0;
    if (!std::isfinite(fixed) || fixed >= static_cast<double>(maximum_thicken)) {
        return static_cast<std::int32_t>(maximum_thicken);
    }
    const long long strength = std::max(0LL, std::llround(fixed));
    return static_cast<std::int32_t>(strength);
}

// A 16.16 matrix entry; nullopt beyond what 32 bits hold.
std::optional<std::int32_t> fixed_16_16(const double value) noexcept {
    std::optional<std::int32_t> result{};
    if (!std::isfinite(value) || std::abs(value) >= 32768.0) return result;
    result = static_cast<std::int32_t>(std::llround(value * 65536.0));
    return result;
}

// The run window as STXlib takes it: whole pixels from the origin pixel,
// limited to 32 bits.
void set_window(stxlib::glyphs::DrawStyle& style, const std::optional<GlyphRunWindow>& window) noexcept {
    style.windowed = window.has_value();
    if (!window.has_value()) return;
    style.window_left = clamp_to_i32((*window).left);
    style.window_top = clamp_to_i32((*window).top);
    style.window_right = clamp_to_i32((*window).right);
    style.window_bottom = clamp_to_i32((*window).bottom);
}

// `matrix` (the outline's y-up space) after the oblique shear x' = x + 0.25 y:
// the product matrix * shear, so the shear is applied first.
GlyphRunTransform sheared(const GlyphRunTransform& matrix) noexcept {
    GlyphRunTransform product{};
    product.xx = matrix.xx;
    product.xy = matrix.xx * 0.25 + matrix.xy;
    product.yx = matrix.yx;
    product.yy = matrix.yx * 0.25 + matrix.yy;
    return product;
}

// Sets the style's matrix (y-up outline space) from `matrix`;
// invalid_argument for an entry beyond a 16.16 matrix.
void set_matrix(stxlib::glyphs::DrawStyle& style, const GlyphRunTransform& matrix) {
    const std::optional<std::int32_t> xx = fixed_16_16(matrix.xx);
    const std::optional<std::int32_t> xy = fixed_16_16(matrix.xy);
    const std::optional<std::int32_t> yx = fixed_16_16(matrix.yx);
    const std::optional<std::int32_t> yy = fixed_16_16(matrix.yy);
    if (!xx || !xy || !yx || !yy) throw std::invalid_argument("Glyph run transform beyond a 16.16 matrix");
    style.transformed = true;
    style.xx = *xx;
    style.xy = *xy;
    style.yx = *yx;
    style.yy = *yy;
}

// The placement of a run drawn as STXlib's turned text: turned by `turn`
// (16.16, y up, a turn and nothing else) about its origin at (device_x,
// device_y), at `scale` device pixels per logical unit. The scale goes into
// the size and the style is fitted (Fit::light), so STXlib draws a quarter
// turn by quarters and other turns by the mask's surface (or, above its
// lattice size, by the fitted outline turned). The glyphs' places are given
// as laid out upright, scaled, and STXlib turns them (turn_places); the
// origin's part of a pixel, which STXlib would turn with them, is given
// turned back by the inverse turn (the transpose), so that it lands where it
// is, within a 64th of a pixel. invalid_argument for a size beyond 26.6.
RunPlacement turned_placement(const stxlib::glyphs::TextMatrix& turn, const double logical_size, const double scale,
                              const double device_x, const double device_y, const GlyphRunSynthesis synthesis,
                              const std::optional<GlyphRunWindow>& window, GlyphRunOrigin& origin) {
    const double device_size = logical_size * scale;
    const double size_26_6 = device_size * 64.0;
    if (!std::isfinite(size_26_6) || size_26_6 > maximum_size_26_6) throw std::invalid_argument("Invalid glyph run placement");
    const double pixel_x = std::floor(device_x);
    const double pixel_y = std::floor(device_y);
    origin.pixel_x = static_cast<std::int64_t>(pixel_x);
    origin.pixel_y = static_cast<std::int64_t>(pixel_y);
    origin.phase_x = 0;
    stxlib::glyphs::Point part{};
    part.x = static_cast<std::int32_t>(std::llround((device_x - pixel_x) * 64.0));
    part.y = static_cast<std::int32_t>(std::llround((device_y - pixel_y) * 64.0));
    stxlib::glyphs::TextMatrix inverse{};
    inverse.xx = turn.xx;
    inverse.xy = turn.yx;
    inverse.yx = turn.xy;
    inverse.yy = turn.yy;
    const stxlib::glyphs::Point unturned = stxlib::glyphs::turn_place(inverse, part);
    RunPlacement placement{};
    placement.offset_x = unturned.x;
    placement.offset_y = unturned.y;
    placement.linear = {scale, 0.0, 0.0, scale};
    placement.style.size = static_cast<stxlib::i64>(std::llround(size_26_6));
    placement.style.fit = stxlib::glyphs::Fit::light;
    stxlib::glyphs::set_matrix(placement.style, turn);
    placement.style.turn_places = true;
    if (synthesis.thicken) placement.style.thicken = thicken_strength(device_size);
    set_window(placement.style, window);
    return placement;
}

} // namespace

class GlyphRunRasterizer::Impl::Runner final {
public:
    static GlyphRunCoverage run(Impl& impl, const ShapedText& shaped, const RunPlacement& placement) {
        GlyphRunCoverage result{};
        result.profile_version = face_profile_version;
        impl.pieces_.clear();
        impl.piece_pixels_.clear();
        PixelBox bounds{std::numeric_limits<std::int64_t>::max(), std::numeric_limits<std::int64_t>::max(),
                        std::numeric_limits<std::int64_t>::min(), std::numeric_limits<std::int64_t>::min()};
        // Set once the font runs' union passes maximum_extent; no further run
        // is drawn and the whole draws nothing.
        bool oversized = false;
        for (const ShapedFontRun& run : shaped.runs) {
            if (oversized) break;
            const std::uint32_t glyph_count = static_cast<std::uint32_t>(run.glyphs.size());
            detail::ExecutorFace* const face = impl.face(run.face);
            if (face == nullptr) {
                result.missing_glyphs += glyph_count;
                continue;
            }
            place_glyphs(impl, run, placement, result);
            if (impl.run_glyphs_.empty()) continue;
            const stxlib::View<stxlib::glyphs::RunGlyph> glyphs(impl.run_glyphs_.data(), impl.run_glyphs_.size());
            const stxlib::OperationStatus drawn = (*face).draw_run(glyphs, placement.style, impl.run_coverage_);
            const FontFamilyId family = impl.face_family(run.face);
            const bool application = app_font_family_number(family);
            const stxlib::glyphs::RunCoverage& coverage = impl.run_coverage_;
            const bool refused = drawn.failed() || (application && coverage.missing_glyphs != 0U);
            if (refused) {
                // A run an application family's face cannot draw quarantines the family and draws
                // nothing (its glyphs count as missing); re-shaped text then draws with the font's
                // other faces. A bundled face's refusal (STXlib's storage exhausted) is the
                // renderer's.
                if (!application) throw std::runtime_error("Glyph run draw failed");
                quarantine_app_font_family(family, AppFontQuarantine::rasterising_failed);
                result.missing_glyphs += glyph_count;
                continue;
            }
            result.missing_glyphs += coverage.missing_glyphs;
            result.outside_glyphs += coverage.outside_glyphs;
            if (coverage.oversized) {
                oversized = true;
                continue;
            }
            if (coverage.width == 0U || coverage.height == 0U) continue;
            CoveragePiece piece{};
            piece.left = static_cast<std::int64_t>(coverage.left);
            piece.top = static_cast<std::int64_t>(coverage.top);
            piece.width = coverage.width;
            piece.height = coverage.height;
            piece.offset = impl.piece_pixels_.size();
            PixelBox grown = bounds;
            grown.left = std::min(grown.left, piece.left);
            grown.top = std::min(grown.top, piece.top);
            grown.right = std::max(grown.right, piece.left + static_cast<std::int64_t>(piece.width));
            grown.bottom = std::max(grown.bottom, piece.top + static_cast<std::int64_t>(piece.height));
            if (grown.right - grown.left > maximum_extent || grown.bottom - grown.top > maximum_extent) {
                oversized = true;
                continue;
            }
            bounds = grown;
            const stxlib::View<stxlib::u8> pixels = coverage.pixels.view();
            if (pixels.count() != 0U) {
                impl.piece_pixels_.insert(impl.piece_pixels_.end(), pixels.data(), pixels.data() + pixels.count());
            }
            impl.pieces_.push_back(piece);
        }
        if (oversized) {
            result.oversized = true;
            return result;
        }
        if (impl.pieces_.empty()) return result;
        composite_pieces(impl, bounds, result);
        return result;
    }

private:
    // The run's glyphs as STXlib takes them, into impl.run_glyphs_: each
    // one's device position in 26.6 from the origin pixel, y downward. A
    // glyph whose position is beyond 32 bits of 26.6 (33 million pixels)
    // cannot show in any window and is counted as outside.
    static void place_glyphs(Impl& impl, const ShapedFontRun& run, const RunPlacement& placement,
                             GlyphRunCoverage& result) {
        impl.run_glyphs_.clear();
        impl.run_glyphs_.reserve(run.glyphs.size());
        const std::int64_t low = std::numeric_limits<std::int32_t>::min();
        const std::int64_t high = std::numeric_limits<std::int32_t>::max();
        for (const ShapedGlyph& glyph : run.glyphs) {
            const double x = static_cast<double>(glyph.x);
            const double y = static_cast<double>(glyph.y);
            const double mapped_x = placement.linear.xx * x + placement.linear.xy * y;
            const double mapped_y = placement.linear.yx * x + placement.linear.yy * y;
            const double fixed_x = mapped_x * 64.0;
            const double fixed_y = mapped_y * 64.0;
            if (!std::isfinite(fixed_x) || !std::isfinite(fixed_y) || std::abs(fixed_x) > 2.0e9 ||
                std::abs(fixed_y) > 2.0e9) {
                result.outside_glyphs += 1U;
                continue;
            }
            // Integer 26.6 placement relative to the origin pixel.
            const std::int64_t device_x = placement.offset_x + std::llround(fixed_x);
            const std::int64_t device_y = placement.offset_y + std::llround(fixed_y);
            if (device_x < low || device_x > high || device_y < low || device_y > high) {
                result.outside_glyphs += 1U;
                continue;
            }
            stxlib::glyphs::RunGlyph placed{};
            placed.glyph = glyph.glyph.value;
            placed.x = static_cast<std::int32_t>(device_x);
            placed.y = static_cast<std::int32_t>(device_y);
            impl.run_glyphs_.push_back(placed);
        }
    }

    // The font runs' coverage composited in run order into one mask spanning
    // `bounds`: a pixel that has p and gets c becomes c + p * (255 - c) / 255,
    // the rule STXlib combines the glyphs of one run by.
    static void composite_pieces(const Impl& impl, const PixelBox& bounds, GlyphRunCoverage& result) {
        result.left = static_cast<std::int32_t>(bounds.left);
        result.top = static_cast<std::int32_t>(bounds.top);
        result.width = static_cast<std::uint32_t>(bounds.right - bounds.left);
        result.height = static_cast<std::uint32_t>(bounds.bottom - bounds.top);
        result.pixels.assign(static_cast<std::size_t>(result.width) * result.height, 0U);
        for (const CoveragePiece& piece : impl.pieces_) {
            for (std::uint32_t row = 0; row < piece.height; ++row) {
                const std::size_t source = piece.offset + static_cast<std::size_t>(row) * piece.width;
                const std::size_t target_row = static_cast<std::size_t>(piece.top - bounds.top) + row;
                const std::size_t target = target_row * result.width + static_cast<std::size_t>(piece.left - bounds.left);
                for (std::uint32_t column = 0; column < piece.width; ++column) {
                    const unsigned coverage = impl.piece_pixels_[source + column];
                    const unsigned previous = result.pixels[target + column];
                    const unsigned combined = coverage + previous * (255U - coverage) / 255U;
                    result.pixels[target + column] = static_cast<std::uint8_t>(combined);
                }
            }
        }
    }
};

GlyphRunCoverage GlyphRunRasterizer::rasterize(const ShapedText& shaped, const FontSpec font, const double scale,
                                               const std::int32_t phase_x, const GlyphRunSynthesis synthesis,
                                               const std::optional<GlyphRunWindow> window) {
    if (!std::isfinite(scale) || scale <= 0.0 || !std::isfinite(font.size) || phase_x < 0 || phase_x > 63) {
        throw std::invalid_argument("Invalid glyph run placement");
    }
    const double device_size = std::clamp(font.size, 1.0, 4096.0) * scale;
    const double size_26_6 = device_size * 64.0;
    if (!std::isfinite(size_26_6) || size_26_6 > maximum_size_26_6) throw std::invalid_argument("Invalid glyph run placement");
    RunPlacement placement{};
    placement.offset_x = phase_x;
    placement.linear = {scale, 0.0, 0.0, scale};
    placement.style.size = static_cast<stxlib::i64>(std::llround(size_26_6));
    placement.style.fit = stxlib::glyphs::Fit::light;
    if (synthesis.oblique) {
        // STXlib takes a matrix only unhinted: oblique text is the scaled outline, sheared.
        placement.style.fit = stxlib::glyphs::Fit::none;
        set_matrix(placement.style, sheared(GlyphRunTransform{}));
    }
    if (synthesis.thicken) placement.style.thicken = thicken_strength(device_size);
    set_window(placement.style, window);
    GlyphRunCoverage result = Impl::Runner::run(*impl_, shaped, placement);
    return result;
}

GlyphRunCoverage GlyphRunRasterizer::rasterize_transformed(const ShapedText& shaped, const FontSpec font,
                                                           const GlyphRunTransform transform, const double device_x,
                                                           const double device_y, const GlyphRunSynthesis synthesis,
                                                           GlyphRunOrigin& origin,
                                                           const std::optional<GlyphRunWindow> window) {
    const double determinant = transform.xx * transform.yy - transform.xy * transform.yx;
    if (!std::isfinite(font.size) || !std::isfinite(device_x) || !std::isfinite(device_y) ||
        !std::isfinite(determinant) || determinant == 0.0 || std::abs(device_x) > 16'000'000.0 ||
        std::abs(device_y) > 16'000'000.0) {
        throw std::invalid_argument("Invalid glyph run transform");
    }
    const double logical_size = std::clamp(font.size, 1.0, 4096.0);
    // An even scale and a turn (xx = yy, xy = -yx; on the y-down surface
    // the turn anticlockwise as seen with cosine xx / s and sine xy / s, s
    // the scale) is drawn as STXlib's turned text, its turn rounded to
    // 16.16 entries ([c -s; s c] on the outline's y-up axes).
    const bool even_turn = !synthesis.oblique && transform.xx == transform.yy && transform.xy == -transform.yx;
    if (even_turn) {
        const double scale = std::sqrt(transform.xx * transform.xx + transform.xy * transform.xy);
        const std::optional<std::int32_t> cosine = fixed_16_16(transform.xx / scale);
        const std::optional<std::int32_t> sine = fixed_16_16(transform.xy / scale);
        if (!cosine || !sine) throw std::invalid_argument("Invalid glyph run transform");
        stxlib::glyphs::TextMatrix turn{};
        turn.xx = *cosine;
        turn.xy = -*sine;
        turn.yx = *sine;
        turn.yy = *cosine;
        const RunPlacement turned =
            turned_placement(turn, logical_size, scale, device_x, device_y, synthesis, window, origin);
        GlyphRunCoverage result = Impl::Runner::run(*impl_, shaped, turned);
        return result;
    }
    // Logical (y down) to device (y down), expressed in the outline's y-up
    // space: [xx -xy; -yx yy].
    GlyphRunTransform outline_matrix{transform.xx, -transform.xy, -transform.yx, transform.yy};
    if (synthesis.oblique) outline_matrix = sheared(outline_matrix);
    RunPlacement placement{};
    // Hinting assumes axis alignment; rotated and skewed text is unhinted.
    placement.style.fit = stxlib::glyphs::Fit::none;
    set_matrix(placement.style, outline_matrix);
    const double pixel_x = std::floor(device_x);
    const double pixel_y = std::floor(device_y);
    origin.pixel_x = static_cast<std::int64_t>(pixel_x);
    origin.pixel_y = static_cast<std::int64_t>(pixel_y);
    origin.phase_x = 0;
    placement.offset_x = std::llround((device_x - pixel_x) * 64.0);
    placement.offset_y = std::llround((device_y - pixel_y) * 64.0);
    placement.linear = transform;
    placement.style.size = static_cast<stxlib::i64>(std::llround(logical_size * 64.0));
    if (synthesis.thicken) placement.style.thicken = thicken_strength(logical_size * std::sqrt(std::abs(determinant)));
    set_window(placement.style, window);
    GlyphRunCoverage result = Impl::Runner::run(*impl_, shaped, placement);
    return result;
}

GlyphRunCoverage GlyphRunRasterizer::rasterize_turned(const ShapedText& shaped, const FontSpec font, const double degrees,
                                                      const double scale, const double device_x, const double device_y,
                                                      const GlyphRunSynthesis synthesis, GlyphRunOrigin& origin,
                                                      const std::optional<GlyphRunWindow> window) {
    if (!std::isfinite(degrees) || !std::isfinite(scale) || scale <= 0.0 || !std::isfinite(font.size) ||
        !std::isfinite(device_x) || !std::isfinite(device_y) || std::abs(device_x) > 16'000'000.0 ||
        std::abs(device_y) > 16'000'000.0) {
        throw std::invalid_argument("Invalid glyph run turn");
    }
    const double logical_size = std::clamp(font.size, 1.0, 4096.0);
    const stxlib::glyphs::TextMatrix turn = stxlib::glyphs::turn_matrix(gui_forms::detail::text_turn_degrees_16_16(degrees));
    if (synthesis.oblique) {
        // An oblique turned run is not a turn: the outline, as rasterize_transformed draws any matrix.
        const double cosine = static_cast<double>(turn.xx) / 65536.0;
        const double sine = static_cast<double>(turn.yx) / 65536.0;
        const GlyphRunTransform transform{scale * cosine, scale * sine, -scale * sine, scale * cosine};
        GlyphRunCoverage sheared_result =
            rasterize_transformed(shaped, font, transform, device_x, device_y, synthesis, origin, window);
        return sheared_result;
    }
    const RunPlacement placement =
        turned_placement(turn, logical_size, scale, device_x, device_y, synthesis, window, origin);
    GlyphRunCoverage result = Impl::Runner::run(*impl_, shaped, placement);
    return result;
}

} // namespace gui_forms::render::text
