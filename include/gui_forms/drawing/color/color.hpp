#pragma once

#include "gui_forms/drawing/geometry/drawing_geometry.hpp"

#include <cstdint>
#include <string_view>

namespace gui_drawing {

class Color final {
public:
    constexpr Color() noexcept = default;

    [[nodiscard]] static constexpr Color empty() noexcept { return {}; }
    [[nodiscard]] static constexpr Color from_argb(std::uint32_t argb) noexcept {
        return Color(argb, false, false);
    }
    [[nodiscard]] static constexpr Color from_argb(std::uint8_t alpha,
                                                    std::uint8_t red,
                                                    std::uint8_t green,
                                                    std::uint8_t blue) noexcept {
        return from_argb((static_cast<std::uint32_t>(alpha) << 24U) |
                         (static_cast<std::uint32_t>(red) << 16U) |
                         (static_cast<std::uint32_t>(green) << 8U) | blue);
    }
    [[nodiscard]] static constexpr Color from_rgb(std::uint8_t red,
                                                   std::uint8_t green,
                                                   std::uint8_t blue) noexcept {
        return from_argb(255U, red, green, blue);
    }
    [[nodiscard]] static constexpr Color with_alpha(std::uint8_t alpha,
                                                     Color color) noexcept {
        return color.empty_ ? Color::empty() :
            Color((static_cast<std::uint32_t>(alpha) << 24U) |
                  (color.argb_ & UINT32_C(0x00ffffff)), false, color.known_);
    }

    [[nodiscard]] static Color from_name(std::string_view name);
    [[nodiscard]] static Color from_html(std::string_view value);

    [[nodiscard]] constexpr bool is_empty() const noexcept { return empty_; }
    [[nodiscard]] constexpr bool is_known() const noexcept { return known_; }
    [[nodiscard]] constexpr std::uint32_t argb() const noexcept { return argb_; }
    [[nodiscard]] constexpr std::uint8_t alpha() const noexcept {
        return static_cast<std::uint8_t>(argb_ >> 24U);
    }
    [[nodiscard]] constexpr std::uint8_t red() const noexcept {
        return static_cast<std::uint8_t>(argb_ >> 16U);
    }
    [[nodiscard]] constexpr std::uint8_t green() const noexcept {
        return static_cast<std::uint8_t>(argb_ >> 8U);
    }
    [[nodiscard]] constexpr std::uint8_t blue() const noexcept {
        return static_cast<std::uint8_t>(argb_);
    }
    [[nodiscard]] double brightness() const noexcept;

    friend constexpr bool operator==(const Color& left,
                                     const Color& right) noexcept {
        return left.argb_ == right.argb_ && left.empty_ == right.empty_ &&
               left.known_ == right.known_;
    }

private:
    constexpr Color(std::uint32_t argb, bool empty, bool known) noexcept
        : argb_(argb), empty_(empty), known_(known) {}

    [[nodiscard]] static constexpr Color known(std::uint32_t argb) noexcept {
        return Color(argb, false, true);
    }

    std::uint32_t argb_{};
    bool empty_{true};
    bool known_{};
};

// Explicit color-space values used by editors and image applications. sRGB
// means IEC 61966-2-1 transfer/primaries with D65; XYZ values are normalized
// so reference white Y is 1. Alpha is always straight and normalized.
struct LinearSrgb final {
    double red{};
    double green{};
    double blue{};
    double alpha{1.0};
    friend constexpr bool operator==(const LinearSrgb& left,
                                     const LinearSrgb& right) noexcept {
        return left.red == right.red && left.green == right.green &&
               left.blue == right.blue && left.alpha == right.alpha;
    }
};

struct XyzD65 final {
    double x{};
    double y{};
    double z{};
    double alpha{1.0};
    friend constexpr bool operator==(const XyzD65& left,
                                     const XyzD65& right) noexcept {
        return left.x == right.x && left.y == right.y && left.z == right.z &&
               left.alpha == right.alpha;
    }
};

struct Oklab final {
    double lightness{};
    double a{};
    double b{};
    double alpha{1.0};
    friend constexpr bool operator==(const Oklab& left,
                                     const Oklab& right) noexcept {
        return left.lightness == right.lightness && left.a == right.a &&
               left.b == right.b && left.alpha == right.alpha;
    }
};

struct Oklch final {
    double lightness{};
    double chroma{};
    double hue_degrees{};
    double alpha{1.0};
    friend constexpr bool operator==(const Oklch& left,
                                     const Oklch& right) noexcept {
        return left.lightness == right.lightness &&
               left.chroma == right.chroma &&
               left.hue_degrees == right.hue_degrees &&
               left.alpha == right.alpha;
    }
};

struct SrgbConversion final {
    Color color;
    LinearSrgb unclamped;
    bool in_gamut{};
    bool clipped{};
};

struct OklchGamutMapping final {
    Oklch requested;
    Oklch mapped;
    SrgbConversion srgb;
};

[[nodiscard]] LinearSrgb srgb_to_linear(Color color);
// The WCAG 2 contrast ratio of two colours' sRGB channels, from 1 (equal) to
// 21 (black and white). Alpha is ignored; an empty colour counts as black.
[[nodiscard]] double contrast_ratio(Color first, Color second);
// The contrast rule for text over a fill: of `light` and `dark`, the one
// with the greater contrast ratio against `background` (dark on a tie).
[[nodiscard]] Color contrasting_text_color(Color background, Color light, Color dark);
[[nodiscard]] SrgbConversion linear_to_srgb(LinearSrgb color);
[[nodiscard]] XyzD65 linear_srgb_to_xyz_d65(LinearSrgb color);
[[nodiscard]] LinearSrgb xyz_d65_to_linear_srgb(XyzD65 color);
[[nodiscard]] Oklab linear_srgb_to_oklab(LinearSrgb color);
[[nodiscard]] LinearSrgb oklab_to_linear_srgb(Oklab color);
[[nodiscard]] Oklab xyz_d65_to_oklab(XyzD65 color);
[[nodiscard]] XyzD65 oklab_to_xyz_d65(Oklab color);
[[nodiscard]] Oklch oklab_to_oklch(Oklab color);
[[nodiscard]] Oklab oklch_to_oklab(Oklch color);
[[nodiscard]] SrgbConversion oklch_to_srgb(Oklch color);
// Reduces OKLCH chroma only, preserving lightness, hue, and alpha. The mapped
// value is deterministic to 24 binary-search iterations and reports both the
// requested and committed colors so a dialog can disclose gamut mapping.
[[nodiscard]] OklchGamutMapping map_oklch_to_srgb_gamut(Oklch color);

struct SystemPalette final {
    Color control{Color::from_rgb(240, 240, 240)};
    Color control_light{Color::from_rgb(255, 255, 255)};
    Color control_text{Color::from_rgb(0, 0, 0)};
};

[[nodiscard]] const SystemPalette& default_system_palette() noexcept;

} // namespace gui_drawing
