// Stage L, phase L8a: TextBlock, the CSS text leaf (PLAN section 12).
// Wrapping at the provider's break opportunities, white-space collapsing,
// the intrinsic sizes, `line-height: normal` and a set line height, the
// first and last baselines, the text scale, `text-align` in both
// directions, paint at the baselines, the property table, and the three
// containers aligning by its baselines (flex and grid by the first, an
// inline item in BlockPanel by the last), with BlockPanel's strut from its
// font. The metrics provider is a fixed one with Carlito's design
// proportions, so every expected value is exact arithmetic; the bundled
// engine's own numbers are in text_block_engine_tests.cpp.

#include "gui_forms/basic_controls.hpp"
#include "gui_forms/container_controls.hpp"
#include "gui_forms/window.hpp"

#include <cmath>
#include <cstdlib>
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

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

bool near(double left, double right) {
    return std::abs(left - right) <= 1.0e-9;
}

void require_near(double actual, double expected, const std::string& what) {
    require(near(actual, expected), what + ": got " + std::to_string(actual) + ", expected " +
                                        std::to_string(expected));
}

// Seven logical px per byte at 13 px, scaling with the size; Carlito's
// ascender and descender (1950 and 550 of 2048 units) and no line gap. At
// 13 px the line box is round(12.38) + round(3.49) = 15 with its baseline at
// 12; at 19.5 px it is 19 + 5 = 24 with its baseline at 19.
class CarlitoProportions : public TextMetricsProvider {
public:
    ResolvedTextLayout resolve_text_layout_utf8(std::string_view utf8, FontSpec font) override {
        ++resolutions;
        ResolvedTextLayout layout{};
        layout.effective_font = font;
        const double advance = 7.0 * font.size / 13.0;
        layout.logical_size = Size{advance * static_cast<double>(utf8.size()), font.size * 1.25};
        FontVerticalMetrics design{};
        design.ascent = 1950.0 / 2048.0 * font.size;
        design.descent = 550.0 / 2048.0 * font.size;
        design.x_height = 0.5 * font.size;
        layout.design_metrics = design;
        layout.ascent = std::ceil(design.ascent);
        layout.descent = std::ceil(design.descent);
        layout.status = TextResolutionStatus::exact;
        return layout;
    }

    std::size_t resolutions{};
};

// The same, with UAX #14's break after a hyphen, which the white-space
// estimate does not give.
class HyphenBreaks final : public CarlitoProportions {
public:
    TextResolutionStatus fill_line_break_opportunities_utf8(
        std::string_view utf8, std::span<LineBreakOpportunity> opportunities) override {
        const TextResolutionStatus estimated = estimate_line_break_opportunities_utf8(utf8, opportunities);
        if (estimated == TextResolutionStatus::invalid_request) return estimated;
        for (std::size_t index = 0U; index + 1U < utf8.size(); ++index) {
            if (utf8[index] == '-') opportunities[index] = LineBreakOpportunity::allowed;
        }
        return TextResolutionStatus::exact;
    }
};

struct TextChangedRecorder final {
    std::string& last;
    std::size_t& count;
    void operator()(const std::string& text) const {
        last = text;
        ++count;
    }
};

class TextPainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point origin, std::string_view text, FontSpec, Color) override {
        texts.emplace_back(text);
        origins.push_back(origin);
    }
    void draw_image(ImageId, Rect, double) override {}

    std::vector<std::string> texts{};
    std::vector<Point> origins{};
};

FontSpec carlito(double size) {
    const FontSpec font{FontRole::content, size, 400, false, 0.0};
    return font;
}

std::shared_ptr<TextBlock> text_block(const std::string& id, const std::string& text) {
    std::shared_ptr<TextBlock> made = make_control<TextBlock>(StableId(id), text);
    (*made).font = carlito(13.0);
    (*made).margin = {0.0, 0.0, 0.0, 0.0};
    return made;
}

Size measured_at(TextBlock& block, LayoutConstraint constraint) {
    const Size measured = block.measure_for_layout(constraint);
    return measured;
}

LayoutConstraint width_query(double width) {
    const LayoutConstraint constraint{definite_extent(LogicalLength{width}), max_content_extent()};
    return constraint;
}

