// Text rasterization profile probe. Diagnostic only: it selects nothing.
//
// GIVEN (owner, 2026-10-10): grayscale only, light auto-hinting, glyphs shaped
// with unhinted advances. This probe compares that profile, with optional
// coverage gamma or FreeType stem darkening, against today's hinted-advance
// spacing and the Skia reference (CoreText on macOS). It writes a contact sheet
// per font, scale and compositing rule, and a CSV of ink weight, difference
// from the reference, coverage hash for cross-platform identity, spacing drift
// against the outlines, and raster cost. Glyph placement uses integer 26.6
// arithmetic so hashes compare rasterization, not floating-point contraction.
//
// Usage: gui_forms_text_raster_profile_probe <font-directory> <output-directory>

#include "harfbuzz_font_engine.hpp"
#if defined(GUI_FORMS_PROFILE_PROBE_SKIA_REFERENCE)
#include "skia_raster.hpp"
#endif

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_ADVANCES_H
#include FT_DRIVER_H
#include FT_MODULE_H
#include <stxlib/glyph_face.hpp>
#include <stxlib/glyph_shape.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using gui_forms::FontRole;
using gui_forms::FontSpec;
namespace text = gui_forms::render::text;

void require(const bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

// Where glyph positions come from: today's production engine (FreeType-hinted
// advances at the logical size) or HarfBuzz over unhinted outlines.
enum class Advances : std::uint8_t { production_hinted, unhinted };

struct RasterProfile final {
    std::string_view name;
    Advances advances{Advances::unhinted};
    bool autofit_darkening{};
    // Coverage c becomes 255 * (c / 255)^(1 / gamma); 1 leaves it unchanged.
    double coverage_gamma{1.0};
};

constexpr std::array<RasterProfile, 6> profiles{{
    {"light_hinted_advances", Advances::production_hinted, false, 1.0},
    {"light", Advances::unhinted, false, 1.0},
    {"light_gamma_1.2", Advances::unhinted, false, 1.2},
    {"light_gamma_1.45", Advances::unhinted, false, 1.45},
    {"light_gamma_1.7", Advances::unhinted, false, 1.7},
    {"light_darkened", Advances::unhinted, true, 1.0},
}};

constexpr FT_Int32 light_flags = FT_LOAD_NO_BITMAP | FT_LOAD_TARGET_LIGHT | FT_LOAD_FORCE_AUTOHINT;

enum class Compositing : std::uint8_t { srgb_naive, linear_light };

struct FontCase final {
    std::string_view name;
    std::string_view file;
};

constexpr std::array<FontCase, 2> fonts{{
    {"PortsmouthRapids", "PortsmouthRapids.ttf"},
    {"Carlito", "Carlito-Regular.ttf"},
}};

constexpr std::array<double, 5> sizes{9.0, 11.0, 13.0, 15.0, 20.0};
constexpr std::array<double, 3> scales{1.0, 1.5, 2.0};
constexpr std::string_view corpus = "Hamburgefonstiv quick brown fox 0123456789 Help";
constexpr double sheet_width = 420.0;
constexpr int timing_repeats = 20;

// Grayscale image, one byte per pixel, row-major, stride == width.
struct Gray final {
    int width{};
    int height{};
    std::vector<std::uint8_t> pixels;
};

Gray make_gray(const int width, const int height, const std::uint8_t value) {
    Gray result{};
    result.width = width;
    result.height = height;
    result.pixels.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), value);
    return result;
}

// ---- PNG writer: stored (uncompressed) deflate, no external dependency ----

std::array<std::uint32_t, 256> make_crc_table() {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t index = 0; index < 256U; ++index) {
        std::uint32_t value = index;
        for (int bit = 0; bit < 8; ++bit) {
            if ((value & 1U) != 0U) value = 0xEDB88320U ^ (value >> 1U);
            else value = value >> 1U;
        }
        table[index] = value;
    }
    return table;
}

std::uint32_t crc32(const std::array<std::uint32_t, 256>& table, std::span<const std::uint8_t> bytes) {
    std::uint32_t value = 0xFFFFFFFFU;
    for (const std::uint8_t byte : bytes) value = table[(value ^ byte) & 0xFFU] ^ (value >> 8U);
    return value ^ 0xFFFFFFFFU;
}

