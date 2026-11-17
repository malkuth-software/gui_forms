#include "../src/render/text/text_mask/text_mask_native.hpp"
#include "prepared_text_test_support.hpp"

#include <stxlib/glyph_face.hpp>
#include <stxlib/vector.hpp>
#include <stxlib/view.hpp>

#include <cmath>
#include <iostream>

namespace {
using namespace gui_forms;
using namespace gui_forms::detail;
using namespace gui_forms::detail::mask_native;
using prepared_test::require;

std::vector<std::byte> read_native_fixture(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    require(static_cast<bool>(input), "native fixture opens");
    const std::streamsize length = input.tellg();
    require(length > 0 && length <= 4*1024*1024, "native fixture bounded length");
    std::vector<std::byte> bytes(static_cast<std::size_t>(length));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()), length);
    require(static_cast<bool>(input), "native fixture read");
    return bytes;
}

struct NativeFixture final {
    TextMaskService service{};
    EncodedFontLease bank{};
    std::unique_ptr<TextMaskSession> session{};
    std::shared_ptr<MaskLedger> ledger{};
    explicit NativeFixture(const std::span<const std::byte> bytes) {
        ledger = (*TextMaskAccess::state(service)).ledger;
        const std::array<PreparedFontSource, 1> sources{{{.encoded = bytes}}};
        TextMaskResult result = service.create_font_bank(sources, bank);
        require(result.status == TextMaskStatus::success, "native fixture bank admission");
        result = service.open_session(nullptr, session);
        require(result.status == TextMaskStatus::success, "native fixture session");
    }
    std::shared_ptr<const MaskKey> key(const TextMaskRequest& request) {
        MaskOptions options{};
        const TextMaskResult result = normalize_mask_request(request, options);
        require(result.status == TextMaskStatus::success, "native fixture request normalizes");
        MaskCharge count{};
        count.reserve(ledger, MaskResource::keys, 1);
        const MaskAllocator<MaskKey> allocator(ledger);
        std::shared_ptr<MaskKey> key = std::allocate_shared<MaskKey>(allocator, ledger, std::move(count));
        (*key).fonts = (*TextMaskAccess::state(*session)).find_bank(bank);
        (*key).options = options;
        (*key).text.resize(request.utf8.size());
        std::copy(request.utf8.begin(), request.utf8.end(), (*key).text.begin());
        return key;
    }
};

void test_shape_and_scale(const std::span<const std::byte> bytes) {
    NativeFixture fixture(bytes);
    const std::string text = "office cafe\xCC\x81 AV";
    const std::shared_ptr<const MaskKey> first = fixture.key({.utf8 = text, .size = 17.0, .device_scale = 1.0});
    const std::shared_ptr<const MaskKey> second = fixture.key({.utf8 = text, .size = 17.0, .device_scale = 2.0});
    NativeFonts first_fonts(*first);
    NativeFonts second_fonts(*second);
    std::cout << "Fixture coverage: a=" << stxlib::glyphs::glyph_of(*first_fonts.faces[0].font, 0x61)
        << " acute=" << stxlib::glyphs::glyph_of(*first_fonts.faces[0].font, 0x301)
        << " alef=" << stxlib::glyphs::glyph_of(*first_fonts.faces[0].font, 0x5D0)
        << " arabic-beh=" << stxlib::glyphs::glyph_of(*first_fonts.faces[0].font, 0x628) << '\n';
    NativeWork first_work{};
    NativeWork second_work{};
    NativeParagraph first_paragraph(fixture.ledger, first_fonts, first_work, text);
    NativeParagraph second_paragraph(fixture.ledger, second_fonts, second_work, text);
    NativeGlyphBuffer first_glyphs{MaskAllocator<NativeGlyph>(fixture.ledger, MaskResource::shaping)};
    NativeGlyphBuffer second_glyphs{MaskAllocator<NativeGlyph>(fixture.ledger, MaskResource::shaping)};
    const NativeShape first_shape = first_paragraph.shape(0, text.size(), first_glyphs);
    const NativeShape second_shape = second_paragraph.shape(0, text.size(), second_glyphs);
    require(first_shape.glyph_count > 0 && first_shape.glyph_count == second_shape.glyph_count, "logical glyph count independent of device scale");
    require(first_shape.advance == second_shape.advance && first_shape.ascent == second_shape.ascent, "logical metrics independent of device scale");
    for (std::size_t index = 0; index < first_shape.glyph_count; ++index) {
        const NativeGlyph& a = first_glyphs[index];
        const NativeGlyph& b = second_glyphs[index];
        require(a.glyph == b.glyph && a.cluster == b.cluster && a.x == b.x && a.y == b.y && a.advance == b.advance,
            "exact logical positions and clusters independent of scale");
    }
    const NativeShape fragment = first_paragraph.shape(0, 2, first_glyphs);
    require(fragment.glyph_count > 0, "line context reshapes selected fragment");
    for (std::size_t index = 0; index < fragment.glyph_count; ++index) require(first_glyphs[index].cluster < 2, "no glyph cluster crosses selected line");
    require(first_work.shape_calls > 0 && first_work.context_bytes > 0, "native calls and context bytes accounted");
}

