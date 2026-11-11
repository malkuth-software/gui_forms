#include "gui_forms/text/markdown/rtf_conversion.hpp"

#include <stx_rtf2md/markdown.hpp>
#include <stx_rtf2md/rtf.hpp>

#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace gui_forms {

namespace rtf2md = stx_rtf2md;

std::uint32_t MarkdownLossReport::count(MarkdownLoss kind) const noexcept {
    const std::size_t index = static_cast<std::size_t>(kind);
    if (index >= counts_.size()) return 0U;
    return counts_[index];
}

bool MarkdownLossReport::empty() const noexcept {
    for (const std::uint32_t count : counts_) {
        if (count != 0U) return false;
    }
    return true;
}

void MarkdownLossReport::add(MarkdownLoss kind, std::uint32_t count) noexcept {
    const std::size_t index = static_cast<std::size_t>(kind);
    if (index >= counts_.size()) return;
    const std::uint32_t room = std::numeric_limits<std::uint32_t>::max() - counts_[index];
    counts_[index] += count > room ? room : count;
}

void MarkdownLossReport::clear() noexcept {
    counts_.fill(0U);
}

RtfConversionResult::RtfConversionResult(State state) noexcept : state_(std::move(state)) {}

RtfConversionResult RtfConversionResult::imported(const MarkdownLossReport& losses, RtfBodySize body) noexcept {
    const Imported imported_state{losses, body};
    const RtfConversionResult result{State(imported_state)};
    return result;
}

RtfConversionResult RtfConversionResult::exported(const MarkdownLossReport& losses) noexcept {
    const Exported exported_state{losses};
    const RtfConversionResult result{State(exported_state)};
    return result;
}

RtfConversionResult RtfConversionResult::failed(RtfConversionError error, std::uint64_t offset) noexcept {
    const Failed failure{error, offset};
    const RtfConversionResult result{State(failure)};
    return result;
}

bool RtfConversionResult::succeeded() const noexcept {
    const bool failed_state = std::holds_alternative<Failed>(state_);
    return !failed_state;
}

std::optional<MarkdownLossReport> RtfConversionResult::losses() const noexcept {
    const Imported* imported_state = std::get_if<Imported>(&state_);
    if (imported_state != nullptr) return (*imported_state).losses;
    const Exported* exported_state = std::get_if<Exported>(&state_);
    if (exported_state != nullptr) return (*exported_state).losses;
    return std::nullopt;
}

std::optional<RtfBodySize> RtfConversionResult::body() const noexcept {
    const Imported* imported_state = std::get_if<Imported>(&state_);
    if (imported_state == nullptr) return std::nullopt;
    return (*imported_state).body;
}

std::optional<RtfConversionError> RtfConversionResult::error() const noexcept {
    const Failed* failure = std::get_if<Failed>(&state_);
    if (failure == nullptr) return std::nullopt;
    return (*failure).error;
}

std::uint64_t RtfConversionResult::offset() const noexcept {
    const Failed* failure = std::get_if<Failed>(&state_);
    if (failure == nullptr) return 0U;
    return (*failure).offset;
}

