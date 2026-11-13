#pragma once

#include "gui_forms/export/application_export.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace gui_forms {

// RTF in and out of the markdown dialect (markdown.hpp), through
// stx_rtf2md. import_rtf reads the RTF 1.9.1 subset into a document and
// writes it as the dialect's canonical text; export_rtf reads the dialect
// with stx_rtf2md's normative reader and writes canonical RTF, which
// import_rtf reads back to the same document. Each direction reports what
// the other format could not hold.

// What a conversion could not keep, one kind per reason. The first group
// is RTF that the dialect cannot say; the second is markdown outside the
// dialect, which is kept as text.
enum class MarkdownLoss : std::uint8_t {
    destination_skipped,
    picture_skipped,
    object_skipped,
    control_word_ignored,
    field_result_kept,
    table_flattened,
    justify_read_as_left,
    underline_style_collapsed,
    font_dropped,
    size_quantised,
    indent_dropped,
    spacing_dropped,
    control_character_dropped,
    html_kept_as_text,
    image_kept_as_text,
    reference_link_kept_as_text,
    table_kept_as_text,
    task_marker_kept_as_text,
    footnote_kept_as_text,
    entity_kept_as_text,
    thematic_break_kept_as_text,
    structure_flattened,
    empty_block_dropped,
    code_language_dropped,
    link_title_dropped,
};

inline constexpr std::size_t markdown_loss_kind_count = 25U;

// How many times a conversion met each kind of loss.
class GUI_FORMS_API MarkdownLossReport final {
public:
    [[nodiscard]] std::uint32_t count(MarkdownLoss kind) const noexcept;
    // Whether nothing was lost.
    [[nodiscard]] bool empty() const noexcept;
    // Adds count losses of kind, saturating at the largest count.
    void add(MarkdownLoss kind, std::uint32_t count) noexcept;
    void clear() noexcept;

private:
    std::array<std::uint32_t, markdown_loss_kind_count> counts_{};
};

// One sentence for a reader saying what count losses of kind were, for
// example "2 pictures were skipped."; empty for a count of 0.
[[nodiscard]] GUI_FORMS_API std::string describe_loss(MarkdownLoss kind, std::uint32_t count);

// Which rule chose an imported document's body size: no sized text (the
// 12-point default), the document's default character format, the Normal
// style, or the size of most of the text. Size steps are relative to it.
enum class RtfBodyRule : std::uint8_t { none, default_character_format, normal_style, most_common };

struct GUI_FORMS_API RtfBodySize final {
    std::int32_t half_points{24};
    RtfBodyRule rule{RtfBodyRule::none};
};

// Why a conversion failed: input that is not readable (malformed, with the
// offset where reading stopped), RTF in a code page other than 1252, or an
// input past the converter's size or effort bounds.
enum class RtfConversionError : std::uint8_t { malformed, unsupported_encoding, limit_exceeded, effort_exceeded };

// The result of import_rtf or export_rtf: imported, with its losses and the
// body size the sizes are relative to; exported, with its losses; or failed
// with an error and the input offset where reading stopped.
class GUI_FORMS_API RtfConversionResult final {
public:
    [[nodiscard]] static RtfConversionResult imported(const MarkdownLossReport& losses, RtfBodySize body) noexcept;
    [[nodiscard]] static RtfConversionResult exported(const MarkdownLossReport& losses) noexcept;
    [[nodiscard]] static RtfConversionResult failed(RtfConversionError error, std::uint64_t offset) noexcept;

    [[nodiscard]] bool succeeded() const noexcept;
    // The losses of a conversion that succeeded; none for a failure.
    [[nodiscard]] std::optional<MarkdownLossReport> losses() const noexcept;
    // An import's body size; none for an export or a failure.
    [[nodiscard]] std::optional<RtfBodySize> body() const noexcept;
    // The error of a conversion that failed; none when it succeeded.
    [[nodiscard]] std::optional<RtfConversionError> error() const noexcept;
    // Where a failed conversion stopped reading; 0 when it succeeded.
    [[nodiscard]] std::uint64_t offset() const noexcept;

private:
    struct Imported final {
        MarkdownLossReport losses{};
        RtfBodySize body{};
    };
    struct Exported final {
        MarkdownLossReport losses{};
    };
    struct Failed final {
        RtfConversionError error{RtfConversionError::malformed};
        std::uint64_t offset{};
    };
    using State = std::variant<Imported, Exported, Failed>;
    explicit RtfConversionResult(State state) noexcept;

    State state_;
};

// Whether bytes start as RTF does ("{\rtf"). The extension alone does not
// make a file RTF.
[[nodiscard]] GUI_FORMS_API bool looks_like_rtf(std::span<const std::byte> bytes) noexcept;

// Reads rtf and writes the document as the dialect's canonical markdown
// into markdown, which is replaced. rtf is read only; the two may not
// share storage. On failure markdown is empty.
[[nodiscard]] GUI_FORMS_API RtfConversionResult import_rtf(std::span<const std::byte> rtf, std::string& markdown);

// Reads markdown in the dialect and writes it as canonical RTF into rtf,
// which is replaced. markdown is read only; the two may not share storage.
// On failure rtf is empty.
[[nodiscard]] GUI_FORMS_API RtfConversionResult export_rtf(std::string_view markdown, std::vector<std::byte>& rtf);

} // namespace gui_forms
