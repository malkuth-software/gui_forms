#include "gui_forms/diagnostics/invalidation_overlay/invalidation_overlay.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace gui_forms {
namespace {

// -- the process switch ---------------------------------------------------------

// 0 unread, 1 off, 2 on: the environment is read once, on the first read
// of the switch, unless a program set it first.
std::atomic<std::uint8_t> process_switch{0U};
std::atomic<std::uint64_t> process_switch_generation{1U};

constexpr std::uint8_t switch_unread = 0U;
constexpr std::uint8_t switch_off = 1U;
constexpr std::uint8_t switch_on = 2U;

// -- the label font ---------------------------------------------------------------

// A 5 by 7 bitmap font for the labels and the legend: each glyph is seven
// rows, the row's five pixels in its low five bits, the leftmost the
// highest. Lowercase letters draw as capitals; a character it lacks draws
// as '?'. Cells are 6 by 8 font pixels (one column and one row apart).
constexpr std::size_t glyph_rows = 7U;
constexpr std::int32_t cell_width = 6;
constexpr std::int32_t cell_height = 8;

struct Glyph final {
    char character{};
    std::array<std::uint8_t, glyph_rows> rows{};
};

constexpr std::array<Glyph, 49> glyphs{{
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
    {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}},
    {'D', {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}},
    {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}},
    {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
    {'G', {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}},
    {'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'I', {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'J', {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}},
    {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}},
    {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
    {'M', {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}},
    {'N', {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11}},
    {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
    {'Q', {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}},
    {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
    {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}},
    {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},
    {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}},
    {'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},
    {'Y', {0x11, 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04}},
    {'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}},
    {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}},
    {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},
    {'3', {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}},
    {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}},
    {'5', {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}},
    {'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}},
    {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},
    {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},
    {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}},
    {'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}},
    {'_', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F}},
    {':', {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00}},
    {'/', {0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x00}},
    {'(', {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02}},
    {')', {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08}},
    {'#', {0x0A, 0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x0A}},
    {'[', {0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E}},
    {']', {0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E}},
    {',', {0x00, 0x00, 0x00, 0x00, 0x0C, 0x04, 0x08}},
    {'?', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}},
}};

const Glyph& glyph_of(char character) noexcept {
    char wanted = character;
    if (wanted >= 'a' && wanted <= 'z') wanted = static_cast<char>(wanted - 'a' + 'A');
    for (const Glyph& glyph : glyphs) {
        if (glyph.character == wanted) return glyph;
    }
    return glyphs[glyphs.size() - 1U];
}

// -- device geometry --------------------------------------------------------------

// The font pixel and the outline unit in device pixels at `scale`: whole
// pixels, at least one.
std::int32_t device_unit(double scale) noexcept {
    const double rounded = std::floor(scale + 0.5);
    if (!std::isfinite(rounded) || rounded < 1.0) return 1;
    if (rounded > 8.0) return 8;
    return static_cast<std::int32_t>(rounded);
}

DevicePixelRect device_bounds(Rect bounds, double scale) noexcept {
    DevicePixelRect device{};
    device.left = static_cast<std::int32_t>(std::floor(bounds.x * scale));
    device.top = static_cast<std::int32_t>(std::floor(bounds.y * scale));
    device.right = static_cast<std::int32_t>(std::ceil((bounds.x + bounds.width) * scale));
    device.bottom = static_cast<std::int32_t>(std::ceil((bounds.y + bounds.height) * scale));
    return device;
}

DevicePixelRect intersect(DevicePixelRect first, DevicePixelRect second) noexcept {
    DevicePixelRect result{};
    result.left = std::max(first.left, second.left);
    result.top = std::max(first.top, second.top);
    result.right = std::min(first.right, second.right);
    result.bottom = std::min(first.bottom, second.bottom);
    if (result.right < result.left) result.right = result.left;
    if (result.bottom < result.top) result.bottom = result.top;
    return result;
}

bool empty_rect(DevicePixelRect rect) noexcept {
    const bool empty = rect.right <= rect.left || rect.bottom <= rect.top;
    return empty;
}

