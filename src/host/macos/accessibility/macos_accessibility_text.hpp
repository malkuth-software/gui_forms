#pragma once

// -- editable text (stage A, A-T1) -------------------------------------------
//
// A control's own text box that publishes a text selection (TextBox,
// MaskedTextBox, TokenBox's editor; a password box publishes none, A-T2)
// publishes AppKit's text attributes and parameterized attributes. AppKit
// counts text in UTF-16 code units (NSRange) and the core in UTF-8 bytes:
// every range crosses here, and a range that falls inside a UTF-16
// surrogate pair widens to the whole scalar. Lines are visual lines, each
// running to the next line's start (so it holds its break); a composed
// character is a grapheme; the text has one style run.
//
// These are the conversions and the layout queries the accessibility
// element's text attributes use (macos_accessibility_text.mm).

#include "../application/macos_host_private.hpp"

namespace gui_forms::host::macos {

// The bytes of the UTF-8 sequence `lead` starts; 1 for a byte that starts
// none.
std::size_t utf8_sequence_bytes(unsigned char lead) noexcept;

// The UTF-16 code units of value[0, bytes).
std::size_t utf16_units_before(std::string_view value, std::size_t bytes) noexcept;

// How a UTF-16 position inside a surrogate pair maps to a byte offset.
enum class SurrogateRounding : std::uint8_t {
    down,
    up,
};

// The byte offset in `value` of UTF-16 unit `units`: the end of the value
// past its last unit, and inside a surrogate pair the start (down) or the
// end (up) of its scalar.
std::size_t utf8_offset_at_units(std::string_view value, std::size_t units,
                                 SurrogateRounding rounding) noexcept;

// [start, end) of `value`, in bytes, as an NSRange in UTF-16 code units.
NSRange utf16_range(std::string_view value, std::size_t start, std::size_t end) noexcept;

// `range`, in UTF-16 code units, as bytes [start, end) of `value`. Returns
// false, leaving both unchanged, when the range starts past the text's end
// or its end overflows; a range that runs past the end stops there.
bool utf8_range(std::string_view value, NSRange range, std::size_t& start, std::size_t& end) noexcept;

// The visual line that holds byte `offset`: the last line starting at or
// before it, so a line break belongs to the line it ends. A binary search
// (N101): the lines start in ascending order.
std::size_t line_of_offset(const SemanticTextLayout& layout, std::size_t offset) noexcept;

// The box of the caret at byte `offset`, one point wide: at the left edge
// of the grapheme starting there, else at the right edge of the grapheme
// ending there, else at the start of the line that holds it. None for a
// layout without lines.
std::optional<GFRect> caret_box(const SemanticTextLayout& layout, std::size_t offset) noexcept;

// The union of the boxes of the graphemes within bytes [start, end), or the
// caret's box for an empty range; none when no grapheme lies within.
std::optional<GFRect> range_box(const SemanticTextLayout& layout, std::size_t start,
                                std::size_t end) noexcept;

// The grapheme at the point (x, y), in the view's semantic coordinates: on
// the line whose band holds y, or the nearest line, the grapheme whose
// horizontal extent holds x, or the nearest one. Null when the text has
// none. Borrows `layout`.
const SemanticTextGrapheme* grapheme_at_point(const SemanticTextLayout& layout, double x,
                                              double y) noexcept;
const SemanticTextGrapheme* grapheme_at_point(SemanticTextLayout&&, double, double) = delete;

// Whether `selector` is one of the text attributes above.
bool text_range_selector(SEL selector) noexcept;

} // namespace gui_forms::host::macos

using gui_forms::host::macos::grapheme_at_point;
using gui_forms::host::macos::line_of_offset;
using gui_forms::host::macos::range_box;
using gui_forms::host::macos::SurrogateRounding;
using gui_forms::host::macos::text_range_selector;
using gui_forms::host::macos::utf16_range;
using gui_forms::host::macos::utf16_units_before;
using gui_forms::host::macos::utf8_offset_at_units;
using gui_forms::host::macos::utf8_range;
using gui_forms::host::macos::utf8_sequence_bytes;
