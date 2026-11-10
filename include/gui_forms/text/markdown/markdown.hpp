#pragma once

#include "gui_forms/text/types/text_types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace gui_forms {

// The markdown framework (owner decision 2026-10-28): GUI.Forms' rich text
// is markdown source in the stx_rtf2md dialect, CommonMark 0.31.2 with
// ~~strikethrough~~, attribute spans ([text]{u color=#rrggbb size=+1}) and
// block attribute lines ({align=center}). parse_markdown reads it for
// presentation and editing: every span records the source bytes it comes
// from, so an editor can style the source in place and tell content from
// markup. Tables and task-list markers are read as text, raw HTML is text,
// and an image is its alternative text. Parsing never opens a link or an
// image.

// An sRGB colour without alpha, as an attribute span gives it.
struct MarkdownRgb final {
    std::uint8_t red{};
    std::uint8_t green{};
    std::uint8_t blue{};
};

[[nodiscard]] bool operator==(const MarkdownRgb& left, const MarkdownRgb& right) noexcept;

enum class MarkdownBlockKind : std::uint8_t { paragraph, heading, list_item, code_block, rule };
enum class MarkdownListMarker : std::uint8_t { bullet, ordered };
enum class MarkdownAlignment : std::uint8_t { left, center, right };
// A span's position against the baseline; superscript and subscript
// exclude each other.
enum class MarkdownBaseline : std::uint8_t { normal, superscript, subscript };
enum class MarkdownSpanKind : std::uint8_t { text, line_break };

inline constexpr std::int8_t minimum_markdown_size_step = -2;
inline constexpr std::int8_t maximum_markdown_size_step = 4;

// How a span is set. The default value is body text. Bold, italic and
// strikethrough come from emphasis, ~~ and the span items b, i and s; code
// from a code span or a code block; the rest from attribute spans.
// size_step counts steps from the body size, minimum_markdown_size_step to
// maximum_markdown_size_step.
struct MarkdownSpanStyle final {
    bool bold{};
    bool italic{};
    bool underline{};
    bool strikethrough{};
    bool code{};
    MarkdownBaseline baseline{MarkdownBaseline::normal};
    std::optional<MarkdownRgb> colour{};
    std::optional<MarkdownRgb> background{};
    std::int8_t size_step{};
};

[[nodiscard]] bool operator==(const MarkdownSpanStyle& left, const MarkdownSpanStyle& right) noexcept;

// A piece of a block's text in one style.
//
// text is the text as shown: backslash escapes and character references
// decoded, a soft line break as one space, a code block's lines joined by
// line feeds. A line break span has empty text.
//
// source is the range of the source the span comes from. Spans of a block
// follow the source in order and never overlap. A span whose text equals
// its source bytes is literal text; a character reference's source is the
// reference, a soft or hard line break's source is the line ending, and
// text the parser supplies (a code block's indentation inside a container,
// a code span's line ending read as a space) has an empty range where it
// stands. Source bytes no span covers are markup: emphasis delimiters,
// heading and list markers, quote markers, escapes' backslashes, link
// destinations, attribute lists and fences.
struct MarkdownSpan final {
    MarkdownSpanKind kind{MarkdownSpanKind::text};
    std::string text{};
    Utf8Range source{};
    MarkdownSpanStyle style{};
    // The link the span is the text of: an index into
    // MarkdownDocument::links.
    std::optional<std::size_t> link{};
    // The image the span is the alternative text of: an index into
    // MarkdownDocument::images.
    std::optional<std::size_t> image{};
};

// A link's destination, decoded. Every link of the source has its own
// entry, in source order.
struct MarkdownLink final {
    std::string target{};
    // The link title (`[text](target "title")`), decoded; empty without one.
    std::string title{};
};

// An image's source and title, decoded. Every image of the source has its
// own entry, in source order; its spans are its alternative text. Parsing
// records the source only and never opens it.
struct MarkdownImage final {
    std::string source{};
    std::string title{};
};