void append_u32(std::vector<std::uint8_t>& output, const std::uint32_t value) {
    output.push_back(static_cast<std::uint8_t>(value >> 24U));
    output.push_back(static_cast<std::uint8_t>(value >> 16U));
    output.push_back(static_cast<std::uint8_t>(value >> 8U));
    output.push_back(static_cast<std::uint8_t>(value));
}

void append_chunk(std::vector<std::uint8_t>& file, const std::array<std::uint32_t, 256>& table,
                  const char* type, const std::vector<std::uint8_t>& data) {
    append_u32(file, static_cast<std::uint32_t>(data.size()));
    std::vector<std::uint8_t> typed{static_cast<std::uint8_t>(type[0]), static_cast<std::uint8_t>(type[1]),
                                    static_cast<std::uint8_t>(type[2]), static_cast<std::uint8_t>(type[3])};
    typed.insert(typed.end(), data.begin(), data.end());
    file.insert(file.end(), typed.begin(), typed.end());
    append_u32(file, crc32(table, typed));
}

void write_png(const std::filesystem::path& path, const Gray& image) {
    const std::array<std::uint32_t, 256> table = make_crc_table();
    std::vector<std::uint8_t> raw;
    raw.reserve(static_cast<std::size_t>(image.height) * static_cast<std::size_t>(image.width + 1));
    for (int row = 0; row < image.height; ++row) {
        raw.push_back(0U);
        const std::size_t start = static_cast<std::size_t>(row) * static_cast<std::size_t>(image.width);
        raw.insert(raw.end(), image.pixels.begin() + static_cast<std::ptrdiff_t>(start),
                   image.pixels.begin() + static_cast<std::ptrdiff_t>(start + static_cast<std::size_t>(image.width)));
    }
    std::vector<std::uint8_t> deflate{0x78U, 0x01U};
    std::uint32_t adler_a = 1U;
    std::uint32_t adler_b = 0U;
    std::size_t offset = 0;
    while (offset < raw.size()) {
        const std::size_t length = std::min<std::size_t>(65535U, raw.size() - offset);
        const bool last = offset + length == raw.size();
        deflate.push_back(last ? 1U : 0U);
        deflate.push_back(static_cast<std::uint8_t>(length));
        deflate.push_back(static_cast<std::uint8_t>(length >> 8U));
        deflate.push_back(static_cast<std::uint8_t>(~length));
        deflate.push_back(static_cast<std::uint8_t>(~length >> 8U));
        for (std::size_t index = offset; index < offset + length; ++index) {
            deflate.push_back(raw[index]);
            adler_a = (adler_a + raw[index]) % 65521U;
            adler_b = (adler_b + adler_a) % 65521U;
        }
        offset += length;
    }
    append_u32(deflate, (adler_b << 16U) | adler_a);
    std::vector<std::uint8_t> file{0x89U, 'P', 'N', 'G', '\r', '\n', 0x1AU, '\n'};
    std::vector<std::uint8_t> header;
    append_u32(header, static_cast<std::uint32_t>(image.width));
    append_u32(header, static_cast<std::uint32_t>(image.height));
    header.push_back(8U);  // bit depth
    header.push_back(0U);  // grayscale
    header.push_back(0U);
    header.push_back(0U);
    header.push_back(0U);
    append_chunk(file, table, "IHDR", header);
    append_chunk(file, table, "IDAT", deflate);
    append_chunk(file, table, "IEND", {});
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
    require(static_cast<bool>(stream), "PNG written");
}

// ---- Compositing of coverage (0 = no ink) over white with black ink ----

double srgb_to_linear(const double value) {
    if (value <= 0.04045) return value / 12.92;
    return std::pow((value + 0.055) / 1.055, 2.4);
}

double linear_to_srgb(const double value) {
    if (value <= 0.0031308) return value * 12.92;
    return 1.055 * std::pow(value, 1.0 / 2.4) - 0.055;
}

