#include "gui_forms/text/markdown/markdown.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <new>
#include <stdexcept>
#include <system_error>
#include <utility>

// stxlib::markdown (STXlib, a conversion of MD4C 0.6.0's design; its NOTICE
// carries MD4C's): the CommonMark 0.31.2 reader. It writes a tape of events
// and returns; MarkdownParser then reads the tape in order. The tape's
// ranges are offsets into the source, and its decoded text (character
// references, destinations, titles) is the tape's own, copied out here.
#include "stxlib/markdown.hpp"
#include "stxlib/result.hpp"
#include "stxlib/threads.hpp"
#include "stxlib/view.hpp"
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
    images.clear();
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

// U+FFFD, the text a NUL byte of the source is shown as.
constexpr std::string_view replacement_character = "\xEF\xBF\xBD";


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
    // Text the reader supplies: source is empty where it stands.
    supplied,
};

// One text event's text, as the block being built collects it.
struct Piece final {
    PieceOrigin origin{PieceOrigin::literal};
    MarkdownSpanKind kind{MarkdownSpanKind::text};
    std::string text{};
    Utf8Range source{};
    MarkdownSpanStyle style{};
    std::optional<std::size_t> link{};
    std::optional<std::size_t> image{};
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

// A span whose content the attribute sweep is inside: its content ends
// before `close`, and `attributes` is the sum of it and every span around it.
struct OpenSpan final {
    std::size_t close{};
    SpanAttributes attributes{};
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

// The list a list item belongs to, while the tape is inside it.
struct ListState final {
    MarkdownListMarker marker{MarkdownListMarker::bullet};
    std::uint32_t next{1U};
};

// The list item the tape has entered whose first block has not begun yet.
struct PendingItem final {
    MarkdownListMarker marker{MarkdownListMarker::bullet};
    std::size_t level{};
    std::uint32_t counted{};
};

// The tape reader for one parse. Each step returns false to stop; failure
// then names why. The parser borrows source, document and limits for the
// call and keeps nothing after it.
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
    // The images whose alternative text is being read, innermost last.
    std::vector<std::size_t> open_images{};
    std::size_t quotes{};
    std::size_t strong{};
    std::size_t emphasis{};
    std::size_t code{};
    std::size_t strike{};
    // An attribute-only paragraph's alignment, for the next block.
    std::optional<MarkdownAlignment> pending_alignment{};
    // The end of the last source byte a piece mapped; text the reader
    // supplies stands here.
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

