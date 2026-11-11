#pragma once

#include "gui_forms/text/types/text_types.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace gui_forms {

// Renderer- and platform-neutral mutable Unicode storage. Typed positions keep
// callers independent from the current contiguous UTF-8 representation.
class TextStore final {
public:
    explicit TextStore(std::string_view text = {}, TextStoreLimits limits = {});

    [[nodiscard]] std::string_view utf8() const& noexcept { return text_; }
    std::string_view utf8() const&& = delete;
    [[nodiscard]] Utf8Offset utf8_size() const noexcept {
        return Utf8Offset(text_.size());
    }
    [[nodiscard]] Utf16Offset utf16_size() const noexcept {
        return Utf16Offset(utf16_units_);
    }
    [[nodiscard]] ScalarIndex scalar_count() const noexcept {
        return ScalarIndex(scalar_count_);
    }
    [[nodiscard]] GraphemeIndex grapheme_count() const noexcept {
        return GraphemeIndex(grapheme_boundaries_.size() - 1U);
    }
    [[nodiscard]] std::size_t line_count() const noexcept {
        return line_starts_.size();
    }
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
    [[nodiscard]] const TextStoreLimits& limits() const noexcept {
        return limits_;
    }
    [[nodiscard]] std::span<const TextStyleSpan> style_spans() const& noexcept {
        return style_spans_;
    }
    std::span<const TextStyleSpan> style_spans() const&& = delete;

    void set_text(std::string_view text);
    [[nodiscard]] TextEditResult replace(
        Utf8Range range, std::string_view replacement,
        std::optional<TextStyleId> inserted_style = std::nullopt);
    void set_style_spans(std::span<const TextStyleSpan> spans);
    [[nodiscard]] std::optional<TextStyleId> style_at(
        Utf8Offset position) const;
    [[nodiscard]] bool is_scalar_boundary(Utf8Offset position) const noexcept;
    [[nodiscard]] bool is_grapheme_boundary(Utf8Offset position) const noexcept;
    [[nodiscard]] Utf8Offset utf8_offset(Utf16Offset position) const;
    [[nodiscard]] Utf8Offset utf8_offset(ScalarIndex position) const;
    [[nodiscard]] Utf8Offset utf8_offset(GraphemeIndex position) const;
    [[nodiscard]] Utf16Offset utf16_offset(Utf8Offset position) const;
    [[nodiscard]] ScalarIndex scalar_index(Utf8Offset position) const;
    [[nodiscard]] GraphemeIndex grapheme_index(Utf8Offset position) const;
    [[nodiscard]] Utf8Offset next_scalar_boundary(Utf8Offset position) const;
    [[nodiscard]] Utf8Offset previous_scalar_boundary(Utf8Offset position) const;
    [[nodiscard]] Utf8Offset next_grapheme_boundary(Utf8Offset position) const;
    [[nodiscard]] Utf8Offset previous_grapheme_boundary(
        Utf8Offset position) const;
    [[nodiscard]] Utf8Range grapheme_range(GraphemeIndex grapheme) const;
    [[nodiscard]] char32_t scalar_at(Utf8Offset position) const;
    [[nodiscard]] Utf8Offset line_start(LineIndex line) const;
    [[nodiscard]] Utf8Range line_content_range(LineIndex line) const;
    [[nodiscard]] TextStoreSnapshot snapshot() const noexcept;

private:
    struct Metadata;
    [[nodiscard]] static Metadata analyze(std::string_view text);
    [[nodiscard]] std::vector<TextStyleSpan> normalize_style_spans(
        std::span<const TextStyleSpan> spans,
        std::string_view candidate_text) const;
    [[nodiscard]] std::vector<TextStyleSpan> transform_style_spans(
        Utf8Range removed, std::size_t inserted_bytes,
        std::optional<TextStyleId> inserted_style,
        std::string_view candidate_text) const;
    void validate_position(Utf8Offset position) const;
    void validate_range(Utf8Range range) const;
    [[noreturn]] void reject_invalid_utf8(
        const Utf8ValidationResult& validation);

    TextStoreLimits limits_;
    std::string text_;
    // Byte offsets into text_, ascending. Four bytes each: text_ never
    // exceeds maximum_offset_bytes (text_store.cpp), whatever the limits.
    std::vector<std::uint32_t> line_starts_;
    std::vector<std::uint32_t> line_content_ends_;
    std::vector<std::uint32_t> grapheme_boundaries_;
    std::vector<TextStyleSpan> style_spans_;
    std::size_t scalar_count_{};
    std::size_t utf16_units_{};
    std::uint64_t revision_{};
    std::uint64_t edit_count_{};
    std::uint64_t style_update_count_{};
    std::uint64_t metadata_rebuild_count_{};
    std::uint64_t rejected_mutation_count_{};
    mutable std::uint64_t rejected_position_query_count_{};
};

} // namespace gui_forms
