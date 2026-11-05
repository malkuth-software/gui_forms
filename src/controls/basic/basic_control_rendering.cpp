#include "basic_control_rendering.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/text.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace gui_forms {
namespace {
struct EstimatedWidth final {
    FontSpec font{};

    [[nodiscard]] double operator()(std::string_view candidate) const noexcept {
        const double width = estimated_text_width(candidate, font);
        return width;
    }
};

// One synchronous paragraph traversal has one stable width resolver. Keep a
// small exact-text cache for repeated words/line prefixes; long candidates and
// eviction still use the original resolver. No font/context borrow survives
// this call and no mutable line buffer is retained by reference.
class ParagraphWidths final {
public:
    explicit ParagraphWidths(const TextWidthResolver& resolver) noexcept
        : resolver_(resolver) {}

    [[nodiscard]] double resolve(const std::string_view text) {
        if (text.size() > maximum_key_bytes) {
            const double width = resolver_(text);
            return width;
        }
        for (std::size_t index = 0U; index < count_; ++index) {
            const Entry& entry = entries_[index];
            const std::string_view previous(entry.bytes.data(), entry.length);
            if (text == previous) return entry.width;
        }
        // Resolve before replacing a key: provider failure leaves this cache
        // coherent. Copy only the live key; unused fixed storage is never read.
        const double width = resolver_(text);
        Entry& entry = entries_[next_];
        std::copy(text.begin(), text.end(), entry.bytes.begin());
        entry.length = text.size();
        entry.width = width;
        if (count_ < entries_.size()) ++count_;
        ++next_;
        if (next_ == entries_.size()) next_ = 0U;
        return width;
    }

private:
    static constexpr std::size_t maximum_key_bytes = 128U;
    struct Entry final {
        std::array<char, maximum_key_bytes> bytes{};
        std::size_t length{};
        double width{};
    };
    const TextWidthResolver& resolver_;
    std::array<Entry, 32U> entries_{};
    std::size_t count_{};
    std::size_t next_{};
};

// How much of the `free` width an alignment leaves before the content: none
// at the left, half in the centre, all at the right.
double horizontal_offset(ContentAlignment alignment, double free) noexcept {
    switch (alignment) {
    case ContentAlignment::top_left:
    case ContentAlignment::middle_left:
    case ContentAlignment::bottom_left: return 0.0;
    case ContentAlignment::top_center:
    case ContentAlignment::middle_center:
    case ContentAlignment::bottom_center: return free * 0.5;
    case ContentAlignment::top_right:
    case ContentAlignment::middle_right:
    case ContentAlignment::bottom_right: return free;
    }
    // An alignment outside the enumeration keeps to the left.
    return 0.0;
}

// How much of the `free` height an alignment leaves above the content.
double vertical_offset(ContentAlignment alignment, double free) noexcept {
    switch (alignment) {
    case ContentAlignment::top_left:
    case ContentAlignment::top_center:
    case ContentAlignment::top_right: return 0.0;
    case ContentAlignment::middle_left:
    case ContentAlignment::middle_center:
    case ContentAlignment::middle_right: return free * 0.5;
    case ContentAlignment::bottom_left:
    case ContentAlignment::bottom_center:
    case ContentAlignment::bottom_right: return free;
    }
    // An alignment outside the enumeration keeps to the top.
    return 0.0;
}
}

std::shared_ptr<const PropertyEnumDescriptor> picture_box_size_mode_enum() {
    static const std::shared_ptr<const gui_forms::PropertyEnumDescriptor> value = std::make_shared<const PropertyEnumDescriptor>(
        PropertyEnumDescriptor{
            "picture_box_size_mode",
            {{"normal", 0}, {"stretch_image", 1}, {"auto_size", 2},
             {"center_image", 3}, {"zoom", 4}},
            false});
    return value;
}

BindingValue picture_box_size_mode_value(PictureBoxSizeMode mode) {
    PropertyDescriptor descriptor;
    descriptor.kind = BindingValueKind::enumeration;
    descriptor.enumeration = picture_box_size_mode_enum();
    const std::optional<BindingValue> normalized = convert_property_value(
        BindingValue{static_cast<std::int64_t>(mode)}, descriptor);
    if (!normalized) {
        throw std::logic_error(
            "GUI.Forms retained PictureBoxSizeMode is outside its property schema");
    }
    return *normalized;
}

double estimated_text_width(std::string_view text, FontSpec font) noexcept {
    std::size_t scalars{};
    for (const unsigned char byte : text) {
        if ((byte & 0xc0U) != 0x80U) {
            ++scalars;
        }
    }
    const double width = static_cast<double>(scalars) * font.size * 0.56;
    return width;
}

bool valid_content_alignment(ContentAlignment alignment) noexcept {
    switch (alignment) {
    case ContentAlignment::top_left:
    case ContentAlignment::top_center:
    case ContentAlignment::top_right:
    case ContentAlignment::middle_left:
    case ContentAlignment::middle_center:
    case ContentAlignment::middle_right:
    case ContentAlignment::bottom_left:
    case ContentAlignment::bottom_center:
    case ContentAlignment::bottom_right: return true;
    }
    return false;
}

bool valid_text_image_relation(TextImageRelation relation) noexcept {
    switch (relation) {
    case TextImageRelation::overlay:
    case TextImageRelation::image_before_text:
    case TextImageRelation::text_before_image:
    case TextImageRelation::image_above_text:
    case TextImageRelation::text_above_image: return true;
    }
    return false;
}