// "aaa bbb cc" at 7 px a byte: the runs are 21, 49 and 70 px wide.
void test_wrapping_and_line_height() {
    CarlitoProportions metrics{};
    std::shared_ptr<TextBlock> block = text_block("text.wrap", "aaa bbb cc");
    const std::unique_ptr<Window> held = std::make_unique<Window>(block, Size{800.0, 600.0});
    (*held).set_text_metrics_provider(&metrics);
    const Size wide = measured_at(*block, width_query(200.0));
    require_near(wide.width, 200.0, "a definite width is reported exactly");
    require_near(wide.height, 15.0, "one line is line-height: normal tall");
    const Size two = measured_at(*block, width_query(50.0));
    require_near(two.height, 30.0, "\"aaa bbb\" fits 50 px and \"cc\" wraps");
    const Size three = measured_at(*block, width_query(40.0));
    require_near(three.height, 45.0, "at 40 px each word takes a line");
    const Size exact = measured_at(*block, width_query(49.0));
    require_near(exact.height, 30.0, "a run exactly as wide as the line fits it");

    (*block).text = "aaaaa bb";
    const Size overflow = measured_at(*block, width_query(20.0));
    require_near(overflow.height, 30.0, "a word wider than the line overflows on a line of its own");

    (*block).line_height = 20.0;
    require_near(measured_at(*block, width_query(20.0)).height, 40.0, "a set line height is each line's height");
    (*block).line_height = std::nullopt;
    require_near(measured_at(*block, width_query(20.0)).height, 30.0, "clearing it resumes normal");
}

void test_white_space_collapses() {
    CarlitoProportions metrics{};
    std::shared_ptr<TextBlock> block = text_block("text.collapse", "  aaa \n\t  bbb  ");
    const std::unique_ptr<Window> held = std::make_unique<Window>(block, Size{800.0, 600.0});
    (*held).set_text_metrics_provider(&metrics);
    const LayoutConstraint widest{max_content_extent(), max_content_extent()};
    const Size measured = measured_at(*block, widest);
    require_near(measured.width, 49.0, "leading and trailing white space goes, a run becomes one space");
    require_near(measured.height, 15.0, "a line feed is white space, not a break");
    (*block).text = " \t\n ";
    const Size empty = measured_at(*block, widest);
    require_near(empty.width, 0.0, "white space only has no width");
    require_near(empty.height, 0.0, "and no line box");
    require(!(*block).first_baseline(100.0).has_value(), "and no baseline");
}

void test_intrinsic_sizes_and_padding() {
    CarlitoProportions metrics{};
    std::shared_ptr<TextBlock> block = text_block("text.intrinsic", "aaa bbbbb cc");
    (*block).padding = {2.0, 3.0, 4.0, 5.0};
    const std::unique_ptr<Window> held = std::make_unique<Window>(block, Size{800.0, 600.0});
    (*held).set_text_metrics_provider(&metrics);
    const Size minimum = measured_at(*block, LayoutConstraint{min_content_extent(), max_content_extent()});
    require_near(minimum.width, 35.0 + 6.0, "min-content is the widest word and the padding");
    require_near(minimum.height, 45.0 + 8.0, "each word on its own line");
    const Size maximum = measured_at(*block, LayoutConstraint{max_content_extent(), max_content_extent()});
    require_near(maximum.width, 84.0 + 6.0, "max-content is the whole text on one line");
    require_near(maximum.height, 15.0 + 8.0, "one line");
    const Size narrowed = measured_at(*block, width_query(minimum.width));
    require_near(narrowed.height, 45.0 + 8.0, "handed back its min-content width it keeps every word whole");
    require_near((*block).first_baseline(100.0).value(), 3.0 + 12.0, "the first baseline is below the top padding");
    require_near((*block).last_baseline(minimum.width).value(), 3.0 + 30.0 + 12.0,
                 "the last baseline is on the third line");
}

void test_text_scale() {
    CarlitoProportions metrics{};
    std::shared_ptr<TextBlock> block = text_block("text.scale", "aaa bbb");
    (*block).line_height = 20.0;
    const std::unique_ptr<Window> held = std::make_unique<Window>(block, Size{800.0, 600.0});
    (*held).set_text_metrics_provider(&metrics);
    (*held).set_text_scale(1.5);
    const Size measured = measured_at(*block, LayoutConstraint{max_content_extent(), max_content_extent()});
    require_near(measured.width, 73.5, "the font scales: 10.5 px a byte");
    require_near(measured.height, 30.0, "a set line height scales with it");
    (*block).line_height = std::nullopt;
    require_near(measured_at(*block, width_query(200.0)).height, 24.0, "19.5 px's line box is 19 + 5");
    require_near((*block).first_baseline(200.0).value(), 19.0, "its baseline is its rounded ascent");
}

