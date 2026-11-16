#include "gui_forms/text_shaping.hpp"

#include <stxlib/optional.hpp>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <optional>
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

class DeterministicMockShaper final : public TextShaper {
public:
  GlyphRun shape(const ShapingRequest &request) override {
    if (validate_shaping_request(request) != ShapingRequestError::none) {
      throw std::invalid_argument("invalid mock shaping request");
    }
    TextStore text(request.utf8_text);
    std::vector<GlyphPlacement> glyphs;
    GraphemeIndex first = text.grapheme_index(request.range.start);
    GraphemeIndex end = text.grapheme_index(request.range.end);
    for (std::size_t index = first.value(); index < end.value(); ++index) {
      glyphs.push_back({GlyphId{static_cast<std::uint32_t>(index + 1U)},
                        text.utf8_offset(GraphemeIndex(index)), 8.0F, 0.0F,
                        0.0F, 0.0F});
    }
    if (request.direction == TextDirection::right_to_left) {
      std::reverse(glyphs.begin(), glyphs.end());
    }
    return {request.range, request.face, request.direction, std::move(glyphs)};
  }
};

class OrderedMockFallback final : public FontFallbackResolver {
public:
  stxlib::Optional<FontFallbackMatch>
  resolve(const FontFallbackRequest &request) override {
    observed_attempts.assign(request.attempted_faces.begin(),
                             request.attempted_faces.end());
    if (request.cluster.empty()) {
      return stxlib::Optional<gui_forms::FontFallbackMatch>::none();
    }
    return stxlib::Optional<gui_forms::FontFallbackMatch>::of(FontFallbackMatch{FontFaceId{99}, request.cluster.size()});
  }

  std::vector<FontFaceId> observed_attempts;
};

void test_request_and_result_contract() {
  const std::string text = "a\xcc\x81"
                           "b";
  const ShapingFeature ligatures{open_type_tag('l', 'i', 'g', 'a'), 1,
                                 {Utf8Offset(0), Utf8Offset(text.size())}};
  const ShapingRequest request{
      text,
      {Utf8Offset(0), Utf8Offset(text.size())},
      FontFaceId{7},
      13.0F,
      TextDirection::left_to_right,
      open_type_tag('l', 'a', 't', 'n'),
      "en",
      std::span<const ShapingFeature>(&ligatures, 1),
  };
  require(validate_shaping_request(request) == ShapingRequestError::none,
          "complete grapheme-aligned requests must validate");

  DeterministicMockShaper shaper;
  GlyphRun run = shaper.shape(request);
  require(run.glyphs.size() == 2 && run.glyphs[0].cluster == Utf8Offset(0) &&
              run.glyphs[1].cluster == Utf8Offset(3) &&
              validate_glyph_run(request, run) == GlyphRunError::none,
          "glyph cluster maps must use absolute grapheme-aligned UTF-8 offsets");

  ShapingRequest rtl = request;
  rtl.direction = TextDirection::right_to_left;
  GlyphRun rtl_run = shaper.shape(rtl);
  require(rtl_run.glyphs.front().cluster == Utf8Offset(3) &&
              validate_glyph_run(rtl, rtl_run) == GlyphRunError::none,
          "right-to-left runs must expose deterministic descending clusters");
}

void test_validation_rejects_ambiguous_boundaries() {
  const std::string text = "a\xcc\x81"
                           "b";
  ShapingRequest request{text,
                         {Utf8Offset(1), Utf8Offset(text.size())},
                         FontFaceId{1},
                         12.0F,
                         TextDirection::left_to_right,
                         open_type_tag('l', 'a', 't', 'n'),
                         "en",
                         {}};
  require(validate_shaping_request(request) ==
              ShapingRequestError::range_splits_grapheme,
          "a shaping range may not start inside an extended grapheme cluster");

  request.range.start = Utf8Offset(2);
  require(validate_shaping_request(request) ==
              ShapingRequestError::range_splits_scalar,
          "a shaping range may not split UTF-8 scalar encoding");

  request.range.start = Utf8Offset(0);
  GlyphRun run{request.range,
               request.face,
               request.direction,
               {{GlyphId{1}, Utf8Offset(1), 1.0F, 0.0F, 0.0F, 0.0F}}};
  require(validate_glyph_run(request, run) ==
              GlyphRunError::cluster_splits_grapheme,
          "a shaper result may not return a cluster inside a grapheme");
  run.glyphs[0].cluster = Utf8Offset(0);
  run.glyphs[0].advance_x = std::nanf("");
  require(validate_glyph_run(request, run) ==
              GlyphRunError::nonfinite_placement,
          "nonfinite glyph metrics must be rejected at the service boundary");
}

void test_fallback_preserves_attempt_order() {
  OrderedMockFallback fallback;
  const std::vector<FontFaceId> attempts = {{7}, {8}, {9}};
  const std::vector<char32_t> cluster = {U'\U0001f469', U'\u200d',
                                         U'\U0001f4bb'};
  const FontFallbackRequest request{FontFaceId{7}, attempts, cluster, "en",
                                    open_type_tag('z', 'y', 'y', 'y')};
  const stxlib::Optional<FontFallbackMatch> match = fallback.resolve(request);
  require(match.present() && ((*match.value())).face == FontFaceId{99} &&
              ((*match.value())).covered_scalars == cluster.size() &&
              fallback.observed_attempts == attempts,
          "fallback services must receive caller-declared attempt order and the "
          "complete grapheme cluster");
}

} // namespace

int main() {
  try {
    test_request_and_result_contract();
    test_validation_rejects_ambiguous_boundaries();
    test_fallback_preserves_attempt_order();
    std::cout << "text_shaping_tests: PASS\n";
    return EXIT_SUCCESS;
  } catch (const std::exception &error) {
    std::cerr << "text_shaping_tests: FAIL: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
