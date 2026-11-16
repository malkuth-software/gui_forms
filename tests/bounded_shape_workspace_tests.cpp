#include "harfbuzz_font_engine.hpp"
#include "bounded_shape_workspace.hpp"
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>

namespace allocation_probe {
bool enabled = false;
std::size_t calls = 0U;
std::size_t fail_at = 0U;

void* allocate(const std::size_t bytes) {
    if (enabled) {
        ++calls;
        if (calls == fail_at) throw std::bad_alloc();
    }
    const std::size_t requested = bytes == 0U ? 1U : bytes;
    void* result = std::malloc(requested);
    if (result == nullptr) throw std::bad_alloc();
    return result;
}
}

void* operator new(const std::size_t bytes) {
    void* result = allocation_probe::allocate(bytes);
    return result;
}
void* operator new[](const std::size_t bytes) {
    void* result = allocation_probe::allocate(bytes);
    return result;
}
void operator delete(void* address) noexcept { std::free(address); }
void operator delete[](void* address) noexcept { std::free(address); }
void operator delete(void* address, std::size_t) noexcept { std::free(address); }
void operator delete[](void* address, std::size_t) noexcept { std::free(address); }

namespace {
using namespace gui_forms;
using namespace gui_forms::render::text;
void require(const bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void load_fonts(HarfBuzzFontEngine& engine, const std::filesystem::path& directory) {
    const std::array<const char*, 4> names{"Carlito-Regular.ttf", "NotoSansArabic-Minimal.ttf",
        "NotoSansHebrew-Regular.ttf", "NotoEmoji-Regular.ttf"};
    for (std::size_t index = 0; index < names.size(); ++index) {
        const std::filesystem::path path = directory / names[index];
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        require(static_cast<bool>(input), "pinned font fixture opens");
        const std::streampos end_position = input.tellg();
        const std::streamoff end_offset = static_cast<std::streamoff>(end_position);
        const std::streamsize size = static_cast<std::streamsize>(end_offset);
        require(size > 0 && size <= 4 * 1024 * 1024, "fixture font size admitted");
        const std::size_t byte_count = static_cast<std::size_t>(size);
        std::vector<std::byte> bytes(byte_count);
        input.seekg(0);
        char* destination = reinterpret_cast<char*>(bytes.data());
        input.read(destination, size);
        require(static_cast<bool>(input), "pinned font fixture read");
        std::optional<FontFaceId> registered{};
        if (index == 0) registered = engine.register_typeface(FontRole::content, 400, false, bytes);
        else registered = engine.register_fallback_typeface(400, false, bytes);
        require(registered.has_value(), "fixture face registration");
    }
}

void compare(const BoundedShapedText& expected, const BoundedShapedText& actual) {
    require(expected.width == actual.width && expected.height == actual.height &&
        expected.ascent == actual.ascent && expected.descent == actual.descent,
        "workspace metrics match bounded control");
    require(expected.missing_clusters == actual.missing_clusters &&
        expected.missing_primary_face == actual.missing_primary_face, "coverage matches bounded control");
    require(expected.run_count == actual.run_count && expected.glyph_count == actual.glyph_count,
        "active counts match bounded control");
    require(actual.run_capacity == actual.run_count && actual.glyph_capacity == actual.glyph_count,
        "output capacity equals actual counts");
    for (std::size_t index = 0U; index < actual.run_count; ++index) {
        const BoundedFontRun& left = expected.runs[index];
        const BoundedFontRun& right = actual.runs[index];
        require(left.face == right.face && left.source_range == right.source_range &&
            left.glyph_begin == right.glyph_begin && left.glyph_count == right.glyph_count,
            "run identity and source extents match");
    }
    for (std::size_t index = 0U; index < actual.glyph_count; ++index) {
        require(expected.glyphs[index] == actual.glyphs[index], "glyph geometry matches");
    }
    const std::size_t run_bytes = actual.run_count * sizeof(BoundedFontRun);
    const std::size_t glyph_bytes = actual.glyph_count * sizeof(ShapedGlyph);
    const std::size_t bytes = sizeof(BoundedShapedText) + run_bytes + glyph_bytes;
    require(actual.controlled_output_bytes == bytes, "exact retained output accounting");
}

void test_geometry(HarfBuzzFontEngine& engine, BoundedShapeWorkspace& workspace) {
    const std::array<std::string_view, 9> texts{"", "   ", "office AV 123", "a\xcc\x81",
        "שלום ABC 123", "اللون 123 (RGB)", "Hello 😀!", "a\r\nb\tc", "Latin العربية שלום á 😀"};
    const std::array<FontSpec, 3> fonts{{
        {FontRole::content, 16, 400, false, 0},
        {FontRole::content, 11.875, 400, false, 0.375},
        {FontRole::content, 32, 400, false, -0.25}}};
    std::unique_ptr<BoundedShapedText> retained{};
    for (std::size_t font_index = 0U; font_index < fonts.size(); ++font_index) {
        for (std::size_t index = 0U; index < texts.size(); ++index) {
            const std::unique_ptr<BoundedShapedText> control = engine.shape_bounded(texts[index], fonts[font_index], {});
            const std::unique_ptr<BoundedShapedText> output = engine.shape_with_workspace(texts[index], fonts[font_index], {}, workspace);
            compare(*control, *output);
            if (retained) require((*retained).glyph_count == 3U, "old independent output survives scratch reuse");
            if (!retained) retained = engine.shape_with_workspace("abc", fonts[0], {}, workspace);
        }
    }
}

void test_failures(HarfBuzzFontEngine& engine, BoundedShapeWorkspace& workspace) {
    const FontSpec font{FontRole::content, 16, 400, false};
    std::unique_ptr<BoundedShapedText> output = engine.shape_with_workspace("hello", font, {}, workspace);
    const BoundedShapedText* old = output.get();
    ShapeStorageLimits exact{};
    exact.output_bytes = (*output).controlled_output_bytes;
    const std::unique_ptr<BoundedShapedText> accepted = engine.shape_with_workspace("hello", font, exact, workspace);
    compare(*output, *accepted);
    --exact.output_bytes;
    bool refused = false;
    try { output = engine.shape_with_workspace("hello", font, exact, workspace); }
    catch (const std::length_error&) { refused = true; }
    require(refused && output.get() == old, "one byte short preserves old output");
    for (std::size_t failure = 1U; failure <= 3U; ++failure) {
        allocation_probe::calls = 0U;
        allocation_probe::fail_at = failure;
        allocation_probe::enabled = true;
        bool failed = false;
        try { output = engine.shape_with_workspace("hello", font, {}, workspace); }
        catch (const std::bad_alloc&) { failed = true; }
        allocation_probe::enabled = false;
        require(failed && output.get() == old, "each exact-output allocation failure preserves owner");
    }
    allocation_probe::fail_at = 0U;
    allocation_probe::calls = 0U;
    allocation_probe::enabled = true;
    output = engine.shape_with_workspace("hello", font, {}, workspace);
    allocation_probe::enabled = false;
    require(allocation_probe::calls == 3U, "prepared nonempty shape allocates only result owner and two arrays");
    ShapeStorageLimits small{};
    small.glyphs = 1U;
    refused = false;
    try { output = engine.shape_with_workspace("hello", font, small, workspace); }
    catch (const std::length_error&) { refused = true; }
    require(refused, "per-call glyph ceiling honored below prepared capacity");
    output = engine.shape_with_workspace("hello", font, {}, workspace);
    require((*output).glyph_count == 5U, "workspace reusable after shaping refusal");
    allocation_probe::calls = 0U;
    allocation_probe::enabled = true;
    output = engine.shape_with_workspace("", font, {}, workspace);
    allocation_probe::enabled = false;
    require(allocation_probe::calls == 1U && !(*output).runs && !(*output).glyphs,
        "empty row allocates no glyph or run arrays");
}

void test_preparation(HarfBuzzFontEngine& engine) {
    ShapeStorageLimits small{};
    small.input_bytes = 8U;
    small.runs = 8U;
    small.glyphs = 16U;
    BoundedShapeWorkspace workspace{};
    engine.prepare_workspace(workspace, small);
    const std::size_t retained = workspace.controlled_bytes();
    for (std::size_t failure = 1U; failure <= 8U; ++failure) {
        allocation_probe::calls = 0U;
        allocation_probe::fail_at = failure;
        allocation_probe::enabled = true;
        bool refused = false;
        try { engine.prepare_workspace(workspace, {}); }
        catch (const std::bad_alloc&) { refused = true; }
        allocation_probe::enabled = false;
        require(refused && workspace.controlled_bytes() == retained, "failed workspace replacement retains original arrays");
        const std::unique_ptr<BoundedShapedText> result = engine.shape_with_workspace("abc", {FontRole::content, 16, 400, false}, small, workspace);
        require((*result).glyph_count == 3U, "old workspace remains usable after failed replacement");
    }
    allocation_probe::fail_at = 0U;
    BoundedShapeWorkspace large{};
    engine.prepare_workspace(large, {});
    const std::unique_ptr<BoundedShapedText> measured = engine.shape_with_workspace(
        "", {FontRole::content, 16, 400, false}, {}, large);
    ShapeStorageLimits replacement{};
    replacement.workspace_bytes = (*measured).controlled_workspace_peak + retained;
    --replacement.workspace_bytes;
    bool refused = false;
    try { engine.prepare_workspace(workspace, replacement); }
    catch (const std::length_error&) { refused = true; }
    require(refused && workspace.controlled_bytes() == retained, "one below simultaneous old/new workspace peak refused");
    ++replacement.workspace_bytes;
    engine.prepare_workspace(workspace, replacement);
    allocation_probe::calls = 0U;
    allocation_probe::enabled = true;
    engine.prepare_workspace(workspace, {});
    allocation_probe::enabled = false;
    require(allocation_probe::calls == 0U, "sufficient prepared arrays reused");
}
void test_invalid_requests(HarfBuzzFontEngine& engine, BoundedShapeWorkspace& workspace) {
    const FontSpec font{FontRole::content, 16, 400, false};
    std::unique_ptr<BoundedShapedText> output = engine.shape_with_workspace("abc", font, {}, workspace);
    const BoundedShapedText* old = output.get();
    const std::array<std::string_view, 4U> invalid{"\x80", "\xc0\xaf", "\xed\xa0\x80", "\xf0\x9f\x98"};
    for (std::size_t index = 0U; index < invalid.size(); ++index) {
        bool refused = false;
        try { output = engine.shape_with_workspace(invalid[index], font, {}, workspace); }
        catch (const std::invalid_argument&) { refused = true; }
        require(refused && output.get() == old, "malformed text preserves old result");
    }
    BoundedShapeWorkspace unprepared{};
    bool refused = false;
    try { output = engine.shape_with_workspace("abc", font, {}, unprepared); }
    catch (const std::length_error&) { refused = true; }
    require(refused && output.get() == old, "unprepared workspace refused");
    HarfBuzzFontEngine unbounded{};
    refused = false;
    try { unbounded.prepare_workspace(unprepared, {}); }
    catch (const std::invalid_argument&) { refused = true; }
    require(refused, "unbounded font registration refused");
    const std::size_t registration = unbounded.configure_bounded_registration(1U, 16U * 1024U * 1024U);
    require(registration != 0U, "empty bounded registry charged");
    unbounded.prepare_workspace(unprepared, {});
    const std::unique_ptr<BoundedShapedText> control = unbounded.shape_bounded("abc", font, {});
    const std::unique_ptr<BoundedShapedText> missing = unbounded.shape_with_workspace("abc", font, {}, unprepared);
    compare(*control, *missing);
    require((*missing).missing_primary_face, "missing primary reported without fabricated glyphs");
    const std::unique_ptr<BoundedShapedText> recovered = engine.shape_with_workspace("abc", font, {}, workspace);
    compare(*output, *recovered);
}

void test_retained_rows(HarfBuzzFontEngine& engine, BoundedShapeWorkspace& workspace) {
    const std::array<std::string_view, 4U> texts{"office", "العربية", "שלום", "á 😀"};
    const FontSpec font{FontRole::content, 16, 400, false};
    std::array<std::unique_ptr<BoundedShapedText>, 512U> rows{};
    std::size_t retained_bytes = sizeof(rows);
    const ShapeStorageLimits ceiling{};
    for (std::size_t index = 0U; index < rows.size(); ++index) {
        ShapeStorageLimits limits{};
        require(retained_bytes <= limits.output_bytes, "retained row accounting fits aggregate");
        limits.output_bytes -= retained_bytes;
        rows[index] = engine.shape_with_workspace(texts[index % texts.size()], font, limits, workspace);
        retained_bytes += (*rows[index]).controlled_output_bytes;
    }
    require(retained_bytes <= ceiling.output_bytes, "512 retained shaped rows fit output allowance");
    for (std::size_t index = 0U; index < rows.size(); ++index) {
        const std::unique_ptr<BoundedShapedText> control = engine.shape_bounded(texts[index % texts.size()], font, {});
        compare(*control, *rows[index]);
    }
}

#if defined(GUI_FORMS_TEXT_LAYOUT_DIAGNOSTICS)
void test_native_failures(HarfBuzzFontEngine& engine, BoundedShapeWorkspace& workspace) {
    const std::array<TextLayoutFailure, 6U> failures{TextLayoutFailure::empty_font,
        TextLayoutFailure::unbound_font, TextLayoutFailure::empty_buffer,
        TextLayoutFailure::after_add, TextLayoutFailure::after_shape, TextLayoutFailure::no_shaper};
    const FontSpec font{FontRole::content, 16, 400, false};
    const std::string_view text = "Latin العربية שלום";
    const std::unique_ptr<BoundedShapedText> control = engine.shape_bounded(text, font, {});
    std::unique_ptr<BoundedShapedText> output = engine.shape_with_workspace(text, font, {}, workspace);
    for (std::size_t index = 0U; index < failures.size(); ++index) {
        const BoundedShapedText* old = output.get();
        engine.set_diagnostic_failure(failures[index], 1U);
        bool refused = false;
        try { output = engine.shape_with_workspace(text, font, {}, workspace); }
        catch (const std::bad_alloc&) { refused = true; }
        catch (const std::runtime_error&) { refused = true; }
        require(refused && output.get() == old, "native failure preserves caller output");
        compare(*control, *output);
        output = engine.shape_with_workspace(text, font, {}, workspace);
        compare(*control, *output);
    }
    engine.set_diagnostic_failure(TextLayoutFailure::after_shape, 2U);
    const BoundedShapedText* old = output.get();
    bool refused = false;
    try { output = engine.shape_with_workspace(text, font, {}, workspace); }
    catch (const std::bad_alloc&) { refused = true; }
    require(refused && output.get() == old, "late native failure publishes no partial output");
    output = engine.shape_with_workspace(text, font, {}, workspace);
    compare(*control, *output);
}
#endif
} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "font directory required");
        HarfBuzzFontEngine engine{};
        const std::size_t registration = engine.configure_bounded_registration(4U, 16U * 1024U * 1024U);
        require(registration != 0U, "bounded registration charged");
        const std::filesystem::path fonts(argv[1]);
        load_fonts(engine, fonts);
        BoundedShapeWorkspace workspace{};
        engine.prepare_workspace(workspace, {});
        test_geometry(engine, workspace);
        test_failures(engine, workspace);
        test_preparation(engine);
        test_invalid_requests(engine, workspace);
        test_retained_rows(engine, workspace);
#if defined(GUI_FORMS_TEXT_LAYOUT_DIAGNOSTICS)
        test_native_failures(engine, workspace);
#endif
        std::cout << "Reusable bounded shaping equivalence and ownership fixtures passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        allocation_probe::enabled = false;
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
