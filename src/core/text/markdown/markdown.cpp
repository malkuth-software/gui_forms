#include "gui_forms/text/markdown/markdown.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <climits>
#include <functional>
#include <new>
#include <stdexcept>
#include <system_error>
#include <utility>

// MD4C (stx third_party/md4c, MIT): the CommonMark parser. It calls back into
// MarkdownParser with text that points into the source or at its own static
// strings; nothing it passes is retained past a callback.
#include "md4c.h"
extern "C" {
#include "entity.h"
}

namespace gui_forms {

bool operator==(const MarkdownRgb& left, const MarkdownRgb& right) noexcept {
    const bool equal = left.red == right.red && left.green == right.green && left.blue == right.blue;
    return equal;
}

bool operator==(const MarkdownSpanStyle& left, const MarkdownSpanStyle& right) noexcept {
    const bool equal = left.bold == right.bold && left.italic == right.italic &&
                       left.underline == right.underline && left.strikethrough == right.strikethrough &&
                       left.code == right.code && left.baseline == right.baseline &&
                       left.colour == right.colour && left.background == right.background &&
                       left.size_step == right.size_step;
    return equal;
}

void MarkdownDocument::clear() noexcept {
    blocks.clear();
    spans.clear();
    links.clear();
}

MarkdownParseResult::MarkdownParseResult(std::variant<Parsed, Failed> state) noexcept : state_(state) {}

MarkdownParseResult MarkdownParseResult::parsed() noexcept {
    const MarkdownParseResult result{std::variant<Parsed, Failed>(Parsed{})};
    return result;
}

MarkdownParseResult MarkdownParseResult::failed(MarkdownParseError error, Utf8Offset offset) noexcept {
    const Failed failure{error, offset};
    const MarkdownParseResult result{std::variant<Parsed, Failed>(failure)};
    return result;
}

bool MarkdownParseResult::succeeded() const noexcept {
    const bool parsed = std::holds_alternative<Parsed>(state_);
    return parsed;
}

std::optional<MarkdownParseError> MarkdownParseResult::error() const noexcept {
    const Failed* failure = std::get_if<Failed>(&state_);
    if (failure == nullptr) return std::nullopt;
    return (*failure).error;
}

Utf8Offset MarkdownParseResult::offset() const noexcept {
    const Failed* failure = std::get_if<Failed>(&state_);
    if (failure == nullptr) return Utf8Offset(0U);
    return (*failure).offset;
}

namespace {

// Appends the UTF-8 encoding of scalar, or of U+FFFD for a value that is
// not a scalar (zero, a surrogate or past U+10FFFF).
void append_scalar(std::string& text, unsigned scalar) {
    if (scalar == 0U || scalar > 0x10FFFFU || (scalar >= 0xD800U && scalar <= 0xDFFFU)) scalar = 0xFFFDU;
    if (scalar < 0x80U) {
        text.push_back(static_cast<char>(scalar));
    } else if (scalar < 0x800U) {
        text.push_back(static_cast<char>(0xC0U | (scalar >> 6U)));
        text.push_back(static_cast<char>(0x80U | (scalar & 0x3FU)));
    } else if (scalar < 0x10000U) {
        text.push_back(static_cast<char>(0xE0U | (scalar >> 12U)));
        text.push_back(static_cast<char>(0x80U | ((scalar >> 6U) & 0x3FU)));
        text.push_back(static_cast<char>(0x80U | (scalar & 0x3FU)));
    } else {
        text.push_back(static_cast<char>(0xF0U | (scalar >> 18U)));
        text.push_back(static_cast<char>(0x80U | ((scalar >> 12U) & 0x3FU)));
        text.push_back(static_cast<char>(0x80U | ((scalar >> 6U) & 0x3FU)));
        text.push_back(static_cast<char>(0x80U | (scalar & 0x3FU)));
    }
}

// Appends what a character reference stands for: a numeric reference's
// scalar, a named reference's one or two scalars, or the reference itself
// when MD4C passed one it does not resolve.
void append_entity(std::string& text, std::string_view reference) {
    if (reference.starts_with("&#") && reference.ends_with(';')) {
        std::string_view digits = reference.substr(2U, reference.size() - 3U);
        int radix = 10;
        if (digits.starts_with('x') || digits.starts_with('X')) {
            digits.remove_prefix(1U);
            radix = 16;
        }
        unsigned scalar = 0U;
        const char* first = digits.data();
        const char* last = digits.data() + digits.size();
        const std::from_chars_result read = std::from_chars(first, last, scalar, radix);
        if (read.ec == std::errc() && read.ptr == last && !digits.empty()) {
            append_scalar(text, scalar);
        } else {
            text.append(reference);
        }
        return;
    }
    const ENTITY* entity = entity_lookup(reference.data(), reference.size());
    if (entity == nullptr) {
        text.append(reference);
        return;
    }
    append_scalar(text, (*entity).codepoints[0]);
    if ((*entity).codepoints[1] != 0U) append_scalar(text, (*entity).codepoints[1]);
}

// Where a piece of a block's text came from, which decides how it maps to
// the source.
enum class PieceOrigin : std::uint8_t {
    // Source bytes shown as they are: text equals source.
    literal,
    // A character reference or a NUL: text is the source decoded.
    decoded,
    // A soft line break, a hard line break or a code block's line feed:
    // source is the line ending.
    line_ending,
    // Text MD4C supplies: source is empty where it stands.
    supplied,
};

// One callback's text, as the block being built collects it.
struct Piece final {
    PieceOrigin origin{PieceOrigin::literal};
    MarkdownSpanKind kind{MarkdownSpanKind::text};
    std::string text{};
    Utf8Range source{};
    MarkdownSpanStyle style{};
    std::optional<std::size_t> link{};
};

// A character of literal text that may be dialect markup: an unescaped
// [ ] { } outside code, or a soft line break ('\n'). position is its byte in
// the block's joined piece text; source is its source byte.
struct Mark final {
    std::size_t position{};
    std::size_t source{};
    char character{};
};

// The attributes one [content]{list} span gives its content (stx_rtf2md
// README, attribute spans).
struct SpanAttributes final {
    bool underline{};
    bool bold{};
    bool italic{};
    bool strikethrough{};
    MarkdownBaseline baseline{MarkdownBaseline::normal};
    std::optional<MarkdownRgb> colour{};
    std::optional<MarkdownRgb> background{};
    std::int8_t size_step{};
};

// A recognised attribute span over joined text positions: content is
// (open, close), and close through end (the `}`) is its markup.
struct SpanPair final {
    std::size_t open{};
    std::size_t close{};
    std::size_t end{};
    SpanAttributes attributes{};
};

// Orders pairs outer first: by opening position, the longer first.
struct OuterFirst final {
    bool operator()(const SpanPair& left, const SpanPair& right) const noexcept {
        if (left.open != right.open) return left.open < right.open;
        return left.end > right.end;
    }
};

// What the dialect gives one byte of a block's joined text: removed when
// it is attribute markup, otherwise the attributes of the spans around it.
struct ByteStyle final {
    bool removed{};
    SpanAttributes attributes{};
};

[[nodiscard]] bool same_attributes(const SpanAttributes& left, const SpanAttributes& right) noexcept {
    const bool equal = left.underline == right.underline && left.bold == right.bold &&
                       left.italic == right.italic && left.strikethrough == right.strikethrough &&
                       left.baseline == right.baseline && left.colour == right.colour &&
                       left.background == right.background && left.size_step == right.size_step;
    return equal;
}

[[nodiscard]] unsigned hex_digit(char character) noexcept {
    if (character >= '0' && character <= '9') return static_cast<unsigned>(character - '0');
    if (character >= 'a' && character <= 'f') return static_cast<unsigned>(character - 'a') + 10U;
    if (character >= 'A' && character <= 'F') return static_cast<unsigned>(character - 'A') + 10U;
    return 16U;
}

// Reads #rrggbb, digits in either case.
[[nodiscard]] bool parse_colour(std::string_view text, MarkdownRgb& colour) noexcept {
    if (text.size() != 7U || text[0] != '#') return false;
    std::array<unsigned, 6> digits{};
    for (std::size_t index = 0U; index < digits.size(); ++index) {
        digits[index] = hex_digit(text[index + 1U]);
        if (digits[index] > 15U) return false;
    }
    colour.red = static_cast<std::uint8_t>(digits[0] * 16U + digits[1]);
    colour.green = static_cast<std::uint8_t>(digits[2] * 16U + digits[3]);
    colour.blue = static_cast<std::uint8_t>(digits[4] * 16U + digits[5]);
    return true;
}

// The attribute list grammar: one or more items separated by spaces, each
// at most once, sup and sub not together. Anything else is not a list.
[[nodiscard]] bool parse_attributes(std::string_view list, SpanAttributes& attributes) noexcept {
    bool seen_underline = false;
    bool seen_baseline = false;
    bool seen_colour = false;
    bool seen_background = false;
    bool seen_size = false;
    bool seen_bold = false;
    bool seen_italic = false;
    bool seen_strikethrough = false;
    bool any = false;
    std::size_t start = 0U;
    while (start < list.size()) {
        if (list[start] == ' ') {
            ++start;
            continue;
        }
        std::size_t end = list.find(' ', start);
        if (end == std::string_view::npos) end = list.size();
        const std::string_view item = list.substr(start, end - start);
        start = end;
        any = true;
        if (item == "u" && !seen_underline) {
            seen_underline = true;
            attributes.underline = true;
        } else if ((item == "sup" || item == "sub") && !seen_baseline) {
            seen_baseline = true;
            attributes.baseline = item == "sup" ? MarkdownBaseline::superscript : MarkdownBaseline::subscript;
        } else if (item == "b" && !seen_bold) {
            seen_bold = true;
            attributes.bold = true;
        } else if (item == "i" && !seen_italic) {
            seen_italic = true;
            attributes.italic = true;
        } else if (item == "s" && !seen_strikethrough) {
            seen_strikethrough = true;
            attributes.strikethrough = true;
        } else if (item.starts_with("color=") && !seen_colour) {
            MarkdownRgb colour{};
            if (!parse_colour(item.substr(6U), colour)) return false;
            seen_colour = true;
            attributes.colour = colour;
        } else if (item.starts_with("bg=") && !seen_background) {
            MarkdownRgb colour{};
            if (!parse_colour(item.substr(3U), colour)) return false;
            seen_background = true;
            attributes.background = colour;
        } else if (item.size() == 7U && item.starts_with("size=") && !seen_size) {
            const char sign = item[5];
            const char digit = item[6];
            const bool raise = sign == '+' && digit >= '1' && digit <= '4';
            const bool lower = sign == '-' && digit >= '1' && digit <= '2';
            if (!raise && !lower) return false;
            seen_size = true;
            const std::int8_t step = static_cast<std::int8_t>(digit - '0');
            attributes.size_step = raise ? step : static_cast<std::int8_t>(-step);
        } else {
            return false;
        }
    }
    return any;
}

// Adds an inner span's attributes to the outer ones a byte already has: the
// on-or-off items add, a colour, background, baseline or size replaces.
void add_attributes(SpanAttributes& style, const SpanAttributes& own) noexcept {
    style.underline = style.underline || own.underline;
    style.bold = style.bold || own.bold;
    style.italic = style.italic || own.italic;
    style.strikethrough = style.strikethrough || own.strikethrough;
    if (own.baseline != MarkdownBaseline::normal) style.baseline = own.baseline;
    if (own.colour.has_value()) style.colour = own.colour;
    if (own.background.has_value()) style.background = own.background;
    if (own.size_step != 0) style.size_step = own.size_step;
}

// The list a list item belongs to, while MD4C is inside it.
struct ListState final {
    MarkdownListMarker marker{MarkdownListMarker::bullet};
    std::uint32_t next{1U};
};

// The list item MD4C has entered whose first block has not begun yet.
struct PendingItem final {
    MarkdownListMarker marker{MarkdownListMarker::bullet};
    std::size_t level{};
    std::uint32_t counted{};
};

// The MD4C callback context for one parse. Callbacks return nonzero to stop
// MD4C; failure then names why. The parser borrows source, document and
// limits for the call and keeps nothing after it.
struct MarkdownParser final {
    std::string_view source{};
    MarkdownDocument* document{};
    const MarkdownLimits* limits{};
    std::stop_token cancellation{};
    std::optional<MarkdownParseError> failure{};