// ADR-004: outline_gray keeps each glyph's 1/64 horizontal phase. Shifting a
// run by a quarter device pixel changes coverage but conserves its ink.
std::vector<std::uint8_t> gray_mask(NativeFixture& fixture, const std::string& text, const double shift) {
    TextMaskRequest request{.utf8 = text, .size = 19.0, .device_scale = 1.0};
    const std::shared_ptr<const MaskKey> key = fixture.key(request);
    NativeFonts fonts(*key);
    NativeWork work{};
    NativeParagraph paragraph(fixture.ledger, fonts, work, text);
    NativeGlyphBuffer glyphs{MaskAllocator<NativeGlyph>(fixture.ledger, MaskResource::shaping)};
    const NativeShape shape = paragraph.shape(0, text.size(), glyphs);
    for (std::size_t index = 0; index < shape.glyph_count; ++index) {
        glyphs[index].y += shape.ascent;
        // Whole-pixel starting positions: only a kept phase can see the shift.
        glyphs[index].x = std::round(glyphs[index].x) + 2.0 + shift;
    }
    const std::span<const NativeGlyph> live(glyphs.data(), shape.glyph_count);
    TextMaskMetrics metrics{};
    metrics.device_scale = 1.0;
    metrics.ink_left_px = 0;
    metrics.ink_top_px = 0;
    metrics.width_px = static_cast<std::uint32_t>(std::ceil(shape.advance)) + 8U;
    metrics.height_px = static_cast<std::uint32_t>(std::ceil(shape.ascent + shape.descent)) + 4U;
    metrics.stride_bytes = metrics.width_px;
    std::shared_ptr<TextMaskStorage> mask = allocate_mask_storage(fixture.ledger, key, metrics, 1);
    NativeRasterWork raster_work(fixture.ledger);
    fill_native_mask(fonts, (*key).options, live, raster_work, *mask);
    return std::vector<std::uint8_t>((*mask).pixels.begin(), (*mask).pixels.end());
}

void test_gray_subpixel_phase(const std::span<const std::byte> bytes) {
    NativeFixture fixture(bytes);
    const std::string text = "Hamburgefonstiv";
    const std::vector<std::uint8_t> whole = gray_mask(fixture, text, 0.0);
    const std::vector<std::uint8_t> quarter = gray_mask(fixture, text, 0.25);
    require(whole.size() == quarter.size(), "same mask extent");
    require(whole != quarter, "a quarter-pixel shift changes gray coverage (phase kept)");
    double whole_ink = 0.0;
    double quarter_ink = 0.0;
    for (std::size_t index = 0; index < whole.size(); ++index) {
        whole_ink += static_cast<double>(whole[index]);
        quarter_ink += static_cast<double>(quarter[index]);
    }
    require(whole_ink > 0.0 && std::abs(quarter_ink - whole_ink) / whole_ink < 0.03, "phase conserves ink");
}