std::string elide_text_end(Painter& painter, std::string_view text, FontSpec font,
                           const double maximum_width) {
    if (text.empty() || maximum_width <= 0.0) return {};
    if (painter.measure_text_utf8(text, font).width <= maximum_width) {
        return std::string(text);
    }
    constexpr std::string_view ellipsis = "…";
    if (painter.measure_text_utf8(ellipsis, font).width > maximum_width) {
        return {};
    }
    const TextStore store(text);
    std::size_t grapheme = store.grapheme_count().value();
    while (grapheme > 0U) {
        --grapheme;
        const std::size_t end =
            store.utf8_offset(GraphemeIndex(grapheme)).value();
        std::string candidate(text.substr(0U, end));
        candidate.append(ellipsis);
        if (painter.measure_text_utf8(candidate, font).width <= maximum_width) {
            return candidate;
        }
    }
    return std::string(ellipsis);
}

Rect aligned_rect(Rect bounds, Size size,
                  ContentAlignment alignment) noexcept {
    const double width = std::min(std::max(0.0, size.width), bounds.width);
    const double height = std::min(std::max(0.0, size.height), bounds.height);
    const double x = bounds.x + horizontal_offset(alignment, bounds.width - width);
    const double y = bounds.y + vertical_offset(alignment, bounds.height - height);
    return {x, y, width, height};
}

std::vector<std::string> label_lines(const std::string_view text, const FontSpec font,
                                     const double width, const TextWrapping wrapping,
                                     const std::size_t maximum_lines) {
    const TextWidthResolver resolve_width{EstimatedWidth{font}};
    std::vector<std::string> lines = label_lines(text, font, width, wrapping, resolve_width, maximum_lines);
    return lines;
}

std::vector<std::string> label_lines(
    const std::string_view text, const FontSpec font, const double width, const TextWrapping wrapping,
    const TextWidthResolver& resolve_width, const std::size_t maximum_lines) {
    std::vector<std::string> lines{};
    const std::size_t line_limit = maximum_lines == 0U
        ? std::numeric_limits<std::size_t>::max() : maximum_lines;
    const bool wrap_paragraphs = wrapping != TextWrapping::no_wrap && !(width <= 4.0);
    std::size_t paragraph_start{};
    while (paragraph_start <= text.size()) {
        const std::size_t newline = text.find('\n', paragraph_start);
        const std::size_t paragraph_end = newline == std::string_view::npos
            ? text.size() : newline;
        const std::string_view paragraph =
            text.substr(paragraph_start, paragraph_end - paragraph_start);
        if (!wrap_paragraphs || paragraph.empty()) {
            lines.emplace_back(paragraph);
            if (lines.size() >= line_limit) return lines;
        } else {
            ParagraphWidths widths(resolve_width);
            // Build and measure in reusable line storage. Its capacity grows
            // only for a larger candidate, not for whitespace or paragraph size.
            // Published lines own just their live text rather than this scratch.
            std::string line{};
            std::size_t cursor{};
            while (cursor < paragraph.size()) {
                while (cursor < paragraph.size() &&
                       std::isspace(static_cast<unsigned char>(paragraph[cursor])) != 0) {
                    ++cursor;
                }
                if (cursor >= paragraph.size()) {
                    break;
                }
                std::size_t word_end = cursor;
                while (word_end < paragraph.size() &&
                       std::isspace(static_cast<unsigned char>(paragraph[word_end])) == 0) {
                    ++word_end;
                }
                const std::string_view word = paragraph.substr(cursor, word_end - cursor);
                // CJK paragraphs and long unbroken identifiers have no ASCII
                // word boundary. Break only at grapheme boundaries, retaining
                // combining marks and joined emoji with their base character.
                if (widths.resolve(word) > width) {
                    const TextStore store(word);
                    if (!line.empty()) { line.push_back(' '); }
                    const std::size_t graphemes = store.grapheme_count().value();
                    for (std::size_t index = 0; index < graphemes; ++index) {
                        const Utf8Range range = store.grapheme_range(GraphemeIndex(index));
                        const std::string_view cluster = word.substr(range.start.value(), range.end.value() - range.start.value());
                        const std::size_t previous_length = line.size();
                        line.append(cluster);
                        if (previous_length != 0U && widths.resolve(line) > width) {
                            line.resize(previous_length);
                            if (line.back() == ' ') { line.pop_back(); }
                            lines.push_back(line);
                            if (lines.size() >= line_limit) return lines;
                            line.assign(cluster);
                        }
                    }
                    cursor = word_end;
                    continue;
                }
                const std::size_t previous_length = line.size();
                if (previous_length != 0U) {
                    line.push_back(' ');
                }
                line.append(word);
                if (previous_length != 0U && widths.resolve(line) > width) {
                    line.resize(previous_length);
                    lines.push_back(line);
                    if (lines.size() >= line_limit) return lines;
                    line.assign(word);
                }
                cursor = word_end;
            }
            if (!line.empty()) {
                lines.push_back(line);
                if (lines.size() >= line_limit) return lines;
            } else if (paragraph.empty()) {
                lines.emplace_back();
            }
        }
        if (newline == std::string_view::npos) {
            break;
        }
        paragraph_start = newline + 1U;
    }
    if (lines.empty()) {
        lines.emplace_back();
    }
    return lines;
}

void paint_theme_cues(Painter& painter, Rect bounds,
                      const ControlVisualRecipe& recipe,
                      ControlVisualContext context) {
    PartPaint cue;
    cue.bounds = bounds;
    cue.role = PartRole::preselect;
    paint_part_from_recipe(painter, cue, recipe, context);
    cue.role = PartRole::focus;
    paint_part_from_recipe(painter, cue, recipe, context);
}

} // namespace gui_forms