    // The block being built and its pieces and marks.
    bool building{};
    MarkdownBlock block{};
    std::vector<Piece> pieces{};
    std::vector<Mark> marks{};
    std::size_t joined_bytes{};

    std::vector<ListState> lists{};
    std::optional<PendingItem> pending_item{};
    std::vector<std::size_t> open_links{};
    std::size_t quotes{};
    std::size_t strong{};
    std::size_t emphasis{};
    std::size_t code{};
    std::size_t strike{};
    // An attribute-only paragraph's alignment, for the next block.
    std::optional<MarkdownAlignment> pending_alignment{};
    // The end of the last source byte a piece mapped; text MD4C supplies
    // stands here.
    std::size_t cursor{};
    std::size_t text_bytes{};

    [[nodiscard]] bool stop(MarkdownParseError error) noexcept {
        if (!failure.has_value()) failure = error;
        return false;
    }

    [[nodiscard]] bool cancelled() const noexcept {
        const bool stop_requested = cancellation.stop_requested();
        return stop_requested;
    }

    // The source range text occupies, or none when MD4C passed one of its
    // own strings.
    [[nodiscard]] std::optional<Utf8Range> source_range(const MD_CHAR* text, MD_SIZE size) const noexcept {
        // MD4C's own strings are separate objects: compare with the total
        // pointer order.
        const std::less<const char*> before{};
        const char* begin = source.data();
        const char* end = begin + source.size();
        if (before(text, begin) || !before(text, end)) return std::nullopt;
        const std::size_t start = static_cast<std::size_t>(text - begin);
        if (size > source.size() - start) return std::nullopt;
        const Utf8Range range{Utf8Offset(start), Utf8Offset(start + size)};
        return range;
    }

