#include "harfbuzz_font_engine.hpp"
#include "../src/core/resources/fonts/bundled_font_set.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace gui_forms;
using namespace gui_forms::render::text;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::vector<std::byte> read_file(const char* path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) throw std::runtime_error("font fixture could not be opened");
    const std::streamsize length = stream.tellg();
    if (length <= 0) throw std::runtime_error("font fixture is empty");
    std::vector<std::byte> bytes(static_cast<std::size_t>(length));
    stream.seekg(0);
    stream.read(reinterpret_cast<char*>(bytes.data()), length);
    if (!stream) throw std::runtime_error("font fixture could not be read");
    return bytes;
}

bool near(double left, double right, double tolerance) {
    return std::abs(left - right) <= tolerance;
}

bool shaped_text_uses_face(const ShapedText& shaped, FontFaceId id) {
    for (const ShapedFontRun& run : shaped.runs) {
        if (run.face == id) return true;
    }
    return false;
}

void test_mixed_direction_text() {
    HarfBuzzFontEngine engine;
    require(engine.register_typeface(FontRole::control, 400, false,
        read_file(GUI_FORMS_TEST_CARLITO_REGULAR)).has_value(), "Latin primary registers");
    require(engine.register_fallback_typeface(400, false,
        read_file(GUI_FORMS_TEST_NOTO_ARABIC_REGULAR)).has_value(), "Arabic fallback registers");
    require(engine.register_fallback_typeface(400, false,
        read_file(GUI_FORMS_TEST_NOTO_HEBREW_REGULAR)).has_value(), "Hebrew fallback registers");
    const std::string text = "שלום ABC 123";
    const ShapedText shaped = engine.shape(text, {FontRole::control, 18, 400, false});
    require(shaped.missing_clusters == 0 && shaped.runs.size() >= 2, "mixed Hebrew Latin and digits have complete glyphs");
    // The LTR island belongs to the left of the Hebrew word. Stored offsets
    // remain offsets into the original UTF-8 string, suitable for hit testing.
    require(shaped.runs.front().source_range.start.value() >= 8 &&
            shaped.runs.back().source_range.start.value() == 0,
            "Unicode visual run order differs from logical source order");
    const ShapedFontRun& hebrew = shaped.runs.back();
    require(hebrew.glyphs.front().cluster.value() > hebrew.glyphs.back().cluster.value(),
            "Hebrew glyph clusters progress right to left");
    const ShapedText arabic = engine.shape("اللون 123 (RGB)", {FontRole::control, 18, 400, false});
    require(arabic.missing_clusters == 0 && arabic.width > 0, "Arabic with a number and Latin abbreviation shapes");
}

void test_shared_font_lifetime() {
    std::weak_ptr<const std::vector<std::byte>> weak;
    {
        HarfBuzzFontEngine engine;
        std::shared_ptr<const std::vector<std::byte>> bytes =
            std::make_shared<const std::vector<std::byte>>(read_file(GUI_FORMS_TEST_CARLITO_REGULAR));
        weak = bytes;
        require(engine.register_shared_typeface(FontRole::content, 400, false, bytes).has_value(),
                "shared immutable font registers");
        require(bytes.use_count() == 2, "font engine shares caller bytes without a copy");
        bytes.reset();
        require(!weak.expired() && engine.shape("office", {FontRole::content, 16, 400, false}).width > 0,
                "shared font remains valid after caller releases ownership");
    }
    require(weak.expired(), "font storage releases after the final owner is destroyed");
}

void test_owned_font_view_lifetime() {
    struct FontOwner final { std::vector<std::byte> bytes; };
    std::weak_ptr<const FontOwner> weak;
    {
        HarfBuzzFontEngine engine;
        std::shared_ptr<const FontOwner> owner = std::make_shared<const FontOwner>(
            FontOwner{read_file(GUI_FORMS_TEST_CARLITO_REGULAR)});
        weak = owner;
        require(!engine.register_owned_typeface(FontRole::content, 400, false,
                    (*owner).bytes, {}).has_value(), "borrowed bytes require a lifetime owner");
        require(engine.register_owned_typeface(FontRole::content, 400, false,
                    (*owner).bytes, owner).has_value(), "owned immutable view registers");
        require(owner.use_count() == 2, "engine retains owner without copying the font");
        owner.reset();
        require(!weak.expired() && engine.shape("office", {FontRole::content, 16, 400, false}).width > 0,
                "owned view stays valid after the caller drops it");
    }
    require(weak.expired(), "font mapping owner releases with the last face");
}

