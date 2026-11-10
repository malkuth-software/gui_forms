#include "gui_forms/text.hpp"
#include "gui_forms/detail/algorithm/binary_search.hpp"

#include "../unicode/unicode_grapheme.hpp"
#include "../unicode/unicode_grapheme_data.hpp"

#include <algorithm>
#include <limits>
#include <span>
#include <stdexcept>
#include <utility>

namespace gui_forms {
namespace {

struct DecodedScalar final {
  char32_t value{};
  std::size_t bytes{};
};

struct Utf8OffsetBeforeStyleSpan final {
  [[nodiscard]] bool operator()(Utf8Offset value,
                                const TextStyleSpan &span) const noexcept {
    return value < span.range.start;
  }
};

[[nodiscard]] constexpr bool continuation(std::uint8_t byte) noexcept {
  return (byte & 0xc0U) == 0x80U;
}

[[nodiscard]] DecodedScalar decode_valid(std::string_view text,
                                         std::size_t offset) noexcept {
  const std::uint8_t first = static_cast<std::uint8_t>(text[offset]);
  if (first < 0x80U) {
    return {first, 1};
  }
  const std::uint8_t second = static_cast<std::uint8_t>(text[offset + 1U]);
  if (first < 0xe0U) {
    return {static_cast<char32_t>(((first & 0x1fU) << 6U) | (second & 0x3fU)),
            2};
  }
  const std::uint8_t third = static_cast<std::uint8_t>(text[offset + 2U]);
  if (first < 0xf0U) {
    return {static_cast<char32_t>(((first & 0x0fU) << 12U) |
                                  ((second & 0x3fU) << 6U) | (third & 0x3fU)),
            3};
  }
  const std::uint8_t fourth = static_cast<std::uint8_t>(text[offset + 3U]);
  return {static_cast<char32_t>(((first & 0x07U) << 18U) |
                                ((second & 0x3fU) << 12U) |
                                ((third & 0x3fU) << 6U) | (fourth & 0x3fU)),
          4};
}

[[nodiscard]] bool is_line_break(char32_t scalar) noexcept {
  return scalar == U'\n' || scalar == U'\r' || scalar == U'\u0085' ||
         scalar == U'\u2028' || scalar == U'\u2029';
}

[[nodiscard]] std::size_t checked_new_size(std::size_t current,
                                           std::size_t removed,
                                           std::size_t inserted) {
  if (removed > current || inserted > std::numeric_limits<std::size_t>::max() -
                                          (current - removed)) {
    throw std::length_error("GUI.Forms text edit size overflow");
  }
  return current - removed + inserted;
}

[[nodiscard]] bool style_span_less(const TextStyleSpan& left,
                                   const TextStyleSpan& right) noexcept {
  if (left.range.start != right.range.start) {
    return left.range.start < right.range.start;
  }
  if (left.range.end != right.range.end) {
    return left.range.end < right.range.end;
  }
  return left.style < right.style;
}

[[nodiscard]] bool utf8_boundary(std::string_view text,
                                 Utf8Offset position) noexcept {
  return position.value() == text.size() ||
         !continuation(static_cast<std::uint8_t>(text[position.value()]));
}

[[nodiscard]] std::size_t shifted_offset(bool grows, std::size_t magnitude,
                                         std::size_t value) noexcept {
  return grows ? value + magnitude : value - magnitude;
}

// The largest text a store holds, whatever its limits: its offsets are
// stored in 32 bits.
constexpr std::size_t maximum_offset_bytes =
    std::numeric_limits<std::uint32_t>::max();

// `relative`, each plus `base`, into `offsets` (replacing its contents).
// Every sum is at most maximum_offset_bytes.
void fill_offsets(std::vector<std::uint32_t> &offsets,
                  std::span<const std::size_t> relative, std::size_t base) {
  offsets.resize(relative.size());
  for (std::size_t index = 0; index < relative.size(); ++index) {
    offsets[index] = static_cast<std::uint32_t>(relative[index] + base);
  }
}

// One offset table's change for an edit (N50): entries [first, last) are
// replaced by `relative` (each plus `base`), and the entries after them,
// which lie past the edit, move by the edit's size change.
struct OffsetSplice final {
  std::size_t first{};
  std::size_t last{};
  std::span<const std::size_t> relative{};
  std::size_t base{};
};

[[nodiscard]] std::size_t spliced_size(const std::vector<std::uint32_t> &offsets,
                                       const OffsetSplice &splice) noexcept {
  return offsets.size() - (splice.last - splice.first) + splice.relative.size();
}

// Applies `splice` to `offsets`, whose capacity is at least
// spliced_size(offsets, splice), so that nothing is allocated and nothing
// throws. An entry past the edit is at least `removed` bytes beyond its
// start, so the subtraction never wraps.
void apply_offset_splice(std::vector<std::uint32_t> &offsets,
                         const OffsetSplice &splice, std::size_t removed,
                         std::size_t inserted) noexcept {
  const std::uint32_t removed_bytes = static_cast<std::uint32_t>(removed);
  const std::uint32_t inserted_bytes = static_cast<std::uint32_t>(inserted);
  for (std::size_t index = splice.last; index < offsets.size(); ++index) {
    offsets[index] = offsets[index] - removed_bytes + inserted_bytes;
  }
  const std::size_t replaced = splice.last - splice.first;
  const std::size_t wanted = splice.relative.size();
  const std::vector<std::uint32_t>::iterator gap =
      offsets.begin() + static_cast<std::ptrdiff_t>(splice.last);
  if (wanted > replaced) {
    offsets.insert(gap, wanted - replaced, 0U);
  } else if (wanted < replaced) {
    offsets.erase(gap - static_cast<std::ptrdiff_t>(replaced - wanted), gap);
  }
  for (std::size_t index = 0; index < wanted; ++index) {
    offsets[splice.first + index] =
        static_cast<std::uint32_t>(splice.relative[index] + splice.base);
  }
}

} // namespace

struct TextStore::Metadata final {
  std::size_t scalars{};
  std::size_t utf16_units{};
  std::vector<std::size_t> line_starts;
  std::vector<std::size_t> line_content_ends;
  std::vector<std::size_t> grapheme_boundaries;
};

Utf8ValidationResult validate_utf8(std::string_view text) noexcept {
  Utf8ValidationResult result;
  std::size_t offset = 0;
  while (offset < text.size()) {
    const std::uint8_t first = static_cast<std::uint8_t>(text[offset]);
    std::size_t length = 0;
    char32_t scalar = 0;
    if (first < 0x80U) {
      length = 1;
      scalar = first;
    } else if (first < 0xc0U) {
      result.error = Utf8ValidationError::unexpected_continuation;
    } else if (first < 0xc2U) {
      result.error = Utf8ValidationError::overlong_encoding;
    } else if (first < 0xe0U) {
      length = 2;
    } else if (first < 0xf0U) {
      length = 3;
    } else if (first < 0xf5U) {
      length = 4;
    } else {
      result.error = Utf8ValidationError::code_point_out_of_range;
    }

    if (result.error != Utf8ValidationError::none) {
      result.error_offset = Utf8Offset(offset);
      return result;
    }
    if (length > text.size() - offset) {
      result.error = Utf8ValidationError::truncated_sequence;
      result.error_offset = Utf8Offset(offset);
      return result;
    }
    for (std::size_t continuation_index = 1; continuation_index < length;
         ++continuation_index) {
      if (!continuation(
              static_cast<std::uint8_t>(text[offset + continuation_index]))) {
        result.error = Utf8ValidationError::invalid_continuation;
        result.error_offset = Utf8Offset(offset + continuation_index);
        return result;
      }
    }

    if (length > 1) {
      scalar = decode_valid(text, offset).value;
      const std::uint8_t second = static_cast<std::uint8_t>(text[offset + 1U]);
      if ((first == 0xe0U && second < 0xa0U) ||
          (first == 0xf0U && second < 0x90U)) {
        result.error = Utf8ValidationError::overlong_encoding;
      } else if (first == 0xedU && second >= 0xa0U) {
        result.error = Utf8ValidationError::surrogate_code_point;
      } else if (first == 0xf4U && second >= 0x90U) {
        result.error = Utf8ValidationError::code_point_out_of_range;
      }
      if (result.error != Utf8ValidationError::none) {
        result.error_offset = Utf8Offset(offset);
        return result;
      }
    }

    ++result.scalar_count;
    result.utf16_unit_count += scalar > 0xffffU ? 2U : 1U;
    offset += length;
  }
  result.error_offset = Utf8Offset(text.size());
  return result;
}

std::string_view to_string(Utf8ValidationError error) noexcept {
  switch (error) {
  case Utf8ValidationError::none:
    return "none";
  case Utf8ValidationError::unexpected_continuation:
    return "unexpected_continuation";
  case Utf8ValidationError::invalid_continuation:
    return "invalid_continuation";
  case Utf8ValidationError::truncated_sequence:
    return "truncated_sequence";
  case Utf8ValidationError::overlong_encoding:
    return "overlong_encoding";
  case Utf8ValidationError::surrogate_code_point:
    return "surrogate_code_point";
  case Utf8ValidationError::code_point_out_of_range:
    return "code_point_out_of_range";
  }
  return "unknown";
}

std::string_view grapheme_unicode_version() noexcept {
  return unicode_data::version;
}

TextStore::TextStore(std::string_view text, TextStoreLimits limits)
    : limits_(limits) {
  if (text.size() > limits_.maximum_utf8_bytes ||
      text.size() > maximum_offset_bytes) {
    throw std::length_error("GUI.Forms initial text exceeds its byte limit");
  }
  const Metadata metadata = analyze(text);
  text_.assign(text);
  scalar_count_ = metadata.scalars;
  utf16_units_ = metadata.utf16_units;
  fill_offsets(line_starts_, metadata.line_starts, 0U);
  fill_offsets(line_content_ends_, metadata.line_content_ends, 0U);
  fill_offsets(grapheme_boundaries_, metadata.grapheme_boundaries, 0U);
  metadata_rebuild_count_ = 1;
}

void TextStore::set_text(std::string_view text) {
  static_cast<void>(replace({Utf8Offset(0), utf8_size()}, text));
}

TextEditResult TextStore::replace(Utf8Range range, std::string_view replacement,
                                  std::optional<TextStyleId> inserted_style) {
  try {
    validate_range(range);
    const Utf8ValidationResult replacement_validation =
        validate_utf8(replacement);
    if (!replacement_validation.valid()) {
      reject_invalid_utf8(replacement_validation);
    }

    const std::size_t start = range.start.value();
    const std::size_t end = range.end.value();
    const std::size_t removed_bytes = end - start;
    const std::size_t next_size =
        checked_new_size(text_.size(), removed_bytes, replacement.size());
    if (next_size > limits_.maximum_utf8_bytes ||
        next_size > maximum_offset_bytes) {
      throw std::length_error("GUI.Forms text edit exceeds its byte limit");
    }
    if (removed_bytes == 0 && replacement.empty() && !inserted_style) {
      return {range, range, 0, 0, revision_, false};
    }

    const Utf8ValidationResult removed_validation =
        validate_utf8(std::string_view(text_).substr(start, removed_bytes));

    // The bytes the analysis must see again (N50): from the start of the
    // line holding the byte before the edit to the first line start after
    // it. A line break ends every grapheme cluster and every line, and the
    // break before the region and the one ending it lie outside the edit
    // (a CR before the region has the region's first byte after it, which
    // the edit leaves alone), so the metadata outside the region stays.
    const std::size_t old_size = text_.size();
    std::size_t region_start = 0U;
    if (start > 0U) {
      const std::size_t line = detail::upper_bound_index(
          std::span<const std::uint32_t>(line_starts_), start - 1U);
      region_start = line_starts_[line - 1U];
    }
    const std::size_t next_line = detail::upper_bound_index(
        std::span<const std::uint32_t>(line_starts_), end);
    const std::size_t region_old_end =
        next_line == line_starts_.size() ? old_size : line_starts_[next_line];
    const bool region_reaches_end = region_old_end == old_size;
    const std::size_t region_new_end =
        region_old_end - removed_bytes + replacement.size();

    // The edit is made in place within capacity reserved first, so that
    // undoing it on a failure below allocates nothing.
    const std::string removed_text(text_, start, removed_bytes);
    text_.reserve(std::max(old_size, next_size));
    text_.replace(start, removed_bytes, replacement);
    bool styles_changed = false;
    try {
      std::vector<TextStyleSpan> transformed = transform_style_spans(
          range, replacement.size(), inserted_style, text_);
      if (transformed.size() > limits_.maximum_style_spans) {
        throw std::length_error(
            "GUI.Forms text edit exceeds its style-span limit");
      }
      const Metadata region = analyze(std::string_view(text_).substr(
          region_start, region_new_end - region_start));
      // analyze() closes its last line at the region's end; that is a
      // line's content end only where the region ends the text.
      std::span<const std::size_t> region_content_ends = region.line_content_ends;
      if (!region_reaches_end) {
        region_content_ends = region_content_ends.first(region_content_ends.size() - 1U);
      }
      const std::span<const std::uint32_t> old_starts(line_starts_);
      const std::span<const std::uint32_t> old_ends(line_content_ends_);
      const std::span<const std::uint32_t> old_boundaries(grapheme_boundaries_);
      const OffsetSplice starts{
          detail::lower_bound_index(old_starts, region_start),
          detail::upper_bound_index(old_starts, region_old_end),
          region.line_starts, region_start};
      const OffsetSplice ends{
          detail::lower_bound_index(old_ends, region_start),
          region_reaches_end ? old_ends.size()
                             : detail::lower_bound_index(old_ends, region_old_end),
          region_content_ends, region_start};
      const OffsetSplice boundaries{
          detail::lower_bound_index(old_boundaries, region_start),
          detail::upper_bound_index(old_boundaries, region_old_end),
          region.grapheme_boundaries, region_start};
      line_starts_.reserve(spliced_size(line_starts_, starts));
      line_content_ends_.reserve(spliced_size(line_content_ends_, ends));
      grapheme_boundaries_.reserve(spliced_size(grapheme_boundaries_, boundaries));

      // Nothing below throws.
      apply_offset_splice(line_starts_, starts, removed_bytes, replacement.size());
      apply_offset_splice(line_content_ends_, ends, removed_bytes, replacement.size());
      apply_offset_splice(grapheme_boundaries_, boundaries, removed_bytes, replacement.size());
      styles_changed = transformed != style_spans_;
      style_spans_ = std::move(transformed);
    } catch (...) {
      // Within the reserved capacity: restores the text without allocating.
      text_.replace(start, replacement.size(), removed_text);
      throw;
    }
    scalar_count_ = scalar_count_ - removed_validation.scalar_count +
                    replacement_validation.scalar_count;
    utf16_units_ = utf16_units_ - removed_validation.utf16_unit_count +
                   replacement_validation.utf16_unit_count;
    ++revision_;
    ++edit_count_;
    ++metadata_rebuild_count_;
    if (styles_changed) {
      ++style_update_count_;
    }
    return {
        range,
        {range.start, Utf8Offset(start + replacement.size())},
        removed_validation.scalar_count,
        replacement_validation.scalar_count,
        revision_,
        true,
    };
  } catch (const std::invalid_argument &) {
    ++rejected_mutation_count_;
    throw;
  } catch (const std::out_of_range &) {
    ++rejected_mutation_count_;
    throw;
  } catch (const std::length_error &) {
    ++rejected_mutation_count_;
    throw;
  }
}

void TextStore::set_style_spans(std::span<const TextStyleSpan> spans) {
  try {
    std::vector<TextStyleSpan> normalized = normalize_style_spans(spans, text_);
    if (normalized == style_spans_) {
      return;
    }
    style_spans_ = std::move(normalized);
    ++revision_;
    ++style_update_count_;
  } catch (const std::invalid_argument &) {
    ++rejected_mutation_count_;
    throw;
  } catch (const std::out_of_range &) {
    ++rejected_mutation_count_;
    throw;
  } catch (const std::length_error &) {
    ++rejected_mutation_count_;
    throw;
  }
}

std::optional<TextStyleId> TextStore::style_at(Utf8Offset position) const {
  validate_position(position);
  if (position == utf8_size()) {
    return std::nullopt;
  }
  const std::size_t candidate_position = detail::upper_bound_index(
      std::span<const TextStyleSpan>(style_spans_), position,
      Utf8OffsetBeforeStyleSpan{});
  if (candidate_position == 0U) {
    return std::nullopt;
  }
  const TextStyleSpan &candidate = style_spans_[candidate_position - 1U];
  return position < candidate.range.end ? std::optional(candidate.style)
                                        : std::nullopt;
}

bool TextStore::is_scalar_boundary(Utf8Offset position) const noexcept {
  if (position.value() > text_.size()) {
    return false;
  }
  return position.value() == text_.size() ||
         !continuation(static_cast<std::uint8_t>(text_[position.value()]));
}

bool TextStore::is_grapheme_boundary(Utf8Offset position) const noexcept {
  return detail::binary_search_contains(
      std::span<const std::uint32_t>(grapheme_boundaries_), position.value());
}

Utf8Offset TextStore::utf8_offset(Utf16Offset position) const {
  if (position.value() > utf16_units_) {
    ++rejected_position_query_count_;
    throw std::out_of_range("GUI.Forms UTF-16 position exceeds text length");
  }
  std::size_t offset = 0;
  std::size_t units = 0;
  while (offset < text_.size() && units < position.value()) {
    const DecodedScalar decoded = decode_valid(text_, offset);
    const std::size_t next_units = units + (decoded.value > 0xffffU ? 2U : 1U);
    if (position.value() < next_units) {
      ++rejected_position_query_count_;
      throw std::invalid_argument(
          "GUI.Forms UTF-16 position splits a surrogate pair");
    }
    units = next_units;
    offset += decoded.bytes;
  }
  return Utf8Offset(offset);
}

Utf8Offset TextStore::utf8_offset(ScalarIndex position) const {
  if (position.value() > scalar_count_) {
    ++rejected_position_query_count_;
    throw std::out_of_range("GUI.Forms scalar position exceeds text length");
  }
  std::size_t offset = 0;
  for (std::size_t scalar = 0; scalar < position.value(); ++scalar) {
    offset += decode_valid(text_, offset).bytes;
  }
  return Utf8Offset(offset);
}

Utf8Offset TextStore::utf8_offset(GraphemeIndex position) const {
  if (position.value() >= grapheme_boundaries_.size()) {
    ++rejected_position_query_count_;
    throw std::out_of_range("GUI.Forms grapheme position exceeds text length");
  }
  return Utf8Offset(grapheme_boundaries_[position.value()]);
}

Utf16Offset TextStore::utf16_offset(Utf8Offset position) const {
  validate_position(position);
  std::size_t offset = 0;
  std::size_t units = 0;
  while (offset < position.value()) {
    const DecodedScalar decoded = decode_valid(text_, offset);
    units += decoded.value > 0xffffU ? 2U : 1U;
    offset += decoded.bytes;
  }
  return Utf16Offset(units);
}

ScalarIndex TextStore::scalar_index(Utf8Offset position) const {
  validate_position(position);
  std::size_t offset = 0;
  std::size_t scalars = 0;
  while (offset < position.value()) {
    offset += decode_valid(text_, offset).bytes;
    ++scalars;
  }
  return ScalarIndex(scalars);
}

GraphemeIndex TextStore::grapheme_index(Utf8Offset position) const {
  validate_position(position);
  const std::size_t boundary_position = detail::lower_bound_index(
      std::span<const std::uint32_t>(grapheme_boundaries_), position.value());
  if (boundary_position == grapheme_boundaries_.size() ||
      grapheme_boundaries_[boundary_position] != position.value()) {
    ++rejected_position_query_count_;
    throw std::invalid_argument(
        "GUI.Forms UTF-8 position splits an extended grapheme cluster");
  }
  return GraphemeIndex(boundary_position);
}

Utf8Offset TextStore::next_scalar_boundary(Utf8Offset position) const {
  validate_position(position);
  if (position == utf8_size()) {
    return position;
  }
  return Utf8Offset(position.value() +
                    decode_valid(text_, position.value()).bytes);
}

Utf8Offset TextStore::previous_scalar_boundary(Utf8Offset position) const {
  validate_position(position);
  if (position.value() == 0) {
    return position;
  }
  std::size_t offset = position.value() - 1U;
  while (offset > 0 && continuation(static_cast<std::uint8_t>(text_[offset]))) {
    --offset;
  }
  return Utf8Offset(offset);
}

Utf8Offset TextStore::next_grapheme_boundary(Utf8Offset position) const {
  const GraphemeIndex index = grapheme_index(position);
  if (index == grapheme_count()) {
    return position;
  }
  return Utf8Offset(grapheme_boundaries_[index.value() + 1U]);
}

Utf8Offset TextStore::previous_grapheme_boundary(Utf8Offset position) const {
  const GraphemeIndex index = grapheme_index(position);
  if (index.value() == 0U) {
    return position;
  }
  return Utf8Offset(grapheme_boundaries_[index.value() - 1U]);
}

Utf8Range TextStore::grapheme_range(GraphemeIndex grapheme) const {
  if (grapheme >= grapheme_count()) {
    ++rejected_position_query_count_;
    throw std::out_of_range("GUI.Forms grapheme index has no cluster");
  }
  return {Utf8Offset(grapheme_boundaries_[grapheme.value()]),
          Utf8Offset(grapheme_boundaries_[grapheme.value() + 1U])};
}

char32_t TextStore::scalar_at(Utf8Offset position) const {
  validate_position(position);
  if (position == utf8_size()) {
    ++rejected_position_query_count_;
    throw std::out_of_range("GUI.Forms cannot read a scalar at end of text");
  }
  return decode_valid(text_, position.value()).value;
}

Utf8Offset TextStore::line_start(LineIndex line) const {
  if (line.value() >= line_starts_.size()) {
    ++rejected_position_query_count_;
    throw std::out_of_range("GUI.Forms line position exceeds text length");
  }
  return Utf8Offset(line_starts_[line.value()]);
}

Utf8Range TextStore::line_content_range(LineIndex line) const {
  if (line.value() >= line_starts_.size()) {
    ++rejected_position_query_count_;
    throw std::out_of_range("GUI.Forms line position exceeds text length");
  }
  return {Utf8Offset(line_starts_[line.value()]),
          Utf8Offset(line_content_ends_[line.value()])};
}

TextStoreSnapshot TextStore::snapshot() const noexcept {
  return {
      text_.size(),
      utf16_units_,
      scalar_count_,
      grapheme_boundaries_.size() - 1U,
      line_starts_.size(),
      style_spans_.size(),
      revision_,
      edit_count_,
      style_update_count_,
      metadata_rebuild_count_,
      rejected_mutation_count_,
      rejected_position_query_count_,
  };
}

TextStore::Metadata TextStore::analyze(std::string_view text) {
  const Utf8ValidationResult validation = validate_utf8(text);
  if (!validation.valid()) {
    throw std::invalid_argument(
        "GUI.Forms text is not valid UTF-8 at byte " +
        std::to_string(validation.error_offset.value()) + ": " +
        std::string(to_string(validation.error)));
  }

  Metadata metadata;
  metadata.scalars = validation.scalar_count;
  metadata.utf16_units = validation.utf16_unit_count;
  metadata.grapheme_boundaries =
      detail::extended_grapheme_boundaries(text, validation.scalar_count);
  metadata.line_starts.push_back(0);
  std::size_t offset = 0;
  while (offset < text.size()) {
    const DecodedScalar decoded = decode_valid(text, offset);
    if (!is_line_break(decoded.value)) {
      offset += decoded.bytes;
      continue;
    }
    metadata.line_content_ends.push_back(offset);
    offset += decoded.bytes;
    if (decoded.value == U'\r' && offset < text.size()) {
      const DecodedScalar next = decode_valid(text, offset);
      if (next.value == U'\n') {
        offset += next.bytes;
      }
    }
    metadata.line_starts.push_back(offset);
  }
  metadata.line_content_ends.push_back(text.size());
  return metadata;
}

std::vector<TextStyleSpan>
TextStore::normalize_style_spans(std::span<const TextStyleSpan> spans,
                                 std::string_view candidate_text) const {
  if (spans.size() > limits_.maximum_style_spans) {
    throw std::length_error("GUI.Forms style-span input exceeds its limit");
  }
  std::vector<TextStyleSpan> result(spans.begin(), spans.end());
  std::sort(result.begin(), result.end(), style_span_less);

  std::vector<TextStyleSpan> normalized;
  normalized.reserve(result.size());
  for (const TextStyleSpan &span : result) {
    if (span.range.start >= span.range.end) {
      throw std::invalid_argument("GUI.Forms style spans must be nonempty");
    }
    if (span.range.end.value() > candidate_text.size()) {
      throw std::out_of_range("GUI.Forms style span exceeds text length");
    }
    if (!utf8_boundary(candidate_text, span.range.start) ||
        !utf8_boundary(candidate_text, span.range.end)) {
      throw std::invalid_argument("GUI.Forms style span splits a UTF-8 scalar");
    }
    if (!normalized.empty() && span.range.start < normalized.back().range.end) {
      throw std::invalid_argument("GUI.Forms style spans may not overlap");
    }
    if (!normalized.empty() &&
        span.range.start == normalized.back().range.end &&
        span.style == normalized.back().style) {
      normalized.back().range.end = span.range.end;
    } else {
      normalized.push_back(span);
    }
  }
  return normalized;
}

std::vector<TextStyleSpan>
TextStore::transform_style_spans(Utf8Range removed, std::size_t inserted_bytes,
                                 std::optional<TextStyleId> inserted_style,
                                 std::string_view candidate_text) const {
  const std::size_t start = removed.start.value();
  const std::size_t end = removed.end.value();
  const std::size_t removed_bytes = end - start;
  const bool grows = inserted_bytes >= removed_bytes;
  const std::size_t magnitude =
      grows ? inserted_bytes - removed_bytes : removed_bytes - inserted_bytes;
  std::vector<TextStyleSpan> result;
  result.reserve(style_spans_.size() +
                 (inserted_style && inserted_bytes > 0 ? 1U : 0U));
  for (const TextStyleSpan &span : style_spans_) {
    if (span.range.end.value() <= start) {
      result.push_back(span);
    } else if (span.range.start.value() >= end) {
      result.push_back({{Utf8Offset(shifted_offset(
                             grows, magnitude, span.range.start.value())),
                         Utf8Offset(shifted_offset(
                             grows, magnitude, span.range.end.value()))},
                        span.style});
    } else {
      if (span.range.start.value() < start) {
        result.push_back({{span.range.start, Utf8Offset(start)}, span.style});
      }
      if (span.range.end.value() > end) {
        result.push_back({{Utf8Offset(start + inserted_bytes),
                           Utf8Offset(shifted_offset(
                               grows, magnitude, span.range.end.value()))},
                          span.style});
      }
    }
  }
  if (inserted_style && inserted_bytes > 0) {
    result.push_back({{Utf8Offset(start), Utf8Offset(start + inserted_bytes)},
                      *inserted_style});
  }
  return normalize_style_spans(result, candidate_text);
}

void TextStore::validate_position(Utf8Offset position) const {
  if (position.value() > text_.size()) {
    ++rejected_position_query_count_;
    throw std::out_of_range("GUI.Forms UTF-8 position exceeds text length");
  }
  if (!is_scalar_boundary(position)) {
    ++rejected_position_query_count_;
    throw std::invalid_argument("GUI.Forms UTF-8 position splits a scalar");
  }
}

void TextStore::validate_range(Utf8Range range) const {
  if (range.start > range.end) {
    throw std::invalid_argument("GUI.Forms text range is reversed");
  }
  if (range.end.value() > text_.size()) {
    throw std::out_of_range("GUI.Forms text range exceeds text length");
  }
  if (!is_scalar_boundary(range.start) || !is_scalar_boundary(range.end)) {
    throw std::invalid_argument("GUI.Forms text range splits a UTF-8 scalar");
  }
}

void TextStore::reject_invalid_utf8(const Utf8ValidationResult &validation) {
  throw std::invalid_argument(
      "GUI.Forms replacement is not valid UTF-8 at byte " +
      std::to_string(validation.error_offset.value()) + ": " +
      std::string(to_string(validation.error)));
}

} // namespace gui_forms