    // The first line ending at or after the cursor ("\n", "\r\n" or "\r"),
    // or an empty range at the end of the source.
    [[nodiscard]] Utf8Range next_line_ending() const noexcept {
        std::size_t position = std::min(cursor, source.size());
        while (position < source.size() && source[position] != '\n' && source[position] != '\r') ++position;
        std::size_t end = position;
        if (end < source.size()) {
            ++end;
            if (source[position] == '\r' && end < source.size() && source[end] == '\n') ++end;
        }
        const Utf8Range range{Utf8Offset(position), Utf8Offset(end)};
        return range;
    }

    [[nodiscard]] MarkdownSpanStyle current_style() const noexcept {
        MarkdownSpanStyle style{};
        style.bold = strong != 0U;
        style.italic = emphasis != 0U;
        style.strikethrough = strike != 0U;
        style.code = code != 0U || (building && block.kind == MarkdownBlockKind::code_block);
        return style;
    }

    // Starts a block of kind. A paragraph that is the first block of a list
    // item becomes the list item.
    [[nodiscard]] bool begin(MarkdownBlockKind kind, std::size_t level) {
        if (!finish()) return false;
        if ((*document).blocks.size() >= (*limits).blocks) return stop(MarkdownParseError::limit_exceeded);
        block = MarkdownBlock{};
        block.kind = kind;
        block.level = level;
        block.quote_depth = quotes;
        if (pending_item.has_value()) {
            if (kind == MarkdownBlockKind::paragraph) {
                block.kind = MarkdownBlockKind::list_item;
                block.level = (*pending_item).level;
                block.marker = (*pending_item).marker;
                if (block.marker == MarkdownListMarker::ordered) block.ordinal = (*pending_item).counted;
            }
            pending_item.reset();
        }
        building = true;
        return true;
    }

    // Adds one callback's text to the block being built, starting a
    // paragraph when there is none (a tight list item's text).
    [[nodiscard]] bool append(PieceOrigin origin, MarkdownSpanKind kind, std::string&& text, Utf8Range range) {
        if (!building && !begin(MarkdownBlockKind::paragraph, 0U)) return false;
        if (pieces.size() >= (*limits).spans) return stop(MarkdownParseError::limit_exceeded);
        Piece piece{};
        piece.origin = origin;
        piece.kind = kind;
        piece.text = std::move(text);
        piece.source = range;
        piece.style = current_style();
        if (!open_links.empty()) piece.link = open_links.back();
        joined_bytes += piece.text.size();
        pieces.push_back(std::move(piece));
        cursor = std::max(cursor, range.end.value());
        return true;
    }

