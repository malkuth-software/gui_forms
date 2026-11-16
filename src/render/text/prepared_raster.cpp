#include "../../core/text/prepared/prepared_storage.hpp"
#include "../../core/resources/fonts/program_typefaces.hpp"
#include "../../core/text/prepared/prepared_face_pool.hpp"

#include <stxlib/glyph_face.hpp>
#include <stxlib/result.hpp>
#include <stxlib/types.hpp>
#include <stxlib/view.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace gui_forms {
namespace {

// The faces one rasterization draws with: one ExecutorFace per bank face,
// leased from the bank's pool and opened over its typefaces on first use
// (prepared_face_pool.hpp).
using RasterFaces = std::array<detail::ExecutorFace, PreparedTextLimits::font_faces>;

struct InkBounds final {
    std::int64_t left{};
    std::int64_t top{};
    std::int64_t right{};
    std::int64_t bottom{};
    bool present{};
};

std::size_t raster_face_index(const detail::PreparedTextStorage& storage, const FontFaceId id) noexcept {
    const std::size_t count = (*storage.fonts).face_count;
    for (std::size_t index = 0; index < count; ++index) {
        if (storage.face_ids[index] == id) return index;
    }
    return count;
}

// ADR-004 profile, drawn by STXlib's glyph face: light auto-hinting along
// y, grey coverage, at the layout's device size.
stxlib::glyphs::DrawStyle raster_style(const detail::PreparedTextStorage& storage) noexcept {
    stxlib::glyphs::DrawStyle style{};
    style.size = static_cast<stxlib::i64>(storage.metrics.device_size_26_6);
    style.fit = stxlib::glyphs::Fit::light;
    return style;
}

PreparedTextStatus open_faces(const detail::PreparedTextStorage& storage, RasterFaces& faces) {
    const detail::PreparedFontBank& bank = *storage.fonts;
    detail::FaceByteLimits limits{};
    limits.fitted_bytes = PreparedTextLimits::fitted_glyph_bytes;
    limits.mask_width = static_cast<std::uint32_t>(PreparedTextLimits::mask_dimension);
    limits.mask_height = static_cast<std::uint32_t>(PreparedTextLimits::mask_dimension);
    for (std::size_t index = 0; index < bank.face_count; ++index) {
        if (!storage.face_ids[index].valid()) return PreparedTextStatus::incompatible_font;
        for (std::size_t prior = 0; prior < index; ++prior) {
            if (storage.face_ids[prior] == storage.face_ids[index]) return PreparedTextStatus::incompatible_font;
        }
        const std::shared_ptr<const detail::ProgramTypeface>& typeface = bank.typefaces[index];
        if (!typeface) return PreparedTextStatus::incompatible_font;
        // A face this set opened before is kept, with what it has fitted.
        if (faces[index].is_open()) continue;
        // No keeper: the bank holds the typeface and outlives its pool.
        detail::TypefaceBorrow borrow{};
        borrow.typeface = typeface.get();
        if (!faces[index].open(std::move(borrow), limits)) return PreparedTextStatus::resource_failure;
    }
    if (storage.metrics.device_size_26_6 <= 0 || storage.metrics.device_size_26_6 > (std::int64_t{1} << 30)) {
        return PreparedTextStatus::unsupported_profile;
    }
    return PreparedTextStatus::success;
}

PreparedTextStatus validate_geometry(const detail::PreparedTextStorage& storage, const RasterFaces& faces) {
    const render::text::BoundedShapedText& geometry = *storage.geometry;
    if (geometry.run_count > geometry.run_capacity || geometry.glyph_count > geometry.glyph_capacity ||
        geometry.run_count > 16'384 || geometry.glyph_count > 65'536 ||
        (geometry.run_count != 0 && !geometry.runs) || (geometry.glyph_count != 0 && !geometry.glyphs) ||
        !std::isfinite(geometry.width) || !std::isfinite(geometry.height) || !std::isfinite(geometry.ascent) ||
        !std::isfinite(geometry.descent) || geometry.width < 0 || geometry.height < 0 ||
        geometry.ascent < 0 || geometry.descent < 0) return PreparedTextStatus::invalid_geometry;
    std::size_t consumed = 0;
    for (std::size_t run_index = 0; run_index < geometry.run_count; ++run_index) {
        const render::text::BoundedFontRun& run = geometry.runs[run_index];
        const std::size_t face_index = raster_face_index(storage, run.face);
        if (face_index == (*storage.fonts).face_count) return PreparedTextStatus::incompatible_font;
        if (run.glyph_begin != consumed || run.glyph_count > geometry.glyph_count - consumed ||
            run.source_range.start.value() >= run.source_range.end.value() ||
            run.source_range.end.value() > (*storage.input).text_bytes) return PreparedTextStatus::invalid_geometry;
        const std::uint64_t glyph_limit = static_cast<std::uint64_t>((*faces[face_index].font()).glyph_count);
        for (std::size_t offset = 0; offset < run.glyph_count; ++offset) {
            const render::text::ShapedGlyph& glyph = geometry.glyphs[consumed + offset];
            if (glyph.glyph.value == 0 || glyph.glyph.value >= glyph_limit) return PreparedTextStatus::incompatible_font;
            if (!std::isfinite(glyph.x) || !std::isfinite(glyph.y) || !std::isfinite(glyph.advance_x) ||
                !std::isfinite(glyph.advance_y) || std::abs(glyph.x) > 1'000'000.0F || std::abs(glyph.y) > 1'000'000.0F ||
                glyph.cluster.value() < run.source_range.start.value() ||
                glyph.cluster.value() >= run.source_range.end.value()) return PreparedTextStatus::invalid_geometry;
        }
        consumed += run.glyph_count;
    }
    if (consumed != geometry.glyph_count) return PreparedTextStatus::invalid_geometry;
    return PreparedTextStatus::success;
}

// One run's glyphs as STXlib takes them, into `placed`: positions in 26.6
// device pixels from the layout origin, y downward. validate_geometry has
// bounded every position to a million pixels, so each fits 32 bits.
void place_run(const render::text::BoundedShapedText& geometry, const render::text::BoundedFontRun& run,
               std::vector<stxlib::glyphs::RunGlyph>& placed) {
    placed.clear();
    for (std::size_t offset = 0; offset < run.glyph_count; ++offset) {
        const render::text::ShapedGlyph& glyph = geometry.glyphs[run.glyph_begin + offset];
        stxlib::glyphs::RunGlyph entry{};
        entry.glyph = glyph.glyph.value;
        entry.x = static_cast<std::int32_t>(std::llround(static_cast<double>(glyph.x) * 64.0));
        entry.y = static_cast<std::int32_t>(std::llround(static_cast<double>(glyph.y) * 64.0));
        placed.push_back(entry);
    }
}

PreparedTextStatus measure_ink(const detail::PreparedTextStorage& storage, RasterFaces& faces,
    std::vector<stxlib::glyphs::RunGlyph>& placed, InkBounds& bounds) {
    const render::text::BoundedShapedText& geometry = *storage.geometry;
    const stxlib::glyphs::DrawStyle style = raster_style(storage);
    for (std::size_t run_index = 0; run_index < geometry.run_count; ++run_index) {
        const render::text::BoundedFontRun& run = geometry.runs[run_index];
        const std::size_t face_index = raster_face_index(storage, run.face);
        place_run(geometry, run, placed);
        stxlib::glyphs::RunBox box{};
        const stxlib::View<stxlib::glyphs::RunGlyph> glyphs(placed.data(), placed.size());
        const stxlib::Status measured = faces[face_index].measure_run(glyphs, style, box);
        if (measured.failed() || box.missing_glyphs != 0U) return PreparedTextStatus::native_failure;
        if (box.oversized) return PreparedTextStatus::budget_exceeded;
        if (box.right <= box.left || box.bottom <= box.top) continue;
        const InkBounds run_bounds{box.left, box.top, box.right, box.bottom, true};
        if (!bounds.present) bounds = run_bounds;
        else {
            bounds.left = std::min(bounds.left, run_bounds.left);
            bounds.top = std::min(bounds.top, run_bounds.top);
            bounds.right = std::max(bounds.right, run_bounds.right);
            bounds.bottom = std::max(bounds.bottom, run_bounds.bottom);
        }
        if (bounds.right - bounds.left > static_cast<std::int64_t>(PreparedTextLimits::mask_dimension) ||
            bounds.bottom - bounds.top > static_cast<std::int64_t>(PreparedTextLimits::mask_dimension)) {
            return PreparedTextStatus::budget_exceeded;
        }
    }
    return PreparedTextStatus::success;
}

PreparedTextStatus fill_mask(const detail::PreparedTextStorage& storage, RasterFaces& faces,
    std::vector<stxlib::glyphs::RunGlyph>& placed, stxlib::glyphs::RunCoverage& coverage,
    detail::PreparedMaskStorage& mask) {
    const render::text::BoundedShapedText& geometry = *storage.geometry;
    const stxlib::glyphs::DrawStyle style = raster_style(storage);
    for (std::size_t run_index = 0; run_index < geometry.run_count; ++run_index) {
        const render::text::BoundedFontRun& run = geometry.runs[run_index];
        const std::size_t face_index = raster_face_index(storage, run.face);
        place_run(geometry, run, placed);
        const stxlib::View<stxlib::glyphs::RunGlyph> glyphs(placed.data(), placed.size());
        const stxlib::Status drawn = faces[face_index].draw_run(glyphs, style, coverage);
        if (drawn.failed() || coverage.missing_glyphs != 0U || coverage.oversized) return PreparedTextStatus::native_failure;
        if (coverage.width == 0U || coverage.height == 0U) continue;
        const std::int64_t left = static_cast<std::int64_t>(coverage.left) - mask.left;
        const std::int64_t top = static_cast<std::int64_t>(coverage.top) - mask.top;
        if (left < 0 || top < 0 || left + coverage.width > mask.width || top + coverage.height > mask.height) {
            return PreparedTextStatus::invalid_geometry;
        }
        const stxlib::View<stxlib::u8> pixels = coverage.pixels.view();
        const std::size_t target_left = static_cast<std::size_t>(left);
        const std::size_t target_top = static_cast<std::size_t>(top);
        for (std::size_t row = 0; row < coverage.height; ++row) {
            const std::size_t source_row = row * coverage.width;
            const std::size_t destination_row = (target_top + row) * mask.width + target_left;
            for (std::size_t column = 0; column < coverage.width; ++column) {
                const unsigned value = pixels[source_row + column];
                const unsigned previous = mask.pixels[destination_row + column];
                const unsigned combined = value + previous * (255U - value) / 255U;
                mask.pixels[destination_row + column] = static_cast<std::uint8_t>(combined);
            }
        }
    }
    return PreparedTextStatus::success;
}

// Charges to the bank's ledger what its typefaces, its pool's idle faces and
// this rasterization's faces hold (faces leased by other rasterizations at
// the time are charged when they next charge), and the most this
// rasterization's faces and their typefaces held together.
void charge_faces(const detail::PreparedTextStorage& storage, const RasterFaces& faces) noexcept {
    const detail::PreparedFontBank& bank = *storage.fonts;
    std::size_t held = (*bank.face_pool).idle_face_bytes();
    std::size_t peak = 0;
    for (std::size_t index = 0; index < bank.face_count; ++index) {
        if (bank.typefaces[index]) held += (*bank.typefaces[index]).bytes_held();
        held += faces[index].face_bytes_held();
        peak += faces[index].bytes_peak();
    }
    bank.face_charge.charge(bank.ledger, held, peak);
}
} // namespace

