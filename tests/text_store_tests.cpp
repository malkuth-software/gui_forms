#include "gui_forms/text.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace gui_forms;

void require(bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

#define REQUIRE_THROWS(type, message, ...) do {                                \
  bool text_store_expected_exception = false;                                  \
  try {                                                                         \
    static_cast<void>(__VA_ARGS__);                                             \
  } catch (const type &) {                                                      \
    text_store_expected_exception = true;                                      \
  }                                                                             \
  require(text_store_expected_exception, message);                             \
} while (false)

std::uint32_t next_random(std::uint32_t &state) {
  state = state * 1'664'525U + 1'013'904'223U;
  return state;
}

void test_strict_utf8_validation() {
  const std::string valid = "A\xc3\xa9\xf0\x9f\x98\x80";
  const Utf8ValidationResult result = validate_utf8(valid);
  require(result.valid() && result.scalar_count == 3 &&
              result.utf16_unit_count == 4 &&
              result.error_offset == Utf8Offset(valid.size()),
          "strict UTF-8 validation must count scalars and UTF-16 units");

  const std::string unexpected{"\x80", 1};
  const std::string truncated{"\xf0\x9f\x98", 3};
  const std::string overlong{"\xc0\x80", 2};
  const std::string surrogate{"\xed\xa0\x80", 3};
  const std::string too_large{"\xf4\x90\x80\x80", 4};
  const std::string bad_continuation{"\xe2\x28\xa1", 3};
  require(
      validate_utf8(unexpected).error ==
              Utf8ValidationError::unexpected_continuation &&
          validate_utf8(truncated).error ==
              Utf8ValidationError::truncated_sequence &&
          validate_utf8(overlong).error ==
              Utf8ValidationError::overlong_encoding &&
          validate_utf8(surrogate).error ==
              Utf8ValidationError::surrogate_code_point &&
          validate_utf8(too_large).error ==
              Utf8ValidationError::code_point_out_of_range &&
          validate_utf8(bad_continuation).error ==
              Utf8ValidationError::invalid_continuation,
      "strict UTF-8 validation must distinguish malformed sequence classes");
}

void test_typed_position_round_trips() {
  const std::string text = "A\xc3\xa9\xf0\x9f\x98\x80Z";
  TextStore store(text);
  require(
      store.utf8_size() == Utf8Offset(8) &&
          store.utf16_size() == Utf16Offset(5) &&
          store.scalar_count() == ScalarIndex(4),
      "text store must expose byte, UTF-16, and scalar sizes independently");

  const std::vector<Utf8Offset> bytes = {Utf8Offset(0), Utf8Offset(1),
                                         Utf8Offset(3), Utf8Offset(7),
                                         Utf8Offset(8)};
  const std::vector<Utf16Offset> utf16 = {Utf16Offset(0), Utf16Offset(1),
                                          Utf16Offset(2), Utf16Offset(4),
                                          Utf16Offset(5)};
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    require(store.utf16_offset(bytes[index]) == utf16[index] &&
                store.utf8_offset(utf16[index]) == bytes[index],
            "valid UTF-8 and UTF-16 positions must round-trip exactly");
    require(store.scalar_index(bytes[index]) == ScalarIndex(index) &&
                store.utf8_offset(ScalarIndex(index)) == bytes[index],
            "valid UTF-8 and scalar positions must round-trip exactly");
  }
  REQUIRE_THROWS(std::invalid_argument,
                 "UTF-16 positions may not split a surrogate pair",
                 store.utf8_offset(Utf16Offset(3)));
  REQUIRE_THROWS(std::invalid_argument,
                 "UTF-8 positions may not split a scalar",
                 store.scalar_index(Utf8Offset(2)));
  REQUIRE_THROWS(std::out_of_range,
                 "reading a scalar at end-of-text must be rejected",
                 store.scalar_at(store.utf8_size()));

  require(store.scalar_at(Utf8Offset(3)) == U'\U0001f600' &&
              store.next_scalar_boundary(Utf8Offset(3)) == Utf8Offset(7) &&
              store.previous_scalar_boundary(Utf8Offset(7)) == Utf8Offset(3),
          "scalar navigation must preserve a supplementary-plane scalar");
}