void test_owned_registration_style_selection_and_ligatures() {
    HarfBuzzFontEngine engine;
    std::vector<std::byte> regular = read_file(GUI_FORMS_TEST_CARLITO_REGULAR);
    std::vector<std::byte> bold = read_file(GUI_FORMS_TEST_CARLITO_BOLD);
    const std::optional<FontFaceId> regular_id = engine.register_typeface(
        FontRole::content, 400, false, regular);
    const std::optional<FontFaceId> bold_id = engine.register_typeface(
        FontRole::content, 700, false, bold);
    require(regular_id && bold_id && *regular_id != *bold_id &&
                engine.face_count() == 2U,
            "valid scalable faces must receive distinct stable engine IDs");

    std::fill(regular.begin(), regular.end(), std::byte{});
    regular.clear();
    bold.clear();
    const ShapedText normal = engine.shape(
        "office", {FontRole::content, 16.0, 400, false});
    const ShapedText heavy = engine.shape(
        "office", {FontRole::content, 16.0, 700, false});
    require(normal.runs.size() == 1U && normal.runs.front().face == *regular_id &&
                heavy.runs.size() == 1U && heavy.runs.front().face == *bold_id &&
                normal.runs.front().glyphs.size() < 6U &&
                normal.width > 0.0 && normal.height > 0.0,
            "owned bytes must survive caller release, select weight, and shape ligatures");
}

void test_cluster_fallback_is_bounded_and_absolute() {
    HarfBuzzFontEngine engine;
    const std::vector<std::byte> rapids =
        read_file(GUI_FORMS_TEST_RAPIDS_REGULAR);
    const std::vector<std::byte> carlito =
        read_file(GUI_FORMS_TEST_CARLITO_REGULAR);
    const std::optional<FontFaceId> primary = engine.register_typeface(
        FontRole::control, 400, false, rapids);
    const std::optional<FontFaceId> fallback = engine.register_typeface(
        FontRole::control, 400, false, carlito);
    require(primary && fallback, "fallback fixture faces must register");

    const std::string mixed = "A\xd0\x96"
                              "B";
    const ShapedText shaped = engine.shape(
        mixed, {FontRole::control, 14.0, 400, false});
    require(shaped.runs.size() == 3U &&
                shaped.runs[0].face == *primary &&
                shaped.runs[1].face == *fallback &&
                shaped.runs[2].face == *primary &&
                shaped.runs[0].glyphs.front().cluster == Utf8Offset(0U) &&
                shaped.runs[1].glyphs.front().cluster == Utf8Offset(1U) &&
                shaped.runs[2].glyphs.front().cluster == Utf8Offset(3U) &&
                shaped.missing_clusters == 0U,
            "fallback must preserve whole grapheme boundaries and absolute UTF-8 clusters");

    const ShapedText missing = engine.shape(
        "a\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb"
        "b", {FontRole::control, 14.0, 400, false});
    require(missing.missing_clusters == 1U,
            "an uncovered emoji ZWJ sequence must report one indivisible pack fault");
}

void test_control_navigation_symbols_stay_in_rapids() {
    const std::vector<std::byte> rapids_regular = read_file(
        GUI_FORMS_TEST_RAPIDS_REGULAR);
    const std::vector<std::byte> rapids_bold = read_file(
        GUI_FORMS_TEST_RAPIDS_BOLD);
    const std::vector<std::byte> carlito = read_file(
        GUI_FORMS_TEST_CARLITO_REGULAR);
    HarfBuzzFontEngine engine;
    const std::optional<FontFaceId> regular = engine.register_typeface(
        FontRole::control, 400, false, rapids_regular);
    const std::optional<FontFaceId> bold = engine.register_typeface(
        FontRole::control, 700, false, rapids_bold);
    const std::optional<FontFaceId> body = engine.register_typeface(
        FontRole::content, 400, false, carlito);
    require(regular && bold && body,
            "house control/body faces must register for navigation shaping");
    const std::string text = "Back ←  Up ↑  Forward →  Sort A→Z ▼";
    const ShapedText normal = engine.shape(
        text, {FontRole::control, 14.0, 400, false});
    const ShapedText heavy = engine.shape(
        text, {FontRole::control, 14.0, 700, false});
    require(normal.missing_clusters == 0U && normal.runs.size() == 1U &&
                normal.runs.front().face == *regular &&
                heavy.missing_clusters == 0U && heavy.runs.size() == 1U &&
                heavy.runs.front().face == *bold,
            "navigation controls must shape wholly in Portsmouth Rapids");
}

void test_metrics_scale_and_repeat_deterministically() {
    HarfBuzzFontEngine engine;
    const std::vector<std::byte> carlito =
        read_file(GUI_FORMS_TEST_CARLITO_REGULAR);
    require(engine.register_typeface(FontRole::content, 400, false, carlito)
                .has_value(),
            "metric fixture face must register");
    const ShapedText first = engine.shape(
        "Calibrated 123", {FontRole::content, 12.0, 400, false});
    const ShapedText repeat = engine.shape(
        "Calibrated 123", {FontRole::content, 12.0, 400, false});
    const ShapedText doubled = engine.shape(
        "Calibrated 123", {FontRole::content, 24.0, 400, false});
    require(first.runs.size() == repeat.runs.size() &&
                near(first.width, repeat.width, 0.000001) &&
                near(first.height, repeat.height, 0.000001) &&
                first.runs.front().glyphs == repeat.runs.front().glyphs &&
                doubled.width > first.width * 1.85 &&
                doubled.width < first.width * 2.15 &&
                doubled.height > first.height,
            "same pack/profile must repeat exactly and scale predictably");
}

