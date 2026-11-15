#include "gui_forms/text.hpp"

#include <stx_rtf2md/document.hpp>
#include <stx_rtf2md/limits.hpp>
#include <stx_rtf2md/markdown.hpp>
#include <stx_rtf2md/rtf.hpp>

#include <array>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace gui_forms;
namespace rtf2md = stx_rtf2md;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::string read_file(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    require(stream.good(), "cannot open " + path);
    std::ostringstream contents;
    contents << stream.rdbuf();
    const std::string text = contents.str();
    return text;
}

std::span<const std::byte> bytes_of(std::string_view text) {
    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(text.data()), text.size());
    return bytes;
}

MarkdownDocument parse(std::string_view source) {
    MarkdownDocument document;
    const MarkdownParseResult result = parse_markdown(source, document);
    require(result.succeeded(), "parse failed: " + std::string(source));
    return document;
}

const MarkdownSpan& span_of(const MarkdownDocument& document, std::size_t block, std::size_t index) {
    const MarkdownBlock& entry = document.blocks.at(block);
    require(index < entry.span_count, "span index past the block");
    return document.spans.at(entry.first_span + index);
}

std::string_view source_text(std::string_view source, Utf8Range range) {
    const std::string_view text = source.substr(range.start.value(), range.end.value() - range.start.value());
    return text;
}

// The text of a block's spans joined, line breaks as line feeds.
std::string block_text(const MarkdownDocument& document, std::size_t block) {
    std::string text;
    const MarkdownBlock& entry = document.blocks.at(block);
    for (std::size_t index = 0U; index < entry.span_count; ++index) {
        const MarkdownSpan& span = document.spans.at(entry.first_span + index);
        if (span.kind == MarkdownSpanKind::line_break) {
            text.push_back('\n');
        } else {
            text.append(span.text);
        }
    }
    return text;
}

// Every span's source lies in the source, after the one before it; a span
// whose origin is literal text has text equal to its source; a block's
// source holds its spans and the blocks follow each other.
void check_source_ranges(std::string_view source, const MarkdownDocument& document, const std::string& name) {
    std::size_t previous = 0U;
    std::size_t covered = 0U;
    for (std::size_t block = 0U; block < document.blocks.size(); ++block) {
        const MarkdownBlock& entry = document.blocks[block];
        require(entry.first_span == covered, name + ": block spans are not contiguous");
        covered += entry.span_count;
        require(entry.source.start.value() <= entry.source.end.value(), name + ": block source reversed");
        require(entry.source.end.value() <= source.size(), name + ": block source past the end");
        require(entry.source.start.value() >= previous || entry.span_count == 0U, name + ": blocks overlap");
        for (std::size_t index = 0U; index < entry.span_count; ++index) {
            const MarkdownSpan& span = document.spans[entry.first_span + index];
            require(span.source.start.value() <= span.source.end.value(), name + ": span source reversed");
            require(span.source.end.value() <= source.size(), name + ": span source past the end");
            require(span.source.start.value() >= previous, name + ": spans out of source order");
            if (!span.source.empty()) {
                require(span.source.start.value() >= entry.source.start.value() &&
                            span.source.end.value() <= entry.source.end.value() + 2U,
                        name + ": span outside its block's lines");
            }
            previous = span.source.end.value();
        }
    }
    require(covered == document.spans.size(), name + ": spans outside every block");
    for (const MarkdownSpan& span : document.spans) {
        if (!span.link.has_value()) continue;
        require(*span.link < document.links.size(), name + ": link index past the table");
    }
}

void paragraphs_and_emphasis() {
    const std::string_view source = "Plain **bold** and *it* ~~gone~~ `code`.\n";
    const MarkdownDocument document = parse(source);
    require(document.blocks.size() == 1U, "one paragraph");
    require(document.blocks[0].kind == MarkdownBlockKind::paragraph, "a paragraph");
    require(block_text(document, 0U) == "Plain bold and it gone code.", "markers are not text");
    const MarkdownSpan& bold = span_of(document, 0U, 1U);
    require(bold.text == "bold" && bold.style.bold && !bold.style.italic, "bold span");
    require(source_text(source, bold.source) == "bold", "bold span maps to its source");
    require(span_of(document, 0U, 3U).style.italic, "italic span");
    require(span_of(document, 0U, 5U).style.strikethrough, "strikethrough span");
    require(span_of(document, 0U, 7U).style.code, "code span");
    require(document.blocks[0].source.start.value() == 0U &&
                document.blocks[0].source.end.value() == source.size() - 1U,
            "the block's source is its line");
    check_source_ranges(source, document, "emphasis");
}