// The stroke's whole thickness in device pixels: a line, or two lines and
// the gap between them.
std::int32_t stroke_thickness(const InvalidationOutlineStyle& style, double scale) noexcept {
    const double rounded = std::floor(style.width * scale + 0.5);
    const std::int32_t line = rounded < 1.0 ? 1 : static_cast<std::int32_t>(rounded);
    const std::int32_t thickness = style.double_line ? line * 3 : line;
    return thickness;
}

// The label an outline shows: its source, else its cause's name.
std::string_view label_of(const InvalidationOutline& outline) noexcept {
    if (!outline.source.empty()) return outline.source;
    return invalidation_cause_name(outline.cause);
}

// The label's box: inside the outline's top-left corner, below its stroke.
DevicePixelRect label_box(DevicePixelRect outline, std::int32_t thickness, std::size_t characters,
                          std::int32_t unit) noexcept {
    DevicePixelRect box{};
    box.left = outline.left + thickness;
    box.top = outline.top + thickness;
    box.right = box.left + (static_cast<std::int32_t>(characters) * cell_width + 2) * unit;
    box.bottom = box.top + (cell_height + 2) * unit;
    return box;
}

// The legend: a title and one row a cause, at the client's bottom right.
constexpr std::int32_t legend_rows = static_cast<std::int32_t>(invalidation_cause_count) + 1;
constexpr std::int32_t legend_sample = 18;
constexpr std::int32_t legend_characters = 13;
constexpr std::string_view legend_title = "invalidation";

DevicePixelRect legend_box(std::uint32_t width, std::uint32_t height, std::int32_t unit) noexcept {
    const std::int32_t box_width = (4 + legend_sample + 4 + legend_characters * cell_width + 4) * unit;
    const std::int32_t box_height = (legend_rows * (cell_height + 3) + 4) * unit;
    DevicePixelRect box{};
    box.right = static_cast<std::int32_t>(width) - 4 * unit;
    box.bottom = static_cast<std::int32_t>(height) - 4 * unit;
    box.left = box.right - box_width;
    box.top = box.bottom - box_height;
    return box;
}

// -- compositing ------------------------------------------------------------------

// Draws `colour` at `opacity` over one premultiplied BGRA pixel.
void blend_pixel(std::byte* pixel, Color colour, double opacity) noexcept {
    const double alpha = static_cast<double>(colour.alpha) / 255.0 * opacity;
    if (alpha <= 0.0) return;
    const double keep = 1.0 - alpha;
    const std::array<double, 4> source{static_cast<double>(colour.blue) * alpha,
                                       static_cast<double>(colour.green) * alpha,
                                       static_cast<double>(colour.red) * alpha, 255.0 * alpha};
    for (std::size_t channel = 0U; channel < 4U; ++channel) {
        const double below = static_cast<double>(std::to_integer<std::uint8_t>(pixel[channel]));
        const double mixed = std::clamp(source[channel] + below * keep, 0.0, 255.0);
        pixel[channel] = static_cast<std::byte>(static_cast<std::uint8_t>(mixed + 0.5));
    }
}

// The pixels compose writes: the target's bounds and the caller's clip.
struct Canvas final {
    const InvalidationOverlayTarget* target{};
    DevicePixelRect clip{};
};

void fill(const Canvas& canvas, DevicePixelRect rect, Color colour, double opacity) noexcept {
    const DevicePixelRect drawn = intersect(rect, canvas.clip);
    for (std::int32_t y = drawn.top; y < drawn.bottom; ++y) {
        std::byte* const row = (*canvas.target).pixels.data() + static_cast<std::size_t>(y) * (*canvas.target).row_bytes;
        for (std::int32_t x = drawn.left; x < drawn.right; ++x) {
            blend_pixel(row + static_cast<std::size_t>(x) * 4U, colour, opacity);
        }
    }
}