PreparedTextStatus detail::validate_prepared_fonts(PreparedFontBank& bank) {
    try {
        const bool opened = open_bank_typefaces(bank);
        if (!opened) return PreparedTextStatus::incompatible_font;
        return PreparedTextStatus::success;
    } catch (const std::bad_alloc&) { return PreparedTextStatus::resource_failure; }
}

PreparedTextStatus rasterize_prepared_text(const PreparedTextLayout& layout, const LayoutAuthority expected, GrayTextMask& output) {
    const std::shared_ptr<const detail::PreparedTextStorage> storage = detail::PreparedTextAccess::layout(layout);
    if (!storage) return PreparedTextStatus::invalid_input;
    if (!detail::prepared_authority_current(*storage, expected)) return PreparedTextStatus::stale;
    try {
        if (!(*(*storage).fonts).face_pool) return PreparedTextStatus::incompatible_font;
        // Returned to the bank's pool before `storage`, which holds the bank.
        detail::PreparedFaceLease lease(*(*(*storage).fonts).face_pool);
        detail::PreparedRasterFaces& set = lease.set();
        RasterFaces& faces = set.faces;
        const PreparedTextStatus opened = open_faces(*storage, faces);
        if (opened != PreparedTextStatus::success) return opened;
        const PreparedTextStatus validated = validate_geometry(*storage, faces);
        if (validated != PreparedTextStatus::success) return validated;
        std::vector<stxlib::glyphs::RunGlyph>& placed = set.placed;
        placed.reserve((*(*storage).geometry).glyph_count);
        InkBounds bounds{};
        const PreparedTextStatus measured = measure_ink(*storage, faces, placed, bounds);
        charge_faces(*storage, faces);
        if (measured != PreparedTextStatus::success) return measured;
        const std::size_t width = static_cast<std::size_t>(bounds.right - bounds.left);
        const std::size_t height = static_cast<std::size_t>(bounds.bottom - bounds.top);
        if (width != 0 && height > PreparedTextLimits::mask_bytes / width) return PreparedTextStatus::budget_exceeded;
        const std::size_t bytes = width * height;
        detail::PreparedReservation reservation{};
        const PreparedTextStatus admitted = reservation.acquire((*storage).ledger, detail::PreparedResource::mask, bytes);
        if (admitted != PreparedTextStatus::success) return admitted;
        std::unique_ptr<detail::PreparedMaskStorage> candidate = std::make_unique<detail::PreparedMaskStorage>();
        detail::PreparedMaskStorage& mask = *candidate;
        mask.reservation = std::move(reservation);
        mask.metrics = (*storage).metrics;
        mask.width = static_cast<std::uint32_t>(width);
        mask.height = static_cast<std::uint32_t>(height);
        mask.left = static_cast<std::int32_t>(bounds.left);
        mask.top = static_cast<std::int32_t>(bounds.top);
        mask.pixel_count = bytes;
        if (bytes != 0) mask.pixels = std::make_unique<std::uint8_t[]>(bytes);
        const PreparedTextStatus painted = fill_mask(*storage, faces, placed, set.coverage, mask);
        charge_faces(*storage, faces);
        if (painted != PreparedTextStatus::success) return painted;
        detail::PreparedAuthorityState& authority = *(*storage).authority_state;
        std::lock_guard<std::mutex> publication_lock(authority.mutex);
        if (!detail::prepared_authority_current_locked(*storage, expected)) return PreparedTextStatus::stale;
        detail::PreparedTextAccess::mask(output) = std::move(candidate);
        return PreparedTextStatus::success;
    } catch (const std::bad_alloc&) { return PreparedTextStatus::resource_failure; }
    catch (const std::runtime_error&) { return PreparedTextStatus::native_failure; }
}
} // namespace gui_forms