void test_extended_grapheme_navigation() {
  const std::string text =
      "a\xcc\x81"                         // base + combining acute
      "\xf0\x9f\x91\xa9\xe2\x80\x8d" // woman + ZWJ
      "\xf0\x9f\x92\xbb"               // laptop
      "\xf0\x9f\x87\xba\xf0\x9f\x87\xb8"; // US flag
  TextStore store(text);
  require(store.scalar_count() == ScalarIndex(7) &&
              store.grapheme_count() == GraphemeIndex(3) &&
              store.snapshot().graphemes == 3 &&
              grapheme_unicode_version() == "17.0.0",
          "combining, emoji ZWJ, and flag sequences must each form one cluster");

  const std::vector<Utf8Offset> expected = {
      Utf8Offset(0), Utf8Offset(3), Utf8Offset(14), Utf8Offset(text.size())};
  for (std::size_t index = 0; index < expected.size(); ++index) {
    require(store.utf8_offset(GraphemeIndex(index)) == expected[index] &&
                store.grapheme_index(expected[index]) == GraphemeIndex(index) &&
                store.is_grapheme_boundary(expected[index]),
            "typed grapheme positions must round-trip at every cluster edge");
  }
  require(store.next_grapheme_boundary(expected[0]) == expected[1] &&
              store.previous_grapheme_boundary(expected[2]) == expected[1] &&
              store.grapheme_range(GraphemeIndex(1)) ==
                  Utf8Range{expected[1], expected[2]},
          "grapheme navigation must step over a complete emoji ZWJ sequence");
  REQUIRE_THROWS(
      std::invalid_argument,
      "grapheme lookup must reject a scalar boundary inside a cluster",
      store.grapheme_index(Utf8Offset(1)));
  REQUIRE_THROWS(
      std::out_of_range,
      "end-of-text grapheme position may not be read as a cluster",
      store.grapheme_range(store.grapheme_count()));
}

void test_atomic_mutation_and_limits() {
  TextStore store("alpha \xf0\x9f\x98\x80 omega", {.maximum_utf8_bytes = 64});
  const TextEditResult edit = store.replace({Utf8Offset(6), Utf8Offset(10)},
                                            "\xe4\xb8\x96\xe7\x95\x8c");
  require(store.utf8() == "alpha \xe4\xb8\x96\xe7\x95\x8c omega" &&
              edit.changed && edit.removed_scalars == 1 &&
              edit.inserted_scalars == 2 &&
              edit.inserted == Utf8Range{Utf8Offset(6), Utf8Offset(12)},
          "replacement must report exact removed and inserted Unicode units");

  const std::string before(store.utf8());
  const TextStoreSnapshot before_rejection = store.snapshot();
  const std::string invalid{"\xed\xa0\x80", 3};
  REQUIRE_THROWS(std::invalid_argument,
                 "invalid replacement UTF-8 must be rejected",
                 store.replace(Utf8Range{Utf8Offset(0), Utf8Offset(0)},
                               invalid));
  REQUIRE_THROWS(std::invalid_argument,
                 "replacement ranges may not split a scalar",
                 store.replace(Utf8Range{Utf8Offset(7), Utf8Offset(8)}, "x"));
  REQUIRE_THROWS(std::length_error,
                 "text byte limits must be enforced before mutation",
                 store.replace(Utf8Range{Utf8Offset(0), Utf8Offset(0)},
                               std::string(65, 'x')));
  const TextStoreSnapshot after_rejection = store.snapshot();
  require(store.utf8() == before &&
              after_rejection.revision == before_rejection.revision &&
              after_rejection.edit_count == before_rejection.edit_count &&
              after_rejection.rejected_mutation_count ==
                  before_rejection.rejected_mutation_count + 3,
          "rejected mutations must preserve text/revision while incrementing "
          "diagnostics");

  const TextEditResult no_op =
      store.replace({Utf8Offset(0), Utf8Offset(0)}, std::string_view{});
  require(!no_op.changed && no_op.revision == store.revision(),
          "empty insertion must remain an observable no-op");
}

void test_unicode_line_index() {
  const std::string text = "a\r\nb\rc\nd\xc2\x85"
                           "e\xe2\x80\xa8"
                           "f\xe2\x80\xa9";
  TextStore store(text);
  require(store.line_count() == 7, "CRLF, CR, LF, NEL, line separator, and "
                                   "paragraph separator must index lines");
  const std::vector<std::string_view> expected = {"a", "b", "c", "d",
                                                  "e", "f", ""};
  for (std::size_t line = 0; line < expected.size(); ++line) {
    const Utf8Range range = store.line_content_range(LineIndex(line));
    require(
        store.utf8().substr(range.start.value(),
                            range.end.value() - range.start.value()) ==
            expected[line],
        "line content ranges must exclude the complete line-break sequence");
    require(store.line_start(LineIndex(line)) == range.start,
            "line start and line content range must agree");
  }

  static_cast<void>(
      store.replace(store.line_content_range(LineIndex(3)), "delta"));
  require(store.line_count() == 7 &&
              store.utf8().substr(
                  store.line_content_range(LineIndex(3)).start.value(), 5) ==
                  "delta",
          "line index must rebuild deterministically after an edit");
  require(
      store.snapshot().metadata_rebuild_count == 2,
      "M4a contiguous baseline must expose its full metadata rebuild count");
}

