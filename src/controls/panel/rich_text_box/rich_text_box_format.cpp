#include "gui_forms/controls/rich_text_box.hpp"

#include "../text_editing/text_editing_engine.hpp"
#include "gui_forms/detail/algorithm/binary_search.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace gui_forms {

namespace {

enum class ListMarkerKind : std::uint8_t { none, bullet, ordered };

// The markup at the start of a source line, as offsets into the line:
// block quote markers to quote_end, indentation to marker_start, a list
// marker and its space to marker_end, an ATX heading marker and its spaces
// to body.
struct LinePrefix final {
    std::size_t quote_end{};
    std::size_t marker_start{};
    std::size_t marker_end{};
    ListMarkerKind kind{ListMarkerKind::none};
    char bullet{'-'};
    char delimiter{'.'};
    std::uint32_t ordinal{};
    std::size_t heading_level{};
    std::size_t body{};
};

[[nodiscard]] bool is_blank(char character) noexcept {
    const bool blank = character == ' ' || character == '\t';
    return blank;
}

[[nodiscard]] bool is_digit(char character) noexcept {
    const bool digit = character >= '0' && character <= '9';
    return digit;
}

// Whether position ends a marker: the end of the line or a blank after it.
[[nodiscard]] bool marker_ends(std::string_view line, std::size_t position) noexcept {
    const bool ends = position == line.size() || is_blank(line[position]);
    return ends;
}

[[nodiscard]] std::size_t skip_spaces(std::string_view line, std::size_t position, std::size_t limit) noexcept {
    std::size_t cursor = position;
    while (cursor < line.size() && cursor - position < limit && line[cursor] == ' ') ++cursor;
    return cursor;
}

[[nodiscard]] LinePrefix parse_prefix(std::string_view line) {
    LinePrefix prefix{};
    std::size_t cursor = 0U;
    while (true) {
        const std::size_t indented = skip_spaces(line, cursor, 3U);
        if (indented == line.size() || line[indented] != '>') break;
        cursor = indented + 1U;
        if (cursor < line.size() && is_blank(line[cursor])) ++cursor;
        prefix.quote_end = cursor;
    }
    cursor = prefix.quote_end;
    while (cursor < line.size() && line[cursor] == ' ') ++cursor;
    prefix.marker_start = cursor;
    prefix.marker_end = cursor;
    const bool bullet_char = cursor < line.size() &&
        (line[cursor] == '-' || line[cursor] == '*' || line[cursor] == '+');
    if (bullet_char && marker_ends(line, cursor + 1U)) {
        prefix.kind = ListMarkerKind::bullet;
        prefix.bullet = line[cursor];
        prefix.marker_end = std::min(line.size(), cursor + 2U);
    } else {
        std::size_t digits = cursor;
        std::uint32_t ordinal = 0U;
        while (digits < line.size() && is_digit(line[digits]) && digits - cursor < 9U) {
            ordinal = ordinal * 10U + static_cast<std::uint32_t>(line[digits] - '0');
            ++digits;
        }
        const bool delimited = digits > cursor && digits < line.size() &&
                               (line[digits] == '.' || line[digits] == ')');
        if (delimited && marker_ends(line, digits + 1U)) {
            prefix.kind = ListMarkerKind::ordered;
            prefix.ordinal = ordinal;
            prefix.delimiter = line[digits];
            prefix.marker_end = std::min(line.size(), digits + 2U);
        }
    }
    cursor = prefix.marker_end;
    std::size_t hashes = cursor;
    while (hashes < line.size() && line[hashes] == '#' && hashes - cursor < 7U) ++hashes;
    const std::size_t level = hashes - cursor;
    prefix.body = prefix.marker_end;
    if (level >= 1U && level <= 6U && marker_ends(line, hashes)) {
        prefix.heading_level = level;
        std::size_t body = hashes;
        while (body < line.size() && is_blank(line[body])) ++body;
        prefix.body = body;
    }
    return prefix;
}

// Whether a run of `run` delimiter characters wraps text as `delimiter`
// does: a double delimiter needs two or three, a single * or _ an odd
// run, a backtick exactly one.
[[nodiscard]] bool run_matches(std::string_view delimiter, std::size_t run) noexcept {
    if (delimiter.size() == 2U) {
        const bool matches = run == 2U || run == 3U;
        return matches;
    }
    if (delimiter == "`") {
        const bool matches = run == 1U;
        return matches;
    }
    const bool matches = run == 1U || run == 3U;
    return matches;
}

// The run of `character` ending at end, no further back than limit.
[[nodiscard]] std::size_t run_before(std::string_view text, std::size_t end, std::size_t limit, char character) noexcept {
    std::size_t cursor = end;
    while (cursor > limit && text[cursor - 1U] == character) --cursor;
    const std::size_t run = end - cursor;
    return run;
}

// The run of `character` starting at start, no further than limit.
[[nodiscard]] std::size_t run_after(std::string_view text, std::size_t start, std::size_t limit, char character) noexcept {
    std::size_t cursor = start;
    while (cursor < limit && text[cursor] == character) ++cursor;
    const std::size_t run = cursor - start;
    return run;
}

// How one range of text is wrapped by a delimiter already.
enum class Wrapping : std::uint8_t { none, outside, inside };

struct WrappedRange final {
    std::size_t start{};
    std::size_t end{};
    Wrapping wrapping{Wrapping::none};
    std::size_t delimiter{};
};

// The span whose source holds offset, or null.
[[nodiscard]] const MarkdownSpan* span_at(const MarkdownDocument& document, std::size_t offset) {
    struct StartsAfter final {
        bool operator()(std::size_t value, const MarkdownSpan& span) const noexcept {
            const bool after = value < span.source.start.value();
            return after;
        }
    };
    const std::span<const MarkdownSpan> spans(document.spans);
    const StartsAfter after{};
    std::size_t index = detail::upper_bound_index(spans, offset, after);
    while (index > 0U) {
        --index;
        const MarkdownSpan& span = spans[index];
        if (span.source.start == span.source.end) continue;
        if (span.source.end.value() <= offset || span.kind != MarkdownSpanKind::text) return nullptr;
        const MarkdownSpan* found = &span;
        return found;
    }
    return nullptr;
}

RichTextFormatState format_state(bool holds) noexcept {
    const RichTextFormatState state = holds ? RichTextFormatState::on : RichTextFormatState::off;
    return state;
}

// Counts where a format holds and where it does not, over a selection's
// characters or lines.
struct FormatTally final {
    bool with{};
    bool without{};
    void add(bool holds) noexcept {
        if (holds) {
            with = true;
        } else {
            without = true;
        }
    }
    [[nodiscard]] RichTextFormatState state() const noexcept {
        if (with && without) return RichTextFormatState::mixed;
        const RichTextFormatState result = with ? RichTextFormatState::on : RichTextFormatState::off;
        return result;
    }
};

} // namespace

