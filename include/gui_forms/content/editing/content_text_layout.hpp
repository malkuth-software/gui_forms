#pragma once

// The layout a consumer supplies for the content editing mode (owner
// decisions 2026-11-13, "Content editing mode and the integrity boundary"):
// the consumer lays out and renders text with its own engine and fonts, and
// hands GUI.Forms the positions of its lines, clusters and carets. GUI.Forms
// keeps the editing logic (ContentTextEditor). Everything here is validated
// against the editor's text before use (validate_content_text_layout).

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/text/types/text_types.hpp"
#include "gui_forms/types.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace gui_forms {

class TextStore;

// The most lines, clusters or caret stops one layout may hold: one million
// each. A layout holding more is refused before it is read further.
inline constexpr std::size_t maximum_content_layout_entries = 1000000U;

// One visual line: the UTF-8 range of the text it shows (without the line
// break that ends it) and its box, in the editor's local coordinates.
struct GUI_FORMS_API ContentTextLine final {
    Utf8Range range{};
    Rect bounds{};
};

// One shaped cluster (one or more graphemes drawn as a unit, such as a
// ligature): its UTF-8 range, the index of its line in
// ContentTextLayout::lines, its box and its direction. The caret stands at
// its leading edge before it and at its trailing edge after it: left and
// right for left-to-right clusters, right and left for right-to-left ones.
struct GUI_FORMS_API ContentTextCluster final {
    Utf8Range range{};
    std::uint32_t line{};
    Rect bounds{};
    bool right_to_left{};
};

// An explicit caret position, for a consumer whose carets do not stand at
// cluster edges (split carets, vertical text): the caret at `offset` is the
// rectangle `caret` (a bar, usually one unit wide), in local coordinates.
struct GUI_FORMS_API ContentCaretStop final {
    Utf8Offset offset{};
    Rect caret{};
};

// A whole layout for one revision of the editor's text
// (ContentTextEditor::text_revision). Lines and clusters are in text order;
// caret stops, when given, ascend by offset and override the edge-derived
// caret at their offsets.
struct GUI_FORMS_API ContentTextLayout final {
    std::uint64_t text_revision{};
    std::vector<ContentTextLine> lines{};
    std::vector<ContentTextCluster> clusters{};
    std::vector<ContentCaretStop> carets{};
};

// What validation found. Every result but `accepted` leaves the editor's
// previous layout in force.
enum class ContentLayoutResult : std::uint8_t {
    accepted,
    // Made for another revision of the text than the current one.
    stale_revision,
    // More than maximum_content_layout_entries lines, clusters or carets.
    refused_too_large,
    // A range past the end of the text, reversed, or a line or cluster out
    // of text order or overlapping the one before.
    refused_range,
    // A range or caret offset that is not a grapheme boundary of the text.
    refused_boundary,
    // A cluster naming no line, lying outside its line's range, or empty;
    // or a line whose bytes its clusters do not cover contiguously.
    refused_cluster,
    // A box that is not finite or has a negative size.
    refused_geometry,
    // Caret stops not ascending by offset.
    refused_caret,
};

[[nodiscard]] GUI_FORMS_API std::string_view content_layout_result_name(ContentLayoutResult result) noexcept;

// Checks `layout` against `text` (the editor's text store) without keeping
// either: counts first, then every range against the text's size and
// grapheme boundaries, the order of lines and clusters, the coverage of each
// line by its clusters, and every box. `revision` is the text's revision the
// layout must name.
[[nodiscard]] GUI_FORMS_API ContentLayoutResult validate_content_text_layout(const ContentTextLayout& layout,
                                                                             const TextStore& text,
                                                                             std::uint64_t revision);

} // namespace gui_forms
