#include "gui_forms/text/paragraph_layout/paragraph_layout.hpp"

#include "gui_forms/types/painter/painter.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace gui_forms {
namespace {

// A run fits a line when it is at most this much wider than the line: the
// tolerance TextBlock and DataGridView's wrapping use
// (line_wrapping_detail::wrap_fit_tolerance), so the same runs fit.
constexpr double paragraph_fit_tolerance = 1.0 / 1024.0;

[[nodiscard]] bool collapsible_space(char character) noexcept {
    return character == ' ' || character == '\t' || character == '\n' || character == '\r' ||
           character == '\f';
}

// A byte that hangs at a line's end: not measured or painted (CSS's
// trailing spaces, and a line's own line end).
[[nodiscard]] bool hanging_byte(char character) noexcept {
    return character == ' ' || character == '\n' || character == '\r';
}

[[nodiscard]] bool continuation_byte(char character) noexcept {
    return (static_cast<unsigned char>(character) & 0xc0U) == 0x80U;
}

// The collapsed size of `text` (TextBlock's white-space collapsing), without
// writing it.
[[nodiscard]] std::size_t collapsed_size(std::string_view text) noexcept {
    std::size_t size = 0U;
    bool pending_space = false;
    for (const char character : text) {
        if (collapsible_space(character)) {
            pending_space = size != 0U;
            continue;
        }
        if (pending_space) ++size;
        pending_space = false;
        ++size;
    }
    return size;
}

// Writes the collapsed text into `output`, which holds collapsed_size(text)
// bytes and does not overlap `text`.
void write_collapsed(std::string_view text, std::span<char> output) noexcept {
    std::size_t size = 0U;
    bool pending_space = false;
    for (const char character : text) {
        if (collapsible_space(character)) {
            pending_space = size != 0U;
            continue;
        }
        if (pending_space) {
            output[size] = ' ';
            ++size;
        }
        pending_space = false;
        output[size] = character;
        ++size;
    }
}

[[nodiscard]] bool less_exact(TextResolutionStatus candidate, TextResolutionStatus current) noexcept {
    return static_cast<std::uint8_t>(candidate) > static_cast<std::uint8_t>(current);
}

// One call's laid-out text, its per-byte facts, and what the call has cost.
// The text and arrays are borrowed for the call; the arrays hold one
// element per byte of `text`.
struct ParagraphPass final {
    TextMetricsProvider* provider{};
    std::string_view text{};
    std::span<const LineBreakOpportunity> opportunities{};
    std::span<const double> advances{};
    FontSpec font{};
    // The widest run that fits: the line width plus the tolerance.
    double limit{};
    ParagraphFit fit{ParagraphFit::verified};
    ParagraphOverflowWrap overflow_wrap{ParagraphOverflowWrap::normal};
    TextResolutionStatus status{TextResolutionStatus::exact};
    std::uint64_t shaping_calls{};
    std::uint64_t shaped_bytes{};
};

void note_status(ParagraphPass& pass, TextResolutionStatus status) noexcept {
    if (status == TextResolutionStatus::invalid_request) return;
    if (less_exact(status, pass.status)) pass.status = status;
}

// Whether a line may end at `position` (after byte position - 1): the
// text's end, or an allowed or mandatory opportunity.
[[nodiscard]] bool line_may_end(const ParagraphPass& pass, std::size_t position) noexcept {
    if (position == pass.text.size()) return true;
    const LineBreakOpportunity opportunity = pass.opportunities[position - 1U];
    const bool may_end = opportunity == LineBreakOpportunity::allowed || opportunity == LineBreakOpportunity::mandatory;
    return may_end;
}

// Whether a line must end at `position`, before the text's end.
[[nodiscard]] bool line_must_end(const ParagraphPass& pass, std::size_t position) noexcept {
    if (position == pass.text.size()) return false;
    const bool forced = pass.opportunities[position - 1U] == LineBreakOpportunity::mandatory;
    return forced;
}

[[nodiscard]] std::size_t content_end(std::string_view text, std::size_t begin, std::size_t end) noexcept {
    while (end > begin && hanging_byte(text[end - 1U])) --end;
    return end;
}

[[nodiscard]] std::size_t next_line_end(const ParagraphPass& pass, std::size_t position) noexcept {
    std::size_t next = position + 1U;
    while (!line_may_end(pass, next)) ++next;
    return next;
}

// The opportunity before `position` on the line from `begin`; the caller
// knows there is one (position is not the line's first).
[[nodiscard]] std::size_t previous_line_end(const ParagraphPass& pass, std::size_t begin,
                                            std::size_t position) noexcept {
    std::size_t previous = position - 1U;
    while (previous > begin + 1U && !line_may_end(pass, previous)) --previous;
    return previous;
}

// The provider's width of text[begin, end) shaped alone: what TextBlock
// measures and Painter::draw_text_utf8 draws.
[[nodiscard]] double whole_width(ParagraphPass& pass, std::size_t begin, std::size_t end) {
    if (end == begin) return 0.0;
    const std::string_view run = pass.text.substr(begin, end - begin);
    ++pass.shaping_calls;
    pass.shaped_bytes += static_cast<std::uint64_t>(run.size());
    TextExtent extent{};
    if (pass.provider != nullptr) {
        extent = (*pass.provider).measure_text_extent_utf8(run, pass.font);
    } else {
        extent = text_extent_of(estimate_text_layout_utf8(run, pass.font));
    }
    note_status(pass, extent.status);
    const double width = extent.logical_size.width;
    return width;
}

// The sum of the cluster advances of text[begin, end), less the letter
// spacing the paragraph's shaping put after its last cluster when that is
// not the text's last (a run shaped alone has none after its last).
[[nodiscard]] double advance_sum(const ParagraphPass& pass, std::size_t begin, std::size_t end) noexcept {
    double sum = 0.0;
    for (std::size_t index = begin; index < end; ++index) sum += pass.advances[index];
    if (end > begin && end < pass.text.size()) sum -= pass.font.letter_spacing;
    return sum;
}

// A line as the search chooses it: [begin, content) shown, `next` where the
// following line starts, `width` its width.
struct LineChoice final {
    std::size_t begin{};
    std::size_t content{};
    std::size_t next{};
    double width{};
};

// The end of the line from `begin` by advance sums alone: the last
// candidate end before the first whose run does not fit, the first always
// taken and a mandatory end always ending the line. One pass over the
// line's bytes, keeping a running sum (its value at the last unhung byte is
// the candidate run's sum).
[[nodiscard]] LineChoice estimate_line(const ParagraphPass& pass, std::size_t begin) noexcept {
    const std::size_t size = pass.text.size();
    LineChoice accepted{begin, begin, begin, 0.0};
    bool has_accepted = false;
    double running = 0.0;
    double content_sum = 0.0;
    std::size_t content = begin;
    for (std::size_t position = begin + 1U; position <= size; ++position) {
        const char byte = pass.text[position - 1U];
        running += pass.advances[position - 1U];
        if (!hanging_byte(byte)) {
            content_sum = running;
            content = position;
        }
        if (!line_may_end(pass, position)) continue;
        double estimate = content_sum;
        if (content > begin && content < size) estimate -= pass.font.letter_spacing;
        if (has_accepted && estimate > pass.limit) break;
        accepted = LineChoice{begin, content, position, estimate};
        has_accepted = true;
        if (line_must_end(pass, position)) break;
    }
    return accepted;
}

// Confirms an estimated line with whole-run measurements: steps back while
// the chosen run does not fit (keeping the first candidate), then forward
// while the next candidate's run fits, stopping at a mandatory end. Under
// widths that do not shrink as a run grows, the result is TextBlock's line:
// the run before the first candidate that does not fit.
[[nodiscard]] LineChoice verify_line(ParagraphPass& pass, LineChoice choice) {
    const std::size_t begin = choice.begin;
    const std::size_t first = next_line_end(pass, begin);
    choice.width = whole_width(pass, begin, choice.content);
    std::size_t known_too_wide = 0U;
    while (choice.next != first && choice.width > pass.limit) {
        known_too_wide = choice.next;
        choice.next = previous_line_end(pass, begin, choice.next);
        choice.content = content_end(pass.text, begin, choice.next);
        choice.width = whole_width(pass, begin, choice.content);
    }
    while (choice.next < pass.text.size() && !line_must_end(pass, choice.next)) {
        const std::size_t candidate = next_line_end(pass, choice.next);
        if (candidate == known_too_wide) break;
        const std::size_t candidate_content = content_end(pass.text, begin, candidate);
        const double candidate_width = whole_width(pass, begin, candidate_content);
        if (candidate_width > pass.limit) break;
        choice.next = candidate;
        choice.content = candidate_content;
        choice.width = candidate_width;
    }
    return choice;
}

// Whether a word may break before byte `position`: a cluster starts there
// (the provider put its advance there; a zero-advance cluster joins the one
// before it) at a character start.
[[nodiscard]] bool cluster_starts(const ParagraphPass& pass, std::size_t position) noexcept {
    const bool starts = !continuation_byte(pass.text[position]) && pass.advances[position] != 0.0;
    return starts;
}

// Breaks the over-wide first word of a line, text[begin, word_end), between
// clusters (ParagraphOverflowWrap::break_word): the longest run of whole
// clusters that fits, at least one. Advance sums propose the break; under
// ParagraphFit::verified whole-run measurements confirm it as verify_line
// does. A word of one cluster is returned whole, overflowing.
[[nodiscard]] LineChoice break_inside_word(ParagraphPass& pass, std::size_t begin, std::size_t word_end,
                                           LineChoice whole_word) {
    std::size_t first_break = begin + 1U;
    while (first_break < word_end && !cluster_starts(pass, first_break)) ++first_break;
    if (first_break >= word_end) return whole_word;
    // Advance sums: the last cluster start whose run fits, at least the
    // first.
    std::size_t chosen = first_break;
    double running = 0.0;
    for (std::size_t index = begin; index < first_break; ++index) running += pass.advances[index];
    for (std::size_t position = first_break + 1U; position < word_end; ++position) {
        running += pass.advances[position - 1U];
        if (!cluster_starts(pass, position)) continue;
        const double estimate = running - pass.font.letter_spacing;
        if (estimate > pass.limit) break;
        chosen = position;
    }
    LineChoice choice{begin, chosen, chosen, advance_sum(pass, begin, chosen)};
    if (pass.fit != ParagraphFit::verified) return choice;
    choice.width = whole_width(pass, begin, chosen);
    std::size_t known_too_wide = 0U;
    while (choice.next != first_break && choice.width > pass.limit) {
        known_too_wide = choice.next;
        std::size_t previous = choice.next - 1U;
        while (previous > first_break && !cluster_starts(pass, previous)) --previous;
        choice.next = previous;
        choice.content = previous;
        choice.width = whole_width(pass, begin, previous);
    }
    while (true) {
        std::size_t candidate = choice.next + 1U;
        while (candidate < word_end && !cluster_starts(pass, candidate)) ++candidate;
        if (candidate >= word_end || candidate == known_too_wide) break;
        const double candidate_width = whole_width(pass, begin, candidate);
        if (candidate_width > pass.limit) break;
        choice.next = candidate;
        choice.content = candidate;
        choice.width = candidate_width;
    }
    return choice;
}

// The line from `begin`: estimated, verified under ParagraphFit::verified,
// and its first word broken under break_word when the line is that word
// alone and too wide.
[[nodiscard]] LineChoice choose_line(ParagraphPass& pass, std::size_t begin) {
    LineChoice choice = estimate_line(pass, begin);
    if (pass.fit == ParagraphFit::verified) choice = verify_line(pass, choice);
    if (pass.overflow_wrap != ParagraphOverflowWrap::break_word) return choice;
    if (choice.width <= pass.limit) return choice;
    if (choice.next != next_line_end(pass, begin)) return choice;
    const LineChoice broken = break_inside_word(pass, begin, choice.content, choice);
    return broken;
}

// The left edge of a line `width` wide in a box `box_width` wide, as
// TextBlock places its lines.
[[nodiscard]] double line_left(const ParagraphStyle& style, double width) noexcept {
    const bool right_to_left = style.direction == TextDirection::right_to_left;
    const double free = style.width - width;
    switch (style.alignment) {
    case TextAlignment::start: return right_to_left ? free : 0.0;
    case TextAlignment::end: return right_to_left ? 0.0 : free;
    case TextAlignment::left: return 0.0;
    case TextAlignment::right: return free;
    case TextAlignment::center: return free * 0.5;
    }
    return 0.0;
}

// Counts characters (scalar values; the text is valid UTF-8, so every
// non-continuation byte starts one) as positions advance through the text.
struct ScalarCursor final {
    std::size_t byte{};
    std::size_t scalar{};
};

[[nodiscard]] ScalarIndex advance_cursor(std::string_view text, ScalarCursor& cursor, std::size_t byte) noexcept {
    for (; cursor.byte < byte; ++cursor.byte) {
        if (!continuation_byte(text[cursor.byte])) ++cursor.scalar;
    }
    const ScalarIndex index(cursor.scalar);
    return index;
}

enum class LineOutput : std::uint8_t { records, count_only };

[[nodiscard]] std::optional<ParagraphRequestError> request_error(std::string_view text,
                                                                 const ParagraphStyle& style) noexcept {
    if (!validate_utf8(text).valid()) return ParagraphRequestError::invalid_utf8;
    if (!valid_font_spec(style.font)) return ParagraphRequestError::invalid_font;
    if (!std::isfinite(style.width) || style.width < 0.0) return ParagraphRequestError::invalid_width;
    if (!std::isfinite(style.line_box.height) || !(style.line_box.height > 0.0)) {
        return ParagraphRequestError::invalid_line_box;
    }
    return std::nullopt;
}

// The shared body of lay_out_paragraph and measure_paragraph; `output`
// chooses whether line records are written, before the loop.
template <LineOutput Output>
[[nodiscard]] ParagraphLayoutResult lay_out(TextMetricsProvider* provider, std::string_view text,
                                            const ParagraphStyle& style, const ParagraphStorage& storage) {
    // Checks, before anything is written.
    const std::optional<ParagraphRequestError> error = request_error(text, style);
    if (error) return ParagraphLayoutResult::refused(*error);
    const ParagraphStorageRequirement bound = paragraph_storage_bound(text, style.white_space);
    const std::size_t size = bound.per_byte;
    const bool per_byte_short = storage.opportunities.size() < size || storage.advances.size() < size ||
                                storage.collapsed_text.size() < bound.collapsed_bytes;
    if (per_byte_short) return ParagraphLayoutResult::storage_too_small(bound);

    // The laid-out text: the caller's, or its collapsed form in the
    // caller's storage.
    std::string_view laid_out = text;
    if (style.white_space == ParagraphWhiteSpace::collapse) {
        const std::span<char> collapsed = storage.collapsed_text.first(size);
        write_collapsed(text, collapsed);
        laid_out = std::string_view(collapsed.data(), collapsed.size());
    }
    ParagraphMetrics metrics{};
    metrics.laid_out_bytes = size;
    if (size == 0U) return ParagraphLayoutResult::laid_out(metrics);

    // Break opportunities: prohibited everywhere until the provider
    // answers; a provider that refuses the (valid) text leaves the
    // estimate's.
    const std::span<LineBreakOpportunity> opportunities = storage.opportunities.first(size);
    std::fill(opportunities.begin(), opportunities.end(), LineBreakOpportunity::prohibited);
    TextResolutionStatus break_status = TextResolutionStatus::invalid_request;
    if (provider != nullptr) break_status = (*provider).fill_line_break_opportunities_utf8(laid_out, opportunities);
    if (break_status == TextResolutionStatus::invalid_request) {
        break_status = estimate_line_break_opportunities_utf8(laid_out, opportunities);
    }
    // Cluster advances: one shaping of the paragraph.
    const std::span<double> advances = storage.advances.first(size);
    std::fill(advances.begin(), advances.end(), 0.0);
    TextResolutionStatus advance_status = TextResolutionStatus::invalid_request;
    if (provider != nullptr) advance_status = (*provider).fill_cluster_advances_utf8(laid_out, style.font, advances);
    if (advance_status == TextResolutionStatus::invalid_request) {
        advance_status = estimate_cluster_advances_utf8(laid_out, style.font, advances);
    }

    ParagraphPass pass{};
    pass.provider = provider;
    pass.text = laid_out;
    pass.opportunities = opportunities;
    pass.advances = advances;
    pass.font = style.font;
    pass.limit = style.width + paragraph_fit_tolerance;
    pass.fit = style.fit;
    pass.overflow_wrap = style.overflow_wrap;
    pass.shaping_calls = 1U;
    pass.shaped_bytes = static_cast<std::uint64_t>(size);
    note_status(pass, break_status);
    note_status(pass, advance_status);

    // Lines, in order; each starts where the one before said the next
    // starts, and holds at least one byte, so the loop ends.
    [[maybe_unused]] const std::span<ParagraphLine> lines = storage.lines;
    [[maybe_unused]] ScalarCursor cursor{};
    std::size_t line_count = 0U;
    double widest = 0.0;
    std::size_t begin = 0U;
    while (begin < size) {
        const LineChoice choice = choose_line(pass, begin);
        widest = std::max(widest, choice.width);
        if constexpr (Output == LineOutput::records) {
            if (line_count < lines.size()) {
                ParagraphLine line{};
                const ScalarIndex first_character = advance_cursor(laid_out, cursor, choice.begin);
                const ScalarIndex end_character = advance_cursor(laid_out, cursor, choice.content);
                line.bytes = Utf8Range{Utf8Offset(choice.begin), Utf8Offset(choice.content)};
                line.characters = ScalarRange{first_character, end_character};
                line.next_line_start = Utf8Offset(choice.next);
                line.width = choice.width;
                line.x = line_left(style, choice.width);
                line.baseline = static_cast<double>(line_count) * style.line_box.height + style.line_box.baseline;
                lines[line_count] = line;
            }
        }
        ++line_count;
        begin = choice.next;
    }

    if constexpr (Output == LineOutput::records) {
        if (line_count > lines.size()) {
            const ParagraphStorageRequirement required{bound.per_byte, bound.collapsed_bytes, line_count};
            return ParagraphLayoutResult::storage_too_small(required);
        }
    }
    metrics.line_count = line_count;
    metrics.height = static_cast<double>(line_count) * style.line_box.height;
    metrics.widest_line = widest;
    metrics.status = pass.status;
    metrics.shaping_calls = pass.shaping_calls;
    metrics.shaped_bytes = pass.shaped_bytes;
    return ParagraphLayoutResult::laid_out(metrics);
}

} // namespace

