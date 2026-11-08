// Stage L, phase L8a: what TextBlock reaches through the bundled text
// engine (PLAN section 12). The engine's UAX #14 break opportunities
// (libunibreak 8.0, admitted to the text engine by the owner on 2026-10-26)
// against the white-space estimate; the faces' unrounded design metrics;
// and the line boxes they give, which are Chromium's (measured against
// Chrome 155 headless: Carlito 15 px tall at 13 px and 24 px at 19.5 px,
// Portsmouth Rapids 16 and 25, with their baselines at the rounded ascent).

#include "gui_forms/basic_controls.hpp"
#include "gui_forms/window.hpp"
#include "harfbuzz_font_engine.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace gui_forms;
using render::text::HarfBuzzFontEngine;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void require_near(double actual, double expected, double tolerance, const std::string& what) {
    require(std::abs(actual - expected) <= tolerance,
            what + ": got " + std::to_string(actual) + ", expected " + std::to_string(expected));
}

std::vector<std::byte> read_face(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    require(static_cast<bool>(input), "bundled face opens: " + path.string());
    const std::streamsize size = input.tellg();
    require(size > 0 && size <= 16 * 1024 * 1024, "bundled face size admitted");
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()), size);
    require(static_cast<bool>(input), "bundled face reads");
    return bytes;
}

// The window's provider over the engine, as the hosts' painters are.
class EngineMetrics final : public TextMetricsProvider {
public:
    explicit EngineMetrics(HarfBuzzFontEngine& engine) : engine_(engine) {}

    ResolvedTextLayout resolve_text_layout_utf8(std::string_view utf8, FontSpec font) override {
        const ResolvedTextLayout layout = engine_.resolve(utf8, font);
        return layout;
    }

    TextResolutionStatus fill_line_break_opportunities_utf8(
        std::string_view utf8, std::span<LineBreakOpportunity> opportunities) override {
        const TextResolutionStatus status =
            HarfBuzzFontEngine::fill_line_break_opportunities_utf8(utf8, opportunities);
        return status;
    }

private:
    HarfBuzzFontEngine& engine_;
};

std::vector<LineBreakOpportunity> engine_breaks(std::string_view text) {
    std::vector<LineBreakOpportunity> opportunities(text.size(), LineBreakOpportunity::prohibited);
    const TextResolutionStatus status = HarfBuzzFontEngine::fill_line_break_opportunities_utf8(text, opportunities);
    require(status == TextResolutionStatus::exact, "the engine's breaks are exact");
    return opportunities;
}

std::vector<LineBreakOpportunity> estimated_breaks(std::string_view text) {
    std::vector<LineBreakOpportunity> opportunities(text.size(), LineBreakOpportunity::prohibited);
    const TextResolutionStatus status = estimate_line_break_opportunities_utf8(text, opportunities);
    require(status == TextResolutionStatus::estimated, "the white-space breaks are estimated");
    return opportunities;
}

void test_break_opportunities() {
    const std::vector<LineBreakOpportunity> hyphen = engine_breaks("well-known");
    require(hyphen[4] == LineBreakOpportunity::allowed, "UAX #14 allows a break after a hyphen");
    require(hyphen[3] == LineBreakOpportunity::prohibited, "and not inside the word");
    require(hyphen[9] == LineBreakOpportunity::mandatory, "the text's end is mandatory");
    require(estimated_breaks("well-known")[4] == LineBreakOpportunity::prohibited,
            "the estimate breaks at white space only");

    const std::vector<LineBreakOpportunity> spaces = engine_breaks("a  b");
    require(spaces[1] == LineBreakOpportunity::prohibited && spaces[2] == LineBreakOpportunity::allowed,
            "a break follows the last space of a run");
    const std::vector<LineBreakOpportunity> estimated_spaces = estimated_breaks("a  b");
    require(estimated_spaces[1] == LineBreakOpportunity::prohibited &&
                estimated_spaces[2] == LineBreakOpportunity::allowed,
            "as the estimate's does");

    // Three ideographs of three bytes each: a break between each pair.
    const std::string ideographs = "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e";
    const std::vector<LineBreakOpportunity> cjk = engine_breaks(ideographs);
    require(cjk[0] == LineBreakOpportunity::inside_character && cjk[1] == LineBreakOpportunity::inside_character,
            "a scalar's leading bytes are inside the character");
    require(cjk[2] == LineBreakOpportunity::allowed && cjk[5] == LineBreakOpportunity::allowed,
            "ideographs break between each other");
    require(estimated_breaks(ideographs)[2] == LineBreakOpportunity::prohibited,
            "the estimate does not break ideographs");

    const std::vector<LineBreakOpportunity> line_feed = engine_breaks("a\nb");
    require(line_feed[1] == LineBreakOpportunity::mandatory, "a line feed ends its line");
    require(estimated_breaks("a\nb")[1] == LineBreakOpportunity::mandatory, "in the estimate too");

    std::array<LineBreakOpportunity, 2> short_storage{LineBreakOpportunity::allowed, LineBreakOpportunity::allowed};
    require(HarfBuzzFontEngine::fill_line_break_opportunities_utf8("abc", short_storage) ==
                TextResolutionStatus::invalid_request,
            "storage of the wrong size is refused");
    require(short_storage[0] == LineBreakOpportunity::allowed, "and left unchanged");
    std::array<LineBreakOpportunity, 2> invalid_storage{LineBreakOpportunity::allowed, LineBreakOpportunity::allowed};
    require(HarfBuzzFontEngine::fill_line_break_opportunities_utf8("\xc3\x28", invalid_storage) ==
                TextResolutionStatus::invalid_request,
            "invalid UTF-8 is refused");
    require(estimate_line_break_opportunities_utf8("\xc3\x28", invalid_storage) ==
                TextResolutionStatus::invalid_request,
            "by the estimate too");
    require(HarfBuzzFontEngine::fill_line_break_opportunities_utf8("", std::span<LineBreakOpportunity>{}) ==
                TextResolutionStatus::exact,
            "an empty text has nothing to fill");
}