void test_shared_cjk_and_emoji_fallback_across_roles() {
    HarfBuzzFontEngine engine;
    const std::vector<std::byte> rapids =
        read_file(GUI_FORMS_TEST_RAPIDS_REGULAR);
    const std::vector<std::byte> carlito =
        read_file(GUI_FORMS_TEST_CARLITO_REGULAR);
    const std::vector<std::byte> cousine =
        read_file(GUI_FORMS_TEST_COUSINE_REGULAR);
    const std::vector<std::byte> noto_cjk =
        read_file(GUI_FORMS_TEST_NOTO_CJK_REGULAR);
    const std::vector<std::byte> noto_emoji =
        read_file(GUI_FORMS_TEST_NOTO_EMOJI);
    const std::optional<FontFaceId> control = engine.register_typeface(FontRole::control, 400, false, rapids);
    const std::optional<FontFaceId> content = engine.register_typeface(FontRole::content, 400, false, carlito);
    const std::optional<FontFaceId> monospace = engine.register_typeface(FontRole::monospace, 400, false, cousine);
    require(control && content && monospace,
            "each public font role must have an explicit primary face");
    const std::optional<FontFaceId> cjk = engine.register_fallback_typeface(
        400, false, noto_cjk);
    const std::optional<FontFaceId> emoji = engine.register_fallback_typeface(
        400, false, noto_emoji);
    require(cjk && emoji && engine.face_count() == 5U,
            "shared fallback faces must register once rather than once per role");

    for (const FontRole role : std::array{
             FontRole::control, FontRole::content, FontRole::monospace}) {
        const std::string sample = "A日本語🚀Z";
        const ShapedText shaped = engine.shape(
            sample, {role, 15.0, 400, false});
        const bool complete = !shaped.missing_primary_face &&
            shaped.missing_clusters == 0U &&
            shaped_text_uses_face(shaped, *cjk) &&
            shaped_text_uses_face(shaped, *emoji) &&
            shaped.width > 0.0;
        if (!complete) {
            std::cerr << "fallback diagnostic role=" << static_cast<int>(role)
                      << " missing=" << shaped.missing_clusters
                      << " runs=" << shaped.runs.size()
                      << " cjk=" << shaped_text_uses_face(shaped, *cjk)
                      << " emoji=" << shaped_text_uses_face(shaped, *emoji)
                      << " faces=";
            for (const ShapedFontRun& run : shaped.runs) {
                std::cerr << run.face.value << ',';
            }
            TextStore store(sample);
            std::cerr << " bytes=" << sample.size()
                      << " graphemes=" << store.grapheme_count().value()
                      << " ranges=";
            for (std::size_t index = 0; index < store.grapheme_count().value();
                 ++index) {
                const Utf8Range range = store.grapheme_range(GraphemeIndex(index));
                std::cerr << range.start.value() << '-' << range.end.value() << ',';
            }
            std::cerr << '\n';
        }
        require(complete,
                "CJK and emoji fallback must be complete and role-independent");
    }
}

void test_letter_spacing_is_a_shaped_layout_input() {
    HarfBuzzFontEngine engine;
    const std::vector<std::byte> carlito =
        read_file(GUI_FORMS_TEST_CARLITO_REGULAR);
    require(engine.register_typeface(FontRole::content, 400, false, carlito)
                .has_value(),
            "tracking fixture face must register");
    const ShapedText native = engine.shape(
        "AB", {FontRole::content, 12.0, 400, false, 0.0});
    const ShapedText tracked = engine.shape(
        "AB", {FontRole::content, 12.0, 400, false, 0.75});
    require(native.runs.size() == 1U && tracked.runs.size() == 1U &&
                native.runs.front().glyphs.size() == 2U &&
                tracked.runs.front().glyphs.size() == 2U &&
                near(tracked.width - native.width, 0.75, 0.001) &&
                near(tracked.runs.front().glyphs[1].x -
                         native.runs.front().glyphs[1].x, 0.75, 0.001),
            "letter spacing must shift following clusters and measurement identically");

    FontSpec invalid{FontRole::content, 12.0, 400, false, 13.0};
    require(engine.shape("AB", invalid).runs.empty(),
            "pathological tracking must be rejected at the shaping boundary");
}

void test_rejection_and_missing_role_are_honest() {
    HarfBuzzFontEngine engine;
    const std::vector<std::byte> invalid(64U, std::byte{0x7f});
    const std::optional<FontFaceId> empty_face = engine.register_typeface(FontRole::content, 400, false, {});
    const std::optional<FontFaceId> invalid_face = engine.register_typeface(FontRole::content, 400, false, invalid);
    require(!empty_face && !invalid_face &&
                engine.face_count() == 0U,
            "empty and malformed font data must be rejected without partial state");
    const ShapedText missing = engine.shape(
        "No host fallback", {FontRole::content, 12.0, 400, false});
    require(missing.missing_primary_face && missing.runs.empty() &&
                missing.width == 0.0,
            "an absent bundled role must be reported rather than resolved from the host");
}

