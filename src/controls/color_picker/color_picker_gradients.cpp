#include "color_picker_gradients.hpp"

#include "gui_forms/types/painter/painter.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace gui_forms::color_picker_detail {

namespace {

// The checkerboard's two squares: content colours, the backdrop a
// translucent colour is judged against, the same in every theme (as
// ColorValueEditor's swatch).
constexpr Color checker_light = Color::rgba(232, 232, 232);
constexpr Color checker_dark = Color::rgba(178, 178, 178);

// The hue's pure colour (saturation and brightness 1).
Color pure_hue(double hue) noexcept {
    const double sector = std::fmod(std::max(0.0, hue), 360.0) / 60.0;
    const double whole = std::floor(sector);
    const double rising = sector - whole;
    const std::uint8_t up = static_cast<std::uint8_t>(std::lround(rising * 255.0));
    const std::uint8_t down = static_cast<std::uint8_t>(255 - up);
    switch (static_cast<int>(whole)) {
    case 0: return Color::rgba(255, up, 0);
    case 1: return Color::rgba(down, 255, 0);
    case 2: return Color::rgba(0, 255, up);
    case 3: return Color::rgba(0, down, 255);
    case 4: return Color::rgba(up, 0, 255);
    default: return Color::rgba(255, 0, down);
    }
}

// Fills `area` with the checkerboard; the cells are clipped to it.
void paint_checkerboard(Painter& painter, Rect area, double cell) {
    if (area.width <= 0.0 || area.height <= 0.0 || cell <= 0.0) return;
    painter.fill_rect(area, checker_light);
    const std::size_t columns = static_cast<std::size_t>(std::ceil(area.width / cell));
    const std::size_t rows = static_cast<std::size_t>(std::ceil(area.height / cell));
    for (std::size_t row = 0U; row < rows; ++row) {
        for (std::size_t column = (row % 2U == 0U) ? 1U : 0U; column < columns; column += 2U) {
            const double x = area.x + static_cast<double>(column) * cell;
            const double y = area.y + static_cast<double>(row) * cell;
            const Rect square{x, y, std::min(cell, area.x + area.width - x), std::min(cell, area.y + area.height - y)};
            painter.fill_rect(square, checker_dark);
        }
    }
}

} // namespace

void paint_field(Painter& painter, Rect area, double hue) {
    if (area.width <= 0.0 || area.height <= 0.0) return;
    const Color white = Color::rgba(255, 255, 255);
    const std::array<GradientStop, 2> across{{{0.0, white}, {1.0, pure_hue(hue)}}};
    painter.fill_linear_gradient(area, Point{area.x, area.y}, Point{area.x + area.width, area.y}, across);
    const std::array<GradientStop, 2> down{{{0.0, Color::rgba(0, 0, 0, 0)}, {1.0, Color::rgba(0, 0, 0)}}};
    painter.fill_linear_gradient(area, Point{area.x, area.y}, Point{area.x, area.y + area.height}, down);
}

void paint_hue_track(Painter& painter, Rect area) {
    if (area.width <= 0.0 || area.height <= 0.0) return;
    std::array<GradientStop, 7> stops{};
    for (std::size_t index = 0U; index < stops.size(); ++index) {
        const double offset = static_cast<double>(index) / 6.0;
        stops[index] = GradientStop{offset, pure_hue(offset * 360.0)};
    }
    // 360 degrees is red again.
    stops[6].color = Color::rgba(255, 0, 0);
    painter.fill_linear_gradient(area, Point{area.x, area.y}, Point{area.x, area.y + area.height}, stops);
}

void paint_alpha_track(Painter& painter, Rect area, Color colour, double cell) {
    paint_checkerboard(painter, area, cell);
    if (area.width <= 0.0 || area.height <= 0.0) return;
    Color opaque = colour;
    opaque.alpha = 255;
    Color clear = colour;
    clear.alpha = 0;
    const std::array<GradientStop, 2> stops{{{0.0, opaque}, {1.0, clear}}};
    painter.fill_linear_gradient(area, Point{area.x, area.y}, Point{area.x, area.y + area.height}, stops);
}

void paint_swatch(Painter& painter, Rect area, Color colour, double cell) {
    paint_checkerboard(painter, area, cell);
    if (area.width <= 0.0 || area.height <= 0.0) return;
    painter.fill_rect(area, colour);
}

} // namespace gui_forms::color_picker_detail
