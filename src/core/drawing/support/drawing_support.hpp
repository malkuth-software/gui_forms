#pragma once

#include "gui_forms/drawing.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace gui_drawing {

// A bitmap's pixels: owned (bytes) or borrowed (borrowed, row_bytes apart,
// owned by the native object a borrowing bitmap wraps, which outlives it).
// Never copied while borrowed: a copy of a borrowed storage would alias the
// native memory (Bitmap::snapshot copies the pixels instead).
struct PixelStorage final {
    std::uint32_t width{};
    std::uint32_t height{};
    PixelFormat pixel_format{PixelFormat::bgra32_premultiplied};
    std::size_t row_bytes{};
    std::vector<std::byte> bytes;
    std::byte* borrowed{};

    [[nodiscard]] std::byte* data() noexcept { return borrowed != nullptr ? borrowed : bytes.data(); }
    [[nodiscard]] const std::byte* data() const noexcept { return borrowed != nullptr ? borrowed : bytes.data(); }
    [[nodiscard]] std::size_t size() const noexcept { return row_bytes * static_cast<std::size_t>(height); }
};

namespace {

std::atomic_uint64_t next_bitmap_id{1};

[[nodiscard]] std::int32_t clamp_i32(std::int64_t value) noexcept {
    return static_cast<std::int32_t>(std::clamp(
        value,
        static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::min()),
        static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max())));
}

void require_finite(double value, std::string_view field) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument(std::string(field) + " must be finite");
    }
}

void require_finite(PointF point, std::string_view field) {
    if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
        throw std::invalid_argument(std::string(field) + " must be finite");
    }
}

void require_finite(RectF rect, std::string_view field) {
    if (!rect.finite()) {
        throw std::invalid_argument(std::string(field) + " must be finite");
    }
}

[[nodiscard]] PointF point_on_ellipse(RectF bounds,
                                      double angle_degrees) noexcept {
    constexpr double degrees_to_radians =
        0.01745329251994329576923690768489;
    const double radians = angle_degrees * degrees_to_radians;
    return {bounds.x + bounds.width * 0.5 +
                bounds.width * 0.5 * std::cos(radians),
            bounds.y + bounds.height * 0.5 +
                bounds.height * 0.5 * std::sin(radians)};
}

[[nodiscard]] double ellipse_parameter_angle(
    double radius_x, double radius_y, double geometric_degrees) noexcept {
    constexpr double degrees_to_radians =
        0.01745329251994329576923690768489;
    const double geometric = geometric_degrees * degrees_to_radians;
    return std::atan2(radius_x * std::sin(geometric),
                      radius_y * std::cos(geometric));
}

[[nodiscard]] PointF ellipse_point(double center_x, double center_y,
                                   double radius_x, double radius_y,
                                   double parameter) noexcept {
    return {center_x + radius_x * std::cos(parameter),
            center_y + radius_y * std::sin(parameter)};
}

class RectBoundsAccumulator final {
public:
    void include(PointF point) noexcept {
        if (!any_) {
            left_ = right_ = point.x;
            top_ = bottom_ = point.y;
            any_ = true;
            return;
        }
        left_ = std::min(left_, point.x);
        top_ = std::min(top_, point.y);
        right_ = std::max(right_, point.x);
        bottom_ = std::max(bottom_, point.y);
    }

    void include(RectF rectangle) noexcept {
        if (rectangle.empty()) return;
        include(PointF{rectangle.left(), rectangle.top()});
        include(PointF{rectangle.right(), rectangle.bottom()});
    }

    [[nodiscard]] RectF bounds() const noexcept {
        return any_ ? RectF{left_, top_, right_ - left_, bottom_ - top_} :
                      RectF{};
    }

private:
    bool any_{};
    double left_{};
    double top_{};
    double right_{};
    double bottom_{};
};

[[nodiscard]] std::string ascii_lower(std::string_view input) {
    std::string result;
    result.reserve(input.size());
    for (const unsigned char value : input) {
        result.push_back(value >= 'A' && value <= 'Z'
            ? static_cast<char>(value + ('a' - 'A'))
            : static_cast<char>(value));
    }
    return result;
}