    // Literal source text: records its dialect markup characters.
    [[nodiscard]] bool append_literal(Utf8Range range, bool markable) {
        const std::size_t start = range.start.value();
        const std::size_t size = range.end.value() - start;
        const std::size_t base = joined_bytes;
        std::string text(source.substr(start, size));
        if (!append(PieceOrigin::literal, MarkdownSpanKind::text, std::move(text), range)) return false;
        if (!markable) return true;
        for (std::size_t index = 0U; index < size; ++index) {
            const char character = source[start + index];
            if (character != '[' && character != ']' && character != '{' && character != '}') continue;
            std::size_t backslashes = 0U;
            while (backslashes < start + index && source[start + index - backslashes - 1U] == '\\') ++backslashes;
            if (backslashes % 2U != 0U) continue;
            const Mark mark{base + index, start + index, character};
            marks.push_back(mark);
        }
        return true;
    }

    [[nodiscard]] bool append_line_ending(MarkdownSpanKind kind, bool soft) {
        const Utf8Range range = next_line_ending();
        const std::size_t position = joined_bytes;
        std::string text{};
        if (soft) text = " ";
        if (kind == MarkdownSpanKind::text && !soft) text = "\n";
        if (!append(PieceOrigin::line_ending, kind, std::move(text), range)) return false;
        if (soft) {
            const Mark mark{position, range.start.value(), '\n'};
            marks.push_back(mark);
        }
        return true;
    }

    // Whether joined text from first to last (inclusive) is literal source
    // text, byte for byte in order, so markup found in it was written as
    // one piece of source.
    [[nodiscard]] static bool source_adjacent(const Mark& first, const Mark& last) noexcept {
        const bool adjacent = last.source - first.source == last.position - first.position;
        return adjacent;
    }

    // Applies the dialect to the block that is ending and moves its spans
    // into the document: a leading block attribute line sets its alignment
    // (or, alone, the next block's), and each [content]{list} span styles
    // its content and makes its brackets and list markup.
    [[nodiscard]] bool finish() {
        if (!building) return true;
        building = false;
        if (block.kind == MarkdownBlockKind::code_block && !pieces.empty() &&
            pieces.back().origin == PieceOrigin::line_ending) {
            joined_bytes -= pieces.back().text.size();
            pieces.pop_back();
        }
        std::string joined{};
        joined.reserve(joined_bytes);
        std::vector<std::size_t> starts(pieces.size() + 1U, 0U);
        for (std::size_t index = 0U; index < pieces.size(); ++index) {
            starts[index] = joined.size();
            joined.append(pieces[index].text);
        }
        starts[pieces.size()] = joined.size();
        std::vector<ByteStyle> styles(joined.size());
        std::size_t first_mark = 0U;
        const bool alignable = block.kind == MarkdownBlockKind::paragraph ||
                               block.kind == MarkdownBlockKind::heading ||
                               block.kind == MarkdownBlockKind::list_item;
        if (pending_alignment.has_value()) {
            if (alignable) block.alignment = *pending_alignment;
            pending_alignment.reset();
        }
        if (marks.size() >= 2U && marks[0].character == '{' && marks[0].position == 0U &&
            marks[1].character == '}' && source_adjacent(marks[0], marks[1])) {
            const std::string_view line(joined.data(), marks[1].position + 1U);
            std::optional<MarkdownAlignment> alignment{};
            if (line == "{align=left}") alignment = MarkdownAlignment::left;
            if (line == "{align=center}") alignment = MarkdownAlignment::center;
            if (line == "{align=right}") alignment = MarkdownAlignment::right;
            const bool alone = marks[1].position + 1U == joined.size();
            const bool line_ends = marks.size() >= 3U && marks[2].character == '\n' &&
                                   marks[2].position == marks[1].position + 1U;
            if (alignment.has_value() && alone && block.kind == MarkdownBlockKind::paragraph) {
                pending_alignment = alignment;
                pieces.clear();
                marks.clear();
                joined_bytes = 0U;
                return true;
            }
            if (alignment.has_value() && line_ends && alignable) {
                block.alignment = *alignment;
                for (std::size_t index = 0U; index <= marks[2].position; ++index) styles[index].removed = true;
                first_mark = 3U;
            }
        }
        std::vector<std::size_t> opens{};
        std::vector<SpanPair> pairs{};
        for (std::size_t index = first_mark; index < marks.size(); ++index) {
            const char character = marks[index].character;
            if (character == '[') {
                opens.push_back(index);
                continue;
            }
            if (character != ']' || opens.empty()) continue;
            const std::size_t open = opens.back();
            opens.pop_back();
            const bool brace = index + 2U < marks.size() && marks[index + 1U].character == '{' &&
                               marks[index + 1U].position == marks[index].position + 1U &&
                               marks[index + 2U].character == '}' &&
                               source_adjacent(marks[index], marks[index + 2U]);
            if (!brace || marks[open].position + 1U == marks[index].position) continue;
            const std::size_t list_start = marks[index].position + 2U;
            const std::string_view list(joined.data() + list_start, marks[index + 2U].position - list_start);
            SpanAttributes attributes{};
            if (!parse_attributes(list, attributes)) continue;
            const SpanPair pair{marks[open].position, marks[index].position, marks[index + 2U].position, attributes};
            pairs.push_back(pair);
            index += 2U;
        }
        std::sort(pairs.begin(), pairs.end(), OuterFirst{});
        for (const SpanPair& pair : pairs) {
            styles[pair.open].removed = true;
            for (std::size_t index = pair.close; index <= pair.end; ++index) styles[index].removed = true;
            for (std::size_t index = pair.open + 1U; index < pair.close; ++index) {
                add_attributes(styles[index].attributes, pair.attributes);
            }
        }
        marks.clear();
        const bool kept = publish(joined, starts, styles);
        pieces.clear();
        joined_bytes = 0U;
        return kept;
    }

