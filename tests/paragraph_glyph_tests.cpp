// Paragraph glyph runs through the bundled engine and SkiaRaster
// (include/gui_forms/text/paragraph_layout/paragraph_layout.hpp,
// ParagraphGlyphOutput::runs). Three properties:
// - Painting a paragraph from its glyph runs (Painter::draw_glyph_runs)
//   gives the same pixels, byte for byte, as painting it from its text,
//   which reshapes each line: for Latin, Hebrew and Arabic bidi and emoji
//   text, in both paragraph directions, at several widths.
// - Glyph runs laid out by another raster's engine are not drawn as glyphs:
//   the raster draws their text, with the same pixels as its own text paint.
// - Once the buffers and the workspace hold the rows' sizes and the faces
//   the rows need are open (one warm-up pass), laying out 1000 paragraphs
//   with glyph output allocates nothing; see test_layout_allocates_nothing
//   for exactly what is counted.
// The faces are the bundled Carlito, Noto Emoji, Noto Sans Hebrew and Noto
// Sans Arabic, read from the assets/fonts directory given as argument 1.

#include "gui_forms/allocation/statistics/allocation_statistics.hpp"
#include "gui_forms/text/paragraph_layout/paragraph_layout.hpp"
#include "gui_forms/types/damage_region/damage_region.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "harfbuzz_font_engine.hpp"
#include "skia_raster.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// Counts the calling thread's global operator new calls while armed.
namespace {
thread_local bool counting_allocations = false;
thread_local std::uint64_t counted_allocations = 0U;
} // namespace

void* operator new(std::size_t size) {
    if (counting_allocations) ++counted_allocations;
    void* allocation = std::malloc(size == 0U ? 1U : size);
    if (allocation == nullptr) throw std::bad_alloc();
    return allocation;
}

void* operator new[](std::size_t size) {
    if (counting_allocations) ++counted_allocations;
    void* allocation = std::malloc(size == 0U ? 1U : size);
    if (allocation == nullptr) throw std::bad_alloc();
    return allocation;
}

void operator delete(void* allocation) noexcept {
    std::free(allocation);
}

void operator delete[](void* allocation) noexcept {
    std::free(allocation);
}

void operator delete(void* allocation, std::size_t) noexcept {
    std::free(allocation);
}

void operator delete[](void* allocation, std::size_t) noexcept {
    std::free(allocation);
}