void test_raster_profiles(const std::span<const std::byte> bytes) {
    NativeFixture fixture(bytes);
    const std::string text = "Agj cafe\xCC\x81";
    for (unsigned profile = 0; profile < 2; ++profile) {
        for (unsigned scale = 1; scale <= 4; ++scale) {
            TextMaskRequest request{.utf8 = text, .size = 19.0, .device_scale = static_cast<double>(scale)};
            if (profile == 1) request.raster = TextMaskRaster::true_mono;
            const std::shared_ptr<const MaskKey> key = fixture.key(request);
            NativeFonts fonts(*key);
            NativeWork work{};
            NativeParagraph paragraph(fixture.ledger, fonts, work, text);
            NativeGlyphBuffer glyphs{MaskAllocator<NativeGlyph>(fixture.ledger, MaskResource::shaping)};
            const NativeShape shape = paragraph.shape(0, text.size(), glyphs);
            for (std::size_t index = 0; index < shape.glyph_count; ++index) glyphs[index].y += shape.ascent;
            const std::span<const NativeGlyph> live(glyphs.data(), shape.glyph_count);
            NativeRasterWork raster_work(fixture.ledger);
            const NativeInk ink = measure_native_ink(fonts, (*key).options, live, raster_work);
            require(ink.present && ink.right > ink.left && ink.bottom > ink.top, "native ink measured");
            TextMaskMetrics metrics{};
            metrics.device_scale = request.device_scale;
            metrics.ink_left_px = static_cast<std::int32_t>(ink.left);
            metrics.ink_top_px = static_cast<std::int32_t>(ink.top);
            metrics.width_px = static_cast<std::uint32_t>(ink.right - ink.left);
            metrics.height_px = static_cast<std::uint32_t>(ink.bottom - ink.top);
            metrics.stride_bytes = metrics.width_px;
            std::shared_ptr<TextMaskStorage> mask = allocate_mask_storage(fixture.ledger, key, metrics, 1);
            fill_native_mask(fonts, (*key).options, live, raster_work, *mask);
            bool ink_found = false;
            bool fractional = false;
            for (std::size_t index = 0; index < (*mask).pixels.size(); ++index) {
                const std::uint8_t value = (*mask).pixels[index];
                if (value != 0) ink_found = true;
                if (value != 0 && value != 255) fractional = true;
            }
            require(ink_found, "native raster contains coverage");
            if (profile == 1) require(!fractional, "true FT mono expands only zero and255");
            else require(fractional, "outline gray retains partial coverage");
        }
    }
}

void test_native_failure_and_work(const std::span<const std::byte> bytes) {
    NativeFixture fixture(bytes);
    const std::shared_ptr<const MaskKey> key = fixture.key({.utf8 = "a"});
    NativeFonts fonts(*key);
    NativeWork work{};
    NativeParagraph paragraph(fixture.ledger, fonts, work, "a");
    NativeGlyphBuffer glyphs{MaskAllocator<NativeGlyph>(fixture.ledger, MaskResource::shaping)};
    work.shape_calls = 2048;
    bool refused = false;
    try { const NativeShape shape = paragraph.shape(0, 1, glyphs); static_cast<void>(shape); }
    catch (const MaskLimitFailure& failure) { refused = failure.limit == TextMaskLimit::shape_work; }
    require(refused, "native call budget checked before call");
    work.shape_calls = 0;
    work.context_bytes = 4U*1024U*1024U;
    refused = false;
    try { const NativeShape shape = paragraph.shape(0, 1, glyphs); static_cast<void>(shape); }
    catch (const MaskLimitFailure& failure) { refused = failure.limit == TextMaskLimit::shape_work; }
    require(refused, "aggregate context checked before call");
    const std::array<std::byte, 8> invalid{};
    NativeFixture malformed(invalid);
    const std::shared_ptr<const MaskKey> bad = malformed.key({});
    refused = false;
    try { NativeFonts invalid_fonts(*bad); }
    catch (const NativeFailure& failure) { refused = failure.status == TextMaskStatus::unsupported_profile; }
    require(refused, "malformed encoded font fails native opening explicitly");
    const std::string missing = "\xF4\x8F\xBF\xBF";
    refused = false;
    try { NativeParagraph unavailable(fixture.ledger, fonts, work, missing); }
    catch (const NativeFailure& failure) { refused = failure.status == TextMaskStatus::missing_font_coverage; }
    require(refused, "missing coverage never uses system fonts or replacement glyphs");
}

void test_paragraph_bidi(const std::span<const std::byte> bytes) {
    NativeFixture fixture(bytes);
    const std::string text = "\xD7\x90\xD7\x91 123.";
    const std::shared_ptr<const MaskKey> key = fixture.key({.utf8 = text});
    NativeFonts fonts(*key);
    NativeWork work{};
    NativeParagraph paragraph(fixture.ledger, fonts, work, text);
    NativeGlyphBuffer glyphs{MaskAllocator<NativeGlyph>(fixture.ledger, MaskResource::shaping)};
    const NativeShape line = paragraph.shape(5, text.size(), glyphs);
    require(line.glyph_count == 4 && glyphs[0].cluster == text.size() - 1U,
        "wrapped numeric line retains original RTL paragraph context");
    const std::string standalone = "123.";
    NativeParagraph independent(fixture.ledger, fonts, work, standalone);
    const NativeShape comparison = independent.shape(0, standalone.size(), glyphs);
    require(comparison.glyph_count == 4 && glyphs[3].cluster == 3,
        "standalone numeric paragraph has different visual punctuation order");
    NativeParagraph empty(fixture.ledger, fonts, work, {});
    const NativeShape empty_line = empty.shape(0, 0, glyphs);
    require(empty_line.glyph_count == 0 && empty_line.advance == 0.0 && empty_line.ascent > 0.0 && empty_line.descent > 0.0,
        "empty line uses primary face metrics with no glyph or ink");
}