// -- lines -----------------------------------------------------------------------

std::size_t RichTextBox::line_of(Utf8Offset offset) const {
    const TextStore& store = (*editing_).store();
    std::size_t low = 0U;
    std::size_t high = store.line_count();
    while (high - low > 1U) {
        const std::size_t middle = low + (high - low) / 2U;
        if (store.line_start(LineIndex(middle)).value() <= offset.value()) {
            low = middle;
        } else {
            high = middle;
        }
    }
    return low;
}

RichTextBox::LineSpan RichTextBox::touched_lines() const {
    const TextSelection selection = (*editing_).selection();
    const TextStore& store = (*editing_).store();
    LineSpan span{.first = line_of(selection.start()), .last = line_of(selection.end())};
    // A selection ending at the start of a line does not touch that line.
    if (span.last > span.first && store.line_start(LineIndex(span.last)) == selection.end()) --span.last;
    return span;
}

// -- the format at the selection ----------------------------------------------------

RichTextFormat RichTextBox::selection_format() const {
    RichTextFormat format{};
    const TextSelection selection = (*editing_).selection();
    const TextStore& store = (*editing_).store();
    const BorrowedText source = store.utf8();
    if (selection.empty()) {
        if (selection.caret.value() > 0U) {
            const Utf8Offset before = store.previous_grapheme_boundary(selection.caret);
            const MarkdownSpan* span = span_at(document_, before.value());
            if (span != nullptr) {
                format.bold = format_state((*span).style.bold);
                format.italic = format_state((*span).style.italic);
                format.code = format_state((*span).style.code);
                format.link = format_state((*span).link.has_value());
            }
        }
    } else {
        FormatTally bold{};
        FormatTally italic{};
        FormatTally code{};
        FormatTally link{};
        const std::size_t end = selection.end().value();
        for (const MarkdownSpan& span : document_.spans) {
            const std::size_t start = span.source.start.value();
            if (start >= end) break;
            if (span.kind != MarkdownSpanKind::text || start == span.source.end.value() ||
                span.source.end.value() <= selection.start().value()) {
                continue;
            }
            bold.add(span.style.bold);
            italic.add(span.style.italic);
            code.add(span.style.code);
            link.add(span.link.has_value());
        }
        format.bold = bold.state();
        format.italic = italic.state();
        format.code = code.state();
        format.link = link.state();
    }
    const LineSpan lines = touched_lines();
    FormatTally bulleted{};
    FormatTally numbered{};
    FormatTally heading{};
    std::optional<std::size_t> level{};
    bool same_level = true;
    for (std::size_t line = lines.first; line <= lines.last; ++line) {
        const Utf8Range range = store.line_content_range(LineIndex(line));
        const std::string_view content = source.substr(range.start.value(), range.end.value() - range.start.value());
        const LinePrefix prefix = parse_prefix(content);
        const bool blank = prefix.body == content.size() && prefix.kind == ListMarkerKind::none &&
                           prefix.heading_level == 0U;
        if (blank && lines.first != lines.last) continue;
        bulleted.add(prefix.kind == ListMarkerKind::bullet);
        numbered.add(prefix.kind == ListMarkerKind::ordered);
        heading.add(prefix.heading_level > 0U);
        if (!level) level = prefix.heading_level;
        same_level = same_level && *level == prefix.heading_level;
    }
    format.bulleted = bulleted.state();
    format.numbered = numbered.state();
    // Headings of different levels are mixed, as body text beside a heading is.
    format.heading = heading.state() == RichTextFormatState::on && !same_level ? RichTextFormatState::mixed
                                                                              : heading.state();
    format.heading_level = format.heading == RichTextFormatState::on && level ? *level : 0U;
    return format;
}

