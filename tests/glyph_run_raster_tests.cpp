#include "../src/render/text/glyph_run/glyph_run_raster.hpp"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

// ADR-004 glyph-run rasterization: light auto-hinting, grayscale, integer
// 26.6 placement with a snapped baseline and a 1/64 pixel horizontal phase.
namespace {
using namespace gui_forms;
using namespace gui_forms::render::text;

void require(const bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

std::vector<std::byte> read_file(const char* path) {
    std::ifstream stream(path, std::ios::binary);
    require(static_cast<bool>(stream), "font fixture opens");
    const std::vector<char> characters((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    std::vector<std::byte> bytes(characters.size());
    for (std::size_t index = 0; index < characters.size(); ++index) bytes[index] = static_cast<std::byte>(characters[index]);
    return bytes;
}

double ink(const GlyphRunCoverage& coverage) {
    double total = 0.0;
    for (const std::uint8_t value : coverage.pixels) total += static_cast<double>(value) / 255.0;
    return total;
}

const std::string corpus = "Hamburgefonstiv quick brown fox 0123456789 Help";

void origin_keeps_phase_and_snaps_baseline() {
    const GlyphRunOrigin positive = glyph_run_origin(10.25, 20.6);
    require(positive.pixel_x == 10 && positive.phase_x == 16 && positive.pixel_y == 21, "positive origin phase and baseline");
    const GlyphRunOrigin negative = glyph_run_origin(-0.5, -2.4);
    require(negative.pixel_x == -1 && negative.phase_x == 32 && negative.pixel_y == -2, "negative origin floors its pixel");
    const GlyphRunOrigin whole = glyph_run_origin(7.0, 3.0);
    require(whole.pixel_x == 7 && whole.phase_x == 0 && whole.pixel_y == 3, "whole-pixel origin has no phase");
}

void rasterization_is_deterministic_and_scales(const char* font_path) {
    HarfBuzzFontEngine engine;
    require(engine.register_typeface(FontRole::control, 400, false, read_file(font_path)).has_value(), "font registers");
    const FontSpec font{FontRole::control, 13.0, 400, false};
    const ShapedText shaped = engine.shape(corpus, font);
    GlyphRunRasterizer rasterizer(engine);
    const GlyphRunCoverage first = rasterizer.rasterize(shaped, font, 1.0, 0);
    const GlyphRunCoverage again = rasterizer.rasterize(shaped, font, 1.0, 0);
    require(first.width > 0 && first.height > 0 && first.missing_glyphs == 0U, "run rasterizes completely");
    require(first.pixels == again.pixels && first.left == again.left && first.top == again.top,
            "identical input gives identical coverage");
    // The mask spans the shaped advance (within a pixel of side bearings).
    require(std::abs(static_cast<double>(first.width) - shaped.width) < 3.0, "mask width follows the shaped width");
    // Ink is conserved across subpixel phases.
    const GlyphRunCoverage shifted = rasterizer.rasterize(shaped, font, 1.0, 32);
    require(shifted.pixels != first.pixels, "phase moves coverage");
    require(std::abs(ink(shifted) - ink(first)) / ink(first) < 0.03, "phase preserves ink");
    // Ink grows with the square of the device scale.
    const GlyphRunCoverage doubled = rasterizer.rasterize(shaped, font, 2.0, 0);
    const double ratio = ink(doubled) / ink(first);
    require(ratio > 3.6 && ratio < 4.4, "ink scales with device area");
    require(std::abs(static_cast<double>(doubled.width) - shaped.width * 2.0) < 4.0, "2x mask spans twice the width");
}

// FNV-1a over a mask and its placement. Printed so runs on different
// platforms can be compared for byte identity (ADR-004).
std::uint64_t coverage_hash(const GlyphRunCoverage& coverage) {
    std::uint64_t value = 1469598103934665603ULL;
    const std::int64_t header[4]{coverage.left, coverage.top, coverage.width, coverage.height};
    for (const std::int64_t field : header) {
        value ^= static_cast<std::uint64_t>(field);
        value *= 1099511628211ULL;
    }
    for (const std::uint8_t byte : coverage.pixels) {
        value ^= byte;
        value *= 1099511628211ULL;
    }
    return value;
}

// Expected hashes in report order (rapids then carlito; size; scale; phase),
// identical on macOS arm64 and Alpine aarch64 musl, 2026-10-11. Every CI
// platform must reproduce them.
constexpr std::uint64_t expected_identity[36]{
    0xc5214daa69de2890ULL,  // rapids 9 1 0
    0xc90c2fd02163deefULL,  // rapids 9 1 23
    0x64405a9166c0a3a9ULL,  // rapids 9 1.5 0
    0xc8375f0cd99fbb67ULL,  // rapids 9 1.5 23
    0x5e168d691cb0de4dULL,  // rapids 9 2 0
    0x24e647927da5a5bdULL,  // rapids 9 2 23
    0x7d7dba25a3eac0fULL,  // rapids 13 1 0
    0x62f300fa8b54567aULL,  // rapids 13 1 23
    0x1413749b73e00469ULL,  // rapids 13 1.5 0
    0x79e7eb9b291a9442ULL,  // rapids 13 1.5 23
    0x57a38740bff65ca1ULL,  // rapids 13 2 0
    0xeb1ece057f8e50fdULL,  // rapids 13 2 23
    0xf8185b254e18bd6bULL,  // rapids 20 1 0
    0xc0ffb19c5f732c24ULL,  // rapids 20 1 23
    0x808b1880a380e506ULL,  // rapids 20 1.5 0
    0xb5ac0ec84e0bc0f7ULL,  // rapids 20 1.5 23
    0x20453454113ca2d2ULL,  // rapids 20 2 0
    0xc683469cb990571cULL,  // rapids 20 2 23
    0x1fe2c22874baa87ULL,  // carlito 9 1 0
    0xae3f9580e1f2be27ULL,  // carlito 9 1 23
    0x4e53156bb6f6a168ULL,  // carlito 9 1.5 0
    0xb1703db90555fb4ULL,  // carlito 9 1.5 23
    0x8b378aa0547e6fc9ULL,  // carlito 9 2 0
    0x3b71d356a2251cc6ULL,  // carlito 9 2 23
    0x402f75218c874ea7ULL,  // carlito 13 1 0
    0x5acd323bbe73f564ULL,  // carlito 13 1 23
    0xd3ae1596e22aa3d8ULL,  // carlito 13 1.5 0
    0xae12c2447d728a4bULL,  // carlito 13 1.5 23
    0x167ec98c14fbe984ULL,  // carlito 13 2 0
    0xc9832d66402bfbecULL,  // carlito 13 2 23
    0x3710d46d4095643fULL,  // carlito 20 1 0
    0xccfa8f070265b808ULL,  // carlito 20 1 23
    0xea2b90c0c5764bd8ULL,  // carlito 20 1.5 0
    0xda97a2a42763cefeULL,  // carlito 20 1.5 23
    0x1d1f6592b02fdc77ULL,  // carlito 20 2 0
    0xee09aa358d62088aULL,  // carlito 20 2 23
};

std::size_t identity_index = 0;

void report_identity_hashes(const char* name, const char* font_path) {
    HarfBuzzFontEngine engine;
    require(engine.register_typeface(FontRole::control, 400, false, read_file(font_path)).has_value(), "font registers");
    GlyphRunRasterizer rasterizer(engine);
    const double sizes[3]{9.0, 13.0, 20.0};
    const double scales[3]{1.0, 1.5, 2.0};
    const std::int32_t phases[2]{0, 23};
    for (const double size : sizes) {
        const FontSpec font{FontRole::control, size, 400, false};
        const ShapedText shaped = engine.shape(corpus, font);
        for (const double scale : scales) {
            for (const std::int32_t phase : phases) {
                const GlyphRunCoverage coverage = rasterizer.rasterize(shaped, font, scale, phase);
                const std::uint64_t observed = coverage_hash(coverage);
                std::cout << "identity " << name << ' ' << size << ' ' << scale << ' ' << phase << ' ' << std::hex
                          << observed << std::dec << '\n';
                require(identity_index < 36U && observed == expected_identity[identity_index],
                        "glyph-run coverage differs from the cross-platform expectation");
                ++identity_index;
            }
        }
    }
}

// Bold and oblique synthesis, and the unhinted general-transform path used
// by the C drawing API.
void synthesis_and_transforms(const char* font_path, const std::uint64_t (&expected)[5]) {
    HarfBuzzFontEngine engine;
    require(engine.register_typeface(FontRole::control, 400, false, read_file(font_path)).has_value(), "font registers");
    const FontSpec font{FontRole::control, 16.0, 400, false};
    const ShapedText shaped = engine.shape(corpus, font);
    GlyphRunRasterizer rasterizer(engine);
    const GlyphRunCoverage plain = rasterizer.rasterize(shaped, font, 1.0, 0);
    const GlyphRunCoverage bold = rasterizer.rasterize(shaped, font, 1.0, 0, {true, false});
    const double bold_ratio = ink(bold) / ink(plain);
    require(bold_ratio > 1.15 && bold_ratio < 1.8, "embolden thickens strokes");
    const GlyphRunCoverage oblique = rasterizer.rasterize(shaped, font, 1.0, 0, {false, true});
    require(oblique.pixels != plain.pixels, "oblique changes coverage");
    require(std::abs(ink(oblique) - ink(plain)) / ink(plain) < 0.06, "oblique preserves ink");
    // Shear leans tops right: ascenders reach past the plain mask's right edge.
    require(oblique.left + static_cast<std::int32_t>(oblique.width) > plain.left + static_cast<std::int32_t>(plain.width),
            "oblique leans right");

    GlyphRunOrigin origin{};
    const GlyphRunCoverage identity = rasterizer.rasterize_transformed(shaped, font, {}, 10.25, 20.5, {}, origin);
    require(origin.pixel_x == 10 && origin.pixel_y == 20 && origin.phase_x == 0, "transformed origin floors both axes");
    require(std::abs(ink(identity) - ink(plain)) / ink(plain) < 0.08, "unhinted identity transform keeps ink");
    require(std::abs(static_cast<double>(identity.width) - static_cast<double>(plain.width)) <= 2.0,
            "unhinted identity transform keeps width");
    const GlyphRunCoverage again = rasterizer.rasterize_transformed(shaped, font, {}, 10.25, 20.5, {}, origin);
    require(again.pixels == identity.pixels && again.left == identity.left && again.top == identity.top,
            "transformed rasterization is deterministic");

    // A quarter turn clockwise (y down): the run reads downward and the
    // glyphs' tops face right.
    const GlyphRunCoverage turned = rasterizer.rasterize_transformed(shaped, font, {0.0, -1.0, 1.0, 0.0}, 0.0, 0.0, {}, origin);
    require(std::abs(ink(turned) - ink(identity)) / ink(identity) < 0.08, "rotation keeps ink");
    require(std::abs(static_cast<double>(turned.height) - shaped.width) < 3.0, "rotated run spans its advance vertically");
    require(turned.top >= -2 && turned.left >= -3, "rotated run lies below and right of its origin");

    const GlyphRunCoverage doubled = rasterizer.rasterize_transformed(shaped, font, {2.0, 0.0, 0.0, 2.0}, 0.0, 0.0, {}, origin);
    const double area_ratio = ink(doubled) / ink(identity);
    require(area_ratio > 3.6 && area_ratio < 4.4, "transformed ink scales with device area");
    const GlyphRunCoverage turned_bold = rasterizer.rasterize_transformed(shaped, font, {0.0, -1.0, 1.0, 0.0}, 0.0, 0.0,
                                                                          {true, true}, origin);
    require(ink(turned_bold) > ink(turned) * 1.15, "synthesis composes with rotation");

    bool refused = false;
    try {
        static_cast<void>(rasterizer.rasterize_transformed(shaped, font, {1.0, 2.0, 0.5, 1.0}, 0.0, 0.0, {}, origin));
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused, "singular transform is refused");
    // Bold, oblique, identity, quarter turn, quarter turn bold-oblique;
    // identical on macOS arm64 and Alpine aarch64 musl, 2026-10-11.
    const std::uint64_t observed[5]{coverage_hash(bold), coverage_hash(oblique), coverage_hash(identity),
                                    coverage_hash(turned), coverage_hash(turned_bold)};
    for (std::size_t index = 0; index < 5U; ++index) {
        std::cout << "synthesis " << index << ' ' << std::hex << observed[index] << std::dec << '\n';
        require(observed[index] == expected[index], "synthesized coverage differs from the cross-platform expectation");
    }
}

bool same_coverage(const GlyphRunCoverage& left, const GlyphRunCoverage& right) {
    const bool same = left.pixels == right.pixels && left.width == right.width && left.height == right.height &&
        left.left == right.left && left.top == right.top && left.missing_glyphs == right.missing_glyphs;
    return same;
}

// The rasterizer loads each distinct glyph of a run once and places that
// outline at each occurrence. The run must equal its glyphs rasterized one
// by one (each loaded afresh) and combined in run order as documented,
// c + p * (255 - c) / 255, for every synthesis.
void repeated_glyphs_match_single_glyph_runs(const char* font_path) {
    HarfBuzzFontEngine engine;
    require(engine.register_typeface(FontRole::control, 400, false, read_file(font_path)).has_value(), "font registers");
    const FontSpec font{FontRole::control, 13.0, 400, false};
    const ShapedText shaped = engine.shape(corpus, font);
    require(shaped.runs.size() == 1U, "corpus shapes as one run");
    GlyphRunRasterizer rasterizer(engine);
    const GlyphRunSynthesis syntheses[4]{{false, false}, {true, false}, {false, true}, {true, true}};
    for (const GlyphRunSynthesis synthesis : syntheses) {
        const GlyphRunCoverage whole = rasterizer.rasterize(shaped, font, 1.0, 37, synthesis);
        std::vector<std::uint8_t> combined(whole.pixels.size(), 0U);
        for (const ShapedGlyph& glyph : shaped.runs[0].glyphs) {
            ShapedText single{};
            ShapedFontRun run{};
            run.face = shaped.runs[0].face;
            run.glyphs.push_back(glyph);
            single.runs.push_back(run);
            const GlyphRunCoverage part = rasterizer.rasterize(single, font, 1.0, 37, synthesis);
            for (std::uint32_t row = 0; row < part.height; ++row) {
                for (std::uint32_t column = 0; column < part.width; ++column) {
                    const std::int64_t x = static_cast<std::int64_t>(part.left) - whole.left + column;
                    const std::int64_t y = static_cast<std::int64_t>(part.top) - whole.top + row;
                    require(x >= 0 && y >= 0 && x < static_cast<std::int64_t>(whole.width) &&
                            y < static_cast<std::int64_t>(whole.height), "glyph lies inside its run");
                    const std::size_t target = static_cast<std::size_t>(y) * whole.width + static_cast<std::size_t>(x);
                    const unsigned coverage = part.pixels[static_cast<std::size_t>(row) * part.width + column];
                    const unsigned previous = combined[target];
                    combined[target] = static_cast<std::uint8_t>(coverage + previous * (255U - coverage) / 255U);
                }
            }
        }
        require(combined == whole.pixels, "a run equals its glyphs rasterized one by one");
    }
}

// FT_Render_Glyph refuses a glyph box reaching ten ems from its origin; the
// run then fails as a whole, and the rasterizer stays usable.
void glyphs_beyond_ten_ems_are_refused(const char* font_path) {
    HarfBuzzFontEngine engine;
    require(engine.register_typeface(FontRole::control, 400, false, read_file(font_path)).has_value(), "font registers");
    const FontSpec font{FontRole::control, 16.0, 400, false};
    const ShapedText shaped = engine.shape(corpus, font);
    GlyphRunRasterizer rasterizer(engine);
    GlyphRunOrigin origin{};
    bool refused = false;
    try {
        static_cast<void>(rasterizer.rasterize_transformed(shaped, font, {16.0, 0.0, 0.0, 16.0}, 0.0, 0.0, {}, origin));
    } catch (const std::runtime_error&) {
        refused = true;
    }
    require(refused, "a glyph box beyond ten ems is refused");
    GlyphRunRasterizer fresh(engine);
    require(same_coverage(rasterizer.rasterize(shaped, font, 1.0, 9), fresh.rasterize(shaped, font, 1.0, 9)),
            "a refused run leaves the rasterizer usable");
}

void unknown_faces_are_reported() {
    HarfBuzzFontEngine engine;
    GlyphRunRasterizer rasterizer(engine);
    ShapedText shaped{};
    ShapedFontRun run{};
    run.face = FontFaceId{999U};
    run.glyphs.push_back(ShapedGlyph{});
    shaped.runs.push_back(run);
    const GlyphRunCoverage coverage = rasterizer.rasterize(shaped, {FontRole::control, 13.0, 400, false}, 1.0, 0);
    require(coverage.missing_glyphs == 1U && coverage.pixels.empty(), "unknown face draws nothing and is counted");
}

// A-gf-core-10-F1: a line whose ink is wider than glyph_run_maximum_extent
// is reported as oversized and draws nothing, rather than throwing out of
// paint; with a window, only the glyphs that can show are rendered, so the
// same line draws its visible part.
void long_lines_fail_soft_and_window_limits_glyphs(const char* font_path) {
    HarfBuzzFontEngine engine;
    require(engine.register_typeface(FontRole::control, 400, false, read_file(font_path)).has_value(), "font registers");
    const FontSpec font{FontRole::control, 13.0, 400, false};
    std::string line;
    while (line.size() < 6000U) line += corpus;
    line += ' ';
    const ShapedText shaped = engine.shape(line, font);
    require(shaped.width * 2.0 > static_cast<double>(glyph_run_maximum_extent), "the line is wider than the extent at scale 2");
    GlyphRunRasterizer rasterizer(engine);
    bool thrown = false;
    GlyphRunCoverage whole{};
    try {
        whole = rasterizer.rasterize(shaped, font, 2.0, 0);
    } catch (const std::exception&) {
        thrown = true;
    }
    require(!thrown, "an over-wide line does not throw");
    require(whole.oversized && whole.pixels.empty() && whole.width == 0U, "an over-wide line is reported and draws nothing");

    const GlyphRunWindow window{0, -200, 800, 200};
    const GlyphRunCoverage shown = rasterizer.rasterize(shaped, font, 2.0, 0, {}, window);
    require(!shown.oversized && !shown.pixels.empty(), "the visible part of the line draws");
    require(shown.outside_glyphs > 0U, "glyphs outside the window are left out");
    require(static_cast<std::int64_t>(shown.left) + static_cast<std::int64_t>(shown.width) <= window.right + 64,
            "the coverage ends near the window's right edge");
    require(static_cast<std::int64_t>(shown.width) < glyph_run_maximum_extent, "the coverage is bounded by the window");

    // The window's part equals the same glyphs rasterized without a window.
    const ShapedText head = engine.shape(corpus, font);
    const GlyphRunCoverage head_whole = rasterizer.rasterize(head, font, 2.0, 0);
    const GlyphRunCoverage head_windowed = rasterizer.rasterize(head, font, 2.0, 0, {}, GlyphRunWindow{-64, -200, 4096, 200});
    require(head_windowed.outside_glyphs == 0U && same_coverage(head_whole, head_windowed),
            "a window that holds the whole run changes nothing");

    const GlyphRunCoverage none = rasterizer.rasterize(shaped, font, 2.0, 0, {}, GlyphRunWindow{0, 5000, 800, 5100});
    require(none.pixels.empty() && !none.oversized && none.outside_glyphs > 0U, "a window below the line draws nothing");
}

} // namespace

int main() {
    origin_keeps_phase_and_snaps_baseline();
    rasterization_is_deterministic_and_scales(GUI_FORMS_TEST_RAPIDS_REGULAR);
    rasterization_is_deterministic_and_scales(GUI_FORMS_TEST_CARLITO_REGULAR);
    unknown_faces_are_reported();
    repeated_glyphs_match_single_glyph_runs(GUI_FORMS_TEST_RAPIDS_REGULAR);
    repeated_glyphs_match_single_glyph_runs(GUI_FORMS_TEST_CARLITO_REGULAR);
    glyphs_beyond_ten_ems_are_refused(GUI_FORMS_TEST_CARLITO_REGULAR);
    long_lines_fail_soft_and_window_limits_glyphs(GUI_FORMS_TEST_RAPIDS_REGULAR);
    synthesis_and_transforms(GUI_FORMS_TEST_RAPIDS_REGULAR, {0xa2199bda6d0e05e7ULL, 0xf3fdfff6b1ef2453ULL, 0x3509b246406a7aadULL,
                                                             0xeece270b10c7e4b9ULL, 0xb26a36159298eee6ULL});
    synthesis_and_transforms(GUI_FORMS_TEST_CARLITO_REGULAR, {0x740132e6ee72d463ULL, 0x21207356b2411a38ULL, 0x535dbda7fa2302b1ULL,
                                                              0xda491c0d10f163a9ULL, 0x2f4536e59ea96f93ULL});
    report_identity_hashes("rapids", GUI_FORMS_TEST_RAPIDS_REGULAR);
    report_identity_hashes("carlito", GUI_FORMS_TEST_CARLITO_REGULAR);
    return 0;
}