void test_public_resolution_record_names_actual_bundled_runs() {
    HarfBuzzFontEngine engine;
    const std::vector<std::byte> rapids =
        read_file(GUI_FORMS_TEST_RAPIDS_REGULAR);
    const std::vector<std::byte> carlito =
        read_file(GUI_FORMS_TEST_CARLITO_REGULAR);
    const std::vector<std::byte> cousine =
        read_file(GUI_FORMS_TEST_COUSINE_REGULAR);
    const std::vector<std::byte> noto_cjk =
        read_file(GUI_FORMS_TEST_NOTO_CJK_REGULAR);
    const std::vector<std::byte> noto_emoji =
        read_file(GUI_FORMS_TEST_NOTO_EMOJI);
    const std::optional<FontFaceId> control = engine.register_typeface(FontRole::control, 400, false, rapids);
    const std::optional<FontFaceId> content = engine.register_typeface(FontRole::content, 400, false, carlito);
    const std::optional<FontFaceId> monospace = engine.register_typeface(FontRole::monospace, 400, false, cousine);
    const std::optional<FontFaceId> cjk = engine.register_fallback_typeface(400, false, noto_cjk);
    const std::optional<FontFaceId> emoji = engine.register_fallback_typeface(400, false, noto_emoji);
    require(control && content && monospace && cjk && emoji,
            "resolution-record fixture pack must register completely");

    const std::string sample = "Report 日本語 · launch 🚀";
    const ResolvedTextLayout resolved = engine.resolve(
        sample, {FontRole::content, 12.0, 400, false});
    bool cjk_fallback = false;
    bool emoji_fallback = false;
    for (const ResolvedFontRun& run : resolved.runs) {
        if (!run.fallback) continue;
        if (run.family == "Noto Sans CJK JP") cjk_fallback = true;
        if (run.family == "Noto Emoji") emoji_fallback = true;
    }
    require(resolved.status == TextResolutionStatus::exact &&
                resolved.primary_family == "Carlito" &&
                resolved.logical_size.width > 0.0 && resolved.ascent > 0.0 &&
                resolved.descent >= 0.0 && resolved.missing_clusters == 0U &&
                cjk_fallback && emoji_fallback,
            "resolution record must name only faces selected by actual shaped runs");
    std::cout << std::fixed << std::setprecision(3)
              << "resolution corpus bytes=" << sample.size()
              << " width=" << resolved.logical_size.width
              << " ascent=" << resolved.ascent
              << " descent=" << resolved.descent
              << " runs=" << resolved.runs.size() << '\n';

    double prior_width{};
    for (const double scale : std::array{1.0, 1.25, 1.5, 2.0}) {
        const ResolvedTextLayout scaled = engine.resolve(
            sample, {FontRole::content, 12.0 * scale, 400, false});
        require(scaled.status == TextResolutionStatus::exact &&
                    scaled.primary_family == "Carlito" &&
                    scaled.logical_size.width > prior_width &&
                    scaled.runs.size() == resolved.runs.size(),
                "text scaling must preserve actual face/run selection while metrics grow");
        prior_width = scaled.logical_size.width;
        std::cout << "resolution scale=" << scale
                  << " width=" << scaled.logical_size.width
                  << " line=" << scaled.logical_size.height << '\n';
    }

    HarfBuzzFontEngine missing;
    require(missing.register_fallback_typeface(400, false, noto_cjk).has_value(),
            "missing-primary fixture fallback must register");
    const ResolvedTextLayout unavailable = missing.resolve(
        "No host substitution", {FontRole::content, 12.0, 400, false});
    require(unavailable.status == TextResolutionStatus::missing_primary_face &&
                unavailable.primary_family.empty() && unavailable.runs.empty(),
            "missing bundled role must remain unavailable in resolution diagnostics");
}

} // namespace

namespace {
void require_same_shape(const ShapedText& left, const ShapedText& right) {
    require(left.width == right.width && left.height == right.height &&
            left.ascent == right.ascent && left.descent == right.descent &&
            left.missing_clusters == right.missing_clusters &&
            left.missing_primary_face == right.missing_primary_face &&
            left.runs.size() == right.runs.size(), "shape summary unchanged");
    const std::size_t run_count = left.runs.size();
    for (std::size_t index = 0; index < run_count; ++index) {
        const ShapedFontRun& first = left.runs[index];
        const ShapedFontRun& second = right.runs[index];
        require(first.face == second.face &&
                first.source_range.start == second.source_range.start &&
                first.source_range.end == second.source_range.end &&
                first.glyphs == second.glyphs, "shape run unchanged");
#if defined(GUI_FORMS_TEXT_LAYOUT_GEOMETRY_TRACE)
        require(first.diagnostic_rtl == second.diagnostic_rtl, "shape direction unchanged");
#endif
    }
}

// A lazy fallback's loader, counting its calls. `throws` calls fail before the bytes are
// handed out.
struct CountingLoader final {
    std::shared_ptr<const std::vector<std::byte>> bytes;
    std::shared_ptr<int> calls;
    std::shared_ptr<int> throws;

