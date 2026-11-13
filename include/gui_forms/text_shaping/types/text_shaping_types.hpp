#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/text.hpp"

#include <compare>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace gui_forms {

struct GUI_FORMS_API FontFaceId final {
    std::uint64_t value{};
    [[nodiscard]] constexpr bool valid() const noexcept { return value != 0; }
    friend constexpr bool operator==(const FontFaceId& left,
                                     const FontFaceId& right) noexcept {
        return left.value == right.value;
    }
    friend constexpr std::strong_ordering operator<=>(
        const FontFaceId& left, const FontFaceId& right) noexcept {
        return left.value <=> right.value;
    }
};
struct GUI_FORMS_API GlyphId final {
    std::uint32_t value{};
    friend constexpr bool operator==(const GlyphId& left,
                                     const GlyphId& right) noexcept {
        return left.value == right.value;
    }
    friend constexpr std::strong_ordering operator<=>(
        const GlyphId& left, const GlyphId& right) noexcept {
        return left.value <=> right.value;
    }
};
struct GUI_FORMS_API OpenTypeTag final {
    std::uint32_t value{};
    [[nodiscard]] constexpr bool valid() const noexcept { return value != 0; }
    friend constexpr bool operator==(const OpenTypeTag& left,
                                     const OpenTypeTag& right) noexcept {
        return left.value == right.value;
    }
    friend constexpr std::strong_ordering operator<=>(
        const OpenTypeTag& left, const OpenTypeTag& right) noexcept {
        return left.value <=> right.value;
    }
};

[[nodiscard]] constexpr OpenTypeTag open_type_tag(
    char first, char second, char third, char fourth) noexcept {
    return {static_cast<std::uint32_t>(static_cast<unsigned char>(first)) << 24U |
            static_cast<std::uint32_t>(static_cast<unsigned char>(second)) << 16U |
            static_cast<std::uint32_t>(static_cast<unsigned char>(third)) << 8U |
            static_cast<std::uint32_t>(static_cast<unsigned char>(fourth))};
}

enum class TextDirection : std::uint8_t { left_to_right, right_to_left };

struct GUI_FORMS_API ShapingFeature final {
    OpenTypeTag tag{};
    std::uint32_t value{};
    Utf8Range range{};
    friend constexpr bool operator==(const ShapingFeature& left,
                                     const ShapingFeature& right) noexcept {
        return left.tag == right.tag && left.value == right.value &&
               left.range == right.range;
    }
};
struct GUI_FORMS_API ShapingRequest final {
    std::string_view utf8_text;
    Utf8Range range{};
    FontFaceId face{};
    float font_size{};
    TextDirection direction{TextDirection::left_to_right};
    OpenTypeTag script{};
    std::string_view language;
    std::span<const ShapingFeature> features;
};
struct GUI_FORMS_API GlyphPlacement final {
    GlyphId glyph{};
    Utf8Offset cluster{};
    float advance_x{};
    float advance_y{};
    float offset_x{};
    float offset_y{};
    friend constexpr bool operator==(const GlyphPlacement& left,
                                     const GlyphPlacement& right) noexcept {
        return left.glyph == right.glyph && left.cluster == right.cluster &&
               left.advance_x == right.advance_x &&
               left.advance_y == right.advance_y &&
               left.offset_x == right.offset_x &&
               left.offset_y == right.offset_y;
    }
};
struct GUI_FORMS_API GlyphRun final {
    Utf8Range source_range{};
    FontFaceId face{};
    TextDirection direction{TextDirection::left_to_right};
    std::vector<GlyphPlacement> glyphs;
};

enum class ShapingRequestError : std::uint8_t {
    none, invalid_utf8, invalid_face, invalid_font_size, reversed_range,
    range_out_of_bounds, range_splits_scalar, range_splits_grapheme,
    invalid_feature_tag, feature_outside_range, feature_splits_scalar,
};
enum class GlyphRunError : std::uint8_t {
    none, invalid_request, mismatched_source_range, mismatched_face,
    mismatched_direction, cluster_out_of_range, cluster_splits_grapheme,
    nonmonotonic_clusters, nonfinite_placement,
};

[[nodiscard]] GUI_FORMS_API ShapingRequestError validate_shaping_request(
    const ShapingRequest& request);
[[nodiscard]] GUI_FORMS_API GlyphRunError validate_glyph_run(
    const ShapingRequest& request, const GlyphRun& run);
[[nodiscard]] GUI_FORMS_API std::string_view to_string(
    ShapingRequestError error) noexcept;
[[nodiscard]] GUI_FORMS_API std::string_view to_string(GlyphRunError error) noexcept;

struct GUI_FORMS_API FontFallbackRequest final {
    FontFaceId primary_face{};
    std::span<const FontFaceId> attempted_faces;
    std::span<const char32_t> cluster;
    std::string_view language;
    OpenTypeTag script{};
};
struct GUI_FORMS_API FontFallbackMatch final {
    FontFaceId face{};
    std::size_t covered_scalars{};
};

} // namespace gui_forms
