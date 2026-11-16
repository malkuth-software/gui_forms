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
bool coverage_ignorable(const std::uint32_t scalar) noexcept {
    const hb_unicode_general_category_t category = hb_unicode_general_category(hb_unicode_funcs_get_default(), scalar);
    const bool ignored = category == HB_UNICODE_GENERAL_CATEGORY_FORMAT ||
        (scalar >= 0xFE00 && scalar <= 0xFE0F) || (scalar >= 0xE0100 && scalar <= 0xE01EF);
    return ignored;
}
bool covers(const NativeFace& face, const std::string_view text) noexcept {
    std::size_t offset = 0;
    while (offset < text.size()) {
        const std::uint32_t scalar = decode_scalar(text, offset);
        if (coverage_ignorable(scalar)) continue;
        const FT_UInt glyph = FT_Get_Char_Index(face.value, scalar);
        if (glyph == 0) return false;
    }
    return true;
}
}

NativeLibrary::NativeLibrary() {
    const FT_Error status = FT_Init_FreeType(&value);
    if (status != 0 || value == nullptr) throw NativeFailure(TextMaskStatus::native_failure);
}
NativeLibrary::~NativeLibrary() { if (value != nullptr) FT_Done_FreeType(value); }
NativeFace::~NativeFace() {
    if (shaping != nullptr) hb_font_destroy(shaping);
    if (value != nullptr) FT_Done_Face(value);
}
NativeFonts::NativeFonts(const MaskKey& key) {
    const PreparedFontBank& bank = (*key.fonts).bank;
    count = bank.face_count;
    primary = key.options.primary_face;
    const double logical_size = static_cast<double>(key.options.size_64) / 64.0;
    for (std::size_t index = 0; index < count; ++index) {
        const PreparedFontFace& source = bank.faces[index];
        NativeFace& face = faces[index];
        const FT_Byte* bytes = reinterpret_cast<const FT_Byte*>(source.bytes.get());
        const FT_Long length = static_cast<FT_Long>(source.size);
        const FT_Long ordinal = static_cast<FT_Long>(source.index);
        const FT_Error opened = FT_New_Memory_Face(library.value, bytes, length, ordinal, &face.value);
        if (opened != 0 || face.value == nullptr) throw NativeFailure(TextMaskStatus::unsupported_profile);
        const FT_FaceRec& native = *face.value;
        if (!FT_IS_SCALABLE(face.value) || native.units_per_EM == 0 || native.num_glyphs <= 0 || native.num_glyphs > 1'000'000) {
            throw NativeFailure(TextMaskStatus::unsupported_profile);
        }
        const FT_Error charmap = FT_Select_Charmap(face.value, FT_ENCODING_UNICODE);
        const FT_Error sized = FT_Set_Char_Size(face.value, 0, static_cast<FT_F26Dot6>(key.options.size_64), 72, 72);
        if (charmap != 0 || sized != 0) throw NativeFailure(TextMaskStatus::unsupported_profile);
        const double units = static_cast<double>(native.units_per_EM);
        face.ascent = std::max(0.0, static_cast<double>(native.ascender) * logical_size / units);
        face.descent = std::max(0.0, -static_cast<double>(native.descender) * logical_size / units);
        face.shaping = hb_ft_font_create_referenced(face.value);
        if (face.shaping == nullptr || face.shaping == hb_font_get_empty() || hb_ft_font_get_ft_face(face.shaping) != face.value) {
            throw std::bad_alloc{};
        }
        hb_ft_font_set_load_flags(face.shaping, FT_LOAD_NO_HINTING | FT_LOAD_NO_AUTOHINT | FT_LOAD_NO_BITMAP);
        // The raster's face over the bank's typeface (opened with the bank, text_mask_service).
        const std::shared_ptr<const ProgramTypeface>& typeface = bank.typefaces[index];
        if (!typeface) throw NativeFailure(TextMaskStatus::unsupported_profile);
        FaceByteLimits limits{};
        limits.mask_width = 4096U;
        limits.mask_height = 4096U;
        if (!raster_faces[index].open(typeface, limits)) throw std::bad_alloc{};
    }
}
std::uint32_t NativeFonts::select_face(const std::string_view cluster) const {
    if (covers(faces[primary], cluster)) return primary;
    for (std::size_t index = 0; index < count; ++index) {
        if (index == primary) continue;
        if (covers(faces[index], cluster)) {
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