void blocks_and_lists() {
    const std::string_view source = "# Title #\n\n"
                                    "1. one\n"
                                    "7. two\n"
                                    "   - nested\n\n"
                                    "> quoted\n"
                                    "> > deeper\n\n"
                                    "```c\nint x;\n\nreturn;\n```\n\n"
                                    "***\n\n"
                                    "Setext\n======\n";
    const MarkdownDocument document = parse(source);
    require(document.blocks.size() == 9U, "nine blocks");
    const MarkdownBlock& title = document.blocks[0];
    require(title.kind == MarkdownBlockKind::heading && title.level == 1U, "an ATX heading");
    require(block_text(document, 0U) == "Title", "the closing hashes are markup");
    const MarkdownBlock& one = document.blocks[1];
    require(one.kind == MarkdownBlockKind::list_item && one.marker == MarkdownListMarker::ordered, "an ordered item");
    require(one.ordinal == 1U && one.level == 0U, "the first ordinal");
    require(document.blocks[2].ordinal == 7U, "the ordinal as written");
    const MarkdownBlock& nested = document.blocks[3];
    require(nested.kind == MarkdownBlockKind::list_item && nested.marker == MarkdownListMarker::bullet, "a bullet");
    require(nested.level == 1U, "nested one level");
    require(source_text(source, nested.source) == "   - nested", "an item's source holds its marker");
    require(document.blocks[4].quote_depth == 1U && document.blocks[5].quote_depth == 2U, "quote depths");
    const MarkdownBlock& code = document.blocks[6];
    require(code.kind == MarkdownBlockKind::code_block, "a code block");
    require(block_text(document, 6U) == "int x;\n\nreturn;", "a code block's lines");
    require(span_of(document, 6U, 0U).style.code, "code block text is code");
    require(document.blocks[7].kind == MarkdownBlockKind::rule && document.blocks[7].span_count == 0U, "a rule");
    require(document.blocks[8].kind == MarkdownBlockKind::heading && document.blocks[8].level == 1U, "setext");
    check_source_ranges(source, document, "blocks");
}

void dialect_attributes() {
    const std::string_view source = "{align=center}\n"
                                    "Water is H[2]{sub}O and [red [big]{size=+2}]{u color=#FF0000}.\n\n"
                                    "{align=right}\n\n"
                                    "# Heading\n\n"
                                    "\\[not]{u} and [bad]{v}\n";
    const MarkdownDocument document = parse(source);
    require(document.blocks.size() == 3U, "the lone attribute line is no block");
    require(document.blocks[0].alignment == MarkdownAlignment::center, "a leading attribute line aligns");
    require(block_text(document, 0U) == "Water is H2O and red big.", "attribute markup is removed");
    const MarkdownSpan& sub = span_of(document, 0U, 1U);
    require(sub.text == "2" && sub.style.baseline == MarkdownBaseline::subscript, "subscript");
    require(source_text(source, sub.source) == "2", "the subscript maps to its digit");
    const MarkdownSpan& red = span_of(document, 0U, 3U);
    const MarkdownRgb pure_red{255U, 0U, 0U};
    require(red.text == "red " && red.style.underline && red.style.colour == pure_red, "outer span");
    const MarkdownSpan& big = span_of(document, 0U, 4U);
    require(big.text == "big" && big.style.size_step == 2 && big.style.underline, "inner span adds");
    require(document.blocks[1].alignment == MarkdownAlignment::right, "a lone attribute line aligns the next");
    require(block_text(document, 2U) == "[not]{u} and [bad]{v}", "escaped or invalid spans stay text");
    check_source_ranges(source, document, "dialect");
}