// -- applying changes -----------------------------------------------------------------

namespace {

// Where an offset lands after sorted, nonoverlapping changes. An insertion
// exactly at the offset lands it after the inserted text when `after`.
struct OffsetCarrier final {
    [[nodiscard]] std::size_t operator()(std::size_t offset, std::span<const detail::RichTextSourceChange> changes,
                                         bool after) const noexcept {
        std::ptrdiff_t delta = 0;
        for (const detail::RichTextSourceChange& change : changes) {
            const std::ptrdiff_t grown = static_cast<std::ptrdiff_t>(change.text.size()) -
                                         static_cast<std::ptrdiff_t>(change.end - change.start);
            const bool insertion = change.start == change.end;
            if (insertion && offset == change.start) {
                if (after) delta += grown;
                break;
            }
            if (offset >= change.end) {
                delta += grown;
                continue;
            }
            if (offset > change.start) {
                const std::size_t landed = static_cast<std::size_t>(
                    static_cast<std::ptrdiff_t>(change.start) + delta) + change.text.size();
                return landed;
            }
            break;
        }
        const std::size_t landed = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(offset) + delta);
        return landed;
    }
};

} // namespace

bool RichTextBox::apply_changes(const std::vector<SourceChange>& changes, std::optional<TextSelection> selection) {
    require_mutable();
    if (changes.empty() || read_only.read()) return false;
    const BorrowedText source = (*editing_).text();
    const std::size_t region_start = changes.front().start;
    const std::size_t region_end = changes.back().end;
    std::string replacement{};
    std::size_t cursor = region_start;
    for (const SourceChange& change : changes) {
        replacement.append(source.substr(cursor, change.start - cursor));
        replacement.append(change.text);
        cursor = change.end;
    }
    replacement.append(source.substr(cursor, region_end - cursor));
    if (replacement == source.substr(region_start, region_end - region_start)) return false;
    TextSelection next{};
    if (selection) {
        next = *selection;
    } else {
        const TextSelection old = (*editing_).selection();
        const OffsetCarrier carry{};
        const std::span<const SourceChange> view(changes);
        if (old.empty()) {
            const Utf8Offset caret(carry(old.caret.value(), view, true));
            next = {caret, caret};
        } else {
            const Utf8Offset start(carry(old.start().value(), view, true));
            const Utf8Offset end(carry(old.end().value(), view, false));
            const bool forward = old.anchor <= old.caret;
            next = forward ? TextSelection{start, end} : TextSelection{end, start};
        }
    }
    const bool replaced = (*editing_).replace(Utf8Offset(region_start), Utf8Offset(region_end), replacement,
                                              TextEditKind::typing);
    if (!replaced) return false;
    const TextStore& store = (*editing_).store();
    const bool valid = next.anchor <= store.utf8_size() && next.caret <= store.utf8_size() &&
                       store.is_grapheme_boundary(next.anchor) && store.is_grapheme_boundary(next.caret);
    if (valid) set_selection(next);
    return true;
}

