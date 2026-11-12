// Paragraph layout over the bundled text engine (HarfBuzz, FreeType,
// SheenBidi and libunibreak, as the hosts' painters use them): the engine's
// cluster advances sum to its shaped width; collapsed, verified layout
// paints the lines TextBlock paints for Latin, Hebrew, Arabic and emoji
// text, in both directions, at several widths; the estimated fit shapes
// once; and break_word never splits an emoji sequence the engine shapes as
// one cluster.

#include "gui_forms/basic_controls.hpp"
#include "gui_forms/text/paragraph_layout/paragraph_layout.hpp"
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
    require(size > 0 && size <= 32 * 1024 * 1024, "bundled face size admitted");
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()), size);
    require(static_cast<bool>(input), "bundled face reads");
    return bytes;
}

// The window's provider over the engine, as the hosts' painters are: the
// same resolution, break opportunities and cluster advances.
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

    TextResolutionStatus fill_cluster_advances_utf8(std::string_view utf8, FontSpec font,
                                                    std::span<double> advances) override {
        const TextResolutionStatus status = engine_.fill_cluster_advances_utf8(utf8, font, advances);
        return status;
    }

private:
    HarfBuzzFontEngine& engine_;
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

FontSpec content(double size) {
    const FontSpec font{FontRole::content, size, 400, false, 0.0};
    return font;
}

// The faces the engine tests register: Carlito for content, with the Noto
// Emoji, Hebrew and Arabic fallbacks. The byte vectors outlive the engine's
// copies' registration (span overloads copy).
void register_faces(HarfBuzzFontEngine& engine, const std::filesystem::path& fonts) {
    require(engine.register_typeface(FontRole::content, 400, false, read_face(fonts / "Carlito-Regular.ttf"))
                .has_value(),
            "Carlito registers");
    require(engine.register_fallback_typeface(400, false, read_face(fonts / "NotoEmoji-Regular.ttf")).has_value(),
            "Noto Emoji registers");
    require(engine.register_fallback_typeface(400, false, read_face(fonts / "NotoSansHebrew-Regular.ttf"))
                .has_value(),
            "Noto Sans Hebrew registers");
    require(engine.register_fallback_typeface(400, false, read_face(fonts / "NotoSansArabic-Regular.ttf"))
                .has_value(),
            "Noto Sans Arabic registers");
}

const std::string family = "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7";
const std::string thumbs = "\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD";
const std::string heart = "\xE2\x9D\xA4\xEF\xB8\x8F";

// The engine's advances sum to the width it shapes the whole text to.
void test_advances_sum_to_width(HarfBuzzFontEngine& engine) {
    const std::array<std::string, 4> texts{
        "The quick brown fox jumps over the lazy dog",
        "\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D ABC 123",
        "a " + family + " b " + thumbs + heart,
        "office affine waffle",
    };
    for (const std::string& text : texts) {
        std::vector<double> advances(text.size(), -1.0);
        const TextResolutionStatus status = engine.fill_cluster_advances_utf8(text, content(13.0), advances);
        require(status != TextResolutionStatus::invalid_request && status != TextResolutionStatus::missing_primary_face,
                text + ": the advances are shaped");
        double sum = 0.0;
        for (std::size_t index = 0U; index < advances.size(); ++index) {
            require(advances[index] >= 0.0, text + ": every byte is written");
            if ((static_cast<unsigned char>(text[index]) & 0xc0U) == 0x80U) {
                require(advances[index] == 0.0, text + ": a continuation byte begins no cluster");
            }
            sum += advances[index];
        }
        const ResolvedTextLayout resolved = engine.resolve(text, content(13.0));
        require_near(sum, resolved.logical_size.width, 1.0e-3, text + ": the advances sum to the shaped width");
    }
    std::vector<double> wrong(3U, 7.0);
    require(engine.fill_cluster_advances_utf8("abcd", content(13.0), wrong) == TextResolutionStatus::invalid_request,
            "storage of the wrong size is refused");
    require(wrong[0] == 7.0, "and left unchanged");
}