std::uint8_t composite(const std::uint8_t coverage, const Compositing rule) {
    const double alpha = static_cast<double>(coverage) / 255.0;
    double light = 1.0 - alpha;
    if (rule == Compositing::linear_light) {
        // Blend white and black in linear light, then encode: black ink over
        // white keeps 1 - alpha of the white's linear luminance.
        light = linear_to_srgb((1.0 - alpha) * srgb_to_linear(1.0));
    }
    const long encoded = std::lround(std::clamp(light, 0.0, 1.0) * 255.0);
    return static_cast<std::uint8_t>(encoded);
}

// ---- Shaping: glyph IDs with positions in logical 26.6 units ----

struct PlacedGlyph final {
    std::uint32_t glyph{};
    std::int64_t x{};
    std::int64_t y{};
};

struct PlacedLine final {
    std::vector<PlacedGlyph> glyphs;
    std::int64_t width{};
};

std::vector<std::byte> read_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    require(static_cast<bool>(stream), "font file readable");
    const std::vector<char> characters((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    std::vector<std::byte> bytes(characters.size());
    for (std::size_t index = 0; index < characters.size(); ++index) {
        bytes[index] = static_cast<std::byte>(characters[index]);
    }
    return bytes;
}

// Today's production positions: the HarfBuzz engine with hinted advances.
PlacedLine production_line(text::HarfBuzzFontEngine& engine, const double size) {
    const text::ShapedText shaped = engine.shape(corpus, FontSpec{.role = FontRole::control, .size = size});
    PlacedLine line{};
    for (const text::ShapedFontRun& run : shaped.runs) {
        for (const text::ShapedGlyph& glyph : run.glyphs) {
            line.glyphs.push_back({glyph.glyph.value, std::llround(static_cast<double>(glyph.x) * 64.0),
                                   std::llround(static_cast<double>(glyph.y) * 64.0)});
        }
    }
    line.width = std::llround(shaped.width * 64.0);
    return line;
}

// The chosen shaping: STXlib's shaper with unhinted advances, at the
// logical size (HarfBuzz's for a FreeType font at that size, unhinted).
// Kerning and GPOS are the font's own.
PlacedLine unhinted_line(const std::vector<std::byte>& font, const double size) {
    const stxlib::View<stxlib::u8> bytes(reinterpret_cast<const stxlib::u8*>(font.data()), font.size());
    stxlib::Result<stxlib::Shared<stxlib::glyphs::Typeface>> opened = stxlib::glyphs::open_typeface(bytes, 0U);
    require(opened.succeeded(), "shaping typeface opened");
    const stxlib::Shared<stxlib::glyphs::Typeface> typeface = std::move(opened).take_value();
    stxlib::glyphs::Shaper shaper{};
    require(shaper.open(typeface).succeeded(), "shaper opened");
    std::vector<std::uint32_t> characters{};
    std::size_t offset = 0U;
    while (offset < corpus.size()) {
        const unsigned char first = static_cast<unsigned char>(corpus[offset]);
        std::uint32_t scalar = first;
        std::size_t following = 0U;
        if (first >= 0xF0U) { scalar = first & 7U; following = 3U; }
        else if (first >= 0xE0U) { scalar = first & 15U; following = 2U; }
        else if (first >= 0xC0U) { scalar = first & 31U; following = 1U; }
        for (std::size_t index = 1U; index <= following; ++index) {
            scalar = (scalar << 6U) | (static_cast<unsigned char>(corpus[offset + index]) & 63U);
        }
        characters.push_back(scalar);
        offset += following + 1U;
    }
    stxlib::glyphs::TextRun run{};
    run.characters = stxlib::View<std::uint32_t>(characters.data(), characters.size());
    run.size = static_cast<std::int64_t>(std::lround(size * 64.0));
    stxlib::Vector<stxlib::glyphs::ShapedGlyph> glyphs{};
    require(shaper.shape(run, glyphs).succeeded(), "corpus shaped");
    PlacedLine line{};
    std::int64_t pen = 0;
    for (const stxlib::glyphs::ShapedGlyph& glyph : glyphs) {
        line.glyphs.push_back({glyph.glyph, pen + glyph.x_offset, -static_cast<std::int64_t>(glyph.y_offset)});
        pen += glyph.x_advance;
    }
    line.width = pen;
    return line;
}

// ---- FreeType rasterization for one profile ----

// One library and face per profile: stem darkening is a library property.
class ProfileRasterizer final {
public:
    ProfileRasterizer(const RasterProfile& profile, const std::vector<std::byte>& font) : profile_(profile) {
        require(FT_Init_FreeType(&library_) == 0, "FreeType initialized");
        if (profile.autofit_darkening) {
            const FT_Bool no_darkening = 0;
            require(FT_Property_Set(library_, "autofitter", "no-stem-darkening", &no_darkening) == 0,
                    "autofitter darkening enabled");
        }
        require(FT_New_Memory_Face(library_, reinterpret_cast<const FT_Byte*>(font.data()),
                                   static_cast<FT_Long>(font.size()), 0, &face_) == 0, "face opened");
        for (int value = 0; value < 256; ++value) {
            const double normalized = static_cast<double>(value) / 255.0;
            const double adjusted = std::pow(normalized, 1.0 / profile.coverage_gamma);
            gamma_[static_cast<std::size_t>(value)] = static_cast<std::uint8_t>(std::lround(adjusted * 255.0));
        }
    }
    ~ProfileRasterizer() {
        FT_Done_Face(face_);
        FT_Done_FreeType(library_);
    }
    ProfileRasterizer(const ProfileRasterizer&) = delete;
    ProfileRasterizer& operator=(const ProfileRasterizer&) = delete;

    FT_Face face() const noexcept { return face_; }

    void set_ppem(const double ppem) {
        require(FT_Set_Char_Size(face_, 0, static_cast<FT_F26Dot6>(std::lround(ppem * 64.0)), 72U, 72U) == 0, "size set");
    }

    // Places glyphs at origin + position * scale in device 26.6, draws them
    // with light hinting, and keeps the maximum coverage where boxes overlap.
    std::size_t draw(const PlacedLine& line, const std::int64_t origin_x, const std::int64_t baseline_y,
                     const double scale, Gray& coverage) {
        for (const PlacedGlyph& glyph : line.glyphs) {
            const std::int64_t device_x = origin_x + std::llround(static_cast<double>(glyph.x) * scale);
            const std::int64_t device_y = baseline_y + std::llround(static_cast<double>(glyph.y) * scale);
            const std::int64_t pixel_x = floor_div_64(device_x);
            const std::int64_t pixel_y = floor_div_64(device_y);
            FT_Vector delta{static_cast<FT_Pos>(device_x - pixel_x * 64), -static_cast<FT_Pos>(device_y - pixel_y * 64)};
            FT_Set_Transform(face_, nullptr, &delta);
            require(FT_Load_Glyph(face_, glyph.glyph, light_flags) == 0, "glyph loaded");
            FT_GlyphSlot slot = (*face_).glyph;
            require((*slot).format == FT_GLYPH_FORMAT_OUTLINE, "outline glyph");
            require(FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL) == 0, "glyph rendered");
            blend(*slot, static_cast<int>(pixel_x), static_cast<int>(pixel_y), coverage);
        }
        return line.glyphs.size();
    }

    // Sum of unhinted outline advances at the current ppem, device pixels.
    double outline_advance(const PlacedLine& line) {
        double total = 0.0;
        for (const PlacedGlyph& glyph : line.glyphs) {
            FT_Fixed advance{};
            require(FT_Get_Advance(face_, glyph.glyph, FT_LOAD_NO_HINTING, &advance) == 0, "advance read");
            total += static_cast<double>(advance) / 65536.0;
        }
        return total;
    }

private:
    static std::int64_t floor_div_64(const std::int64_t value) {
        std::int64_t quotient = value / 64;
        if (value % 64 != 0 && value < 0) --quotient;
        return quotient;
    }

    void blend(const FT_GlyphSlotRec& slot, const int pen_x, const int pen_y, Gray& coverage) const {
        const FT_Bitmap& bitmap = slot.bitmap;
        const int left = pen_x + slot.bitmap_left;
        const int top = pen_y - slot.bitmap_top;
        for (unsigned row = 0; row < bitmap.rows; ++row) {
            const int y = top + static_cast<int>(row);
            if (y < 0 || y >= coverage.height) continue;
            for (unsigned column = 0; column < bitmap.width; ++column) {
                const int x = left + static_cast<int>(column);
                if (x < 0 || x >= coverage.width) continue;
                const std::uint8_t raw = bitmap.buffer[static_cast<std::size_t>(row) * static_cast<std::size_t>(bitmap.pitch) + column];
                const std::uint8_t value = gamma_[raw];
                std::uint8_t& target = coverage.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(coverage.width) +
                                                       static_cast<std::size_t>(x)];
                target = std::max(target, value);
            }
        }
    }

    const RasterProfile& profile_;
    FT_Library library_{};
    FT_Face face_{};
    std::array<std::uint8_t, 256> gamma_{};
};