    // The span style a piece's byte takes: the piece's own (from emphasis,
    // strikethrough and code) with the attribute spans' added.
    [[nodiscard]] static MarkdownSpanStyle byte_style(const Piece& piece, const ByteStyle& style) noexcept {
        MarkdownSpanStyle result = piece.style;
        const SpanAttributes& attributes = style.attributes;
        result.bold = result.bold || attributes.bold;
        result.italic = result.italic || attributes.italic;
        result.strikethrough = result.strikethrough || attributes.strikethrough;
        result.underline = attributes.underline;
        result.baseline = attributes.baseline;
        result.colour = attributes.colour;
        result.background = attributes.background;
        result.size_step = attributes.size_step;
        return result;
    }

    // Adds one unit (a literal byte or a whole other piece) to the
    // document's spans, extending the last span of the block when the unit
    // continues it: same kind of text, same style and link, contiguous
    // source.
    [[nodiscard]] bool add_unit(const Piece& piece, std::string_view text, Utf8Range range,
                                const MarkdownSpanStyle& style, std::size_t block_first) {
        std::vector<MarkdownSpan>& spans = (*document).spans;
        if (spans.size() > block_first) {
            MarkdownSpan& last = spans.back();
            const bool continues = last.kind == MarkdownSpanKind::text && piece.kind == MarkdownSpanKind::text &&
                                   last.style == style && last.link == piece.link && last.source.end == range.start;
            if (continues) {
                if (text.size() > (*limits).text_bytes - text_bytes) return stop(MarkdownParseError::limit_exceeded);
                last.text.append(text);
                last.source.end = range.end;
                text_bytes += text.size();
                return true;
            }
        }
        if (spans.size() >= (*limits).spans) return stop(MarkdownParseError::limit_exceeded);
        if (text.size() > (*limits).text_bytes - text_bytes) return stop(MarkdownParseError::limit_exceeded);
        MarkdownSpan span{};
        span.kind = piece.kind;
        span.text.assign(text);
        span.source = range;
        span.style = style;
        span.link = piece.link;
        spans.push_back(std::move(span));
        text_bytes += text.size();
        return true;
    }

    // Moves the block and its rebuilt spans into the document.
    [[nodiscard]] bool publish(const std::string& joined, const std::vector<std::size_t>& starts,
                               const std::vector<ByteStyle>& styles) {
        const std::size_t block_first = (*document).spans.size();
        for (std::size_t index = 0U; index < pieces.size(); ++index) {
            const Piece& piece = pieces[index];
            const std::size_t start = starts[index];
            const std::size_t end = starts[index + 1U];
            if (piece.origin != PieceOrigin::literal) {
                const bool removed = start < end && styles[start].removed;
                if (removed) continue;
                const ByteStyle plain{};
                const ByteStyle& style = start < end ? styles[start] : plain;
                const MarkdownSpanStyle span_style = byte_style(piece, style);
                const std::string_view text(joined.data() + start, end - start);
                if (!add_unit(piece, text, piece.source, span_style, block_first)) return false;
                continue;
            }
            for (std::size_t byte = start; byte < end; ++byte) {
                if (styles[byte].removed) continue;
                // A run of bytes in one style is one unit, so the span texts
                // are appended a run at a time.
                std::size_t run_end = byte + 1U;
                while (run_end < end && !styles[run_end].removed &&
                       same_attributes(styles[run_end].attributes, styles[byte].attributes)) {
                    ++run_end;
                }
                const std::size_t source_start = piece.source.start.value() + (byte - start);
                const Utf8Range range{Utf8Offset(source_start), Utf8Offset(source_start + (run_end - byte))};
                const std::string_view text(joined.data() + byte, run_end - byte);
                const MarkdownSpanStyle span_style = byte_style(piece, styles[byte]);
                if (!add_unit(piece, text, range, span_style, block_first)) return false;
                byte = run_end - 1U;
            }
        }
        block.first_span = block_first;
        block.span_count = (*document).spans.size() - block_first;
        (*document).blocks.push_back(block);
        return true;
    }

    [[nodiscard]] bool enter_block(MD_BLOCKTYPE type, void* detail) {
        switch (type) {
        case MD_BLOCK_DOC:
            return true;
        case MD_BLOCK_QUOTE:
            if (!finish()) return false;
            ++quotes;
            return true;
        case MD_BLOCK_UL: {
            if (!finish()) return false;
            const ListState list{MarkdownListMarker::bullet, 1U};
            lists.push_back(list);
            return true;
        }
        case MD_BLOCK_OL: {
            if (!finish()) return false;
            const MD_BLOCK_OL_DETAIL& ordered = *static_cast<const MD_BLOCK_OL_DETAIL*>(detail);
            const ListState list{MarkdownListMarker::ordered, ordered.start};
            lists.push_back(list);
            return true;
        }
        case MD_BLOCK_LI: {
            if (!finish()) return false;
            PendingItem item{};
            if (!lists.empty()) {
                item.marker = lists.back().marker;
                item.level = lists.size() - 1U;
                item.counted = lists.back().next;
                if (lists.back().next < UINT32_MAX) ++lists.back().next;
            }
            pending_item = item;
            return true;
        }
        case MD_BLOCK_HR:
            return begin(MarkdownBlockKind::rule, 0U);
        case MD_BLOCK_H: {
            const MD_BLOCK_H_DETAIL& heading = *static_cast<const MD_BLOCK_H_DETAIL*>(detail);
            return begin(MarkdownBlockKind::heading, heading.level);
        }
        case MD_BLOCK_CODE:
            return begin(MarkdownBlockKind::code_block, 0U);
        case MD_BLOCK_P:
            return begin(MarkdownBlockKind::paragraph, 0U);
        case MD_BLOCK_HTML:
        case MD_BLOCK_TABLE:
        case MD_BLOCK_THEAD:
        case MD_BLOCK_TBODY:
        case MD_BLOCK_TR:
        case MD_BLOCK_TH:
        case MD_BLOCK_TD:
        case MD_BLOCK_FOOTNOTE_DEF_SECTION:
        case MD_BLOCK_FOOTNOTE_DEF:
        case MD_BLOCK_ADMONITION:
        case MD_BLOCK_BLANK:
        default:
            // HTML blocks are off (MD_FLAG_NOHTML) and tables are off; any
            // other block MD4C adds is read through its text alone.
            return true;
        }
    }

