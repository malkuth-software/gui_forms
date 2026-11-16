#include "text_mask_native.hpp"
#include <algorithm>
#include <cmath>

namespace gui_forms::detail::mask_native {
namespace {
std::uint32_t decode_scalar(const std::string_view text, std::size_t& offset) noexcept {
    const unsigned char first = static_cast<unsigned char>(text[offset]);
    ++offset;
    std::uint32_t scalar = first;
    std::size_t following = 0;
    if (first >= 0xF0) { scalar = first & 7U; following = 3; }
    else if (first >= 0xE0) { scalar = first & 15U; following = 2; }
    else if (first >= 0xC0) { scalar = first & 31U; following = 1; }
    for (std::size_t index = 0; index < following; ++index) {
        scalar = (scalar << 6U) | (static_cast<unsigned char>(text[offset]) & 63U);
        ++offset;
    }
    return scalar;
}
// The scalars coverage passes over (as the text engine's cluster coverage
// does): the joiners and the variation selectors.
bool coverage_ignorable(const std::uint32_t scalar) noexcept {
    const bool ignored = scalar == 0x200C || scalar == 0x200D || (scalar >= 0xFE00 && scalar <= 0xFE0F) ||
        (scalar >= 0xE0100 && scalar <= 0xE01EF);
    return ignored;
}
bool nominally_covers(const stxlib::glyphs::Font& font, const std::string_view text) noexcept {
    std::size_t offset = 0;
    while (offset < text.size()) {
        const std::uint32_t scalar = decode_scalar(text, offset);
        if (coverage_ignorable(scalar)) continue;
        const std::uint32_t glyph = stxlib::glyphs::glyph_of(font, scalar);
        if (glyph == 0) return false;
    }
    return true;
}
// Whether the job shapes with `font`: outlines, a glyph count in range,
// units per em and a Unicode character map.
bool shapeable(const stxlib::glyphs::Font& font) noexcept {
    const bool admitted = font.outlines != stxlib::glyphs::OutlineKind::none && font.glyph_count != 0 &&
        font.glyph_count <= 1'000'000 && font.units_per_em != 0 && font.character_map.length != 0 &&
        font.character_map_kind == stxlib::glyphs::character_map_unicode;
    return admitted;
}
}

namespace {

// The pool of the key's bank; NativeFailure(unsupported_profile) when the
// bank has none (STXlib refused one of its faces, text_mask_service).
PreparedFacePool& mask_face_pool(const MaskKey& key) {
    const std::shared_ptr<PreparedFacePool>& pool = (*key.fonts).bank.face_pool;
    if (!pool) throw NativeFailure(TextMaskStatus::unsupported_profile);
    return *pool;
}

} // namespace

NativeFonts::NativeFonts(const MaskKey& key) : raster(mask_face_pool(key)), raster_faces(raster.set().faces) {
    const PreparedFontBank& bank = (*key.fonts).bank;
    count = bank.face_count;
    primary = key.options.primary_face;
    size_64 = static_cast<std::int64_t>(key.options.size_64);
    const double logical_size = static_cast<double>(key.options.size_64) / 64.0;
    for (std::size_t index = 0; index < count; ++index) {
        NativeFace& face = faces[index];
        // The bank's typeface (opened with the bank, text_mask_service). No keeper: the bank
        // holds the typeface and the key holds the bank for the job.
        const std::shared_ptr<const ProgramTypeface>& typeface = bank.typefaces[index];
        if (!typeface) throw NativeFailure(TextMaskStatus::unsupported_profile);
        const stxlib::Shared<stxlib::glyphs::Typeface>& shared = (*typeface).typeface();
        const stxlib::glyphs::Font* const font = (*shared).font();
        if (font == nullptr || !shapeable(*font)) throw NativeFailure(TextMaskStatus::unsupported_profile);
        face.font = font;
        const double units = static_cast<double>((*font).units_per_em);
        face.ascent = std::max(0.0, static_cast<double>((*font).ascender) * logical_size / units);
        face.descent = std::max(0.0, -static_cast<double>((*font).descender) * logical_size / units);
        const stxlib::Status opened = face.shaper.open(shared);
        if (opened.failed()) throw std::bad_alloc{};
        // The raster's face over the same typeface, unless an earlier job opened it in this
        // leased set.
        if (raster_faces[index].is_open()) continue;
        FaceByteLimits limits{};
        limits.mask_width = 4096U;
        limits.mask_height = 4096U;
        TypefaceBorrow borrow{};
        borrow.typeface = typeface.get();
        if (!raster_faces[index].open(std::move(borrow), limits)) throw std::bad_alloc{};
    }
}
bool NativeFonts::covers(const std::size_t face, const std::string_view cluster) {
    NativeFace& candidate = faces[face];
    if (nominally_covers(*candidate.font, cluster)) return true;
    coverage_characters.clear();
    std::size_t offset = 0;
    while (offset < cluster.size()) {
        const std::uint32_t scalar = decode_scalar(cluster, offset);
        const stxlib::Status pushed = coverage_characters.push_copy(scalar);
        if (pushed.failed()) throw std::bad_alloc{};
    }
    stxlib::glyphs::TextRun run{};
    run.characters = coverage_characters.view();
    const stxlib::Status shaped = candidate.shaper.shape(run, coverage_glyphs);
    if (shaped.failed()) throw NativeFailure(TextMaskStatus::native_failure);
    for (const stxlib::glyphs::ShapedGlyph& glyph : coverage_glyphs) {
        if (glyph.glyph == 0) return false;
    }
    return true;
}
std::uint32_t NativeFonts::select_face(const std::string_view cluster) {
    if (covers(primary, cluster)) return primary;
    for (std::size_t index = 0; index < count; ++index) {
        if (index == primary) continue;
        if (covers(index, cluster)) {
            const std::uint32_t ordinal = static_cast<std::uint32_t>(index);
            return ordinal;
        }
    }
    throw NativeFailure(TextMaskStatus::missing_font_coverage);
}
void NativeWork::admit(const std::size_t bytes) {
    if (shape_calls == 2048 || bytes > 4U * 1024U * 1024U - context_bytes) {
        throw MaskLimitFailure(TextMaskLimit::shape_work);
    }
    ++shape_calls;
    context_bytes += bytes;
}
} // namespace gui_forms::detail::mask_native
