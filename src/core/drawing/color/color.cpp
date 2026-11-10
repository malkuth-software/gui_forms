#include "../support/drawing_support.hpp"

Color Color::from_name(std::string_view name) {
    struct Entry { std::string_view name; std::uint32_t argb; };
    static constexpr Entry entries[] = {
        {"aqua", 0xff00ffffU}, {"aquamarine", 0xff7fffd4U},
        {"azure", 0xfff0ffffU}, {"black", 0xff000000U},
        {"blue", 0xff0000ffU}, {"darkblue", 0xff00008bU},
        {"darkgray", 0xffa9a9a9U}, {"dodgerblue", 0xff1e90ffU},
        {"gainsboro", 0xffdcdcdcU}, {"gray", 0xff808080U},
        {"green", 0xff008000U}, {"khaki", 0xfff0e68cU},
        {"lightblue", 0xffadd8e6U}, {"lightcoral", 0xfff08080U},
        {"lightgreen", 0xff90ee90U}, {"lightpink", 0xffffb6c1U},
        {"lime", 0xff00ff00U}, {"limegreen", 0xff32cd32U},
        {"magenta", 0xffff00ffU}, {"maroon", 0xff800000U},
        {"orange", 0xffffa500U}, {"orangered", 0xffff4500U},
        {"orchid", 0xffda70d6U}, {"purple", 0xff800080U},
        {"red", 0xffff0000U}, {"salmon", 0xfffa8072U},
        {"silver", 0xffc0c0c0U}, {"springgreen", 0xff00ff7fU},
        {"transparent", 0x00ffffffU}, {"white", 0xffffffffU},
        {"yellow", 0xffffff00U}, {"yellowgreen", 0xff9acd32U},
    };
    const std::string key = ascii_lower(name);
    const Entry* found = std::begin(entries);
    while (found != std::end(entries) && (*found).name != key) {
        ++found;
    }
    return found == std::end(entries) ? Color::empty() :
        Color((*found).argb, false, true);
}

Color Color::from_html(std::string_view value) {
    if (value.empty()) return Color::empty();
    if (value.front() != '#') return from_name(value);
    const std::string_view digits = value.substr(1);
    if (digits.size() == 3) {
        const std::uint32_t compact = parse_hex(digits);
        const std::uint8_t red = static_cast<std::uint8_t>(((compact >> 8U) & 0xfU) * 17U);
        const std::uint8_t green = static_cast<std::uint8_t>(((compact >> 4U) & 0xfU) * 17U);
        const std::uint8_t blue = static_cast<std::uint8_t>((compact & 0xfU) * 17U);
        return from_rgb(red, green, blue);
    }
    if (digits.size() == 6) return from_argb(UINT32_C(0xff000000) | parse_hex(digits));
    if (digits.size() == 8) return from_argb(parse_hex(digits));
    throw std::invalid_argument("HTML color must have 3, 6, or 8 hexadecimal digits");
}

double Color::brightness() const noexcept {
    if (empty_) return 0.0;
    const unsigned char minimum = std::min({red(), green(), blue()});
    const unsigned char maximum = std::max({red(), green(), blue()});
    return (static_cast<double>(minimum) + maximum) / 510.0;
}

