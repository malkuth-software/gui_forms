#pragma once

// Owner decision L-Q8: layout publishes unsnapped logical boxes and painting
// rounds each edge to the nearest device pixel, so boxes that share a logical
// edge share a device edge, with no seam and no overlap. SkiaRaster, the one
// CPU painter, paints this scene, and the check reads its pixels.
//
// The scene is 40 x 34 logical units under a fractional translation. Two
// translucent boxes share a fractional vertical edge, two share a fractional
// horizontal edge, and a stroked rectangle, a flat gradient and a rounded
// fill with a rounded border on it sit at fractional positions. Translucent
// colours make a seam (both boxes covering an edge pixel partly) and an
// overlap (both covering it fully) visible as a colour that is neither box's.

#include "gui_forms/types.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>

namespace edge_snapping {

constexpr gui_forms::Size scene_size{40.0, 34.0};
constexpr gui_forms::Point translation{0.3, 0.45};
constexpr gui_forms::Rect left_box{2.2, 2.1, 10.37 - 2.2, 8.0};
constexpr gui_forms::Rect right_box{10.37, 2.1, 20.08 - 10.37, 8.0};
constexpr gui_forms::Rect top_box{24.15, 2.1, 11.6, 6.43 - 2.1};
constexpr gui_forms::Rect bottom_box{24.15, 6.43, 11.6, 10.11 - 6.43};
constexpr gui_forms::Rect stroked{3.3, 13.6, 14.45, 7.2};
constexpr double stroke_width = 1.3;
constexpr gui_forms::Rect gradient_box{24.15, 13.6, 11.6, 7.2};
constexpr gui_forms::Rect rounded_box{6.35, 24.2, 27.4, 6.6};
constexpr double rounded_radius = 2.2;

inline void paint_scene(gui_forms::Painter& painter) {
    painter.fill_rect({0.0, 0.0, scene_size.width, scene_size.height}, gui_forms::Color::rgba(250, 250, 250));
    painter.save();
    painter.translate(translation);
    const gui_forms::Color first = gui_forms::Color::rgba(200, 30, 30, 128);
    const gui_forms::Color second = gui_forms::Color::rgba(30, 30, 200, 128);
    painter.fill_rect(left_box, first);
    painter.fill_rect(right_box, second);
    painter.fill_rect(top_box, first);
    painter.fill_rect(bottom_box, second);
    painter.stroke_rect(stroked, gui_forms::Color::rgba(20, 120, 20, 160), stroke_width);
    const gui_forms::Color flat = gui_forms::Color::rgba(120, 20, 120, 160);
    const std::array<gui_forms::GradientStop, 2> stops{{{0.0, flat}, {1.0, flat}}};
    painter.fill_linear_gradient(gradient_box, {gradient_box.x, gradient_box.y},
                                 {gradient_box.right(), gradient_box.y}, stops);
    // A control's face: a rounded fill with its border on the same box.
    painter.fill_rounded_rect(rounded_box, rounded_radius, gui_forms::Color::rgba(200, 220, 240));
    painter.stroke_rounded_rect(rounded_box, rounded_radius, gui_forms::Color::rgba(40, 60, 90), stroke_width);
    painter.restore();
}

// A read-back of the painted scene: `width` x `height` device pixels of four
// bytes each, `row_bytes` apart, top row first. The byte order is the
// painter's own; the check only compares pixels with each other.
struct PixelImage final {
    int width{0};
    int height{0};
    std::size_t row_bytes{0};
    const std::uint8_t* bytes{nullptr};
};

inline std::uint32_t pixel_at(const PixelImage& image, const int x, const int y) {
    const std::size_t offset = static_cast<std::size_t>(y) * image.row_bytes + static_cast<std::size_t>(x) * 4U;
    const std::uint32_t value = static_cast<std::uint32_t>(image.bytes[offset]) |
        (static_cast<std::uint32_t>(image.bytes[offset + 1U]) << 8U) |
        (static_cast<std::uint32_t>(image.bytes[offset + 2U]) << 16U) |
        (static_cast<std::uint32_t>(image.bytes[offset + 3U]) << 24U);
    return value;
}

// The device pixel boundary of a logical edge: rounded to the nearest, halves
// upward (written here independently of the painters).
inline int device_edge(const double logical, const double offset, const double scale) {
    const double device = std::floor((logical + offset) * scale + 0.5);
    return static_cast<int>(device);
}

inline bool fail(const char* const what, const double scale, const int x, const int y) {
    std::cerr << "edge snapping: " << what << " at scale " << scale << ", pixel " << x << "," << y << '\n';
    return false;
}

// As fail, with the pixel found and the one expected, as hexadecimal bytes in
// the painter's order (lowest first).
inline bool fail_values(const char* const what, const double scale, const int x, const int y,
                        const std::uint32_t found, const std::uint32_t expected) {
    std::cerr << "edge snapping: " << what << " at scale " << scale << ", pixel " << x << "," << y
              << ": found 0x" << std::hex << found << ", expected 0x" << expected << std::dec << '\n';
    return false;
}

// Along one row (horizontal) or column: background before `start`, one
// colour on [start, middle), another on [middle, end), background at `end`.
inline bool shared_edge(const PixelImage& image, const double scale, const bool horizontal, const int line,
                        const int start, const int middle, const int end, const std::uint32_t background) {
    std::array<int, 2> position{0, 0};
    const std::size_t along = horizontal ? 0U : 1U;
    position[1U - along] = line;
    position[along] = start - 1;
    if (pixel_at(image, position[0], position[1]) != background) {
        return fail("a box reaches past its rounded first edge", scale, position[0], position[1]);
    }
    position[along] = start;
    const std::uint32_t first = pixel_at(image, position[0], position[1]);
    position[along] = middle;
    const std::uint32_t second = pixel_at(image, position[0], position[1]);
    if (first == background || second == background || first == second) {
        return fail("the boxes' colours are not distinct", scale, position[0], position[1]);
    }
    for (int step = start; step < end; ++step) {
        position[along] = step;
        const std::uint32_t expected = step < middle ? first : second;
        if (pixel_at(image, position[0], position[1]) != expected) {
            return fail("a seam or an overlap at the shared edge", scale, position[0], position[1]);
        }
    }
    position[along] = end;
    if (pixel_at(image, position[0], position[1]) != background) {
        return fail("a box reaches past its rounded last edge", scale, position[0], position[1]);
    }
    return true;
}

// Every pixel of `area` (logical, before the translation) is the background
// or one ink colour, and some pixel is inked: coverage is all or nothing.
inline bool whole_pixels(const PixelImage& image, const double scale, const gui_forms::Rect area,
                         const std::uint32_t background) {
    const int left = device_edge(area.x, translation.x, scale);
    const int top = device_edge(area.y, translation.y, scale);
    const int right = device_edge(area.right(), translation.x, scale);
    const int bottom = device_edge(area.bottom(), translation.y, scale);
    bool inked = false;
    std::uint32_t ink = background;
    for (int y = top; y < bottom; ++y) {
        for (int x = left; x < right; ++x) {
            const std::uint32_t value = pixel_at(image, x, y);
            if (value == background) continue;
            if (!inked) {
                inked = true;
                ink = value;
                continue;
            }
            if (value != ink) return fail_values("a partly covered pixel", scale, x, y, value, ink);
        }
    }
    if (!inked) return fail("nothing painted", scale, left, top);
    return true;
}

// The flat gradient, with its coverage and its colour checked apart.
// - Coverage, exact: every pixel inside the rounded device box is inked
//   (not the background) and every pixel in the two-pixel band around it is
//   the background, so the box's edges are its rounded edges exactly.
// - Colour, exact: every pixel inside the box, edges included, is the colour
//   of the first interior pixel, so the painter fills the gradient without
//   dithering and a partly covered edge pixel (tens of levels toward the
//   background) fails.
inline bool gradient_coverage(const PixelImage& image, const double scale, const std::uint32_t background) {
    const int left = device_edge(gradient_box.x, translation.x, scale);
    const int top = device_edge(gradient_box.y, translation.y, scale);
    const int right = device_edge(gradient_box.right(), translation.x, scale);
    const int bottom = device_edge(gradient_box.bottom(), translation.y, scale);
    if (right - left < 3 || bottom - top < 3) return fail("the gradient box has no interior", scale, left, top);
    for (int y = top - 2; y < bottom + 2; ++y) {
        for (int x = left - 2; x < right + 2; ++x) {
            const bool inside = x >= left && x < right && y >= top && y < bottom;
            const std::uint32_t value = pixel_at(image, x, y);
            if (inside && value == background) return fail("a gradient pixel inside its box is not painted", scale, x, y);
            if (!inside && value != background) {
                return fail_values("a gradient pixel outside its rounded box", scale, x, y, value, background);
            }
        }
    }
    const std::uint32_t ink = pixel_at(image, left + 1, top + 1);
    for (int y = top; y < bottom; ++y) {
        for (int x = left; x < right; ++x) {
            const std::uint32_t value = pixel_at(image, x, y);
            if (value == ink) continue;
            const bool edge = x == left || x == right - 1 || y == top || y == bottom - 1;
            const char* const what = edge ? "a partly covered gradient edge pixel" : "the gradient's interior is not flat";
            return fail_values(what, scale, x, y, value, ink);
        }
    }
    return true;
}

// Across a rounded fill with its border, along one row (horizontal) or
// column from `from` to `to`: background, border, fill, border, background,
// each pixel exactly one of the three. The border's outer edges are whole
// device pixels at the box's edges, rounded, either centred on them (a
// stroke half outside) or inside them (a painter that strokes inside).
inline bool border_on_fill(const PixelImage& image, const double scale, const bool horizontal, const int line,
                           const double first_edge, const double last_edge, const double offset,
                           const int from, const int to, const std::uint32_t background) {
    std::array<int, 2> position{0, 0};
    const std::size_t along = horizontal ? 0U : 1U;
    position[1U - along] = line;
    int stage = 0; // 0 before, 1 first border, 2 fill, 3 last border, 4 after
    std::uint32_t border = background;
    std::uint32_t fill = background;
    int border_start = 0;
    int border_end = 0;
    for (int step = from; step < to; ++step) {
        position[along] = step;
        const std::uint32_t value = pixel_at(image, position[0], position[1]);
        if (stage == 0 && value != background) {
            stage = 1;
            border = value;
            border_start = step;
        } else if (stage == 1 && value != border) {
            stage = 2;
            fill = value;
        } else if (stage == 2 && value != fill) {
            stage = 3;
        } else if (stage == 3 && value != border) {
            stage = 4;
            border_end = step;
        }
        const bool expected = (stage == 0 && value == background) || ((stage == 1 || stage == 3) && value == border) ||
            (stage == 2 && value == fill) || (stage == 4 && value == background);
        if (!expected) {
            const std::uint32_t wanted = stage == 0 || stage == 4 ? background : (stage == 2 ? fill : border);
            return fail_values("the border and its fill do not meet on whole pixels", scale, position[0], position[1],
                               value, wanted);
        }
    }
    if (stage != 4) return fail("the border and fill did not both appear", scale, position[0], position[1]);
    const double half = stroke_width * 0.5;
    const bool start_snapped = border_start == device_edge(first_edge - half, offset, scale) ||
        border_start == device_edge(first_edge, offset, scale);
    const bool end_snapped = border_end == device_edge(last_edge + half, offset, scale) ||
        border_end == device_edge(last_edge, offset, scale);
    if (!start_snapped || !end_snapped) return fail("the border's outer edge is not the rounded box edge", scale, border_start, border_end);
    return true;
}

inline bool check_scene(const PixelImage& image, const double scale) {
    const std::uint32_t background = pixel_at(image, 0, 0);
    const int row = device_edge(6.0, translation.y, scale);
    const bool across = shared_edge(image, scale, true, row,
        device_edge(left_box.x, translation.x, scale), device_edge(left_box.right(), translation.x, scale),
        device_edge(right_box.right(), translation.x, scale), background);
    const int column = device_edge(30.0, translation.x, scale);
    const bool down = shared_edge(image, scale, false, column,
        device_edge(top_box.y, translation.y, scale), device_edge(top_box.bottom(), translation.y, scale),
        device_edge(bottom_box.bottom(), translation.y, scale), background);
    const gui_forms::Rect stroke_area{stroked.x - stroke_width, stroked.y - stroke_width,
                                      stroked.width + stroke_width * 2.0, stroked.height + stroke_width * 2.0};
    const bool stroke = whole_pixels(image, scale, stroke_area, background);
    const bool gradient = gradient_coverage(image, scale, background);
    const int rounded_row = device_edge(27.5, translation.y, scale);
    const bool rounded_across = border_on_fill(image, scale, true, rounded_row, rounded_box.x, rounded_box.right(),
        translation.x, device_edge(rounded_box.x - 2.0, translation.x, scale),
        device_edge(rounded_box.right() + 2.0, translation.x, scale), background);
    const int rounded_column = device_edge(20.0, translation.x, scale);
    const bool rounded_down = border_on_fill(image, scale, false, rounded_column, rounded_box.y, rounded_box.bottom(),
        translation.y, device_edge(rounded_box.y - 2.0, translation.y, scale),
        device_edge(rounded_box.bottom() + 2.0, translation.y, scale), background);
    const bool passed = across && down && stroke && gradient && rounded_across && rounded_down;
    return passed;
}

constexpr std::array<double, 4> scales{1.0, 1.25, 1.5, 2.0};

} // namespace edge_snapping