ParagraphLayoutResult::ParagraphLayoutResult(
    std::variant<ParagraphMetrics, ParagraphStorageRequirement, ParagraphRequestError> state) noexcept
    : state_(std::move(state)) {}

ParagraphLayoutResult ParagraphLayoutResult::laid_out(const ParagraphMetrics& metrics) noexcept {
    const ParagraphLayoutResult result(
        std::variant<ParagraphMetrics, ParagraphStorageRequirement, ParagraphRequestError>(
            std::in_place_type<ParagraphMetrics>, metrics));
    return result;
}

ParagraphLayoutResult ParagraphLayoutResult::storage_too_small(const ParagraphStorageRequirement& required) noexcept {
    const ParagraphLayoutResult result(
        std::variant<ParagraphMetrics, ParagraphStorageRequirement, ParagraphRequestError>(
            std::in_place_type<ParagraphStorageRequirement>, required));
    return result;
}

ParagraphLayoutResult ParagraphLayoutResult::refused(ParagraphRequestError error) noexcept {
    const ParagraphLayoutResult result(
        std::variant<ParagraphMetrics, ParagraphStorageRequirement, ParagraphRequestError>(
            std::in_place_type<ParagraphRequestError>, error));
    return result;
}

bool ParagraphLayoutResult::succeeded() const noexcept {
    return std::holds_alternative<ParagraphMetrics>(state_);
}