    [[nodiscard]] bool leave_block(MD_BLOCKTYPE type) {
        switch (type) {
        case MD_BLOCK_QUOTE:
            if (!finish()) return false;
            if (quotes > 0U) --quotes;
            return true;
        case MD_BLOCK_UL:
        case MD_BLOCK_OL:
            if (!finish()) return false;
            if (!lists.empty()) lists.pop_back();
            return true;
        case MD_BLOCK_LI:
            pending_item.reset();
            return finish();
        case MD_BLOCK_HR:
        case MD_BLOCK_H:
        case MD_BLOCK_CODE:
        case MD_BLOCK_P:
            return finish();
        case MD_BLOCK_DOC:
        case MD_BLOCK_HTML:
        case MD_BLOCK_TABLE:
        case MD_BLOCK_THEAD:
        case MD_BLOCK_TBODY:
        case MD_BLOCK_TR:
        case MD_BLOCK_TH:
        case MD_BLOCK_TD:
        case MD_BLOCK_FOOTNOTE_DEF_SECTION:
        case MD_BLOCK_FOOTNOTE_DEF:
        case MD_BLOCK_ADMONITION:
        case MD_BLOCK_BLANK:
        default:
            return true;
        }
    }

    // A link destination, decoded from MD4C's attribute pieces. The pieces
    // are MD4C's for the callback only; the result is a copy.
    [[nodiscard]] bool attribute_text(const MD_ATTRIBUTE& attribute, std::string& result) const {
        std::size_t index = 0U;
        MD_OFFSET offset = 0U;
        while (offset < attribute.size) {
            const MD_OFFSET end = attribute.substr_offsets[index + 1U];
            const std::string_view part(attribute.text + offset, end - offset);
            if (attribute.substr_types[index] == MD_TEXT_ENTITY) {
                append_entity(result, part);
            } else if (attribute.substr_types[index] == MD_TEXT_NULLCHAR) {
                append_scalar(result, 0xFFFDU);
            } else {
                result.append(part);
            }
            offset = end;
            ++index;
        }
        return true;
    }

    [[nodiscard]] bool enter_span(MD_SPANTYPE type, void* detail) {
        switch (type) {
        case MD_SPAN_STRONG:
            ++strong;
            return true;
        case MD_SPAN_EM:
            ++emphasis;
            return true;
        case MD_SPAN_CODE:
            ++code;
            return true;
        case MD_SPAN_DEL:
            ++strike;
            return true;
        case MD_SPAN_A: {
            const MD_SPAN_A_DETAIL& link = *static_cast<const MD_SPAN_A_DETAIL*>(detail);
            MarkdownLink entry{};
            if (!attribute_text(link.href, entry.target)) return false;
            if (entry.target.size() > (*limits).text_bytes - text_bytes) return stop(MarkdownParseError::limit_exceeded);
            text_bytes += entry.target.size();
            open_links.push_back((*document).links.size());
            (*document).links.push_back(std::move(entry));
            return true;
        }
        case MD_SPAN_IMG:
            // An image is its alternative text, which MD4C passes as text.
            return true;
        case MD_SPAN_INS:
        case MD_SPAN_U:
        case MD_SPAN_LATEXMATH:
        case MD_SPAN_LATEXMATH_DISPLAY:
        case MD_SPAN_WIKILINK:
        case MD_SPAN_SPOILER:
        case MD_SPAN_SUPERSCRIPT:
        case MD_SPAN_SUBSCRIPT:
        case MD_SPAN_FOOTNOTE_REF:
        case MD_SPAN_MARK:
        default:
            // Only enabled by flags this parser does not set.
            return true;
        }
    }

    [[nodiscard]] bool leave_span(MD_SPANTYPE type) {
        switch (type) {
        case MD_SPAN_STRONG:
            if (strong > 0U) --strong;
            return true;
        case MD_SPAN_EM:
            if (emphasis > 0U) --emphasis;
            return true;
        case MD_SPAN_CODE:
            if (code > 0U) --code;
            return true;
        case MD_SPAN_DEL:
            if (strike > 0U) --strike;
            return true;
        case MD_SPAN_A:
            if (!open_links.empty()) open_links.pop_back();
            return true;
        case MD_SPAN_IMG:
        case MD_SPAN_INS:
        case MD_SPAN_U:
        case MD_SPAN_LATEXMATH:
        case MD_SPAN_LATEXMATH_DISPLAY:
        case MD_SPAN_WIKILINK:
        case MD_SPAN_SPOILER:
        case MD_SPAN_SUPERSCRIPT:
        case MD_SPAN_SUBSCRIPT:
        case MD_SPAN_FOOTNOTE_REF:
        case MD_SPAN_MARK:
        default:
            return true;
        }
    }