    [[nodiscard]] LazyFaceBytes operator()() const {
        ++(*calls);
        if (*throws > 0) {
            --(*throws);
            throw std::runtime_error("lazy face read failed");
        }
        LazyFaceBytes result{};
        if (!bytes) return result;
        result.bytes = std::span<const std::byte>(*bytes);
        result.owner = bytes;
        return result;
    }
};

CountingLoader counting_loader(const char* path) {
    CountingLoader loader{};
    if (path != nullptr) loader.bytes = std::make_shared<const std::vector<std::byte>>(read_file(path));
    loader.calls = std::make_shared<int>(0);
    loader.throws = std::make_shared<int>(0);
    return loader;
}

std::span<const gui_forms::detail::CodepointRange> table(const char* file_name) {
    const std::optional<std::span<const gui_forms::detail::CodepointRange>> coverage =
        gui_forms::detail::bundled_face_coverage(file_name);
    require(coverage.has_value(), "bundled fallback has a coverage table");
    return *coverage;
}

void register_primaries(HarfBuzzFontEngine& engine, const std::vector<std::byte>& rapids,
                        const std::vector<std::byte>& carlito, const std::vector<std::byte>& cousine) {
    require(engine.register_typeface(FontRole::control, 400, false, rapids) &&
            engine.register_typeface(FontRole::content, 400, false, carlito) &&
            engine.register_typeface(FontRole::monospace, 400, false, cousine), "primaries register");
}

// A lazy fallback is read only when a cluster first needs it, once, and then selects and shapes
// exactly as the same face registered eagerly; one that cannot be read or opened covers nothing,
// and one whose read throws is read again by the next call.
void test_lazy_fallback_matches_eager_and_loads_once() {
    const std::vector<std::byte> rapids = read_file(GUI_FORMS_TEST_RAPIDS_REGULAR);
    const std::vector<std::byte> carlito = read_file(GUI_FORMS_TEST_CARLITO_REGULAR);
    const std::vector<std::byte> cousine = read_file(GUI_FORMS_TEST_COUSINE_REGULAR);
    const std::array<const char*, 4> paths{GUI_FORMS_TEST_NOTO_CJK_REGULAR, GUI_FORMS_TEST_NOTO_ARABIC_REGULAR,
                                           GUI_FORMS_TEST_NOTO_HEBREW_REGULAR, GUI_FORMS_TEST_NOTO_EMOJI};
    const std::array<const char*, 4> names{"NotoSansCJKjp-Regular.otf", "NotoSansArabic-Regular.ttf",
                                           "NotoSansHebrew-Regular.ttf", "NotoEmoji-Regular.ttf"};
    HarfBuzzFontEngine eager;
    HarfBuzzFontEngine lazy;
    register_primaries(eager, rapids, carlito, cousine);
    register_primaries(lazy, rapids, carlito, cousine);
    std::array<CountingLoader, 4> loaders{};
    for (std::size_t index = 0; index < paths.size(); ++index) {
        loaders[index] = counting_loader(paths[index]);
        require(eager.register_fallback_typeface(400, false, *loaders[index].bytes).has_value(),
                "eager fallback registers");
        require(lazy.register_lazy_fallback_typeface(400, false, table(names[index]), loaders[index]).has_value(),
                "lazy fallback registers");
    }
    require(lazy.face_count() == 7U && lazy.opened_face_count() == 3U && eager.opened_face_count() == 7U,
            "lazy fallbacks are registered unopened");
    require(!lazy.face_source(FontFaceId{4U}).has_value(), "an unopened face has no source");

    const FontSpec control{FontRole::control, 15.0, 400, false};
    require_same_shape(lazy.shape("Plain Latin text", control), eager.shape("Plain Latin text", control));
    require(*loaders[0].calls + *loaders[1].calls + *loaders[2].calls + *loaders[3].calls == 0,
            "text the primaries cover reads no fallback");
    require_same_shape(lazy.shape("A日本語Z", control), eager.shape("A日本語Z", control));
    require(*loaders[0].calls == 1 && *loaders[1].calls + *loaders[2].calls + *loaders[3].calls == 0 &&
            lazy.opened_face_count() == 4U, "CJK text reads only the CJK face");
    const std::optional<FontFaceSource> source = lazy.face_source(FontFaceId{4U});
    require(source && (*source).bytes.data() == (*loaders[0].bytes).data() &&
            (*source).bytes.size() == (*loaders[0].bytes).size(), "an opened face's source is its loaded bytes");

    const std::string sample = "A日本語🚀Z مرحبا שלום é ü";
    for (const FontRole role : std::array{FontRole::control, FontRole::content, FontRole::monospace}) {
        const FontSpec font{role, 15.0, 400, false};
        require_same_shape(lazy.shape(sample, font), eager.shape(sample, font));
        const ResolvedTextLayout lazy_resolved = lazy.resolve(sample, font);
        const ResolvedTextLayout eager_resolved = eager.resolve(sample, font);
        require(lazy_resolved.status == eager_resolved.status &&
                lazy_resolved.primary_family == eager_resolved.primary_family &&
                lazy_resolved.runs == eager_resolved.runs, "a lazy face resolves as the eager face does");
    }
    for (const CountingLoader& loader : loaders) require(*loader.calls == 1, "each fallback is read once");
    require(lazy.opened_face_count() == 7U, "every needed fallback opened");

    // Unreadable bytes, and bytes that are not a font, cover nothing and are not read again.
    HarfBuzzFontEngine primaries;
    register_primaries(primaries, rapids, carlito, cousine);
    for (const char* path : std::array<const char*, 2>{nullptr, GUI_FORMS_TEST_NOTO_EMOJI}) {
        HarfBuzzFontEngine damaged;
        register_primaries(damaged, rapids, carlito, cousine);
        CountingLoader loader = counting_loader(path);
        if (path != nullptr) {
            std::vector<std::byte> garbage(*loader.bytes);
            std::fill(garbage.begin(), garbage.begin() + 64, std::byte{0x5A});
            loader.bytes = std::make_shared<const std::vector<std::byte>>(std::move(garbage));
        }
        require(damaged.register_lazy_fallback_typeface(400, false, table(names[0]), loader).has_value(),
                "damaged lazy fallback registers");
        require_same_shape(damaged.shape("日本", control), primaries.shape("日本", control));
        require_same_shape(damaged.shape("日本", control), primaries.shape("日本", control));
        require(*loader.calls == 1 && damaged.opened_face_count() == 3U,
                "a face that cannot be opened is read once and covers nothing");
    }

    // A read that throws fails the call and leaves the face to be read by the next one.
    HarfBuzzFontEngine retried;
    register_primaries(retried, rapids, carlito, cousine);
    CountingLoader flaky = counting_loader(GUI_FORMS_TEST_NOTO_CJK_REGULAR);
    *flaky.throws = 1;
    require(retried.register_lazy_fallback_typeface(400, false, table(names[0]), flaky).has_value(),
            "retried lazy fallback registers");
    bool threw = false;
    try {
        static_cast<void>(retried.shape("日本", control));
    } catch (const std::runtime_error&) {
        threw = true;
    }
    require(threw && retried.opened_face_count() == 3U, "a throwing read fails the call");
    require_same_shape(retried.shape("A日本語Z", control), eager.shape("A日本語Z", control));
    require(*flaky.calls == 2 && retried.opened_face_count() == 4U, "the next call reads the face again");

    require(!lazy.register_lazy_fallback_typeface(400, false, {}, loaders[0]).has_value() &&
            !lazy.register_lazy_fallback_typeface(400, false, table(names[0]), LazyFaceLoader{}).has_value(),
            "a lazy face needs coverage and a loader");
}

// A numbered bundled family (theme-language `font-family: Selawik`): its faces are read only
// when text names the family, then draw before every role face, by weight, with the role's
// faces and the fallbacks still covering what the family lacks. Text that names no family,
// or another family, never reaches them; a family face that cannot be opened leaves the
// role's faces in charge.
void test_named_family_draws_first_and_loads_on_demand() {
    const std::vector<std::byte> rapids = read_file(GUI_FORMS_TEST_RAPIDS_REGULAR);
    const std::vector<std::byte> carlito = read_file(GUI_FORMS_TEST_CARLITO_REGULAR);
    const std::vector<std::byte> cousine = read_file(GUI_FORMS_TEST_COUSINE_REGULAR);
    HarfBuzzFontEngine engine;
    register_primaries(engine, rapids, carlito, cousine);
    require(engine.register_fallback_typeface(400, false, read_file(GUI_FORMS_TEST_NOTO_CJK_REGULAR)).has_value(),
            "CJK fallback registers");
    const BundledFontFamily selawik{1U};
    CountingLoader regular = counting_loader(GUI_FORMS_TEST_SELAWIK_REGULAR);
    CountingLoader bold = counting_loader(GUI_FORMS_TEST_SELAWIK_BOLD);
    const std::optional<FontFaceId> regular_id = engine.register_lazy_family_typeface(selawik, 400, false, regular);
    require(regular_id.has_value() && engine.register_lazy_family_typeface(selawik, 700, false, bold).has_value(),
            "family faces register");
    require(engine.face_count() == 6U && engine.opened_face_count() == 4U, "family faces are registered unopened");

    const FontSpec content{FontRole::content, 12.0, 400, false};
    const ResolvedTextLayout plain = engine.resolve("Plain text", content);
    require(plain.status == TextResolutionStatus::exact && plain.primary_family == "Carlito",
            "text that names no family draws the role's face");
    FontSpec other = content;
    other.family = BundledFontFamily{2U};
    require(engine.resolve("Plain text", other).primary_family == "Carlito", "another family draws the role's face");
    require(*regular.calls + *bold.calls == 0, "no family face is read for text that does not name it");

    FontSpec named = content;
    named.family = selawik;
    const ResolvedTextLayout drawn = engine.resolve("Plain text", named);
    require(drawn.status == TextResolutionStatus::exact && drawn.primary_family == "Selawik",
            "text that names the family draws its face");
    require(!drawn.runs.empty() && drawn.runs.front().family == "Selawik" && !drawn.runs.front().fallback &&
                drawn.runs.front().registered_weight == 400,
            "the family's run is its regular face and not a fallback");
    require(*regular.calls == 1 && *bold.calls == 1 && engine.opened_face_count() == 6U,
            "the family's faces are read once, when first named");
    FontSpec named_bold = named;
    named_bold.weight = 700;
    const ResolvedTextLayout heavy = engine.resolve("Bold", named_bold);
    require(!heavy.runs.empty() && heavy.runs.front().family == "Selawik" &&
                heavy.runs.front().registered_weight == 700,
            "a bold request draws the family's bold face");
    const ResolvedTextLayout mixed = engine.resolve("A日", named);
    require(mixed.runs.size() == 2U && mixed.runs[0].family == "Selawik" && mixed.runs[1].family != "Selawik" &&
                mixed.runs[1].fallback,
            "the fallbacks cover what the family lacks");
    const std::optional<PrimaryLineMetrics> metrics = engine.primary_line_metrics(named);
    require(metrics && regular_id && (*metrics).face == *regular_id, "the family's face gives the primary line metrics");
    require(*regular.calls == 1 && *bold.calls == 1, "an opened family face is not read again");

    HarfBuzzFontEngine unreadable;
    register_primaries(unreadable, rapids, carlito, cousine);
    CountingLoader missing = counting_loader(nullptr);
    require(unreadable.register_lazy_family_typeface(selawik, 400, false, missing).has_value(),
            "an unreadable family face registers");
    const ResolvedTextLayout instead = unreadable.resolve("Plain text", named);
    require(instead.status == TextResolutionStatus::exact && instead.primary_family == "Carlito",
            "a family face that cannot be opened leaves the role's face in charge");
    static_cast<void>(unreadable.resolve("Plain text", named));
    require(*missing.calls == 1, "a family face that cannot be opened is read once");

    require(!engine.register_lazy_family_typeface(BundledFontFamily{}, 400, false, regular).has_value() &&
                !engine.register_lazy_family_typeface(selawik, 400, false, LazyFaceLoader{}).has_value(),
            "a family face needs a family and a loader");
}

#if defined(GUI_FORMS_TEXT_LAYOUT_DIAGNOSTICS)
void test_call_failure_preserves_caller_and_recovers() {
    HarfBuzzFontEngine engine{};
    const std::vector<std::byte> primary = read_file(GUI_FORMS_TEST_CARLITO_REGULAR);
    const std::vector<std::byte> fallback = read_file(GUI_FORMS_TEST_NOTO_HEBREW_REGULAR);
    const std::optional<FontFaceId> primary_id =
        engine.register_typeface(FontRole::content, 400, false, primary);
    require(primary_id.has_value(), "failure primary registers");
    const std::optional<FontFaceId> fallback_id =
        engine.register_fallback_typeface(400, false, fallback);
    require(fallback_id.has_value(), "failure fallback registers");
    constexpr FontSpec font{FontRole::content, 16.0, 400, false};
    const std::string input = "abc שלום def";
    const ShapedText expected = engine.shape(input, font);
    require(expected.runs.size() >= 3, "fixture exercises partial result before failure");
    ShapedText published = engine.shape(input, font);
    struct FailureCase final {
        TextLayoutFailure point{};
        std::uint64_t run{};
        bool allocation_failure{};
    };
    constexpr std::array<FailureCase, 6> failures{
        FailureCase{TextLayoutFailure::empty_font, 1, true},
        FailureCase{TextLayoutFailure::unbound_font, 1, true},
        FailureCase{TextLayoutFailure::empty_buffer, 1, true},
        FailureCase{TextLayoutFailure::after_add, 2, true},
        FailureCase{TextLayoutFailure::after_shape, 2, true},
        FailureCase{TextLayoutFailure::no_shaper, 2, false}};
    const std::size_t failure_count = failures.size();
    for (std::size_t index = 0; index < failure_count; ++index) {
        const FailureCase failure = failures[index];
        // Acquisition failures use run one; later-stage failures unwind after
        // at least one complete run has entered the call-owned partial result.
        engine.set_diagnostic_failure(failure.point, failure.run);
        bool rejected = false;
        try {
            ShapedText replacement = engine.shape(input, font);
            published = std::move(replacement);
        } catch (const std::bad_alloc&) {
            rejected = failure.allocation_failure;
        } catch (const std::runtime_error&) {
            rejected = !failure.allocation_failure;
        }
        require(rejected, "native acquisition/failure must not return partial success");
        const TextLayoutDiagnostics diagnostics = engine.diagnostics();
        require(diagnostics.append_calls == failure.run, "failure occurred at requested run");
        require_same_shape(published, expected);
        const ShapedText recovered = engine.shape(input, font);
        require_same_shape(recovered, expected);
    }
}
#endif
} // namespace

