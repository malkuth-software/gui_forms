// The accessibility element's text conversions and layout queries
// (macos_accessibility_text.hpp, which states each one's contract).

#include "macos_accessibility_text.hpp"

namespace gui_forms::host::macos {

std::size_t utf8_sequence_bytes(unsigned char lead) noexcept {
    if (lead >= 0xF0U) return 4U;
    if (lead >= 0xE0U) return 3U;
    if (lead >= 0xC0U) return 2U;
    return 1U;
}

std::size_t utf16_units_before(std::string_view value, std::size_t bytes) noexcept {
    const std::size_t end = std::min(bytes, value.size());
    std::size_t units = 0U;
    std::size_t index = 0U;
    while (index < end) {
        const std::size_t length = utf8_sequence_bytes(static_cast<unsigned char>(value[index]));
        units += length == 4U ? 2U : 1U;
        index += length;
    }
    return units;
}

std::size_t utf8_offset_at_units(std::string_view value, std::size_t units,
                                 SurrogateRounding rounding) noexcept {
    std::size_t counted = 0U;
    std::size_t index = 0U;
    while (index < value.size() && counted < units) {
        const std::size_t length = utf8_sequence_bytes(static_cast<unsigned char>(value[index]));
        const std::size_t width = length == 4U ? 2U : 1U;
        if (counted + width > units && rounding == SurrogateRounding::down) break;
        counted += width;
        index += length;
    }
    const std::size_t offset = std::min(index, value.size());
    return offset;
}

NSRange utf16_range(std::string_view value, std::size_t start, std::size_t end) noexcept {
    const std::size_t first = utf16_units_before(value, start);
    const std::size_t last = utf16_units_before(value, std::max(start, end));
    const NSRange range = NSMakeRange(static_cast<NSUInteger>(first), static_cast<NSUInteger>(last - first));
    return range;
}

bool utf8_range(std::string_view value, NSRange range, std::size_t& start, std::size_t& end) noexcept {
    const std::size_t units = utf16_units_before(value, value.size());
    if (range.location > units || range.length > std::numeric_limits<NSUInteger>::max() - range.location) {
        return false;
    }
    const std::size_t last = std::min(static_cast<std::size_t>(range.location + range.length), units);
    start = utf8_offset_at_units(value, range.location, SurrogateRounding::down);
    end = utf8_offset_at_units(value, last, SurrogateRounding::up);
    return true;
}

// Orders an offset before a line that starts after it (std::upper_bound
// over the lines, which run in text order).
struct OffsetBeforeLine final {
    bool operator()(std::size_t offset, const SemanticTextLine& line) const noexcept {
        return offset < line.range.start.value();
    }
};

std::size_t line_of_offset(const SemanticTextLayout& layout, std::size_t offset) noexcept {
    const std::vector<SemanticTextLine>::const_iterator after =
        std::upper_bound(layout.lines.begin(), layout.lines.end(), offset, OffsetBeforeLine{});
    const std::size_t past = static_cast<std::size_t>(after - layout.lines.begin());
    const std::size_t found = past == 0U ? 0U : past - 1U;
    return found;
}

std::optional<GFRect> caret_box(const SemanticTextLayout& layout, std::size_t offset) noexcept {
    if (layout.lines.empty()) return std::nullopt;
    std::size_t line = line_of_offset(layout, offset);
    double x = layout.lines[line].bounds.x;
    bool placed = false;
    for (const SemanticTextGrapheme& grapheme : layout.graphemes) {
        if (grapheme.range.start.value() == offset) {
            line = grapheme.line.value();
            x = grapheme.bounds.x;
            placed = true;
            break;
        }
    }
    if (!placed) {
        for (const SemanticTextGrapheme& grapheme : layout.graphemes) {
            if (grapheme.range.end.value() != offset) continue;
            line = grapheme.line.value();
            x = grapheme.bounds.x + grapheme.bounds.width;
        }
    }
    if (line >= layout.lines.size()) return std::nullopt;
    const GFRect line_bounds = layout.lines[line].bounds;
    const GFRect box{x, line_bounds.y, 1.0, line_bounds.height};
    return box;
}

std::optional<GFRect> range_box(const SemanticTextLayout& layout, std::size_t start,
                                std::size_t end) noexcept {
    if (start == end) return caret_box(layout, start);
    std::optional<GFRect> united;
    for (const SemanticTextGrapheme& grapheme : layout.graphemes) {
        if (grapheme.range.start.value() < start || grapheme.range.end.value() > end) continue;
        united = united ? GFRect::united(*united, grapheme.bounds) : grapheme.bounds;
    }
    return united;
}

const SemanticTextGrapheme* grapheme_at_point(const SemanticTextLayout& layout, double x,
                                              double y) noexcept {
    std::size_t line = 0U;
    double nearest_line = std::numeric_limits<double>::infinity();
    for (std::size_t index = 0U; index < layout.lines.size(); ++index) {
        const GFRect band = layout.lines[index].bounds;
        const double distance = std::max(0.0, std::max(band.y - y, y - (band.y + band.height)));
        if (distance < nearest_line) {
            nearest_line = distance;
            line = index;
        }
    }
    const SemanticTextGrapheme* found = nullptr;
    double nearest = std::numeric_limits<double>::infinity();
    for (const SemanticTextGrapheme& grapheme : layout.graphemes) {
        if (grapheme.line.value() != line) continue;
        const double left = grapheme.bounds.x;
        const double right = grapheme.bounds.x + grapheme.bounds.width;
        const double distance = std::max(0.0, std::max(left - x, x - right));
        if (distance < nearest) {
            nearest = distance;
            found = &grapheme;
        }
    }
    return found;
}

bool text_range_selector(SEL selector) noexcept {
    const bool text = selector == @selector(accessibilityNumberOfCharacters) ||
                      selector == @selector(accessibilitySelectedTextRange) ||
                      selector == @selector(setAccessibilitySelectedTextRange:) ||
                      selector == @selector(accessibilitySelectedText) ||
                      selector == @selector(accessibilityVisibleCharacterRange) ||
                      selector == @selector(accessibilityInsertionPointLineNumber) ||
                      selector == @selector(accessibilityStringForRange:) ||
                      selector == @selector(accessibilityAttributedStringForRange:) ||
                      selector == @selector(accessibilityLineForIndex:) ||
                      selector == @selector(accessibilityRangeForLine:) ||
                      selector == @selector(accessibilityFrameForRange:) ||
                      selector == @selector(accessibilityRangeForPosition:) ||
                      selector == @selector(accessibilityRangeForIndex:) ||
                      selector == @selector(accessibilityStyleRangeForIndex:);
    return text;
}

} // namespace gui_forms::host::macos
