#include "harfbuzz_font_engine.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <locale>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace gui_forms;
using namespace gui_forms::render::text;
using Clock = std::chrono::steady_clock;

void require(bool valid, const char* message) {
    if (!valid) { throw std::runtime_error(message); }
}

std::vector<std::byte> read_font(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    require(static_cast<bool>(stream), "font open failed");
    const std::streamsize length = stream.tellg();
    require(length > 0 && length <= 16 * 1024 * 1024, "fixed font exceeds probe admission");
    const std::size_t count = static_cast<std::size_t>(length);
    std::vector<std::byte> bytes(count);
    stream.seekg(0);
    char* destination = reinterpret_cast<char*>(bytes.data());
    stream.read(destination, length);
    require(static_cast<bool>(stream), "font read failed");
    return bytes;
}

std::size_t register_fonts(HarfBuzzFontEngine& engine, const std::filesystem::path& root) {
    const std::array<const char*, 4> names{
        "Carlito-Regular.ttf", "NotoSansArabic-Minimal.ttf",
        "NotoSansHebrew-Regular.ttf", "NotoEmoji-Regular.ttf"};
    std::size_t encoded_capacity = 0;
    for (std::size_t index = 0; index < names.size(); ++index) {
        const std::filesystem::path name(names[index]);
        const std::filesystem::path path = root / name;
        std::vector<std::byte> bytes = read_font(path);
        encoded_capacity += bytes.capacity();
        const std::shared_ptr<const std::vector<std::byte>> owner =
            std::make_shared<const std::vector<std::byte>>(std::move(bytes));
        std::optional<FontRole> role{};
        if (index == 0) { role = FontRole::content; }
        const std::optional<FontFaceId> registered = engine.register_shared_typeface(role, 400, false, owner);
        require(registered.has_value(), "fixed font registration failed");
    }
    return encoded_capacity;
}

std::string repeat_complete(std::string_view unit, std::size_t budget) {
    require(!unit.empty(), "empty unit");
    const std::size_t count = budget / unit.size();
    const std::size_t size = count * unit.size();
    std::string text{};
    text.reserve(size);
    for (std::size_t index = 0; index < count; ++index) { text.append(unit); }
    return text;
}

struct ShapeFacts final {
    std::size_t glyphs{};
    std::size_t capacity_bytes{};
    std::uint64_t signature{1469598103934665603ULL};
};

ShapeFacts inspect(const ShapedText& shaped, const TextStore& source) {
    require(!shaped.missing_primary_face, "primary face missing");
    const bool finite_size = std::isfinite(shaped.width) && std::isfinite(shaped.height);
    require(finite_size && shaped.width >= 0 && shaped.height > 0, "nonfinite or empty shape metrics");
    ShapeFacts facts{};
    facts.capacity_bytes = shaped.runs.capacity() * sizeof(ShapedFontRun);
    const Utf8Offset source_end = source.utf8_size();
    const std::size_t run_count = shaped.runs.size();
    for (std::size_t index = 0; index < run_count; ++index) {
        const ShapedFontRun& run = shaped.runs[index];
        require(run.source_range.start.value() <= run.source_range.end.value() &&
                run.source_range.end.value() <= source_end.value(), "run source extent");
        const std::size_t glyph_capacity = run.glyphs.capacity() * sizeof(ShapedGlyph);
        facts.capacity_bytes += glyph_capacity;
        const std::size_t glyph_count = run.glyphs.size();
        facts.glyphs += glyph_count;
        for (std::size_t glyph_index = 0; glyph_index < glyph_count; ++glyph_index) {
            const ShapedGlyph& glyph = run.glyphs[glyph_index];
            const bool boundary = source.is_grapheme_boundary(glyph.cluster);
            require(boundary && glyph.cluster.value() >= run.source_range.start.value() &&
                    glyph.cluster.value() < run.source_range.end.value(), "glyph cluster boundary/range");
            const bool finite = std::isfinite(glyph.x) && std::isfinite(glyph.y) &&
                std::isfinite(glyph.advance_x) && std::isfinite(glyph.advance_y);
            require(finite, "glyph position finite");
            facts.signature ^= glyph.glyph.value;
            facts.signature *= 1099511628211ULL;
            facts.signature ^= glyph.cluster.value();
            facts.signature *= 1099511628211ULL;
        }
    }
    return facts;
}

double elapsed_ms(Clock::time_point start, Clock::time_point end) {
    const Clock::duration elapsed = end - start;
    const std::chrono::duration<double, std::milli> duration(elapsed);
    const double milliseconds = duration.count();
    return milliseconds;
}