// ---- Measurements ----

struct LineBox final {
    int top{};
    int height{};
};

double ink(const Gray& coverage, const LineBox box) {
    double total = 0.0;
    for (int y = box.top; y < box.top + box.height; ++y) {
        for (int x = 0; x < coverage.width; ++x) {
            total += static_cast<double>(coverage.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(coverage.width) +
                                                         static_cast<std::size_t>(x)]) / 255.0;
        }
    }
    return total;
}

double mean_abs_difference(const Gray& left, const Gray& right, const LineBox box) {
    double total = 0.0;
    const double count = static_cast<double>(box.height) * static_cast<double>(left.width);
    for (int y = box.top; y < box.top + box.height; ++y) {
        for (int x = 0; x < left.width; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * static_cast<std::size_t>(left.width) + static_cast<std::size_t>(x);
            total += std::abs(static_cast<double>(left.pixels[index]) - static_cast<double>(right.pixels[index]));
        }
    }
    return total / count;
}

// FNV-1a over coverage bytes: a cross-platform identity check, not a digest.
std::uint64_t coverage_hash(const Gray& coverage, const LineBox box) {
    std::uint64_t value = 1469598103934665603ULL;
    for (int y = box.top; y < box.top + box.height; ++y) {
        for (int x = 0; x < coverage.width; ++x) {
            value ^= coverage.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(coverage.width) +
                                     static_cast<std::size_t>(x)];
            value *= 1099511628211ULL;
        }
    }
    return value;
}