FontSpec content(double size) {
    const FontSpec font{FontRole::content, size, 400, false, 0.0};
    return font;
}

void require_line_box(HarfBuzzFontEngine& engine, double size, double height, double baseline,
                      const std::string& face) {
    const ResolvedTextLayout primary = engine.resolve(" ", content(size));
    require(primary.design_metrics.has_value(), face + " resolves its design metrics");
    const LineBoxMetrics line = line_box_metrics(primary, std::nullopt);
    require_near(line.height, height, 0.0, face + " " + std::to_string(size) + " px line-height: normal");
    require_near(line.baseline, baseline, 0.0, face + " " + std::to_string(size) + " px baseline");
}

void test_design_metrics_and_line_boxes(const std::filesystem::path& fonts) {
    const std::vector<std::byte> carlito_bytes = read_face(fonts / "Carlito-Regular.ttf");
    HarfBuzzFontEngine carlito{};
    require(carlito.register_typeface(FontRole::content, 400, false, carlito_bytes).has_value(),
            "Carlito registers");
    const ResolvedTextLayout resolved = carlito.resolve("Settings", content(13.0));
    const FontVerticalMetrics design = resolved.design_metrics.value();
    require_near(design.ascent, 1950.0 * 13.0 / 2048.0, 1.0e-9, "Carlito's hhea ascender at 13 px");
    require_near(design.descent, 550.0 * 13.0 / 2048.0, 1.0e-9, "its descender");
    require_near(design.line_gap, 0.0, 1.0e-9, "and no line gap");
    require(design.x_height > 0.0, "its x-height is recorded");
    require(resolved.ascent + resolved.descent > 15.0,
            "the size metrics round outward (13 + 4), so the design metrics are needed");
    require_line_box(carlito, 13.0, 15.0, 12.0, "Carlito");
    require_line_box(carlito, 19.5, 24.0, 19.0, "Carlito");
    require_line_box(carlito, 12.0, 14.0, 11.0, "Carlito");
    require_line_box(carlito, 14.0, 17.0, 13.0, "Carlito");

    const std::vector<std::byte> rapids_bytes = read_face(fonts / "PortsmouthRapids.ttf");
    HarfBuzzFontEngine rapids{};
    require(rapids.register_typeface(FontRole::content, 400, false, rapids_bytes).has_value(),
            "Portsmouth Rapids registers");
    require_line_box(rapids, 13.0, 16.0, 12.0, "Portsmouth Rapids");
    require_line_box(rapids, 19.5, 25.0, 19.0, "Portsmouth Rapids");
    require_line_box(rapids, 12.0, 15.0, 11.0, "Portsmouth Rapids");

    // A TextBlock over the engine: one line of "Settings" is Chromium's 15
    // px line box, and as wide as the engine shapes it.
    EngineMetrics metrics(carlito);
    std::shared_ptr<TextBlock> block = make_control<TextBlock>(StableId("engine.text"), "Settings");
    (*block).font = content(13.0);
    const std::unique_ptr<Window> held = std::make_unique<Window>(block, Size{400.0, 300.0});
    (*held).set_text_metrics_provider(&metrics);
    const LayoutConstraint widest{max_content_extent(), max_content_extent()};
    const Size measured = (*block).measure_for_layout(widest);
    require_near(measured.width, resolved.logical_size.width, 0.0, "the block is as wide as its shaped line");
    require_near(measured.height, 15.0, 0.0, "and one line box tall");
    require_near((*block).first_baseline(measured.width).value(), 12.0, 0.0, "its baseline is Chromium's");
}

} // namespace

int main(int argument_count, char** arguments) {
    try {
        require(argument_count == 2, "usage: gui_forms_text_block_engine_tests <assets/fonts>");
        test_break_opportunities();
        test_design_metrics_and_line_boxes(std::filesystem::path(arguments[1]));
        std::cout << "text-block-engine-tests: pass\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "text-block-engine-tests: fail: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