const ParagraphMetrics* ParagraphLayoutResult::metrics() const noexcept {
    return std::get_if<ParagraphMetrics>(&state_);
}

const ParagraphStorageRequirement* ParagraphLayoutResult::required_storage() const noexcept {
    return std::get_if<ParagraphStorageRequirement>(&state_);
}

std::optional<ParagraphRequestError> ParagraphLayoutResult::error() const noexcept {
    const ParagraphRequestError* const refused_error = std::get_if<ParagraphRequestError>(&state_);
    if (refused_error == nullptr) return std::nullopt;
    return *refused_error;
}

LineBoxMetrics paragraph_line_box(TextMetricsProvider* provider, FontSpec font, std::optional<double> line_height) {
    // The primary face's metrics do not depend on the text, so a space
    // resolves them (TextBlock::line_metrics).
    ResolvedTextLayout primary{};
    if (provider != nullptr) {
        primary = (*provider).resolve_text_layout_utf8(" ", font);
    } else {
        primary = estimate_text_layout_utf8(" ", font);
    }
    const LineBoxMetrics metrics = line_box_metrics(primary, line_height);
    return metrics;
}

ParagraphStorageRequirement paragraph_storage_bound(std::string_view text, ParagraphWhiteSpace white_space) noexcept {
    ParagraphStorageRequirement required{};
    required.per_byte = text.size();
    if (white_space == ParagraphWhiteSpace::collapse) {
        required.per_byte = collapsed_size(text);
        required.collapsed_bytes = required.per_byte;
    }
    required.lines = required.per_byte;
    return required;
}