namespace {

// One loss kind: stx_rtf2md's, ours, and the sentence parts for one and
// for several.
struct LossEntry final {
    rtf2md::LossKind source_kind{};
    MarkdownLoss kind{};
    const char* singular{};
    const char* plural{};
};

constexpr std::array<LossEntry, markdown_loss_kind_count> loss_table{{
    {rtf2md::LossKind::destination_skipped, MarkdownLoss::destination_skipped,
     "header, footer, note or hidden part was skipped", "headers, footers, notes or hidden parts were skipped"},
    {rtf2md::LossKind::picture_skipped, MarkdownLoss::picture_skipped, "picture was skipped",
     "pictures were skipped"},
    {rtf2md::LossKind::object_skipped, MarkdownLoss::object_skipped, "embedded object was skipped",
     "embedded objects were skipped"},
    {rtf2md::LossKind::control_word_ignored, MarkdownLoss::control_word_ignored,
     "unsupported formatting instruction was ignored", "unsupported formatting instructions were ignored"},
    {rtf2md::LossKind::field_result_kept, MarkdownLoss::field_result_kept, "field kept only its displayed text",
     "fields kept only their displayed text"},
    {rtf2md::LossKind::table_flattened, MarkdownLoss::table_flattened, "table became paragraphs",
     "tables became paragraphs"},
    {rtf2md::LossKind::justify_read_as_left, MarkdownLoss::justify_read_as_left,
     "justified paragraph became left-aligned", "justified paragraphs became left-aligned"},
    {rtf2md::LossKind::underline_style_collapsed, MarkdownLoss::underline_style_collapsed,
     "underline style became a plain underline", "underline styles became a plain underline"},
    {rtf2md::LossKind::font_dropped, MarkdownLoss::font_dropped, "font choice was not kept",
     "font choices were not kept"},
    {rtf2md::LossKind::size_quantised, MarkdownLoss::size_quantised, "text size was rounded to the nearest step",
     "text sizes were rounded to the nearest step"},
    {rtf2md::LossKind::indent_dropped, MarkdownLoss::indent_dropped, "paragraph indent was not kept",
     "paragraph indents were not kept"},
    {rtf2md::LossKind::spacing_dropped, MarkdownLoss::spacing_dropped,
     "paragraph spacing or empty paragraph was not kept", "paragraph spacings or empty paragraphs were not kept"},
    {rtf2md::LossKind::control_character_dropped, MarkdownLoss::control_character_dropped,
     "control character was removed", "control characters were removed"},
    {rtf2md::LossKind::html_kept_as_text, MarkdownLoss::html_kept_as_text, "piece of HTML was kept as text",
     "pieces of HTML were kept as text"},
    {rtf2md::LossKind::image_kept_as_text, MarkdownLoss::image_kept_as_text, "image was kept as text",
     "images were kept as text"},
    {rtf2md::LossKind::reference_link_kept_as_text, MarkdownLoss::reference_link_kept_as_text,
     "reference link was kept as text", "reference links were kept as text"},
    {rtf2md::LossKind::table_kept_as_text, MarkdownLoss::table_kept_as_text, "table was kept as text",
     "tables were kept as text"},
    {rtf2md::LossKind::task_marker_kept_as_text, MarkdownLoss::task_marker_kept_as_text,
     "task marker was kept as text", "task markers were kept as text"},
    {rtf2md::LossKind::footnote_kept_as_text, MarkdownLoss::footnote_kept_as_text,
     "footnote reference was kept as text", "footnote references were kept as text"},
    {rtf2md::LossKind::entity_kept_as_text, MarkdownLoss::entity_kept_as_text,
     "character reference was kept as text", "character references were kept as text"},
    {rtf2md::LossKind::thematic_break_kept_as_text, MarkdownLoss::thematic_break_kept_as_text,
     "horizontal rule was kept as text", "horizontal rules were kept as text"},
    {rtf2md::LossKind::structure_flattened, MarkdownLoss::structure_flattened,
     "nested block was moved out of its list item", "nested blocks were moved out of their list items"},
    {rtf2md::LossKind::empty_block_dropped, MarkdownLoss::empty_block_dropped, "empty block was removed",
     "empty blocks were removed"},
    {rtf2md::LossKind::code_language_dropped, MarkdownLoss::code_language_dropped,
     "code block's language was not kept", "code blocks' languages were not kept"},
    {rtf2md::LossKind::link_title_dropped, MarkdownLoss::link_title_dropped, "link title was not kept",
     "link titles were not kept"},
}};

static_assert(markdown_loss_kind_count == rtf2md::loss_kind_count, "every stx_rtf2md loss kind has an entry");

void read_losses(const rtf2md::LossReport& report, MarkdownLossReport& losses) noexcept {
    losses.clear();
    for (const LossEntry& entry : loss_table) losses.add(entry.kind, report.entry(entry.source_kind).count);
}

// The conversion's error for a library status (audit N114: never throws).
// ok, output_too_small and invalid_document are not failures a caller can
// meet (ok is success, the writers are retried at the size they ask for,
// and the readers only produce documents in normal form); should the
// library report one anyway, or a status outside the enumeration, the
// conversion fails as malformed rather than raising.
[[nodiscard]] RtfConversionError error_of(rtf2md::Status status) noexcept {
    switch (status) {
    case rtf2md::Status::malformed: return RtfConversionError::malformed;
    case rtf2md::Status::limit_exceeded: return RtfConversionError::limit_exceeded;
    case rtf2md::Status::unsupported_encoding: return RtfConversionError::unsupported_encoding;
    case rtf2md::Status::effort_exceeded: return RtfConversionError::effort_exceeded;
    case rtf2md::Status::ok:
    case rtf2md::Status::output_too_small:
    case rtf2md::Status::invalid_document:
        return RtfConversionError::malformed;
    }
    return RtfConversionError::malformed;
}

[[nodiscard]] RtfBodyRule body_rule_of(rtf2md::BodyRule rule) noexcept {
    switch (rule) {
    case rtf2md::BodyRule::none: return RtfBodyRule::none;
    case rtf2md::BodyRule::default_character_format: return RtfBodyRule::default_character_format;
    case rtf2md::BodyRule::normal_style: return RtfBodyRule::normal_style;
    case rtf2md::BodyRule::most_common: return RtfBodyRule::most_common;
    }
    // A rule outside the enumeration: the 12-point default's.
    return RtfBodyRule::none;
}

} // namespace