namespace {
void test_canonical_pair_coverage_preserves_source() {
    HarfBuzzFontEngine engine{};
    const std::vector<std::byte> bytes = read_file(GUI_FORMS_TEST_CARLITO_REGULAR);
    const std::optional<FontFaceId> registered =
        engine.register_typeface(FontRole::content, 400, false, bytes);
    require(registered.has_value(), "canonical pair primary registers");
    constexpr FontSpec font{FontRole::content, 20.0, 400, false};
    const ShapedText decomposed = engine.shape("a\xcc\x81", font);
    const ShapedText composed = engine.shape("\xc3\xa1", font);
    require(decomposed.missing_clusters == 0 && composed.missing_clusters == 0,
            "canonical pair uses composed glyph coverage");
    require(decomposed.runs.size() == 1 && composed.runs.size() == 1,
            "canonical equivalents use one run");
    const ShapedFontRun& first = decomposed.runs[0];
    const ShapedFontRun& second = composed.runs[0];
    require(first.face == *registered && second.face == *registered &&
            first.glyphs.size() == 1 && first.glyphs == second.glyphs,
            "canonical equivalents have identical glyph identity and geometry");
    require(first.glyphs[0].glyph.value == 1955 && first.glyphs[0].cluster == Utf8Offset(0),
            "pinned Carlito composition and original cluster");
    require(first.source_range.start == Utf8Offset(0) && first.source_range.end == Utf8Offset(3) &&
            second.source_range.start == Utf8Offset(0) && second.source_range.end == Utf8Offset(2),
            "different original source byte extents remain authoritative");
    require(decomposed.width == composed.width && decomposed.height == composed.height &&
            decomposed.ascent == composed.ascent && decomposed.descent == composed.descent,
            "canonical equivalents have identical metrics");
    struct MissingCase final { std::string_view input{}; const char* description{}; };
    constexpr std::array<MissingCase, 3> missing_inputs{
        MissingCase{"\xcc\x81", "isolated mark remains reported"},
        MissingCase{"q\xcc\x81", "noncomposable q plus acute remains reported"},
        MissingCase{"a\xcc\x81\xcc\x81", "longer unsupported cluster remains reported"}};
    for (const MissingCase& test : missing_inputs) {
        const ShapedText missing = engine.shape(test.input, font);
        std::cout << "coverage_case," << test.description << ',' << missing.missing_clusters << '\n';
        require(missing.missing_clusters == 1, test.description);
    }
}
} // namespace