void links_breaks_and_text() {
    const std::string_view source = "See [the *site*](https://example.com/a&amp;b \"title\") or <mailto:a@b.c>.\n"
                                    "Next line\\\n"
                                    "after &copy; &#65;\r\n"
                                    "| a | b |\n"
                                    "- [ ] task\n\n"
                                    "![alt text](image.png)\n";
    const MarkdownDocument document = parse(source);
    require(document.links.size() == 2U, "two links");
    require(document.links[0].target == "https://example.com/a&b", "a decoded destination");
    require(document.links[1].target == "mailto:a@b.c", "an autolink");
    const MarkdownSpan& site = span_of(document, 0U, 1U);
    require(site.text == "the " && site.link == std::optional<std::size_t>(0U), "link text");
    require(span_of(document, 0U, 2U).style.italic && span_of(document, 0U, 2U).link.has_value(), "styled link");
    const std::string text = block_text(document, 0U);
    require(text.find("Next line\nafter \xC2\xA9 A") != std::string::npos, "a hard break and references");
    bool found_break = false;
    for (std::size_t index = 0U; index < document.blocks[0].span_count; ++index) {
        const MarkdownSpan& span = span_of(document, 0U, index);
        if (span.kind != MarkdownSpanKind::line_break) continue;
        found_break = true;
        require(source_text(source, span.source) == "\n", "a hard break maps to its line ending");
    }
    require(found_break, "a hard break span");
    require(text.find("A | a | b |") != std::string::npos, "a CRLF soft break and a table row are text");
    require(document.blocks.size() == 3U, "the task is a list item, the image its own paragraph");
    require(block_text(document, 1U) == "[ ] task", "a task marker is text");
    require(block_text(document, 2U) == "alt text", "an image is its alternative text");
    std::string plain;
    markdown_plain_text(document, plain);
    require(plain.ends_with("\n[ ] task\nalt text"), "plain text joins blocks with line feeds");
    check_source_ranges(source, document, "links");
}

// A stop source that is already stopped.
struct StoppedSource final {
    std::stop_source source{};
    StoppedSource() { static_cast<void>(source.request_stop()); }
};

void failures() {
    MarkdownDocument document;
    const std::string invalid = std::string("ok ") + static_cast<char>(0xFF);
    MarkdownParseResult result = parse_markdown(invalid, document);
    require(!result.succeeded() && result.error() == MarkdownParseError::invalid_utf8, "invalid UTF-8");
    require(result.offset().value() == 3U, "the invalid byte's offset");
    MarkdownLimits limits;
    limits.blocks = 2U;
    result = parse_markdown("a\n\nb\n\nc\n", document, limits);
    require(!result.succeeded() && result.error() == MarkdownParseError::limit_exceeded, "the block limit");
    require(document.blocks.empty() && document.spans.empty(), "a failed parse leaves the document empty");
    limits = MarkdownLimits{};
    limits.source_bytes = 3U;
    result = parse_markdown("four", document, limits);
    require(result.error() == MarkdownParseError::limit_exceeded, "the source limit");
    limits = MarkdownLimits{};
    limits.text_bytes = 4U;
    result = parse_markdown("12345", document, limits);
    require(result.error() == MarkdownParseError::limit_exceeded, "the text limit");
    const StoppedSource stopped;
    result = parse_markdown("text", document, MarkdownLimits{}, stopped.source.get_token());
    require(result.error() == MarkdownParseError::cancelled, "cancellation");
    result = parse_markdown("", document);
    require(result.succeeded() && document.blocks.empty(), "an empty source");
}

// One run of a block, as both readers can say it: kind, text, style and
// link target.
struct ComparableRun final {
    bool line_break{};
    std::string text{};
    bool bold{};
    bool italic{};
    bool underline{};
    bool strikethrough{};
    bool code{};
    bool superscript{};
    bool subscript{};
    std::optional<MarkdownRgb> colour{};
    std::optional<MarkdownRgb> background{};
    int size_step{};
    std::optional<std::string> link{};
};

bool same_style(const ComparableRun& left, const ComparableRun& right) {
    const bool equal = left.line_break == right.line_break && left.bold == right.bold &&
                       left.italic == right.italic && left.underline == right.underline &&
                       left.strikethrough == right.strikethrough && left.code == right.code &&
                       left.superscript == right.superscript && left.subscript == right.subscript &&
                       left.colour == right.colour && left.background == right.background &&
                       left.size_step == right.size_step && left.link == right.link;
    return equal;
}