namespace {

void require_alpha(double alpha, std::string_view field) {
    require_finite(alpha, field);
    if (alpha < 0.0 || alpha > 1.0) {
        throw std::invalid_argument(std::string(field) +
                                    " must be in the unit interval");
    }
}

void require_linear(LinearSrgb color) {
    require_finite(color.red, "linear sRGB red");
    require_finite(color.green, "linear sRGB green");
    require_finite(color.blue, "linear sRGB blue");
    require_alpha(color.alpha, "linear sRGB alpha");
}

void require_xyz(XyzD65 color) {
    require_finite(color.x, "XYZ X");
    require_finite(color.y, "XYZ Y");
    require_finite(color.z, "XYZ Z");
    require_alpha(color.alpha, "XYZ alpha");
}

void require_oklab(Oklab color) {
    require_finite(color.lightness, "OKLab lightness");
    require_finite(color.a, "OKLab a");
    require_finite(color.b, "OKLab b");
    require_alpha(color.alpha, "OKLab alpha");
}

[[nodiscard]] double normalized_hue(double degrees) noexcept {
    double result = std::fmod(degrees, 360.0);
    if (result < 0.0) result += 360.0;
    return result == 360.0 ? 0.0 : result;
}

void require_oklch(Oklch color) {
    require_finite(color.lightness, "OKLCH lightness");
    require_finite(color.chroma, "OKLCH chroma");
    require_finite(color.hue_degrees, "OKLCH hue");
    require_alpha(color.alpha, "OKLCH alpha");
    if (color.chroma < 0.0) {
        throw std::invalid_argument("OKLCH chroma must be nonnegative");
    }
}

[[nodiscard]] double decode_srgb(double value) noexcept {
    return value <= 0.04045 ? value / 12.92 :
        std::pow((value + 0.055) / 1.055, 2.4);
}

[[nodiscard]] double encode_srgb(double value) noexcept {
    return value <= 0.0031308 ? value * 12.92 :
        1.055 * std::pow(value, 1.0 / 2.4) - 0.055;
}

} // namespace

LinearSrgb srgb_to_linear(Color color) {
    if (color.is_empty()) return {};
    return {decode_srgb(color.red() / 255.0),
            decode_srgb(color.green() / 255.0),
            decode_srgb(color.blue() / 255.0),
            color.alpha() / 255.0};
}

double contrast_ratio(Color first, Color second) {
    const LinearSrgb first_linear = srgb_to_linear(first);
    const LinearSrgb second_linear = srgb_to_linear(second);
    const double first_luminance =
        0.2126 * first_linear.red + 0.7152 * first_linear.green + 0.0722 * first_linear.blue;
    const double second_luminance =
        0.2126 * second_linear.red + 0.7152 * second_linear.green + 0.0722 * second_linear.blue;
    const double lighter = std::max(first_luminance, second_luminance);
    const double darker = std::min(first_luminance, second_luminance);
    const double ratio = (lighter + 0.05) / (darker + 0.05);
    return ratio;
}

Color contrasting_text_color(Color background, Color light, Color dark) {
    const double light_ratio = contrast_ratio(background, light);
    const double dark_ratio = contrast_ratio(background, dark);
    return light_ratio > dark_ratio ? light : dark;
}

SrgbConversion linear_to_srgb(LinearSrgb color) {
    require_linear(color);
    constexpr double epsilon = 5e-7;
    const bool in_gamut = color.red >= -epsilon && color.red <= 1.0 + epsilon &&
        color.green >= -epsilon && color.green <= 1.0 + epsilon &&
        color.blue >= -epsilon && color.blue <= 1.0 + epsilon;
    const double red = std::clamp(encode_srgb(color.red), 0.0, 1.0);
    const double green = std::clamp(encode_srgb(color.green), 0.0, 1.0);
    const double blue = std::clamp(encode_srgb(color.blue), 0.0, 1.0);
    return {Color::from_argb(normalized_channel(color.alpha),
                             normalized_channel(red),
                             normalized_channel(green),
                             normalized_channel(blue)),
            color, in_gamut, !in_gamut};
}

XyzD65 linear_srgb_to_xyz_d65(LinearSrgb color) {
    require_linear(color);
    return {
        0.4124564 * color.red + 0.3575761 * color.green +
            0.1804375 * color.blue,
        0.2126729 * color.red + 0.7151522 * color.green +
            0.0721750 * color.blue,
        0.0193339 * color.red + 0.1191920 * color.green +
            0.9503041 * color.blue,
        color.alpha};
}

LinearSrgb xyz_d65_to_linear_srgb(XyzD65 color) {
    require_xyz(color);
    return {
         3.2404542 * color.x - 1.5371385 * color.y - 0.4985314 * color.z,
        -0.9692660 * color.x + 1.8760108 * color.y + 0.0415560 * color.z,
         0.0556434 * color.x - 0.2040259 * color.y + 1.0572252 * color.z,
         color.alpha};
}

