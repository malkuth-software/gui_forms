#include "range_control_rendering.hpp"

#include "gui_forms/controls/range_control/progress_bar/progress_bar.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace gui_forms::range_control_detail {

void require_finite(double value, const char* message) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument(message);
    }
}

void paint_sunken(Painter& painter, Rect bounds,
                  const BasicControlStyle& style, Color fill) {
    painter.fill_rect(bounds, fill);
    painter.draw_line({bounds.x, bounds.y},
                      {bounds.x + bounds.width, bounds.y}, style.dark_border, 1.0);
    painter.draw_line({bounds.x, bounds.y},
                      {bounds.x, bounds.y + bounds.height}, style.dark_border, 1.0);
    painter.draw_line({bounds.x, bounds.y + bounds.height - 1.0},
                      {bounds.x + bounds.width, bounds.y + bounds.height - 1.0},
                      style.highlight, 1.0);
    painter.draw_line({bounds.x + bounds.width - 1.0, bounds.y},
                      {bounds.x + bounds.width - 1.0, bounds.y + bounds.height},
                      style.highlight, 1.0);
}

void paint_thumb(Painter& painter, Rect bounds,
                 const BasicControlStyle& style, bool focused) {
    painter.fill_rect(bounds, style.face);
    painter.fill_rect({bounds.x + 1.0, bounds.y + 1.0,
                       std::max(0.0, bounds.width - 2.0),
                       std::max(0.0, (bounds.height - 2.0) * 0.42)},
                      style.face_light);
    painter.draw_line({bounds.x, bounds.y},
                      {bounds.x + bounds.width - 1.0, bounds.y},
                      style.highlight, 1.0);
    painter.draw_line({bounds.x, bounds.y},
                      {bounds.x, bounds.y + bounds.height - 1.0},
                      style.highlight, 1.0);
    painter.draw_line({bounds.x, bounds.y + bounds.height - 1.0},
                      {bounds.x + bounds.width - 1.0,
                       bounds.y + bounds.height - 1.0}, style.dark_border, 1.0);
    painter.draw_line({bounds.x + bounds.width - 1.0, bounds.y},
                      {bounds.x + bounds.width - 1.0,
                       bounds.y + bounds.height - 1.0}, style.dark_border, 1.0);
    if (focused && bounds.width > 4.0 && bounds.height > 4.0) {
        painter.stroke_rect({bounds.x + 2.0, bounds.y + 2.0,
                             bounds.width - 4.0, bounds.height - 4.0},
                            style.accent, 1.0);
    }
}

void paint_progress_stripes(Painter& painter, Rect fill, Color color,
                            double stripe_width, double phase) {
    if (fill.empty()) return;
    const double pitch = stripe_width * 2.0;
    const double travel = phase * pitch;
    painter.save();
    painter.clip_rect(fill);
    const double begin = fill.x - fill.height - pitch + travel;
    const double end = fill.x + fill.width + fill.height + pitch;
    for (double x = begin; x <= end; x += pitch) {
        painter.draw_line({x, fill.y + fill.height},
                          {x + fill.height, fill.y}, color, stripe_width);
    }
    painter.restore();
}

Rect progress_luminance_band(Rect fill, double extent, double phase) noexcept {
    const double band = std::max(8.0, fill.width * extent);
    const double x = fill.x - band + (fill.width + band) * phase;
    const Rect pulse{x, fill.y, band, fill.height};
    return pulse;
}

void paint_progress_luminance(Painter& painter, Rect fill, Color color,
                              double extent, double phase) {
    if (fill.empty()) return;
    const Rect pulse = progress_luminance_band(fill, extent, phase);
    const std::array<GradientStop, 5> stops{{
        {0.0, Color::rgba(color.red, color.green, color.blue, 0)},
        {0.22, Color::rgba(color.red, color.green, color.blue,
                           static_cast<std::uint8_t>(color.alpha / 3U))},
        {0.5, color},
        {0.78, Color::rgba(color.red, color.green, color.blue,
                           static_cast<std::uint8_t>(color.alpha / 3U))},
        {1.0, Color::rgba(color.red, color.green, color.blue, 0)},
    }};
    painter.save();
    painter.clip_rect(fill);
    painter.fill_linear_gradient(pulse, {pulse.x, pulse.y},
                                 {pulse.x + pulse.width, pulse.y}, stops);
    painter.restore();
}