// A block and the spans it holds.
//
// level is a heading's level (1 to 6) or a list item's nesting (0 for an
// item of a top-level list), and 0 for other kinds. quote_depth counts the
// block quotes around the block. marker and ordinal are a list item's: an
// ordered item's ordinal is the number written on its marker. alignment
// comes from a block attribute line before a paragraph, heading or list
// item. A list item's later paragraphs are paragraphs.
//
// source runs from the start of the line holding the block's first source
// byte to the end of the line holding its last, so it includes the
// block's leading markup (quote, list and heading markers) but not a fence
// line or a setext underline. A block with no text (a rule, an empty
// heading or code block) has an empty source at the end of the block
// before it.
struct MarkdownBlock final {
    MarkdownBlockKind kind{MarkdownBlockKind::paragraph};
    std::size_t level{};
    std::size_t quote_depth{};
    MarkdownListMarker marker{MarkdownListMarker::bullet};
    std::uint32_t ordinal{};
    MarkdownAlignment alignment{MarkdownAlignment::left};
    Utf8Range source{};
    // The block's spans: MarkdownDocument::spans from first_span, span_count
    // of them.
    std::size_t first_span{};
    std::size_t span_count{};
};

// A parsed source: blocks in source order over one span table and one link
// table. A document holds no borrow of the source it was parsed from.
class MarkdownDocument final {
public:
    std::vector<MarkdownBlock> blocks{};
    std::vector<MarkdownSpan> spans{};
    std::vector<MarkdownLink> links{};
    std::vector<MarkdownImage> images{};

    // Ends every element's lifetime; capacity remains.
    void clear() noexcept;
};

// Bounds on one parse. source_bytes bounds the input; blocks and spans
// bound the tables; text_bytes bounds the span texts, link targets and
// titles, and image sources and titles together.
struct MarkdownLimits final {
    std::size_t source_bytes = 16U * 1024U * 1024U;
    std::size_t blocks = 100000U;
    std::size_t spans = 250000U;
    std::size_t text_bytes = 32U * 1024U * 1024U;
};

enum class MarkdownParseError : std::uint8_t {
    // The source is not valid UTF-8; the result's offset is the first
    // invalid byte.
    invalid_utf8,
    // The source, the tables or the text pass a MarkdownLimits bound, or
    // storage for them could not be allocated.
    limit_exceeded,
    // The stop token was signalled.
    cancelled,
};

// The result of parse_markdown: parsed, or failed with an error and, for
// invalid_utf8, the offset of the first invalid byte.
class MarkdownParseResult final {
public:
    [[nodiscard]] static MarkdownParseResult parsed() noexcept;
    [[nodiscard]] static MarkdownParseResult failed(MarkdownParseError error, Utf8Offset offset) noexcept;

    [[nodiscard]] bool succeeded() const noexcept;
    // The error of a parse that failed; none when it succeeded.
    [[nodiscard]] std::optional<MarkdownParseError> error() const noexcept;
    // Where a failed parse stopped; 0 when it succeeded.
    [[nodiscard]] Utf8Offset offset() const noexcept;

private:
    struct Parsed final {};
    struct Failed final {
        MarkdownParseError error{MarkdownParseError::limit_exceeded};
        Utf8Offset offset{};
    };
    explicit MarkdownParseResult(std::variant<Parsed, Failed> state) noexcept;

    std::variant<Parsed, Failed> state_;
};

// Parses source into document, which is replaced; on failure document is
// empty. source is only read and must not change during the call; document
// keeps no borrow of it. Cancellation is observed between the parser's
// callbacks.
[[nodiscard]] MarkdownParseResult parse_markdown(std::string_view source, MarkdownDocument& document,
                                                 const MarkdownLimits& limits = MarkdownLimits{},
                                                 std::stop_token cancellation = std::stop_token{});

// Writes the document's plain text into text, which is replaced: the
// blocks' span texts joined by line feeds, a line break span as a line
// feed, a rule as an empty line. This is the text with the markup removed.
void markdown_plain_text(const MarkdownDocument& document, std::string& text);

} // namespace gui_forms
