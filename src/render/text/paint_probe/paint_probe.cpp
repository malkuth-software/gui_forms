#include "paint_probe.hpp"

#include <stxlib/glyph_face.hpp>
#include <stxlib/result.hpp>
#include <stxlib/types.hpp>
#include <stxlib/view.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <stdexcept>
#include <span>
#include <utility>
#include <vector>

namespace gui_forms::render::paint_probe {
namespace {
using namespace worker_probe;

Status validate_font_table(const Prepared& prepared, const FontSet& pinned) {
    if (!prepared.fonts) { return Status::missing_font; }
    const FontSet& supplied = *prepared.fonts;
    if (supplied.identity != pinned.identity || supplied.generation != pinned.generation ||
        prepared.identity.font_set != pinned.identity ||
        prepared.identity.font_generation != pinned.generation) { return Status::incompatible_face; }
    const std::size_t face_count = pinned.faces.size();
    for (std::size_t index = 0; index < face_count; ++index) {
        const FontBytes& expected = pinned.faces[index];
        const FontBytes& actual = supplied.faces[index];
        if (!actual.encoded || !expected.encoded || (*actual.encoded).empty()) { return Status::missing_font; }
        if (actual.role != expected.role || actual.weight != expected.weight ||
            actual.italic != expected.italic || actual.face_index != expected.face_index ||
            *actual.encoded != *expected.encoded) { return Status::incompatible_face; }
        if (!prepared.face_ids[index].valid()) { return Status::incompatible_face; }
        for (std::size_t prior = 0; prior < index; ++prior) {
            if (prepared.face_ids[prior] == prepared.face_ids[index]) { return Status::incompatible_face; }
        }
    }
    return Status::success;
}

std::size_t find_face(const Prepared& prepared, FontFaceId id) noexcept {
    const std::size_t count = prepared.face_ids.size();
    for (std::size_t index = 0; index < count; ++index) {
        if (prepared.face_ids[index] == id) { return index; }
    }
    return count;
}

using PainterFaces = std::array<gui_forms::detail::ExecutorFace, 4>;

// Opens the executor's faces over the program's typefaces of the pinned
// fonts, once; a face already open is kept.
Status open_faces(const FontSet& pinned, PainterFaces& faces) {
    for (std::size_t index = 0; index < faces.size(); ++index) {
        if (faces[index].is_open()) continue;
        const FontBytes& source = pinned.faces[index];
        const std::vector<std::byte>& bytes = *source.encoded;
        if (bytes.size() > 64U * 1024U * 1024U || source.face_index > 255U) { return Status::incompatible_face; }
        const std::shared_ptr<const gui_forms::detail::ProgramTypeface> typeface =
            gui_forms::detail::program_typeface(std::span<const std::byte>(bytes.data(), bytes.size()), source.encoded,
                                                source.face_index);
        if (!typeface) { return Status::native_failure; }
        const gui_forms::detail::FaceByteLimits limits{};
        if (!faces[index].open(typeface, limits)) { return Status::resource_failure; }
        const stxlib::glyphs::Font* const font = faces[index].font();
        if ((*font).glyph_count == 0U || (*font).character_map.length == 0U) { return Status::native_failure; }
    }
    return Status::success;
}

// Composites one run's coverage into the surface, clipped to it, by
// c + p * (255 - c) / 255.
void composite_run(const stxlib::glyphs::RunCoverage& coverage, std::vector<std::uint8_t>& pixels) {
    const stxlib::View<stxlib::u8> source = coverage.pixels.view();
    for (std::uint32_t row = 0; row < coverage.height; ++row) {
        const long target_y = static_cast<long>(coverage.top) + static_cast<long>(row);
        if (target_y < 0 || target_y >= static_cast<long>(Painter::height)) { continue; }
        const std::size_t source_row = static_cast<std::size_t>(row) * coverage.width;
        const std::size_t target_row = static_cast<std::size_t>(target_y) * Painter::width;
        for (std::uint32_t column = 0; column < coverage.width; ++column) {
            const long target_x = static_cast<long>(coverage.left) + static_cast<long>(column);
            if (target_x < 0 || target_x >= static_cast<long>(Painter::width)) { continue; }
            const std::size_t destination = target_row + static_cast<std::size_t>(target_x);
            const unsigned value = source[source_row + column];
            const unsigned prior = pixels[destination];
            const unsigned composite = value + prior * (255U - value) / 255U;
            pixels[destination] = static_cast<std::uint8_t>(composite);
        }
    }
}

Status rasterize(const Prepared& prepared, const FontSet& pinned, PainterFaces& faces, std::vector<std::uint8_t>& pixels) {
    const std::size_t face_count = faces.size();
    const Status opened = open_faces(pinned, faces);
    if (opened != Status::success) { return opened; }
    // Validate every run and glyph before producing any staged pixel.
    const std::size_t source_bytes = prepared.display_utf8.size();
    for (const text::ShapedFontRun& run : prepared.geometry.runs) {
        const std::size_t index = find_face(prepared, run.face);
        if (index == face_count) { return Status::incompatible_face; }
        if (run.source_range.start.value() > run.source_range.end.value() ||
            run.source_range.end.value() > source_bytes) { return Status::invalid_geometry; }
        const std::uint64_t glyph_limit = static_cast<std::uint64_t>((*faces[index].font()).glyph_count);
        for (const text::ShapedGlyph& glyph : run.glyphs) {
            if (glyph.glyph.value == 0 || glyph.glyph.value >= glyph_limit) { return Status::incompatible_glyph; }
            if (!std::isfinite(glyph.x) || !std::isfinite(glyph.y) ||
                !std::isfinite(glyph.advance_x) || !std::isfinite(glyph.advance_y) ||
                std::abs(glyph.x) > 1'000'000.0F || std::abs(glyph.y) > 1'000'000.0F ||
                glyph.cluster.value() < run.source_range.start.value() ||
                glyph.cluster.value() >= run.source_range.end.value()) { return Status::invalid_geometry; }
        }
    }
    // ADR-004's profile (light auto-hinting along y, grey), at the font's
    // size; only glyphs that can show on the surface are drawn.
    const double bounded_size = std::clamp(prepared.identity.font.size, 1.0, 4096.0);
    stxlib::glyphs::DrawStyle style{};
    style.size = static_cast<stxlib::i64>(std::llround(bounded_size * 64.0));
    style.fit = stxlib::glyphs::Fit::light;
    style.windowed = true;
    style.window_right = static_cast<std::int32_t>(Painter::width);
    style.window_bottom = static_cast<std::int32_t>(Painter::height);
    pixels.assign(Painter::width * Painter::height, 0);
    std::vector<stxlib::glyphs::RunGlyph> placed{};
    stxlib::glyphs::RunCoverage coverage{};
    for (const text::ShapedFontRun& run : prepared.geometry.runs) {
        const std::size_t index = find_face(prepared, run.face);
        placed.clear();
        for (const text::ShapedGlyph& glyph : run.glyphs) {
            // The run's origin is pixel (8, 64); positions are bounded to a
            // million pixels above, so 26.6 fits 32 bits.
            const double x = 8.0 + static_cast<double>(glyph.x);
            const double y = 64.0 + static_cast<double>(glyph.y);
            stxlib::glyphs::RunGlyph entry{};
            entry.glyph = glyph.glyph.value;
            entry.x = static_cast<std::int32_t>(std::llround(x * 64.0));
            entry.y = static_cast<std::int32_t>(std::llround(y * 64.0));
            placed.push_back(entry);
        }
        const stxlib::View<stxlib::glyphs::RunGlyph> glyphs(placed.data(), placed.size());
        const stxlib::Status drawn = faces[index].draw_run(glyphs, style, coverage);
        if (drawn.failed() || coverage.missing_glyphs != 0U) { return Status::native_failure; }
        if (coverage.oversized) { return Status::unsupported_raster; }
        composite_run(coverage, pixels);
    }
    return Status::success;
}
} // namespace

Painter::Painter(std::shared_ptr<const worker_probe::FontSet> pinned_fonts)
    : pinned_fonts_(std::move(pinned_fonts)), executor_(std::this_thread::get_id()) {
    if (!pinned_fonts_) { throw std::invalid_argument("Paint requires pinned fonts"); }
}

Status Painter::paint(const worker_probe::Prepared& prepared, const worker_probe::Identity& expected,
                      std::uint64_t authority_epoch) {
    if (std::this_thread::get_id() != executor_) { return Status::stale; }
    if (!worker_probe::same_identity(prepared.identity, expected) ||
        authority_epoch == 0 || prepared.authority_epoch != authority_epoch) { return Status::stale; }
    if (!valid_font_spec(expected.font) || expected.scale != 1.0 || expected.wrap_width != 0.0 ||
        expected.tab_columns != 4 || prepared.display_utf8.size() > 16'384 ||
        !validate_utf8(prepared.display_utf8).valid()) { return Status::invalid_geometry; }
    const Status fonts = validate_font_table(prepared, *pinned_fonts_);
    if (fonts != Status::success) { return fonts; }
    if (prepared.geometry.missing_primary_face || prepared.geometry.missing_clusters != 0) {
        return Status::missing_font;
    }
    const text::ShapedText& geometry = prepared.geometry;
    if (!std::isfinite(geometry.width) || !std::isfinite(geometry.height) ||
        !std::isfinite(geometry.ascent) || !std::isfinite(geometry.descent) ||
        geometry.width < 0 || geometry.height < 0 || geometry.ascent < 0 || geometry.descent < 0 ||
        (!prepared.display_utf8.empty() && geometry.runs.empty())) { return Status::invalid_geometry; }
    if (geometry.runs.size() > 16'384) { return Status::invalid_geometry; }
    std::size_t glyph_count = 0;
    for (const text::ShapedFontRun& run : geometry.runs) {
        const std::size_t count = run.glyphs.size();
        if (count > 65'536 - glyph_count) { return Status::invalid_geometry; }
        glyph_count += count;
    }
    try {
        std::vector<std::uint8_t> staged{};
        const Status painted = rasterize(prepared, *pinned_fonts_, faces_, staged);
        if (painted != Status::success) { return painted; }
        pixels_ = std::move(staged);
        painted_identity_ = prepared.identity;
        return Status::success;
    } catch (const std::bad_alloc&) {
        return Status::resource_failure;
    } catch (const std::runtime_error&) {
        return Status::native_failure;
    }
}
const std::vector<std::uint8_t>& Painter::pixels() const noexcept { return pixels_; }
const worker_probe::Identity& Painter::painted_identity() const noexcept { return painted_identity_; }
} // namespace gui_forms::render::paint_probe