ParagraphLayoutResult lay_out_paragraph(TextMetricsProvider* provider, std::string_view text,
                                        const ParagraphStyle& style, const ParagraphStorage& storage) {
    const ParagraphLayoutResult result = lay_out<LineOutput::records>(provider, text, style, storage);
    return result;
}

ParagraphLayoutResult measure_paragraph(TextMetricsProvider* provider, std::string_view text,
                                        const ParagraphStyle& style, const ParagraphStorage& storage) {
    const ParagraphLayoutResult result = lay_out<LineOutput::count_only>(provider, text, style, storage);
    return result;
}

std::size_t paint_paragraph(Painter& painter, std::string_view text, std::span<const ParagraphLine> lines,
                            const ParagraphStyle& style, const ParagraphPaint& paint) {
    const double line_height = style.line_box.height;
    if (lines.empty() || !std::isfinite(line_height) || !(line_height > 0.0)) return 0U;
    // Lines are line_height apart from the box's top, so the visible ones
    // are an index interval: no line outside it is visited.
    const double top = paint.visible.y - paint.origin.y;
    const double bottom = top + paint.visible.height;
    const double first_row = std::floor(top / line_height);
    const double end_row = std::ceil(bottom / line_height);
    const double line_total = static_cast<double>(lines.size());
    if (!std::isfinite(first_row) || !std::isfinite(end_row) || end_row <= 0.0 || first_row >= line_total) return 0U;
    const std::size_t first = first_row > 0.0 ? static_cast<std::size_t>(first_row) : 0U;
    const std::size_t end = end_row < line_total ? static_cast<std::size_t>(end_row) : lines.size();
    std::size_t drawn = 0U;
    for (std::size_t index = first; index < end; ++index) {
        const ParagraphLine& line = lines[index];
        const std::size_t line_begin = line.bytes.start.value();
        const std::size_t line_end = line.bytes.end.value();
        if (line_begin > line_end || line_end > text.size()) break;
        if (line_begin == line_end) continue;
        const std::string_view run = text.substr(line_begin, line_end - line_begin);
        const double baseline = snap_text_baseline(paint.origin.y + line.baseline, paint.device_scale);
        const Point origin{paint.origin.x + line.x, baseline};
        painter.draw_text_utf8(origin, run, style.font, paint.color);
        ++drawn;
    }
    return drawn;
}