// The paragraph's bidi analysis (stxlib::textlayout's Paragraph and Line,
// on STXlib's allocator) is charged to the mask ledger as workspace, in
// bytes, while the paragraph holds it, and released with it.
void test_paragraph_bidi_charge(const std::span<const std::byte> bytes) {
    NativeFixture fixture(bytes);
    const std::string text = "abc \xD7\x90\xD7\x91\xD7\x92 def";
    const std::shared_ptr<const MaskKey> key = fixture.key({.utf8 = text});
    NativeFonts fonts(*key);
    NativeWork work{};
    NativeGlyphBuffer glyphs{MaskAllocator<NativeGlyph>(fixture.ledger, MaskResource::shaping)};
    const std::size_t workspace = static_cast<std::size_t>(MaskResource::workspace);
    const std::size_t before = (*fixture.ledger).usage[workspace].live;
    std::size_t analysed = 0;
    std::size_t shaped = 0;
    {
        NativeParagraph paragraph(fixture.ledger, fonts, work, text);
        analysed = (*fixture.ledger).usage[workspace].live;
        static_cast<void>(paragraph.shape(0, text.size(), glyphs));
        shaped = (*fixture.ledger).usage[workspace].live;
        static_cast<void>(paragraph.shape(0, text.size(), glyphs));
        require((*fixture.ledger).usage[workspace].live == shaped, "a line analysed again is charged once");
    }
    require(analysed > before, "the paragraph analysis is charged as workspace");
    require(shaped >= analysed, "the line analysis adds to the charge");
    require((*fixture.ledger).usage[workspace].live == before, "the charge goes with the paragraph");
}