// -- span commands --------------------------------------------------------------------

bool RichTextBox::toggle_wrap(std::span<const std::string_view> delimiters) {
    require_mutable();
    if (read_only.read()) return false;
    const TextSelection selection = (*editing_).selection();
    const TextStore& store = (*editing_).store();
    const BorrowedText source = store.utf8();
    const std::string_view written = delimiters.front();
    std::vector<SourceChange> changes{};
    if (selection.empty()) {
        const std::size_t caret = selection.caret.value();
        for (const std::string_view delimiter : delimiters) {
            const std::size_t length = delimiter.size();
            const bool open = caret >= length && source.substr(caret - length, length) == delimiter;
            const bool close = source.substr(caret, length) == delimiter;
            if (open && close) {
                changes.push_back({.start = caret - length, .end = caret, .text = {}});
                changes.push_back({.start = caret, .end = caret + length, .text = {}});
                const Utf8Offset landed(caret - length);
                const bool done = apply_changes(changes, TextSelection{landed, landed});
                return done;
            }
        }
        std::string pair(written);
        pair.append(written);
        changes.push_back({.start = caret, .end = caret, .text = pair});
        const Utf8Offset landed(caret + written.size());
        const bool done = apply_changes(changes, TextSelection{landed, landed});
        return done;
    }
    // The selected text on each line it covers, past the line's markers,
    // without its leading and trailing blanks.
    std::vector<WrappedRange> ranges{};
    const LineSpan lines = touched_lines();
    for (std::size_t line = lines.first; line <= lines.last; ++line) {
        const Utf8Range range = store.line_content_range(LineIndex(line));
        const std::size_t line_start = range.start.value();
        const std::size_t line_end = range.end.value();
        const LinePrefix prefix = parse_prefix(source.substr(line_start, line_end - line_start));
        std::size_t start = std::max(selection.start().value(), line_start + prefix.body);
        std::size_t end = std::min(selection.end().value(), line_end);
        while (start < end && is_blank(source[start])) ++start;
        while (end > start && is_blank(source[end - 1U])) --end;
        if (start >= end) continue;
        WrappedRange wrapped{.start = start, .end = end};
        for (std::size_t index = 0U; index < delimiters.size(); ++index) {
            const std::string_view delimiter = delimiters[index];
            const char character = delimiter.front();
            const std::size_t outside_before = run_before(source, start, line_start, character);
            const std::size_t outside_after = run_after(source, end, line_end, character);
            if (run_matches(delimiter, outside_before) && run_matches(delimiter, outside_after)) {
                wrapped.wrapping = Wrapping::outside;
                wrapped.delimiter = index;
                break;
            }
            const std::size_t inside_start = run_after(source, start, end, character);
            const std::size_t inside_end = run_before(source, end, start, character);
            const bool room = end - start > 2U * delimiter.size();
            if (room && run_matches(delimiter, inside_start) && run_matches(delimiter, inside_end)) {
                wrapped.wrapping = Wrapping::inside;
                wrapped.delimiter = index;
                break;
            }
        }
        ranges.push_back(wrapped);
    }
    if (ranges.empty()) return false;
    bool all_wrapped = true;
    for (const WrappedRange& range : ranges) {
        all_wrapped = all_wrapped && range.wrapping != Wrapping::none;
    }
    for (const WrappedRange& range : ranges) {
        const std::size_t length = delimiters[range.delimiter].size();
        if (all_wrapped && range.wrapping == Wrapping::outside) {
            changes.push_back({.start = range.start - length, .end = range.start, .text = {}});
            changes.push_back({.start = range.end, .end = range.end + length, .text = {}});
        } else if (all_wrapped && range.wrapping == Wrapping::inside) {
            changes.push_back({.start = range.start, .end = range.start + length, .text = {}});
            changes.push_back({.start = range.end - length, .end = range.end, .text = {}});
        } else if (range.wrapping == Wrapping::none) {
            changes.push_back({.start = range.start, .end = range.start, .text = std::string(written)});
            changes.push_back({.start = range.end, .end = range.end, .text = std::string(written)});
        }
    }
    const bool done = apply_changes(changes, std::nullopt);
    return done;
}

