#include "prepared_window_shape.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace gui_forms::render::text {
namespace {

constexpr std::size_t context_bytes = sizeof(PreparedWindowShaper) - sizeof(BoundedShapeWorkspace);
static_assert(context_bytes < PreparedTextLimits::workspace_bytes);

bool finite_geometry(const BoundedShapedText& geometry, const std::size_t text_bytes,
    const std::span<const FontFaceId> faces) noexcept {
    if (!std::isfinite(geometry.width) || !std::isfinite(geometry.height) ||
        !std::isfinite(geometry.ascent) || !std::isfinite(geometry.descent) ||
        geometry.width < 0.0 || geometry.height < 0.0) return false;
    std::size_t glyph_begin = 0U;
    for (std::size_t index = 0U; index < geometry.run_count; ++index) {
        const BoundedFontRun& run = geometry.runs[index];
        if (run.source_range.start.value() > run.source_range.end.value() ||
            run.source_range.end.value() > text_bytes || run.glyph_begin != glyph_begin ||
            run.glyph_count > geometry.glyph_count - glyph_begin) return false;
        bool known = false;
        for (std::size_t face = 0U; face < faces.size(); ++face) {
            if (faces[face] == run.face) { known = true; break; }
        }
        if (!known) return false;
        glyph_begin += run.glyph_count;
    }
    if (glyph_begin != geometry.glyph_count) return false;
    for (std::size_t index = 0U; index < geometry.glyph_count; ++index) {
        const ShapedGlyph& glyph = geometry.glyphs[index];
        if (glyph.cluster.value() >= text_bytes || !std::isfinite(glyph.x) ||
            !std::isfinite(glyph.y) || !std::isfinite(glyph.advance_x) ||
            !std::isfinite(glyph.advance_y)) return false;
    }
    return true;
}

} // namespace

PreparedWindowShaper::PreparedWindowShaper(std::shared_ptr<const detail::PreparedFontBank> fonts,
    const std::size_t owner_bytes)
    : fonts_(std::move(fonts)), owner_bytes_(owner_bytes) {}

PreparedTextStatus PreparedWindowShaper::initialize() {
    if (executor_ != std::this_thread::get_id()) return PreparedTextStatus::wrong_executor;
    if (engine_) return PreparedTextStatus::busy;
    if (!fonts_ || !(*fonts_).ledger || (*fonts_).face_count == 0U ||
        (*fonts_).face_count > PreparedTextLimits::font_faces) return PreparedTextStatus::invalid_input;
    if (owner_bytes_ > PreparedTextLimits::workspace_bytes - context_bytes)
        return PreparedTextStatus::budget_exceeded;
    const std::size_t reserved_context = context_bytes + owner_bytes_;
    {
        std::lock_guard<std::mutex> lock((*(*fonts_).ledger).mutex);
        if ((*(*fonts_).ledger).closing) return PreparedTextStatus::closing;
    }
    try {
        std::unique_ptr<HarfBuzzFontEngine> candidate = std::make_unique<HarfBuzzFontEngine>();
        ShapeStorageLimits limits{};
        limits.workspace_bytes -= reserved_context;
        const detail::PreparedFontBank& bank = *fonts_;
        const std::size_t registration = (*candidate).configure_bounded_registration(bank.face_count, limits.workspace_bytes);
        static_cast<void>(registration); // Already included by workspace preparation; do not charge twice.
        std::array<FontFaceId, PreparedTextLimits::font_faces> ids{};
        for (std::size_t index = 0U; index < bank.face_count; ++index) {
            const detail::PreparedFontFace& face = bank.faces[index];
            const std::span<const std::byte> encoded(face.bytes.get(), face.size);
            const std::optional<FontFaceId> id = face.family.value != 0U
                ? (*candidate).register_owned_family_typeface(face.family, face.weight, face.italic, encoded, fonts_,
                                                              face.index)
                : (*candidate).register_owned_typeface(face.role, face.weight, face.italic, encoded, fonts_,
                                                       face.index);
            if (!id) return PreparedTextStatus::incompatible_font;
            ids[index] = *id;
        }
        (*candidate).prepare_workspace(workspace_, limits);
        std::lock_guard<std::mutex> lock((*bank.ledger).mutex);
        if ((*bank.ledger).closing) return PreparedTextStatus::closing;
        face_ids_ = ids;
        engine_ = std::move(candidate);
    } catch (const std::length_error&) { return PreparedTextStatus::budget_exceeded; }
    catch (const std::bad_alloc&) { return PreparedTextStatus::resource_failure; }
    catch (...) { return PreparedTextStatus::native_failure; }
    return PreparedTextStatus::success;
}