void test_mono_native_oracle(const std::span<const std::byte> bytes) {
    NativeFixture fixture(bytes);
    const std::shared_ptr<const MaskKey> key = fixture.key({.utf8 = "j", .size = 13.0, .device_scale = 2.0, .raster = TextMaskRaster::true_mono});
    NativeFonts fonts(*key);
    NativeWork work{};
    NativeParagraph paragraph(fixture.ledger, fonts, work, "j");
    NativeGlyphBuffer glyphs{MaskAllocator<NativeGlyph>(fixture.ledger, MaskResource::shaping)};
    const NativeShape shape = paragraph.shape(0, 1, glyphs);
    require(shape.glyph_count == 1, "mono oracle single glyph");
    glyphs[0].y += shape.ascent;
    const std::span<const NativeGlyph> live(glyphs.data(), 1);
    NativeRasterWork raster_work(fixture.ledger);
    const NativeInk ink = measure_native_ink(fonts, (*key).options, live, raster_work);
    TextMaskMetrics metrics{};
    metrics.device_scale = 2.0;
    metrics.ink_left_px = static_cast<std::int32_t>(ink.left);
    metrics.ink_top_px = static_cast<std::int32_t>(ink.top);
    metrics.width_px = static_cast<std::uint32_t>(ink.right - ink.left);
    metrics.height_px = static_cast<std::uint32_t>(ink.bottom - ink.top);
    metrics.stride_bytes = metrics.width_px;
    std::shared_ptr<TextMaskStorage> mask = allocate_mask_storage(fixture.ledger, key, metrics, 1);
    fill_native_mask(fonts, (*key).options, live, raster_work, *mask);
    // Independent oracle: a face of its own over the same bytes (not the
    // program typeface the job drew from), one glyph's monochrome mask at
    // the job's device size, whole-pixel origin.
    stxlib::glyphs::Face oracle{};
    const stxlib::View<stxlib::u8> font_bytes(reinterpret_cast<const stxlib::u8*>(bytes.data()), bytes.size());
    require(oracle.open(font_bytes, 0U).succeeded(), "mono oracle face opens");
    stxlib::glyphs::DrawStyle style{};
    style.size = static_cast<stxlib::i64>(std::floor(static_cast<double>((*key).options.size_64) * 2.0 + 0.5));
    style.fit = stxlib::glyphs::Fit::monochrome;
    stxlib::glyphs::Bounds bounds{};
    stxlib::Vector<stxlib::u8> pixels{};
    require(oracle.mask(glyphs[0].glyph, style, stxlib::glyphs::Point{}, bounds, pixels).succeeded(), "mono oracle glyph mask");
    require(bounds.width == metrics.width_px && bounds.height == metrics.height_px, "mask dimensions equal the oracle's");
    const std::int64_t expected_left = std::llround(glyphs[0].x * 2.0) + bounds.left;
    const std::int64_t expected_top = std::llround(glyphs[0].y * 2.0) - bounds.top;
    require(expected_left == metrics.ink_left_px && expected_top == metrics.ink_top_px, "signed bearings preserved");
    for (std::size_t row = 0; row < bounds.height; ++row) {
        for (std::size_t column = 0; column < bounds.width; ++column) {
            const std::uint8_t expected = pixels[row * bounds.width + column];
            require(expected == 0U || expected == 255U, "oracle mono coverage is one bit");
            require((*mask).pixels[row * metrics.stride_bytes + column] == expected, "mono coverage equals the oracle's");
        }
    }
}
void test_arabic_line_context(const std::span<const std::byte> bytes) {
    NativeFixture fixture(bytes);
    const std::string text = "\xD8\xA8\xD8\xA8";
    const std::shared_ptr<const MaskKey> key = fixture.key({.utf8 = text});
    NativeFonts fonts(*key);
    require(stxlib::glyphs::glyph_of(*fonts.faces[0].font, 0x628) != 0, "approved Amiri covers Arabic beh");
    NativeWork work{};
    NativeParagraph paragraph(fixture.ledger, fonts, work, text);
    NativeGlyphBuffer full_glyphs{MaskAllocator<NativeGlyph>(fixture.ledger, MaskResource::shaping)};
    const NativeShape full = paragraph.shape(0, text.size(), full_glyphs);
    require(full.glyph_count == 2, "two Arabic letters shape contextually");
    NativeGlyphBuffer first_glyphs{MaskAllocator<NativeGlyph>(fixture.ledger, MaskResource::shaping)};
    NativeGlyphBuffer second_glyphs{MaskAllocator<NativeGlyph>(fixture.ledger, MaskResource::shaping)};
    const NativeShape first = paragraph.shape(0, 2, first_glyphs);
    const NativeShape second = paragraph.shape(2, 4, second_glyphs);
    require(first.glyph_count == 1 && second.glyph_count == 1 && first_glyphs[0].glyph == second_glyphs[0].glyph,
        "line-boundary reshaping isolates each Arabic letter");
    // The joining-forms check returns with STXlib's shaper joining (stxlib claude/shaper-arabic-joining, held by the owner 2026-11-17).
    require(first_glyphs[0].cluster == 0 && second_glyphs[0].cluster == 2, "line reshaping retains original source offsets");
}
}

int main(const int argc, char** const argv) {
    try {
        require(argc == 3, "pass approved font directory and Amiri fixture path");
        const std::filesystem::path directory(argv[1]);
        const std::vector<std::byte> bytes = read_native_fixture(directory / "Cousine-Regular.ttf");
        const std::vector<std::byte> arabic = read_native_fixture(std::filesystem::path(argv[2]));
        std::cout << "native group: shape and scale\n";
        test_shape_and_scale(bytes);
        std::cout << "native group: raster profiles\n";
        test_raster_profiles(bytes);
        test_gray_subpixel_phase(bytes);
        std::cout << "native group: failures and work bounds\n";
        test_native_failure_and_work(bytes);
        test_paragraph_bidi(bytes);
        test_paragraph_bidi_charge(bytes);
        test_mono_native_oracle(bytes);
        test_arabic_line_context(arabic);
        std::cout << "text mask native components: six groups passed\n";
        return EXIT_SUCCESS;
    } catch (const NativeFailure& failure) {
        std::cerr << "Native status: " << static_cast<int>(failure.status) << '\n';
        return EXIT_FAILURE;
    } catch (const MaskLimitFailure& failure) {
        std::cerr << "Native limit: " << static_cast<int>(failure.limit) << '\n';
        return EXIT_FAILURE;
    } catch (const std::exception& failure) {
        std::cerr << failure.what() << '\n';
        return EXIT_FAILURE;
    }
}