void ParagraphBuffers::reserve(std::size_t per_byte, std::size_t lines) {
    if (lines_.size() < lines) lines_.resize(lines);
    if (collapsed_text_.size() < per_byte) collapsed_text_.resize(per_byte);
    if (opportunities_.size() < per_byte) opportunities_.resize(per_byte, LineBreakOpportunity::prohibited);
    if (advances_.size() < per_byte) advances_.resize(per_byte, 0.0);
}

void ParagraphBuffers::reserve(const ParagraphStorageRequirement& required) {
    reserve(required.per_byte, required.lines);
}

void ParagraphBuffers::release() noexcept {
    lines_ = std::vector<ParagraphLine>{};
    collapsed_text_ = std::vector<char>{};
    opportunities_ = std::vector<LineBreakOpportunity>{};
    advances_ = std::vector<double>{};
}

ParagraphStorage ParagraphBuffers::storage() noexcept {
    ParagraphStorage view{};
    view.lines = std::span<ParagraphLine>(lines_);
    view.collapsed_text = std::span<char>(collapsed_text_);
    view.opportunities = std::span<LineBreakOpportunity>(opportunities_);
    view.advances = std::span<double>(advances_);
    return view;
}

std::span<const ParagraphLine> ParagraphBuffers::lines(std::size_t line_count) const noexcept {
    const std::size_t count = std::min(line_count, lines_.size());
    const std::span<const ParagraphLine> written(lines_.data(), count);
    return written;
}

std::string_view ParagraphBuffers::collapsed_text(std::size_t laid_out_bytes) const noexcept {
    const std::size_t count = std::min(laid_out_bytes, collapsed_text_.size());
    const std::string_view written(collapsed_text_.data(), count);
    return written;
}

} // namespace gui_forms
