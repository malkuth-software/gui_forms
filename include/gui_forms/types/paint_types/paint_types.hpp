#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/binding/type_tag/type_tag.hpp"

#include <cmath>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>

namespace gui_forms {

struct GUI_FORMS_API Color {
    std::uint8_t red{};
    std::uint8_t green{};
    std::uint8_t blue{};
    std::uint8_t alpha{255};
    friend constexpr bool operator==(const Color& left,
                                     const Color& right) noexcept {
        return left.red == right.red && left.green == right.green &&
               left.blue == right.blue && left.alpha == right.alpha;
    }

    [[nodiscard]] static constexpr Color rgba(std::uint8_t red_value,
                                               std::uint8_t green_value,
                                               std::uint8_t blue_value,
                                               std::uint8_t alpha_value = 255) noexcept {
        return {red_value, green_value, blue_value, alpha_value};
    }
};

// A rounded rectangle's four corner radii, in the rectangle's units:
// top-left, top-right, bottom-right and bottom-left. A painter draws each at
// most half the rectangle's shorter side.
struct GUI_FORMS_API PaintCornerRadii final {
    double top_left{};
    double top_right{};
    double bottom_right{};
    double bottom_left{};

    friend constexpr bool operator==(PaintCornerRadii left, PaintCornerRadii right) noexcept {
        return left.top_left == right.top_left && left.top_right == right.top_right &&
               left.bottom_right == right.bottom_right && left.bottom_left == right.bottom_left;
    }
};

// Renderer-neutral retained paint vocabulary.  Stops are deliberately copied
// into display chunks; callers may therefore supply stack-backed spans without
// extending their lifetime through presentation.
struct GUI_FORMS_API GradientStop final {
    double offset{};
    Color color{};
    friend constexpr bool operator==(const GradientStop& left,
                                     const GradientStop& right) noexcept {
        return left.offset == right.offset && left.color == right.color;
    }
};

// Controls how a linear gradient behaves outside its authored start/end
// interval. `repeat` is the retained material primitive behind pinstripes,
// grooves, scanlines, and other scale-independent surface texture; `reflect`
// mirrors alternate intervals so the seam remains continuous.
enum class GradientSpreadMode : std::uint8_t {
    pad,
    repeat,
    reflect,
};

inline constexpr std::size_t maximum_gradient_stops = 32U;

[[nodiscard]] GUI_FORMS_API bool valid_gradient_stops(
    std::span<const GradientStop> stops) noexcept;

enum class FontRole : std::uint8_t {
    control,
    content,
    monospace,
};

enum class CursorKind : std::uint8_t {
    arrow,
    text,
    hand,
    crosshair,
    resize_horizontal,
    resize_vertical,
    wait,
    forbidden,
    resize_diagonal_down,
    resize_diagonal_up,
};

namespace types {

inline constexpr TypeTag<CursorKind> cursor_kind{};

} // namespace types

// A bundled font family a theme names in place of a role's faces
// (theme-language `font-family: Selawik`). Zero is none: the role's own faces.
// Only families bundled with the library have numbers (the table in
// src/core/resources/fonts/bundled_font_families.hpp); a host font never has
// one, so a family can never reach a font the application did not ship.
struct GUI_FORMS_API BundledFontFamily {
    std::uint8_t value{};
    friend constexpr bool operator==(const BundledFontFamily& left,
                                     const BundledFontFamily& right) noexcept {
        return left.value == right.value;
    }
};

struct GUI_FORMS_API FontSpec {
    FontRole role{FontRole::control};
    double size{13.0};
    std::uint16_t weight{400};
    bool italic{};
    // Additional logical pixels inserted between shaped grapheme clusters.
    // This is a layout input: painters and measurement must apply the same
    // value. Zero preserves the typeface's native spacing.
    double letter_spacing{};
    // A named bundled family whose faces draw before the role's. The role
    // still chooses the faces for characters the family does not cover, and
    // a family whose faces could not be loaded leaves the role's faces in
    // charge.
    BundledFontFamily family{};
    friend constexpr bool operator==(const FontSpec& left,
                                     const FontSpec& right) noexcept {
        return left.role == right.role && left.size == right.size &&
               left.weight == right.weight && left.italic == right.italic &&
               left.letter_spacing == right.letter_spacing &&
               left.family == right.family;
    }
};

[[nodiscard]] inline bool valid_font_spec(FontSpec font) noexcept {
    return std::isfinite(font.size) && font.size > 0.0 &&
           std::isfinite(font.letter_spacing) &&
           font.letter_spacing >= -font.size * 0.25 &&
           font.letter_spacing <= font.size;
}

struct GUI_FORMS_API ImageId {
    std::uint64_t value{};
    friend constexpr bool operator==(const ImageId& left,
                                     const ImageId& right) noexcept {
        return left.value == right.value;
    }
    friend constexpr std::strong_ordering operator<=>(
        const ImageId& left, const ImageId& right) noexcept {
        return left.value <=> right.value;
    }
};

enum class ImagePatternWrap : std::uint8_t {
    tile,
};

enum class ImageSampling : std::uint8_t {
    nearest,
    linear,
};

// The direction a line of text reads in (design language 8.5; owner
// decision DK-35, question J4). The two vertical orientations are the
// horizontal run turned a quarter about its origin: `bottom_to_top` reads
// up the surface with the tops of the glyphs facing left (a left side
// tab), `top_to_bottom` reads down it with the tops facing right.
enum class TextOrientation : std::uint8_t {
    horizontal,
    bottom_to_top,
    top_to_bottom,
};

} // namespace gui_forms