Gray composite_sheet(const Gray& coverage, const Compositing rule) {
    Gray result = make_gray(coverage.width, coverage.height, 255U);
    for (std::size_t index = 0; index < coverage.pixels.size(); ++index) {
        result.pixels[index] = composite(coverage.pixels[index], rule);
    }
    return result;
}

// ---- Polarity analysis: is text weight independent of the theme? ----

enum class BlendSpace : std::uint8_t { srgb_values, linear_light };

constexpr std::array<double, 4> analysis_gammas{1.0, 1.2, 1.45, 1.7};

// CIE 1976 lightness L* of an 8-bit sRGB gray.
double lightness(const std::uint8_t value) {
    const double luminance = srgb_to_linear(static_cast<double>(value) / 255.0);
    const double epsilon = 216.0 / 24389.0;
    double scaled = luminance * 24389.0 / 27.0;
    if (luminance > epsilon) scaled = 116.0 * std::cbrt(luminance) - 16.0;
    return scaled;
}

// Ink color fg over background bg at coverage alpha, as an 8-bit sRGB gray.
std::uint8_t blend_gray(const std::uint8_t foreground, const std::uint8_t background, const double alpha,
                        const BlendSpace space) {
    double result = 0.0;
    if (space == BlendSpace::srgb_values) {
        result = static_cast<double>(background) + (static_cast<double>(foreground) - static_cast<double>(background)) * alpha;
    } else {
        const double back = srgb_to_linear(static_cast<double>(background) / 255.0);
        const double fore = srgb_to_linear(static_cast<double>(foreground) / 255.0);
        result = linear_to_srgb(back + (fore - back) * alpha) * 255.0;
    }
    const long rounded = std::lround(std::clamp(result, 0.0, 255.0));
    return static_cast<std::uint8_t>(rounded);
}