void test_style_span_normalization_and_edit_transform() {
  TextStore store("abcdef");
  const TextStyleId strong{7};
  const TextStyleId accent{9};
  const std::vector<TextStyleSpan> spans = {
      {{Utf8Offset(2), Utf8Offset(4)}, strong},
      {{Utf8Offset(0), Utf8Offset(2)}, strong},
      {{Utf8Offset(4), Utf8Offset(6)}, accent},
  };
  store.set_style_spans(spans);
  require(
      store.style_spans().size() == 2 &&
          store.style_spans()[0] ==
              TextStyleSpan{{Utf8Offset(0), Utf8Offset(4)}, strong} &&
          store.style_at(Utf8Offset(3)) == strong &&
          store.style_at(Utf8Offset(5)) == accent,
      "adjacent equal style spans must normalize after deterministic sorting");

  static_cast<void>(
      store.replace({Utf8Offset(2), Utf8Offset(3)}, "XYZ", TextStyleId{11}));
  require(store.utf8() == "abXYZdef" && store.style_spans().size() == 4 &&
              store.style_at(Utf8Offset(1)) == strong &&
              store.style_at(Utf8Offset(2)) == TextStyleId{11} &&
              store.style_at(Utf8Offset(5)) == strong &&
              store.style_at(Utf8Offset(7)) == accent,
          "text replacement must preserve surrounding spans and explicitly "
          "style insertion");

  const TextStoreSnapshot prior = store.snapshot();
  const std::vector<TextStyleSpan> overlapping = {
      {{Utf8Offset(0), Utf8Offset(3)}, strong},
      {{Utf8Offset(2), Utf8Offset(4)}, accent},
  };
  REQUIRE_THROWS(std::invalid_argument,
                 "overlapping style spans must be rejected",
                 store.set_style_spans(overlapping));
  require(
      store.snapshot().revision == prior.revision &&
          store.snapshot().rejected_mutation_count ==
              prior.rejected_mutation_count + 1,
      "rejected style changes must preserve the committed style generation");

  TextStore unicode("a\xf0\x9f\x98\x80z");
  const std::vector<TextStyleSpan> split_scalar = {
      {{Utf8Offset(1), Utf8Offset(3)}, strong},
  };
  REQUIRE_THROWS(std::invalid_argument,
                 "style spans may not split a supplementary-plane scalar",
                 unicode.set_style_spans(split_scalar));
}

void test_deterministic_edit_corpus() {
  const std::vector<std::string> corpus = {
      "a",
      "\xc3\xa9",
      "\xcc\x81",
      "\xf0\x9f\x98\x80",
      "\xe4\xb8\xad",
      "\xd7\x90",
      "\n",
      "\xe2\x80\x8d",
  };
  TextStore store;
  std::vector<std::string> reference;
  std::uint32_t state = 0x6d346134U;

  for (std::size_t iteration = 0; iteration < 2'000; ++iteration) {
    std::size_t start = next_random(state) % (reference.size() + 1U);
    std::size_t end = next_random(state) % (reference.size() + 1U);
    if (end < start) {
      std::swap(start, end);
    }
    std::size_t insertion_count = next_random(state) % 4U;
    if (reference.size() - (end - start) + insertion_count > 64U) {
      insertion_count = 0;
    }
    std::vector<std::string> insertion;
    std::string replacement;
    for (std::size_t item = 0; item < insertion_count; ++item) {
      insertion.push_back(corpus[next_random(state) % corpus.size()]);
      replacement += insertion.back();
    }

    const Utf8Range range{
        store.utf8_offset(ScalarIndex(start)),
        store.utf8_offset(ScalarIndex(end)),
    };
    const TextEditResult result = store.replace(range, replacement);
    const bool expected_change = start != end || !insertion.empty();
    require(result.changed == expected_change,
            "deterministic corpus edit must report no-op state exactly");
    reference.erase(reference.begin() + static_cast<std::ptrdiff_t>(start),
                    reference.begin() + static_cast<std::ptrdiff_t>(end));
    reference.insert(reference.begin() + static_cast<std::ptrdiff_t>(start),
                     insertion.begin(), insertion.end());

    std::string expected;
    for (const std::string &scalar : reference) {
      expected += scalar;
    }
    require(store.utf8() == expected &&
                store.scalar_count() == ScalarIndex(reference.size()) &&
                validate_utf8(store.utf8()).valid(),
            "deterministic mixed-script edits must match the scalar reference "
            "model");
    for (std::size_t scalar = 0; scalar <= reference.size(); ++scalar) {
      const Utf8Offset byte = store.utf8_offset(ScalarIndex(scalar));
      require(store.scalar_index(byte) == ScalarIndex(scalar),
              "every surviving scalar boundary must round-trip after mutation");
    }
    for (std::size_t grapheme = 0;
         grapheme <= store.grapheme_count().value(); ++grapheme) {
      const Utf8Offset byte = store.utf8_offset(GraphemeIndex(grapheme));
      require(store.grapheme_index(byte) == GraphemeIndex(grapheme),
              "every surviving grapheme boundary must round-trip after mutation");
    }
  }

  const TextStoreSnapshot snapshot = store.snapshot();
  require(snapshot.metadata_rebuild_count == snapshot.edit_count + 1U &&
              snapshot.rejected_mutation_count == 0,
          "successful baseline edits must expose one deterministic metadata "
          "rebuild each");
}