// Collapsed, verified layout paints TextBlock's lines over the engine.
void test_matches_text_block(HarfBuzzFontEngine& engine) {
    EngineMetrics metrics(engine);
    const std::array<std::string, 5> texts{
        "The quick brown fox jumps over the lazy dog, and then  the   dog sleeps under a tree.",
        "Settings: well-known values, pre-configured defaults and a supercalifragilisticexpialidocious name.",
        "\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D \xD7\xA2\xD7\x95\xD7\x9C\xD7\x9D ABC 123 \xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D "
        "\xD7\xA2\xD7\x95\xD7\x9C\xD7\x9D",
        "\xD8\xA7\xD9\x84\xD9\x84\xD9\x88\xD9\x86 123 (RGB) \xD8\xA7\xD9\x84\xD9\x84\xD9\x88\xD9\x86 "
        "\xD8\xA7\xD9\x84\xD9\x84\xD9\x88\xD9\x86",
        "Family " + family + " likes " + thumbs + " and " + heart + " in every message " + family + family,
    };
    const std::array<double, 3> widths{60.0, 120.0, 240.0};
    const std::array<LayoutDirection, 2> directions{LayoutDirection::left_to_right, LayoutDirection::right_to_left};
    for (const std::string& text : texts) {
        for (const double width : widths) {
            for (const LayoutDirection direction : directions) {
                const std::string what = text + " at " + std::to_string(width);
                std::shared_ptr<TextBlock> block = make_control<TextBlock>(StableId("engine.paragraph"), text);
                (*block).font = content(13.0);
                (*block).margin = {0.0, 0.0, 0.0, 0.0};
                (*block).padding = {0.0, 0.0, 0.0, 0.0};
                (*block).layout_direction = direction;
                (*block).set_bounds({0.0, 0.0, width, 800.0});
                const std::unique_ptr<Window> held = std::make_unique<Window>(block, Size{width, 800.0});
                (*held).set_text_metrics_provider(&metrics);
                (*held).perform_layout();
                TextPainter block_painter{};
                (*block).on_paint(block_painter, Rect{0.0, 0.0, width, 800.0});

                ParagraphStyle style{};
                style.font = (*block).text_font((*block).resolved_font());
                style.line_box = paragraph_line_box(&metrics, style.font, std::nullopt);
                style.width = width;
                style.white_space = ParagraphWhiteSpace::collapse;
                style.direction = direction == LayoutDirection::right_to_left ? TextDirection::right_to_left
                                                                              : TextDirection::left_to_right;
                ParagraphBuffers buffers{};
                buffers.reserve(paragraph_storage_bound(text, ParagraphWhiteSpace::collapse));
                const ParagraphLayoutResult result = lay_out_paragraph(&metrics, text, style, buffers.storage());
                require(result.succeeded(), what + ": lays out");
                const ParagraphMetrics laid_out = *result.metrics();
                require(laid_out.status != TextResolutionStatus::missing_primary_face, what + ": the primary face is used");
                TextPainter paragraph_painter{};
                ParagraphPaint paint{};
                paint.visible = Rect{0.0, 0.0, width, 800.0};
                const std::size_t drawn =
                    paint_paragraph(paragraph_painter, buffers.collapsed_text(laid_out.laid_out_bytes),
                                    buffers.lines(laid_out.line_count), style, paint);
                require(drawn == block_painter.texts.size(), what + ": TextBlock's line count " +
                                                                 std::to_string(block_painter.texts.size()));
                for (std::size_t index = 0U; index < drawn; ++index) {
                    const std::string line_what = what + " line " + std::to_string(index);
                    require(paragraph_painter.texts[index] == block_painter.texts[index],
                            line_what + ": TextBlock's text \"" + block_painter.texts[index] + "\", got \"" +
                                paragraph_painter.texts[index] + "\"");
                    require_near(paragraph_painter.origins[index].x, block_painter.origins[index].x, 1.0e-9,
                                 line_what + ": TextBlock's x");
                    require_near(paragraph_painter.origins[index].y, block_painter.origins[index].y, 1.0e-9,
                                 line_what + ": TextBlock's baseline");
                }
                require(laid_out.shaping_calls <= 1U + 3U * static_cast<std::uint64_t>(laid_out.line_count),
                        what + ": about two shapings a line");

                ParagraphStyle estimated = style;
                estimated.fit = ParagraphFit::estimated;
                ParagraphStorage no_lines = buffers.storage();
                no_lines.lines = std::span<ParagraphLine>{};
                const ParagraphLayoutResult quick = measure_paragraph(&metrics, text, estimated, no_lines);
                require(quick.succeeded() && (*quick.metrics()).shaping_calls == 1U,
                        what + ": the estimated measure shapes once");
            }
        }
    }
}

// break_word over the engine's clusters: an emoji word at a width of about
// one cluster breaks only between the sequences.
void test_break_word_keeps_emoji_sequences(HarfBuzzFontEngine& engine) {
    EngineMetrics metrics(engine);
    const std::string emoji = family + thumbs + heart + family;
    const std::array<std::size_t, 5> boundaries{0U, 18U, 26U, 32U, 50U};
    ParagraphStyle style{};
    style.font = content(13.0);
    style.line_box = paragraph_line_box(&metrics, style.font, std::nullopt);
    style.width = 1.0;
    style.overflow_wrap = ParagraphOverflowWrap::break_word;
    ParagraphBuffers buffers{};
    buffers.reserve(paragraph_storage_bound(emoji, ParagraphWhiteSpace::preserve_line_ends));
    const ParagraphLayoutResult result = lay_out_paragraph(&metrics, emoji, style, buffers.storage());
    require(result.succeeded(), "the emoji word lays out");
    const std::span<const ParagraphLine> lines = buffers.lines((*result.metrics()).line_count);
    require(lines.size() == 4U, "one sequence a line at 1 px: got " + std::to_string(lines.size()));
    for (std::size_t index = 0U; index < lines.size(); ++index) {
        require(lines[index].bytes.start.value() == boundaries[index] &&
                    lines[index].bytes.end.value() == boundaries[index + 1U],
                "line " + std::to_string(index) + " is one whole emoji sequence");
    }
}

} // namespace

int main(int argument_count, char** arguments) {
    try {
        require(argument_count == 2, "usage: gui_forms_paragraph_layout_engine_tests <assets/fonts>");
        const std::filesystem::path fonts(arguments[1]);
        HarfBuzzFontEngine engine{};
        register_faces(engine, fonts);
        test_advances_sum_to_width(engine);
        test_matches_text_block(engine);
        test_break_word_keeps_emoji_sequences(engine);
        std::cout << "paragraph-layout-engine-tests: pass\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "paragraph-layout-engine-tests: fail: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