bool RichTextBox::toggle_bold() {
    constexpr std::array<std::string_view, 2> delimiters{"**", "__"};
    const bool done = toggle_wrap(delimiters);
    return done;
}

bool RichTextBox::toggle_italic() {
    constexpr std::array<std::string_view, 2> delimiters{"*", "_"};
    const bool done = toggle_wrap(delimiters);
    return done;
}

bool RichTextBox::toggle_code() {
    constexpr std::array<std::string_view, 1> delimiters{"`"};
    const bool done = toggle_wrap(delimiters);
    return done;
}

bool RichTextBox::insert_link() {
    require_mutable();
    if (read_only.read()) return false;
    const TextSelection selection = (*editing_).selection();
    std::vector<SourceChange> changes{};
    if (selection.empty()) {
        const std::size_t caret = selection.caret.value();
        changes.push_back({.start = caret, .end = caret, .text = "[]()"});
        const Utf8Offset landed(caret + 1U);
        const bool done = apply_changes(changes, TextSelection{landed, landed});
        return done;
    }
    const std::size_t start = selection.start().value();
    const std::size_t end = selection.end().value();
    changes.push_back({.start = start, .end = start, .text = "["});
    changes.push_back({.start = end, .end = end, .text = "]()"});
    // The caret goes between the parentheses, where the destination goes.
    const Utf8Offset landed(end + 3U);
    const bool done = apply_changes(changes, TextSelection{landed, landed});
    return done;
}

// -- line commands ------------------------------------------------------------------

bool RichTextBox::set_heading_level(std::size_t level) {
    require_mutable();
    if (level > 6U) throw std::invalid_argument("RichTextBox heading level must be 0 to 6");
    if (read_only.read()) return false;
    const TextStore& store = (*editing_).store();
    const BorrowedText source = store.utf8();
    const LineSpan lines = touched_lines();
    std::string marker{};
    if (level > 0U) {
        marker.assign(level, '#');
        marker.push_back(' ');
    }
    std::vector<SourceChange> changes{};
    for (std::size_t line = lines.first; line <= lines.last; ++line) {
        const Utf8Range range = store.line_content_range(LineIndex(line));
        const std::size_t line_start = range.start.value();
        const std::string_view content = source.substr(line_start, range.end.value() - line_start);
        const LinePrefix prefix = parse_prefix(content);
        const bool blank = prefix.body == content.size();
        if (blank && lines.first != lines.last) continue;
        changes.push_back({.start = line_start + prefix.marker_end, .end = line_start + prefix.body,
                           .text = marker});
    }
    const bool done = apply_changes(changes, std::nullopt);
    return done;
}