// One band of a stroke, `along` horizontal or vertical, dashed from
// `origin` (the outline's corner on that axis) when the style is.
void fill_band(const Canvas& canvas, DevicePixelRect band, bool horizontal, std::int32_t origin,
               const InvalidationOutlineStyle& style, double scale, double opacity) noexcept {
    if (style.dash <= 0.0) {
        fill(canvas, band, style.colour, opacity);
        return;
    }
    const std::int32_t dash = std::max<std::int32_t>(1, static_cast<std::int32_t>(std::floor(style.dash * scale + 0.5)));
    const std::int32_t gap = std::max<std::int32_t>(1, static_cast<std::int32_t>(std::floor(style.gap * scale + 0.5)));
    const std::int32_t period = dash + gap;
    const std::int32_t first = horizontal ? band.left : band.top;
    const std::int32_t last = horizontal ? band.right : band.bottom;
    for (std::int32_t start = first - ((first - origin) % period); start < last; start += period) {
        DevicePixelRect piece = band;
        if (horizontal) {
            piece.left = std::max(start, band.left);
            piece.right = std::min(start + dash, band.right);
        } else {
            piece.top = std::max(start, band.top);
            piece.bottom = std::min(start + dash, band.bottom);
        }
        if (!empty_rect(piece)) fill(canvas, piece, style.colour, opacity);
    }
}

// An outline's four bands, each `thickness` wide, inside `outline`; a
// double line draws the outer and inner thirds.
void stroke(const Canvas& canvas, DevicePixelRect outline, const InvalidationOutlineStyle& style, double scale,
            double opacity) noexcept {
    const std::int32_t thickness = stroke_thickness(style, scale);
    const std::int32_t line = style.double_line ? thickness / 3 : thickness;
    const std::int32_t passes = style.double_line ? 2 : 1;
    for (std::int32_t pass = 0; pass < passes; ++pass) {
        const std::int32_t inset = pass * 2 * line;
        DevicePixelRect inner{outline.left + inset, outline.top + inset, outline.right - inset,
                              outline.bottom - inset};
        if (empty_rect(inner)) return;
        const DevicePixelRect top{inner.left, inner.top, inner.right, std::min(inner.top + line, inner.bottom)};
        const DevicePixelRect bottom{inner.left, std::max(inner.bottom - line, inner.top), inner.right, inner.bottom};
        const DevicePixelRect left{inner.left, inner.top, std::min(inner.left + line, inner.right), inner.bottom};
        const DevicePixelRect right{std::max(inner.right - line, inner.left), inner.top, inner.right, inner.bottom};
        fill_band(canvas, top, true, outline.left, style, scale, opacity);
        fill_band(canvas, bottom, true, outline.left, style, scale, opacity);
        fill_band(canvas, left, false, outline.top, style, scale, opacity);
        fill_band(canvas, right, false, outline.top, style, scale, opacity);
    }
}

// `text` in the label font from (x, y), each font pixel `unit` device
// pixels square.
void draw_text(const Canvas& canvas, std::int32_t x, std::int32_t y, std::string_view text, std::int32_t unit,
               Color colour, double opacity) noexcept {
    std::int32_t pen = x;
    for (const char character : text) {
        const Glyph& glyph = glyph_of(character);
        for (std::size_t row = 0U; row < glyph_rows; ++row) {
            for (std::int32_t column = 0; column < 5; ++column) {
                const bool lit = (glyph.rows[row] & (0x10U >> static_cast<std::uint32_t>(column))) != 0U;
                if (!lit) continue;
                const std::int32_t left = pen + column * unit;
                const std::int32_t top = y + static_cast<std::int32_t>(row) * unit;
                fill(canvas, DevicePixelRect{left, top, left + unit, top + unit}, colour, opacity);
            }
        }
        pen += cell_width * unit;
    }
}

constexpr Color label_backing = Color::rgba(0, 0, 0, 176);
constexpr Color legend_text = Color::rgba(255, 255, 255);

void draw_legend(const Canvas& canvas, std::uint32_t width, std::uint32_t height, double scale) noexcept {
    const std::int32_t unit = device_unit(scale);
    const DevicePixelRect box = legend_box(width, height, unit);
    fill(canvas, box, label_backing, 1.0);
    std::int32_t y = box.top + 2 * unit;
    draw_text(canvas, box.left + 4 * unit, y, legend_title, unit, legend_text, 1.0);
    for (std::size_t index = 0U; index < invalidation_cause_count; ++index) {
        y += (cell_height + 3) * unit;
        const InvalidationCause cause = static_cast<InvalidationCause>(index);
        const InvalidationOutlineStyle style = invalidation_outline_style(cause);
        const std::int32_t thickness = stroke_thickness(style, scale);
        const std::int32_t middle = y + static_cast<std::int32_t>(glyph_rows) * unit / 2 - thickness / 2;
        const DevicePixelRect sample{box.left + 4 * unit, middle, box.left + (4 + legend_sample) * unit,
                                     middle + thickness};
        if (style.double_line) {
            const std::int32_t line = thickness / 3;
            fill_band(canvas, DevicePixelRect{sample.left, sample.top, sample.right, sample.top + line}, true,
                      sample.left, style, scale, 1.0);
            fill_band(canvas, DevicePixelRect{sample.left, sample.bottom - line, sample.right, sample.bottom}, true,
                      sample.left, style, scale, 1.0);
        } else {
            fill_band(canvas, sample, true, sample.left, style, scale, 1.0);
        }
        draw_text(canvas, sample.right + 4 * unit, y, invalidation_cause_name(cause), unit, style.colour, 1.0);
    }
}

} // namespace