// ADR-004: advances are unhinted outline advances at the logical size, so a
// line's width is linear in the font size. Each glyph's advance is rounded to
// 1/64 px at each size; hinted advances missed linearity by several pixels.
void test_advances_are_unhinted_and_scale_linearly() {
    const std::string corpus = "Hamburgefonstiv quick brown fox 0123456789 Help";
    const std::array<const char*, 2> fonts{GUI_FORMS_TEST_RAPIDS_REGULAR, GUI_FORMS_TEST_CARLITO_REGULAR};
    for (const char* font : fonts) {
        HarfBuzzFontEngine engine;
        require(engine.register_typeface(FontRole::control, 400, false, read_file(font)).has_value(),
                "linearity font registers");
        const std::array<double, 3> sizes{9.0, 11.0, 13.0};
        for (const double size : sizes) {
            const ShapedText single = engine.shape(corpus, {FontRole::control, size, 400, false});
            const ShapedText doubled = engine.shape(corpus, {FontRole::control, size * 2.0, 400, false});
            std::size_t glyphs = 0;
            for (const ShapedFontRun& run : single.runs) glyphs += run.glyphs.size();
            require(glyphs > 40U, "linearity corpus shapes every glyph");
            const double tolerance = static_cast<double>(glyphs) * 1.5 / 64.0;
            require(near(doubled.width, single.width * 2.0, tolerance),
                    "line width doubles with the font size (unhinted advances)");
        }
    }
}

int main() {
    try {
        test_advances_are_unhinted_and_scale_linearly();
        test_canonical_pair_coverage_preserves_source();
#if defined(GUI_FORMS_TEXT_LAYOUT_DIAGNOSTICS)
        test_call_failure_preserves_caller_and_recovers();
#endif
        test_shared_font_lifetime();
        test_owned_font_view_lifetime();
        test_mixed_direction_text();
        test_owned_registration_style_selection_and_ligatures();
        test_cluster_fallback_is_bounded_and_absolute();
        test_control_navigation_symbols_stay_in_rapids();
        test_metrics_scale_and_repeat_deterministically();
        test_shared_cjk_and_emoji_fallback_across_roles();
        test_lazy_fallback_matches_eager_and_loads_once();
        test_named_family_draws_first_and_loads_on_demand();
        test_letter_spacing_is_a_shaped_layout_input();
        test_rejection_and_missing_role_are_honest();
        test_public_resolution_record_names_actual_bundled_runs();
        std::cout << "harfbuzz font engine tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "harfbuzz font engine tests failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
