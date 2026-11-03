#pragma once

#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace gui_forms::collection_detail {

inline constexpr std::chrono::milliseconds type_timeout{850};

inline std::string fold_ascii(std::string_view text) {
    std::string result(text);
    for (char& character : result) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    return result;
}

// Whether `text`, ASCII case folded, starts with `folded_prefix` (already
// folded, as fold_ascii returns it). The same answer as
// fold_ascii(text).starts_with(folded_prefix), without allocating a folded
// copy, so a type-ahead search allocates nothing per item.
inline bool starts_with_folded(std::string_view text, std::string_view folded_prefix) noexcept {
    if (text.size() < folded_prefix.size()) return false;
    for (std::size_t index = 0; index < folded_prefix.size(); ++index) {
        char character = text[index];
        if (character >= 'A' && character <= 'Z') character = static_cast<char>(character - 'A' + 'a');
        if (character != folded_prefix[index]) return false;
    }
    return true;
}

// An item's identity names its semantic node, so it follows the StableId
// segment rule; its text is valid UTF-8.
inline void validate_identity_text(std::string_view id,
                                   std::string_view text) {
    if (!valid_stable_id(id) || !validate_utf8(text).valid()) {
        throw std::invalid_argument(
            "collection item identity must follow the StableId segment rule and its text must be valid UTF-8");
    }
}

inline Color with_alpha(Color color, std::uint8_t alpha) noexcept {
    color.alpha = alpha;
    return color;
}

inline Rect fit_image_rect(Rect bounds, Size source) noexcept {
    if (bounds.empty() || source.width <= 0.0 || source.height <= 0.0) return {};
    const double scale = std::min(bounds.width / source.width,
                                  bounds.height / source.height);
    const double width = source.width * scale;
    const double height = source.height * scale;
    return {bounds.x + (bounds.width - width) * 0.5,
            bounds.y + (bounds.height - height) * 0.5, width, height};
}

// Draws `text` at `origin` (on its baseline) as `run`'s text parts, each
// match of a term (ASCII case-folded, the longest at a position) on a
// selection part behind it (owner decision, 2026-11-02). `run` supplies the
// family, role, states, tone and font; its text and bounds are replaced.
inline void paint_emphasized_text(Painter& painter, const Control& control, Point origin,
                                  std::string_view text, std::span<const std::string> terms,
                                  PartPaint run) {
    if (text.empty()) return;
    if (terms.empty()) {
        run.text = text;
        paint_text_part_at(painter, control, run, origin, 0.0);
        return;
    }
    const std::string folded = fold_ascii(text);
    std::size_t cursor{};
    double x = origin.x;
    while (cursor < text.size()) {
        std::size_t next = text.size();
        std::size_t length{};
        for (const std::string& term : terms) {
            if (term.empty()) continue;
            const std::string folded_term = fold_ascii(term);
            const std::size_t found = folded.find(folded_term, cursor);
            if (found < next || (found == next && folded_term.size() > length)) {
                next = found;
                length = folded_term.size();
            }
        }
        if (next == std::string::npos || next >= text.size()) {
            run.text = text.substr(cursor);
            run.measured_text.reset();
            paint_text_part_at(painter, control, run, {x, origin.y}, 0.0);
            break;
        }
        if (next > cursor) {
            run.text = text.substr(cursor, next - cursor);
            const Size prefix = painter.measure_text_utf8(run.text, run.font);
            run.measured_text = prefix;
            paint_text_part_at(painter, control, run, {x, origin.y}, prefix.width);
            x += prefix.width;
        }
        run.text = text.substr(next, length);
        const Size emphasized = painter.measure_text_utf8(run.text, run.font);
        PartPaint highlight;
        highlight.family = ControlVisualRole::selection;
        highlight.role = PartRole::selection;
        highlight.bounds = {x - 1.0, origin.y - run.font.size, emphasized.width + 2.0,
                            run.font.size + 3.0};
        highlight.states = run.states;
        highlight.states.insert(AnatomyState::selected);
        paint_part(painter, control, highlight);
        run.measured_text = emphasized;
        paint_text_part_at(painter, control, run, {x, origin.y}, emphasized.width);
        x += emphasized.width;
        cursor = next + length;
    }
}

} // namespace gui_forms::collection_detail
