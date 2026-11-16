#include "font_metrics_differential.hpp"

#include "harfbuzz_font_engine.hpp"

#include <stxlib/font_file.hpp>
#include <stxlib/vector.hpp>
#include <stxlib/view.hpp>

#include <algorithm>
#include <new>
#include <optional>
#include <string>

// One face's measurements: facts and per-code-point advances from the font's
// own tables in font units (STXlib's font reader, which reads them as
// FreeType 2.14.2 did), then line metrics, corpus string widths and greedy
// line breaks from the bundled text engine, which shapes exactly as
// GUI.Forms does (ADR-004: unhinted advances at the logical size).

namespace gui_forms::tools::font_metrics {

namespace {

// The font name `name_id` as UTF-8, by FreeType 2.14.2's choice of record
// (stxlib::glyphs::font_name); empty when the font has none.
std::string face_name(const stxlib::glyphs::Font& font, const stxlib::u32 name_id) {
    stxlib::Vector<char> text{};
    const stxlib::OperationStatus read = stxlib::glyphs::font_name(font, name_id, text);
    if (read.failed()) throw std::bad_alloc();
    std::string name{};
    name.reserve(static_cast<std::size_t>(text.count()));
    for (stxlib::usize index = 0; index < text.count(); ++index) name.push_back(text[index]);
    return name;
}

// The name table's version string (name identifier 5).
constexpr stxlib::u32 name_version = 5;

FaceFacts read_facts(const stxlib::glyphs::Font& font, std::string_view file_name, std::span<const std::byte> encoded) {
    if (font.os2_version == 0xFFFFU) throw InputRejected("face lacks hhea or OS/2");
    FaceFacts facts{};
    facts.file_name = std::string(file_name);
    facts.byte_count = encoded.size();
    facts.sha256_hex = sha256_hex(encoded);
    facts.family = face_name(font, stxlib::glyphs::name_family);
    facts.version = face_name(font, name_version);
    facts.units_per_em = static_cast<std::uint16_t>(font.units_per_em);
    facts.weight_class = static_cast<std::uint16_t>(font.weight_class);
    facts.italic = (font.selection & 1U) != 0U;
    facts.hhea_ascender = static_cast<std::int16_t>(font.hhea_ascender);
    facts.hhea_descender = static_cast<std::int16_t>(font.hhea_descender);
    facts.hhea_line_gap = static_cast<std::int16_t>(font.hhea_line_gap);
    facts.typo_ascender = static_cast<std::int16_t>(font.typo_ascender);
    facts.typo_descender = static_cast<std::int16_t>(font.typo_descender);
    facts.typo_line_gap = static_cast<std::int16_t>(font.typo_line_gap);
    facts.win_ascent = static_cast<std::uint16_t>(font.win_ascent);
    facts.win_descent = static_cast<std::uint16_t>(font.win_descent);
    facts.glyph_count = static_cast<std::uint32_t>(font.glyph_count);
    if (facts.units_per_em == 0U) throw InputRejected("face has zero units per em");
    return facts;
}

std::vector<CodepointAdvance> read_advances(const stxlib::glyphs::Font& font) {
    std::size_t count = 0U;
    for (const CodepointRange& range : advance_coverage) count += range.last - range.first + 1U;
    std::vector<CodepointAdvance> advances{};
    advances.reserve(count);
    for (const CodepointRange& range : advance_coverage) {
        for (char32_t codepoint = range.first; codepoint <= range.last; ++codepoint) {
            CodepointAdvance advance{};
            advance.codepoint = codepoint;
            const stxlib::u32 glyph = stxlib::glyphs::glyph_of(font, static_cast<stxlib::u32>(codepoint));
            if (glyph != 0U) {
                advance.mapped = true;
                advance.advance_units = static_cast<std::int32_t>(stxlib::glyphs::advance_of(font, glyph));
            }
            advances.push_back(advance);
        }
    }
    return advances;
}

FontSpec measured_font(double size_px) {
    FontSpec font{};
    font.role = FontRole::content;
    font.size = size_px;
    font.weight = 400U;
    font.italic = false;
    font.letter_spacing = 0.0;
    return font;
}

// The words of a paragraph: views into it, split at single spaces (the corpus
// parser rejects leading, trailing and doubled spaces).
std::vector<std::string_view> split_words(std::string_view paragraph) {
    std::vector<std::string_view> words{};
    words.reserve(static_cast<std::size_t>(std::count(paragraph.begin(), paragraph.end(), ' ')) + 1U);
    std::size_t start = 0U;
    while (start <= paragraph.size()) {
        const std::size_t space = paragraph.find(' ', start);
        const std::size_t end = space == std::string_view::npos ? paragraph.size() : space;
        words.push_back(paragraph.substr(start, end - start));
        start = end + 1U;
    }
    return words;
}

// Greedy breaking: a line takes words while its shaped width fits; a word
// wider than the line stays alone on its own line. line is the caller's
// reusable buffer, reserved to the paragraph's length.
std::vector<std::uint32_t> wrap_paragraph(render::text::HarfBuzzFontEngine& engine,
                                          const std::vector<std::string_view>& words,
                                          FontSpec font, double width_px, std::string& line) {
    std::vector<std::uint32_t> starts{};
    line.assign(words.front());
    for (std::size_t index = 1U; index < words.size(); ++index) {
        line.push_back(' ');
        line.append(words[index]);
        const render::text::ShapedText shaped = engine.shape(line, font);
        if (shaped.width <= width_px) continue;
        starts.push_back(static_cast<std::uint32_t>(index));
        line.assign(words[index]);
    }
    return starts;
}

std::vector<EngineSizeMeasurement> measure_sizes(std::span<const std::byte> encoded, const Corpus& corpus) {
    render::text::HarfBuzzFontEngine engine{};
    const std::optional<FontFaceId> registered = engine.register_typeface(FontRole::content, 400U, false, encoded);
    if (!registered) throw InputRejected("the bundled engine refused the face");
    std::vector<std::vector<std::string_view>> paragraph_words{};
    paragraph_words.reserve(corpus.paragraphs.size());
    std::size_t longest = 0U;
    for (const std::string& paragraph : corpus.paragraphs) {
        paragraph_words.push_back(split_words(paragraph));
        longest = std::max(longest, paragraph.size());
    }
    std::string line{};
    line.reserve(longest);

    std::vector<EngineSizeMeasurement> sizes{};
    sizes.reserve(measured_sizes_px.size());
    for (const double size_px : measured_sizes_px) {
        const FontSpec font = measured_font(size_px);
        const std::optional<render::text::PrimaryLineMetrics> metrics = engine.primary_line_metrics(font);
        if (!metrics) throw std::runtime_error("the registered face has no primary line metrics");
        EngineSizeMeasurement size{};
        size.size_px = size_px;
        size.ascent_px = (*metrics).ascent_device;
        size.descent_px = (*metrics).descent_device;
        size.line_gap_px = (*metrics).line_gap_device;
        size.string_widths_px.reserve(corpus.strings.size());
        for (const std::string& text : corpus.strings) {
            const render::text::ShapedText shaped = engine.shape(text, font);
            size.string_widths_px.push_back(shaped.width);
            size.missing_clusters += shaped.missing_clusters;
        }
        size.line_starts.reserve(paragraph_words.size() * wrap_widths_em.size());
        for (const std::vector<std::string_view>& words : paragraph_words) {
            for (const double width_em : wrap_widths_em) {
                size.line_starts.push_back(wrap_paragraph(engine, words, font, width_em * size_px, line));
            }
        }
        sizes.push_back(std::move(size));
    }
    return sizes;
}

} // namespace

FaceMeasurement measure_face(std::string_view file_name, std::span<const std::byte> encoded,
                             const Corpus& corpus, bool with_gdi) {
    validate_face_bytes(encoded);
    FaceMeasurement measurement{};
    {
        const stxlib::View<stxlib::u8> bytes(reinterpret_cast<const stxlib::u8*>(encoded.data()), encoded.size());
        stxlib::glyphs::Font font{};
        const stxlib::OperationStatus opened = stxlib::glyphs::open_font(bytes, 0U, font);
        if (opened.failed()) throw InputRejected("the font reader refused the face");
        measurement.facts = read_facts(font, file_name, encoded);
        measurement.advances = read_advances(font);
    }
    measurement.sizes = measure_sizes(encoded, corpus);
    if (with_gdi) {
        measurement.gdi = measure_with_gdi(encoded, measurement.facts, corpus);
    } else {
        measurement.gdi.unavailable_reason = "not requested";
    }
    return measurement;
}

} // namespace gui_forms::tools::font_metrics