// Perceived ink in units of fully inked pixels: L* distance from the
// background, normalized by the full foreground-to-background distance.
double perceived_weight(const Gray& coverage, const LineBox box, const std::array<std::uint8_t, 256>& gamma,
                        const std::uint8_t foreground, const std::uint8_t background, const BlendSpace space) {
    const double background_lightness = lightness(background);
    const double span = std::abs(lightness(foreground) - background_lightness);
    std::array<double, 256> per_coverage{};
    for (int value = 0; value < 256; ++value) {
        const double alpha = static_cast<double>(gamma[static_cast<std::size_t>(value)]) / 255.0;
        const std::uint8_t shown = blend_gray(foreground, background, alpha, space);
        per_coverage[static_cast<std::size_t>(value)] = std::abs(lightness(shown) - background_lightness) / span;
    }
    double total = 0.0;
    for (int y = box.top; y < box.top + box.height; ++y) {
        for (int x = 0; x < coverage.width; ++x) {
            total += per_coverage[coverage.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(coverage.width) +
                                                  static_cast<std::size_t>(x)]];
        }
    }
    return total;
}

std::array<std::uint8_t, 256> gamma_table(const double gamma) {
    std::array<std::uint8_t, 256> table{};
    for (int value = 0; value < 256; ++value) {
        const double adjusted = std::pow(static_cast<double>(value) / 255.0, 1.0 / gamma);
        table[static_cast<std::size_t>(value)] = static_cast<std::uint8_t>(std::lround(adjusted * 255.0));
    }
    return table;
}

Gray polarity_image(const Gray& coverage, const std::array<std::uint8_t, 256>& gamma, const std::uint8_t foreground,
                    const std::uint8_t background, const BlendSpace space) {
    Gray result = make_gray(coverage.width, coverage.height, background);
    for (std::size_t index = 0; index < coverage.pixels.size(); ++index) {
        const double alpha = static_cast<double>(gamma[coverage.pixels[index]]) / 255.0;
        result.pixels[index] = blend_gray(foreground, background, alpha, space);
    }
    return result;
}

// Outline area is the ungamma'd coverage sum: exact analytic coverage.
void analyze_polarity(const FontCase& font_case, const double scale, const std::span<const LineBox> boxes,
                      const Gray& coverage, std::ofstream& polarity_csv, const std::filesystem::path& output) {
    std::vector<Gray> blocks;
    for (const BlendSpace space : {BlendSpace::srgb_values, BlendSpace::linear_light}) {
        for (const double gamma_value : analysis_gammas) {
            const std::array<std::uint8_t, 256> gamma = gamma_table(gamma_value);
            for (std::size_t index = 0; index < boxes.size(); ++index) {
                const double area = ink(coverage, boxes[index]);
                const double on_light = perceived_weight(coverage, boxes[index], gamma, 0U, 255U, space);
                const double on_dark = perceived_weight(coverage, boxes[index], gamma, 255U, 0U, space);
                polarity_csv << font_case.name << ',' << scale << ',' << sizes[index] << ','
                             << (space == BlendSpace::srgb_values ? "srgb" : "linear") << ',' << gamma_value << ','
                             << area << ',' << on_light / area << ',' << on_dark / area << ',' << on_dark / on_light << '\n';
            }
            if (gamma_value == 1.0 || gamma_value == 1.45) {
                blocks.push_back(polarity_image(coverage, gamma, 0U, 255U, space));
                blocks.push_back(polarity_image(coverage, gamma, 255U, 0U, space));
            }
        }
    }
    const int gap = 6;
    const int block_height = coverage.height;
    Gray sheet = make_gray(coverage.width, static_cast<int>(blocks.size()) * (block_height + gap), 128U);
    for (std::size_t block = 0; block < blocks.size(); ++block) {
        const std::size_t offset = block * static_cast<std::size_t>(block_height + gap) * static_cast<std::size_t>(coverage.width);
        std::copy(blocks[block].pixels.begin(), blocks[block].pixels.end(), sheet.pixels.begin() + static_cast<std::ptrdiff_t>(offset));
    }
    char name[160]{};
    std::snprintf(name, sizeof(name), "polarity_%s_scale%.1f.png", std::string(font_case.name).c_str(), scale);
    write_png(output / name, sheet);
}

double line_height() {
    return sizes.back() * 1.6;
}

std::int64_t baseline_26_6(const std::size_t index, const double scale) {
    return std::llround((static_cast<double>(index) + 0.75) * line_height() * scale * 64.0);
}