// A set line height's leading goes half above the text, rounded down.
void test_half_leading() {
    CarlitoProportions metrics{};
    std::shared_ptr<TextBlock> block = text_block("text.leading", "aaa");
    const std::unique_ptr<Window> held = std::make_unique<Window>(block, Size{800.0, 600.0});
    (*held).set_text_metrics_provider(&metrics);
    (*block).line_height = 20.0;
    require_near((*block).first_baseline(100.0).value(), 14.0, "5 px of leading puts 2 above");
    (*block).line_height = 21.0;
    require_near((*block).first_baseline(100.0).value(), 15.0, "6 px puts 3 above");
    (*block).line_height = 12.0;
    require_near((*block).first_baseline(100.0).value(), 10.0, "-3 px puts -2 above");
}

void test_breaks_come_from_the_provider() {
    CarlitoProportions estimate{};
    HyphenBreaks hyphens{};
    std::shared_ptr<TextBlock> block = text_block("text.breaks", "well-known");
    const std::unique_ptr<Window> held = std::make_unique<Window>(block, Size{800.0, 600.0});
    (*held).set_text_metrics_provider(&estimate);
    require_near(measured_at(*block, width_query(40.0)).height, 15.0, "white space alone does not break a hyphen");
    (*held).set_text_metrics_provider(&hyphens);
    require_near(measured_at(*block, width_query(40.0)).height, 30.0, "the provider's opportunity after the hyphen");
    const Size minimum = measured_at(*block, LayoutConstraint{min_content_extent(), max_content_extent()});
    require_near(minimum.width, 35.0, "min-content is the widest run between opportunities, \"well-\"");
}

void test_alignment_and_paint() {
    CarlitoProportions metrics{};
    std::shared_ptr<TextBlock> block = text_block("text.align", "aaa bbb cc");
    (*block).padding = {1.0, 2.0, 0.0, 0.0};
    (*block).set_bounds({0.0, 0.0, 51.0, 32.0});
    const std::unique_ptr<Window> held = std::make_unique<Window>(block, Size{51.0, 32.0});
    (*held).set_text_metrics_provider(&metrics);
    (*held).perform_layout();
    TextPainter start{};
    (*block).on_paint(start, Rect{0.0, 0.0, 51.0, 32.0});
    require(start.texts.size() == 2U && start.texts[0] == "aaa bbb" && start.texts[1] == "cc",
            "the lines are \"aaa bbb\" and \"cc\", trailing space excluded");
    require_near(start.origins[0].x, 1.0, "start is left in left to right");
    require_near(start.origins[0].y, 2.0 + 12.0, "the first line's baseline");
    require_near(start.origins[1].y, 2.0 + 15.0 + 12.0, "the second line's baseline");
    (*block).layout_direction = LayoutDirection::right_to_left;
    TextPainter mirrored{};
    (*block).on_paint(mirrored, Rect{0.0, 0.0, 51.0, 32.0});
    require_near(mirrored.origins[1].x, 1.0 + 50.0 - 14.0, "start is right in right to left");
    (*block).text_align = TextAlignment::left;
    TextPainter left{};
    (*block).on_paint(left, Rect{0.0, 0.0, 51.0, 32.0});
    require_near(left.origins[1].x, 1.0, "left is physical");
    (*block).text_align = TextAlignment::center;
    TextPainter centred{};
    (*block).on_paint(centred, Rect{0.0, 0.0, 51.0, 32.0});
    require_near(centred.origins[1].x, 1.0 + 18.0, "center splits the free space");
    require(!(*block).hit_test_local(Point{5.0, 5.0}), "the text does not take pointer input");
    const TextBlock::Parts parts = (*block).parts();
    require(parts.text.present, "the text part is published");
}

void test_properties_and_validation() {
    std::shared_ptr<TextBlock> block = make_control<TextBlock>(StableId("text.properties"), "Hello");
    bool refused = false;
    try {
        (*block).line_height = 0.0;
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused, "a zero line height is refused");
    refused = false;
    try {
        (*block).text_align = static_cast<TextAlignment>(9);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused, "an unknown alignment is refused");
    require(!(*block).font.read().has_value(), "the font is the theme's until set");
    (*block).set_property_value("text_align", BindingValue{PropertyEnumValue{"text_alignment", "end", 1}});
    require((*block).text_align.read() == TextAlignment::end, "the text_align property sets the alignment");
    (*block).set_property_value("line_height", BindingValue{18.0});
    require((*block).line_height.read() == std::optional<double>{18.0}, "the line_height property sets it");
    (*block).set_property_value("line_height", BindingValue{0.0});
    require(!(*block).line_height.read().has_value(), "a zero line_height is normal");
    std::string changed;
    std::size_t changes = 0U;
    const SubscriptionToken token = (*block).text_changed().subscribe(TextChangedRecorder{changed, changes});
    (*block).text = "World";
    (*block).text = "World";
    require(changes == 1U && changed == "World", "text_changed fires once a change");
    const SemanticDescriptor semantics = (*block).semantic_descriptor();
    require(semantics.role == SemanticRole::static_text && semantics.name == "World",
            "it reads as static text named by its text");
}

