#include "glyph_run_raster.hpp"

#include "gui_forms/typography/app_fonts/app_fonts.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_OUTLINE_H

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>
#include <vector>

namespace gui_forms::render::text {
namespace {

constexpr FT_Int32 light_flags = FT_LOAD_NO_BITMAP | FT_LOAD_TARGET_LIGHT | FT_LOAD_FORCE_AUTOHINT;
// Coverage dimensions above this are reported as oversized rather than allocated.
constexpr std::int64_t maximum_extent = glyph_run_maximum_extent;

std::int64_t floor_div_64(const std::int64_t value) noexcept {
    std::int64_t quotient = value / 64;
    if (value % 64 != 0 && value < 0) --quotient;
    return quotient;
}

std::int64_t ceil_div_64(const std::int64_t value) noexcept {
    std::int64_t quotient = value / 64;
    if (value % 64 != 0 && value > 0) ++quotient;
    return quotient;
}

// A device pixel box, [left, right) x [top, bottom), relative to the origin pixel.
struct PixelBox final {
    std::int64_t left{};
    std::int64_t top{};
    std::int64_t right{};
    std::int64_t bottom{};
};

// Whether a placed outline's box can reach the window. The box is the
// outline's control box in whole pixels, which contains the bitmap
// FT_Render_Glyph makes from it, widened by one pixel for antialiased edges.
bool outline_can_show(const FT_Outline& outline, const std::int64_t pixel_x, const std::int64_t pixel_y,
                      const GlyphRunWindow& window) noexcept {
    FT_BBox control{};
    FT_Outline_Get_CBox(&outline, &control);
    // FreeType's outline space is y-up; device rows grow downward.
    PixelBox box{};
    box.left = pixel_x + floor_div_64(static_cast<std::int64_t>(control.xMin)) - 1;
    box.right = pixel_x + ceil_div_64(static_cast<std::int64_t>(control.xMax)) + 1;
    box.top = pixel_y - ceil_div_64(static_cast<std::int64_t>(control.yMax)) - 1;
    box.bottom = pixel_y - floor_div_64(static_cast<std::int64_t>(control.yMin)) + 1;
    const bool apart = box.right <= window.left || box.left >= window.right || box.bottom <= window.top ||
        box.top >= window.bottom;
    return !apart;
}

// One rendered glyph, copied out of FreeType's slot before the next render.
struct RenderedGlyph final {
    std::int64_t left{};
    std::int64_t top{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::size_t offset{};
};

// An outline the caller owns (FT_Outline_New), released with its library.
// A placed copy of a hinted outline is restored from it before each render.
class OwnedOutline final {
public:
    OwnedOutline(const FT_Library library, const FT_Outline& source) : library_(library) {
        if (FT_Outline_New(library, static_cast<FT_UInt>(source.n_points), source.n_contours, &outline_) != 0) {
            throw std::runtime_error("Glyph outline copy failed");
        }
        if (FT_Outline_Copy(&source, &outline_) != 0) {
            FT_Outline_Done(library_, &outline_);
            throw std::runtime_error("Glyph outline copy failed");
        }
    }
    ~OwnedOutline() { FT_Outline_Done(library_, &outline_); }
    OwnedOutline(const OwnedOutline&) = delete;
    OwnedOutline& operator=(const OwnedOutline&) = delete;

    const FT_Outline& outline() const noexcept { return outline_; }

private:
    FT_Library library_{};
    FT_Outline outline_{};
};

// Orders a run's glyph indices by glyph ID, so the occurrences of one glyph
// are adjacent; the sort is stable, so each glyph's occurrences keep their
// run order.
struct ByGlyphId final {
    const std::vector<ShapedGlyph>* glyphs{};
    bool operator()(const std::size_t left, const std::size_t right) const noexcept {
        return (*glyphs)[left].glyph.value < (*glyphs)[right].glyph.value;
    }
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

class GlyphRunRasterizer::Impl final {
public:
    class Runner;
    explicit Impl(const HarfBuzzFontEngine& engine) : engine_(engine) {
        if (FT_Init_FreeType(&library_) != 0) throw std::runtime_error("Glyph run FreeType initialization failed");
    }
    ~Impl() {
        for (OpenFace& face : faces_) FT_Done_Face(face.face);
        FT_Done_FreeType(library_);
    }
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    // Nullptr when the engine has no source for this ID.
    FT_Library library() const noexcept { return library_; }