    [[nodiscard]] bool add_text(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size) {
        const std::optional<Utf8Range> range = source_range(text, size);
        const Utf8Range here{Utf8Offset(cursor), Utf8Offset(cursor)};
        switch (type) {
        case MD_TEXT_SOFTBR:
            return append_line_ending(MarkdownSpanKind::text, true);
        case MD_TEXT_BR:
            return append_line_ending(MarkdownSpanKind::line_break, false);
        case MD_TEXT_NULLCHAR: {
            std::string replacement{};
            append_scalar(replacement, 0xFFFDU);
            return append(PieceOrigin::decoded, MarkdownSpanKind::text, std::move(replacement), range.value_or(here));
        }
        case MD_TEXT_ENTITY: {
            std::string decoded{};
            append_entity(decoded, std::string_view(text, size));
            return append(PieceOrigin::decoded, MarkdownSpanKind::text, std::move(decoded), range.value_or(here));
        }
        case MD_TEXT_NORMAL:
        case MD_TEXT_CODE:
        case MD_TEXT_HTML:
        case MD_TEXT_LATEXMATH:
        default: {
            if (size == 0U) return true;
            if (range.has_value()) {
                const bool markable = type == MD_TEXT_NORMAL && code == 0U;
                return append_literal(*range, markable);
            }
            // A code block's line feed stands for its line ending; other
            // supplied text stands where the cursor is.
            const std::string_view supplied(text, size);
            const bool code_block = building && block.kind == MarkdownBlockKind::code_block;
            if (code_block && supplied == "\n") {
                return append_line_ending(MarkdownSpanKind::text, false);
            }
            return append(PieceOrigin::supplied, MarkdownSpanKind::text, std::string(supplied), here);
        }
        }
    }

    // MD4C's callbacks. Each one is a C entry point: exceptions stay inside.
    // Their types are checked by the MD_PARSER field assignments.
    static int block_start(MD_BLOCKTYPE type, void* detail, void* context) noexcept {
        MarkdownParser& parser = *static_cast<MarkdownParser*>(context);
        const int result = parser.guard_block(type, detail, true);
        return result;
    }

    static int block_end(MD_BLOCKTYPE type, void* detail, void* context) noexcept {
        MarkdownParser& parser = *static_cast<MarkdownParser*>(context);
        const int result = parser.guard_block(type, detail, false);
        return result;
    }

    static int span_start(MD_SPANTYPE type, void* detail, void* context) noexcept {
        MarkdownParser& parser = *static_cast<MarkdownParser*>(context);
        const int result = parser.guard_span(type, detail, true);
        return result;
    }

    static int span_end(MD_SPANTYPE type, void* detail, void* context) noexcept {
        MarkdownParser& parser = *static_cast<MarkdownParser*>(context);
        const int result = parser.guard_span(type, detail, false);
        return result;
    }

    static int text_callback(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* context) noexcept {
        MarkdownParser& parser = *static_cast<MarkdownParser*>(context);
        const int result = parser.guard_text(type, text, size);
        return result;
    }

    [[nodiscard]] int guard_block(MD_BLOCKTYPE type, void* detail, bool entering) noexcept {
        if (cancelled()) {
            static_cast<void>(stop(MarkdownParseError::cancelled));
            return 1;
        }
        try {
            const bool kept = entering ? enter_block(type, detail) : leave_block(type);
            return kept ? 0 : 1;
        } catch (const std::bad_alloc&) {
            static_cast<void>(stop(MarkdownParseError::limit_exceeded));
        } catch (const std::length_error&) {
            static_cast<void>(stop(MarkdownParseError::limit_exceeded));
        }
        return 1;
    }

    [[nodiscard]] int guard_span(MD_SPANTYPE type, void* detail, bool entering) noexcept {
        if (cancelled()) {
            static_cast<void>(stop(MarkdownParseError::cancelled));
            return 1;
        }
        try {
            const bool kept = entering ? enter_span(type, detail) : leave_span(type);
            return kept ? 0 : 1;
        } catch (const std::bad_alloc&) {
            static_cast<void>(stop(MarkdownParseError::limit_exceeded));
        } catch (const std::length_error&) {
            static_cast<void>(stop(MarkdownParseError::limit_exceeded));
        }
        return 1;
    }