#if defined(GUI_FORMS_TEXT_LAYOUT_GEOMETRY_TRACE)
// This is a separate, untimed process mode. Explicit fields avoid padding and
// native object representations. Hex floats preserve finite values and -0.
void write_font_manifest(const std::filesystem::path& root) {
    constexpr std::array<const char*, 4> names{
        "Carlito-Regular.ttf", "NotoSansArabic-Minimal.ttf",
        "NotoSansHebrew-Regular.ttf", "NotoEmoji-Regular.ttf"};
    constexpr char digits[] = "0123456789abcdef";
    const std::size_t face_count = names.size();
    for (std::size_t index = 0; index < face_count; ++index) {
        const std::filesystem::path path = root / names[index];
        const std::vector<std::byte> encoded = read_font(path);
        const std::size_t byte_count = encoded.size();
        const std::size_t local_id = index + 1;
        std::cout << "face," << local_id << ',' << names[index]
                  << ",face_index=0,weight=400,italic=0,primary=" << (index == 0)
                  << ",bytes=" << byte_count << '\n';
        for (std::size_t byte_index = 0; byte_index < byte_count; ++byte_index) {
            const unsigned byte = std::to_integer<unsigned>(encoded[byte_index]);
            std::cout.put(digits[byte / 16U]);
            std::cout.put(digits[byte % 16U]);
        }
        std::cout << '\n';
    }
}

void write_geometry(const std::filesystem::path& fonts, std::string_view name,
                    const std::string& input) {
    require(input.size() <= 16 * 1024, "geometry source work bound");
    constexpr std::array<FontSpec, 3> configurations{
        FontSpec{FontRole::content, 16.0, 400, false, 0.0},
        FontSpec{FontRole::content, 9.5, 400, false, 0.25},
        FontSpec{FontRole::content, 32.0, 700, true, -0.5}};
    HarfBuzzFontEngine engine{};
    static_cast<void>(register_fonts(engine, fonts));
    const TextStore source(input);
    const std::size_t configuration_count = configurations.size();
    for (std::size_t configuration = 0; configuration < configuration_count; ++configuration) {
        const FontSpec font = configurations[configuration];
        const ShapedText shaped = engine.shape(input, font);
        if (!input.empty()) { static_cast<void>(inspect(shaped, source)); }
        const std::size_t run_count = shaped.runs.size();
        std::cout << "shape," << name << ',' << input.size() << ',' << configuration
                  << ',' << static_cast<unsigned>(font.role) << ',' << font.size
                  << ',' << font.weight << ',' << font.italic << ',' << font.letter_spacing
                  << ',' << shaped.width << ',' << shaped.height << ',' << shaped.ascent
                  << ',' << shaped.descent << ',' << shaped.missing_clusters
                  << ',' << shaped.missing_primary_face << ',' << run_count << '\n';
        for (std::size_t run_index = 0; run_index < run_count; ++run_index) {
            const ShapedFontRun& run = shaped.runs[run_index];
            const std::size_t glyph_count = run.glyphs.size();
            std::cout << "run," << run_index << ',' << run.face.value
                      << ',' << run.source_range.start.value() << ',' << run.source_range.end.value()
                      << ',' << run.diagnostic_rtl << ',' << glyph_count << '\n';
            for (std::size_t glyph_index = 0; glyph_index < glyph_count; ++glyph_index) {
                const ShapedGlyph& glyph = run.glyphs[glyph_index];
                std::cout << "glyph," << glyph.glyph.value << ',' << glyph.cluster.value()
                          << ',' << glyph.x << ',' << glyph.y
                          << ',' << glyph.advance_x << ',' << glyph.advance_y << '\n';
            }
        }
    }
    std::cout.flush();
}
#endif