InvalidationOutlineStyle invalidation_outline_style(InvalidationCause cause) noexcept {
    InvalidationOutlineStyle style{};
    switch (cause) {
    case InvalidationCause::control:
        style = InvalidationOutlineStyle{Color::rgba(255, 32, 32), 2.0, 0.0, 0.0, false};
        break;
    case InvalidationCause::layout:
        style = InvalidationOutlineStyle{Color::rgba(255, 160, 0), 2.0, 6.0, 4.0, false};
        break;
    case InvalidationCause::host_expose:
        style = InvalidationOutlineStyle{Color::rgba(48, 128, 255), 2.0, 2.0, 3.0, false};
        break;
    case InvalidationCause::scroll_copy:
        style = InvalidationOutlineStyle{Color::rgba(0, 200, 80), 2.0, 12.0, 4.0, false};
        break;
    case InvalidationCause::live_surface:
        style = InvalidationOutlineStyle{Color::rgba(230, 0, 230), 1.0, 0.0, 0.0, true};
        break;
    case InvalidationCause::external_content:
        style = InvalidationOutlineStyle{Color::rgba(0, 210, 230), 1.0, 5.0, 3.0, true};
        break;
    default:
        // An enumerator outside the declared set (a corrupt value) does nothing.
        break;
    }
    return style;
}

std::string_view invalidation_cause_name(InvalidationCause cause) noexcept {
    switch (cause) {
    case InvalidationCause::control: return "control";
    case InvalidationCause::layout: return "layout";
    case InvalidationCause::host_expose: return "expose";
    case InvalidationCause::scroll_copy: return "scroll copy";
    case InvalidationCause::live_surface: return "live surface";
    case InvalidationCause::external_content: return "external";
    }
    return "control";
}

bool invalidation_overlay_requested_by_environment() noexcept {
    const char* const value = std::getenv("GUI_FORMS_DEBUG_INVALIDATION");
    if (value == nullptr) return false;
    const std::string_view text(value);
    const bool requested = !text.empty() && text != "0";
    return requested;
}

void set_invalidation_overlay_default(bool enabled) noexcept {
    const std::uint8_t wanted = enabled ? switch_on : switch_off;
    const std::uint8_t previous = process_switch.exchange(wanted, std::memory_order_acq_rel);
    if (previous != wanted) process_switch_generation.fetch_add(1U, std::memory_order_acq_rel);
}

bool invalidation_overlay_default() noexcept {
    std::uint8_t state = process_switch.load(std::memory_order_acquire);
    if (state == switch_unread) {
        const std::uint8_t read = invalidation_overlay_requested_by_environment() ? switch_on : switch_off;
        // A program's set wins over the environment read racing it.
        std::uint8_t expected = switch_unread;
        if (process_switch.compare_exchange_strong(expected, read, std::memory_order_acq_rel)) {
            state = read;
        } else {
            state = expected;
        }
    }
    return state == switch_on;
}

std::uint64_t invalidation_overlay_default_generation() noexcept {
    return process_switch_generation.load(std::memory_order_relaxed);
}

void InvalidationOverlay::record(Rect bounds, InvalidationCause cause, std::string_view source,
                                 InvalidationClock::time_point at) {
    if (bounds.empty() || !bounds.finite()) return;
    if (outlines_.size() >= maximum_invalidation_outlines) outlines_.erase(outlines_.begin());
    InvalidationOutline outline{};
    outline.bounds = bounds;
    outline.cause = cause;
    outline.source = std::string(source.substr(0U, std::min(source.size(), maximum_invalidation_label)));
    outline.at = at;
    outlines_.push_back(std::move(outline));
    ++recorded_[static_cast<std::size_t>(cause)];
}