namespace {

using namespace gui_forms;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

const std::string family = "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7";
const std::string thumbs = "\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD";
const std::string heart = "\xE2\x9D\xA4\xEF\xB8\x8F";
// "shalom" in Hebrew, "marhaban" in Arabic.
const std::string hebrew = "\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D";
const std::string arabic = "\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7";

// The texts every test lays out: Latin with kerning and ligature pairs,
// right-to-left words and numbers among left-to-right ones, emoji sequences
// (ZWJ family, skin tone modifier, variation selector), and a hard line end.
std::vector<std::string> corpus() {
    std::vector<std::string> texts{};
    texts.push_back("The quick brown fox jumps over the lazy dog; office affine waffle AVATAR To.");
    texts.push_back("Mixed " + hebrew + " 123 and " + arabic + " " + hebrew + " words, then English again.");
    texts.push_back(hebrew + " " + hebrew + " " + arabic + " 2026 " + arabic + " " + hebrew + " " + arabic);
    texts.push_back("Emoji " + family + " and " + thumbs + " then " + heart + " in a row " + family + thumbs);
    texts.push_back("Line one " + thumbs + "\n" + hebrew + " line two " + arabic + "\nline three");
    return texts;
}

FontSpec content_font() {
    const FontSpec font{FontRole::content, 13.0, 400, false, 0.0};
    return font;
}

void register_faces(render::SkiaRaster& raster, const std::filesystem::path& fonts) {
    const std::string carlito = (fonts / "Carlito-Regular.ttf").string();
    const std::string emoji = (fonts / "NotoEmoji-Regular.ttf").string();
    const std::string hebrew_face = (fonts / "NotoSansHebrew-Regular.ttf").string();
    const std::string arabic_face = (fonts / "NotoSansArabic-Regular.ttf").string();
    require(raster.register_typeface_file(FontRole::content, 400, false, carlito.c_str()), "Carlito registers");
    require(raster.register_fallback_typeface_file(400, false, emoji.c_str()), "Noto Emoji registers");
    require(raster.register_fallback_typeface_file(400, false, hebrew_face.c_str()), "Noto Sans Hebrew registers");
    require(raster.register_fallback_typeface_file(400, false, arabic_face.c_str()), "Noto Sans Arabic registers");
}

constexpr Size canvas_size{260.0, 220.0};

ParagraphStyle style_for(render::SkiaRaster& raster, const double width, const TextDirection direction) {
    ParagraphStyle style{};
    style.font = content_font();
    style.line_box = paragraph_line_box(&raster, style.font, std::nullopt);
    style.width = width;
    style.direction = direction;
    style.glyph_output = ParagraphGlyphOutput::runs;
    return style;
}

ParagraphPaint paint_for(const double width) {
    ParagraphPaint paint{};
    paint.origin = Point{6.0, 4.0};
    paint.color = Color::rgba(20, 24, 40, 255);
    paint.visible = Rect{0.0, 0.0, width, canvas_size.height};
    return paint;
}

// Starts a frame over the whole canvas, cleared to one opaque colour.
void begin_clear_frame(render::SkiaRaster& raster) {
    DamageRegion damage{};
    damage.add(Rect{0.0, 0.0, canvas_size.width, canvas_size.height});
    raster.begin_frame(damage);
    raster.fill_rect(Rect{0.0, 0.0, canvas_size.width, canvas_size.height}, Color::rgba(250, 248, 240, 255));
}

[[nodiscard]] std::vector<std::byte> end_and_snapshot(render::SkiaRaster& raster) {
    raster.end_frame();
    const std::byte* const bytes = static_cast<const std::byte*>(raster.pixels());
    require(bytes != nullptr, "raster readable");
    const std::size_t size = raster.row_bytes() * static_cast<std::size_t>(raster.pixel_height());
    std::vector<std::byte> copy(bytes, bytes + size);
    return copy;
}

// The paragraph painted from its text (each line reshaped by
// draw_text_utf8) and from its glyph runs, on `painter`.
struct PaintedPair final {
    std::vector<std::byte> from_text{};
    std::vector<std::byte> from_glyphs{};
    std::size_t lines_drawn{};
};

PaintedPair paint_both(render::SkiaRaster& painter, std::string_view text, std::span<const ParagraphLine> lines,
                       const ParagraphGlyphRuns& glyphs, const ParagraphStyle& style) {
    const ParagraphPaint paint = paint_for(style.width);
    PaintedPair pair{};
    begin_clear_frame(painter);
    pair.lines_drawn = paint_paragraph(painter, text, lines, style, paint);
    pair.from_text = end_and_snapshot(painter);
    begin_clear_frame(painter);
    const std::size_t glyph_lines = paint_paragraph(painter, text, lines, glyphs, style, paint);
    pair.from_glyphs = end_and_snapshot(painter);
    require(glyph_lines == pair.lines_drawn, "the same lines are drawn either way");
    return pair;
}

// Pixel equality of glyph-run painting with reshaped painting, on the
// raster that laid the paragraph out; and on another raster with the same
// faces, which draws the runs as text.
void test_glyph_runs_paint_like_text(render::SkiaRaster& raster, render::SkiaRaster& other) {
    const std::array<double, 3> widths{90.0, 150.0, 240.0};
    const std::array<TextDirection, 2> directions{TextDirection::left_to_right, TextDirection::right_to_left};
    ParagraphBuffers buffers{};
    std::size_t compared = 0U;
    std::size_t right_to_left_runs = 0U;
    for (const std::string& text : corpus()) {
        buffers.reserve(
            paragraph_storage_bound(text, ParagraphWhiteSpace::preserve_line_ends, ParagraphGlyphOutput::runs),
            &raster);
        for (const double width : widths) {
            for (const TextDirection direction : directions) {
                const std::string what = text + " at " + std::to_string(width) +
                                         (direction == TextDirection::right_to_left ? " rtl" : " ltr");
                const ParagraphStyle style = style_for(raster, width, direction);
                const ParagraphLayoutResult result = lay_out_paragraph(&raster, text, style, buffers.storage());
                require(result.succeeded(), what + ": lays out");
                const ParagraphMetrics metrics = *result.metrics();
                require(metrics.glyph_source.valid() && metrics.glyph_count != 0U, what + ": glyphs are reported");
                const std::span<const ParagraphLine> lines = buffers.lines(metrics.line_count);
                const ParagraphGlyphRuns glyphs = buffers.glyph_runs(metrics);
                for (const GlyphRunRecord& run : glyphs.runs) {
                    if (run.direction == TextDirection::right_to_left) ++right_to_left_runs;
                }

                const PaintedPair own = paint_both(raster, text, lines, glyphs, style);
                require(own.lines_drawn != 0U, what + ": lines are drawn");
                require(own.from_glyphs == own.from_text, what + ": glyph runs paint the reshaped pixels");

                const PaintedPair foreign = paint_both(other, text, lines, glyphs, style);
                require(foreign.from_glyphs == foreign.from_text,
                        what + ": another raster draws the runs as their text");
                require(foreign.from_text == own.from_text, what + ": the two rasters agree");
                ++compared;
            }
        }
    }
    require(compared == corpus().size() * widths.size() * directions.size(), "every case is compared");
    require(right_to_left_runs != 0U, "the corpus shapes right-to-left runs");
}

// Lays out 1000 paragraphs (the corpus in turn, at three widths and both
// directions) with glyph output into buffers reserved for the corpus, after
// one warm-up pass over every case. Counted over the 1000, on this thread:
// - global operator new and operator new[] (replaced above): every C++
//   allocation, the library's and the standard library's;
// - HarfBuzzFontEngine::thread_native_allocations(): FreeType's heap calls
//   through the engine's FT_Memory, HarfBuzz's through its hb_malloc_impl
//   hooks, and SheenBidi's calls that went to the C heap (all must stay
//   zero), and SheenBidi's calls served by the workspace's arena (reported,
//   not an allocation: the arena is reserved storage);
// - AllocationScope: the counted text_layout subsystem (the buffers' and
//   workspace's arrays), which must not change.
// Not counted: Skia (layout does not reach it) and other threads (none run).
void test_layout_allocates_nothing(render::SkiaRaster& raster) {
    const std::vector<std::string> texts = corpus();
    const std::array<double, 3> widths{90.0, 150.0, 240.0};
    const std::array<TextDirection, 2> directions{TextDirection::left_to_right, TextDirection::right_to_left};
    std::vector<ParagraphStyle> styles{};
    for (const double width : widths) {
        for (const TextDirection direction : directions) styles.push_back(style_for(raster, width, direction));
    }
    ParagraphBuffers buffers{};
    for (const std::string& text : texts) {
        buffers.reserve(
            paragraph_storage_bound(text, ParagraphWhiteSpace::preserve_line_ends, ParagraphGlyphOutput::runs),
            &raster);
    }
    const ParagraphStorage storage = buffers.storage();
    for (const std::string& text : texts) {
        for (const ParagraphStyle& style : styles) {
            const ParagraphLayoutResult warm = lay_out_paragraph(&raster, text, style, storage);
            require(warm.succeeded() && (*warm.metrics()).glyph_source.valid(), text + ": warm-up lays out");
        }
    }

    std::size_t failures = 0U;
    std::size_t glyph_count = 0U;
    using render::text::HarfBuzzFontEngine;
    using render::text::NativeAllocationCounts;
    const NativeAllocationCounts native_before = HarfBuzzFontEngine::thread_native_allocations();
    const AllocationScope counted_paths{};
    counted_allocations = 0U;
    counting_allocations = true;
    for (std::size_t index = 0U; index < 1000U; ++index) {
        const std::string& text = texts[index % texts.size()];
        const ParagraphStyle& style = styles[(index / texts.size()) % styles.size()];
        const ParagraphLayoutResult result = lay_out_paragraph(&raster, text, style, storage);
        const ParagraphMetrics* const metrics = result.metrics();
        if (metrics == nullptr || !(*metrics).glyph_source.valid()) {
            ++failures;
            continue;
        }
        glyph_count += (*metrics).glyph_count;
    }
    counting_allocations = false;
    const std::uint64_t operator_new_calls = counted_allocations;
    const AllocationCounts text_layout = counted_paths.counted().of(AllocationSubsystem::text_layout);
    const NativeAllocationCounts native_after = HarfBuzzFontEngine::thread_native_allocations();

    require(failures == 0U, "every paragraph lays out with glyphs");
    require(glyph_count != 0U, "glyphs are written");
    require(operator_new_calls == 0U, "no operator new; counted " + std::to_string(operator_new_calls));
    require(text_layout.allocations == 0U && text_layout.releases == 0U, "the text_layout arrays are not touched");
    require(native_after.freetype == native_before.freetype,
            "no FreeType allocation; counted " + std::to_string(native_after.freetype - native_before.freetype));
    require(native_after.harfbuzz == native_before.harfbuzz,
            "no HarfBuzz allocation; counted " + std::to_string(native_after.harfbuzz - native_before.harfbuzz));
    require(native_after.sheenbidi_heap == native_before.sheenbidi_heap,
            "SheenBidi stays in the workspace's arena; heap calls " +
                std::to_string(native_after.sheenbidi_heap - native_before.sheenbidi_heap));
    require(native_after.sheenbidi_arena > native_before.sheenbidi_arena, "SheenBidi ran from the arena");
}

} // namespace

int main(int argument_count, char** arguments) {
    try {
        require(argument_count == 2, "usage: gui_forms_paragraph_glyph_tests <assets/fonts>");
        const std::filesystem::path fonts(arguments[1]);
        render::SkiaRaster raster{};
        render::SkiaRaster other{};
        require(raster.resize(canvas_size, 1.0) && other.resize(canvas_size, 1.0), "rasters allocated");
        register_faces(raster, fonts);
        register_faces(other, fonts);
        test_glyph_runs_paint_like_text(raster, other);
        test_layout_allocates_nothing(raster);
        std::cout << "paragraph-glyph-tests: pass\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "paragraph-glyph-tests: fail: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