// N50: an edit re-analyses only the lines it touches and splices the
// offsets. After every edit the store must hold exactly the metadata a store
// built from its text holds, across line breaks of every kind (a CRLF split
// and joined, NEL, LS), regional-indicator pairs, ZWJ sequences and
// combining marks on either side of an edit.
void test_local_reanalysis_matches_full_analysis() {
  const std::vector<std::string> corpus = {
      "a", "b", "\r", "\n", "\r\n", "\xc2\x85", "\xe2\x80\xa8",
      "\xcc\x81", "\xe2\x80\x8d", "\xf0\x9f\x87\xa6", "\xf0\x9f\x87\xa9",
      "\xf0\x9f\x91\xa9", "\xe0\xa4\x95", "\xe0\xa5\x8d", " ",
  };
  TextStore store;
  std::uint32_t state = 0x50a1e5c1U;
  for (std::size_t iteration = 0; iteration < 3'000; ++iteration) {
    const std::size_t scalars = store.scalar_count().value();
    std::size_t start = next_random(state) % (scalars + 1U);
    std::size_t end = next_random(state) % (scalars + 1U);
    if (end < start) {
      std::swap(start, end);
    }
    if (end - start > 6U) {
      end = start + next_random(state) % 7U;
    }
    std::size_t insertion_count = next_random(state) % 5U;
    if (store.utf8().size() > 256U) {
      insertion_count = 0U;
    }
    std::string replacement;
    for (std::size_t item = 0; item < insertion_count; ++item) {
      replacement += corpus[next_random(state) % corpus.size()];
    }
    const Utf8Range range{store.utf8_offset(ScalarIndex(start)),
                          store.utf8_offset(ScalarIndex(end))};
    static_cast<void>(store.replace(range, replacement));

    const TextStore fresh(store.utf8());
    require(store.scalar_count() == fresh.scalar_count() &&
                store.utf16_size() == fresh.utf16_size() &&
                store.line_count() == fresh.line_count() &&
                store.grapheme_count() == fresh.grapheme_count(),
            "a locally re-analysed store must count what a fresh one counts");
    for (std::size_t line = 0; line < fresh.line_count(); ++line) {
      const Utf8Range kept = store.line_content_range(LineIndex(line));
      const Utf8Range built = fresh.line_content_range(LineIndex(line));
      require(store.line_start(LineIndex(line)) == fresh.line_start(LineIndex(line)) &&
                  kept.start == built.start && kept.end == built.end,
              "a locally re-analysed store must hold a fresh store's lines");
    }
    for (std::size_t grapheme = 0; grapheme < fresh.grapheme_count().value(); ++grapheme) {
      const Utf8Range kept = store.grapheme_range(GraphemeIndex(grapheme));
      const Utf8Range built = fresh.grapheme_range(GraphemeIndex(grapheme));
      require(kept.start == built.start && kept.end == built.end,
              "a locally re-analysed store must hold a fresh store's graphemes");
    }
  }
}

} // namespace

int main() {
  try {
    test_strict_utf8_validation();
    test_typed_position_round_trips();
    test_extended_grapheme_navigation();
    test_atomic_mutation_and_limits();
    test_unicode_line_index();
    test_style_span_normalization_and_edit_transform();
    test_deterministic_edit_corpus();
    test_local_reanalysis_matches_full_analysis();
    std::cout << "text_store_tests: PASS\n";
    return EXIT_SUCCESS;
  } catch (const std::exception &error) {
    std::cerr << "text_store_tests: FAIL: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