void InvalidationOverlay::expire(InvalidationClock::time_point now) {
    std::size_t kept = 0U;
    while (kept < outlines_.size() && now - outlines_[kept].at >= invalidation_outline_lifetime) ++kept;
    // Outlines are recorded in time order, so the expired ones lead.
    if (kept != 0U) outlines_.erase(outlines_.begin(), outlines_.begin() + static_cast<std::ptrdiff_t>(kept));
}

void InvalidationOverlay::clear() noexcept {
    outlines_.clear();
}

void InvalidationOverlay::fill_regions(double scale, std::uint32_t width, std::uint32_t height,
                                       std::vector<DevicePixelRect>& regions) const {
    regions.clear();
    if (outlines_.empty() || width == 0U || height == 0U) return;
    const DevicePixelRect client{0, 0, static_cast<std::int32_t>(width), static_cast<std::int32_t>(height)};
    const std::int32_t unit = device_unit(scale);
    regions.reserve(outlines_.size() * 5U + 1U);
    for (const InvalidationOutline& outline : outlines_) {
        const DevicePixelRect device = device_bounds(outline.bounds, scale);
        const std::int32_t thickness = stroke_thickness(invalidation_outline_style(outline.cause), scale);
        const std::array<DevicePixelRect, 5> parts{{
            {device.left, device.top, device.right, device.top + thickness},
            {device.left, device.bottom - thickness, device.right, device.bottom},
            {device.left, device.top, device.left + thickness, device.bottom},
            {device.right - thickness, device.top, device.right, device.bottom},
            label_box(device, thickness, label_of(outline).size(), unit),
        }};
        for (const DevicePixelRect& part : parts) {
            const DevicePixelRect clipped = intersect(part, client);
            if (!empty_rect(clipped)) regions.push_back(clipped);
        }
    }
    const DevicePixelRect legend = intersect(legend_box(width, height, unit), client);
    if (!empty_rect(legend)) regions.push_back(legend);
}

void InvalidationOverlay::compose(const InvalidationOverlayTarget& target, double scale,
                                  InvalidationClock::time_point now, DevicePixelRect clip) const {
    if (outlines_.empty() || target.width == 0U || target.height == 0U) return;
    const std::uint64_t needed = static_cast<std::uint64_t>(target.row_bytes) * (target.height - 1U) +
                                 static_cast<std::uint64_t>(target.width) * 4U;
    if (target.row_bytes < static_cast<std::size_t>(target.width) * 4U || target.pixels.size() < needed) return;
    const DevicePixelRect bounds{0, 0, static_cast<std::int32_t>(target.width), static_cast<std::int32_t>(target.height)};
    Canvas canvas{};
    canvas.target = &target;
    canvas.clip = intersect(clip, bounds);
    if (empty_rect(canvas.clip)) return;
    const std::int32_t unit = device_unit(scale);
    const double lifetime = static_cast<double>(
        std::chrono::duration_cast<std::chrono::microseconds>(invalidation_outline_lifetime).count());
    for (const InvalidationOutline& outline : outlines_) {
        const double age = static_cast<double>(
            std::chrono::duration_cast<std::chrono::microseconds>(now - outline.at).count());
        const double opacity = std::clamp(1.0 - age / lifetime, 0.0, 1.0);
        if (opacity <= 0.0) continue;
        const InvalidationOutlineStyle style = invalidation_outline_style(outline.cause);
        const DevicePixelRect device = device_bounds(outline.bounds, scale);
        stroke(canvas, device, style, scale, opacity);
        const std::string_view label = label_of(outline);
        const std::int32_t thickness = stroke_thickness(style, scale);
        const DevicePixelRect box = label_box(device, thickness, label.size(), unit);
        fill(canvas, box, label_backing, opacity);
        draw_text(canvas, box.left + unit, box.top + unit, label, unit, style.colour, opacity);
    }
    draw_legend(canvas, target.width, target.height, scale);
}

} // namespace gui_forms