void measure(const std::filesystem::path& fonts, std::string_view name, const std::string& input) {
    constexpr std::size_t samples = 31;
    require(input.size() <= 16 * 1024, "probe source work bound");
    const TextStore oracle(input);
    HarfBuzzFontEngine engine{};
    const std::size_t font_bytes = register_fonts(engine, fonts);
    const FontSpec font{FontRole::content, 16.0, 400, false};
    std::vector<double> timings(samples, 0);
    ShapeFacts first_facts{};
    double first_width = 0;
    double first_ms = 0;
    std::size_t missing = 0;
    std::size_t run_count = 0;
    {
        const Clock::time_point start = Clock::now();
        const ShapedText first = engine.shape(input, font);
        const Clock::time_point end = Clock::now();
        first_ms = elapsed_ms(start, end);
        first_facts = inspect(first, oracle);
        first_width = first.width;
        missing = first.missing_clusters;
        run_count = first.runs.size();
    }
    std::size_t maximum_returned_capacity = first_facts.capacity_bytes;
#if defined(GUI_FORMS_TEXT_LAYOUT_DIAGNOSTICS)
    TextLayoutDiagnostics phase_totals{};
    TextLayoutDiagnostics last_diagnostics{};
    const std::size_t phase_count = phase_totals.nanoseconds.size();
#endif
    for (std::size_t index = 0; index < samples; ++index) {
        const Clock::time_point start = Clock::now();
        const ShapedText result = engine.shape(input, font);
        const Clock::time_point end = Clock::now();
        timings[index] = elapsed_ms(start, end);
#if defined(GUI_FORMS_TEXT_LAYOUT_DIAGNOSTICS)
        last_diagnostics = engine.diagnostics();
        for (std::size_t phase = 0; phase < phase_count; ++phase) {
            phase_totals.nanoseconds[phase] += last_diagnostics.nanoseconds[phase];
        }
#endif
        const ShapeFacts facts = inspect(result, oracle);
        require(facts.signature == first_facts.signature && facts.glyphs == first_facts.glyphs &&
                result.width == first_width && result.missing_clusters == missing,
                "repeated shape must match deterministic first result");
        maximum_returned_capacity = std::max(maximum_returned_capacity, facts.capacity_bytes);
    }
    std::sort(timings.begin(), timings.end());
    constexpr std::size_t last = samples - 1;
    constexpr std::size_t p50 = last * 50 / 100;
    constexpr std::size_t p95 = last * 95 / 100;
    constexpr std::size_t p99 = last * 99 / 100;
    std::cout << name << ',' << input.size() << ',' << first_ms << ',' << samples << ','
              << timings[p50] << ',' << timings[p95] << ',' << timings[p99] << ','
              << timings[last] << ',' << run_count << ',' << first_facts.glyphs << ','
              << maximum_returned_capacity << ',' << font_bytes << ',' << missing << '\n';
    // Preserve completed cases if the external deadline terminates a later one.
    std::cout.flush();
#if defined(GUI_FORMS_TEXT_LAYOUT_DIAGNOSTICS)
    const double sample_divisor = static_cast<double>(samples);
    const std::array<const char*, 10> phase_names{
        "store_graphemes", "fallback", "bidi", "intersections", "append_total",
        "ft_size", "hb_font", "buffer_setup", "hb_shape", "glyph_output"};
    static_assert(phase_names.size() == static_cast<std::size_t>(TextLayoutPhase::count));
    for (std::size_t phase = 0; phase < phase_count; ++phase) {
        const double nanoseconds = static_cast<double>(phase_totals.nanoseconds[phase]);
        const double mean_ms = nanoseconds / sample_divisor / 1'000'000.0;
        std::cout << "phase," << name << ',' << phase_names[phase] << ',' << mean_ms << '\n';
    }
    std::cout << "counts," << name << ",graphemes=" << last_diagnostics.graphemes
              << ",segments=" << last_diagnostics.segments
              << ",directions=" << last_diagnostics.directions
              << ",intersection_pairs=" << last_diagnostics.intersection_pairs
              << ",visual_runs=" << last_diagnostics.visual_runs
              << ",append_calls=" << last_diagnostics.append_calls << '\n';
    std::cout.flush();
#endif
}

} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2 || argc == 3, "usage: text_layout_probe FONT_DIRECTORY [--geometry]");
        const std::filesystem::path fonts(argv[1]);
        const std::string tiny = repeat_complete("office abc ", 64);
        const std::string medium = repeat_complete("office abc ", 4096);
        const std::string long_line = repeat_complete("office abc ", 16384);
        const std::string labels = repeat_complete("[BYTE FF]", 16384);
        const std::string bidi = repeat_complete("ABC שלום العربية 123 ", 16384);
        const std::string emoji = repeat_complete("a👩‍💻b ", 16384);
        std::string combining = "a";
        const std::string marks = repeat_complete("\xcc\x81", 16382);
        combining.append(marks);
#if defined(GUI_FORMS_TEXT_LAYOUT_GEOMETRY_TRACE)
        if (argc == 3) {
            require(std::string_view(argv[2]) == "--geometry", "unknown probe mode");
            std::cout.imbue(std::locale::classic());
            std::cout << std::hexfloat;
            write_font_manifest(fonts);
            write_geometry(fonts, "ascii_small", tiny);
            write_geometry(fonts, "ascii_4k", medium);
            write_geometry(fonts, "ascii_16k", long_line);
            write_geometry(fonts, "labels_16k", labels);
            write_geometry(fonts, "bidi_16k", bidi);
            write_geometry(fonts, "emoji_16k", emoji);
            write_geometry(fonts, "one_grapheme_16k", combining);
            write_geometry(fonts, "empty", "");
            write_geometry(fonts, "mixed_controls", "fi\tABC שלום\nالعربية a\xcc\x81 123");
            require(static_cast<bool>(std::cout), "geometry output failed");
            return EXIT_SUCCESS;
        }
#endif
        require(argc == 2, "geometry trace was not enabled in this build");
        std::cout << "case,input_bytes,first_ms,samples,p50_ms,p95_ms,p99_ms,worst_ms,runs,glyphs,returned_capacity,font_bytes,missing_clusters\n";
        measure(fonts, "ascii_small", tiny);
        measure(fonts, "ascii_4k", medium);
        measure(fonts, "ascii_16k", long_line);
        measure(fonts, "labels_16k", labels);
        measure(fonts, "bidi_16k", bidi);
        measure(fonts, "emoji_16k", emoji);
        measure(fonts, "one_grapheme_16k", combining);
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