// Flex and grid align TextBlocks by their first baselines: a 13 px block
// (baseline 12) beside a 19.5 px block (baseline 19) sits 7 px lower.
void test_containers_align_by_baselines() {
    CarlitoProportions metrics{};
    std::shared_ptr<FlexPanel> row = make_control<FlexPanel>(StableId("text.flex"));
    (*row).set_align_items(LayoutAlignment::baseline);
    std::shared_ptr<TextBlock> small = text_block("text.flex.small", "aaa");
    std::shared_ptr<TextBlock> large = text_block("text.flex.large", "bbb");
    (*large).font = carlito(19.5);
    (*row).add_child(small);
    (*row).add_child(large);
    const std::unique_ptr<Window> held = std::make_unique<Window>(row, Size{400.0, 300.0});
    (*held).set_text_metrics_provider(&metrics);
    (*held).perform_layout();
    require_near((*small).arranged_bounds().y, 7.0, "flex: the small block drops to the large one's baseline");
    require_near((*large).arranged_bounds().y, 0.0, "flex: the large block is at the line's top");

    std::shared_ptr<GridPanel> grid = make_control<GridPanel>(StableId("text.grid"));
    (*grid).set_align_items(LayoutAlignment::baseline);
    std::shared_ptr<TextBlock> left = text_block("text.grid.small", "aaa");
    std::shared_ptr<TextBlock> right = text_block("text.grid.large", "bbb");
    (*right).font = carlito(19.5);
    GridTrackList columns{};
    columns.tracks.push_back(GridTrackSize{});
    columns.tracks.push_back(GridTrackSize{});
    (*grid).set_template_columns(columns);
    (*grid).add_child(left);
    (*grid).add_child(right);
    const std::unique_ptr<Window> grid_window = std::make_unique<Window>(grid, Size{400.0, 300.0});
    (*grid_window).set_text_metrics_provider(&metrics);
    (*grid_window).perform_layout();
    require_near((*left).arranged_bounds().y, 7.0, "grid: the small block drops to the row's baseline");
    require_near((*right).arranged_bounds().y, 0.0, "grid: the large block is at the row's top");
}

// An inline TextBlock in a BlockPanel sits by its last baseline on a line
// that starts with the panel's strut, from its font when none is set.
void test_block_panel_strut_and_inline_baseline() {
    CarlitoProportions metrics{};
    std::shared_ptr<BlockPanel> flow = make_control<BlockPanel>(StableId("text.block"));
    (*flow).set_font(carlito(19.5));
    std::shared_ptr<TextBlock> words = text_block("text.block.words", "aaa bbb");
    (*flow).add_child(words);
    BlockFlowItemLayout inline_layout{};
    inline_layout.level = BlockFlowLevel::inline_level;
    (*flow).set_item_layout(*words, inline_layout);
    const std::unique_ptr<Window> held = std::make_unique<Window>(flow, Size{400.0, 300.0});
    (*held).set_text_metrics_provider(&metrics);
    const LineStrut strut = (*flow).used_line_strut();
    require_near(strut.ascent, 19.0, "the strut's ascent is the font's baseline");
    require_near(strut.descent, 5.0, "and its descent the rest of its line box");
    require_near(strut.x_height, 9.75, "and the face's x-height");
    (*held).perform_layout();
    require_near((*words).arranged_bounds().y, 19.0 - 12.0, "the block's baseline is on the strut's");
    require_near((*words).arranged_bounds().height, 15.0, "the block keeps its own line box");
    (*flow).set_line_strut(LineStrut{});
    (*held).perform_layout();
    require_near((*words).arranged_bounds().y, 0.0, "a zero strut leaves the line as tall as the item");
    (*flow).clear_line_strut();
    require(!(*flow).line_strut().has_value(), "clearing it resumes the font's");
}

} // namespace

int main() {
    try {
        test_wrapping_and_line_height();
        test_white_space_collapses();
        test_intrinsic_sizes_and_padding();
        test_text_scale();
        test_half_leading();
        test_breaks_come_from_the_provider();
        test_alignment_and_paint();
        test_properties_and_validation();
        test_containers_align_by_baselines();
        test_block_panel_strut_and_inline_baseline();
        std::cout << "text-block-tests: pass\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "text-block-tests: fail: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