#if defined(GUI_FORMS_PROFILE_PROBE_SKIA_REFERENCE)
// Skia reference coverage: black on white through draw_text_utf8 (production
// shaping and the platform rasterizer), inverted to coverage.
Gray skia_reference(const std::filesystem::path& font_path, const double scale, const int width_pixels,
                    const int height_pixels) {
    gui_forms::render::SkiaRaster raster{};
    const double logical_height = static_cast<double>(height_pixels) / scale;
    require(raster.resize({sheet_width, logical_height}, scale), "reference raster allocated");
    const std::string path_text = font_path.string();
    require(raster.register_typeface_file(FontRole::control, 400, false, path_text.c_str()), "reference font registered");
    gui_forms::DamageRegion damage{};
    damage.add({0.0, 0.0, sheet_width, logical_height});
    raster.begin_frame(damage);
    raster.fill_rect({0.0, 0.0, sheet_width, logical_height}, gui_forms::Color::rgba(255, 255, 255, 255));
    for (std::size_t index = 0; index < sizes.size(); ++index) {
        const double baseline = static_cast<double>(baseline_26_6(index, scale)) / 64.0 / scale;
        const FontSpec font{.role = FontRole::control, .size = sizes[index]};
        raster.draw_text_utf8({4.0, baseline}, corpus, font, gui_forms::Color::rgba(0, 0, 0, 255));
    }
    raster.end_frame();
    const std::uint8_t* const bytes = static_cast<const std::uint8_t*>(raster.pixels());
    require(bytes != nullptr, "reference readable");
    Gray coverage = make_gray(width_pixels, height_pixels, 0U);
    for (int y = 0; y < coverage.height && y < static_cast<int>(raster.pixel_height()); ++y) {
        for (int x = 0; x < coverage.width && x < static_cast<int>(raster.pixel_width()); ++x) {
            const std::uint8_t red = bytes[static_cast<std::size_t>(y) * raster.row_bytes() + static_cast<std::size_t>(x) * 4U];
            coverage.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_pixels) + static_cast<std::size_t>(x)] =
                static_cast<std::uint8_t>(255U - red);
        }
    }
    return coverage;
}
#endif