    // The line ending a break event's range ends with ("\n", "\r\n" or
    // "\r"): a hard break's range also holds the backslash or the spaces
    // before it, which are markup. Empty at the end of a source with no
    // final line ending.
    [[nodiscard]] Utf8Range line_ending_within(Utf8Range range) const noexcept {
        std::size_t position = range.start.value();
        const std::size_t end = range.end.value();
        while (position < end && source[position] != '\n' && source[position] != '\r') ++position;
        const Utf8Range ending{Utf8Offset(position), Utf8Offset(end)};
        return ending;
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

    // Adds one text event's text to the block being built, starting a
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
        if (!open_images.empty()) piece.image = open_images.back();
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

    [[nodiscard]] bool append_line_ending(MarkdownSpanKind kind, bool soft, Utf8Range range) {
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
        // The pairs nest (they were matched through a stack) and each one's
        // markup, its `[` and its `]{list}`, is its own bytes, so marking the
        // markup is linear in the text.
        for (const SpanPair& pair : pairs) {
            styles[pair.open].removed = true;
            for (std::size_t index = pair.close; index <= pair.end; ++index) styles[index].removed = true;
        }
        // A content byte takes every enclosing span's attributes added outer
        // first. One sweep keeps the open spans on a stack, each frame
        // holding the sum so far, and writes each byte once. Walking every
        // span's content made spans nested D deep over L bytes cost D x L
        // (A-gf-core-12-F1); the attributes each byte gets are the same.
        std::vector<OpenSpan> open_spans{};
        open_spans.reserve(pairs.size());
        std::size_t next_pair = 0U;
        for (std::size_t index = 0U; index < styles.size(); ++index) {
            while (!open_spans.empty() && open_spans.back().close == index) open_spans.pop_back();
            if (next_pair < pairs.size() && pairs[next_pair].open + 1U == index) {
                SpanAttributes sum = open_spans.empty() ? SpanAttributes{} : open_spans.back().attributes;
                add_attributes(sum, pairs[next_pair].attributes);
                const OpenSpan opened{pairs[next_pair].close, sum};
                open_spans.push_back(opened);
                next_pair = next_pair + 1U;
            }
            if (!open_spans.empty()) styles[index].attributes = open_spans.back().attributes;
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
                                   last.style == style && last.link == piece.link && last.image == piece.image &&
                                   last.source.end == range.start;
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
        span.image = piece.image;
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

    [[nodiscard]] bool enter_block(const stxlib::markdown::Event& event) {
        const stxlib::markdown::BlockType type = static_cast<stxlib::markdown::BlockType>(event.type);
        switch (type) {
        case stxlib::markdown::BlockType::document:
            return true;
        case stxlib::markdown::BlockType::quote:
            if (!finish()) return false;
            ++quotes;
            return true;
        case stxlib::markdown::BlockType::bullet_list: {
            if (!finish()) return false;
            const ListState list{MarkdownListMarker::bullet, 1U};
            lists.push_back(list);
            return true;
        }
        case stxlib::markdown::BlockType::ordered_list: {
            if (!finish()) return false;
            // Event::detail is the first item's number.
            const ListState list{MarkdownListMarker::ordered, event.detail};
            lists.push_back(list);
            return true;
        }
        case stxlib::markdown::BlockType::list_item: {
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
        case stxlib::markdown::BlockType::rule:
            return begin(MarkdownBlockKind::rule, 0U);
        case stxlib::markdown::BlockType::heading:
            // Event::data is the level, 1 to 6.
            return begin(MarkdownBlockKind::heading, event.data);
        case stxlib::markdown::BlockType::code_block:
            return begin(MarkdownBlockKind::code_block, 0U);
        case stxlib::markdown::BlockType::paragraph:
            return begin(MarkdownBlockKind::paragraph, 0U);
        default:
            // A block type this reader does not know is read through its
            // text alone.
            return true;
        }
    }

    [[nodiscard]] bool leave_block(const stxlib::markdown::Event& event) {
        const stxlib::markdown::BlockType type = static_cast<stxlib::markdown::BlockType>(event.type);
        switch (type) {
        case stxlib::markdown::BlockType::quote:
            if (!finish()) return false;
            if (quotes > 0U) --quotes;
            return true;
        case stxlib::markdown::BlockType::bullet_list:
        case stxlib::markdown::BlockType::ordered_list:
            if (!finish()) return false;
            if (!lists.empty()) lists.pop_back();
            return true;
        case stxlib::markdown::BlockType::list_item:
            pending_item.reset();
            return finish();
        case stxlib::markdown::BlockType::rule:
        case stxlib::markdown::BlockType::heading:
        case stxlib::markdown::BlockType::code_block:
        case stxlib::markdown::BlockType::paragraph:
            return finish();
        case stxlib::markdown::BlockType::document:
        default:
            return true;
        }
    }

    // Counts a link's or image's decoded strings against the text limit.
    [[nodiscard]] bool count_attribute_bytes(std::size_t bytes) noexcept {
        if (bytes > (*limits).text_bytes - text_bytes) return stop(MarkdownParseError::limit_exceeded);
        text_bytes += bytes;
        return true;
    }

    [[nodiscard]] bool enter_span(const stxlib::markdown::Event& event, const stxlib::markdown::Document& tape) {
        const stxlib::markdown::SpanType type = static_cast<stxlib::markdown::SpanType>(event.type);
        switch (type) {
        case stxlib::markdown::SpanType::strong:
            ++strong;
            return true;
        case stxlib::markdown::SpanType::emphasis:
            ++emphasis;
            return true;
        case stxlib::markdown::SpanType::code:
            ++code;
            return true;
        case stxlib::markdown::SpanType::strikethrough:
            ++strike;
            return true;
        case stxlib::markdown::SpanType::link: {
            // Event::detail is the destination's attribute; the title's is
            // the next. Both are decoded in the tape's text.
            const stxlib::View<char> target = tape.attribute(event.detail);
            const stxlib::View<char> title = tape.attribute(event.detail + 1U);
            MarkdownLink entry{};
            // A mail autolink's destination is the address as written; the
            // link target keeps the "mailto:" scheme a reader opens it by
            // (MD4C wrote it into the destination).
            const bool mail = (event.flags & stxlib::markdown::event_flags::mail_address) != 0;
            if (mail) entry.target = "mailto:";
            entry.target.append(target.data(), target.count());
            entry.title.assign(title.data(), title.count());
            if (!count_attribute_bytes(entry.target.size() + entry.title.size())) return false;
            open_links.push_back((*document).links.size());
            (*document).links.push_back(std::move(entry));
            return true;
        }
        case stxlib::markdown::SpanType::image: {
            // An image is its alternative text, the text events up to its
            // leave; its source and title are recorded, never opened.
            const stxlib::View<char> image_source = tape.attribute(event.detail);
            const stxlib::View<char> title = tape.attribute(event.detail + 1U);
            MarkdownImage entry{};
            entry.source.assign(image_source.data(), image_source.count());
            entry.title.assign(title.data(), title.count());
            if (!count_attribute_bytes(entry.source.size() + entry.title.size())) return false;
            open_images.push_back((*document).images.size());
            (*document).images.push_back(std::move(entry));
            return true;
        }
        default:
            // A span type this reader does not know is read through its
            // text alone.
            return true;
        }
    }

    [[nodiscard]] bool leave_span(const stxlib::markdown::Event& event) {
        const stxlib::markdown::SpanType type = static_cast<stxlib::markdown::SpanType>(event.type);
        switch (type) {
        case stxlib::markdown::SpanType::strong:
            if (strong > 0U) --strong;
            return true;
        case stxlib::markdown::SpanType::emphasis:
            if (emphasis > 0U) --emphasis;
            return true;
        case stxlib::markdown::SpanType::code:
            if (code > 0U) --code;
            return true;
        case stxlib::markdown::SpanType::strikethrough:
            if (strike > 0U) --strike;
            return true;
        case stxlib::markdown::SpanType::link:
            if (!open_links.empty()) open_links.pop_back();
            return true;
        case stxlib::markdown::SpanType::image:
            if (!open_images.empty()) open_images.pop_back();
            return true;
        default:
            return true;
        }
    }

    [[nodiscard]] bool add_text(const stxlib::markdown::Event& event, const stxlib::markdown::Document& tape) {
        const stxlib::markdown::TextType type = static_cast<stxlib::markdown::TextType>(event.type);
        const Utf8Range range{Utf8Offset(event.begin), Utf8Offset(event.end)};
        const Utf8Range here{Utf8Offset(cursor), Utf8Offset(cursor)};
        switch (type) {
        case stxlib::markdown::TextType::literal:
        case stxlib::markdown::TextType::code: {
            if (range.empty()) return true;
            const bool markable = type == stxlib::markdown::TextType::literal && code == 0U;
            return append_literal(range, markable);
        }
        case stxlib::markdown::TextType::reference: {
            const stxlib::View<char> decoded = tape.reference_text(event);
            std::string text(decoded.data(), decoded.count());
            return append(PieceOrigin::decoded, MarkdownSpanKind::text, std::move(text), range);
        }
        case stxlib::markdown::TextType::null_character: {
            std::string text(replacement_character);
            return append(PieceOrigin::decoded, MarkdownSpanKind::text, std::move(text), range);
        }
        case stxlib::markdown::TextType::soft_break:
            return append_line_ending(MarkdownSpanKind::text, true, line_ending_within(range));
        case stxlib::markdown::TextType::hard_break:
            return append_line_ending(MarkdownSpanKind::line_break, false, line_ending_within(range));
        case stxlib::markdown::TextType::line_end:
            // A code block's line feed stands for its line ending.
            return append_line_ending(MarkdownSpanKind::text, false, line_ending_within(range));
        case stxlib::markdown::TextType::code_space: {
            // Supplied text, an empty range where it stands (MarkdownSpan),
            // although the tape names the line ending it replaces.
            std::string text(" ");
            return append(PieceOrigin::supplied, MarkdownSpanKind::text, std::move(text), here);
        }
        case stxlib::markdown::TextType::code_indent: {
            // Event::detail spaces that are not source bytes.
            std::string text(static_cast<std::size_t>(event.detail), ' ');
            return append(PieceOrigin::supplied, MarkdownSpanKind::text, std::move(text), here);
        }
        default:
            // A text type this reader does not know adds nothing.
            return true;
        }
    }

    // One event of the tape. Cancellation is read at every block the tape
    // enters, as the reader read it once per block.
    [[nodiscard]] bool read_event(const stxlib::markdown::Event& event, const stxlib::markdown::Document& tape) {
        switch (event.kind) {
        case stxlib::markdown::EventKind::enter_block:
            if (cancelled()) return stop(MarkdownParseError::cancelled);
            return enter_block(event);
        case stxlib::markdown::EventKind::leave_block:
            return leave_block(event);
        case stxlib::markdown::EventKind::enter_span:
            return enter_span(event, tape);
        case stxlib::markdown::EventKind::leave_span:
            return leave_span(event);
        case stxlib::markdown::EventKind::text:
            return add_text(event, tape);
        default:
            return true;
        }
    }

    // Reads the whole tape in order, then finishes the last block.
    [[nodiscard]] bool read_tape(const stxlib::markdown::Document& tape) {
        const stxlib::usize count = tape.events.count();
        for (stxlib::usize index = 0U; index < count; ++index) {
            const stxlib::markdown::Event& event = tape.events[index];
            if (!read_event(event, tape)) return false;
        }
        const bool finished = finish();
        return finished;
    }
};

// Requests the tape reader's stop when the caller's token is signalled; a
// std::stop_callback runs it on the thread that requests the stop.
struct RequestTapeStop final {
    stxlib::StopSignal* signal{};

    void operator()() const noexcept { (*signal).request(); }
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
    if (source.size() > limits.source_bytes || source.size() > stxlib::markdown::largest_source_bytes) {
        return MarkdownParseResult::failed(MarkdownParseError::limit_exceeded, Utf8Offset(0U));
    }
    const Utf8ValidationResult valid = validate_utf8(source);
    if (!valid.valid()) return MarkdownParseResult::failed(MarkdownParseError::invalid_utf8, valid.error_offset);
    // The tape. Tables and task lists are read as text; raw HTML is text.
    // Its own tables keep the reader's default limits; the source limit is
    // the caller's, checked above. The stop signal is read once per block;
    // the bridge, declared after it, is removed before it ends.
    stxlib::markdown::Options options{};
    options.strikethrough = true;
    stxlib::markdown::Limits tape_limits{};
    tape_limits.source_bytes = source.size();
    stxlib::StopSignal tape_stop{};
    const std::stop_callback<RequestTapeStop> stop_bridge(cancellation, RequestTapeStop{&tape_stop});
    stxlib::markdown::Document tape{};
    const stxlib::View<char> tape_source(source.data(), source.size());
    const stxlib::Status status = stxlib::markdown::parse(tape_source, options, tape_limits, tape, &tape_stop);
    if (status.failed()) {
        // interrupted is the token's stop; a reader limit or its allocation
        // failure is a limit, as MarkdownParseError says.
        const bool interrupted = (*status.error()).kind() == stxlib::ErrorKind::interrupted;
        const MarkdownParseError error = interrupted ? MarkdownParseError::cancelled : MarkdownParseError::limit_exceeded;
        return MarkdownParseResult::failed(error, Utf8Offset(0U));
    }
    MarkdownParser state{};
    state.source = source;
    state.document = &document;
    state.limits = &limits;
    state.cancellation = cancellation;
    bool finished = false;
    try {
        finished = state.read_tape(tape);
        if (finished) place_blocks(source, document);
    } catch (const std::bad_alloc&) {
        static_cast<void>(state.stop(MarkdownParseError::limit_exceeded));
        finished = false;
    } catch (const std::length_error&) {
        static_cast<void>(state.stop(MarkdownParseError::limit_exceeded));
        finished = false;
    }
    if (!finished) {
        document.clear();
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