std::string describe_loss(MarkdownLoss kind, std::uint32_t count) {
    std::string sentence{};
    if (count == 0U) return sentence;
    for (const LossEntry& entry : loss_table) {
        if (entry.kind != kind) continue;
        sentence = std::to_string(count);
        sentence.push_back(' ');
        sentence.append(count == 1U ? entry.singular : entry.plural);
        sentence.push_back('.');
        return sentence;
    }
    return sentence;
}

bool looks_like_rtf(std::span<const std::byte> bytes) noexcept {
    constexpr std::string_view signature = "{\\rtf";
    if (bytes.size() < signature.size()) return false;
    for (std::size_t index = 0U; index < signature.size(); ++index) {
        if (static_cast<char>(bytes[index]) != signature[index]) return false;
    }
    return true;
}

// The whole conversion, reader included, runs inside the try (audit
// N114): an allocation failure while reading pasted RTF fails the
// conversion as the same failure in the writer does, with no exception.
RtfConversionResult import_rtf(std::span<const std::byte> rtf, std::string& markdown) {
    markdown.clear();
    try {
        const rtf2md::Limits limits{};
        rtf2md::RtfReader reader(limits);
        rtf2md::Document document;
        rtf2md::LossReport loss;
        const rtf2md::Result read = reader.read(rtf, document, loss);
        if (read.status != rtf2md::Status::ok) {
            return RtfConversionResult::failed(error_of(read.status), read.stop.value);
        }
        MarkdownLossReport losses;
        read_losses(loss, losses);
        const rtf2md::BodySize chosen = reader.last_body();
        const RtfBodySize body{chosen.half_points, body_rule_of(chosen.rule)};
        rtf2md::MarkdownWriter writer(limits);
        // The writer states the exact size it needs when the first guess is
        // short.
        markdown.resize(document.text.size() * 2U + 64U);
        rtf2md::Result written = writer.write(document, std::span<char>(markdown.data(), markdown.size()));
        if (written.status == rtf2md::Status::output_too_small) {
            markdown.resize(written.required);
            written = writer.write(document, std::span<char>(markdown.data(), markdown.size()));
        }
        if (written.status != rtf2md::Status::ok) {
            markdown.clear();
            return RtfConversionResult::failed(error_of(written.status), 0U);
        }
        markdown.resize(written.produced);
        return RtfConversionResult::imported(losses, body);
    } catch (const std::bad_alloc&) {
        markdown.clear();
        return RtfConversionResult::failed(RtfConversionError::limit_exceeded, 0U);
    } catch (const std::length_error&) {
        markdown.clear();
        return RtfConversionResult::failed(RtfConversionError::limit_exceeded, 0U);
    }
}

// As import_rtf: the reader runs inside the try too (N114).
RtfConversionResult export_rtf(std::string_view markdown, std::vector<std::byte>& rtf) {
    rtf.clear();
    try {
        const rtf2md::Limits limits{};
        rtf2md::MarkdownReader reader(limits);
        rtf2md::Document document;
        rtf2md::LossReport loss;
        const std::span<const char> input(markdown.data(), markdown.size());
        const rtf2md::Result read = reader.read(input, document, loss);
        if (read.status != rtf2md::Status::ok) {
            return RtfConversionResult::failed(error_of(read.status), read.stop.value);
        }
        MarkdownLossReport losses;
        read_losses(loss, losses);
        rtf2md::RtfWriter writer(limits);
        rtf.resize(document.text.size() * 2U + 1024U);
        rtf2md::Result written = writer.write(document, std::span<std::byte>(rtf.data(), rtf.size()));
        if (written.status == rtf2md::Status::output_too_small) {
            rtf.resize(written.required);
            written = writer.write(document, std::span<std::byte>(rtf.data(), rtf.size()));
        }
        if (written.status != rtf2md::Status::ok) {
            rtf.clear();
            return RtfConversionResult::failed(error_of(written.status), 0U);
        }
        rtf.resize(written.produced);
        return RtfConversionResult::exported(losses);
    } catch (const std::bad_alloc&) {
        rtf.clear();
        return RtfConversionResult::failed(RtfConversionError::limit_exceeded, 0U);
    } catch (const std::length_error&) {
        rtf.clear();
        return RtfConversionResult::failed(RtfConversionError::limit_exceeded, 0U);
    }
}

} // namespace gui_forms