struct ComparableBlock final {
    int kind{};
    std::size_t level{};
    std::size_t quote_depth{};
    bool ordered{};
    std::uint32_t ordinal{};
    int alignment{};
    std::vector<ComparableRun> runs{};
};

std::string describe(const ComparableBlock& block) {
    std::string text = "kind " + std::to_string(block.kind) + " level " + std::to_string(block.level) + " quote " +
                       std::to_string(block.quote_depth) + " ordered " + std::to_string(block.ordered) +
                       " ordinal " + std::to_string(block.ordinal) + " align " + std::to_string(block.alignment) +
                       ":";
    for (const ComparableRun& run : block.runs) {
        text += run.line_break ? std::string(" <br>") : " [" + run.text + "]";
        if (run.bold) text += "b";
        if (run.italic) text += "i";
        if (run.underline) text += "u";
        if (run.strikethrough) text += "s";
        if (run.code) text += "c";
        if (run.superscript) text += "^";
        if (run.subscript) text += "_";
        if (run.colour.has_value()) text += "#";
        if (run.background.has_value()) text += "bg";
        if (run.size_step != 0) text += "z" + std::to_string(run.size_step);
        if (run.link.has_value()) text += "->" + *run.link;
    }
    return text;
}

std::optional<MarkdownRgb> rgb_of(const std::optional<rtf2md::Rgb>& colour) {
    if (!colour.has_value()) return std::nullopt;
    const MarkdownRgb rgb{(*colour).red, (*colour).green, (*colour).blue};
    return rgb;
}

int kind_of(rtf2md::BlockKind kind) {
    switch (kind) {
    case rtf2md::BlockKind::paragraph: return 0;
    case rtf2md::BlockKind::heading: return 1;
    case rtf2md::BlockKind::list_item: return 2;
    case rtf2md::BlockKind::code_block: return 3;
    }
    return -1;
}

int kind_of(MarkdownBlockKind kind) {
    switch (kind) {
    case MarkdownBlockKind::paragraph: return 0;
    case MarkdownBlockKind::heading: return 1;
    case MarkdownBlockKind::list_item: return 2;
    case MarkdownBlockKind::code_block: return 3;
    case MarkdownBlockKind::rule: return 4;
    }
    return -1;
}

void from_reader(const rtf2md::Document& document, std::vector<ComparableBlock>& blocks) {
    blocks.clear();
    for (const rtf2md::Block& block : document.blocks) {
        ComparableBlock entry;
        entry.kind = kind_of(block.kind);
        entry.level = block.kind == rtf2md::BlockKind::paragraph || block.kind == rtf2md::BlockKind::code_block
                          ? 0U
                          : block.level;
        entry.quote_depth = block.quote_depth;
        entry.ordered = block.kind == rtf2md::BlockKind::list_item && block.marker == rtf2md::ListMarker::ordered;
        entry.ordinal = entry.ordered ? block.ordinal : 0U;
        entry.alignment = static_cast<int>(block.alignment);
        for (std::uint32_t index = 0U; index < block.run_count; ++index) {
            const rtf2md::Run& run = document.runs[block.first_run.value + index];
            ComparableRun comparable;
            comparable.line_break = run.kind == rtf2md::RunKind::line_break;
            comparable.text = document.text.substr(run.start.value, run.length.value);
            comparable.bold = run.style.bold;
            comparable.italic = run.style.italic;
            comparable.underline = run.style.underline;
            comparable.strikethrough = run.style.strikethrough;
            comparable.code = run.style.code;
            comparable.superscript = run.style.superscript;
            comparable.subscript = run.style.subscript;
            comparable.colour = rgb_of(run.style.colour);
            comparable.background = rgb_of(run.style.background);
            comparable.size_step = run.style.size_step;
            if (run.style.link.has_value()) {
                const rtf2md::Link& link = document.links[(*run.style.link).value];
                comparable.link = document.text.substr(link.target_start.value, link.target_length.value);
            }
            entry.runs.push_back(comparable);
        }
        blocks.push_back(entry);
    }
}