void probe_font(const FontCase& font_case, const std::filesystem::path& font_directory,
                const std::filesystem::path& output, std::ofstream& csv, std::ofstream& polarity_csv) {
    const std::filesystem::path font_path = font_directory / font_case.file;
    const std::vector<std::byte> font = read_file(font_path);
    text::HarfBuzzFontEngine engine{};
    require(engine.register_typeface(FontRole::control, 400, false, font).has_value(), "engine font registered");
    std::vector<PlacedLine> production;
    std::vector<PlacedLine> unhinted;
    for (const double size : sizes) {
        production.push_back(production_line(engine, size));
        unhinted.push_back(unhinted_line(font, size));
    }
    for (const double scale : scales) {
        const int width_pixels = static_cast<int>(std::ceil(sheet_width * scale));
        std::vector<LineBox> boxes;
        for (std::size_t index = 0; index < sizes.size(); ++index) {
            const int top = static_cast<int>(std::floor(static_cast<double>(index) * line_height() * scale));
            const int bottom = static_cast<int>(std::floor(static_cast<double>(index + 1U) * line_height() * scale));
            boxes.push_back({top, bottom - top});
        }
        const int height_pixels = boxes.back().top + boxes.back().height;
        std::optional<Gray> reference{};
#if defined(GUI_FORMS_PROFILE_PROBE_SKIA_REFERENCE)
        reference = skia_reference(font_path, scale, width_pixels, height_pixels);
#endif
        std::vector<Gray> rows;
        std::vector<std::string> row_names;
        if (reference) {
            rows.push_back(*reference);
            row_names.push_back("skia_reference");
        }
        const std::int64_t origin = std::llround(4.0 * scale * 64.0);
        for (const RasterProfile& profile : profiles) {
            ProfileRasterizer rasterizer(profile, font);
            const std::vector<PlacedLine>& lines = profile.advances == Advances::unhinted ? unhinted : production;
            Gray coverage = make_gray(width_pixels, height_pixels, 0U);
            for (std::size_t index = 0; index < sizes.size(); ++index) {
                rasterizer.set_ppem(sizes[index] * scale);
                const std::int64_t baseline = baseline_26_6(index, scale);
                const std::size_t glyphs = rasterizer.draw(lines[index], origin, baseline, scale, coverage);
                Gray scratch = make_gray(width_pixels, height_pixels, 0U);
                const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
                for (int repeat = 0; repeat < timing_repeats; ++repeat) {
                    static_cast<void>(rasterizer.draw(lines[index], origin, baseline, scale, scratch));
                }
                const std::chrono::steady_clock::duration elapsed = std::chrono::steady_clock::now() - started;
                const double nanoseconds_per_glyph =
                    static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count()) /
                    static_cast<double>(glyphs * static_cast<std::size_t>(timing_repeats));
                const double placed_width = static_cast<double>(lines[index].width) / 64.0 * scale;
                const double drift = placed_width - rasterizer.outline_advance(lines[index]);
                const LineBox box = boxes[index];
                const double profile_ink = ink(coverage, box);
                double ink_ratio = 0.0;
                double difference = -1.0;
                if (reference) {
                    ink_ratio = profile_ink / std::max(1e-9, ink(*reference, box));
                    difference = mean_abs_difference(coverage, *reference, box);
                }
                char hash[17]{};
                std::snprintf(hash, sizeof(hash), "%016llx", static_cast<unsigned long long>(coverage_hash(coverage, box)));
                csv << font_case.name << ',' << scale << ',' << sizes[index] << ',' << profile.name << ','
                    << profile_ink << ',' << ink_ratio << ',' << difference << ',' << hash << ','
                    << drift << ',' << nanoseconds_per_glyph << '\n';
            }
            if (profile.name == "light") analyze_polarity(font_case, scale, boxes, coverage, polarity_csv, output);
            rows.push_back(std::move(coverage));
            row_names.push_back(std::string(profile.name));
        }
        for (const Compositing rule : {Compositing::srgb_naive, Compositing::linear_light}) {
            const int gap = 6;
            Gray sheet = make_gray(width_pixels, static_cast<int>(rows.size()) * (height_pixels + gap), 200U);
            for (std::size_t block = 0; block < rows.size(); ++block) {
                // The reference is already composited pixels; invert it unchanged.
                Compositing block_rule = rule;
                if (reference && block == 0U) block_rule = Compositing::srgb_naive;
                const Gray composited = composite_sheet(rows[block], block_rule);
                const std::size_t offset = block * static_cast<std::size_t>(height_pixels + gap) * static_cast<std::size_t>(width_pixels);
                std::copy(composited.pixels.begin(), composited.pixels.end(),
                          sheet.pixels.begin() + static_cast<std::ptrdiff_t>(offset));
            }
            char name[160]{};
            std::snprintf(name, sizeof(name), "%s_scale%.1f_%s.png", std::string(font_case.name).c_str(), scale,
                          rule == Compositing::srgb_naive ? "srgb" : "linear");
            write_png(output / name, sheet);
        }
        std::ofstream order(output / (std::string(font_case.name) + "_row_order.txt"));
        for (const std::string& row_name : row_names) order << row_name << '\n';
    }
}

} // namespace

int main(int argument_count, char** arguments) {
    require(argument_count == 3, "usage: gui_forms_text_raster_profile_probe <font-directory> <output-directory>");
    const std::filesystem::path font_directory = arguments[1];
    const std::filesystem::path output = arguments[2];
    std::filesystem::create_directories(output);
    std::ofstream csv(output / "profiles.csv");
    csv << "font,scale,size_pt,profile,ink,ink_ratio_vs_reference,mean_abs_difference_vs_reference,"
           "coverage_fnv1a,placed_minus_outline_px,raster_ns_per_glyph\n";
    std::ofstream polarity_csv(output / "polarity.csv");
    polarity_csv << "font,scale,size_pt,blend,gamma,outline_area_px,weight_on_light_vs_area,"
                    "weight_on_dark_vs_area,dark_over_light\n";
    for (const FontCase& font_case : fonts) {
        probe_font(font_case, font_directory, output, csv, polarity_csv);
    }
    require(static_cast<bool>(csv) && static_cast<bool>(polarity_csv), "CSVs written");
    return 0;
}
