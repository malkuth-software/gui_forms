#include "harfbuzz_font_engine.hpp"

#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
using namespace gui_forms;
using namespace gui_forms::render::text;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void load_fonts(HarfBuzzFontEngine& engine, const std::filesystem::path& directory) {
    const std::array<const char*, 4> names{"Carlito-Regular.ttf", "NotoSansArabic-Minimal.ttf",
        "NotoSansHebrew-Regular.ttf", "NotoEmoji-Regular.ttf"};
    for (std::size_t index = 0; index < names.size(); ++index) {
        const std::filesystem::path path = directory / names[index];
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        require(static_cast<bool>(input), "pinned font fixture opens");
        const std::streamsize size = input.tellg();
        require(size > 0 && size <= 4 * 1024 * 1024, "fixture font size admitted");
        std::vector<std::byte> bytes(static_cast<std::size_t>(size));
        input.seekg(0);
        input.read(reinterpret_cast<char*>(bytes.data()), size);
        require(static_cast<bool>(input), "pinned font fixture read");
        std::optional<FontFaceId> registered{};
        if (index == 0) registered = engine.register_typeface(FontRole::content, 400, false, bytes);
        else registered = engine.register_fallback_typeface(400, false, bytes);
        require(registered.has_value(), "fixture face registration");
    }
}

void compare_geometry(const ShapedText& expected, const BoundedShapedText& actual) {
    require(expected.width == actual.width && expected.height == actual.height &&
        expected.ascent == actual.ascent && expected.descent == actual.descent,
        "bounded shaping metrics differ");
    require(expected.missing_clusters == actual.missing_clusters &&
        expected.missing_primary_face == actual.missing_primary_face, "coverage differs");
    require(expected.runs.size() == actual.run_count, "run count differs");
    std::size_t glyph_count = 0;
    for (std::size_t index = 0; index < actual.run_count; ++index) {
        const ShapedFontRun& old_run = expected.runs[index];
        const BoundedFontRun& run = actual.runs[index];
        require(old_run.face == run.face && old_run.source_range == run.source_range,
                "font identity or original source range differs");
        require(run.glyph_begin == glyph_count && run.glyph_count == old_run.glyphs.size(),
                "flat glyph coverage differs");
        for (std::size_t glyph_index = 0; glyph_index < run.glyph_count; ++glyph_index) {
            require(old_run.glyphs[glyph_index] == actual.glyphs[glyph_count + glyph_index],
                    "bounded glyph geometry differs");
        }
        glyph_count += run.glyph_count;
    }
    require(glyph_count == actual.glyph_count, "flat glyph count differs");
}

void test_geometry(HarfBuzzFontEngine& engine) {
    const std::array<std::string_view, 9> texts{"", "   ", "office AV 123", "a\xcc\x81",
        "שלום ABC 123", "اللون 123 (RGB)", "Hello 😀!", "a\r\nb\tc",
        "Latin العربية שלום á 😀"};
    const std::array<FontSpec, 3> fonts{{
        {FontRole::content, 16, 400, false, 0},
        {FontRole::content, 11.875, 400, false, 0.375},
        {FontRole::content, 32, 400, false, -0.25}}};
    for (std::size_t font_index = 0; font_index < fonts.size(); ++font_index) {
        for (std::size_t text_index = 0; text_index < texts.size(); ++text_index) {
            const ShapedText expected = engine.shape(texts[text_index], fonts[font_index]);
            const std::unique_ptr<BoundedShapedText> actual = engine.shape_bounded(texts[text_index], fonts[font_index], {});
            compare_geometry(expected, *actual);
        }
    }
    std::string marks("a");
    marks.reserve(16'383);
    for (std::size_t index = 0; index < 8191; ++index) marks.append("\xcc\x81");
    const ShapedText expected = engine.shape(marks, fonts[0]);
    const std::unique_ptr<BoundedShapedText> actual = engine.shape_bounded(marks, fonts[0], {});
    compare_geometry(expected, *actual);
    const std::unique_ptr<BoundedShapedText> horizontal = engine.shape_bounded("abc", fonts[0], {});
    for (std::size_t index = 0; index < (*horizontal).glyph_count; ++index) {
        const float advance_y = (*horizontal).glyphs[index].advance_y;
        require(advance_y == 0.0F && !std::signbit(advance_y), "horizontal zero advance preserves positive sign");
    }
}

void expect_budget_refusal(HarfBuzzFontEngine& engine, std::unique_ptr<BoundedShapedText>& old,
                           std::string_view text, ShapeStorageLimits limits) {
    const BoundedShapedText* previous = old.get();
    bool refused = false;
    try {
        old = engine.shape_bounded(text, {FontRole::content, 16, 400, false}, limits);
    } catch (const std::length_error&) {
        refused = true;
    }
    require(refused && old.get() == previous, "budget refusal must preserve caller's old owner");
}

void test_budgets(HarfBuzzFontEngine& engine) {
    const std::string_view text = "hello שלום";
    std::unique_ptr<BoundedShapedText> old = engine.shape_bounded(text, {FontRole::content, 16, 400, false}, {});
    ShapeStorageLimits limits{};
    limits.output_bytes = (*old).controlled_output_bytes - 1;
    expect_budget_refusal(engine, old, text, limits);
    limits = {};
    limits.workspace_bytes = (*old).controlled_workspace_peak - 1;
    expect_budget_refusal(engine, old, text, limits);
    limits = {};
    limits.input_bytes = text.size() - 1;
    expect_budget_refusal(engine, old, text, limits);
    limits = {};
    limits.runs = 1;
    expect_budget_refusal(engine, old, text, limits);
    limits = {};
    limits.glyphs = 1;
    expect_budget_refusal(engine, old, text, limits);
    limits = {};
    limits.runs = std::numeric_limits<std::size_t>::max();
    expect_budget_refusal(engine, old, text, limits);
    limits = {};
    limits.output_bytes = (*old).controlled_output_bytes;
    limits.workspace_bytes = (*old).controlled_workspace_peak;
    const std::unique_ptr<BoundedShapedText> exact = engine.shape_bounded(text, {FontRole::content, 16, 400, false}, limits);
    require((*exact).glyph_count == (*old).glyph_count, "exact storage limits accepted");
    const BoundedShapedText* previous = old.get();
    bool invalid = false;
    try {
        old = engine.shape_bounded("\x80", {FontRole::content, 16, 400, false}, {});
    } catch (const std::invalid_argument&) {
        invalid = true;
    }
    require(invalid && old.get() == previous, "invalid UTF-8 preserves caller owner");
}
} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "expected pinned font directory");
        HarfBuzzFontEngine engine{};
        const std::filesystem::path directory(argv[1]);
        load_fonts(engine, directory);
        test_geometry(engine);
        test_budgets(engine);
        std::cout << "Bounded shaping exact geometry and controlled storage fixtures passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