bool RichTextBox::toggle_list(bool numbered) {
    require_mutable();
    if (read_only.read()) return false;
    const TextStore& store = (*editing_).store();
    const BorrowedText text = store.utf8();
    const LineSpan lines = touched_lines();
    const ListMarkerKind wanted = numbered ? ListMarkerKind::ordered : ListMarkerKind::bullet;
    struct Touched final {
        std::size_t start{};
        LinePrefix prefix{};
    };
    std::vector<Touched> touched{};
    bool all_marked = true;
    for (std::size_t line = lines.first; line <= lines.last; ++line) {
        const Utf8Range range = store.line_content_range(LineIndex(line));
        const std::size_t line_start = range.start.value();
        const std::string_view content = text.substr(line_start, range.end.value() - line_start);
        const LinePrefix prefix = parse_prefix(content);
        const bool blank = prefix.body == content.size() && prefix.kind == ListMarkerKind::none;
        if (blank && lines.first != lines.last) continue;
        all_marked = all_marked && prefix.kind == wanted;
        touched.push_back({.start = line_start, .prefix = prefix});
    }
    if (touched.empty()) return false;
    std::vector<SourceChange> changes{};
    std::uint32_t ordinal = 1U;
    for (const Touched& line : touched) {
        const std::size_t start = line.start + line.prefix.marker_start;
        const std::size_t end = line.start + line.prefix.marker_end;
        std::string marker{};
        if (!all_marked) {
            marker = numbered ? std::to_string(ordinal) + ". " : std::string("- ");
            ++ordinal;
        }
        changes.push_back({.start = start, .end = end, .text = marker});
    }
    const bool done = apply_changes(changes, std::nullopt);
    return done;
}

bool RichTextBox::toggle_bulleted_list() {
    const bool done = toggle_list(false);
    return done;
}

bool RichTextBox::toggle_numbered_list() {
    const bool done = toggle_list(true);
    return done;
}

bool RichTextBox::continue_list() {
    const TextSelection selection = (*editing_).selection();
    if (!selection.empty() || read_only.read()) return false;
    const TextStore& store = (*editing_).store();
    const BorrowedText text = store.utf8();
    const std::size_t line = line_of(selection.caret);
    const Utf8Range range = store.line_content_range(LineIndex(line));
    const std::size_t line_start = range.start.value();
    const std::string_view content = text.substr(line_start, range.end.value() - line_start);
    const LinePrefix prefix = parse_prefix(content);
    if (prefix.kind == ListMarkerKind::none && prefix.quote_end == 0U) return false;
    const std::size_t caret = selection.caret.value();
    const std::size_t body = prefix.kind == ListMarkerKind::none ? prefix.quote_end : prefix.marker_end;
    if (caret < line_start + body) return false;
    std::vector<SourceChange> changes{};
    const bool empty_item = body == content.size();
    if (empty_item) {
        // Enter on an empty item ends the list (or the quote): its marker
        // goes and the line stays.
        const std::size_t from = prefix.kind == ListMarkerKind::none ? 0U : prefix.marker_start;
        changes.push_back({.start = line_start + from, .end = line_start + body, .text = {}});
        const Utf8Offset landed(line_start + from);
        const bool done = apply_changes(changes, TextSelection{landed, landed});
        return done;
    }
    std::string next("\n");
    next.append(content.substr(0U, prefix.marker_start));
    if (prefix.kind == ListMarkerKind::bullet) {
        next.push_back(prefix.bullet);
        next.push_back(' ');
    } else if (prefix.kind == ListMarkerKind::ordered) {
        next.append(std::to_string(prefix.ordinal + 1U));
        next.push_back(prefix.delimiter);
        next.push_back(' ');
    } else {
        // A quote alone: the quote markers copied above continue it.
    }
    changes.push_back({.start = caret, .end = caret, .text = next});
    const Utf8Offset landed(caret + next.size());
    const bool done = apply_changes(changes, TextSelection{landed, landed});
    return done;
}

} // namespace gui_forms