PreparedTextStatus PreparedWindowShaper::shape(detail::PreparedWindowBatchStorage& batch) {
    if (executor_ != std::this_thread::get_id()) return PreparedTextStatus::wrong_executor;
    if (batch.geometry) return PreparedTextStatus::busy;
    if (!engine_ || !batch.input || !batch.rows || batch.row_count == 0U ||
        batch.row_count > 512U) return PreparedTextStatus::invalid_input;
    // initialize established that this sum fits the allowance; both fields
    // are fixed for the lifetime of the worker context.
    const std::size_t reserved_context = context_bytes + owner_bytes_;
    if (batch.fonts != fonts_ || batch.ledger != (*fonts_).ledger ||
        batch.key.text.font_set != (*fonts_).identity ||
        batch.key.text.font_generation != (*fonts_).generation) return PreparedTextStatus::incompatible_font;
    bool matching_primary = false;
    for (std::size_t index = 0U; index < (*fonts_).face_count; ++index) {
        const detail::PreparedFontFace& face = (*fonts_).faces[index];
        if (face.role && *face.role == batch.key.text.font.role &&
            face.weight == batch.key.text.font.weight && face.italic == batch.key.text.font.italic)
            matching_primary = true;
    }
    if (!matching_primary) return PreparedTextStatus::incompatible_font;
    PreparedTextStatus current = detail::prepared_window_worker_current(batch);
    if (current != PreparedTextStatus::success) return current;
    const detail::PreparedWindowInputData& input = *batch.input;
    if (batch.row_count != input.paragraph_count || !detail::same_prepared_window_key(batch.key, input.key))
        return PreparedTextStatus::invalid_input;
    constexpr std::size_t payload_limit = PreparedTextLimits::payload_bytes;
    if (batch.requested_bytes > payload_limit ||
        batch.row_count > (payload_limit - batch.requested_bytes) / sizeof(detail::PreparedWindowGeometryRow))
        return PreparedTextStatus::budget_exceeded;
    const std::size_t table_bytes = batch.row_count * sizeof(detail::PreparedWindowGeometryRow);
    std::size_t bytes = batch.requested_bytes + table_bytes;
    std::size_t runs = 0U;
    std::size_t glyphs = 0U;
    std::size_t workspace_peak = 0U;
    double width_device = 0.0;
    double height_device = 0.0;
    const ShapeStorageLimits ceiling{};
    try {
        std::unique_ptr<detail::PreparedWindowGeometryRow[]> rows =
            std::make_unique<detail::PreparedWindowGeometryRow[]>(batch.row_count);
        const PreparedTextKey& key = batch.key.text;
        const PreparedTextMetrics base_metrics = detail::prepared_device_metrics(key.font, key.scale);
        FontSpec device_font = key.font;
        device_font.size = static_cast<double>(base_metrics.device_size_26_6) / 64.0;
        device_font.letter_spacing *= key.scale;
        // Invariant font/scale: one exact-primary metrics lookup for the whole
        // batch, including empty rows. Native sizing is never done for placement
        // inside the row loop; ordinary glyph shaping retains its own setup.
        const std::optional<PrimaryLineMetrics> primary = (*engine_).primary_line_metrics(device_font);
        if (!primary) return PreparedTextStatus::incompatible_font;
        const PrimaryLineMetrics& line = *primary;
        if (!std::isfinite(line.ascent_device) || !std::isfinite(line.descent_device) ||
            !std::isfinite(line.line_gap_device) || line.ascent_device < 0.0 || line.descent_device < 0.0 ||
            line.line_gap_device < 0.0) return PreparedTextStatus::invalid_geometry;
        const std::span<const FontFaceId> faces(face_ids_.data(), (*fonts_).face_count);
        for (std::size_t index = 0U; index < batch.row_count; ++index) {
            current = detail::prepared_window_worker_current(batch);
            if (current != PreparedTextStatus::success) return current;
            const std::size_t paragraph_index = batch.rows[index].paragraph_index;
            if (paragraph_index != index) return PreparedTextStatus::invalid_input;
            const detail::PreparedWindowParagraph& paragraph = input.paragraphs[paragraph_index];
            if (paragraph.display_begin.value < key.display_begin.value ||
                paragraph.display_end.value < paragraph.display_begin.value) return PreparedTextStatus::invalid_input;
            const std::size_t begin = paragraph.display_begin.value - key.display_begin.value;
            const std::size_t length = paragraph.display_end.value - paragraph.display_begin.value;
            if (begin > input.display_bytes || length > input.display_bytes - begin) return PreparedTextStatus::invalid_input;
            std::string_view text{};
            if (length != 0U) text = std::string_view(input.display.get() + begin, length);
            ShapeStorageLimits limits{};
            limits.output_bytes = payload_limit - bytes;
            limits.runs = ceiling.runs - runs;
            limits.glyphs = ceiling.glyphs - glyphs;
            limits.workspace_bytes -= reserved_context;
            std::unique_ptr<BoundedShapedText> shaped = (*engine_).shape_with_workspace(text, device_font, limits, workspace_);
            current = detail::prepared_window_worker_current(batch);
            if (current != PreparedTextStatus::success) return current;
            BoundedShapedText& geometry = *shaped;
            if (geometry.missing_primary_face || geometry.missing_clusters != 0U) return PreparedTextStatus::missing_font_coverage;
            if (!finite_geometry(geometry, length, faces)) return PreparedTextStatus::invalid_geometry;
            if (geometry.ascent < 0.0 || geometry.descent < 0.0) return PreparedTextStatus::invalid_geometry;
            const double ascent = std::max(line.ascent_device, geometry.ascent);
            const double descent = std::max(line.descent_device, geometry.descent);
            const double line_height = std::max(geometry.height, ascent + descent + line.line_gap_device);
            const double baseline = height_device + ascent;
            const double next_top = height_device + line_height;
            if (!std::isfinite(line_height) || line_height <= 0.0 || !std::isfinite(baseline) ||
                !std::isfinite(next_top) || next_top <= height_device || baseline > next_top)
                return PreparedTextStatus::invalid_geometry;
            detail::PreparedWindowGeometryRow& row = rows[index];
            row.paragraph_index = paragraph_index;
            row.top_device = height_device;
            row.baseline_device = baseline;
            row.height_device = line_height;
            height_device = next_top;
            width_device = std::max(width_device, geometry.width);
            row.metrics = base_metrics;
            row.metrics.advance_dip = geometry.width / key.scale;
            row.metrics.height_dip = geometry.height / key.scale;
            row.metrics.ascent_dip = geometry.ascent / key.scale;
            row.metrics.descent_dip = geometry.descent / key.scale;
            row.run_count = geometry.run_count;
            row.glyph_count = geometry.glyph_count;
            runs += geometry.run_count;
            glyphs += geometry.glyph_count;
            // shape_with_workspace precharged its temporary owner and arrays.
            // Arrays transfer without copying; the temporary owner retires here.
            bytes += geometry.controlled_output_bytes - sizeof(BoundedShapedText);
            workspace_peak = std::max(workspace_peak, geometry.controlled_workspace_peak + reserved_context);
            row.runs = std::move(geometry.runs);
            row.glyphs = std::move(geometry.glyphs);
        }
        std::lock_guard<std::mutex> authority_lock((*batch.authority).mutex);
        std::lock_guard<std::mutex> ledger_lock((*batch.ledger).mutex);
        current = detail::prepared_window_worker_current_locked(batch);
        if (current != PreparedTextStatus::success) return current;
        batch.geometry = std::move(rows);
        batch.face_ids = face_ids_;
        batch.run_count = runs;
        batch.glyph_count = glyphs;
        batch.workspace_peak_bytes = workspace_peak;
        batch.requested_bytes = bytes;
        batch.width_device = width_device;
        batch.height_device = height_device;
    } catch (const std::length_error&) { return PreparedTextStatus::budget_exceeded; }
    catch (const std::bad_alloc&) { return PreparedTextStatus::resource_failure; }
    catch (...) { return PreparedTextStatus::native_failure; }
    return PreparedTextStatus::success;
}

} // namespace gui_forms::render::text