[[nodiscard]] int hex_digit(char value) noexcept {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

[[nodiscard]] std::uint32_t parse_hex(std::string_view value) {
    std::uint32_t result{};
    for (const char character : value) {
        const int digit = hex_digit(character);
        if (digit < 0) {
            throw std::invalid_argument("HTML color contains a non-hexadecimal digit");
        }
        result = (result << 4U) | static_cast<std::uint32_t>(digit);
    }
    return result;
}

[[nodiscard]] bool valid_utf8(std::string_view input) noexcept {
    std::size_t index{};
    while (index < input.size()) {
        const unsigned char lead = static_cast<unsigned char>(input[index]);
        if (lead <= 0x7fU) {
            ++index;
            continue;
        }
        std::size_t count{};
        std::uint32_t scalar{};
        std::uint32_t minimum{};
        if ((lead & 0xe0U) == 0xc0U) {
            count = 2;
            scalar = lead & 0x1fU;
            minimum = 0x80U;
        } else if ((lead & 0xf0U) == 0xe0U) {
            count = 3;
            scalar = lead & 0x0fU;
            minimum = 0x800U;
        } else if ((lead & 0xf8U) == 0xf0U) {
            count = 4;
            scalar = lead & 0x07U;
            minimum = 0x10000U;
        } else {
            return false;
        }
        if (index + count > input.size()) return false;
        for (std::size_t offset = 1; offset < count; ++offset) {
            const unsigned char continuation = static_cast<unsigned char>(input[index + offset]);
            if ((continuation & 0xc0U) != 0x80U) return false;
            scalar = (scalar << 6U) | (continuation & 0x3fU);
        }
        if (scalar < minimum || scalar > 0x10ffffU ||
            (scalar >= 0xd800U && scalar <= 0xdfffU)) {
            return false;
        }
        index += count;
    }
    return true;
}

[[nodiscard]] std::string number(double value) {
    if (value == 0.0) value = 0.0;
    std::array<char, 64> buffer{};
    const std::to_chars_result result = std::to_chars(buffer.data(), buffer.data() + buffer.size(),
                                      value, std::chars_format::general,
                                      std::numeric_limits<double>::max_digits10);
    if (result.ec != std::errc{}) {
        throw std::runtime_error("GUI.Drawing could not format a finite number");
    }
    return std::string(buffer.data(), result.ptr);
}

[[nodiscard]] std::string color_text(Color color) {
    if (color.is_empty()) return "empty";
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::hex << std::nouppercase << std::setfill('0') << std::setw(8)
           << color.argb();
    return output.str();
}

[[nodiscard]] std::string point_text(PointF point) {
    return number(point.x) + "," + number(point.y);
}

[[nodiscard]] std::string rect_text(RectF rect) {
    return number(rect.x) + "," + number(rect.y) + "," +
           number(rect.width) + "," + number(rect.height);
}

[[nodiscard]] std::string matrix_text(const Matrix& value) {
    return number(value.m11()) + "," + number(value.m12()) + "," +
           number(value.m21()) + "," + number(value.m22()) + "," +
           number(value.dx()) + "," + number(value.dy());
}

[[nodiscard]] const char* command_name(CommandKind kind) noexcept {
    switch (kind) {
    case CommandKind::save: return "save";
    case CommandKind::restore: return "restore";
    case CommandKind::translate: return "translate";
    case CommandKind::set_transform: return "set_transform";
    case CommandKind::set_clip: return "set_clip";
    case CommandKind::reset_clip: return "reset_clip";
    case CommandKind::set_quality: return "set_quality";
    case CommandKind::clear: return "clear";
    case CommandKind::fill_rectangle: return "fill_rectangle";
    case CommandKind::draw_rectangle: return "draw_rectangle";
    case CommandKind::draw_line: return "draw_line";
    case CommandKind::draw_string: return "draw_string";
    case CommandKind::draw_ellipse: return "draw_ellipse";
    case CommandKind::fill_ellipse: return "fill_ellipse";
    case CommandKind::fill_polygon: return "fill_polygon";
    case CommandKind::draw_path: return "draw_path";
    case CommandKind::fill_path: return "fill_path";
    case CommandKind::draw_image: return "draw_image";
    case CommandKind::draw_lines: return "draw_lines";
    case CommandKind::fill_rectangles: return "fill_rectangles";
    case CommandKind::draw_line_segments: return "draw_line_segments";
    case CommandKind::draw_markers: return "draw_markers";
    case CommandKind::fill_grid_pattern: return "fill_grid_pattern";
    case CommandKind::fill_proportion: return "fill_proportion";
    }
    return "unknown";
}

[[nodiscard]] std::string escaped(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (const unsigned char value : text) {
        switch (value) {
        case '\\': result += "\\\\"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        case '|': result += "\\|"; break;
        default: result.push_back(static_cast<char>(value)); break;
        }
    }
    return result;
}

void require_pen_width(double width) {
    require_finite(width, "pen width");
    if (width <= 0.0 || width > 1'000'000.0) {
        throw std::invalid_argument("pen width must be positive and bounded");
    }
}

void require_blend_positions(std::span<const double> positions,
                             std::size_t color_count) {
    if (positions.size() != color_count || positions.size() < 2U ||
        positions.size() > 4096U) {
        throw std::invalid_argument(
            "gradient colors and positions require equal bounded counts");
    }
    double previous = -1.0;
    for (std::size_t index = 0; index < positions.size(); ++index) {
        const double position = positions[index];
        require_finite(position, "gradient position");
        if (position < 0.0 || position > 1.0 ||
            (index != 0U && position <= previous)) {
            throw std::invalid_argument(
                "gradient positions must be strictly ordered in the unit interval");
        }
        previous = position;
    }
    if (positions.front() != 0.0 || positions.back() != 1.0) {
        throw std::invalid_argument("gradient positions must begin at zero and end at one");
    }
}

[[nodiscard]] std::uint8_t interpolate_channel(
    std::uint8_t left, std::uint8_t right, double amount) noexcept {
    return static_cast<std::uint8_t>(std::lround(
        static_cast<double>(left) +
        (static_cast<double>(right) - left) * amount));
}

[[nodiscard]] Color interpolate_color(Color first, Color second,
                                      double amount) noexcept {
    return Color::from_argb(
        interpolate_channel(first.alpha(), second.alpha(), amount),
        interpolate_channel(first.red(), second.red(), amount),
        interpolate_channel(first.green(), second.green(), amount),
        interpolate_channel(first.blue(), second.blue(), amount));
}

[[nodiscard]] std::uint8_t premultiply(std::uint8_t channel,
                                       std::uint8_t alpha) noexcept {
    return static_cast<std::uint8_t>(
        (static_cast<unsigned>(channel) * alpha + 127U) / 255U);
}

[[nodiscard]] std::uint8_t unpremultiply(std::uint8_t channel,
                                         std::uint8_t alpha) noexcept {
    if (alpha == 0U) return 0U;
    return static_cast<std::uint8_t>(std::min(
        255U, (static_cast<unsigned>(channel) * 255U + alpha / 2U) / alpha));
}

[[nodiscard]] std::uint8_t normalized_channel(double value) noexcept {
    if (value <= 0.0) return 0U;
    if (value >= 1.0) return 255U;
    return static_cast<std::uint8_t>(std::lround(value * 255.0));
}

[[nodiscard]] std::size_t pixel_offset(const PixelStorage& storage,
                                       std::uint32_t x,
                                       std::uint32_t y) noexcept {
    return static_cast<std::size_t>(y) * storage.row_bytes +
           static_cast<std::size_t>(x) * 4U;
}

void store_color(PixelStorage& storage, std::uint32_t x, std::uint32_t y,
                 Color color) noexcept {
    const std::size_t offset = pixel_offset(storage, x, y);
    const std::uint8_t alpha = color.is_empty() ? 0U : color.alpha();
    const std::uint8_t red = premultiply(color.red(), alpha);
    const std::uint8_t green = premultiply(color.green(), alpha);
    const std::uint8_t blue = premultiply(color.blue(), alpha);
    storage.data()[offset] = static_cast<std::byte>(blue);
    storage.data()[offset + 1U] = static_cast<std::byte>(green);
    storage.data()[offset + 2U] = static_cast<std::byte>(red);
    storage.data()[offset + 3U] = static_cast<std::byte>(alpha);
}

[[nodiscard]] Color load_color(const PixelStorage& storage,
                               std::uint32_t x,
                               std::uint32_t y) noexcept {
    const std::size_t offset = pixel_offset(storage, x, y);
    const std::uint8_t blue = std::to_integer<std::uint8_t>(storage.data()[offset]);
    const std::uint8_t green =
        std::to_integer<std::uint8_t>(storage.data()[offset + 1U]);
    const std::uint8_t red =
        std::to_integer<std::uint8_t>(storage.data()[offset + 2U]);
    const std::uint8_t alpha =
        std::to_integer<std::uint8_t>(storage.data()[offset + 3U]);
    return Color::from_argb(alpha, unpremultiply(red, alpha),
                            unpremultiply(green, alpha), unpremultiply(blue, alpha));
}

[[nodiscard]] bool stored_color_equals(const PixelStorage& storage,
                                       std::uint32_t x, std::uint32_t y,
                                       Color color) noexcept {
    const std::size_t offset = pixel_offset(storage, x, y);
    const std::uint8_t alpha = color.is_empty() ? 0U : color.alpha();
    const std::uint8_t red = premultiply(color.red(), alpha);
    const std::uint8_t green = premultiply(color.green(), alpha);
    const std::uint8_t blue = premultiply(color.blue(), alpha);
    return std::to_integer<std::uint8_t>(storage.data()[offset]) == blue &&
        std::to_integer<std::uint8_t>(storage.data()[offset + 1U]) == green &&
        std::to_integer<std::uint8_t>(storage.data()[offset + 2U]) == red &&
        std::to_integer<std::uint8_t>(storage.data()[offset + 3U]) == alpha;
}

template <typename Enum>
void require_enum(Enum value, unsigned maximum, std::string_view field) {
    using Underlying = std::underlying_type_t<Enum>;
    if (static_cast<unsigned>(static_cast<Underlying>(value)) > maximum) {
        throw std::invalid_argument(std::string(field) + " is outside the declared enum");
    }
}

[[nodiscard]] std::string pen_text(const PenSnapshot& pen) {
    std::string result = color_text(pen.color) + "," + number(pen.width) + "," +
                         std::to_string(static_cast<unsigned>(pen.dash_style));
    if (pen.dash_style == DashStyle::custom) {
        result += ",pattern=";
        for (std::size_t index = 0; index < pen.dash_pattern.size(); ++index) {
            if (index != 0U) result += ",";
            result += number(pen.dash_pattern[index]);
        }
    }
    // Written only when not the defaults, so older traces stay as they were.
    if (pen.join != LineJoin::miter || pen.miter_limit != 4.0) {
        result += ",join=" + std::to_string(static_cast<unsigned>(pen.join)) + "/" + number(pen.miter_limit);
    }
    if (pen.cap != LineCap::flat) result += ",cap=" + std::to_string(static_cast<unsigned>(pen.cap));
    return result;
}

[[nodiscard]] std::string brush_text(const BrushSnapshot& brush) {
    if (brush.kind == BrushKind::solid) return color_text(brush.primary);
    std::string result = std::to_string(static_cast<unsigned>(brush.kind)) + "," +
        color_text(brush.primary) + "," + color_text(brush.secondary) + "," +
        std::to_string(static_cast<unsigned>(brush.wrap_mode));
    if (brush.kind == BrushKind::hatch) {
        return result + ",hatch=" +
            std::to_string(static_cast<unsigned>(brush.hatch_style));
    }
    if (brush.kind == BrushKind::checker) {
        return result + ",checker=" + number(brush.cell_size);
    }
    if (brush.kind == BrushKind::texture) {
        return result + ",image=" + std::to_string(brush.image.stable_id) +
            "@" + std::to_string(brush.image.generation) + ",size=" +
            std::to_string(brush.image.width) + "x" +
            std::to_string(brush.image.height) + ",transform=" +
            matrix_text(brush.transform);
    }
    result += ",bounds=" + rect_text(brush.bounds) +
              ",center=" + point_text(brush.center) +
              ",angle=" + number(brush.angle) + ",stops=";
    for (std::size_t index = 0; index < brush.colors.size(); ++index) {
        if (index != 0U) result += ";";
        result += color_text(brush.colors[index]) + "@" +
                  number(brush.positions[index]);
    }
    return result;
}

[[nodiscard]] std::string points_text(std::span<const PointF> points) {
    std::string result;
    for (std::size_t index = 0; index < points.size(); ++index) {
        if (index != 0U) result += ";";
        result += point_text(points[index]);
    }
    return result;
}

// One path element's points or box, included in `accumulator`.
void include_path_element(const PathElement& element, RectBoundsAccumulator& accumulator) {
    switch (element.verb) {
    case PathVerb::line:
        accumulator.include(element.first);
        accumulator.include(element.second);
        return;
    case PathVerb::quadratic:
        accumulator.include(element.first);
        accumulator.include(element.second);
        accumulator.include(element.third);
        return;
    case PathVerb::bezier:
        accumulator.include(element.first);
        accumulator.include(element.second);
        accumulator.include(element.third);
        accumulator.include(element.fourth);
        return;
    case PathVerb::rectangle:
    case PathVerb::ellipse:
    case PathVerb::arc:
        accumulator.include(element.rect);
        return;
    case PathVerb::start_figure:
    case PathVerb::close_figure:
        // Figure marks carry no extent.
        return;
    }
    // A verb outside the enumeration has no extent.
    return;
}

// One path element's coordinates in a trace, appended to `result`.
void append_element_text(const PathElement& element, std::string& result) {
    switch (element.verb) {
    case PathVerb::line:
        result += "," + point_text(element.first) + "," + point_text(element.second);
        return;
    case PathVerb::quadratic:
        result += "," + point_text(element.first) + "," +
                  point_text(element.second) + "," + point_text(element.third);
        return;
    case PathVerb::bezier:
        result += "," + point_text(element.first) + "," +
                  point_text(element.second) + "," + point_text(element.third) +
                  "," + point_text(element.fourth);
        return;
    case PathVerb::rectangle:
    case PathVerb::ellipse:
        result += "," + rect_text(element.rect);
        return;
    case PathVerb::arc:
        result += "," + rect_text(element.rect) + "," +
                  number(element.start_angle) + "," +
                  number(element.sweep_angle);
        return;
    case PathVerb::start_figure:
    case PathVerb::close_figure:
        // Figure marks are traced by their verb alone.
        return;
    }
    // A verb outside the enumeration is traced by its number alone.
    return;
}

[[nodiscard]] std::string path_text(const PathSnapshot& path) {
    std::string result = std::to_string(static_cast<unsigned>(path.fill_mode));
    for (const PathElement& element : path.elements) {
        result += ";" + std::to_string(static_cast<unsigned>(element.verb));
        append_element_text(element, result);
    }
    return result;
}

[[nodiscard]] std::string image_attributes_text(
    const ImageAttributesSnapshot& attributes) {
    std::string result;
    if (attributes.color_matrix.has_value()) {
        const std::array<double, 25>& matrix = *attributes.color_matrix;
        result = "matrix:";
        for (std::size_t index = 0; index < matrix.size(); ++index) {
            if (index != 0U) result += ",";
            result += number(matrix[index]);
        }
    }
    if (!attributes.remap_table.empty()) {
        if (!result.empty()) result += ";";
        result += "remap:";
        for (std::size_t index = 0; index < attributes.remap_table.size(); ++index) {
            if (index != 0U) result += ",";
            result += color_text(attributes.remap_table[index].old_color) + ">" +
                      color_text(attributes.remap_table[index].new_color);
        }
    }
    if (result.empty()) return "identity";
    return result;
}

void include_polygon_visibility(std::vector<PointF>& figure, PointF point,
                                FillMode fill_mode, bool& inside) {
    if (figure.size() < 3U) {
        figure.clear();
        return;
    }
    bool hit = false;
    for (std::size_t first = 0U, previous = figure.size() - 1U;
         first < figure.size(); previous = first++) {
        const PointF a = figure[first];
        const PointF b = figure[previous];
        const bool crosses = ((a.y > point.y) != (b.y > point.y)) &&
            (point.x < (b.x - a.x) * (point.y - a.y) /
                           (b.y - a.y) + a.x);
        if (crosses) hit = !hit;
    }
    inside = fill_mode == FillMode::alternate ? inside != hit : inside || hit;
    figure.clear();
}

// One element's part in whether `point` is inside: outlines collect into
// `figure`, which a figure mark or a closed shape resolves into `inside`.
void include_element_visibility(const PathElement& element, PointF point, FillMode fill_mode,
                                std::vector<PointF>& figure, bool& inside) {
    bool primitive_hit = false;
    switch (element.verb) {
    case PathVerb::start_figure:
        include_polygon_visibility(
            figure, point, fill_mode, inside);
        return;
    case PathVerb::line:
        if (figure.empty()) figure.push_back(element.first);
        figure.push_back(element.second);
        return;
    case PathVerb::quadratic: {
        if (figure.empty() || figure.back() != element.first) {
            figure.push_back(element.first);
        }
        constexpr int segments = 24;
        for (int index = 1; index <= segments; ++index) {
            const double t = static_cast<double>(index) / segments;
            const double u = 1.0 - t;
            figure.push_back({u * u * element.first.x +
                                  2.0 * u * t * element.second.x +
                                  t * t * element.third.x,
                              u * u * element.first.y +
                                  2.0 * u * t * element.second.y +
                                  t * t * element.third.y});
        }
        return;
    }
    case PathVerb::bezier: {
        if (figure.empty() || figure.back() != element.first) {
            figure.push_back(element.first);
        }
        constexpr int segments = 32;
        for (int index = 1; index <= segments; ++index) {
            const double t = static_cast<double>(index) / segments;
            const double u = 1.0 - t;
            figure.push_back({u * u * u * element.first.x +
                                  3.0 * u * u * t * element.second.x +
                                  3.0 * u * t * t * element.third.x +
                                  t * t * t * element.fourth.x,
                              u * u * u * element.first.y +
                                  3.0 * u * u * t * element.second.y +
                                  3.0 * u * t * t * element.third.y +
                                  t * t * t * element.fourth.y});
        }
        return;
    }
    case PathVerb::rectangle:
        include_polygon_visibility(
            figure, point, fill_mode, inside);
        primitive_hit = element.rect.contains(point);
        inside = fill_mode == FillMode::alternate ?
            inside != primitive_hit : inside || primitive_hit;
        return;
    case PathVerb::ellipse: {
        include_polygon_visibility(
            figure, point, fill_mode, inside);
        const double rx = element.rect.width / 2.0;
        const double ry = element.rect.height / 2.0;
        if (rx > 0.0 && ry > 0.0) {
            const double dx = (point.x - (element.rect.x + rx)) / rx;
            const double dy = (point.y - (element.rect.y + ry)) / ry;
            primitive_hit = dx * dx + dy * dy <= 1.0;
        }
        inside = fill_mode == FillMode::alternate ?
            inside != primitive_hit : inside || primitive_hit;
        return;
    }
    case PathVerb::arc: {
        const PointF start =
            point_on_ellipse(element.rect, element.start_angle);
        if (figure.empty() || figure.back() != start) figure.push_back(start);
        const int segments = std::max(
            1, static_cast<int>(std::ceil(std::abs(element.sweep_angle) / 12.0)));
        for (int index = 1; index <= segments; ++index) {
            const double ratio = static_cast<double>(index) / segments;
            figure.push_back(point_on_ellipse(element.rect,
                element.start_angle + element.sweep_angle * ratio));
        }
        return;
    }
    case PathVerb::close_figure:
        include_polygon_visibility(
            figure, point, fill_mode, inside);
        return;
    }
    // A verb outside the enumeration adds nothing to the figure.
    return;
}

[[nodiscard]] bool path_snapshot_visible(const PathSnapshot& path, PointF point) {
    bool inside = false;
    std::vector<PointF> figure;
    for (const PathElement& element : path.elements) {
        include_element_visibility(element, point, path.fill_mode, figure, inside);
    }
    include_polygon_visibility(figure, point, path.fill_mode, inside);
    return inside;
}

} // namespace