Oklab linear_srgb_to_oklab(LinearSrgb color) {
    require_linear(color);
    const double l = 0.4122214708 * color.red +
                     0.5363325363 * color.green +
                     0.0514459929 * color.blue;
    const double m = 0.2119034982 * color.red +
                     0.6806995451 * color.green +
                     0.1073969566 * color.blue;
    const double s = 0.0883024619 * color.red +
                     0.2817188376 * color.green +
                     0.6299787005 * color.blue;
    const double l_root = std::cbrt(l);
    const double m_root = std::cbrt(m);
    const double s_root = std::cbrt(s);
    return {
        0.2104542553 * l_root + 0.7936177850 * m_root -
            0.0040720468 * s_root,
        1.9779984951 * l_root - 2.4285922050 * m_root +
            0.4505937099 * s_root,
        0.0259040371 * l_root + 0.7827717662 * m_root -
            0.8086757660 * s_root,
        color.alpha};
}

LinearSrgb oklab_to_linear_srgb(Oklab color) {
    require_oklab(color);
    const double l_root = color.lightness + 0.3963377774 * color.a +
                          0.2158037573 * color.b;
    const double m_root = color.lightness - 0.1055613458 * color.a -
                          0.0638541728 * color.b;
    const double s_root = color.lightness - 0.0894841775 * color.a -
                          1.2914855480 * color.b;
    const double l = l_root * l_root * l_root;
    const double m = m_root * m_root * m_root;
    const double s = s_root * s_root * s_root;
    return {
         4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
        -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
        -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s,
        color.alpha};
}

Oklab xyz_d65_to_oklab(XyzD65 color) {
    return linear_srgb_to_oklab(xyz_d65_to_linear_srgb(color));
}

XyzD65 oklab_to_xyz_d65(Oklab color) {
    return linear_srgb_to_xyz_d65(oklab_to_linear_srgb(color));
}

Oklch oklab_to_oklch(Oklab color) {
    require_oklab(color);
    const double chroma = std::hypot(color.a, color.b);
    const double hue = chroma <= 1e-15 ? 0.0 : normalized_hue(
        std::atan2(color.b, color.a) * 180.0 / std::acos(-1.0));
    return {color.lightness, chroma, hue, color.alpha};
}

Oklab oklch_to_oklab(Oklch color) {
    require_oklch(color);
    const double radians = normalized_hue(color.hue_degrees) *
                           std::acos(-1.0) / 180.0;
    return {color.lightness, color.chroma * std::cos(radians),
            color.chroma * std::sin(radians), color.alpha};
}

SrgbConversion oklch_to_srgb(Oklch color) {
    return linear_to_srgb(oklab_to_linear_srgb(oklch_to_oklab(color)));
}

OklchGamutMapping map_oklch_to_srgb_gamut(Oklch color) {
    require_oklch(color);
    color.hue_degrees = normalized_hue(color.hue_degrees);
    SrgbConversion requested = oklch_to_srgb(color);
    if (requested.in_gamut) return {color, color, requested};

    Oklch neutral = color;
    neutral.chroma = 0.0;
    SrgbConversion neutral_srgb = oklch_to_srgb(neutral);
    if (!neutral_srgb.in_gamut) {
        return {color, neutral, neutral_srgb};
    }

    double low = 0.0;
    double high = color.chroma;
    Oklch mapped = neutral;
    SrgbConversion mapped_srgb = neutral_srgb;
    for (std::size_t iteration = 0; iteration < 24U; ++iteration) {
        const double candidate_chroma = (low + high) * 0.5;
        Oklch candidate = color;
        candidate.chroma = candidate_chroma;
        SrgbConversion converted = oklch_to_srgb(candidate);
        if (converted.in_gamut) {
            low = candidate_chroma;
            mapped = candidate;
            mapped_srgb = converted;
        } else {
            high = candidate_chroma;
        }
    }
    return {color, mapped, mapped_srgb};
}

const SystemPalette& default_system_palette() noexcept {
    static const SystemPalette palette{};
    return palette;
}


} // namespace gui_drawing