    FT_Face face(const FontFaceId id) {
        for (const OpenFace& open : faces_) {
            if (open.id == id) return open.face;
        }
        const std::optional<FontFaceSource> source = engine_.face_source(id);
        if (!source) return nullptr;
        FT_Face opened = nullptr;
        const FT_Error status = FT_New_Memory_Face(library_, reinterpret_cast<const FT_Byte*>((*source).bytes.data()),
                                                   static_cast<FT_Long>((*source).bytes.size()),
                                                   static_cast<FT_Long>((*source).face_index), &opened);
        if (status != 0) {
            // An application family's face that will not open draws nothing from now on (its
            // glyphs count as missing); a bundled face's failure is the renderer's.
            if (!app_font_family_number((*source).family)) throw std::runtime_error("Glyph run face open failed");
            quarantine_app_font_family((*source).family, AppFontQuarantine::rasterising_failed);
            return nullptr;
        }
        faces_.push_back({id, opened, (*source).owner, (*source).family});
        return opened;
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
        FT_Face face{};
        // Keeps the encoded bytes alive for FreeType's memory face.
        std::shared_ptr<const void> owner;
        FontFamilyId family{};
    };

    const HarfBuzzFontEngine& engine_;
    FT_Library library_{};
    std::vector<OpenFace> faces_;
};

GlyphRunRasterizer::GlyphRunRasterizer(const HarfBuzzFontEngine& engine) : impl_(std::make_unique<Impl>(engine)) {}

GlyphRunRasterizer::~GlyphRunRasterizer() = default;

namespace {

// How a run's glyphs reach device pixels. Device position of glyph (x, y),
// in 26.6 relative to the origin pixel, is
// offset + llround((linear * (x, y)) * 64).
struct RunPlacement final {
    std::int64_t offset_x{};
    std::int64_t offset_y{};
    GlyphRunTransform linear{};
    FT_F26Dot6 size{};
    FT_Int32 flags{light_flags};
    // FreeType's outline transform; none when the run is only scaled
    // uniformly (no oblique, no transform).
    std::optional<FT_Matrix> matrix{};
    // Total outline thickening in 26.6 device units; 0 for none.
    FT_Pos embolden{};
    // The pixels the run can show, relative to the origin pixel; none: all.
    std::optional<GlyphRunWindow> window{};
};

FT_Fixed fixed_16_16(const double value) noexcept {
    return static_cast<FT_Fixed>(std::llround(value * 65536.0));
}

// Fake-bold thickening for a device pixel size: 1/24 of the size at 9 px
// falling linearly to 1/32 at 36 px and beyond.
FT_Pos embolden_strength(const double device_size) noexcept {
    const double clamped = std::clamp(device_size, 9.0, 36.0);
    const double ratio = 1.0 / 24.0 + (clamped - 9.0) / 27.0 * (1.0 / 32.0 - 1.0 / 24.0);
    return static_cast<FT_Pos>(std::llround(device_size * ratio * 64.0));
}

// Oblique shear, in FreeType's y-up outline space: x' = x + 0.25 y.
FT_Matrix oblique_matrix() noexcept {
    FT_Matrix shear{};
    shear.xx = 0x10000;
    shear.xy = 0x4000;
    shear.yx = 0;
    shear.yy = 0x10000;
    return shear;
}

} // namespace

class GlyphRunRasterizer::Impl::Runner final {
public:
    static GlyphRunCoverage run(Impl& impl, const ShapedText& shaped, const RunPlacement& placement) {
        GlyphRunCoverage result{};
        std::vector<RenderedGlyph> glyphs;
        std::vector<std::uint8_t> bitmaps;
        std::int64_t left = std::numeric_limits<std::int64_t>::max();
        std::int64_t top = std::numeric_limits<std::int64_t>::max();
        std::int64_t right = std::numeric_limits<std::int64_t>::min();
        std::int64_t bottom = std::numeric_limits<std::int64_t>::min();
        // Set once the kept glyphs' union passes maximum_extent; no further
        // glyph is rendered and the run draws nothing.
        bool oversized = false;
        for (const ShapedFontRun& run : shaped.runs) {
            if (oversized) break;
            const FT_Face face = impl.face(run.face);
            if (face == nullptr) {
                result.missing_glyphs += static_cast<std::uint32_t>(run.glyphs.size());
                continue;
            }
            const std::vector<ShapedGlyph>& run_glyphs = run.glyphs;
            // Rendered glyphs by their run position: compositing below keeps
            // run order. Empty glyphs stay unset.
            std::vector<std::optional<RenderedGlyph>> placed(run_glyphs.size());
            const FontFamilyId family = impl.face_family(run.face);
            // This run's kept glyphs with those of the runs before it; merged
            // into (left, top, right, bottom) only once the run has succeeded.
            PixelBox run_union{left, top, right, bottom};
            try {
                if (FT_Set_Char_Size(face, 0, placement.size, 72U, 72U) != 0) throw std::runtime_error("Glyph run size failed");
                // FT_Load_Glyph hints (or, unhinted, scales) a glyph before it
                // applies the transform matrix and the subpixel delta, so the
                // loaded outline is the same at every placement. Each distinct
                // glyph is loaded once, without a transform; every occurrence
                // then restores that outline into the slot and applies what
                // FT_Load_Glyph would have applied (matrix, then delta), the
                // embolding, and the render.
                FT_Set_Transform(face, nullptr, nullptr);
                std::vector<std::size_t> order(run_glyphs.size());
                for (std::size_t index = 0; index < order.size(); ++index) order[index] = index;
                std::stable_sort(order.begin(), order.end(), ByGlyphId{&run_glyphs});
                std::size_t next = 0;
                while (next < order.size() && !oversized) {
                    const std::uint32_t glyph_id = run_glyphs[order[next]].glyph.value;
                    if (FT_Load_Glyph(face, glyph_id, placement.flags) != 0) throw std::runtime_error("Glyph load failed");
                    const FT_GlyphSlot slot = (*face).glyph;
                    if ((*slot).format != FT_GLYPH_FORMAT_OUTLINE) throw std::runtime_error("Glyph is not an outline");
                    const OwnedOutline loaded(impl.library(), (*slot).outline);
                    for (; next < order.size() && run_glyphs[order[next]].glyph.value == glyph_id && !oversized; ++next) {
                        const ShapedGlyph& glyph = run_glyphs[order[next]];
                        const double x = static_cast<double>(glyph.x);
                        const double y = static_cast<double>(glyph.y);
                        const double mapped_x = placement.linear.xx * x + placement.linear.xy * y;
                        const double mapped_y = placement.linear.yx * x + placement.linear.yy * y;
                        // Integer 26.6 placement relative to the origin pixel.
                        const std::int64_t device_x = placement.offset_x + std::llround(mapped_x * 64.0);
                        const std::int64_t device_y = placement.offset_y + std::llround(mapped_y * 64.0);
                        const std::int64_t pixel_x = floor_div_64(device_x);
                        const std::int64_t pixel_y = floor_div_64(device_y);
                        const FT_Pos delta_x = static_cast<FT_Pos>(device_x - pixel_x * 64);
                        const FT_Pos delta_y = -static_cast<FT_Pos>(device_y - pixel_y * 64);
                        // The slot still holds this glyph's arrays (same point
                        // and contour counts); the previous render left them
                        // placed and the slot marked as a bitmap.
                        if (FT_Outline_Copy(&loaded.outline(), &(*slot).outline) != 0) {
                            throw std::runtime_error("Glyph outline restore failed");
                        }
                        (*slot).format = FT_GLYPH_FORMAT_OUTLINE;
                        if (placement.matrix.has_value()) FT_Outline_Transform(&(*slot).outline, &*placement.matrix);
                        FT_Outline_Translate(&(*slot).outline, delta_x, delta_y);
                        if (placement.embolden != 0 &&
                            FT_Outline_EmboldenXY(&(*slot).outline, placement.embolden, placement.embolden) != 0) {
                            throw std::runtime_error("Glyph embolden failed");
                        }
                        if (placement.window.has_value() &&
                            !outline_can_show((*slot).outline, pixel_x, pixel_y, *placement.window)) {
                            result.outside_glyphs += 1U;
                            continue;
                        }
                        if (FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL) != 0) throw std::runtime_error("Glyph render failed");
                        const FT_Bitmap& bitmap = (*slot).bitmap;
                        if (bitmap.width == 0 || bitmap.rows == 0) continue;
                        if (bitmap.pixel_mode != FT_PIXEL_MODE_GRAY || bitmap.num_grays != 256 || bitmap.pitch <= 0 ||
                            static_cast<unsigned>(bitmap.pitch) < bitmap.width) {
                            throw std::runtime_error("Unexpected glyph bitmap");
                        }
                        RenderedGlyph rendered{};
                        rendered.left = pixel_x + (*slot).bitmap_left;
                        rendered.top = pixel_y - (*slot).bitmap_top;
                        rendered.width = bitmap.width;
                        rendered.height = bitmap.rows;
                        rendered.offset = bitmaps.size();
                        // The extent is checked as glyphs are kept, before their bitmaps are.
                        run_union.left = std::min(run_union.left, rendered.left);
                        run_union.top = std::min(run_union.top, rendered.top);
                        run_union.right = std::max(run_union.right, rendered.left + static_cast<std::int64_t>(rendered.width));
                        run_union.bottom = std::max(run_union.bottom, rendered.top + static_cast<std::int64_t>(rendered.height));
                        if (run_union.right - run_union.left > maximum_extent ||
                            run_union.bottom - run_union.top > maximum_extent) {
                            oversized = true;
                            continue;
                        }
                        for (unsigned row = 0; row < bitmap.rows; ++row) {
                            const std::uint8_t* const line = bitmap.buffer + static_cast<std::size_t>(row) * static_cast<std::size_t>(bitmap.pitch);
                            bitmaps.insert(bitmaps.end(), line, line + bitmap.width);
                        }
                        placed[order[next]] = rendered;
                    }
                }
            } catch (const std::runtime_error&) {
                // A glyph an application family's face cannot load or render quarantines the
                // family and the run draws nothing (its glyphs count as missing); re-shaped text
                // then draws with the font's other faces. A bundled face's failure propagates.
                if (!app_font_family_number(family)) throw;
                quarantine_app_font_family(family, AppFontQuarantine::rasterising_failed);
                result.missing_glyphs += static_cast<std::uint32_t>(run_glyphs.size());
                continue;
            }
            for (const std::optional<RenderedGlyph>& rendered : placed) {
                if (!rendered.has_value()) continue;
                left = std::min(left, (*rendered).left);
                top = std::min(top, (*rendered).top);
                right = std::max(right, (*rendered).left + static_cast<std::int64_t>((*rendered).width));
                bottom = std::max(bottom, (*rendered).top + static_cast<std::int64_t>((*rendered).height));
                glyphs.push_back(*rendered);
            }
        }
        if (oversized) {
            result.oversized = true;
            return result;
        }
        if (glyphs.empty()) return result;
        result.left = static_cast<std::int32_t>(left);
        result.top = static_cast<std::int32_t>(top);
        result.width = static_cast<std::uint32_t>(right - left);
        result.height = static_cast<std::uint32_t>(bottom - top);
        result.pixels.assign(static_cast<std::size_t>(result.width) * result.height, 0U);
        for (const RenderedGlyph& glyph : glyphs) {
            for (std::uint32_t row = 0; row < glyph.height; ++row) {
                const std::size_t source = glyph.offset + static_cast<std::size_t>(row) * glyph.width;
                const std::size_t target_row = static_cast<std::size_t>(glyph.top - top) + row;
                const std::size_t target = target_row * result.width + static_cast<std::size_t>(glyph.left - left);
                for (std::uint32_t column = 0; column < glyph.width; ++column) {
                    const unsigned coverage = bitmaps[source + column];
                    const unsigned previous = result.pixels[target + column];
                    const unsigned combined = coverage + previous * (255U - coverage) / 255U;
                    result.pixels[target + column] = static_cast<std::uint8_t>(combined);
                }
            }
        }
        return result;
    }
};

GlyphRunCoverage GlyphRunRasterizer::rasterize(const ShapedText& shaped, const FontSpec font, const double scale,
                                               const std::int32_t phase_x, const GlyphRunSynthesis synthesis,
                                               const std::optional<GlyphRunWindow> window) {
    if (!std::isfinite(scale) || scale <= 0.0 || !std::isfinite(font.size) || phase_x < 0 || phase_x > 63) {
        throw std::invalid_argument("Invalid glyph run placement");
    }
    const double device_size = std::clamp(font.size, 1.0, 4096.0) * scale;
    RunPlacement placement{};
    placement.offset_x = phase_x;
    placement.linear = {scale, 0.0, 0.0, scale};
    placement.size = static_cast<FT_F26Dot6>(std::llround(device_size * 64.0));
    placement.flags = light_flags;
    if (synthesis.oblique) {
        placement.matrix = oblique_matrix();
    }
    if (synthesis.embolden) placement.embolden = embolden_strength(device_size);
    placement.window = window;
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
    const double pixel_x = std::floor(device_x);
    const double pixel_y = std::floor(device_y);
    origin.pixel_x = static_cast<std::int64_t>(pixel_x);
    origin.pixel_y = static_cast<std::int64_t>(pixel_y);
    origin.phase_x = 0;
    RunPlacement placement{};
    placement.offset_x = std::llround((device_x - pixel_x) * 64.0);
    placement.offset_y = std::llround((device_y - pixel_y) * 64.0);
    placement.linear = transform;
    placement.size = static_cast<FT_F26Dot6>(std::llround(logical_size * 64.0));
    // Hinting assumes axis alignment; rotated and skewed text is unhinted.
    placement.flags = FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING;
    // Logical (y down) to device (y down), expressed in FreeType's y-up
    // outline space: [xx -xy; -yx yy].
    FT_Matrix matrix{};
    matrix.xx = fixed_16_16(transform.xx);
    matrix.xy = fixed_16_16(-transform.xy);
    matrix.yx = fixed_16_16(-transform.yx);
    matrix.yy = fixed_16_16(transform.yy);
    if (synthesis.oblique) {
        FT_Matrix shear = oblique_matrix();
        FT_Matrix_Multiply(&matrix, &shear);
        matrix = shear;
    }
    placement.matrix = matrix;
    if (synthesis.embolden) placement.embolden = embolden_strength(logical_size * std::sqrt(std::abs(determinant)));
    placement.window = window;
    GlyphRunCoverage result = Impl::Runner::run(*impl_, shaped, placement);
    return result;
}

} // namespace gui_forms::render::text