    [[nodiscard]] int guard_text(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size) noexcept {
        if (cancelled()) {
            static_cast<void>(stop(MarkdownParseError::cancelled));
            return 1;
        }
        try {
            const bool kept = add_text(type, text, size);
            return kept ? 0 : 1;
        } catch (const std::bad_alloc&) {
            static_cast<void>(stop(MarkdownParseError::limit_exceeded));
        } catch (const std::length_error&) {
            static_cast<void>(stop(MarkdownParseError::limit_exceeded));
        }
        return 1;
    }
};

// The offsets at which the source's lines start, for block source ranges.
void line_starts(std::string_view source, std::vector<std::size_t>& starts) {
    starts.clear();
    starts.push_back(0U);
    for (std::size_t index = 0U; index < source.size(); ++index) {
        const char character = source[index];
        if (character == '\r' && index + 1U < source.size() && source[index + 1U] == '\n') continue;
        if (character == '\n' || character == '\r') starts.push_back(index + 1U);
    }
}

// The line holding offset: the last line start at or before it.
[[nodiscard]] std::size_t line_of(const std::vector<std::size_t>& starts, std::size_t offset) noexcept {
    const std::vector<std::size_t>::const_iterator after = std::upper_bound(starts.begin(), starts.end(), offset);
    const std::size_t line = static_cast<std::size_t>(after - starts.begin()) - 1U;
    return line;
}

// The end of a line's text, before its line ending.
[[nodiscard]] std::size_t line_end(std::string_view source, const std::vector<std::size_t>& starts,
                                   std::size_t line) noexcept {
    std::size_t end = line + 1U < starts.size() ? starts[line + 1U] : source.size();
    if (end > starts[line] && source[end - 1U] == '\n') --end;
    if (end > starts[line] && source[end - 1U] == '\r') --end;
    return end;
}

// Sets every block's source range (MarkdownBlock) and each ordered list
// item's ordinal as written on its marker.
void place_blocks(std::string_view source, MarkdownDocument& document) {
    std::vector<std::size_t> starts{};
    line_starts(source, starts);
    std::size_t previous_end = 0U;
    for (MarkdownBlock& block : document.blocks) {
        bool found = false;
        std::size_t first = 0U;
        std::size_t last = 0U;
        for (std::size_t index = 0U; index < block.span_count; ++index) {
            const MarkdownSpan& span = document.spans[block.first_span + index];
            if (span.source.empty()) continue;
            if (!found) first = span.source.start.value();
            found = true;
            last = span.source.end.value() - 1U;
        }
        if (!found) {
            block.source = Utf8Range{Utf8Offset(previous_end), Utf8Offset(previous_end)};
            continue;
        }
        const std::size_t first_line = line_of(starts, first);
        const std::size_t last_line = line_of(starts, last);
        const std::size_t end = std::max(line_end(source, starts, last_line), first);
        block.source = Utf8Range{Utf8Offset(starts[first_line]), Utf8Offset(end)};
        previous_end = end;
        if (block.kind != MarkdownBlockKind::list_item || block.marker != MarkdownListMarker::ordered) continue;
        // The marker is the digits and delimiter before the first content
        // byte, past the spaces between them.
        std::size_t position = first;
        while (position > starts[first_line] && (source[position - 1U] == ' ' || source[position - 1U] == '\t')) {
            --position;
        }
        if (position == starts[first_line]) continue;
        const char delimiter = source[position - 1U];
        if (delimiter != '.' && delimiter != ')') continue;
        const std::size_t digits_end = position - 1U;
        std::size_t digits_start = digits_end;
        while (digits_start > starts[first_line] && source[digits_start - 1U] >= '0' &&
               source[digits_start - 1U] <= '9' && digits_end - digits_start < 9U) {
            --digits_start;
        }
        if (digits_start == digits_end) continue;
        std::uint32_t ordinal = 0U;
        const char* digits_first = source.data() + digits_start;
        const char* digits_last = source.data() + digits_end;
        const std::from_chars_result read = std::from_chars(digits_first, digits_last, ordinal);
        if (read.ec == std::errc() && read.ptr == digits_last) block.ordinal = ordinal;
    }
}

} // namespace

MarkdownParseResult parse_markdown(std::string_view source, MarkdownDocument& document, const MarkdownLimits& limits,
                                   std::stop_token cancellation) {
    document.clear();
    if (cancellation.stop_requested()) return MarkdownParseResult::failed(MarkdownParseError::cancelled, Utf8Offset(0U));
    if (source.size() > limits.source_bytes || source.size() > UINT_MAX) {
        return MarkdownParseResult::failed(MarkdownParseError::limit_exceeded, Utf8Offset(0U));
    }
    const Utf8ValidationResult valid = validate_utf8(source);
    if (!valid.valid()) return MarkdownParseResult::failed(MarkdownParseError::invalid_utf8, valid.error_offset);
    MarkdownParser state{};
    state.source = source;
    state.document = &document;
    state.limits = &limits;
    state.cancellation = cancellation;
    MD_PARSER parser{};
    parser.abi_version = 0U;
    // Tables and task lists are read as text; raw HTML is text.
    parser.flags = MD_FLAG_STRIKETHROUGH | MD_FLAG_NOHTML;
    parser.enter_block = MarkdownParser::block_start;
    parser.leave_block = MarkdownParser::block_end;
    parser.enter_span = MarkdownParser::span_start;
    parser.leave_span = MarkdownParser::span_end;
    parser.text = MarkdownParser::text_callback;
    const int parsed = md_parse(source.data(), static_cast<MD_SIZE>(source.size()), &parser, &state);
    bool finished = state.failure.has_value() == false && parsed == 0;
    if (finished) {
        try {
            finished = state.finish();
            if (finished) place_blocks(source, document);
        } catch (const std::bad_alloc&) {
            static_cast<void>(state.stop(MarkdownParseError::limit_exceeded));
            finished = false;
        } catch (const std::length_error&) {
            static_cast<void>(state.stop(MarkdownParseError::limit_exceeded));
            finished = false;
        }
    }
    if (!finished) {
        document.clear();
        // MD4C reports its own allocation failure as -1 with no failure of
        // ours recorded.
        const MarkdownParseError error = state.failure.value_or(MarkdownParseError::limit_exceeded);
        return MarkdownParseResult::failed(error, Utf8Offset(0U));
    }
    return MarkdownParseResult::parsed();
}

void markdown_plain_text(const MarkdownDocument& document, std::string& text) {
    text.clear();
    for (std::size_t block = 0U; block < document.blocks.size(); ++block) {
        if (block > 0U) text.push_back('\n');
        const MarkdownBlock& entry = document.blocks[block];
        for (std::size_t index = 0U; index < entry.span_count; ++index) {
            const MarkdownSpan& span = document.spans[entry.first_span + index];
            if (span.kind == MarkdownSpanKind::line_break) {
                text.push_back('\n');
            } else {
                text.append(span.text);
            }
        }
    }
}

} // namespace gui_forms