void from_parser(const MarkdownDocument& document, std::vector<ComparableBlock>& blocks) {
    blocks.clear();
    for (const MarkdownBlock& block : document.blocks) {
        ComparableBlock entry;
        entry.kind = kind_of(block.kind);
        entry.level = block.level;
        entry.quote_depth = block.quote_depth;
        entry.ordered = block.kind == MarkdownBlockKind::list_item && block.marker == MarkdownListMarker::ordered;
        entry.ordinal = entry.ordered ? block.ordinal : 0U;
        entry.alignment = static_cast<int>(block.alignment);
        std::optional<std::size_t> last_link;
        for (std::size_t index = 0U; index < block.span_count; ++index) {
            const MarkdownSpan& span = document.spans[block.first_span + index];
            ComparableRun comparable;
            comparable.line_break = span.kind == MarkdownSpanKind::line_break;
            comparable.text = span.text;
            comparable.bold = span.style.bold;
            comparable.italic = span.style.italic;
            comparable.underline = span.style.underline;
            comparable.strikethrough = span.style.strikethrough;
            // The normative model's code block is one unstyled run.
            comparable.code = span.style.code && block.kind != MarkdownBlockKind::code_block;
            comparable.superscript = span.style.baseline == MarkdownBaseline::superscript;
            comparable.subscript = span.style.baseline == MarkdownBaseline::subscript;
            comparable.colour = span.style.colour;
            comparable.background = span.style.background;
            comparable.size_step = span.style.size_step;
            if (span.link.has_value()) comparable.link = document.links[*span.link].target;
            const bool merges = !entry.runs.empty() && !comparable.line_break &&
                                same_style(entry.runs.back(), comparable) && last_link == span.link;
            if (merges) {
                entry.runs.back().text += comparable.text;
            } else {
                entry.runs.push_back(comparable);
            }
            last_link = span.link;
        }
        blocks.push_back(entry);
    }
}

// The cross-check (owner decision 2026-10-28): over every canonical dialect
// document of stx_rtf2md's corpus, the presentation parser's blocks and
// spans equal the Document stx_rtf2md's normative reader reads.
void cross_check(const std::string& directory, const std::string& name) {
    const std::string source = read_file(directory + "/" + name + ".md");
    const rtf2md::Limits limits{};
    rtf2md::MarkdownReader reader(limits);
    rtf2md::Document normative;
    rtf2md::LossReport loss;
    const rtf2md::Result read = reader.read(std::span<const char>(source.data(), source.size()), normative, loss);
    require(read.status == rtf2md::Status::ok, name + ".md does not read");
    const MarkdownDocument document = parse(source);
    check_source_ranges(source, document, name);
    std::vector<ComparableBlock> expected;
    std::vector<ComparableBlock> actual;
    from_reader(normative, expected);
    from_parser(document, actual);
    const std::size_t count = std::max(expected.size(), actual.size());
    for (std::size_t index = 0U; index < count; ++index) {
        const std::string want = index < expected.size() ? describe(expected[index]) : std::string("(none)");
        const std::string have = index < actual.size() ? describe(actual[index]) : std::string("(none)");
        require(want == have, name + ".md block " + std::to_string(index) + " differs:\n  reader: " + want +
                                  "\n  parser: " + have);
    }
    for (std::size_t index = 0U; index < count; ++index) {
        const std::vector<ComparableRun>& want = expected[index].runs;
        const std::vector<ComparableRun>& have = actual[index].runs;
        for (std::size_t run = 0U; run < want.size(); ++run) {
            require(want[run].colour == have[run].colour && want[run].background == have[run].background,
                    name + ".md block " + std::to_string(index) + " colours differ");
        }
    }
}

// RTF to markdown to RTF to markdown gives the same markdown, and the
// markdown reads to the document the RTF read to; the canonical markdown
// survives markdown to RTF to markdown unchanged.
void round_trips(const std::string& directory, const std::string& name) {
    const std::string rtf = read_file(directory + "/" + name + ".rtf");
    std::string markdown;
    RtfConversionResult result = import_rtf(bytes_of(rtf), markdown);
    require(result.succeeded() && result.body().has_value(), name + ".rtf does not import");
    const std::string expected = read_file(directory + "/" + name + ".md");
    require(markdown == expected, name + ".rtf does not import as the corpus markdown");
    std::vector<std::byte> written;
    result = export_rtf(markdown, written);
    require(result.succeeded() && !result.body().has_value(), name + ".md does not export");
    require(result.losses().has_value() && (*result.losses()).empty(), name + ".md exports with losses");
    require(looks_like_rtf(written), name + " exports RTF");
    std::string reread;
    result = import_rtf(written, reread);
    require(result.succeeded() && reread == markdown, name + " does not survive markdown to RTF to markdown");
}