void paint_progress_laser(Painter& painter, Rect fill, Rect interior,
                          Orientation orientation,
                          const ProgressBarAnimationAppearance& appearance,
                          double phase, bool reduced) {
    if (fill.empty()) return;
    const double pitch = appearance.laser_phase_pitch;
    const double offset = phase * pitch;
    const Color phase_color = appearance.laser_phase_color;
    const std::array<GradientStop, 5> phase_stops{{
        {0.0, Color::rgba(phase_color.red, phase_color.green,
                          phase_color.blue, 0)},
        {0.24, phase_color},
        {0.5, Color::rgba(255, 255, 255,
                          static_cast<std::uint8_t>(phase_color.alpha / 2U))},
        {0.76, phase_color},
        {1.0, Color::rgba(phase_color.red, phase_color.green,
                          phase_color.blue, 0)},
    }};
    painter.save();
    painter.clip_rect(fill);
    painter.fill_linear_gradient_spread(
        fill, {fill.x, fill.y - offset},
        {fill.x, fill.y - offset + pitch}, phase_stops,
        GradientSpreadMode::repeat);
    painter.restore();

    const double edge_extent = std::min(
        appearance.laser_edge_extent,
        orientation == Orientation::horizontal ? fill.width : fill.height);
    if (edge_extent <= 0.0) return;
    const Color edge = appearance.laser_edge_color;
    const std::array<GradientStop, 4> edge_stops{{
        {0.0, Color::rgba(edge.red, edge.green, edge.blue, 0)},
        {0.58, Color::rgba(edge.red, edge.green, edge.blue,
                           static_cast<std::uint8_t>(edge.alpha / 2U))},
        {0.86, edge},
        {1.0, appearance.laser_spark_color},
    }};
    if (orientation == Orientation::horizontal) {
        const Rect edge_rect{fill.x + fill.width - edge_extent, fill.y,
                             edge_extent, fill.height};
        painter.fill_linear_gradient(
            edge_rect, {edge_rect.x, edge_rect.y},
            {edge_rect.x + edge_rect.width, edge_rect.y}, edge_stops);
    } else {
        const Rect edge_rect{fill.x, fill.y, fill.width, edge_extent};
        painter.fill_linear_gradient(
            edge_rect, {edge_rect.x, edge_rect.y + edge_rect.height},
            {edge_rect.x, edge_rect.y}, edge_stops);
    }

    // The front is a compact white-hot burn, not a set of long particle
    // tendrils. A soft corona and a razor-bright core carry most of the energy;
    // the deterministic sparks are deliberately short ember flecks.
    constexpr double tau = 6.28318530717958647692;
    const Color spark = appearance.laser_spark_color;
    const std::array<GradientStop, 4> corona_stops{{
        {0.0, Color::rgba(255, 255, 255, 255)},
        {0.2, spark},
        {0.56, Color::rgba(edge.red, edge.green, edge.blue,
                           static_cast<std::uint8_t>(edge.alpha * 2U / 3U))},
        {1.0, Color::rgba(edge.red, edge.green, edge.blue, 0)},
    }};
    painter.save();
    painter.clip_rect(interior);
    if (orientation == Orientation::horizontal) {
        const Point center{fill.x + fill.width,
                           fill.y + fill.height * 0.5};
        const Size radii{std::clamp(edge_extent * 0.72, 4.0, 10.0),
                         std::max(3.0, fill.height * 0.68)};
        painter.fill_radial_gradient(
            {center.x - radii.width, center.y - radii.height,
             radii.width * 2.0, radii.height * 2.0},
            center, radii, corona_stops);
        painter.draw_line({center.x, fill.y + 1.0},
                          {center.x, fill.y + fill.height - 1.0},
                          Color::rgba(255, 255, 255, 252), 1.8);
    } else {
        const Point center{fill.x + fill.width * 0.5, fill.y};
        const Size radii{std::max(3.0, fill.width * 0.68),
                         std::clamp(edge_extent * 0.72, 4.0, 10.0)};
        painter.fill_radial_gradient(
            {center.x - radii.width, center.y - radii.height,
             radii.width * 2.0, radii.height * 2.0},
            center, radii, corona_stops);
        painter.draw_line({fill.x + 1.0, center.y},
                          {fill.x + fill.width - 1.0, center.y},
                          Color::rgba(255, 255, 255, 252), 1.8);
    }

    // Small overlapping hot lobes make the corona flicker like combustion at
    // the cut, while remaining compact enough not to resemble long filaments.
    const int flame_count = reduced ? 2 : 4;
    for (int index = 0; index < flame_count; ++index) {
        const double lane = (static_cast<double>(index) + 0.5) /
                            static_cast<double>(flame_count);
        const double wave = std::sin(tau * phase * 1.7 +
                                     static_cast<double>(index) * 2.11);
        const double heat = 0.5 + 0.5 * wave;
        if (orientation == Orientation::horizontal) {
            const Point center{fill.x + fill.width + 0.6 + heat * 1.4,
                               fill.y + fill.height * lane + wave * 1.1};
            const Size radii{2.4 + heat * 2.2, 1.8 + (1.0 - heat) * 2.0};
            painter.fill_radial_gradient(
                {center.x - radii.width, center.y - radii.height,
                 radii.width * 2.0, radii.height * 2.0},
                center, radii, corona_stops);
        } else {
            const Point center{fill.x + fill.width * lane + wave * 1.1,
                               fill.y - 0.6 - heat * 1.4};
            const Size radii{1.8 + (1.0 - heat) * 2.0, 2.4 + heat * 2.2};
            painter.fill_radial_gradient(
                {center.x - radii.width, center.y - radii.height,
                 radii.width * 2.0, radii.height * 2.0},
                center, radii, corona_stops);
        }
    }

    const int spark_count = reduced ? 3 : 7;
    for (int index = 0; index < spark_count; ++index) {
        const double lane = (static_cast<double>(index) + 0.5) /
                            static_cast<double>(spark_count);
        const double wave = std::sin(tau * phase +
                                     static_cast<double>(index) * 1.73);
        const double length = (reduced ? 0.9 : 1.2) +
                              (wave + 1.0) * (reduced ? 0.65 : 1.35);
        const double spark_width = reduced ? 0.75 :
            0.8 + 0.28 * static_cast<double>(index % 3);
        if (orientation == Orientation::horizontal) {
            const double y = fill.y + fill.height * lane;
            const double edge_x = fill.x + fill.width;
            painter.draw_line({edge_x - 0.35, y},
                              {edge_x + length, y + wave * 0.8},
                              spark, spark_width);
        } else {
            const double x = fill.x + fill.width * lane;
            const double edge_y = fill.y;
            painter.draw_line({x, edge_y + 0.35},
                              {x + wave * 0.8, edge_y - length},
                              spark, spark_width);
        }
    }
    painter.restore();
}

} // namespace gui_forms::range_control_detail