// The documented examples (MarkdownDocument.rst, RtfConversionResult.rst)
// say what they produce; these hold them to it.
void documented_examples() {
    const std::string_view source = "# Plan for **May**\n";
    const MarkdownDocument document = parse(source);
    const MarkdownSpan& month = document.spans.at(1U);
    require(month.text == "May" && month.style.bold, "the example's bold span");
    require(month.source.start.value() == 13U && month.source.end.value() == 16U, "the example's source bytes");
    std::string plain;
    markdown_plain_text(document, plain);
    require(plain == "Plan for May", "the example's plain text");
    std::string markdown;
    const RtfConversionResult imported = import_rtf(bytes_of("{\\rtf1\\ansi {\\b Bold} text\\par}"), markdown);
    require(imported.succeeded() && markdown == "**Bold** text\n", "the example's import gave " + markdown);
}

void losses_and_failures() {
    std::vector<std::byte> rtf;
    RtfConversionResult result = export_rtf("<b>x</b>\n\n---\n\n| a |\n", rtf);
    require(result.succeeded(), "out-of-dialect markdown still exports");
    const MarkdownLossReport losses = *result.losses();
    require(losses.count(MarkdownLoss::html_kept_as_text) >= 1U, "HTML is kept as text");
    require(losses.count(MarkdownLoss::thematic_break_kept_as_text) == 1U, "the rule is kept as text");
    require(!losses.empty(), "the report says something was lost");
    require(describe_loss(MarkdownLoss::thematic_break_kept_as_text, 1U) == "1 horizontal rule was kept as text.",
            "a singular phrase");
    require(describe_loss(MarkdownLoss::picture_skipped, 3U) == "3 pictures were skipped.", "a plural phrase");
    require(describe_loss(MarkdownLoss::picture_skipped, 0U).empty(), "no phrase for no loss");
    std::vector<std::string> phrases;
    for (std::size_t index = 0U; index < markdown_loss_kind_count; ++index) {
        const std::string phrase = describe_loss(static_cast<MarkdownLoss>(index), 2U);
        require(!phrase.empty(), "every loss kind has a phrase");
        for (const std::string& other : phrases) require(other != phrase, "every phrase differs");
        phrases.push_back(phrase);
    }
    MarkdownLossReport report;
    report.add(MarkdownLoss::font_dropped, 0xFFFFFFF0U);
    report.add(MarkdownLoss::font_dropped, 0x100U);
    require(report.count(MarkdownLoss::font_dropped) == 0xFFFFFFFFU, "counts saturate");
    report.clear();
    require(report.empty(), "a cleared report is empty");

    std::string markdown = "stale";
    result = import_rtf(bytes_of("{\\rtf1 {unbalanced"), markdown);
    require(!result.succeeded() && result.error() == RtfConversionError::malformed, "malformed RTF");
    require(markdown.empty() && !result.losses().has_value(), "a failed import leaves no markdown");
    result = import_rtf(bytes_of("plain text"), markdown);
    require(result.error() == RtfConversionError::malformed, "text that is not RTF");
    require(!looks_like_rtf(bytes_of("{\\rt")) && looks_like_rtf(bytes_of("{\\rtf1}")), "the RTF signature");
    rtf.assign(4U, std::byte{1});
    const std::string invalid = std::string("bad ") + static_cast<char>(0xC0);
    result = export_rtf(invalid, rtf);
    require(!result.succeeded() && rtf.empty(), "invalid markdown does not export");
}

} // namespace

int main(int count, char** values) {
    try {
        require(count >= 2, "usage: gui_forms_markdown_tests <stx_rtf2md corpus directory>");
        const std::string directory = values[1];
        paragraphs_and_emphasis();
        blocks_and_lists();
        dialect_attributes();
        links_breaks_and_text();
        failures();
        const std::array<const char*, 4> names{"wordpad", "word", "textedit", "richtextbox"};
        for (const char* name : names) cross_check(directory, name);
        for (const char* name : names) round_trips(directory, name);
        losses_and_failures();
        documented_examples();
        std::cout << "gui_forms_markdown_tests: all tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_markdown_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
